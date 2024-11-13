// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/arm64/assembler-arm64-inl.h"
#include "src/codegen/arm64/constants-arm64.h"
#include "src/codegen/arm64/macro-assembler-arm64-inl.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/code-generator-impl.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/gap-resolver.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/osr.h"
#include "src/execution/frame-constants.h"
#include "src/heap/mutable-page-metadata.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace compiler {

#define __ masm()->

// Adds Arm64-specific methods to convert InstructionOperands.
class Arm64OperandConverter final : public InstructionOperandConverter {
 public:
  Arm64OperandConverter(CodeGenerator* gen, Instruction* instr)
      : InstructionOperandConverter(gen, instr) {}

  DoubleRegister InputFloat32Register(size_t index) {
    return InputDoubleRegister(index).S();
  }

  DoubleRegister InputFloat64Register(size_t index) {
    return InputDoubleRegister(index);
  }

  DoubleRegister InputSimd128Register(size_t index) {
    return InputDoubleRegister(index).Q();
  }

  CPURegister InputFloat32OrZeroRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) {
      DCHECK_EQ(0, base::bit_cast<int32_t>(InputFloat32(index)));
      return wzr;
    }
    DCHECK(instr_->InputAt(index)->IsFPRegister());
    return InputDoubleRegister(index).S();
  }

  DoubleRegister InputFloat32OrFPZeroRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) {
      DCHECK_EQ(0, base::bit_cast<int32_t>(InputFloat32(index)));
      return fp_zero.S();
    }
    DCHECK(instr_->InputAt(index)->IsFPRegister());
    return InputDoubleRegister(index).S();
  }

  CPURegister InputFloat64OrZeroRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) {
      DCHECK_EQ(0, base::bit_cast<int64_t>(InputDouble(index)));
      return xzr;
    }
    DCHECK(instr_->InputAt(index)->IsDoubleRegister());
    return InputDoubleRegister(index);
  }

  DoubleRegister InputFloat64OrFPZeroRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) {
      DCHECK_EQ(0, base::bit_cast<int64_t>(InputDouble(index)));
      return fp_zero;
    }
    DCHECK(instr_->InputAt(index)->IsDoubleRegister());
    return InputDoubleRegister(index);
  }

  size_t OutputCount() { return instr_->OutputCount(); }

  DoubleRegister OutputFloat32Register(size_t index = 0) {
    return OutputDoubleRegister(index).S();
  }

  DoubleRegister OutputFloat64Register(size_t index = 0) {
    return OutputDoubleRegister(index);
  }

  DoubleRegister OutputSimd128Register() { return OutputDoubleRegister().Q(); }

  Register InputRegister32(size_t index) {
    return ToRegister(instr_->InputAt(index)).W();
  }

  Register InputOrZeroRegister32(size_t index) {
    DCHECK(instr_->InputAt(index)->IsRegister() ||
           (instr_->InputAt(index)->IsImmediate() && (InputInt32(index) == 0)));
    if (instr_->InputAt(index)->IsImmediate()) {
      return wzr;
    }
    return InputRegister32(index);
  }

  Register InputRegister64(size_t index) { return InputRegister(index); }

  Register InputOrZeroRegister64(size_t index) {
    DCHECK(instr_->InputAt(index)->IsRegister() ||
           (instr_->InputAt(index)->IsImmediate() && (InputInt64(index) == 0)));
    if (instr_->InputAt(index)->IsImmediate()) {
      return xzr;
    }
    return InputRegister64(index);
  }

  Operand InputOperand(size_t index) {
    return ToOperand(instr_->InputAt(index));
  }

  Operand InputOperand64(size_t index) { return InputOperand(index); }

  Operand InputOperand32(size_t index) {
    return ToOperand32(instr_->InputAt(index));
  }

  Register OutputRegister64(size_t index = 0) { return OutputRegister(index); }

  Register OutputRegister32(size_t index = 0) {
    return OutputRegister(index).W();
  }

  Register TempRegister32(size_t index) {
    return ToRegister(instr_->TempAt(index)).W();
  }

  Operand InputOperand2_32(size_t index) {
    switch (AddressingModeField::decode(instr_->opcode())) {
      case kMode_None:
        return InputOperand32(index);
      case kMode_Operand2_R_LSL_I:
        return Operand(InputRegister32(index), LSL, InputInt5(index + 1));
      case kMode_Operand2_R_LSR_I:
        return Operand(InputRegister32(index), LSR, InputInt5(index + 1));
      case kMode_Operand2_R_ASR_I:
        return Operand(InputRegister32(index), ASR, InputInt5(index + 1));
      case kMode_Operand2_R_ROR_I:
        return Operand(InputRegister32(index), ROR, InputInt5(index + 1));
      case kMode_Operand2_R_UXTB:
        return Operand(InputRegister32(index), UXTB);
      case kMode_Operand2_R_UXTH:
        return Operand(InputRegister32(index), UXTH);
      case kMode_Operand2_R_SXTB:
        return Operand(InputRegister32(index), SXTB);
      case kMode_Operand2_R_SXTH:
        return Operand(InputRegister32(index), SXTH);
      case kMode_Operand2_R_SXTW:
        return Operand(InputRegister32(index), SXTW);
      case kMode_MRI:
      case kMode_MRR:
      case kMode_Root:
        break;
    }
    UNREACHABLE();
  }

  Operand InputOperand2_64(size_t index) {
    switch (AddressingModeField::decode(instr_->opcode())) {
      case kMode_None:
        return InputOperand64(index);
      case kMode_Operand2_R_LSL_I:
        return Operand(InputRegister64(index), LSL, InputInt6(index + 1));
      case kMode_Operand2_R_LSR_I:
        return Operand(InputRegister64(index), LSR, InputInt6(index + 1));
      case kMode_Operand2_R_ASR_I:
        return Operand(InputRegister64(index), ASR, InputInt6(index + 1));
      case kMode_Operand2_R_ROR_I:
        return Operand(InputRegister64(index), ROR, InputInt6(index + 1));
      case kMode_Operand2_R_UXTB:
        return Operand(InputRegister64(index), UXTB);
      case kMode_Operand2_R_UXTH:
        return Operand(InputRegister64(index), UXTH);
      case kMode_Operand2_R_SXTB:
        return Operand(InputRegister64(index), SXTB);
      case kMode_Operand2_R_SXTH:
        return Operand(InputRegister64(index), SXTH);
      case kMode_Operand2_R_SXTW:
        return Operand(InputRegister64(index), SXTW);
      case kMode_MRI:
      case kMode_MRR:
      case kMode_Root:
        break;
    }
    UNREACHABLE();
  }

  MemOperand MemoryOperand(size_t index = 0) {
    switch (AddressingModeField::decode(instr_->opcode())) {
      case kMode_None:
      case kMode_Operand2_R_LSR_I:
      case kMode_Operand2_R_ASR_I:
      case kMode_Operand2_R_ROR_I:
      case kMode_Operand2_R_UXTB:
      case kMode_Operand2_R_UXTH:
      case kMode_Operand2_R_SXTB:
      case kMode_Operand2_R_SXTH:
      case kMode_Operand2_R_SXTW:
        break;
      case kMode_Root:
        return MemOperand(kRootRegister, InputInt64(index));
      case kMode_Operand2_R_LSL_I:
        return MemOperand(InputRegister(index + 0), InputRegister(index + 1),
                          LSL, InputInt32(index + 2));
      case kMode_MRI:
        return MemOperand(InputRegister(index + 0), InputInt32(index + 1));
      case kMode_MRR:
        return MemOperand(InputRegister(index + 0), InputRegister(index + 1));
    }
    UNREACHABLE();
  }

  Operand ToOperand(InstructionOperand* op) {
    if (op->IsRegister()) {
      return Operand(ToRegister(op));
    }
    return ToImmediate(op);
  }

  Operand ToOperand32(InstructionOperand* op) {
    if (op->IsRegister()) {
      return Operand(ToRegister(op).W());
    }
    return ToImmediate(op);
  }

  Operand ToImmediate(InstructionOperand* operand) {
    Constant constant = ToConstant(operand);
    switch (constant.type()) {
      case Constant::kInt32:
        return Operand(constant.ToInt32(), constant.rmode());
      case Constant::kInt64:
        return Operand(constant.ToInt64(), constant.rmode());
      case Constant::kFloat32:
        return Operand::EmbeddedNumber(constant.ToFloat32());
      case Constant::kFloat64:
        return Operand::EmbeddedNumber(constant.ToFloat64().value());
      case Constant::kExternalReference:
        return Operand(constant.ToExternalReference());
      case Constant::kCompressedHeapObject: {
        RootIndex root_index;
        if (gen_->isolate()->roots_table().IsRootHandle(constant.ToHeapObject(),
                                                        &root_index)) {
          CHECK(COMPRESS_POINTERS_BOOL);
          CHECK(V8_STATIC_ROOTS_BOOL || !gen_->isolate()->bootstrapper());
          Tagged_t ptr =
              MacroAssemblerBase::ReadOnlyRootPtr(root_index, gen_->isolate());
          CHECK(Assembler::IsImmAddSub(ptr));
          return Immediate(ptr);
        }

        return Operand(constant.ToHeapObject());
      }
      case Constant::kHeapObject:
        return Operand(constant.ToHeapObject());
      case Constant::kRpoNumber:
        UNREACHABLE();  // TODO(dcarney): RPO immediates on arm64.
    }
    UNREACHABLE();
  }

  MemOperand ToMemOperand(InstructionOperand* op, MacroAssembler* masm) const {
    DCHECK_NOT_NULL(op);
    DCHECK(op->IsStackSlot() || op->IsFPStackSlot());
    return SlotToMemOperand(AllocatedOperand::cast(op)->index(), masm);
  }

  MemOperand SlotToMemOperand(int slot, MacroAssembler* masm) const {
    FrameOffset offset = frame_access_state()->GetFrameOffset(slot);
    if (offset.from_frame_pointer()) {
      int from_sp = offset.offset() + frame_access_state()->GetSPToFPOffset();
      // Convert FP-offsets to SP-offsets if it results in better code.
      if (!frame_access_state()->FPRelativeOnly() &&
          (Assembler::IsImmLSUnscaled(from_sp) ||
           Assembler::IsImmLSScaled(from_sp, 3))) {
        offset = FrameOffset::FromStackPointer(from_sp);
      }
    }
    // Access below the stack pointer is not expected in arm64 and is actively
    // prevented at run time in the simulator.
    DCHECK_IMPLIES(offset.from_stack_pointer(), offset.offset() >= 0);
    return MemOperand(offset.from_stack_pointer() ? sp : fp, offset.offset());
  }
};

namespace {

class OutOfLineRecordWrite final : public OutOfLineCode {
 public:
  OutOfLineRecordWrite(
      CodeGenerator* gen, Register object, Operand offset, Register value,
      RecordWriteMode mode, StubCallMode stub_mode,
      UnwindingInfoWriter* unwinding_info_writer,
      IndirectPointerTag indirect_pointer_tag = kIndirectPointerNullTag)
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
        zone_(gen->zone()),
        indirect_pointer_tag_(indirect_pointer_tag) {
  }

  void Generate() final {
    // When storing an indirect pointer, the value will always be a
    // full/decompressed pointer.
    if (COMPRESS_POINTERS_BOOL &&
        mode_ != RecordWriteMode::kValueIsIndirectPointer) {
      __ DecompressTagged(value_, value_);
    }

    // No need to check value page flags with the indirect pointer write barrier
    // because the value is always an ExposedTrustedObject.
    if (mode_ != RecordWriteMode::kValueIsIndirectPointer) {
      __ CheckPageFlag(value_, MemoryChunk::kPointersToHereAreInterestingMask,
                       eq, exit());
    }

    SaveFPRegsMode const save_fp_mode = frame()->DidAllocateDoubleRegisters()
                                            ? SaveFPRegsMode::kSave
                                            : SaveFPRegsMode::kIgnore;
    if (must_save_lr_) {
      // We need to save and restore lr if the frame was elided.
      __ Push<MacroAssembler::kSignLR>(lr, padreg);
      unwinding_info_writer_->MarkLinkRegisterOnTopOfStack(__ pc_offset(), sp);
    }
    if (mode_ == RecordWriteMode::kValueIsEphemeronKey) {
      __ CallEphemeronKeyBarrier(object_, offset_, save_fp_mode);
    } else if (mode_ == RecordWriteMode::kValueIsIndirectPointer) {
      // We must have a valid indirect pointer tag here. Otherwise, we risk not
      // invoking the correct write barrier, which may lead to subtle issues.
      CHECK(IsValidIndirectPointerTag(indirect_pointer_tag_));
      __ CallIndirectPointerBarrier(object_, offset_, save_fp_mode,
                                    indirect_pointer_tag_);
#if V8_ENABLE_WEBASSEMBLY
    } else if (stub_mode_ == StubCallMode::kCallWasmRuntimeStub) {
      // A direct call to a wasm runtime stub defined in this module.
      // Just encode the stub index. This will be patched when the code
      // is added to the native module and copied into wasm code space.
      __ CallRecordWriteStubSaveRegisters(object_, offset_, save_fp_mode,
                                          StubCallMode::kCallWasmRuntimeStub);
#endif  // V8_ENABLE_WEBASSEMBLY
    } else {
      __ CallRecordWriteStubSaveRegisters(object_, offset_, save_fp_mode);
    }
    if (must_save_lr_) {
      __ Pop<MacroAssembler::kAuthLR>(padreg, lr);
      unwinding_info_writer_->MarkPopLinkRegisterFromTopOfStack(__ pc_offset());
    }
  }

 private:
  Register const object_;
  Operand const offset_;
  Register const value_;
  RecordWriteMode const mode_;
#if V8_ENABLE_WEBASSEMBLY
  StubCallMode const stub_mode_;
#endif  // V8_ENABLE_WEBASSEMBLY
  bool must_save_lr_;
  UnwindingInfoWriter* const unwinding_info_writer_;
  Zone* zone_;
  IndirectPointerTag indirect_pointer_tag_;
};

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
    case kUnorderedEqual:
    case kUnorderedNotEqual:
    case kIsNaN:
    case kIsNotNaN:
      break;
    case kPositiveOrZero:
      return pl;
    case kNegative:
      return mi;
  }
  UNREACHABLE();
}

#if V8_ENABLE_WEBASSEMBLY
class WasmOutOfLineTrap : public OutOfLineCode {
 public:
  WasmOutOfLineTrap(CodeGenerator* gen, Instruction* instr)
      : OutOfLineCode(gen), gen_(gen), instr_(instr) {}
  void Generate() override {
    Arm64OperandConverter i(gen_, instr_);
    TrapId trap_id =
        static_cast<TrapId>(i.InputInt32(instr_->InputCount() - 1));
    GenerateCallToTrap(trap_id);
  }

 protected:
  CodeGenerator* gen_;

  void GenerateWithTrapId(TrapId trap_id) { GenerateCallToTrap(trap_id); }

 private:
  void GenerateCallToTrap(TrapId trap_id) {
    gen_->AssembleSourcePosition(instr_);
    __ Call(static_cast<Address>(trap_id), RelocInfo::WASM_STUB_CALL);
    ReferenceMap* reference_map = gen_->zone()->New<ReferenceMap>(gen_->zone());
    gen_->RecordSafepoint(reference_map);
    __ AssertUnreachable(AbortReason::kUnexpectedReturnFromWasmTrap);
  }

  Instruction* instr_;
};

void RecordTrapInfoIfNeeded(Zone* zone, CodeGenerator* codegen,
                            InstructionCode opcode, Instruction* instr,
                            int pc) {
  const MemoryAccessMode access_mode = AccessModeField::decode(opcode);
  if (access_mode == kMemoryAccessProtectedMemOutOfBounds ||
      access_mode == kMemoryAccessProtectedNullDereference) {
    codegen->RecordProtectedInstruction(pc);
  }
}
#else
void RecordTrapInfoIfNeeded(Zone* zone, CodeGenerator* codegen,
                            InstructionCode opcode, Instruction* instr,
                            int pc) {
  DCHECK_EQ(kMemoryAccessDirect, AccessModeField::decode(opcode));
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Handles unary ops that work for float (scalar), double (scalar), or NEON.
template <typename Fn>
void EmitFpOrNeonUnop(MacroAssembler* masm, Fn fn, Instruction* instr,
                      Arm64OperandConverter i, VectorFormat scalar,
                      VectorFormat vector) {
  VectorFormat f = instr->InputAt(0)->IsSimd128Register() ? vector : scalar;

  VRegister output = VRegister::Create(i.OutputDoubleRegister().code(), f);
  VRegister input = VRegister::Create(i.InputDoubleRegister(0).code(), f);
  (masm->*fn)(output, input);
}

}  // namespace

#define ASSEMBLE_SHIFT(asm_instr, width)                                    \
  do {                                                                      \
    if (instr->InputAt(1)->IsRegister()) {                                  \
      __ asm_instr(i.OutputRegister##width(), i.InputRegister##width(0),    \
                   i.InputRegister##width(1));                              \
    } else {                                                                \
      uint32_t imm =                                                        \
          static_cast<uint32_t>(i.InputOperand##width(1).ImmediateValue()); \
      __ asm_instr(i.OutputRegister##width(), i.InputRegister##width(0),    \
                   imm % (width));                                          \
    }                                                                       \
  } while (0)

#define ASSEMBLE_ATOMIC_LOAD_INTEGER(asm_instr, reg)                     \
  do {                                                                   \
    __ Add(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));   \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
    __ asm_instr(i.Output##reg(), i.TempRegister(0));                    \
  } while (0)

#define ASSEMBLE_ATOMIC_STORE_INTEGER(asm_instr, reg)                    \
  do {                                                                   \
    __ Add(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));   \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
    __ asm_instr(i.Input##reg(2), i.TempRegister(0));                    \
  } while (0)

#define ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(suffix, reg)                      \
  do {                                                                     \
    __ Add(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));     \
    if (CpuFeatures::IsSupported(LSE)) {                                   \
      CpuFeatureScope scope(masm(), LSE);                                  \
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
      __ Swpal##suffix(i.Input##reg(2), i.Output##reg(),                   \
                       MemOperand(i.TempRegister(0)));                     \
    } else {                                                               \
      Label exchange;                                                      \
      __ Bind(&exchange);                                                  \
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
      __ ldaxr##suffix(i.Output##reg(), i.TempRegister(0));                \
      __ stlxr##suffix(i.TempRegister32(1), i.Input##reg(2),               \
                       i.TempRegister(0));                                 \
      __ Cbnz(i.TempRegister32(1), &exchange);                             \
    }                                                                      \
  } while (0)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(suffix, ext, reg)         \
  do {                                                                     \
    __ Add(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));     \
    if (CpuFeatures::IsSupported(LSE)) {                                   \
      DCHECK_EQ(i.OutputRegister(), i.InputRegister(2));                   \
      CpuFeatureScope scope(masm(), LSE);                                  \
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
      __ Casal##suffix(i.Output##reg(), i.Input##reg(3),                   \
                       MemOperand(i.TempRegister(0)));                     \
    } else {                                                               \
      Label compareExchange;                                               \
      Label exit;                                                          \
      __ Bind(&compareExchange);                                           \
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
      __ ldaxr##suffix(i.Output##reg(), i.TempRegister(0));                \
      __ Cmp(i.Output##reg(), Operand(i.Input##reg(2), ext));              \
      __ B(ne, &exit);                                                     \
      __ stlxr##suffix(i.TempRegister32(1), i.Input##reg(3),               \
                       i.TempRegister(0));                                 \
      __ Cbnz(i.TempRegister32(1), &compareExchange);                      \
      __ Bind(&exit);                                                      \
    }                                                                      \
  } while (0)

#define ASSEMBLE_ATOMIC_SUB(suffix, reg)                                   \
  do {                                                                     \
    __ Add(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));     \
    if (CpuFeatures::IsSupported(LSE)) {                                   \
      CpuFeatureScope scope(masm(), LSE);                                  \
      UseScratchRegisterScope temps(masm());                               \
      Register scratch = temps.AcquireSameSizeAs(i.Input##reg(2));         \
      __ Neg(scratch, i.Input##reg(2));                                    \
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
      __ Ldaddal##suffix(scratch, i.Output##reg(),                         \
                         MemOperand(i.TempRegister(0)));                   \
    } else {                                                               \
      Label binop;                                                         \
      __ Bind(&binop);                                                     \
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
      __ ldaxr##suffix(i.Output##reg(), i.TempRegister(0));                \
      __ Sub(i.Temp##reg(1), i.Output##reg(), Operand(i.Input##reg(2)));   \
      __ stlxr##suffix(i.TempRegister32(2), i.Temp##reg(1),                \
                       i.TempRegister(0));                                 \
      __ Cbnz(i.TempRegister32(2), &binop);                                \
    }                                                                      \
  } while (0)

