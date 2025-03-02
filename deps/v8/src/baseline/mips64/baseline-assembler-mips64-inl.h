// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_MIPS64_BASELINE_ASSEMBLER_MIPS64_INL_H_
#define V8_BASELINE_MIPS64_BASELINE_ASSEMBLER_MIPS64_INL_H_

#include "src/baseline/baseline-assembler.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/mips64/assembler-mips64-inl.h"
#include "src/objects/literal-objects-inl.h"

namespace v8 {
namespace internal {
namespace baseline {

class BaselineAssembler::ScratchRegisterScope {
 public:
  explicit ScratchRegisterScope(BaselineAssembler* assembler)
      : assembler_(assembler),
        prev_scope_(assembler->scratch_register_scope_),
        wrapped_scope_(assembler->masm()) {
    if (!assembler_->scratch_register_scope_) {
      // If we haven't opened a scratch scope yet, for the first one add a
      // couple of extra registers.
      wrapped_scope_.Include({t0, t1, t2, t3});
    }
    assembler_->scratch_register_scope_ = this;
  }
  ~ScratchRegisterScope() { assembler_->scratch_register_scope_ = prev_scope_; }

  Register AcquireScratch() { return wrapped_scope_.Acquire(); }

 private:
  BaselineAssembler* assembler_;
  ScratchRegisterScope* prev_scope_;
  UseScratchRegisterScope wrapped_scope_;
};

namespace detail {

#ifdef DEBUG
inline bool Clobbers(Register target, MemOperand op) {
  return op.is_reg() && op.rm() == target;
}
#endif

}  // namespace detail

#define __ masm_->

MemOperand BaselineAssembler::RegisterFrameOperand(
    interpreter::Register interpreter_register) {
  return MemOperand(fp, interpreter_register.ToOperand() * kSystemPointerSize);
}
void BaselineAssembler::RegisterFrameAddress(
    interpreter::Register interpreter_register, Register rscratch) {
  return __ Daddu(rscratch, fp,
                  interpreter_register.ToOperand() * kSystemPointerSize);
}
MemOperand BaselineAssembler::FeedbackVectorOperand() {
  return MemOperand(fp, BaselineFrameConstants::kFeedbackVectorFromFp);
}
MemOperand BaselineAssembler::FeedbackCellOperand() {
  return MemOperand(fp, BaselineFrameConstants::kFeedbackCellFromFp);
}

void BaselineAssembler::Bind(Label* label) { __ bind(label); }

void BaselineAssembler::JumpTarget() {
  // NOP.
}
void BaselineAssembler::Jump(Label* target, Label::Distance distance) {
  __ Branch(target);
}
void BaselineAssembler::JumpIfRoot(Register value, RootIndex index,
                                   Label* target, Label::Distance) {
  __ JumpIfRoot(value, index, target);
}
void BaselineAssembler::JumpIfNotRoot(Register value, RootIndex index,
                                      Label* target, Label::Distance) {
  __ JumpIfNotRoot(value, index, target);
}
void BaselineAssembler::JumpIfSmi(Register value, Label* target,
                                  Label::Distance) {
  __ JumpIfSmi(value, target);
}
void BaselineAssembler::JumpIfNotSmi(Register value, Label* target,
                                     Label::Distance) {
  __ JumpIfNotSmi(value, target);
}
void BaselineAssembler::JumpIfImmediate(Condition cc, Register left, int right,
                                        Label* target,
                                        Label::Distance distance) {
  JumpIf(cc, left, Operand(right), target, distance);
}

void BaselineAssembler::TestAndBranch(Register value, int mask, Condition cc,
                                      Label* target, Label::Distance) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  __ And(scratch, value, Operand(mask));
  __ Branch(target, cc, scratch, Operand(zero_reg));
}

void BaselineAssembler::JumpIf(Condition cc, Register lhs, const Operand& rhs,
                               Label* target, Label::Distance) {
  __ Branch(target, cc, lhs, Operand(rhs));
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
                                         Label::Distance) {
  ScratchRegisterScope temps(this);
  Register type = temps.AcquireScratch();
  __ GetObjectType(object, map, type);
  __ Branch(target, cc, type, Operand(instance_type));
}
void BaselineAssembler::JumpIfInstanceType(Condition cc, Register map,
                                           InstanceType instance_type,
                                           Label* target, Label::Distance) {
  ScratchRegisterScope temps(this);
  Register type = temps.AcquireScratch();
  if (v8_flags.debug_code) {
    __ AssertNotSmi(map);
    __ GetObjectType(map, type, type);
    __ Assert(eq, AbortReason::kUnexpectedValue, type, Operand(MAP_TYPE));
  }
  __ Ld(type, FieldMemOperand(map, Map::kInstanceTypeOffset));
  __ Branch(target, cc, type, Operand(instance_type));
}
void BaselineAssembler::JumpIfPointer(Condition cc, Register value,
                                      MemOperand operand, Label* target,
                                      Label::Distance) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  __ Ld(scratch, operand);
  __ Branch(target, cc, value, Operand(scratch));
}
void BaselineAssembler::JumpIfSmi(Condition cc, Register value, Tagged<Smi> smi,
                                  Label* target, Label::Distance) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  __ li(scratch, Operand(smi));
  __ SmiUntag(scratch);
  __ Branch(target, cc, value, Operand(scratch));
}
void BaselineAssembler::JumpIfSmi(Condition cc, Register lhs, Register rhs,
                                  Label* target, Label::Distance) {
  __ AssertSmi(lhs);
  __ AssertSmi(rhs);
  __ Branch(target, cc, lhs, Operand(rhs));
}
void BaselineAssembler::JumpIfTagged(Condition cc, Register value,
                                     MemOperand operand, Label* target,
                                     Label::Distance) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  __ Ld(scratch, operand);
  __ Branch(target, cc, value, Operand(scratch));
}
void BaselineAssembler::JumpIfTagged(Condition cc, MemOperand operand,
                                     Register value, Label* target,
                                     Label::Distance) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  __ Ld(scratch, operand);
  __ Branch(target, cc, scratch, Operand(value));
}
void BaselineAssembler::JumpIfByte(Condition cc, Register value, int32_t byte,
                                   Label* target, Label::Distance) {
  __ Branch(target, cc, value, Operand(byte));
}

