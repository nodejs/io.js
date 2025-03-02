// Copyright 2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_MACRO_ASSEMBLER_TRACER_H_
#define V8_REGEXP_REGEXP_MACRO_ASSEMBLER_TRACER_H_

#include "src/base/strings.h"
#include "src/regexp/regexp-macro-assembler.h"

namespace v8 {
namespace internal {

// Decorator on a RegExpMacroAssembler that write all calls.
class RegExpMacroAssemblerTracer: public RegExpMacroAssembler {
 public:
  RegExpMacroAssemblerTracer(Isolate* isolate, RegExpMacroAssembler* assembler);
  ~RegExpMacroAssemblerTracer() override;
  void AbortedCodeGeneration() override;
  int stack_limit_slack_slot_count() override {
    return assembler_->stack_limit_slack_slot_count();
  }
  bool CanReadUnaligned() const override {
    return assembler_->CanReadUnaligned();
  }
  void AdvanceCurrentPosition(int by) override;    // Signed cp change.
  void AdvanceRegister(int reg, int by) override;  // r[reg] += by.
  void Backtrack() override;
  void Bind(Label* label) override;
  void CheckCharacter(unsigned c, Label* on_equal) override;
  void CheckCharacterAfterAnd(unsigned c, unsigned and_with,
                              Label* on_equal) override;
  void CheckCharacterGT(base::uc16 limit, Label* on_greater) override;
  void CheckCharacterLT(base::uc16 limit, Label* on_less) override;
  void CheckGreedyLoop(Label* on_tos_equals_current_position) override;
  void CheckAtStart(int cp_offset, Label* on_at_start) override;
  void CheckNotAtStart(int cp_offset, Label* on_not_at_start) override;
  void CheckNotBackReference(int start_reg, bool read_backward,
                             Label* on_no_match) override;
  void CheckNotBackReferenceIgnoreCase(int start_reg, bool read_backward,
                                       bool unicode,
                                       Label* on_no_match) override;
  void CheckNotCharacter(unsigned c, Label* on_not_equal) override;
  void CheckNotCharacterAfterAnd(unsigned c, unsigned and_with,
                                 Label* on_not_equal) override;
  void CheckNotCharacterAfterMinusAnd(base::uc16 c, base::uc16 minus,
                                      base::uc16 and_with,
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
  bool SkipUntilBitInTableUseSimd(int advance_by) override {
    return assembler_->SkipUntilBitInTableUseSimd(advance_by);
  }
  void SkipUntilBitInTable(int cp_offset, Handle<ByteArray> table,
                           Handle<ByteArray> nibble_table,
                           int advance_by) override;
  void CheckPosition(int cp_offset, Label* on_outside_input) override;
  bool CheckSpecialClassRanges(StandardCharacterSet type,
                               Label* on_no_match) override;
  void Fail() override;
  Handle<HeapObject> GetCode(Handle<String> source, RegExpFlags flags) override;
  void GoTo(Label* label) override;
  void IfRegisterGE(int reg, int comparand, Label* if_ge) override;
  void IfRegisterLT(int reg, int comparand, Label* if_lt) override;
  void IfRegisterEqPos(int reg, Label* if_eq) override;
  IrregexpImplementation Implementation() override;
  void LoadCurrentCharacterImpl(int cp_offset, Label* on_end_of_input,
                                bool check_bounds, int characters,
                                int eats_at_least) override;
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

 private:
  RegExpMacroAssembler* assembler_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_MACRO_ASSEMBLER_TRACER_H_