#define ASSEMBLE_ATOMIC_AND(suffix, reg)                                   \
  do {                                                                     \
    __ Add(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));     \
    if (CpuFeatures::IsSupported(LSE)) {                                   \
      CpuFeatureScope scope(masm(), LSE);                                  \
      UseScratchRegisterScope temps(masm());                               \
      Register scratch = temps.AcquireSameSizeAs(i.Input##reg(2));         \
      __ Mvn(scratch, i.Input##reg(2));                                    \
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
      __ Ldclral##suffix(scratch, i.Output##reg(),                         \
                         MemOperand(i.TempRegister(0)));                   \
    } else {                                                               \
      Label binop;                                                         \
      __ Bind(&binop);                                                     \
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
      __ ldaxr##suffix(i.Output##reg(), i.TempRegister(0));                \
      __ And(i.Temp##reg(1), i.Output##reg(), Operand(i.Input##reg(2)));   \
      __ stlxr##suffix(i.TempRegister32(2), i.Temp##reg(1),                \
                       i.TempRegister(0));                                 \
      __ Cbnz(i.TempRegister32(2), &binop);                                \
    }                                                                      \
  } while (0)

#define ASSEMBLE_ATOMIC_BINOP(suffix, bin_instr, lse_instr, reg)               \
  do {                                                                         \
    __ Add(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));         \
    if (CpuFeatures::IsSupported(LSE)) {                                       \
      CpuFeatureScope scope(masm(), LSE);                                      \
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());     \
      __ lse_instr##suffix(i.Input##reg(2), i.Output##reg(),                   \
                           MemOperand(i.TempRegister(0)));                     \
    } else {                                                                   \
      Label binop;                                                             \
      __ Bind(&binop);                                                         \
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());     \
      __ ldaxr##suffix(i.Output##reg(), i.TempRegister(0));                    \
      __ bin_instr(i.Temp##reg(1), i.Output##reg(), Operand(i.Input##reg(2))); \
      __ stlxr##suffix(i.TempRegister32(2), i.Temp##reg(1),                    \
                       i.TempRegister(0));                                     \
      __ Cbnz(i.TempRegister32(2), &binop);                                    \
    }                                                                          \
  } while (0)

#define ASSEMBLE_IEEE754_BINOP(name)                                        \
  do {                                                                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                           \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 2); \
  } while (0)

#define ASSEMBLE_IEEE754_UNOP(name)                                         \
  do {                                                                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                           \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 1); \
  } while (0)

