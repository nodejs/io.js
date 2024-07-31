// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_ARM

#include "src/regexp/arm/regexp-macro-assembler-arm.h"

#include "src/codegen/arm/assembler-arm-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/heap/factory.h"
#include "src/logging/log.h"
#include "src/objects/code-inl.h"
#include "src/regexp/regexp-stack.h"
#include "src/snapshot/embedded/embedded-data-inl.h"

namespace v8 {
namespace internal {

/*
 * This assembler uses the following register assignment convention
 * - r4 : Temporarily stores the index of capture start after a matching pass
 *        for a global regexp.
 * - r5 : Pointer to current InstructionStream object including heap object tag.
 * - r6 : Current position in input, as negative offset from end of string.
 *        Please notice that this is the byte offset, not the character offset!
 * - r7 : Currently loaded character. Must be loaded using
 *        LoadCurrentCharacter before using any of the dispatch methods.
 * - r8 : Points to tip of backtrack stack
 * - r9 : Unused, might be used by C code and expected unchanged.
 * - r10 : End of input (points to byte after last character in input).
 * - r11 : Frame pointer. Used to access arguments, local variables and
 *         RegExp registers.
 * - r12 : IP register, used by assembler. Very volatile.
 * - r13/sp : Points to tip of C stack.
 *
 * The remaining registers are free for computations.
 * Each call to a public method should retain this convention.
 *
 * The stack will have the following structure:
 *  - fp[52]  Address regexp     (address of the JSRegExp object; unused in
 *                                native code, passed to match signature of
 *                                the interpreter)
 *  - fp[48]  Isolate* isolate   (address of the current isolate)
 *  - fp[44]  direct_call        (if 1, direct call from JavaScript code,
 *                                if 0, call through the runtime system).
 *  - fp[40]  capture array size (may fit multiple sets of matches)
 *  - fp[36]  int* capture_array (int[num_saved_registers_], for output).
 *  --- sp when called ---
 *  - fp[32]  return address     (lr).
 *  - fp[28]  old frame pointer  (r11).
 *  - fp[0..24]  backup of registers r4..r10.
 *  --- frame pointer ----
 *  - fp[-4]  frame marker
 *  - fp[-8]  end of input       (address of end of string).
 *  - fp[-12]  start of input     (address of first character in string).
 *  - fp[-16] start index        (character index of start).
 *  - fp[-20] void* input_string (location of a handle containing the string).
 *  - fp[-24] success counter    (only for global regexps to count matches).
 *  - fp[-28] Offset of location before start of input (effectively character
 *            string start - 1). Used to initialize capture registers to a
 *            non-position.
 *  - fp[-32] At start (if 1, we are starting at the start of the
 *    string, otherwise 0)
 *  - fp[-36] register 0         (Only positions must be stored in the first
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
 *              int* capture_output_array,
 *              int num_capture_registers,
 *              bool direct_call = false,
 *              Isolate* isolate,
 *              Address regexp);
 * The call is performed by NativeRegExpMacroAssembler::Execute()
 * (in regexp-macro-assembler.cc) via the GeneratedCode wrapper.
 */

#define __ ACCESS_MASM(masm_)

const int RegExpMacroAssemblerARM::kRegExpCodeSize;

RegExpMacroAssemblerARM::RegExpMacroAssemblerARM(Isolate* isolate, Zone* zone,
                                                 Mode mode,
                                                 int registers_to_save)
    : NativeRegExpMacroAssembler(isolate, zone),
      masm_(std::make_unique<MacroAssembler>(
          isolate, CodeObjectRequired::kYes,
          NewAssemblerBuffer(kRegExpCodeSize))),
      no_root_array_scope_(masm_.get()),
      mode_(mode),
      num_registers_(registers_to_save),
      num_saved_registers_(registers_to_save),
      entry_label_(),
      start_label_(),
      success_label_(),
      backtrack_label_(),
      exit_label_() {
  DCHECK_EQ(0, registers_to_save % 2);
  __ jmp(&entry_label_);   // We'll write the entry code later.
  __ bind(&start_label_);  // And then continue from here.
}

RegExpMacroAssemblerARM::~RegExpMacroAssemblerARM() = default;

void RegExpMacroAssemblerARM::AbortedCodeGeneration() {
  masm_->AbortedCodeGeneration();
  // Unuse labels in case we throw away the assembler without calling GetCode.
  entry_label_.Unuse();
  start_label_.Unuse();
  success_label_.Unuse();
  backtrack_label_.Unuse();
  exit_label_.Unuse();
  check_preempt_label_.Unuse();
  stack_overflow_label_.Unuse();
  fallback_label_.Unuse();
}

int RegExpMacroAssemblerARM::stack_limit_slack()  {
  return RegExpStack::kStackLimitSlack;
}


void RegExpMacroAssemblerARM::AdvanceCurrentPosition(int by) {
  if (by != 0) {
    __ add(current_input_offset(),
           current_input_offset(), Operand(by * char_size()));
  }
}


void RegExpMacroAssemblerARM::AdvanceRegister(int reg, int by) {
  DCHECK_LE(0, reg);
  DCHECK_GT(num_registers_, reg);
  if (by != 0) {
    __ ldr(r0, register_location(reg));
    __ add(r0, r0, Operand(by));
    __ str(r0, register_location(reg));
  }
}


void RegExpMacroAssemblerARM::Backtrack() {
  CheckPreemption();
  if (has_backtrack_limit()) {
    Label next;
    __ ldr(r0, MemOperand(frame_pointer(), kBacktrackCountOffset));
    __ add(r0, r0, Operand(1));
    __ str(r0, MemOperand(frame_pointer(), kBacktrackCountOffset));
    __ cmp(r0, Operand(backtrack_limit()));
    __ b(ne, &next);

    // Backtrack limit exceeded.
    if (can_fallback()) {
      __ jmp(&fallback_label_);
    } else {
      // Can't fallback, so we treat it as a failed match.
      Fail();
    }

    __ bind(&next);
  }
  // Pop InstructionStream offset from backtrack stack, add InstructionStream
  // and jump to location.
  Pop(r0);
  __ add(pc, r0, Operand(code_pointer()));
}


void RegExpMacroAssemblerARM::Bind(Label* label) {
  __ bind(label);
}


void RegExpMacroAssemblerARM::CheckCharacter(uint32_t c, Label* on_equal) {
  __ cmp(current_character(), Operand(c));
  BranchOrBacktrack(eq, on_equal);
}

void RegExpMacroAssemblerARM::CheckCharacterGT(base::uc16 limit,
                                               Label* on_greater) {
  __ cmp(current_character(), Operand(limit));
  BranchOrBacktrack(gt, on_greater);
}

void RegExpMacroAssemblerARM::CheckAtStart(int cp_offset, Label* on_at_start) {
  __ ldr(r1, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
  __ add(r0, current_input_offset(),
         Operand(-char_size() + cp_offset * char_size()));
  __ cmp(r0, r1);
  BranchOrBacktrack(eq, on_at_start);
}

void RegExpMacroAssemblerARM::CheckNotAtStart(int cp_offset,
                                              Label* on_not_at_start) {
  __ ldr(r1, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
  __ add(r0, current_input_offset(),
         Operand(-char_size() + cp_offset * char_size()));
  __ cmp(r0, r1);
  BranchOrBacktrack(ne, on_not_at_start);
}

void RegExpMacroAssemblerARM::CheckCharacterLT(base::uc16 limit,
                                               Label* on_less) {
  __ cmp(current_character(), Operand(limit));
  BranchOrBacktrack(lt, on_less);
}

void RegExpMacroAssemblerARM::CheckGreedyLoop(Label* on_equal) {
  __ ldr(r0, MemOperand(backtrack_stackpointer(), 0));
  __ cmp(current_input_offset(), r0);
  __ add(backtrack_stackpointer(), backtrack_stackpointer(),
         Operand(kSystemPointerSize), LeaveCC, eq);
  BranchOrBacktrack(eq, on_equal);
}

void RegExpMacroAssemblerARM::CheckNotBackReferenceIgnoreCase(
    int start_reg, bool read_backward, bool unicode, Label* on_no_match) {
  Label fallthrough;
  __ ldr(r0, register_location(start_reg));  // Index of start of capture
  __ ldr(r1, register_location(start_reg + 1));  // Index of end of capture
  __ sub(r1, r1, r0, SetCC);  // Length of capture.

  // At this point, the capture registers are either both set or both cleared.
  // If the capture length is zero, then the capture is either empty or cleared.
  // Fall through in both cases.
  __ b(eq, &fallthrough);

  // Check that there are enough characters left in the input.
  if (read_backward) {
    __ ldr(r3, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
    __ add(r3, r3, r1);
    __ cmp(current_input_offset(), r3);
    BranchOrBacktrack(le, on_no_match);
  } else {
    __ cmn(r1, Operand(current_input_offset()));
    BranchOrBacktrack(gt, on_no_match);
  }

  if (mode_ == LATIN1) {
    Label success;
    Label fail;
    Label loop_check;

    // r0 - offset of start of capture
    // r1 - length of capture
    __ add(r0, r0, end_of_input_address());
    __ add(r2, end_of_input_address(), current_input_offset());
    if (read_backward) {
      __ sub(r2, r2, r1);  // Offset by length when matching backwards.
    }
    __ add(r1, r0, r1);

    // r0 - Address of start of capture.
    // r1 - Address of end of capture
    // r2 - Address of current input position.

    Label loop;
    __ bind(&loop);
    __ ldrb(r3, MemOperand(r0, char_size(), PostIndex));
    __ ldrb(r4, MemOperand(r2, char_size(), PostIndex));
    __ cmp(r4, r3);
    __ b(eq, &loop_check);

    // Mismatch, try case-insensitive match (converting letters to lower-case).
    __ orr(r3, r3, Operand(0x20));  // Convert capture character to lower-case.
    __ orr(r4, r4, Operand(0x20));  // Also convert input character.
    __ cmp(r4, r3);
    __ b(ne, &fail);
    __ sub(r3, r3, Operand('a'));
    __ cmp(r3, Operand('z' - 'a'));  // Is r3 a lowercase letter?
    __ b(ls, &loop_check);  // In range 'a'-'z'.
    // Latin-1: Check for values in range [224,254] but not 247.
    __ sub(r3, r3, Operand(224 - 'a'));
    __ cmp(r3, Operand(254 - 224));
    __ b(hi, &fail);  // Weren't Latin-1 letters.
    __ cmp(r3, Operand(247 - 224));  // Check for 247.
    __ b(eq, &fail);

    __ bind(&loop_check);
    __ cmp(r0, r1);
    __ b(lt, &loop);
    __ jmp(&success);

    __ bind(&fail);
    BranchOrBacktrack(al, on_no_match);

    __ bind(&success);
    // Compute new value of character position after the matched part.
    __ sub(current_input_offset(), r2, end_of_input_address());
    if (read_backward) {
      __ ldr(r0, register_location(start_reg));  // Index of start of capture
      __ ldr(r1, register_location(start_reg + 1));  // Index of end of capture
      __ add(current_input_offset(), current_input_offset(), r0);
      __ sub(current_input_offset(), current_input_offset(), r1);
    }
  } else {
    DCHECK(mode_ == UC16);
    int argument_count = 4;
    __ PrepareCallCFunction(argument_count);

    // r0 - offset of start of capture
    // r1 - length of capture

    // Put arguments into arguments registers.
    // Parameters are
    //   r0: Address byte_offset1 - Address captured substring's start.
    //   r1: Address byte_offset2 - Address of current character position.
    //   r2: size_t byte_length - length of capture in bytes(!)
    //   r3: Isolate* isolate.

    // Address of start of capture.
    __ add(r0, r0, Operand(end_of_input_address()));
    // Length of capture.
    __ mov(r2, Operand(r1));
    // Save length in callee-save register for use on return.
    __ mov(r4, Operand(r1));
    // Address of current input position.
    __ add(r1, current_input_offset(), end_of_input_address());
    if (read_backward) {
      __ sub(r1, r1, r4);
    }
    // Isolate.
    __ mov(r3, Operand(ExternalReference::isolate_address(isolate())));

    {
      AllowExternalCallThatCantCauseGC scope(masm_.get());
      ExternalReference function =
          unicode
              ? ExternalReference::re_case_insensitive_compare_unicode()
              : ExternalReference::re_case_insensitive_compare_non_unicode();
      CallCFunctionFromIrregexpCode(function, argument_count);
    }

    // Check if function returned non-zero for success or zero for failure.
    __ cmp(r0, Operand::Zero());
    BranchOrBacktrack(eq, on_no_match);

    // On success, advance position by length of capture.
    if (read_backward) {
      __ sub(current_input_offset(), current_input_offset(), r4);
    } else {
      __ add(current_input_offset(), current_input_offset(), r4);
    }
  }

  __ bind(&fallthrough);
}

void RegExpMacroAssemblerARM::CheckNotBackReference(int start_reg,
                                                    bool read_backward,
                                                    Label* on_no_match) {
  Label fallthrough;

  // Find length of back-referenced capture.
  __ ldr(r0, register_location(start_reg));
  __ ldr(r1, register_location(start_reg + 1));
  __ sub(r1, r1, r0, SetCC);  // Length to check.

  // At this point, the capture registers are either both set or both cleared.
  // If the capture length is zero, then the capture is either empty or cleared.
  // Fall through in both cases.
  __ b(eq, &fallthrough);

  // Check that there are enough characters left in the input.
  if (read_backward) {
    __ ldr(r3, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
    __ add(r3, r3, r1);
    __ cmp(current_input_offset(), r3);
    BranchOrBacktrack(le, on_no_match);
  } else {
    __ cmn(r1, Operand(current_input_offset()));
    BranchOrBacktrack(gt, on_no_match);
  }

  // r0 - offset of start of capture
  // r1 - length of capture
  __ add(r0, r0, end_of_input_address());
  __ add(r2, end_of_input_address(), current_input_offset());
  if (read_backward) {
    __ sub(r2, r2, r1);  // Offset by length when matching backwards.
  }
  __ add(r1, r0, r1);

  Label loop;
  __ bind(&loop);
  if (mode_ == LATIN1) {
    __ ldrb(r3, MemOperand(r0, char_size(), PostIndex));
    __ ldrb(r4, MemOperand(r2, char_size(), PostIndex));
  } else {
    DCHECK(mode_ == UC16);
    __ ldrh(r3, MemOperand(r0, char_size(), PostIndex));
    __ ldrh(r4, MemOperand(r2, char_size(), PostIndex));
  }
  __ cmp(r3, r4);
  BranchOrBacktrack(ne, on_no_match);
  __ cmp(r0, r1);
  __ b(lt, &loop);

  // Move current character position to position after match.
  __ sub(current_input_offset(), r2, end_of_input_address());
  if (read_backward) {
    __ ldr(r0, register_location(start_reg));      // Index of start of capture
    __ ldr(r1, register_location(start_reg + 1));  // Index of end of capture
    __ add(current_input_offset(), current_input_offset(), r0);
    __ sub(current_input_offset(), current_input_offset(), r1);
  }

  __ bind(&fallthrough);
}


void RegExpMacroAssemblerARM::CheckNotCharacter(unsigned c,
                                                Label* on_not_equal) {
  __ cmp(current_character(), Operand(c));
  BranchOrBacktrack(ne, on_not_equal);
}


void RegExpMacroAssemblerARM::CheckCharacterAfterAnd(uint32_t c,
                                                     uint32_t mask,
                                                     Label* on_equal) {
  if (c == 0) {
    __ tst(current_character(), Operand(mask));
  } else {
    __ and_(r0, current_character(), Operand(mask));
    __ cmp(r0, Operand(c));
  }
  BranchOrBacktrack(eq, on_equal);
}


void RegExpMacroAssemblerARM::CheckNotCharacterAfterAnd(unsigned c,
                                                        unsigned mask,
                                                        Label* on_not_equal) {
  if (c == 0) {
    __ tst(current_character(), Operand(mask));
  } else {
    __ and_(r0, current_character(), Operand(mask));
    __ cmp(r0, Operand(c));
  }
  BranchOrBacktrack(ne, on_not_equal);
}

void RegExpMacroAssemblerARM::CheckNotCharacterAfterMinusAnd(
    base::uc16 c, base::uc16 minus, base::uc16 mask, Label* on_not_equal) {
  DCHECK_GT(String::kMaxUtf16CodeUnit, minus);
  __ sub(r0, current_character(), Operand(minus));
  __ and_(r0, r0, Operand(mask));
  __ cmp(r0, Operand(c));
  BranchOrBacktrack(ne, on_not_equal);
}

void RegExpMacroAssemblerARM::CheckCharacterInRange(base::uc16 from,
                                                    base::uc16 to,
                                                    Label* on_in_range) {
  __ sub(r0, current_character(), Operand(from));
  __ cmp(r0, Operand(to - from));
  BranchOrBacktrack(ls, on_in_range);  // Unsigned lower-or-same condition.
}

void RegExpMacroAssemblerARM::CheckCharacterNotInRange(base::uc16 from,
                                                       base::uc16 to,
                                                       Label* on_not_in_range) {
  __ sub(r0, current_character(), Operand(from));
  __ cmp(r0, Operand(to - from));
  BranchOrBacktrack(hi, on_not_in_range);  // Unsigned higher condition.
}

void RegExpMacroAssemblerARM::CallIsCharacterInRangeArray(
    const ZoneList<CharacterRange>* ranges) {
  static const int kNumArguments = 3;
  __ PrepareCallCFunction(kNumArguments);

  __ mov(r0, current_character());
  __ mov(r1, Operand(GetOrAddRangeArray(ranges)));
  __ mov(r2, Operand(ExternalReference::isolate_address(isolate())));

  {
    // We have a frame (set up in GetCode), but the assembler doesn't know.
    FrameScope scope(masm_.get(), StackFrame::MANUAL);
    CallCFunctionFromIrregexpCode(
        ExternalReference::re_is_character_in_range_array(), kNumArguments);
  }

  __ mov(code_pointer(), Operand(masm_->CodeObject()));
}

bool RegExpMacroAssemblerARM::CheckCharacterInRangeArray(
    const ZoneList<CharacterRange>* ranges, Label* on_in_range) {
  CallIsCharacterInRangeArray(ranges);
  __ cmp(r0, Operand::Zero());
  BranchOrBacktrack(ne, on_in_range);
  return true;
}

bool RegExpMacroAssemblerARM::CheckCharacterNotInRangeArray(
    const ZoneList<CharacterRange>* ranges, Label* on_not_in_range) {
  CallIsCharacterInRangeArray(ranges);
  __ cmp(r0, Operand::Zero());
  BranchOrBacktrack(eq, on_not_in_range);
  return true;
}

void RegExpMacroAssemblerARM::CheckBitInTable(
    Handle<ByteArray> table,
    Label* on_bit_set) {
  __ mov(r0, Operand(table));
  if (mode_ != LATIN1 || kTableMask != String::kMaxOneByteCharCode) {
    __ and_(r1, current_character(), Operand(kTableSize - 1));
    __ add(r1, r1, Operand(ByteArray::kHeaderSize - kHeapObjectTag));
  } else {
    __ add(r1,
           current_character(),
           Operand(ByteArray::kHeaderSize - kHeapObjectTag));
  }
  __ ldrb(r0, MemOperand(r0, r1));
  __ cmp(r0, Operand::Zero());
  BranchOrBacktrack(ne, on_bit_set);
}

bool RegExpMacroAssemblerARM::CheckSpecialClassRanges(StandardCharacterSet type,
                                                      Label* on_no_match) {
  // Range checks (c in min..max) are generally implemented by an unsigned
  // (c - min) <= (max - min) check
  // TODO(jgruber): No custom implementation (yet): s(UC16), S(UC16).
  switch (type) {
    case StandardCharacterSet::kWhitespace:
      // Match space-characters.
      if (mode_ == LATIN1) {
        // One byte space characters are '\t'..'\r', ' ' and \u00a0.
        Label success;
        __ cmp(current_character(), Operand(' '));
        __ b(eq, &success);
        // Check range 0x09..0x0D.
        __ sub(r0, current_character(), Operand('\t'));
        __ cmp(r0, Operand('\r' - '\t'));
        __ b(ls, &success);
        // \u00a0 (NBSP).
        __ cmp(r0, Operand(0x00A0 - '\t'));
        BranchOrBacktrack(ne, on_no_match);
        __ bind(&success);
        return true;
      }
      return false;
    case StandardCharacterSet::kNotWhitespace:
      // The emitted code for generic character classes is good enough.
      return false;
    case StandardCharacterSet::kDigit:
      // Match ASCII digits ('0'..'9')
      __ sub(r0, current_character(), Operand('0'));
      __ cmp(r0, Operand('9' - '0'));
      BranchOrBacktrack(hi, on_no_match);
      return true;
    case StandardCharacterSet::kNotDigit:
      // Match non ASCII-digits
      __ sub(r0, current_character(), Operand('0'));
      __ cmp(r0, Operand('9' - '0'));
      BranchOrBacktrack(ls, on_no_match);
      return true;
    case StandardCharacterSet::kNotLineTerminator: {
      // Match non-newlines (not 0x0A('\n'), 0x0D('\r'), 0x2028 and 0x2029)
      __ eor(r0, current_character(), Operand(0x01));
      // See if current character is '\n'^1 or '\r'^1, i.e., 0x0B or 0x0C
      __ sub(r0, r0, Operand(0x0B));
      __ cmp(r0, Operand(0x0C - 0x0B));
      BranchOrBacktrack(ls, on_no_match);
      if (mode_ == UC16) {
        // Compare original value to 0x2028 and 0x2029, using the already
        // computed (current_char ^ 0x01 - 0x0B). I.e., check for
        // 0x201D (0x2028 - 0x0B) or 0x201E.
        __ sub(r0, r0, Operand(0x2028 - 0x0B));
        __ cmp(r0, Operand(1));
        BranchOrBacktrack(ls, on_no_match);
      }
      return true;
    }
    case StandardCharacterSet::kLineTerminator: {
      // Match newlines (0x0A('\n'), 0x0D('\r'), 0x2028 and 0x2029)
      __ eor(r0, current_character(), Operand(0x01));
      // See if current character is '\n'^1 or '\r'^1, i.e., 0x0B or 0x0C
      __ sub(r0, r0, Operand(0x0B));
      __ cmp(r0, Operand(0x0C - 0x0B));
      if (mode_ == LATIN1) {
        BranchOrBacktrack(hi, on_no_match);
      } else {
        Label done;
        __ b(ls, &done);
        // Compare original value to 0x2028 and 0x2029, using the already
        // computed (current_char ^ 0x01 - 0x0B). I.e., check for
        // 0x201D (0x2028 - 0x0B) or 0x201E.
        __ sub(r0, r0, Operand(0x2028 - 0x0B));
        __ cmp(r0, Operand(1));
        BranchOrBacktrack(hi, on_no_match);
        __ bind(&done);
      }
      return true;
    }
    case StandardCharacterSet::kWord: {
      if (mode_ != LATIN1) {
        // Table is 256 entries, so all Latin1 characters can be tested.
        __ cmp(current_character(), Operand('z'));
        BranchOrBacktrack(hi, on_no_match);
      }
      ExternalReference map = ExternalReference::re_word_character_map();
      __ mov(r0, Operand(map));
      __ ldrb(r0, MemOperand(r0, current_character()));
      __ cmp(r0, Operand::Zero());
      BranchOrBacktrack(eq, on_no_match);
      return true;
    }
    case StandardCharacterSet::kNotWord: {
      Label done;
      if (mode_ != LATIN1) {
        // Table is 256 entries, so all Latin1 characters can be tested.
        __ cmp(current_character(), Operand('z'));
        __ b(hi, &done);
      }
      ExternalReference map = ExternalReference::re_word_character_map();
      __ mov(r0, Operand(map));
      __ ldrb(r0, MemOperand(r0, current_character()));
      __ cmp(r0, Operand::Zero());
      BranchOrBacktrack(ne, on_no_match);
      if (mode_ != LATIN1) {
        __ bind(&done);
      }
      return true;
    }
    case StandardCharacterSet::kEverything:
      // Match any character.
      return true;
  }
}

void RegExpMacroAssemblerARM::Fail() {
  __ mov(r0, Operand(FAILURE));
  __ jmp(&exit_label_);
}

void RegExpMacroAssemblerARM::LoadRegExpStackPointerFromMemory(Register dst) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_stack_pointer(isolate());
  __ mov(dst, Operand(ref));
  __ ldr(dst, MemOperand(dst));
}

void RegExpMacroAssemblerARM::StoreRegExpStackPointerToMemory(
    Register src, Register scratch) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_stack_pointer(isolate());
  __ mov(scratch, Operand(ref));
  __ str(src, MemOperand(scratch));
}

void RegExpMacroAssemblerARM::PushRegExpBasePointer(Register stack_pointer,
                                                    Register scratch) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ mov(scratch, Operand(ref));
  __ ldr(scratch, MemOperand(scratch));
  __ sub(scratch, stack_pointer, scratch);
  __ str(scratch, MemOperand(frame_pointer(), kRegExpStackBasePointerOffset));
}

void RegExpMacroAssemblerARM::PopRegExpBasePointer(Register stack_pointer_out,
                                                   Register scratch) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ ldr(stack_pointer_out,
         MemOperand(frame_pointer(), kRegExpStackBasePointerOffset));
  __ mov(scratch, Operand(ref));
  __ ldr(scratch, MemOperand(scratch));
  __ add(stack_pointer_out, stack_pointer_out, scratch);
  StoreRegExpStackPointerToMemory(stack_pointer_out, scratch);
}

Handle<HeapObject> RegExpMacroAssemblerARM::GetCode(Handle<String> source) {
  Label return_r0;
  // Finalize code - write the entry point code now we know how many
  // registers we need.

  // Entry code:
  __ bind(&entry_label_);

  // Tell the system that we have a stack frame.  Because the type is MANUAL, no
  // is generated.
  FrameScope scope(masm_.get(), StackFrame::MANUAL);

  // Emit code to start a new stack frame. In the following we push all
  // callee-save registers (these end up above the fp) and all register
  // arguments (in {r0,r1,r2,r3}, these end up below the fp).
  RegList registers_to_retain = {r4, r5, r6, r7, r8, r9, r10, fp};
  __ stm(db_w, sp, registers_to_retain | lr);
  __ mov(frame_pointer(), sp);

  // Registers {r0,r1,r2,r3} are the first four arguments as per the C calling
  // convention, and must match our specified offsets (e.g. kInputEndOffset).
  //
  // r0: input_string
  // r1: start_offset
  // r2: input_start
  // r3: input_end
  RegList argument_registers = {r0, r1, r2, r3};
  // Also push the frame marker.
  __ mov(r4, Operand(StackFrame::TypeToMarker(StackFrame::IRREGEXP)));
  static_assert(kFrameTypeOffset == kFramePointerOffset - kSystemPointerSize);
  static_assert(kInputEndOffset == kFrameTypeOffset - kSystemPointerSize);
  static_assert(kInputStartOffset == kInputEndOffset - kSystemPointerSize);
  static_assert(kStartIndexOffset == kInputStartOffset - kSystemPointerSize);
  static_assert(kInputStringOffset == kStartIndexOffset - kSystemPointerSize);
  __ stm(db_w, sp, argument_registers | r4);

  static_assert(kSuccessfulCapturesOffset ==
                kInputStringOffset - kSystemPointerSize);
  __ mov(r0, Operand::Zero());
  __ push(r0);  // Make room for success counter and initialize it to 0.
  static_assert(kStringStartMinusOneOffset ==
                kSuccessfulCapturesOffset - kSystemPointerSize);
  __ push(r0);  // Make room for "string start - 1" constant.
  static_assert(kBacktrackCountOffset ==
                kStringStartMinusOneOffset - kSystemPointerSize);
  __ push(r0);  // The backtrack counter.
  static_assert(kRegExpStackBasePointerOffset ==
                kBacktrackCountOffset - kSystemPointerSize);
  __ push(r0);  // The regexp stack base ptr.

  // Initialize backtrack stack pointer. It must not be clobbered from here on.
  // Note the backtrack_stackpointer is callee-saved.
  static_assert(backtrack_stackpointer() == r8);
  LoadRegExpStackPointerFromMemory(backtrack_stackpointer());

  // Store the regexp base pointer - we'll later restore it / write it to
  // memory when returning from this irregexp code object.
  PushRegExpBasePointer(backtrack_stackpointer(), r1);

  {
    // Check if we have space on the stack for registers.
    Label stack_limit_hit, stack_ok;

    ExternalReference stack_limit =
        ExternalReference::address_of_jslimit(isolate());
    __ mov(r0, Operand(stack_limit));
    __ ldr(r0, MemOperand(r0));
    __ sub(r0, sp, r0, SetCC);
    Operand extra_space_for_variables(num_registers_ * kSystemPointerSize);

    // Handle it if the stack pointer is already below the stack limit.
    __ b(ls, &stack_limit_hit);
    // Check if there is room for the variable number of registers above
    // the stack limit.
    __ cmp(r0, extra_space_for_variables);
    __ b(hs, &stack_ok);
    // Exit with OutOfMemory exception. There is not enough space on the stack
    // for our working registers.
    __ mov(r0, Operand(EXCEPTION));
    __ jmp(&return_r0);

    __ bind(&stack_limit_hit);
    CallCheckStackGuardState(extra_space_for_variables);
    __ cmp(r0, Operand::Zero());
    // If returned value is non-zero, we exit with the returned value as result.
    __ b(ne, &return_r0);

    __ bind(&stack_ok);
  }

  // Allocate space on stack for registers.
  __ AllocateStackSpace(num_registers_ * kSystemPointerSize);
  // Load string end.
  __ ldr(end_of_input_address(), MemOperand(frame_pointer(), kInputEndOffset));
  // Load input start.
  __ ldr(r0, MemOperand(frame_pointer(), kInputStartOffset));
  // Find negative length (offset of start relative to end).
  __ sub(current_input_offset(), r0, end_of_input_address());
  // Set r0 to address of char before start of the input string
  // (effectively string position -1).
  __ ldr(r1, MemOperand(frame_pointer(), kStartIndexOffset));
  __ sub(r0, current_input_offset(), Operand(char_size()));
  __ sub(r0, r0, Operand(r1, LSL, (mode_ == UC16) ? 1 : 0));
  // Store this value in a local variable, for use when clearing
  // position registers.
  __ str(r0, MemOperand(frame_pointer(), kStringStartMinusOneOffset));

  // Initialize code pointer register
  __ mov(code_pointer(), Operand(masm_->CodeObject()));

  Label load_char_start_regexp;
  {
    Label start_regexp;
    // Load newline if index is at start, previous character otherwise.
    __ cmp(r1, Operand::Zero());
    __ b(ne, &load_char_start_regexp);
    __ mov(current_character(), Operand('\n'), LeaveCC, eq);
    __ jmp(&start_regexp);

    // Global regexp restarts matching here.
    __ bind(&load_char_start_regexp);
    // Load previous char as initial value of current character register.
    LoadCurrentCharacterUnchecked(-1, 1);
    __ bind(&start_regexp);
  }

  // Initialize on-stack registers.
  if (num_saved_registers_ > 0) {  // Always is, if generated from a regexp.
    // Fill saved registers with initial value = start offset - 1
    if (num_saved_registers_ > 8) {
      // Address of register 0.
      __ add(r1, frame_pointer(), Operand(kRegisterZeroOffset));
      __ mov(r2, Operand(num_saved_registers_));
      Label init_loop;
      __ bind(&init_loop);
      __ str(r0, MemOperand(r1, kSystemPointerSize, NegPostIndex));
      __ sub(r2, r2, Operand(1), SetCC);
      __ b(ne, &init_loop);
    } else {
      for (int i = 0; i < num_saved_registers_; i++) {
        __ str(r0, register_location(i));
      }
    }
  }

  __ jmp(&start_label_);

  // Exit code:
  if (success_label_.is_linked()) {
    // Save captures when successful.
    __ bind(&success_label_);
    if (num_saved_registers_ > 0) {
      // copy captures to output
      __ ldr(r1, MemOperand(frame_pointer(), kInputStartOffset));
      __ ldr(r0, MemOperand(frame_pointer(), kRegisterOutputOffset));
      __ ldr(r2, MemOperand(frame_pointer(), kStartIndexOffset));
      __ sub(r1, end_of_input_address(), r1);
      // r1 is length of input in bytes.
      if (mode_ == UC16) {
        __ mov(r1, Operand(r1, LSR, 1));
      }
      // r1 is length of input in characters.
      __ add(r1, r1, Operand(r2));
      // r1 is length of string in characters.

      DCHECK_EQ(0, num_saved_registers_ % 2);
      // Always an even number of capture registers. This allows us to
      // unroll the loop once to add an operation between a load of a register
      // and the following use of that register.
      for (int i = 0; i < num_saved_registers_; i += 2) {
        __ ldr(r2, register_location(i));
        __ ldr(r3, register_location(i + 1));
        if (i == 0 && global_with_zero_length_check()) {
          // Keep capture start in r4 for the zero-length check later.
          __ mov(r4, r2);
        }
        if (mode_ == UC16) {
          __ add(r2, r1, Operand(r2, ASR, 1));
          __ add(r3, r1, Operand(r3, ASR, 1));
        } else {
          __ add(r2, r1, Operand(r2));
          __ add(r3, r1, Operand(r3));
        }
        __ str(r2, MemOperand(r0, kSystemPointerSize, PostIndex));
        __ str(r3, MemOperand(r0, kSystemPointerSize, PostIndex));
      }
    }

    if (global()) {
      // Restart matching if the regular expression is flagged as global.
      __ ldr(r0, MemOperand(frame_pointer(), kSuccessfulCapturesOffset));
      __ ldr(r1, MemOperand(frame_pointer(), kNumOutputRegistersOffset));
      __ ldr(r2, MemOperand(frame_pointer(), kRegisterOutputOffset));
      // Increment success counter.
      __ add(r0, r0, Operand(1));
      __ str(r0, MemOperand(frame_pointer(), kSuccessfulCapturesOffset));
      // Capture results have been stored, so the number of remaining global
      // output registers is reduced by the number of stored captures.
      __ sub(r1, r1, Operand(num_saved_registers_));
      // Check whether we have enough room for another set of capture results.
      __ cmp(r1, Operand(num_saved_registers_));
      __ b(lt, &return_r0);

      __ str(r1, MemOperand(frame_pointer(), kNumOutputRegistersOffset));
      // Advance the location for output.
      __ add(r2, r2, Operand(num_saved_registers_ * kSystemPointerSize));
      __ str(r2, MemOperand(frame_pointer(), kRegisterOutputOffset));

      // Restore the original regexp stack pointer value (effectively, pop the
      // stored base pointer).
      PopRegExpBasePointer(backtrack_stackpointer(), r2);

      Label reload_string_start_minus_one;

      if (global_with_zero_length_check()) {
        // Special case for zero-length matches.
        // r4: capture start index
        __ cmp(current_input_offset(), r4);
        // Not a zero-length match, restart.
        __ b(ne, &reload_string_start_minus_one);
        // Offset from the end is zero if we already reached the end.
        __ cmp(current_input_offset(), Operand::Zero());
        __ b(eq, &exit_label_);
        // Advance current position after a zero-length match.
        Label advance;
        __ bind(&advance);
        __ add(current_input_offset(), current_input_offset(),
               Operand((mode_ == UC16) ? 2 : 1));
        if (global_unicode()) CheckNotInSurrogatePair(0, &advance);
      }

      __ bind(&reload_string_start_minus_one);
      // Prepare r0 to initialize registers with its value in the next run.
      // Must be immediately before the jump to avoid clobbering.
      __ ldr(r0, MemOperand(frame_pointer(), kStringStartMinusOneOffset));

      __ b(&load_char_start_regexp);
    } else {
      __ mov(r0, Operand(SUCCESS));
    }
  }

  // Exit and return r0
  __ bind(&exit_label_);
  if (global()) {
    __ ldr(r0, MemOperand(frame_pointer(), kSuccessfulCapturesOffset));
  }

  __ bind(&return_r0);
  // Restore the original regexp stack pointer value (effectively, pop the
  // stored base pointer).
  PopRegExpBasePointer(backtrack_stackpointer(), r2);

  // Skip sp past regexp registers and local variables..
  __ mov(sp, frame_pointer());
  // Restore registers r4..r11 and return (restoring lr to pc).
  __ ldm(ia_w, sp, registers_to_retain | pc);

  // Backtrack code (branch target for conditional backtracks).
  if (backtrack_label_.is_linked()) {
    __ bind(&backtrack_label_);
    Backtrack();
  }

  Label exit_with_exception;

  // Preempt-code.
  if (check_preempt_label_.is_linked()) {
    SafeCallTarget(&check_preempt_label_);

    StoreRegExpStackPointerToMemory(backtrack_stackpointer(), r1);

    CallCheckStackGuardState();
    __ cmp(r0, Operand::Zero());
    // If returning non-zero, we should end execution with the given
    // result as return value.
    __ b(ne, &return_r0);

    LoadRegExpStackPointerFromMemory(backtrack_stackpointer());

    // String might have moved: Reload end of string from frame.
    __ ldr(end_of_input_address(),
           MemOperand(frame_pointer(), kInputEndOffset));
    SafeReturn();
  }

  // Backtrack stack overflow code.
  if (stack_overflow_label_.is_linked()) {
    SafeCallTarget(&stack_overflow_label_);
    // Reached if the backtrack-stack limit has been hit.

    // Call GrowStack(isolate).

    StoreRegExpStackPointerToMemory(backtrack_stackpointer(), r1);

    static constexpr int kNumArguments = 1;
    __ PrepareCallCFunction(kNumArguments);
    __ mov(r0, Operand(ExternalReference::isolate_address(isolate())));
    ExternalReference grow_stack = ExternalReference::re_grow_stack();
    CallCFunctionFromIrregexpCode(grow_stack, kNumArguments);
    // If nullptr is returned, we have failed to grow the stack, and must exit
    // with a stack-overflow exception.
    __ cmp(r0, Operand::Zero());
    __ b(eq, &exit_with_exception);
    // Otherwise use return value as new stack pointer.
    __ mov(backtrack_stackpointer(), r0);
    // Restore saved registers and continue.
    SafeReturn();
  }

  if (exit_with_exception.is_linked()) {
    // If any of the code above needed to exit with an exception.
    __ bind(&exit_with_exception);
    // Exit with Result EXCEPTION(-1) to signal thrown exception.
    __ mov(r0, Operand(EXCEPTION));
    __ jmp(&return_r0);
  }

  if (fallback_label_.is_linked()) {
    __ bind(&fallback_label_);
    __ mov(r0, Operand(FALLBACK_TO_EXPERIMENTAL));
    __ jmp(&return_r0);
  }

  CodeDesc code_desc;
  masm_->GetCode(isolate(), &code_desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate(), code_desc, CodeKind::REGEXP)
          .set_self_reference(masm_->CodeObject())
          .set_empty_source_position_table()
          .Build();
  PROFILE(masm_->isolate(),
          RegExpCodeCreateEvent(Handle<AbstractCode>::cast(code), source));
  return Handle<HeapObject>::cast(code);
}


void RegExpMacroAssemblerARM::GoTo(Label* to) {
  BranchOrBacktrack(al, to);
}


void RegExpMacroAssemblerARM::IfRegisterGE(int reg,
                                           int comparand,
                                           Label* if_ge) {
  __ ldr(r0, register_location(reg));
  __ cmp(r0, Operand(comparand));
  BranchOrBacktrack(ge, if_ge);
}


void RegExpMacroAssemblerARM::IfRegisterLT(int reg,
                                           int comparand,
                                           Label* if_lt) {
  __ ldr(r0, register_location(reg));
  __ cmp(r0, Operand(comparand));
  BranchOrBacktrack(lt, if_lt);
}


void RegExpMacroAssemblerARM::IfRegisterEqPos(int reg,
                                              Label* if_eq) {
  __ ldr(r0, register_location(reg));
  __ cmp(r0, Operand(current_input_offset()));
  BranchOrBacktrack(eq, if_eq);
}


RegExpMacroAssembler::IrregexpImplementation
    RegExpMacroAssemblerARM::Implementation() {
  return kARMImplementation;
}


void RegExpMacroAssemblerARM::PopCurrentPosition() {
  Pop(current_input_offset());
}


void RegExpMacroAssemblerARM::PopRegister(int register_index) {
  Pop(r0);
  __ str(r0, register_location(register_index));
}


void RegExpMacroAssemblerARM::PushBacktrack(Label* label) {
  __ mov_label_offset(r0, label);
  Push(r0);
  CheckStackLimit();
}


void RegExpMacroAssemblerARM::PushCurrentPosition() {
  Push(current_input_offset());
}


void RegExpMacroAssemblerARM::PushRegister(int register_index,
                                           StackCheckFlag check_stack_limit) {
  __ ldr(r0, register_location(register_index));
  Push(r0);
  if (check_stack_limit) CheckStackLimit();
}


void RegExpMacroAssemblerARM::ReadCurrentPositionFromRegister(int reg) {
  __ ldr(current_input_offset(), register_location(reg));
}

void RegExpMacroAssemblerARM::WriteStackPointerToRegister(int reg) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ mov(r1, Operand(ref));
  __ ldr(r1, MemOperand(r1));
  __ sub(r0, backtrack_stackpointer(), r1);
  __ str(r0, register_location(reg));
}

void RegExpMacroAssemblerARM::ReadStackPointerFromRegister(int reg) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ mov(r0, Operand(ref));
  __ ldr(r0, MemOperand(r0));
  __ ldr(backtrack_stackpointer(), register_location(reg));
  __ add(backtrack_stackpointer(), backtrack_stackpointer(), r0);
}

