// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/riscv/regexp-macro-assembler-riscv.h"

#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/logging/log.h"
#include "src/objects/objects-inl.h"
#include "src/regexp/regexp-macro-assembler.h"
#include "src/regexp/regexp-stack.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/strings/unicode.h"

namespace v8 {
namespace internal {

/* clang-format off
 * This assembler uses the following register assignment convention
 * - s1 : Pointer to current InstructionStream object including heap object tag.
 * - s2 : Current position in input, as negative offset from end of string.
 *        Please notice that this is the byte offset, not the character offset!
 * - s5 : Currently loaded character. Must be loaded using
 *        LoadCurrentCharacter before using any of the dispatch methods.
 * - s6 : Points to tip of backtrack stack
 * - s8 : End of input (points to byte after last character in input).
 * - fp : Frame pointer. Used to access arguments, local variables and
 *        RegExp registers.
 * - sp : Points to tip of C stack.
 *
 * The remaining registers are free for computations.
 * Each call to a public method should retain this convention.
 *
 * The stack will have the following structure:
 *
 *                                                                              kStackFrameHeader
 *  --- sp when called ---
 *  - fp[72]  ra                  Return from RegExp code (ra).                  kReturnAddress
 *  - fp[64]  old-fp              Old fp, callee saved(s9).
 *  - fp[0..63]  s1..s11          Callee-saved registers fp..s11.
 *  --- frame pointer ----
 *  - fp[-8]  frame marker
 *  - fp[-16]  Isolate* isolate   (address of the current isolate)               kIsolate
 *  - fp[-24] direct_call        (1 = direct call from JS, 0 = from runtime)    kDirectCall
 *  - fp[-32] output_size (may fit multiple sets of matches)                    kNumOutputRegisters
 *  - fp[-40] int* output (int[num_saved_registers_], for output).              kRegisterOutput
 *  - fp[-48] end of input       (address of end of string).                    kInputEnd
 *  - fp[-56] start of input     (address of first character in string).        kInputStart
 *  - fp[-64] start index        (character index of start).                    kStartIndex
 *  - fp[-72] void* input_string (location of a handle containing the string).  kInputString
 *  - fp[-80] success counter    (only for global regexps to count matches).    kSuccessfulCaptures
 *  - fp[-88] Offset of location before start of input (effectively character   kStringStartMinusOne
 *            position -1). Used to initialize capture registers to a
 *            non-position.
 *  --------- The following output registers are 32-bit values. ---------
 *  - fp[-96] register 0         (Only positions must be stored in the first    kRegisterZero
 *  -         register 1          num_saved_registers_ registers)
 *  -         ...
 *  -         register num_registers-1
 *  --- sp ---
 *
 * The first num_saved_registers_ registers are initialized to point to
 * "character -1" in the string (i.e., char_size() bytes before the first
 * character of the string). The remaining registers start out as garbage.
 *
 * The data up to the return address must be placed there by the calling
 * code and the remaining arguments are passed in registers, e.g. by calling the
 * code entry as cast to a function with the signature:
 * int (*match)(String input_string,
 *              int start_index,
 *              Address start,
 *              Address end,
 *              int* output,
 *              int output_size,
 *              bool direct_call = false,
 *              Isolate* isolate,
 *              Address regexp);
 * The call is performed by NativeRegExpMacroAssembler::Execute()
 * (in regexp-macro-assembler.cc) via the GeneratedCode wrapper.
 *
 * clang-format on
 */

#define __ ACCESS_MASM(masm_)

RegExpMacroAssemblerRISCV::RegExpMacroAssemblerRISCV(Isolate* isolate,
                                                     Zone* zone, Mode mode,
                                                     int registers_to_save)
    : NativeRegExpMacroAssembler(isolate, zone),
      masm_(std::make_unique<MacroAssembler>(
          isolate, CodeObjectRequired::kYes,
          NewAssemblerBuffer(kInitialBufferSize))),
      no_root_array_scope_(masm_.get()),
      mode_(mode),
      num_registers_(registers_to_save),
      num_saved_registers_(registers_to_save),
      entry_label_(),
      start_label_(),
      success_label_(),
      backtrack_label_(),
      exit_label_(),
      internal_failure_label_() {
  DCHECK_EQ(0, registers_to_save % 2);
  __ jmp(&entry_label_);  // We'll write the entry code later.
  // If the code gets too big or corrupted, an internal exception will be
  // raised, and we will exit right away.
  __ bind(&internal_failure_label_);
  __ li(a0, Operand(FAILURE));
  __ Ret();
  __ bind(&start_label_);  // And then continue from here.
}

RegExpMacroAssemblerRISCV::~RegExpMacroAssemblerRISCV() {
  // Unuse labels in case we throw away the assembler without calling GetCode.
  entry_label_.Unuse();
  start_label_.Unuse();
  success_label_.Unuse();
  backtrack_label_.Unuse();
  exit_label_.Unuse();
  check_preempt_label_.Unuse();
  stack_overflow_label_.Unuse();
  internal_failure_label_.Unuse();
  fallback_label_.Unuse();
}

int RegExpMacroAssemblerRISCV::stack_limit_slack() {
  return RegExpStack::kStackLimitSlack;
}

void RegExpMacroAssemblerRISCV::AdvanceCurrentPosition(int by) {
  if (by != 0) {
    __ AddWord(current_input_offset(), current_input_offset(),
               Operand(by * char_size()));
  }
}

void RegExpMacroAssemblerRISCV::AdvanceRegister(int reg, int by) {
  DCHECK_LE(0, reg);
  DCHECK_GT(num_registers_, reg);
  if (by != 0) {
    __ LoadWord(a0, register_location(reg));
    __ AddWord(a0, a0, Operand(by));
    __ StoreWord(a0, register_location(reg));
  }
}

void RegExpMacroAssemblerRISCV::Backtrack() {
  CheckPreemption();
  if (has_backtrack_limit()) {
    Label next;
    __ LoadWord(a0, MemOperand(frame_pointer(), kBacktrackCountOffset));
    __ AddWord(a0, a0, Operand(1));
    __ StoreWord(a0, MemOperand(frame_pointer(), kBacktrackCountOffset));
    __ BranchShort(&next, ne, a0, Operand(backtrack_limit()));

    // Backtrack limit exceeded.
    if (can_fallback()) {
      __ jmp(&fallback_label_);
    } else {
      // Can't fallback, so we treat it as a failed match.
      Fail();
    }

    __ bind(&next);
  }
  // Pop Code offset from backtrack stack, add Code and jump to location.
  Pop(a0);
  __ AddWord(a0, a0, code_pointer());
  __ Jump(a0);
}

void RegExpMacroAssemblerRISCV::Bind(Label* label) { __ bind(label); }

void RegExpMacroAssemblerRISCV::CheckCharacter(uint32_t c, Label* on_equal) {
  BranchOrBacktrack(on_equal, eq, current_character(), Operand(c));
}

void RegExpMacroAssemblerRISCV::CheckCharacterGT(base::uc16 limit,
                                                 Label* on_greater) {
  BranchOrBacktrack(on_greater, gt, current_character(), Operand(limit));
}

void RegExpMacroAssemblerRISCV::CheckAtStart(int cp_offset,
                                             Label* on_at_start) {
  __ LoadWord(a1, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
  __ AddWord(a0, current_input_offset(),
             Operand(-char_size() + cp_offset * char_size()));
  BranchOrBacktrack(on_at_start, eq, a0, Operand(a1));
}

void RegExpMacroAssemblerRISCV::CheckNotAtStart(int cp_offset,
                                                Label* on_not_at_start) {
  __ LoadWord(a1, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
  __ AddWord(a0, current_input_offset(),
             Operand(-char_size() + cp_offset * char_size()));
  BranchOrBacktrack(on_not_at_start, ne, a0, Operand(a1));
}

void RegExpMacroAssemblerRISCV::CheckCharacterLT(base::uc16 limit,
                                                 Label* on_less) {
  BranchOrBacktrack(on_less, lt, current_character(), Operand(limit));
}

void RegExpMacroAssemblerRISCV::CheckGreedyLoop(Label* on_equal) {
  Label backtrack_non_equal;
  __ Lw(a0, MemOperand(backtrack_stackpointer(), 0));
  __ BranchShort(&backtrack_non_equal, ne, current_input_offset(), Operand(a0));
  __ AddWord(backtrack_stackpointer(), backtrack_stackpointer(),
             Operand(kIntSize));
  __ bind(&backtrack_non_equal);
  BranchOrBacktrack(on_equal, eq, current_input_offset(), Operand(a0));
}

void RegExpMacroAssemblerRISCV::CallIsCharacterInRangeArray(
    const ZoneList<CharacterRange>* ranges) {
  static const int kNumArguments = 3;
  __ PrepareCallCFunction(kNumArguments, a0);

  __ mv(a0, current_character());
  __ li(a1, Operand(GetOrAddRangeArray(ranges)));
  __ li(a2, Operand(ExternalReference::isolate_address(isolate())));

  {
    // We have a frame (set up in GetCode), but the assembler doesn't know.
    FrameScope scope(masm_.get(), StackFrame::MANUAL);
    CallCFunctionFromIrregexpCode(
        ExternalReference::re_is_character_in_range_array(), kNumArguments);
  }
  __ li(code_pointer(), Operand(masm_->CodeObject()));
}

bool RegExpMacroAssemblerRISCV::CheckCharacterInRangeArray(
    const ZoneList<CharacterRange>* ranges, Label* on_in_range) {
  CallIsCharacterInRangeArray(ranges);
  BranchOrBacktrack(on_in_range, ne, a0, Operand(zero_reg));
  return true;
}

bool RegExpMacroAssemblerRISCV::CheckCharacterNotInRangeArray(
    const ZoneList<CharacterRange>* ranges, Label* on_not_in_range) {
  CallIsCharacterInRangeArray(ranges);
  BranchOrBacktrack(on_not_in_range, eq, a0, Operand(zero_reg));
  return true;
}

void RegExpMacroAssemblerRISCV::CheckNotBackReferenceIgnoreCase(
    int start_reg, bool read_backward, bool unicode, Label* on_no_match) {
  Label fallthrough;
  __ LoadWord(a0, register_location(start_reg));  // Index of start of capture.
  __ LoadWord(a1,
              register_location(start_reg + 1));  // Index of end of capture.
  __ SubWord(a1, a1, a0);                         // Length of capture.

  // At this point, the capture registers are either both set or both cleared.
  // If the capture length is zero, then the capture is either empty or cleared.
  // Fall through in both cases.
  __ BranchShort(&fallthrough, eq, a1, Operand(zero_reg));

  if (read_backward) {
    __ LoadWord(t1, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
    __ AddWord(t1, t1, a1);
    BranchOrBacktrack(on_no_match, le, current_input_offset(), Operand(t1));
  } else {
    __ AddWord(t1, a1, current_input_offset());
    // Check that there are enough characters left in the input.
    BranchOrBacktrack(on_no_match, gt, t1, Operand(zero_reg));
  }

  if (mode_ == LATIN1) {
    Label success;
    Label fail;
    Label loop_check;

    // a0 - offset of start of capture.
    // a1 - length of capture.
    __ AddWord(a0, a0, Operand(end_of_input_address()));
    __ AddWord(a2, end_of_input_address(), Operand(current_input_offset()));
    if (read_backward) {
      __ SubWord(a2, a2, Operand(a1));
    }
    __ AddWord(a1, a0, Operand(a1));

    // a0 - Address of start of capture.
    // a1 - Address of end of capture.
    // a2 - Address of current input position.

    Label loop;
    __ bind(&loop);
    __ Lbu(a3, MemOperand(a0, 0));
    __ addi(a0, a0, char_size());
    __ Lbu(a4, MemOperand(a2, 0));
    __ addi(a2, a2, char_size());

    __ BranchShort(&loop_check, eq, a4, Operand(a3));

    // Mismatch, try case-insensitive match (converting letters to lower-case).
    __ Or(a3, a3, Operand(0x20));  // Convert capture character to lower-case.
    __ Or(a4, a4, Operand(0x20));  // Also convert input character.
    __ BranchShort(&fail, ne, a4, Operand(a3));
    __ SubWord(a3, a3, Operand('a'));
    __ BranchShort(&loop_check, Uless_equal, a3, Operand('z' - 'a'));
    // Latin-1: Check for values in range [224,254] but not 247.
    __ SubWord(a3, a3, Operand(224 - 'a'));
    // Weren't Latin-1 letters.
    __ BranchShort(&fail, Ugreater, a3, Operand(254 - 224));
    // Check for 247.
    __ BranchShort(&fail, eq, a3, Operand(247 - 224));

    __ bind(&loop_check);
    __ Branch(&loop, lt, a0, Operand(a1));
    __ jmp(&success);

    __ bind(&fail);
    GoTo(on_no_match);

    __ bind(&success);
    // Compute new value of character position after the matched part.
    __ SubWord(current_input_offset(), a2, end_of_input_address());
    if (read_backward) {
      __ LoadWord(t1,
                  register_location(start_reg));  // Index of start of capture.
      __ LoadWord(
          a2, register_location(start_reg + 1));  // Index of end of capture.
      __ AddWord(current_input_offset(), current_input_offset(), Operand(t1));
      __ SubWord(current_input_offset(), current_input_offset(), Operand(a2));
    }
  } else {
    DCHECK(mode_ == UC16);

    int argument_count = 4;
    __ PrepareCallCFunction(argument_count, a2);

    // a0 - offset of start of capture.
    // a1 - length of capture.

    // Put arguments into arguments registers.
    // Parameters are
    //   a0: Address byte_offset1 - Address captured substring's start.
    //   a1: Address byte_offset2 - Address of current character position.
    //   a2: size_t byte_length - length of capture in bytes(!).
    //   a3: Isolate* isolate.

    // Address of start of capture.
    __ AddWord(a0, a0, Operand(end_of_input_address()));
    // Length of capture.
    __ mv(a2, a1);
    // Save length in callee-save register for use on return.
    __ mv(s3, a1);
    // Address of current input position.
    __ AddWord(a1, current_input_offset(), Operand(end_of_input_address()));
    if (read_backward) {
      __ SubWord(a1, a1, Operand(s3));
    }
    // Isolate.
    __ li(a3, Operand(ExternalReference::isolate_address(masm_->isolate())));

    {
      AllowExternalCallThatCantCauseGC scope(masm_.get());
      ExternalReference function =
          unicode
              ? ExternalReference::re_case_insensitive_compare_unicode()
              : ExternalReference::re_case_insensitive_compare_non_unicode();
      CallCFunctionFromIrregexpCode(function, argument_count);
    }

    // Check if function returned non-zero for success or zero for failure.
    BranchOrBacktrack(on_no_match, eq, a0, Operand(zero_reg));
    // On success, increment position by length of capture.
    if (read_backward) {
      __ SubWord(current_input_offset(), current_input_offset(), Operand(s3));
    } else {
      __ AddWord(current_input_offset(), current_input_offset(), Operand(s3));
    }
  }

  __ bind(&fallthrough);
}

void RegExpMacroAssemblerRISCV::CheckNotBackReference(int start_reg,
                                                      bool read_backward,
                                                      Label* on_no_match) {
  Label fallthrough;

  // Find length of back-referenced capture.
  __ LoadWord(a0, register_location(start_reg));
  __ LoadWord(a1, register_location(start_reg + 1));
  __ SubWord(a1, a1, a0);  // Length to check.

  // At this point, the capture registers are either both set or both cleared.
  // If the capture length is zero, then the capture is either empty or cleared.
  // Fall through in both cases.
  __ BranchShort(&fallthrough, eq, a1, Operand(zero_reg));

  if (read_backward) {
    __ LoadWord(t1, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
    __ AddWord(t1, t1, a1);
    BranchOrBacktrack(on_no_match, le, current_input_offset(), Operand(t1));
  } else {
    __ AddWord(t1, a1, current_input_offset());
    // Check that there are enough characters left in the input.
    BranchOrBacktrack(on_no_match, gt, t1, Operand(zero_reg));
  }

  // Compute pointers to match string and capture string.
  __ AddWord(a0, a0, Operand(end_of_input_address()));
  __ AddWord(a2, end_of_input_address(), Operand(current_input_offset()));
  if (read_backward) {
    __ SubWord(a2, a2, Operand(a1));
  }
  __ AddWord(a1, a1, Operand(a0));

  Label loop;
  __ bind(&loop);
  if (mode_ == LATIN1) {
    __ Lbu(a3, MemOperand(a0, 0));
    __ addi(a0, a0, char_size());
    __ Lbu(a4, MemOperand(a2, 0));
    __ addi(a2, a2, char_size());
  } else {
    DCHECK(mode_ == UC16);
    __ Lhu(a3, MemOperand(a0, 0));
    __ addi(a0, a0, char_size());
    __ Lhu(a4, MemOperand(a2, 0));
    __ addi(a2, a2, char_size());
  }
  BranchOrBacktrack(on_no_match, ne, a3, Operand(a4));
  __ Branch(&loop, lt, a0, Operand(a1));

  // Move current character position to position after match.
  __ SubWord(current_input_offset(), a2, end_of_input_address());
  if (read_backward) {
    __ LoadWord(t1,
                register_location(start_reg));  // Index of start of capture.
    __ LoadWord(a2,
                register_location(start_reg + 1));  // Index of end of capture.
    __ AddWord(current_input_offset(), current_input_offset(), Operand(t1));
    __ SubWord(current_input_offset(), current_input_offset(), Operand(a2));
  }
  __ bind(&fallthrough);
}

void RegExpMacroAssemblerRISCV::CheckNotCharacter(uint32_t c,
                                                  Label* on_not_equal) {
  BranchOrBacktrack(on_not_equal, ne, current_character(), Operand(c));
}

void RegExpMacroAssemblerRISCV::CheckCharacterAfterAnd(uint32_t c,
                                                       uint32_t mask,
                                                       Label* on_equal) {
  __ And(a0, current_character(), Operand(mask));
  Operand rhs = (c == 0) ? Operand(zero_reg) : Operand(c);
  BranchOrBacktrack(on_equal, eq, a0, rhs);
}

void RegExpMacroAssemblerRISCV::CheckNotCharacterAfterAnd(uint32_t c,
                                                          uint32_t mask,
                                                          Label* on_not_equal) {
  __ And(a0, current_character(), Operand(mask));
  Operand rhs = (c == 0) ? Operand(zero_reg) : Operand(c);
  BranchOrBacktrack(on_not_equal, ne, a0, rhs);
}

void RegExpMacroAssemblerRISCV::CheckNotCharacterAfterMinusAnd(
    base::uc16 c, base::uc16 minus, base::uc16 mask, Label* on_not_equal) {
  DCHECK_GT(String::kMaxUtf16CodeUnit, minus);
  __ SubWord(a0, current_character(), Operand(minus));
  __ And(a0, a0, Operand(mask));
  BranchOrBacktrack(on_not_equal, ne, a0, Operand(c));
}

void RegExpMacroAssemblerRISCV::CheckCharacterInRange(base::uc16 from,
                                                      base::uc16 to,
                                                      Label* on_in_range) {
  __ SubWord(a0, current_character(), Operand(from));
  // Unsigned lower-or-same condition.
  BranchOrBacktrack(on_in_range, Uless_equal, a0, Operand(to - from));
}

void RegExpMacroAssemblerRISCV::CheckCharacterNotInRange(
    base::uc16 from, base::uc16 to, Label* on_not_in_range) {
  __ SubWord(a0, current_character(), Operand(from));
  // Unsigned higher condition.
  BranchOrBacktrack(on_not_in_range, Ugreater, a0, Operand(to - from));
}

void RegExpMacroAssemblerRISCV::CheckBitInTable(Handle<ByteArray> table,
                                                Label* on_bit_set) {
  __ li(a0, Operand(table));
  if (mode_ != LATIN1 || kTableMask != String::kMaxOneByteCharCode) {
    __ And(a1, current_character(), Operand(kTableSize - 1));
    __ AddWord(a0, a0, a1);
  } else {
    __ AddWord(a0, a0, current_character());
  }

  __ Lbu(a0, FieldMemOperand(a0, ByteArray::kHeaderSize));
  BranchOrBacktrack(on_bit_set, ne, a0, Operand(zero_reg));
}

bool RegExpMacroAssemblerRISCV::CheckSpecialClassRanges(
    StandardCharacterSet type, Label* on_no_match) {
  // Range checks (c in min..max) are generally implemented by an unsigned
  // (c - min) <= (max - min) check.
  switch (type) {
    case StandardCharacterSet::kWhitespace:
      // Match space-characters.
      if (mode_ == LATIN1) {
        // One byte space characters are '\t'..'\r', ' ' and \u00a0.
        Label success;
        __ BranchShort(&success, eq, current_character(), Operand(' '));
        // Check range 0x09..0x0D.
        __ SubWord(a0, current_character(), Operand('\t'));
        __ BranchShort(&success, Uless_equal, a0, Operand('\r' - '\t'));
        // \u00a0 (NBSP).
        BranchOrBacktrack(on_no_match, ne, a0, Operand(0x00A0 - '\t'));
        __ bind(&success);
        return true;
      }
      return false;
    case StandardCharacterSet::kNotWhitespace:
      // The emitted code for generic character classes is good enough.
      return false;
    case StandardCharacterSet::kDigit:
      // Match Latin1 digits ('0'..'9').
      __ SubWord(a0, current_character(), Operand('0'));
      BranchOrBacktrack(on_no_match, Ugreater, a0, Operand('9' - '0'));
      return true;
    case StandardCharacterSet::kNotDigit:
      // Match non Latin1-digits.
      __ SubWord(a0, current_character(), Operand('0'));
      BranchOrBacktrack(on_no_match, Uless_equal, a0, Operand('9' - '0'));
      return true;
    case StandardCharacterSet::kNotLineTerminator: {
      // Match non-newlines (not 0x0A('\n'), 0x0D('\r'), 0x2028 and 0x2029).
      __ Xor(a0, current_character(), Operand(0x01));
      // See if current character is '\n'^1 or '\r'^1, i.e., 0x0B or 0x0C.
      __ SubWord(a0, a0, Operand(0x0B));
      BranchOrBacktrack(on_no_match, Uless_equal, a0, Operand(0x0C - 0x0B));
      if (mode_ == UC16) {
        // Compare original value to 0x2028 and 0x2029, using the already
        // computed (current_char ^ 0x01 - 0x0B). I.e., check for
        // 0x201D (0x2028 - 0x0B) or 0x201E.
        __ SubWord(a0, a0, Operand(0x2028 - 0x0B));
        BranchOrBacktrack(on_no_match, Uless_equal, a0, Operand(1));
      }
      return true;
    }
    case StandardCharacterSet::kLineTerminator: {
      // Match newlines (0x0A('\n'), 0x0D('\r'), 0x2028 and 0x2029).
      __ Xor(a0, current_character(), Operand(0x01));
      // See if current character is '\n'^1 or '\r'^1, i.e., 0x0B or 0x0C.
      __ SubWord(a0, a0, Operand(0x0B));
      if (mode_ == LATIN1) {
        BranchOrBacktrack(on_no_match, Ugreater, a0, Operand(0x0C - 0x0B));
      } else {
        Label done;
        BranchOrBacktrack(&done, Uless_equal, a0, Operand(0x0C - 0x0B));
        // Compare original value to 0x2028 and 0x2029, using the already
        // computed (current_char ^ 0x01 - 0x0B). I.e., check for
        // 0x201D (0x2028 - 0x0B) or 0x201E.
        __ SubWord(a0, a0, Operand(0x2028 - 0x0B));
        BranchOrBacktrack(on_no_match, Ugreater, a0, Operand(1));
        __ bind(&done);
      }
      return true;
    }
    case StandardCharacterSet::kWord: {
      if (mode_ != LATIN1) {
        // Table is 256 entries, so all Latin1 characters can be tested.
        BranchOrBacktrack(on_no_match, Ugreater, current_character(),
                          Operand('z'));
      }
      ExternalReference map = ExternalReference::re_word_character_map();
      __ li(a0, Operand(map));
      __ AddWord(a0, a0, current_character());
      __ Lbu(a0, MemOperand(a0, 0));
      BranchOrBacktrack(on_no_match, eq, a0, Operand(zero_reg));
      return true;
    }
    case StandardCharacterSet::kNotWord: {
      Label done;
      if (mode_ != LATIN1) {
        // Table is 256 entries, so all Latin1 characters can be tested.
        __ BranchShort(&done, Ugreater, current_character(), Operand('z'));
      }
      ExternalReference map = ExternalReference::re_word_character_map();
      __ li(a0, Operand(map));
      __ AddWord(a0, a0, current_character());
      __ Lbu(a0, MemOperand(a0, 0));
      BranchOrBacktrack(on_no_match, ne, a0, Operand(zero_reg));
      if (mode_ != LATIN1) {
        __ bind(&done);
      }
      return true;
    }
    case StandardCharacterSet::kEverything:
      // Match any character.
      return true;
    // No custom implementation (yet): s(UC16), S(UC16).
    default:
      return false;
  }
}

void RegExpMacroAssemblerRISCV::Fail() {
  __ li(a0, Operand(FAILURE));
  __ jmp(&exit_label_);
}

void RegExpMacroAssemblerRISCV::LoadRegExpStackPointerFromMemory(Register dst) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_stack_pointer(isolate());
  __ li(dst, Operand(ref));
  __ LoadWord(dst, MemOperand(dst));
}

void RegExpMacroAssemblerRISCV::StoreRegExpStackPointerToMemory(
    Register src, Register scratch) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_stack_pointer(isolate());
  __ li(scratch, Operand(ref));
  __ StoreWord(src, MemOperand(scratch));
}

void RegExpMacroAssemblerRISCV::PushRegExpBasePointer(Register stack_pointer,
                                                      Register scratch) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ li(scratch, Operand(ref));
  __ LoadWord(scratch, MemOperand(scratch));
  __ SubWord(scratch, stack_pointer, scratch);
  __ StoreWord(scratch,
               MemOperand(frame_pointer(), kRegExpStackBasePointerOffset));
}

void RegExpMacroAssemblerRISCV::PopRegExpBasePointer(Register stack_pointer_out,
                                                     Register scratch) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ LoadWord(stack_pointer_out,
              MemOperand(frame_pointer(), kRegExpStackBasePointerOffset));
  __ li(scratch, Operand(ref));
  __ LoadWord(scratch, MemOperand(scratch));
  __ AddWord(stack_pointer_out, stack_pointer_out, scratch);
  StoreRegExpStackPointerToMemory(stack_pointer_out, scratch);
}

Handle<HeapObject> RegExpMacroAssemblerRISCV::GetCode(Handle<String> source) {
  Label return_a0;
  if (masm_->has_exception()) {
    // If the code gets corrupted due to long regular expressions and lack of
    // space on trampolines, an internal exception flag is set. If this case
    // is detected, we will jump into exit sequence right away.
    __ bind_to(&entry_label_, internal_failure_label_.pos());
  } else {
    // Finalize code - write the entry point code now we know how many
    // registers we need.

    // Entry code:
    __ bind(&entry_label_);

    // Tell the system that we have a stack frame.  Because the type is MANUAL,
    // no is generated.
    FrameScope scope(masm_.get(), StackFrame::MANUAL);

    // Actually emit code to start a new stack frame.
    // Push arguments
    // Save callee-save registers.
    // Start new stack frame.
    // Store link register in existing stack-cell.
    // Order here should correspond to order of offset constants in header file.
    // TODO(plind): we save fp..s11, but ONLY use s3 here - use the regs
    // or dont save.
    RegList registers_to_retain = {fp, s1, s2, s3, s4,  s5,
                                   s6, s7, s8, s9, s10, s11};
    DCHECK(registers_to_retain.Count() == kNumCalleeRegsToRetain);

    // The remaining arguments are passed in registers, e.g.by calling the code
    // entry as cast to a function with the signature:
    //
    // *int(*match)(String input_string,      // a0
    //             int start_offset,          // a1
    //             uint8_t* input_start,      // a2
    //             uint8_t* input_end,        // a3
    //             int* output,               // a4
    //             int output_size,           // a5
    //             int call_origin,           // a6
    //             Isolate* isolate,          // a7
    //             Address regexp);           // on the stack
    RegList argument_registers = {a0, a1, a2, a3, a4, a5, a6, a7};

    // According to MultiPush implementation, registers will be pushed in the
    // order of ra, fp, then s8, ..., s1, and finally a7,...a0
    __ MultiPush(RegList{ra} | registers_to_retain);

    // Set frame pointer in space for it if this is not a direct call
    // from generated code.
    __ AddWord(frame_pointer(), sp, Operand(0));
    static_assert(kFrameTypeOffset == -kSystemPointerSize);
    __ li(kScratchReg, Operand(StackFrame::TypeToMarker(StackFrame::IRREGEXP)));
    __ push(kScratchReg);
    __ MultiPush(argument_registers);
    static_assert(kSuccessfulCapturesOffset ==
                  kInputStringOffset - kSystemPointerSize);
    __ mv(a0, zero_reg);
    __ push(a0);  // Make room for success counter and initialize it to 0.
    static_assert(kStringStartMinusOneOffset ==
                  kSuccessfulCapturesOffset - kSystemPointerSize);
    __ push(a0);  // Make room for "string start - 1" constant.
    static_assert(kBacktrackCountOffset ==
                  kStringStartMinusOneOffset - kSystemPointerSize);
    __ push(a0);  // The backtrack counter
    static_assert(kRegExpStackBasePointerOffset ==
                  kBacktrackCountOffset - kSystemPointerSize);
    __ push(a0);  // The regexp stack base ptr.

    // Initialize backtrack stack pointer. It must not be clobbered from here
    // on. Note the backtrack_stackpointer is callee-saved.
    static_assert(backtrack_stackpointer() == s8);
    LoadRegExpStackPointerFromMemory(backtrack_stackpointer());
    // Store the regexp base pointer - we'll later restore it / write it to
    // memory when returning from this irregexp code object.
    PushRegExpBasePointer(backtrack_stackpointer(), a1);

    {
      // Check if we have space on the stack for registers.
      Label stack_limit_hit, stack_ok;

      ExternalReference stack_limit =
          ExternalReference::address_of_jslimit(masm_->isolate());
      __ li(a0, Operand(stack_limit));
      __ LoadWord(a0, MemOperand(a0));
      __ SubWord(a0, sp, a0);
      Operand extra_space_for_variables(num_registers_ * kSystemPointerSize);
      // Handle it if the stack pointer is already below the stack limit.
      __ Branch(&stack_limit_hit, le, a0, Operand(zero_reg));
      // Check if there is room for the variable number of registers above
      // the stack limit.
      __ Branch(&stack_ok, uge, a0, extra_space_for_variables);
      // Exit with OutOfMemory exception. There is not enough space on the stack
      // for our working registers.
      __ li(a0, Operand(EXCEPTION));
      __ jmp(&return_a0);

      __ bind(&stack_limit_hit);
      CallCheckStackGuardState(a0, extra_space_for_variables);
      // If returned value is non-zero, we exit with the returned value as
      // result.
      __ Branch(&return_a0, ne, a0, Operand(zero_reg));

      __ bind(&stack_ok);
    }
    // Allocate space on stack for registers.
    __ SubWord(sp, sp, Operand(num_registers_ * kSystemPointerSize));
    // Load string end.
    __ LoadWord(end_of_input_address(),
                MemOperand(frame_pointer(), kInputEndOffset));
    // Load input start.
    __ LoadWord(a0, MemOperand(frame_pointer(), kInputStartOffset));
    // Find negative length (offset of start relative to end).
    __ SubWord(current_input_offset(), a0, end_of_input_address());
    // Set a0 to address of char before start of the input string
    // (effectively string position -1).
    __ LoadWord(a1, MemOperand(frame_pointer(), kStartIndexOffset));
    __ SubWord(a0, current_input_offset(), Operand(char_size()));
    __ slli(t1, a1, (mode_ == UC16) ? 1 : 0);
    __ SubWord(a0, a0, t1);
    // Store this value in a local variable, for use when clearing
    // position registers.
    __ StoreWord(a0, MemOperand(frame_pointer(), kStringStartMinusOneOffset));

    // Initialize code pointer register
    __ li(code_pointer(), Operand(masm_->CodeObject()), CONSTANT_SIZE);

    Label load_char_start_regexp;
    {
      Label start_regexp;
      // Load newline if index is at start, previous character otherwise.
      __ Branch(&load_char_start_regexp, ne, a1, Operand(zero_reg));
      __ li(current_character(), Operand('\n'));
      __ jmp(&start_regexp);

      // Global regexp restarts matching here.
      __ bind(&load_char_start_regexp);
      // Load previous char as initial value of current character register.
      LoadCurrentCharacterUnchecked(-1, 1);
      __ bind(&start_regexp);
    }

    // Initialize on-stack registers.
    if (num_saved_registers_ > 0) {  // Always is, if generated from a regexp.
      // Fill saved registers with initial value = start offset - 1.
      if (num_saved_registers_ > 8) {
        // Address of register 0.
        __ AddWord(a1, frame_pointer(), Operand(kRegisterZeroOffset));
        __ li(a2, Operand(num_saved_registers_));
        Label init_loop;
        __ bind(&init_loop);
        __ StoreWord(a0, MemOperand(a1));
        __ AddWord(a1, a1, Operand(-kSystemPointerSize));
        __ SubWord(a2, a2, Operand(1));
        __ Branch(&init_loop, ne, a2, Operand(zero_reg));
      } else {
        for (int i = 0; i < num_saved_registers_; i++) {
          __ StoreWord(a0, register_location(i));
        }
      }
    }

    __ jmp(&start_label_);

    // Exit code:
    if (success_label_.is_linked()) {
      // Save captures when successful.
      __ bind(&success_label_);
      if (num_saved_registers_ > 0) {
        // Copy captures to output.
        __ LoadWord(a1, MemOperand(frame_pointer(), kInputStartOffset));
        __ LoadWord(a0, MemOperand(frame_pointer(), kRegisterOutputOffset));
        __ LoadWord(a2, MemOperand(frame_pointer(), kStartIndexOffset));
        __ SubWord(a1, end_of_input_address(), a1);
        // a1 is length of input in bytes.
        if (mode_ == UC16) {
          __ srli(a1, a1, 1);
        }
        // a1 is length of input in characters.
        __ AddWord(a1, a1, Operand(a2));
        // a1 is length of string in characters.

        DCHECK_EQ(0, num_saved_registers_ % 2);
        // Always an even number of capture registers. This allows us to
        // unroll the loop once to add an operation between a load of a
        // register and the following use of that register.
        for (int i = 0; i < num_saved_registers_; i += 2) {
          __ LoadWord(a2, register_location(i));
          __ LoadWord(a3, register_location(i + 1));
          if (i == 0 && global_with_zero_length_check()) {
            // Keep capture start in a4 for the zero-length check later.
            __ mv(s3, a2);
          }
          if (mode_ == UC16) {
            __ srai(a2, a2, 1);
            __ AddWord(a2, a2, a1);
            __ srai(a3, a3, 1);
            __ AddWord(a3, a3, a1);
          } else {
            __ AddWord(a2, a1, Operand(a2));
            __ AddWord(a3, a1, Operand(a3));
          }
          // V8 expects the output to be an int32_t array.
          __ Sw(a2, MemOperand(a0));
          __ AddWord(a0, a0, kIntSize);
          __ Sw(a3, MemOperand(a0));
          __ AddWord(a0, a0, kIntSize);
        }
      }

      if (global()) {
        // Restart matching if the regular expression is flagged as global.
        __ LoadWord(a0, MemOperand(frame_pointer(), kSuccessfulCapturesOffset));
        __ LoadWord(a1, MemOperand(frame_pointer(), kNumOutputRegistersOffset));
        __ LoadWord(a2, MemOperand(frame_pointer(), kRegisterOutputOffset));
        // Increment success counter.
        __ AddWord(a0, a0, 1);
        __ StoreWord(a0,
                     MemOperand(frame_pointer(), kSuccessfulCapturesOffset));
        // Capture results have been stored, so the number of remaining global
        // output registers is reduced by the number of stored captures.
        __ SubWord(a1, a1, num_saved_registers_);
        // Check whether we have enough room for another set of capture results.
        __ Branch(&return_a0, lt, a1, Operand(num_saved_registers_));

        __ StoreWord(a1,
                     MemOperand(frame_pointer(), kNumOutputRegistersOffset));
        // Advance the location for output.
        __ AddWord(a2, a2, num_saved_registers_ * kIntSize);
        __ StoreWord(a2, MemOperand(frame_pointer(), kRegisterOutputOffset));

        // Restore the original regexp stack pointer value (effectively, pop the
        // stored base pointer).
        PopRegExpBasePointer(backtrack_stackpointer(), a2);

        Label reload_string_start_minus_one;

        if (global_with_zero_length_check()) {
          // Special case for zero-length matches.
          // s3: capture start index
          // Not a zero-length match, restart.
          __ Branch(&reload_string_start_minus_one, ne, current_input_offset(),
                    Operand(s3));
          // Offset from the end is zero if we already reached the end.
          __ Branch(&exit_label_, eq, current_input_offset(),
                    Operand(zero_reg));
          // Advance current position after a zero-length match.
          Label advance;
          __ bind(&advance);
          __ AddWord(current_input_offset(), current_input_offset(),
                     Operand((mode_ == UC16) ? 2 : 1));
          if (global_unicode()) CheckNotInSurrogatePair(0, &advance);
        }

        __ bind(&reload_string_start_minus_one);
        // Prepare a0 to initialize registers with its value in the next run.
        // Must be immediately before the jump to avoid clobbering.
        __ LoadWord(a0,
                    MemOperand(frame_pointer(), kStringStartMinusOneOffset));

        __ Branch(&load_char_start_regexp);
      } else {
        __ li(a0, Operand(SUCCESS));
      }
    }
    // Exit and return a0.
    __ bind(&exit_label_);
    if (global()) {
      __ LoadWord(a0, MemOperand(frame_pointer(), kSuccessfulCapturesOffset));
    }

    __ bind(&return_a0);
    // Restore the original regexp stack pointer value (effectively, pop the
    // stored base pointer).
    PopRegExpBasePointer(backtrack_stackpointer(), a1);
    // Skip sp past regexp registers and local variables..
    __ mv(sp, frame_pointer());

    // Restore registers fp..s11 and return (restoring ra to pc).
    __ MultiPop(registers_to_retain | ra);

    __ Ret();

    // Backtrack code (branch target for conditional backtracks).
    if (backtrack_label_.is_linked()) {
      __ bind(&backtrack_label_);
      Backtrack();
    }

    Label exit_with_exception;

    // Preempt-code.
    if (check_preempt_label_.is_linked()) {
      SafeCallTarget(&check_preempt_label_);
      StoreRegExpStackPointerToMemory(backtrack_stackpointer(), a1);
      // Put regexp engine registers on stack.
      CallCheckStackGuardState(a0);
      // If returning non-zero, we should end execution with the given
      // result as return value.
      __ Branch(&return_a0, ne, a0, Operand(zero_reg));
      LoadRegExpStackPointerFromMemory(backtrack_stackpointer());
      // String might have moved: Reload end of string from frame.
      __ LoadWord(end_of_input_address(),
                  MemOperand(frame_pointer(), kInputEndOffset));
      SafeReturn();
    }

    // Backtrack stack overflow code.
    if (stack_overflow_label_.is_linked()) {
      SafeCallTarget(&stack_overflow_label_);
      // Call GrowStack(isolate).
      StoreRegExpStackPointerToMemory(backtrack_stackpointer(), a1);

      static constexpr int kNumArguments = 1;
      __ PrepareCallCFunction(kNumArguments, 0, a0);
      __ li(a0, ExternalReference::isolate_address(isolate()));
      ExternalReference grow_stack = ExternalReference::re_grow_stack();
      CallCFunctionFromIrregexpCode(grow_stack, kNumArguments);
      // If nullptr is returned, we have failed to grow the stack, and must exit
      // with a stack-overflow exception.
      __ BranchShort(&exit_with_exception, eq, a0, Operand(zero_reg));
      // Otherwise use return value as new stack pointer.
      __ mv(backtrack_stackpointer(), a0);
      // Restore saved registers and continue.
      SafeReturn();
    }

    if (exit_with_exception.is_linked()) {
      // If any of the code above needed to exit with an exception.
      __ bind(&exit_with_exception);
      // Exit with Result EXCEPTION(-1) to signal thrown exception.
      __ li(a0, Operand(EXCEPTION));
      __ jmp(&return_a0);
    }

    if (fallback_label_.is_linked()) {
      __ bind(&fallback_label_);
      __ li(a0, Operand(FALLBACK_TO_EXPERIMENTAL));
      __ jmp(&return_a0);
    }
  }

  CodeDesc code_desc;
  masm_->GetCode(isolate(), &code_desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate(), code_desc, CodeKind::REGEXP)
          .set_self_reference(masm_->CodeObject())
          .set_empty_source_position_table()
          .Build();
  LOG(masm_->isolate(),
      RegExpCodeCreateEvent(Handle<AbstractCode>::cast(code), source));
  return Handle<HeapObject>::cast(code);
}

void RegExpMacroAssemblerRISCV::GoTo(Label* to) {
  if (to == nullptr) {
    Backtrack();
    return;
  }
  __ jmp(to);
  return;
}

void RegExpMacroAssemblerRISCV::IfRegisterGE(int reg, int comparand,
                                             Label* if_ge) {
  __ LoadWord(a0, register_location(reg));
  BranchOrBacktrack(if_ge, ge, a0, Operand(comparand));
}

void RegExpMacroAssemblerRISCV::IfRegisterLT(int reg, int comparand,
                                             Label* if_lt) {
  __ LoadWord(a0, register_location(reg));
  BranchOrBacktrack(if_lt, lt, a0, Operand(comparand));
}

void RegExpMacroAssemblerRISCV::IfRegisterEqPos(int reg, Label* if_eq) {
  __ LoadWord(a0, register_location(reg));
  BranchOrBacktrack(if_eq, eq, a0, Operand(current_input_offset()));
}

RegExpMacroAssembler::IrregexpImplementation
RegExpMacroAssemblerRISCV::Implementation() {
  return kRISCVImplementation;
}

void RegExpMacroAssemblerRISCV::PopCurrentPosition() {
  Pop(current_input_offset());
}

void RegExpMacroAssemblerRISCV::PopRegister(int register_index) {
  Pop(a0);
  __ StoreWord(a0, register_location(register_index));
}

void RegExpMacroAssemblerRISCV::PushBacktrack(Label* label) {
  if (label->is_bound()) {
    int target = label->pos();
    __ li(a0,
          Operand(target + InstructionStream::kHeaderSize - kHeapObjectTag));
  } else {
    Assembler::BlockTrampolinePoolScope block_trampoline_pool(masm_.get());
    Label after_constant;
    __ BranchShort(&after_constant);
    int offset = masm_->pc_offset();
    int cp_offset = offset + InstructionStream::kHeaderSize - kHeapObjectTag;
    __ emit(0);
    masm_->label_at_put(label, offset);
    __ bind(&after_constant);
    if (is_int16(cp_offset)) {
      __ Load32U(a0, MemOperand(code_pointer(), cp_offset));
    } else {
      __ AddWord(a0, code_pointer(), cp_offset);
      __ Load32U(a0, MemOperand(a0, 0));
    }
  }
  Push(a0);
  CheckStackLimit();
}

void RegExpMacroAssemblerRISCV::PushCurrentPosition() {
  Push(current_input_offset());
}

void RegExpMacroAssemblerRISCV::PushRegister(int register_index,
                                             StackCheckFlag check_stack_limit) {
  __ LoadWord(a0, register_location(register_index));
  Push(a0);
  if (check_stack_limit) CheckStackLimit();
}

void RegExpMacroAssemblerRISCV::ReadCurrentPositionFromRegister(int reg) {
  __ LoadWord(current_input_offset(), register_location(reg));
}

void RegExpMacroAssemblerRISCV::WriteStackPointerToRegister(int reg) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ li(a0, ref);
  __ LoadWord(a0, MemOperand(a0));
  __ SubWord(a0, backtrack_stackpointer(), a0);
  __ Sw(a0, register_location(reg));
}

void RegExpMacroAssemblerRISCV::ReadStackPointerFromRegister(int reg) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ li(a1, ref);
  __ LoadWord(a1, MemOperand(a1));
  __ Lw(backtrack_stackpointer(), register_location(reg));
  __ AddWord(backtrack_stackpointer(), backtrack_stackpointer(), a1);
}