// If shift value is an immediate, we can call asm_imm, taking the shift value
// modulo 2^width. Otherwise, emit code to perform the modulus operation, and
// call asm_shl.
#define ASSEMBLE_SIMD_SHIFT_LEFT(asm_imm, width, format, asm_shl, gp)       \
  do {                                                                      \
    if (instr->InputAt(1)->IsImmediate()) {                                 \
      __ asm_imm(i.OutputSimd128Register().format(),                        \
                 i.InputSimd128Register(0).format(), i.InputInt##width(1)); \
    } else {                                                                \
      UseScratchRegisterScope temps(masm());                                \
      VRegister tmp = temps.AcquireQ();                                     \
      Register shift = temps.Acquire##gp();                                 \
      constexpr int mask = (1 << width) - 1;                                \
      __ And(shift, i.InputRegister32(1), mask);                            \
      __ Dup(tmp.format(), shift);                                          \
      __ asm_shl(i.OutputSimd128Register().format(),                        \
                 i.InputSimd128Register(0).format(), tmp.format());         \
    }                                                                       \
  } while (0)

// If shift value is an immediate, we can call asm_imm, taking the shift value
// modulo 2^width. Otherwise, emit code to perform the modulus operation, and
// call asm_shl, passing in the negative shift value (treated as right shift).
#define ASSEMBLE_SIMD_SHIFT_RIGHT(asm_imm, width, format, asm_shl, gp)      \
  do {                                                                      \
    if (instr->InputAt(1)->IsImmediate()) {                                 \
      __ asm_imm(i.OutputSimd128Register().format(),                        \
                 i.InputSimd128Register(0).format(), i.InputInt##width(1)); \
    } else {                                                                \
      UseScratchRegisterScope temps(masm());                                \
      VRegister tmp = temps.AcquireQ();                                     \
      Register shift = temps.Acquire##gp();                                 \
      constexpr int mask = (1 << width) - 1;                                \
      __ And(shift, i.InputRegister32(1), mask);                            \
      __ Dup(tmp.format(), shift);                                          \
      __ Neg(tmp.format(), tmp.format());                                   \
      __ asm_shl(i.OutputSimd128Register().format(),                        \
                 i.InputSimd128Register(0).format(), tmp.format());         \
    }                                                                       \
  } while (0)

void CodeGenerator::AssembleDeconstructFrame() {
  __ Mov(sp, fp);
  __ Pop<MacroAssembler::kAuthLR>(fp, lr);

  unwinding_info_writer_.MarkFrameDeconstructed(__ pc_offset());
}

void CodeGenerator::AssemblePrepareTailCall() {
  if (frame_access_state()->has_frame()) {
    __ RestoreFPAndLR();
  }
  frame_access_state()->SetFrameAccessToSP();
}

namespace {

void AdjustStackPointerForTailCall(MacroAssembler* masm,
                                   FrameAccessState* state,
                                   int new_slot_above_sp,
                                   bool allow_shrinkage = true) {
  int current_sp_offset = state->GetSPToFPSlotCount() +
                          StandardFrameConstants::kFixedSlotCountAboveFp;
  int stack_slot_delta = new_slot_above_sp - current_sp_offset;
  DCHECK_EQ(stack_slot_delta % 2, 0);
  if (stack_slot_delta > 0) {
    masm->Claim(stack_slot_delta);
    state->IncreaseSPDelta(stack_slot_delta);
  } else if (allow_shrinkage && stack_slot_delta < 0) {
    masm->Drop(-stack_slot_delta);
    state->IncreaseSPDelta(stack_slot_delta);
  }
}

}  // namespace

void CodeGenerator::AssembleTailCallBeforeGap(Instruction* instr,
                                              int first_unused_slot_offset) {
  AdjustStackPointerForTailCall(masm(), frame_access_state(),
                                first_unused_slot_offset, false);
}

void CodeGenerator::AssembleTailCallAfterGap(Instruction* instr,
                                             int first_unused_slot_offset) {
  DCHECK_EQ(first_unused_slot_offset % 2, 0);
  AdjustStackPointerForTailCall(masm(), frame_access_state(),
                                first_unused_slot_offset);
  DCHECK(instr->IsTailCall());
  InstructionOperandConverter g(this, instr);
  int optional_padding_offset = g.InputInt32(instr->InputCount() - 2);
  if (optional_padding_offset % 2) {
    __ Poke(padreg, optional_padding_offset * kSystemPointerSize);
  }
}

// Check that {kJavaScriptCallCodeStartRegister} is correct.
void CodeGenerator::AssembleCodeStartRegisterCheck() {
  UseScratchRegisterScope temps(masm());
  Register scratch = temps.AcquireX();
  __ ComputeCodeStartAddress(scratch);
  __ cmp(scratch, kJavaScriptCallCodeStartRegister);
  __ Assert(eq, AbortReason::kWrongFunctionCodeStart);
}

void CodeGenerator::BailoutIfDeoptimized() { __ BailoutIfDeoptimized(); }

// Assembles an instruction after register allocation, producing machine code.
CodeGenerator::CodeGenResult CodeGenerator::AssembleArchInstruction(
    Instruction* instr) {
  Arm64OperandConverter i(this, instr);
  InstructionCode opcode = instr->opcode();
  ArchOpcode arch_opcode = ArchOpcodeField::decode(opcode);
  switch (arch_opcode) {
    case kArchCallCodeObject: {
      if (instr->InputAt(0)->IsImmediate()) {
        __ Call(i.InputCode(0), RelocInfo::CODE_TARGET);
      } else {
        Register reg = i.InputRegister(0);
        CodeEntrypointTag tag =
            i.InputCodeEntrypointTag(instr->CodeEnrypointTagInputIndex());
        DCHECK_IMPLIES(
            instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ CallCodeObject(reg, tag);
      }
      RecordCallPosition(instr);
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
    case kArchCallWasmFunction: {
      if (instr->InputAt(0)->IsImmediate()) {
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt64());
        __ Call(wasm_code, constant.rmode());
      } else {
        Register target = i.InputRegister(0);
        __ Call(target);
      }
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchTailCallWasm: {
      if (instr->InputAt(0)->IsImmediate()) {
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt64());
        __ Jump(wasm_code, constant.rmode());
      } else {
        Register target = i.InputRegister(0);
        UseScratchRegisterScope temps(masm());
        temps.Exclude(x17);
        __ Mov(x17, target);
        __ Jump(x17);
      }
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
        CodeEntrypointTag tag =
            i.InputCodeEntrypointTag(instr->CodeEnrypointTagInputIndex());
        DCHECK_IMPLIES(
            instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ JumpCodeObject(reg, tag);
      }
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
      UseScratchRegisterScope temps(masm());
      temps.Exclude(x17);
      __ Mov(x17, reg);
      __ Jump(x17);
      unwinding_info_writer_.MarkBlockWillExit();
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchCallJSFunction: {
      Register func = i.InputRegister(0);
      if (v8_flags.debug_code) {
        // Check the function's context matches the context argument.
        UseScratchRegisterScope scope(masm());
        Register temp = scope.AcquireX();
        __ LoadTaggedField(temp,
                           FieldMemOperand(func, JSFunction::kContextOffset));
        __ cmp(cp, temp);
        __ Assert(eq, AbortReason::kWrongFunctionContext);
      }
      __ CallJSFunction(func);
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchPrepareCallCFunction:
      // We don't need kArchPrepareCallCFunction on arm64 as the instruction
      // selector has already performed a Claim to reserve space on the stack.
      // Frame alignment is always 16 bytes, and the stack pointer is already
      // 16-byte aligned, therefore we do not need to align the stack pointer
      // by an unknown value, and it is safe to continue accessing the frame
      // via the stack pointer.
      UNREACHABLE();
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
      int const num_gp_parameters = ParamField::decode(instr->opcode());
      int const num_fp_parameters = FPParamField::decode(instr->opcode());
      Label return_location;
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes;
#if V8_ENABLE_WEBASSEMBLY
      if (linkage()->GetIncomingDescriptor()->IsWasmCapiFunction()) {
        // Put the return address in a stack slot.
        __ StoreReturnAddressInWasmExitFrame(&return_location);
        set_isolate_data_slots = SetIsolateDataSlots::kNo;
      }
#endif  // V8_ENABLE_WEBASSEMBLY
      int pc_offset;
      if (instr->InputAt(0)->IsImmediate()) {
        ExternalReference ref = i.InputExternalReference(0);
        pc_offset = __ CallCFunction(ref, num_gp_parameters, num_fp_parameters,
                                     set_isolate_data_slots, &return_location);
      } else {
        Register func = i.InputRegister(0);
        pc_offset = __ CallCFunction(func, num_gp_parameters, num_fp_parameters,
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
      break;
    case kArchTableSwitch:
      AssembleArchTableSwitch(instr);
      break;
    case kArchBinarySearchSwitch:
      AssembleArchBinarySearchSwitch(instr);
      break;
    case kArchAbortCSADcheck:
      DCHECK_EQ(i.InputRegister(0), x1);
      {
        // We don't actually want to generate a pile of code for this, so just
        // claim there is a stack frame, without generating one.
        FrameScope scope(masm(), StackFrame::NO_FRAME_TYPE);
        __ CallBuiltin(Builtin::kAbortCSADcheck);
      }
      __ Debug("kArchAbortCSADcheck", 0, BREAK);
      unwinding_info_writer_.MarkBlockWillExit();
      break;
    case kArchDebugBreak:
      __ DebugBreak();
      break;
    case kArchComment:
      __ RecordComment(reinterpret_cast<const char*>(i.InputInt64(0)));
      break;
    case kArchThrowTerminator:
      unwinding_info_writer_.MarkBlockWillExit();
      break;
    case kArchNop:
      // don't emit code for nops.
      break;
    case kArchDeoptimize: {
      DeoptimizationExit* exit =
          BuildTranslation(instr, -1, 0, 0, OutputFrameStateCombine::Ignore());
      __ B(exit->label());
      break;
    }
    case kArchRet:
      AssembleReturn(instr->InputAt(0));
      break;
    case kArchFramePointer:
      __ mov(i.OutputRegister(), fp);
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
    case kArchSetStackPointer: {
      DCHECK(instr->InputAt(0)->IsRegister());
      if (masm()->options().enable_simulator_code) {
        __ RecordComment("-- Set simulator stack limit --");
        DCHECK(__ TmpList()->IncludesAliasOf(kSimulatorHltArgument));
        __ LoadStackLimit(kSimulatorHltArgument,
                          StackLimitKind::kRealStackLimit);
        __ hlt(kImmExceptionIsSwitchStackLimit);
      }
      __ Mov(sp, i.InputRegister(0));
      break;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    case kArchStackPointerGreaterThan: {
      // Potentially apply an offset to the current stack pointer before the
      // comparison to consider the size difference of an optimized frame versus
      // the contained unoptimized frames.

      Register lhs_register = sp;
      uint32_t offset;

      if (ShouldApplyOffsetToStackCheck(instr, &offset)) {
        lhs_register = i.TempRegister(0);
        __ Sub(lhs_register, sp, offset);
      }

      constexpr size_t kValueIndex = 0;
      DCHECK(instr->InputAt(kValueIndex)->IsRegister());
      __ Cmp(lhs_register, i.InputRegister(kValueIndex));
      break;
    }
    case kArchStackCheckOffset:
      __ Move(i.OutputRegister(), Smi::FromInt(GetStackCheckOffset()));
      break;
    case kArchTruncateDoubleToI:
      __ TruncateDoubleToI(isolate(), zone(), i.OutputRegister(),
                           i.InputDoubleRegister(0), DetermineStubCallMode(),
                           frame_access_state()->has_frame()
                               ? kLRHasBeenSaved
                               : kLRHasNotBeenSaved);

      break;
    case kArchStoreWithWriteBarrier: {
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      // Indirect pointer writes must use a different opcode.
      DCHECK_NE(mode, RecordWriteMode::kValueIsIndirectPointer);
      AddressingMode addressing_mode =
          AddressingModeField::decode(instr->opcode());
      Register object = i.InputRegister(0);
      Operand offset(0);
      if (addressing_mode == kMode_MRI) {
        offset = Operand(i.InputInt64(1));
      } else {
        DCHECK_EQ(addressing_mode, kMode_MRR);
        offset = Operand(i.InputRegister(1));
      }
      Register value = i.InputRegister(2);

      if (v8_flags.debug_code) {
        // Checking that |value| is not a cleared weakref: our write barrier
        // does not support that for now.
        __ cmp(value, Operand(kClearedWeakHeapObjectLower32));
        __ Check(ne, AbortReason::kOperandIsCleared);
      }

      auto ool = zone()->New<OutOfLineRecordWrite>(
          this, object, offset, value, mode, DetermineStubCallMode(),
          &unwinding_info_writer_);
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ StoreTaggedField(value, MemOperand(object, offset));
      if (mode > RecordWriteMode::kValueIsIndirectPointer) {
        __ JumpIfSmi(value, ool->exit());
      }
      __ CheckPageFlag(object, MemoryChunk::kPointersFromHereAreInterestingMask,
                       ne, ool->entry());
      __ Bind(ool->exit());
      break;
    }
    case kArchAtomicStoreWithWriteBarrier: {
      DCHECK_EQ(AddressingModeField::decode(instr->opcode()), kMode_MRR);
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      // Indirect pointer writes must use a different opcode.
      DCHECK_NE(mode, RecordWriteMode::kValueIsIndirectPointer);
      Register object = i.InputRegister(0);
      Register offset = i.InputRegister(1);
      Register value = i.InputRegister(2);
      auto ool = zone()->New<OutOfLineRecordWrite>(
          this, object, offset, value, mode, DetermineStubCallMode(),
          &unwinding_info_writer_);
      __ AtomicStoreTaggedField(value, object, offset, i.TempRegister(0));
      // Skip the write barrier if the value is a Smi. However, this is only
      // valid if the value isn't an indirect pointer. Otherwise the value will
      // be a pointer table index, which will always look like a Smi (but
      // actually reference a pointer in the pointer table).
      if (mode > RecordWriteMode::kValueIsIndirectPointer) {
        __ JumpIfSmi(value, ool->exit());
      }
      __ CheckPageFlag(object, MemoryChunk::kPointersFromHereAreInterestingMask,
                       ne, ool->entry());
      __ Bind(ool->exit());
      break;
    }
    case kArchStoreIndirectWithWriteBarrier: {
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      DCHECK_EQ(mode, RecordWriteMode::kValueIsIndirectPointer);
      AddressingMode addressing_mode =
          AddressingModeField::decode(instr->opcode());
      Register object = i.InputRegister(0);
      Operand offset(0);
      if (addressing_mode == kMode_MRI) {
        offset = Operand(i.InputInt64(1));
      } else {
        DCHECK_EQ(addressing_mode, kMode_MRR);
        offset = Operand(i.InputRegister(1));
      }
      Register value = i.InputRegister(2);
      IndirectPointerTag tag = static_cast<IndirectPointerTag>(i.InputInt64(3));
      DCHECK(IsValidIndirectPointerTag(tag));

      auto ool = zone()->New<OutOfLineRecordWrite>(
          this, object, offset, value, mode, DetermineStubCallMode(),
          &unwinding_info_writer_, tag);
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ StoreIndirectPointerField(value, MemOperand(object, offset));
      __ JumpIfMarking(ool->entry());
      __ Bind(ool->exit());
      break;
    }
    case kArchStackSlot: {
      FrameOffset offset =
          frame_access_state()->GetFrameOffset(i.InputInt32(0));
      Register base = offset.from_stack_pointer() ? sp : fp;
      __ Add(i.OutputRegister(0), base, Operand(offset.offset()));
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
    case kIeee754Float64Cos:
      ASSEMBLE_IEEE754_UNOP(cos);
      break;
    case kIeee754Float64Cosh:
      ASSEMBLE_IEEE754_UNOP(cosh);
      break;
    case kIeee754Float64Cbrt:
      ASSEMBLE_IEEE754_UNOP(cbrt);
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
    case kArm64Float16RoundDown:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintm, instr, i, kFormatH,
                       kFormat8H);
      break;
    case kArm64Float32RoundDown:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintm, instr, i, kFormatS,
                       kFormat4S);
      break;
    case kArm64Float64RoundDown:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintm, instr, i, kFormatD,
                       kFormat2D);
      break;
    case kArm64Float16RoundUp:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintp, instr, i, kFormatH,
                       kFormat8H);
      break;
    case kArm64Float32RoundUp:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintp, instr, i, kFormatS,
                       kFormat4S);
      break;
    case kArm64Float64RoundUp:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintp, instr, i, kFormatD,
                       kFormat2D);
      break;
    case kArm64Float64RoundTiesAway:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frinta, instr, i, kFormatD,
                       kFormat2D);
      break;
    case kArm64Float16RoundTruncate:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintz, instr, i, kFormatH,
                       kFormat8H);
      break;
    case kArm64Float32RoundTruncate:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintz, instr, i, kFormatS,
                       kFormat4S);
      break;
    case kArm64Float64RoundTruncate:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintz, instr, i, kFormatD,
                       kFormat2D);
      break;
    case kArm64Float16RoundTiesEven:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintn, instr, i, kFormatH,
                       kFormat8H);
      break;
    case kArm64Float32RoundTiesEven:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintn, instr, i, kFormatS,
                       kFormat4S);
      break;
    case kArm64Float64RoundTiesEven:
      EmitFpOrNeonUnop(masm(), &MacroAssembler::Frintn, instr, i, kFormatD,
                       kFormat2D);
      break;
    case kArm64Add:
      if (FlagsModeField::decode(opcode) != kFlags_none) {
        __ Adds(i.OutputRegister(), i.InputOrZeroRegister64(0),
                i.InputOperand2_64(1));
      } else {
        __ Add(i.OutputRegister(), i.InputOrZeroRegister64(0),
               i.InputOperand2_64(1));
      }
      break;
    case kArm64Add32:
      if (FlagsModeField::decode(opcode) != kFlags_none) {
        __ Adds(i.OutputRegister32(), i.InputOrZeroRegister32(0),
                i.InputOperand2_32(1));
      } else {
        __ Add(i.OutputRegister32(), i.InputOrZeroRegister32(0),
               i.InputOperand2_32(1));
      }
      break;
    case kArm64And:
      if (FlagsModeField::decode(opcode) != kFlags_none) {
        // The ands instruction only sets N and Z, so only the following
        // conditions make sense.
        DCHECK(FlagsConditionField::decode(opcode) == kEqual ||
               FlagsConditionField::decode(opcode) == kNotEqual ||
               FlagsConditionField::decode(opcode) == kPositiveOrZero ||
               FlagsConditionField::decode(opcode) == kNegative);
        __ Ands(i.OutputRegister(), i.InputOrZeroRegister64(0),
                i.InputOperand2_64(1));
      } else {
        __ And(i.OutputRegister(), i.InputOrZeroRegister64(0),
               i.InputOperand2_64(1));
      }
      break;
    case kArm64And32:
      if (FlagsModeField::decode(opcode) != kFlags_none) {
        // The ands instruction only sets N and Z, so only the following
        // conditions make sense.
        DCHECK(FlagsConditionField::decode(opcode) == kEqual ||
               FlagsConditionField::decode(opcode) == kNotEqual ||
               FlagsConditionField::decode(opcode) == kPositiveOrZero ||
               FlagsConditionField::decode(opcode) == kNegative);
        __ Ands(i.OutputRegister32(), i.InputOrZeroRegister32(0),
                i.InputOperand2_32(1));
      } else {
        __ And(i.OutputRegister32(), i.InputOrZeroRegister32(0),
               i.InputOperand2_32(1));
      }
      break;
    case kArm64Bic:
      __ Bic(i.OutputRegister(), i.InputOrZeroRegister64(0),
             i.InputOperand2_64(1));
      break;
    case kArm64Bic32:
      __ Bic(i.OutputRegister32(), i.InputOrZeroRegister32(0),
             i.InputOperand2_32(1));
      break;
    case kArm64Mul:
      __ Mul(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kArm64Smulh:
      __ Smulh(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kArm64Umulh:
      __ Umulh(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kArm64Mul32:
      __ Mul(i.OutputRegister32(), i.InputRegister32(0), i.InputRegister32(1));
      break;
#if V8_ENABLE_WEBASSEMBLY
    case kArm64Sadalp: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat src_f = VectorFormatHalfWidthDoubleLanes(dst_f);
      __ Sadalp(i.OutputSimd128Register().Format(dst_f),
                i.InputSimd128Register(1).Format(src_f));
      break;
    }
    case kArm64Saddlp: {
      VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat src_f = VectorFormatHalfWidthDoubleLanes(dst_f);
      __ Saddlp(i.OutputSimd128Register().Format(dst_f),
                i.InputSimd128Register(0).Format(src_f));
      break;
    }
    case kArm64Uadalp: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat src_f = VectorFormatHalfWidthDoubleLanes(dst_f);
      __ Uadalp(i.OutputSimd128Register().Format(dst_f),
                i.InputSimd128Register(1).Format(src_f));
      break;
    }
    case kArm64Uaddlp: {
      VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat src_f = VectorFormatHalfWidthDoubleLanes(dst_f);
      __ Uaddlp(i.OutputSimd128Register().Format(dst_f),
                i.InputSimd128Register(0).Format(src_f));
      break;
    }
    case kArm64ISplat: {
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      Register src = LaneSizeField::decode(opcode) == 64 ? i.InputRegister64(0)
                                                         : i.InputRegister32(0);
      __ Dup(i.OutputSimd128Register().Format(f), src);
      break;
    }
    case kArm64FSplat: {
      VectorFormat src_f =
          ScalarFormatFromLaneSize(LaneSizeField::decode(opcode));
      VectorFormat dst_f = VectorFormatFillQ(src_f);
      if (src_f == kFormatH) {
        __ Fcvt(i.OutputFloat32Register(0).H(), i.InputFloat32Register(0));
        __ Dup(i.OutputSimd128Register().Format(dst_f),
               i.OutputSimd128Register().Format(src_f), 0);
      } else {
        __ Dup(i.OutputSimd128Register().Format(dst_f),
               i.InputSimd128Register(0).Format(src_f), 0);
      }
      break;
    }
    case kArm64Smlal: {
      VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat src_f = VectorFormatHalfWidth(dst_f);
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      __ Smlal(i.OutputSimd128Register().Format(dst_f),
               i.InputSimd128Register(1).Format(src_f),
               i.InputSimd128Register(2).Format(src_f));
      break;
    }
    case kArm64Smlal2: {
      VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat src_f = VectorFormatHalfWidthDoubleLanes(dst_f);
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      __ Smlal2(i.OutputSimd128Register().Format(dst_f),
                i.InputSimd128Register(1).Format(src_f),
                i.InputSimd128Register(2).Format(src_f));
      break;
    }
    case kArm64Umlal: {
      VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat src_f = VectorFormatHalfWidth(dst_f);
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      __ Umlal(i.OutputSimd128Register().Format(dst_f),
               i.InputSimd128Register(1).Format(src_f),
               i.InputSimd128Register(2).Format(src_f));
      break;
    }
    case kArm64Umlal2: {
      VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat src_f = VectorFormatHalfWidthDoubleLanes(dst_f);
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      __ Umlal2(i.OutputSimd128Register().Format(dst_f),
                i.InputSimd128Register(1).Format(src_f),
                i.InputSimd128Register(2).Format(src_f));
      break;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    case kArm64Smull: {
      if (instr->InputAt(0)->IsRegister()) {
        __ Smull(i.OutputRegister(), i.InputRegister32(0),
                 i.InputRegister32(1));
      } else {
        DCHECK(instr->InputAt(0)->IsSimd128Register());
        VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
        VectorFormat src_f = VectorFormatHalfWidth(dst_f);
        __ Smull(i.OutputSimd128Register().Format(dst_f),
                 i.InputSimd128Register(0).Format(src_f),
                 i.InputSimd128Register(1).Format(src_f));
      }
      break;
    }
    case kArm64Smull2: {
      VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat src_f = VectorFormatHalfWidthDoubleLanes(dst_f);
      __ Smull2(i.OutputSimd128Register().Format(dst_f),
                i.InputSimd128Register(0).Format(src_f),
                i.InputSimd128Register(1).Format(src_f));
      break;
    }
    case kArm64Umull: {
      if (instr->InputAt(0)->IsRegister()) {
        __ Umull(i.OutputRegister(), i.InputRegister32(0),
                 i.InputRegister32(1));
      } else {
        DCHECK(instr->InputAt(0)->IsSimd128Register());
        VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
        VectorFormat src_f = VectorFormatHalfWidth(dst_f);
        __ Umull(i.OutputSimd128Register().Format(dst_f),
                 i.InputSimd128Register(0).Format(src_f),
                 i.InputSimd128Register(1).Format(src_f));
      }
      break;
    }
    case kArm64Umull2: {
      VectorFormat dst_f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat src_f = VectorFormatHalfWidthDoubleLanes(dst_f);
      __ Umull2(i.OutputSimd128Register().Format(dst_f),
                i.InputSimd128Register(0).Format(src_f),
                i.InputSimd128Register(1).Format(src_f));
      break;
    }
    case kArm64Madd:
      __ Madd(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
              i.InputRegister(2));
      break;
    case kArm64Madd32:
      __ Madd(i.OutputRegister32(), i.InputRegister32(0), i.InputRegister32(1),
              i.InputRegister32(2));
      break;
    case kArm64Msub:
      __ Msub(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
              i.InputRegister(2));
      break;
    case kArm64Msub32:
      __ Msub(i.OutputRegister32(), i.InputRegister32(0), i.InputRegister32(1),
              i.InputRegister32(2));
      break;
    case kArm64Mneg:
      __ Mneg(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kArm64Mneg32:
      __ Mneg(i.OutputRegister32(), i.InputRegister32(0), i.InputRegister32(1));
      break;
    case kArm64Idiv:
      __ Sdiv(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kArm64Idiv32:
      __ Sdiv(i.OutputRegister32(), i.InputRegister32(0), i.InputRegister32(1));
      break;
    case kArm64Udiv:
      __ Udiv(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kArm64Udiv32:
      __ Udiv(i.OutputRegister32(), i.InputRegister32(0), i.InputRegister32(1));
      break;
    case kArm64Imod: {
      UseScratchRegisterScope scope(masm());
      Register temp = scope.AcquireX();
      __ Sdiv(temp, i.InputRegister(0), i.InputRegister(1));
      __ Msub(i.OutputRegister(), temp, i.InputRegister(1), i.InputRegister(0));
      break;
    }
    case kArm64Imod32: {
      UseScratchRegisterScope scope(masm());
      Register temp = scope.AcquireW();
      __ Sdiv(temp, i.InputRegister32(0), i.InputRegister32(1));
      __ Msub(i.OutputRegister32(), temp, i.InputRegister32(1),
              i.InputRegister32(0));
      break;
    }
    case kArm64Umod: {
      UseScratchRegisterScope scope(masm());
      Register temp = scope.AcquireX();
      __ Udiv(temp, i.InputRegister(0), i.InputRegister(1));
      __ Msub(i.OutputRegister(), temp, i.InputRegister(1), i.InputRegister(0));
      break;
    }
    case kArm64Umod32: {
      UseScratchRegisterScope scope(masm());
      Register temp = scope.AcquireW();
      __ Udiv(temp, i.InputRegister32(0), i.InputRegister32(1));
      __ Msub(i.OutputRegister32(), temp, i.InputRegister32(1),
              i.InputRegister32(0));
      break;
    }
    case kArm64Not:
      __ Mvn(i.OutputRegister(), i.InputOperand(0));
      break;
    case kArm64Not32:
      __ Mvn(i.OutputRegister32(), i.InputOperand32(0));
      break;
    case kArm64Or:
      __ Orr(i.OutputRegister(), i.InputOrZeroRegister64(0),
             i.InputOperand2_64(1));
      break;
    case kArm64Or32:
      __ Orr(i.OutputRegister32(), i.InputOrZeroRegister32(0),
             i.InputOperand2_32(1));
      break;
    case kArm64Orn:
      __ Orn(i.OutputRegister(), i.InputOrZeroRegister64(0),
             i.InputOperand2_64(1));
      break;
    case kArm64Orn32:
      __ Orn(i.OutputRegister32(), i.InputOrZeroRegister32(0),
             i.InputOperand2_32(1));
      break;
    case kArm64Eor:
      __ Eor(i.OutputRegister(), i.InputOrZeroRegister64(0),
             i.InputOperand2_64(1));
      break;
    case kArm64Eor32:
      __ Eor(i.OutputRegister32(), i.InputOrZeroRegister32(0),
             i.InputOperand2_32(1));
      break;
    case kArm64Eon:
      __ Eon(i.OutputRegister(), i.InputOrZeroRegister64(0),
             i.InputOperand2_64(1));
      break;
    case kArm64Eon32:
      __ Eon(i.OutputRegister32(), i.InputOrZeroRegister32(0),
             i.InputOperand2_32(1));
      break;
    case kArm64Sub:
      if (FlagsModeField::decode(opcode) != kFlags_none) {
        __ Subs(i.OutputRegister(), i.InputOrZeroRegister64(0),
                i.InputOperand2_64(1));
      } else {
        __ Sub(i.OutputRegister(), i.InputOrZeroRegister64(0),
               i.InputOperand2_64(1));
      }
      break;
    case kArm64Sub32:
      if (FlagsModeField::decode(opcode) != kFlags_none) {
        __ Subs(i.OutputRegister32(), i.InputOrZeroRegister32(0),
                i.InputOperand2_32(1));
      } else {
        __ Sub(i.OutputRegister32(), i.InputOrZeroRegister32(0),
               i.InputOperand2_32(1));
      }
      break;
    case kArm64Lsl:
      ASSEMBLE_SHIFT(Lsl, 64);
      break;
    case kArm64Lsl32:
      ASSEMBLE_SHIFT(Lsl, 32);
      break;
    case kArm64Lsr:
      ASSEMBLE_SHIFT(Lsr, 64);
      break;
    case kArm64Lsr32:
      ASSEMBLE_SHIFT(Lsr, 32);
      break;
    case kArm64Asr:
      ASSEMBLE_SHIFT(Asr, 64);
      break;
    case kArm64Asr32:
      ASSEMBLE_SHIFT(Asr, 32);
      break;
    case kArm64Ror:
      ASSEMBLE_SHIFT(Ror, 64);
      break;
    case kArm64Ror32:
      ASSEMBLE_SHIFT(Ror, 32);
      break;
    case kArm64Mov32:
      __ Mov(i.OutputRegister32(), i.InputRegister32(0));
      break;
    case kArm64Sxtb32:
      __ Sxtb(i.OutputRegister32(), i.InputRegister32(0));
      break;
    case kArm64Sxth32:
      __ Sxth(i.OutputRegister32(), i.InputRegister32(0));
      break;
    case kArm64Sxtb:
      __ Sxtb(i.OutputRegister(), i.InputRegister32(0));
      break;
    case kArm64Sxth:
      __ Sxth(i.OutputRegister(), i.InputRegister32(0));
      break;
    case kArm64Sxtw:
      __ Sxtw(i.OutputRegister(), i.InputRegister32(0));
      break;
    case kArm64Sbfx:
      __ Sbfx(i.OutputRegister(), i.InputRegister(0), i.InputInt6(1),
              i.InputInt6(2));
      break;
    case kArm64Sbfx32:
      __ Sbfx(i.OutputRegister32(), i.InputRegister32(0), i.InputInt5(1),
              i.InputInt5(2));
      break;
    case kArm64Ubfx:
      __ Ubfx(i.OutputRegister(), i.InputRegister(0), i.InputInt6(1),
              i.InputInt32(2));
      break;
    case kArm64Ubfx32:
      __ Ubfx(i.OutputRegister32(), i.InputRegister32(0), i.InputInt5(1),
              i.InputInt32(2));
      break;
    case kArm64Ubfiz32:
      __ Ubfiz(i.OutputRegister32(), i.InputRegister32(0), i.InputInt5(1),
               i.InputInt5(2));
      break;
    case kArm64Sbfiz:
      __ Sbfiz(i.OutputRegister(), i.InputRegister(0), i.InputInt6(1),
               i.InputInt6(2));
      break;
    case kArm64Bfi:
      __ Bfi(i.OutputRegister(), i.InputRegister(1), i.InputInt6(2),
             i.InputInt6(3));
      break;
    case kArm64TestAndBranch32:
    case kArm64TestAndBranch:
      // Pseudo instructions turned into tbz/tbnz in AssembleArchBranch.
      break;
    case kArm64CompareAndBranch32:
    case kArm64CompareAndBranch:
      // Pseudo instruction handled in AssembleArchBranch.
      break;
    case kArm64Claim: {
      int count = i.InputInt32(0);
      DCHECK_EQ(count % 2, 0);
      __ AssertSpAligned();
      if (count > 0) {
        __ Claim(count);
        frame_access_state()->IncreaseSPDelta(count);
      }
      break;
    }
    case kArm64Poke: {
      Operand operand(i.InputInt32(1) * kSystemPointerSize);
      if (instr->InputAt(0)->IsSimd128Register()) {
        __ Poke(i.InputSimd128Register(0), operand);
      } else if (instr->InputAt(0)->IsFPRegister()) {
        __ Poke(i.InputFloat64Register(0), operand);
      } else {
        __ Poke(i.InputOrZeroRegister64(0), operand);
      }
      break;
    }
    case kArm64PokePair: {
      int slot = i.InputInt32(2) - 1;
      if (instr->InputAt(0)->IsFPRegister()) {
        __ PokePair(i.InputFloat64Register(1), i.InputFloat64Register(0),
                    slot * kSystemPointerSize);
      } else {
        __ PokePair(i.InputRegister(1), i.InputRegister(0),
                    slot * kSystemPointerSize);
      }
      break;
    }
    case kArm64Peek: {
      int reverse_slot = i.InputInt32(0);
      int offset =
          FrameSlotToFPOffset(frame()->GetTotalFrameSlotCount() - reverse_slot);
      if (instr->OutputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->OutputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ Ldr(i.OutputDoubleRegister(), MemOperand(fp, offset));
        } else if (op->representation() == MachineRepresentation::kFloat32) {
          __ Ldr(i.OutputFloatRegister(), MemOperand(fp, offset));
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
          __ Ldr(i.OutputSimd128Register(), MemOperand(fp, offset));
        }
      } else {
        __ Ldr(i.OutputRegister(), MemOperand(fp, offset));
      }
      break;
    }
    case kArm64Clz:
      __ Clz(i.OutputRegister64(), i.InputRegister64(0));
      break;
    case kArm64Clz32:
      __ Clz(i.OutputRegister32(), i.InputRegister32(0));
      break;
    case kArm64Rbit:
      __ Rbit(i.OutputRegister64(), i.InputRegister64(0));
      break;
    case kArm64Rbit32:
      __ Rbit(i.OutputRegister32(), i.InputRegister32(0));
      break;
    case kArm64Rev:
      __ Rev(i.OutputRegister64(), i.InputRegister64(0));
      break;
    case kArm64Rev32:
      __ Rev(i.OutputRegister32(), i.InputRegister32(0));
      break;
    case kArm64Cmp:
      __ Cmp(i.InputOrZeroRegister64(0), i.InputOperand2_64(1));
      break;
    case kArm64Cmp32:
      __ Cmp(i.InputOrZeroRegister32(0), i.InputOperand2_32(1));
      break;
    case kArm64Cmn:
      __ Cmn(i.InputOrZeroRegister64(0), i.InputOperand2_64(1));
      break;
    case kArm64Cmn32:
      __ Cmn(i.InputOrZeroRegister32(0), i.InputOperand2_32(1));
      break;
    case kArm64Cnt32: {
      __ PopcntHelper(i.OutputRegister32(), i.InputRegister32(0));
      break;
    }
    case kArm64Cnt64: {
      __ PopcntHelper(i.OutputRegister64(), i.InputRegister64(0));
      break;
    }
    case kArm64Cnt: {
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      __ Cnt(i.OutputSimd128Register().Format(f),
             i.InputSimd128Register(0).Format(f));
      break;
    }
    case kArm64Tst:
      __ Tst(i.InputOrZeroRegister64(0), i.InputOperand2_64(1));
      break;
    case kArm64Tst32:
      __ Tst(i.InputOrZeroRegister32(0), i.InputOperand2_32(1));
      break;
    case kArm64Float32Cmp:
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Fcmp(i.InputFloat32Register(0), i.InputFloat32Register(1));
      } else {
        DCHECK(instr->InputAt(1)->IsImmediate());
        // 0.0 is the only immediate supported by fcmp instructions.
        DCHECK_EQ(0.0f, i.InputFloat32(1));
        __ Fcmp(i.InputFloat32Register(0), i.InputFloat32(1));
      }
      break;
    case kArm64Float32Add:
      __ Fadd(i.OutputFloat32Register(), i.InputFloat32Register(0),
              i.InputFloat32Register(1));
      break;
    case kArm64Float32Sub:
      __ Fsub(i.OutputFloat32Register(), i.InputFloat32Register(0),
              i.InputFloat32Register(1));
      break;
    case kArm64Float32Mul:
      __ Fmul(i.OutputFloat32Register(), i.InputFloat32Register(0),
              i.InputFloat32Register(1));
      break;
    case kArm64Float32Div:
      __ Fdiv(i.OutputFloat32Register(), i.InputFloat32Register(0),
              i.InputFloat32Register(1));
      break;
    case kArm64Float32Abs:
      __ Fabs(i.OutputFloat32Register(), i.InputFloat32Register(0));
      break;
    case kArm64Float32Abd:
      __ Fabd(i.OutputFloat32Register(), i.InputFloat32Register(0),
              i.InputFloat32Register(1));
      break;
    case kArm64Float32Neg:
      __ Fneg(i.OutputFloat32Register(), i.InputFloat32Register(0));
      break;
    case kArm64Float32Sqrt:
      __ Fsqrt(i.OutputFloat32Register(), i.InputFloat32Register(0));
      break;
    case kArm64Float32Fnmul: {
      __ Fnmul(i.OutputFloat32Register(), i.InputFloat32Register(0),
               i.InputFloat32Register(1));
      break;
    }
    case kArm64Float64Cmp:
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Fcmp(i.InputDoubleRegister(0), i.InputDoubleRegister(1));
      } else {
        DCHECK(instr->InputAt(1)->IsImmediate());
        // 0.0 is the only immediate supported by fcmp instructions.
        DCHECK_EQ(0.0, i.InputDouble(1));
        __ Fcmp(i.InputDoubleRegister(0), i.InputDouble(1));
      }
      break;
    case kArm64Float64Add:
      __ Fadd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      break;
    case kArm64Float64Sub:
      __ Fsub(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      break;
    case kArm64Float64Mul:
      __ Fmul(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      break;
    case kArm64Float64Div:
      __ Fdiv(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      break;
    case kArm64Float64Mod: {
      // TODO(turbofan): implement directly.
      FrameScope scope(masm(), StackFrame::MANUAL);
      DCHECK_EQ(d0, i.InputDoubleRegister(0));
      DCHECK_EQ(d1, i.InputDoubleRegister(1));
      DCHECK_EQ(d0, i.OutputDoubleRegister());
      // TODO(turbofan): make sure this saves all relevant registers.
      __ CallCFunction(ExternalReference::mod_two_doubles_operation(), 0, 2);
      break;
    }
    case kArm64Float32Max: {
      __ Fmax(i.OutputFloat32Register(), i.InputFloat32Register(0),
              i.InputFloat32Register(1));
      break;
    }
    case kArm64Float64Max: {
      __ Fmax(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      break;
    }
    case kArm64Float32Min: {
      __ Fmin(i.OutputFloat32Register(), i.InputFloat32Register(0),
              i.InputFloat32Register(1));
      break;
    }
    case kArm64Float64Min: {
      __ Fmin(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      break;
    }
    case kArm64Float64Abs:
      __ Fabs(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kArm64Float64Abd:
      __ Fabd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      break;
    case kArm64Float64Neg:
      __ Fneg(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kArm64Float64Sqrt:
      __ Fsqrt(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kArm64Float64Fnmul:
      __ Fnmul(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kArm64Float32ToFloat64:
      __ Fcvt(i.OutputDoubleRegister(), i.InputDoubleRegister(0).S());
      break;
    case kArm64Float64ToFloat32:
      __ Fcvt(i.OutputDoubleRegister().S(), i.InputDoubleRegister(0));
      break;
    case kArm64Float32ToInt32: {
      __ Fcvtzs(i.OutputRegister32(), i.InputFloat32Register(0));
      bool set_overflow_to_min_i32 = MiscField::decode(instr->opcode());
      if (set_overflow_to_min_i32) {
        // Avoid INT32_MAX as an overflow indicator and use INT32_MIN instead,
        // because INT32_MIN allows easier out-of-bounds detection.
        __ Cmn(i.OutputRegister32(), 1);
        __ Csinc(i.OutputRegister32(), i.OutputRegister32(),
                 i.OutputRegister32(), vc);
      }
      break;
    }
    case kArm64Float64ToInt32:
      __ Fcvtzs(i.OutputRegister32(), i.InputDoubleRegister(0));
      if (i.OutputCount() > 1) {
        // Check for inputs below INT32_MIN and NaN.
        __ Fcmp(i.InputDoubleRegister(0), static_cast<double>(INT32_MIN));
        __ Cset(i.OutputRegister(1).W(), ge);
        __ Fcmp(i.InputDoubleRegister(0), static_cast<double>(INT32_MAX) + 1);
        __ CmovX(i.OutputRegister(1), xzr, ge);
      }
      break;
    case kArm64Float32ToUint32: {
      __ Fcvtzu(i.OutputRegister32(), i.InputFloat32Register(0));
      bool set_overflow_to_min_u32 = MiscField::decode(instr->opcode());
      if (set_overflow_to_min_u32) {
        // Avoid UINT32_MAX as an overflow indicator and use 0 instead,
        // because 0 allows easier out-of-bounds detection.
        __ Cmn(i.OutputRegister32(), 1);
        __ Adc(i.OutputRegister32(), i.OutputRegister32(), Operand(0));
      }
      break;
    }
    case kArm64Float64ToUint32:
      __ Fcvtzu(i.OutputRegister32(), i.InputDoubleRegister(0));
      if (i.OutputCount() > 1) {
        __ Fcmp(i.InputDoubleRegister(0), -1.0);
        __ Cset(i.OutputRegister(1).W(), gt);
        __ Fcmp(i.InputDoubleRegister(0), static_cast<double>(UINT32_MAX) + 1);
        __ CmovX(i.OutputRegister(1), xzr, ge);
      }
      break;
    case kArm64Float32ToInt64:
      __ Fcvtzs(i.OutputRegister64(), i.InputFloat32Register(0));
      if (i.OutputCount() > 1) {
        // Check for inputs below INT64_MIN and NaN.
        __ Fcmp(i.InputFloat32Register(0), static_cast<float>(INT64_MIN));
        // Check overflow.
        // -1 value is used to indicate a possible overflow which will occur
        // when subtracting (-1) from the provided INT64_MAX operand.
        // OutputRegister(1) is set to 0 if the input was out of range or NaN.
        __ Ccmp(i.OutputRegister(0), -1, VFlag, ge);
        __ Cset(i.OutputRegister(1), vc);
      }
      break;
    case kArm64Float64ToInt64: {
      __ Fcvtzs(i.OutputRegister(0), i.InputDoubleRegister(0));
      bool set_overflow_to_min_i64 = MiscField::decode(instr->opcode());
      DCHECK_IMPLIES(set_overflow_to_min_i64, i.OutputCount() == 1);
      if (set_overflow_to_min_i64) {
        // Avoid INT64_MAX as an overflow indicator and use INT64_MIN instead,
        // because INT64_MIN allows easier out-of-bounds detection.
        __ Cmn(i.OutputRegister64(), 1);
        __ Csinc(i.OutputRegister64(), i.OutputRegister64(),
                 i.OutputRegister64(), vc);
      } else if (i.OutputCount() > 1) {
        // See kArm64Float32ToInt64 for a detailed description.
        __ Fcmp(i.InputDoubleRegister(0), static_cast<double>(INT64_MIN));
        __ Ccmp(i.OutputRegister(0), -1, VFlag, ge);
        __ Cset(i.OutputRegister(1), vc);
      }
      break;
    }
    case kArm64Float32ToUint64:
      __ Fcvtzu(i.OutputRegister64(), i.InputFloat32Register(0));
      if (i.OutputCount() > 1) {
        // See kArm64Float32ToInt64 for a detailed description.
        __ Fcmp(i.InputFloat32Register(0), -1.0);
        __ Ccmp(i.OutputRegister(0), -1, ZFlag, gt);
        __ Cset(i.OutputRegister(1), ne);
      }
      break;
    case kArm64Float64ToUint64:
      __ Fcvtzu(i.OutputRegister64(), i.InputDoubleRegister(0));
      if (i.OutputCount() > 1) {
        // See kArm64Float32ToInt64 for a detailed description.
        __ Fcmp(i.InputDoubleRegister(0), -1.0);
        __ Ccmp(i.OutputRegister(0), -1, ZFlag, gt);
        __ Cset(i.OutputRegister(1), ne);
      }
      break;
    case kArm64Int32ToFloat32:
      __ Scvtf(i.OutputFloat32Register(), i.InputRegister32(0));
      break;
    case kArm64Int32ToFloat64:
      __ Scvtf(i.OutputDoubleRegister(), i.InputRegister32(0));
      break;
    case kArm64Int64ToFloat32:
      __ Scvtf(i.OutputDoubleRegister().S(), i.InputRegister64(0));
      break;
    case kArm64Int64ToFloat64:
      __ Scvtf(i.OutputDoubleRegister(), i.InputRegister64(0));
      break;
    case kArm64Uint32ToFloat32:
      __ Ucvtf(i.OutputFloat32Register(), i.InputRegister32(0));
      break;
    case kArm64Uint32ToFloat64:
      __ Ucvtf(i.OutputDoubleRegister(), i.InputRegister32(0));
      break;
    case kArm64Uint64ToFloat32:
      __ Ucvtf(i.OutputDoubleRegister().S(), i.InputRegister64(0));
      break;
    case kArm64Uint64ToFloat64:
      __ Ucvtf(i.OutputDoubleRegister(), i.InputRegister64(0));
      break;
    case kArm64Float64ExtractLowWord32:
      __ Fmov(i.OutputRegister32(), i.InputFloat32Register(0));
      break;
    case kArm64Float64ExtractHighWord32:
      __ Umov(i.OutputRegister32(), i.InputFloat64Register(0).V2S(), 1);
      break;
    case kArm64Float64InsertLowWord32:
      DCHECK_EQ(i.OutputFloat64Register(), i.InputFloat64Register(0));
      __ Ins(i.OutputFloat64Register().V2S(), 0, i.InputRegister32(1));
      break;
    case kArm64Float64InsertHighWord32:
      DCHECK_EQ(i.OutputFloat64Register(), i.InputFloat64Register(0));
      __ Ins(i.OutputFloat64Register().V2S(), 1, i.InputRegister32(1));
      break;
    case kArm64Float64MoveU64:
      __ Fmov(i.OutputFloat64Register(), i.InputRegister(0));
      break;
    case kArm64Float64SilenceNaN:
      __ CanonicalizeNaN(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kArm64U64MoveFloat64:
      __ Fmov(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kArm64Ldrb:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldrb(i.OutputRegister(), i.MemoryOperand());
      break;
    case kArm64Ldrsb:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldrsb(i.OutputRegister(), i.MemoryOperand());
      break;
    case kArm64LdrsbW:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldrsb(i.OutputRegister32(), i.MemoryOperand());
      break;
    case kArm64Strb:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Strb(i.InputOrZeroRegister64(0), i.MemoryOperand(1));
      break;
    case kArm64Ldrh:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldrh(i.OutputRegister(), i.MemoryOperand());
      break;
    case kArm64Ldrsh:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldrsh(i.OutputRegister(), i.MemoryOperand());
      break;
    case kArm64LdrshW:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldrsh(i.OutputRegister32(), i.MemoryOperand());
      break;
    case kArm64Strh:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Strh(i.InputOrZeroRegister64(0), i.MemoryOperand(1));
      break;
    case kArm64Ldrsw:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldrsw(i.OutputRegister(), i.MemoryOperand());
      break;
    case kArm64LdrW:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputRegister32(), i.MemoryOperand());
      break;
    case kArm64StrW:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Str(i.InputOrZeroRegister32(0), i.MemoryOperand(1));
      break;
    case kArm64StrWPair:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Stp(i.InputOrZeroRegister32(0), i.InputOrZeroRegister32(1),
             i.MemoryOperand(2));
      break;
    case kArm64Ldr:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputRegister(), i.MemoryOperand());
      break;
    case kArm64LdrDecompressTaggedSigned:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ DecompressTaggedSigned(i.OutputRegister(), i.MemoryOperand());
      break;
    case kArm64LdrDecompressTagged:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ DecompressTagged(i.OutputRegister(), i.MemoryOperand());
      break;
    case kArm64LdrDecompressProtected:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ DecompressProtected(i.OutputRegister(), i.MemoryOperand());
      break;
    case kArm64LdarDecompressTaggedSigned:
      __ AtomicDecompressTaggedSigned(i.OutputRegister(), i.InputRegister(0),
                                      i.InputRegister(1), i.TempRegister(0));
      break;
    case kArm64LdarDecompressTagged:
      __ AtomicDecompressTagged(i.OutputRegister(), i.InputRegister(0),
                                i.InputRegister(1), i.TempRegister(0));
      break;
    case kArm64LdrDecodeSandboxedPointer:
      __ LoadSandboxedPointerField(i.OutputRegister(), i.MemoryOperand());
      break;
    case kArm64Str:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Str(i.InputOrZeroRegister64(0), i.MemoryOperand(1));
      break;
    case kArm64StrPair:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Stp(i.InputOrZeroRegister64(0), i.InputOrZeroRegister64(1),
             i.MemoryOperand(2));
      break;
    case kArm64StrCompressTagged:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ StoreTaggedField(i.InputOrZeroRegister64(0), i.MemoryOperand(1));
      break;
    case kArm64StlrCompressTagged:
      // To be consistent with other STLR instructions, the value is stored at
      // the 3rd input register instead of the 1st.
      __ AtomicStoreTaggedField(i.InputRegister(2), i.InputRegister(0),
                                i.InputRegister(1), i.TempRegister(0));
      break;
    case kArm64StrIndirectPointer:
      __ StoreIndirectPointerField(i.InputOrZeroRegister64(0),
                                   i.MemoryOperand(1));
      break;
    case kArm64StrEncodeSandboxedPointer:
      __ StoreSandboxedPointerField(i.InputOrZeroRegister64(0),
                                    i.MemoryOperand(1));
      break;
    case kArm64LdrH: {
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputDoubleRegister().H(), i.MemoryOperand());
      __ Fcvt(i.OutputDoubleRegister().S(), i.OutputDoubleRegister().H());
      break;
    }
    case kArm64StrH:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Fcvt(i.InputFloat32OrZeroRegister(0).H(),
              i.InputFloat32OrZeroRegister(0).S());
      __ Str(i.InputFloat32OrZeroRegister(0).H(), i.MemoryOperand(1));
      break;
    case kArm64LdrS:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputDoubleRegister().S(), i.MemoryOperand());
      break;
    case kArm64StrS:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Str(i.InputFloat32OrZeroRegister(0), i.MemoryOperand(1));
      break;
    case kArm64LdrD:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputDoubleRegister(), i.MemoryOperand());
      break;
    case kArm64StrD:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Str(i.InputFloat64OrZeroRegister(0), i.MemoryOperand(1));
      break;
    case kArm64LdrQ:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputSimd128Register(), i.MemoryOperand());
      break;
    case kArm64StrQ:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Str(i.InputSimd128Register(0), i.MemoryOperand(1));
      break;
    case kArm64DmbIsh:
      __ Dmb(InnerShareable, BarrierAll);
      break;
    case kArm64DsbIsb:
      __ Dsb(FullSystem, BarrierAll);
      __ Isb();
      break;
    case kAtomicLoadInt8:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ldarb, Register32);
      __ Sxtb(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicLoadUint8:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ldarb, Register32);
      break;
    case kAtomicLoadInt16:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ldarh, Register32);
      __ Sxth(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicLoadUint16:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ldarh, Register32);
      break;
    case kAtomicLoadWord32:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ldar, Register32);
      break;
    case kArm64Word64AtomicLoadUint64:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ldar, Register);
      break;
    case kAtomicStoreWord8:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Stlrb, Register32);
      break;
    case kAtomicStoreWord16:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Stlrh, Register32);
      break;
    case kAtomicStoreWord32:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Stlr, Register32);
      break;
    case kArm64Word64AtomicStoreWord64:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Stlr, Register);
      break;
    case kAtomicExchangeInt8:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(b, Register32);
      __ Sxtb(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicExchangeUint8:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(b, Register32);
      break;
    case kAtomicExchangeInt16:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(h, Register32);
      __ Sxth(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicExchangeUint16:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(h, Register32);
      break;
    case kAtomicExchangeWord32:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(, Register32);
      break;
    case kArm64Word64AtomicExchangeUint64:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(, Register);
      break;
    case kAtomicCompareExchangeInt8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(b, UXTB, Register32);
      __ Sxtb(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicCompareExchangeUint8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(b, UXTB, Register32);
      break;
    case kAtomicCompareExchangeInt16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(h, UXTH, Register32);
      __ Sxth(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicCompareExchangeUint16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(h, UXTH, Register32);
      break;
    case kAtomicCompareExchangeWord32:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(, UXTW, Register32);
      break;
    case kArm64Word64AtomicCompareExchangeUint64:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(, UXTX, Register);
      break;
    case kAtomicSubInt8:
      ASSEMBLE_ATOMIC_SUB(b, Register32);
      __ Sxtb(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicSubUint8:
      ASSEMBLE_ATOMIC_SUB(b, Register32);
      break;
    case kAtomicSubInt16:
      ASSEMBLE_ATOMIC_SUB(h, Register32);
      __ Sxth(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicSubUint16:
      ASSEMBLE_ATOMIC_SUB(h, Register32);
      break;
    case kAtomicSubWord32:
      ASSEMBLE_ATOMIC_SUB(, Register32);
      break;
    case kArm64Word64AtomicSubUint64:
      ASSEMBLE_ATOMIC_SUB(, Register);
      break;
    case kAtomicAndInt8:
      ASSEMBLE_ATOMIC_AND(b, Register32);
      __ Sxtb(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicAndUint8:
      ASSEMBLE_ATOMIC_AND(b, Register32);
      break;
    case kAtomicAndInt16:
      ASSEMBLE_ATOMIC_AND(h, Register32);
      __ Sxth(i.OutputRegister(0), i.OutputRegister(0));
      break;
    case kAtomicAndUint16:
      ASSEMBLE_ATOMIC_AND(h, Register32);
      break;
    case kAtomicAndWord32:
      ASSEMBLE_ATOMIC_AND(, Register32);
      break;
    case kArm64Word64AtomicAndUint64:
      ASSEMBLE_ATOMIC_AND(, Register);
      break;
#define ATOMIC_BINOP_CASE(op, inst, lse_instr)             \
  case kAtomic##op##Int8:                                  \
    ASSEMBLE_ATOMIC_BINOP(b, inst, lse_instr, Register32); \
    __ Sxtb(i.OutputRegister(0), i.OutputRegister(0));     \
    break;                                                 \
  case kAtomic##op##Uint8:                                 \
    ASSEMBLE_ATOMIC_BINOP(b, inst, lse_instr, Register32); \
    break;                                                 \
  case kAtomic##op##Int16:                                 \
    ASSEMBLE_ATOMIC_BINOP(h, inst, lse_instr, Register32); \
    __ Sxth(i.OutputRegister(0), i.OutputRegister(0));     \
    break;                                                 \
  case kAtomic##op##Uint16:                                \
    ASSEMBLE_ATOMIC_BINOP(h, inst, lse_instr, Register32); \
    break;                                                 \
  case kAtomic##op##Word32:                                \
    ASSEMBLE_ATOMIC_BINOP(, inst, lse_instr, Register32);  \
    break;                                                 \
  case kArm64Word64Atomic##op##Uint64:                     \
    ASSEMBLE_ATOMIC_BINOP(, inst, lse_instr, Register);    \
    break;
      ATOMIC_BINOP_CASE(Add, Add, Ldaddal)
      ATOMIC_BINOP_CASE(Or, Orr, Ldsetal)
      ATOMIC_BINOP_CASE(Xor, Eor, Ldeoral)
#undef ATOMIC_BINOP_CASE
#undef ASSEMBLE_SHIFT
#undef ASSEMBLE_ATOMIC_LOAD_INTEGER
#undef ASSEMBLE_ATOMIC_STORE_INTEGER
#undef ASSEMBLE_ATOMIC_EXCHANGE_INTEGER
#undef ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER
#undef ASSEMBLE_ATOMIC_BINOP
#undef ASSEMBLE_IEEE754_BINOP
#undef ASSEMBLE_IEEE754_UNOP

#if V8_ENABLE_WEBASSEMBLY
#define SIMD_UNOP_CASE(Op, Instr, FORMAT)            \
  case Op:                                           \
    __ Instr(i.OutputSimd128Register().V##FORMAT(),  \
             i.InputSimd128Register(0).V##FORMAT()); \
    break;
#define SIMD_UNOP_LANE_SIZE_CASE(Op, Instr)                            \
  case Op: {                                                           \
    VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode)); \
    __ Instr(i.OutputSimd128Register().Format(f),                      \
             i.InputSimd128Register(0).Format(f));                     \
    break;                                                             \
  }
#define SIMD_BINOP_CASE(Op, Instr, FORMAT)           \
  case Op:                                           \
    __ Instr(i.OutputSimd128Register().V##FORMAT(),  \
             i.InputSimd128Register(0).V##FORMAT(),  \
             i.InputSimd128Register(1).V##FORMAT()); \
    break;
#define SIMD_BINOP_LANE_SIZE_CASE(Op, Instr)                           \
  case Op: {                                                           \
    VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode)); \
    __ Instr(i.OutputSimd128Register().Format(f),                      \
             i.InputSimd128Register(0).Format(f),                      \
             i.InputSimd128Register(1).Format(f));                     \
    break;                                                             \
  }
#define SIMD_FCM_L_CASE(Op, ImmOp, RegOp)                              \
  case Op: {                                                           \
    VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode)); \
    if (instr->InputCount() == 1) {                                    \
      __ Fcm##ImmOp(i.OutputSimd128Register().Format(f),               \
                    i.InputSimd128Register(0).Format(f), +0.0);        \
    } else {                                                           \
      __ Fcm##RegOp(i.OutputSimd128Register().Format(f),               \
                    i.InputSimd128Register(1).Format(f),               \
                    i.InputSimd128Register(0).Format(f));              \
    }                                                                  \
    break;                                                             \
  }
#define SIMD_FCM_G_CASE(Op, ImmOp)                                     \
  case Op: {                                                           \
    VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode)); \
    /* Currently Gt/Ge instructions are only used with zero */         \
    DCHECK_EQ(instr->InputCount(), 1);                                 \
    __ Fcm##ImmOp(i.OutputSimd128Register().Format(f),                 \
                  i.InputSimd128Register(0).Format(f), +0.0);          \
    break;                                                             \
  }
