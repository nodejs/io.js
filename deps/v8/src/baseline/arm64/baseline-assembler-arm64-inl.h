// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_ARM64_BASELINE_ASSEMBLER_ARM64_INL_H_
#define V8_BASELINE_ARM64_BASELINE_ASSEMBLER_ARM64_INL_H_

#include "src/baseline/baseline-assembler.h"
#include "src/codegen/arm64/macro-assembler-arm64-inl.h"
#include "src/codegen/interface-descriptors.h"
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
      wrapped_scope_.Include(x14, x15);
      wrapped_scope_.Include(x19);
    }
    assembler_->scratch_register_scope_ = this;
  }
  ~ScratchRegisterScope() { assembler_->scratch_register_scope_ = prev_scope_; }

  Register AcquireScratch() { return wrapped_scope_.AcquireX(); }

 private:
  BaselineAssembler* assembler_;
  ScratchRegisterScope* prev_scope_;
  UseScratchRegisterScope wrapped_scope_;
};

namespace detail {

#ifdef DEBUG
inline bool Clobbers(Register target, MemOperand op) {
  return op.base() == target || op.regoffset() == target;
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
  return __ Add(rscratch, fp,
                interpreter_register.ToOperand() * kSystemPointerSize);
}
MemOperand BaselineAssembler::FeedbackVectorOperand() {
  return MemOperand(fp, BaselineFrameConstants::kFeedbackVectorFromFp);
}
MemOperand BaselineAssembler::FeedbackCellOperand() {
  return MemOperand(fp, BaselineFrameConstants::kFeedbackCellFromFp);
}

void BaselineAssembler::Bind(Label* label) { __ Bind(label); }

void BaselineAssembler::JumpTarget() { __ JumpTarget(); }

void BaselineAssembler::Jump(Label* target, Label::Distance distance) {
  __ B(target);
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
  JumpIf(cc, left, Immediate(right), target, distance);
}

void BaselineAssembler::TestAndBranch(Register value, int mask, Condition cc,
                                      Label* target, Label::Distance) {
  if (cc == kZero) {
    __ TestAndBranchIfAllClear(value, mask, target);
  } else if (cc == kNotZero) {
    __ TestAndBranchIfAnySet(value, mask, target);
  } else {
    __ Tst(value, Immediate(mask));
    __ B(cc, target);
  }
}

void BaselineAssembler::JumpIf(Condition cc, Register lhs, const Operand& rhs,
                               Label* target, Label::Distance) {
  __ CompareAndBranch(lhs, rhs, cc, target);
}
#if V8_STATIC_ROOTS_BOOL
void BaselineAssembler::JumpIfJSAnyIsPrimitive(Register heap_object,
                                               Label* target,
                                               Label::Distance distance) {
  __ AssertNotSmi(heap_object);
  ScratchRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  __ JumpIfJSAnyIsPrimitive(heap_object, scratch, target, distance);
}
#endif  // V8_STATIC_ROOTS_BOOL
void BaselineAssembler::JumpIfObjectTypeFast(Condition cc, Register object,
                                             InstanceType instance_type,
                                             Label* target,
                                             Label::Distance distance) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  if (cc == eq || cc == ne) {
    __ IsObjectType(object, scratch, scratch, instance_type);
    __ B(cc, target);
    return;
  }
  JumpIfObjectType(cc, object, instance_type, scratch, target, distance);
}
void BaselineAssembler::JumpIfObjectType(Condition cc, Register object,
                                         InstanceType instance_type,
                                         Register map, Label* target,
                                         Label::Distance) {
  ScratchRegisterScope temps(this);
  Register type = temps.AcquireScratch();
  __ LoadMap(map, object);
  __ Ldrh(type, FieldMemOperand(map, Map::kInstanceTypeOffset));
  JumpIf(cc, type, instance_type, target);
}
void BaselineAssembler::JumpIfInstanceType(Condition cc, Register map,
                                           InstanceType instance_type,
                                           Label* target, Label::Distance) {
  ScratchRegisterScope temps(this);
  Register type = temps.AcquireScratch();
  if (v8_flags.debug_code) {
    __ AssertNotSmi(map);
    __ CompareObjectType(map, type, type, MAP_TYPE);
    __ Assert(eq, AbortReason::kUnexpectedValue);
  }
  __ Ldrh(type, FieldMemOperand(map, Map::kInstanceTypeOffset));
  JumpIf(cc, type, instance_type, target);
}
void BaselineAssembler::JumpIfPointer(Condition cc, Register value,
                                      MemOperand operand, Label* target,
                                      Label::Distance) {
  ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  __ Ldr(tmp, operand);
  JumpIf(cc, value, tmp, target);
}
void BaselineAssembler::JumpIfSmi(Condition cc, Register value, Tagged<Smi> smi,
                                  Label* target, Label::Distance distance) {
  __ AssertSmi(value);
  __ CompareTaggedAndBranch(value, smi, cc, target);
}

