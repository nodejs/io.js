// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_BYTECODE_GENERATOR_H_
#define V8_REGEXP_REGEXP_BYTECODE_GENERATOR_H_

#include "src/base/strings.h"
#include "src/codegen/label.h"
#include "src/regexp/regexp-macro-assembler.h"

namespace v8 {
namespace internal {

// An assembler/generator for the Irregexp byte code.
class V8_EXPORT_PRIVATE RegExpBytecodeGenerator : public RegExpMacroAssembler {
 public:
  // Create an assembler. Instructions and relocation information are emitted
  // into a buffer, with the instructions starting from the beginning and the
  // relocation information starting from the end of the buffer. See CodeDesc
  // for a detailed comment on the layout (globals.h).
  //
  // The assembler allocates and grows its own buffer, and buffer_size
  // determines the initial buffer size. The buffer is owned by the assembler
  // and deallocated upon destruction of the assembler.
  RegExpBytecodeGenerator(Isolate* isolate, Zone* zone);
  ~RegExpBytecodeGenerator() override;
  // The byte-code interpreter checks on each push anyway.
  int stack_limit_slack() override { return 1; }
  bool CanReadUnaligned() const override { return false; }
  void Bind(Label* label) override;
  void AdvanceCurrentPosition(int by) override;  // Signed cp change.
  void PopCurrentPosition() override;
  void PushCurrentPosition() override;
  void Backtrack() override;
  void GoTo(Label* label) override;
  void PushBacktrack(Label* label) override;
  bool Succeed() override;
  void Fail() override;
  void PopRegister(int register_index) override;
  void PushRegister(int register_index,
                    StackCheckFlag check_stack_limit) override;
  void AdvanceRegister(int reg, int by) override;  // r[reg] += by.
  void SetCurrentPositionFromEnd(int by) override;
  void SetRegister(int register_index, int to) override;
  void WriteCurrentPositionToRegister(int reg, int cp_offset) override;
  void ClearRegisters(int reg_from, int reg_to) override;
  void ReadCurrentPositionFromRegister(int reg) override;
  void WriteStackPointerToRegister(int reg) override;
  void ReadStackPointerFromRegister(int reg) override;
  void LoadCurrentCharacterImpl(int cp_offset, Label* on_end_of_input,
                                bool check_bounds, int characters,
                                int eats_at_least) override;
  void CheckCharacter(unsigned c, Label* on_equal) override;
  void CheckCharacterAfterAnd(unsigned c, unsigned mask,
                              Label* on_equal) override;
  void CheckCharacterGT(base::uc16 limit, Label* on_greater) override;
  void CheckCharacterLT(base::uc16 limit, Label* on_less) override;
  void CheckGreedyLoop(Label* on_tos_equals_current_position) override;
  void CheckAtStart(int cp_offset, Label* on_at_start) override;
  void CheckNotAtStart(int cp_offset, Label* on_not_at_start) override;
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
                                  Label* on_in_range) override {
    // Disabled in the interpreter, because 1) there is no constant pool that
    // could store the ByteArray pointer, 2) bytecode size limits are not as
    // restrictive as code (e.g. branch distances on arm), 3) bytecode for
    // large character classes is already quite compact.
    // TODO(jgruber): Consider using BytecodeArrays (with a constant pool)
    // instead of plain ByteArrays; then we could implement
    // CheckCharacterInRangeArray in the interpreter.
    return false;
  }
  bool CheckCharacterNotInRangeArray(const ZoneList<CharacterRange>* ranges,
                                     Label* on_not_in_range) override {
    return false;
  }
  void CheckBitInTable(Handle<ByteArray> table, Label* on_bit_set) override;
  void SkipUntilBitInTable(int cp_offset, Handle<ByteArray> table,
                           Handle<ByteArray> nibble_table,
                           int advance_by) override;
  void CheckNotBackReference(int start_reg, bool read_backward,
                             Label* on_no_match) override;
  void CheckNotBackReferenceIgnoreCase(int start_reg, bool read_backward,
                                       bool unicode,
                                       Label* on_no_match) override;
  void IfRegisterLT(int register_index, int comparand, Label* if_lt) override;
  void IfRegisterGE(int register_index, int comparand, Label* if_ge) override;
  void IfRegisterEqPos(int register_index, Label* if_eq) override;

  IrregexpImplementation Implementation() override;
  Handle<HeapObject> GetCode(Handle<String> source) override;

 private:
  void ExpandBuffer();

  // Code and bitmap emission.
  inline void EmitOrLink(Label* label);
  inline void Emit32(uint32_t x);
  inline void Emit16(uint32_t x);
  inline void Emit8(uint32_t x);
  inline void Emit(uint32_t bc, uint32_t arg);
  inline void Emit(uint32_t bc, int32_t arg);
  void EmitSkipTable(Handle<ByteArray> table);
  // Bytecode buffer.
  int length();
  void Copy(uint8_t* a);

  // The buffer into which code and relocation info are generated.
  static constexpr int kInitialBufferSize = 1024;
  ZoneVector<uint8_t> buffer_;

  // The program counter.
  int pc_;
  Label backtrack_;

  int advance_current_start_;
  int advance_current_offset_;
  int advance_current_end_;

  // Stores jump edges emitted for the bytecode (used by
  // RegExpBytecodePeepholeOptimization).
  // Key: jump source (offset in buffer_ where jump destination is stored).
  // Value: jump destination (offset in buffer_ to jump to).
  ZoneUnorderedMap<int, int> jump_edges_;

  Isolate* isolate_;

  static const int kInvalidPC = -1;

  DISALLOW_IMPLICIT_CONSTRUCTORS(RegExpBytecodeGenerator);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_BYTECODE_GENERATOR_H_
