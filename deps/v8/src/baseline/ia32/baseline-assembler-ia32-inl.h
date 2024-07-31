// Use of this source code is governed by a BSD-style license that can be
// Copyright 2021 the V8 project authors. All rights reserved.
// found in the LICENSE file.

#ifndef V8_BASELINE_IA32_BASELINE_ASSEMBLER_IA32_INL_H_
#define V8_BASELINE_IA32_BASELINE_ASSEMBLER_IA32_INL_H_

#include "src/baseline/baseline-assembler.h"
#include "src/codegen/ia32/register-ia32.h"
#include "src/codegen/interface-descriptors.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/literal-objects-inl.h"

namespace v8 {
namespace internal {
namespace baseline {

namespace detail {

static constexpr Register kScratchRegisters[] = {ecx, edx, esi, edi};
static constexpr int kNumScratchRegisters = arraysize(kScratchRegisters);

}  // namespace detail

class BaselineAssembler::ScratchRegisterScope {
 public:
  explicit ScratchRegisterScope(BaselineAssembler* assembler)
      : assembler_(assembler),
        prev_scope_(assembler->scratch_register_scope_),
        registers_used_(prev_scope_ == nullptr ? 0
                                               : prev_scope_->registers_used_) {
    assembler_->scratch_register_scope_ = this;
  }
  ~ScratchRegisterScope() { assembler_->scratch_register_scope_ = prev_scope_; }

  Register AcquireScratch() {
    DCHECK_LT(registers_used_, detail::kNumScratchRegisters);
    return detail::kScratchRegisters[registers_used_++];
  }

