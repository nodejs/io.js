// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/numbers/double.h"
#include "src/codegen/arm/assembler-arm.h"
#include "src/codegen/arm/constants-arm.h"
#include "src/codegen/arm/register-arm.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/common/globals.h"
#include "src/compiler/backend/code-generator-impl.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/gap-resolver.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/osr.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/utils/boxed-float.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace compiler {

#define __ masm()->

// Adds Arm-specific methods to convert InstructionOperands.
class ArmOperandConverter final : public InstructionOperandConverter {
 public:
  ArmOperandConverter(CodeGenerator* gen, Instruction* instr)
      : InstructionOperandConverter(gen, instr) {}

  SBit OutputSBit() const {
    switch (instr_->flags_mode()) {
      case kFlags_branch:
      case kFlags_conditional_branch:
      case kFlags_deoptimize:
      case kFlags_set:
      case kFlags_conditional_set:
      case kFlags_trap:
      case kFlags_select:
        return SetCC;
      case kFlags_none:
        return LeaveCC;
    }
    UNREACHABLE();
  }

  Operand InputImmediate(size_t index) const {
    return ToImmediate(instr_->InputAt(index));
  }

  Operand InputOperand2(size_t first_index) {
    const size_t index = first_index;
    switch (AddressingModeField::decode(instr_->opcode())) {
      case kMode_None:
      case kMode_Offset_RI:
      case kMode_Offset_RR:
      case kMode_Root:
        break;
      case kMode_Operand2_I:
        return InputImmediate(index + 0);
      case kMode_Operand2_R:
        return Operand(InputRegister(index + 0));
      case kMode_Operand2_R_ASR_I:
        return Operand(InputRegister(index + 0), ASR, InputInt5(index + 1));
      case kMode_Operand2_R_ASR_R:
        return Operand(InputRegister(index + 0), ASR, InputRegister(index + 1));
      case kMode_Operand2_R_LSL_I:
        return Operand(InputRegister(index + 0), LSL, InputInt5(index + 1));
      case kMode_Operand2_R_LSL_R:
        return Operand(InputRegister(index + 0), LSL, InputRegister(index + 1));
      case kMode_Operand2_R_LSR_I:
        return Operand(InputRegister(index + 0), LSR, InputInt5(index + 1));
      case kMode_Operand2_R_LSR_R:
        return Operand(InputRegister(index + 0), LSR, InputRegister(index + 1));
      case kMode_Operand2_R_ROR_I:
        return Operand(InputRegister(index + 0), ROR, InputInt5(index + 1));
      case kMode_Operand2_R_ROR_R:
        return Operand(InputRegister(index + 0), ROR, InputRegister(index + 1));
    }
    UNREACHABLE();
  }

  MemOperand InputOffset(size_t* first_index) {
    const size_t index = *first_index;
    switch (AddressingModeField::decode(instr_->opcode())) {
      case kMode_None:
      case kMode_Operand2_I:
      case kMode_Operand2_R:
      case kMode_Operand2_R_ASR_I:
      case kMode_Operand2_R_ASR_R:
      case kMode_Operand2_R_LSL_R:
      case kMode_Operand2_R_LSR_I:
      case kMode_Operand2_R_LSR_R:
      case kMode_Operand2_R_ROR_I:
      case kMode_Operand2_R_ROR_R:
        break;
      case kMode_Operand2_R_LSL_I:
        *first_index += 3;
        return MemOperand(InputRegister(index + 0), InputRegister(index + 1),
                          LSL, InputInt32(index + 2));
      case kMode_Offset_RI:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputInt32(index + 1));
      case kMode_Offset_RR:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputRegister(index + 1));
      case kMode_Root:
        *first_index += 1;
        return MemOperand(kRootRegister, InputInt32(index));
    }
    UNREACHABLE();
  }

  MemOperand InputOffset(size_t first_index = 0) {
    return InputOffset(&first_index);
  }

  Operand ToImmediate(InstructionOperand* operand) const {
    Constant constant = ToConstant(operand);
    switch (constant.type()) {
      case Constant::kInt32:
        return Operand(constant.ToInt32(), constant.rmode());
      case Constant::kFloat32:
        return Operand::EmbeddedNumber(constant.ToFloat32());
      case Constant::kFloat64:
        return Operand::EmbeddedNumber(constant.ToFloat64().value());
      case Constant::kExternalReference:
        return Operand(constant.ToExternalReference());
      case Constant::kInt64:
      case Constant::kCompressedHeapObject:
      case Constant::kHeapObject:
      // TODO(dcarney): loading RPO constants on arm.
      case Constant::kRpoNumber:
        break;
    }
    UNREACHABLE();
  }

  MemOperand ToMemOperand(InstructionOperand* op) const {
    DCHECK_NOT_NULL(op);
    DCHECK(op->IsStackSlot() || op->IsFPStackSlot());
    return SlotToMemOperand(AllocatedOperand::cast(op)->index());
  }

  MemOperand SlotToMemOperand(int slot) const {
    FrameOffset offset = frame_access_state()->GetFrameOffset(slot);
    return MemOperand(offset.from_stack_pointer() ? sp : fp, offset.offset());
  }

  NeonMemOperand NeonInputOperand(size_t first_index) {
    const size_t index = first_index;
    switch (AddressingModeField::decode(instr_->opcode())) {
      case kMode_Operand2_R:
        return NeonMemOperand(InputRegister(index + 0));
      default:
        break;
    }
    UNREACHABLE();
  }
};

namespace {

class OutOfLineRecordWrite final : public OutOfLineCode {
 public:
  OutOfLineRecordWrite(CodeGenerator* gen, Register object, Operand offset,
                       Register value, RecordWriteMode mode,
                       StubCallMode stub_mode,
                       UnwindingInfoWriter* unwinding_info_writer)
      : OutOfLineCode(gen),
        object_(object),
        offset_(offset),
        value_(value),
        mode_(mode),
#if V8_ENABLE_WEBASSEMBLY
        stub_mode_(stub_mode),
#endif  // V8_ENABLE_WEBASSEMBLY
        must_save_lr_(!gen->frame_access_state()->has_frame()),
        unwinding_info_writer_(unwinding_info_writer),
        zone_(gen->zone()) {
  }

  void Generate() final {
    __ CheckPageFlag(value_, MemoryChunk::kPointersToHereAreInterestingMask, eq,
                     exit());
    SaveFPRegsMode const save_fp_mode = frame()->DidAllocateDoubleRegisters()
                                            ? SaveFPRegsMode::kSave
                                            : SaveFPRegsMode::kIgnore;
    if (must_save_lr_) {
      // We need to save and restore lr if the frame was elided.
      __ Push(lr);
      unwinding_info_writer_->MarkLinkRegisterOnTopOfStack(__ pc_offset());
    }
    if (mode_ == RecordWriteMode::kValueIsEphemeronKey) {
      __ CallEphemeronKeyBarrier(object_, offset_, save_fp_mode);
#if V8_ENABLE_WEBASSEMBLY
    } else if (stub_mode_ == StubCallMode::kCallWasmRuntimeStub) {
      __ CallRecordWriteStubSaveRegisters(object_, offset_, save_fp_mode,
                                          StubCallMode::kCallWasmRuntimeStub);
#endif  // V8_ENABLE_WEBASSEMBLY
    } else {
      __ CallRecordWriteStubSaveRegisters(object_, offset_, save_fp_mode);
    }
    if (must_save_lr_) {
      __ Pop(lr);
      unwinding_info_writer_->MarkPopLinkRegisterFromTopOfStack(__ pc_offset());
    }
  }

 private:
  Register const object_;
  Operand const offset_;
  Register const value_;
  RecordWriteMode const mode_;
#if V8_ENABLE_WEBASSEMBLY
  StubCallMode stub_mode_;
#endif  // V8_ENABLE_WEBASSEMBLY
  bool must_save_lr_;
  UnwindingInfoWriter* const unwinding_info_writer_;
  Zone* zone_;
};

template <typename T>
class OutOfLineFloatMin final : public OutOfLineCode {
 public:
  OutOfLineFloatMin(CodeGenerator* gen, T result, T left, T right)
      : OutOfLineCode(gen), result_(result), left_(left), right_(right) {}

  void Generate() final { __ FloatMinOutOfLine(result_, left_, right_); }

 private:
  T const result_;
  T const left_;
  T const right_;
};
using OutOfLineFloat32Min = OutOfLineFloatMin<SwVfpRegister>;
using OutOfLineFloat64Min = OutOfLineFloatMin<DwVfpRegister>;

template <typename T>
class OutOfLineFloatMax final : public OutOfLineCode {
 public:
  OutOfLineFloatMax(CodeGenerator* gen, T result, T left, T right)
      : OutOfLineCode(gen), result_(result), left_(left), right_(right) {}

  void Generate() final { __ FloatMaxOutOfLine(result_, left_, right_); }

 private:
  T const result_;
  T const left_;
  T const right_;
};
using OutOfLineFloat32Max = OutOfLineFloatMax<SwVfpRegister>;
using OutOfLineFloat64Max = OutOfLineFloatMax<DwVfpRegister>;

Condition FlagsConditionToCondition(FlagsCondition condition) {
  switch (condition) {
    case kEqual:
      return eq;
    case kNotEqual:
      return ne;
    case kSignedLessThan:
      return lt;
    case kSignedGreaterThanOrEqual:
      return ge;
    case kSignedLessThanOrEqual:
      return le;
    case kSignedGreaterThan:
      return gt;
    case kUnsignedLessThan:
      return lo;
    case kUnsignedGreaterThanOrEqual:
      return hs;
    case kUnsignedLessThanOrEqual:
      return ls;
    case kUnsignedGreaterThan:
      return hi;
    case kFloatLessThanOrUnordered:
      return lt;
    case kFloatGreaterThanOrEqual:
      return ge;
    case kFloatLessThanOrEqual:
      return ls;
    case kFloatGreaterThanOrUnordered:
      return hi;
    case kFloatLessThan:
      return lo;
    case kFloatGreaterThanOrEqualOrUnordered:
      return hs;
    case kFloatLessThanOrEqualOrUnordered:
      return le;
    case kFloatGreaterThan:
      return gt;
    case kOverflow:
      return vs;
    case kNotOverflow:
      return vc;
    case kPositiveOrZero:
      return pl;
    case kNegative:
      return mi;
    default:
      break;
  }
  UNREACHABLE();
}

}  // namespace

#define ASSEMBLE_ATOMIC_LOAD_INTEGER(asm_instr)                       \
  do {                                                                \
    __ asm_instr(i.OutputRegister(),                                  \
                 MemOperand(i.InputRegister(0), i.InputRegister(1))); \
    __ dmb(ISH);                                                      \
  } while (0)

#define ASSEMBLE_ATOMIC_STORE_INTEGER(asm_instr, order)   \
  do {                                                    \
    __ dmb(ISH);                                          \
    __ asm_instr(i.InputRegister(0), i.InputOffset(1));   \
    if (order == AtomicMemoryOrder::kSeqCst) __ dmb(ISH); \
  } while (0)

#define ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(load_instr, store_instr)             \
  do {                                                                        \
    Label exchange;                                                           \
    __ add(i.TempRegister(1), i.InputRegister(0), i.InputRegister(1));        \
    __ dmb(ISH);                                                              \
    __ bind(&exchange);                                                       \
    __ load_instr(i.OutputRegister(0), i.TempRegister(1));                    \
    __ store_instr(i.TempRegister(0), i.InputRegister(2), i.TempRegister(1)); \
    __ teq(i.TempRegister(0), Operand(0));                                    \
    __ b(ne, &exchange);                                                      \
    __ dmb(ISH);                                                              \
  } while (0)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(load_instr, store_instr,     \
                                                 cmp_reg)                     \
  do {                                                                        \
    Label compareExchange;                                                    \
    Label exit;                                                               \
    __ dmb(ISH);                                                              \
    __ bind(&compareExchange);                                                \
    __ load_instr(i.OutputRegister(0), i.TempRegister(1));                    \
    __ teq(cmp_reg, Operand(i.OutputRegister(0)));                            \
    __ b(ne, &exit);                                                          \
    __ store_instr(i.TempRegister(0), i.InputRegister(3), i.TempRegister(1)); \
    __ teq(i.TempRegister(0), Operand(0));                                    \
    __ b(ne, &compareExchange);                                               \
    __ bind(&exit);                                                           \
    __ dmb(ISH);                                                              \
  } while (0)

#define ASSEMBLE_ATOMIC_BINOP(load_instr, store_instr, bin_instr)            \
  do {                                                                       \
    Label binop;                                                             \
    __ add(i.TempRegister(1), i.InputRegister(0), i.InputRegister(1));       \
    __ dmb(ISH);                                                             \
    __ bind(&binop);                                                         \
    __ load_instr(i.OutputRegister(0), i.TempRegister(1));                   \
    __ bin_instr(i.TempRegister(0), i.OutputRegister(0),                     \
                 Operand(i.InputRegister(2)));                               \
    __ store_instr(i.TempRegister(2), i.TempRegister(0), i.TempRegister(1)); \
    __ teq(i.TempRegister(2), Operand(0));                                   \
    __ b(ne, &binop);                                                        \
    __ dmb(ISH);                                                             \
  } while (0)

#define ASSEMBLE_ATOMIC64_ARITH_BINOP(instr1, instr2)                  \
  do {                                                                 \
    Label binop;                                                       \
    __ add(i.TempRegister(0), i.InputRegister(2), i.InputRegister(3)); \
    __ dmb(ISH);                                                       \
    __ bind(&binop);                                                   \
    __ ldrexd(r2, r3, i.TempRegister(0));                              \
    __ instr1(i.TempRegister(1), r2, i.InputRegister(0), SetCC);       \
    __ instr2(i.TempRegister(2), r3, Operand(i.InputRegister(1)));     \
    DCHECK_EQ(LeaveCC, i.OutputSBit());                                \
    __ strexd(i.TempRegister(3), i.TempRegister(1), i.TempRegister(2), \
              i.TempRegister(0));                                      \
    __ teq(i.TempRegister(3), Operand(0));                             \
    __ b(ne, &binop);                                                  \
    __ dmb(ISH);                                                       \
  } while (0)

#define ASSEMBLE_ATOMIC64_LOGIC_BINOP(instr)                           \
  do {                                                                 \
    Label binop;                                                       \
    __ add(i.TempRegister(0), i.InputRegister(2), i.InputRegister(3)); \
    __ dmb(ISH);                                                       \
    __ bind(&binop);                                                   \
    __ ldrexd(r2, r3, i.TempRegister(0));                              \
    __ instr(i.TempRegister(1), r2, Operand(i.InputRegister(0)));      \
    __ instr(i.TempRegister(2), r3, Operand(i.InputRegister(1)));      \
    __ strexd(i.TempRegister(3), i.TempRegister(1), i.TempRegister(2), \
              i.TempRegister(0));                                      \
    __ teq(i.TempRegister(3), Operand(0));                             \
    __ b(ne, &binop);                                                  \
    __ dmb(ISH);                                                       \
  } while (0)

#define ASSEMBLE_IEEE754_BINOP(name)                                           \
  do {                                                                         \
    /* TODO(bmeurer): We should really get rid of this special instruction, */ \
    /* and generate a CallAddress instruction instead. */                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                              \
    __ PrepareCallCFunction(0, 2);                                             \
    __ MovToFloatParameters(i.InputDoubleRegister(0),                          \
                            i.InputDoubleRegister(1));                         \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 2);    \
    /* Move the result in the double result register. */                       \
    __ MovFromFloatResult(i.OutputDoubleRegister());                           \
    DCHECK_EQ(LeaveCC, i.OutputSBit());                                        \
  } while (0)

#define ASSEMBLE_IEEE754_UNOP(name)                                            \
  do {                                                                         \
    /* TODO(bmeurer): We should really get rid of this special instruction, */ \
    /* and generate a CallAddress instruction instead. */                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                              \
    __ PrepareCallCFunction(0, 1);                                             \
    __ MovToFloatParameter(i.InputDoubleRegister(0));                          \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 1);    \
    /* Move the result in the double result register. */                       \
    __ MovFromFloatResult(i.OutputDoubleRegister());                           \
    DCHECK_EQ(LeaveCC, i.OutputSBit());                                        \
  } while (0)

#define ASSEMBLE_NEON_NARROWING_OP(dt, sdt)           \
  do {                                                \
    Simd128Register dst = i.OutputSimd128Register(),  \
                    src0 = i.InputSimd128Register(0), \
                    src1 = i.InputSimd128Register(1); \
    if (dst == src0 && dst == src1) {                 \
      __ vqmovn(dt, sdt, dst.low(), src0);            \
      __ vmov(dst.high(), dst.low());                 \
    } else if (dst == src0) {                         \
      __ vqmovn(dt, sdt, dst.low(), src0);            \
      __ vqmovn(dt, sdt, dst.high(), src1);           \
    } else {                                          \
      __ vqmovn(dt, sdt, dst.high(), src1);           \
      __ vqmovn(dt, sdt, dst.low(), src0);            \
    }                                                 \
  } while (0)

#define ASSEMBLE_F64X2_ARITHMETIC_BINOP(op)                                   \
  do {                                                                        \
    __ op(i.OutputSimd128Register().low(), i.InputSimd128Register(0).low(),   \
          i.InputSimd128Register(1).low());                                   \
    __ op(i.OutputSimd128Register().high(), i.InputSimd128Register(0).high(), \
          i.InputSimd128Register(1).high());                                  \
  } while (0)