#define SIMD_CM_L_CASE(Op, ImmOp)                                      \
  case Op: {                                                           \
    VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode)); \
    DCHECK_EQ(instr->InputCount(), 1);                                 \
    __ Cm##ImmOp(i.OutputSimd128Register().Format(f),                  \
                 i.InputSimd128Register(0).Format(f), 0);              \
    break;                                                             \
  }
#define SIMD_CM_G_CASE(Op, CmOp)                                       \
  case Op: {                                                           \
    VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode)); \
    if (instr->InputCount() == 1) {                                    \
      __ Cm##CmOp(i.OutputSimd128Register().Format(f),                 \
                  i.InputSimd128Register(0).Format(f), 0);             \
    } else {                                                           \
      __ Cm##CmOp(i.OutputSimd128Register().Format(f),                 \
                  i.InputSimd128Register(0).Format(f),                 \
                  i.InputSimd128Register(1).Format(f));                \
    }                                                                  \
    break;                                                             \
  }
#define SIMD_DESTRUCTIVE_BINOP_CASE(Op, Instr, FORMAT)     \
  case Op: {                                               \
    VRegister dst = i.OutputSimd128Register().V##FORMAT(); \
    DCHECK_EQ(dst, i.InputSimd128Register(0).V##FORMAT()); \
    __ Instr(dst, i.InputSimd128Register(1).V##FORMAT(),   \
             i.InputSimd128Register(2).V##FORMAT());       \
    break;                                                 \
  }
#define SIMD_DESTRUCTIVE_BINOP_LANE_SIZE_CASE(Op, Instr)               \
  case Op: {                                                           \
    VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode)); \
    VRegister dst = i.OutputSimd128Register().Format(f);               \
    DCHECK_EQ(dst, i.InputSimd128Register(0).Format(f));               \
    __ Instr(dst, i.InputSimd128Register(1).Format(f),                 \
             i.InputSimd128Register(2).Format(f));                     \
    break;                                                             \
  }