void BaselineAssembler::JumpIfSmi(Condition cc, Register lhs, Register rhs,
                                  Label* target, Label::Distance) {
  __ AssertSmi(lhs);
  __ AssertSmi(rhs);
  __ CompareTaggedAndBranch(lhs, rhs, cc, target);
}
void BaselineAssembler::JumpIfTagged(Condition cc, Register value,
                                     MemOperand operand, Label* target,
                                     Label::Distance) {
  ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  __ Ldr(tmp, operand);
  __ CompareTaggedAndBranch(value, tmp, cc, target);
}
void BaselineAssembler::JumpIfTagged(Condition cc, MemOperand operand,
                                     Register value, Label* target,
                                     Label::Distance) {
  ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  __ Ldr(tmp, operand);
  __ CompareTaggedAndBranch(tmp, value, cc, target);
}
void BaselineAssembler::JumpIfByte(Condition cc, Register value, int32_t byte,
                                   Label* target, Label::Distance) {
  JumpIf(cc, value, Immediate(byte), target);
}

void BaselineAssembler::Move(interpreter::Register output, Register source) {
  Move(RegisterFrameOperand(output), source);
}
void BaselineAssembler::Move(Register output, Tagged<TaggedIndex> value) {
  __ Mov(output, Immediate(value.ptr()));
}
void BaselineAssembler::Move(MemOperand output, Register source) {
  __ Str(source, output);
}
void BaselineAssembler::Move(Register output, ExternalReference reference) {
  __ Mov(output, Operand(reference));
}
void BaselineAssembler::Move(Register output, Handle<HeapObject> value) {
  __ Mov(output, Operand(value));
}
void BaselineAssembler::Move(Register output, int32_t value) {
  __ Mov(output, Immediate(value));
}
void BaselineAssembler::MoveMaybeSmi(Register output, Register source) {
  __ Mov(output, source);
}
void BaselineAssembler::MoveSmi(Register output, Register source) {
  __ Mov(output, source);
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
struct CountPushHelper;
template <>
struct CountPushHelper<> {
  static int Count() { return 0; }
};
template <typename Arg, typename... Args>
struct CountPushHelper<Arg, Args...> {
  static int Count(Arg arg, Args... args) {
    return 1 + CountPushHelper<Args...>::Count(args...);
  }
};
template <typename... Args>
struct CountPushHelper<interpreter::RegisterList, Args...> {
  static int Count(interpreter::RegisterList list, Args... args) {
    return list.register_count() + CountPushHelper<Args...>::Count(args...);
  }
};

template <typename... Args>
struct PushAllHelper;
template <typename... Args>
inline void PushAll(BaselineAssembler* basm, Args... args) {
  PushAllHelper<Args...>::Push(basm, args...);
}
template <typename... Args>
inline void PushAllReverse(BaselineAssembler* basm, Args... args) {
  PushAllHelper<Args...>::PushReverse(basm, args...);
}

template <>
struct PushAllHelper<> {
  static void Push(BaselineAssembler* basm) {}
  static void PushReverse(BaselineAssembler* basm) {}
};
template <typename Arg>
struct PushAllHelper<Arg> {
  static void Push(BaselineAssembler* basm, Arg) { FATAL("Unaligned push"); }
  static void PushReverse(BaselineAssembler* basm, Arg arg) {
    // Push the padding register to round up the amount of values pushed.
    return PushAllReverse(basm, arg, padreg);
  }
};
template <typename Arg1, typename Arg2, typename... Args>
struct PushAllHelper<Arg1, Arg2, Args...> {
  static void Push(BaselineAssembler* basm, Arg1 arg1, Arg2 arg2,
                   Args... args) {
    {
      BaselineAssembler::ScratchRegisterScope scope(basm);
      basm->masm()->Push(ToRegister(basm, &scope, arg1),
                         ToRegister(basm, &scope, arg2));
    }
    PushAll(basm, args...);
  }
  static void PushReverse(BaselineAssembler* basm, Arg1 arg1, Arg2 arg2,
                          Args... args) {
    PushAllReverse(basm, args...);
    {
      BaselineAssembler::ScratchRegisterScope scope(basm);
      basm->masm()->Push(ToRegister(basm, &scope, arg2),
                         ToRegister(basm, &scope, arg1));
    }
  }
};
// Currently RegisterLists are always be the last argument, so we don't
// specialize for the case where they're not. We do still specialise for the
// aligned and unaligned cases.
template <typename Arg>
struct PushAllHelper<Arg, interpreter::RegisterList> {
  static void Push(BaselineAssembler* basm, Arg arg,
                   interpreter::RegisterList list) {
    DCHECK_EQ(list.register_count() % 2, 1);
    PushAll(basm, arg, list[0], list.PopLeft());
  }
  static void PushReverse(BaselineAssembler* basm, Arg arg,
                          interpreter::RegisterList list) {
    if (list.register_count() == 0) {
      PushAllReverse(basm, arg);
    } else {
      PushAllReverse(basm, arg, list[0], list.PopLeft());
    }
  }
};
template <>
struct PushAllHelper<interpreter::RegisterList> {
  static void Push(BaselineAssembler* basm, interpreter::RegisterList list) {
    DCHECK_EQ(list.register_count() % 2, 0);
    for (int reg_index = 0; reg_index < list.register_count(); reg_index += 2) {
      PushAll(basm, list[reg_index], list[reg_index + 1]);
    }
  }
  static void PushReverse(BaselineAssembler* basm,
                          interpreter::RegisterList list) {
    int reg_index = list.register_count() - 1;
    if (reg_index % 2 == 0) {
      // Push the padding register to round up the amount of values pushed.
      PushAllReverse(basm, list[reg_index], padreg);
      reg_index--;
    }
    for (; reg_index >= 1; reg_index -= 2) {
      PushAllReverse(basm, list[reg_index - 1], list[reg_index]);
    }
  }
};

template <typename... T>
struct PopAllHelper;
template <>
struct PopAllHelper<> {
  static void Pop(BaselineAssembler* basm) {}
};
template <>
struct PopAllHelper<Register> {
  static void Pop(BaselineAssembler* basm, Register reg) {
    basm->masm()->Pop(reg, padreg);
  }
};
template <typename... T>
struct PopAllHelper<Register, Register, T...> {
  static void Pop(BaselineAssembler* basm, Register reg1, Register reg2,
                  T... tail) {
    basm->masm()->Pop(reg1, reg2);
    PopAllHelper<T...>::Pop(basm, tail...);
  }
};

}  // namespace detail