void RegExpMacroAssemblerRISCV::SetCurrentPositionFromEnd(int by) {
  Label after_position;
  __ BranchShort(&after_position, ge, current_input_offset(),
                 Operand(-by * char_size()));
  __ li(current_input_offset(), -by * char_size());
  // On RegExp code entry (where this operation is used), the character before
  // the current position is expected to be already loaded.
  // We have advanced the position, so it's safe to read backwards.
  LoadCurrentCharacterUnchecked(-1, 1);
  __ bind(&after_position);
}

void RegExpMacroAssemblerRISCV::SetRegister(int register_index, int to) {
  DCHECK(register_index >= num_saved_registers_);  // Reserved for positions!
  __ li(a0, Operand(to));
  __ StoreWord(a0, register_location(register_index));
}

bool RegExpMacroAssemblerRISCV::Succeed() {
  __ jmp(&success_label_);
  return global();
}

void RegExpMacroAssemblerRISCV::WriteCurrentPositionToRegister(int reg,
                                                               int cp_offset) {
  if (cp_offset == 0) {
    __ StoreWord(current_input_offset(), register_location(reg));
  } else {
    __ AddWord(a0, current_input_offset(), Operand(cp_offset * char_size()));
    __ StoreWord(a0, register_location(reg));
  }
}

