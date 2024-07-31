// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_ARM64_REGEXP_MACRO_ASSEMBLER_ARM64_H_
#define V8_REGEXP_ARM64_REGEXP_MACRO_ASSEMBLER_ARM64_H_

#include "src/base/strings.h"
#include "src/codegen/arm64/assembler-arm64.h"
#include "src/codegen/macro-assembler.h"
#include "src/regexp/regexp-macro-assembler.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE RegExpMacroAssemblerARM64
    : public NativeRegExpMacroAssembler {
 public:
  RegExpMacroAssemblerARM64(Isolate* isolate, Zone* zone, Mode mode,
                            int registers_to_save);
  ~RegExpMacroAssemblerARM64() override;
  void AbortedCodeGeneration() override;
  int stack_limit_slack() override;
  void AdvanceCurrentPosition(int by) override;
  void AdvanceRegister(int reg, int by) override;
  void Backtrack() override;
  void Bind(Label* label) override;
  void CheckAtStart(int cp_offset, Label* on_at_start) override;
  void CheckCharacter(unsigned c, Label* on_equal) override;
  void CheckCharacterAfterAnd(unsigned c, unsigned mask,
                              Label* on_equal) override;
  void CheckCharacterGT(base::uc16 limit, Label* on_greater) override;
  void CheckCharacterLT(base::uc16 limit, Label* on_less) override;
  void CheckCharacters(base::Vector<const base::uc16> str, int cp_offset,
                       Label* on_failure, bool check_end_of_string);
  // A "greedy loop" is a loop that is both greedy and with a simple
  // body. It has a particularly simple implementation.
  void CheckGreedyLoop(Label* on_tos_equals_current_position) override;
  void CheckNotAtStart(int cp_offset, Label* on_not_at_start) override;
  void CheckNotBackReference(int start_reg, bool read_backward,
                             Label* on_no_match) override;
  void CheckNotBackReferenceIgnoreCase(int start_reg, bool read_backward,
                                       bool unicode,
                                       Label* on_no_match) override;
  void CheckNotCharacter(unsigned c, Label* on_not_equal) override;
  void CheckNotCharacterAfterAnd(unsigned c, unsigned mask,
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
  void BindJumpTarget(Label* label = nullptr) override;
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
                                  Address re_frame, int start_offset,
                                  const uint8_t** input_start,
                                  const uint8_t** input_end,
                                  uintptr_t extra_space);

 private:
  static constexpr int kFramePointerOffset = 0;

  // Above the frame pointer - Stored registers and stack passed parameters.
  static constexpr int kReturnAddressOffset =
      kFramePointerOffset + kSystemPointerSize;
  // Callee-saved registers (x19-x28).
  static constexpr int kNumCalleeSavedRegisters = 10;
  static constexpr int kCalleeSavedRegistersOffset =
      kReturnAddressOffset + kSystemPointerSize;

  // Below the frame pointer - the stack frame type marker and locals.
  static constexpr int kFrameTypeOffset =
      kFramePointerOffset - kSystemPointerSize;
  static_assert(kFrameTypeOffset ==
                CommonFrameConstants::kContextOrFrameTypeOffset);
  static constexpr int kPaddingAfterFrameType = kSystemPointerSize;
  // Register parameters stored by setup code.
  static constexpr int kIsolateOffset =
      kFrameTypeOffset - kPaddingAfterFrameType - kSystemPointerSize;
  static constexpr int kDirectCallOffset = kIsolateOffset - kSystemPointerSize;
  // For the case of global regular expression, we have room to store at least
  // one set of capture results.  For the case of non-global regexp, we ignore
  // this value.
  static constexpr int kNumOutputRegistersOffset =
      kDirectCallOffset - kSystemPointerSize;
  static constexpr int kInputStringOffset =
      kNumOutputRegistersOffset - kSystemPointerSize;
  // When adding local variables remember to push space for them in
  // the frame in GetCode.
  static constexpr int kSuccessfulCapturesOffset =
      kInputStringOffset - kSystemPointerSize;
  static constexpr int kBacktrackCountOffset =
      kSuccessfulCapturesOffset - kSystemPointerSize;
  // Stores the initial value of the regexp stack pointer in a
  // position-independent representation (in case the regexp stack grows and
  // thus moves).
  static constexpr int kRegExpStackBasePointerOffset =
      kBacktrackCountOffset - kSystemPointerSize;
  // A padding slot to preserve alignment.
  static constexpr int kStackLocalPadding =
      kRegExpStackBasePointerOffset - kSystemPointerSize;
  static constexpr int kNumberOfStackLocals = 4;

  // First position register address on the stack. Following positions are
  // below it. A position is a 32 bit value.
  static constexpr int kFirstRegisterOnStackOffset =
      kStackLocalPadding - kWRegSize;
  // A capture is a 64 bit value holding two position.
  static constexpr int kFirstCaptureOnStackOffset =
      kStackLocalPadding - kXRegSize;

  static constexpr int kInitialBufferSize = 1024;

  // Registers x0 to x7 are used to store the first captures, they need to be
  // retained over calls to C++ code.
  void PushCachedRegisters();
  void PopCachedRegisters();

  // When initializing registers to a non-position value we can unroll
  // the loop. Set the limit of registers to unroll.
  static constexpr int kNumRegistersToUnroll = 16;

  // We are using x0 to x7 as a register cache. Each hardware register must
  // contain one capture, that is two 32 bit registers. We can cache at most
  // 16 registers.
  static constexpr int kNumCachedRegisters = 16;

  void CallCFunctionFromIrregexpCode(ExternalReference function,
                                     int num_arguments);

  // Check whether preemption has been requested.
  void CheckPreemption();

  // Check whether we are exceeding the stack limit on the backtrack stack.
  void CheckStackLimit();

  void CallCheckStackGuardState(Register scratch,
                                Operand extra_space = Operand(0));
  void CallIsCharacterInRangeArray(const ZoneList<CharacterRange>* ranges);

  // Location of a 32 bit position register.
  MemOperand register_location(int register_index);

  // Location of a 64 bit capture, combining two position registers.
  MemOperand capture_location(int register_index, Register scratch);

  // Register holding the current input position as negative offset from
  // the end of the string.
  static constexpr Register current_input_offset() { return w21; }

  // The register containing the current character after LoadCurrentCharacter.
  static constexpr Register current_character() { return w22; }

  // Register holding address of the end of the input string.
  static constexpr Register input_end() { return x25; }

  // Register holding address of the start of the input string.
  static constexpr Register input_start() { return x26; }

  // Register holding the offset from the start of the string where we should
  // start matching.
  static constexpr Register start_offset() { return w27; }

  // Pointer to the output array's first element.
  static constexpr Register output_array() { return x28; }

  // Register holding the frame address. Local variables, parameters and
  // regexp registers are addressed relative to this.
  static constexpr Register frame_pointer() { return fp; }

  // The register containing the backtrack stack top. Provides a meaningful
  // name to the register.
  static constexpr Register backtrack_stackpointer() { return x23; }

  // Register holding pointer to the current code object.
  static constexpr Register code_pointer() { return x20; }

  // Register holding the value used for clearing capture registers.
  static constexpr Register string_start_minus_one() { return w24; }
  // The top 32 bit of this register is used to store this value
  // twice. This is used for clearing more than one register at a time.
  static constexpr Register twice_non_position_value() { return x24; }

  // Byte size of chars in the string to match (decided by the Mode argument)
  int char_size() const { return static_cast<int>(mode_); }

  // Equivalent to a conditional branch to the label, unless the label
  // is nullptr, in which case it is a conditional Backtrack.
  void BranchOrBacktrack(Condition condition, Label* to);

  // Compares reg against immmediate before calling BranchOrBacktrack.
  // It makes use of the Cbz and Cbnz instructions.
  void CompareAndBranchOrBacktrack(Register reg,
                                   int immediate,
                                   Condition condition,
                                   Label* to);

  inline void CallIf(Label* to, Condition condition);

  // Save and restore the link register on the stack in a way that
  // is GC-safe.
  inline void SaveLinkRegister();
  inline void RestoreLinkRegister();

  // Pushes the value of a register on the backtrack stack. Decrements the
  // stack pointer by a word size and stores the register's value there.
  inline void Push(Register source);

  // Pops a value from the backtrack stack. Reads the word at the stack pointer
  // and increments it by a word size.
  inline void Pop(Register target);

  // This state indicates where the register actually is.
  enum RegisterState {
    STACKED,     // Resides in memory.
    CACHED_LSW,  // Least Significant Word of a 64 bit hardware register.
    CACHED_MSW   // Most Significant Word of a 64 bit hardware register.
  };

  RegisterState GetRegisterState(int register_index) {
    DCHECK_LE(0, register_index);
    if (register_index >= kNumCachedRegisters) {
      return STACKED;
    } else {
      if ((register_index % 2) == 0) {
        return CACHED_LSW;
      } else {
        return CACHED_MSW;
      }
    }
  }

  // Store helper that takes the state of the register into account.
  inline void StoreRegister(int register_index, Register source);

  // Returns a hardware W register that holds the value of the capture
  // register.
  //
  // This function will try to use an existing cache register (w0-w7) for the
  // result. Otherwise, it will load the value into maybe_result.
  //
  // If the returned register is anything other than maybe_result, calling code
  // must not write to it.
  inline Register GetRegister(int register_index, Register maybe_result);

  // Returns the harware register (x0-x7) holding the value of the capture
  // register.
  // This assumes that the state of the register is not STACKED.
  inline Register GetCachedRegister(int register_index);

  void LoadRegExpStackPointerFromMemory(Register dst);
  void StoreRegExpStackPointerToMemory(Register src, Register scratch);
  void PushRegExpBasePointer(Register stack_pointer, Register scratch);
  void PopRegExpBasePointer(Register stack_pointer_out, Register scratch);

  Isolate* isolate() const { return masm_->isolate(); }

  const std::unique_ptr<MacroAssembler> masm_;
  const NoRootArrayScope no_root_array_scope_;

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

#endif  // V8_REGEXP_ARM64_REGEXP_MACRO_ASSEMBLER_ARM64_H_