template <typename... T>
int BaselineAssembler::Push(T... vals) {
  // We have to count the pushes first, to decide whether to add padding before
  // the first push.
  int push_count = detail::CountPushHelper<T...>::Count(vals...);
  if (push_count % 2 == 0) {
    detail::PushAll(this, vals...);
  } else {
    detail::PushAll(this, padreg, vals...);
  }
  return push_count;
}

template <typename... T>
void BaselineAssembler::PushReverse(T... vals) {
  detail::PushAllReverse(this, vals...);
}

template <typename... T>
void BaselineAssembler::Pop(T... registers) {
  detail::PopAllHelper<T...>::Pop(this, registers...);
}

void BaselineAssembler::LoadTaggedField(Register output, Register source,
                                        int offset) {
  __ LoadTaggedField(output, FieldMemOperand(source, offset));
}

void BaselineAssembler::LoadTaggedSignedField(Register output, Register source,
                                              int offset) {
  __ LoadTaggedSignedField(output, FieldMemOperand(source, offset));
}

void BaselineAssembler::LoadTaggedSignedFieldAndUntag(Register output,
                                                      Register source,
                                                      int offset) {
  LoadTaggedSignedField(output, source, offset);
  SmiUntag(output);
}

void BaselineAssembler::LoadWord16FieldZeroExtend(Register output,
                                                  Register source, int offset) {
  __ Ldrh(output, FieldMemOperand(source, offset));
}

void BaselineAssembler::LoadWord8Field(Register output, Register source,
                                       int offset) {
  __ Ldrb(output, FieldMemOperand(source, offset));
}

void BaselineAssembler::StoreTaggedSignedField(Register target, int offset,
                                               Tagged<Smi> value) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  __ Mov(tmp, Operand(value));
  __ StoreTaggedField(tmp, FieldMemOperand(target, offset));
}

void BaselineAssembler::StoreTaggedFieldWithWriteBarrier(Register target,
                                                         int offset,
                                                         Register value) {
  ASM_CODE_COMMENT(masm_);
  __ StoreTaggedField(value, FieldMemOperand(target, offset));
  __ RecordWriteField(target, offset, value, kLRHasNotBeenSaved,
                      SaveFPRegsMode::kIgnore);
}

void BaselineAssembler::StoreTaggedFieldNoWriteBarrier(Register target,
                                                       int offset,
                                                       Register value) {
  __ StoreTaggedField(value, FieldMemOperand(target, offset));
}