 private:
  BaselineAssembler* assembler_;
  ScratchRegisterScope* prev_scope_;
  int registers_used_;
};

namespace detail {

#define __ masm_->

#ifdef DEBUG
inline bool Clobbers(Register target, MemOperand op) {
  return op.is_reg(target);
}
#endif

}  // namespace detail

MemOperand BaselineAssembler::RegisterFrameOperand(
    interpreter::Register interpreter_register) {
  return MemOperand(ebp, interpreter_register.ToOperand() * kSystemPointerSize);
}
void BaselineAssembler::RegisterFrameAddress(
    interpreter::Register interpreter_register, Register rscratch) {
  return __ lea(rscratch, MemOperand(ebp, interpreter_register.ToOperand() *
                                              kSystemPointerSize));
}
MemOperand BaselineAssembler::FeedbackVectorOperand() {
  return MemOperand(ebp, BaselineFrameConstants::kFeedbackVectorFromFp);
}
MemOperand BaselineAssembler::FeedbackCellOperand() {
  return MemOperand(ebp, BaselineFrameConstants::kFeedbackCellFromFp);
}

void BaselineAssembler::Bind(Label* label) { __ bind(label); }

void BaselineAssembler::JumpTarget() {
  // NOP on ia32.
}

void BaselineAssembler::Jump(Label* target, Label::Distance distance) {
  __ jmp(target, distance);
}

void BaselineAssembler::JumpIfRoot(Register value, RootIndex index,
                                   Label* target, Label::Distance distance) {
  __ JumpIfRoot(value, index, target, distance);
}

void BaselineAssembler::JumpIfNotRoot(Register value, RootIndex index,
                                      Label* target, Label::Distance distance) {
  __ JumpIfNotRoot(value, index, target, distance);
}

void BaselineAssembler::JumpIfSmi(Register value, Label* target,
                                  Label::Distance distance) {
  __ JumpIfSmi(value, target, distance);
}

void BaselineAssembler::JumpIfImmediate(Condition cc, Register left, int right,
                                        Label* target,
                                        Label::Distance distance) {
  __ cmp(left, Immediate(right));
  __ j(cc, target, distance);
}

void BaselineAssembler::JumpIfNotSmi(Register value, Label* target,
                                     Label::Distance distance) {
  __ JumpIfNotSmi(value, target, distance);
}

void BaselineAssembler::TestAndBranch(Register value, int mask, Condition cc,
                                      Label* target, Label::Distance distance) {
  if ((mask & 0xff) == mask) {
    __ test_b(value, Immediate(mask));
  } else {
    __ test(value, Immediate(mask));
  }
  __ j(cc, target, distance);
}

void BaselineAssembler::JumpIf(Condition cc, Register lhs, const Operand& rhs,
                               Label* target, Label::Distance distance) {
  __ cmp(lhs, rhs);
  __ j(cc, target, distance);
}

void BaselineAssembler::JumpIfObjectTypeFast(Condition cc, Register object,
                                             InstanceType instance_type,
                                             Label* target,
                                             Label::Distance distance) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  JumpIfObjectType(cc, object, instance_type, scratch, target, distance);
}

void BaselineAssembler::JumpIfObjectType(Condition cc, Register object,
                                         InstanceType instance_type,
                                         Register map, Label* target,
                                         Label::Distance distance) {
  __ AssertNotSmi(object);
  __ CmpObjectType(object, instance_type, map);
  __ j(cc, target, distance);
}
void BaselineAssembler::JumpIfInstanceType(Condition cc, Register map,
                                           InstanceType instance_type,
                                           Label* target,
                                           Label::Distance distance) {
  if (v8_flags.debug_code) {
    __ movd(xmm0, eax);
    __ AssertNotSmi(map);
    __ CmpObjectType(map, MAP_TYPE, eax);
    __ Assert(equal, AbortReason::kUnexpectedValue);
    __ movd(eax, xmm0);
  }
  __ CmpInstanceType(map, instance_type);
  __ j(cc, target, distance);
}
void BaselineAssembler::JumpIfPointer(Condition cc, Register value,
                                      MemOperand operand, Label* target,
                                      Label::Distance distance) {
  JumpIf(cc, value, operand, target, distance);
}
void BaselineAssembler::JumpIfSmi(Condition cc, Register value, Tagged<Smi> smi,
                                  Label* target, Label::Distance distance) {
  if (smi.value() == 0) {
    __ test(value, value);
  } else {
    __ cmp(value, Immediate(smi));
  }
  __ j(cc, target, distance);
}
void BaselineAssembler::JumpIfSmi(Condition cc, Register lhs, Register rhs,
                                  Label* target, Label::Distance distance) {
  __ AssertSmi(lhs);
  __ AssertSmi(rhs);
  __ cmp(lhs, rhs);
  __ j(cc, target, distance);
}
void BaselineAssembler::JumpIfTagged(Condition cc, Register value,
                                     MemOperand operand, Label* target,
                                     Label::Distance distance) {
  __ cmp(operand, value);
  __ j(cc, target, distance);
}
void BaselineAssembler::JumpIfTagged(Condition cc, MemOperand operand,
                                     Register value, Label* target,
                                     Label::Distance distance) {
  __ cmp(operand, value);
  __ j(cc, target, distance);
}
void BaselineAssembler::JumpIfByte(Condition cc, Register value, int32_t byte,
                                   Label* target, Label::Distance distance) {
  __ cmpb(value, Immediate(byte));
  __ j(cc, target, distance);
}
void BaselineAssembler::Move(interpreter::Register output, Register source) {
  return __ mov(RegisterFrameOperand(output), source);
}
void BaselineAssembler::Move(Register output, Tagged<TaggedIndex> value) {
  __ Move(output, Immediate(value.ptr()));
}
void BaselineAssembler::Move(MemOperand output, Register source) {
  __ mov(output, source);
}
void BaselineAssembler::Move(Register output, ExternalReference reference) {
  __ Move(output, Immediate(reference));
}
void BaselineAssembler::Move(Register output, Handle<HeapObject> value) {
  __ Move(output, value);
}
void BaselineAssembler::Move(Register output, int32_t value) {
  __ Move(output, Immediate(value));
}
void BaselineAssembler::MoveMaybeSmi(Register output, Register source) {
  __ mov(output, source);
}
void BaselineAssembler::MoveSmi(Register output, Register source) {
  __ mov(output, source);
}

namespace detail {
inline void PushSingle(MacroAssembler* masm, RootIndex source) {
  masm->PushRoot(source);
}
inline void PushSingle(MacroAssembler* masm, Register reg) { masm->Push(reg); }
inline void PushSingle(MacroAssembler* masm, Tagged<TaggedIndex> value) {
  masm->Push(Immediate(value.ptr()));
}
inline void PushSingle(MacroAssembler* masm, Tagged<Smi> value) {
  masm->Push(value);
}
inline void PushSingle(MacroAssembler* masm, Handle<HeapObject> object) {
  masm->Push(object);
}
inline void PushSingle(MacroAssembler* masm, int32_t immediate) {
  masm->Push(Immediate(immediate));
}
inline void PushSingle(MacroAssembler* masm, MemOperand operand) {
  masm->Push(operand);
}
inline void PushSingle(MacroAssembler* masm, interpreter::Register source) {
  return PushSingle(masm, BaselineAssembler::RegisterFrameOperand(source));
}

template <typename Arg>
struct PushHelper {
  static int Push(BaselineAssembler* basm, Arg arg) {
    PushSingle(basm->masm(), arg);
    return 1;
  }
  static int PushReverse(BaselineAssembler* basm, Arg arg) {
    return Push(basm, arg);
  }
};

template <>
struct PushHelper<interpreter::RegisterList> {
  static int Push(BaselineAssembler* basm, interpreter::RegisterList list) {
    for (int reg_index = 0; reg_index < list.register_count(); ++reg_index) {
      PushSingle(basm->masm(), list[reg_index]);
    }
    return list.register_count();
  }
  static int PushReverse(BaselineAssembler* basm,
                         interpreter::RegisterList list) {
    for (int reg_index = list.register_count() - 1; reg_index >= 0;
         --reg_index) {
      PushSingle(basm->masm(), list[reg_index]);
    }
    return list.register_count();
  }
};

template <typename... Args>
struct PushAllHelper;
template <>
struct PushAllHelper<> {
  static int Push(BaselineAssembler* masm) { return 0; }
  static int PushReverse(BaselineAssembler* masm) { return 0; }
};
template <typename Arg, typename... Args>
struct PushAllHelper<Arg, Args...> {
  static int Push(BaselineAssembler* masm, Arg arg, Args... args) {
    int nargs = PushHelper<Arg>::Push(masm, arg);
    return nargs + PushAllHelper<Args...>::Push(masm, args...);
  }
  static int PushReverse(BaselineAssembler* masm, Arg arg, Args... args) {
    int nargs = PushAllHelper<Args...>::PushReverse(masm, args...);
    return nargs + PushHelper<Arg>::PushReverse(masm, arg);
  }
};

}  // namespace detail

template <typename... T>
int BaselineAssembler::Push(T... vals) {
  return detail::PushAllHelper<T...>::Push(this, vals...);
}

template <typename... T>
void BaselineAssembler::PushReverse(T... vals) {
  detail::PushAllHelper<T...>::PushReverse(this, vals...);
}

template <typename... T>
void BaselineAssembler::Pop(T... registers) {
  (__ Pop(registers), ...);
}

void BaselineAssembler::LoadTaggedField(Register output, Register source,
                                        int offset) {
  __ mov(output, FieldOperand(source, offset));
}

void BaselineAssembler::LoadTaggedSignedField(Register output, Register source,
                                              int offset) {
  __ mov(output, FieldOperand(source, offset));
}

void BaselineAssembler::LoadTaggedSignedFieldAndUntag(Register output,
                                                      Register source,
                                                      int offset) {
  LoadTaggedSignedField(output, source, offset);
  SmiUntag(output);
}

void BaselineAssembler::LoadWord16FieldZeroExtend(Register output,
                                                  Register source, int offset) {
  __ movzx_w(output, FieldOperand(source, offset));
}

void BaselineAssembler::LoadWord8Field(Register output, Register source,
                                       int offset) {
  __ mov_b(output, FieldOperand(source, offset));
}

void BaselineAssembler::StoreTaggedSignedField(Register target, int offset,
                                               Tagged<Smi> value) {
  __ mov(FieldOperand(target, offset), Immediate(value));
}

void BaselineAssembler::StoreTaggedFieldWithWriteBarrier(Register target,
                                                         int offset,
                                                         Register value) {
  ASM_CODE_COMMENT(masm_);
  BaselineAssembler::ScratchRegisterScope scratch_scope(this);
  Register scratch = scratch_scope.AcquireScratch();
  DCHECK(!AreAliased(scratch, target, value));
  __ mov(FieldOperand(target, offset), value);
  __ RecordWriteField(target, offset, value, scratch, SaveFPRegsMode::kIgnore);
}

void BaselineAssembler::StoreTaggedFieldNoWriteBarrier(Register target,
                                                       int offset,
                                                       Register value) {
  DCHECK(!AreAliased(target, value));
  __ mov(FieldOperand(target, offset), value);
}

void BaselineAssembler::TryLoadOptimizedOsrCode(Register scratch_and_result,
                                                Register feedback_vector,
                                                FeedbackSlot slot,
                                                Label* on_result,
                                                Label::Distance distance) {
  Label fallthrough;
  LoadTaggedField(scratch_and_result, feedback_vector,
                  FeedbackVector::OffsetOfElementAt(slot.ToInt()));
  __ LoadWeakValue(scratch_and_result, &fallthrough);

  // Is it marked_for_deoptimization? If yes, clear the slot.
  {
    ScratchRegisterScope temps(this);

    // The entry references a CodeWrapper object. Unwrap it now.
    __ mov(scratch_and_result,
           FieldOperand(scratch_and_result, CodeWrapper::kCodeOffset));

    __ TestCodeIsMarkedForDeoptimization(scratch_and_result);
    __ j(equal, on_result, distance);
    __ mov(FieldOperand(feedback_vector,
                        FeedbackVector::OffsetOfElementAt(slot.ToInt())),
           __ ClearedValue());
  }

  __ bind(&fallthrough);
  __ Move(scratch_and_result, 0);
}

void BaselineAssembler::AddToInterruptBudgetAndJumpIfNotExceeded(
    int32_t weight, Label* skip_interrupt_label) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope scratch_scope(this);
  Register feedback_cell = scratch_scope.AcquireScratch();
  LoadFeedbackCell(feedback_cell);
  __ add(FieldOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset),
         Immediate(weight));
  if (skip_interrupt_label) {
    DCHECK_LT(weight, 0);
    __ j(greater_equal, skip_interrupt_label);
  }
}