#define SIMD_DESTRUCTIVE_RELAXED_FUSED_CASE(Op, Instr, FORMAT) \
  case Op: {                                                   \
    VRegister dst = i.OutputSimd128Register().V##FORMAT();     \
    DCHECK_EQ(dst, i.InputSimd128Register(2).V##FORMAT());     \
    __ Instr(dst, i.InputSimd128Register(0).V##FORMAT(),       \
             i.InputSimd128Register(1).V##FORMAT());           \
    break;                                                     \
  }
      SIMD_BINOP_LANE_SIZE_CASE(kArm64FMin, Fmin);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64FMax, Fmax);
      SIMD_UNOP_LANE_SIZE_CASE(kArm64FAbs, Fabs);
      SIMD_UNOP_LANE_SIZE_CASE(kArm64FSqrt, Fsqrt);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64FAdd, Fadd);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64FSub, Fsub);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64FMul, Fmul);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64FDiv, Fdiv);
      SIMD_UNOP_LANE_SIZE_CASE(kArm64FNeg, Fneg);
      SIMD_UNOP_LANE_SIZE_CASE(kArm64IAbs, Abs);
      SIMD_UNOP_LANE_SIZE_CASE(kArm64INeg, Neg);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64RoundingAverageU, Urhadd);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64IMinS, Smin);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64IMaxS, Smax);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64IMinU, Umin);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64IMaxU, Umax);
      SIMD_DESTRUCTIVE_BINOP_LANE_SIZE_CASE(kArm64Mla, Mla);
      SIMD_DESTRUCTIVE_BINOP_LANE_SIZE_CASE(kArm64Mls, Mls);
    case kArm64Sxtl: {
      VectorFormat wide = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat narrow = VectorFormatHalfWidth(wide);
      __ Sxtl(i.OutputSimd128Register().Format(wide),
              i.InputSimd128Register(0).Format(narrow));
      break;
    }
    case kArm64Sxtl2: {
      VectorFormat wide = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat narrow = VectorFormatHalfWidthDoubleLanes(wide);
      __ Sxtl2(i.OutputSimd128Register().Format(wide),
               i.InputSimd128Register(0).Format(narrow));
      break;
    }
    case kArm64Uxtl: {
      VectorFormat wide = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat narrow = VectorFormatHalfWidth(wide);
      __ Uxtl(i.OutputSimd128Register().Format(wide),
              i.InputSimd128Register(0).Format(narrow));
      break;
    }
    case kArm64Uxtl2: {
      VectorFormat wide = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VectorFormat narrow = VectorFormatHalfWidthDoubleLanes(wide);
      __ Uxtl2(i.OutputSimd128Register().Format(wide),
               i.InputSimd128Register(0).Format(narrow));
      break;
    }
    case kArm64F64x2ConvertLowI32x4S: {
      VRegister dst = i.OutputSimd128Register().V2D();
      __ Sxtl(dst, i.InputSimd128Register(0).V2S());
      __ Scvtf(dst, dst);
      break;
    }
    case kArm64F64x2ConvertLowI32x4U: {
      VRegister dst = i.OutputSimd128Register().V2D();
      __ Uxtl(dst, i.InputSimd128Register(0).V2S());
      __ Ucvtf(dst, dst);
      break;
    }
    case kArm64I32x4TruncSatF64x2SZero: {
      VRegister dst = i.OutputSimd128Register();
      __ Fcvtzs(dst.V2D(), i.InputSimd128Register(0).V2D());
      __ Sqxtn(dst.V2S(), dst.V2D());
      break;
    }
    case kArm64I32x4TruncSatF64x2UZero: {
      VRegister dst = i.OutputSimd128Register();
      __ Fcvtzu(dst.V2D(), i.InputSimd128Register(0).V2D());
      __ Uqxtn(dst.V2S(), dst.V2D());
      break;
    }
    case kArm64F32x4DemoteF64x2Zero: {
      __ Fcvtn(i.OutputSimd128Register().V2S(),
               i.InputSimd128Register(0).V2D());
      break;
    }
    case kArm64F64x2PromoteLowF32x4: {
      __ Fcvtl(i.OutputSimd128Register().V2D(),
               i.InputSimd128Register(0).V2S());
      break;
    }
      SIMD_UNOP_CASE(kArm64F16x8SConvertI16x8, Scvtf, 8H);
      SIMD_UNOP_CASE(kArm64F16x8UConvertI16x8, Ucvtf, 8H);
      SIMD_UNOP_CASE(kArm64I16x8UConvertF16x8, Fcvtzu, 8H);
      SIMD_UNOP_CASE(kArm64I16x8SConvertF16x8, Fcvtzs, 8H);
    case kArm64F16x8DemoteF32x4Zero: {
      __ Fcvtn(i.OutputSimd128Register().V4H(),
               i.InputSimd128Register(0).V4S());
      break;
    }
    case kArm64F16x8DemoteF64x2Zero: {
      // There is no vector f64 -> f16 conversion instruction,
      // so convert them by component using scalar version.
      // Convert high double to a temp reg first, because dst and src
      // can overlap.
      __ Mov(fp_scratch.D(), i.InputSimd128Register(0).V2D(), 1);
      __ Fcvt(fp_scratch.H(), fp_scratch.D());

      __ Fcvt(i.OutputSimd128Register().H(), i.InputSimd128Register(0).D());
      __ Mov(i.OutputSimd128Register().V8H(), 1, fp_scratch.V8H(), 0);
      break;
    }
    case kArm64F32x4PromoteLowF16x8: {
      __ Fcvtl(i.OutputSimd128Register().V4S(),
               i.InputSimd128Register(0).V4H());
      break;
    }
    case kArm64FExtractLane: {
      VectorFormat dst_f =
          ScalarFormatFromLaneSize(LaneSizeField::decode(opcode));
      VectorFormat src_f = VectorFormatFillQ(dst_f);
      __ Mov(i.OutputSimd128Register().Format(dst_f),
             i.InputSimd128Register(0).Format(src_f), i.InputInt8(1));
      if (dst_f == kFormatH) {
        __ Fcvt(i.OutputSimd128Register().S(), i.OutputSimd128Register().H());
      }
      break;
    }
    case kArm64FReplaceLane: {
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VRegister dst = i.OutputSimd128Register().Format(f),
                src1 = i.InputSimd128Register(0).Format(f);
      if (dst != src1) {
        __ Mov(dst, src1);
      }
      if (f == kFormat8H) {
        UseScratchRegisterScope scope(masm());
        VRegister tmp = scope.AcquireV(kFormat8H);
        __ Fcvt(tmp.H(), i.InputSimd128Register(2).S());
        __ Mov(dst, i.InputInt8(1), tmp.Format(f), 0);
      } else {
        __ Mov(dst, i.InputInt8(1), i.InputSimd128Register(2).Format(f), 0);
      }
      break;
    }
      SIMD_FCM_L_CASE(kArm64FEq, eq, eq);
    case kArm64FNe: {
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VRegister dst = i.OutputSimd128Register().Format(f);
      if (instr->InputCount() == 1) {
        __ Fcmeq(dst, i.InputSimd128Register(0).Format(f), +0.0);
      } else {
        __ Fcmeq(dst, i.InputSimd128Register(0).Format(f),
                 i.InputSimd128Register(1).Format(f));
      }
      __ Mvn(dst, dst);
      break;
    }
      SIMD_FCM_L_CASE(kArm64FLt, lt, gt);
      SIMD_FCM_L_CASE(kArm64FLe, le, ge);
      SIMD_FCM_G_CASE(kArm64FGt, gt);
      SIMD_FCM_G_CASE(kArm64FGe, ge);
      SIMD_DESTRUCTIVE_RELAXED_FUSED_CASE(kArm64F64x2Qfma, Fmla, 2D);
      SIMD_DESTRUCTIVE_RELAXED_FUSED_CASE(kArm64F64x2Qfms, Fmls, 2D);
    case kArm64F64x2Pmin: {
      VRegister dst = i.OutputSimd128Register().V2D();
      VRegister lhs = i.InputSimd128Register(0).V2D();
      VRegister rhs = i.InputSimd128Register(1).V2D();
      // f64x2.pmin(lhs, rhs)
      // = v128.bitselect(rhs, lhs, f64x2.lt(rhs,lhs))
      // = v128.bitselect(rhs, lhs, f64x2.gt(lhs,rhs))
      __ Fcmgt(dst, lhs, rhs);
      __ Bsl(dst.V16B(), rhs.V16B(), lhs.V16B());
      break;
    }
    case kArm64F64x2Pmax: {
      VRegister dst = i.OutputSimd128Register().V2D();
      VRegister lhs = i.InputSimd128Register(0).V2D();
      VRegister rhs = i.InputSimd128Register(1).V2D();
      // f64x2.pmax(lhs, rhs)
      // = v128.bitselect(rhs, lhs, f64x2.gt(rhs, lhs))
      __ Fcmgt(dst, rhs, lhs);
      __ Bsl(dst.V16B(), rhs.V16B(), lhs.V16B());
      break;
    }
      SIMD_UNOP_CASE(kArm64F32x4SConvertI32x4, Scvtf, 4S);
      SIMD_UNOP_CASE(kArm64F32x4UConvertI32x4, Ucvtf, 4S);
    case kArm64FMulElement: {
      VectorFormat s_f =
          ScalarFormatFromLaneSize(LaneSizeField::decode(opcode));
      VectorFormat v_f = VectorFormatFillQ(s_f);
      __ Fmul(i.OutputSimd128Register().Format(v_f),
              i.InputSimd128Register(0).Format(v_f),
              i.InputSimd128Register(1).Format(s_f), i.InputInt8(2));
      break;
    }
      SIMD_DESTRUCTIVE_RELAXED_FUSED_CASE(kArm64F32x4Qfma, Fmla, 4S);
      SIMD_DESTRUCTIVE_RELAXED_FUSED_CASE(kArm64F32x4Qfms, Fmls, 4S);
    case kArm64F32x4Pmin: {
      VRegister dst = i.OutputSimd128Register().V4S();
      VRegister lhs = i.InputSimd128Register(0).V4S();
      VRegister rhs = i.InputSimd128Register(1).V4S();
      // f32x4.pmin(lhs, rhs)
      // = v128.bitselect(rhs, lhs, f32x4.lt(rhs, lhs))
      // = v128.bitselect(rhs, lhs, f32x4.gt(lhs, rhs))
      __ Fcmgt(dst, lhs, rhs);
      __ Bsl(dst.V16B(), rhs.V16B(), lhs.V16B());
      break;
    }
    case kArm64F32x4Pmax: {
      VRegister dst = i.OutputSimd128Register().V4S();
      VRegister lhs = i.InputSimd128Register(0).V4S();
      VRegister rhs = i.InputSimd128Register(1).V4S();
      // f32x4.pmax(lhs, rhs)
      // = v128.bitselect(rhs, lhs, f32x4.gt(rhs, lhs))
      __ Fcmgt(dst, rhs, lhs);
      __ Bsl(dst.V16B(), rhs.V16B(), lhs.V16B());
      break;
    }
    case kArm64F16x8Pmin: {
      VRegister dst = i.OutputSimd128Register().V8H();
      VRegister lhs = i.InputSimd128Register(0).V8H();
      VRegister rhs = i.InputSimd128Register(1).V8H();
      // f16x8.pmin(lhs, rhs)
      // = v128.bitselect(rhs, lhs, f16x8.lt(rhs, lhs))
      // = v128.bitselect(rhs, lhs, f16x8.gt(lhs, rhs))
      __ Fcmgt(dst, lhs, rhs);
      __ Bsl(dst.V16B(), rhs.V16B(), lhs.V16B());
      break;
    }
    case kArm64F16x8Pmax: {
      VRegister dst = i.OutputSimd128Register().V8H();
      VRegister lhs = i.InputSimd128Register(0).V8H();
      VRegister rhs = i.InputSimd128Register(1).V8H();
      // f16x8.pmax(lhs, rhs)
      // = v128.bitselect(rhs, lhs, f16x8.gt(rhs, lhs))
      __ Fcmgt(dst, rhs, lhs);
      __ Bsl(dst.V16B(), rhs.V16B(), lhs.V16B());
      break;
    }
      SIMD_DESTRUCTIVE_RELAXED_FUSED_CASE(kArm64F16x8Qfma, Fmla, 8H);
      SIMD_DESTRUCTIVE_RELAXED_FUSED_CASE(kArm64F16x8Qfms, Fmls, 8H);
    case kArm64IExtractLane: {
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      Register dst =
          f == kFormat2D ? i.OutputRegister64() : i.OutputRegister32();
      __ Mov(dst, i.InputSimd128Register(0).Format(f), i.InputInt8(1));
      break;
    }
    case kArm64IReplaceLane: {
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VRegister dst = i.OutputSimd128Register().Format(f),
                src1 = i.InputSimd128Register(0).Format(f);
      Register src2 =
          f == kFormat2D ? i.InputRegister64(2) : i.InputRegister32(2);
      if (dst != src1) {
        __ Mov(dst, src1);
      }
      __ Mov(dst, i.InputInt8(1), src2);
      break;
    }
    case kArm64I64x2Shl: {
      ASSEMBLE_SIMD_SHIFT_LEFT(Shl, 6, V2D, Sshl, X);
      break;
    }
    case kArm64I64x2ShrS: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(Sshr, 6, V2D, Sshl, X);
      break;
    }
      SIMD_BINOP_LANE_SIZE_CASE(kArm64IAdd, Add);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64ISub, Sub);
    case kArm64I64x2Mul: {
      UseScratchRegisterScope scope(masm());
      VRegister dst = i.OutputSimd128Register();
      VRegister src1 = i.InputSimd128Register(0);
      VRegister src2 = i.InputSimd128Register(1);
      VRegister tmp1 = scope.AcquireSameSizeAs(dst);
      VRegister tmp2 = scope.AcquireSameSizeAs(dst);
      VRegister tmp3 = i.ToSimd128Register(instr->TempAt(0));

      // This 2x64-bit multiplication is performed with several 32-bit
      // multiplications.

      // 64-bit numbers x and y, can be represented as:
      //   x = a + 2^32(b)
      //   y = c + 2^32(d)

      // A 64-bit multiplication is:
      //   x * y = ac + 2^32(ad + bc) + 2^64(bd)
      // note: `2^64(bd)` can be ignored, the value is too large to fit in
      // 64-bits.

      // This sequence implements a 2x64bit multiply, where the registers
      // `src1` and `src2` are split up into 32-bit components:
      //   src1 = |d|c|b|a|
      //   src2 = |h|g|f|e|
      //
      //   src1 * src2 = |cg + 2^32(ch + dg)|ae + 2^32(af + be)|

      // Reverse the 32-bit elements in the 64-bit words.
      //   tmp2 = |g|h|e|f|
      __ Rev64(tmp2.V4S(), src2.V4S());

      // Calculate the high half components.
      //   tmp2 = |dg|ch|be|af|
      __ Mul(tmp2.V4S(), tmp2.V4S(), src1.V4S());

      // Extract the low half components of src1.
      //   tmp1 = |c|a|
      __ Xtn(tmp1.V2S(), src1.V2D());

      // Sum the respective high half components.
      //   tmp2 = |dg+ch|be+af||dg+ch|be+af|
      __ Addp(tmp2.V4S(), tmp2.V4S(), tmp2.V4S());

      // Extract the low half components of src2.
      //   tmp3 = |g|e|
      __ Xtn(tmp3.V2S(), src2.V2D());

      // Shift the high half components, into the high half.
      //   dst = |dg+ch << 32|be+af << 32|
      __ Shll(dst.V2D(), tmp2.V2S(), 32);

      // Multiply the low components together, and accumulate with the high
      // half.
      //   dst = |dst[1] + cg|dst[0] + ae|
      __ Umlal(dst.V2D(), tmp3.V2S(), tmp1.V2S());

      break;
    }
      SIMD_CM_G_CASE(kArm64IEq, eq);
    case kArm64INe: {
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      VRegister dst = i.OutputSimd128Register().Format(f);
      if (instr->InputCount() == 1) {
        __ Cmeq(dst, i.InputSimd128Register(0).Format(f), 0);
      } else {
        __ Cmeq(dst, i.InputSimd128Register(0).Format(f),
                i.InputSimd128Register(1).Format(f));
      }
      __ Mvn(dst, dst);
      break;
    }
      SIMD_CM_L_CASE(kArm64ILtS, lt);
      SIMD_CM_L_CASE(kArm64ILeS, le);
      SIMD_CM_G_CASE(kArm64IGtS, gt);
      SIMD_CM_G_CASE(kArm64IGeS, ge);
    case kArm64I64x2ShrU: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(Ushr, 6, V2D, Ushl, X);
      break;
    }
    case kArm64I64x2BitMask: {
      __ I64x2BitMask(i.OutputRegister32(), i.InputSimd128Register(0));
      break;
    }
      SIMD_UNOP_CASE(kArm64I32x4SConvertF32x4, Fcvtzs, 4S);
    case kArm64I32x4Shl: {
      ASSEMBLE_SIMD_SHIFT_LEFT(Shl, 5, V4S, Sshl, W);
      break;
    }
    case kArm64I32x4ShrS: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(Sshr, 5, V4S, Sshl, W);
      break;
    }
      SIMD_BINOP_CASE(kArm64I32x4Mul, Mul, 4S);
      SIMD_UNOP_CASE(kArm64I32x4UConvertF32x4, Fcvtzu, 4S);
    case kArm64I32x4ShrU: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(Ushr, 5, V4S, Ushl, W);
      break;
    }
      SIMD_BINOP_LANE_SIZE_CASE(kArm64IGtU, Cmhi);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64IGeU, Cmhs);
    case kArm64I32x4BitMask: {
      __ I32x4BitMask(i.OutputRegister32(), i.InputSimd128Register(0));
      break;
    }
    case kArm64I8x16Addv: {
      __ Addv(i.OutputSimd128Register().B(), i.InputSimd128Register(0).V16B());
      break;
    }
    case kArm64I16x8Addv: {
      __ Addv(i.OutputSimd128Register().H(), i.InputSimd128Register(0).V8H());
      break;
    }
    case kArm64I32x4Addv: {
      __ Addv(i.OutputSimd128Register().S(), i.InputSimd128Register(0).V4S());
      break;
    }
    case kArm64I64x2AddPair: {
      __ Addp(i.OutputSimd128Register().D(), i.InputSimd128Register(0).V2D());
      break;
    }
    case kArm64F32x4AddReducePairwise: {
      UseScratchRegisterScope scope(masm());
      VRegister tmp = scope.AcquireV(kFormat4S);
      __ Faddp(tmp.V4S(), i.InputSimd128Register(0).V4S(),
               i.InputSimd128Register(0).V4S());
      __ Faddp(i.OutputSimd128Register().S(), tmp.V2S());
      break;
    }
    case kArm64F64x2AddPair: {
      __ Faddp(i.OutputSimd128Register().D(), i.InputSimd128Register(0).V2D());
      break;
    }
    case kArm64I32x4DotI16x8S: {
      UseScratchRegisterScope scope(masm());
      VRegister lhs = i.InputSimd128Register(0);
      VRegister rhs = i.InputSimd128Register(1);
      VRegister tmp1 = scope.AcquireV(kFormat4S);
      VRegister tmp2 = scope.AcquireV(kFormat4S);
      __ Smull(tmp1, lhs.V4H(), rhs.V4H());
      __ Smull2(tmp2, lhs.V8H(), rhs.V8H());
      __ Addp(i.OutputSimd128Register().V4S(), tmp1, tmp2);
      break;
    }
    case kArm64I16x8DotI8x16S: {
      UseScratchRegisterScope scope(masm());
      VRegister lhs = i.InputSimd128Register(0);
      VRegister rhs = i.InputSimd128Register(1);
      VRegister tmp1 = scope.AcquireV(kFormat8H);
      VRegister tmp2 = scope.AcquireV(kFormat8H);
      __ Smull(tmp1, lhs.V8B(), rhs.V8B());
      __ Smull2(tmp2, lhs.V16B(), rhs.V16B());
      __ Addp(i.OutputSimd128Register().V8H(), tmp1, tmp2);
      break;
    }
    case kArm64I32x4DotI8x16AddS: {
      if (CpuFeatures::IsSupported(DOTPROD)) {
        CpuFeatureScope scope(masm(), DOTPROD);

        DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(2));
        __ Sdot(i.InputSimd128Register(2).V4S(),
                i.InputSimd128Register(0).V16B(),
                i.InputSimd128Register(1).V16B());

      } else {
        UseScratchRegisterScope scope(masm());
        VRegister lhs = i.InputSimd128Register(0);
        VRegister rhs = i.InputSimd128Register(1);
        VRegister tmp1 = scope.AcquireV(kFormat8H);
        VRegister tmp2 = scope.AcquireV(kFormat8H);
        __ Smull(tmp1, lhs.V8B(), rhs.V8B());
        __ Smull2(tmp2, lhs.V16B(), rhs.V16B());
        __ Addp(tmp1, tmp1, tmp2);
        __ Saddlp(tmp1.V4S(), tmp1);
        __ Add(i.OutputSimd128Register().V4S(), tmp1.V4S(),
               i.InputSimd128Register(2).V4S());
      }
      break;
    }
    case kArm64IExtractLaneU: {
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      __ Umov(i.OutputRegister32(), i.InputSimd128Register(0).Format(f),
              i.InputInt8(1));
      break;
    }
    case kArm64IExtractLaneS: {
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      __ Smov(i.OutputRegister32(), i.InputSimd128Register(0).Format(f),
              i.InputInt8(1));
      break;
    }
    case kArm64I16x8Shl: {
      ASSEMBLE_SIMD_SHIFT_LEFT(Shl, 4, V8H, Sshl, W);
      break;
    }
    case kArm64I16x8ShrS: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(Sshr, 4, V8H, Sshl, W);
      break;
    }
    case kArm64I16x8SConvertI32x4: {
      VRegister dst = i.OutputSimd128Register(),
                src0 = i.InputSimd128Register(0),
                src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope scope(masm());
      VRegister temp = scope.AcquireV(kFormat4S);
      if (dst == src1) {
        __ Mov(temp, src1.V4S());
        src1 = temp;
      }
      __ Sqxtn(dst.V4H(), src0.V4S());
      __ Sqxtn2(dst.V8H(), src1.V4S());
      break;
    }
      SIMD_BINOP_LANE_SIZE_CASE(kArm64IAddSatS, Sqadd);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64ISubSatS, Sqsub);
      SIMD_BINOP_CASE(kArm64I16x8Mul, Mul, 8H);
    case kArm64I16x8ShrU: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(Ushr, 4, V8H, Ushl, W);
      break;
    }
    case kArm64I16x8UConvertI32x4: {
      VRegister dst = i.OutputSimd128Register(),
                src0 = i.InputSimd128Register(0),
                src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope scope(masm());
      VRegister temp = scope.AcquireV(kFormat4S);
      if (dst == src1) {
        __ Mov(temp, src1.V4S());
        src1 = temp;
      }
      __ Sqxtun(dst.V4H(), src0.V4S());
      __ Sqxtun2(dst.V8H(), src1.V4S());
      break;
    }
      SIMD_BINOP_LANE_SIZE_CASE(kArm64IAddSatU, Uqadd);
      SIMD_BINOP_LANE_SIZE_CASE(kArm64ISubSatU, Uqsub);
      SIMD_BINOP_CASE(kArm64I16x8Q15MulRSatS, Sqrdmulh, 8H);
    case kArm64I16x8BitMask: {
      __ I16x8BitMask(i.OutputRegister32(), i.InputSimd128Register(0));
      break;
    }
    case kArm64I8x16Shl: {
      ASSEMBLE_SIMD_SHIFT_LEFT(Shl, 3, V16B, Sshl, W);
      break;
    }
    case kArm64I8x16ShrS: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(Sshr, 3, V16B, Sshl, W);
      break;
    }
    case kArm64I8x16SConvertI16x8: {
      VRegister dst = i.OutputSimd128Register(),
                src0 = i.InputSimd128Register(0),
                src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope scope(masm());
      VRegister temp = scope.AcquireV(kFormat8H);
      if (dst == src1) {
        __ Mov(temp, src1.V8H());
        src1 = temp;
      }
      __ Sqxtn(dst.V8B(), src0.V8H());
      __ Sqxtn2(dst.V16B(), src1.V8H());
      break;
    }
    case kArm64I8x16ShrU: {
      ASSEMBLE_SIMD_SHIFT_RIGHT(Ushr, 3, V16B, Ushl, W);
      break;
    }
    case kArm64I8x16UConvertI16x8: {
      VRegister dst = i.OutputSimd128Register(),
                src0 = i.InputSimd128Register(0),
                src1 = i.InputSimd128Register(1);
      UseScratchRegisterScope scope(masm());
      VRegister temp = scope.AcquireV(kFormat8H);
      if (dst == src1) {
        __ Mov(temp, src1.V8H());
        src1 = temp;
      }
      __ Sqxtun(dst.V8B(), src0.V8H());
      __ Sqxtun2(dst.V16B(), src1.V8H());
      break;
    }
    case kArm64I8x16BitMask: {
      VRegister temp = NoVReg;

      if (CpuFeatures::IsSupported(PMULL1Q)) {
        temp = i.TempSimd128Register(0);
      }

      __ I8x16BitMask(i.OutputRegister32(), i.InputSimd128Register(0), temp);
      break;
    }
    case kArm64S128Const: {
      uint64_t imm1 = make_uint64(i.InputUint32(1), i.InputUint32(0));
      uint64_t imm2 = make_uint64(i.InputUint32(3), i.InputUint32(2));
      __ Movi(i.OutputSimd128Register().V16B(), imm2, imm1);
      break;
    }
      SIMD_BINOP_CASE(kArm64S128And, And, 16B);
      SIMD_BINOP_CASE(kArm64S128Or, Orr, 16B);
      SIMD_BINOP_CASE(kArm64S128Xor, Eor, 16B);
      SIMD_UNOP_CASE(kArm64S128Not, Mvn, 16B);
    case kArm64S128Dup: {
      VRegister dst = i.OutputSimd128Register(),
                src = i.InputSimd128Register(0);
      int lanes = i.InputInt32(1);
      int index = i.InputInt32(2);
      switch (lanes) {
        case 4:
          __ Dup(dst.V4S(), src.V4S(), index);
          break;
        case 8:
          __ Dup(dst.V8H(), src.V8H(), index);
          break;
        case 16:
          __ Dup(dst.V16B(), src.V16B(), index);
          break;
        default:
          UNREACHABLE();
      }
      break;
    }
      SIMD_DESTRUCTIVE_BINOP_CASE(kArm64S128Select, Bsl, 16B);
    case kArm64S128AndNot:
      if (instr->InputAt(1)->IsImmediate()) {
        VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
        VRegister dst = i.OutputSimd128Register().Format(f);
        DCHECK_EQ(dst, i.InputSimd128Register(0).Format(f));
        __ Bic(dst, i.InputInt32(1), i.InputInt8(2));
      } else {
        __ Bic(i.OutputSimd128Register().V16B(),
               i.InputSimd128Register(0).V16B(),
               i.InputSimd128Register(1).V16B());
      }
      break;
    case kArm64Ssra: {
      int8_t laneSize = LaneSizeField::decode(opcode);
      VectorFormat f = VectorFormatFillQ(laneSize);
      int8_t mask = laneSize - 1;
      VRegister dst = i.OutputSimd128Register().Format(f);
      DCHECK_EQ(dst, i.InputSimd128Register(0).Format(f));
      __ Ssra(dst, i.InputSimd128Register(1).Format(f), i.InputInt8(2) & mask);
      break;
    }
    case kArm64Usra: {
      int8_t laneSize = LaneSizeField::decode(opcode);
      VectorFormat f = VectorFormatFillQ(laneSize);
      int8_t mask = laneSize - 1;
      VRegister dst = i.OutputSimd128Register().Format(f);
      DCHECK_EQ(dst, i.InputSimd128Register(0).Format(f));
      __ Usra(dst, i.InputSimd128Register(1).Format(f), i.InputUint8(2) & mask);
      break;
    }
    case kArm64S32x4Shuffle: {
      Simd128Register dst = i.OutputSimd128Register().V4S(),
                      src0 = i.InputSimd128Register(0).V4S(),
                      src1 = i.InputSimd128Register(1).V4S();
      // Check for in-place shuffles.
      // If dst == src0 == src1, then the shuffle is unary and we only use src0.
      UseScratchRegisterScope scope(masm());
      VRegister temp = scope.AcquireV(kFormat4S);
      if (dst == src0) {
        __ Mov(temp, src0);
        src0 = temp;
      } else if (dst == src1) {
        __ Mov(temp, src1);
        src1 = temp;
      }
      int32_t shuffle = i.InputInt32(2);

      // Check whether we can reduce the number of vmovs by performing a dup
      // first.
      if (src0 == src1) {
        const std::array<int, 4> lanes{shuffle & 0x3, shuffle >> 8 & 0x3,
                                       shuffle >> 16 & 0x3,
                                       shuffle >> 24 & 0x3};
        std::array<int, 4> lane_counts{};
        for (int lane : lanes) {
          ++lane_counts[lane];
        }

        int duplicate_lane = -1;
        for (int lane = 0; lane < 4; ++lane) {
          if (lane_counts[lane] > 1) {
            duplicate_lane = lane;
            break;
          }
        }

        if (duplicate_lane != -1) {
          __ Dup(dst, src0, duplicate_lane);
          for (int i = 0; i < 4; ++i) {
            int lane = lanes[i];
            if (lane == duplicate_lane) continue;
            __ Mov(dst, i, src0, lane);
          }
          break;
        }
      }

      // Perform shuffle as a vmov per lane.
      for (int i = 0; i < 4; i++) {
        VRegister src = src0;
        int lane = shuffle & 0x7;
        if (lane >= 4) {
          src = src1;
          lane &= 0x3;
        }
        __ Mov(dst, i, src, lane);
        shuffle >>= 8;
      }
      break;
    }
      SIMD_BINOP_CASE(kArm64S32x4ZipLeft, Zip1, 4S);
      SIMD_BINOP_CASE(kArm64S32x4ZipRight, Zip2, 4S);
      SIMD_BINOP_CASE(kArm64S32x4UnzipLeft, Uzp1, 4S);
      SIMD_BINOP_CASE(kArm64S32x4UnzipRight, Uzp2, 4S);
      SIMD_BINOP_CASE(kArm64S32x4TransposeLeft, Trn1, 4S);
      SIMD_BINOP_CASE(kArm64S32x4TransposeRight, Trn2, 4S);
      SIMD_BINOP_CASE(kArm64S16x8ZipLeft, Zip1, 8H);
      SIMD_BINOP_CASE(kArm64S16x8ZipRight, Zip2, 8H);
      SIMD_BINOP_CASE(kArm64S16x8UnzipLeft, Uzp1, 8H);
      SIMD_BINOP_CASE(kArm64S16x8UnzipRight, Uzp2, 8H);
      SIMD_BINOP_CASE(kArm64S16x8TransposeLeft, Trn1, 8H);
      SIMD_BINOP_CASE(kArm64S16x8TransposeRight, Trn2, 8H);
      SIMD_BINOP_CASE(kArm64S8x16ZipLeft, Zip1, 16B);
      SIMD_BINOP_CASE(kArm64S8x16ZipRight, Zip2, 16B);
      SIMD_BINOP_CASE(kArm64S8x16UnzipLeft, Uzp1, 16B);
      SIMD_BINOP_CASE(kArm64S8x16UnzipRight, Uzp2, 16B);
      SIMD_BINOP_CASE(kArm64S8x16TransposeLeft, Trn1, 16B);
      SIMD_BINOP_CASE(kArm64S8x16TransposeRight, Trn2, 16B);
    case kArm64S8x16Concat: {
      __ Ext(i.OutputSimd128Register().V16B(), i.InputSimd128Register(0).V16B(),
             i.InputSimd128Register(1).V16B(), i.InputInt4(2));
      break;
    }
    case kArm64I8x16Swizzle: {
      __ Tbl(i.OutputSimd128Register().V16B(), i.InputSimd128Register(0).V16B(),
             i.InputSimd128Register(1).V16B());
      break;
    }
    case kArm64I8x16Shuffle: {
      Simd128Register dst = i.OutputSimd128Register().V16B(),
                      src0 = i.InputSimd128Register(0).V16B(),
                      src1 = i.InputSimd128Register(1).V16B();
      // Unary shuffle table is in src0, binary shuffle table is in src0, src1,
      // which must be consecutive.
      if (src0 != src1) {
        DCHECK(AreConsecutive(src0, src1));
      }

      int64_t imm1 = make_uint64(i.InputInt32(3), i.InputInt32(2));
      int64_t imm2 = make_uint64(i.InputInt32(5), i.InputInt32(4));
      DCHECK_EQ(0, (imm1 | imm2) & (src0 == src1 ? 0xF0F0F0F0F0F0F0F0
                                                 : 0xE0E0E0E0E0E0E0E0));

      UseScratchRegisterScope scope(masm());
      VRegister temp = scope.AcquireV(kFormat16B);
      __ Movi(temp, imm2, imm1);

      if (src0 == src1) {
        __ Tbl(dst, src0, temp.V16B());
      } else {
        __ Tbl(dst, src0, src1, temp.V16B());
      }
      break;
    }
    case kArm64S32x4Reverse: {
      Simd128Register dst = i.OutputSimd128Register().V16B(),
                      src = i.InputSimd128Register(0).V16B();
      __ Rev64(dst.V4S(), src.V4S());
      __ Ext(dst.V16B(), dst.V16B(), dst.V16B(), 8);
      break;
    }
      SIMD_UNOP_CASE(kArm64S32x2Reverse, Rev64, 4S);
      SIMD_UNOP_CASE(kArm64S16x4Reverse, Rev64, 8H);
      SIMD_UNOP_CASE(kArm64S16x2Reverse, Rev32, 8H);
      SIMD_UNOP_CASE(kArm64S8x8Reverse, Rev64, 16B);
      SIMD_UNOP_CASE(kArm64S8x4Reverse, Rev32, 16B);
      SIMD_UNOP_CASE(kArm64S8x2Reverse, Rev16, 16B);
    case kArm64LoadSplat: {
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      __ ld1r(i.OutputSimd128Register().Format(f), i.MemoryOperand(0));
      break;
    }
    case kArm64LoadLane: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      int laneidx = i.InputInt8(1);
      __ ld1(i.OutputSimd128Register().Format(f), laneidx, i.MemoryOperand(2));
      break;
    }
    case kArm64StoreLane: {
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      VectorFormat f = VectorFormatFillQ(LaneSizeField::decode(opcode));
      int laneidx = i.InputInt8(1);
      __ st1(i.InputSimd128Register(0).Format(f), laneidx, i.MemoryOperand(2));
      break;
    }
    case kArm64S128Load8x8S: {
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputSimd128Register().V8B(), i.MemoryOperand(0));
      __ Sxtl(i.OutputSimd128Register().V8H(), i.OutputSimd128Register().V8B());
      break;
    }
    case kArm64S128Load8x8U: {
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputSimd128Register().V8B(), i.MemoryOperand(0));
      __ Uxtl(i.OutputSimd128Register().V8H(), i.OutputSimd128Register().V8B());
      break;
    }
    case kArm64S128Load16x4S: {
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputSimd128Register().V4H(), i.MemoryOperand(0));
      __ Sxtl(i.OutputSimd128Register().V4S(), i.OutputSimd128Register().V4H());
      break;
    }
    case kArm64S128Load16x4U: {
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputSimd128Register().V4H(), i.MemoryOperand(0));
      __ Uxtl(i.OutputSimd128Register().V4S(), i.OutputSimd128Register().V4H());
      break;
    }
    case kArm64S128Load32x2S: {
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputSimd128Register().V2S(), i.MemoryOperand(0));
      __ Sxtl(i.OutputSimd128Register().V2D(), i.OutputSimd128Register().V2S());
      break;
    }
    case kArm64S128Load32x2U: {
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ldr(i.OutputSimd128Register().V2S(), i.MemoryOperand(0));
      __ Uxtl(i.OutputSimd128Register().V2D(), i.OutputSimd128Register().V2S());
      break;
    }
    case kArm64I64x2AllTrue: {
      __ I64x2AllTrue(i.OutputRegister32(), i.InputSimd128Register(0));
      break;
    }
    case kArm64V128AnyTrue: {
      UseScratchRegisterScope scope(masm());
      // For AnyTrue, the format does not matter; also, we would like to avoid
      // an expensive horizontal reduction.
      VRegister temp = scope.AcquireV(kFormat4S);
      __ Umaxp(temp, i.InputSimd128Register(0).V4S(),
               i.InputSimd128Register(0).V4S());
      __ Fmov(i.OutputRegister64(), temp.D());
      __ Cmp(i.OutputRegister64(), 0);
      __ Cset(i.OutputRegister32(), ne);
      break;
    }
    case kArm64S32x4OneLaneSwizzle: {
      Simd128Register dst = i.OutputSimd128Register().V4S(),
                      src = i.InputSimd128Register(0).V4S();
      int from = i.InputInt32(1);
      int to = i.InputInt32(2);
      if (dst != src) {
        __ Mov(dst, src);
      }
      __ Mov(dst, to, src, from);
      break;
    }
