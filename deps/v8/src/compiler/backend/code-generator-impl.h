// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_CODE_GENERATOR_IMPL_H_
#define V8_COMPILER_BACKEND_CODE_GENERATOR_IMPL_H_

#include "src/codegen/macro-assembler.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/linkage.h"
#include "src/compiler/opcodes.h"

namespace v8 {
namespace internal {
namespace compiler {

// Converts InstructionOperands from a given instruction to
// architecture-specific
// registers and operands after they have been assigned by the register
// allocator.
class InstructionOperandConverter {
 public:
  InstructionOperandConverter(CodeGenerator* gen, Instruction* instr)
      : gen_(gen), instr_(instr) {}

  // -- Instruction operand accesses with conversions --------------------------

  Register InputRegister(size_t index) const {
    return ToRegister(instr_->InputAt(index));
  }

  FloatRegister InputFloatRegister(size_t index) {
    return ToFloatRegister(instr_->InputAt(index));
  }

  DoubleRegister InputDoubleRegister(size_t index) {
    return ToDoubleRegister(instr_->InputAt(index));
  }

  Simd128Register InputSimd128Register(size_t index) {
    return ToSimd128Register(instr_->InputAt(index));
  }

  double InputDouble(size_t index) { return ToDouble(instr_->InputAt(index)); }

  float InputFloat32(size_t index) { return ToFloat32(instr_->InputAt(index)); }

  int32_t InputInt32(size_t index) {
    return ToConstant(instr_->InputAt(index)).ToInt32();
  }

  uint32_t InputUint32(size_t index) {
    return base::bit_cast<uint32_t>(InputInt32(index));
  }

  int64_t InputInt64(size_t index) {
    return ToConstant(instr_->InputAt(index)).ToInt64();
  }

  int8_t InputInt8(size_t index) {
    return static_cast<int8_t>(InputInt32(index));
  }

  uint8_t InputUint8(size_t index) {
    return base::bit_cast<uint8_t>(InputInt8(index));
  }

  int16_t InputInt16(size_t index) {
    return static_cast<int16_t>(InputInt32(index));
  }

  uint8_t InputInt3(size_t index) {
    return static_cast<uint8_t>(InputInt32(index) & 0x7);
  }

  uint8_t InputInt4(size_t index) {
    return static_cast<uint8_t>(InputInt32(index) & 0xF);
  }

  uint8_t InputInt5(size_t index) {
    return static_cast<uint8_t>(InputInt32(index) & 0x1F);
  }

  uint8_t InputInt6(size_t index) {
    return static_cast<uint8_t>(InputInt32(index) & 0x3F);
  }

  CodeEntrypointTag InputCodeEntrypointTag(size_t index) {
    // Tags are stored shifted to the right so they fit into 32-bits.
    uint64_t shifted_tag = InputUint32(index);
    return static_cast<CodeEntrypointTag>(shifted_tag
                                          << kCodeEntrypointTagShift);
  }

  ExternalReference InputExternalReference(size_t index) {
    return ToExternalReference(instr_->InputAt(index));
  }

  Handle<Code> InputCode(size_t index) {
    return ToCode(instr_->InputAt(index));
  }

  Label* InputLabel(size_t index) { return ToLabel(instr_->InputAt(index)); }

  RpoNumber InputRpo(size_t index) {
    return ToRpoNumber(instr_->InputAt(index));
  }

  Register OutputRegister(size_t index = 0) const {
    return ToRegister(instr_->OutputAt(index));
  }

  Register TempRegister(size_t index) {
    return ToRegister(instr_->TempAt(index));
  }

  FloatRegister OutputFloatRegister(size_t index = 0) {
    return ToFloatRegister(instr_->OutputAt(index));
  }

  DoubleRegister OutputDoubleRegister(size_t index = 0) {
    return ToDoubleRegister(instr_->OutputAt(index));
  }

  DoubleRegister TempDoubleRegister(size_t index) {
    return ToDoubleRegister(instr_->TempAt(index));
  }

  Simd128Register OutputSimd128Register() {
    return ToSimd128Register(instr_->Output());
  }

  Simd128Register TempSimd128Register(size_t index) {
    return ToSimd128Register(instr_->TempAt(index));
  }

#if defined(V8_TARGET_ARCH_X64)
  Simd256Register InputSimd256Register(size_t index) {
    return ToSimd256Register(instr_->InputAt(index));
  }

  Simd256Register OutputSimd256Register() {
    return ToSimd256Register(instr_->Output());
  }

  Simd256Register TempSimd256Register(size_t index) {
    return ToSimd256Register(instr_->TempAt(index));
  }
#endif

  // -- Conversions for operands -----------------------------------------------

  Label* ToLabel(InstructionOperand* op) {
    return gen_->GetLabel(ToRpoNumber(op));
  }

  RpoNumber ToRpoNumber(InstructionOperand* op) {
    return ToConstant(op).ToRpoNumber();
  }

  Register ToRegister(InstructionOperand* op) const {
    return LocationOperand::cast(op)->GetRegister();
  }

  FloatRegister ToFloatRegister(InstructionOperand* op) {
    return LocationOperand::cast(op)->GetFloatRegister();
  }

