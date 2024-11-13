// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/overflowing-math.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/callable.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/ia32/assembler-ia32.h"
#include "src/codegen/ia32/register-ia32.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/code-generator-impl.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/gap-resolver.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/osr.h"
#include "src/execution/frame-constants.h"
#include "src/execution/frames.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/objects/smi.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace compiler {

#define __ masm()->

#define kScratchDoubleReg xmm0

// Adds IA-32 specific methods for decoding operands.
class IA32OperandConverter : public InstructionOperandConverter {
 public:
  IA32OperandConverter(CodeGenerator* gen, Instruction* instr)
      : InstructionOperandConverter(gen, instr) {}

  Operand InputOperand(size_t index, int extra = 0) {
    return ToOperand(instr_->InputAt(index), extra);
  }

  Immediate InputImmediate(size_t index) {
    return ToImmediate(instr_->InputAt(index));
  }

  Operand OutputOperand() { return ToOperand(instr_->Output()); }

  Operand ToOperand(InstructionOperand* op, int extra = 0) {
    if (op->IsRegister()) {
      DCHECK_EQ(0, extra);
      return Operand(ToRegister(op));
    } else if (op->IsFPRegister()) {
      DCHECK_EQ(0, extra);
      return Operand(ToDoubleRegister(op));
    }
    DCHECK(op->IsStackSlot() || op->IsFPStackSlot());
    return SlotToOperand(AllocatedOperand::cast(op)->index(), extra);
  }

  Operand SlotToOperand(int slot, int extra = 0) {
    FrameOffset offset = frame_access_state()->GetFrameOffset(slot);
    return Operand(offset.from_stack_pointer() ? esp : ebp,
                   offset.offset() + extra);
  }

  Immediate ToImmediate(InstructionOperand* operand) {
    Constant constant = ToConstant(operand);
    switch (constant.type()) {
      case Constant::kInt32:
        return Immediate(constant.ToInt32(), constant.rmode());
      case Constant::kFloat32:
        return Immediate::EmbeddedNumber(constant.ToFloat32());
      case Constant::kFloat64:
        return Immediate::EmbeddedNumber(constant.ToFloat64().value());
      case Constant::kExternalReference:
        return Immediate(constant.ToExternalReference());
      case Constant::kHeapObject:
        return Immediate(constant.ToHeapObject());
      case Constant::kCompressedHeapObject:
        break;
      case Constant::kInt64:
        break;
      case Constant::kRpoNumber:
        return Immediate::CodeRelativeOffset(ToLabel(operand));
    }
    UNREACHABLE();
  }

  static size_t NextOffset(size_t* offset) {
    size_t i = *offset;
    (*offset)++;
    return i;
  }

  static ScaleFactor ScaleFor(AddressingMode one, AddressingMode mode) {
    static_assert(0 == static_cast<int>(times_1));
    static_assert(1 == static_cast<int>(times_2));
    static_assert(2 == static_cast<int>(times_4));
    static_assert(3 == static_cast<int>(times_8));
    int scale = static_cast<int>(mode - one);
    DCHECK(scale >= 0 && scale < 4);
    return static_cast<ScaleFactor>(scale);
  }

  Operand MemoryOperand(size_t* offset) {
    AddressingMode mode = AddressingModeField::decode(instr_->opcode());
    switch (mode) {
      case kMode_MR: {
        Register base = InputRegister(NextOffset(offset));
        int32_t disp = 0;
        return Operand(base, disp);
      }
      case kMode_MRI: {
        Register base = InputRegister(NextOffset(offset));
        Constant ctant = ToConstant(instr_->InputAt(NextOffset(offset)));
        return Operand(base, ctant.ToInt32(), ctant.rmode());
      }
      case kMode_MR1:
      case kMode_MR2:
      case kMode_MR4:
      case kMode_MR8: {
        Register base = InputRegister(NextOffset(offset));
        Register index = InputRegister(NextOffset(offset));
        ScaleFactor scale = ScaleFor(kMode_MR1, mode);
        int32_t disp = 0;
        return Operand(base, index, scale, disp);
      }
      case kMode_MR1I:
      case kMode_MR2I:
      case kMode_MR4I:
      case kMode_MR8I: {
        Register base = InputRegister(NextOffset(offset));
        Register index = InputRegister(NextOffset(offset));
        ScaleFactor scale = ScaleFor(kMode_MR1I, mode);
        Constant ctant = ToConstant(instr_->InputAt(NextOffset(offset)));
        return Operand(base, index, scale, ctant.ToInt32(), ctant.rmode());
      }
      case kMode_M1:
      case kMode_M2:
      case kMode_M4:
      case kMode_M8: {
        Register index = InputRegister(NextOffset(offset));
        ScaleFactor scale = ScaleFor(kMode_M1, mode);
        int32_t disp = 0;
        return Operand(index, scale, disp);
      }
      case kMode_M1I:
      case kMode_M2I:
      case kMode_M4I:
      case kMode_M8I: {
        Register index = InputRegister(NextOffset(offset));
        ScaleFactor scale = ScaleFor(kMode_M1I, mode);
        Constant ctant = ToConstant(instr_->InputAt(NextOffset(offset)));
        return Operand(index, scale, ctant.ToInt32(), ctant.rmode());
      }
      case kMode_MI: {
        Constant ctant = ToConstant(instr_->InputAt(NextOffset(offset)));
        return Operand(ctant.ToInt32(), ctant.rmode());
      }
      case kMode_Root: {
        Register base = kRootRegister;
        int32_t disp = InputInt32(NextOffset(offset));
        return Operand(base, disp);
      }
      case kMode_None:
        UNREACHABLE();
    }
    UNREACHABLE();
  }

  Operand MemoryOperand(size_t first_input = 0) {
    return MemoryOperand(&first_input);
  }

  Operand NextMemoryOperand(size_t offset = 0) {
    AddressingMode mode = AddressingModeField::decode(instr_->opcode());
    Register base = InputRegister(NextOffset(&offset));
    const int32_t disp = 4;
    if (mode == kMode_MR1) {
      Register index = InputRegister(NextOffset(&offset));
      ScaleFactor scale = ScaleFor(kMode_MR1, kMode_MR1);
      return Operand(base, index, scale, disp);
    } else if (mode == kMode_MRI) {
      Constant ctant = ToConstant(instr_->InputAt(NextOffset(&offset)));
      return Operand(base, ctant.ToInt32() + disp, ctant.rmode());
    } else {
      UNREACHABLE();
    }
  }

  void MoveInstructionOperandToRegister(Register destination,
                                        InstructionOperand* op) {
    if (op->IsImmediate() || op->IsConstant()) {
      gen_->masm()->mov(destination, ToImmediate(op));
    } else if (op->IsRegister()) {
      gen_->masm()->Move(destination, ToRegister(op));
    } else {
      gen_->masm()->mov(destination, ToOperand(op));
    }
  }
};

namespace {

bool HasAddressingMode(Instruction* instr) {
  return instr->addressing_mode() != kMode_None;
}

bool HasImmediateInput(Instruction* instr, size_t index) {
  return instr->InputAt(index)->IsImmediate();
}

bool HasRegisterInput(Instruction* instr, size_t index) {
  return instr->InputAt(index)->IsRegister();
}

class OutOfLineLoadFloat32NaN final : public OutOfLineCode {
 public:
  OutOfLineLoadFloat32NaN(CodeGenerator* gen, XMMRegister result)
      : OutOfLineCode(gen), result_(result) {}

  void Generate() final {
    __ xorps(result_, result_);
    __ divss(result_, result_);
  }

 private:
  XMMRegister const result_;
};

class OutOfLineLoadFloat64NaN final : public OutOfLineCode {
 public:
  OutOfLineLoadFloat64NaN(CodeGenerator* gen, XMMRegister result)
      : OutOfLineCode(gen), result_(result) {}

  void Generate() final {
    __ xorpd(result_, result_);
    __ divsd(result_, result_);
  }

 private:
  XMMRegister const result_;
};

class OutOfLineTruncateDoubleToI final : public OutOfLineCode {
 public:
  OutOfLineTruncateDoubleToI(CodeGenerator* gen, Register result,
                             XMMRegister input, StubCallMode stub_mode)
      : OutOfLineCode(gen),
        result_(result),
        input_(input),
#if V8_ENABLE_WEBASSEMBLY
        stub_mode_(stub_mode),
#endif  // V8_ENABLE_WEBASSEMBLY
        isolate_(gen->isolate()),
        zone_(gen->zone()) {
  }

  void Generate() final {
    __ AllocateStackSpace(kDoubleSize);
    __ Movsd(MemOperand(esp, 0), input_);
#if V8_ENABLE_WEBASSEMBLY
    if (stub_mode_ == StubCallMode::kCallWasmRuntimeStub) {
      // A direct call to a builtin. Just encode the builtin index. This will be
      // patched when the code is added to the native module and copied into
      // wasm code space.
      __ wasm_call(static_cast<Address>(Builtin::kDoubleToI),
                   RelocInfo::WASM_STUB_CALL);
#else
    // For balance.
    if (false) {
#endif  // V8_ENABLE_WEBASSEMBLY
    } else {
      __ CallBuiltin(Builtin::kDoubleToI);
    }
    __ mov(result_, MemOperand(esp, 0));
    __ add(esp, Immediate(kDoubleSize));
  }

 private:
  Register const result_;
  XMMRegister const input_;
#if V8_ENABLE_WEBASSEMBLY
  StubCallMode stub_mode_;
#endif  // V8_ENABLE_WEBASSEMBLY
  Isolate* isolate_;
  Zone* zone_;
};

class OutOfLineRecordWrite final : public OutOfLineCode {
 public:
  OutOfLineRecordWrite(CodeGenerator* gen, Register object, Operand operand,
                       Register value, Register scratch0, Register scratch1,
                       RecordWriteMode mode, StubCallMode stub_mode)
      : OutOfLineCode(gen),
        object_(object),
        operand_(operand),
        value_(value),
        scratch0_(scratch0),
        scratch1_(scratch1),
        mode_(mode),
#if V8_ENABLE_WEBASSEMBLY
        stub_mode_(stub_mode),
#endif  // V8_ENABLE_WEBASSEMBLY
        zone_(gen->zone()) {
    DCHECK(!AreAliased(object, scratch0, scratch1));
    DCHECK(!AreAliased(value, scratch0, scratch1));
  }

  void Generate() final {
    __ CheckPageFlag(value_, scratch0_,
                     MemoryChunk::kPointersToHereAreInterestingMask, zero,
                     exit());
    __ lea(scratch1_, operand_);
    SaveFPRegsMode const save_fp_mode = frame()->DidAllocateDoubleRegisters()
                                            ? SaveFPRegsMode::kSave
                                            : SaveFPRegsMode::kIgnore;
    if (mode_ == RecordWriteMode::kValueIsEphemeronKey) {
      __ CallEphemeronKeyBarrier(object_, scratch1_, save_fp_mode);
#if V8_ENABLE_WEBASSEMBLY
    } else if (stub_mode_ == StubCallMode::kCallWasmRuntimeStub) {
      // A direct call to a wasm runtime stub defined in this module.
      // Just encode the stub index. This will be patched when the code
      // is added to the native module and copied into wasm code space.
      __ CallRecordWriteStubSaveRegisters(object_, scratch1_, save_fp_mode,
                                          StubCallMode::kCallWasmRuntimeStub);
#endif  // V8_ENABLE_WEBASSEMBLY
    } else {
      __ CallRecordWriteStubSaveRegisters(object_, scratch1_, save_fp_mode);
    }
  }

 private:
  Register const object_;
  Operand const operand_;
  Register const value_;
  Register const scratch0_;
  Register const scratch1_;
  RecordWriteMode const mode_;
#if V8_ENABLE_WEBASSEMBLY
  StubCallMode const stub_mode_;
#endif  // V8_ENABLE_WEBASSEMBLY
  Zone* zone_;
};

}  // namespace

#define ASSEMBLE_COMPARE(asm_instr)                              \
  do {                                                           \
    if (HasAddressingMode(instr)) {                              \
      size_t index = 0;                                          \
      Operand left = i.MemoryOperand(&index);                    \
      if (HasImmediateInput(instr, index)) {                     \
        __ asm_instr(left, i.InputImmediate(index));             \
      } else {                                                   \
        __ asm_instr(left, i.InputRegister(index));              \
      }                                                          \
    } else {                                                     \
      if (HasImmediateInput(instr, 1)) {                         \
        if (HasRegisterInput(instr, 0)) {                        \
          __ asm_instr(i.InputRegister(0), i.InputImmediate(1)); \
        } else {                                                 \
          __ asm_instr(i.InputOperand(0), i.InputImmediate(1));  \
        }                                                        \
      } else {                                                   \
        if (HasRegisterInput(instr, 1)) {                        \
          __ asm_instr(i.InputRegister(0), i.InputRegister(1));  \
        } else {                                                 \
          __ asm_instr(i.InputRegister(0), i.InputOperand(1));   \
        }                                                        \
      }                                                          \
    }                                                            \
  } while (0)

#define ASSEMBLE_IEEE754_BINOP(name)                                     \
  do {                                                                   \
    /* Pass two doubles as arguments on the stack. */                    \
    __ PrepareCallCFunction(4, eax);                                     \
    __ movsd(Operand(esp, 0 * kDoubleSize), i.InputDoubleRegister(0));   \
    __ movsd(Operand(esp, 1 * kDoubleSize), i.InputDoubleRegister(1));   \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 4); \
    /* Return value is in st(0) on ia32. */                              \
    /* Store it into the result register. */                             \
    __ AllocateStackSpace(kDoubleSize);                                  \
    __ fstp_d(Operand(esp, 0));                                          \
    __ movsd(i.OutputDoubleRegister(), Operand(esp, 0));                 \
    __ add(esp, Immediate(kDoubleSize));                                 \
  } while (false)

#define ASSEMBLE_IEEE754_UNOP(name)                                      \
  do {                                                                   \
    /* Pass one double as argument on the stack. */                      \
    __ PrepareCallCFunction(2, eax);                                     \
    __ movsd(Operand(esp, 0 * kDoubleSize), i.InputDoubleRegister(0));   \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 2); \
    /* Return value is in st(0) on ia32. */                              \
    /* Store it into the result register. */                             \
    __ AllocateStackSpace(kDoubleSize);                                  \
    __ fstp_d(Operand(esp, 0));                                          \
    __ movsd(i.OutputDoubleRegister(), Operand(esp, 0));                 \
    __ add(esp, Immediate(kDoubleSize));                                 \
  } while (false)

#define ASSEMBLE_BINOP(asm_instr)                             \
  do {                                                        \
    if (HasAddressingMode(instr)) {                           \
      size_t index = 1;                                       \
      Operand right = i.MemoryOperand(&index);                \
      __ asm_instr(i.InputRegister(0), right);                \
    } else {                                                  \
      if (HasImmediateInput(instr, 1)) {                      \
        __ asm_instr(i.InputOperand(0), i.InputImmediate(1)); \
      } else {                                                \
        __ asm_instr(i.InputRegister(0), i.InputOperand(1));  \
      }                                                       \
    }                                                         \
  } while (0)

#define ASSEMBLE_ATOMIC_BINOP(bin_inst, mov_inst, cmpxchg_inst) \
  do {                                                          \
    Label binop;                                                \
    __ bind(&binop);                                            \
    __ mov_inst(eax, i.MemoryOperand(1));                       \
    __ Move(i.TempRegister(0), eax);                            \
    __ bin_inst(i.TempRegister(0), i.InputRegister(0));         \
    __ lock();                                                  \
    __ cmpxchg_inst(i.MemoryOperand(1), i.TempRegister(0));     \
    __ j(not_equal, &binop);                                    \
  } while (false)

#define ASSEMBLE_I64ATOMIC_BINOP(instr1, instr2)                \
  do {                                                          \
    Label binop;                                                \
    __ bind(&binop);                                            \
    __ mov(eax, i.MemoryOperand(2));                            \
    __ mov(edx, i.NextMemoryOperand(2));                        \
    __ push(ebx);                                               \
    frame_access_state()->IncreaseSPDelta(1);                   \
    i.MoveInstructionOperandToRegister(ebx, instr->InputAt(0)); \
    __ push(i.InputRegister(1));                                \
    __ instr1(ebx, eax);                                        \
    __ instr2(i.InputRegister(1), edx);                         \
    __ lock();                                                  \
    __ cmpxchg8b(i.MemoryOperand(2));                           \
    __ pop(i.InputRegister(1));                                 \
    __ pop(ebx);                                                \
    frame_access_state()->IncreaseSPDelta(-1);                  \
    __ j(not_equal, &binop);                                    \
  } while (false);

#define ASSEMBLE_MOVX(mov_instr)                            \
  do {                                                      \
    if (HasAddressingMode(instr)) {                         \
      __ mov_instr(i.OutputRegister(), i.MemoryOperand());  \
    } else if (HasRegisterInput(instr, 0)) {                \
      __ mov_instr(i.OutputRegister(), i.InputRegister(0)); \
    } else {                                                \
      __ mov_instr(i.OutputRegister(), i.InputOperand(0));  \
    }                                                       \
  } while (0)

#define ASSEMBLE_SIMD_PUNPCK_SHUFFLE(opcode)                         \
  do {                                                               \
    XMMRegister src0 = i.InputSimd128Register(0);                    \
    Operand src1 = i.InputOperand(instr->InputCount() == 2 ? 1 : 0); \
    if (CpuFeatures::IsSupported(AVX)) {                             \
      CpuFeatureScope avx_scope(masm(), AVX);                        \
      __ v##opcode(i.OutputSimd128Register(), src0, src1);           \
    } else {                                                         \
      DCHECK_EQ(i.OutputSimd128Register(), src0);                    \
      __ opcode(i.OutputSimd128Register(), src1);                    \
    }                                                                \
  } while (false)

#define ASSEMBLE_SIMD_IMM_SHUFFLE(opcode, SSELevel, imm)               \
  if (CpuFeatures::IsSupported(AVX)) {                                 \
    CpuFeatureScope avx_scope(masm(), AVX);                            \
    __ v##opcode(i.OutputSimd128Register(), i.InputSimd128Register(0), \
                 i.InputOperand(1), imm);                              \
  } else {                                                             \
    CpuFeatureScope sse_scope(masm(), SSELevel);                       \
    DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));   \
    __ opcode(i.OutputSimd128Register(), i.InputOperand(1), imm);      \
  }

#define ASSEMBLE_SIMD_ALL_TRUE(opcode)               \
  do {                                               \
    Register dst = i.OutputRegister();               \
    Operand src = i.InputOperand(0);                 \
    Register tmp = i.TempRegister(0);                \
    XMMRegister tmp_simd = i.TempSimd128Register(1); \
    __ mov(tmp, Immediate(1));                       \
    __ xor_(dst, dst);                               \
    __ Pxor(tmp_simd, tmp_simd);                     \
    __ opcode(tmp_simd, src);                        \
    __ Ptest(tmp_simd, tmp_simd);                    \
    __ cmov(zero, dst, tmp);                         \
  } while (false)