// If shift value is an immediate, we can call asm_imm, taking the shift value
// modulo 2^width. Otherwise, emit code to perform the modulus operation, and
// call vshl.
#define ASSEMBLE_SIMD_SHIFT_LEFT(asm_imm, width, sz, dt) \
  do {                                                   \
    QwNeonRegister dst = i.OutputSimd128Register();      \
    QwNeonRegister src = i.InputSimd128Register(0);      \
    if (instr->InputAt(1)->IsImmediate()) {              \
      __ asm_imm(dt, dst, src, i.InputInt##width(1));    \
    } else {                                             \
      UseScratchRegisterScope temps(masm());             \
      Simd128Register tmp = temps.AcquireQ();            \
      Register shift = temps.Acquire();                  \
      constexpr int mask = (1 << width) - 1;             \
      __ and_(shift, i.InputRegister(1), Operand(mask)); \
      __ vdup(sz, tmp, shift);                           \
      __ vshl(dt, dst, src, tmp);                        \
    }                                                    \
  } while (0)

// If shift value is an immediate, we can call asm_imm, taking the shift value
// modulo 2^width. Otherwise, emit code to perform the modulus operation, and
// call vshl, passing in the negative shift value (treated as a right shift).
#define ASSEMBLE_SIMD_SHIFT_RIGHT(asm_imm, width, sz, dt) \
  do {                                                    \
    QwNeonRegister dst = i.OutputSimd128Register();       \
    QwNeonRegister src = i.InputSimd128Register(0);       \
    if (instr->InputAt(1)->IsImmediate()) {               \
      __ asm_imm(dt, dst, src, i.InputInt##width(1));     \
    } else {                                              \
      UseScratchRegisterScope temps(masm());              \
      Simd128Register tmp = temps.AcquireQ();             \
      Register shift = temps.Acquire();                   \
      constexpr int mask = (1 << width) - 1;              \
      __ and_(shift, i.InputRegister(1), Operand(mask));  \
      __ vdup(sz, tmp, shift);                            \
      __ vneg(sz, tmp, tmp);                              \
      __ vshl(dt, dst, src, tmp);                         \
    }                                                     \
  } while (0)

void CodeGenerator::AssembleDeconstructFrame() {
  __ LeaveFrame(StackFrame::MANUAL);
  unwinding_info_writer_.MarkFrameDeconstructed(__ pc_offset());
}

void CodeGenerator::AssemblePrepareTailCall() {
  if (frame_access_state()->has_frame()) {
    __ ldm(ia, fp, {lr, fp});
  }
  frame_access_state()->SetFrameAccessToSP();
}

namespace {

void FlushPendingPushRegisters(MacroAssembler* masm,
                               FrameAccessState* frame_access_state,
                               ZoneVector<Register>* pending_pushes) {
  switch (pending_pushes->size()) {
    case 0:
      break;
    case 1:
      masm->push((*pending_pushes)[0]);
      break;
    case 2:
      masm->Push((*pending_pushes)[0], (*pending_pushes)[1]);
      break;
    case 3:
      masm->Push((*pending_pushes)[0], (*pending_pushes)[1],
                 (*pending_pushes)[2]);
      break;
    default:
      UNREACHABLE();
  }
  frame_access_state->IncreaseSPDelta(pending_pushes->size());
  pending_pushes->clear();
}

void AdjustStackPointerForTailCall(
    MacroAssembler* masm, FrameAccessState* state, int new_slot_above_sp,
    ZoneVector<Register>* pending_pushes = nullptr,
    bool allow_shrinkage = true) {
  int current_sp_offset = state->GetSPToFPSlotCount() +
                          StandardFrameConstants::kFixedSlotCountAboveFp;
  int stack_slot_delta = new_slot_above_sp - current_sp_offset;
  if (stack_slot_delta > 0) {
    if (pending_pushes != nullptr) {
      FlushPendingPushRegisters(masm, state, pending_pushes);
    }
    masm->AllocateStackSpace(stack_slot_delta * kSystemPointerSize);
    state->IncreaseSPDelta(stack_slot_delta);
  } else if (allow_shrinkage && stack_slot_delta < 0) {
    if (pending_pushes != nullptr) {
      FlushPendingPushRegisters(masm, state, pending_pushes);
    }
    masm->add(sp, sp, Operand(-stack_slot_delta * kSystemPointerSize));
    state->IncreaseSPDelta(stack_slot_delta);
  }
}

#if DEBUG
bool VerifyOutputOfAtomicPairInstr(ArmOperandConverter* converter,
                                   const Instruction* instr, Register low,
                                   Register high) {
  DCHECK_GE(instr->OutputCount() + instr->TempCount(), 2);
  if (instr->OutputCount() == 2) {
    return (converter->OutputRegister(0) == low &&
            converter->OutputRegister(1) == high);
  }
  if (instr->OutputCount() == 1) {
    return (converter->OutputRegister(0) == low &&
            converter->TempRegister(instr->TempCount() - 1) == high) ||
           (converter->OutputRegister(0) == high &&
            converter->TempRegister(instr->TempCount() - 1) == low);
  }
  DCHECK_EQ(instr->OutputCount(), 0);
  return (converter->TempRegister(instr->TempCount() - 2) == low &&
          converter->TempRegister(instr->TempCount() - 1) == high);
}
#endif

}  // namespace

void CodeGenerator::AssembleTailCallBeforeGap(Instruction* instr,
                                              int first_unused_slot_offset) {
  ZoneVector<MoveOperands*> pushes(zone());
  GetPushCompatibleMoves(instr, kRegisterPush, &pushes);

  if (!pushes.empty() &&
      (LocationOperand::cast(pushes.back()->destination()).index() + 1 ==
       first_unused_slot_offset)) {
    ArmOperandConverter g(this, instr);
    ZoneVector<Register> pending_pushes(zone());
    for (auto move : pushes) {
      LocationOperand destination_location(
          LocationOperand::cast(move->destination()));
      InstructionOperand source(move->source());
      AdjustStackPointerForTailCall(
          masm(), frame_access_state(),
          destination_location.index() - pending_pushes.size(),
          &pending_pushes);
      // Pushes of non-register data types are not supported.
      DCHECK(source.IsRegister());
      LocationOperand source_location(LocationOperand::cast(source));
      pending_pushes.push_back(source_location.GetRegister());
      // TODO(arm): We can push more than 3 registers at once. Add support in
      // the macro-assembler for pushing a list of registers.
      if (pending_pushes.size() == 3) {
        FlushPendingPushRegisters(masm(), frame_access_state(),
                                  &pending_pushes);
      }
      move->Eliminate();
    }
    FlushPendingPushRegisters(masm(), frame_access_state(), &pending_pushes);
  }
  AdjustStackPointerForTailCall(masm(), frame_access_state(),
                                first_unused_slot_offset, nullptr, false);
}

void CodeGenerator::AssembleTailCallAfterGap(Instruction* instr,
                                             int first_unused_slot_offset) {
  AdjustStackPointerForTailCall(masm(), frame_access_state(),
                                first_unused_slot_offset);
}

// Check that {kJavaScriptCallCodeStartRegister} is correct.
void CodeGenerator::AssembleCodeStartRegisterCheck() {
  UseScratchRegisterScope temps(masm());
  Register scratch = temps.Acquire();
  __ ComputeCodeStartAddress(scratch);
  __ cmp(scratch, kJavaScriptCallCodeStartRegister);
  __ Assert(eq, AbortReason::kWrongFunctionCodeStart);
}

#ifdef V8_ENABLE_LEAPTIERING
void CodeGenerator::AssembleDispatchHandleRegisterCheck() {
  CHECK(!V8_JS_LINKAGE_INCLUDES_DISPATCH_HANDLE_BOOL);
}
#endif  // V8_ENABLE_LEAPTIERING

// Check if the code object is marked for deoptimization. If it is, then it
// jumps to the CompileLazyDeoptimizedCode builtin. In order to do this we need
// to:
//    1. read from memory the word that contains that bit, which can be found in
//       the flags in the referenced {Code} object;
//    2. test kMarkedForDeoptimizationBit in those flags; and
//    3. if it is not zero then it jumps to the builtin.
void CodeGenerator::BailoutIfDeoptimized() { __ BailoutIfDeoptimized(); }

// Assembles an instruction after register allocation, producing machine code.
CodeGenerator::CodeGenResult CodeGenerator::AssembleArchInstruction(
    Instruction* instr) {
  ArmOperandConverter i(this, instr);

  __ MaybeCheckConstPool();
  InstructionCode opcode = instr->opcode();
  ArchOpcode arch_opcode = ArchOpcodeField::decode(opcode);
  switch (arch_opcode) {
    case kArchCallCodeObject: {
      if (instr->InputAt(0)->IsImmediate()) {
        __ Call(i.InputCode(0), RelocInfo::CODE_TARGET);
      } else {
        Register reg = i.InputRegister(0);
        DCHECK_IMPLIES(
            instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ CallCodeObject(reg);
      }
      RecordCallPosition(instr);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchCallBuiltinPointer: {
      DCHECK(!instr->InputAt(0)->IsImmediate());
      Register builtin_index = i.InputRegister(0);
      Register target =
          instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister)
              ? kJavaScriptCallCodeStartRegister
              : builtin_index;
      __ CallBuiltinByIndex(builtin_index, target);
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
#if V8_ENABLE_WEBASSEMBLY
    case kArchCallWasmFunction:
    case kArchCallWasmFunctionIndirect: {
      if (instr->InputAt(0)->IsImmediate()) {
        DCHECK_EQ(arch_opcode, kArchCallWasmFunction);
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt32());
        __ Call(wasm_code, constant.rmode());
      } else if (arch_opcode == kArchCallWasmFunctionIndirect) {
        __ CallWasmCodePointer(i.InputRegister(0));
      } else {
        __ Call(i.InputRegister(0));
      }
      RecordCallPosition(instr);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchTailCallWasm:
    case kArchTailCallWasmIndirect: {
      if (instr->InputAt(0)->IsImmediate()) {
        DCHECK_EQ(arch_opcode, kArchTailCallWasm);
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt32());
        __ Jump(wasm_code, constant.rmode());
      } else if (arch_opcode == kArchTailCallWasmIndirect) {
        __ CallWasmCodePointer(i.InputRegister(0), CallJumpMode::kTailCall);
      } else {
        __ Jump(i.InputRegister(0));
      }
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      unwinding_info_writer_.MarkBlockWillExit();
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    case kArchTailCallCodeObject: {
      if (instr->InputAt(0)->IsImmediate()) {
        __ Jump(i.InputCode(0), RelocInfo::CODE_TARGET);
      } else {
        Register reg = i.InputRegister(0);
        DCHECK_IMPLIES(
            instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ JumpCodeObject(reg);
      }
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      unwinding_info_writer_.MarkBlockWillExit();
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchTailCallAddress: {
      CHECK(!instr->InputAt(0)->IsImmediate());
      Register reg = i.InputRegister(0);
      DCHECK_IMPLIES(
          instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
          reg == kJavaScriptCallCodeStartRegister);
      __ Jump(reg);
      unwinding_info_writer_.MarkBlockWillExit();
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchCallJSFunction: {
      Register func = i.InputRegister(0);
      if (v8_flags.debug_code) {
        UseScratchRegisterScope temps(masm());
        Register scratch = temps.Acquire();
        // Check the function's context matches the context argument.
        __ ldr(scratch, FieldMemOperand(func, JSFunction::kContextOffset));
        __ cmp(cp, scratch);
        __ Assert(eq, AbortReason::kWrongFunctionContext);
      }
      uint32_t num_arguments =
          i.InputUint32(instr->JSCallArgumentCountInputIndex());
      __ CallJSFunction(func, num_arguments);
      RecordCallPosition(instr);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchPrepareCallCFunction: {
      int const num_gp_parameters = ParamField::decode(instr->opcode());
      int const num_fp_parameters = FPParamField::decode(instr->opcode());
      __ PrepareCallCFunction(num_gp_parameters + num_fp_parameters);
      // Frame alignment requires using FP-relative frame addressing.
      frame_access_state()->SetFrameAccessToFP();
      break;
    }
    case kArchSaveCallerRegisters: {
      fp_mode_ =
          static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode()));
      DCHECK(fp_mode_ == SaveFPRegsMode::kIgnore ||
             fp_mode_ == SaveFPRegsMode::kSave);
      // kReturnRegister0 should have been saved before entering the stub.
      int bytes = __ PushCallerSaved(fp_mode_, kReturnRegister0);
      DCHECK(IsAligned(bytes, kSystemPointerSize));
      DCHECK_EQ(0, frame_access_state()->sp_delta());
      frame_access_state()->IncreaseSPDelta(bytes / kSystemPointerSize);
      DCHECK(!caller_registers_saved_);
      caller_registers_saved_ = true;
      break;
    }
    case kArchRestoreCallerRegisters: {
      DCHECK(fp_mode_ ==
             static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode())));
      DCHECK(fp_mode_ == SaveFPRegsMode::kIgnore ||
             fp_mode_ == SaveFPRegsMode::kSave);
      // Don't overwrite the returned value.
      int bytes = __ PopCallerSaved(fp_mode_, kReturnRegister0);
      frame_access_state()->IncreaseSPDelta(-(bytes / kSystemPointerSize));
      DCHECK_EQ(0, frame_access_state()->sp_delta());
      DCHECK(caller_registers_saved_);
      caller_registers_saved_ = false;
      break;
    }
    case kArchPrepareTailCall:
      AssemblePrepareTailCall();
      break;
    case kArchCallCFunctionWithFrameState:
    case kArchCallCFunction: {
      int const num_parameters = ParamField::decode(instr->opcode()) +
                                 FPParamField::decode(instr->opcode());
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes;
      Label return_location;
#if V8_ENABLE_WEBASSEMBLY
      if (linkage()->GetIncomingDescriptor()->IsWasmCapiFunction()) {
        // Put the return address in a stack slot.
        Register pc_scratch = r5;
        __ Push(pc_scratch);
        __ GetLabelAddress(pc_scratch, &return_location);
        __ str(pc_scratch,
               MemOperand(fp, WasmExitFrameConstants::kCallingPCOffset));
        __ Pop(pc_scratch);
        set_isolate_data_slots = SetIsolateDataSlots::kNo;
      }
#endif  // V8_ENABLE_WEBASSEMBLY
      int pc_offset;
      if (instr->InputAt(0)->IsImmediate()) {
        ExternalReference ref = i.InputExternalReference(0);
        pc_offset = __ CallCFunction(ref, num_parameters,
                                     set_isolate_data_slots, &return_location);
      } else {
        Register func = i.InputRegister(0);
        pc_offset = __ CallCFunction(func, num_parameters,
                                     set_isolate_data_slots, &return_location);
      }
      RecordSafepoint(instr->reference_map(), pc_offset);

      bool const needs_frame_state =
          (arch_opcode == kArchCallCFunctionWithFrameState);
      if (needs_frame_state) {
        RecordDeoptInfo(instr, pc_offset);
      }

      frame_access_state()->SetFrameAccessToDefault();
      // Ideally, we should decrement SP delta to match the change of stack
      // pointer in CallCFunction. However, for certain architectures (e.g.
      // ARM), there may be more strict alignment requirement, causing old SP
      // to be saved on the stack. In those cases, we can not calculate the SP
      // delta statically.
      frame_access_state()->ClearSPDelta();
      if (caller_registers_saved_) {
        // Need to re-sync SP delta introduced in kArchSaveCallerRegisters.
        // Here, we assume the sequence to be:
        //   kArchSaveCallerRegisters;
        //   kArchCallCFunction;
        //   kArchRestoreCallerRegisters;
        int bytes =
            __ RequiredStackSizeForCallerSaved(fp_mode_, kReturnRegister0);
        frame_access_state()->IncreaseSPDelta(bytes / kSystemPointerSize);
      }
      break;
    }
    case kArchJmp:
      AssembleArchJump(i.InputRpo(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArchBinarySearchSwitch:
      AssembleArchBinarySearchSwitch(instr);
      break;
    case kArchTableSwitch:
      AssembleArchTableSwitch(instr);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArchAbortCSADcheck:
      DCHECK(i.InputRegister(0) == r1);
      {
        // We don't actually want to generate a pile of code for this, so just
        // claim there is a stack frame, without generating one.
        FrameScope scope(masm(), StackFrame::NO_FRAME_TYPE);
        __ CallBuiltin(Builtin::kAbortCSADcheck);
      }
      __ stop();
      unwinding_info_writer_.MarkBlockWillExit();
      break;
    case kArchDebugBreak:
      __ DebugBreak();
      break;
    case kArchComment:
      __ RecordComment(reinterpret_cast<const char*>(i.InputInt32(0)),
                       SourceLocation());
      break;
    case kArchThrowTerminator:
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      unwinding_info_writer_.MarkBlockWillExit();
      break;
    case kArchNop:
      // don't emit code for nops.
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArchDeoptimize: {
      DeoptimizationExit* exit =
          BuildTranslation(instr, -1, 0, 0, OutputFrameStateCombine::Ignore());
      __ b(exit->label());
      break;
    }
    case kArchRet:
      AssembleReturn(instr->InputAt(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArchFramePointer:
      __ mov(i.OutputRegister(), fp);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArchParentFramePointer:
      if (frame_access_state()->has_frame()) {
        __ ldr(i.OutputRegister(), MemOperand(fp, 0));
      } else {
        __ mov(i.OutputRegister(), fp);
      }
      break;
#if V8_ENABLE_WEBASSEMBLY
    case kArchStackPointer:
      // The register allocator expects an allocatable register for the output,
      // we cannot use sp directly.
      __ mov(i.OutputRegister(), sp);
      break;
    case kArchSetStackPointer:
      DCHECK(instr->InputAt(0)->IsRegister());
      __ mov(sp, i.InputRegister(0));
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    case kArchStackPointerGreaterThan: {
      // Potentially apply an offset to the current stack pointer before the
      // comparison to consider the size difference of an optimized frame versus
      // the contained unoptimized frames.

      Register lhs_register = sp;
      uint32_t offset;

      if (ShouldApplyOffsetToStackCheck(instr, &offset)) {
        lhs_register = i.TempRegister(0);
        __ sub(lhs_register, sp, Operand(offset));
      }

      constexpr size_t kValueIndex = 0;
      DCHECK(instr->InputAt(kValueIndex)->IsRegister());
      __ cmp(lhs_register, i.InputRegister(kValueIndex));
      break;
    }
    case kArchStackCheckOffset:
      __ Move(i.OutputRegister(), Smi::FromInt(GetStackCheckOffset()));
      break;
    case kArchTruncateDoubleToI:
      __ TruncateDoubleToI(isolate(), zone(), i.OutputRegister(),
                           i.InputDoubleRegister(0), DetermineStubCallMode());
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArchStoreWithWriteBarrier:  // Fall through.
    case kArchAtomicStoreWithWriteBarrier: {
      RecordWriteMode mode;
      if (arch_opcode == kArchStoreWithWriteBarrier) {
        mode = RecordWriteModeField::decode(instr->opcode());
      } else {
        mode = AtomicStoreRecordWriteModeField::decode(instr->opcode());
      }
      Register object = i.InputRegister(0);
      Register value = i.InputRegister(2);

      if (v8_flags.debug_code) {
        // Checking that |value| is not a cleared weakref: our write barrier
        // does not support that for now.
        __ cmp(value, Operand(kClearedWeakHeapObjectLower32));
        __ Check(ne, AbortReason::kOperandIsCleared);
      }

      AddressingMode addressing_mode =
          AddressingModeField::decode(instr->opcode());
      Operand offset(0);

      if (arch_opcode == kArchAtomicStoreWithWriteBarrier) {
        __ dmb(ISH);
      }
      if (addressing_mode == kMode_Offset_RI) {
        int32_t immediate = i.InputInt32(1);
        offset = Operand(immediate);
        __ str(value, MemOperand(object, immediate));
      } else {
        DCHECK_EQ(kMode_Offset_RR, addressing_mode);
        Register reg = i.InputRegister(1);
        offset = Operand(reg);
        __ str(value, MemOperand(object, reg));
      }
      if (arch_opcode == kArchAtomicStoreWithWriteBarrier &&
          AtomicMemoryOrderField::decode(instr->opcode()) ==
              AtomicMemoryOrder::kSeqCst) {
        __ dmb(ISH);
      }

      auto ool = zone()->New<OutOfLineRecordWrite>(
          this, object, offset, value, mode, DetermineStubCallMode(),
          &unwinding_info_writer_);
      if (mode > RecordWriteMode::kValueIsPointer) {
        __ JumpIfSmi(value, ool->exit());
      }
      __ CheckPageFlag(object, MemoryChunk::kPointersFromHereAreInterestingMask,
                       ne, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kArchStoreIndirectWithWriteBarrier:
      UNREACHABLE();
    case kArchStackSlot: {
      FrameOffset offset =
          frame_access_state()->GetFrameOffset(i.InputInt32(0));
      Register base = offset.from_stack_pointer() ? sp : fp;
      __ add(i.OutputRegister(0), base, Operand(offset.offset()));
      break;
    }
    case kIeee754Float64Acos:
      ASSEMBLE_IEEE754_UNOP(acos);
      break;
    case kIeee754Float64Acosh:
      ASSEMBLE_IEEE754_UNOP(acosh);
      break;
    case kIeee754Float64Asin:
      ASSEMBLE_IEEE754_UNOP(asin);
      break;
    case kIeee754Float64Asinh:
      ASSEMBLE_IEEE754_UNOP(asinh);
      break;
    case kIeee754Float64Atan:
      ASSEMBLE_IEEE754_UNOP(atan);
      break;
    case kIeee754Float64Atanh:
      ASSEMBLE_IEEE754_UNOP(atanh);
      break;
    case kIeee754Float64Atan2:
      ASSEMBLE_IEEE754_BINOP(atan2);
      break;
    case kIeee754Float64Cbrt:
      ASSEMBLE_IEEE754_UNOP(cbrt);
      break;
    case kIeee754Float64Cos:
      ASSEMBLE_IEEE754_UNOP(cos);
      break;
    case kIeee754Float64Cosh:
      ASSEMBLE_IEEE754_UNOP(cosh);
      break;
    case kIeee754Float64Exp:
      ASSEMBLE_IEEE754_UNOP(exp);
      break;
    case kIeee754Float64Expm1:
      ASSEMBLE_IEEE754_UNOP(expm1);
      break;
    case kIeee754Float64Log:
      ASSEMBLE_IEEE754_UNOP(log);
      break;
    case kIeee754Float64Log1p:
      ASSEMBLE_IEEE754_UNOP(log1p);
      break;
    case kIeee754Float64Log2:
      ASSEMBLE_IEEE754_UNOP(log2);
      break;
    case kIeee754Float64Log10:
      ASSEMBLE_IEEE754_UNOP(log10);
      break;
    case kIeee754Float64Pow:
      ASSEMBLE_IEEE754_BINOP(pow);
      break;
    case kIeee754Float64Sin:
      ASSEMBLE_IEEE754_UNOP(sin);
      break;
    case kIeee754Float64Sinh:
      ASSEMBLE_IEEE754_UNOP(sinh);
      break;
    case kIeee754Float64Tan:
      ASSEMBLE_IEEE754_UNOP(tan);
      break;
    case kIeee754Float64Tanh:
      ASSEMBLE_IEEE754_UNOP(tanh);
      break;
    case kArmAdd:
      __ add(i.OutputRegister(), i.InputRegister(0), i.InputOperand2(1),
             i.OutputSBit());
      break;
    case kArmAnd:
      __ and_(i.OutputRegister(), i.InputRegister(0), i.InputOperand2(1),
              i.OutputSBit());
      break;
    case kArmBic:
      __ bic(i.OutputRegister(), i.InputRegister(0), i.InputOperand2(1),
             i.OutputSBit());
      break;
    case kArmMul:
      __ mul(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
             i.OutputSBit());
      break;
    case kArmMla:
      __ mla(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
             i.InputRegister(2), i.OutputSBit());
      break;
    case kArmMls: {
      CpuFeatureScope scope(masm(), ARMv7);
      __ mls(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
             i.InputRegister(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmSmull:
      __ smull(i.OutputRegister(0), i.OutputRegister(1), i.InputRegister(0),
               i.InputRegister(1));
      break;
    case kArmSmmul:
      __ smmul(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmSmmla:
      __ smmla(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.InputRegister(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmUmull:
      __ umull(i.OutputRegister(0), i.OutputRegister(1), i.InputRegister(0),
               i.InputRegister(1), i.OutputSBit());
      break;
    case kArmSdiv: {
      CpuFeatureScope scope(masm(), SUDIV);
      __ sdiv(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmUdiv: {
      CpuFeatureScope scope(masm(), SUDIV);
      __ udiv(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmMov:
      __ Move(i.OutputRegister(), i.InputOperand2(0), i.OutputSBit());
      break;
    case kArmMvn:
      __ mvn(i.OutputRegister(), i.InputOperand2(0), i.OutputSBit());
      break;
    case kArmOrr:
      __ orr(i.OutputRegister(), i.InputRegister(0), i.InputOperand2(1),
             i.OutputSBit());
      break;
    case kArmEor:
      __ eor(i.OutputRegister(), i.InputRegister(0), i.InputOperand2(1),
             i.OutputSBit());
      break;
    case kArmSub:
      __ sub(i.OutputRegister(), i.InputRegister(0), i.InputOperand2(1),
             i.OutputSBit());
      break;
    case kArmRsb:
      __ rsb(i.OutputRegister(), i.InputRegister(0), i.InputOperand2(1),
             i.OutputSBit());
      break;
    case kArmBfc: {
      CpuFeatureScope scope(masm(), ARMv7);
      __ bfc(i.OutputRegister(), i.InputInt8(1), i.InputInt8(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmUbfx: {
      CpuFeatureScope scope(masm(), ARMv7);
      __ ubfx(i.OutputRegister(), i.InputRegister(0), i.InputInt8(1),
              i.InputInt8(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmSbfx: {
      CpuFeatureScope scope(masm(), ARMv7);
      __ sbfx(i.OutputRegister(), i.InputRegister(0), i.InputInt8(1),
              i.InputInt8(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmSxtb:
      __ sxtb(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmSxth:
      __ sxth(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmSxtab:
      __ sxtab(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.InputInt32(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmSxtah:
      __ sxtah(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.InputInt32(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmUxtb:
      __ uxtb(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmUxth:
      __ uxth(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmUxtab:
      __ uxtab(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.InputInt32(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmUxtah:
      __ uxtah(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.InputInt32(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmRbit: {
      CpuFeatureScope scope(masm(), ARMv7);
      __ rbit(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmRev:
      __ rev(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmClz:
      __ clz(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmCmp:
      __ cmp(i.InputRegister(0), i.InputOperand2(1));
      DCHECK_EQ(SetCC, i.OutputSBit());
      break;
    case kArmCmn:
      __ cmn(i.InputRegister(0), i.InputOperand2(1));
      DCHECK_EQ(SetCC, i.OutputSBit());
      break;
    case kArmTst:
      __ tst(i.InputRegister(0), i.InputOperand2(1));
      DCHECK_EQ(SetCC, i.OutputSBit());
      break;
    case kArmTeq:
      __ teq(i.InputRegister(0), i.InputOperand2(1));
      DCHECK_EQ(SetCC, i.OutputSBit());
      break;
    case kArmAddPair:
      // i.InputRegister(0) ... left low word.
      // i.InputRegister(1) ... left high word.
      // i.InputRegister(2) ... right low word.
      // i.InputRegister(3) ... right high word.
      __ add(i.OutputRegister(0), i.InputRegister(0), i.InputRegister(2),
             SetCC);
      __ adc(i.OutputRegister(1), i.InputRegister(1),
             Operand(i.InputRegister(3)));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmSubPair:
      // i.InputRegister(0) ... left low word.
      // i.InputRegister(1) ... left high word.
      // i.InputRegister(2) ... right low word.
      // i.InputRegister(3) ... right high word.
      __ sub(i.OutputRegister(0), i.InputRegister(0), i.InputRegister(2),
             SetCC);
      __ sbc(i.OutputRegister(1), i.InputRegister(1),
             Operand(i.InputRegister(3)));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmMulPair:
      // i.InputRegister(0) ... left low word.
      // i.InputRegister(1) ... left high word.
      // i.InputRegister(2) ... right low word.
      // i.InputRegister(3) ... right high word.
      __ umull(i.OutputRegister(0), i.OutputRegister(1), i.InputRegister(0),
               i.InputRegister(2));
      __ mla(i.OutputRegister(1), i.InputRegister(0), i.InputRegister(3),
             i.OutputRegister(1));
      __ mla(i.OutputRegister(1), i.InputRegister(2), i.InputRegister(1),
             i.OutputRegister(1));
      break;
    case kArmLslPair: {
      Register second_output =
          instr->OutputCount() >= 2 ? i.OutputRegister(1) : i.TempRegister(0);
      if (instr->InputAt(2)->IsImmediate()) {
        __ LslPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), i.InputInt32(2));
      } else {
        __ LslPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), i.InputRegister(2));
      }
      break;
    }
    case kArmLsrPair: {
      Register second_output =
          instr->OutputCount() >= 2 ? i.OutputRegister(1) : i.TempRegister(0);
      if (instr->InputAt(2)->IsImmediate()) {
        __ LsrPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), i.InputInt32(2));
      } else {
        __ LsrPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), i.InputRegister(2));
      }
      break;
    }
    case kArmAsrPair: {
      Register second_output =
          instr->OutputCount() >= 2 ? i.OutputRegister(1) : i.TempRegister(0);
      if (instr->InputAt(2)->IsImmediate()) {
        __ AsrPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), i.InputInt32(2));
      } else {
        __ AsrPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), i.InputRegister(2));
      }
      break;
    }
    case kArmVcmpF32:
      if (instr->InputAt(1)->IsFPRegister()) {
        __ VFPCompareAndSetFlags(i.InputFloatRegister(0),
                                 i.InputFloatRegister(1));
      } else {
        DCHECK(instr->InputAt(1)->IsImmediate());
        // 0.0 is the only immediate supported by vcmp instructions.
        DCHECK_EQ(0.0f, i.InputFloat32(1));
        __ VFPCompareAndSetFlags(i.InputFloatRegister(0), i.InputFloat32(1));
      }
      DCHECK_EQ(SetCC, i.OutputSBit());
      break;
    case kArmVaddF32:
      __ vadd(i.OutputFloatRegister(), i.InputFloatRegister(0),
              i.InputFloatRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVsubF32:
      __ vsub(i.OutputFloatRegister(), i.InputFloatRegister(0),
              i.InputFloatRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmulF32:
      __ vmul(i.OutputFloatRegister(), i.InputFloatRegister(0),
              i.InputFloatRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmlaF32:
      __ vmla(i.OutputFloatRegister(), i.InputFloatRegister(1),
              i.InputFloatRegister(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmlsF32:
      __ vmls(i.OutputFloatRegister(), i.InputFloatRegister(1),
              i.InputFloatRegister(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVdivF32:
      __ vdiv(i.OutputFloatRegister(), i.InputFloatRegister(0),
              i.InputFloatRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVsqrtF32:
      __ vsqrt(i.OutputFloatRegister(), i.InputFloatRegister(0));
      break;
    case kArmVabsF32:
      __ vabs(i.OutputFloatRegister(), i.InputFloatRegister(0));
      break;
    case kArmVnegF32:
      __ vneg(i.OutputFloatRegister(), i.InputFloatRegister(0));
      break;
    case kArmVcmpF64:
      if (instr->InputAt(1)->IsFPRegister()) {
        __ VFPCompareAndSetFlags(i.InputDoubleRegister(0),
                                 i.InputDoubleRegister(1));
      } else {
        DCHECK(instr->InputAt(1)->IsImmediate());
        // 0.0 is the only immediate supported by vcmp instructions.
        DCHECK_EQ(0.0, i.InputDouble(1));
        __ VFPCompareAndSetFlags(i.InputDoubleRegister(0), i.InputDouble(1));
      }
      DCHECK_EQ(SetCC, i.OutputSBit());
      break;
    case kArmVaddF64:
      __ vadd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVsubF64:
      __ vsub(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmulF64:
      __ vmul(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmlaF64:
      __ vmla(i.OutputDoubleRegister(), i.InputDoubleRegister(1),
              i.InputDoubleRegister(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmlsF64:
      __ vmls(i.OutputDoubleRegister(), i.InputDoubleRegister(1),
              i.InputDoubleRegister(2));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVdivF64:
      __ vdiv(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmodF64: {
      // TODO(bmeurer): We should really get rid of this special instruction,
      // and generate a CallAddress instruction instead.
      FrameScope scope(masm(), StackFrame::MANUAL);
      __ PrepareCallCFunction(0, 2);
      __ MovToFloatParameters(i.InputDoubleRegister(0),
                              i.InputDoubleRegister(1));
      __ CallCFunction(ExternalReference::mod_two_doubles_operation(), 0, 2);
      // Move the result in the double result register.
      __ MovFromFloatResult(i.OutputDoubleRegister());
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVsqrtF64:
      __ vsqrt(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kArmVabsF64:
      __ vabs(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kArmVnegF64:
      __ vneg(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kArmVrintmF32: {
      CpuFeatureScope scope(masm(), ARMv8);
      if (instr->InputAt(0)->IsSimd128Register()) {
        __ vrintm(NeonS32, i.OutputSimd128Register(),
                  i.InputSimd128Register(0));
      } else {
        __ vrintm(i.OutputFloatRegister(), i.InputFloatRegister(0));
      }
      break;
    }
    case kArmVrintmF64: {
      CpuFeatureScope scope(masm(), ARMv8);
      __ vrintm(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kArmVrintpF32: {
      CpuFeatureScope scope(masm(), ARMv8);
      if (instr->InputAt(0)->IsSimd128Register()) {
        __ vrintp(NeonS32, i.OutputSimd128Register(),
                  i.InputSimd128Register(0));
      } else {
        __ vrintp(i.OutputFloatRegister(), i.InputFloatRegister(0));
      }
      break;
    }
    case kArmVrintpF64: {
      CpuFeatureScope scope(masm(), ARMv8);
      __ vrintp(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kArmVrintzF32: {
      CpuFeatureScope scope(masm(), ARMv8);
      if (instr->InputAt(0)->IsSimd128Register()) {
        __ vrintz(NeonS32, i.OutputSimd128Register(),
                  i.InputSimd128Register(0));
      } else {
        __ vrintz(i.OutputFloatRegister(), i.InputFloatRegister(0));
      }
      break;
    }
    case kArmVrintzF64: {
      CpuFeatureScope scope(masm(), ARMv8);
      __ vrintz(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kArmVrintaF64: {
      CpuFeatureScope scope(masm(), ARMv8);
      __ vrinta(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kArmVrintnF32: {
      CpuFeatureScope scope(masm(), ARMv8);
      if (instr->InputAt(0)->IsSimd128Register()) {
        __ vrintn(NeonS32, i.OutputSimd128Register(),
                  i.InputSimd128Register(0));
      } else {
        __ vrintn(i.OutputFloatRegister(), i.InputFloatRegister(0));
      }
      break;
    }
    case kArmVrintnF64: {
      CpuFeatureScope scope(masm(), ARMv8);
      __ vrintn(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kArmVcvtF32F64: {
      __ vcvt_f32_f64(i.OutputFloatRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVcvtF64F32: {
      __ vcvt_f64_f32(i.OutputDoubleRegister(), i.InputFloatRegister(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVcvtF32S32: {
      UseScratchRegisterScope temps(masm());
      SwVfpRegister scratch = temps.AcquireS();
      __ vmov(scratch, i.InputRegister(0));
      __ vcvt_f32_s32(i.OutputFloatRegister(), scratch);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVcvtF32U32: {
      UseScratchRegisterScope temps(masm());
      SwVfpRegister scratch = temps.AcquireS();
      __ vmov(scratch, i.InputRegister(0));
      __ vcvt_f32_u32(i.OutputFloatRegister(), scratch);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVcvtF64S32: {
      UseScratchRegisterScope temps(masm());
      SwVfpRegister scratch = temps.AcquireS();
      __ vmov(scratch, i.InputRegister(0));
      __ vcvt_f64_s32(i.OutputDoubleRegister(), scratch);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVcvtF64U32: {
      UseScratchRegisterScope temps(masm());
      SwVfpRegister scratch = temps.AcquireS();
      __ vmov(scratch, i.InputRegister(0));
      __ vcvt_f64_u32(i.OutputDoubleRegister(), scratch);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVcvtS32F32: {
      UseScratchRegisterScope temps(masm());
      SwVfpRegister scratch = temps.AcquireS();
      __ vcvt_s32_f32(scratch, i.InputFloatRegister(0));
      __ vmov(i.OutputRegister(), scratch);
      bool set_overflow_to_min_i32 = MiscField::decode(instr->opcode());
      if (set_overflow_to_min_i32) {
        // Avoid INT32_MAX as an overflow indicator and use INT32_MIN instead,
        // because INT32_MIN allows easier out-of-bounds detection.
        __ cmn(i.OutputRegister(), Operand(1));
        __ mov(i.OutputRegister(), Operand(INT32_MIN), LeaveCC, vs);
      }
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVcvtU32F32: {
      UseScratchRegisterScope temps(masm());
      SwVfpRegister scratch = temps.AcquireS();
      __ vcvt_u32_f32(scratch, i.InputFloatRegister(0));
      __ vmov(i.OutputRegister(), scratch);
      bool set_overflow_to_min_u32 = MiscField::decode(instr->opcode());
      if (set_overflow_to_min_u32) {
        // Avoid UINT32_MAX as an overflow indicator and use 0 instead,
        // because 0 allows easier out-of-bounds detection.
        __ cmn(i.OutputRegister(), Operand(1));
        __ adc(i.OutputRegister(), i.OutputRegister(), Operand::Zero());
      }
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVcvtS32F64: {
      UseScratchRegisterScope temps(masm());
      SwVfpRegister scratch = temps.AcquireS();
      __ vcvt_s32_f64(scratch, i.InputDoubleRegister(0));
      __ vmov(i.OutputRegister(), scratch);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVcvtU32F64: {
      UseScratchRegisterScope temps(masm());
      SwVfpRegister scratch = temps.AcquireS();
      __ vcvt_u32_f64(scratch, i.InputDoubleRegister(0));
      __ vmov(i.OutputRegister(), scratch);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVmovU32F32:
      __ vmov(i.OutputRegister(), i.InputFloatRegister(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmovF32U32:
      __ vmov(i.OutputFloatRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmovLowU32F64:
      __ VmovLow(i.OutputRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmovLowF64U32:
      __ VmovLow(i.OutputDoubleRegister(), i.InputRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmovHighU32F64:
      __ VmovHigh(i.OutputRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmovHighF64U32:
      __ VmovHigh(i.OutputDoubleRegister(), i.InputRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmovF64U32U32:
      __ vmov(i.OutputDoubleRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVmovU32U32F64:
      __ vmov(i.OutputRegister(0), i.OutputRegister(1),
              i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVcnt: {
      __ vcnt(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmLdrb:
      __ ldrb(i.OutputRegister(), i.InputOffset());
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmLdrsb:
      __ ldrsb(i.OutputRegister(), i.InputOffset());
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmStrb:
      __ strb(i.InputRegister(0), i.InputOffset(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmLdrh:
      __ ldrh(i.OutputRegister(), i.InputOffset());
      break;
    case kArmLdrsh:
      __ ldrsh(i.OutputRegister(), i.InputOffset());
      break;
    case kArmStrh:
      __ strh(i.InputRegister(0), i.InputOffset(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmLdr:
      __ ldr(i.OutputRegister(), i.InputOffset());
      break;
    case kArmStr:
      __ str(i.InputRegister(0), i.InputOffset(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVldrF32: {
      __ vldr(i.OutputFloatRegister(), i.InputOffset());
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVstrF32:
      __ vstr(i.InputFloatRegister(0), i.InputOffset(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmVld1F64: {
      __ vld1(Neon8, NeonListOperand(i.OutputDoubleRegister()),
              i.NeonInputOperand(0));
      break;
    }
    case kArmVst1F64: {
      __ vst1(Neon8, NeonListOperand(i.InputDoubleRegister(0)),
              i.NeonInputOperand(1));
      break;
    }
    case kArmVld1S128: {
      __ vld1(Neon8, NeonListOperand(i.OutputSimd128Register()),
              i.NeonInputOperand(0));
      break;
    }
    case kArmVst1S128: {
      __ vst1(Neon8, NeonListOperand(i.InputSimd128Register(0)),
              i.NeonInputOperand(1));
      break;
    }
    case kArmVldrF64: {
      __ vldr(i.OutputDoubleRegister(), i.InputOffset());
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmVstrF64:
      __ vstr(i.InputDoubleRegister(0), i.InputOffset(1));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    case kArmFloat32Max: {
      SwVfpRegister result = i.OutputFloatRegister();
      SwVfpRegister left = i.InputFloatRegister(0);
      SwVfpRegister right = i.InputFloatRegister(1);
      if (left == right) {
        __ Move(result, left);
      } else {
        auto ool = zone()->New<OutOfLineFloat32Max>(this, result, left, right);
        __ FloatMax(result, left, right, ool->entry());
        __ bind(ool->exit());
      }
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmFloat64Max: {
      DwVfpRegister result = i.OutputDoubleRegister();
      DwVfpRegister left = i.InputDoubleRegister(0);
      DwVfpRegister right = i.InputDoubleRegister(1);
      if (left == right) {
        __ Move(result, left);
      } else {
        auto ool = zone()->New<OutOfLineFloat64Max>(this, result, left, right);
        __ FloatMax(result, left, right, ool->entry());
        __ bind(ool->exit());
      }
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmFloat32Min: {
      SwVfpRegister result = i.OutputFloatRegister();
      SwVfpRegister left = i.InputFloatRegister(0);
      SwVfpRegister right = i.InputFloatRegister(1);
      if (left == right) {
        __ Move(result, left);
      } else {
        auto ool = zone()->New<OutOfLineFloat32Min>(this, result, left, right);
        __ FloatMin(result, left, right, ool->entry());
        __ bind(ool->exit());
      }
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmFloat64Min: {
      DwVfpRegister result = i.OutputDoubleRegister();
      DwVfpRegister left = i.InputDoubleRegister(0);
      DwVfpRegister right = i.InputDoubleRegister(1);
      if (left == right) {
        __ Move(result, left);
      } else {
        auto ool = zone()->New<OutOfLineFloat64Min>(this, result, left, right);
        __ FloatMin(result, left, right, ool->entry());
        __ bind(ool->exit());
      }
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmFloat64SilenceNaN: {
      DwVfpRegister value = i.InputDoubleRegister(0);
      DwVfpRegister result = i.OutputDoubleRegister();
      __ VFPCanonicalizeNaN(result, value);
      break;
    }
    case kArmPush: {
      int stack_decrement = i.InputInt32(0);
      int slots = stack_decrement / kSystemPointerSize;
      LocationOperand* op = LocationOperand::cast(instr->InputAt(1));
      MachineRepresentation rep = op->representation();
      int pushed_slots = ElementSizeInPointers(rep);
      // Slot-sized arguments are never padded but there may be a gap if
      // the slot allocator reclaimed other padding slots. Adjust the stack
      // here to skip any gap.
      __ AllocateStackSpace((slots - pushed_slots) * kSystemPointerSize);
      switch (rep) {
        case MachineRepresentation::kFloat32:
          __ vpush(i.InputFloatRegister(1));
          break;
        case MachineRepresentation::kFloat64:
          __ vpush(i.InputDoubleRegister(1));
          break;
        case MachineRepresentation::kSimd128:
          __ vpush(i.InputSimd128Register(1));
          break;
        default:
          __ push(i.InputRegister(1));
          break;
      }
      frame_access_state()->IncreaseSPDelta(slots);
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmPoke: {
      int const slot = MiscField::decode(instr->opcode());
      __ str(i.InputRegister(0), MemOperand(sp, slot * kSystemPointerSize));
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmPeek: {
      int reverse_slot = i.InputInt32(0);
      int offset =
          FrameSlotToFPOffset(frame()->GetTotalFrameSlotCount() - reverse_slot);
      if (instr->OutputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->OutputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ vldr(i.OutputDoubleRegister(), MemOperand(fp, offset));
        } else if (op->representation() == MachineRepresentation::kFloat32) {
          __ vldr(i.OutputFloatRegister(), MemOperand(fp, offset));
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
          UseScratchRegisterScope temps(masm());
          Register scratch = temps.Acquire();
          __ add(scratch, fp, Operand(offset));
          __ vld1(Neon8, NeonListOperand(i.OutputSimd128Register()),
                  NeonMemOperand(scratch));
        }
      } else {
        __ ldr(i.OutputRegister(), MemOperand(fp, offset));
      }
      break;
    }
    case kArmDmbIsh: {
      __ dmb(ISH);
      break;
    }
    case kArmDsbIsb: {
      __ dsb(SY);
      __ isb(SY);
      break;
    }
    case kArmVmullLow: {
      auto dt = static_cast<NeonDataType>(MiscField::decode(instr->opcode()));
      __ vmull(dt, i.OutputSimd128Register(), i.InputSimd128Register(0).low(),
               i.InputSimd128Register(1).low());
      break;
    }
    case kArmVmullHigh: {
      auto dt = static_cast<NeonDataType>(MiscField::decode(instr->opcode()));
      __ vmull(dt, i.OutputSimd128Register(), i.InputSimd128Register(0).high(),
               i.InputSimd128Register(1).high());
      break;
    }
    case kArmVpadal: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      auto dt = static_cast<NeonDataType>(MiscField::decode(instr->opcode()));
      __ vpadal(dt, i.OutputSimd128Register(), i.InputSimd128Register(1));
      break;
    }
    case kArmVpaddl: {
      auto dt = static_cast<NeonDataType>(MiscField::decode(instr->opcode()));
      __ vpaddl(dt, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmF64x2Splat: {
      Simd128Register dst = i.OutputSimd128Register();
      DoubleRegister src = i.InputDoubleRegister(0);
      __ Move(dst.low(), src);
      __ Move(dst.high(), src);
      break;
    }
    case kArmF64x2ExtractLane: {
      __ ExtractLane(i.OutputDoubleRegister(), i.InputSimd128Register(0),
                     i.InputInt8(1));
      break;
    }
    case kArmF64x2ReplaceLane: {
      __ ReplaceLane(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputDoubleRegister(2), i.InputInt8(1));
      break;
    }
    case kArmF64x2Abs: {
      __ vabs(i.OutputSimd128Register().low(), i.InputSimd128Register(0).low());
      __ vabs(i.OutputSimd128Register().high(),
              i.InputSimd128Register(0).high());
      break;
    }
    case kArmF64x2Neg: {
      __ vneg(i.OutputSimd128Register().low(), i.InputSimd128Register(0).low());
      __ vneg(i.OutputSimd128Register().high(),
              i.InputSimd128Register(0).high());
      break;
    }
    case kArmF64x2Sqrt: {
      __ vsqrt(i.OutputSimd128Register().low(),
               i.InputSimd128Register(0).low());
      __ vsqrt(i.OutputSimd128Register().high(),
               i.InputSimd128Register(0).high());
      break;
    }
    case kArmF64x2Add: {
      ASSEMBLE_F64X2_ARITHMETIC_BINOP(vadd);
      break;
    }
    case kArmF64x2Sub: {
      ASSEMBLE_F64X2_ARITHMETIC_BINOP(vsub);
      break;
    }
    case kArmF64x2Mul: {
      ASSEMBLE_F64X2_ARITHMETIC_BINOP(vmul);
      break;
    }
    case kArmF64x2Div: {
      ASSEMBLE_F64X2_ARITHMETIC_BINOP(vdiv);
      break;
    }
    case kArmF64x2Min: {
      Simd128Register result = i.OutputSimd128Register();
      Simd128Register left = i.InputSimd128Register(0);
      Simd128Register right = i.InputSimd128Register(1);
      if (left == right) {
        __ Move(result, left);
      } else {
        auto ool_low = zone()->New<OutOfLineFloat64Min>(
            this, result.low(), left.low(), right.low());
        auto ool_high = zone()->New<OutOfLineFloat64Min>(
            this, result.high(), left.high(), right.high());
        __ FloatMin(result.low(), left.low(), right.low(), ool_low->entry());
        __ bind(ool_low->exit());
        __ FloatMin(result.high(), left.high(), right.high(),
                    ool_high->entry());
        __ bind(ool_high->exit());
      }
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
    case kArmF64x2Max: {
      Simd128Register result = i.OutputSimd128Register();
      Simd128Register left = i.InputSimd128Register(0);
      Simd128Register right = i.InputSimd128Register(1);
      if (left == right) {
        __ Move(result, left);
      } else {
        auto ool_low = zone()->New<OutOfLineFloat64Max>(
            this, result.low(), left.low(), right.low());
        auto ool_high = zone()->New<OutOfLineFloat64Max>(
            this, result.high(), left.high(), right.high());
        __ FloatMax(result.low(), left.low(), right.low(), ool_low->entry());
        __ bind(ool_low->exit());
        __ FloatMax(result.high(), left.high(), right.high(),
                    ool_high->entry());
        __ bind(ool_high->exit());
      }
      DCHECK_EQ(LeaveCC, i.OutputSBit());
      break;
    }
#undef ASSEMBLE_F64X2_ARITHMETIC_BINOP
    case kArmF64x2Eq: {
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.Acquire();
      __ mov(scratch, Operand(0));
      __ VFPCompareAndSetFlags(i.InputSimd128Register(0).low(),
                               i.InputSimd128Register(1).low());
      __ mov(scratch, Operand(-1), LeaveCC, eq);
      __ vmov(i.OutputSimd128Register().low(), scratch, scratch);

      __ mov(scratch, Operand(0));
      __ VFPCompareAndSetFlags(i.InputSimd128Register(0).high(),
                               i.InputSimd128Register(1).high());
      __ mov(scratch, Operand(-1), LeaveCC, eq);
      __ vmov(i.OutputSimd128Register().high(), scratch, scratch);
      break;
    }
    case kArmF64x2Ne: {
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.Acquire();
      __ mov(scratch, Operand(0));
      __ VFPCompareAndSetFlags(i.InputSimd128Register(0).low(),
                               i.InputSimd128Register(1).low());
      __ mov(scratch, Operand(-1), LeaveCC, ne);
      __ vmov(i.OutputSimd128Register().low(), scratch, scratch);

      __ mov(scratch, Operand(0));
      __ VFPCompareAndSetFlags(i.InputSimd128Register(0).high(),
                               i.InputSimd128Register(1).high());
      __ mov(scratch, Operand(-1), LeaveCC, ne);
      __ vmov(i.OutputSimd128Register().high(), scratch, scratch);
      break;
    }
    case kArmF64x2Lt: {
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.Acquire();
      __ VFPCompareAndSetFlags(i.InputSimd128Register(0).low(),
                               i.InputSimd128Register(1).low());
      __ mov(scratch, Operand(0), LeaveCC, cs);
      __ mov(scratch, Operand(-1), LeaveCC, mi);
      __ vmov(i.OutputSimd128Register().low(), scratch, scratch);

      __ VFPCompareAndSetFlags(i.InputSimd128Register(0).high(),
                               i.InputSimd128Register(1).high());
      __ mov(scratch, Operand(0), LeaveCC, cs);
      __ mov(scratch, Operand(-1), LeaveCC, mi);
      __ vmov(i.OutputSimd128Register().high(), scratch, scratch);
      break;
    }
    case kArmF64x2Le: {
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.Acquire();
      __ VFPCompareAndSetFlags(i.InputSimd128Register(0).low(),
                               i.InputSimd128Register(1).low());
      __ mov(scratch, Operand(0), LeaveCC, hi);
      __ mov(scratch, Operand(-1), LeaveCC, ls);
      __ vmov(i.OutputSimd128Register().low(), scratch, scratch);

      __ VFPCompareAndSetFlags(i.InputSimd128Register(0).high(),
                               i.InputSimd128Register(1).high());
      __ mov(scratch, Operand(0), LeaveCC, hi);
      __ mov(scratch, Operand(-1), LeaveCC, ls);
      __ vmov(i.OutputSimd128Register().high(), scratch, scratch);
      break;
    }
    case kArmF64x2Pmin: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register lhs = i.InputSimd128Register(0);
      Simd128Register rhs = i.InputSimd128Register(1);
      DCHECK_EQ(dst, lhs);

      // Move rhs only when rhs is strictly lesser (mi).
      __ VFPCompareAndSetFlags(rhs.low(), lhs.low());
      __ vmov(dst.low(), rhs.low(), mi);
      __ VFPCompareAndSetFlags(rhs.high(), lhs.high());
      __ vmov(dst.high(), rhs.high(), mi);
      break;
    }
    case kArmF64x2Pmax: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register lhs = i.InputSimd128Register(0);
      Simd128Register rhs = i.InputSimd128Register(1);
      DCHECK_EQ(dst, lhs);

      // Move rhs only when rhs is strictly greater (gt).
      __ VFPCompareAndSetFlags(rhs.low(), lhs.low());
      __ vmov(dst.low(), rhs.low(), gt);
      __ VFPCompareAndSetFlags(rhs.high(), lhs.high());
      __ vmov(dst.high(), rhs.high(), gt);
      break;
    }
    case kArmF64x2Qfma: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src0 = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      Simd128Register src2 = i.InputSimd128Register(2);
      __ vmul(dst.low(), src0.low(), src1.low());
      __ vmul(dst.high(), src0.high(), src1.high());
      __ vadd(dst.low(), src2.low(), dst.low());
      __ vadd(dst.high(), src2.high(), dst.high());
      break;
    }
    case kArmF64x2Qfms: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src0 = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      Simd128Register src2 = i.InputSimd128Register(2);
      __ vmul(dst.low(), src0.low(), src1.low());
      __ vmul(dst.high(), src0.high(), src1.high());
      __ vsub(dst.low(), src2.low(), dst.low());
      __ vsub(dst.high(), src2.high(), dst.high());
      break;
    }
    case kArmF64x2Ceil: {
      CpuFeatureScope scope(masm(), ARMv8);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ vrintp(dst.low(), src.low());
      __ vrintp(dst.high(), src.high());
      break;
    }
    case kArmF64x2Floor: {
      CpuFeatureScope scope(masm(), ARMv8);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ vrintm(dst.low(), src.low());
      __ vrintm(dst.high(), src.high());
      break;
    }
    case kArmF64x2Trunc: {
      CpuFeatureScope scope(masm(), ARMv8);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ vrintz(dst.low(), src.low());
      __ vrintz(dst.high(), src.high());
      break;
    }
    case kArmF64x2NearestInt: {
      CpuFeatureScope scope(masm(), ARMv8);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ vrintn(dst.low(), src.low());
      __ vrintn(dst.high(), src.high());
      break;
    }
    case kArmF64x2ConvertLowI32x4S: {
      __ F64x2ConvertLowI32x4S(i.OutputSimd128Register(),
                               i.InputSimd128Register(0));
      break;
    }
    case kArmF64x2ConvertLowI32x4U: {
      __ F64x2ConvertLowI32x4U(i.OutputSimd128Register(),
                               i.InputSimd128Register(0));
      break;
    }
    case kArmF64x2PromoteLowF32x4: {
      __ F64x2PromoteLowF32x4(i.OutputSimd128Register(),
                              i.InputSimd128Register(0));
      break;
    }
    case kArmI64x2SplatI32Pair: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vdup(Neon32, dst, i.InputRegister(0));
      __ ReplaceLane(dst, dst, i.InputRegister(1), NeonS32, 1);
      __ ReplaceLane(dst, dst, i.InputRegister(1), NeonS32, 3);
      break;
    }
    case kArmI64x2ReplaceLaneI32Pair: {
      Simd128Register dst = i.OutputSimd128Register();
      int8_t lane = i.InputInt8(1);
      __ ReplaceLane(dst, dst, i.InputRegister(2), NeonS32, lane * 2);
      __ ReplaceLane(dst, dst, i.InputRegister(3), NeonS32, lane * 2 + 1);
      break;
    }
    case kArmI64x2Add: {
      __ vadd(Neon64, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI64x2Sub: {
      __ vsub(Neon64, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI64x2Mul: {
      UseScratchRegisterScope temps(masm());
      QwNeonRegister dst = i.OutputSimd128Register();
      QwNeonRegister left = i.InputSimd128Register(0);
      QwNeonRegister right = i.InputSimd128Register(1);
      QwNeonRegister tmp1 = i.TempSimd128Register(0);
      QwNeonRegister tmp2 = temps.AcquireQ();

      // This algorithm uses vector operations to perform 64-bit integer
      // multiplication by splitting it into a high and low 32-bit integers.
      // The tricky part is getting the low and high integers in the correct
      // place inside a NEON register, so that we can use as little vmull and
      // vmlal as possible.

      // Move left and right into temporaries, they will be modified by vtrn.
      __ vmov(tmp1, left);
      __ vmov(tmp2, right);

      // This diagram shows how the 64-bit integers fit into NEON registers.
      //
      //             [q.high()| q.low()]
      // left/tmp1:  [ a3, a2 | a1, a0 ]
      // right/tmp2: [ b3, b2 | b1, b0 ]
      //
      // We want to multiply the low 32 bits of left with high 32 bits of right,
      // for each lane, i.e. a2 * b3, a0 * b1. However, vmull takes two input d
      // registers, and multiply the corresponding low/high 32 bits, to get a
      // 64-bit integer: a1 * b1, a0 * b0. In order to make it work we transpose
      // the vectors, so that we get the low 32 bits of each 64-bit integer into
      // the same lane, similarly for high 32 bits.
      __ vtrn(Neon32, tmp1.low(), tmp1.high());
      // tmp1: [ a3, a1 | a2, a0 ]
      __ vtrn(Neon32, tmp2.low(), tmp2.high());
      // tmp2: [ b3, b1 | b2, b0 ]

      __ vmull(NeonU32, dst, tmp1.low(), tmp2.high());
      // dst: [ a2*b3 | a0*b1 ]
      __ vmlal(NeonU32, dst, tmp1.high(), tmp2.low());
      // dst: [ a2*b3 + a3*b2 | a0*b1 + a1*b0 ]
      __ vshl(NeonU64, dst, dst, 32);
      // dst: [ (a2*b3 + a3*b2) << 32 | (a0*b1 + a1*b0) << 32 ]

      __ vmlal(NeonU32, dst, tmp1.low(), tmp2.low());
      // dst: [ (a2*b3 + a3*b2)<<32 + (a2*b2) | (a0*b1 + a1*b0)<<32 + (a0*b0) ]
      break;
    }
    case kArmI64x2Abs: {
      __ I64x2Abs(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmI64x2Neg: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vmov(dst, uint64_t{0});
      __ vsub(Neon64, dst, dst, i.InputSimd128Register(0));
      break;
    }
    case kArmI64x2Shl: {
      ASSEMBLE_SIMD_SHIFT_LEFT(vshl, 6, Neon32, NeonS64);
      break;
    }
    case kArmI64x2ShrS: {
      // Only the least significant byte of each lane is used, so we can use
      // Neon32 as the size.
      ASSEMBLE_SIMD_SHIFT_RIGHT(vshr, 6, Neon32, NeonS64);
      break;
    }
    case kArmI64x2ShrU: {
      // Only the least significant byte of each lane is used, so we can use
      // Neon32 as the size.
      ASSEMBLE_SIMD_SHIFT_RIGHT(vshr, 6, Neon32, NeonU64);
      break;
    }
    case kArmI64x2BitMask: {
      __ I64x2BitMask(i.OutputRegister(), i.InputSimd128Register(0));
      break;
    }
    case kArmI64x2SConvertI32x4Low: {
      __ vmovl(NeonS32, i.OutputSimd128Register(),
               i.InputSimd128Register(0).low());
      break;
    }
    case kArmI64x2SConvertI32x4High: {
      __ vmovl(NeonS32, i.OutputSimd128Register(),
               i.InputSimd128Register(0).high());
      break;
    }
    case kArmI64x2UConvertI32x4Low: {
      __ vmovl(NeonU32, i.OutputSimd128Register(),
               i.InputSimd128Register(0).low());
      break;
    }
    case kArmI64x2UConvertI32x4High: {
      __ vmovl(NeonU32, i.OutputSimd128Register(),
               i.InputSimd128Register(0).high());
      break;
    }
    case kArmF32x4Splat: {
      int src_code = i.InputFloatRegister(0).code();
      __ vdup(Neon32, i.OutputSimd128Register(),
              DwVfpRegister::from_code(src_code / 2), src_code % 2);
      break;
    }
    case kArmF32x4ExtractLane: {
      __ ExtractLane(i.OutputFloatRegister(), i.InputSimd128Register(0),
                     i.InputInt8(1));
      break;
    }
    case kArmF32x4ReplaceLane: {
      __ ReplaceLane(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputFloatRegister(2), i.InputInt8(1));
      break;
    }
    case kArmF32x4SConvertI32x4: {
      __ vcvt_f32_s32(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmF32x4UConvertI32x4: {
      __ vcvt_f32_u32(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmF32x4Abs: {
      __ vabs(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmF32x4Neg: {
      __ vneg(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmF32x4Sqrt: {
      QwNeonRegister dst = i.OutputSimd128Register();
      QwNeonRegister src1 = i.InputSimd128Register(0);
      DCHECK_EQ(dst, q0);
      DCHECK_EQ(src1, q0);
#define S_FROM_Q(reg, lane) SwVfpRegister::from_code(reg.code() * 4 + lane)
      __ vsqrt(S_FROM_Q(dst, 0), S_FROM_Q(src1, 0));
      __ vsqrt(S_FROM_Q(dst, 1), S_FROM_Q(src1, 1));
      __ vsqrt(S_FROM_Q(dst, 2), S_FROM_Q(src1, 2));
      __ vsqrt(S_FROM_Q(dst, 3), S_FROM_Q(src1, 3));
#undef S_FROM_Q
      break;
    }
    case kArmF32x4Add: {
      __ vadd(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmF32x4Sub: {
      __ vsub(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmF32x4Mul: {
      __ vmul(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmF32x4Div: {
      QwNeonRegister dst = i.OutputSimd128Register();
      QwNeonRegister src1 = i.InputSimd128Register(0);
      QwNeonRegister src2 = i.InputSimd128Register(1);
      DCHECK_EQ(dst, q0);
      DCHECK_EQ(src1, q0);
      DCHECK_EQ(src2, q1);
#define S_FROM_Q(reg, lane) SwVfpRegister::from_code(reg.code() * 4 + lane)
      __ vdiv(S_FROM_Q(dst, 0), S_FROM_Q(src1, 0), S_FROM_Q(src2, 0));
      __ vdiv(S_FROM_Q(dst, 1), S_FROM_Q(src1, 1), S_FROM_Q(src2, 1));
      __ vdiv(S_FROM_Q(dst, 2), S_FROM_Q(src1, 2), S_FROM_Q(src2, 2));
      __ vdiv(S_FROM_Q(dst, 3), S_FROM_Q(src1, 3), S_FROM_Q(src2, 3));
#undef S_FROM_Q
      break;
    }
    case kArmF32x4Min: {
      __ vmin(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmF32x4Max: {
      __ vmax(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmF32x4Eq: {
      __ vceq(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmF32x4Ne: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vceq(dst, i.InputSimd128Register(0), i.InputSimd128Register(1));
      __ vmvn(dst, dst);
      break;
    }
    case kArmF32x4Lt: {
      __ vcgt(i.OutputSimd128Register(), i.InputSimd128Register(1),
              i.InputSimd128Register(0));
      break;
    }
    case kArmF32x4Le: {
      __ vcge(i.OutputSimd128Register(), i.InputSimd128Register(1),
              i.InputSimd128Register(0));
      break;
    }
    case kArmF32x4Pmin: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register lhs = i.InputSimd128Register(0);
      Simd128Register rhs = i.InputSimd128Register(1);
      DCHECK_NE(dst, lhs);
      DCHECK_NE(dst, rhs);

      // f32x4.pmin(lhs, rhs)
      // = v128.bitselect(rhs, lhs, f32x4.lt(rhs, lhs))
      // = v128.bitselect(rhs, lhs, f32x4.gt(lhs, rhs))
      __ vcgt(dst, lhs, rhs);
      __ vbsl(dst, rhs, lhs);
      break;
    }
    case kArmF32x4Pmax: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register lhs = i.InputSimd128Register(0);
      Simd128Register rhs = i.InputSimd128Register(1);
      DCHECK_NE(dst, lhs);
      DCHECK_NE(dst, rhs);

      // f32x4.pmax(lhs, rhs)
      // = v128.bitselect(rhs, lhs, f32x4.gt(rhs, lhs))
      __ vcgt(dst, rhs, lhs);
      __ vbsl(dst, rhs, lhs);
      break;
    }
    case kArmF32x4Qfma: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vmul(dst, i.InputSimd128Register(0), i.InputSimd128Register(1));
      __ vadd(dst, i.InputSimd128Register(2), dst);
      break;
    }
    case kArmF32x4Qfms: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vmul(dst, i.InputSimd128Register(0), i.InputSimd128Register(1));
      __ vsub(dst, i.InputSimd128Register(2), dst);
      break;
    }
    case kArmF32x4DemoteF64x2Zero: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ vcvt_f32_f64(SwVfpRegister::from_code(dst.code() * 4), src.low());
      __ vcvt_f32_f64(SwVfpRegister::from_code(dst.code() * 4 + 1), src.high());
      __ vmov(dst.high(), 0);
      break;
    }
    case kArmI32x4Splat: {
      __ vdup(Neon32, i.OutputSimd128Register(), i.InputRegister(0));
      break;
    }
    case kArmI32x4ExtractLane: {
      __ ExtractLane(i.OutputRegister(), i.InputSimd128Register(0), NeonS32,
                     i.InputInt8(1));
      break;
    }
    case kArmI32x4ReplaceLane: {
      __ ReplaceLane(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputRegister(2), NeonS32, i.InputInt8(1));
      break;
    }
    case kArmI32x4SConvertF32x4: {
      __ vcvt_s32_f32(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmI32x4SConvertI16x8Low: {
      __ vmovl(NeonS16, i.OutputSimd128Register(),
               i.InputSimd128Register(0).low());
      break;
    }
    case kArmI32x4SConvertI16x8High: {
      __ vmovl(NeonS16, i.OutputSimd128Register(),
               i.InputSimd128Register(0).high());
      break;
    }
    case kArmI32x4Neg: {
      __ vneg(Neon32, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmI32x4Shl: {
      ASSEMBLE_SIMD_SHIFT_LEFT(vshl, 5, Neon32, NeonS32);
      break;
    }
    case kArmI32x4ShrS: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(vshr, 5, Neon32, NeonS32);
      break;
    }
    case kArmI32x4Add: {
      __ vadd(Neon32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4Sub: {
      __ vsub(Neon32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4Mul: {
      __ vmul(Neon32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4MinS: {
      __ vmin(NeonS32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4MaxS: {
      __ vmax(NeonS32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI64x2Eq: {
      __ I64x2Eq(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kArmI64x2Ne: {
      __ I64x2Ne(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kArmI64x2GtS: {
      __ I64x2GtS(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kArmI64x2GeS: {
      __ I64x2GeS(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4Eq: {
      __ vceq(Neon32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4Ne: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vceq(Neon32, dst, i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      __ vmvn(dst, dst);
      break;
    }
    case kArmI32x4GtS: {
      __ vcgt(NeonS32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4GeS: {
      __ vcge(NeonS32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4UConvertF32x4: {
      __ vcvt_u32_f32(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmI32x4UConvertI16x8Low: {
      __ vmovl(NeonU16, i.OutputSimd128Register(),
               i.InputSimd128Register(0).low());
      break;
    }
    case kArmI32x4UConvertI16x8High: {
      __ vmovl(NeonU16, i.OutputSimd128Register(),
               i.InputSimd128Register(0).high());
      break;
    }
    case kArmI32x4ShrU: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(vshr, 5, Neon32, NeonU32);
      break;
    }
    case kArmI32x4MinU: {
      __ vmin(NeonU32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4MaxU: {
      __ vmax(NeonU32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4GtU: {
      __ vcgt(NeonU32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4GeU: {
      __ vcge(NeonU32, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI32x4Abs: {
      __ vabs(Neon32, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmI32x4BitMask: {
      Register dst = i.OutputRegister();
      UseScratchRegisterScope temps(masm());
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register tmp = temps.AcquireQ();
      Simd128Register mask = i.TempSimd128Register(0);

      __ vshr(NeonS32, tmp, src, 31);
      // Set i-th bit of each lane i. When AND with tmp, the lanes that
      // are signed will have i-th bit set, unsigned will be 0.
      __ vmov(mask.low(), base::Double(uint64_t{0x0000'0002'0000'0001}));
      __ vmov(mask.high(), base::Double(uint64_t{0x0000'0008'0000'0004}));
      __ vand(tmp, mask, tmp);
      __ vpadd(Neon32, tmp.low(), tmp.low(), tmp.high());
      __ vpadd(Neon32, tmp.low(), tmp.low(), kDoubleRegZero);
      __ VmovLow(dst, tmp.low());
      break;
    }
    case kArmI32x4DotI16x8S: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register lhs = i.InputSimd128Register(0);
      Simd128Register rhs = i.InputSimd128Register(1);
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      __ vmull(NeonS16, scratch, lhs.low(), rhs.low());
      __ vpadd(Neon32, dst.low(), scratch.low(), scratch.high());
      __ vmull(NeonS16, scratch, lhs.high(), rhs.high());
      __ vpadd(Neon32, dst.high(), scratch.low(), scratch.high());
      break;
    }
    case kArmI16x8DotI8x16S: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register lhs = i.InputSimd128Register(0);
      Simd128Register rhs = i.InputSimd128Register(1);
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      __ vmull(NeonS8, scratch, lhs.low(), rhs.low());
      __ vpadd(Neon16, dst.low(), scratch.low(), scratch.high());
      __ vmull(NeonS8, scratch, lhs.high(), rhs.high());
      __ vpadd(Neon16, dst.high(), scratch.low(), scratch.high());
      break;
    }
    case kArmI32x4DotI8x16AddS: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register lhs = i.InputSimd128Register(0);
      Simd128Register rhs = i.InputSimd128Register(1);
      Simd128Register tmp1 = i.TempSimd128Register(0);
      DCHECK_EQ(dst, i.InputSimd128Register(2));
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      __ vmull(NeonS8, scratch, lhs.low(), rhs.low());
      __ vpadd(Neon16, tmp1.low(), scratch.low(), scratch.high());
      __ vmull(NeonS8, scratch, lhs.high(), rhs.high());
      __ vpadd(Neon16, tmp1.high(), scratch.low(), scratch.high());
      __ vpadal(NeonS16, dst, tmp1);
      break;
    }
    case kArmI32x4TruncSatF64x2SZero: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ vcvt_s32_f64(SwVfpRegister::from_code(dst.code() * 4), src.low());
      __ vcvt_s32_f64(SwVfpRegister::from_code(dst.code() * 4 + 1), src.high());
      __ vmov(dst.high(), 0);
      break;
    }
    case kArmI32x4TruncSatF64x2UZero: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ vcvt_u32_f64(SwVfpRegister::from_code(dst.code() * 4), src.low());
      __ vcvt_u32_f64(SwVfpRegister::from_code(dst.code() * 4 + 1), src.high());
      __ vmov(dst.high(), 0);
      break;
    }
    case kArmI16x8Splat: {
      __ vdup(Neon16, i.OutputSimd128Register(), i.InputRegister(0));
      break;
    }
    case kArmI16x8ExtractLaneU: {
      __ ExtractLane(i.OutputRegister(), i.InputSimd128Register(0), NeonU16,
                     i.InputInt8(1));
      break;
    }
    case kArmI16x8ExtractLaneS: {
      __ ExtractLane(i.OutputRegister(), i.InputSimd128Register(0), NeonS16,
                     i.InputInt8(1));
      break;
    }
    case kArmI16x8ReplaceLane: {
      __ ReplaceLane(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputRegister(2), NeonS16, i.InputInt8(1));
      break;
    }
    case kArmI16x8SConvertI8x16Low: {
      __ vmovl(NeonS8, i.OutputSimd128Register(),
               i.InputSimd128Register(0).low());
      break;
    }
    case kArmI16x8SConvertI8x16High: {
      __ vmovl(NeonS8, i.OutputSimd128Register(),
               i.InputSimd128Register(0).high());
      break;
    }
    case kArmI16x8Neg: {
      __ vneg(Neon16, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmI16x8Shl: {
      ASSEMBLE_SIMD_SHIFT_LEFT(vshl, 4, Neon16, NeonS16);
      break;
    }
    case kArmI16x8ShrS: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(vshr, 4, Neon16, NeonS16);
      break;
    }
    case kArmI16x8SConvertI32x4:
      ASSEMBLE_NEON_NARROWING_OP(NeonS16, NeonS16);
      break;
    case kArmI16x8Add: {
      __ vadd(Neon16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8AddSatS: {
      __ vqadd(NeonS16, i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8Sub: {
      __ vsub(Neon16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8SubSatS: {
      __ vqsub(NeonS16, i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8Mul: {
      __ vmul(Neon16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8MinS: {
      __ vmin(NeonS16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8MaxS: {
      __ vmax(NeonS16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8Eq: {
      __ vceq(Neon16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8Ne: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vceq(Neon16, dst, i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      __ vmvn(dst, dst);
      break;
    }
    case kArmI16x8GtS: {
      __ vcgt(NeonS16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8GeS: {
      __ vcge(NeonS16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8UConvertI8x16Low: {
      __ vmovl(NeonU8, i.OutputSimd128Register(),
               i.InputSimd128Register(0).low());
      break;
    }
    case kArmI16x8UConvertI8x16High: {
      __ vmovl(NeonU8, i.OutputSimd128Register(),
               i.InputSimd128Register(0).high());
      break;
    }
    case kArmI16x8ShrU: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(vshr, 4, Neon16, NeonU16);
      break;
    }
    case kArmI16x8UConvertI32x4:
      ASSEMBLE_NEON_NARROWING_OP(NeonU16, NeonS16);
      break;
    case kArmI16x8AddSatU: {
      __ vqadd(NeonU16, i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8SubSatU: {
      __ vqsub(NeonU16, i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8MinU: {
      __ vmin(NeonU16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8MaxU: {
      __ vmax(NeonU16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8GtU: {
      __ vcgt(NeonU16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8GeU: {
      __ vcge(NeonU16, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8RoundingAverageU: {
      __ vrhadd(NeonU16, i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kArmI16x8Abs: {
      __ vabs(Neon16, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmI16x8BitMask: {
      UseScratchRegisterScope temps(masm());
      Register dst = i.OutputRegister();
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register tmp = temps.AcquireQ();
      Simd128Register mask = i.TempSimd128Register(0);

      __ vshr(NeonS16, tmp, src, 15);
      // Set i-th bit of each lane i. When AND with tmp, the lanes that
      // are signed will have i-th bit set, unsigned will be 0.
      __ vmov(mask.low(), base::Double(uint64_t{0x0008'0004'0002'0001}));
      __ vmov(mask.high(), base::Double(uint64_t{0x0080'0040'0020'0010}));
      __ vand(tmp, mask, tmp);
      __ vpadd(Neon16, tmp.low(), tmp.low(), tmp.high());
      __ vpadd(Neon16, tmp.low(), tmp.low(), tmp.low());
      __ vpadd(Neon16, tmp.low(), tmp.low(), tmp.low());
      __ vmov(NeonU16, dst, tmp.low(), 0);
      break;
    }
    case kArmI16x8Q15MulRSatS: {
      __ vqrdmulh(NeonS16, i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16Splat: {
      __ vdup(Neon8, i.OutputSimd128Register(), i.InputRegister(0));
      break;
    }
    case kArmI8x16ExtractLaneU: {
      __ ExtractLane(i.OutputRegister(), i.InputSimd128Register(0), NeonU8,
                     i.InputInt8(1));
      break;
    }
    case kArmI8x16ExtractLaneS: {
      __ ExtractLane(i.OutputRegister(), i.InputSimd128Register(0), NeonS8,
                     i.InputInt8(1));
      break;
    }
    case kArmI8x16ReplaceLane: {
      __ ReplaceLane(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputRegister(2), NeonS8, i.InputInt8(1));
      break;
    }
    case kArmI8x16Neg: {
      __ vneg(Neon8, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmI8x16Shl: {
      ASSEMBLE_SIMD_SHIFT_LEFT(vshl, 3, Neon8, NeonS8);
      break;
    }
    case kArmI8x16ShrS: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(vshr, 3, Neon8, NeonS8);
      break;
    }
    case kArmI8x16SConvertI16x8:
      ASSEMBLE_NEON_NARROWING_OP(NeonS8, NeonS8);
      break;
    case kArmI8x16Add: {
      __ vadd(Neon8, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16AddSatS: {
      __ vqadd(NeonS8, i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16Sub: {
      __ vsub(Neon8, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16SubSatS: {
      __ vqsub(NeonS8, i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16MinS: {
      __ vmin(NeonS8, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16MaxS: {
      __ vmax(NeonS8, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16Eq: {
      __ vceq(Neon8, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16Ne: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vceq(Neon8, dst, i.InputSimd128Register(0), i.InputSimd128Register(1));
      __ vmvn(dst, dst);
      break;
    }
    case kArmI8x16GtS: {
      __ vcgt(NeonS8, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16GeS: {
      __ vcge(NeonS8, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16ShrU: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(vshr, 3, Neon8, NeonU8);
      break;
    }
    case kArmI8x16UConvertI16x8:
      ASSEMBLE_NEON_NARROWING_OP(NeonU8, NeonS8);
      break;
    case kArmI8x16AddSatU: {
      __ vqadd(NeonU8, i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16SubSatU: {
      __ vqsub(NeonU8, i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16MinU: {
      __ vmin(NeonU8, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16MaxU: {
      __ vmax(NeonU8, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16GtU: {
      __ vcgt(NeonU8, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16GeU: {
      __ vcge(NeonU8, i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16RoundingAverageU: {
      __ vrhadd(NeonU8, i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kArmI8x16Abs: {
      __ vabs(Neon8, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmI8x16BitMask: {
      UseScratchRegisterScope temps(masm());
      Register dst = i.OutputRegister();
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register tmp = temps.AcquireQ();
      Simd128Register mask = i.TempSimd128Register(0);

      __ vshr(NeonS8, tmp, src, 7);
      // Set i-th bit of each lane i. When AND with tmp, the lanes that
      // are signed will have i-th bit set, unsigned will be 0.
      __ vmov(mask.low(), base::Double(uint64_t{0x8040'2010'0804'0201}));
      __ vmov(mask.high(), base::Double(uint64_t{0x8040'2010'0804'0201}));
      __ vand(tmp, mask, tmp);
      __ vext(mask, tmp, tmp, 8);
      __ vzip(Neon8, mask, tmp);
      __ vpadd(Neon16, tmp.low(), tmp.low(), tmp.high());
      __ vpadd(Neon16, tmp.low(), tmp.low(), tmp.low());
      __ vpadd(Neon16, tmp.low(), tmp.low(), tmp.low());
      __ vmov(NeonU16, dst, tmp.low(), 0);
      break;
    }
    case kArmS128Const: {
      QwNeonRegister dst = i.OutputSimd128Register();
      uint64_t imm1 = make_uint64(i.InputUint32(1), i.InputUint32(0));
      uint64_t imm2 = make_uint64(i.InputUint32(3), i.InputUint32(2));
      __ vmov(dst.low(), base::Double(imm1));
      __ vmov(dst.high(), base::Double(imm2));
      break;
    }
    case kArmS128Zero: {
      __ veor(i.OutputSimd128Register(), i.OutputSimd128Register(),
              i.OutputSimd128Register());
      break;
    }
    case kArmS128AllOnes: {
      __ vmov(i.OutputSimd128Register(), uint64_t{0xffff'ffff'ffff'ffff});
      break;
    }
    case kArmS128Dup: {
      NeonSize size = static_cast<NeonSize>(i.InputInt32(1));
      int lanes = kSimd128Size >> size;
      int index = i.InputInt32(2);
      DCHECK(index < lanes);
      int d_lanes = lanes / 2;
      int src_d_index = index & (d_lanes - 1);
      int src_d_code = i.InputSimd128Register(0).low().code() + index / d_lanes;
      __ vdup(size, i.OutputSimd128Register(),
              DwVfpRegister::from_code(src_d_code), src_d_index);
      break;
    }
    case kArmS128And: {
      __ vand(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmS128Or: {
      __ vorr(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmS128Xor: {
      __ veor(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmS128Not: {
      __ vmvn(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmS128Select: {
      Simd128Register dst = i.OutputSimd128Register();
      DCHECK(dst == i.InputSimd128Register(0));
      __ vbsl(dst, i.InputSimd128Register(1), i.InputSimd128Register(2));
      break;
    }
    case kArmS128AndNot: {
      __ vbic(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kArmS32x4ZipLeft: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [0, 1, 2, 3], src1 = [4, 5, 6, 7]
      __ vmov(dst.high(), src1.low());         // dst = [0, 1, 4, 5]
      __ vtrn(Neon32, dst.low(), dst.high());  // dst = [0, 4, 1, 5]
      break;
    }
    case kArmS32x4ZipRight: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [4, 5, 6, 7], src1 = [0, 1, 2, 3] (flipped from ZipLeft).
      __ vmov(dst.low(), src1.high());         // dst = [2, 3, 6, 7]
      __ vtrn(Neon32, dst.low(), dst.high());  // dst = [2, 6, 3, 7]
      break;
    }
    case kArmS32x4UnzipLeft: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      DCHECK(dst == i.InputSimd128Register(0));
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      // src0 = [0, 1, 2, 3], src1 = [4, 5, 6, 7]
      __ vmov(scratch, src1);
      __ vuzp(Neon32, dst, scratch);  // dst = [0, 2, 4, 6]
      break;
    }
    case kArmS32x4UnzipRight: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      DCHECK(dst == i.InputSimd128Register(0));
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      // src0 = [4, 5, 6, 7], src1 = [0, 1, 2, 3] (flipped from UnzipLeft).
      __ vmov(scratch, src1);
      __ vuzp(Neon32, scratch, dst);  // dst = [1, 3, 5, 7]
      break;
    }
    case kArmS32x4TransposeLeft: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      DCHECK(dst == i.InputSimd128Register(0));
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      // src0 = [0, 1, 2, 3], src1 = [4, 5, 6, 7]
      __ vmov(scratch, src1);
      __ vtrn(Neon32, dst, scratch);  // dst = [0, 4, 2, 6]
      break;
    }
    case kArmS32x4Shuffle: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      DCHECK_NE(dst, src0);
      DCHECK_NE(dst, src1);
      // Perform shuffle as a vmov per lane.
      int dst_code = dst.code() * 4;
      int src0_code = src0.code() * 4;
      int src1_code = src1.code() * 4;
      int32_t shuffle = i.InputInt32(2);
      for (int i = 0; i < 4; i++) {
        int lane = shuffle & 0x7;
        int src_code = src0_code;
        if (lane >= 4) {
          src_code = src1_code;
          lane &= 0x3;
        }
        __ VmovExtended(dst_code + i, src_code + lane);
        shuffle >>= 8;
      }
      break;
    }
    case kArmS32x4TransposeRight: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [4, 5, 6, 7], src1 = [0, 1, 2, 3] (flipped from TransposeLeft).
      __ vmov(scratch, src1);
      __ vtrn(Neon32, scratch, dst);  // dst = [1, 5, 3, 7]
      break;
    }
    case kArmS16x8ZipLeft: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      // src0 = [0, 1, 2, 3, ... 7], src1 = [8, 9, 10, 11, ... 15]
      DCHECK(dst == i.InputSimd128Register(0));
      __ vmov(dst.high(), src1.low());         // dst = [0, 1, 2, 3, 8, ... 11]
      __ vzip(Neon16, dst.low(), dst.high());  // dst = [0, 8, 1, 9, ... 11]
      break;
    }
    case kArmS16x8ZipRight: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [8, 9, 10, 11, ... 15], src1 = [0, 1, 2, 3, ... 7] (flipped).
      __ vmov(dst.low(), src1.high());
      __ vzip(Neon16, dst.low(), dst.high());  // dst = [4, 12, 5, 13, ... 15]
      break;
    }
    case kArmS16x8UnzipLeft: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [0, 1, 2, 3, ... 7], src1 = [8, 9, 10, 11, ... 15]
      __ vmov(scratch, src1);
      __ vuzp(Neon16, dst, scratch);  // dst = [0, 2, 4, 6, ... 14]
      break;
    }
    case kArmS16x8UnzipRight: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [8, 9, 10, 11, ... 15], src1 = [0, 1, 2, 3, ... 7] (flipped).
      __ vmov(scratch, src1);
      __ vuzp(Neon16, scratch, dst);  // dst = [1, 3, 5, 7, ... 15]
      break;
    }
    case kArmS16x8TransposeLeft: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [0, 1, 2, 3, ... 7], src1 = [8, 9, 10, 11, ... 15]
      __ vmov(scratch, src1);
      __ vtrn(Neon16, dst, scratch);  // dst = [0, 8, 2, 10, ... 14]
      break;
    }
    case kArmS16x8TransposeRight: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [8, 9, 10, 11, ... 15], src1 = [0, 1, 2, 3, ... 7] (flipped).
      __ vmov(scratch, src1);
      __ vtrn(Neon16, scratch, dst);  // dst = [1, 9, 3, 11, ... 15]
      break;
    }
    case kArmS8x16ZipLeft: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [0, 1, 2, 3, ... 15], src1 = [16, 17, 18, 19, ... 31]
      __ vmov(dst.high(), src1.low());
      __ vzip(Neon8, dst.low(), dst.high());  // dst = [0, 16, 1, 17, ... 23]
      break;
    }
    case kArmS8x16ZipRight: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [16, 17, 18, 19, ... 31], src1 = [0, 1, 2, 3, ... 15] (flipped).
      __ vmov(dst.low(), src1.high());
      __ vzip(Neon8, dst.low(), dst.high());  // dst = [8, 24, 9, 25, ... 31]
      break;
    }
    case kArmS8x16UnzipLeft: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [0, 1, 2, 3, ... 15], src1 = [16, 17, 18, 19, ... 31]
      __ vmov(scratch, src1);
      __ vuzp(Neon8, dst, scratch);  // dst = [0, 2, 4, 6, ... 30]
      break;
    }
    case kArmS8x16UnzipRight: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [16, 17, 18, 19, ... 31], src1 = [0, 1, 2, 3, ... 15] (flipped).
      __ vmov(scratch, src1);
      __ vuzp(Neon8, scratch, dst);  // dst = [1, 3, 5, 7, ... 31]
      break;
    }
    case kArmS8x16TransposeLeft: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [0, 1, 2, 3, ... 15], src1 = [16, 17, 18, 19, ... 31]
      __ vmov(scratch, src1);
      __ vtrn(Neon8, dst, scratch);  // dst = [0, 16, 2, 18, ... 30]
      break;
    }
    case kArmS8x16TransposeRight: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      DCHECK(dst == i.InputSimd128Register(0));
      // src0 = [16, 17, 18, 19, ... 31], src1 = [0, 1, 2, 3, ... 15] (flipped).
      __ vmov(scratch, src1);
      __ vtrn(Neon8, scratch, dst);  // dst = [1, 17, 3, 19, ... 31]
      break;
    }
    case kArmS8x16Concat: {
      __ vext(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1), i.InputInt4(2));
      break;
    }
    case kArmI8x16Swizzle: {
      Simd128Register dst = i.OutputSimd128Register(),
                      tbl = i.InputSimd128Register(0),
                      src = i.InputSimd128Register(1);
      NeonListOperand table(tbl);
      __ vtbl(dst.low(), table, src.low());
      __ vtbl(dst.high(), table, src.high());
      break;
    }
    case kArmI8x16Shuffle: {
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      DwVfpRegister table_base = src0.low();
      UseScratchRegisterScope temps(masm());
      Simd128Register scratch = temps.AcquireQ();
      // If unary shuffle, table is src0 (2 d-registers), otherwise src0 and
      // src1. They must be consecutive.
      int table_size = src0 == src1 ? 2 : 4;
      DCHECK_IMPLIES(src0 != src1, src0.code() + 1 == src1.code());
      // The shuffle lane mask is a byte mask, materialize in scratch.
      int scratch_s_base = scratch.code() * 4;
      for (int j = 0; j < 4; j++) {
        uint32_t four_lanes = i.InputUint32(2 + j);
        DCHECK_EQ(0, four_lanes & (table_size == 2 ? 0xF0F0F0F0 : 0xE0E0E0E0));
        __ vmov(SwVfpRegister::from_code(scratch_s_base + j),
                Float32::FromBits(four_lanes));
      }
      NeonListOperand table(table_base, table_size);
      if (dst != src0 && dst != src1) {
        __ vtbl(dst.low(), table, scratch.low());
        __ vtbl(dst.high(), table, scratch.high());
      } else {
        __ vtbl(scratch.low(), table, scratch.low());
        __ vtbl(scratch.high(), table, scratch.high());
        __ vmov(dst, scratch);
      }
      break;
    }
    case kArmS32x2Reverse: {
      __ vrev64(Neon32, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmS16x4Reverse: {
      __ vrev64(Neon16, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmS16x2Reverse: {
      __ vrev32(Neon16, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmS8x8Reverse: {
      __ vrev64(Neon8, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmS8x4Reverse: {
      __ vrev32(Neon8, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmS8x2Reverse: {
      __ vrev16(Neon8, i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kArmV128AnyTrue: {
      const QwNeonRegister& src = i.InputSimd128Register(0);
      UseScratchRegisterScope temps(masm());
      DwVfpRegister scratch = temps.AcquireD();
      __ vpmax(NeonU32, scratch, src.low(), src.high());
      __ vpmax(NeonU32, scratch, scratch, scratch);
      __ ExtractLane(i.OutputRegister(), scratch, NeonS32, 0);
      __ cmp(i.OutputRegister(), Operand(0));
      __ mov(i.OutputRegister(), Operand(1), LeaveCC, ne);
      break;
    }
    case kArmI64x2AllTrue: {
      __ I64x2AllTrue(i.OutputRegister(), i.InputSimd128Register(0));
      break;
    }
    case kArmI32x4AllTrue: {
      const QwNeonRegister& src = i.InputSimd128Register(0);
      UseScratchRegisterScope temps(masm());
      DwVfpRegister scratch = temps.AcquireD();
      __ vpmin(NeonU32, scratch, src.low(), src.high());
      __ vpmin(NeonU32, scratch, scratch, scratch);
      __ ExtractLane(i.OutputRegister(), scratch, NeonS32, 0);
      __ cmp(i.OutputRegister(), Operand(0));
      __ mov(i.OutputRegister(), Operand(1), LeaveCC, ne);
      break;
    }
    case kArmI16x8AllTrue: {
      const QwNeonRegister& src = i.InputSimd128Register(0);
      UseScratchRegisterScope temps(masm());
      DwVfpRegister scratch = temps.AcquireD();
      __ vpmin(NeonU16, scratch, src.low(), src.high());
      __ vpmin(NeonU16, scratch, scratch, scratch);
      __ vpmin(NeonU16, scratch, scratch, scratch);
      __ ExtractLane(i.OutputRegister(), scratch, NeonS16, 0);
      __ cmp(i.OutputRegister(), Operand(0));
      __ mov(i.OutputRegister(), Operand(1), LeaveCC, ne);
      break;
    }
    case kArmI8x16AllTrue: {
      const QwNeonRegister& src = i.InputSimd128Register(0);
      UseScratchRegisterScope temps(masm());
      DwVfpRegister scratch = temps.AcquireD();
      __ vpmin(NeonU8, scratch, src.low(), src.high());
      __ vpmin(NeonU8, scratch, scratch, scratch);
      __ vpmin(NeonU8, scratch, scratch, scratch);
      __ vpmin(NeonU8, scratch, scratch, scratch);
      __ ExtractLane(i.OutputRegister(), scratch, NeonS8, 0);
      __ cmp(i.OutputRegister(), Operand(0));
      __ mov(i.OutputRegister(), Operand(1), LeaveCC, ne);
      break;
    }
    case kArmS128Load8Splat: {
      __ vld1r(Neon8, NeonListOperand(i.OutputSimd128Register()),
               i.NeonInputOperand(0));
      break;
    }
    case kArmS128Load16Splat: {
      __ vld1r(Neon16, NeonListOperand(i.OutputSimd128Register()),
               i.NeonInputOperand(0));
      break;
    }
    case kArmS128Load32Splat: {
      __ vld1r(Neon32, NeonListOperand(i.OutputSimd128Register()),
               i.NeonInputOperand(0));
      break;
    }
    case kArmS128Load64Splat: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vld1(Neon32, NeonListOperand(dst.low()), i.NeonInputOperand(0));
      __ Move(dst.high(), dst.low());
      break;
    }
    case kArmS128Load8x8S: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vld1(Neon8, NeonListOperand(dst.low()), i.NeonInputOperand(0));
      __ vmovl(NeonS8, dst, dst.low());
      break;
    }
    case kArmS128Load8x8U: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vld1(Neon8, NeonListOperand(dst.low()), i.NeonInputOperand(0));
      __ vmovl(NeonU8, dst, dst.low());
      break;
    }
    case kArmS128Load16x4S: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vld1(Neon16, NeonListOperand(dst.low()), i.NeonInputOperand(0));
      __ vmovl(NeonS16, dst, dst.low());
      break;
    }
    case kArmS128Load16x4U: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vld1(Neon16, NeonListOperand(dst.low()), i.NeonInputOperand(0));
      __ vmovl(NeonU16, dst, dst.low());
      break;
    }
    case kArmS128Load32x2S: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vld1(Neon32, NeonListOperand(dst.low()), i.NeonInputOperand(0));
      __ vmovl(NeonS32, dst, dst.low());
      break;
    }
    case kArmS128Load32x2U: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vld1(Neon32, NeonListOperand(dst.low()), i.NeonInputOperand(0));
      __ vmovl(NeonU32, dst, dst.low());
      break;
    }
    case kArmS128Load32Zero: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vmov(dst, 0);
      __ vld1s(Neon32, NeonListOperand(dst.low()), 0, i.NeonInputOperand(0));
      break;
    }
    case kArmS128Load64Zero: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vmov(dst.high(), 0);
      __ vld1(Neon64, NeonListOperand(dst.low()), i.NeonInputOperand(0));
      break;
    }
    case kArmS128LoadLaneLow: {
      Simd128Register dst = i.OutputSimd128Register();
      DCHECK_EQ(dst, i.InputSimd128Register(0));
      auto sz = static_cast<NeonSize>(MiscField::decode(instr->opcode()));
      NeonListOperand dst_list = NeonListOperand(dst.low());
      __ LoadLane(sz, dst_list, i.InputUint8(1), i.NeonInputOperand(2));
      break;
    }
    case kArmS128LoadLaneHigh: {
      Simd128Register dst = i.OutputSimd128Register();
      DCHECK_EQ(dst, i.InputSimd128Register(0));
      auto sz = static_cast<NeonSize>(MiscField::decode(instr->opcode()));
      NeonListOperand dst_list = NeonListOperand(dst.high());
      __ LoadLane(sz, dst_list, i.InputUint8(1), i.NeonInputOperand(2));
      break;
    }
    case kArmS128StoreLaneLow: {
      Simd128Register src = i.InputSimd128Register(0);
      NeonListOperand src_list = NeonListOperand(src.low());
      auto sz = static_cast<NeonSize>(MiscField::decode(instr->opcode()));
      __ StoreLane(sz, src_list, i.InputUint8(1), i.NeonInputOperand(2));
      break;
    }
    case kArmS128StoreLaneHigh: {
      Simd128Register src = i.InputSimd128Register(0);
      NeonListOperand src_list = NeonListOperand(src.high());
      auto sz = static_cast<NeonSize>(MiscField::decode(instr->opcode()));
      __ StoreLane(sz, src_list, i.InputUint8(1), i.NeonInputOperand(2));
      break;
    }
    case kAtomicLoadInt8:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(ldrsb);
      break;
    case kAtomicLoadUint8:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(ldrb);
      break;
    case kAtomicLoadInt16:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(ldrsh);
      break;
    case kAtomicLoadUint16:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(ldrh);
      break;
    case kAtomicLoadWord32:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(ldr);
      break;
    case kAtomicStoreWord8:
      ASSEMBLE_ATOMIC_STORE_INTEGER(strb,
                                    AtomicMemoryOrderField::decode(opcode));
      break;
    case kAtomicStoreWord16:
      ASSEMBLE_ATOMIC_STORE_INTEGER(strh,
                                    AtomicMemoryOrderField::decode(opcode));
      break;
    case kAtomicStoreWord32:
      ASSEMBLE_ATOMIC_STORE_INTEGER(str,
                                    AtomicMemoryOrderField::decode(opcode));
      break;
    case kAtomicExchangeInt8:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(ldrexb, strexb);
      __ sxtb(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicExchangeUint8:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(ldrexb, strexb);
      break;
    case kAtomicExchangeInt16:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(ldrexh, strexh);
      __ sxth(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicExchangeUint16:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(ldrexh, strexh);
      break;
    case kAtomicExchangeWord32:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(ldrex, strex);
      break;
    case kAtomicCompareExchangeInt8:
      __ add(i.TempRegister(1), i.InputRegister(0), i.InputRegister(1));
      __ uxtb(i.TempRegister(2), i.InputRegister(2));
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(ldrexb, strexb,
                                               i.TempRegister(2));
      __ sxtb(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicCompareExchangeUint8:
      __ add(i.TempRegister(1), i.InputRegister(0), i.InputRegister(1));
      __ uxtb(i.TempRegister(2), i.InputRegister(2));
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(ldrexb, strexb,
                                               i.TempRegister(2));
      break;
    case kAtomicCompareExchangeInt16:
      __ add(i.TempRegister(1), i.InputRegister(0), i.InputRegister(1));
      __ uxth(i.TempRegister(2), i.InputRegister(2));
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(ldrexh, strexh,
                                               i.TempRegister(2));
      __ sxth(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicCompareExchangeUint16:
      __ add(i.TempRegister(1), i.InputRegister(0), i.InputRegister(1));
      __ uxth(i.TempRegister(2), i.InputRegister(2));
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(ldrexh, strexh,
                                               i.TempRegister(2));
      break;
    case kAtomicCompareExchangeWord32:
      __ add(i.TempRegister(1), i.InputRegister(0), i.InputRegister(1));
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(ldrex, strex,
                                               i.InputRegister(2));
      break;
#define ATOMIC_BINOP_CASE(op, inst)                    \
  case kAtomic##op##Int8:                              \
    ASSEMBLE_ATOMIC_BINOP(ldrexb, strexb, inst);       \
    __ sxtb(i.OutputRegister(0), i.OutputRegister(0)); \
    break;                                             \
  case kAtomic##op##Uint8:                             \
    ASSEMBLE_ATOMIC_BINOP(ldrexb, strexb, inst);       \
    break;                                             \
  case kAtomic##op##Int16:                             \
    ASSEMBLE_ATOMIC_BINOP(ldrexh, strexh, inst);       \
    __ sxth(i.OutputRegister(0), i.OutputRegister(0)); \
    break;                                             \
  case kAtomic##op##Uint16:                            \
    ASSEMBLE_ATOMIC_BINOP(ldrexh, strexh, inst);       \
    break;                                             \
  case kAtomic##op##Word32:                            \
    ASSEMBLE_ATOMIC_BINOP(ldrex, strex, inst);         \
    break;
      ATOMIC_BINOP_CASE(Add, add)
      ATOMIC_BINOP_CASE(Sub, sub)
      ATOMIC_BINOP_CASE(And, and_)
      ATOMIC_BINOP_CASE(Or, orr)
      ATOMIC_BINOP_CASE(Xor, eor)
#undef ATOMIC_BINOP_CASE
    case kArmWord32AtomicPairLoad: {
      if (instr->OutputCount() == 2) {
        DCHECK(VerifyOutputOfAtomicPairInstr(&i, instr, r0, r1));
        __ add(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
        __ ldrexd(r0, r1, i.TempRegister(0));
        __ dmb(ISH);
      } else {
        // A special case of this instruction: even though this is a pair load,
        // we only need one of the two words. We emit a normal atomic load.
        DCHECK_EQ(instr->OutputCount(), 1);
        Register base = i.InputRegister(0);
        Register offset = i.InputRegister(1);
        DCHECK(instr->InputAt(2)->IsImmediate());
        int32_t offset_imm = i.InputInt32(2);
        if (offset_imm != 0) {
          Register temp = i.TempRegister(0);
          __ add(temp, offset, Operand(offset_imm));
          offset = temp;
        }
        __ ldr(i.OutputRegister(), MemOperand(base, offset));
        __ dmb(ISH);
      }
      break;
    }
    case kArmWord32AtomicPairStore: {
      Label store;
      Register base = i.InputRegister(0);
      Register offset = i.InputRegister(1);
      Register value_low = i.InputRegister(2);
      Register value_high = i.InputRegister(3);
      Register actual_addr = i.TempRegister(0);
      // The {ldrexd} instruction needs two temp registers. We do not need the
      // result of {ldrexd}, but {strexd} likely fails without the {ldrexd}.
      Register tmp1 = i.TempRegister(1);
      Register tmp2 = i.TempRegister(2);
      // Reuse one of the temp registers for the result of {strexd}.
      Register store_result = tmp1;
      __ add(actual_addr, base, offset);
      __ dmb(ISH);
      __ bind(&store);
      // Add this {ldrexd} instruction here so that {strexd} below can succeed.
      // We don't need the result of {ldrexd} itself.
      __ ldrexd(tmp1, tmp2, actual_addr);
      __ strexd(store_result, value_low, value_high, actual_addr);
      __ cmp(store_result, Operand(0));
      __ b(ne, &store);
      __ dmb(ISH);
      break;
    }
#define ATOMIC_ARITH_BINOP_CASE(op, instr1, instr2)           \
  case kArmWord32AtomicPair##op: {                            \
    DCHECK(VerifyOutputOfAtomicPairInstr(&i, instr, r2, r3)); \
    ASSEMBLE_ATOMIC64_ARITH_BINOP(instr1, instr2);            \
    break;                                                    \
  }
      ATOMIC_ARITH_BINOP_CASE(Add, add, adc)
      ATOMIC_ARITH_BINOP_CASE(Sub, sub, sbc)
#undef ATOMIC_ARITH_BINOP_CASE
#define ATOMIC_LOGIC_BINOP_CASE(op, instr1)                   \
  case kArmWord32AtomicPair##op: {                            \
    DCHECK(VerifyOutputOfAtomicPairInstr(&i, instr, r2, r3)); \
    ASSEMBLE_ATOMIC64_LOGIC_BINOP(instr1);                    \
    break;                                                    \
  }
      ATOMIC_LOGIC_BINOP_CASE(And, and_)
      ATOMIC_LOGIC_BINOP_CASE(Or, orr)
      ATOMIC_LOGIC_BINOP_CASE(Xor, eor)
#undef ATOMIC_LOGIC_BINOP_CASE
    case kArmWord32AtomicPairExchange: {
      DCHECK(VerifyOutputOfAtomicPairInstr(&i, instr, r6, r7));
      Label exchange;
      __ add(i.TempRegister(0), i.InputRegister(2), i.InputRegister(3));
      __ dmb(ISH);
      __ bind(&exchange);
      __ ldrexd(r6, r7, i.TempRegister(0));
      __ strexd(i.TempRegister(1), i.InputRegister(0), i.InputRegister(1),
                i.TempRegister(0));
      __ teq(i.TempRegister(1), Operand(0));
      __ b(ne, &exchange);
      __ dmb(ISH);
      break;
    }
    case kArmWord32AtomicPairCompareExchange: {
      DCHECK(VerifyOutputOfAtomicPairInstr(&i, instr, r2, r3));
      __ add(i.TempRegister(0), i.InputRegister(4), i.InputRegister(5));
      Label compareExchange;
      Label exit;
      __ dmb(ISH);
      __ bind(&compareExchange);
      __ ldrexd(r2, r3, i.TempRegister(0));
      __ teq(i.InputRegister(0), Operand(r2));
      __ b(ne, &exit);
      __ teq(i.InputRegister(1), Operand(r3));
      __ b(ne, &exit);
      __ strexd(i.TempRegister(1), i.InputRegister(2), i.InputRegister(3),
                i.TempRegister(0));
      __ teq(i.TempRegister(1), Operand(0));
      __ b(ne, &compareExchange);
      __ bind(&exit);
      __ dmb(ISH);
      break;
    }
#undef ASSEMBLE_ATOMIC_LOAD_INTEGER
#undef ASSEMBLE_ATOMIC_STORE_INTEGER
#undef ASSEMBLE_ATOMIC_EXCHANGE_INTEGER
#undef ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER
#undef ASSEMBLE_ATOMIC_BINOP
#undef ASSEMBLE_ATOMIC64_ARITH_BINOP
#undef ASSEMBLE_ATOMIC64_LOGIC_BINOP
#undef ASSEMBLE_IEEE754_BINOP
#undef ASSEMBLE_IEEE754_UNOP
#undef ASSEMBLE_NEON_NARROWING_OP
#undef ASSEMBLE_SIMD_SHIFT_LEFT
#undef ASSEMBLE_SIMD_SHIFT_RIGHT
  }
  return kSuccess;
}

// Assembles branches after an instruction.
void CodeGenerator::AssembleArchBranch(Instruction* instr, BranchInfo* branch) {
  ArmOperandConverter i(this, instr);
  Label* tlabel = branch->true_label;
  Label* flabel = branch->false_label;
  Condition cc = FlagsConditionToCondition(branch->condition);
  __ b(cc, tlabel);
  if (!branch->fallthru) __ b(flabel);  // no fallthru to flabel.
}

void CodeGenerator::AssembleArchDeoptBranch(Instruction* instr,
                                            BranchInfo* branch) {
  AssembleArchBranch(instr, branch);
}

void CodeGenerator::AssembleArchJumpRegardlessOfAssemblyOrder(
    RpoNumber target) {
  __ b(GetLabel(target));
}

#if V8_ENABLE_WEBASSEMBLY
void CodeGenerator::AssembleArchTrap(Instruction* instr,
                                     FlagsCondition condition) {
  class OutOfLineTrap final : public OutOfLineCode {
   public:
    OutOfLineTrap(CodeGenerator* gen, Instruction* instr)
        : OutOfLineCode(gen), instr_(instr), gen_(gen) {}

    void Generate() final {
      ArmOperandConverter i(gen_, instr_);
      TrapId trap_id =
          static_cast<TrapId>(i.InputInt32(instr_->InputCount() - 1));
      GenerateCallToTrap(trap_id);
    }

   private:
    void GenerateCallToTrap(TrapId trap_id) {
      gen_->AssembleSourcePosition(instr_);
      // A direct call to a wasm runtime stub defined in this module.
      // Just encode the stub index. This will be patched when the code
      // is added to the native module and copied into wasm code space.
      __ Call(static_cast<Address>(trap_id), RelocInfo::WASM_STUB_CALL);
      ReferenceMap* reference_map =
          gen_->zone()->New<ReferenceMap>(gen_->zone());
      gen_->RecordSafepoint(reference_map);
      if (v8_flags.debug_code) {
        __ stop();
      }
    }

    Instruction* instr_;
    CodeGenerator* gen_;
  };
  auto ool = zone()->New<OutOfLineTrap>(this, instr);
  Label* tlabel = ool->entry();
  Condition cc = FlagsConditionToCondition(condition);
  __ b(cc, tlabel);
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Assembles boolean materializations after an instruction.
void CodeGenerator::AssembleArchBoolean(Instruction* instr,
                                        FlagsCondition condition) {
  ArmOperandConverter i(this, instr);

  // Materialize a full 32-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  DCHECK_NE(0u, instr->OutputCount());
  Register reg = i.OutputRegister(instr->OutputCount() - 1);
  Condition cc = FlagsConditionToCondition(condition);
  __ mov(reg, Operand(0));
  __ mov(reg, Operand(1), LeaveCC, cc);
}

void CodeGenerator::AssembleArchConditionalBoolean(Instruction* instr) {
  UNREACHABLE();
}

void CodeGenerator::AssembleArchConditionalBranch(Instruction* instr,
                                                  BranchInfo* branch) {
  UNREACHABLE();
}

void CodeGenerator::AssembleArchBinarySearchSwitch(Instruction* instr) {
  ArmOperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  std::vector<std::pair<int32_t, Label*>> cases;
  for (size_t index = 2; index < instr->InputCount(); index += 2) {
    cases.push_back({i.InputInt32(index + 0), GetLabel(i.InputRpo(index + 1))});
  }
  AssembleArchBinarySearchSwitchRange(input, i.InputRpo(1), cases.data(),
                                      cases.data() + cases.size());
}

void CodeGenerator::AssembleArchTableSwitch(Instruction* instr) {
  ArmOperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  size_t const case_count = instr->InputCount() - 2;
  // This {cmp} might still emit a constant pool entry.
  __ cmp(input, Operand(case_count));
  // Ensure to emit the constant pool first if necessary.
  __ CheckConstPool(true, true);
  __ BlockConstPoolFor(case_count + 2);
  __ add(pc, pc, Operand(input, LSL, 2), LeaveCC, lo);
  __ b(GetLabel(i.InputRpo(1)));
  for (size_t index = 0; index < case_count; ++index) {
    __ b(GetLabel(i.InputRpo(index + 2)));
  }
}

void CodeGenerator::AssembleArchSelect(Instruction* instr,
                                       FlagsCondition condition) {
  UNIMPLEMENTED();
}

void CodeGenerator::FinishFrame(Frame* frame) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  const DoubleRegList saves_fp = call_descriptor->CalleeSavedFPRegisters();
  if (!saves_fp.is_empty()) {
    frame->AlignSavedCalleeRegisterSlots();
  }

  if (!saves_fp.is_empty()) {
    // Save callee-saved FP registers.
    static_assert(DwVfpRegister::kNumRegisters == 32);
    uint32_t last = base::bits::CountLeadingZeros32(saves_fp.bits()) - 1;
    uint32_t first = base::bits::CountTrailingZeros32(saves_fp.bits());
    DCHECK_EQ((last - first + 1), saves_fp.Count());
    frame->AllocateSavedCalleeRegisterSlots((last - first + 1) *
                                            (kDoubleSize / kSystemPointerSize));
  }
  const RegList saves = call_descriptor->CalleeSavedRegisters();
  if (!saves.is_empty()) {
    // Save callee-saved registers.
    frame->AllocateSavedCalleeRegisterSlots(saves.Count());
  }
}

void CodeGenerator::AssembleConstructFrame() {
  auto call_descriptor = linkage()->GetIncomingDescriptor();
  if (frame_access_state()->has_frame()) {
    if (call_descriptor->IsCFunctionCall()) {
#if V8_ENABLE_WEBASSEMBLY
      if (info()->GetOutputStackFrameType() == StackFrame::C_WASM_ENTRY) {
        __ StubPrologue(StackFrame::C_WASM_ENTRY);
        // Reserve stack space for saving the c_entry_fp later.
        __ AllocateStackSpace(kSystemPointerSize);
#else
      // For balance.
      if (false) {
#endif  // V8_ENABLE_WEBASSEMBLY
      } else {
        __ Push(lr, fp);
        __ mov(fp, sp);
      }
    } else if (call_descriptor->IsJSFunctionCall()) {
      __ Prologue();
    } else {
      __ StubPrologue(info()->GetOutputStackFrameType());
#if V8_ENABLE_WEBASSEMBLY
      if (call_descriptor->IsAnyWasmFunctionCall() ||
          call_descriptor->IsWasmImportWrapper() ||
          call_descriptor->IsWasmCapiFunction()) {
        // For import wrappers and C-API functions, this stack slot is only used
        // for printing stack traces in V8. Also, it holds a WasmImportData
        // instead of the trusted instance data, which is taken care of in the
        // frames accessors.
        __ Push(kWasmImplicitArgRegister);
      }
      if (call_descriptor->IsWasmCapiFunction()) {
        // Reserve space for saving the PC later.
        __ AllocateStackSpace(kSystemPointerSize);
      }
#endif  // V8_ENABLE_WEBASSEMBLY
    }

    unwinding_info_writer_.MarkFrameConstructed(__ pc_offset());
  }

  int required_slots =
      frame()->GetTotalFrameSlotCount() - frame()->GetFixedSlotCount();

  if (info()->is_osr()) {
    // TurboFan OSR-compiled functions cannot be entered directly.
    __ Abort(AbortReason::kShouldNotDirectlyEnterOsrFunction);

    // Unoptimized code jumps directly to this entrypoint while the unoptimized
    // frame is still on the stack. Optimized code uses OSR values directly from
    // the unoptimized frame. Thus, all that needs to be done is to allocate the
    // remaining stack slots.
    __ RecordComment("-- OSR entrypoint --");
    osr_pc_offset_ = __ pc_offset();
    required_slots -= osr_helper()->UnoptimizedFrameSlots();
  }

  const RegList saves = call_descriptor->CalleeSavedRegisters();
  const DoubleRegList saves_fp = call_descriptor->CalleeSavedFPRegisters();

  if (required_slots > 0) {
    DCHECK(frame_access_state()->has_frame());
#if V8_ENABLE_WEBASSEMBLY
    if (info()->IsWasm() && required_slots * kSystemPointerSize > 4 * KB) {
      // For WebAssembly functions with big frames we have to do the stack
      // overflow check before we construct the frame. Otherwise we may not
      // have enough space on the stack to call the runtime for the stack
      // overflow.
      Label done;

      // If the frame is bigger than the stack, we throw the stack overflow
      // exception unconditionally. Thereby we can avoid the integer overflow
      // check in the condition code.
      if (required_slots * kSystemPointerSize < v8_flags.stack_size * KB) {
        UseScratchRegisterScope temps(masm());
        Register stack_limit = temps.Acquire();
        __ LoadStackLimit(stack_limit, StackLimitKind::kRealStackLimit);
        __ add(stack_limit, stack_limit,
               Operand(required_slots * kSystemPointerSize));
        __ cmp(sp, stack_limit);
        __ b(cs, &done);
      }

      if (v8_flags.experimental_wasm_growable_stacks) {
        RegList regs_to_save;
        regs_to_save.set(WasmHandleStackOverflowDescriptor::GapRegister());
        regs_to_save.set(
            WasmHandleStackOverflowDescriptor::FrameBaseRegister());
        for (auto reg : wasm::kGpParamRegisters) regs_to_save.set(reg);
        __ stm(db_w, sp, regs_to_save);
        DoubleRegList fp_regs_to_save;
        for (auto reg : wasm::kFpParamRegisters) fp_regs_to_save.set(reg);
        __ vstm(db_w, sp, fp_regs_to_save.first(), fp_regs_to_save.last());
        __ mov(WasmHandleStackOverflowDescriptor::GapRegister(),
               Operand(required_slots * kSystemPointerSize));
        __ add(
            WasmHandleStackOverflowDescriptor::FrameBaseRegister(), fp,
            Operand(call_descriptor->ParameterSlotCount() * kSystemPointerSize +
                    CommonFrameConstants::kFixedFrameSizeAboveFp));
        __ CallBuiltin(Builtin::kWasmHandleStackOverflow);
        __ vldm(ia_w, sp, fp_regs_to_save.first(), fp_regs_to_save.last());
        __ ldm(ia_w, sp, regs_to_save);
      } else {
        __ Call(static_cast<intptr_t>(Builtin::kWasmStackOverflow),
                RelocInfo::WASM_STUB_CALL);
        // The call does not return, hence we can ignore any references and just
        // define an empty safepoint.
        ReferenceMap* reference_map = zone()->New<ReferenceMap>(zone());
        RecordSafepoint(reference_map);
        if (v8_flags.debug_code) __ stop();
      }

      __ bind(&done);
    }
#endif  // V8_ENABLE_WEBASSEMBLY

    // Skip callee-saved and return slots, which are pushed below.
    required_slots -= saves.Count();
    required_slots -= frame()->GetReturnSlotCount();
    required_slots -= 2 * saves_fp.Count();
    if (required_slots > 0) {
      __ AllocateStackSpace(required_slots * kSystemPointerSize);
    }
  }

  if (!saves_fp.is_empty()) {
    // Save callee-saved FP registers.
    static_assert(DwVfpRegister::kNumRegisters == 32);
    __ vstm(db_w, sp, saves_fp.first(), saves_fp.last());
  }

  if (!saves.is_empty()) {
    // Save callee-saved registers.
    __ stm(db_w, sp, saves);
  }

  const int returns = frame()->GetReturnSlotCount();
  // Create space for returns.
  __ AllocateStackSpace(returns * kSystemPointerSize);

  if (!frame()->tagged_slots().IsEmpty()) {
    UseScratchRegisterScope temps(masm());
    Register zero = temps.Acquire();
    __ mov(zero, Operand(0));
    for (int spill_slot : frame()->tagged_slots()) {
      FrameOffset offset = frame_access_state()->GetFrameOffset(spill_slot);
      DCHECK(offset.from_frame_pointer());
      __ str(zero, MemOperand(fp, offset.offset()));
    }
  }
}

void CodeGenerator::AssembleReturn(InstructionOperand* additional_pop_count) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  const int returns = frame()->GetReturnSlotCount();
  if (returns != 0) {
    // Free space of returns.
    __ add(sp, sp, Operand(returns * kSystemPointerSize));
  }

  // Restore registers.
  const RegList saves = call_descriptor->CalleeSavedRegisters();
  if (!saves.is_empty()) {
    __ ldm(ia_w, sp, saves);
  }

  // Restore FP registers.
  const DoubleRegList saves_fp = call_descriptor->CalleeSavedFPRegisters();
  if (!saves_fp.is_empty()) {
    static_assert(DwVfpRegister::kNumRegisters == 32);
    __ vldm(ia_w, sp, saves_fp.first(), saves_fp.last());
  }

  unwinding_info_writer_.MarkBlockWillExit();

  ArmOperandConverter g(this, nullptr);
  const int parameter_slots =
      static_cast<int>(call_descriptor->ParameterSlotCount());

  // {additional_pop_count} is only greater than zero if {parameter_slots = 0}.
  // Check RawMachineAssembler::PopAndReturn.
  if (parameter_slots != 0) {
    if (additional_pop_count->IsImmediate()) {
      DCHECK_EQ(g.ToConstant(additional_pop_count).ToInt32(), 0);
    } else if (v8_flags.debug_code) {
      __ cmp(g.ToRegister(additional_pop_count), Operand(0));
      __ Assert(eq, AbortReason::kUnexpectedAdditionalPopValue);
    }
  }

#if V8_ENABLE_WEBASSEMBLY
  if (call_descriptor->IsAnyWasmFunctionCall() &&
      v8_flags.experimental_wasm_growable_stacks) {
    {
      UseScratchRegisterScope temps{masm()};
      Register scratch = temps.Acquire();
      __ ldr(scratch, MemOperand(fp, TypedFrameConstants::kFrameTypeOffset));
      __ cmp(scratch,
             Operand(StackFrame::TypeToMarker(StackFrame::WASM_SEGMENT_START)));
    }
    Label done;
    __ b(&done, ne);
    RegList regs_to_save;
    for (auto reg : wasm::kGpReturnRegisters) regs_to_save.set(reg);
    __ stm(db_w, sp, regs_to_save);
    DoubleRegList fp_regs_to_save;
    for (auto reg : wasm::kFpParamRegisters) fp_regs_to_save.set(reg);
    __ vstm(db_w, sp, fp_regs_to_save.first(), fp_regs_to_save.last());
    __ Move(kCArgRegs[0], ExternalReference::isolate_address());
    __ PrepareCallCFunction(1);
    __ CallCFunction(ExternalReference::wasm_shrink_stack(), 1);
    // Restore old FP. We don't need to restore old SP explicitly, because
    // it will be restored from FP in LeaveFrame before return.
    __ mov(fp, kReturnRegister0);
    __ vldm(ia_w, sp, fp_regs_to_save.first(), fp_regs_to_save.last());
    __ ldm(ia_w, sp, regs_to_save);
    __ bind(&done);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  Register argc_reg = r3;
  // Functions with JS linkage have at least one parameter (the receiver).
  // If {parameter_slots} == 0, it means it is a builtin with
  // kDontAdaptArgumentsSentinel, which takes care of JS arguments popping
  // itself.
  const bool drop_jsargs = parameter_slots != 0 &&
                           frame_access_state()->has_frame() &&
                           call_descriptor->IsJSFunctionCall();
  if (call_descriptor->IsCFunctionCall()) {
    AssembleDeconstructFrame();
  } else if (frame_access_state()->has_frame()) {
    // Canonicalize JSFunction return sites for now unless they have an variable
    // number of stack slot pops.
    if (additional_pop_count->IsImmediate() &&
        g.ToConstant(additional_pop_count).ToInt32() == 0) {
      if (return_label_.is_bound()) {
        __ b(&return_label_);
        return;
      } else {
        __ bind(&return_label_);
      }
    }
    if (drop_jsargs) {
      // Get the actual argument count.
      __ ldr(argc_reg, MemOperand(fp, StandardFrameConstants::kArgCOffset));
      DCHECK(!call_descriptor->CalleeSavedRegisters().has(argc_reg));
    }
    AssembleDeconstructFrame();
  }

  if (drop_jsargs) {
    // We must pop all arguments from the stack (including the receiver).
    // The number of arguments without the receiver is
    // max(argc_reg, parameter_slots-1), and the receiver is added in
    // DropArguments().
    DCHECK(!call_descriptor->CalleeSavedRegisters().has(argc_reg));
    if (parameter_slots > 1) {
      __ cmp(argc_reg, Operand(parameter_slots));
      __ mov(argc_reg, Operand(parameter_slots), LeaveCC, lt);
    }
    __ DropArguments(argc_reg);
  } else if (additional_pop_count->IsImmediate()) {
    DCHECK_EQ(Constant::kInt32, g.ToConstant(additional_pop_count).type());
    int additional_count = g.ToConstant(additional_pop_count).ToInt32();
    __ Drop(parameter_slots + additional_count);
  } else if (parameter_slots == 0) {
    __ Drop(g.ToRegister(additional_pop_count));
  } else {
    // {additional_pop_count} is guaranteed to be zero if {parameter_slots !=
    // 0}. Check RawMachineAssembler::PopAndReturn.
    __ Drop(parameter_slots);
  }
  __ Ret();
}

void CodeGenerator::FinishCode() { __ CheckConstPool(true, false); }

void CodeGenerator::PrepareForDeoptimizationExits(
    ZoneDeque<DeoptimizationExit*>* exits) {
  __ CheckConstPool(true, false);
}

void CodeGenerator::AssembleMove(InstructionOperand* source,
                                 InstructionOperand* destination) {
  ArmOperandConverter g(this, nullptr);
  // Helper function to write the given constant to the dst register.
  auto MoveConstantToRegister = [&](Register dst, Constant src) {
    if (src.type() == Constant::kHeapObject) {
      Handle<HeapObject> src_object = src.ToHeapObject();
      RootIndex index;
      if (IsMaterializableFromRoot(src_object, &index)) {
        __ LoadRoot(dst, index);
      } else {
        __ Move(dst, src_object);
      }
    } else if (src.type() == Constant::kExternalReference) {
      __ Move(dst, src.ToExternalReference());
    } else {
      __ mov(dst, g.ToImmediate(source));
    }
  };
  switch (MoveType::InferMove(source, destination)) {
    case MoveType::kRegisterToRegister:
      if (source->IsRegister()) {
        __ mov(g.ToRegister(destination), g.ToRegister(source));
      } else if (source->IsFloatRegister()) {
        DCHECK(destination->IsFloatRegister());
        // GapResolver may give us reg codes that don't map to actual
        // s-registers. Generate code to work around those cases.
        int src_code = LocationOperand::cast(source)->register_code();
        int dst_code = LocationOperand::cast(destination)->register_code();
        __ VmovExtended(dst_code, src_code);
      } else if (source->IsDoubleRegister()) {
        __ Move(g.ToDoubleRegister(destination), g.ToDoubleRegister(source));
      } else {
        __ Move(g.ToSimd128Register(destination), g.ToSimd128Register(source));
      }
      return;
    case MoveType::kRegisterToStack: {
      MemOperand dst = g.ToMemOperand(destination);
      if (source->IsRegister()) {
        __ str(g.ToRegister(source), dst);
      } else if (source->IsFloatRegister()) {
        // GapResolver may give us reg codes that don't map to actual
        // s-registers. Generate code to work around those cases.
        int src_code = LocationOperand::cast(source)->register_code();
        __ VmovExtended(dst, src_code);
      } else if (source->IsDoubleRegister()) {
        __ vstr(g.ToDoubleRegister(source), dst);
      } else {
        UseScratchRegisterScope temps(masm());
        Register temp = temps.Acquire();
        QwNeonRegister src = g.ToSimd128Register(source);
        __ add(temp, dst.rn(), Operand(dst.offset()));
        __ vst1(Neon8, NeonListOperand(src.low(), 2), NeonMemOperand(temp));
      }
      return;
    }
    case MoveType::kStackToRegister: {
      MemOperand src = g.ToMemOperand(source);
      if (source->IsStackSlot()) {
        __ ldr(g.ToRegister(destination), src);
      } else if (source->IsFloatStackSlot()) {
        DCHECK(destination->IsFloatRegister());
        // GapResolver may give us reg codes that don't map to actual
        // s-registers. Generate code to work around those cases.
        int dst_code = LocationOperand::cast(destination)->register_code();
        __ VmovExtended(dst_code, src);
      } else if (source->IsDoubleStackSlot()) {
        __ vldr(g.ToDoubleRegister(destination), src);
      } else {
        UseScratchRegisterScope temps(masm());
        Register temp = temps.Acquire();
        QwNeonRegister dst = g.ToSimd128Register(destination);
        __ add(temp, src.rn(), Operand(src.offset()));
        __ vld1(Neon8, NeonListOperand(dst.low(), 2), NeonMemOperand(temp));
      }
      return;
    }
    case MoveType::kStackToStack: {
      MemOperand src = g.ToMemOperand(source);
      MemOperand dst = g.ToMemOperand(destination);
      UseScratchRegisterScope temps(masm());
      if (source->IsStackSlot() || source->IsFloatStackSlot()) {
        SwVfpRegister temp = temps.AcquireS();
        __ vldr(temp, src);
        __ vstr(temp, dst);
      } else if (source->IsDoubleStackSlot()) {
        DwVfpRegister temp = temps.AcquireD();
        __ vldr(temp, src);
        __ vstr(temp, dst);
      } else {
        DCHECK(source->IsSimd128StackSlot());
        Register temp = temps.Acquire();
        QwNeonRegister temp_q = temps.AcquireQ();
        __ add(temp, src.rn(), Operand(src.offset()));
        __ vld1(Neon8, NeonListOperand(temp_q.low(), 2), NeonMemOperand(temp));
        __ add(temp, dst.rn(), Operand(dst.offset()));
        __ vst1(Neon8, NeonListOperand(temp_q.low(), 2), NeonMemOperand(temp));
      }
      return;
    }
    case MoveType::kConstantToRegister: {
      Constant src = g.ToConstant(source);
      if (destination->IsRegister()) {
        MoveConstantToRegister(g.ToRegister(destination), src);
      } else if (destination->IsFloatRegister()) {
        __ vmov(g.ToFloatRegister(destination),
                Float32::FromBits(src.ToFloat32AsInt()));
      } else {
        // TODO(arm): Look into optimizing this further if possible. Supporting
        // the NEON version of VMOV may help.
        __ vmov(g.ToDoubleRegister(destination), src.ToFloat64());
      }
      return;
    }
    case MoveType::kConstantToStack: {
      Constant src = g.ToConstant(source);
      MemOperand dst = g.ToMemOperand(destination);
      if (destination->IsStackSlot()) {
        UseScratchRegisterScope temps(masm());
        // Acquire a S register instead of a general purpose register in case
        // `vstr` needs one to compute the address of `dst`.
        SwVfpRegister s_temp = temps.AcquireS();
        {
          // TODO(arm): This sequence could be optimized further if necessary by
          // writing the constant directly into `s_temp`.
          UseScratchRegisterScope temps(masm());
          Register temp = temps.Acquire();
          MoveConstantToRegister(temp, src);
          __ vmov(s_temp, temp);
        }
        __ vstr(s_temp, dst);
      } else if (destination->IsFloatStackSlot()) {
        UseScratchRegisterScope temps(masm());
        SwVfpRegister temp = temps.AcquireS();
        __ vmov(temp, Float32::FromBits(src.ToFloat32AsInt()));
        __ vstr(temp, dst);
      } else {
        DCHECK(destination->IsDoubleStackSlot());
        UseScratchRegisterScope temps(masm());
        DwVfpRegister temp = temps.AcquireD();
        // TODO(arm): Look into optimizing this further if possible. Supporting
        // the NEON version of VMOV may help.
        __ vmov(temp, src.ToFloat64());
        __ vstr(temp, g.ToMemOperand(destination));
      }
      return;
    }
  }
  UNREACHABLE();
}

AllocatedOperand CodeGenerator::Push(InstructionOperand* source) {
  auto rep = LocationOperand::cast(source)->representation();
  int new_slots = ElementSizeInPointers(rep);
  ArmOperandConverter g(this, nullptr);
  int last_frame_slot_id =
      frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
  int sp_delta = frame_access_state_->sp_delta();
  int slot_id = last_frame_slot_id + sp_delta + new_slots;
  AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
  if (source->IsRegister()) {
    __ push(g.ToRegister(source));
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else if (source->IsStackSlot()) {
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.Acquire();
    __ ldr(scratch, g.ToMemOperand(source));
    __ push(scratch);
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else {
    // No push instruction for this operand type. Bump the stack pointer and
    // assemble the move.
    __ sub(sp, sp, Operand(new_slots * kSystemPointerSize));
    frame_access_state()->IncreaseSPDelta(new_slots);
    AssembleMove(source, &stack_slot);
  }
  temp_slots_ += new_slots;
  return stack_slot;
}

void CodeGenerator::Pop(InstructionOperand* dest, MachineRepresentation rep) {
  int dropped_slots = ElementSizeInPointers(rep);
  ArmOperandConverter g(this, nullptr);
  if (dest->IsRegister()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ pop(g.ToRegister(dest));
  } else if (dest->IsStackSlot()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.Acquire();
    __ pop(scratch);
    __ str(scratch, g.ToMemOperand(dest));
  } else {
    int last_frame_slot_id =
        frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
    int sp_delta = frame_access_state_->sp_delta();
    int slot_id = last_frame_slot_id + sp_delta;
    AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
    AssembleMove(&stack_slot, dest);
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ add(sp, sp, Operand(dropped_slots * kSystemPointerSize));
  }
  temp_slots_ -= dropped_slots;
}

void CodeGenerator::PopTempStackSlots() {
  if (temp_slots_ > 0) {
    frame_access_state()->IncreaseSPDelta(-temp_slots_);
    __ add(sp, sp, Operand(temp_slots_ * kSystemPointerSize));
    temp_slots_ = 0;
  }
}

void CodeGenerator::MoveToTempLocation(InstructionOperand* source,
                                       MachineRepresentation rep) {
  // Must be kept in sync with {MoveTempLocationTo}.
  move_cycle_.temps.emplace(masm());
  auto& temps = *move_cycle_.temps;
  // Temporarily exclude the reserved scratch registers while we pick a
  // location to resolve the cycle. Re-include them immediately afterwards so
  // that they are available to assemble the move.
  temps.Exclude(move_cycle_.scratch_v_reglist);
  int reg_code = -1;
  if ((!IsFloatingPoint(rep) || rep == MachineRepresentation::kFloat32) &&
      temps.CanAcquireS()) {
    reg_code = temps.AcquireS().code();
  } else if (rep == MachineRepresentation::kFloat64 && temps.CanAcquireD()) {
    reg_code = temps.AcquireD().code();
  } else if (rep == MachineRepresentation::kSimd128 && temps.CanAcquireQ()) {
    reg_code = temps.AcquireQ().code();
  }
  temps.Include(move_cycle_.scratch_v_reglist);
  if (reg_code != -1) {
    // A scratch register is available for this rep.
    move_cycle_.scratch_reg_code = reg_code;
    if (IsFloatingPoint(rep)) {
      AllocatedOperand scratch(LocationOperand::REGISTER, rep, reg_code);
      AssembleMove(source, &scratch);
    } else {
      AllocatedOperand scratch(LocationOperand::REGISTER,
                               MachineRepresentation::kFloat32, reg_code);
      ArmOperandConverter g(this, nullptr);
      if (source->IsStackSlot()) {
        __ vldr(g.ToFloatRegister(&scratch), g.ToMemOperand(source));
      } else {
        DCHECK(source->IsRegister());
        __ vmov(g.ToFloatRegister(&scratch), g.ToRegister(source));
      }
    }
  } else {
    // The scratch registers are blocked by pending moves. Use the stack
    // instead.
    Push(source);
  }
}

void CodeGenerator::MoveTempLocationTo(InstructionOperand* dest,
                                       MachineRepresentation rep) {
  int scratch_reg_code = move_cycle_.scratch_reg_code;
  DCHECK(move_cycle_.temps.has_value());
  if (scratch_reg_code != -1) {
    if (IsFloatingPoint(rep)) {
      AllocatedOperand scratch(LocationOperand::REGISTER, rep,
                               scratch_reg_code);
      AssembleMove(&scratch, dest);
    } else {
      AllocatedOperand scratch(LocationOperand::REGISTER,
                               MachineRepresentation::kFloat32,
                               scratch_reg_code);
      ArmOperandConverter g(this, nullptr);
      if (dest->IsStackSlot()) {
        __ vstr(g.ToFloatRegister(&scratch), g.ToMemOperand(dest));
      } else {
        DCHECK(dest->IsRegister());
        __ vmov(g.ToRegister(dest), g.ToFloatRegister(&scratch));
      }
    }
  } else {
    Pop(dest, rep);
  }
  // Restore the default state to release the {UseScratchRegisterScope} and to
  // prepare for the next cycle.
  move_cycle_ = MoveCycleState();
}

void CodeGenerator::SetPendingMove(MoveOperands* move) {
  InstructionOperand& source = move->source();
  InstructionOperand& destination = move->destination();
  MoveType::Type move_type =
      MoveType::InferMove(&move->source(), &move->destination());
  UseScratchRegisterScope temps(masm());
  if (move_type == MoveType::kStackToStack) {
    if (source.IsStackSlot() || source.IsFloatStackSlot()) {
      SwVfpRegister temp = temps.AcquireS();
      move_cycle_.scratch_v_reglist |= temp.ToVfpRegList();
    } else if (source.IsDoubleStackSlot()) {
      DwVfpRegister temp = temps.AcquireD();
      move_cycle_.scratch_v_reglist |= temp.ToVfpRegList();
    } else {
      QwNeonRegister temp = temps.AcquireQ();
      move_cycle_.scratch_v_reglist |= temp.ToVfpRegList();
    }
    return;
  } else if (move_type == MoveType::kConstantToStack) {
    if (destination.IsStackSlot()) {
      // Acquire a S register instead of a general purpose register in case
      // `vstr` needs one to compute the address of `dst`.
      SwVfpRegister s_temp = temps.AcquireS();
      move_cycle_.scratch_v_reglist |= s_temp.ToVfpRegList();
    } else if (destination.IsFloatStackSlot()) {
      SwVfpRegister temp = temps.AcquireS();
      move_cycle_.scratch_v_reglist |= temp.ToVfpRegList();
    } else {
      DwVfpRegister temp = temps.AcquireD();
      move_cycle_.scratch_v_reglist |= temp.ToVfpRegList();
    }
  }
}

void CodeGenerator::AssembleSwap(InstructionOperand* source,
                                 InstructionOperand* destination) {
  ArmOperandConverter g(this, nullptr);
  switch (MoveType::InferSwap(source, destination)) {
    case MoveType::kRegisterToRegister:
      if (source->IsRegister()) {
        __ Swap(g.ToRegister(source), g.ToRegister(destination));
      } else if (source->IsFloatRegister()) {
        DCHECK(destination->IsFloatRegister());
        // GapResolver may give us reg codes that don't map to actual
        // s-registers. Generate code to work around those cases.
        UseScratchRegisterScope temps(masm());
        LowDwVfpRegister temp = temps.AcquireLowD();
        int src_code = LocationOperand::cast(source)->register_code();
        int dst_code = LocationOperand::cast(destination)->register_code();
        __ VmovExtended(temp.low().code(), src_code);
        __ VmovExtended(src_code, dst_code);
        __ VmovExtended(dst_code, temp.low().code());
      } else if (source->IsDoubleRegister()) {
        __ Swap(g.ToDoubleRegister(source), g.ToDoubleRegister(destination));
      } else {
        __ Swap(g.ToSimd128Register(source), g.ToSimd128Register(destination));
      }
      return;
    case MoveType::kRegisterToStack: {
      MemOperand dst = g.ToMemOperand(destination);
      if (source->IsRegister()) {
        Register src = g.ToRegister(source);
        UseScratchRegisterScope temps(masm());
        SwVfpRegister temp = temps.AcquireS();
        __ vmov(temp, src);
        __ ldr(src, dst);
        __ vstr(temp, dst);
      } else if (source->IsFloatRegister()) {
        int src_code = LocationOperand::cast(source)->register_code();
        UseScratchRegisterScope temps(masm());
        LowDwVfpRegister temp = temps.AcquireLowD();
        __ VmovExtended(temp.low().code(), src_code);
        __ VmovExtended(src_code, dst);
        __ vstr(temp.low(), dst);
      } else if (source->IsDoubleRegister()) {
        UseScratchRegisterScope temps(masm());
        DwVfpRegister temp = temps.AcquireD();
        DwVfpRegister src = g.ToDoubleRegister(source);
        __ Move(temp, src);
        __ vldr(src, dst);
        __ vstr(temp, dst);
      } else {
        QwNeonRegister src = g.ToSimd128Register(source);
        UseScratchRegisterScope temps(masm());
        Register temp = temps.Acquire();
        QwNeonRegister temp_q = temps.AcquireQ();
        __ Move(temp_q, src);
        __ add(temp, dst.rn(), Operand(dst.offset()));
        __ vld1(Neon8, NeonListOperand(src.low(), 2), NeonMemOperand(temp));
        __ vst1(Neon8, NeonListOperand(temp_q.low(), 2), NeonMemOperand(temp));
      }
      return;
    }
    case MoveType::kStackToStack: {
      MemOperand src = g.ToMemOperand(source);
      MemOperand dst = g.ToMemOperand(destination);
      if (source->IsStackSlot() || source->IsFloatStackSlot()) {
        UseScratchRegisterScope temps(masm());
        SwVfpRegister temp_0 = temps.AcquireS();
        SwVfpRegister temp_1 = temps.AcquireS();
        __ vldr(temp_0, dst);
        __ vldr(temp_1, src);
        __ vstr(temp_0, src);
        __ vstr(temp_1, dst);
      } else if (source->IsDoubleStackSlot()) {
        UseScratchRegisterScope temps(masm());
        LowDwVfpRegister temp = temps.AcquireLowD();
        if (temps.CanAcquireD()) {
          DwVfpRegister temp_0 = temp;
          DwVfpRegister temp_1 = temps.AcquireD();
          __ vldr(temp_0, dst);
          __ vldr(temp_1, src);
          __ vstr(temp_0, src);
          __ vstr(temp_1, dst);
        } else {
          // We only have a single D register available. However, we can split
          // it into 2 S registers and swap the slots 32 bits at a time.
          MemOperand src0 = src;
          MemOperand dst0 = dst;
          MemOperand src1(src.rn(), src.offset() + kFloatSize);
          MemOperand dst1(dst.rn(), dst.offset() + kFloatSize);
          SwVfpRegister temp_0 = temp.low();
          SwVfpRegister temp_1 = temp.high();
          __ vldr(temp_0, dst0);
          __ vldr(temp_1, src0);
          __ vstr(temp_0, src0);
          __ vstr(temp_1, dst0);
          __ vldr(temp_0, dst1);
          __ vldr(temp_1, src1);
          __ vstr(temp_0, src1);
          __ vstr(temp_1, dst1);
        }
      } else {
        DCHECK(source->IsSimd128StackSlot());
        MemOperand src0 = src;
        MemOperand dst0 = dst;
        MemOperand src1(src.rn(), src.offset() + kDoubleSize);
        MemOperand dst1(dst.rn(), dst.offset() + kDoubleSize);
        UseScratchRegisterScope temps(masm());
        DwVfpRegister temp_0 = temps.AcquireD();
        DwVfpRegister temp_1 = temps.AcquireD();
        __ vldr(temp_0, dst0);
        __ vldr(temp_1, src0);
        __ vstr(temp_0, src0);
        __ vstr(temp_1, dst0);
        __ vldr(temp_0, dst1);
        __ vldr(temp_1, src1);
        __ vstr(temp_0, src1);
        __ vstr(temp_1, dst1);
      }
      return;
    }
    default:
      UNREACHABLE();
  }
}

void CodeGenerator::AssembleJumpTable(base::Vector<Label*> targets) {
  // On 32-bit ARM we emit the jump tables inline.
  UNREACHABLE();
}

#undef __

}  // namespace compiler
}  // namespace internal
}  // namespace v8
