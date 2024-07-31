// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_X64_REGEXP_MACRO_ASSEMBLER_X64_H_
#define V8_REGEXP_X64_REGEXP_MACRO_ASSEMBLER_X64_H_

#include "src/codegen/macro-assembler.h"
#include "src/regexp/regexp-macro-assembler.h"
#include "src/zone/zone-chunk-list.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE RegExpMacroAssemblerX64
    : public NativeRegExpMacroAssembler {
 public:
  RegExpMacroAssemblerX64(Isolate* isolate, Zone* zone, Mode mode,
                          int registers_to_save);
  ~RegExpMacroAssemblerX64() override;
  int stack_limit_slack() override;
  void AdvanceCurrentPosition(int by) override;
  void AdvanceRegister(int reg, int by) override;
  void Backtrack() override;
  void Bind(Label* label) override;
  void CheckAtStart(int cp_offset, Label* on_at_start) override;
  void CheckCharacter(uint32_t c, Label* on_equal) override;
  void CheckCharacterAfterAnd(uint32_t c, uint32_t mask,
                              Label* on_equal) override;
  void CheckCharacterGT(base::uc16 limit, Label* on_greater) override;
  void CheckCharacterLT(base::uc16 limit, Label* on_less) override;
  // A "greedy loop" is a loop that is both greedy and with a simple
  // body. It has a particularly simple implementation.
  void CheckGreedyLoop(Label* on_tos_equals_current_position) override;
  void CheckNotAtStart(int cp_offset, Label* on_not_at_start) override;
  void CheckNotBackReference(int start_reg, bool read_backward,
                             Label* on_no_match) override;
  void CheckNotBackReferenceIgnoreCase(int start_reg, bool read_backward,
                                       bool unicode,
                                       Label* on_no_match) override;
  void CheckNotCharacter(uint32_t c, Label* on_not_equal) override;
  void CheckNotCharacterAfterAnd(uint32_t c, uint32_t mask,
                                 Label* on_not_equal) override;
  void CheckNotCharacterAfterMinusAnd(base::uc16 c, base::uc16 minus,
                                      base::uc16 mask,
                                      Label* on_not_equal) override;
  void CheckCharacterInRange(base::uc16 from, base::uc16 to,
                             Label* on_in_range) override;
  void CheckCharacterNotInRange(base::uc16 from, base::uc16 to,
                                Label* on_not_in_range) override;
  bool CheckCharacterInRangeArray(const ZoneList<CharacterRange>* ranges,
                                  Label* on_in_range) override;
  bool CheckCharacterNotInRangeArray(const ZoneList<CharacterRange>* ranges,
                                     Label* on_not_in_range) override;
  void CheckBitInTable(Handle<ByteArray> table, Label* on_bit_set) override;

  // Checks whether the given offset from the current position is before
  // the end of the string.
  void CheckPosition(int cp_offset, Label* on_outside_input) override;
  bool CheckSpecialClassRanges(StandardCharacterSet type,
                               Label* on_no_match) override;

  void BindJumpTarget(Label* label) override;

  void Fail() override;
  Handle<HeapObject> GetCode(Handle<String> source) override;
  void GoTo(Label* label) override;
  void IfRegisterGE(int reg, int comparand, Label* if_ge) override;
  void IfRegisterLT(int reg, int comparand, Label* if_lt) override;
  void IfRegisterEqPos(int reg, Label* if_eq) override;
  IrregexpImplementation Implementation() override;
  void LoadCurrentCharacterUnchecked(int cp_offset,
                                     int character_count) override;
  void PopCurrentPosition() override;
  void PopRegister(int register_index) override;
  void PushBacktrack(Label* label) override;
  void PushCurrentPosition() override;
  void PushRegister(int register_index,
                    StackCheckFlag check_stack_limit) override;
  void ReadCurrentPositionFromRegister(int reg) override;
  void ReadStackPointerFromRegister(int reg) override;
  void SetCurrentPositionFromEnd(int by) override;
  void SetRegister(int register_index, int to) override;
  bool Succeed() override;
  void WriteCurrentPositionToRegister(int reg, int cp_offset) override;
  void ClearRegisters(int reg_from, int reg_to) override;
  void WriteStackPointerToRegister(int reg) override;

  // Called from RegExp if the stack-guard is triggered.
  // If the code object is relocated, the return address is fixed before
  // returning.
  // {raw_code} is an Address because this is called via ExternalReference.
  static int CheckStackGuardState(Address* return_address, Address raw_code,
                                  Address re_frame, uintptr_t extra_space);

 private:
  // Offsets from rbp of function parameters and stored registers.
  static constexpr int kFramePointerOffset = 0;
  // Above the frame pointer - function parameters and return address.
  static constexpr int kReturnAddressOffset =
      kFramePointerOffset + kSystemPointerSize;
  static constexpr int kFrameAlign = kReturnAddressOffset + kSystemPointerSize;
  // Below the frame pointer - the stack frame type marker and locals.
  static constexpr int kFrameTypeOffset =
      kFramePointerOffset - kSystemPointerSize;
  static_assert(kFrameTypeOffset ==
                CommonFrameConstants::kContextOrFrameTypeOffset);

#ifdef V8_TARGET_OS_WIN
  // Parameters (first four passed as registers, but with room on stack).
  // In Microsoft 64-bit Calling Convention, there is room on the callers
  // stack (before the return address) to spill parameter registers. We
  // use this space to store the register passed parameters.
  static constexpr int kInputStringOffset = kFrameAlign;
  // StartIndex is passed as 32 bit int.
  static constexpr int kStartIndexOffset =
      kInputStringOffset + kSystemPointerSize;
  static constexpr int kInputStartOffset =
      kStartIndexOffset + kSystemPointerSize;
  static constexpr int kInputEndOffset = kInputStartOffset + kSystemPointerSize;
  static constexpr int kRegisterOutputOffset =
      kInputEndOffset + kSystemPointerSize;
  // For the case of global regular expression, we have room to store at least
  // one set of capture results.  For the case of non-global regexp, we ignore
  // this value. NumOutputRegisters is passed as 32-bit value.  The upper
  // 32 bit of this 64-bit stack slot may contain garbage.
  static constexpr int kNumOutputRegistersOffset =
      kRegisterOutputOffset + kSystemPointerSize;
  // DirectCall is passed as 32 bit int (values 0 or 1).
  static constexpr int kDirectCallOffset =
      kNumOutputRegistersOffset + kSystemPointerSize;
  static constexpr int kIsolateOffset = kDirectCallOffset + kSystemPointerSize;
#else
  // In AMD64 ABI Calling Convention, the first six integer parameters
  // are passed as registers, and caller must allocate space on the stack
  // if it wants them stored. We push the parameters after the frame pointer.
  static constexpr int kInputStringOffset =
      kFrameTypeOffset - kSystemPointerSize;
  static constexpr int kStartIndexOffset =
      kInputStringOffset - kSystemPointerSize;
  static constexpr int kInputStartOffset =
      kStartIndexOffset - kSystemPointerSize;
  static constexpr int kInputEndOffset = kInputStartOffset - kSystemPointerSize;
  static constexpr int kRegisterOutputOffset =
      kInputEndOffset - kSystemPointerSize;
  // For the case of global regular expression, we have room to store at least
  // one set of capture results.  For the case of non-global regexp, we ignore
  // this value.
  static constexpr int kNumOutputRegistersOffset =
      kRegisterOutputOffset - kSystemPointerSize;

  static constexpr int kDirectCallOffset = kFrameAlign;
  static constexpr int kIsolateOffset = kDirectCallOffset + kSystemPointerSize;
#endif

  // We push callee-save registers that we use after the frame pointer (and
  // after the parameters).
#ifdef V8_TARGET_OS_WIN
  static constexpr int kBackupRsiOffset = kFrameTypeOffset - kSystemPointerSize;
  static constexpr int kBackupRdiOffset = kBackupRsiOffset - kSystemPointerSize;
  static constexpr int kBackupRbxOffset = kBackupRdiOffset - kSystemPointerSize;
  static constexpr int kNumCalleeSaveRegisters = 3;
  static constexpr int kLastCalleeSaveRegister = kBackupRbxOffset;
#else
  static constexpr int kBackupRbxOffset =
      kNumOutputRegistersOffset - kSystemPointerSize;
  static constexpr int kNumCalleeSaveRegisters = 1;
  static constexpr int kLastCalleeSaveRegister = kBackupRbxOffset;
#endif

  // When adding local variables remember to push space for them in
  // the frame in GetCode.
  static constexpr int kSuccessfulCapturesOffset =
      kLastCalleeSaveRegister - kSystemPointerSize;
  static constexpr int kStringStartMinusOneOffset =
      kSuccessfulCapturesOffset - kSystemPointerSize;
  static constexpr int kBacktrackCountOffset =
      kStringStartMinusOneOffset - kSystemPointerSize;
  // Stores the initial value of the regexp stack pointer in a
  // position-independent representation (in case the regexp stack grows and
  // thus moves).
  static constexpr int kRegExpStackBasePointerOffset =
      kBacktrackCountOffset - kSystemPointerSize;

  // First register address. Following registers are below it on the stack.
  static constexpr int kRegisterZeroOffset =
      kRegExpStackBasePointerOffset - kSystemPointerSize;

  // Initial size of code buffer.
  static constexpr int kRegExpCodeSize = 1024;

  void CallCFunctionFromIrregexpCode(ExternalReference function,
                                     int num_arguments);

  void PushCallerSavedRegisters();
  void PopCallerSavedRegisters();

  // Check whether preemption has been requested.
  void CheckPreemption();

  // Check whether we are exceeding the stack limit on the backtrack stack.
  void CheckStackLimit();

  void CallCheckStackGuardState(Immediate extra_space = Immediate(0));
  void CallIsCharacterInRangeArray(const ZoneList<CharacterRange>* ranges);

  // The rbp-relative location of a regexp register.
  Operand register_location(int register_index);

  // The register containing the current character after LoadCurrentCharacter.
  static constexpr Register current_character() { return rdx; }

  // The register containing the backtrack stack top. Provides a meaningful
  // name to the register.
  static constexpr Register backtrack_stackpointer() { return rcx; }

  // The registers containing a self pointer to this code's InstructionStream
  // object.
  static constexpr Register code_object_pointer() { return r8; }

  // Byte size of chars in the string to match (decided by the Mode argument)
  inline int char_size() { return static_cast<int>(mode_); }

  // Equivalent to an unconditional branch to the label, unless the label
  // is nullptr, in which case it is a Backtrack.
  void BranchOrBacktrack(Label* to);

  // Equivalent to a conditional branch to the label, unless the label
  // is nullptr, in which case it is a conditional Backtrack.
  void BranchOrBacktrack(Condition condition, Label* to);

  void MarkPositionForCodeRelativeFixup() {
    code_relative_fixup_positions_.push_back(masm_.pc_offset());
  }

  void FixupCodeRelativePositions();

  // Call and return internally in the generated code in a way that
  // is GC-safe (i.e., doesn't leave absolute code addresses on the stack)
  inline void SafeCall(Label* to);
  inline void SafeCallTarget(Label* label);
  inline void SafeReturn();

  // Pushes the value of a register on the backtrack stack. Decrements the
  // stack pointer (rcx) by a word size and stores the register's value there.
  inline void Push(Register source);

  // Pushes a value on the backtrack stack. Decrements the stack pointer (rcx)
  // by a word size and stores the value there.
  inline void Push(Immediate value);

  // Pushes the InstructionStream object relative offset of a label on the
  // backtrack stack (i.e., a backtrack target). Decrements the stack pointer
  // (rcx) by a word size and stores the value there.
  inline void Push(Label* label);

  // Pops a value from the backtrack stack. Reads the word at the stack pointer
  // (rcx) and increments it by a word size.
  inline void Pop(Register target);

  // Drops the top value from the backtrack stack without reading it.
  // Increments the stack pointer (rcx) by a word size.
  inline void Drop();

  void LoadRegExpStackPointerFromMemory(Register dst);
  void StoreRegExpStackPointerToMemory(Register src, Register scratch);
  void PushRegExpBasePointer(Register scratch_pointer, Register scratch);
  void PopRegExpBasePointer(Register scratch_pointer_out, Register scratch);

  inline void ReadPositionFromRegister(Register dst, int reg);

  Isolate* isolate() const { return masm_.isolate(); }

  MacroAssembler masm_;

  // On x64, there is no reason to keep the kRootRegister uninitialized; we
  // could easily use it by 1. initializing it and 2. storing/restoring it
  // as callee-save on entry/exit.
  // But: on other platforms, specifically ia32, it would be tricky to enable
  // the kRootRegister since it's currently used for other purposes. Thus, for
  // consistency, we also keep it uninitialized here.
  const NoRootArrayScope no_root_array_scope_;

  ZoneChunkList<int> code_relative_fixup_positions_;

  // Which mode to generate code for (LATIN1 or UC16).
  const Mode mode_;

  // One greater than maximal register index actually used.
  int num_registers_;

  // Number of registers to output at the end (the saved registers
  // are always 0..num_saved_registers_-1)
  const int num_saved_registers_;

  // Labels used internally.
  Label entry_label_;
  Label start_label_;
  Label success_label_;
  Label backtrack_label_;
  Label exit_label_;
  Label check_preempt_label_;
  Label stack_overflow_label_;
  Label fallback_label_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_X64_REGEXP_MACRO_ASSEMBLER_X64_H_