#define SIMD_REDUCE_OP_CASE(Op, Instr, format, FORMAT)     \
  case Op: {                                               \
    UseScratchRegisterScope scope(masm());                 \
    VRegister temp = scope.AcquireV(format);               \
    __ Instr(temp, i.InputSimd128Register(0).V##FORMAT()); \
    __ Umov(i.OutputRegister32(), temp, 0);                \
    __ Cmp(i.OutputRegister32(), 0);                       \
    __ Cset(i.OutputRegister32(), ne);                     \
    break;                                                 \
  }
      SIMD_REDUCE_OP_CASE(kArm64I32x4AllTrue, Uminv, kFormatS, 4S);
      SIMD_REDUCE_OP_CASE(kArm64I16x8AllTrue, Uminv, kFormatH, 8H);
      SIMD_REDUCE_OP_CASE(kArm64I8x16AllTrue, Uminv, kFormatB, 16B);
#endif  // V8_ENABLE_WEBASSEMBLY
  }
  return kSuccess;
}

#undef SIMD_UNOP_CASE
#undef SIMD_UNOP_LANE_SIZE_CASE
#undef SIMD_BINOP_CASE
#undef SIMD_BINOP_LANE_SIZE_CASE
#undef SIMD_DESTRUCTIVE_BINOP_CASE
#undef SIMD_DESTRUCTIVE_BINOP_LANE_SIZE_CASE
#undef SIMD_DESTRUCTIVE_RELAXED_FUSED_CASE
#undef SIMD_REDUCE_OP_CASE
#undef ASSEMBLE_SIMD_SHIFT_LEFT
#undef ASSEMBLE_SIMD_SHIFT_RIGHT