void RegExpMacroAssemblerARM::SetCurrentPositionFromEnd(int by) {
  Label after_position;
  __ cmp(current_input_offset(), Operand(-by * char_size()));
  __ b(ge, &after_position);
  __ mov(current_input_offset(), Operand(-by * char_size()));
  // On RegExp code entry (where this operation is used), the character before
  // the current position is expected to be already loaded.
  // We have advanced the position, so it's safe to read backwards.
  LoadCurrentCharacterUnchecked(-1, 1);
  __ bind(&after_position);
}


void RegExpMacroAssemblerARM::SetRegister(int register_index, int to) {
  DCHECK(register_index >= num_saved_registers_);  // Reserved for positions!
  __ mov(r0, Operand(to));
  __ str(r0, register_location(register_index));
}


bool RegExpMacroAssemblerARM::Succeed() {
  __ jmp(&success_label_);
  return global();
}


void RegExpMacroAssemblerARM::WriteCurrentPositionToRegister(int reg,
                                                             int cp_offset) {
  if (cp_offset == 0) {
    __ str(current_input_offset(), register_location(reg));
  } else {
    __ add(r0, current_input_offset(), Operand(cp_offset * char_size()));
    __ str(r0, register_location(reg));
  }
}


void RegExpMacroAssemblerARM::ClearRegisters(int reg_from, int reg_to) {
  DCHECK(reg_from <= reg_to);
  __ ldr(r0, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
  for (int reg = reg_from; reg <= reg_to; reg++) {
    __ str(r0, register_location(reg));
  }
}

// Private methods:

void RegExpMacroAssemblerARM::CallCheckStackGuardState(Operand extra_space) {
  DCHECK(!isolate()->IsGeneratingEmbeddedBuiltins());
  DCHECK(!masm_->options().isolate_independent_code);

  __ PrepareCallCFunction(4);

  // Extra space for variables to consider in stack check.
  __ mov(kCArgRegs[3], extra_space);
  // RegExp code frame pointer.
  __ mov(kCArgRegs[2], frame_pointer());
  // InstructionStream of self.
  __ mov(kCArgRegs[1], Operand(masm_->CodeObject()));

  // We need to make room for the return address on the stack.
  int stack_alignment = base::OS::ActivationFrameAlignment();
  DCHECK(IsAligned(stack_alignment, kSystemPointerSize));
  __ AllocateStackSpace(stack_alignment);

  // r0 will point to the return address, placed by DirectCEntry.
  __ mov(r0, sp);

  ExternalReference stack_guard_check =
      ExternalReference::re_check_stack_guard_state();
  __ mov(ip, Operand(stack_guard_check));

  EmbeddedData d = EmbeddedData::FromBlob();
  Address entry = d.InstructionStartOf(Builtin::kDirectCEntry);
  __ mov(lr, Operand(entry, RelocInfo::OFF_HEAP_TARGET));
  __ Call(lr);

  // Drop the return address from the stack.
  __ add(sp, sp, Operand(stack_alignment));

  DCHECK_NE(0, stack_alignment);
  __ ldr(sp, MemOperand(sp, 0));

  __ mov(code_pointer(), Operand(masm_->CodeObject()));
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

int RegExpMacroAssemblerARM::CheckStackGuardState(Address* return_address,
                                                  Address raw_code,
                                                  Address re_frame,
                                                  uintptr_t extra_space) {
  Tagged<InstructionStream> re_code =
      InstructionStream::cast(Tagged<Object>(raw_code));
  return NativeRegExpMacroAssembler::CheckStackGuardState(
      frame_entry<Isolate*>(re_frame, kIsolateOffset),
      frame_entry<int>(re_frame, kStartIndexOffset),
      static_cast<RegExp::CallOrigin>(
          frame_entry<int>(re_frame, kDirectCallOffset)),
      return_address, re_code,
      frame_entry_address<Address>(re_frame, kInputStringOffset),
      frame_entry_address<const uint8_t*>(re_frame, kInputStartOffset),
      frame_entry_address<const uint8_t*>(re_frame, kInputEndOffset),
      extra_space);
}

MemOperand RegExpMacroAssemblerARM::register_location(int register_index) {
  DCHECK(register_index < (1<<30));
  if (num_registers_ <= register_index) {
    num_registers_ = register_index + 1;
  }
  return MemOperand(frame_pointer(),
                    kRegisterZeroOffset - register_index * kSystemPointerSize);
}


void RegExpMacroAssemblerARM::CheckPosition(int cp_offset,
                                            Label* on_outside_input) {
  if (cp_offset >= 0) {
    __ cmp(current_input_offset(), Operand(-cp_offset * char_size()));
    BranchOrBacktrack(ge, on_outside_input);
  } else {
    __ ldr(r1, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
    __ add(r0, current_input_offset(), Operand(cp_offset * char_size()));
    __ cmp(r0, r1);
    BranchOrBacktrack(le, on_outside_input);
  }
}


void RegExpMacroAssemblerARM::BranchOrBacktrack(Condition condition,
                                                Label* to) {
  if (condition == al) {  // Unconditional.
    if (to == nullptr) {
      Backtrack();
      return;
    }
    __ jmp(to);
    return;
  }
  if (to == nullptr) {
    __ b(condition, &backtrack_label_);
    return;
  }
  __ b(condition, to);
}


void RegExpMacroAssemblerARM::SafeCall(Label* to, Condition cond) {
  __ bl(to, cond);
}


void RegExpMacroAssemblerARM::SafeReturn() {
  __ pop(lr);
  __ add(pc, lr, Operand(masm_->CodeObject()));
}


void RegExpMacroAssemblerARM::SafeCallTarget(Label* name) {
  __ bind(name);
  __ sub(lr, lr, Operand(masm_->CodeObject()));
  __ push(lr);
}


void RegExpMacroAssemblerARM::Push(Register source) {
  DCHECK(source != backtrack_stackpointer());
  __ str(source,
         MemOperand(backtrack_stackpointer(), kSystemPointerSize, NegPreIndex));
}


void RegExpMacroAssemblerARM::Pop(Register target) {
  DCHECK(target != backtrack_stackpointer());
  __ ldr(target,
         MemOperand(backtrack_stackpointer(), kSystemPointerSize, PostIndex));
}

void RegExpMacroAssemblerARM::CallCFunctionFromIrregexpCode(
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

void RegExpMacroAssemblerARM::CheckPreemption() {
  // Check for preemption.
  ExternalReference stack_limit =
      ExternalReference::address_of_jslimit(isolate());
  __ mov(r0, Operand(stack_limit));
  __ ldr(r0, MemOperand(r0));
  __ cmp(sp, r0);
  SafeCall(&check_preempt_label_, ls);
}


void RegExpMacroAssemblerARM::CheckStackLimit() {
  ExternalReference stack_limit =
      ExternalReference::address_of_regexp_stack_limit_address(isolate());
  __ mov(r0, Operand(stack_limit));
  __ ldr(r0, MemOperand(r0));
  __ cmp(backtrack_stackpointer(), Operand(r0));
  SafeCall(&stack_overflow_label_, ls);
}


void RegExpMacroAssemblerARM::LoadCurrentCharacterUnchecked(int cp_offset,
                                                            int characters) {
  Register offset = current_input_offset();
  if (cp_offset != 0) {
    // r4 is not being used to store the capture start index at this point.
    __ add(r4, current_input_offset(), Operand(cp_offset * char_size()));
    offset = r4;
  }
  // The ldr, str, ldrh, strh instructions can do unaligned accesses, if the CPU
  // and the operating system running on the target allow it.
  // If unaligned load/stores are not supported then this function must only
  // be used to load a single character at a time.
  if (!CanReadUnaligned()) {
    DCHECK_EQ(1, characters);
  }

  if (mode_ == LATIN1) {
    if (characters == 4) {
      __ ldr(current_character(), MemOperand(end_of_input_address(), offset));
    } else if (characters == 2) {
      __ ldrh(current_character(), MemOperand(end_of_input_address(), offset));
    } else {
      DCHECK_EQ(1, characters);
      __ ldrb(current_character(), MemOperand(end_of_input_address(), offset));
    }
  } else {
    DCHECK(mode_ == UC16);
    if (characters == 2) {
      __ ldr(current_character(), MemOperand(end_of_input_address(), offset));
    } else {
      DCHECK_EQ(1, characters);
      __ ldrh(current_character(), MemOperand(end_of_input_address(), offset));
    }
  }
}


#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM
