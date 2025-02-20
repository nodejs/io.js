// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/logging.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/riscv/assembler-riscv-inl.h"
#include "src/codegen/riscv/register-riscv.h"
#include "src/maglev/maglev-assembler-inl.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"
#include "src/maglev/riscv/maglev-assembler-riscv-inl.h"
#include "src/objects/feedback-cell.h"
#include "src/objects/js-function.h"

namespace v8 {
namespace internal {
namespace maglev {

#define __ masm->

void Int32NegateWithOverflow::SetValueLocationConstraints() {
  UseRegister(value_input());
  DefineAsRegister(this);
}

void Int32NegateWithOverflow::GenerateCode(MaglevAssembler* masm,
                                           const ProcessingState& state) {
  Register value = ToRegister(value_input());
  Register out = ToRegister(result());

  static_assert(Int32NegateWithOverflow::kProperties.can_eager_deopt());
  // Deopt when result would be -0.
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kOverflow);
  __ RecordComment("-- Jump to eager deopt");
  __ MacroAssembler::Branch(fail, equal, value, Operand(zero_reg));

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  __ neg(scratch, value);
  __ negw(out, value);

  // Are the results of NEG and NEGW on the operand different?
  __ RecordComment("-- Jump to eager deopt");
  __ MacroAssembler::Branch(fail, not_equal, scratch, Operand(out));

  // Output register must not be a register input into the eager deopt info.
  DCHECK_REGLIST_EMPTY(RegList{out} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
}

void Int32AbsWithOverflow::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register out = ToRegister(result());

  static_assert(Int32AbsWithOverflow::kProperties.can_eager_deopt());
  Label done;
  DCHECK(ToRegister(input()) == out);
  // fast-path
  __ MacroAssembler::Branch(&done, greater_equal, out, Operand(zero_reg),
                            Label::Distance::kNear);

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  __ neg(scratch, out);
  __ negw(out, out);

  // Are the results of NEG and NEGW on the operand different?
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kOverflow);
  __ RecordComment("-- Jump to eager deopt");
  __ MacroAssembler::Branch(fail, not_equal, scratch, Operand(out));

  __ bind(&done);

  // Output register must not be a register input into the eager deopt info.
  DCHECK_REGLIST_EMPTY(RegList{out} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
}

void Int32IncrementWithOverflow::SetValueLocationConstraints() {
  UseRegister(value_input());
  DefineAsRegister(this);
}

void Int32IncrementWithOverflow::GenerateCode(MaglevAssembler* masm,
                                              const ProcessingState& state) {
  Register value = ToRegister(value_input());
  Register out = ToRegister(result());

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  __ Add32(scratch, value, Operand(1));

  static_assert(Int32IncrementWithOverflow::kProperties.can_eager_deopt());
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kOverflow);
  __ RecordComment("-- Jump to eager deopt");
  __ MacroAssembler::Branch(fail, less, scratch, Operand(value));
  __ Mv(out, scratch);

  // Output register must not be a register input into the eager deopt info.
  DCHECK_REGLIST_EMPTY(RegList{out} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
}

void Int32DecrementWithOverflow::SetValueLocationConstraints() {
  UseRegister(value_input());
  DefineAsRegister(this);
}

void Int32DecrementWithOverflow::GenerateCode(MaglevAssembler* masm,
                                              const ProcessingState& state) {
  Register value = ToRegister(value_input());
  Register out = ToRegister(result());

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  __ Sub32(scratch, value, Operand(1));

  static_assert(Int32DecrementWithOverflow::kProperties.can_eager_deopt());
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kOverflow);
  __ RecordComment("-- Jump to eager deopt");
  __ MacroAssembler::Branch(fail, greater, scratch, Operand(value));
  __ Mv(out, scratch);