#define ASSEMBLE_SIMD_SHIFT(opcode, width)                \
  do {                                                    \
    XMMRegister dst = i.OutputSimd128Register();          \
    DCHECK_EQ(dst, i.InputSimd128Register(0));            \
    if (HasImmediateInput(instr, 1)) {                    \
      __ opcode(dst, dst, uint8_t{i.InputInt##width(1)}); \
    } else {                                              \
      XMMRegister tmp = i.TempSimd128Register(0);         \
      Register tmp_shift = i.TempRegister(1);             \
      constexpr int mask = (1 << width) - 1;              \
      __ mov(tmp_shift, i.InputRegister(1));              \
      __ and_(tmp_shift, Immediate(mask));                \
      __ Movd(tmp, tmp_shift);                            \
      __ opcode(dst, dst, tmp);                           \
    }                                                     \
  } while (false)

#define ASSEMBLE_SIMD_PINSR(OPCODE, CPU_FEATURE)             \
  do {                                                       \
    XMMRegister dst = i.OutputSimd128Register();             \
    XMMRegister src = i.InputSimd128Register(0);             \
    int8_t laneidx = i.InputInt8(1);                         \
    if (HasAddressingMode(instr)) {                          \
      if (CpuFeatures::IsSupported(AVX)) {                   \
        CpuFeatureScope avx_scope(masm(), AVX);              \
        __ v##OPCODE(dst, src, i.MemoryOperand(2), laneidx); \
      } else {                                               \
        DCHECK_EQ(dst, src);                                 \
        CpuFeatureScope sse_scope(masm(), CPU_FEATURE);      \
        __ OPCODE(dst, i.MemoryOperand(2), laneidx);         \
      }                                                      \
    } else {                                                 \
      if (CpuFeatures::IsSupported(AVX)) {                   \
        CpuFeatureScope avx_scope(masm(), AVX);              \
        __ v##OPCODE(dst, src, i.InputOperand(2), laneidx);  \
      } else {                                               \
        DCHECK_EQ(dst, src);                                 \
        CpuFeatureScope sse_scope(masm(), CPU_FEATURE);      \
        __ OPCODE(dst, i.InputOperand(2), laneidx);          \
      }                                                      \
    }                                                        \
  } while (false)

void CodeGenerator::AssembleDeconstructFrame() {
  __ mov(esp, ebp);
  __ pop(ebp);
}

void CodeGenerator::AssemblePrepareTailCall() {
  if (frame_access_state()->has_frame()) {
    __ mov(ebp, MemOperand(ebp, 0));
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
  if (stack_slot_delta > 0) {
    masm->AllocateStackSpace(stack_slot_delta * kSystemPointerSize);
    state->IncreaseSPDelta(stack_slot_delta);
  } else if (allow_shrinkage && stack_slot_delta < 0) {
    masm->add(esp, Immediate(-stack_slot_delta * kSystemPointerSize));
    state->IncreaseSPDelta(stack_slot_delta);
  }
}

#ifdef DEBUG
bool VerifyOutputOfAtomicPairInstr(IA32OperandConverter* converter,
                                   const Instruction* instr) {
  if (instr->OutputCount() == 2) {
    return (converter->OutputRegister(0) == eax &&
            converter->OutputRegister(1) == edx);
  }
  if (instr->OutputCount() == 1) {
    return (converter->OutputRegister(0) == eax &&
            converter->TempRegister(0) == edx) ||
           (converter->OutputRegister(0) == edx &&
            converter->TempRegister(0) == eax);
  }
  DCHECK_EQ(instr->OutputCount(), 0);
  return (converter->TempRegister(0) == eax &&
          converter->TempRegister(1) == edx);
}
#endif

}  // namespace

void CodeGenerator::AssembleTailCallBeforeGap(Instruction* instr,
                                              int first_unused_slot_offset) {
  CodeGenerator::PushTypeFlags flags(kImmediatePush | kScalarPush);
  ZoneVector<MoveOperands*> pushes(zone());
  GetPushCompatibleMoves(instr, flags, &pushes);

  if (!pushes.empty() &&
      (LocationOperand::cast(pushes.back()->destination()).index() + 1 ==
       first_unused_slot_offset)) {
    IA32OperandConverter g(this, instr);
    for (auto move : pushes) {
      LocationOperand destination_location(
          LocationOperand::cast(move->destination()));
      InstructionOperand source(move->source());
      AdjustStackPointerForTailCall(masm(), frame_access_state(),
                                    destination_location.index());
      if (source.IsStackSlot()) {
        LocationOperand source_location(LocationOperand::cast(source));
        __ push(g.SlotToOperand(source_location.index()));
      } else if (source.IsRegister()) {
        LocationOperand source_location(LocationOperand::cast(source));
        __ push(source_location.GetRegister());
      } else if (source.IsImmediate()) {
        __ Push(Immediate(ImmediateOperand::cast(source).inline_int32_value()));
      } else {
        // Pushes of non-scalar data types is not supported.
        UNIMPLEMENTED();
      }
      frame_access_state()->IncreaseSPDelta(1);
      move->Eliminate();
    }
  }
  AdjustStackPointerForTailCall(masm(), frame_access_state(),
                                first_unused_slot_offset, false);
}

void CodeGenerator::AssembleTailCallAfterGap(Instruction* instr,
                                             int first_unused_slot_offset) {
  AdjustStackPointerForTailCall(masm(), frame_access_state(),
                                first_unused_slot_offset);
}

// Check that {kJavaScriptCallCodeStartRegister} is correct.
void CodeGenerator::AssembleCodeStartRegisterCheck() {
  __ push(eax);  // Push eax so we can use it as a scratch register.
  __ ComputeCodeStartAddress(eax);
  __ cmp(eax, kJavaScriptCallCodeStartRegister);
  __ Assert(equal, AbortReason::kWrongFunctionCodeStart);
  __ pop(eax);  // Restore eax.
}

// Check if the code object is marked for deoptimization. If it is, then it
// jumps to the CompileLazyDeoptimizedCode builtin. In order to do this we need
// to:
//    1. read from memory the word that contains that bit, which can be found in
//       the flags in the referenced {Code} object;
//    2. test kMarkedForDeoptimizationBit in those flags; and
//    3. if it is not zero then it jumps to the builtin.
void CodeGenerator::BailoutIfDeoptimized() {
  int offset = InstructionStream::kCodeOffset - InstructionStream::kHeaderSize;
  __ push(eax);  // Push eax so we can use it as a scratch register.
  __ mov(eax, Operand(kJavaScriptCallCodeStartRegister, offset));
  __ test(FieldOperand(eax, Code::kFlagsOffset),
          Immediate(1 << Code::kMarkedForDeoptimizationBit));
  __ pop(eax);  // Restore eax.

  Label skip;
  __ j(zero, &skip, Label::kNear);
  __ TailCallBuiltin(Builtin::kCompileLazyDeoptimizedCode);
  __ bind(&skip);
}

// Assembles an instruction after register allocation, producing machine code.
CodeGenerator::CodeGenResult CodeGenerator::AssembleArchInstruction(
    Instruction* instr) {
  IA32OperandConverter i(this, instr);
  InstructionCode opcode = instr->opcode();
  ArchOpcode arch_opcode = ArchOpcodeField::decode(opcode);
  switch (arch_opcode) {
    case kArchCallCodeObject: {
      InstructionOperand* op = instr->InputAt(0);
      if (op->IsImmediate()) {
        Handle<Code> code = i.InputCode(0);
        __ Call(code, RelocInfo::CODE_TARGET);
      } else {
        Register reg = i.InputRegister(0);
        DCHECK_IMPLIES(
            instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ CallCodeObject(reg);
      }
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchCallBuiltinPointer: {
      DCHECK(!HasImmediateInput(instr, 0));
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
      if (HasImmediateInput(instr, 0)) {
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt32());
        if (DetermineStubCallMode() == StubCallMode::kCallWasmRuntimeStub) {
          __ wasm_call(wasm_code, constant.rmode());
        } else {
          __ call(wasm_code, constant.rmode());
        }
      } else {
        __ call(i.InputRegister(0));
      }
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchTailCallWasm: {
      if (HasImmediateInput(instr, 0)) {
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt32());
        __ jmp(wasm_code, constant.rmode());
      } else {
        __ jmp(i.InputRegister(0));
      }
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    case kArchTailCallCodeObject: {
      if (HasImmediateInput(instr, 0)) {
        Handle<Code> code = i.InputCode(0);
        __ Jump(code, RelocInfo::CODE_TARGET);
      } else {
        Register reg = i.InputRegister(0);
        DCHECK_IMPLIES(
            instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ JumpCodeObject(reg);
      }
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchTailCallAddress: {
      CHECK(!HasImmediateInput(instr, 0));
      Register reg = i.InputRegister(0);
      DCHECK_IMPLIES(
          instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
          reg == kJavaScriptCallCodeStartRegister);
      __ jmp(reg);
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchCallJSFunction: {
      Register func = i.InputRegister(0);
      if (v8_flags.debug_code) {
        // Check the function's context matches the context argument.
        __ cmp(esi, FieldOperand(func, JSFunction::kContextOffset));
        __ Assert(equal, AbortReason::kWrongFunctionContext);
      }
      __ CallJSFunction(func);
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchPrepareCallCFunction: {
      // Frame alignment requires using FP-relative frame addressing.
      frame_access_state()->SetFrameAccessToFP();
      int const num_gp_parameters = ParamField::decode(instr->opcode());
      int const num_fp_parameters = FPParamField::decode(instr->opcode());
      __ PrepareCallCFunction(num_gp_parameters + num_fp_parameters,
                              i.TempRegister(0));
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

      Label return_location;
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes;
#if V8_ENABLE_WEBASSEMBLY
      if (linkage()->GetIncomingDescriptor()->IsWasmCapiFunction()) {
        // Put the return address in a stack slot.
        Register scratch = eax;
        __ push(scratch);
        __ LoadLabelAddress(scratch, &return_location);
        __ mov(MemOperand(ebp, WasmExitFrameConstants::kCallingPCOffset),
               scratch);
        __ pop(scratch);
        set_isolate_data_slots = SetIsolateDataSlots::kNo;
      }
#endif  // V8_ENABLE_WEBASSEMBLY
      int pc_offset;
      if (HasImmediateInput(instr, 0)) {
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
      break;
    case kArchBinarySearchSwitch:
      AssembleArchBinarySearchSwitch(instr);
      break;
    case kArchTableSwitch:
      AssembleArchTableSwitch(instr);
      break;
    case kArchComment:
      __ RecordComment(reinterpret_cast<const char*>(i.InputInt32(0)),
                       SourceLocation());
      break;
    case kArchAbortCSADcheck:
      DCHECK(i.InputRegister(0) == edx);
      {
        // We don't actually want to generate a pile of code for this, so just
        // claim there is a stack frame, without generating one.
        FrameScope scope(masm(), StackFrame::NO_FRAME_TYPE);
        __ CallBuiltin(Builtin::kAbortCSADcheck);
      }
      __ int3();
      break;
    case kArchDebugBreak:
      __ DebugBreak();
      break;
    case kArchNop:
    case kArchThrowTerminator:
      // don't emit code for nops.
      break;
    case kArchDeoptimize: {
      DeoptimizationExit* exit =
          BuildTranslation(instr, -1, 0, 0, OutputFrameStateCombine::Ignore());
      __ jmp(exit->label());
      break;
    }
    case kArchRet:
      AssembleReturn(instr->InputAt(0));
      break;
    case kArchFramePointer:
      __ mov(i.OutputRegister(), ebp);
      break;
    case kArchParentFramePointer:
      if (frame_access_state()->has_frame()) {
        __ mov(i.OutputRegister(), Operand(ebp, 0));
      } else {
        __ mov(i.OutputRegister(), ebp);
      }
      break;
#if V8_ENABLE_WEBASSEMBLY
    case kArchStackPointer:
      __ mov(i.OutputRegister(), esp);
      break;
    case kArchSetStackPointer:
      if (instr->InputAt(0)->IsRegister()) {
        __ mov(esp, i.InputRegister(0));
      } else {
        __ mov(esp, i.InputOperand(0));
      }
      break;
#endif
    case kArchStackPointerGreaterThan: {
      // Potentially apply an offset to the current stack pointer before the
      // comparison to consider the size difference of an optimized frame versus
      // the contained unoptimized frames.
      Register lhs_register = esp;
      uint32_t offset;

      if (ShouldApplyOffsetToStackCheck(instr, &offset)) {
        lhs_register = i.TempRegister(0);
        __ lea(lhs_register, Operand(esp, -1 * static_cast<int32_t>(offset)));
      }

      constexpr size_t kValueIndex = 0;
      if (HasAddressingMode(instr)) {
        __ cmp(lhs_register, i.MemoryOperand(kValueIndex));
      } else {
        __ cmp(lhs_register, i.InputRegister(kValueIndex));
      }
      break;
    }
    case kArchStackCheckOffset:
      __ Move(i.OutputRegister(), Smi::FromInt(GetStackCheckOffset()));
      break;
    case kArchTruncateDoubleToI: {
      auto result = i.OutputRegister();
      auto input = i.InputDoubleRegister(0);
      auto ool = zone()->New<OutOfLineTruncateDoubleToI>(
          this, result, input, DetermineStubCallMode());
      __ cvttsd2si(result, Operand(input));
      __ cmp(result, 1);
      __ j(overflow, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kArchStoreWithWriteBarrier:  // Fall thrugh.
    case kArchAtomicStoreWithWriteBarrier: {
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      Register object = i.InputRegister(0);
      size_t index = 0;
      Operand operand = i.MemoryOperand(&index);
      Register value = i.InputRegister(index);
      Register scratch0 = i.TempRegister(0);
      Register scratch1 = i.TempRegister(1);

      if (v8_flags.debug_code) {
        // Checking that |value| is not a cleared weakref: our write barrier
        // does not support that for now.
        __ cmp(value, Immediate(kClearedWeakHeapObjectLower32));
        __ Check(not_equal, AbortReason::kOperandIsCleared);
      }

      auto ool = zone()->New<OutOfLineRecordWrite>(this, object, operand, value,
                                                   scratch0, scratch1, mode,
                                                   DetermineStubCallMode());
      if (arch_opcode == kArchStoreWithWriteBarrier) {
        __ mov(operand, value);
      } else {
        __ mov(scratch0, value);
        __ xchg(scratch0, operand);
      }
      if (mode > RecordWriteMode::kValueIsPointer) {
        __ JumpIfSmi(value, ool->exit());
      }
      __ CheckPageFlag(object, scratch0,
                       MemoryChunk::kPointersFromHereAreInterestingMask,
                       not_zero, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kArchStoreIndirectWithWriteBarrier:
      UNREACHABLE();
    case kArchStackSlot: {
      FrameOffset offset =
          frame_access_state()->GetFrameOffset(i.InputInt32(0));
      Register base = offset.from_stack_pointer() ? esp : ebp;
      __ lea(i.OutputRegister(), Operand(base, offset.offset()));
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
    case kIeee754Float64Expm1:
      ASSEMBLE_IEEE754_UNOP(expm1);
      break;
    case kIeee754Float64Exp:
      ASSEMBLE_IEEE754_UNOP(exp);
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
    case kIA32Add:
      ASSEMBLE_BINOP(add);
      break;
    case kIA32And:
      ASSEMBLE_BINOP(and_);
      break;
    case kIA32Cmp:
      ASSEMBLE_COMPARE(cmp);
      break;
    case kIA32Cmp16:
      ASSEMBLE_COMPARE(cmpw);
      break;
    case kIA32Cmp8:
      ASSEMBLE_COMPARE(cmpb);
      break;
    case kIA32Test:
      ASSEMBLE_COMPARE(test);
      break;
    case kIA32Test16:
      ASSEMBLE_COMPARE(test_w);
      break;
    case kIA32Test8:
      ASSEMBLE_COMPARE(test_b);
      break;
    case kIA32Imul:
      if (HasImmediateInput(instr, 1)) {
        __ imul(i.OutputRegister(), i.InputOperand(0), i.InputInt32(1));
      } else {
        __ imul(i.OutputRegister(), i.InputOperand(1));
      }
      break;
    case kIA32ImulHigh:
      __ imul(i.InputRegister(1));
      break;
    case kIA32UmulHigh:
      __ mul(i.InputRegister(1));
      break;
    case kIA32Idiv:
      __ cdq();
      __ idiv(i.InputOperand(1));
      break;
    case kIA32Udiv:
      __ Move(edx, Immediate(0));
      __ div(i.InputOperand(1));
      break;
    case kIA32Not:
      __ not_(i.OutputOperand());
      break;
    case kIA32Neg:
      __ neg(i.OutputOperand());
      break;
    case kIA32Or:
      ASSEMBLE_BINOP(or_);
      break;
    case kIA32Xor:
      ASSEMBLE_BINOP(xor_);
      break;
    case kIA32Sub:
      ASSEMBLE_BINOP(sub);
      break;
    case kIA32Shl:
      if (HasImmediateInput(instr, 1)) {
        __ shl(i.OutputOperand(), i.InputInt5(1));
      } else {
        __ shl_cl(i.OutputOperand());
      }
      break;
    case kIA32Shr:
      if (HasImmediateInput(instr, 1)) {
        __ shr(i.OutputOperand(), i.InputInt5(1));
      } else {
        __ shr_cl(i.OutputOperand());
      }
      break;
    case kIA32Sar:
      if (HasImmediateInput(instr, 1)) {
        __ sar(i.OutputOperand(), i.InputInt5(1));
      } else {
        __ sar_cl(i.OutputOperand());
      }
      break;
    case kIA32AddPair: {
      // i.OutputRegister(0) == i.InputRegister(0) ... left low word.
      // i.InputRegister(1) ... left high word.
      // i.InputRegister(2) ... right low word.
      // i.InputRegister(3) ... right high word.
      bool use_temp = false;
      if ((HasRegisterInput(instr, 1) &&
           i.OutputRegister(0).code() == i.InputRegister(1).code()) ||
          i.OutputRegister(0).code() == i.InputRegister(3).code()) {
        // We cannot write to the output register directly, because it would
        // overwrite an input for adc. We have to use the temp register.
        use_temp = true;
        __ Move(i.TempRegister(0), i.InputRegister(0));
        __ add(i.TempRegister(0), i.InputRegister(2));
      } else {
        __ add(i.OutputRegister(0), i.InputRegister(2));
      }
      i.MoveInstructionOperandToRegister(i.OutputRegister(1),
                                         instr->InputAt(1));
      __ adc(i.OutputRegister(1), Operand(i.InputRegister(3)));
      if (use_temp) {
        __ Move(i.OutputRegister(0), i.TempRegister(0));
      }
      break;
    }
    case kIA32SubPair: {
      // i.OutputRegister(0) == i.InputRegister(0) ... left low word.
      // i.InputRegister(1) ... left high word.
      // i.InputRegister(2) ... right low word.
      // i.InputRegister(3) ... right high word.
      bool use_temp = false;
      if ((HasRegisterInput(instr, 1) &&
           i.OutputRegister(0).code() == i.InputRegister(1).code()) ||
          i.OutputRegister(0).code() == i.InputRegister(3).code()) {
        // We cannot write to the output register directly, because it would
        // overwrite an input for adc. We have to use the temp register.
        use_temp = true;
        __ Move(i.TempRegister(0), i.InputRegister(0));
        __ sub(i.TempRegister(0), i.InputRegister(2));
      } else {
        __ sub(i.OutputRegister(0), i.InputRegister(2));
      }
      i.MoveInstructionOperandToRegister(i.OutputRegister(1),
                                         instr->InputAt(1));
      __ sbb(i.OutputRegister(1), Operand(i.InputRegister(3)));
      if (use_temp) {
        __ Move(i.OutputRegister(0), i.TempRegister(0));
      }
      break;
    }
    case kIA32MulPair: {
      __ imul(i.OutputRegister(1), i.InputOperand(0));
      i.MoveInstructionOperandToRegister(i.TempRegister(0), instr->InputAt(1));
      __ imul(i.TempRegister(0), i.InputOperand(2));
      __ add(i.OutputRegister(1), i.TempRegister(0));
      __ mov(i.OutputRegister(0), i.InputOperand(0));
      // Multiplies the low words and stores them in eax and edx.
      __ mul(i.InputRegister(2));
      __ add(i.OutputRegister(1), i.TempRegister(0));

      break;
    }
    case kIA32ShlPair:
      if (HasImmediateInput(instr, 2)) {
        __ ShlPair(i.InputRegister(1), i.InputRegister(0), i.InputInt6(2));
      } else {
        // Shift has been loaded into CL by the register allocator.
        __ ShlPair_cl(i.InputRegister(1), i.InputRegister(0));
      }
      break;
    case kIA32ShrPair:
      if (HasImmediateInput(instr, 2)) {
        __ ShrPair(i.InputRegister(1), i.InputRegister(0), i.InputInt6(2));
      } else {
        // Shift has been loaded into CL by the register allocator.
        __ ShrPair_cl(i.InputRegister(1), i.InputRegister(0));
      }
      break;
    case kIA32SarPair:
      if (HasImmediateInput(instr, 2)) {
        __ SarPair(i.InputRegister(1), i.InputRegister(0), i.InputInt6(2));
      } else {
        // Shift has been loaded into CL by the register allocator.
        __ SarPair_cl(i.InputRegister(1), i.InputRegister(0));
      }
      break;
    case kIA32Rol:
      if (HasImmediateInput(instr, 1)) {
        __ rol(i.OutputOperand(), i.InputInt5(1));
      } else {
        __ rol_cl(i.OutputOperand());
      }
      break;
    case kIA32Ror:
      if (HasImmediateInput(instr, 1)) {
        __ ror(i.OutputOperand(), i.InputInt5(1));
      } else {
        __ ror_cl(i.OutputOperand());
      }
      break;
    case kIA32Lzcnt:
      __ Lzcnt(i.OutputRegister(), i.InputOperand(0));
      break;
    case kIA32Tzcnt:
      __ Tzcnt(i.OutputRegister(), i.InputOperand(0));
      break;
    case kIA32Popcnt:
      __ Popcnt(i.OutputRegister(), i.InputOperand(0));
      break;
    case kIA32Bswap:
      __ bswap(i.OutputRegister());
      break;
    case kIA32MFence:
      __ mfence();
      break;
    case kIA32LFence:
      __ lfence();
      break;
    case kIA32Float32Cmp:
      __ Ucomiss(i.InputDoubleRegister(0), i.InputOperand(1));
      break;
    case kIA32Float32Sqrt:
      __ Sqrtss(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kIA32Float32Round: {
      CpuFeatureScope sse_scope(masm(), SSE4_1);
      RoundingMode const mode =
          static_cast<RoundingMode>(MiscField::decode(instr->opcode()));
      __ Roundss(i.OutputDoubleRegister(), i.InputDoubleRegister(0), mode);
      break;
    }
    case kIA32Float64Cmp:
      __ Ucomisd(i.InputDoubleRegister(0), i.InputOperand(1));
      break;
    case kIA32Float32Max: {
      Label compare_swap, done_compare;
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Ucomiss(i.InputDoubleRegister(0), i.InputDoubleRegister(1));
      } else {
        __ Ucomiss(i.InputDoubleRegister(0), i.InputOperand(1));
      }
      auto ool =
          zone()->New<OutOfLineLoadFloat32NaN>(this, i.OutputDoubleRegister());
      __ j(parity_even, ool->entry());
      __ j(above, &done_compare, Label::kNear);
      __ j(below, &compare_swap, Label::kNear);
      __ Movmskps(i.TempRegister(0), i.InputDoubleRegister(0));
      __ test(i.TempRegister(0), Immediate(1));
      __ j(zero, &done_compare, Label::kNear);
      __ bind(&compare_swap);
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Movss(i.InputDoubleRegister(0), i.InputDoubleRegister(1));
      } else {
        __ Movss(i.InputDoubleRegister(0), i.InputOperand(1));
      }
      __ bind(&done_compare);
      __ bind(ool->exit());
      break;
    }

    case kIA32Float64Max: {
      Label compare_swap, done_compare;
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Ucomisd(i.InputDoubleRegister(0), i.InputDoubleRegister(1));
      } else {
        __ Ucomisd(i.InputDoubleRegister(0), i.InputOperand(1));
      }
      auto ool =
          zone()->New<OutOfLineLoadFloat64NaN>(this, i.OutputDoubleRegister());
      __ j(parity_even, ool->entry());
      __ j(above, &done_compare, Label::kNear);
      __ j(below, &compare_swap, Label::kNear);
      __ Movmskpd(i.TempRegister(0), i.InputDoubleRegister(0));
      __ test(i.TempRegister(0), Immediate(1));
      __ j(zero, &done_compare, Label::kNear);
      __ bind(&compare_swap);
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Movsd(i.InputDoubleRegister(0), i.InputDoubleRegister(1));
      } else {
        __ Movsd(i.InputDoubleRegister(0), i.InputOperand(1));
      }
      __ bind(&done_compare);
      __ bind(ool->exit());
      break;
    }
    case kIA32Float32Min: {
      Label compare_swap, done_compare;
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Ucomiss(i.InputDoubleRegister(0), i.InputDoubleRegister(1));
      } else {
        __ Ucomiss(i.InputDoubleRegister(0), i.InputOperand(1));
      }
      auto ool =
          zone()->New<OutOfLineLoadFloat32NaN>(this, i.OutputDoubleRegister());
      __ j(parity_even, ool->entry());
      __ j(below, &done_compare, Label::kNear);
      __ j(above, &compare_swap, Label::kNear);
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Movmskps(i.TempRegister(0), i.InputDoubleRegister(1));
      } else {
        __ Movss(kScratchDoubleReg, i.InputOperand(1));
        __ Movmskps(i.TempRegister(0), kScratchDoubleReg);
      }
      __ test(i.TempRegister(0), Immediate(1));
      __ j(zero, &done_compare, Label::kNear);
      __ bind(&compare_swap);
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Movss(i.InputDoubleRegister(0), i.InputDoubleRegister(1));
      } else {
        __ Movss(i.InputDoubleRegister(0), i.InputOperand(1));
      }
      __ bind(&done_compare);
      __ bind(ool->exit());
      break;
    }
    case kIA32Float64Min: {
      Label compare_swap, done_compare;
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Ucomisd(i.InputDoubleRegister(0), i.InputDoubleRegister(1));
      } else {
        __ Ucomisd(i.InputDoubleRegister(0), i.InputOperand(1));
      }
      auto ool =
          zone()->New<OutOfLineLoadFloat64NaN>(this, i.OutputDoubleRegister());
      __ j(parity_even, ool->entry());
      __ j(below, &done_compare, Label::kNear);
      __ j(above, &compare_swap, Label::kNear);
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Movmskpd(i.TempRegister(0), i.InputDoubleRegister(1));
      } else {
        __ Movsd(kScratchDoubleReg, i.InputOperand(1));
        __ Movmskpd(i.TempRegister(0), kScratchDoubleReg);
      }
      __ test(i.TempRegister(0), Immediate(1));
      __ j(zero, &done_compare, Label::kNear);
      __ bind(&compare_swap);
      if (instr->InputAt(1)->IsFPRegister()) {
        __ Movsd(i.InputDoubleRegister(0), i.InputDoubleRegister(1));
      } else {
        __ Movsd(i.InputDoubleRegister(0), i.InputOperand(1));
      }
      __ bind(&done_compare);
      __ bind(ool->exit());
      break;
    }
    case kIA32Float64Mod: {
      Register tmp = i.TempRegister(1);
      __ mov(tmp, esp);
      __ AllocateStackSpace(kDoubleSize);
      __ and_(esp, -8);  // align to 8 byte boundary.
      // Move values to st(0) and st(1).
      __ Movsd(Operand(esp, 0), i.InputDoubleRegister(1));
      __ fld_d(Operand(esp, 0));
      __ Movsd(Operand(esp, 0), i.InputDoubleRegister(0));
      __ fld_d(Operand(esp, 0));
      // Loop while fprem isn't done.
      Label mod_loop;
      __ bind(&mod_loop);
      // This instruction traps on all kinds of inputs, but we are assuming the
      // floating point control word is set to ignore them all.
      __ fprem();
      // fnstsw_ax clobbers eax.
      DCHECK_EQ(eax, i.TempRegister(0));
      __ fnstsw_ax();
      __ sahf();
      __ j(parity_even, &mod_loop);
      // Move output to stack and clean up.
      __ fstp(1);
      __ fstp_d(Operand(esp, 0));
      __ Movsd(i.OutputDoubleRegister(), Operand(esp, 0));
      __ mov(esp, tmp);
      break;
    }
    case kIA32Float64Sqrt:
      __ Sqrtsd(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kIA32Float64Round: {
      RoundingMode const mode =
          static_cast<RoundingMode>(MiscField::decode(instr->opcode()));
      __ Roundsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0), mode);
      break;
    }
    case kIA32Float32ToFloat64:
      __ Cvtss2sd(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kIA32Float64ToFloat32:
      __ Cvtsd2ss(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kIA32Float32ToInt32:
      __ Cvttss2si(i.OutputRegister(), i.InputOperand(0));
      break;
    case kIA32Float32ToUint32:
      __ Cvttss2ui(i.OutputRegister(), i.InputOperand(0),
                   i.TempSimd128Register(0));
      break;
    case kIA32Float64ToInt32:
      __ Cvttsd2si(i.OutputRegister(), i.InputOperand(0));
      break;
    case kIA32Float64ToUint32:
      __ Cvttsd2ui(i.OutputRegister(), i.InputOperand(0),
                   i.TempSimd128Register(0));
      break;
    case kSSEInt32ToFloat32:
      // Calling Cvtsi2ss (which does a xor) regresses some benchmarks.
      __ cvtsi2ss(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kIA32Uint32ToFloat32:
      __ Cvtui2ss(i.OutputDoubleRegister(), i.InputOperand(0),
                  i.TempRegister(0));
      break;
    case kSSEInt32ToFloat64:
      // Calling Cvtsi2sd (which does a xor) regresses some benchmarks.
      __ cvtsi2sd(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kIA32Uint32ToFloat64:
      __ Cvtui2sd(i.OutputDoubleRegister(), i.InputOperand(0),
                  i.TempRegister(0));
      break;
    case kIA32Float64ExtractLowWord32:
      if (instr->InputAt(0)->IsFPStackSlot()) {
        __ mov(i.OutputRegister(), i.InputOperand(0));
      } else {
        __ Movd(i.OutputRegister(), i.InputDoubleRegister(0));
      }
      break;
    case kIA32Float64ExtractHighWord32:
      if (instr->InputAt(0)->IsFPStackSlot()) {
        __ mov(i.OutputRegister(), i.InputOperand(0, kDoubleSize / 2));
      } else {
        __ Pextrd(i.OutputRegister(), i.InputDoubleRegister(0), 1);
      }
      break;
    case kIA32Float64InsertLowWord32:
      __ Pinsrd(i.OutputDoubleRegister(), i.InputOperand(1), 0);
      break;
    case kIA32Float64InsertHighWord32:
      __ Pinsrd(i.OutputDoubleRegister(), i.InputOperand(1), 1);
      break;
    case kIA32Float64FromWord32Pair:
      __ Pinsrd(i.OutputDoubleRegister(), i.InputOperand(0), 0);
      __ Pinsrd(i.OutputDoubleRegister(), i.InputOperand(1), 1);
      break;
    case kIA32Float64LoadLowWord32:
      __ Movd(i.OutputDoubleRegister(), i.InputOperand(0));
      break;
    case kFloat32Add: {
      __ Addss(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      break;
    }
    case kFloat32Sub: {
      __ Subss(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      break;
    }
    case kFloat32Mul: {
      __ Mulss(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      break;
    }
    case kFloat32Div: {
      __ Divss(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      // Don't delete this mov. It may improve performance on some CPUs,
      // when there is a (v)mulss depending on the result.
      __ movaps(i.OutputDoubleRegister(), i.OutputDoubleRegister());
      break;
    }
    case kFloat64Add: {
      __ Addsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      break;
    }
    case kFloat64Sub: {
      __ Subsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      break;
    }
    case kFloat64Mul: {
      __ Mulsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      break;
    }
    case kFloat64Div: {
      __ Divsd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      // Don't delete this mov. It may improve performance on some CPUs,
      // when there is a (v)mulsd depending on the result.
      __ movaps(i.OutputDoubleRegister(), i.OutputDoubleRegister());
      break;
    }
    case kFloat32Abs: {
      __ Absps(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.TempRegister(0));
      break;
    }
    case kFloat32Neg: {
      __ Negps(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.TempRegister(0));
      break;
    }
    case kFloat64Abs: {
      __ Abspd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.TempRegister(0));
      break;
    }
    case kFloat64Neg: {
      __ Negpd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.TempRegister(0));
      break;
    }
    case kIA32Float64SilenceNaN:
      __ Xorps(kScratchDoubleReg, kScratchDoubleReg);
      __ Subsd(i.InputDoubleRegister(0), kScratchDoubleReg);
      break;
    case kIA32Movsxbl:
      ASSEMBLE_MOVX(movsx_b);
      break;
    case kIA32Movzxbl:
      ASSEMBLE_MOVX(movzx_b);
      break;
    case kIA32Movb: {
      size_t index = 0;
      Operand operand = i.MemoryOperand(&index);
      if (HasImmediateInput(instr, index)) {
        __ mov_b(operand, i.InputInt8(index));
      } else {
        __ mov_b(operand, i.InputRegister(index));
      }
      break;
    }
    case kIA32Movsxwl:
      ASSEMBLE_MOVX(movsx_w);
      break;
    case kIA32Movzxwl:
      ASSEMBLE_MOVX(movzx_w);
      break;
    case kIA32Movw: {
      size_t index = 0;
      Operand operand = i.MemoryOperand(&index);
      if (HasImmediateInput(instr, index)) {
        __ mov_w(operand, i.InputInt16(index));
      } else {
        __ mov_w(operand, i.InputRegister(index));
      }
      break;
    }
    case kIA32Movl:
      if (instr->HasOutput()) {
        __ mov(i.OutputRegister(), i.MemoryOperand());
      } else {
        size_t index = 0;
        Operand operand = i.MemoryOperand(&index);
        if (HasImmediateInput(instr, index)) {
          __ Move(operand, i.InputImmediate(index));
        } else {
          __ mov(operand, i.InputRegister(index));
        }
      }
      break;
    case kIA32Movsd:
      if (instr->HasOutput()) {
        __ Movsd(i.OutputDoubleRegister(), i.MemoryOperand());
      } else {
        size_t index = 0;
        Operand operand = i.MemoryOperand(&index);
        __ Movsd(operand, i.InputDoubleRegister(index));
      }
      break;
    case kIA32Movss:
      if (instr->HasOutput()) {
        __ Movss(i.OutputDoubleRegister(), i.MemoryOperand());
      } else {
        size_t index = 0;
        Operand operand = i.MemoryOperand(&index);
        __ Movss(operand, i.InputDoubleRegister(index));
      }
      break;
    case kIA32Movdqu:
      if (instr->HasOutput()) {
        __ Movdqu(i.OutputSimd128Register(), i.MemoryOperand());
      } else {
        size_t index = 0;
        Operand operand = i.MemoryOperand(&index);
        __ Movdqu(operand, i.InputSimd128Register(index));
      }
      break;
    case kIA32BitcastFI:
      if (instr->InputAt(0)->IsFPStackSlot()) {
        __ mov(i.OutputRegister(), i.InputOperand(0));
      } else {
        __ Movd(i.OutputRegister(), i.InputDoubleRegister(0));
      }
      break;
    case kIA32BitcastIF:
      if (HasRegisterInput(instr, 0)) {
        __ Movd(i.OutputDoubleRegister(), i.InputRegister(0));
      } else {
        __ Movss(i.OutputDoubleRegister(), i.InputOperand(0));
      }
      break;
    case kIA32Lea: {
      AddressingMode mode = AddressingModeField::decode(instr->opcode());
      // Shorten "leal" to "addl", "subl" or "shll" if the register allocation
      // and addressing mode just happens to work out. The "addl"/"subl" forms
      // in these cases are faster based on measurements.
      if (mode == kMode_MI) {
        __ Move(i.OutputRegister(), Immediate(i.InputInt32(0)));
      } else if (i.InputRegister(0) == i.OutputRegister()) {
        if (mode == kMode_MRI) {
          int32_t constant_summand = i.InputInt32(1);
          if (constant_summand > 0) {
            __ add(i.OutputRegister(), Immediate(constant_summand));
          } else if (constant_summand < 0) {
            __ sub(i.OutputRegister(),
                   Immediate(base::NegateWithWraparound(constant_summand)));
          }
        } else if (mode == kMode_MR1) {
          if (i.InputRegister(1) == i.OutputRegister()) {
            __ shl(i.OutputRegister(), 1);
          } else {
            __ add(i.OutputRegister(), i.InputRegister(1));
          }
        } else if (mode == kMode_M2) {
          __ shl(i.OutputRegister(), 1);
        } else if (mode == kMode_M4) {
          __ shl(i.OutputRegister(), 2);
        } else if (mode == kMode_M8) {
          __ shl(i.OutputRegister(), 3);
        } else {
          __ lea(i.OutputRegister(), i.MemoryOperand());
        }
      } else if (mode == kMode_MR1 &&
                 i.InputRegister(1) == i.OutputRegister()) {
        __ add(i.OutputRegister(), i.InputRegister(0));
      } else {
        __ lea(i.OutputRegister(), i.MemoryOperand());
      }
      break;
    }
    case kIA32Push: {
      int stack_decrement = i.InputInt32(0);
      int slots = stack_decrement / kSystemPointerSize;
      // Whenever codegen uses push, we need to check if stack_decrement
      // contains any extra padding and adjust the stack before the push.
      if (HasAddressingMode(instr)) {
        // Only single slot pushes from memory are supported.
        __ AllocateStackSpace(stack_decrement - kSystemPointerSize);
        size_t index = 1;
        Operand operand = i.MemoryOperand(&index);
        __ push(operand);
      } else if (HasImmediateInput(instr, 1)) {
        __ AllocateStackSpace(stack_decrement - kSystemPointerSize);
        __ push(i.InputImmediate(1));
      } else {
        InstructionOperand* input = instr->InputAt(1);
        if (input->IsRegister()) {
          __ AllocateStackSpace(stack_decrement - kSystemPointerSize);
          __ push(i.InputRegister(1));
        } else if (input->IsFloatRegister()) {
          DCHECK_GE(stack_decrement, kFloatSize);
          __ AllocateStackSpace(stack_decrement);
          __ Movss(Operand(esp, 0), i.InputDoubleRegister(1));
        } else if (input->IsDoubleRegister()) {
          DCHECK_GE(stack_decrement, kDoubleSize);
          __ AllocateStackSpace(stack_decrement);
          __ Movsd(Operand(esp, 0), i.InputDoubleRegister(1));
        } else if (input->IsSimd128Register()) {
          DCHECK_GE(stack_decrement, kSimd128Size);
          __ AllocateStackSpace(stack_decrement);
          // TODO(bbudge) Use Movaps when slots are aligned.
          __ Movups(Operand(esp, 0), i.InputSimd128Register(1));
        } else if (input->IsStackSlot() || input->IsFloatStackSlot()) {
          __ AllocateStackSpace(stack_decrement - kSystemPointerSize);
          __ push(i.InputOperand(1));
        } else if (input->IsDoubleStackSlot()) {
          DCHECK_GE(stack_decrement, kDoubleSize);
          __ Movsd(kScratchDoubleReg, i.InputOperand(1));
          __ AllocateStackSpace(stack_decrement);
          __ Movsd(Operand(esp, 0), kScratchDoubleReg);
        } else {
          DCHECK(input->IsSimd128StackSlot());
          DCHECK_GE(stack_decrement, kSimd128Size);
          // TODO(bbudge) Use Movaps when slots are aligned.
          __ Movups(kScratchDoubleReg, i.InputOperand(1));
          __ AllocateStackSpace(stack_decrement);
          __ Movups(Operand(esp, 0), kScratchDoubleReg);
        }
      }
      frame_access_state()->IncreaseSPDelta(slots);
      break;
    }
    case kIA32Poke: {
      int slot = MiscField::decode(instr->opcode());
      if (HasImmediateInput(instr, 0)) {
        __ mov(Operand(esp, slot * kSystemPointerSize), i.InputImmediate(0));
      } else {
        __ mov(Operand(esp, slot * kSystemPointerSize), i.InputRegister(0));
      }
      break;
    }
    case kIA32Peek: {
      int reverse_slot = i.InputInt32(0);
      int offset =
          FrameSlotToFPOffset(frame()->GetTotalFrameSlotCount() - reverse_slot);
      if (instr->OutputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->OutputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ Movsd(i.OutputDoubleRegister(), Operand(ebp, offset));
        } else if (op->representation() == MachineRepresentation::kFloat32) {
          __ Movss(i.OutputFloatRegister(), Operand(ebp, offset));
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
          __ Movdqu(i.OutputSimd128Register(), Operand(ebp, offset));
        }
      } else {
        __ mov(i.OutputRegister(), Operand(ebp, offset));
      }
      break;
    }
    case kIA32F64x2Splat: {
      __ Movddup(i.OutputSimd128Register(), i.InputDoubleRegister(0));
      break;
    }
    case kIA32F64x2ExtractLane: {
      __ F64x2ExtractLane(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                          i.InputUint8(1));
      break;
    }
    case kIA32F64x2ReplaceLane: {
      __ F64x2ReplaceLane(i.OutputSimd128Register(), i.InputSimd128Register(0),
                          i.InputDoubleRegister(2), i.InputInt8(1));
      break;
    }
    case kIA32F64x2Sqrt: {
      __ Sqrtpd(i.OutputSimd128Register(), i.InputOperand(0));
      break;
    }
    case kIA32F64x2Add: {
      __ Addpd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      break;
    }
    case kIA32F64x2Sub: {
      __ Subpd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      break;
    }
    case kIA32F64x2Mul: {
      __ Mulpd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      break;
    }
    case kIA32F64x2Div: {
      __ Divpd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputOperand(1));
      break;
    }
    case kIA32F64x2Min: {
      __ F64x2Min(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kIA32F64x2Max: {
      __ F64x2Max(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kIA32F64x2Eq: {
      __ Cmpeqpd(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32F64x2Ne: {
      __ Cmpneqpd(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputOperand(1));
      break;
    }
    case kIA32F64x2Lt: {
      __ Cmpltpd(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32F64x2Le: {
      __ Cmplepd(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32F64x2Qfma: {
      __ F64x2Qfma(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1), i.InputSimd128Register(2),
                   kScratchDoubleReg);
      break;
    }
    case kIA32F64x2Qfms: {
      __ F64x2Qfms(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1), i.InputSimd128Register(2),
                   kScratchDoubleReg);
      break;
    }
    case kIA32Minpd: {
      __ Minpd(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kIA32Maxpd: {
      __ Maxpd(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kIA32F64x2Round: {
      RoundingMode const mode =
          static_cast<RoundingMode>(MiscField::decode(instr->opcode()));
      __ Roundpd(i.OutputSimd128Register(), i.InputDoubleRegister(0), mode);
      break;
    }
    case kIA32F64x2PromoteLowF32x4: {
      if (HasAddressingMode(instr)) {
        __ Cvtps2pd(i.OutputSimd128Register(), i.MemoryOperand());
      } else {
        __ Cvtps2pd(i.OutputSimd128Register(), i.InputSimd128Register(0));
      }
      break;
    }
    case kIA32F32x4DemoteF64x2Zero: {
      __ Cvtpd2ps(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kIA32I32x4TruncSatF64x2SZero: {
      __ I32x4TruncSatF64x2SZero(i.OutputSimd128Register(),
                                 i.InputSimd128Register(0), kScratchDoubleReg,
                                 i.TempRegister(0));
      break;
    }
    case kIA32I32x4TruncSatF64x2UZero: {
      __ I32x4TruncSatF64x2UZero(i.OutputSimd128Register(),
                                 i.InputSimd128Register(0), kScratchDoubleReg,
                                 i.TempRegister(0));
      break;
    }
    case kIA32F64x2ConvertLowI32x4S: {
      __ Cvtdq2pd(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kIA32F64x2ConvertLowI32x4U: {
      __ F64x2ConvertLowI32x4U(i.OutputSimd128Register(),
                               i.InputSimd128Register(0), i.TempRegister(0));
      break;
    }
    case kIA32I64x2ExtMulLowI32x4S: {
      __ I64x2ExtMul(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputSimd128Register(1), kScratchDoubleReg,
                     /*low=*/true, /*is_signed=*/true);
      break;
    }
    case kIA32I64x2ExtMulHighI32x4S: {
      __ I64x2ExtMul(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputSimd128Register(1), kScratchDoubleReg,
                     /*low=*/false, /*is_signed=*/true);
      break;
    }
    case kIA32I64x2ExtMulLowI32x4U: {
      __ I64x2ExtMul(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputSimd128Register(1), kScratchDoubleReg,
                     /*low=*/true, /*is_signed=*/false);
      break;
    }
    case kIA32I64x2ExtMulHighI32x4U: {
      __ I64x2ExtMul(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputSimd128Register(1), kScratchDoubleReg,
                     /*low=*/false, /*is_signed=*/false);
      break;
    }
    case kIA32I32x4ExtMulLowI16x8S: {
      __ I32x4ExtMul(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputSimd128Register(1), kScratchDoubleReg,
                     /*low=*/true, /*is_signed=*/true);
      break;
    }
    case kIA32I32x4ExtMulHighI16x8S: {
      __ I32x4ExtMul(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputSimd128Register(1), kScratchDoubleReg,
                     /*low=*/false, /*is_signed=*/true);
      break;
    }
    case kIA32I32x4ExtMulLowI16x8U: {
      __ I32x4ExtMul(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputSimd128Register(1), kScratchDoubleReg,
                     /*low=*/true, /*is_signed=*/false);
      break;
    }
    case kIA32I32x4ExtMulHighI16x8U: {
      __ I32x4ExtMul(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputSimd128Register(1), kScratchDoubleReg,
                     /*low=*/false, /*is_signed=*/false);
      break;
    }
    case kIA32I16x8ExtMulLowI8x16S: {
      __ I16x8ExtMulLow(i.OutputSimd128Register(), i.InputSimd128Register(0),
                        i.InputSimd128Register(1), kScratchDoubleReg,
                        /*is_signed=*/true);
      break;
    }
    case kIA32I16x8ExtMulHighI8x16S: {
      __ I16x8ExtMulHighS(i.OutputSimd128Register(), i.InputSimd128Register(0),
                          i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kIA32I16x8ExtMulLowI8x16U: {
      __ I16x8ExtMulLow(i.OutputSimd128Register(), i.InputSimd128Register(0),
                        i.InputSimd128Register(1), kScratchDoubleReg,
                        /*is_signed=*/false);
      break;
    }
    case kIA32I16x8ExtMulHighI8x16U: {
      __ I16x8ExtMulHighU(i.OutputSimd128Register(), i.InputSimd128Register(0),
                          i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kIA32I64x2SplatI32Pair: {
      XMMRegister dst = i.OutputSimd128Register();
      __ Pinsrd(dst, i.InputRegister(0), 0);
      __ Pinsrd(dst, i.InputOperand(1), 1);
      __ Pshufd(dst, dst, uint8_t{0x44});
      break;
    }
    case kIA32I64x2ReplaceLaneI32Pair: {
      int8_t lane = i.InputInt8(1);
      __ Pinsrd(i.OutputSimd128Register(), i.InputOperand(2), lane * 2);
      __ Pinsrd(i.OutputSimd128Register(), i.InputOperand(3), lane * 2 + 1);
      break;
    }
    case kIA32I64x2Abs: {
      __ I64x2Abs(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  kScratchDoubleReg);
      break;
    }
    case kIA32I64x2Neg: {
      __ I64x2Neg(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  kScratchDoubleReg);
      break;
    }
    case kIA32I64x2Shl: {
      ASSEMBLE_SIMD_SHIFT(Psllq, 6);
      break;
    }
    case kIA32I64x2ShrS: {
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src = i.InputSimd128Register(0);
      if (HasImmediateInput(instr, 1)) {
        __ I64x2ShrS(dst, src, i.InputInt6(1), kScratchDoubleReg);
      } else {
        __ I64x2ShrS(dst, src, i.InputRegister(1), kScratchDoubleReg,
                     i.TempSimd128Register(0), i.TempRegister(1));
      }
      break;
    }
    case kIA32I64x2Add: {
      __ Paddq(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32I64x2Sub: {
      __ Psubq(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32I64x2Mul: {
      __ I64x2Mul(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), i.TempSimd128Register(0),
                  i.TempSimd128Register(1));
      break;
    }
    case kIA32I64x2ShrU: {
      ASSEMBLE_SIMD_SHIFT(Psrlq, 6);
      break;
    }
    case kIA32I64x2BitMask: {
      __ Movmskpd(i.OutputRegister(), i.InputSimd128Register(0));
      break;
    }
    case kIA32I64x2Eq: {
      __ Pcmpeqq(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32I64x2Ne: {
      __ Pcmpeqq(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      __ Pcmpeqq(kScratchDoubleReg, kScratchDoubleReg, kScratchDoubleReg);
      __ Pxor(i.OutputSimd128Register(), kScratchDoubleReg);
      break;
    }
    case kIA32I64x2GtS: {
      __ I64x2GtS(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kIA32I64x2GeS: {
      __ I64x2GeS(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kIA32I64x2SConvertI32x4Low: {
      __ Pmovsxdq(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kIA32I64x2SConvertI32x4High: {
      __ I64x2SConvertI32x4High(i.OutputSimd128Register(),
                                i.InputSimd128Register(0));
      break;
    }
    case kIA32I64x2UConvertI32x4Low: {
      __ Pmovzxdq(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kIA32I64x2UConvertI32x4High: {
      __ I64x2UConvertI32x4High(i.OutputSimd128Register(),
                                i.InputSimd128Register(0), kScratchDoubleReg);
      break;
    }
    case kIA32I32x4ExtAddPairwiseI16x8S: {
      __ I32x4ExtAddPairwiseI16x8S(i.OutputSimd128Register(),
                                   i.InputSimd128Register(0),
                                   i.TempRegister(0));
      break;
    }
    case kIA32I32x4ExtAddPairwiseI16x8U: {
      __ I32x4ExtAddPairwiseI16x8U(i.OutputSimd128Register(),
                                   i.InputSimd128Register(0),
                                   kScratchDoubleReg);
      break;
    }
    case kIA32I16x8ExtAddPairwiseI8x16S: {
      __ I16x8ExtAddPairwiseI8x16S(i.OutputSimd128Register(),
                                   i.InputSimd128Register(0), kScratchDoubleReg,
                                   i.TempRegister(0));
      break;
    }
    case kIA32I16x8ExtAddPairwiseI8x16U: {
      __ I16x8ExtAddPairwiseI8x16U(i.OutputSimd128Register(),
                                   i.InputSimd128Register(0),
                                   i.TempRegister(0));
      break;
    }
    case kIA32I16x8Q15MulRSatS: {
      __ I16x8Q15MulRSatS(i.OutputSimd128Register(), i.InputSimd128Register(0),
                          i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kIA32I16x8RelaxedQ15MulRS: {
      __ Pmulhrsw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kIA32I16x8DotI8x16I7x16S: {
      __ I16x8DotI8x16I7x16S(i.OutputSimd128Register(),
                             i.InputSimd128Register(0),
                             i.InputSimd128Register(1));
      break;
    }
    case kIA32I32x4DotI8x16I7x16AddS: {
      __ I32x4DotI8x16I7x16AddS(
          i.OutputSimd128Register(), i.InputSimd128Register(0),
          i.InputSimd128Register(1), i.InputSimd128Register(2),
          kScratchDoubleReg, i.TempSimd128Register(0));
      break;
    }
    case kIA32F32x4Splat: {
      __ F32x4Splat(i.OutputSimd128Register(), i.InputDoubleRegister(0));
      break;
    }
    case kIA32F32x4ExtractLane: {
      __ F32x4ExtractLane(i.OutputFloatRegister(), i.InputSimd128Register(0),
                          i.InputUint8(1));
      break;
    }
    case kIA32Insertps: {
      if (CpuFeatures::IsSupported(AVX)) {
        CpuFeatureScope avx_scope(masm(), AVX);
        __ vinsertps(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputOperand(2), i.InputInt8(1) << 4);
      } else {
        DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
        CpuFeatureScope sse_scope(masm(), SSE4_1);
        __ insertps(i.OutputSimd128Register(), i.InputOperand(2),
                    i.InputInt8(1) << 4);
      }
      break;
    }
    case kIA32F32x4SConvertI32x4: {
      __ Cvtdq2ps(i.OutputSimd128Register(), i.InputOperand(0));
      break;
    }
    case kIA32F32x4UConvertI32x4: {
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src = i.InputSimd128Register(0);
      __ Pxor(kScratchDoubleReg, kScratchDoubleReg);      // zeros
      __ Pblendw(kScratchDoubleReg, src, uint8_t{0x55});  // get lo 16 bits
      __ Psubd(dst, src, kScratchDoubleReg);              // get hi 16 bits
      __ Cvtdq2ps(kScratchDoubleReg, kScratchDoubleReg);  // convert lo exactly
      __ Psrld(dst, dst, uint8_t{1});  // divide by 2 to get in unsigned range
      __ Cvtdq2ps(dst, dst);    // convert hi exactly
      __ Addps(dst, dst, dst);  // double hi, exactly
      __ Addps(dst, dst, kScratchDoubleReg);  // add hi and lo, may round.
      break;
    }
    case kIA32F32x4Sqrt: {
      __ Sqrtps(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kIA32F32x4Add: {
      __ Addps(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    };
    case kIA32F32x4Sub: {
      __ Subps(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32F32x4Mul: {
      __ Mulps(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32F32x4Div: {
      __ Divps(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32F32x4Min: {
      __ F32x4Min(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kIA32F32x4Max: {
      __ F32x4Max(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kIA32F32x4Eq: {
      __ Cmpeqps(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32F32x4Ne: {
      __ Cmpneqps(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputOperand(1));
      break;
    }
    case kIA32F32x4Lt: {
      __ Cmpltps(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32F32x4Le: {
      __ Cmpleps(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32F32x4Qfma: {
      __ F32x4Qfma(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1), i.InputSimd128Register(2),
                   kScratchDoubleReg);
      break;
    }
    case kIA32F32x4Qfms: {
      __ F32x4Qfms(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1), i.InputSimd128Register(2),
                   kScratchDoubleReg);
      break;
    }
    case kIA32Minps: {
      __ Minps(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kIA32Maxps: {
      __ Maxps(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kIA32F32x4Round: {
      RoundingMode const mode =
          static_cast<RoundingMode>(MiscField::decode(instr->opcode()));
      __ Roundps(i.OutputSimd128Register(), i.InputDoubleRegister(0), mode);
      break;
    }
    case kIA32I32x4Splat: {
      XMMRegister dst = i.OutputSimd128Register();
      __ Movd(dst, i.InputOperand(0));
      __ Pshufd(dst, dst, uint8_t{0x0});
      break;
    }
    case kIA32I32x4ExtractLane: {
      __ Pextrd(i.OutputRegister(), i.InputSimd128Register(0), i.InputInt8(1));
      break;
    }
    case kIA32I32x4SConvertF32x4: {
      __ I32x4SConvertF32x4(i.OutputSimd128Register(),
                            i.InputSimd128Register(0), kScratchDoubleReg,
                            i.TempRegister(0));
      break;
    }
    case kIA32I32x4SConvertI16x8Low: {
      __ Pmovsxwd(i.OutputSimd128Register(), i.InputOperand(0));
      break;
    }
    case kIA32I32x4SConvertI16x8High: {
      __ I32x4SConvertI16x8High(i.OutputSimd128Register(),
                                i.InputSimd128Register(0));
      break;
    }
    case kIA32I32x4Neg: {
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(0);
      if (src.is_reg(dst)) {
        __ Pcmpeqd(kScratchDoubleReg, kScratchDoubleReg);
        __ Psignd(dst, kScratchDoubleReg);
      } else {
        __ Pxor(dst, dst);
        __ Psubd(dst, src);
      }
      break;
    }
    case kIA32I32x4Shl: {
      ASSEMBLE_SIMD_SHIFT(Pslld, 5);
      break;
    }
    case kIA32I32x4ShrS: {
      ASSEMBLE_SIMD_SHIFT(Psrad, 5);
      break;
    }
    case kIA32I32x4Add: {
      __ Paddd(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32I32x4Sub: {
      __ Psubd(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32I32x4Mul: {
      __ Pmulld(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I32x4MinS: {
      __ Pminsd(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I32x4MaxS: {
      __ Pmaxsd(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I32x4Eq: {
      __ Pcmpeqd(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32I32x4Ne: {
      __ Pcmpeqd(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      __ Pcmpeqd(kScratchDoubleReg, kScratchDoubleReg, kScratchDoubleReg);
      __ Pxor(i.OutputSimd128Register(), i.OutputSimd128Register(),
              kScratchDoubleReg);
      break;
    }
    case kIA32I32x4GtS: {
      __ Pcmpgtd(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32I32x4GeS: {
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src1 = i.InputSimd128Register(0);
      XMMRegister src2 = i.InputSimd128Register(1);
      if (CpuFeatures::IsSupported(AVX)) {
        CpuFeatureScope avx_scope(masm(), AVX);
        __ vpminsd(kScratchDoubleReg, src1, src2);
        __ vpcmpeqd(dst, kScratchDoubleReg, src2);
      } else {
        DCHECK_EQ(dst, src1);
        CpuFeatureScope sse_scope(masm(), SSE4_1);
        __ pminsd(dst, src2);
        __ pcmpeqd(dst, src2);
      }
      break;
    }
    case kSSEI32x4UConvertF32x4: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      CpuFeatureScope sse_scope(masm(), SSE4_1);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister tmp = i.TempSimd128Register(0);
      XMMRegister tmp2 = i.TempSimd128Register(1);
      __ I32x4TruncF32x4U(dst, dst, tmp, tmp2);
      break;
    }
    case kAVXI32x4UConvertF32x4: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister tmp = i.TempSimd128Register(0);
      // NAN->0, negative->0
      __ vpxor(kScratchDoubleReg, kScratchDoubleReg, kScratchDoubleReg);
      __ vmaxps(dst, dst, kScratchDoubleReg);
      // scratch: float representation of max_signed
      __ vpcmpeqd(kScratchDoubleReg, kScratchDoubleReg, kScratchDoubleReg);
      __ vpsrld(kScratchDoubleReg, kScratchDoubleReg, 1);  // 0x7fffffff
      __ vcvtdq2ps(kScratchDoubleReg, kScratchDoubleReg);  // 0x4f000000
      // tmp: convert (src-max_signed).
      // Positive overflow lanes -> 0x7FFFFFFF
      // Negative lanes -> 0
      __ vsubps(tmp, dst, kScratchDoubleReg);
      __ vcmpleps(kScratchDoubleReg, kScratchDoubleReg, tmp);
      __ vcvttps2dq(tmp, tmp);
      __ vpxor(tmp, tmp, kScratchDoubleReg);
      __ vpxor(kScratchDoubleReg, kScratchDoubleReg, kScratchDoubleReg);
      __ vpmaxsd(tmp, tmp, kScratchDoubleReg);
      // convert. Overflow lanes above max_signed will be 0x80000000
      __ vcvttps2dq(dst, dst);
      // Add (src-max_signed) for overflow lanes.
      __ vpaddd(dst, dst, tmp);
      break;
    }
    case kIA32I32x4UConvertI16x8Low: {
      __ Pmovzxwd(i.OutputSimd128Register(), i.InputOperand(0));
      break;
    }
    case kIA32I32x4UConvertI16x8High: {
      __ I32x4UConvertI16x8High(i.OutputSimd128Register(),
                                i.InputSimd128Register(0), kScratchDoubleReg);
      break;
    }
    case kIA32I32x4ShrU: {
      ASSEMBLE_SIMD_SHIFT(Psrld, 5);
      break;
    }
    case kIA32I32x4MinU: {
      __ Pminud(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I32x4MaxU: {
      __ Pmaxud(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kSSEI32x4GtU: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      CpuFeatureScope sse_scope(masm(), SSE4_1);
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(1);
      __ pmaxud(dst, src);
      __ pcmpeqd(dst, src);
      __ pcmpeqd(kScratchDoubleReg, kScratchDoubleReg);
      __ xorps(dst, kScratchDoubleReg);
      break;
    }
    case kAVXI32x4GtU: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src1 = i.InputSimd128Register(0);
      Operand src2 = i.InputOperand(1);
      __ vpmaxud(kScratchDoubleReg, src1, src2);
      __ vpcmpeqd(dst, kScratchDoubleReg, src2);
      __ vpcmpeqd(kScratchDoubleReg, kScratchDoubleReg, kScratchDoubleReg);
      __ vpxor(dst, dst, kScratchDoubleReg);
      break;
    }
    case kSSEI32x4GeU: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      CpuFeatureScope sse_scope(masm(), SSE4_1);
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(1);
      __ pminud(dst, src);
      __ pcmpeqd(dst, src);
      break;
    }
    case kAVXI32x4GeU: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister src1 = i.InputSimd128Register(0);
      Operand src2 = i.InputOperand(1);
      __ vpminud(kScratchDoubleReg, src1, src2);
      __ vpcmpeqd(i.OutputSimd128Register(), kScratchDoubleReg, src2);
      break;
    }
    case kIA32I32x4Abs: {
      __ Pabsd(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kIA32I32x4BitMask: {
      __ Movmskps(i.OutputRegister(), i.InputSimd128Register(0));
      break;
    }
    case kIA32I32x4DotI16x8S: {
      __ Pmaddwd(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32I16x8Splat: {
      if (instr->InputAt(0)->IsRegister()) {
        __ I16x8Splat(i.OutputSimd128Register(), i.InputRegister(0));
      } else {
        __ I16x8Splat(i.OutputSimd128Register(), i.InputOperand(0));
      }
      break;
    }
    case kIA32I16x8ExtractLaneS: {
      Register dst = i.OutputRegister();
      __ Pextrw(dst, i.InputSimd128Register(0), i.InputUint8(1));
      __ movsx_w(dst, dst);
      break;
    }
    case kIA32I16x8SConvertI8x16Low: {
      __ Pmovsxbw(i.OutputSimd128Register(), i.InputOperand(0));
      break;
    }
    case kIA32I16x8SConvertI8x16High: {
      __ I16x8SConvertI8x16High(i.OutputSimd128Register(),
                                i.InputSimd128Register(0));
      break;
    }
    case kIA32I16x8Neg: {
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(0);
      if (src.is_reg(dst)) {
        __ Pcmpeqd(kScratchDoubleReg, kScratchDoubleReg);
        __ Psignw(dst, kScratchDoubleReg);
      } else {
        __ Pxor(dst, dst);
        __ Psubw(dst, src);
      }
      break;
    }
    case kIA32I16x8Shl: {
      ASSEMBLE_SIMD_SHIFT(Psllw, 4);
      break;
    }
    case kIA32I16x8ShrS: {
      ASSEMBLE_SIMD_SHIFT(Psraw, 4);
      break;
    }
    case kIA32I16x8SConvertI32x4: {
      __ Packssdw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputOperand(1));
      break;
    }
    case kIA32I16x8Add: {
      __ Paddw(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32I16x8AddSatS: {
      __ Paddsw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I16x8Sub: {
      __ Psubw(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32I16x8SubSatS: {
      __ Psubsw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I16x8Mul: {
      __ Pmullw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I16x8MinS: {
      __ Pminsw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I16x8MaxS: {
      __ Pmaxsw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I16x8Eq: {
      __ Pcmpeqw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kSSEI16x8Ne: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      __ pcmpeqw(i.OutputSimd128Register(), i.InputOperand(1));
      __ pcmpeqw(kScratchDoubleReg, kScratchDoubleReg);
      __ xorps(i.OutputSimd128Register(), kScratchDoubleReg);
      break;
    }
    case kAVXI16x8Ne: {
      CpuFeatureScope avx_scope(masm(), AVX);
      __ vpcmpeqw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputOperand(1));
      __ vpcmpeqw(kScratchDoubleReg, kScratchDoubleReg, kScratchDoubleReg);
      __ vpxor(i.OutputSimd128Register(), i.OutputSimd128Register(),
               kScratchDoubleReg);
      break;
    }
    case kIA32I16x8GtS: {
      __ Pcmpgtw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kSSEI16x8GeS: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(1);
      __ pminsw(dst, src);
      __ pcmpeqw(dst, src);
      break;
    }
    case kAVXI16x8GeS: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister src1 = i.InputSimd128Register(0);
      Operand src2 = i.InputOperand(1);
      __ vpminsw(kScratchDoubleReg, src1, src2);
      __ vpcmpeqw(i.OutputSimd128Register(), kScratchDoubleReg, src2);
      break;
    }
    case kIA32I16x8UConvertI8x16Low: {
      __ Pmovzxbw(i.OutputSimd128Register(), i.InputOperand(0));
      break;
    }
    case kIA32I16x8UConvertI8x16High: {
      __ I16x8UConvertI8x16High(i.OutputSimd128Register(),
                                i.InputSimd128Register(0), kScratchDoubleReg);
      break;
    }
    case kIA32I16x8ShrU: {
      ASSEMBLE_SIMD_SHIFT(Psrlw, 4);
      break;
    }
    case kIA32I16x8UConvertI32x4: {
      __ Packusdw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kIA32I16x8AddSatU: {
      __ Paddusw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32I16x8SubSatU: {
      __ Psubusw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32I16x8MinU: {
      __ Pminuw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I16x8MaxU: {
      __ Pmaxuw(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kSSEI16x8GtU: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      CpuFeatureScope sse_scope(masm(), SSE4_1);
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(1);
      __ pmaxuw(dst, src);
      __ pcmpeqw(dst, src);
      __ pcmpeqw(kScratchDoubleReg, kScratchDoubleReg);
      __ xorps(dst, kScratchDoubleReg);
      break;
    }
    case kAVXI16x8GtU: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src1 = i.InputSimd128Register(0);
      Operand src2 = i.InputOperand(1);
      __ vpmaxuw(kScratchDoubleReg, src1, src2);
      __ vpcmpeqw(dst, kScratchDoubleReg, src2);
      __ vpcmpeqw(kScratchDoubleReg, kScratchDoubleReg, kScratchDoubleReg);
      __ vpxor(dst, dst, kScratchDoubleReg);
      break;
    }
    case kSSEI16x8GeU: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      CpuFeatureScope sse_scope(masm(), SSE4_1);
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(1);
      __ pminuw(dst, src);
      __ pcmpeqw(dst, src);
      break;
    }
    case kAVXI16x8GeU: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister src1 = i.InputSimd128Register(0);
      Operand src2 = i.InputOperand(1);
      __ vpminuw(kScratchDoubleReg, src1, src2);
      __ vpcmpeqw(i.OutputSimd128Register(), kScratchDoubleReg, src2);
      break;
    }
    case kIA32I16x8RoundingAverageU: {
      __ Pavgw(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32I16x8Abs: {
      __ Pabsw(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kIA32I16x8BitMask: {
      Register dst = i.OutputRegister();
      XMMRegister tmp = i.TempSimd128Register(0);
      __ Packsswb(tmp, i.InputSimd128Register(0));
      __ Pmovmskb(dst, tmp);
      __ shr(dst, 8);
      break;
    }
    case kIA32I8x16Splat: {
      if (instr->InputAt(0)->IsRegister()) {
        __ I8x16Splat(i.OutputSimd128Register(), i.InputRegister(0),
                      kScratchDoubleReg);
      } else {
        __ I8x16Splat(i.OutputSimd128Register(), i.InputOperand(0),
                      kScratchDoubleReg);
      }
      break;
    }
    case kIA32I8x16ExtractLaneS: {
      Register dst = i.OutputRegister();
      __ Pextrb(dst, i.InputSimd128Register(0), i.InputUint8(1));
      __ movsx_b(dst, dst);
      break;
    }
    case kIA32Pinsrb: {
      ASSEMBLE_SIMD_PINSR(pinsrb, SSE4_1);
      break;
    }
    case kIA32Pinsrw: {
      ASSEMBLE_SIMD_PINSR(pinsrw, SSE4_1);
      break;
    }
    case kIA32Pinsrd: {
      ASSEMBLE_SIMD_PINSR(pinsrd, SSE4_1);
      break;
    }
    case kIA32Movlps: {
      if (instr->HasOutput()) {
        __ Movlps(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.MemoryOperand(2));
      } else {
        size_t index = 0;
        Operand dst = i.MemoryOperand(&index);
        __ Movlps(dst, i.InputSimd128Register(index));
      }
      break;
    }
    case kIA32Movhps: {
      if (instr->HasOutput()) {
        __ Movhps(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.MemoryOperand(2));
      } else {
        size_t index = 0;
        Operand dst = i.MemoryOperand(&index);
        __ Movhps(dst, i.InputSimd128Register(index));
      }
      break;
    }
    case kIA32Pextrb: {
      if (HasAddressingMode(instr)) {
        size_t index = 0;
        Operand operand = i.MemoryOperand(&index);
        __ Pextrb(operand, i.InputSimd128Register(index),
                  i.InputUint8(index + 1));
      } else {
        Register dst = i.OutputRegister();
        __ Pextrb(dst, i.InputSimd128Register(0), i.InputUint8(1));
      }
      break;
    }
    case kIA32Pextrw: {
      if (HasAddressingMode(instr)) {
        size_t index = 0;
        Operand operand = i.MemoryOperand(&index);
        __ Pextrw(operand, i.InputSimd128Register(index),
                  i.InputUint8(index + 1));
      } else {
        Register dst = i.OutputRegister();
        __ Pextrw(dst, i.InputSimd128Register(0), i.InputUint8(1));
      }
      break;
    }
    case kIA32S128Store32Lane: {
      size_t index = 0;
      Operand operand = i.MemoryOperand(&index);
      uint8_t laneidx = i.InputUint8(index + 1);
      __ S128Store32Lane(operand, i.InputSimd128Register(index), laneidx);
      break;
    }
    case kIA32I8x16SConvertI16x8: {
      __ Packsswb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputOperand(1));
      break;
    }
    case kIA32I8x16Neg: {
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(0);
      if (src.is_reg(dst)) {
        __ Pcmpeqd(kScratchDoubleReg, kScratchDoubleReg);
        __ Psignb(dst, kScratchDoubleReg);
      } else {
        __ Pxor(dst, dst);
        __ Psubb(dst, src);
      }
      break;
    }
    case kIA32I8x16Shl: {
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src = i.InputSimd128Register(0);
      DCHECK_IMPLIES(!CpuFeatures::IsSupported(AVX), dst == src);
      Register tmp = i.TempRegister(0);

      if (HasImmediateInput(instr, 1)) {
        __ I8x16Shl(dst, src, i.InputInt3(1), tmp, kScratchDoubleReg);
      } else {
        XMMRegister tmp_simd = i.TempSimd128Register(1);
        __ I8x16Shl(dst, src, i.InputRegister(1), tmp, kScratchDoubleReg,
                    tmp_simd);
      }
      break;
    }
    case kIA32I8x16ShrS: {
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src = i.InputSimd128Register(0);
      DCHECK_IMPLIES(!CpuFeatures::IsSupported(AVX), dst == src);

      if (HasImmediateInput(instr, 1)) {
        __ I8x16ShrS(dst, src, i.InputInt3(1), kScratchDoubleReg);
      } else {
        __ I8x16ShrS(dst, src, i.InputRegister(1), i.TempRegister(0),
                     kScratchDoubleReg, i.TempSimd128Register(1));
      }
      break;
    }
    case kIA32I8x16Add: {
      __ Paddb(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32I8x16AddSatS: {
      __ Paddsb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I8x16Sub: {
      __ Psubb(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32I8x16SubSatS: {
      __ Psubsb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I8x16MinS: {
      __ Pminsb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I8x16MaxS: {
      __ Pmaxsb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I8x16Eq: {
      __ Pcmpeqb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kSSEI8x16Ne: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      __ pcmpeqb(i.OutputSimd128Register(), i.InputOperand(1));
      __ pcmpeqb(kScratchDoubleReg, kScratchDoubleReg);
      __ xorps(i.OutputSimd128Register(), kScratchDoubleReg);
      break;
    }
    case kAVXI8x16Ne: {
      CpuFeatureScope avx_scope(masm(), AVX);
      __ vpcmpeqb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputOperand(1));
      __ vpcmpeqb(kScratchDoubleReg, kScratchDoubleReg, kScratchDoubleReg);
      __ vpxor(i.OutputSimd128Register(), i.OutputSimd128Register(),
               kScratchDoubleReg);
      break;
    }
    case kIA32I8x16GtS: {
      __ Pcmpgtb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kSSEI8x16GeS: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      CpuFeatureScope sse_scope(masm(), SSE4_1);
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(1);
      __ pminsb(dst, src);
      __ pcmpeqb(dst, src);
      break;
    }
    case kAVXI8x16GeS: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister src1 = i.InputSimd128Register(0);
      Operand src2 = i.InputOperand(1);
      __ vpminsb(kScratchDoubleReg, src1, src2);
      __ vpcmpeqb(i.OutputSimd128Register(), kScratchDoubleReg, src2);
      break;
    }
    case kIA32I8x16UConvertI16x8: {
      __ Packuswb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kIA32I8x16AddSatU: {
      __ Paddusb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32I8x16SubSatU: {
      __ Psubusb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputOperand(1));
      break;
    }
    case kIA32I8x16ShrU: {
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src = i.InputSimd128Register(0);
      DCHECK_IMPLIES(!CpuFeatures::IsSupported(AVX), dst == src);
      Register tmp = i.TempRegister(0);

      if (HasImmediateInput(instr, 1)) {
        __ I8x16ShrU(dst, src, i.InputInt3(1), tmp, kScratchDoubleReg);
      } else {
        __ I8x16ShrU(dst, src, i.InputRegister(1), tmp, kScratchDoubleReg,
                     i.TempSimd128Register(1));
      }

      break;
    }
    case kIA32I8x16MinU: {
      __ Pminub(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kIA32I8x16MaxU: {
      __ Pmaxub(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputOperand(1));
      break;
    }
    case kSSEI8x16GtU: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(1);
      __ pmaxub(dst, src);
      __ pcmpeqb(dst, src);
      __ pcmpeqb(kScratchDoubleReg, kScratchDoubleReg);
      __ xorps(dst, kScratchDoubleReg);
      break;
    }
    case kAVXI8x16GtU: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src1 = i.InputSimd128Register(0);
      Operand src2 = i.InputOperand(1);
      __ vpmaxub(kScratchDoubleReg, src1, src2);
      __ vpcmpeqb(dst, kScratchDoubleReg, src2);
      __ vpcmpeqb(kScratchDoubleReg, kScratchDoubleReg, kScratchDoubleReg);
      __ vpxor(dst, dst, kScratchDoubleReg);
      break;
    }
    case kSSEI8x16GeU: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(1);
      __ pminub(dst, src);
      __ pcmpeqb(dst, src);
      break;
    }
    case kAVXI8x16GeU: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister src1 = i.InputSimd128Register(0);
      Operand src2 = i.InputOperand(1);
      __ vpminub(kScratchDoubleReg, src1, src2);
      __ vpcmpeqb(i.OutputSimd128Register(), kScratchDoubleReg, src2);
      break;
    }
    case kIA32I8x16RoundingAverageU: {
      __ Pavgb(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputOperand(1));
      break;
    }
    case kIA32I8x16Abs: {
      __ Pabsb(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kIA32I8x16BitMask: {
      __ Pmovmskb(i.OutputRegister(), i.InputSimd128Register(0));
      break;
    }
    case kIA32I8x16Popcnt: {
      __ I8x16Popcnt(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     kScratchDoubleReg, i.TempSimd128Register(0),
                     i.TempRegister(1));
      break;
    }
    case kIA32S128Const: {
      XMMRegister dst = i.OutputSimd128Register();
      Register tmp = i.TempRegister(0);
      uint64_t low_qword = make_uint64(i.InputUint32(1), i.InputUint32(0));
      __ Move(dst, low_qword);
      __ Move(tmp, Immediate(i.InputUint32(2)));
      __ Pinsrd(dst, tmp, 2);
      __ Move(tmp, Immediate(i.InputUint32(3)));
      __ Pinsrd(dst, tmp, 3);
      break;
    }
    case kIA32S128Zero: {
      XMMRegister dst = i.OutputSimd128Register();
      __ Pxor(dst, dst);
      break;
    }
    case kIA32S128AllOnes: {
      XMMRegister dst = i.OutputSimd128Register();
      __ Pcmpeqd(dst, dst);
      break;
    }
    case kIA32S128Not: {
      __ S128Not(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kScratchDoubleReg);
      break;
    }
    case kIA32S128And: {
      __ Pand(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputOperand(1));
      break;
    }
    case kIA32S128Or: {
      __ Por(i.OutputSimd128Register(), i.InputSimd128Register(0),
             i.InputOperand(1));
      break;
    }
    case kIA32S128Xor: {
      __ Pxor(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputOperand(1));
      break;
    }
    case kIA32S128Select: {
      __ S128Select(i.OutputSimd128Register(), i.InputSimd128Register(0),
                    i.InputSimd128Register(1), i.InputSimd128Register(2),
                    kScratchDoubleReg);
      break;
    }
    case kIA32S128AndNot: {
      // The inputs have been inverted by instruction selector, so we can call
      // andnps here without any modifications.
      __ Andnps(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kIA32I8x16Swizzle: {
      __ I8x16Swizzle(i.OutputSimd128Register(), i.InputSimd128Register(0),
                      i.InputSimd128Register(1), kScratchDoubleReg,
                      i.TempRegister(0), MiscField::decode(instr->opcode()));
      break;
    }
    case kIA32I8x16Shuffle: {
      XMMRegister dst = i.OutputSimd128Register();
      Operand src0 = i.InputOperand(0);
      Register tmp = i.TempRegister(0);
      // Prepare 16 byte aligned buffer for shuffle control mask
      __ mov(tmp, esp);
      __ and_(esp, -16);
      if (instr->InputCount() == 5) {  // only one input operand
        DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
        for (int j = 4; j > 0; j--) {
          uint32_t mask = i.InputUint32(j);
          __ push(Immediate(mask));
        }
        __ Pshufb(dst, Operand(esp, 0));
      } else {  // two input operands
        DCHECK_EQ(6, instr->InputCount());
        __ Movups(kScratchDoubleReg, src0);
        for (int j = 5; j > 1; j--) {
          uint32_t lanes = i.InputUint32(j);
          uint32_t mask = 0;
          for (int k = 0; k < 32; k += 8) {
            uint8_t lane = lanes >> k;
            mask |= (lane < kSimd128Size ? lane : 0x80) << k;
          }
          __ push(Immediate(mask));
        }
        __ Pshufb(kScratchDoubleReg, Operand(esp, 0));
        Operand src1 = i.InputOperand(1);
        if (!src1.is_reg(dst)) __ Movups(dst, src1);
        for (int j = 5; j > 1; j--) {
          uint32_t lanes = i.InputUint32(j);
          uint32_t mask = 0;
          for (int k = 0; k < 32; k += 8) {
            uint8_t lane = lanes >> k;
            mask |= (lane >= kSimd128Size ? (lane & 0xF) : 0x80) << k;
          }
          __ push(Immediate(mask));
        }
        __ Pshufb(dst, Operand(esp, 0));
        __ por(dst, kScratchDoubleReg);
      }
      __ mov(esp, tmp);
      break;
    }
    case kIA32S128Load8Splat: {
      __ S128Load8Splat(i.OutputSimd128Register(), i.MemoryOperand(),
                        kScratchDoubleReg);
      break;
    }
    case kIA32S128Load16Splat: {
      __ S128Load16Splat(i.OutputSimd128Register(), i.MemoryOperand(),
                         kScratchDoubleReg);
      break;
    }
    case kIA32S128Load32Splat: {
      __ S128Load32Splat(i.OutputSimd128Register(), i.MemoryOperand());
      break;
    }
    case kIA32S128Load64Splat: {
      __ Movddup(i.OutputSimd128Register(), i.MemoryOperand());
      break;
    }
    case kIA32S128Load8x8S: {
      __ Pmovsxbw(i.OutputSimd128Register(), i.MemoryOperand());
      break;
    }
    case kIA32S128Load8x8U: {
      __ Pmovzxbw(i.OutputSimd128Register(), i.MemoryOperand());
      break;
    }
    case kIA32S128Load16x4S: {
      __ Pmovsxwd(i.OutputSimd128Register(), i.MemoryOperand());
      break;
    }
    case kIA32S128Load16x4U: {
      __ Pmovzxwd(i.OutputSimd128Register(), i.MemoryOperand());
      break;
    }
    case kIA32S128Load32x2S: {
      __ Pmovsxdq(i.OutputSimd128Register(), i.MemoryOperand());
      break;
    }
    case kIA32S128Load32x2U: {
      __ Pmovzxdq(i.OutputSimd128Register(), i.MemoryOperand());
      break;
    }
    case kIA32S32x4Rotate: {
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src = i.InputSimd128Register(0);
      uint8_t mask = i.InputUint8(1);
      if (dst == src) {
        // 1-byte shorter encoding than pshufd.
        __ Shufps(dst, src, src, mask);
      } else {
        __ Pshufd(dst, src, mask);
      }
      break;
    }
    case kIA32S32x4Swizzle: {
      DCHECK_EQ(2, instr->InputCount());
      __ Pshufd(i.OutputSimd128Register(), i.InputOperand(0), i.InputUint8(1));
      break;
    }
    case kIA32S32x4Shuffle: {
      DCHECK_EQ(4, instr->InputCount());  // Swizzles should be handled above.
      uint8_t shuffle = i.InputUint8(2);
      DCHECK_NE(0xe4, shuffle);  // A simple blend should be handled below.
      __ Pshufd(kScratchDoubleReg, i.InputOperand(1), shuffle);
      __ Pshufd(i.OutputSimd128Register(), i.InputOperand(0), shuffle);
      __ Pblendw(i.OutputSimd128Register(), kScratchDoubleReg, i.InputUint8(3));
      break;
    }
    case kIA32S16x8Blend:
      ASSEMBLE_SIMD_IMM_SHUFFLE(pblendw, SSE4_1, i.InputInt8(2));
      break;
    case kIA32S16x8HalfShuffle1: {
      XMMRegister dst = i.OutputSimd128Register();
      __ Pshuflw(dst, i.InputOperand(0), i.InputUint8(1));
      __ Pshufhw(dst, dst, i.InputUint8(2));
      break;
    }
    case kIA32S16x8HalfShuffle2: {
      XMMRegister dst = i.OutputSimd128Register();
      __ Pshuflw(kScratchDoubleReg, i.InputOperand(1), i.InputUint8(2));
      __ Pshufhw(kScratchDoubleReg, kScratchDoubleReg, i.InputUint8(3));
      __ Pshuflw(dst, i.InputOperand(0), i.InputUint8(2));
      __ Pshufhw(dst, dst, i.InputUint8(3));
      __ Pblendw(dst, kScratchDoubleReg, i.InputUint8(4));
      break;
    }
    case kIA32S8x16Alignr:
      ASSEMBLE_SIMD_IMM_SHUFFLE(palignr, SSSE3, i.InputInt8(2));
      break;
    case kIA32S16x8Dup: {
      XMMRegister dst = i.OutputSimd128Register();
      Operand src = i.InputOperand(0);
      uint8_t lane = i.InputUint8(1) & 0x7;
      uint8_t lane4 = lane & 0x3;
      uint8_t half_dup = lane4 | (lane4 << 2) | (lane4 << 4) | (lane4 << 6);
      if (lane < 4) {
        __ Pshuflw(dst, src, half_dup);
        __ Punpcklqdq(dst, dst);
      } else {
        __ Pshufhw(dst, src, half_dup);
        __ Punpckhqdq(dst, dst);
      }
      break;
    }
    case kIA32S8x16Dup: {
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src = i.InputSimd128Register(0);
      uint8_t lane = i.InputUint8(1) & 0xf;
      if (CpuFeatures::IsSupported(AVX)) {
        CpuFeatureScope avx_scope(masm(), AVX);
        if (lane < 8) {
          __ vpunpcklbw(dst, src, src);
        } else {
          __ vpunpckhbw(dst, src, src);
        }
      } else {
        DCHECK_EQ(dst, src);
        if (lane < 8) {
          __ punpcklbw(dst, dst);
        } else {
          __ punpckhbw(dst, dst);
        }
      }
      lane &= 0x7;
      uint8_t lane4 = lane & 0x3;
      uint8_t half_dup = lane4 | (lane4 << 2) | (lane4 << 4) | (lane4 << 6);
      if (lane < 4) {
        __ Pshuflw(dst, dst, half_dup);
        __ Punpcklqdq(dst, dst);
      } else {
        __ Pshufhw(dst, dst, half_dup);
        __ Punpckhqdq(dst, dst);
      }
      break;
    }
    case kIA32S64x2UnpackHigh:
      ASSEMBLE_SIMD_PUNPCK_SHUFFLE(punpckhqdq);
      break;
    case kIA32S32x4UnpackHigh:
      ASSEMBLE_SIMD_PUNPCK_SHUFFLE(punpckhdq);
      break;
    case kIA32S16x8UnpackHigh:
      ASSEMBLE_SIMD_PUNPCK_SHUFFLE(punpckhwd);
      break;
    case kIA32S8x16UnpackHigh:
      ASSEMBLE_SIMD_PUNPCK_SHUFFLE(punpckhbw);
      break;
    case kIA32S64x2UnpackLow:
      ASSEMBLE_SIMD_PUNPCK_SHUFFLE(punpcklqdq);
      break;
    case kIA32S32x4UnpackLow:
      ASSEMBLE_SIMD_PUNPCK_SHUFFLE(punpckldq);
      break;
    case kIA32S16x8UnpackLow:
      ASSEMBLE_SIMD_PUNPCK_SHUFFLE(punpcklwd);
      break;
    case kIA32S8x16UnpackLow:
      ASSEMBLE_SIMD_PUNPCK_SHUFFLE(punpcklbw);
      break;
    case kSSES16x8UnzipHigh: {
      CpuFeatureScope sse_scope(masm(), SSE4_1);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src2 = dst;
      DCHECK_EQ(dst, i.InputSimd128Register(0));
      if (instr->InputCount() == 2) {
        __ movups(kScratchDoubleReg, i.InputOperand(1));
        __ psrld(kScratchDoubleReg, 16);
        src2 = kScratchDoubleReg;
      }
      __ psrld(dst, 16);
      __ packusdw(dst, src2);
      break;
    }
    case kAVXS16x8UnzipHigh: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src2 = dst;
      if (instr->InputCount() == 2) {
        __ vpsrld(kScratchDoubleReg, i.InputSimd128Register(1), 16);
        src2 = kScratchDoubleReg;
      }
      __ vpsrld(dst, i.InputSimd128Register(0), 16);
      __ vpackusdw(dst, dst, src2);
      break;
    }
    case kSSES16x8UnzipLow: {
      CpuFeatureScope sse_scope(masm(), SSE4_1);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src2 = dst;
      DCHECK_EQ(dst, i.InputSimd128Register(0));
      __ xorps(kScratchDoubleReg, kScratchDoubleReg);
      if (instr->InputCount() == 2) {
        __ pblendw(kScratchDoubleReg, i.InputOperand(1), 0x55);
        src2 = kScratchDoubleReg;
      }
      __ pblendw(dst, kScratchDoubleReg, 0xaa);
      __ packusdw(dst, src2);
      break;
    }
    case kAVXS16x8UnzipLow: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src2 = dst;
      __ vpxor(kScratchDoubleReg, kScratchDoubleReg, kScratchDoubleReg);
      if (instr->InputCount() == 2) {
        __ vpblendw(kScratchDoubleReg, kScratchDoubleReg, i.InputOperand(1),
                    0x55);
        src2 = kScratchDoubleReg;
      }
      __ vpblendw(dst, kScratchDoubleReg, i.InputSimd128Register(0), 0x55);
      __ vpackusdw(dst, dst, src2);
      break;
    }
    case kSSES8x16UnzipHigh: {
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src2 = dst;
      DCHECK_EQ(dst, i.InputSimd128Register(0));
      if (instr->InputCount() == 2) {
        __ movups(kScratchDoubleReg, i.InputOperand(1));
        __ psrlw(kScratchDoubleReg, 8);
        src2 = kScratchDoubleReg;
      }
      __ psrlw(dst, 8);
      __ packuswb(dst, src2);
      break;
    }
    case kAVXS8x16UnzipHigh: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src2 = dst;
      if (instr->InputCount() == 2) {
        __ vpsrlw(kScratchDoubleReg, i.InputSimd128Register(1), 8);
        src2 = kScratchDoubleReg;
      }
      __ vpsrlw(dst, i.InputSimd128Register(0), 8);
      __ vpackuswb(dst, dst, src2);
      break;
    }
    case kSSES8x16UnzipLow: {
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src2 = dst;
      DCHECK_EQ(dst, i.InputSimd128Register(0));
      if (instr->InputCount() == 2) {
        __ movups(kScratchDoubleReg, i.InputOperand(1));
        __ psllw(kScratchDoubleReg, 8);
        __ psrlw(kScratchDoubleReg, 8);
        src2 = kScratchDoubleReg;
      }
      __ psllw(dst, 8);
      __ psrlw(dst, 8);
      __ packuswb(dst, src2);
      break;
    }
    case kAVXS8x16UnzipLow: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src2 = dst;
      if (instr->InputCount() == 2) {
        __ vpsllw(kScratchDoubleReg, i.InputSimd128Register(1), 8);
        __ vpsrlw(kScratchDoubleReg, kScratchDoubleReg, 8);
        src2 = kScratchDoubleReg;
      }
      __ vpsllw(dst, i.InputSimd128Register(0), 8);
      __ vpsrlw(dst, dst, 8);
      __ vpackuswb(dst, dst, src2);
      break;
    }
    case kSSES8x16TransposeLow: {
      XMMRegister dst = i.OutputSimd128Register();
      DCHECK_EQ(dst, i.InputSimd128Register(0));
      __ psllw(dst, 8);
      if (instr->InputCount() == 1) {
        __ movups(kScratchDoubleReg, dst);
      } else {
        DCHECK_EQ(2, instr->InputCount());
        __ movups(kScratchDoubleReg, i.InputOperand(1));
        __ psllw(kScratchDoubleReg, 8);
      }
      __ psrlw(dst, 8);
      __ orps(dst, kScratchDoubleReg);
      break;
    }
    case kAVXS8x16TransposeLow: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister dst = i.OutputSimd128Register();
      if (instr->InputCount() == 1) {
        __ vpsllw(kScratchDoubleReg, i.InputSimd128Register(0), 8);
        __ vpsrlw(dst, kScratchDoubleReg, 8);
      } else {
        DCHECK_EQ(2, instr->InputCount());
        __ vpsllw(kScratchDoubleReg, i.InputSimd128Register(1), 8);
        __ vpsllw(dst, i.InputSimd128Register(0), 8);
        __ vpsrlw(dst, dst, 8);
      }
      __ vpor(dst, dst, kScratchDoubleReg);
      break;
    }
    case kSSES8x16TransposeHigh: {
      XMMRegister dst = i.OutputSimd128Register();
      DCHECK_EQ(dst, i.InputSimd128Register(0));
      __ psrlw(dst, 8);
      if (instr->InputCount() == 1) {
        __ movups(kScratchDoubleReg, dst);
      } else {
        DCHECK_EQ(2, instr->InputCount());
        __ movups(kScratchDoubleReg, i.InputOperand(1));
        __ psrlw(kScratchDoubleReg, 8);
      }
      __ psllw(kScratchDoubleReg, 8);
      __ orps(dst, kScratchDoubleReg);
      break;
    }
    case kAVXS8x16TransposeHigh: {
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister dst = i.OutputSimd128Register();
      if (instr->InputCount() == 1) {
        __ vpsrlw(dst, i.InputSimd128Register(0), 8);
        __ vpsllw(kScratchDoubleReg, dst, 8);
      } else {
        DCHECK_EQ(2, instr->InputCount());
        __ vpsrlw(kScratchDoubleReg, i.InputSimd128Register(1), 8);
        __ vpsrlw(dst, i.InputSimd128Register(0), 8);
        __ vpsllw(kScratchDoubleReg, kScratchDoubleReg, 8);
      }
      __ vpor(dst, dst, kScratchDoubleReg);
      break;
    }
    case kSSES8x8Reverse:
    case kSSES8x4Reverse:
    case kSSES8x2Reverse: {
      DCHECK_EQ(1, instr->InputCount());
      XMMRegister dst = i.OutputSimd128Register();
      DCHECK_EQ(dst, i.InputSimd128Register(0));
      if (arch_opcode != kSSES8x2Reverse) {
        // First shuffle words into position.
        int8_t shuffle_mask = arch_opcode == kSSES8x4Reverse ? 0xB1 : 0x1B;
        __ pshuflw(dst, dst, shuffle_mask);
        __ pshufhw(dst, dst, shuffle_mask);
      }
      __ movaps(kScratchDoubleReg, dst);
      __ psrlw(kScratchDoubleReg, 8);
      __ psllw(dst, 8);
      __ orps(dst, kScratchDoubleReg);
      break;
    }
    case kAVXS8x2Reverse:
    case kAVXS8x4Reverse:
    case kAVXS8x8Reverse: {
      DCHECK_EQ(1, instr->InputCount());
      CpuFeatureScope avx_scope(masm(), AVX);
      XMMRegister dst = i.OutputSimd128Register();
      XMMRegister src = dst;
      if (arch_opcode != kAVXS8x2Reverse) {
        // First shuffle words into position.
        int8_t shuffle_mask = arch_opcode == kAVXS8x4Reverse ? 0xB1 : 0x1B;
        __ vpshuflw(dst, i.InputOperand(0), shuffle_mask);
        __ vpshufhw(dst, dst, shuffle_mask);
      } else {
        src = i.InputSimd128Register(0);
      }
      // Reverse each 16 bit lane.
      __ vpsrlw(kScratchDoubleReg, src, 8);
      __ vpsllw(dst, src, 8);
      __ vpor(dst, dst, kScratchDoubleReg);
      break;
    }
    case kIA32S128AnyTrue: {
      Register dst = i.OutputRegister();
      XMMRegister src = i.InputSimd128Register(0);
      Register tmp = i.TempRegister(0);
      __ xor_(tmp, tmp);
      __ mov(dst, Immediate(1));
      __ Ptest(src, src);
      __ cmov(zero, dst, tmp);
      break;
    }
    // Need to split up all the different lane structures because the
    // comparison instruction used matters, e.g. given 0xff00, pcmpeqb returns
    // 0x0011, pcmpeqw returns 0x0000, ptest will set ZF to 0 and 1
    // respectively.
    case kIA32I64x2AllTrue:
      ASSEMBLE_SIMD_ALL_TRUE(Pcmpeqq);
      break;
    case kIA32I32x4AllTrue:
      ASSEMBLE_SIMD_ALL_TRUE(Pcmpeqd);
      break;
    case kIA32I16x8AllTrue:
      ASSEMBLE_SIMD_ALL_TRUE(pcmpeqw);
      break;
    case kIA32I8x16AllTrue: {
      ASSEMBLE_SIMD_ALL_TRUE(pcmpeqb);
      break;
    }
    case kIA32Blendvpd: {
      __ Blendvpd(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), i.InputSimd128Register(2));
      break;
    }
    case kIA32Blendvps: {
      __ Blendvps(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), i.InputSimd128Register(2));
      break;
    }
    case kIA32Pblendvb: {
      __ Pblendvb(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), i.InputSimd128Register(2));
      break;
    }
    case kIA32I32x4TruncF64x2UZero: {
      __ I32x4TruncSatF64x2UZero(i.OutputSimd128Register(),
                                 i.InputSimd128Register(0), kScratchDoubleReg,
                                 i.TempRegister(0));
      break;
    }
    case kIA32I32x4TruncF32x4U: {
      __ I32x4TruncF32x4U(i.OutputSimd128Register(), i.InputSimd128Register(0),
                          kScratchDoubleReg, i.TempSimd128Register(0));
      break;
    }
    case kIA32Cvttps2dq: {
      __ Cvttps2dq(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kIA32Cvttpd2dq: {
      __ Cvttpd2dq(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kIA32Word32AtomicPairLoad: {
      __ movq(kScratchDoubleReg, i.MemoryOperand());
      __ Pextrd(i.OutputRegister(0), kScratchDoubleReg, 0);
      __ Pextrd(i.OutputRegister(1), kScratchDoubleReg, 1);
      break;
    }
    case kIA32Word32ReleasePairStore: {
      __ push(ebx);
      i.MoveInstructionOperandToRegister(ebx, instr->InputAt(1));
      __ push(ebx);
      i.MoveInstructionOperandToRegister(ebx, instr->InputAt(0));
      __ push(ebx);
      frame_access_state()->IncreaseSPDelta(3);
      __ movq(kScratchDoubleReg, MemOperand(esp, 0));
      __ pop(ebx);
      __ pop(ebx);
      __ pop(ebx);
      frame_access_state()->IncreaseSPDelta(-3);
      __ movq(i.MemoryOperand(2), kScratchDoubleReg);
      break;
    }
    case kIA32Word32SeqCstPairStore: {
      Label store;
      __ bind(&store);
      __ mov(eax, i.MemoryOperand(2));
      __ mov(edx, i.NextMemoryOperand(2));
      __ push(ebx);
      frame_access_state()->IncreaseSPDelta(1);
      i.MoveInstructionOperandToRegister(ebx, instr->InputAt(0));
      __ lock();
      __ cmpxchg8b(i.MemoryOperand(2));
      __ pop(ebx);
      frame_access_state()->IncreaseSPDelta(-1);
      __ j(not_equal, &store);
      break;
    }
    case kAtomicExchangeInt8: {
      __ xchg_b(i.InputRegister(0), i.MemoryOperand(1));
      __ movsx_b(i.InputRegister(0), i.InputRegister(0));
      break;
    }
    case kAtomicExchangeUint8: {
      __ xchg_b(i.InputRegister(0), i.MemoryOperand(1));
      __ movzx_b(i.InputRegister(0), i.InputRegister(0));
      break;
    }
    case kAtomicExchangeInt16: {
      __ xchg_w(i.InputRegister(0), i.MemoryOperand(1));
      __ movsx_w(i.InputRegister(0), i.InputRegister(0));
      break;
    }
    case kAtomicExchangeUint16: {
      __ xchg_w(i.InputRegister(0), i.MemoryOperand(1));
      __ movzx_w(i.InputRegister(0), i.InputRegister(0));
      break;
    }
    case kAtomicExchangeWord32: {
      __ xchg(i.InputRegister(0), i.MemoryOperand(1));
      break;
    }
    case kIA32Word32AtomicPairExchange: {
      DCHECK(VerifyOutputOfAtomicPairInstr(&i, instr));
      Label exchange;
      __ bind(&exchange);
      __ mov(eax, i.MemoryOperand(2));
      __ mov(edx, i.NextMemoryOperand(2));
      __ push(ebx);
      frame_access_state()->IncreaseSPDelta(1);
      i.MoveInstructionOperandToRegister(ebx, instr->InputAt(0));
      __ lock();
      __ cmpxchg8b(i.MemoryOperand(2));
      __ pop(ebx);
      frame_access_state()->IncreaseSPDelta(-1);
      __ j(not_equal, &exchange);
      break;
    }
    case kAtomicCompareExchangeInt8: {
      __ lock();
      __ cmpxchg_b(i.MemoryOperand(2), i.InputRegister(1));
      __ movsx_b(eax, eax);
      break;
    }
    case kAtomicCompareExchangeUint8: {
      __ lock();
      __ cmpxchg_b(i.MemoryOperand(2), i.InputRegister(1));
      __ movzx_b(eax, eax);
      break;
    }
    case kAtomicCompareExchangeInt16: {
      __ lock();
      __ cmpxchg_w(i.MemoryOperand(2), i.InputRegister(1));
      __ movsx_w(eax, eax);
      break;
    }
    case kAtomicCompareExchangeUint16: {
      __ lock();
      __ cmpxchg_w(i.MemoryOperand(2), i.InputRegister(1));
      __ movzx_w(eax, eax);
      break;
    }
    case kAtomicCompareExchangeWord32: {
      __ lock();
      __ cmpxchg(i.MemoryOperand(2), i.InputRegister(1));
      break;
    }
    case kIA32Word32AtomicPairCompareExchange: {
      __ push(ebx);
      frame_access_state()->IncreaseSPDelta(1);
      i.MoveInstructionOperandToRegister(ebx, instr->InputAt(2));
      __ lock();
      __ cmpxchg8b(i.MemoryOperand(4));
      __ pop(ebx);
      frame_access_state()->IncreaseSPDelta(-1);
      break;
    }
#define ATOMIC_BINOP_CASE(op, inst)                \
  case kAtomic##op##Int8: {                        \
    ASSEMBLE_ATOMIC_BINOP(inst, mov_b, cmpxchg_b); \
    __ movsx_b(eax, eax);                          \
    break;                                         \
  }                                                \
  case kAtomic##op##Uint8: {                       \
    ASSEMBLE_ATOMIC_BINOP(inst, mov_b, cmpxchg_b); \
    __ movzx_b(eax, eax);                          \
    break;                                         \
  }                                                \
  case kAtomic##op##Int16: {                       \
    ASSEMBLE_ATOMIC_BINOP(inst, mov_w, cmpxchg_w); \
    __ movsx_w(eax, eax);                          \
    break;                                         \
  }                                                \
  case kAtomic##op##Uint16: {                      \
    ASSEMBLE_ATOMIC_BINOP(inst, mov_w, cmpxchg_w); \
    __ movzx_w(eax, eax);                          \
    break;                                         \
  }                                                \
  case kAtomic##op##Word32: {                      \
    ASSEMBLE_ATOMIC_BINOP(inst, mov, cmpxchg);     \
    break;                                         \
  }
      ATOMIC_BINOP_CASE(Add, add)
      ATOMIC_BINOP_CASE(Sub, sub)
      ATOMIC_BINOP_CASE(And, and_)
      ATOMIC_BINOP_CASE(Or, or_)
      ATOMIC_BINOP_CASE(Xor, xor_)
#undef ATOMIC_BINOP_CASE
#define ATOMIC_BINOP_CASE(op, instr1, instr2)         \
  case kIA32Word32AtomicPair##op: {                   \
    DCHECK(VerifyOutputOfAtomicPairInstr(&i, instr)); \
    ASSEMBLE_I64ATOMIC_BINOP(instr1, instr2)          \
    break;                                            \
  }
      ATOMIC_BINOP_CASE(Add, add, adc)
      ATOMIC_BINOP_CASE(And, and_, and_)
      ATOMIC_BINOP_CASE(Or, or_, or_)
      ATOMIC_BINOP_CASE(Xor, xor_, xor_)
#undef ATOMIC_BINOP_CASE
    case kIA32Word32AtomicPairSub: {
      DCHECK(VerifyOutputOfAtomicPairInstr(&i, instr));
      Label binop;
      __ bind(&binop);
      // Move memory operand into edx:eax
      __ mov(eax, i.MemoryOperand(2));
      __ mov(edx, i.NextMemoryOperand(2));
      // Save input registers temporarily on the stack.
      __ push(ebx);
      frame_access_state()->IncreaseSPDelta(1);
      i.MoveInstructionOperandToRegister(ebx, instr->InputAt(0));
      __ push(i.InputRegister(1));
      // Negate input in place
      __ neg(ebx);
      __ adc(i.InputRegister(1), 0);
      __ neg(i.InputRegister(1));
      // Add memory operand, negated input.
      __ add(ebx, eax);
      __ adc(i.InputRegister(1), edx);
      __ lock();
      __ cmpxchg8b(i.MemoryOperand(2));
      // Restore input registers
      __ pop(i.InputRegister(1));
      __ pop(ebx);
      frame_access_state()->IncreaseSPDelta(-1);
      __ j(not_equal, &binop);
      break;
    }
    case kAtomicLoadInt8:
    case kAtomicLoadUint8:
    case kAtomicLoadInt16:
    case kAtomicLoadUint16:
    case kAtomicLoadWord32:
    case kAtomicStoreWord8:
    case kAtomicStoreWord16:
    case kAtomicStoreWord32:
      UNREACHABLE();  // Won't be generated by instruction selector.
  }
  return kSuccess;
}

static Condition FlagsConditionToCondition(FlagsCondition condition) {
  switch (condition) {
    case kUnorderedEqual:
    case kEqual:
      return equal;
    case kUnorderedNotEqual:
    case kNotEqual:
      return not_equal;
    case kSignedLessThan:
      return less;
    case kSignedGreaterThanOrEqual:
      return greater_equal;
    case kSignedLessThanOrEqual:
      return less_equal;
    case kSignedGreaterThan:
      return greater;
    case kUnsignedLessThan:
      return below;
    case kUnsignedGreaterThanOrEqual:
      return above_equal;
    case kUnsignedLessThanOrEqual:
      return below_equal;
    case kUnsignedGreaterThan:
      return above;
    case kOverflow:
      return overflow;
    case kNotOverflow:
      return no_overflow;
    default:
      UNREACHABLE();
  }
}

// Assembles a branch after an instruction.
void CodeGenerator::AssembleArchBranch(Instruction* instr, BranchInfo* branch) {
  Label::Distance flabel_distance =
      branch->fallthru ? Label::kNear : Label::kFar;
  Label* tlabel = branch->true_label;
  Label* flabel = branch->false_label;
  if (branch->condition == kUnorderedEqual) {
    __ j(parity_even, flabel, flabel_distance);
  } else if (branch->condition == kUnorderedNotEqual) {
    __ j(parity_even, tlabel);
  }
  __ j(FlagsConditionToCondition(branch->condition), tlabel);

  // Add a jump if not falling through to the next block.
  if (!branch->fallthru) __ jmp(flabel);
}

void CodeGenerator::AssembleArchDeoptBranch(Instruction* instr,
                                            BranchInfo* branch) {
  AssembleArchBranch(instr, branch);
}

void CodeGenerator::AssembleArchJumpRegardlessOfAssemblyOrder(
    RpoNumber target) {
  __ jmp(GetLabel(target));
}

#if V8_ENABLE_WEBASSEMBLY
void CodeGenerator::AssembleArchTrap(Instruction* instr,
                                     FlagsCondition condition) {
  class OutOfLineTrap final : public OutOfLineCode {
   public:
    OutOfLineTrap(CodeGenerator* gen, Instruction* instr)
        : OutOfLineCode(gen), instr_(instr), gen_(gen) {}

    void Generate() final {
      IA32OperandConverter i(gen_, instr_);
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
      __ wasm_call(static_cast<Address>(trap_id), RelocInfo::WASM_STUB_CALL);
      ReferenceMap* reference_map =
          gen_->zone()->New<ReferenceMap>(gen_->zone());
      gen_->RecordSafepoint(reference_map);
      __ AssertUnreachable(AbortReason::kUnexpectedReturnFromWasmTrap);
    }

    Instruction* instr_;
    CodeGenerator* gen_;
  };
  auto ool = zone()->New<OutOfLineTrap>(this, instr);
  Label* tlabel = ool->entry();
  Label end;
  if (condition == kUnorderedEqual) {
    __ j(parity_even, &end, Label::kNear);
  } else if (condition == kUnorderedNotEqual) {
    __ j(parity_even, tlabel);
  }
  __ j(FlagsConditionToCondition(condition), tlabel);
  __ bind(&end);
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Assembles boolean materializations after an instruction.
void CodeGenerator::AssembleArchBoolean(Instruction* instr,
                                        FlagsCondition condition) {
  IA32OperandConverter i(this, instr);
  Label done;

  // Materialize a full 32-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  Label check;
  DCHECK_NE(0u, instr->OutputCount());
  Register reg = i.OutputRegister(instr->OutputCount() - 1);
  if (condition == kUnorderedEqual) {
    __ j(parity_odd, &check, Label::kNear);
    __ Move(reg, Immediate(0));
    __ jmp(&done, Label::kNear);
  } else if (condition == kUnorderedNotEqual) {
    __ j(parity_odd, &check, Label::kNear);
    __ mov(reg, Immediate(1));
    __ jmp(&done, Label::kNear);
  }
  Condition cc = FlagsConditionToCondition(condition);

  __ bind(&check);
  if (reg.is_byte_register()) {
    // setcc for byte registers (al, bl, cl, dl).
    __ setcc(cc, reg);
    __ movzx_b(reg, reg);
  } else {
    // Emit a branch to set a register to either 1 or 0.
    Label set;
    __ j(cc, &set, Label::kNear);
    __ Move(reg, Immediate(0));
    __ jmp(&done, Label::kNear);
    __ bind(&set);
    __ mov(reg, Immediate(1));
  }
  __ bind(&done);
}

void CodeGenerator::AssembleArchConditionalBoolean(Instruction* instr) {
  UNREACHABLE();
}

void CodeGenerator::AssembleArchConditionalBranch(Instruction* instr,
                                                  BranchInfo* branch) {
  UNREACHABLE();
}

void CodeGenerator::AssembleArchBinarySearchSwitch(Instruction* instr) {
  IA32OperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  std::vector<std::pair<int32_t, Label*>> cases;
  for (size_t index = 2; index < instr->InputCount(); index += 2) {
    cases.push_back({i.InputInt32(index + 0), GetLabel(i.InputRpo(index + 1))});
  }
  AssembleArchBinarySearchSwitchRange(input, i.InputRpo(1), cases.data(),
                                      cases.data() + cases.size());
}

void CodeGenerator::AssembleArchTableSwitch(Instruction* instr) {
  IA32OperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  size_t const case_count = instr->InputCount() - 2;
  Label** cases = zone()->AllocateArray<Label*>(case_count);
  for (size_t index = 0; index < case_count; ++index) {
    cases[index] = GetLabel(i.InputRpo(index + 2));
  }
  Label* const table = AddJumpTable(cases, case_count);
  __ cmp(input, Immediate(case_count));
  __ j(above_equal, GetLabel(i.InputRpo(1)));
  __ jmp(Operand::JumpTable(input, times_system_pointer_size, table));
}

void CodeGenerator::AssembleArchSelect(Instruction* instr,
                                       FlagsCondition condition) {
  UNIMPLEMENTED();
}

// The calling convention for JSFunctions on IA32 passes arguments on the
// stack and the JSFunction and context in EDI and ESI, respectively, thus
// the steps of the call look as follows:

// --{ before the call instruction }--------------------------------------------
//                                                         |  caller frame |
//                                                         ^ esp           ^ ebp

// --{ push arguments and setup ESI, EDI }--------------------------------------
//                                       | args + receiver |  caller frame |
//                                       ^ esp                             ^ ebp
//                 [edi = JSFunction, esi = context]

// --{ call [edi + kCodeEntryOffset] }------------------------------------------
//                                 | RET | args + receiver |  caller frame |
//                                 ^ esp                                   ^ ebp

// =={ prologue of called function }============================================
// --{ push ebp }---------------------------------------------------------------
//                            | FP | RET | args + receiver |  caller frame |
//                            ^ esp                                        ^ ebp

// --{ mov ebp, esp }-----------------------------------------------------------
//                            | FP | RET | args + receiver |  caller frame |
//                            ^ ebp,esp

// --{ push esi }---------------------------------------------------------------
//                      | CTX | FP | RET | args + receiver |  caller frame |
//                      ^esp  ^ ebp

// --{ push edi }---------------------------------------------------------------
//                | FNC | CTX | FP | RET | args + receiver |  caller frame |
//                ^esp        ^ ebp

// --{ subi esp, #N }-----------------------------------------------------------
// | callee frame | FNC | CTX | FP | RET | args + receiver |  caller frame |
// ^esp                       ^ ebp

// =={ body of called function }================================================

// =={ epilogue of called function }============================================
// --{ mov esp, ebp }-----------------------------------------------------------
//                            | FP | RET | args + receiver |  caller frame |
//                            ^ esp,ebp

// --{ pop ebp }-----------------------------------------------------------
// |                               | RET | args + receiver |  caller frame |
//                                 ^ esp                                   ^ ebp

// --{ ret #A+1 }-----------------------------------------------------------
// |                                                       |  caller frame |
//                                                         ^ esp           ^ ebp

// Runtime function calls are accomplished by doing a stub call to the
// CEntry (a real code object). On IA32 passes arguments on the
// stack, the number of arguments in EAX, the address of the runtime function
// in EBX, and the context in ESI.

// --{ before the call instruction }--------------------------------------------
//                                                         |  caller frame |
//                                                         ^ esp           ^ ebp

// --{ push arguments and setup EAX, EBX, and ESI }-----------------------------
//                                       | args + receiver |  caller frame |
//                                       ^ esp                             ^ ebp
//              [eax = #args, ebx = runtime function, esi = context]

// --{ call #CEntry }-----------------------------------------------------------
//                                 | RET | args + receiver |  caller frame |
//                                 ^ esp                                   ^ ebp

// =={ body of runtime function }===============================================

// --{ runtime returns }--------------------------------------------------------
//                                                         |  caller frame |
//                                                         ^ esp           ^ ebp

// Other custom linkages (e.g. for calling directly into and out of C++) may
// need to save callee-saved registers on the stack, which is done in the
// function prologue of generated code.

// --{ before the call instruction }--------------------------------------------
//                                                         |  caller frame |
//                                                         ^ esp           ^ ebp

// --{ set up arguments in registers on stack }---------------------------------
//                                                  | args |  caller frame |
//                                                  ^ esp                  ^ ebp
//                  [r0 = arg0, r1 = arg1, ...]

// --{ call code }--------------------------------------------------------------
//                                            | RET | args |  caller frame |
//                                            ^ esp                        ^ ebp

// =={ prologue of called function }============================================
// --{ push ebp }---------------------------------------------------------------
//                                       | FP | RET | args |  caller frame |
//                                       ^ esp                             ^ ebp

// --{ mov ebp, esp }-----------------------------------------------------------
//                                       | FP | RET | args |  caller frame |
//                                       ^ ebp,esp

// --{ save registers }---------------------------------------------------------
//                                | regs | FP | RET | args |  caller frame |
//                                ^ esp  ^ ebp

// --{ subi esp, #N }-----------------------------------------------------------
//                 | callee frame | regs | FP | RET | args |  caller frame |
//                 ^esp                  ^ ebp

// =={ body of called function }================================================

// =={ epilogue of called function }============================================
// --{ restore registers }------------------------------------------------------
//                                | regs | FP | RET | args |  caller frame |
//                                ^ esp  ^ ebp

// --{ mov esp, ebp }-----------------------------------------------------------
//                                       | FP | RET | args |  caller frame |
//                                       ^ esp,ebp

// --{ pop ebp }----------------------------------------------------------------
//                                            | RET | args |  caller frame |
//                                            ^ esp                        ^ ebp

void CodeGenerator::FinishFrame(Frame* frame) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();
  const RegList saves = call_descriptor->CalleeSavedRegisters();
  if (!saves.is_empty()) {  // Save callee-saved registers.
    DCHECK(!info()->is_osr());
    frame->AllocateSavedCalleeRegisterSlots(saves.Count());
  }
}

void CodeGenerator::AssembleConstructFrame() {
  auto call_descriptor = linkage()->GetIncomingDescriptor();
  if (frame_access_state()->has_frame()) {
    if (call_descriptor->IsCFunctionCall()) {
      __ push(ebp);
      __ mov(ebp, esp);
#if V8_ENABLE_WEBASSEMBLY
      if (info()->GetOutputStackFrameType() == StackFrame::C_WASM_ENTRY) {
        __ Push(Immediate(StackFrame::TypeToMarker(StackFrame::C_WASM_ENTRY)));
        // Reserve stack space for saving the c_entry_fp later.
        __ AllocateStackSpace(kSystemPointerSize);
      }
#endif  // V8_ENABLE_WEBASSEMBLY
    } else if (call_descriptor->IsJSFunctionCall()) {
      __ Prologue();
    } else {
      __ StubPrologue(info()->GetOutputStackFrameType());
#if V8_ENABLE_WEBASSEMBLY
      if (call_descriptor->IsWasmFunctionCall() ||
          call_descriptor->IsWasmImportWrapper() ||
          call_descriptor->IsWasmCapiFunction()) {
        // For import wrappers and C-API functions, this stack slot is only used
        // for printing stack traces in V8. Also, it holds a WasmImportData
        // instead of the instance itself, which is taken care of in the frames
        // accessors.
        __ push(kWasmInstanceRegister);
      }
      if (call_descriptor->IsWasmImportWrapper()) {
        // If the wrapper is running on a secondary stack, it will switch to the
        // central stack and fill these slots with the central stack pointer and
        // secondary stack limit. Otherwise the slots remain empty.
        static_assert(WasmImportWrapperFrameConstants::kCentralStackSPOffset ==
                      -12);
        static_assert(
            WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset == -16);
        __ push(Immediate(0));
        __ push(Immediate(0));
      } else if (call_descriptor->IsWasmCapiFunction()) {
        // Reserve space for saving the PC later.
        __ AllocateStackSpace(kSystemPointerSize);
      }
#endif  // V8_ENABLE_WEBASSEMBLY
    }
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
        Register scratch = esi;
        __ push(scratch);
        __ mov(scratch, esp);
        __ sub(scratch, Immediate(required_slots * kSystemPointerSize));
        __ CompareStackLimit(scratch, StackLimitKind::kRealStackLimit);
        __ pop(scratch);
        __ j(above_equal, &done, Label::kNear);
      }

      __ wasm_call(static_cast<Address>(Builtin::kWasmStackOverflow),
                   RelocInfo::WASM_STUB_CALL);
      // The call does not return, hence we can ignore any references and just
      // define an empty safepoint.
      ReferenceMap* reference_map = zone()->New<ReferenceMap>(zone());
      RecordSafepoint(reference_map);
      __ AssertUnreachable(AbortReason::kUnexpectedReturnFromWasmTrap);
      __ bind(&done);
    }
#endif  // V8_ENABLE_WEBASSEMBLY

    // Skip callee-saved and return slots, which are created below.
    required_slots -= saves.Count();
    required_slots -= frame()->GetReturnSlotCount();
    if (required_slots > 0) {
      __ AllocateStackSpace(required_slots * kSystemPointerSize);
    }
  }

  if (!saves.is_empty()) {  // Save callee-saved registers.
    DCHECK(!info()->is_osr());
    for (Register reg : base::Reversed(saves)) {
      __ push(reg);
    }
  }

  // Allocate return slots (located after callee-saved).
  if (frame()->GetReturnSlotCount() > 0) {
    __ AllocateStackSpace(frame()->GetReturnSlotCount() * kSystemPointerSize);
  }

  for (int spill_slot : frame()->tagged_slots()) {
    FrameOffset offset = frame_access_state()->GetFrameOffset(spill_slot);
    DCHECK(offset.from_frame_pointer());
    __ mov(Operand(ebp, offset.offset()), Immediate(0));
  }
}

void CodeGenerator::AssembleReturn(InstructionOperand* additional_pop_count) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  const RegList saves = call_descriptor->CalleeSavedRegisters();
  // Restore registers.
  if (!saves.is_empty()) {
    const int returns = frame()->GetReturnSlotCount();
    if (returns != 0) {
      __ add(esp, Immediate(returns * kSystemPointerSize));
    }
    for (Register reg : saves) {
      __ pop(reg);
    }
  }

  IA32OperandConverter g(this, nullptr);
  int parameter_slots = static_cast<int>(call_descriptor->ParameterSlotCount());

  // {aditional_pop_count} is only greater than zero if {parameter_slots = 0}.
  // Check RawMachineAssembler::PopAndReturn.
  if (parameter_slots != 0) {
    if (additional_pop_count->IsImmediate()) {
      DCHECK_EQ(g.ToConstant(additional_pop_count).ToInt32(), 0);
    } else if (v8_flags.debug_code) {
      __ cmp(g.ToRegister(additional_pop_count), Immediate(0));
      __ Assert(equal, AbortReason::kUnexpectedAdditionalPopValue);
    }
  }

  Register argc_reg = ecx;
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
    // Canonicalize JSFunction return sites for now if they always have the same
    // number of return args.
    if (additional_pop_count->IsImmediate() &&
        g.ToConstant(additional_pop_count).ToInt32() == 0) {
      if (return_label_.is_bound()) {
        __ jmp(&return_label_);
        return;
      } else {
        __ bind(&return_label_);
      }
    }
    if (drop_jsargs) {
      // Get the actual argument count.
      __ mov(argc_reg, Operand(ebp, StandardFrameConstants::kArgCOffset));
      DCHECK(!call_descriptor->CalleeSavedRegisters().has(argc_reg));
    }
    AssembleDeconstructFrame();
  }

  if (drop_jsargs) {
    // We must pop all arguments from the stack (including the receiver).
    // The number of arguments without the receiver is
    // max(argc_reg, parameter_slots-1), and the receiver is added in
    // DropArguments().
    Label mismatch_return;
    Register scratch_reg = edx;
    DCHECK_NE(argc_reg, scratch_reg);
    DCHECK(!call_descriptor->CalleeSavedRegisters().has(argc_reg));
    DCHECK(!call_descriptor->CalleeSavedRegisters().has(scratch_reg));
    __ cmp(argc_reg, Immediate(parameter_slots));
    __ j(greater, &mismatch_return, Label::kNear);
    __ Ret(parameter_slots * kSystemPointerSize, scratch_reg);
    __ bind(&mismatch_return);
    __ DropArguments(argc_reg, scratch_reg);
    // We use a return instead of a jump for better return address prediction.
    __ Ret();
  } else if (additional_pop_count->IsImmediate()) {
    int additional_count = g.ToConstant(additional_pop_count).ToInt32();
    size_t pop_size = (parameter_slots + additional_count) * kSystemPointerSize;
    if (is_uint16(pop_size)) {
      // Avoid the additional scratch register, it might clobber the
      // CalleeSavedRegisters.
      __ ret(static_cast<int>(pop_size));
    } else {
      Register scratch_reg = ecx;
      DCHECK(!call_descriptor->CalleeSavedRegisters().has(scratch_reg));
      CHECK_LE(pop_size, static_cast<size_t>(std::numeric_limits<int>::max()));
      __ Ret(static_cast<int>(pop_size), scratch_reg);
    }
  } else {
    Register pop_reg = g.ToRegister(additional_pop_count);
    Register scratch_reg = pop_reg == ecx ? edi : ecx;
    DCHECK(!call_descriptor->CalleeSavedRegisters().has(scratch_reg));
    DCHECK(!call_descriptor->CalleeSavedRegisters().has(pop_reg));
    int pop_size = static_cast<int>(parameter_slots * kSystemPointerSize);
    __ PopReturnAddressTo(scratch_reg);
    __ lea(esp, Operand(esp, pop_reg, times_system_pointer_size,
                        static_cast<int>(pop_size)));
    __ PushReturnAddressFrom(scratch_reg);
    __ Ret();
  }
}

void CodeGenerator::FinishCode() {}

void CodeGenerator::PrepareForDeoptimizationExits(
    ZoneDeque<DeoptimizationExit*>* exits) {}

AllocatedOperand CodeGenerator::Push(InstructionOperand* source) {
  auto rep = LocationOperand::cast(source)->representation();
  int new_slots = ElementSizeInPointers(rep);
  IA32OperandConverter g(this, nullptr);
  int last_frame_slot_id =
      frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
  int sp_delta = frame_access_state_->sp_delta();
  int slot_id = last_frame_slot_id + sp_delta + new_slots;
  AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
  if (source->IsRegister()) {
    __ push(g.ToRegister(source));
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else if (source->IsStackSlot() || source->IsFloatStackSlot()) {
    __ push(g.ToOperand(source));
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else {
    // No push instruction for this operand type. Bump the stack pointer and
    // assemble the move.
    __ sub(esp, Immediate(new_slots * kSystemPointerSize));
    frame_access_state()->IncreaseSPDelta(new_slots);
    AssembleMove(source, &stack_slot);
  }
  temp_slots_ += new_slots;
  return stack_slot;
}

void CodeGenerator::Pop(InstructionOperand* dest, MachineRepresentation rep) {
  IA32OperandConverter g(this, nullptr);
  int dropped_slots = ElementSizeInPointers(rep);
  if (dest->IsRegister()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ pop(g.ToRegister(dest));
  } else if (dest->IsStackSlot() || dest->IsFloatStackSlot()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ pop(g.ToOperand(dest));
  } else {
    int last_frame_slot_id =
        frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
    int sp_delta = frame_access_state_->sp_delta();
    int slot_id = last_frame_slot_id + sp_delta;
    AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
    AssembleMove(&stack_slot, dest);
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ add(esp, Immediate(dropped_slots * kSystemPointerSize));
  }
  temp_slots_ -= dropped_slots;
}

void CodeGenerator::PopTempStackSlots() {
  if (temp_slots_ > 0) {
    frame_access_state()->IncreaseSPDelta(-temp_slots_);
    __ add(esp, Immediate(temp_slots_ * kSystemPointerSize));
    temp_slots_ = 0;
  }
}

void CodeGenerator::MoveToTempLocation(InstructionOperand* source,
                                       MachineRepresentation rep) {
  // Must be kept in sync with {MoveTempLocationTo}.
  DCHECK(!source->IsImmediate());
  if ((IsFloatingPoint(rep) &&
       !move_cycle_.pending_double_scratch_register_use)) {
    // The scratch double register is available.
    AllocatedOperand scratch(LocationOperand::REGISTER, rep,
                             kScratchDoubleReg.code());
    AssembleMove(source, &scratch);
  } else {
    // The scratch register blocked by pending moves. Use the stack instead.
    Push(source);
  }
}

void CodeGenerator::MoveTempLocationTo(InstructionOperand* dest,
                                       MachineRepresentation rep) {
  if (IsFloatingPoint(rep) &&
      !move_cycle_.pending_double_scratch_register_use) {
    AllocatedOperand scratch(LocationOperand::REGISTER, rep,
                             kScratchDoubleReg.code());
    AssembleMove(&scratch, dest);
  } else {
    Pop(dest, rep);
  }
  move_cycle_ = MoveCycleState();
}

void CodeGenerator::SetPendingMove(MoveOperands* move) {
  InstructionOperand* source = &move->source();
  InstructionOperand* destination = &move->destination();
  MoveType::Type move_type = MoveType::InferMove(source, destination);
  if (move_type == MoveType::kStackToStack) {
    if (!source->IsStackSlot()) {
      move_cycle_.pending_double_scratch_register_use = true;
    }
    return;
  }
}

void CodeGenerator::AssembleMove(InstructionOperand* source,
                                 InstructionOperand* destination) {
  IA32OperandConverter g(this, nullptr);
  // Dispatch on the source and destination operand kinds.
  switch (MoveType::InferMove(source, destination)) {
    case MoveType::kRegisterToRegister:
      if (source->IsRegister()) {
        __ mov(g.ToRegister(destination), g.ToRegister(source));
      } else {
        DCHECK(source->IsFPRegister());
        __ Movaps(g.ToDoubleRegister(destination), g.ToDoubleRegister(source));
      }
      return;
    case MoveType::kRegisterToStack: {
      Operand dst = g.ToOperand(destination);
      if (source->IsRegister()) {
        __ mov(dst, g.ToRegister(source));
      } else {
        DCHECK(source->IsFPRegister());
        XMMRegister src = g.ToDoubleRegister(source);
        MachineRepresentation rep =
            LocationOperand::cast(source)->representation();
        if (rep == MachineRepresentation::kFloat32) {
          __ Movss(dst, src);
        } else if (rep == MachineRepresentation::kFloat64) {
          __ Movsd(dst, src);
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, rep);
          __ Movups(dst, src);
        }
      }
      return;
    }
    case MoveType::kStackToRegister: {
      Operand src = g.ToOperand(source);
      if (source->IsStackSlot()) {
        __ mov(g.ToRegister(destination), src);
      } else {
        DCHECK(source->IsFPStackSlot());
        XMMRegister dst = g.ToDoubleRegister(destination);
        MachineRepresentation rep =
            LocationOperand::cast(source)->representation();
        if (rep == MachineRepresentation::kFloat32) {
          __ Movss(dst, src);
        } else if (rep == MachineRepresentation::kFloat64) {
          __ Movsd(dst, src);
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, rep);
          __ Movups(dst, src);
        }
      }
      return;
    }
    case MoveType::kStackToStack: {
      Operand src = g.ToOperand(source);
      Operand dst = g.ToOperand(destination);
      if (source->IsStackSlot()) {
        __ push(src);
        __ pop(dst);
      } else {
        MachineRepresentation rep =
            LocationOperand::cast(source)->representation();
        if (rep == MachineRepresentation::kFloat32) {
          __ Movss(kScratchDoubleReg, src);
          __ Movss(dst, kScratchDoubleReg);
        } else if (rep == MachineRepresentation::kFloat64) {
          __ Movsd(kScratchDoubleReg, src);
          __ Movsd(dst, kScratchDoubleReg);
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, rep);
          __ Movups(kScratchDoubleReg, src);
          __ Movups(dst, kScratchDoubleReg);
        }
      }
      return;
    }
    case MoveType::kConstantToRegister: {
      Constant src = g.ToConstant(source);
      if (destination->IsRegister()) {
        Register dst = g.ToRegister(destination);
        if (src.type() == Constant::kHeapObject) {
          __ Move(dst, src.ToHeapObject());
        } else if (src.type() == Constant::kExternalReference) {
          __ Move(dst, Immediate(src.ToExternalReference()));
        } else {
          __ Move(dst, g.ToImmediate(source));
        }
      } else {
        DCHECK(destination->IsFPRegister());
        XMMRegister dst = g.ToDoubleRegister(destination);
        if (src.type() == Constant::kFloat32) {
          // TODO(turbofan): Can we do better here?
          __ Move(dst, src.ToFloat32AsInt());
        } else {
          DCHECK_EQ(src.type(), Constant::kFloat64);
          __ Move(dst, src.ToFloat64().AsUint64());
        }
      }
      return;
    }
    case MoveType::kConstantToStack: {
      Constant src = g.ToConstant(source);
      Operand dst = g.ToOperand(destination);
      if (destination->IsStackSlot()) {
        __ Move(dst, g.ToImmediate(source));
      } else {
        DCHECK(destination->IsFPStackSlot());
        if (src.type() == Constant::kFloat32) {
          __ Move(dst, Immediate(src.ToFloat32AsInt()));
        } else {
          DCHECK_EQ(src.type(), Constant::kFloat64);
          uint64_t constant_value = src.ToFloat64().AsUint64();
          uint32_t lower = static_cast<uint32_t>(constant_value);
          uint32_t upper = static_cast<uint32_t>(constant_value >> 32);
          Operand dst0 = dst;
          Operand dst1 = g.ToOperand(destination, kSystemPointerSize);
          __ Move(dst0, Immediate(lower));
          __ Move(dst1, Immediate(upper));
        }
      }
      return;
    }
  }
  UNREACHABLE();
}

void CodeGenerator::AssembleSwap(InstructionOperand* source,
                                 InstructionOperand* destination) {
  IA32OperandConverter g(this, nullptr);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  switch (MoveType::InferSwap(source, destination)) {
    case MoveType::kRegisterToRegister: {
      if (source->IsRegister()) {
        Register src = g.ToRegister(source);
        Register dst = g.ToRegister(destination);
        __ push(src);
        __ mov(src, dst);
        __ pop(dst);
      } else {
        DCHECK(source->IsFPRegister());
        XMMRegister src = g.ToDoubleRegister(source);
        XMMRegister dst = g.ToDoubleRegister(destination);
        __ Movaps(kScratchDoubleReg, src);
        __ Movaps(src, dst);
        __ Movaps(dst, kScratchDoubleReg);
      }
      return;
    }
    case MoveType::kRegisterToStack: {
      if (source->IsRegister()) {
        Register src = g.ToRegister(source);
        __ push(src);
        frame_access_state()->IncreaseSPDelta(1);
        Operand dst = g.ToOperand(destination);
        __ mov(src, dst);
        frame_access_state()->IncreaseSPDelta(-1);
        dst = g.ToOperand(destination);
        __ pop(dst);
      } else {
        DCHECK(source->IsFPRegister());
        XMMRegister src = g.ToDoubleRegister(source);
        Operand dst = g.ToOperand(destination);
        MachineRepresentation rep =
            LocationOperand::cast(source)->representation();
        if (rep == MachineRepresentation::kFloat32) {
          __ Movss(kScratchDoubleReg, dst);
          __ Movss(dst, src);
          __ Movaps(src, kScratchDoubleReg);
        } else if (rep == MachineRepresentation::kFloat64) {
          __ Movsd(kScratchDoubleReg, dst);
          __ Movsd(dst, src);
          __ Movaps(src, kScratchDoubleReg);
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, rep);
          __ Movups(kScratchDoubleReg, dst);
          __ Movups(dst, src);
          __ Movups(src, kScratchDoubleReg);
        }
      }
      return;
    }
    case MoveType::kStackToStack: {
      if (source->IsStackSlot()) {
        Operand dst1 = g.ToOperand(destination);
        __ push(dst1);
        frame_access_state()->IncreaseSPDelta(1);
        Operand src1 = g.ToOperand(source);
        __ push(src1);
        Operand dst2 = g.ToOperand(destination);
        __ pop(dst2);
        frame_access_state()->IncreaseSPDelta(-1);
        Operand src2 = g.ToOperand(source);
        __ pop(src2);
      } else {
        DCHECK(source->IsFPStackSlot());
        Operand src0 = g.ToOperand(source);
        Operand dst0 = g.ToOperand(destination);
        MachineRepresentation rep =
            LocationOperand::cast(source)->representation();
        if (rep == MachineRepresentation::kFloat32) {
          __ Movss(kScratchDoubleReg, dst0);  // Save dst in scratch register.
          __ push(src0);  // Then use stack to copy src to destination.
          __ pop(dst0);
          __ Movss(src0, kScratchDoubleReg);
        } else if (rep == MachineRepresentation::kFloat64) {
          __ Movsd(kScratchDoubleReg, dst0);  // Save dst in scratch register.
          __ push(src0);  // Then use stack to copy src to destination.
          __ pop(dst0);
          __ push(g.ToOperand(source, kSystemPointerSize));
          __ pop(g.ToOperand(destination, kSystemPointerSize));
          __ Movsd(src0, kScratchDoubleReg);
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, rep);
          __ Movups(kScratchDoubleReg, dst0);  // Save dst in scratch register.
          __ push(src0);  // Then use stack to copy src to destination.
          __ pop(dst0);
          __ push(g.ToOperand(source, kSystemPointerSize));
          __ pop(g.ToOperand(destination, kSystemPointerSize));
          __ push(g.ToOperand(source, 2 * kSystemPointerSize));
          __ pop(g.ToOperand(destination, 2 * kSystemPointerSize));
          __ push(g.ToOperand(source, 3 * kSystemPointerSize));
          __ pop(g.ToOperand(destination, 3 * kSystemPointerSize));
          __ Movups(src0, kScratchDoubleReg);
        }
      }
      return;
    }
    default:
      UNREACHABLE();
  }
}

void CodeGenerator::AssembleJumpTable(Label** targets, size_t target_count) {
  for (size_t index = 0; index < target_count; ++index) {
    __ dd(targets[index]);
  }
}

#undef __
#undef kScratchDoubleReg
#undef ASSEMBLE_COMPARE
#undef ASSEMBLE_IEEE754_BINOP
#undef ASSEMBLE_IEEE754_UNOP
#undef ASSEMBLE_BINOP
#undef ASSEMBLE_ATOMIC_BINOP
#undef ASSEMBLE_I64ATOMIC_BINOP
#undef ASSEMBLE_MOVX
#undef ASSEMBLE_SIMD_PUNPCK_SHUFFLE
#undef ASSEMBLE_SIMD_IMM_SHUFFLE
#undef ASSEMBLE_SIMD_ALL_TRUE
#undef ASSEMBLE_SIMD_SHIFT
#undef ASSEMBLE_SIMD_PINSR

}  // namespace compiler
}  // namespace internal
}  // namespace v8