void RegExpMacroAssemblerRISCV::ClearRegisters(int reg_from, int reg_to) {
  DCHECK(reg_from <= reg_to);
  __ LoadWord(a0, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
  for (int reg = reg_from; reg <= reg_to; reg++) {
    __ StoreWord(a0, register_location(reg));
  }
}
#ifdef RISCV_HAS_NO_UNALIGNED
bool RegExpMacroAssemblerRISCV::CanReadUnaligned() const { return false; }
#endif
// Private methods:

void RegExpMacroAssemblerRISCV::CallCheckStackGuardState(Register scratch,
                                                         Operand extra_space) {
  DCHECK(!isolate()->IsGeneratingEmbeddedBuiltins());
  DCHECK(!masm_->options().isolate_independent_code);

  int stack_alignment = base::OS::ActivationFrameAlignment();

  // Align the stack pointer and save the original sp value on the stack.
  __ mv(scratch, sp);
  __ SubWord(sp, sp, Operand(kSystemPointerSize));
  DCHECK(base::bits::IsPowerOfTwo(stack_alignment));
  __ And(sp, sp, Operand(-stack_alignment));
  __ StoreWord(scratch, MemOperand(sp));

  __ li(a3, extra_space);
  __ mv(a2, frame_pointer());
  // InstructionStream of self.
  __ li(a1, Operand(masm_->CodeObject()), CONSTANT_SIZE);

  // We need to make room for the return address on the stack.
  DCHECK(IsAligned(stack_alignment, kSystemPointerSize));
  __ SubWord(sp, sp, Operand(stack_alignment));

  // The stack pointer now points to cell where the return address will be
  // written. Arguments are in registers, meaning we treat the return address as
  // argument 5. Since DirectCEntry will handle allocating space for the C
  // argument slots, we don't need to care about that here. This is how the
  // stack will look (sp meaning the value of sp at this moment):
  // [sp + 3] - empty slot if needed for alignment.
  // [sp + 2] - saved sp.
  // [sp + 1] - second word reserved for return value.
  // [sp + 0] - first word reserved for return value.

  // a0 will point to the return address, placed by DirectCEntry.
  __ mv(a0, sp);

  ExternalReference stack_guard_check =
      ExternalReference::re_check_stack_guard_state();
  __ li(t6, Operand(stack_guard_check));

  EmbeddedData d = EmbeddedData::FromBlob();
  CHECK(Builtins::IsIsolateIndependent(Builtin::kDirectCEntry));
  Address entry = d.InstructionStartOf(Builtin::kDirectCEntry);
  __ li(kScratchReg, Operand(entry, RelocInfo::OFF_HEAP_TARGET));
  __ Call(kScratchReg);

  // DirectCEntry allocated space for the C argument slots so we have to
  // drop them with the return address from the stack with loading saved sp.
  // At this point stack must look:
  // [sp + 7] - empty slot if needed for alignment.
  // [sp + 6] - saved sp.
  // [sp + 5] - second word reserved for return value.
  // [sp + 4] - first word reserved for return value.
  // [sp + 3] - C argument slot.
  // [sp + 2] - C argument slot.
  // [sp + 1] - C argument slot.
  // [sp + 0] - C argument slot.
  __ LoadWord(sp, MemOperand(sp, stack_alignment + kCArgsSlotsSize));

  __ li(code_pointer(), Operand(masm_->CodeObject()));
}

// Helper function for reading a value out of a stack frame.
template <typename T>
static T& frame_entry(Address re_frame, int frame_offset) {
  return reinterpret_cast<T&>(Memory<int32_t>(re_frame + frame_offset));
}

template <typename T>
static T* frame_entry_address(Address re_frame, int frame_offset) {
  return reinterpret_cast<T*>(re_frame + frame_offset);
}

int64_t RegExpMacroAssemblerRISCV::CheckStackGuardState(Address* return_address,
                                                        Address raw_code,
                                                        Address re_frame,
                                                        uintptr_t extra_space) {
  Tagged<InstructionStream> re_code =
      InstructionStream::cast(Tagged<Object>(raw_code));
  return NativeRegExpMacroAssembler::CheckStackGuardState(
      frame_entry<Isolate*>(re_frame, kIsolateOffset),
      static_cast<int>(frame_entry<int64_t>(re_frame, kStartIndexOffset)),
      static_cast<RegExp::CallOrigin>(
          frame_entry<int64_t>(re_frame, kDirectCallOffset)),
      return_address, re_code,
      frame_entry_address<Address>(re_frame, kInputStringOffset),
      frame_entry_address<const uint8_t*>(re_frame, kInputStartOffset),
      frame_entry_address<const uint8_t*>(re_frame, kInputEndOffset),
      extra_space);
}

MemOperand RegExpMacroAssemblerRISCV::register_location(int register_index) {
  DCHECK(register_index < (1 << 30));
  if (num_registers_ <= register_index) {
    num_registers_ = register_index + 1;
  }
  return MemOperand(frame_pointer(),
                    kRegisterZeroOffset - register_index * kSystemPointerSize);
}

void RegExpMacroAssemblerRISCV::CheckPosition(int cp_offset,
                                              Label* on_outside_input) {
  if (cp_offset >= 0) {
    BranchOrBacktrack(on_outside_input, ge, current_input_offset(),
                      Operand(-cp_offset * char_size()));
  } else {
    __ LoadWord(a1, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
    __ AddWord(a0, current_input_offset(), Operand(cp_offset * char_size()));
    BranchOrBacktrack(on_outside_input, le, a0, Operand(a1));
  }
}

void RegExpMacroAssemblerRISCV::BranchOrBacktrack(Label* to,
                                                  Condition condition,
                                                  Register rs,
                                                  const Operand& rt) {
  if (condition == al) {  // Unconditional.
    if (to == nullptr) {
      Backtrack();
      return;
    }
    __ jmp(to);
    return;
  }
  if (to == nullptr) {
    __ Branch(&backtrack_label_, condition, rs, rt);
    return;
  }
  __ Branch(to, condition, rs, rt);
}

void RegExpMacroAssemblerRISCV::SafeCall(Label* to, Condition cond, Register rs,
                                         const Operand& rt) {
  __ BranchAndLink(to, cond, rs, rt);
}

void RegExpMacroAssemblerRISCV::SafeReturn() {
  __ pop(ra);
  __ AddWord(t1, ra, Operand(masm_->CodeObject()));
  __ Jump(t1);
}

void RegExpMacroAssemblerRISCV::SafeCallTarget(Label* name) {
  __ bind(name);
  __ SubWord(ra, ra, Operand(masm_->CodeObject()));
  __ push(ra);
}

void RegExpMacroAssemblerRISCV::Push(Register source) {
  DCHECK(source != backtrack_stackpointer());
  __ AddWord(backtrack_stackpointer(), backtrack_stackpointer(),
             Operand(-kIntSize));
  __ Sw(source, MemOperand(backtrack_stackpointer()));
}

void RegExpMacroAssemblerRISCV::Pop(Register target) {
  DCHECK(target != backtrack_stackpointer());
  __ Lw(target, MemOperand(backtrack_stackpointer()));
  __ AddWord(backtrack_stackpointer(), backtrack_stackpointer(), kIntSize);
}

void RegExpMacroAssemblerRISCV::CheckPreemption() {
  // Check for preemption.
  ExternalReference stack_limit =
      ExternalReference::address_of_jslimit(masm_->isolate());
  __ li(a0, Operand(stack_limit));
  __ LoadWord(a0, MemOperand(a0));
  SafeCall(&check_preempt_label_, Uless_equal, sp, Operand(a0));
}

void RegExpMacroAssemblerRISCV::CheckStackLimit() {
  ExternalReference stack_limit =
      ExternalReference::address_of_regexp_stack_limit_address(
          masm_->isolate());

  __ li(a0, Operand(stack_limit));
  __ LoadWord(a0, MemOperand(a0));
  SafeCall(&stack_overflow_label_, Uless_equal, backtrack_stackpointer(),
           Operand(a0));
}

void RegExpMacroAssemblerRISCV::LoadCurrentCharacterUnchecked(int cp_offset,
                                                              int characters) {
  Register offset = current_input_offset();
  if (cp_offset != 0) {
    // kScratchReg2 is not being used to store the capture start index at this
    // point.
    __ AddWord(kScratchReg2, current_input_offset(),
               Operand(cp_offset * char_size()));
    offset = kScratchReg2;
  }
  // If unaligned load/stores are not supported then this function must only
  // be used to load a single character at a time.
  if (!CanReadUnaligned()) {
    DCHECK_EQ(1, characters);
  }

  if (mode_ == LATIN1) {
    if (characters == 4) {
      __ AddWord(kScratchReg, end_of_input_address(), offset);
      __ Load32U(current_character(), MemOperand(kScratchReg));
    } else if (characters == 2) {
      __ AddWord(kScratchReg, end_of_input_address(), offset);
      __ Lhu(current_character(), MemOperand(kScratchReg));
    } else {
      DCHECK_EQ(1, characters);
      __ AddWord(kScratchReg, end_of_input_address(), offset);
      __ Lbu(current_character(), MemOperand(kScratchReg));
    }
  } else {
    DCHECK_EQ(UC16, mode_);
    if (characters == 2) {
      __ AddWord(kScratchReg, end_of_input_address(), offset);
      __ Load32U(current_character(), MemOperand(kScratchReg));
    } else {
      DCHECK_EQ(1, characters);
      __ AddWord(kScratchReg, end_of_input_address(), offset);
      __ Lhu(current_character(), MemOperand(kScratchReg));
    }
  }
}

void RegExpMacroAssemblerRISCV::CallCFunctionFromIrregexpCode(
    ExternalReference function, int num_arguments) {
  // Irregexp code must not set fast_c_call_caller_fp and fast_c_call_caller_pc
  // since
  //
  // 1. it may itself have been called using CallCFunction and nested calls are
  //    unsupported, and
  // 2. it may itself have been called directly from C where the frame pointer
  //    might not be set (-fomit-frame-pointer), and thus frame iteration would
  //    fail.
  //
  // See also: crbug.com/v8/12670#c17.
  __ CallCFunction(function, num_arguments, SetIsolateDataSlots::kNo);
}
#undef __

}  // namespace internal
}  // namespace v8