  // Output register must not be a register input into the eager deopt info.
  DCHECK_REGLIST_EMPTY(RegList{out} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
}

int BuiltinStringFromCharCode::MaxCallStackArgs() const {
  return AllocateDescriptor::GetStackParameterCount();
}
void BuiltinStringFromCharCode::SetValueLocationConstraints() {
  if (code_input().node()->Is<Int32Constant>()) {
    UseAny(code_input());
  } else {
    UseRegister(code_input());
  }
  set_temporaries_needed(2);
  DefineAsRegister(this);
}
void BuiltinStringFromCharCode::GenerateCode(MaglevAssembler* masm,
                                             const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  Register result_string = ToRegister(result());
  if (Int32Constant* constant = code_input().node()->TryCast<Int32Constant>()) {
    int32_t char_code = constant->value() & 0xFFFF;
    if (0 <= char_code && char_code < String::kMaxOneByteCharCode) {
      __ LoadSingleCharacterString(result_string, char_code);
    } else {
      __ AllocateTwoByteString(register_snapshot(), result_string, 1);
      __ Move(scratch, char_code);
      __ Sh(scratch, FieldMemOperand(result_string,
                                     OFFSET_OF_DATA_START(SeqTwoByteString)));
    }
  } else {
    __ StringFromCharCode(register_snapshot(), nullptr, result_string,
                          ToRegister(code_input()), scratch,
                          MaglevAssembler::CharCodeMaskMode::kMustApplyMask);
  }
}

void InlinedAllocation::SetValueLocationConstraints() {
  UseRegister(allocation_block());
  if (offset() == 0) {
    DefineSameAsFirst(this);
  } else {
    DefineAsRegister(this);
  }
}

void InlinedAllocation::GenerateCode(MaglevAssembler* masm,
                                     const ProcessingState& state) {
  Register out = ToRegister(result());
  Register value = ToRegister(allocation_block());
  if (offset() != 0) {
    __ AddWord(out, value, Operand(offset()));
  }
}

void ArgumentsLength::SetValueLocationConstraints() { DefineAsRegister(this); }

void ArgumentsLength::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  Register out = ToRegister(result());

  __ LoadWord(out, MemOperand(fp, StandardFrameConstants::kArgCOffset));
  __ Sub64(out, out, Operand(1));  // Remove receiver.
}

void RestLength::SetValueLocationConstraints() { DefineAsRegister(this); }

void RestLength::GenerateCode(MaglevAssembler* masm,
                              const ProcessingState& state) {
  Register length = ToRegister(result());
  Label done;
  __ LoadWord(length, MemOperand(fp, StandardFrameConstants::kArgCOffset));
  __ Sub64(length, length, Operand(formal_parameter_count() + 1));
  __ MacroAssembler::Branch(&done, greater_equal, length, Operand(zero_reg),
                            Label::Distance::kNear);
  __ Mv(length, zero_reg);
  __ bind(&done);
  __ UncheckedSmiTagInt32(length);
}

int CheckedObjectToIndex::MaxCallStackArgs() const { return 0; }

void CheckedIntPtrToInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineSameAsFirst(this);
}

void CheckedIntPtrToInt32::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register input_reg = ToRegister(input());
  __ MacroAssembler::Branch(__ GetDeoptLabel(this, DeoptimizeReason::kNotInt32),
                            gt, input_reg,
                            Operand(std::numeric_limits<int32_t>::max()));
  __ MacroAssembler::Branch(__ GetDeoptLabel(this, DeoptimizeReason::kNotInt32),
                            lt, input_reg,
                            Operand(std::numeric_limits<int32_t>::min()));
}

void Int32AddWithOverflow::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
  DefineAsRegister(this);
}

void Int32AddWithOverflow::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register left = ToRegister(left_input());
  Register right = ToRegister(right_input());
  Register out = ToRegister(result());

  static_assert(Int32AddWithOverflow::kProperties.can_eager_deopt());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kOverflow);
  __ Add64(scratch, left, right);
  __ Add32(out, left, right);
  __ RecordComment("-- Jump to eager deopt");
  __ MacroAssembler::Branch(fail, not_equal, scratch, Operand(out));

  // The output register shouldn't be a register input into the eager deopt
  // info.
  DCHECK_REGLIST_EMPTY(RegList{out} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
}

void Int32SubtractWithOverflow::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
  DefineAsRegister(this);
}
void Int32SubtractWithOverflow::GenerateCode(MaglevAssembler* masm,
                                             const ProcessingState& state) {
  Register left = ToRegister(left_input());
  Register right = ToRegister(right_input());
  Register out = ToRegister(result());

  static_assert(Int32SubtractWithOverflow::kProperties.can_eager_deopt());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kOverflow);
  __ Sub64(scratch, left, right);
  __ Sub32(out, left, right);
  __ RecordComment("-- Jump to eager deopt");
  __ MacroAssembler::Branch(fail, ne, scratch, Operand(out));

  // The output register shouldn't be a register input into the eager deopt
  // info.
  DCHECK_REGLIST_EMPTY(RegList{out} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
}