// Assemble branches after this instruction.
void CodeGenerator::AssembleArchBranch(Instruction* instr, BranchInfo* branch) {
  Arm64OperandConverter i(this, instr);
  Label* tlabel = branch->true_label;
  Label* flabel = branch->false_label;
  FlagsCondition condition = branch->condition;
  ArchOpcode opcode = instr->arch_opcode();

  if (opcode == kArm64CompareAndBranch32) {
    switch (condition) {
      case kEqual:
        __ Cbz(i.InputRegister32(0), tlabel);
        break;
      case kNotEqual:
        __ Cbnz(i.InputRegister32(0), tlabel);
        break;
      default:
        UNREACHABLE();
    }
  } else if (opcode == kArm64CompareAndBranch) {
    switch (condition) {
      case kEqual:
        __ Cbz(i.InputRegister64(0), tlabel);
        break;
      case kNotEqual:
        __ Cbnz(i.InputRegister64(0), tlabel);
        break;
      default:
        UNREACHABLE();
    }
  } else if (opcode == kArm64TestAndBranch32) {
    switch (condition) {
      case kEqual:
        __ Tbz(i.InputRegister32(0), i.InputInt5(1), tlabel);
        break;
      case kNotEqual:
        __ Tbnz(i.InputRegister32(0), i.InputInt5(1), tlabel);
        break;
      default:
        UNREACHABLE();
    }
  } else if (opcode == kArm64TestAndBranch) {
    switch (condition) {
      case kEqual:
        __ Tbz(i.InputRegister64(0), i.InputInt6(1), tlabel);
        break;
      case kNotEqual:
        __ Tbnz(i.InputRegister64(0), i.InputInt6(1), tlabel);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    Condition cc = FlagsConditionToCondition(condition);
    __ B(cc, tlabel);
  }
  if (!branch->fallthru) __ B(flabel);  // no fallthru to flabel.
}

void CodeGenerator::AssembleArchDeoptBranch(Instruction* instr,
                                            BranchInfo* branch) {
  AssembleArchBranch(instr, branch);
}

void CodeGenerator::AssembleArchJumpRegardlessOfAssemblyOrder(
    RpoNumber target) {
  __ B(GetLabel(target));
}

#if V8_ENABLE_WEBASSEMBLY
void CodeGenerator::AssembleArchTrap(Instruction* instr,
                                     FlagsCondition condition) {
  auto ool = zone()->New<WasmOutOfLineTrap>(this, instr);
  Label* tlabel = ool->entry();
  Condition cc = FlagsConditionToCondition(condition);
  __ B(cc, tlabel);
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Assemble boolean materializations after this instruction.
void CodeGenerator::AssembleArchBoolean(Instruction* instr,
                                        FlagsCondition condition) {
  Arm64OperandConverter i(this, instr);

  // Materialize a full 64-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  DCHECK_NE(0u, instr->OutputCount());
  Register reg = i.OutputRegister(instr->OutputCount() - 1);
  Condition cc = FlagsConditionToCondition(condition);
  __ Cset(reg, cc);
}

// Given condition, return a value for nzcv which represents it. This is used
// for the default condition for ccmp.
inline StatusFlags ConditionToDefaultFlags(Condition condition) {
  switch (condition) {
    default:
      UNREACHABLE();
    case eq:
      return ZFlag;  // Z == 1
    case ne:
      return NoFlag;  // Z == 0
    case hs:
      return CFlag;  // C == 1
    case lo:
      return NoFlag;  // C == 0
    case mi:
      return NFlag;  // N == 1
    case pl:
      return NoFlag;  // N == 0
    case vs:
      return VFlag;  // V == 1
    case vc:
      return NoFlag;  // V == 0
    case hi:
      return CFlag;  // C == 1 && Z == 0
    case ls:
      return NoFlag;  // C == 0 || Z == 1
    case ge:
      return NoFlag;  // N == V
    case lt:
      return NFlag;  // N != V
    case gt:
      return NoFlag;  // Z == 0 && N == V
    case le:
      return ZFlag;  // Z == 1 || N != V
  }
}

void AssembleConditionalCompareChain(Instruction* instr, int64_t num_ccmps,
                                     size_t ccmp_base_index,
                                     CodeGenerator* gen) {
  Arm64OperandConverter i(gen, instr);
  // The first two, or three operands are the compare that begins the chain.
  // These operands are used when the first compare, the one with the
  // continuation attached, is generated.
  // Then, each five provide:
  //  - cmp opcode
  //  - compare lhs
  //  - compare rhs
  //  - default flags
  //  - user condition
  for (unsigned n = 0; n < num_ccmps; ++n) {
    size_t opcode_index = ccmp_base_index + kCcmpOffsetOfOpcode;
    size_t compare_lhs_index = ccmp_base_index + kCcmpOffsetOfLhs;
    size_t compare_rhs_index = ccmp_base_index + kCcmpOffsetOfRhs;
    size_t default_condition_index =
        ccmp_base_index + kCcmpOffsetOfDefaultFlags;
    size_t compare_condition_index =
        ccmp_base_index + kCcmpOffsetOfCompareCondition;
    ccmp_base_index += kNumCcmpOperands;
    DCHECK_LT(ccmp_base_index, instr->InputCount() - 1);

    InstructionCode code = static_cast<InstructionCode>(
        i.ToConstant(instr->InputAt(opcode_index)).ToInt64());

    FlagsCondition default_condition = static_cast<FlagsCondition>(
        i.ToConstant(instr->InputAt(default_condition_index)).ToInt64());

    StatusFlags default_flags =
        ConditionToDefaultFlags(FlagsConditionToCondition(default_condition));

    FlagsCondition compare_condition = static_cast<FlagsCondition>(
        i.ToConstant(instr->InputAt(compare_condition_index)).ToInt64());

    if (code == kArm64Cmp) {
      gen->masm()->Ccmp(i.InputRegister64(compare_lhs_index),
                        i.InputOperand64(compare_rhs_index), default_flags,
                        FlagsConditionToCondition(compare_condition));
    } else {
      DCHECK_EQ(code, kArm64Cmp32);
      gen->masm()->Ccmp(i.InputRegister32(compare_lhs_index),
                        i.InputOperand32(compare_rhs_index), default_flags,
                        FlagsConditionToCondition(compare_condition));
    }
  }
}

// Assemble a conditional compare and boolean materializations after this
// instruction.
void CodeGenerator::AssembleArchConditionalBoolean(Instruction* instr) {
  // Materialize a full 64-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  DCHECK_NE(0u, instr->OutputCount());
  Arm64OperandConverter i(this, instr);
  Register reg = i.OutputRegister(instr->OutputCount() - 1);
  DCHECK_GE(instr->InputCount(), 6);

  // Input ordering:
  // > InputCount - 1: number of ccmps.
  // > InputCount - 2: branch condition.
  size_t num_ccmps_index =
      instr->InputCount() - kConditionalSetEndOffsetOfNumCcmps;
  size_t set_condition_index =
      instr->InputCount() - kConditionalSetEndOffsetOfCondition;
  int64_t num_ccmps = i.ToConstant(instr->InputAt(num_ccmps_index)).ToInt64();
  size_t ccmp_base_index = set_condition_index - kNumCcmpOperands * num_ccmps;
  AssembleConditionalCompareChain(instr, num_ccmps, ccmp_base_index, this);

  FlagsCondition set_condition = static_cast<FlagsCondition>(
      i.ToConstant(instr->InputAt(set_condition_index)).ToInt64());
  __ Cset(reg, FlagsConditionToCondition(set_condition));
}

void CodeGenerator::AssembleArchConditionalBranch(Instruction* instr,
                                                  BranchInfo* branch) {
  DCHECK_GE(instr->InputCount(), 6);
  Arm64OperandConverter i(this, instr);
  // Input ordering:
  // > InputCount - 1: false block.
  // > InputCount - 2: true block.
  // > InputCount - 3: number of ccmps.
  // > InputCount - 4: branch condition.
  size_t num_ccmps_index =
      instr->InputCount() - kConditionalBranchEndOffsetOfNumCcmps;
  int64_t num_ccmps = i.ToConstant(instr->InputAt(num_ccmps_index)).ToInt64();
  size_t ccmp_base_index = instr->InputCount() -
                           kConditionalBranchEndOffsetOfCondition -
                           kNumCcmpOperands * num_ccmps;
  AssembleConditionalCompareChain(instr, num_ccmps, ccmp_base_index, this);
  Condition cc = FlagsConditionToCondition(branch->condition);
  __ B(cc, branch->true_label);
  if (!branch->fallthru) __ B(branch->false_label);
}

void CodeGenerator::AssembleArchSelect(Instruction* instr,
                                       FlagsCondition condition) {
  Arm64OperandConverter i(this, instr);
  // The result register is always the last output of the instruction.
  size_t output_index = instr->OutputCount() - 1;
  MachineRepresentation rep =
      LocationOperand::cast(instr->OutputAt(output_index))->representation();
  Condition cc = FlagsConditionToCondition(condition);
  // We don't now how many inputs were consumed by the condition, so we have to
  // calculate the indices of the last two inputs.
  DCHECK_GE(instr->InputCount(), 2);
  size_t true_value_index = instr->InputCount() - 2;
  size_t false_value_index = instr->InputCount() - 1;
  if (rep == MachineRepresentation::kFloat32) {
    __ Fcsel(i.OutputFloat32Register(output_index),
             i.InputFloat32OrFPZeroRegister(true_value_index),
             i.InputFloat32OrFPZeroRegister(false_value_index), cc);
  } else if (rep == MachineRepresentation::kFloat64) {
    __ Fcsel(i.OutputFloat64Register(output_index),
             i.InputFloat64OrFPZeroRegister(true_value_index),
             i.InputFloat64OrFPZeroRegister(false_value_index), cc);
  } else if (rep == MachineRepresentation::kWord32) {
    __ Csel(i.OutputRegister32(output_index),
            i.InputOrZeroRegister32(true_value_index),
            i.InputOrZeroRegister32(false_value_index), cc);
  } else {
    DCHECK_EQ(rep, MachineRepresentation::kWord64);
    __ Csel(i.OutputRegister64(output_index),
            i.InputOrZeroRegister64(true_value_index),
            i.InputOrZeroRegister64(false_value_index), cc);
  }
}

void CodeGenerator::AssembleArchBinarySearchSwitch(Instruction* instr) {
  Arm64OperandConverter i(this, instr);
  Register input = i.InputRegister32(0);
  std::vector<std::pair<int32_t, Label*>> cases;
  for (size_t index = 2; index < instr->InputCount(); index += 2) {
    cases.push_back({i.InputInt32(index + 0), GetLabel(i.InputRpo(index + 1))});
  }
  AssembleArchBinarySearchSwitchRange(input, i.InputRpo(1), cases.data(),
                                      cases.data() + cases.size());
}

void CodeGenerator::AssembleArchTableSwitch(Instruction* instr) {
  Arm64OperandConverter i(this, instr);
  UseScratchRegisterScope scope(masm());
  Register input = i.InputRegister32(0);
  Register temp = scope.AcquireX();
  size_t const case_count = instr->InputCount() - 2;
  Label table;
  __ Cmp(input, case_count);
  __ B(hs, GetLabel(i.InputRpo(1)));
  __ Adr(temp, &table);
  int entry_size_log2 = 2;
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  ++entry_size_log2;  // Account for BTI.
  constexpr int instructions_per_jump_target = 1;
#else
  constexpr int instructions_per_jump_target = 0;
#endif
  constexpr int instructions_per_case = 1 + instructions_per_jump_target;
  __ Add(temp, temp, Operand(input, UXTW, entry_size_log2));
  __ Br(temp);
  {
    const size_t instruction_count =
        case_count * instructions_per_case + instructions_per_jump_target;
    MacroAssembler::BlockPoolsScope block_pools(masm(),
                                                instruction_count * kInstrSize);
    __ Bind(&table);
    for (size_t index = 0; index < case_count; ++index) {
      __ JumpTarget();
      __ B(GetLabel(i.InputRpo(index + 2)));
    }
    __ JumpTarget();
  }
}

void CodeGenerator::FinishFrame(Frame* frame) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  // Save FP registers.
  CPURegList saves_fp =
      CPURegList(kDRegSizeInBits, call_descriptor->CalleeSavedFPRegisters());
  int saved_count = saves_fp.Count();
  if (saved_count != 0) {
    DCHECK(saves_fp.bits() == CPURegList::GetCalleeSavedV().bits());
    frame->AllocateSavedCalleeRegisterSlots(saved_count *
                                            (kDoubleSize / kSystemPointerSize));
  }

  CPURegList saves =
      CPURegList(kXRegSizeInBits, call_descriptor->CalleeSavedRegisters());
  saved_count = saves.Count();
  if (saved_count != 0) {
    frame->AllocateSavedCalleeRegisterSlots(saved_count);
  }
  frame->AlignFrame(16);
}

void CodeGenerator::AssembleConstructFrame() {
  auto call_descriptor = linkage()->GetIncomingDescriptor();
  __ AssertSpAligned();

  // The frame has been previously padded in CodeGenerator::FinishFrame().
  DCHECK_EQ(frame()->GetTotalFrameSlotCount() % 2, 0);
  int required_slots =
      frame()->GetTotalFrameSlotCount() - frame()->GetFixedSlotCount();

  CPURegList saves =
      CPURegList(kXRegSizeInBits, call_descriptor->CalleeSavedRegisters());
  DCHECK_EQ(saves.Count() % 2, 0);
  CPURegList saves_fp =
      CPURegList(kDRegSizeInBits, call_descriptor->CalleeSavedFPRegisters());
  DCHECK_EQ(saves_fp.Count() % 2, 0);
  // The number of return slots should be even after aligning the Frame.
  const int returns = frame()->GetReturnSlotCount();
  DCHECK_EQ(returns % 2, 0);

  if (frame_access_state()->has_frame()) {
    // Link the frame
    if (call_descriptor->IsJSFunctionCall()) {
      static_assert(StandardFrameConstants::kFixedFrameSize % 16 == 8);
      DCHECK_EQ(required_slots % 2, 1);
      __ Prologue();
      // Update required_slots count since we have just claimed one extra slot.
      static_assert(MacroAssembler::kExtraSlotClaimedByPrologue == 1);
      required_slots -= MacroAssembler::kExtraSlotClaimedByPrologue;
#if V8_ENABLE_WEBASSEMBLY
    } else if (call_descriptor->IsWasmFunctionCall() ||
               call_descriptor->IsWasmCapiFunction() ||
               call_descriptor->IsWasmImportWrapper() ||
               (call_descriptor->IsCFunctionCall() &&
                info()->GetOutputStackFrameType() ==
                    StackFrame::C_WASM_ENTRY)) {
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.AcquireX();
      __ Mov(scratch,
             StackFrame::TypeToMarker(info()->GetOutputStackFrameType()));
      __ Push<MacroAssembler::kSignLR>(lr, fp, scratch, kWasmInstanceRegister);
      static constexpr int kSPToFPDelta = 2 * kSystemPointerSize;
      __ Add(fp, sp, kSPToFPDelta);
      if (call_descriptor->IsWasmCapiFunction()) {
        // The C-API function has one extra slot for the PC.
        required_slots++;
      } else if (call_descriptor->IsWasmImportWrapper()) {
        // If the wrapper is running on a secondary stack, it will switch to the
        // central stack and fill these slots with the central stack pointer and
        // secondary stack limit. Otherwise the slots remain empty.
        static_assert(WasmImportWrapperFrameConstants::kCentralStackSPOffset ==
                      -24);
        static_assert(
            WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset == -32);
        __ Push(xzr, xzr);
      }
#endif  // V8_ENABLE_WEBASSEMBLY
    } else if (call_descriptor->kind() == CallDescriptor::kCallCodeObject) {
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.AcquireX();
      __ Mov(scratch,
             StackFrame::TypeToMarker(info()->GetOutputStackFrameType()));
      __ Push<MacroAssembler::kSignLR>(lr, fp, scratch, padreg);
      static constexpr int kSPToFPDelta = 2 * kSystemPointerSize;
      __ Add(fp, sp, kSPToFPDelta);
      // One of the extra slots has just been claimed when pushing the padreg.
      // We also know that we have at least one slot to claim here, as the typed
      // frame has an odd number of fixed slots, and all other parts of the
      // total frame slots are even, leaving {required_slots} to be odd.
      DCHECK_GE(required_slots, 1);
      required_slots--;
    } else {
      __ Push<MacroAssembler::kSignLR>(lr, fp);
      __ Mov(fp, sp);
    }
    unwinding_info_writer_.MarkFrameConstructed(__ pc_offset());

    // Create OSR entry if applicable
    if (info()->is_osr()) {
      // TurboFan OSR-compiled functions cannot be entered directly.
      __ Abort(AbortReason::kShouldNotDirectlyEnterOsrFunction);

      // Unoptimized code jumps directly to this entrypoint while the
      // unoptimized frame is still on the stack. Optimized code uses OSR values
      // directly from the unoptimized frame. Thus, all that needs to be done is
      // to allocate the remaining stack slots.
      __ RecordComment("-- OSR entrypoint --");
      osr_pc_offset_ = __ pc_offset();
      __ CodeEntry();
      size_t unoptimized_frame_slots = osr_helper()->UnoptimizedFrameSlots();
      DCHECK(call_descriptor->IsJSFunctionCall());
      DCHECK_EQ(unoptimized_frame_slots % 2, 1);
      // One unoptimized frame slot has already been claimed when the actual
      // arguments count was pushed.
      required_slots -=
          unoptimized_frame_slots - MacroAssembler::kExtraSlotClaimedByPrologue;
    }

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
        Register stack_limit = temps.AcquireX();
        __ LoadStackLimit(stack_limit, StackLimitKind::kRealStackLimit);
        __ Add(stack_limit, stack_limit, required_slots * kSystemPointerSize);
        __ Cmp(sp, stack_limit);
        __ B(hs, &done);
      }

      __ Call(static_cast<intptr_t>(Builtin::kWasmStackOverflow),
              RelocInfo::WASM_STUB_CALL);
      // The call does not return, hence we can ignore any references and just
      // define an empty safepoint.
      ReferenceMap* reference_map = zone()->New<ReferenceMap>(zone());
      RecordSafepoint(reference_map);
      if (v8_flags.debug_code) __ Brk(0);
      __ Bind(&done);
    }
#endif  // V8_ENABLE_WEBASSEMBLY

    // Skip callee-saved slots, which are pushed below.
    required_slots -= saves.Count();
    required_slots -= saves_fp.Count();
    required_slots -= returns;

    __ Claim(required_slots);
  }

  // Save FP registers.
  DCHECK_IMPLIES(saves_fp.Count() != 0,
                 saves_fp.bits() == CPURegList::GetCalleeSavedV().bits());
  __ PushCPURegList(saves_fp);

  // Save registers.
  __ PushCPURegList(saves);

  if (returns != 0) {
    __ Claim(returns);
  }

  for (int spill_slot : frame()->tagged_slots()) {
    FrameOffset offset = frame_access_state()->GetFrameOffset(spill_slot);
    DCHECK(offset.from_frame_pointer());
    __ Str(xzr, MemOperand(fp, offset.offset()));
  }
}