void BaselineAssembler::TryLoadOptimizedOsrCode(Register scratch_and_result,
                                                Register feedback_vector,
                                                FeedbackSlot slot,
                                                Label* on_result,
                                                Label::Distance) {
  __ TryLoadOptimizedOsrCode(scratch_and_result, CodeKind::MAGLEV,
                             feedback_vector, slot, on_result,
                             Label::Distance::kFar);
}

void BaselineAssembler::AddToInterruptBudgetAndJumpIfNotExceeded(
    int32_t weight, Label* skip_interrupt_label) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope scratch_scope(this);
  Register feedback_cell = scratch_scope.AcquireScratch();
  LoadFeedbackCell(feedback_cell);

  Register interrupt_budget = scratch_scope.AcquireScratch().W();
  __ Ldr(interrupt_budget,
         FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  // Remember to set flags as part of the add!
  __ Adds(interrupt_budget, interrupt_budget, weight);
  __ Str(interrupt_budget,
         FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  if (skip_interrupt_label) {
    // Use compare flags set by Adds
    DCHECK_LT(weight, 0);
    __ B(ge, skip_interrupt_label);
  }
}

void BaselineAssembler::AddToInterruptBudgetAndJumpIfNotExceeded(
    Register weight, Label* skip_interrupt_label) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope scratch_scope(this);
  Register feedback_cell = scratch_scope.AcquireScratch();
  LoadFeedbackCell(feedback_cell);

  Register interrupt_budget = scratch_scope.AcquireScratch().W();
  __ Ldr(interrupt_budget,
         FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  // Remember to set flags as part of the add!
  __ Adds(interrupt_budget, interrupt_budget, weight.W());
  __ Str(interrupt_budget,
         FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  if (skip_interrupt_label) __ B(ge, skip_interrupt_label);
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

void BaselineAssembler::IncrementSmi(MemOperand lhs) {
  BaselineAssembler::ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  if (SmiValuesAre31Bits()) {
    tmp = tmp.W();
  }
  __ Ldr(tmp, lhs);
  __ Add(tmp, tmp, Operand(Smi::FromInt(1)));
  __ Str(tmp, lhs);
}

void BaselineAssembler::Word32And(Register output, Register lhs, int rhs) {
  __ And(output, lhs, Immediate(rhs));
}

void BaselineAssembler::Switch(Register reg, int case_value_base,
                               Label** labels, int num_labels) {
  ASM_CODE_COMMENT(masm_);
  Label fallthrough;
  if (case_value_base != 0) {
    __ Sub(reg, reg, Immediate(case_value_base));
  }

  // Mostly copied from code-generator-arm64.cc
  ScratchRegisterScope scope(this);
  Register temp = scope.AcquireScratch();
  Label table;
  JumpIf(kUnsignedGreaterThanEqual, reg, num_labels, &fallthrough);
  __ Adr(temp, &table);
  int entry_size_log2 = 2;
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  ++entry_size_log2;  // Account for BTI.
  constexpr int instructions_per_jump_target = 1;
#else
  constexpr int instructions_per_jump_target = 0;
#endif
  constexpr int instructions_per_label = 1 + instructions_per_jump_target;
  __ Add(temp, temp, Operand(reg, UXTW, entry_size_log2));
  __ Br(temp);
  {
    const int instruction_count =
        num_labels * instructions_per_label + instructions_per_jump_target;
    MacroAssembler::BlockPoolsScope block_pools(masm_,
                                                instruction_count * kInstrSize);
    __ Bind(&table);
    for (int i = 0; i < num_labels; ++i) {
      __ JumpTarget();
      __ B(labels[i]);
    }
    __ JumpTarget();
    __ Bind(&fallthrough);
  }
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
    __ masm()->PushArgument(kJSFunctionRegister);
    __ CallRuntime(Runtime::kBytecodeBudgetInterrupt_Sparkplug, 1);

    __ masm()->Pop(kInterpreterAccumulatorRegister, params_size);
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
  __ masm()->Cmp(params_size, actual_params_size);
  __ masm()->Csel(params_size, actual_params_size, params_size, kLessThan);

  // Leave the frame (also dropping the register file).
  __ masm()->LeaveFrame(StackFrame::BASELINE);

  // Drop receiver + arguments.
  __ masm()->DropArguments(params_size);
  __ masm()->Ret();
}

#undef __

inline void EnsureAccumulatorPreservedScope::AssertEqualToAccumulator(
    Register reg) {
  assembler_->masm()->CmpTagged(reg, kInterpreterAccumulatorRegister);
  assembler_->masm()->Assert(eq, AbortReason::kAccumulatorClobbered);
}

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_ARM64_BASELINE_ASSEMBLER_ARM64_INL_H_