void Int32MultiplyWithOverflow::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
  DefineAsRegister(this);
  set_temporaries_needed(2);
}
void Int32MultiplyWithOverflow::GenerateCode(MaglevAssembler* masm,
                                             const ProcessingState& state) {
  Register left = ToRegister(left_input());
  Register right = ToRegister(right_input());
  Register out = ToRegister(result());

  // TODO(leszeks): peephole optimise multiplication by a constant.

  MaglevAssembler::TemporaryRegisterScope temps(masm);
  bool out_alias_input = out == left || out == right;
  Register res = out;
  if (out_alias_input) {
    res = temps.Acquire();
  }

  Register scratch = temps.Acquire();
  __ MulOverflow32(res, left, Operand(right), scratch);

  static_assert(Int32MultiplyWithOverflow::kProperties.can_eager_deopt());
  // if res != (res[0:31] sign extended to 64 bits), then the multiplication
  // result is too large for 32 bits.
  Label* fail = __ GetDeoptLabel(this, DeoptimizeReason::kOverflow);
  __ RecordComment("-- Jump to eager deopt");
  __ MacroAssembler::Branch(fail, ne, scratch, Operand(zero_reg));

  // If the result is zero, check if either lhs or rhs is negative.
  Label end;
  __ MacroAssembler::Branch(&end, ne, res, Operand(zero_reg),
                            Label::Distance::kNear);
  {
    Register maybeNegative = scratch;
    __ Or(maybeNegative, left, Operand(right));
    // TODO(Vladimir Kempik): consider usage of bexti instruction if Zbs
    // extension is available
    __ And(maybeNegative, maybeNegative, Operand(0x80000000));  // 1 << 31
    // If one of them is negative, we must have a -0 result, which is non-int32,
    // so deopt.
    // TODO(leszeks): Consider splitting these deopts to have distinct deopt
    // reasons. Otherwise, the reason has to match the above.
    __ RecordComment("-- Jump to eager deopt if the result is negative zero");
    Label* deopt_label = __ GetDeoptLabel(this, DeoptimizeReason::kOverflow);
    __ MacroAssembler::Branch(deopt_label, ne, maybeNegative,
                              Operand(zero_reg));
  }

  __ bind(&end);
  if (out_alias_input) {
    __ Move(out, res);
  }

  // The output register shouldn't be a register input into the eager deopt
  // info.
  DCHECK_REGLIST_EMPTY(RegList{out} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
}

void Int32DivideWithOverflow::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
  DefineAsRegister(this);
  set_temporaries_needed(2);
}
void Int32DivideWithOverflow::GenerateCode(MaglevAssembler* masm,
                                           const ProcessingState& state) {
  Register left = ToRegister(left_input());
  Register right = ToRegister(right_input());
  Register out = ToRegister(result());

  // TODO(leszeks): peephole optimise division by a constant.

  static_assert(Int32DivideWithOverflow::kProperties.can_eager_deopt());
  ZoneLabelRef done(masm);
  Label* deferred_overflow_checks = __ MakeDeferredCode(
      [](MaglevAssembler* masm, ZoneLabelRef done, Register left,
         Register right, Int32DivideWithOverflow* node) {
        // {right} is negative or zero.

        // TODO(leszeks): Using kNotInt32 here, but in same places
        // kDivisionByZerokMinusZero/kMinusZero/kOverflow would be better. Right
        // now all eager deopts in a node have to be the same -- we should allow
        // a node to emit multiple eager deopts with different reasons.
        Label* deopt = __ GetDeoptLabel(node, DeoptimizeReason::kNotInt32);

        // Check if {right} is zero.
        __ RecordComment("-- Jump to eager deopt if right is zero");
        __ MacroAssembler::Branch(deopt, eq, right, Operand(zero_reg));

        // Check if {left} is zero, as that would produce minus zero.
        __ RecordComment("-- Jump to eager deopt if left is zero");
        __ MacroAssembler::Branch(deopt, eq, left, Operand(zero_reg));

        // Check if {left} is kMinInt and {right} is -1, in which case we'd have
        // to return -kMinInt, which is not representable as Int32.
        __ MacroAssembler::Branch(*done, ne, left, Operand(kMinInt));
        __ MacroAssembler::Branch(*done, ne, right, Operand(-1));
        __ JumpToDeopt(deopt);
      },
      done, left, right, this);

  // Check if {right} is positive and not zero.
  __ MacroAssembler::Branch(deferred_overflow_checks, less_equal, right,
                            Operand(zero_reg));
  __ bind(*done);

  // Perform the actual integer division.
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  bool out_alias_input = out == left || out == right;
  Register res = out;
  if (out_alias_input) {
    res = temps.Acquire();
  }
  __ Div32(res, left, right);

  // Check that the remainder is zero.
  Register temp = temps.Acquire();
  __ remw(temp, left, right);
  Label* deopt = __ GetDeoptLabel(this, DeoptimizeReason::kNotInt32);
  __ RecordComment("-- Jump to eager deopt if remainder is zero");
  __ MacroAssembler::Branch(deopt, ne, temp, Operand(zero_reg));

  // The output register shouldn't be a register input into the eager deopt
  // info.
  DCHECK_REGLIST_EMPTY(RegList{res} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
  __ Move(out, res);
}

void Int32ModulusWithOverflow::SetValueLocationConstraints() {
  UseAndClobberRegister(left_input());
  UseAndClobberRegister(right_input());
  DefineAsRegister(this);
  set_temporaries_needed(1);
}
void Int32ModulusWithOverflow::GenerateCode(MaglevAssembler* masm,
                                            const ProcessingState& state) {
  // If AreAliased(lhs, rhs):
  //   deopt if lhs < 0  // Minus zero.
  //   0
  //
  // Using same algorithm as in EffectControlLinearizer:
  //   if rhs <= 0 then
  //     rhs = -rhs
  //     deopt if rhs == 0
  //   if lhs < 0 then
  //     let lhs_abs = -lhs in
  //     let res = lhs_abs % rhs in
  //     deopt if res == 0
  //     -res
  //   else
  //     let msk = rhs - 1 in
  //     if rhs & msk == 0 then
  //       lhs & msk
  //     else
  //       lhs % rhs

  Register lhs = ToRegister(left_input());
  Register rhs = ToRegister(right_input());
  Register out = ToRegister(result());

  static_assert(Int32ModulusWithOverflow::kProperties.can_eager_deopt());
  static constexpr DeoptimizeReason deopt_reason =
      DeoptimizeReason::kDivisionByZero;

  // For the modulus algorithm described above, lhs and rhs must not alias
  // each other.
  if (lhs == rhs) {
    // TODO(victorgomes): This ideally should be kMinusZero, but Maglev only
    // allows one deopt reason per IR.
    Label* deopt = __ GetDeoptLabel(this, DeoptimizeReason::kDivisionByZero);
    __ RecordComment("-- Jump to eager deopt");
    __ MacroAssembler::Branch(deopt, less, lhs, Operand(zero_reg));
    __ Move(out, zero_reg);
    return;
  }

  DCHECK(!AreAliased(lhs, rhs));

  ZoneLabelRef done(masm);
  ZoneLabelRef rhs_checked(masm);

  Label* deferred_rhs_check = __ MakeDeferredCode(
      [](MaglevAssembler* masm, ZoneLabelRef rhs_checked, Register rhs,
         Int32ModulusWithOverflow* node) {
        __ negw(rhs, rhs);
        __ MacroAssembler::Branch(*rhs_checked, ne, rhs, Operand(zero_reg));
        __ EmitEagerDeopt(node, deopt_reason);
      },
      rhs_checked, rhs, this);
  __ MacroAssembler::Branch(deferred_rhs_check, less_equal, rhs,
                            Operand(zero_reg));
  __ bind(*rhs_checked);

  Label* deferred_lhs_check = __ MakeDeferredCode(
      [](MaglevAssembler* masm, ZoneLabelRef done, Register lhs, Register rhs,
         Register out, Int32ModulusWithOverflow* node) {
        MaglevAssembler::TemporaryRegisterScope temps(masm);
        Register lhs_abs = temps.AcquireScratch();
        __ negw(lhs_abs, lhs);
        Register res = lhs_abs;
        __ remw(res, lhs_abs, rhs);
        __ negw(out, res);
        __ MacroAssembler::Branch(*done, ne, res, Operand(zero_reg));
        // TODO(victorgomes): This ideally should be kMinusZero, but Maglev
        // only allows one deopt reason per IR.
        __ EmitEagerDeopt(node, deopt_reason);
      },
      done, lhs, rhs, out, this);
  __ MacroAssembler::Branch(deferred_lhs_check, less, lhs, Operand(zero_reg));

  Label rhs_not_power_of_2;
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.AcquireScratch();
  Register msk = temps.AcquireScratch();
  __ Sub32(msk, rhs, Operand(1));
  __ And(scratch, rhs, msk);
  __ MacroAssembler::Branch(&rhs_not_power_of_2, not_equal, scratch,
                            Operand(zero_reg), Label::kNear);
  // {rhs} is power of 2.
  __ And(out, lhs, msk);
  __ MacroAssembler::Branch(*done, Label::kNear);

  __ bind(&rhs_not_power_of_2);
  __ remw(out, lhs, rhs);

  __ bind(*done);
}

#define DEF_BITWISE_BINOP(Instruction, opcode)                   \
  void Instruction::SetValueLocationConstraints() {              \
    UseRegister(left_input());                                   \
    UseRegister(right_input());                                  \
    DefineAsRegister(this);                                      \
  }                                                              \
                                                                 \
  void Instruction::GenerateCode(MaglevAssembler* masm,          \
                                 const ProcessingState& state) { \
    Register lhs = ToRegister(left_input());                     \
    Register rhs = ToRegister(right_input());                    \
    Register out = ToRegister(result());                         \
    __ opcode(out, lhs, Operand(rhs));                           \
    /* TODO: is zero extension really needed here? */            \
    __ ZeroExtendWord(out, out);                                 \
  }
DEF_BITWISE_BINOP(Int32BitwiseAnd, And)
DEF_BITWISE_BINOP(Int32BitwiseOr, Or)
DEF_BITWISE_BINOP(Int32BitwiseXor, Xor)
#undef DEF_BITWISE_BINOP

#define DEF_SHIFT_BINOP(Instruction, opcode)                     \
  void Instruction::SetValueLocationConstraints() {              \
    UseRegister(left_input());                                   \
    if (right_input().node()->Is<Int32Constant>()) {             \
      UseAny(right_input());                                     \
    } else {                                                     \
      UseRegister(right_input());                                \
    }                                                            \
    DefineAsRegister(this);                                      \
  }                                                              \
                                                                 \
  void Instruction::GenerateCode(MaglevAssembler* masm,          \
                                 const ProcessingState& state) { \
    Register out = ToRegister(result());                         \
    Register lhs = ToRegister(left_input());                     \
    if (Int32Constant* constant =                                \
            right_input().node()->TryCast<Int32Constant>()) {    \
      uint32_t shift = constant->value() & 31;                   \
      if (shift == 0) {                                          \
        __ ZeroExtendWord(out, lhs);                             \
        return;                                                  \
      }                                                          \
      __ opcode(out, lhs, Operand(shift));                       \
    } else {                                                     \
      Register rhs = ToRegister(right_input());                  \
      __ opcode(out, lhs, Operand(rhs));                         \
    }                                                            \
  }
DEF_SHIFT_BINOP(Int32ShiftLeft, Sll32)
DEF_SHIFT_BINOP(Int32ShiftRight, Sra32)
DEF_SHIFT_BINOP(Int32ShiftRightLogical, Srl32)
#undef DEF_SHIFT_BINOP

void Int32BitwiseNot::SetValueLocationConstraints() {
  UseRegister(value_input());
  DefineAsRegister(this);
}

void Int32BitwiseNot::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  Register value = ToRegister(value_input());
  Register out = ToRegister(result());
  __ not_(out, value);
  __ ZeroExtendWord(out, out);  // TODO(Yuri Gaevsky): is it really needed?
}