void BaselineAssembler::AddToInterruptBudgetAndJumpIfNotExceeded(
    Register weight, Label* skip_interrupt_label) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope scratch_scope(this);
  Register feedback_cell = scratch_scope.AcquireScratch();
  DCHECK(!AreAliased(feedback_cell, weight));
  LoadFeedbackCell(feedback_cell);
  __ add(FieldOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset),
         weight);
  if (skip_interrupt_label) __ j(greater_equal, skip_interrupt_label);
}

void BaselineAssembler::LdaContextSlot(Register context, uint32_t index,
                                       uint32_t depth) {
  for (; depth > 0; --depth) {
    LoadTaggedField(context, context, Context::kPreviousOffset);
  }
  LoadTaggedField(kInterpreterAccumulatorRegister, context,
                  Context::OffsetOfElementAt(index));
}

void BaselineAssembler::StaContextSlot(Register context, Register value,
                                       uint32_t index, uint32_t depth) {
  for (; depth > 0; --depth) {
    LoadTaggedField(context, context, Context::kPreviousOffset);
  }
  StoreTaggedFieldWithWriteBarrier(context, Context::OffsetOfElementAt(index),
                                   value);
}

void BaselineAssembler::LdaModuleVariable(Register context, int cell_index,
                                          uint32_t depth) {
  for (; depth > 0; --depth) {
    LoadTaggedField(context, context, Context::kPreviousOffset);
  }
  LoadTaggedField(context, context, Context::kExtensionOffset);
  if (cell_index > 0) {
    LoadTaggedField(context, context, SourceTextModule::kRegularExportsOffset);
    // The actual array index is (cell_index - 1).
    cell_index -= 1;
  } else {
    LoadTaggedField(context, context, SourceTextModule::kRegularImportsOffset);
    // The actual array index is (-cell_index - 1).
    cell_index = -cell_index - 1;
  }
  LoadFixedArrayElement(context, context, cell_index);
  LoadTaggedField(kInterpreterAccumulatorRegister, context, Cell::kValueOffset);
}

