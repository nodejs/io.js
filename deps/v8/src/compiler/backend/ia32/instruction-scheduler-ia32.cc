// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/logging.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/backend/instruction-scheduler.h"
#include "src/compiler/backend/instruction.h"

namespace v8 {
namespace internal {
namespace compiler {

bool InstructionScheduler::SchedulerSupported() { return true; }

int InstructionScheduler::GetTargetInstructionFlags(
    const Instruction* instr) const {
  switch (instr->arch_opcode()) {
    case kIA32Add:
    case kIA32And:
    case kIA32Cmp:
    case kIA32Cmp16:
    case kIA32Cmp8:
    case kIA32Test:
    case kIA32Test16:
    case kIA32Test8:
    case kIA32Or:
    case kIA32Xor:
    case kIA32Sub:
    case kIA32Imul:
    case kIA32ImulHigh:
    case kIA32UmulHigh:
    case kIA32Not:
    case kIA32Neg:
    case kIA32Shl:
    case kIA32Shr:
    case kIA32Sar:
    case kIA32AddPair:
    case kIA32SubPair:
    case kIA32MulPair:
    case kIA32ShlPair:
    case kIA32ShrPair:
    case kIA32SarPair:
    case kIA32Rol:
    case kIA32Ror:
    case kIA32Lzcnt:
    case kIA32Tzcnt:
    case kIA32Popcnt:
    case kIA32Bswap:
    case kIA32Lea:
    case kIA32Float32Cmp:
    case kIA32Float32Sqrt:
    case kIA32Float32Round:
    case kIA32Float64Cmp:
    case kIA32Float64Mod:
    case kIA32Float32Max:
    case kIA32Float64Max:
    case kIA32Float32Min:
    case kIA32Float64Min:
    case kIA32Float64Sqrt:
    case kIA32Float64Round:
    case kIA32Float32ToFloat64:
    case kIA32Float64ToFloat32:
    case kIA32Float32ToInt32:
    case kIA32Float32ToUint32:
    case kIA32Float64ToInt32:
    case kIA32Float64ToUint32:
    case kSSEInt32ToFloat32:
    case kIA32Uint32ToFloat32:
    case kSSEInt32ToFloat64:
    case kIA32Uint32ToFloat64:
    case kIA32Float64ExtractLowWord32:
    case kIA32Float64ExtractHighWord32:
    case kIA32Float64InsertLowWord32:
    case kIA32Float64InsertHighWord32:
    case kIA32Float64FromWord32Pair:
    case kIA32Float64LoadLowWord32:
    case kIA32Float64SilenceNaN:
    case kFloat32Add:
    case kFloat32Sub:
    case kFloat64Add:
    case kFloat64Sub:
    case kFloat32Mul:
    case kFloat32Div:
    case kFloat64Mul:
    case kFloat64Div:
    case kFloat64Abs:
    case kFloat64Neg:
    case kFloat32Abs:
    case kFloat32Neg:
    case kIA32BitcastFI:
    case kIA32BitcastIF:
    case kIA32Blendvpd:
    case kIA32Blendvps:
    case kIA32Pblendvb:
    case kIA32Cvttps2dq:
    case kIA32Cvttpd2dq:
    case kIA32I32x4TruncF32x4U:
    case kIA32I32x4TruncF64x2UZero:
    case kIA32F64x2Splat:
    case kIA32F64x2ExtractLane:
    case kIA32F64x2ReplaceLane:
    case kIA32F64x2Sqrt:
    case kIA32F64x2Add:
    case kIA32F64x2Sub:
    case kIA32F64x2Mul:
    case kIA32F64x2Div:
    case kIA32F64x2Min:
    case kIA32F64x2Max:
    case kIA32F64x2Eq:
    case kIA32F64x2Ne:
    case kIA32F64x2Lt:
    case kIA32F64x2Le:
    case kIA32F64x2Qfma:
    case kIA32F64x2Qfms:
    case kIA32Minpd:
    case kIA32Maxpd:
    case kIA32F64x2Round:
    case kIA32F64x2ConvertLowI32x4S:
    case kIA32F64x2ConvertLowI32x4U:
    case kIA32F64x2PromoteLowF32x4:
    case kIA32I64x2SplatI32Pair:
    case kIA32I64x2ReplaceLaneI32Pair:
    case kIA32I64x2Abs:
    case kIA32I64x2Neg:
    case kIA32I64x2Shl:
    case kIA32I64x2ShrS:
    case kIA32I64x2Add:
    case kIA32I64x2Sub:
    case kIA32I64x2Mul:
    case kIA32I64x2ShrU:
    case kIA32I64x2BitMask:
    case kIA32I64x2Eq:
    case kIA32I64x2Ne:
    case kIA32I64x2GtS:
    case kIA32I64x2GeS:
    case kIA32I64x2ExtMulLowI32x4S:
    case kIA32I64x2ExtMulHighI32x4S:
    case kIA32I64x2ExtMulLowI32x4U:
    case kIA32I64x2ExtMulHighI32x4U:
    case kIA32I64x2SConvertI32x4Low:
    case kIA32I64x2SConvertI32x4High:
    case kIA32I64x2UConvertI32x4Low:
    case kIA32I64x2UConvertI32x4High:
    case kIA32F32x4Splat:
    case kIA32F32x4ExtractLane:
    case kIA32Insertps:
    case kIA32F32x4SConvertI32x4:
    case kIA32F32x4UConvertI32x4:
    case kIA32F32x4Sqrt:
    case kIA32F32x4Add:
    case kIA32F32x4Sub:
    case kIA32F32x4Mul:
    case kIA32F32x4Div:
    case kIA32F32x4Min:
    case kIA32F32x4Max:
    case kIA32F32x4Eq:
    case kIA32F32x4Ne:
    case kIA32F32x4Lt:
    case kIA32F32x4Le:
    case kIA32F32x4Qfma:
    case kIA32F32x4Qfms:
    case kIA32Minps:
    case kIA32Maxps:
    case kIA32F32x4Round:
    case kIA32F32x4DemoteF64x2Zero:
    case kIA32I32x4Splat:
    case kIA32I32x4ExtractLane:
    case kIA32I32x4SConvertF32x4:
    case kIA32I32x4SConvertI16x8Low:
    case kIA32I32x4SConvertI16x8High:
    case kIA32I32x4Neg:
    case kIA32I32x4Shl:
    case kIA32I32x4ShrS:
    case kIA32I32x4Add:
    case kIA32I32x4Sub:
    case kIA32I32x4Mul:
    case kIA32I32x4MinS:
    case kIA32I32x4MaxS:
    case kIA32I32x4Eq:
    case kIA32I32x4Ne:
    case kIA32I32x4GtS:
    case kIA32I32x4GeS:
    case kSSEI32x4UConvertF32x4:
    case kAVXI32x4UConvertF32x4:
    case kIA32I32x4UConvertI16x8Low:
    case kIA32I32x4UConvertI16x8High:
    case kIA32I32x4ShrU:
    case kIA32I32x4MinU:
    case kIA32I32x4MaxU:
    case kSSEI32x4GtU:
    case kAVXI32x4GtU:
    case kSSEI32x4GeU:
    case kAVXI32x4GeU:
    case kIA32I32x4Abs:
    case kIA32I32x4BitMask:
    case kIA32I32x4DotI16x8S:
    case kIA32I32x4DotI8x16I7x16AddS:
    case kIA32I32x4ExtMulLowI16x8S:
    case kIA32I32x4ExtMulHighI16x8S:
    case kIA32I32x4ExtMulLowI16x8U:
    case kIA32I32x4ExtMulHighI16x8U:
    case kIA32I32x4ExtAddPairwiseI16x8S:
    case kIA32I32x4ExtAddPairwiseI16x8U:
    case kIA32I32x4TruncSatF64x2SZero:
    case kIA32I32x4TruncSatF64x2UZero:
    case kIA32I16x8Splat:
    case kIA32I16x8ExtractLaneS:
    case kIA32I16x8SConvertI8x16Low:
    case kIA32I16x8SConvertI8x16High:
    case kIA32I16x8Neg:
    case kIA32I16x8Shl:
    case kIA32I16x8ShrS:
    case kIA32I16x8SConvertI32x4:
    case kIA32I16x8Add:
    case kIA32I16x8AddSatS:
    case kIA32I16x8Sub:
    case kIA32I16x8SubSatS:
    case kIA32I16x8Mul:
    case kIA32I16x8MinS:
    case kIA32I16x8MaxS:
    case kIA32I16x8Eq:
    case kSSEI16x8Ne:
    case kAVXI16x8Ne:
    case kIA32I16x8GtS:
    case kSSEI16x8GeS:
    case kAVXI16x8GeS:
    case kIA32I16x8UConvertI8x16Low:
    case kIA32I16x8UConvertI8x16High:
    case kIA32I16x8ShrU:
    case kIA32I16x8UConvertI32x4:
    case kIA32I16x8AddSatU:
    case kIA32I16x8SubSatU:
    case kIA32I16x8MinU:
    case kIA32I16x8MaxU:
    case kSSEI16x8GtU:
    case kAVXI16x8GtU:
    case kSSEI16x8GeU:
    case kAVXI16x8GeU:
    case kIA32I16x8RoundingAverageU:
    case kIA32I16x8Abs:
    case kIA32I16x8BitMask:
    case kIA32I16x8ExtMulLowI8x16S:
    case kIA32I16x8ExtMulHighI8x16S:
    case kIA32I16x8ExtMulLowI8x16U:
    case kIA32I16x8ExtMulHighI8x16U:
    case kIA32I16x8ExtAddPairwiseI8x16S:
    case kIA32I16x8ExtAddPairwiseI8x16U:
    case kIA32I16x8Q15MulRSatS:
    case kIA32I16x8RelaxedQ15MulRS:
    case kIA32I16x8DotI8x16I7x16S:
    case kIA32I8x16Splat:
    case kIA32I8x16ExtractLaneS:
    case kIA32Pinsrb:
    case kIA32Pinsrw:
    case kIA32Pinsrd:
    case kIA32Pextrb:
    case kIA32Pextrw:
    case kIA32S128Store32Lane:
    case kIA32I8x16SConvertI16x8:
    case kIA32I8x16Neg:
    case kIA32I8x16Shl:
    case kIA32I8x16ShrS:
    case kIA32I8x16Add:
    case kIA32I8x16AddSatS:
    case kIA32I8x16Sub:
    case kIA32I8x16SubSatS:
    case kIA32I8x16MinS:
    case kIA32I8x16MaxS:
    case kIA32I8x16Eq:
    case kSSEI8x16Ne:
    case kAVXI8x16Ne:
    case kIA32I8x16GtS:
    case kSSEI8x16GeS:
    case kAVXI8x16GeS:
    case kIA32I8x16UConvertI16x8:
    case kIA32I8x16AddSatU:
    case kIA32I8x16SubSatU:
    case kIA32I8x16ShrU:
    case kIA32I8x16MinU:
    case kIA32I8x16MaxU:
    case kSSEI8x16GtU:
    case kAVXI8x16GtU:
    case kSSEI8x16GeU:
    case kAVXI8x16GeU:
    case kIA32I8x16RoundingAverageU:
    case kIA32I8x16Abs:
    case kIA32I8x16BitMask:
    case kIA32I8x16Popcnt:
    case kIA32S128Const:
    case kIA32S128Zero:
    case kIA32S128AllOnes:
    case kIA32S128Not:
    case kIA32S128And:
    case kIA32S128Or:
    case kIA32S128Xor:
    case kIA32S128Select:
    case kIA32S128AndNot:
    case kIA32I8x16Swizzle:
    case kIA32I8x16Shuffle:
    case kIA32S32x4Rotate:
    case kIA32S32x4Swizzle:
    case kIA32S32x4Shuffle:
    case kIA32S16x8Blend:
    case kIA32S16x8HalfShuffle1:
    case kIA32S16x8HalfShuffle2:
    case kIA32S8x16Alignr:
    case kIA32S16x8Dup:
    case kIA32S8x16Dup:
    case kSSES16x8UnzipHigh:
    case kAVXS16x8UnzipHigh:
    case kSSES16x8UnzipLow:
    case kAVXS16x8UnzipLow:
    case kSSES8x16UnzipHigh:
    case kAVXS8x16UnzipHigh:
    case kSSES8x16UnzipLow:
    case kAVXS8x16UnzipLow:
    case kIA32S64x2UnpackHigh:
    case kIA32S32x4UnpackHigh:
    case kIA32S16x8UnpackHigh:
    case kIA32S8x16UnpackHigh:
    case kIA32S64x2UnpackLow:
    case kIA32S32x4UnpackLow:
    case kIA32S16x8UnpackLow:
    case kIA32S8x16UnpackLow:
    case kSSES8x16TransposeLow:
    case kAVXS8x16TransposeLow:
    case kSSES8x16TransposeHigh:
    case kAVXS8x16TransposeHigh:
    case kSSES8x8Reverse:
    case kAVXS8x8Reverse:
    case kSSES8x4Reverse:
    case kAVXS8x4Reverse:
    case kSSES8x2Reverse:
    case kAVXS8x2Reverse:
    case kIA32S128AnyTrue:
    case kIA32I64x2AllTrue:
    case kIA32I32x4AllTrue:
    case kIA32I16x8AllTrue:
    case kIA32I8x16AllTrue:
      return (instr->addressing_mode() == kMode_None)
                 ? kNoOpcodeFlags
                 : kIsLoadOperation | kHasSideEffect;

    case kIA32Idiv:
    case kIA32Udiv:
      return (instr->addressing_mode() == kMode_None)
                 ? kMayNeedDeoptOrTrapCheck
                 : kMayNeedDeoptOrTrapCheck | kIsLoadOperation | kHasSideEffect;

    case kIA32Movsxbl:
    case kIA32Movzxbl:
    case kIA32Movb:
    case kIA32Movsxwl:
    case kIA32Movzxwl:
    case kIA32Movw:
    case kIA32Movl:
    case kIA32Movss:
    case kIA32Movsd:
    case kIA32Movdqu:
    case kIA32Movlps:
    case kIA32Movhps:
    // Moves are used for memory load/store operations.
    case kIA32S128Load8Splat:
    case kIA32S128Load16Splat:
    case kIA32S128Load32Splat:
    case kIA32S128Load64Splat:
    case kIA32S128Load8x8S:
    case kIA32S128Load8x8U:
    case kIA32S128Load16x4S:
    case kIA32S128Load16x4U:
    case kIA32S128Load32x2S:
    case kIA32S128Load32x2U:
      return instr->HasOutput() ? kIsLoadOperation : kHasSideEffect;

    case kIA32Peek:
      return kIsLoadOperation;

    case kIA32Push:
    case kIA32Poke:
    case kIA32MFence:
    case kIA32LFence:
      return kHasSideEffect;

    case kIA32Word32AtomicPairLoad:
      return kIsLoadOperation;

    case kIA32Word32ReleasePairStore:
    case kIA32Word32SeqCstPairStore:
    case kIA32Word32AtomicPairAdd:
    case kIA32Word32AtomicPairSub:
    case kIA32Word32AtomicPairAnd:
    case kIA32Word32AtomicPairOr:
    case kIA32Word32AtomicPairXor:
    case kIA32Word32AtomicPairExchange:
    case kIA32Word32AtomicPairCompareExchange:
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
  // Basic latency modeling for ia32 instructions. They have been determined
  // in an empirical way.
  switch (instr->arch_opcode()) {
    case kFloat64Mul:
      return 5;
    case kIA32Imul:
    case kIA32ImulHigh:
      return 5;
    case kIA32Float32Cmp:
    case kIA32Float64Cmp:
      return 9;
    case kFloat32Add:
    case kFloat32Sub:
    case kFloat64Add:
    case kFloat64Sub:
    case kFloat32Abs:
    case kFloat32Neg:
    case kIA32Float64Max:
    case kIA32Float64Min:
    case kFloat64Abs:
    case kFloat64Neg:
      return 5;
    case kFloat32Mul:
      return 4;
    case kIA32Float32ToFloat64:
    case kIA32Float64ToFloat32:
      return 6;
    case kIA32Float32Round:
    case kIA32Float64Round:
    case kIA32Float32ToInt32:
    case kIA32Float64ToInt32:
      return 8;
    case kIA32Float32ToUint32:
      return 21;
    case kIA32Float64ToUint32:
      return 15;
    case kIA32Idiv:
      return 33;
    case kIA32Udiv:
      return 26;
    case kFloat32Div:
      return 35;
    case kFloat64Div:
      return 63;
    case kIA32Float32Sqrt:
    case kIA32Float64Sqrt:
      return 25;
    case kIA32Float64Mod:
      return 50;
    case kArchTruncateDoubleToI:
      return 9;
    default:
      return 1;
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