void Float64Add::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
  DefineAsRegister(this);
}

void Float64Add::GenerateCode(MaglevAssembler* masm,
                              const ProcessingState& state) {
  DoubleRegister left = ToDoubleRegister(left_input());
  DoubleRegister right = ToDoubleRegister(right_input());
  DoubleRegister out = ToDoubleRegister(result());
  __ fadd_d(out, left, right);
}

void Float64Subtract::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
  DefineAsRegister(this);
}

void Float64Subtract::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  DoubleRegister left = ToDoubleRegister(left_input());
  DoubleRegister right = ToDoubleRegister(right_input());
  DoubleRegister out = ToDoubleRegister(result());
  __ fsub_d(out, left, right);
}

void Float64Multiply::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
  DefineAsRegister(this);
}

void Float64Multiply::GenerateCode(MaglevAssembler* masm,
                                   const ProcessingState& state) {
  DoubleRegister left = ToDoubleRegister(left_input());
  DoubleRegister right = ToDoubleRegister(right_input());
  DoubleRegister out = ToDoubleRegister(result());
  __ fmul_d(out, left, right);
}

void Float64Divide::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
  DefineAsRegister(this);
}

void Float64Divide::GenerateCode(MaglevAssembler* masm,
                                 const ProcessingState& state) {
  DoubleRegister left = ToDoubleRegister(left_input());
  DoubleRegister right = ToDoubleRegister(right_input());
  DoubleRegister out = ToDoubleRegister(result());
  __ fdiv_d(out, left, right);
}

