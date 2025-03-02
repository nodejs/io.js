// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_S390_BASELINE_ASSEMBLER_S390_INL_H_
#define V8_BASELINE_S390_BASELINE_ASSEMBLER_S390_INL_H_

#include "src/baseline/baseline-assembler.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/s390/assembler-s390-inl.h"
#include "src/objects/literal-objects-inl.h"

namespace v8 {
namespace internal {
namespace baseline {

namespace detail {

static constexpr Register kScratchRegisters[] = {r8, ip, r1};
static constexpr int kNumScratchRegisters = arraysize(kScratchRegisters);

#ifdef DEBUG
inline bool Clobbers(Register target, MemOperand op) {
  return op.rb() == target || op.rx() == target;
}
#endif
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

#define __ assm->
// s390x helper
template <int width = 64>
static void JumpIfHelper(MacroAssembler* assm, Condition cc, Register lhs,
                         Register rhs, Label* target) {
  static_assert(width == 64 || width == 32,
                "only support 64 and 32 bit compare");
  if (width == 64) {
    if (is_signed(cc)) {
      __ CmpS64(lhs, rhs);
    } else {
      __ CmpU64(lhs, rhs);
    }
  } else {
    if (is_signed(cc)) {
      __ CmpS32(lhs, rhs);
    } else {
      __ CmpU32(lhs, rhs);
    }
  }
  __ b(to_condition(cc), target);
}

#undef __

#define __ masm_->

MemOperand BaselineAssembler::RegisterFrameOperand(
    interpreter::Register interpreter_register) {
  return MemOperand(fp, interpreter_register.ToOperand() * kSystemPointerSize);
}
void BaselineAssembler::RegisterFrameAddress(
    interpreter::Register interpreter_register, Register rscratch) {
  return __ AddS64(rscratch, fp,
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
  // NOP on arm.
}

void BaselineAssembler::Jump(Label* target, Label::Distance distance) {
  ASM_CODE_COMMENT(masm_);
  __ b(target);
}

void BaselineAssembler::JumpIfRoot(Register value, RootIndex index,
                                   Label* target, Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  __ JumpIfRoot(value, index, target);
}

void BaselineAssembler::JumpIfNotRoot(Register value, RootIndex index,
                                      Label* target, Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  __ JumpIfNotRoot(value, index, target);
}

void BaselineAssembler::JumpIfSmi(Register value, Label* target,
                                  Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  __ JumpIfSmi(value, target);
}

void BaselineAssembler::JumpIfImmediate(Condition cc, Register left, int right,
                                        Label* target,
                                        Label::Distance distance) {
  ASM_CODE_COMMENT(masm_);
  JumpIf(cc, left, Operand(right), target, distance);
}

void BaselineAssembler::JumpIfNotSmi(Register value, Label* target,
                                     Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  __ JumpIfNotSmi(value, target);
}

void BaselineAssembler::TestAndBranch(Register value, int mask, Condition cc,
                                      Label* target, Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  __ AndP(r0, value, Operand(mask));
  __ b(to_condition(cc), target);
}

void BaselineAssembler::JumpIf(Condition cc, Register lhs, const Operand& rhs,
                               Label* target, Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  if (is_signed(cc)) {
    __ CmpS64(lhs, rhs);
  } else {
    __ CmpU64(lhs, rhs);
  }
  __ b(to_condition(cc), target);
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
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope temps(this);
  Register type = temps.AcquireScratch();
  __ LoadMap(map, object);
  __ LoadU16(type, FieldMemOperand(map, Map::kInstanceTypeOffset));
  JumpIf(cc, type, Operand(instance_type), target);
}

void BaselineAssembler::JumpIfInstanceType(Condition cc, Register map,
                                           InstanceType instance_type,
                                           Label* target, Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope temps(this);
  Register type = temps.AcquireScratch();
  if (v8_flags.debug_code) {
    __ AssertNotSmi(map);
    __ CompareObjectType(map, type, type, MAP_TYPE);
    __ Assert(eq, AbortReason::kUnexpectedValue);
  }
  __ LoadU16(type, FieldMemOperand(map, Map::kInstanceTypeOffset));
  JumpIf(cc, type, Operand(instance_type), target);
}

void BaselineAssembler::JumpIfPointer(Condition cc, Register value,
                                      MemOperand operand, Label* target,
                                      Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  __ LoadU64(tmp, operand);
  JumpIfHelper(masm_, cc, value, tmp, target);
}

void BaselineAssembler::JumpIfSmi(Condition cc, Register value, Tagged<Smi> smi,
                                  Label* target, Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  __ AssertSmi(value);
  __ LoadSmiLiteral(r0, smi);
  JumpIfHelper(masm_, cc, value, r0, target);
}

void BaselineAssembler::JumpIfSmi(Condition cc, Register lhs, Register rhs,
                                  Label* target, Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  __ AssertSmi(lhs);
  __ AssertSmi(rhs);
  JumpIfHelper(masm_, cc, lhs, rhs, target);
}

#ifdef V8_TARGET_BIG_ENDIAN
constexpr static int stack_bias = 4;
#else
constexpr static int stack_bias = 0;
#endif

void BaselineAssembler::JumpIfTagged(Condition cc, Register value,
                                     MemOperand operand, Label* target,
                                     Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  DCHECK(operand.rb() == fp || operand.rx() == fp);
  if (COMPRESS_POINTERS_BOOL) {
    MemOperand addr =
        MemOperand(operand.rx(), operand.rb(), operand.offset() + stack_bias);
    __ LoadTaggedField(ip, addr, r0);
  } else {
    __ LoadTaggedField(ip, operand, r0);
  }
  JumpIfHelper<COMPRESS_POINTERS_BOOL ? 32 : 64>(masm_, cc, value, ip, target);
}

void BaselineAssembler::JumpIfTagged(Condition cc, MemOperand operand,
                                     Register value, Label* target,
                                     Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  DCHECK(operand.rb() == fp || operand.rx() == fp);
  if (COMPRESS_POINTERS_BOOL) {
    MemOperand addr =
        MemOperand(operand.rx(), operand.rb(), operand.offset() + stack_bias);
    __ LoadTaggedField(ip, addr, r0);
  } else {
    __ LoadTaggedField(ip, operand, r0);
  }
  JumpIfHelper<COMPRESS_POINTERS_BOOL ? 32 : 64>(masm_, cc, ip, value, target);
}
void BaselineAssembler::JumpIfByte(Condition cc, Register value, int32_t byte,
                                   Label* target, Label::Distance) {
  ASM_CODE_COMMENT(masm_);
  JumpIf(cc, value, Operand(byte), target);
}

void BaselineAssembler::Move(interpreter::Register output, Register source) {
  Move(RegisterFrameOperand(output), source);
}

void BaselineAssembler::Move(Register output, Tagged<TaggedIndex> value) {
  ASM_CODE_COMMENT(masm_);
  __ mov(output, Operand(value.ptr()));
}

void BaselineAssembler::Move(MemOperand output, Register source) {
  ASM_CODE_COMMENT(masm_);
  __ StoreU64(source, output);
}

void BaselineAssembler::Move(Register output, ExternalReference reference) {
  ASM_CODE_COMMENT(masm_);
  __ Move(output, reference);
}

void BaselineAssembler::Move(Register output, Handle<HeapObject> value) {
  ASM_CODE_COMMENT(masm_);
  __ Move(output, value);
}

void BaselineAssembler::Move(Register output, int32_t value) {
  ASM_CODE_COMMENT(masm_);
  __ mov(output, Operand(value));
}

void BaselineAssembler::MoveMaybeSmi(Register output, Register source) {
  ASM_CODE_COMMENT(masm_);
  __ mov(output, source);
}

void BaselineAssembler::MoveSmi(Register output, Register source) {
  ASM_CODE_COMMENT(masm_);
  __ mov(output, source);
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
  ASM_CODE_COMMENT(masm_);
  __ LoadTaggedField(output, FieldMemOperand(source, offset), r0);
}

void BaselineAssembler::LoadTaggedSignedField(Register output, Register source,
                                              int offset) {
  ASM_CODE_COMMENT(masm_);
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
  ASM_CODE_COMMENT(masm_);
  __ LoadU16(output, FieldMemOperand(source, offset));
}

void BaselineAssembler::LoadWord8Field(Register output, Register source,
                                       int offset) {
  ASM_CODE_COMMENT(masm_);
  __ LoadU8(output, FieldMemOperand(source, offset));
}

void BaselineAssembler::StoreTaggedSignedField(Register target, int offset,
                                               Tagged<Smi> value) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope temps(this);
  Register tmp = temps.AcquireScratch();
  __ LoadSmiLiteral(tmp, value);
  __ StoreTaggedField(tmp, FieldMemOperand(target, offset), r0);
}

void BaselineAssembler::StoreTaggedFieldWithWriteBarrier(Register target,
                                                         int offset,
                                                         Register value) {
  ASM_CODE_COMMENT(masm_);
  Register scratch = WriteBarrierDescriptor::SlotAddressRegister();
  DCHECK(!AreAliased(target, value, scratch));
  __ StoreTaggedField(value, FieldMemOperand(target, offset), r0);
  __ RecordWriteField(target, offset, value, scratch, kLRHasNotBeenSaved,
                      SaveFPRegsMode::kIgnore);
}

void BaselineAssembler::StoreTaggedFieldNoWriteBarrier(Register target,
                                                       int offset,
                                                       Register value) {
  __ StoreTaggedField(value, FieldMemOperand(target, offset), r0);
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
    __ LoadTaggedField(
        scratch_and_result,
        FieldMemOperand(scratch_and_result, CodeWrapper::kCodeOffset));

    Register scratch = temps.AcquireScratch();
    __ TestCodeIsMarkedForDeoptimization(scratch_and_result, scratch);
    __ beq(on_result);
    __ mov(scratch, __ ClearedValue());
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
  __ LoadU32(
      interrupt_budget,
      FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  // Remember to set flags as part of the add!
  __ AddS32(interrupt_budget, Operand(weight));
  __ StoreU32(
      interrupt_budget,
      FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset), r0);
  if (skip_interrupt_label) {
    // Use compare flags set by add
    DCHECK_LT(weight, 0);
    __ b(ge, skip_interrupt_label);
  }
}

void BaselineAssembler::AddToInterruptBudgetAndJumpIfNotExceeded(
    Register weight, Label* skip_interrupt_label) {
  ASM_CODE_COMMENT(masm_);
  ScratchRegisterScope scratch_scope(this);
  Register feedback_cell = scratch_scope.AcquireScratch();
  LoadFeedbackCell(feedback_cell);

  Register interrupt_budget = scratch_scope.AcquireScratch();
  __ LoadU32(
      interrupt_budget,
      FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  // Remember to set flags as part of the add!
  __ AddS32(interrupt_budget, interrupt_budget, weight);
  __ StoreU32(
      interrupt_budget,
      FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  if (skip_interrupt_label) __ b(ge, skip_interrupt_label);
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
  Register scratch = ip;
  if (SmiValuesAre31Bits()) {
    DCHECK(COMPRESS_POINTERS_BOOL);
    DCHECK(lhs.rb() == fp || lhs.rx() == fp);
    MemOperand addr = MemOperand(lhs.rx(), lhs.rb(), lhs.offset() + stack_bias);
    __ LoadS32(scratch, addr);
    __ AddU32(scratch, Operand(Smi::FromInt(1)));
    __ StoreU32(scratch, addr);
  } else {
    __ SmiUntag(scratch, lhs);
    __ AddU64(scratch, Operand(1));
    __ SmiTag(scratch);
    __ StoreU64(scratch, lhs);
  }
}

void BaselineAssembler::Word32And(Register output, Register lhs, int rhs) {
  __ AndP(output, lhs, Operand(rhs));
}

void BaselineAssembler::Switch(Register reg, int case_value_base,
                               Label** labels, int num_labels) {
  ASM_CODE_COMMENT(masm_);
  Label fallthrough, jump_table;
  if (case_value_base != 0) {
    __ AddS64(reg, Operand(-case_value_base));
  }

  // Mostly copied from code-generator-arm.cc
  ScratchRegisterScope scope(this);
  JumpIf(kUnsignedGreaterThanEqual, reg, Operand(num_labels), &fallthrough);
  // Ensure to emit the constant pool first if necessary.
  int entry_size_log2 = 3;
  __ ShiftLeftU32(reg, reg, Operand(entry_size_log2));
  __ larl(r1, &jump_table);
  __ lay(reg, MemOperand(reg, r1));
  __ b(reg);
  __ b(&fallthrough);
  __ bind(&jump_table);
  for (int i = 0; i < num_labels; ++i) {
    __ b(labels[i], Label::kFar);
    __ nop();
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
    {
      __ masm()->SmiTag(params_size);
      __ Push(params_size, kInterpreterAccumulatorRegister);

      __ LoadContext(kContextRegister);
      __ LoadFunction(kJSFunctionRegister);
      __ Push(kJSFunctionRegister);
      __ CallRuntime(Runtime::kBytecodeBudgetInterrupt_Sparkplug, 1);

      __ Pop(kInterpreterAccumulatorRegister, params_size);
      __ masm()->SmiUntag(params_size);
    }

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
  JumpIfHelper(__ masm(), kGreaterThanEqual, params_size, actual_params_size,
               &corrected_args_count);
  __ masm()->mov(params_size, actual_params_size);
  __ Bind(&corrected_args_count);

  // Leave the frame (also dropping the register file).
  __ masm()->LeaveFrame(StackFrame::BASELINE);

  // Drop receiver + arguments.
  __ masm() -> DropArguments(params_size);
  __ masm()->Ret();
}

#undef __

inline void EnsureAccumulatorPreservedScope::AssertEqualToAccumulator(
    Register reg) {
  if (COMPRESS_POINTERS_BOOL) {
    assembler_->masm()->CmpU32(reg, kInterpreterAccumulatorRegister);
  } else {
    assembler_->masm()->CmpU64(reg, kInterpreterAccumulatorRegister);
  }
  assembler_->masm()->Assert(eq, AbortReason::kAccumulatorClobbered);
}

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_S390_BASELINE_ASSEMBLER_S390_INL_H_