void BaselineAssembler::Move(interpreter::Register output, Register source) {
  Move(RegisterFrameOperand(output), source);
}
void BaselineAssembler::Move(Register output, Tagged<TaggedIndex> value) {
  __ li(output, Operand(value.ptr()));
}
void BaselineAssembler::Move(MemOperand output, Register source) {
  __ Sd(source, output);
}
void BaselineAssembler::Move(Register output, ExternalReference reference) {
  __ li(output, Operand(reference));
}
void BaselineAssembler::Move(Register output, Handle<HeapObject> value) {
  __ li(output, Operand(value));
}
void BaselineAssembler::Move(Register output, int32_t value) {
  __ li(output, Operand(value));
}
void BaselineAssembler::MoveMaybeSmi(Register output, Register source) {
  __ Move(output, source);
}
void BaselineAssembler::MoveSmi(Register output, Register source) {
  __ Move(output, source);
}

namespace detail {

template <typename Arg>
inline Register ToRegister(BaselineAssembler* basm,
                           BaselineAssembler::ScratchRegisterScope* scope,
                           Arg arg) {
  Register reg = scope->AcquireScratch();
  basm->Move(reg, arg);
  return reg;
}
inline Register ToRegister(BaselineAssembler* basm,
                           BaselineAssembler::ScratchRegisterScope* scope,
                           Register reg) {
  return reg;
}

template <typename... Args>
struct PushAllHelper;
template <>
struct PushAllHelper<> {
  static int Push(BaselineAssembler* basm) { return 0; }
  static int PushReverse(BaselineAssembler* basm) { return 0; }
};
// TODO(ishell): try to pack sequence of pushes into one instruction by
// looking at regiser codes. For example, Push(r1, r2, r5, r0, r3, r4)
// could be generated as two pushes: Push(r1, r2, r5) and Push(r0, r3, r4).
template <typename Arg>
struct PushAllHelper<Arg> {
  static int Push(BaselineAssembler* basm, Arg arg) {
    BaselineAssembler::ScratchRegisterScope scope(basm);
    basm->masm()->Push(ToRegister(basm, &scope, arg));
    return 1;
  }
  static int PushReverse(BaselineAssembler* basm, Arg arg) {
    return Push(basm, arg);
  }
};
// TODO(ishell): try to pack sequence of pushes into one instruction by
// looking at regiser codes. For example, Push(r1, r2, r5, r0, r3, r4)
// could be generated as two pushes: Push(r1, r2, r5) and Push(r0, r3, r4).
template <typename Arg, typename... Args>
struct PushAllHelper<Arg, Args...> {
  static int Push(BaselineAssembler* basm, Arg arg, Args... args) {
    PushAllHelper<Arg>::Push(basm, arg);
    return 1 + PushAllHelper<Args...>::Push(basm, args...);
  }
  static int PushReverse(BaselineAssembler* basm, Arg arg, Args... args) {
    int nargs = PushAllHelper<Args...>::PushReverse(basm, args...);
    PushAllHelper<Arg>::Push(basm, arg);
    return nargs + 1;
  }
};
template <>
struct PushAllHelper<interpreter::RegisterList> {
  static int Push(BaselineAssembler* basm, interpreter::RegisterList list) {
    for (int reg_index = 0; reg_index < list.register_count(); ++reg_index) {
      PushAllHelper<interpreter::Register>::Push(basm, list[reg_index]);
    }
    return list.register_count();
  }
  static int PushReverse(BaselineAssembler* basm,
                         interpreter::RegisterList list) {
    for (int reg_index = list.register_count() - 1; reg_index >= 0;
         --reg_index) {
      PushAllHelper<interpreter::Register>::Push(basm, list[reg_index]);
    }
    return list.register_count();
  }
};

template <typename... T>
struct PopAllHelper;
template <>
struct PopAllHelper<> {
  static void Pop(BaselineAssembler* basm) {}
};
// TODO(ishell): try to pack sequence of pops into one instruction by
// looking at regiser codes. For example, Pop(r1, r2, r5, r0, r3, r4)
// could be generated as two pops: Pop(r1, r2, r5) and Pop(r0, r3, r4).
template <>
struct PopAllHelper<Register> {
  static void Pop(BaselineAssembler* basm, Register reg) {
    basm->masm()->Pop(reg);
  }
};
template <typename... T>
struct PopAllHelper<Register, T...> {
  static void Pop(BaselineAssembler* basm, Register reg, T... tail) {
    PopAllHelper<Register>::Pop(basm, reg);
    PopAllHelper<T...>::Pop(basm, tail...);
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
  detail::PopAllHelper<T...>::Pop(this, registers...);
}

void BaselineAssembler::LoadTaggedField(Register output, Register source,
                                        int offset) {
  __ Ld(output, FieldMemOperand(source, offset));
}
void BaselineAssembler::LoadTaggedSignedField(Register output, Register source,
                                              int offset) {
  __ Ld(output, FieldMemOperand(source, offset));
}
void BaselineAssembler::LoadTaggedSignedFieldAndUntag(Register output,
                                                      Register source,
                                                      int offset) {
  LoadTaggedSignedField(output, source, offset);
  SmiUntag(output);
}
void BaselineAssembler::LoadWord16FieldZeroExtend(Register output,
                                                  Register source, int offset) {
  __ Lhu(output, FieldMemOperand(source, offset));
}
void BaselineAssembler::LoadWord8Field(Register output, Register source,
                                       int offset) {
  __ Lb(output, FieldMemOperand(source, offset));
}
void BaselineAssembler::StoreTaggedSignedField(Register target, int offset,
                                               Tagged<Smi> value) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  __ li(scratch, Operand(value));
  __ Sd(scratch, FieldMemOperand(target, offset));
}
void BaselineAssembler::StoreTaggedFieldWithWriteBarrier(Register target,
                                                         int offset,
                                                         Register value) {
  ASM_CODE_COMMENT(masm_);
  __ Sd(value, FieldMemOperand(target, offset));
  ScratchRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  __ RecordWriteField(target, offset, value, scratch, kRAHasNotBeenSaved,
                      SaveFPRegsMode::kIgnore);
}
void BaselineAssembler::StoreTaggedFieldNoWriteBarrier(Register target,
                                                       int offset,
                                                       Register value) {
  __ Sd(value, FieldMemOperand(target, offset));
}

void BaselineAssembler::TryLoadOptimizedOsrCode(Register scratch_and_result,
                                                Register feedback_vector,
                                                FeedbackSlot slot,
                                                Label* on_result,
                                                Label::Distance) {
  Label fallthrough;
  LoadTaggedField(scratch_and_result, feedback_vector,
                  FeedbackVector::OffsetOfElementAt(slot.ToInt()));
  __ LoadWeakValue(scratch_and_result, scratch_and_result, &fallthrough);
  // Is it marked_for_deoptimization? If yes, clear the slot.
  {
    ScratchRegisterScope temps(this);

    // The entry references a CodeWrapper object. Unwrap it now.
    __ Ld(scratch_and_result,
          FieldMemOperand(scratch_and_result, CodeWrapper::kCodeOffset));

    Register scratch = temps.AcquireScratch();
    __ TestCodeIsMarkedForDeoptimizationAndJump(scratch_and_result, scratch, eq,
                                                on_result);
    __ li(scratch, __ ClearedValue());
    StoreTaggedFieldNoWriteBarrier(
        feedback_vector, FeedbackVector::OffsetOfElementAt(slot.ToInt()),
        scratch);
  }
  __ bind(&fallthrough);
  Move(scratch_and_result, 0);
}

void BaselineAssembler::AddToInterruptBudgetAndJumpIfNotExceeded(
    int32_t weight, Label* skip_interrupt_label) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope scratch_scope(this);
  Register feedback_cell = scratch_scope.AcquireScratch();
  LoadFeedbackCell(feedback_cell);

