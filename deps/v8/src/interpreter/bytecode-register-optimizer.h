// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_REGISTER_OPTIMIZER_H_
#define V8_INTERPRETER_BYTECODE_REGISTER_OPTIMIZER_H_

#include "src/ast/variables.h"
#include "src/base/compiler-specific.h"
#include "src/common/globals.h"
#include "src/interpreter/bytecode-generator.h"
#include "src/interpreter/bytecode-register-allocator.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace interpreter {

// An optimization stage for eliminating unnecessary transfers between
// registers. The bytecode generator uses temporary registers
// liberally for correctness and convenience and this stage removes
// transfers that are not required and preserves correctness.
class V8_EXPORT_PRIVATE BytecodeRegisterOptimizer final
    : public NON_EXPORTED_BASE(BytecodeRegisterAllocator::Observer),
      public NON_EXPORTED_BASE(ZoneObject) {
 public:
  using TypeHint = BytecodeGenerator::TypeHint;

  class BytecodeWriter {
   public:
    BytecodeWriter() = default;
    virtual ~BytecodeWriter() = default;
    BytecodeWriter(const BytecodeWriter&) = delete;
    BytecodeWriter& operator=(const BytecodeWriter&) = delete;

    // Called to emit a register transfer bytecode.
    virtual void EmitLdar(Register input) = 0;
    virtual void EmitStar(Register output) = 0;
    virtual void EmitMov(Register input, Register output) = 0;
  };

  BytecodeRegisterOptimizer(Zone* zone,
                            BytecodeRegisterAllocator* register_allocator,
                            int fixed_registers_count, int parameter_count,
                            BytecodeWriter* bytecode_writer);
  ~BytecodeRegisterOptimizer() override = default;
  BytecodeRegisterOptimizer(const BytecodeRegisterOptimizer&) = delete;
  BytecodeRegisterOptimizer& operator=(const BytecodeRegisterOptimizer&) =
      delete;

  // Perform explicit register transfer operations.
  void DoLdar(Register input) {
    // TODO(rmcilroy): Avoid treating accumulator loads as clobbering the
    // accumulator until the value is actually materialized in the accumulator.
    RegisterInfo* input_info = GetRegisterInfo(input);
    RegisterTransfer(input_info, accumulator_info_);
  }
  void DoStar(Register output) {
    RegisterInfo* output_info = GetRegisterInfo(output);
    RegisterTransfer(accumulator_info_, output_info);
  }
  void DoMov(Register input, Register output) {
    RegisterInfo* input_info = GetRegisterInfo(input);
    RegisterInfo* output_info = GetRegisterInfo(output);
    RegisterTransfer(input_info, output_info);
  }

  // Materialize all live registers and flush equivalence sets.
  void Flush();
  bool EnsureAllRegistersAreFlushed() const;

  // Prepares for |bytecode|.
  template <Bytecode bytecode, ImplicitRegisterUse implicit_register_use>
  V8_INLINE void PrepareForBytecode() {
    if (Bytecodes::IsJump(bytecode) || Bytecodes::IsSwitch(bytecode) ||
        bytecode == Bytecode::kDebugger ||
        bytecode == Bytecode::kSuspendGenerator ||
        bytecode == Bytecode::kResumeGenerator) {
      // All state must be flushed before emitting
      // - a jump bytecode (as the register equivalents at the jump target
      //   aren't known)
      // - a switch bytecode (as the register equivalents at the switch targets
      //   aren't known)
      // - a call to the debugger (as it can manipulate locals and parameters),
      // - a generator suspend (as this involves saving all registers).
      // - a generator register restore.
      Flush();
    }

    // Materialize the accumulator if it is read by the bytecode. The
    // accumulator is special and no other register can be materialized
    // in it's place.
    if (BytecodeOperands::ReadsAccumulator(implicit_register_use)) {
      Materialize(accumulator_info_);
    }

    // Materialize an equivalent to the accumulator if it will be
    // clobbered when the bytecode is dispatched.
    if (BytecodeOperands::WritesOrClobbersAccumulator(implicit_register_use)) {
      PrepareOutputRegister(accumulator_);
      DCHECK_EQ(GetTypeHint(accumulator_), TypeHint::kAny);
    }
  }

  // Prepares |reg| for being used as an output operand.
  void PrepareOutputRegister(Register reg);

  // Prepares registers in |reg_list| for being used as an output operand.
  void PrepareOutputRegisterList(RegisterList reg_list);

  // Returns an equivalent register to |reg| to be used as an input operand.
  Register GetInputRegister(Register reg);

  // Returns an equivalent register list to |reg_list| to be used as an input
  // operand.
  RegisterList GetInputRegisterList(RegisterList reg_list);

  // Maintain the map between Variable and Register.
  void SetVariableInRegister(Variable* var, Register reg);

  // Get the variable that might be in the reg. This is a variable value that
  // is preserved across flushes.
  Variable* GetPotentialVariableInRegister(Register reg);

  // Get the variable that might be in the accumulator. This is a variable value
  // that is preserved across flushes.
  Variable* GetPotentialVariableInAccumulator() {
    return GetPotentialVariableInRegister(accumulator_);
  }

  // Return true if the var is in the reg.
  bool IsVariableInRegister(Variable* var, Register reg);

  TypeHint GetTypeHint(Register reg);
  void SetTypeHintForAccumulator(TypeHint hint);
  void ResetTypeHintForAccumulator();
  bool IsAccumulatorReset();

  int maxiumum_register_index() const { return max_register_index_; }

 private:
  static const uint32_t kInvalidEquivalenceId;

  class RegisterInfo;

  // BytecodeRegisterAllocator::Observer interface.
  void RegisterAllocateEvent(Register reg) override;
  void RegisterListAllocateEvent(RegisterList reg_list) override;
  void RegisterListFreeEvent(RegisterList reg) override;
  void RegisterFreeEvent(Register reg) override;

  // Update internal state for register transfer from |input| to |output|
  void RegisterTransfer(RegisterInfo* input, RegisterInfo* output);

  // Emit a register transfer bytecode from |input| to |output|.
  void OutputRegisterTransfer(RegisterInfo* input, RegisterInfo* output);

  void CreateMaterializedEquivalent(RegisterInfo* info);
  RegisterInfo* GetMaterializedEquivalentNotAccumulator(RegisterInfo* info);
  void Materialize(RegisterInfo* info);
  void AddToEquivalenceSet(RegisterInfo* set_member,
                           RegisterInfo* non_set_member);

  void PushToRegistersNeedingFlush(RegisterInfo* reg);
  // Methods for finding and creating metadata for each register.
  RegisterInfo* GetRegisterInfo(Register reg) {
    size_t index = GetRegisterInfoTableIndex(reg);
    DCHECK_LT(index, register_info_table_.size());
    return register_info_table_[index];
  }
  RegisterInfo* GetOrCreateRegisterInfo(Register reg) {
    size_t index = GetRegisterInfoTableIndex(reg);
    return index < register_info_table_.size() ? register_info_table_[index]
                                               : NewRegisterInfo(reg);
  }
  RegisterInfo* NewRegisterInfo(Register reg) {
    size_t index = GetRegisterInfoTableIndex(reg);
    DCHECK_GE(index, register_info_table_.size());
    GrowRegisterMap(reg);
    return register_info_table_[index];
  }

  void GrowRegisterMap(Register reg);

  bool RegisterIsTemporary(Register reg) const {
    return reg >= temporary_base_;
  }

  bool RegisterIsObservable(Register reg) const {
    return reg != accumulator_ && !RegisterIsTemporary(reg);
  }

  static Register OperandToRegister(uint32_t operand) {
    return Register::FromOperand(static_cast<int32_t>(operand));
  }

  size_t GetRegisterInfoTableIndex(Register reg) const {
    return static_cast<size_t>(reg.index() + register_info_table_offset_);
  }

  Register RegisterFromRegisterInfoTableIndex(size_t index) const {
    return Register(static_cast<int>(index) - register_info_table_offset_);
  }

  uint32_t NextEquivalenceId() {
    equivalence_id_++;
    CHECK_NE(equivalence_id_, kInvalidEquivalenceId);
    return equivalence_id_;
  }

  void AllocateRegister(RegisterInfo* info);

  Zone* zone() { return zone_; }

  const Register accumulator_;
  RegisterInfo* accumulator_info_;
  const Register temporary_base_;
  int max_register_index_;

  // Direct mapping to register info.
  ZoneVector<RegisterInfo*> register_info_table_;
  int register_info_table_offset_;

  ZoneDeque<RegisterInfo*> registers_needing_flushed_;

  // Counter for equivalence sets identifiers.
  uint32_t equivalence_id_;

  BytecodeWriter* bytecode_writer_;
  bool flush_required_;
  Zone* zone_;
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_REGISTER_OPTIMIZER_H_