int Float64Modulus::MaxCallStackArgs() const { return 0; }
void Float64Modulus::SetValueLocationConstraints() {
  UseFixed(left_input(), fa0);
  UseFixed(right_input(), fa1);
  DefineSameAsFirst(this);
}
void Float64Modulus::GenerateCode(MaglevAssembler* masm,
                                  const ProcessingState& state) {
  AllowExternalCallThatCantCauseGC scope(masm);
  __ PrepareCallCFunction(0, 2);
  __ CallCFunction(ExternalReference::mod_two_doubles_operation(), 0, 2);
}

void Float64Negate::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void Float64Negate::GenerateCode(MaglevAssembler* masm,
                                 const ProcessingState& state) {
  DoubleRegister value = ToDoubleRegister(input());
  DoubleRegister out = ToDoubleRegister(result());
  __ fneg_d(out, value);
}

void Float64Abs::GenerateCode(MaglevAssembler* masm,
                              const ProcessingState& state) {
  DoubleRegister in = ToDoubleRegister(input());
  DoubleRegister out = ToDoubleRegister(result());
  __ fabs_d(out, in);
}

void Float64Round::GenerateCode(MaglevAssembler* masm,
                                const ProcessingState& state) {
  DoubleRegister in = ToDoubleRegister(input());
  DoubleRegister out = ToDoubleRegister(result());
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  DoubleRegister fscratch1 = temps.AcquireScratchDouble();

  if (kind_ == Kind::kNearest) {
    // RISC-V Rounding Mode RNE means "Round to Nearest, ties to Even", while JS
    // expects it to round towards +Infinity (see ECMA-262, 20.2.2.28).
    // The best seems to be to add 0.5 then round with RDN mode.

    DoubleRegister half_one = temps.AcquireDouble();  // available in this mode
    __ LoadFPRImmediate(half_one, 0.5);
    DoubleRegister tmp = half_one;
    __ fadd_d(tmp, in, half_one);
    __ Floor_d_d(out, tmp, fscratch1);
    __ fsgnj_d(out, out, in);
  } else if (kind_ == Kind::kCeil) {
    __ Ceil_d_d(out, in, fscratch1);
  } else if (kind_ == Kind::kFloor) {
    __ Floor_d_d(out, in, fscratch1);
  } else {
    UNREACHABLE();
  }
}