  Register interrupt_budget = scratch_scope.AcquireScratch();
  __ Lw(interrupt_budget,
        FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  __ Addu(interrupt_budget, interrupt_budget, weight);
  __ Sw(interrupt_budget,
        FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  if (skip_interrupt_label) {
    DCHECK_LT(weight, 0);
    __ Branch(skip_interrupt_label, ge, interrupt_budget, Operand(zero_reg));
  }
}
void BaselineAssembler::AddToInterruptBudgetAndJumpIfNotExceeded(
    Register weight, Label* skip_interrupt_label) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope scratch_scope(this);
  Register feedback_cell = scratch_scope.AcquireScratch();
  LoadFeedbackCell(feedback_cell);

  Register interrupt_budget = scratch_scope.AcquireScratch();
  __ Lw(interrupt_budget,
        FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  __ Addu(interrupt_budget, interrupt_budget, weight);
  __ Sw(interrupt_budget,
        FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  if (skip_interrupt_label)
    __ Branch(skip_interrupt_label, ge, interrupt_budget, Operand(zero_reg));
}

void BaselineAssembler::LdaContextSlot(Register context, uint32_t index,
                                       uint32_t depth,
                                       CompressionMode compression_mode) {
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

void BaselineAssembler::IncrementSmi(MemOperand lhs) {
  BaselineAssembler::ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  if (SmiValuesAre31Bits()) {
    __ Lw(tmp, lhs);
    __ Addu(tmp, tmp, Operand(Smi::FromInt(1)));
    __ Sw(tmp, lhs);
  } else {
    __ Ld(tmp, lhs);
    __ Daddu(tmp, tmp, Operand(Smi::FromInt(1)));
    __ Sd(tmp, lhs);
  }
}

void BaselineAssembler::Word32And(Register output, Register lhs, int rhs) {
  __ And(output, lhs, Operand(rhs));
}

void BaselineAssembler::Switch(Register reg, int case_value_base,
                               Label** labels, int num_labels) {
  ASM_CODE_COMMENT(masm_);
  Label fallthrough;
  if (case_value_base != 0) {
    __ Dsubu(reg, reg, Operand(case_value_base));
  }

  __ Branch(&fallthrough, kUnsignedGreaterThanEqual, reg, Operand(num_labels));

  __ GenerateSwitchTable(reg, num_labels,
                         [labels](size_t i) { return labels[i]; });

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
    __ masm()->Push(params_size, kInterpreterAccumulatorRegister);

    __ LoadContext(kContextRegister);
    __ LoadFunction(kJSFunctionRegister);
    __ masm()->Push(kJSFunctionRegister);
    __ CallRuntime(Runtime::kBytecodeBudgetInterrupt_Sparkplug, 1);

    __ masm()->Pop(params_size, kInterpreterAccumulatorRegister);
    __ masm()->SmiUntag(params_size);

  __ Bind(&skip_interrupt_label);
  }

  BaselineAssembler::ScratchRegisterScope temps(&basm);
  Register actual_params_size = temps.AcquireScratch();
  // Compute the size of the actual parameters + receiver.
  __ Move(actual_params_size,
          MemOperand(fp, StandardFrameConstants::kArgCOffset));

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  Label corrected_args_count;
  __ masm()->Branch(&corrected_args_count, ge, params_size,
                    Operand(actual_params_size));
  __ masm()->Move(params_size, actual_params_size);
  __ Bind(&corrected_args_count);

  // Leave the frame (also dropping the register file).
  __ masm()->LeaveFrame(StackFrame::BASELINE);

  // Drop arguments.
  __ masm()->DropArguments(params_size);

  __ masm()->Ret();
}

#undef __

inline void EnsureAccumulatorPreservedScope::AssertEqualToAccumulator(
    Register reg) {
  assembler_->masm()->Assert(eq, AbortReason::kAccumulatorClobbered, reg,
                             Operand(kInterpreterAccumulatorRegister));
}

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_MIPS64_BASELINE_ASSEMBLER_MIPS64_INL_H_