void BaselineAssembler::StaModuleVariable(Register context, Register value,
                                          int cell_index, uint32_t depth) {
  for (; depth > 0; --depth) {
    LoadTaggedField(context, context, Context::kPreviousOffset);
  }
  LoadTaggedField(context, context, Context::kExtensionOffset);
  LoadTaggedField(context, context, SourceTextModule::kRegularExportsOffset);

  // The actual array index is (cell_index - 1).
  cell_index -= 1;
  LoadFixedArrayElement(context, context, cell_index);
  StoreTaggedFieldWithWriteBarrier(context, Cell::kValueOffset, value);
}

void BaselineAssembler::AddSmi(Register lhs, Tagged<Smi> rhs) {
  if (rhs.value() == 0) return;
  __ add(lhs, Immediate(rhs));
}

void BaselineAssembler::Word32And(Register output, Register lhs, int rhs) {
  Move(output, lhs);
  __ and_(output, Immediate(rhs));
}

void BaselineAssembler::Switch(Register reg, int case_value_base,
                               Label** labels, int num_labels) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope scope(this);
  Register table = scope.AcquireScratch();
  DCHECK(!AreAliased(reg, table));
  Label fallthrough, jump_table;
  if (case_value_base != 0) {
    __ sub(reg, Immediate(case_value_base));
  }
  __ cmp(reg, Immediate(num_labels));
  __ j(above_equal, &fallthrough);
  __ lea(table, MemOperand(&jump_table));
  __ jmp(Operand(table, reg, times_system_pointer_size, 0));
  // Emit the jump table inline, under the assumption that it's not too big.
  __ Align(kSystemPointerSize);
  __ bind(&jump_table);
  for (int i = 0; i < num_labels; ++i) {
    __ dd(labels[i]);
  }
  __ bind(&fallthrough);
}

