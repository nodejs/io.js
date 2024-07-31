// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/init/v8.h"

#if V8_TARGET_ARCH_S390

#include "src/codegen/macro-assembler.h"
#include "src/codegen/s390/assembler-s390-inl.h"
#include "src/heap/factory.h"
#include "src/logging/log.h"
#include "src/objects/code-inl.h"
#include "src/regexp/regexp-stack.h"
#include "src/regexp/s390/regexp-macro-assembler-s390.h"
#include "src/snapshot/embedded/embedded-data-inl.h"

namespace v8 {
namespace internal {

/*
 * This assembler uses the following register assignment convention
 * - r6: Temporarily stores the index of capture start after a matching pass
 *        for a global regexp.
 * - r7: Pointer to current InstructionStream object including heap object tag.
 * - r8: Current position in input, as negative offset from end of string.
 *        Please notice that this is the byte offset, not the character offset!
 * - r9: Currently loaded character. Must be loaded using
 *        LoadCurrentCharacter before using any of the dispatch methods.
 * - r13: Points to tip of backtrack stack
 * - r10: End of input (points to byte after last character in input).
 * - r11: Frame pointer. Used to access arguments, local variables and
 *         RegExp registers.
 * - r12: IP register, used by assembler. Very volatile.
 * - r15/sp : Points to tip of C stack.
 *
 * The remaining registers are free for computations.
 * Each call to a public method should retain this convention.
 *
 * The stack will have the following structure
 *  - fp[112]  Address regexp     (address of the JSRegExp object; unused in
 *                                native code, passed to match signature of
 *                                the interpreter)
 *  - fp[108] Isolate* isolate   (address of the current isolate)
 *  - fp[104] direct_call        (if 1, direct call from JavaScript code,
 *                                if 0, call through the runtime system).
 *  - fp[100] stack_area_base    (high end of the memory area to use as
 *                                backtracking stack).
 *  - fp[96]  capture array size (may fit multiple sets of matches)
 *  - fp[0..96] zLinux ABI register saving area
 *  --- sp when called ---
 *  --- frame pointer ----
 *  - fp [-4] frame marker
 *  - fp [-8] isolate
 *  - fp[-12] direct_call        (if 1, direct call from JavaScript code,
 *                                if 0, call through the runtime system).
 *  - fp[-16] stack_area_base    (high end of the memory area to use as
 *                                backtracking stack).
 *  - fp[-20] capture array size (may fit multiple sets of matches)
 *  - fp[-24] int* capture_array (int[num_saved_registers_], for output).
 *  - fp[-28] end of input       (address of end of string).
 *  - fp[-32] start of input     (address of first character in string).
 *  - fp[-36] start index        (character index of start).
 *  - fp[-40] void* input_string (location of a handle containing the string).
 *  - fp[-44] success counter    (only for global regexps to count matches).
 *  - fp[-48] Offset of location before start of input (effectively character
 *            string start - 1). Used to initialize capture registers to a
 *            non-position.
 *  - fp[-52] At start (if 1, we are starting at the start of the
 *    string, otherwise 0)
 *  - fp[-56] register 0         (Only positions must be stored in the first
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
 *              uint8_t* stack_area_base,
 *              bool direct_call = false,
 *              Isolate* isolate,
 *              Address regexp);
 * The call is performed by NativeRegExpMacroAssembler::Execute()
 * (in regexp-macro-assembler.cc) via the GeneratedCode wrapper.
 */

#define __ ACCESS_MASM(masm_)

const int RegExpMacroAssemblerS390::kRegExpCodeSize;

RegExpMacroAssemblerS390::RegExpMacroAssemblerS390(Isolate* isolate, Zone* zone,
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
      exit_label_(),
      internal_failure_label_() {
  DCHECK_EQ(0, registers_to_save % 2);

  __ b(&entry_label_);  // We'll write the entry code later.
  // If the code gets too big or corrupted, an internal exception will be
  // raised, and we will exit right away.
  __ bind(&internal_failure_label_);
  __ mov(r2, Operand(FAILURE));
  __ Ret();
  __ bind(&start_label_);  // And then continue from here.
}

RegExpMacroAssemblerS390::~RegExpMacroAssemblerS390() {
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

int RegExpMacroAssemblerS390::stack_limit_slack() {
  return RegExpStack::kStackLimitSlack;
}

void RegExpMacroAssemblerS390::AdvanceCurrentPosition(int by) {
  if (by != 0) {
    __ AddS64(current_input_offset(), Operand(by * char_size()));
  }
}

void RegExpMacroAssemblerS390::AdvanceRegister(int reg, int by) {
  DCHECK_LE(0, reg);
  DCHECK_GT(num_registers_, reg);
  if (by != 0) {
    if (CpuFeatures::IsSupported(GENERAL_INSTR_EXT) && is_int8(by)) {
      __ agsi(register_location(reg), Operand(by));
    } else {
      __ LoadU64(r2, register_location(reg), r0);
      __ mov(r0, Operand(by));
      __ agr(r2, r0);
      __ StoreU64(r2, register_location(reg));
    }
  }
}

void RegExpMacroAssemblerS390::Backtrack() {
  CheckPreemption();
  if (has_backtrack_limit()) {
    Label next;
    __ LoadU64(r2, MemOperand(frame_pointer(), kBacktrackCountOffset), r0);
    __ AddS64(r2, r2, Operand(1));
    __ StoreU64(r2, MemOperand(frame_pointer(), kBacktrackCountOffset), r0);
    __ CmpU64(r2, Operand(backtrack_limit()));
    __ bne(&next);

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
  Pop(r2);
  __ AddS64(r2, code_pointer());
  __ b(r2);
}

void RegExpMacroAssemblerS390::Bind(Label* label) { __ bind(label); }

void RegExpMacroAssemblerS390::CheckCharacter(uint32_t c, Label* on_equal) {
  __ CmpU64(current_character(), Operand(c));
  BranchOrBacktrack(eq, on_equal);
}

void RegExpMacroAssemblerS390::CheckCharacterGT(base::uc16 limit,
                                                Label* on_greater) {
  __ CmpU64(current_character(), Operand(limit));
  BranchOrBacktrack(gt, on_greater);
}

void RegExpMacroAssemblerS390::CheckAtStart(int cp_offset, Label* on_at_start) {
  __ LoadU64(r3, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
  __ AddS64(r2, current_input_offset(),
            Operand(-char_size() + cp_offset * char_size()));
  __ CmpS64(r2, r3);
  BranchOrBacktrack(eq, on_at_start);
}

void RegExpMacroAssemblerS390::CheckNotAtStart(int cp_offset,
                                               Label* on_not_at_start) {
  __ LoadU64(r3, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
  __ AddS64(r2, current_input_offset(),
            Operand(-char_size() + cp_offset * char_size()));
  __ CmpS64(r2, r3);
  BranchOrBacktrack(ne, on_not_at_start);
}

void RegExpMacroAssemblerS390::CheckCharacterLT(base::uc16 limit,
                                                Label* on_less) {
  __ CmpU64(current_character(), Operand(limit));
  BranchOrBacktrack(lt, on_less);
}

void RegExpMacroAssemblerS390::CheckGreedyLoop(Label* on_equal) {
  Label backtrack_non_equal;
  __ CmpS64(current_input_offset(), MemOperand(backtrack_stackpointer(), 0));
  __ bne(&backtrack_non_equal);
  __ AddS64(backtrack_stackpointer(), Operand(kSystemPointerSize));

  BranchOrBacktrack(al, on_equal);
  __ bind(&backtrack_non_equal);
}

void RegExpMacroAssemblerS390::CheckNotBackReferenceIgnoreCase(
    int start_reg, bool read_backward, bool unicode, Label* on_no_match) {
  Label fallthrough;
  __ LoadU64(r2, register_location(start_reg));      // Index of start of
                                                     // capture
  __ LoadU64(r3, register_location(start_reg + 1));  // Index of end
  __ SubS64(r3, r3, r2);

  // At this point, the capture registers are either both set or both cleared.
  // If the capture length is zero, then the capture is either empty or cleared.
  // Fall through in both cases.
  __ beq(&fallthrough);

  // Check that there are enough characters left in the input.
  if (read_backward) {
    __ LoadU64(r5, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
    __ AddS64(r5, r5, r3);
    __ CmpS64(current_input_offset(), r5);
    BranchOrBacktrack(le, on_no_match);
  } else {
    __ AddS64(r0, r3, current_input_offset());
    BranchOrBacktrack(gt, on_no_match);
  }

  if (mode_ == LATIN1) {
    Label success;
    Label fail;
    Label loop_check;

    // r2 - offset of start of capture
    // r3 - length of capture
    __ AddS64(r2, end_of_input_address());
    __ AddS64(r4, current_input_offset(), end_of_input_address());
    if (read_backward) {
      __ SubS64(r4, r4, r3);  // Offset by length when matching backwards.
    }
    __ mov(r1, Operand::Zero());

    // r1 - Loop index
    // r2 - Address of start of capture.
    // r4 - Address of current input position.

    Label loop;
    __ bind(&loop);
    __ LoadU8(r5, MemOperand(r2, r1));
    __ LoadU8(r6, MemOperand(r4, r1));

    __ CmpS64(r6, r5);
    __ beq(&loop_check);

    // Mismatch, try case-insensitive match (converting letters to lower-case).
    __ Or(r5, Operand(0x20));  // Convert capture character to lower-case.
    __ Or(r6, Operand(0x20));  // Also convert input character.
    __ CmpS64(r6, r5);
    __ bne(&fail);
    __ SubS64(r5, Operand('a'));
    __ CmpU64(r5, Operand('z' - 'a'));       // Is r5 a lowercase letter?
    __ ble(&loop_check);                     // In range 'a'-'z'.
    // Latin-1: Check for values in range [224,254] but not 247.
    __ SubS64(r5, Operand(224 - 'a'));
    __ CmpU64(r5, Operand(254 - 224));
    __ bgt(&fail);                           // Weren't Latin-1 letters.
    __ CmpU64(r5, Operand(247 - 224));       // Check for 247.
    __ beq(&fail);

    __ bind(&loop_check);
    __ la(r1, MemOperand(r1, char_size()));
    __ CmpS64(r1, r3);
    __ blt(&loop);
    __ b(&success);

    __ bind(&fail);
    BranchOrBacktrack(al, on_no_match);

    __ bind(&success);
    // Compute new value of character position after the matched part.
    __ SubS64(current_input_offset(), r4, end_of_input_address());
    if (read_backward) {
      __ LoadU64(r2,
                 register_location(start_reg));  // Index of start of capture
      __ LoadU64(r3,
                 register_location(start_reg + 1));  // Index of end of capture
      __ AddS64(current_input_offset(), current_input_offset(), r2);
      __ SubS64(current_input_offset(), current_input_offset(), r3);
    }
    __ AddS64(current_input_offset(), r1);
  } else {
    DCHECK(mode_ == UC16);
    int argument_count = 4;
    __ PrepareCallCFunction(argument_count, r4);

    // r2 - offset of start of capture
    // r3 - length of capture

    // Put arguments into arguments registers.
    // Parameters are
    //   r2: Address byte_offset1 - Address captured substring's start.
    //   r3: Address byte_offset2 - Address of current character position.
    //   r4: size_t byte_length - length of capture in bytes(!)
    //   r5: Isolate* isolate.

    // Address of start of capture.
    __ AddS64(r2, end_of_input_address());
    // Length of capture.
    __ mov(r4, r3);
    // Save length in callee-save register for use on return.
    __ mov(r6, r3);
    // Address of current input position.
    __ AddS64(r3, current_input_offset(), end_of_input_address());
    if (read_backward) {
      __ SubS64(r3, r3, r6);
    }
// Isolate.
    __ mov(r5, Operand(ExternalReference::isolate_address(isolate())));

    {
      AllowExternalCallThatCantCauseGC scope(masm_.get());
      ExternalReference function =
          unicode
              ? ExternalReference::re_case_insensitive_compare_unicode()
              : ExternalReference::re_case_insensitive_compare_non_unicode();
      CallCFunctionFromIrregexpCode(function, argument_count);
    }

    // Check if function returned non-zero for success or zero for failure.
    __ CmpS64(r2, Operand::Zero());
    BranchOrBacktrack(eq, on_no_match);

    // On success, advance position by length of capture.
    if (read_backward) {
      __ SubS64(current_input_offset(), current_input_offset(), r6);
    } else {
      __ AddS64(current_input_offset(), current_input_offset(), r6);
    }
  }

  __ bind(&fallthrough);
}

void RegExpMacroAssemblerS390::CheckNotBackReference(int start_reg,
                                                     bool read_backward,
                                                     Label* on_no_match) {
  Label fallthrough;

  // Find length of back-referenced capture.
  __ LoadU64(r2, register_location(start_reg));
  __ LoadU64(r3, register_location(start_reg + 1));
  __ SubS64(r3, r3, r2);  // Length to check.

  // At this point, the capture registers are either both set or both cleared.
  // If the capture length is zero, then the capture is either empty or cleared.
  // Fall through in both cases.
  __ beq(&fallthrough);

  // Check that there are enough characters left in the input.
  if (read_backward) {
    __ LoadU64(r5, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
    __ AddS64(r5, r5, r3);
    __ CmpS64(current_input_offset(), r5);
    BranchOrBacktrack(le, on_no_match);
  } else {
    __ AddS64(r0, r3, current_input_offset());
    BranchOrBacktrack(gt, on_no_match, cr0);
  }

  // r2 - offset of start of capture
  // r3 - length of capture
  __ la(r2, MemOperand(r2, end_of_input_address()));
  __ la(r4, MemOperand(current_input_offset(), end_of_input_address()));
  if (read_backward) {
    __ SubS64(r4, r4, r3);  // Offset by length when matching backwards.
  }
  __ mov(r1, Operand::Zero());

  Label loop;
  __ bind(&loop);
  if (mode_ == LATIN1) {
    __ LoadU8(r5, MemOperand(r2, r1));
    __ LoadU8(r6, MemOperand(r4, r1));
  } else {
    DCHECK(mode_ == UC16);
    __ LoadU16(r5, MemOperand(r2, r1));
    __ LoadU16(r6, MemOperand(r4, r1));
  }
  __ la(r1, MemOperand(r1, char_size()));
  __ CmpS64(r5, r6);
  BranchOrBacktrack(ne, on_no_match);
  __ CmpS64(r1, r3);
  __ blt(&loop);

  // Move current character position to position after match.
  __ SubS64(current_input_offset(), r4, end_of_input_address());
  if (read_backward) {
    __ LoadU64(r2, register_location(start_reg));  // Index of start of capture
    __ LoadU64(r3,
               register_location(start_reg + 1));  // Index of end of capture
    __ AddS64(current_input_offset(), current_input_offset(), r2);
    __ SubS64(current_input_offset(), current_input_offset(), r3);
  }
  __ AddS64(current_input_offset(), r1);

  __ bind(&fallthrough);
}

void RegExpMacroAssemblerS390::CheckNotCharacter(unsigned c,
                                                 Label* on_not_equal) {
  __ CmpU64(current_character(), Operand(c));
  BranchOrBacktrack(ne, on_not_equal);
}

void RegExpMacroAssemblerS390::CheckCharacterAfterAnd(uint32_t c, uint32_t mask,
                                                      Label* on_equal) {
  __ AndP(r2, current_character(), Operand(mask));
  if (c != 0) {
    __ CmpU64(r2, Operand(c));
  }
  BranchOrBacktrack(eq, on_equal);
}

void RegExpMacroAssemblerS390::CheckNotCharacterAfterAnd(unsigned c,
                                                         unsigned mask,
                                                         Label* on_not_equal) {
  __ AndP(r2, current_character(), Operand(mask));
  if (c != 0) {
    __ CmpU64(r2, Operand(c));
  }
  BranchOrBacktrack(ne, on_not_equal);
}

void RegExpMacroAssemblerS390::CheckNotCharacterAfterMinusAnd(
    base::uc16 c, base::uc16 minus, base::uc16 mask, Label* on_not_equal) {
  DCHECK_GT(String::kMaxUtf16CodeUnit, minus);
  __ lay(r2, MemOperand(current_character(), -minus));
  __ And(r2, Operand(mask));
  if (c != 0) {
    __ CmpU64(r2, Operand(c));
  }
  BranchOrBacktrack(ne, on_not_equal);
}

void RegExpMacroAssemblerS390::CheckCharacterInRange(base::uc16 from,
                                                     base::uc16 to,
                                                     Label* on_in_range) {
  __ lay(r2, MemOperand(current_character(), -from));
  __ CmpU64(r2, Operand(to - from));
  BranchOrBacktrack(le, on_in_range);  // Unsigned lower-or-same condition.
}

void RegExpMacroAssemblerS390::CheckCharacterNotInRange(
    base::uc16 from, base::uc16 to, Label* on_not_in_range) {
  __ lay(r2, MemOperand(current_character(), -from));
  __ CmpU64(r2, Operand(to - from));
  BranchOrBacktrack(gt, on_not_in_range);  // Unsigned higher condition.
}

void RegExpMacroAssemblerS390::CallIsCharacterInRangeArray(
    const ZoneList<CharacterRange>* ranges) {
  static const int kNumArguments = 3;
  __ PrepareCallCFunction(kNumArguments, r0);

  __ mov(r2, current_character());
  __ mov(r3, Operand(GetOrAddRangeArray(ranges)));
  __ mov(r4, Operand(ExternalReference::isolate_address(isolate())));

  {
    // We have a frame (set up in GetCode), but the assembler doesn't know.
    FrameScope scope(masm_.get(), StackFrame::MANUAL);
    CallCFunctionFromIrregexpCode(
        ExternalReference::re_is_character_in_range_array(), kNumArguments);
  }

  __ mov(code_pointer(), Operand(masm_->CodeObject()));
}

bool RegExpMacroAssemblerS390::CheckCharacterInRangeArray(
    const ZoneList<CharacterRange>* ranges, Label* on_in_range) {
  CallIsCharacterInRangeArray(ranges);
  __ CmpS64(r2, Operand::Zero());
  BranchOrBacktrack(ne, on_in_range);
  return true;
}

bool RegExpMacroAssemblerS390::CheckCharacterNotInRangeArray(
    const ZoneList<CharacterRange>* ranges, Label* on_not_in_range) {
  CallIsCharacterInRangeArray(ranges);
  __ CmpS64(r2, Operand::Zero());
  BranchOrBacktrack(eq, on_not_in_range);
  return true;
}

void RegExpMacroAssemblerS390::CheckBitInTable(Handle<ByteArray> table,
                                               Label* on_bit_set) {
  __ mov(r2, Operand(table));
  Register index = current_character();
  if (mode_ != LATIN1 || kTableMask != String::kMaxOneByteCharCode) {
    __ AndP(r3, current_character(), Operand(kTableSize - 1));
    index = r3;
  }
  __ LoadU8(r2,
            MemOperand(r2, index, (ByteArray::kHeaderSize - kHeapObjectTag)));
  __ CmpS64(r2, Operand::Zero());
  BranchOrBacktrack(ne, on_bit_set);
}

bool RegExpMacroAssemblerS390::CheckSpecialClassRanges(
    StandardCharacterSet type, Label* on_no_match) {
  // Range checks (c in min..max) are generally implemented by an unsigned
  // (c - min) <= (max - min) check
  // TODO(jgruber): No custom implementation (yet): s(UC16), S(UC16).
  switch (type) {
    case StandardCharacterSet::kWhitespace:
      // Match space-characters.
      if (mode_ == LATIN1) {
        // One byte space characters are '\t'..'\r', ' ' and \u00a0.
        Label success;
        __ CmpS64(current_character(), Operand(' '));
        __ beq(&success);
        // Check range 0x09..0x0D.
        __ SubS64(r2, current_character(), Operand('\t'));
        __ CmpU64(r2, Operand('\r' - '\t'));
        __ ble(&success);
        // \u00a0 (NBSP).
        __ CmpU64(r2, Operand(0x00A0 - '\t'));
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
      __ SubS64(r2, current_character(), Operand('0'));
      __ CmpU64(r2, Operand('9' - '0'));
      BranchOrBacktrack(gt, on_no_match);
      return true;
    case StandardCharacterSet::kNotDigit:
      // Match non ASCII-digits
      __ SubS64(r2, current_character(), Operand('0'));
      __ CmpU64(r2, Operand('9' - '0'));
      BranchOrBacktrack(le, on_no_match);
      return true;
    case StandardCharacterSet::kNotLineTerminator: {
      // Match non-newlines (not 0x0A('\n'), 0x0D('\r'), 0x2028 and 0x2029)
      __ XorP(r2, current_character(), Operand(0x01));
      // See if current character is '\n'^1 or '\r'^1, i.e., 0x0B or 0x0C
      __ SubS64(r2, Operand(0x0B));
      __ CmpU64(r2, Operand(0x0C - 0x0B));
      BranchOrBacktrack(le, on_no_match);
      if (mode_ == UC16) {
        // Compare original value to 0x2028 and 0x2029, using the already
        // computed (current_char ^ 0x01 - 0x0B). I.e., check for
        // 0x201D (0x2028 - 0x0B) or 0x201E.
        __ SubS64(r2, Operand(0x2028 - 0x0B));
        __ CmpU64(r2, Operand(1));
        BranchOrBacktrack(le, on_no_match);
      }
      return true;
    }
    case StandardCharacterSet::kLineTerminator: {
      // Match newlines (0x0A('\n'), 0x0D('\r'), 0x2028 and 0x2029)
      __ XorP(r2, current_character(), Operand(0x01));
      // See if current character is '\n'^1 or '\r'^1, i.e., 0x0B or 0x0C
      __ SubS64(r2, Operand(0x0B));
      __ CmpU64(r2, Operand(0x0C - 0x0B));
      if (mode_ == LATIN1) {
        BranchOrBacktrack(gt, on_no_match);
      } else {
        Label done;
        __ ble(&done);
        // Compare original value to 0x2028 and 0x2029, using the already
        // computed (current_char ^ 0x01 - 0x0B). I.e., check for
        // 0x201D (0x2028 - 0x0B) or 0x201E.
        __ SubS64(r2, Operand(0x2028 - 0x0B));
        __ CmpU64(r2, Operand(1));
        BranchOrBacktrack(gt, on_no_match);
        __ bind(&done);
      }
      return true;
    }
    case StandardCharacterSet::kWord: {
      if (mode_ != LATIN1) {
        // Table is 1256 entries, so all LATIN1 characters can be tested.
        __ CmpS64(current_character(), Operand('z'));
        BranchOrBacktrack(gt, on_no_match);
      }
      ExternalReference map = ExternalReference::re_word_character_map();
      __ mov(r2, Operand(map));
      __ LoadU8(r2, MemOperand(r2, current_character()));
      __ CmpU64(r2, Operand::Zero());
      BranchOrBacktrack(eq, on_no_match);
      return true;
    }
    case StandardCharacterSet::kNotWord: {
      Label done;
      if (mode_ != LATIN1) {
        // Table is 256 entries, so all LATIN characters can be tested.
        __ CmpU64(current_character(), Operand('z'));
        __ bgt(&done);
      }
      ExternalReference map = ExternalReference::re_word_character_map();
      __ mov(r2, Operand(map));
      __ LoadU8(r2, MemOperand(r2, current_character()));
      __ CmpU64(r2, Operand::Zero());
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

void RegExpMacroAssemblerS390::Fail() {
  __ mov(r2, Operand(FAILURE));
  __ b(&exit_label_);
}

void RegExpMacroAssemblerS390::LoadRegExpStackPointerFromMemory(Register dst) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_stack_pointer(isolate());
  __ mov(dst, Operand(ref));
  __ LoadU64(dst, MemOperand(dst));
}

void RegExpMacroAssemblerS390::StoreRegExpStackPointerToMemory(
    Register src, Register scratch) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_stack_pointer(isolate());
  __ mov(scratch, Operand(ref));
  __ StoreU64(src, MemOperand(scratch));
}

void RegExpMacroAssemblerS390::PushRegExpBasePointer(Register stack_pointer,
                                                     Register scratch) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ mov(scratch, Operand(ref));
  __ LoadU64(scratch, MemOperand(scratch));
  __ SubS64(scratch, stack_pointer, scratch);
  __ StoreU64(scratch,
              MemOperand(frame_pointer(), kRegExpStackBasePointerOffset));
}

void RegExpMacroAssemblerS390::PopRegExpBasePointer(Register stack_pointer_out,
                                                    Register scratch) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ LoadU64(stack_pointer_out,
             MemOperand(frame_pointer(), kRegExpStackBasePointerOffset));
  __ mov(scratch, Operand(ref));
  __ LoadU64(scratch, MemOperand(scratch));
  __ AddS64(stack_pointer_out, stack_pointer_out, scratch);
  StoreRegExpStackPointerToMemory(stack_pointer_out, scratch);
}

Handle<HeapObject> RegExpMacroAssemblerS390::GetCode(Handle<String> source) {
  Label return_r2;

  // Finalize code - write the entry point code now we know how many
  // registers we need.

  // Entry code:
  __ bind(&entry_label_);

  // Tell the system that we have a stack frame.  Because the type
  // is MANUAL, no is generated.
  FrameScope scope(masm_.get(), StackFrame::MANUAL);

  // Ensure register assigments are consistent with callee save mask
  DCHECK(kRegExpCalleeSaved.has(r6));
  DCHECK(kRegExpCalleeSaved.has(code_pointer()));
  DCHECK(kRegExpCalleeSaved.has(current_input_offset()));
  DCHECK(kRegExpCalleeSaved.has(current_character()));
  DCHECK(kRegExpCalleeSaved.has(backtrack_stackpointer()));
  DCHECK(kRegExpCalleeSaved.has(end_of_input_address()));
  DCHECK(kRegExpCalleeSaved.has(frame_pointer()));

  // Emit code to start a new stack frame. In the following we push all
  // callee-save registers (these end up above the fp) and all register
  // arguments (these end up below the fp).
  //
  // zLinux ABI
  //    Incoming parameters:
  //          r2: input_string
  //          r3: start_index
  //          r4: start addr
  //          r5: end addr
  //          r6: capture output arrray
  //    Requires us to save the callee-preserved registers r6-r13
  //    General convention is to also save r14 (return addr) and
  //    sp/r15 as well in a single STM/STMG
  __ StoreMultipleP(r6, sp, MemOperand(sp, 6 * kSystemPointerSize));

  // Load stack parameters from caller stack frame
  __ LoadMultipleP(
      r7, r9, MemOperand(sp, kStackFrameExtraParamSlot * kSystemPointerSize));
  // r7 = capture array size
  // r8 = stack area base
  // r9 = direct call

  __ mov(frame_pointer(), sp);
  // Also push the frame marker.
  __ mov(r0, Operand(StackFrame::TypeToMarker(StackFrame::IRREGEXP)));
  __ push(r0);
  __ lay(sp, MemOperand(sp, -10 * kSystemPointerSize));

  static_assert(kSuccessfulCapturesOffset ==
                kInputStringOffset - kSystemPointerSize);
  __ mov(r1, Operand::Zero());  // success counter
  static_assert(kStringStartMinusOneOffset ==
                kSuccessfulCapturesOffset - kSystemPointerSize);
  __ mov(r0, r1);  // offset of location
  __ StoreMultipleP(r0, r9, MemOperand(sp, 0));
  static_assert(kBacktrackCountOffset ==
                kStringStartMinusOneOffset - kSystemPointerSize);
  __ Push(r1);  // The backtrack counter.
  static_assert(kRegExpStackBasePointerOffset ==
                kBacktrackCountOffset - kSystemPointerSize);
  __ push(r1);  // The regexp stack base ptr.

  // Initialize backtrack stack pointer. It must not be clobbered from here on.
  // Note the backtrack_stackpointer is callee-saved.
  static_assert(backtrack_stackpointer() == r13);
  LoadRegExpStackPointerFromMemory(backtrack_stackpointer());

  // Store the regexp base pointer - we'll later restore it / write it to
  // memory when returning from this irregexp code object.
  PushRegExpBasePointer(backtrack_stackpointer(), r3);

  {
    // Check if we have space on the stack for registers.
    Label stack_limit_hit, stack_ok;

    ExternalReference stack_limit =
        ExternalReference::address_of_jslimit(isolate());
    __ mov(r2, Operand(stack_limit));
    __ LoadU64(r2, MemOperand(r2));
    __ SubS64(r2, sp, r2);
    Operand extra_space_for_variables(num_registers_ * kSystemPointerSize);

    // Handle it if the stack pointer is already below the stack limit.
    __ ble(&stack_limit_hit);
    // Check if there is room for the variable number of registers above
    // the stack limit.
    __ CmpU64(r2, extra_space_for_variables);
    __ bge(&stack_ok);
    // Exit with OutOfMemory exception. There is not enough space on the stack
    // for our working registers.
    __ mov(r2, Operand(EXCEPTION));
    __ b(&return_r2);

    __ bind(&stack_limit_hit);
    CallCheckStackGuardState(r2, extra_space_for_variables);
    __ CmpS64(r2, Operand::Zero());
    // If returned value is non-zero, we exit with the returned value as result.
    __ bne(&return_r2);

    __ bind(&stack_ok);
  }

  // Allocate space on stack for registers.
  __ lay(sp, MemOperand(sp, (-num_registers_ * kSystemPointerSize)));
  // Load string end.
  __ LoadU64(end_of_input_address(),
             MemOperand(frame_pointer(), kInputEndOffset));
  // Load input start.
  __ LoadU64(r4, MemOperand(frame_pointer(), kInputStartOffset));
  // Find negative length (offset of start relative to end).
  __ SubS64(current_input_offset(), r4, end_of_input_address());
  __ LoadU64(r3, MemOperand(frame_pointer(), kStartIndexOffset));
  // Set r1 to address of char before start of the input string
  // (effectively string position -1).
  __ mov(r1, r4);
  __ SubS64(r1, current_input_offset(), Operand(char_size()));
  if (mode_ == UC16) {
    __ ShiftLeftU64(r0, r3, Operand(1));
    __ SubS64(r1, r1, r0);
  } else {
    __ SubS64(r1, r1, r3);
  }
  // Store this value in a local variable, for use when clearing
  // position registers.
  __ StoreU64(r1, MemOperand(frame_pointer(), kStringStartMinusOneOffset));

  // Initialize code pointer register
  __ mov(code_pointer(), Operand(masm_->CodeObject()));

  Label load_char_start_regexp;
  {
    Label start_regexp;
    // Load newline if index is at start, previous character otherwise.
    __ CmpS64(r3, Operand::Zero());
    __ bne(&load_char_start_regexp);
    __ mov(current_character(), Operand('\n'));
    __ b(&start_regexp);

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
      // One slot beyond address of register 0.
      __ lay(r3, MemOperand(frame_pointer(),
                            kRegisterZeroOffset + kSystemPointerSize));
      __ mov(r4, Operand(num_saved_registers_));
      Label init_loop;
      __ bind(&init_loop);
      __ StoreU64(r1, MemOperand(r3, -kSystemPointerSize));
      __ lay(r3, MemOperand(r3, -kSystemPointerSize));
      __ BranchOnCount(r4, &init_loop);
    } else {
      for (int i = 0; i < num_saved_registers_; i++) {
        __ StoreU64(r1, register_location(i));
      }
    }
  }

  __ b(&start_label_);

  // Exit code:
  if (success_label_.is_linked()) {
    // Save captures when successful.
    __ bind(&success_label_);
    if (num_saved_registers_ > 0) {
      // copy captures to output
      __ LoadU64(r0, MemOperand(frame_pointer(), kInputStartOffset));
      __ LoadU64(r2, MemOperand(frame_pointer(), kRegisterOutputOffset));
      __ LoadU64(r4, MemOperand(frame_pointer(), kStartIndexOffset));
      __ SubS64(r0, end_of_input_address(), r0);
      // r0 is length of input in bytes.
      if (mode_ == UC16) {
        __ ShiftRightU64(r0, r0, Operand(1));
      }
      // r0 is length of input in characters.
      __ AddS64(r0, r4);
      // r0 is length of string in characters.

      DCHECK_EQ(0, num_saved_registers_ % 2);
      // Always an even number of capture registers. This allows us to
      // unroll the loop once to add an operation between a load of a register
      // and the following use of that register.
      __ lay(r2, MemOperand(r2, num_saved_registers_ * kIntSize));
      for (int i = 0; i < num_saved_registers_;) {
        if ((false) && i < num_saved_registers_ - 4) {
          // TODO(john.yan): Can be optimized by SIMD instructions
          __ LoadMultipleP(r3, r6, register_location(i + 3));
          if (mode_ == UC16) {
            __ ShiftRightS64(r3, r3, Operand(1));
            __ ShiftRightS64(r4, r4, Operand(1));
            __ ShiftRightS64(r5, r5, Operand(1));
            __ ShiftRightS64(r6, r6, Operand(1));
          }
          __ AddS64(r3, r0);
          __ AddS64(r4, r0);
          __ AddS64(r5, r0);
          __ AddS64(r6, r0);
          __ StoreU32(
              r3, MemOperand(r2, -(num_saved_registers_ - i - 3) * kIntSize));
          __ StoreU32(
              r4, MemOperand(r2, -(num_saved_registers_ - i - 2) * kIntSize));
          __ StoreU32(
              r5, MemOperand(r2, -(num_saved_registers_ - i - 1) * kIntSize));
          __ StoreU32(r6,
                      MemOperand(r2, -(num_saved_registers_ - i) * kIntSize));
          i += 4;
        } else {
          __ LoadMultipleP(r3, r4, register_location(i + 1));
          if (mode_ == UC16) {
            __ ShiftRightS64(r3, r3, Operand(1));
            __ ShiftRightS64(r4, r4, Operand(1));
          }
          __ AddS64(r3, r0);
          __ AddS64(r4, r0);
          __ StoreU32(
              r3, MemOperand(r2, -(num_saved_registers_ - i - 1) * kIntSize));
          __ StoreU32(r4,
                      MemOperand(r2, -(num_saved_registers_ - i) * kIntSize));
          i += 2;
        }
      }
      if (global_with_zero_length_check()) {
        // Keep capture start in r6 for the zero-length check later.
        __ LoadU64(r6, register_location(0));
      }
    }

    if (global()) {
      // Restart matching if the regular expression is flagged as global.
      __ LoadU64(r2, MemOperand(frame_pointer(), kSuccessfulCapturesOffset));
      __ LoadU64(r3, MemOperand(frame_pointer(), kNumOutputRegistersOffset));
      __ LoadU64(r4, MemOperand(frame_pointer(), kRegisterOutputOffset));
      // Increment success counter.
      __ AddS64(r2, Operand(1));
      __ StoreU64(r2, MemOperand(frame_pointer(), kSuccessfulCapturesOffset));
      // Capture results have been stored, so the number of remaining global
      // output registers is reduced by the number of stored captures.
      __ SubS64(r3, Operand(num_saved_registers_));
      // Check whether we have enough room for another set of capture results.
      __ CmpS64(r3, Operand(num_saved_registers_));
      __ blt(&return_r2);

      __ StoreU64(r3, MemOperand(frame_pointer(), kNumOutputRegistersOffset));
      // Advance the location for output.
      __ AddS64(r4, Operand(num_saved_registers_ * kIntSize));
      __ StoreU64(r4, MemOperand(frame_pointer(), kRegisterOutputOffset));

      // Restore the original regexp stack pointer value (effectively, pop the
      // stored base pointer).
      PopRegExpBasePointer(backtrack_stackpointer(), r4);

      Label reload_string_start_minus_one;

      if (global_with_zero_length_check()) {
        // Special case for zero-length matches.
        // r6: capture start index
        __ CmpS64(current_input_offset(), r6);
        // Not a zero-length match, restart.
        __ bne(&reload_string_start_minus_one);
        // Offset from the end is zero if we already reached the end.
        __ CmpS64(current_input_offset(), Operand::Zero());
        __ beq(&exit_label_);
        // Advance current position after a zero-length match.
        Label advance;
        __ bind(&advance);
        __ AddS64(current_input_offset(), Operand((mode_ == UC16) ? 2 : 1));
        if (global_unicode()) CheckNotInSurrogatePair(0, &advance);
      }

      __ bind(&reload_string_start_minus_one);
      // Prepare r1 to initialize registers with its value in the next run.
      // Must be immediately before the jump to avoid clobbering.
      __ LoadU64(r1, MemOperand(frame_pointer(), kStringStartMinusOneOffset));

      __ b(&load_char_start_regexp);
    } else {
      __ mov(r2, Operand(SUCCESS));
    }
  }

  // Exit and return r2
  __ bind(&exit_label_);
  if (global()) {
    __ LoadU64(r2, MemOperand(frame_pointer(), kSuccessfulCapturesOffset));
  }

  __ bind(&return_r2);
  // Restore the original regexp stack pointer value (effectively, pop the
  // stored base pointer).
  PopRegExpBasePointer(backtrack_stackpointer(), r4);

  // Skip sp past regexp registers and local variables..
  __ mov(sp, frame_pointer());
  // Restore registers r6..r15.
  __ LoadMultipleP(r6, sp, MemOperand(sp, 6 * kSystemPointerSize));

  __ b(r14);

  // Backtrack code (branch target for conditional backtracks).
  if (backtrack_label_.is_linked()) {
    __ bind(&backtrack_label_);
    Backtrack();
  }

  Label exit_with_exception;

  // Preempt-code
  if (check_preempt_label_.is_linked()) {
    SafeCallTarget(&check_preempt_label_);

    StoreRegExpStackPointerToMemory(backtrack_stackpointer(), r3);

    CallCheckStackGuardState(r2);
    __ CmpS64(r2, Operand::Zero());
    // If returning non-zero, we should end execution with the given
    // result as return value.
    __ bne(&return_r2);

    LoadRegExpStackPointerFromMemory(backtrack_stackpointer());

    // String might have moved: Reload end of string from frame.
    __ LoadU64(end_of_input_address(),
               MemOperand(frame_pointer(), kInputEndOffset));
    SafeReturn();
  }

  // Backtrack stack overflow code.
  if (stack_overflow_label_.is_linked()) {
    SafeCallTarget(&stack_overflow_label_);
    // Reached if the backtrack-stack limit has been hit.

    // Call GrowStack(isolate).

    StoreRegExpStackPointerToMemory(backtrack_stackpointer(), r3);

    static constexpr int kNumArguments = 1;
    __ PrepareCallCFunction(kNumArguments, r2);
    __ mov(r2, Operand(ExternalReference::isolate_address(isolate())));
    ExternalReference grow_stack = ExternalReference::re_grow_stack();
    CallCFunctionFromIrregexpCode(grow_stack, kNumArguments);
    // If nullptr is returned, we have failed to grow the stack, and must exit
    // with a stack-overflow exception.
    __ CmpS64(r2, Operand::Zero());
    __ beq(&exit_with_exception);
    // Otherwise use return value as new stack pointer.
    __ mov(backtrack_stackpointer(), r2);
    // Restore saved registers and continue.
    SafeReturn();
  }

  if (exit_with_exception.is_linked()) {
    // If any of the code above needed to exit with an exception.
    __ bind(&exit_with_exception);
    // Exit with Result EXCEPTION(-1) to signal thrown exception.
    __ mov(r2, Operand(EXCEPTION));
    __ b(&return_r2);
  }

  if (fallback_label_.is_linked()) {
    __ bind(&fallback_label_);
    __ mov(r2, Operand(FALLBACK_TO_EXPERIMENTAL));
    __ b(&return_r2);
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

void RegExpMacroAssemblerS390::GoTo(Label* to) { BranchOrBacktrack(al, to); }

void RegExpMacroAssemblerS390::IfRegisterGE(int reg, int comparand,
                                            Label* if_ge) {
  __ LoadU64(r2, register_location(reg), r0);
  __ CmpS64(r2, Operand(comparand));
  BranchOrBacktrack(ge, if_ge);
}

void RegExpMacroAssemblerS390::IfRegisterLT(int reg, int comparand,
                                            Label* if_lt) {
  __ LoadU64(r2, register_location(reg), r0);
  __ CmpS64(r2, Operand(comparand));
  BranchOrBacktrack(lt, if_lt);
}

void RegExpMacroAssemblerS390::IfRegisterEqPos(int reg, Label* if_eq) {
  __ LoadU64(r2, register_location(reg), r0);
  __ CmpS64(r2, current_input_offset());
  BranchOrBacktrack(eq, if_eq);
}

RegExpMacroAssembler::IrregexpImplementation
RegExpMacroAssemblerS390::Implementation() {
  return kS390Implementation;
}

void RegExpMacroAssemblerS390::PopCurrentPosition() {
  Pop(current_input_offset());
}

void RegExpMacroAssemblerS390::PopRegister(int register_index) {
  Pop(r2);
  __ StoreU64(r2, register_location(register_index));
}

void RegExpMacroAssemblerS390::PushBacktrack(Label* label) {
  if (label->is_bound()) {
    int target = label->pos();
    __ mov(r2,
           Operand(target + InstructionStream::kHeaderSize - kHeapObjectTag));
  } else {
    masm_->load_label_offset(r2, label);
  }
  Push(r2);
  CheckStackLimit();
}

void RegExpMacroAssemblerS390::PushCurrentPosition() {
  Push(current_input_offset());
}

void RegExpMacroAssemblerS390::PushRegister(int register_index,
                                            StackCheckFlag check_stack_limit) {
  __ LoadU64(r2, register_location(register_index), r0);
  Push(r2);
  if (check_stack_limit) CheckStackLimit();
}

void RegExpMacroAssemblerS390::ReadCurrentPositionFromRegister(int reg) {
  __ LoadU64(current_input_offset(), register_location(reg), r0);
}

void RegExpMacroAssemblerS390::WriteStackPointerToRegister(int reg) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ mov(r3, Operand(ref));
  __ LoadU64(r3, MemOperand(r3));
  __ SubS64(r2, backtrack_stackpointer(), r3);
  __ StoreU64(r2, register_location(reg));
}

void RegExpMacroAssemblerS390::ReadStackPointerFromRegister(int reg) {
  ExternalReference ref =
      ExternalReference::address_of_regexp_stack_memory_top_address(isolate());
  __ mov(r2, Operand(ref));
  __ LoadU64(r2, MemOperand(r2));
  __ LoadU64(backtrack_stackpointer(), register_location(reg), r0);
  __ AddS64(backtrack_stackpointer(), backtrack_stackpointer(), r2);
}

void RegExpMacroAssemblerS390::SetCurrentPositionFromEnd(int by) {
  Label after_position;
  __ CmpS64(current_input_offset(), Operand(-by * char_size()));
  __ bge(&after_position);
  __ mov(current_input_offset(), Operand(-by * char_size()));
  // On RegExp code entry (where this operation is used), the character before
  // the current position is expected to be already loaded.
  // We have advanced the position, so it's safe to read backwards.
  LoadCurrentCharacterUnchecked(-1, 1);
  __ bind(&after_position);
}

void RegExpMacroAssemblerS390::SetRegister(int register_index, int to) {
  DCHECK(register_index >= num_saved_registers_);  // Reserved for positions!
  __ mov(r2, Operand(to));
  __ StoreU64(r2, register_location(register_index));
}

bool RegExpMacroAssemblerS390::Succeed() {
  __ b(&success_label_);
  return global();
}

void RegExpMacroAssemblerS390::WriteCurrentPositionToRegister(int reg,
                                                              int cp_offset) {
  if (cp_offset == 0) {
    __ StoreU64(current_input_offset(), register_location(reg));
  } else {
    __ AddS64(r2, current_input_offset(), Operand(cp_offset * char_size()));
    __ StoreU64(r2, register_location(reg));
  }
}

void RegExpMacroAssemblerS390::ClearRegisters(int reg_from, int reg_to) {
  DCHECK(reg_from <= reg_to);
  __ LoadU64(r2, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
  for (int reg = reg_from; reg <= reg_to; reg++) {
    __ StoreU64(r2, register_location(reg));
  }
}

// Private methods:

void RegExpMacroAssemblerS390::CallCheckStackGuardState(Register scratch,
                                                        Operand extra_space) {
  DCHECK(!isolate()->IsGeneratingEmbeddedBuiltins());
  DCHECK(!masm_->options().isolate_independent_code);

  static constexpr int num_arguments = 4;
  __ PrepareCallCFunction(num_arguments, scratch);
  // Extra space for variables to consider in stack check.
  __ mov(kCArgRegs[3], extra_space);
  // RegExp code frame pointer.
  __ mov(kCArgRegs[2], frame_pointer());
  // InstructionStream of self.
  __ mov(kCArgRegs[1], Operand(masm_->CodeObject()));
  // r2 becomes return address pointer.
  __ lay(r2, MemOperand(sp, kStackFrameRASlot * kSystemPointerSize));
  ExternalReference stack_guard_check =
      ExternalReference::re_check_stack_guard_state();

  __ mov(ip, Operand(stack_guard_check));
  __ StoreReturnAddressAndCall(ip);

  if (base::OS::ActivationFrameAlignment() > kSystemPointerSize) {
    __ LoadU64(
        sp, MemOperand(sp, (kNumRequiredStackFrameSlots * kSystemPointerSize)));
  } else {
    __ la(sp,
          MemOperand(sp, (kNumRequiredStackFrameSlots * kSystemPointerSize)));
  }

  __ mov(code_pointer(), Operand(masm_->CodeObject()));
}

// Helper function for reading a value out of a stack frame.
template <typename T>
static T& frame_entry(Address re_frame, int frame_offset) {
  DCHECK_EQ(kSystemPointerSize, sizeof(T));
#ifdef V8_TARGET_ARCH_S390X
  return reinterpret_cast<T&>(Memory<uint64_t>(re_frame + frame_offset));
#else
  return reinterpret_cast<T&>(Memory<uint32_t>(re_frame + frame_offset));
#endif
}

template <typename T>
static T* frame_entry_address(Address re_frame, int frame_offset) {
  return reinterpret_cast<T*>(re_frame + frame_offset);
}

int RegExpMacroAssemblerS390::CheckStackGuardState(Address* return_address,
                                                   Address raw_code,
                                                   Address re_frame,
                                                   uintptr_t extra_space) {
  Tagged<InstructionStream> re_code =
      InstructionStream::cast(Tagged<Object>(raw_code));
  return NativeRegExpMacroAssembler::CheckStackGuardState(
      frame_entry<Isolate*>(re_frame, kIsolateOffset),
      frame_entry<intptr_t>(re_frame, kStartIndexOffset),
      static_cast<RegExp::CallOrigin>(
          frame_entry<intptr_t>(re_frame, kDirectCallOffset)),
      return_address, re_code,
      frame_entry_address<Address>(re_frame, kInputStringOffset),
      frame_entry_address<const uint8_t*>(re_frame, kInputStartOffset),
      frame_entry_address<const uint8_t*>(re_frame, kInputEndOffset),
      extra_space);
}

MemOperand RegExpMacroAssemblerS390::register_location(int register_index) {
  DCHECK(register_index < (1 << 30));
  if (num_registers_ <= register_index) {
    num_registers_ = register_index + 1;
  }
  return MemOperand(frame_pointer(),
                    kRegisterZeroOffset - register_index * kSystemPointerSize);
}

void RegExpMacroAssemblerS390::CheckPosition(int cp_offset,
                                             Label* on_outside_input) {
  if (cp_offset >= 0) {
    __ CmpS64(current_input_offset(), Operand(-cp_offset * char_size()));
    BranchOrBacktrack(ge, on_outside_input);
  } else {
    __ LoadU64(r3, MemOperand(frame_pointer(), kStringStartMinusOneOffset));
    __ AddS64(r2, current_input_offset(), Operand(cp_offset * char_size()));
    __ CmpS64(r2, r3);
    BranchOrBacktrack(le, on_outside_input);
  }
}

void RegExpMacroAssemblerS390::BranchOrBacktrack(Condition condition, Label* to,
                                                 CRegister cr) {
  if (condition == al) {  // Unconditional.
    if (to == nullptr) {
      Backtrack();
      return;
    }
    __ b(to);
    return;
  }
  if (to == nullptr) {
    __ b(condition, &backtrack_label_);
    return;
  }
  __ b(condition, to);
}

void RegExpMacroAssemblerS390::SafeCall(Label* to, Condition cond,
                                        CRegister cr) {
  Label skip;
  __ b(NegateCondition(cond), &skip);
  __ b(r14, to);
  __ bind(&skip);
}

void RegExpMacroAssemblerS390::SafeReturn() {
  __ pop(r14);
  __ mov(ip, Operand(masm_->CodeObject()));
  __ AddS64(r14, ip);
  __ Ret();
}

void RegExpMacroAssemblerS390::SafeCallTarget(Label* name) {
  __ bind(name);
  __ CleanseP(r14);
  __ mov(r0, r14);
  __ mov(ip, Operand(masm_->CodeObject()));
  __ SubS64(r0, r0, ip);
  __ push(r0);
}

void RegExpMacroAssemblerS390::Push(Register source) {
  DCHECK(source != backtrack_stackpointer());
  __ lay(backtrack_stackpointer(),
         MemOperand(backtrack_stackpointer(), -kSystemPointerSize));
  __ StoreU64(source, MemOperand(backtrack_stackpointer()));
}

void RegExpMacroAssemblerS390::Pop(Register target) {
  DCHECK(target != backtrack_stackpointer());
  __ LoadU64(target, MemOperand(backtrack_stackpointer()));
  __ la(backtrack_stackpointer(),
        MemOperand(backtrack_stackpointer(), kSystemPointerSize));
}

void RegExpMacroAssemblerS390::CallCFunctionFromIrregexpCode(
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

void RegExpMacroAssemblerS390::CheckPreemption() {
  // Check for preemption.
  ExternalReference stack_limit =
      ExternalReference::address_of_jslimit(isolate());
  __ mov(r2, Operand(stack_limit));
  __ CmpU64(sp, MemOperand(r2));
  SafeCall(&check_preempt_label_, le);
}

void RegExpMacroAssemblerS390::CheckStackLimit() {
  ExternalReference stack_limit =
      ExternalReference::address_of_regexp_stack_limit_address(isolate());
  __ mov(r2, Operand(stack_limit));
  __ CmpU64(backtrack_stackpointer(), MemOperand(r2));
  SafeCall(&stack_overflow_label_, le);
}

void RegExpMacroAssemblerS390::CallCFunctionUsingStub(
    ExternalReference function, int num_arguments) {
  // Must pass all arguments in registers. The stub pushes on the stack.
  DCHECK_GE(8, num_arguments);
  __ mov(code_pointer(), Operand(function));
  Label ret;
  __ larl(r14, &ret);
  __ StoreU64(r14, MemOperand(sp, kStackFrameRASlot * kSystemPointerSize));
  __ b(code_pointer());
  __ bind(&ret);
  if (base::OS::ActivationFrameAlignment() > kSystemPointerSize) {
    __ LoadU64(
        sp, MemOperand(sp, (kNumRequiredStackFrameSlots * kSystemPointerSize)));
  } else {
    __ la(sp,
          MemOperand(sp, (kNumRequiredStackFrameSlots * kSystemPointerSize)));
  }
  __ mov(code_pointer(), Operand(masm_->CodeObject()));
}


void RegExpMacroAssemblerS390::LoadCurrentCharacterUnchecked(int cp_offset,
                                                             int characters) {
  if (mode_ == LATIN1) {
    // using load reverse for big-endian platforms
    if (characters == 4) {
      __ LoadU32LE(current_character(),
                   MemOperand(current_input_offset(), end_of_input_address(),
                              cp_offset * char_size()));
    } else if (characters == 2) {
      __ LoadU16LE(current_character(),
                   MemOperand(current_input_offset(), end_of_input_address(),
                              cp_offset * char_size()));
    } else {
      DCHECK_EQ(1, characters);
      __ LoadU8(current_character(),
                MemOperand(current_input_offset(), end_of_input_address(),
                           cp_offset * char_size()));
    }
  } else {
    DCHECK(mode_ == UC16);
    if (characters == 2) {
      __ LoadU32(current_character(),
                MemOperand(current_input_offset(), end_of_input_address(),
                           cp_offset * char_size()));
#if !V8_TARGET_LITTLE_ENDIAN
      // need to swap the order of the characters for big-endian platforms
      __ rll(current_character(), current_character(), Operand(16));
#endif
    } else {
      DCHECK_EQ(1, characters);
      __ LoadU16(
          current_character(),
                MemOperand(current_input_offset(), end_of_input_address(),
                           cp_offset * char_size()));
    }
  }
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_S390