void CodeGenerator::AssembleReturn(InstructionOperand* additional_pop_count) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  const int returns = RoundUp(frame()->GetReturnSlotCount(), 2);
  if (returns != 0) {
    __ Drop(returns);
  }

  // Restore registers.
  CPURegList saves =
      CPURegList(kXRegSizeInBits, call_descriptor->CalleeSavedRegisters());
  __ PopCPURegList(saves);

  // Restore fp registers.
  CPURegList saves_fp =
      CPURegList(kDRegSizeInBits, call_descriptor->CalleeSavedFPRegisters());
  __ PopCPURegList(saves_fp);

  unwinding_info_writer_.MarkBlockWillExit();

  const int parameter_slots =
      static_cast<int>(call_descriptor->ParameterSlotCount());
  Arm64OperandConverter g(this, nullptr);

  // {aditional_pop_count} is only greater than zero if {parameter_slots = 0}.
  // Check RawMachineAssembler::PopAndReturn.
  if (parameter_slots != 0) {
    if (additional_pop_count->IsImmediate()) {
      DCHECK_EQ(g.ToConstant(additional_pop_count).ToInt32(), 0);
    } else if (v8_flags.debug_code) {
      __ cmp(g.ToRegister(additional_pop_count), Operand(0));
      __ Assert(eq, AbortReason::kUnexpectedAdditionalPopValue);
    }
  }

  Register argc_reg = x3;
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
        __ B(&return_label_);
        return;
      } else {
        __ Bind(&return_label_);
      }
    }
    if (drop_jsargs) {
      // Get the actual argument count.
      DCHECK(!call_descriptor->CalleeSavedRegisters().has(argc_reg));
      __ Ldr(argc_reg, MemOperand(fp, StandardFrameConstants::kArgCOffset));
    }
    AssembleDeconstructFrame();
  }

  if (drop_jsargs) {
    // We must pop all arguments from the stack (including the receiver). This
    // number of arguments is given by max(1 + argc_reg, parameter_slots).
    Label argc_reg_has_final_count;
    DCHECK(!call_descriptor->CalleeSavedRegisters().has(argc_reg));
    if (parameter_slots > 1) {
      __ Cmp(argc_reg, Operand(parameter_slots));
      __ B(&argc_reg_has_final_count, ge);
      __ Mov(argc_reg, Operand(parameter_slots));
      __ Bind(&argc_reg_has_final_count);
    }
    __ DropArguments(argc_reg);
  } else if (additional_pop_count->IsImmediate()) {
    int additional_count = g.ToConstant(additional_pop_count).ToInt32();
    __ DropArguments(parameter_slots + additional_count);
  } else if (parameter_slots == 0) {
    __ DropArguments(g.ToRegister(additional_pop_count));
  } else {
    // {additional_pop_count} is guaranteed to be zero if {parameter_slots !=
    // 0}. Check RawMachineAssembler::PopAndReturn.
    __ DropArguments(parameter_slots);
  }
  __ AssertSpAligned();
  __ Ret();
}

void CodeGenerator::FinishCode() { __ ForceConstantPoolEmissionWithoutJump(); }

void CodeGenerator::PrepareForDeoptimizationExits(
    ZoneDeque<DeoptimizationExit*>* exits) {
  __ ForceConstantPoolEmissionWithoutJump();
  // We are conservative here, reserving sufficient space for the largest deopt
  // kind.
  DCHECK_GE(Deoptimizer::kLazyDeoptExitSize, Deoptimizer::kEagerDeoptExitSize);
  __ CheckVeneerPool(
      false, false,
      static_cast<int>(exits->size()) * Deoptimizer::kLazyDeoptExitSize);

  // Check which deopt kinds exist in this InstructionStream object, to avoid
  // emitting jumps to unused entries.
  bool saw_deopt_kind[kDeoptimizeKindCount] = {false};
  for (auto exit : *exits) {
    saw_deopt_kind[static_cast<int>(exit->kind())] = true;
  }

  // Emit the jumps to deoptimization entries.
  UseScratchRegisterScope scope(masm());
  Register scratch = scope.AcquireX();
  static_assert(static_cast<int>(kFirstDeoptimizeKind) == 0);
  for (int i = 0; i < kDeoptimizeKindCount; i++) {
    if (!saw_deopt_kind[i]) continue;
    DeoptimizeKind kind = static_cast<DeoptimizeKind>(i);
    __ bind(&jump_deoptimization_entry_labels_[i]);
    __ LoadEntryFromBuiltin(Deoptimizer::GetDeoptimizationEntry(kind), scratch);
    __ Jump(scratch);
  }
}

AllocatedOperand CodeGenerator::Push(InstructionOperand* source) {
  auto rep = LocationOperand::cast(source)->representation();
  int new_slots = RoundUp<2>(ElementSizeInPointers(rep));
  Arm64OperandConverter g(this, nullptr);
  int last_frame_slot_id =
      frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
  int sp_delta = frame_access_state_->sp_delta();
  int slot_id = last_frame_slot_id + sp_delta + new_slots;
  AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
  if (source->IsRegister()) {
    __ Push(padreg, g.ToRegister(source));
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else if (source->IsStackSlot()) {
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.AcquireX();
    __ Ldr(scratch, g.ToMemOperand(source, masm()));
    __ Push(padreg, scratch);
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else {
    // No push instruction for this operand type. Bump the stack pointer and
    // assemble the move.
    __ Sub(sp, sp, Operand(new_slots * kSystemPointerSize));
    frame_access_state()->IncreaseSPDelta(new_slots);
    AssembleMove(source, &stack_slot);
  }
  temp_slots_ += new_slots;
  return stack_slot;
}

void CodeGenerator::Pop(InstructionOperand* dest, MachineRepresentation rep) {
  int dropped_slots = RoundUp<2>(ElementSizeInPointers(rep));
  Arm64OperandConverter g(this, nullptr);
  if (dest->IsRegister()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ Pop(g.ToRegister(dest), padreg);
  } else if (dest->IsStackSlot()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.AcquireX();
    __ Pop(scratch, padreg);
    __ Str(scratch, g.ToMemOperand(dest, masm()));
  } else {
    int last_frame_slot_id =
        frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
    int sp_delta = frame_access_state_->sp_delta();
    int slot_id = last_frame_slot_id + sp_delta;
    AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
    AssembleMove(&stack_slot, dest);
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ Add(sp, sp, Operand(dropped_slots * kSystemPointerSize));
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
  DCHECK(!source->IsImmediate());
  move_cycle_.temps.emplace(masm());
  auto& temps = *move_cycle_.temps;
  // Temporarily exclude the reserved scratch registers while we pick one to
  // resolve the move cycle. Re-include them immediately afterwards as they
  // might be needed for the move to the temp location.
  temps.Exclude(CPURegList(64, move_cycle_.scratch_regs));
  temps.ExcludeFP(CPURegList(64, move_cycle_.scratch_fp_regs));
  if (!IsFloatingPoint(rep)) {
    if (temps.CanAcquire()) {
      Register scratch = move_cycle_.temps->AcquireX();
      move_cycle_.scratch_reg.emplace(scratch);
    } else if (temps.CanAcquireFP()) {
      // Try to use an FP register if no GP register is available for non-FP
      // moves.
      DoubleRegister scratch = move_cycle_.temps->AcquireD();
      move_cycle_.scratch_reg.emplace(scratch);
    }
  } else if (rep == MachineRepresentation::kFloat32) {
    VRegister scratch = move_cycle_.temps->AcquireS();
    move_cycle_.scratch_reg.emplace(scratch);
  } else if (rep == MachineRepresentation::kFloat64) {
    VRegister scratch = move_cycle_.temps->AcquireD();
    move_cycle_.scratch_reg.emplace(scratch);
  } else if (rep == MachineRepresentation::kSimd128) {
    VRegister scratch = move_cycle_.temps->AcquireQ();
    move_cycle_.scratch_reg.emplace(scratch);
  }
  temps.Include(CPURegList(64, move_cycle_.scratch_regs));
  temps.IncludeFP(CPURegList(64, move_cycle_.scratch_fp_regs));
  if (move_cycle_.scratch_reg.has_value()) {
    // A scratch register is available for this rep.
    auto& scratch_reg = *move_cycle_.scratch_reg;
    if (scratch_reg.IsD() && !IsFloatingPoint(rep)) {
      AllocatedOperand scratch(LocationOperand::REGISTER,
                               MachineRepresentation::kFloat64,
                               scratch_reg.code());
      Arm64OperandConverter g(this, nullptr);
      if (source->IsStackSlot()) {
        __ Ldr(g.ToDoubleRegister(&scratch), g.ToMemOperand(source, masm()));
      } else {
        DCHECK(source->IsRegister());
        __ fmov(g.ToDoubleRegister(&scratch), g.ToRegister(source));
      }
    } else {
      AllocatedOperand scratch(LocationOperand::REGISTER, rep,
                               move_cycle_.scratch_reg->code());
      AssembleMove(source, &scratch);
    }
  } else {
    // The scratch registers are blocked by pending moves. Use the stack
    // instead.
    Push(source);
  }
}

void CodeGenerator::MoveTempLocationTo(InstructionOperand* dest,
                                       MachineRepresentation rep) {
  if (move_cycle_.scratch_reg.has_value()) {
    auto& scratch_reg = *move_cycle_.scratch_reg;
    if (!IsFloatingPoint(rep) && scratch_reg.IsD()) {
      // We used a D register to move a non-FP operand, change the
      // representation to correctly interpret the InstructionOperand's code.
      AllocatedOperand scratch(LocationOperand::REGISTER,
                               MachineRepresentation::kFloat64,
                               move_cycle_.scratch_reg->code());
      Arm64OperandConverter g(this, nullptr);
      if (dest->IsStackSlot()) {
        __ Str(g.ToDoubleRegister(&scratch), g.ToMemOperand(dest, masm()));
      } else {
        DCHECK(dest->IsRegister());
        __ fmov(g.ToRegister(dest), g.ToDoubleRegister(&scratch));
      }
    } else {
      AllocatedOperand scratch(LocationOperand::REGISTER, rep,
                               move_cycle_.scratch_reg->code());
      AssembleMove(&scratch, dest);
    }
  } else {
    Pop(dest, rep);
  }
  // Restore the default state to release the {UseScratchRegisterScope} and to
  // prepare for the next cycle.
  move_cycle_ = MoveCycleState();
}

void CodeGenerator::SetPendingMove(MoveOperands* move) {
  auto move_type = MoveType::InferMove(&move->source(), &move->destination());
  if (move_type == MoveType::kStackToStack) {
    Arm64OperandConverter g(this, nullptr);
    MemOperand src = g.ToMemOperand(&move->source(), masm());
    MemOperand dst = g.ToMemOperand(&move->destination(), masm());
    UseScratchRegisterScope temps(masm());
    if (move->source().IsSimd128StackSlot()) {
      VRegister temp = temps.AcquireQ();
      move_cycle_.scratch_fp_regs.set(temp);
    } else {
      Register temp = temps.AcquireX();
      move_cycle_.scratch_regs.set(temp);
    }
    int64_t src_offset = src.offset();
    unsigned src_size_log2 = CalcLSDataSizeLog2(LDR_x);
    int64_t dst_offset = dst.offset();
    unsigned dst_size_log2 = CalcLSDataSizeLog2(STR_x);
    // Offset doesn't fit into the immediate field so the assembler will emit
    // two instructions and use a second temp register.
    if ((src.IsImmediateOffset() &&
         !masm()->IsImmLSScaled(src_offset, src_size_log2) &&
         !masm()->IsImmLSUnscaled(src_offset)) ||
        (dst.IsImmediateOffset() &&
         !masm()->IsImmLSScaled(dst_offset, dst_size_log2) &&
         !masm()->IsImmLSUnscaled(dst_offset))) {
      Register temp = temps.AcquireX();
      move_cycle_.scratch_regs.set(temp);
    }
  }
}

void CodeGenerator::AssembleMove(InstructionOperand* source,
                                 InstructionOperand* destination) {
  Arm64OperandConverter g(this, nullptr);
  // Helper function to write the given constant to the dst register.
  auto MoveConstantToRegister = [&](Register dst, Constant src) {
    if (src.type() == Constant::kHeapObject) {
      Handle<HeapObject> src_object = src.ToHeapObject();
      RootIndex index;
      if (IsMaterializableFromRoot(src_object, &index)) {
        __ LoadRoot(dst, index);
      } else {
        __ Mov(dst, src_object);
      }
    } else if (src.type() == Constant::kCompressedHeapObject) {
      Handle<HeapObject> src_object = src.ToHeapObject();
      RootIndex index;
      if (IsMaterializableFromRoot(src_object, &index)) {
        __ LoadTaggedRoot(dst, index);
      } else {
        // TODO(v8:8977): Even though this mov happens on 32 bits (Note the
        // .W()) and we are passing along the RelocInfo, we still haven't made
        // the address embedded in the code-stream actually be compressed.
        __ Mov(dst.W(),
               Immediate(src_object, RelocInfo::COMPRESSED_EMBEDDED_OBJECT));
      }
    } else if (src.type() == Constant::kExternalReference) {
      __ Mov(dst, src.ToExternalReference());
    } else {
      Operand src_op = g.ToImmediate(source);
      if (src.type() == Constant::kInt32 && src_op.NeedsRelocation(masm())) {
        // Use 32-bit loads for relocatable 32-bit constants.
        dst = dst.W();
      }
      __ Mov(dst, src_op);
    }
  };
  switch (MoveType::InferMove(source, destination)) {
    case MoveType::kRegisterToRegister:
      if (source->IsRegister()) {
        __ Mov(g.ToRegister(destination), g.ToRegister(source));
      } else {
        DCHECK(source->IsSimd128Register() || source->IsFloatRegister() ||
               source->IsDoubleRegister());
        __ Mov(g.ToDoubleRegister(destination).Q(),
               g.ToDoubleRegister(source).Q());
      }
      return;
    case MoveType::kRegisterToStack: {
      MemOperand dst = g.ToMemOperand(destination, masm());
      if (source->IsRegister()) {
        __ Str(g.ToRegister(source), dst);
      } else {
        VRegister src = g.ToDoubleRegister(source);
        if (source->IsFloatRegister() || source->IsDoubleRegister()) {
          __ Str(src, dst);
        } else {
          DCHECK(source->IsSimd128Register());
          __ Str(src.Q(), dst);
        }
      }
      return;
    }
    case MoveType::kStackToRegister: {
      MemOperand src = g.ToMemOperand(source, masm());
      if (destination->IsRegister()) {
        __ Ldr(g.ToRegister(destination), src);
      } else {
        VRegister dst = g.ToDoubleRegister(destination);
        if (destination->IsFloatRegister() || destination->IsDoubleRegister()) {
          __ Ldr(dst, src);
        } else {
          DCHECK(destination->IsSimd128Register());
          __ Ldr(dst.Q(), src);
        }
      }
      return;
    }
    case MoveType::kStackToStack: {
      MemOperand src = g.ToMemOperand(source, masm());
      MemOperand dst = g.ToMemOperand(destination, masm());
      if (source->IsSimd128StackSlot()) {
        UseScratchRegisterScope scope(masm());
        VRegister temp = scope.AcquireQ();
        __ Ldr(temp, src);
        __ Str(temp, dst);
      } else {
        UseScratchRegisterScope scope(masm());
        Register temp = scope.AcquireX();
        __ Ldr(temp, src);
        __ Str(temp, dst);
      }
      return;
    }
    case MoveType::kConstantToRegister: {
      Constant src = g.ToConstant(source);
      if (destination->IsRegister()) {
        MoveConstantToRegister(g.ToRegister(destination), src);
      } else {
        VRegister dst = g.ToDoubleRegister(destination);
        if (destination->IsFloatRegister()) {
          __ Fmov(dst.S(), src.ToFloat32());
        } else {
          DCHECK(destination->IsDoubleRegister());
          __ Fmov(dst, src.ToFloat64().value());
        }
      }
      return;
    }
    case MoveType::kConstantToStack: {
      Constant src = g.ToConstant(source);
      MemOperand dst = g.ToMemOperand(destination, masm());
      if (destination->IsStackSlot()) {
        UseScratchRegisterScope scope(masm());
        Register temp = scope.AcquireX();
        MoveConstantToRegister(temp, src);
        __ Str(temp, dst);
      } else if (destination->IsFloatStackSlot()) {
        if (base::bit_cast<int32_t>(src.ToFloat32()) == 0) {
          __ Str(wzr, dst);
        } else {
          UseScratchRegisterScope scope(masm());
          VRegister temp = scope.AcquireS();
          __ Fmov(temp, src.ToFloat32());
          __ Str(temp, dst);
        }
      } else {
        DCHECK(destination->IsDoubleStackSlot());
        if (src.ToFloat64().AsUint64() == 0) {
          __ Str(xzr, dst);
        } else {
          UseScratchRegisterScope scope(masm());
          VRegister temp = scope.AcquireD();
          __ Fmov(temp, src.ToFloat64().value());
          __ Str(temp, dst);
        }
      }
      return;
    }
  }
  UNREACHABLE();
}

void CodeGenerator::AssembleSwap(InstructionOperand* source,
                                 InstructionOperand* destination) {
  Arm64OperandConverter g(this, nullptr);
  switch (MoveType::InferSwap(source, destination)) {
    case MoveType::kRegisterToRegister:
      if (source->IsRegister()) {
        __ Swap(g.ToRegister(source), g.ToRegister(destination));
      } else {
        VRegister src = g.ToDoubleRegister(source);
        VRegister dst = g.ToDoubleRegister(destination);
        if (source->IsFloatRegister() || source->IsDoubleRegister()) {
          __ Swap(src, dst);
        } else {
          DCHECK(source->IsSimd128Register());
          __ Swap(src.Q(), dst.Q());
        }
      }
      return;
    case MoveType::kRegisterToStack: {
      UseScratchRegisterScope scope(masm());
      MemOperand dst = g.ToMemOperand(destination, masm());
      if (source->IsRegister()) {
        Register temp = scope.AcquireX();
        Register src = g.ToRegister(source);
        __ Mov(temp, src);
        __ Ldr(src, dst);
        __ Str(temp, dst);
      } else {
        UseScratchRegisterScope scope(masm());
        VRegister src = g.ToDoubleRegister(source);
        if (source->IsFloatRegister() || source->IsDoubleRegister()) {
          VRegister temp = scope.AcquireD();
          __ Mov(temp, src);
          __ Ldr(src, dst);
          __ Str(temp, dst);
        } else {
          DCHECK(source->IsSimd128Register());
          VRegister temp = scope.AcquireQ();
          __ Mov(temp, src.Q());
          __ Ldr(src.Q(), dst);
          __ Str(temp, dst);
        }
      }
      return;
    }
    case MoveType::kStackToStack: {
      UseScratchRegisterScope scope(masm());
      MemOperand src = g.ToMemOperand(source, masm());
      MemOperand dst = g.ToMemOperand(destination, masm());
      VRegister temp_0 = scope.AcquireD();
      VRegister temp_1 = scope.AcquireD();
      if (source->IsSimd128StackSlot()) {
        __ Ldr(temp_0.Q(), src);
        __ Ldr(temp_1.Q(), dst);
        __ Str(temp_0.Q(), dst);
        __ Str(temp_1.Q(), src);
      } else {
        __ Ldr(temp_0, src);
        __ Ldr(temp_1, dst);
        __ Str(temp_0, dst);
        __ Str(temp_1, src);
      }
      return;
    }
    default:
      UNREACHABLE();
  }
}

void CodeGenerator::AssembleJumpTable(Label** targets, size_t target_count) {
  // On 64-bit ARM we emit the jump tables inline.
  UNREACHABLE();
}

#undef __

}  // namespace compiler
}  // namespace internal
}  // namespace v8