#undef __
#define __ basm.

void BaselineAssembler::EmitReturn(MacroAssembler* masm) {
  ASM_CODE_COMMENT(masm);
  BaselineAssembler basm(masm);

  Register weight = BaselineLeaveFrameDescriptor::WeightRegister();
  Register params_size = BaselineLeaveFrameDescriptor::ParamsSizeRegister();
  {
    ASM_CODE_COMMENT_STRING(masm, "Update Interrupt Budget");

    Label skip_interrupt_label;
    __ AddToInterruptBudgetAndJumpIfNotExceeded(weight, &skip_interrupt_label);
    __ masm()->SmiTag(params_size);
    __ Push(params_size, kInterpreterAccumulatorRegister);

    __ LoadContext(kContextRegister);
    __ Push(MemOperand(ebp, InterpreterFrameConstants::kFunctionOffset));
    __ CallRuntime(Runtime::kBytecodeBudgetInterrupt_Sparkplug, 1);

    __ Pop(kInterpreterAccumulatorRegister, params_size);
    __ masm()->SmiUntag(params_size);

  __ Bind(&skip_interrupt_label);
  }

  BaselineAssembler::ScratchRegisterScope scope(&basm);
  Register scratch = scope.AcquireScratch();
  DCHECK(!AreAliased(weight, params_size, scratch));

  Register actual_params_size = scratch;
  // Compute the size of the actual parameters + receiver (in bytes).
  __ masm()->mov(actual_params_size,
                 MemOperand(ebp, StandardFrameConstants::kArgCOffset));

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  Label corrected_args_count;
  __ masm()->cmp(params_size, actual_params_size);
  __ masm()->j(greater_equal, &corrected_args_count);
  __ masm()->mov(params_size, actual_params_size);
  __ Bind(&corrected_args_count);

  // Leave the frame (also dropping the register file).
  __ masm()->LeaveFrame(StackFrame::BASELINE);

  // Drop receiver + arguments.
  __ masm()->DropArguments(params_size, scratch,
                           MacroAssembler::kCountIsInteger,
                           MacroAssembler::kCountIncludesReceiver);
  __ masm()->Ret();
}

#undef __

inline void EnsureAccumulatorPreservedScope::AssertEqualToAccumulator(
    Register reg) {
  assembler_->masm()->cmp(reg, kInterpreterAccumulatorRegister);
  assembler_->masm()->Assert(equal, AbortReason::kAccumulatorClobbered);
}

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_IA32_BASELINE_ASSEMBLER_IA32_INL_H_
