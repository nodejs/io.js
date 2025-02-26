// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/backend/instruction-scheduler.h"

namespace v8 {
namespace internal {
namespace compiler {

bool InstructionScheduler::SchedulerSupported() { return true; }

int InstructionScheduler::GetTargetInstructionFlags(
    const Instruction* instr) const {
  switch (instr->arch_opcode()) {
    case kX64TraceInstruction:
      return kHasSideEffect;
    case kX64Add:
    case kX64Add32:
    case kX64And:
    case kX64And32:
    case kX64Cmp:
    case kX64Cmp32:
    case kX64Cmp16:
    case kX64Cmp8:
    case kX64Test:
    case kX64Test32:
    case kX64Test16:
    case kX64Test8:
    case kX64Or:
    case kX64Or32:
    case kX64Xor:
    case kX64Xor32:
    case kX64Sub:
    case kX64Sub32:
    case kX64Imul:
    case kX64Imul32:
    case kX64ImulHigh32:
    case kX64UmulHigh32:
    case kX64ImulHigh64:
    case kX64UmulHigh64:
    case kX64Not:
    case kX64Not32:
    case kX64Neg:
    case kX64Neg32:
    case kX64Shl:
    case kX64Shl32:
    case kX64Shr:
    case kX64Shr32:
    case kX64Sar:
    case kX64Sar32:
    case kX64Rol:
    case kX64Rol32:
    case kX64Ror:
    case kX64Ror32:
    case kX64Lzcnt:
    case kX64Lzcnt32:
    case kX64Tzcnt:
    case kX64Tzcnt32:
    case kX64Popcnt:
    case kX64Popcnt32:
    case kX64Bswap:
    case kX64Bswap32:
    case kSSEFloat32Cmp:
    case kSSEFloat32Add:
    case kSSEFloat32Sub:
    case kSSEFloat32Mul:
    case kSSEFloat32Div:
    case kSSEFloat32Sqrt:
    case kSSEFloat32Round:
    case kSSEFloat32ToFloat64:
    case kSSEFloat64Cmp:
    case kSSEFloat64Add:
    case kSSEFloat64Sub:
    case kSSEFloat64Mul:
    case kSSEFloat64Div:
    case kSSEFloat64Mod:
    case kSSEFloat64Sqrt:
    case kSSEFloat64Round:
    case kSSEFloat32Max:
    case kSSEFloat64Max:
    case kSSEFloat32Min:
    case kSSEFloat64Min:
    case kSSEFloat64ToFloat32:
    case kSSEFloat64ToFloat16RawBits:
    case kSSEFloat16RawBitsToFloat64:
    case kSSEFloat32ToInt32:
    case kSSEFloat32ToUint32:
    case kSSEFloat64ToInt32:
    case kSSEFloat64ToUint32:
    case kSSEFloat64ToInt64:
    case kSSEFloat32ToInt64:
    case kSSEFloat64ToUint64:
    case kSSEFloat32ToUint64:
    case kSSEInt32ToFloat64:
    case kSSEInt32ToFloat32:
    case kSSEInt64ToFloat32:
    case kSSEInt64ToFloat64:
    case kSSEUint64ToFloat32:
    case kSSEUint64ToFloat64:
    case kSSEUint32ToFloat64:
    case kSSEUint32ToFloat32:
    case kSSEFloat64ExtractLowWord32:
    case kSSEFloat64ExtractHighWord32:
    case kSSEFloat64InsertLowWord32:
    case kSSEFloat64InsertHighWord32:
    case kSSEFloat64LoadLowWord32:
    case kSSEFloat64SilenceNaN:
    case kAVXFloat32Cmp:
    case kAVXFloat32Add:
    case kAVXFloat32Sub:
    case kAVXFloat32Mul:
    case kAVXFloat32Div:
    case kAVXFloat64Cmp:
    case kAVXFloat64Add:
    case kAVXFloat64Sub:
    case kAVXFloat64Mul:
    case kAVXFloat64Div:
    case kX64Float64Abs:
    case kX64Float64Neg:
    case kX64Float32Abs:
    case kX64Float32Neg:
    case kX64BitcastFI:
    case kX64BitcastDL:
    case kX64BitcastIF:
    case kX64BitcastLD:
    case kX64Lea32:
    case kX64Lea:
    case kX64Dec32:
    case kX64Inc32:
    case kX64Pinsrb:
    case kX64Pinsrw:
    case kX64Pinsrd:
    case kX64Pinsrq:
    case kX64Cvttps2dq:
    case kX64Cvttpd2dq:
    case kX64I32x4TruncF64x2UZero:
    case kX64I32x4TruncF32x4U:
    case kX64I32x8TruncF32x8U:
    case kX64FSplat:
    case kX64FExtractLane:
    case kX64FReplaceLane:
    case kX64FAbs:
    case kX64FNeg:
    case kX64FSqrt:
    case kX64FAdd:
    case kX64FSub:
    case kX64FMul:
    case kX64FDiv:
    case kX64FMin:
    case kX64FMax:
    case kX64FEq:
    case kX64FNe:
    case kX64FLt:
    case kX64FLe:
    case kX64F64x2Qfma:
    case kX64F64x2Qfms:
    case kX64F64x4Qfma:
    case kX64F64x4Qfms:
    case kX64Minpd:
    case kX64Maxpd:
    case kX64F32x8Pmin:
    case kX64F32x8Pmax:
    case kX64F64x4Pmin:
    case kX64F64x4Pmax:
    case kX64F64x2Round:
    case kX64F64x2ConvertLowI32x4S:
    case kX64F64x4ConvertI32x4S:
    case kX64F64x2ConvertLowI32x4U:
    case kX64F64x2PromoteLowF32x4:
    case kX64F32x4SConvertI32x4:
    case kX64F32x8SConvertI32x8:
    case kX64F32x4UConvertI32x4:
    case kX64F32x8UConvertI32x8:
    case kX64F32x4Qfma:
    case kX64F32x4Qfms:
    case kX64F32x8Qfma:
    case kX64F32x8Qfms:
    case kX64Minps:
    case kX64Maxps:
    case kX64F32x4Round:
    case kX64F32x4DemoteF64x2Zero:
    case kX64F32x4DemoteF64x4:
    case kX64F16x8Round:
    case kX64I16x8SConvertF16x8:
    case kX64I16x8UConvertF16x8:
    case kX64F16x8SConvertI16x8:
    case kX64F16x8UConvertI16x8:
    case kX64F16x8DemoteF32x4Zero:
    case kX64F16x8DemoteF64x2Zero:
    case kX64F32x4PromoteLowF16x8:
    case kX64F16x8Qfma:
    case kX64F16x8Qfms:
    case kX64Minph:
    case kX64Maxph:
    case kX64ISplat:
    case kX64IExtractLane:
    case kX64IAbs:
    case kX64INeg:
    case kX64IBitMask:
    case kX64IShl:
    case kX64IShrS:
    case kX64IAdd:
    case kX64ISub:
    case kX64IMul:
    case kX64IEq:
    case kX64IGtS:
    case kX64IGeS:
    case kX64INe:
    case kX64IShrU:
    case kX64I64x2ExtMulLowI32x4S:
    case kX64I64x2ExtMulHighI32x4S:
    case kX64I64x4ExtMulI32x4S:
    case kX64I64x2ExtMulLowI32x4U:
    case kX64I64x2ExtMulHighI32x4U:
    case kX64I64x4ExtMulI32x4U:
    case kX64I64x2SConvertI32x4Low:
    case kX64I64x2SConvertI32x4High:
    case kX64I64x4SConvertI32x4:
    case kX64I64x2UConvertI32x4Low:
    case kX64I64x2UConvertI32x4High:
    case kX64I64x4UConvertI32x4:
    case kX64I32x4SConvertF32x4:
    case kX64I32x8SConvertF32x8:
    case kX64I32x4SConvertI16x8Low:
    case kX64I32x4SConvertI16x8High:
    case kX64I32x8SConvertI16x8:
    case kX64IMinS:
    case kX64IMaxS:
    case kX64I32x4UConvertF32x4:
    case kX64I32x8UConvertF32x8:
    case kX64I32x4UConvertI16x8Low:
    case kX64I32x4UConvertI16x8High:
    case kX64I32x8UConvertI16x8:
    case kX64IMinU:
    case kX64IMaxU:
    case kX64IGtU:
    case kX64IGeU:
    case kX64I32x4DotI16x8S:
    case kX64I32x8DotI16x16S:
    case kX64I32x4DotI8x16I7x16AddS:
    case kX64I32x8DotI8x32I7x32AddS:
    case kX64I32x4ExtMulLowI16x8S:
    case kX64I32x4ExtMulHighI16x8S:
    case kX64I32x8ExtMulI16x8S:
    case kX64I32x4ExtMulLowI16x8U:
    case kX64I32x4ExtMulHighI16x8U:
    case kX64I32x8ExtMulI16x8U:
    case kX64I32x4ExtAddPairwiseI16x8S:
    case kX64I32x8ExtAddPairwiseI16x16S:
    case kX64I32x4ExtAddPairwiseI16x8U:
    case kX64I32x8ExtAddPairwiseI16x16U:
    case kX64I32x4TruncSatF64x2SZero:
    case kX64I32x4TruncSatF64x2UZero:
    case kX64I32X4ShiftZeroExtendI8x16:
    case kX64IExtractLaneS:
    case kX64I16x8SConvertI8x16Low:
    case kX64I16x8SConvertI8x16High:
    case kX64I16x16SConvertI8x16:
    case kX64I16x8SConvertI32x4:
    case kX64I16x16SConvertI32x8:
    case kX64IAddSatS:
    case kX64ISubSatS:
    case kX64I16x8UConvertI8x16Low:
    case kX64I16x8UConvertI8x16High:
    case kX64I16x16UConvertI8x16:
    case kX64I16x8UConvertI32x4:
    case kX64I16x16UConvertI32x8:
    case kX64IAddSatU:
    case kX64ISubSatU:
    case kX64IRoundingAverageU:
    case kX64I16x8ExtMulLowI8x16S:
    case kX64I16x8ExtMulHighI8x16S:
    case kX64I16x16ExtMulI8x16S:
    case kX64I16x8ExtMulLowI8x16U:
    case kX64I16x8ExtMulHighI8x16U:
    case kX64I16x16ExtMulI8x16U:
    case kX64I16x8ExtAddPairwiseI8x16S:
    case kX64I16x16ExtAddPairwiseI8x32S:
    case kX64I16x8ExtAddPairwiseI8x16U:
    case kX64I16x16ExtAddPairwiseI8x32U:
    case kX64I16x8Q15MulRSatS:
    case kX64I16x8RelaxedQ15MulRS:
    case kX64I16x8DotI8x16I7x16S:
    case kX64I16x16DotI8x32I7x32S:
    case kX64I8x16SConvertI16x8:
    case kX64I8x32SConvertI16x16:
    case kX64I8x16UConvertI16x8:
    case kX64I8x32UConvertI16x16:
    case kX64SAnd:
    case kX64SOr:
    case kX64SXor:
    case kX64SNot:
    case kX64SSelect:
    case kX64S128Const:
    case kX64S256Const:
    case kX64SZero:
    case kX64SAllOnes:
    case kX64SAndNot:
    case kX64IAllTrue:
    case kX64I8x16Swizzle:
    case kX64Vpshufd:
    case kX64I8x16Shuffle:
    case kX64I8x16Popcnt:
    case kX64Shufps:
    case kX64S32x4Rotate:
    case kX64S32x4Swizzle:
    case kX64S32x4Shuffle:
    case kX64S16x8Blend:
    case kX64S16x8HalfShuffle1:
    case kX64S16x8HalfShuffle2:
    case kX64S8x16Alignr:
    case kX64S16x8Dup:
    case kX64S8x16Dup:
    case kX64S16x8UnzipHigh:
    case kX64S16x8UnzipLow:
    case kX64S8x16UnzipHigh:
    case kX64S8x16UnzipLow:
    case kX64S64x2UnpackHigh:
    case kX64S32x4UnpackHigh:
    case kX64S16x8UnpackHigh:
    case kX64S8x16UnpackHigh:
    case kX64S32x8UnpackHigh:
    case kX64S64x2UnpackLow:
    case kX64S32x4UnpackLow:
    case kX64S16x8UnpackLow:
    case kX64S8x16UnpackLow:
    case kX64S32x8UnpackLow:
    case kX64S8x16TransposeLow:
    case kX64S8x16TransposeHigh:
    case kX64S8x8Reverse:
    case kX64S8x4Reverse:
    case kX64S8x2Reverse:
    case kX64V128AnyTrue:
    case kX64Blendvpd:
    case kX64Blendvps:
    case kX64Pblendvb:
    case kX64ExtractF128:
    case kX64InsertI128:
      return (instr->addressing_mode() == kMode_None)
                 ? kNoOpcodeFlags
                 : kIsLoadOperation | kHasSideEffect;

    case kX64Idiv:
    case kX64Idiv32:
    case kX64Udiv:
    case kX64Udiv32:
      return (instr->addressing_mode() == kMode_None)
                 ? kMayNeedDeoptOrTrapCheck
                 : kMayNeedDeoptOrTrapCheck | kIsLoadOperation | kHasSideEffect;

    case kX64Movsxbl:
    case kX64Movzxbl:
    case kX64Movsxbq:
    case kX64Movzxbq:
    case kX64Movsxwl:
    case kX64Movzxwl:
    case kX64Movsxwq:
    case kX64Movzxwq:
    case kX64Movsxlq:
      DCHECK_LE(1, instr->InputCount());
      return instr->InputAt(0)->IsRegister() ? kNoOpcodeFlags
                                             : kIsLoadOperation;

    case kX64Movb:
    case kX64Movw:
    case kX64S128Store32Lane:
    case kX64S128Store64Lane:
      return kHasSideEffect;

    case kX64Pextrb:
    case kX64Pextrw:
    case kX64Movl:
      if (instr->HasOutput()) {
        DCHECK_LE(1, instr->InputCount());
        return instr->InputAt(0)->IsRegister() ? kNoOpcodeFlags
                                               : kIsLoadOperation;
      } else {
        return kHasSideEffect;
      }

    case kX64MovqDecompressTaggedSigned:
    case kX64MovqDecompressTagged:
    case kX64MovqDecompressProtected:
    case kX64MovqCompressTagged:
    case kX64MovqStoreIndirectPointer:
    case kX64MovqDecodeSandboxedPointer:
    case kX64MovqEncodeSandboxedPointer:
    case kX64Movq:
    case kX64Movsd:
    case kX64Movss:
    case kX64Movsh:
    case kX64Movdqu:
    case kX64Movdqu256:
    case kX64S128Load8Splat:
    case kX64S256Load8Splat:
    case kX64S128Load16Splat:
    case kX64S256Load16Splat:
    case kX64S128Load32Splat:
    case kX64S256Load32Splat:
    case kX64S128Load64Splat:
    case kX64S256Load64Splat:
    case kX64S128Load8x8S:
    case kX64S128Load8x8U:
    case kX64S128Load16x4S:
    case kX64S128Load16x4U:
    case kX64S128Load32x2S:
    case kX64S128Load32x2U:
    case kX64S256Load8x16S:
    case kX64S256Load8x16U:
    case kX64S256Load8x8U:
    case kX64S256Load16x8S:
    case kX64S256Load16x8U:
    case kX64S256Load32x4S:
    case kX64S256Load32x4U:
      return instr->HasOutput() ? kIsLoadOperation : kHasSideEffect;

    case kX64Peek:
      return kIsLoadOperation;

    case kX64Push:
    case kX64Poke:
      return kHasSideEffect;

    case kX64MFence:
    case kX64LFence:
      return kHasSideEffect;

    case kX64Word64AtomicStoreWord64:
    case kX64Word64AtomicAddUint64:
    case kX64Word64AtomicSubUint64:
    case kX64Word64AtomicAndUint64:
    case kX64Word64AtomicOrUint64:
    case kX64Word64AtomicXorUint64:
    case kX64Word64AtomicExchangeUint64:
    case kX64Word64AtomicCompareExchangeUint64:
      return kHasSideEffect;

#define CASE(Name) case k##Name:
      COMMON_ARCH_OPCODE_LIST(CASE)
#undef CASE
      // Already covered in architecture independent code.
      UNREACHABLE();
  }

  UNREACHABLE();
}

int InstructionScheduler::GetInstructionLatency(const Instruction* instr) {
  // Basic latency modeling for x64 instructions. They have been determined
  // in an empirical way.
  switch (instr->arch_opcode()) {
    case kSSEFloat64Mul:
      return 5;
    case kX64Imul:
    case kX64Imul32:
    case kX64ImulHigh32:
    case kX64UmulHigh32:
    case kX64ImulHigh64:
    case kX64UmulHigh64:
    case kX64Float32Abs:
    case kX64Float32Neg:
    case kX64Float64Abs:
    case kX64Float64Neg:
    case kSSEFloat32Cmp:
    case kSSEFloat32Add:
    case kSSEFloat32Sub:
    case kSSEFloat64Cmp:
    case kSSEFloat64Add:
    case kSSEFloat64Sub:
    case kSSEFloat64Max:
    case kSSEFloat64Min:
      return 3;
    case kSSEFloat32Mul:
    case kSSEFloat32ToFloat64:
    case kSSEFloat64ToFloat32:
    case kSSEFloat32Round:
    case kSSEFloat64Round:
    case kSSEFloat32ToInt32:
    case kSSEFloat32ToUint32:
    case kSSEFloat64ToInt32:
    case kSSEFloat64ToUint32:
      return 4;
    case kX64Idiv:
      return 49;
    case kX64Idiv32:
      return 35;
    case kX64Udiv:
      return 38;
    case kX64Udiv32:
      return 26;
    case kSSEFloat32Div:
    case kSSEFloat64Div:
    case kSSEFloat32Sqrt:
    case kSSEFloat64Sqrt:
      return 13;
    case kSSEFloat32ToInt64:
    case kSSEFloat64ToInt64:
    case kSSEFloat32ToUint64:
    case kSSEFloat64ToUint64:
    case kSSEFloat64ToFloat16RawBits:
    case kSSEFloat16RawBitsToFloat64:
      return 10;
    case kSSEFloat64Mod:
      return 50;
    case kArchTruncateDoubleToI:
      return 6;
    default:
      return 1;
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