int Float64Exponentiate::MaxCallStackArgs() const { return 0; }
void Float64Exponentiate::SetValueLocationConstraints() {
  UseFixed(left_input(), fa0);
  UseFixed(right_input(), fa1);
  DefineSameAsFirst(this);
}
void Float64Exponentiate::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  AllowExternalCallThatCantCauseGC scope(masm);
  __ PrepareCallCFunction(0, 2);
  __ CallCFunction(ExternalReference::ieee754_pow_function(), 2);
}

int Float64Ieee754Unary::MaxCallStackArgs() const { return 0; }
void Float64Ieee754Unary::SetValueLocationConstraints() {
  UseFixed(input(), fa0);
  DefineSameAsFirst(this);
}
void Float64Ieee754Unary::GenerateCode(MaglevAssembler* masm,
                                       const ProcessingState& state) {
  AllowExternalCallThatCantCauseGC scope(masm);
  __ PrepareCallCFunction(0, 1);
  __ CallCFunction(ieee_function_ref(), 1);
}

void LoadTypedArrayLength::SetValueLocationConstraints() {
  UseRegister(receiver_input());
  DefineAsRegister(this);
}
void LoadTypedArrayLength::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register object = ToRegister(receiver_input());
  Register result_register = ToRegister(result());
  if (v8_flags.debug_code) {
    __ AssertObjectType(object, JS_TYPED_ARRAY_TYPE,
                        AbortReason::kUnexpectedValue);
  }
  __ LoadBoundedSizeFromObject(result_register, object,
                               JSTypedArray::kRawByteLengthOffset);
  int shift_size = ElementsKindToShiftSize(elements_kind_);
  if (shift_size > 0) {
    // TODO(leszeks): Merge this shift with the one in LoadBoundedSize.
    DCHECK(shift_size == 1 || shift_size == 2 || shift_size == 3);
    __ SrlWord(result_register, result_register, Operand(shift_size));
  }
}