  DoubleRegister ToDoubleRegister(InstructionOperand* op) {
    return LocationOperand::cast(op)->GetDoubleRegister();
  }

  Simd128Register ToSimd128Register(InstructionOperand* op) {
    LocationOperand* loc_op = LocationOperand::cast(op);
#ifdef V8_TARGET_ARCH_X64
    if (loc_op->IsSimd256Register()) {
      return loc_op->GetSimd256RegisterAsSimd128();
    }
#endif
    return loc_op->GetSimd128Register();
  }

#if defined(V8_TARGET_ARCH_X64)
  Simd256Register ToSimd256Register(InstructionOperand* op) {
    return LocationOperand::cast(op)->GetSimd256Register();
  }
#endif

  Constant ToConstant(InstructionOperand* op) const {
    if (op->IsImmediate()) {
      return gen_->instructions()->GetImmediate(ImmediateOperand::cast(op));
    }
    return gen_->instructions()->GetConstant(
        ConstantOperand::cast(op)->virtual_register());
  }

  double ToDouble(InstructionOperand* op) {
    return ToConstant(op).ToFloat64().value();
  }

  float ToFloat32(InstructionOperand* op) { return ToConstant(op).ToFloat32(); }

  ExternalReference ToExternalReference(InstructionOperand* op) {
    return ToConstant(op).ToExternalReference();
  }

  Handle<Code> ToCode(InstructionOperand* op) {
    return ToConstant(op).ToCode();
  }

  const Frame* frame() const { return gen_->frame(); }
  FrameAccessState* frame_access_state() const {
    return gen_->frame_access_state();
  }
  Isolate* isolate() const { return gen_->isolate(); }
  Linkage* linkage() const { return gen_->linkage(); }

 protected:
  CodeGenerator* gen_;
  Instruction* instr_;
};

// Deoptimization exit.
class DeoptimizationExit : public ZoneObject {
 public:
  explicit DeoptimizationExit(SourcePosition pos, BytecodeOffset bailout_id,
                              int translation_id, int pc_offset,
                              DeoptimizeKind kind, DeoptimizeReason reason,
                              NodeId node_id)
      : deoptimization_id_(kNoDeoptIndex),
        pos_(pos),
        bailout_id_(bailout_id),
        translation_id_(translation_id),
        pc_offset_(pc_offset),
        kind_(kind),
        reason_(reason),
        node_id_(node_id),
        immediate_args_(nullptr),
        emitted_(false) {}

  bool has_deoptimization_id() const {
    return deoptimization_id_ != kNoDeoptIndex;
  }
  int deoptimization_id() const {
    DCHECK(has_deoptimization_id());
    return deoptimization_id_;
  }
  void set_deoptimization_id(int deoptimization_id) {
    deoptimization_id_ = deoptimization_id;
  }
  SourcePosition pos() const { return pos_; }
  // The label for the deoptimization call.
  Label* label() { return &label_; }
  // The label after the deoptimization check, which will resume execution.
  Label* continue_label() { return &continue_label_; }
  BytecodeOffset bailout_id() const { return bailout_id_; }
  int translation_id() const { return translation_id_; }
  int pc_offset() const { return pc_offset_; }
  DeoptimizeKind kind() const { return kind_; }
  DeoptimizeReason reason() const { return reason_; }
  NodeId node_id() const { return node_id_; }
  const ZoneVector<ImmediateOperand*>* immediate_args() const {
    return immediate_args_;
  }
  void set_immediate_args(ZoneVector<ImmediateOperand*>* immediate_args) {
    immediate_args_ = immediate_args;
  }
  // Returns whether the deopt exit has already been emitted. Most deopt exits
  // are emitted contiguously at the end of the code, but unconditional deopt
  // exits (kArchDeoptimize) may be inlined where they are encountered.
  bool emitted() const { return emitted_; }
  void set_emitted() { emitted_ = true; }

 private:
  static const int kNoDeoptIndex = kMaxInt16 + 1;
  int deoptimization_id_;
  const SourcePosition pos_;
  Label label_;
  Label continue_label_;
  const BytecodeOffset bailout_id_;
  const int translation_id_;
  const int pc_offset_;
  const DeoptimizeKind kind_;
  const DeoptimizeReason reason_;
  const NodeId node_id_;
  ZoneVector<ImmediateOperand*>* immediate_args_;
  bool emitted_;
};

// Generator for out-of-line code that is emitted after the main code is done.
class OutOfLineCode : public ZoneObject {
 public:
  explicit OutOfLineCode(CodeGenerator* gen);
  virtual ~OutOfLineCode();

  virtual void Generate() = 0;

  Label* entry() { return &entry_; }
  Label* exit() { return &exit_; }
  const Frame* frame() const { return frame_; }
  MacroAssembler* masm() { return masm_; }
  OutOfLineCode* next() const { return next_; }

 private:
  Label entry_;
  Label exit_;
  const Frame* const frame_;
  MacroAssembler* const masm_;
  OutOfLineCode* const next_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_CODE_GENERATOR_IMPL_H_