int CheckJSDataViewBounds::MaxCallStackArgs() const { return 1; }
void CheckJSDataViewBounds::SetValueLocationConstraints() {
  UseRegister(receiver_input());
  UseRegister(index_input());
  set_temporaries_needed(1);
}
void CheckJSDataViewBounds::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register object = ToRegister(receiver_input());
  Register index = ToRegister(index_input());
  if (v8_flags.debug_code) {
    __ AssertObjectType(object, JS_DATA_VIEW_TYPE,
                        AbortReason::kUnexpectedValue);
  }

  // Normal DataView (backed by AB / SAB) or non-length tracking backed by GSAB.
  Register byte_length = temps.Acquire();
  __ LoadBoundedSizeFromObject(byte_length, object,
                               JSDataView::kRawByteLengthOffset);

  int element_size = compiler::ExternalArrayElementSize(element_type_);
  Label ok;
  if (element_size > 1) {
    __ SubWord(byte_length, byte_length, Operand(element_size - 1));
    __ MacroAssembler::Branch(&ok, ge, byte_length, Operand(zero_reg),
                              Label::Distance::kNear);
    __ EmitEagerDeopt(this, DeoptimizeReason::kOutOfBounds);
  }
  __ MacroAssembler::Branch(&ok, ult, index, Operand(byte_length),
                            Label::Distance::kNear);
  __ EmitEagerDeopt(this, DeoptimizeReason::kOutOfBounds);

  __ bind(&ok);
}

void HoleyFloat64ToMaybeNanFloat64::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void HoleyFloat64ToMaybeNanFloat64::GenerateCode(MaglevAssembler* masm,
                                                 const ProcessingState& state) {
  // The hole value is a signalling NaN, so just silence it to get the float64
  // value.
  __ FPUCanonicalizeNaN(ToDoubleRegister(result()), ToDoubleRegister(input()));
}

namespace {

enum class ReduceInterruptBudgetType { kLoop, kReturn };

void HandleInterruptsAndTiering(MaglevAssembler* masm, ZoneLabelRef done,
                                Node* node, ReduceInterruptBudgetType type,
                                Register scratch0) {
  // For loops, first check for interrupts. Don't do this for returns, as we
  // can't lazy deopt to the end of a return.
  if (type == ReduceInterruptBudgetType::kLoop) {
    Label next;
    // Here, we only care about interrupts since we've already guarded against
    // real stack overflows on function entry.
    {
      Register stack_limit = scratch0;
      __ LoadStackLimit(stack_limit, StackLimitKind::kInterruptStackLimit);
      __ MacroAssembler::Branch(&next, ugt, sp, Operand(stack_limit),
                                Label::Distance::kNear);
    }

    // An interrupt has been requested and we must call into runtime to handle
    // it; since we already pay the call cost, combine with the TieringManager
    // call.
    {
      SaveRegisterStateForCall save_register_state(masm,
                                                   node->register_snapshot());
      Register function = scratch0;
      __ LoadWord(function,
                  MemOperand(fp, StandardFrameConstants::kFunctionOffset));
      __ Push(function);
      // Move into kContextRegister after the load into scratch0, just in case
      // scratch0 happens to be kContextRegister.
      __ Move(kContextRegister, masm->native_context().object());
      __ CallRuntime(Runtime::kBytecodeBudgetInterruptWithStackCheck_Maglev, 1);
      save_register_state.DefineSafepointWithLazyDeopt(node->lazy_deopt_info());
    }
    __ MacroAssembler::Branch(*done);  // All done, continue.
    __ bind(&next);
  }

  // No pending interrupts. Call into the TieringManager if needed.
  {
    SaveRegisterStateForCall save_register_state(masm,
                                                 node->register_snapshot());
    Register function = scratch0;
    __ LoadWord(function,
                MemOperand(fp, StandardFrameConstants::kFunctionOffset));
    __ Push(function);
    // Move into kContextRegister after the load into scratch0, just in case
    // scratch0 happens to be kContextRegister.
    __ Move(kContextRegister, masm->native_context().object());
    // Note: must not cause a lazy deopt!
    __ CallRuntime(Runtime::kBytecodeBudgetInterrupt_Maglev, 1);
    save_register_state.DefineSafepoint();
  }
  __ MacroAssembler::Branch(*done);
}

void GenerateReduceInterruptBudget(MaglevAssembler* masm, Node* node,
                                   ReduceInterruptBudgetType type, int amount) {
  MaglevAssembler::TemporaryRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  Register feedback_cell = scratch;
  Register budget = temps.Acquire();
  __ LoadWord(feedback_cell,
              MemOperand(fp, StandardFrameConstants::kFunctionOffset));
  __ LoadTaggedField(
      feedback_cell,
      FieldMemOperand(feedback_cell, JSFunction::kFeedbackCellOffset));
  __ Lw(budget,
        FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  __ Sub32(budget, budget, Operand(amount));
  __ Sw(budget,
        FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));

  ZoneLabelRef done(masm);
  Label* deferred_code = __ MakeDeferredCode(
      [](MaglevAssembler* masm, ZoneLabelRef done, Node* node,
         ReduceInterruptBudgetType type, Register scratch) {
        HandleInterruptsAndTiering(masm, done, node, type, scratch);
      },
      done, node, type, scratch);
  __ MacroAssembler::Branch(deferred_code, lt, budget, Operand(zero_reg));

  __ bind(*done);
}

}  // namespace

int ReduceInterruptBudgetForLoop::MaxCallStackArgs() const { return 1; }
void ReduceInterruptBudgetForLoop::SetValueLocationConstraints() {
  set_temporaries_needed(2);
}
void ReduceInterruptBudgetForLoop::GenerateCode(MaglevAssembler* masm,
                                                const ProcessingState& state) {
  GenerateReduceInterruptBudget(masm, this, ReduceInterruptBudgetType::kLoop,
                                amount());
}

int ReduceInterruptBudgetForReturn::MaxCallStackArgs() const { return 1; }
void ReduceInterruptBudgetForReturn::SetValueLocationConstraints() {
  set_temporaries_needed(2);
}
void ReduceInterruptBudgetForReturn::GenerateCode(
    MaglevAssembler* masm, const ProcessingState& state) {
  GenerateReduceInterruptBudget(masm, this, ReduceInterruptBudgetType::kReturn,
                                amount());
}

// ---
// Control nodes
// ---
void Return::SetValueLocationConstraints() {
  UseFixed(value_input(), kReturnRegister0);
}

void Return::GenerateCode(MaglevAssembler* masm, const ProcessingState& state) {
  DCHECK_EQ(ToRegister(value_input()), kReturnRegister0);
  // Read the formal number of parameters from the top level compilation unit
  // (i.e. the outermost, non inlined function).
  int formal_params_size =
      masm->compilation_info()->toplevel_compilation_unit()->parameter_count();

  // We're not going to continue execution, so we can use an arbitrary register
  // here instead of relying on temporaries from the register allocator.
  // We cannot use scratch registers, since they're used in LeaveFrame and
  // DropArguments.
  Register actual_params_size = a5;
  Register params_size = a6;

  // Compute the size of the actual parameters + receiver (in bytes).
  // TODO(leszeks): Consider making this an input into Return to reuse the
  // incoming argc's register (if it's still valid).
  __ LoadWord(actual_params_size,
              MemOperand(fp, StandardFrameConstants::kArgCOffset));
  __ Move(params_size, formal_params_size);

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  Label corrected_args_count;
  __ MacroAssembler::Branch(&corrected_args_count, ge, params_size,
                            Operand(actual_params_size),
                            Label::Distance::kNear);
  __ Move(params_size, actual_params_size);
  __ bind(&corrected_args_count);

  // Leave the frame.
  __ LeaveFrame(StackFrame::MAGLEV);

  // Drop receiver + arguments according to dynamic arguments size.
  __ DropArguments(params_size);
  __ Ret();
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
