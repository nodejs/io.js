// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/macro-assembler.h"
#include "src/compiler/backend/instruction-scheduler.h"

namespace v8 {
namespace internal {
namespace compiler {

bool InstructionScheduler::SchedulerSupported() { return true; }

int InstructionScheduler::GetTargetInstructionFlags(
    const Instruction* instr) const {
  switch (instr->arch_opcode()) {
#if V8_TARGET_ARCH_RISCV64
    case kRiscvAdd32:
    case kRiscvBitcastDL:
    case kRiscvBitcastLD:
    case kRiscvByteSwap64:
    case kRiscvCvtDL:
    case kRiscvCvtDUl:
    case kRiscvCvtSL:
    case kRiscvCvtSUl:
    case kRiscvMulHigh64:
    case kRiscvMulHighU64:
    case kRiscvAdd64:
    case kRiscvAddOvf64:
    case kRiscvClz64:
    case kRiscvCtz64:
    case kRiscvDiv64:
    case kRiscvDivU64:
    case kRiscvZeroExtendWord:
    case kRiscvSignExtendWord:
    case kRiscvMod64:
    case kRiscvModU64:
    case kRiscvMul64:
    case kRiscvMulOvf64:
    case kRiscvPopcnt64:
    case kRiscvRor64:
    case kRiscvSar64:
    case kRiscvShl64:
    case kRiscvShr64:
    case kRiscvSub64:
    case kRiscvSubOvf64:
    case kRiscvFloat64RoundDown:
    case kRiscvFloat64RoundTiesEven:
    case kRiscvFloat64RoundTruncate:
    case kRiscvFloat64RoundUp:
    case kRiscvSub32:
    case kRiscvTruncLD:
    case kRiscvTruncLS:
    case kRiscvTruncUlD:
    case kRiscvTruncUlS:
    case kRiscvCmp32:
    case kRiscvCmpZero32:
#elif V8_TARGET_ARCH_RISCV32
    case kRiscvAdd32:
    case kRiscvAddPair:
    case kRiscvSubPair:
    case kRiscvMulPair:
    case kRiscvAndPair:
    case kRiscvOrPair:
    case kRiscvXorPair:
    case kRiscvShlPair:
    case kRiscvShrPair:
    case kRiscvSarPair:
    case kRiscvAddOvf:
    case kRiscvSubOvf:
    case kRiscvSub32:
#endif
    case kRiscvSh1add:
    case kRiscvSh2add:
    case kRiscvSh3add:
#if V8_TARGET_ARCH_RISCV64
    case kRiscvAdduw:
    case kRiscvSh1adduw:
    case kRiscvSh2adduw:
    case kRiscvSh3adduw:
    case kRiscvSlliuw:
#endif
    case kRiscvAndn:
    case kRiscvOrn:
    case kRiscvXnor:
    case kRiscvClz:
    case kRiscvCtz:
    case kRiscvCpop:
#if V8_TARGET_ARCH_RISCV64
    case kRiscvClzw:
    case kRiscvCtzw:
    case kRiscvCpopw:
#endif
    case kRiscvMax:
    case kRiscvMaxu:
    case kRiscvMin:
    case kRiscvMinu:
    case kRiscvSextb:
    case kRiscvSexth:
    case kRiscvZexth:
    case kRiscvRev8:
    case kRiscvBclr:
    case kRiscvBclri:
    case kRiscvBext:
    case kRiscvBexti:
    case kRiscvBinv:
    case kRiscvBinvi:
    case kRiscvBset:
    case kRiscvBseti:
    case kRiscvAbsD:
    case kRiscvAbsS:
    case kRiscvAddD:
    case kRiscvAddS:
    case kRiscvAnd:
    case kRiscvAnd32:
    case kRiscvAssertEqual:
    case kRiscvBitcastInt32ToFloat32:
    case kRiscvBitcastFloat32ToInt32:
    case kRiscvByteSwap32:
    case kRiscvCeilWD:
    case kRiscvCeilWS:
    case kRiscvClz32:
    case kRiscvCmp:
    case kRiscvCmpZero:
    case kRiscvCmpD:
    case kRiscvCmpS:
    case kRiscvCtz32:
    case kRiscvCvtDS:
    case kRiscvCvtDUw:
    case kRiscvCvtDW:
    case kRiscvCvtSD:
    case kRiscvCvtSUw:
    case kRiscvCvtSW:
    case kRiscvMulHighU32:
    case kRiscvDiv32:
    case kRiscvDivD:
    case kRiscvDivS:
    case kRiscvDivU32:
    case kRiscvF64x2Abs:
    case kRiscvF64x2Sqrt:
    case kRiscvF64x2Pmin:
    case kRiscvF64x2Pmax:
    case kRiscvF64x2ConvertLowI32x4S:
    case kRiscvF64x2ConvertLowI32x4U:
    case kRiscvF64x2PromoteLowF32x4:
    case kRiscvF64x2Ceil:
    case kRiscvF64x2Floor:
    case kRiscvF64x2Trunc:
    case kRiscvF64x2NearestInt:
    case kRiscvI64x2SplatI32Pair:
    case kRiscvI64x2ExtractLane:
    case kRiscvI64x2ReplaceLane:
    case kRiscvI64x2ReplaceLaneI32Pair:
    case kRiscvI64x2Shl:
    case kRiscvI64x2ShrS:
    case kRiscvI64x2ShrU:
    case kRiscvF32x4Abs:
    case kRiscvF32x4ExtractLane:
    case kRiscvF32x4Sqrt:
    case kRiscvF64x2Qfma:
    case kRiscvF64x2Qfms:
    case kRiscvF32x4Qfma:
    case kRiscvF32x4Qfms:
    case kRiscvF32x4ReplaceLane:
    case kRiscvF32x4SConvertI32x4:
    case kRiscvF32x4UConvertI32x4:
    case kRiscvF32x4Pmin:
    case kRiscvF32x4Pmax:
    case kRiscvF32x4DemoteF64x2Zero:
    case kRiscvF32x4Ceil:
    case kRiscvF32x4Floor:
    case kRiscvF32x4Trunc:
    case kRiscvF32x4NearestInt:
    case kRiscvF64x2ExtractLane:
    case kRiscvF64x2ReplaceLane:
    case kRiscvFloat32Max:
    case kRiscvFloat32Min:
    case kRiscvFloat32RoundDown:
    case kRiscvFloat32RoundTiesEven:
    case kRiscvFloat32RoundTruncate:
    case kRiscvFloat32RoundUp:
    case kRiscvFloat64ExtractLowWord32:
    case kRiscvFloat64ExtractHighWord32:
    case kRiscvFloat64InsertLowWord32:
    case kRiscvFloat64InsertHighWord32:
    case kRiscvFloat64Max:
    case kRiscvFloat64Min:
    case kRiscvFloat64SilenceNaN:
    case kRiscvFloorWD:
    case kRiscvFloorWS:
    case kRiscvI64x2SConvertI32x4Low:
    case kRiscvI64x2SConvertI32x4High:
    case kRiscvI64x2UConvertI32x4Low:
    case kRiscvI64x2UConvertI32x4High:
    case kRiscvI16x8ExtractLaneU:
    case kRiscvI16x8ExtractLaneS:
    case kRiscvI16x8ReplaceLane:
    case kRiscvI16x8Shl:
    case kRiscvI16x8ShrS:
    case kRiscvI16x8ShrU:
    case kRiscvI32x4TruncSatF64x2SZero:
    case kRiscvI32x4TruncSatF64x2UZero:
    case kRiscvI32x4ExtractLane:
    case kRiscvI32x4ReplaceLane:
    case kRiscvI32x4SConvertF32x4:
    case kRiscvI32x4Shl:
    case kRiscvI32x4ShrS:
    case kRiscvI32x4ShrU:
    case kRiscvI32x4UConvertF32x4:
    case kRiscvI8x16ExtractLaneU:
    case kRiscvI8x16ExtractLaneS:
    case kRiscvI8x16ReplaceLane:
    case kRiscvI8x16Shl:
    case kRiscvI8x16ShrS:
    case kRiscvI8x16ShrU:
    case kRiscvI8x16RoundingAverageU:
    case kRiscvI8x16Popcnt:
    case kRiscvMaxD:
    case kRiscvMaxS:
    case kRiscvMinD:
    case kRiscvMinS:
    case kRiscvMod32:
    case kRiscvModU32:
    case kRiscvMov:
    case kRiscvMul32:
    case kRiscvMulD:
    case kRiscvMulHigh32:
    case kRiscvMulOvf32:
    case kRiscvMulS:
    case kRiscvNegD:
    case kRiscvNegS:
    case kRiscvOr:
    case kRiscvOr32:
    case kRiscvPopcnt32:
    case kRiscvRor32:
    case kRiscvRoundWD:
    case kRiscvRoundWS:
    case kRiscvVnot:
    case kRiscvS128Select:
    case kRiscvS128Const:
    case kRiscvS128Zero:
    case kRiscvS128Load32Zero:
    case kRiscvS128Load64Zero:
    case kRiscvS128AllOnes:
    case kRiscvV128AnyTrue:
    case kRiscvI8x16Shuffle:
    case kRiscvVwmul:
    case kRiscvVwmulu:
    case kRiscvVmv:
    case kRiscvVandVv:
    case kRiscvVorVv:
    case kRiscvVnotVv:
    case kRiscvVxorVv:
    case kRiscvVmvSx:
    case kRiscvVmvXs:
    case kRiscvVfmvVf:
    case kRiscvVcompress:
    case kRiscvVaddVv:
    case kRiscvVwaddVv:
    case kRiscvVwadduVv:
    case kRiscvVwadduWx:
    case kRiscvVsubVv:
    case kRiscvVnegVv:
    case kRiscvVfnegVv:
    case kRiscvVmaxuVv:
    case kRiscvVmax:
    case kRiscvVminsVv:
    case kRiscvVminuVv:
    case kRiscvVmulVv:
    case kRiscvVdivu:
    case kRiscvVsmulVv:
    case kRiscvVmslt:
    case kRiscvVgtsVv:
    case kRiscvVgesVv:
    case kRiscvVgeuVv:
    case kRiscvVgtuVv:
    case kRiscvVeqVv:
    case kRiscvVneVv:
    case kRiscvVAbs:
    case kRiscvVaddSatUVv:
    case kRiscvVaddSatSVv:
    case kRiscvVsubSatUVv:
    case kRiscvVsubSatSVv:
    case kRiscvVrgather:
    case kRiscvVslidedown:
    case kRiscvVredminuVs:
    case kRiscvVAllTrue:
    case kRiscvVnclipu:
    case kRiscvVnclip:
    case kRiscvVsll:
    case kRiscvVfaddVv:
    case kRiscvVfsubVv:
    case kRiscvVfmulVv:
    case kRiscvVfdivVv:
    case kRiscvVfminVv:
    case kRiscvVfmaxVv:
    case kRiscvVmfeqVv:
    case kRiscvVmfneVv:
    case kRiscvVmfltVv:
    case kRiscvVmfleVv:
    case kRiscvVmergeVx:
    case kRiscvVzextVf2:
    case kRiscvVsextVf2:
    case kRiscvSar32:
    case kRiscvSignExtendByte:
    case kRiscvSignExtendShort:
    case kRiscvShl32:
    case kRiscvShr32:
    case kRiscvSqrtD:
    case kRiscvSqrtS:
    case kRiscvSubD:
    case kRiscvSubS:
    case kRiscvTruncUwD:
    case kRiscvTruncUwS:
    case kRiscvTruncWD:
    case kRiscvTruncWS:
    case kRiscvTst32:
    case kRiscvXor:
    case kRiscvXor32:
      return kNoOpcodeFlags;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvTst64:
    case kRiscvLd:
    case kRiscvLwu:
    case kRiscvUlwu:
    case kRiscvWord64AtomicLoadUint64:
    case kRiscvLoadDecompressTaggedSigned:
    case kRiscvLoadDecompressTagged:
    case kRiscvLoadDecodeSandboxedPointer:
    case kRiscvAtomicLoadDecompressTaggedSigned:
    case kRiscvAtomicLoadDecompressTagged:
    case kRiscvAtomicStoreCompressTagged:
    case kRiscvLoadDecompressProtected:
#elif V8_TARGET_ARCH_RISCV32
    case kRiscvWord32AtomicPairLoad:
#endif
    case kRiscvLb:
    case kRiscvLbu:
    case kRiscvLoadDouble:
    case kRiscvLh:
    case kRiscvLhu:
    case kRiscvLw:
    case kRiscvLoadFloat:
    case kRiscvRvvLd:
    case kRiscvPeek:
    case kRiscvUld:
    case kRiscvULoadDouble:
    case kRiscvUlh:
    case kRiscvUlhu:
    case kRiscvUlw:
    case kRiscvULoadFloat:
    case kRiscvS128LoadSplat:
    case kRiscvS128Load64ExtendU:
    case kRiscvS128Load64ExtendS:
    case kRiscvS128LoadLane:
      return kIsLoadOperation;

#if V8_TARGET_ARCH_RISCV64
    case kRiscvSd:
    case kRiscvUsd:
    case kRiscvWord64AtomicStoreWord64:
    case kRiscvWord64AtomicAddUint64:
    case kRiscvWord64AtomicSubUint64:
    case kRiscvWord64AtomicAndUint64:
    case kRiscvWord64AtomicOrUint64:
    case kRiscvWord64AtomicXorUint64:
    case kRiscvWord64AtomicExchangeUint64:
    case kRiscvWord64AtomicCompareExchangeUint64:
    case kRiscvStoreCompressTagged:
    case kRiscvStoreEncodeSandboxedPointer:
    case kRiscvStoreIndirectPointer:
#elif V8_TARGET_ARCH_RISCV32
    case kRiscvWord32AtomicPairStore:
    case kRiscvWord32AtomicPairAdd:
    case kRiscvWord32AtomicPairSub:
    case kRiscvWord32AtomicPairAnd:
    case kRiscvWord32AtomicPairOr:
    case kRiscvWord32AtomicPairXor:
    case kRiscvWord32AtomicPairExchange:
    case kRiscvWord32AtomicPairCompareExchange:
#endif
    case kRiscvModD:
    case kRiscvModS:
    case kRiscvRvvSt:
    case kRiscvPush:
    case kRiscvSb:
    case kRiscvStoreDouble:
    case kRiscvSh:
    case kRiscvStackClaim:
    case kRiscvStoreToStackSlot:
    case kRiscvSw:
    case kRiscvStoreFloat:
    case kRiscvUStoreDouble:
    case kRiscvUsh:
    case kRiscvUsw:
    case kRiscvUStoreFloat:
    case kRiscvSync:
    case kRiscvS128StoreLane:
      return kHasSideEffect;

#define CASE(Name) case k##Name:
      COMMON_ARCH_OPCODE_LIST(CASE)
#undef CASE
      // Already covered in architecture independent code.
      UNREACHABLE();
  }

  UNREACHABLE();
}

enum Latency {
  ADD = 1,

  BRANCH = 4,  // Estimated max.
  RINT_S = 4,  // Estimated.
  RINT_D = 4,  // Estimated.

  // TODO(RISCV): remove MULT instructions (MIPS legacy).
  MUL = 4,
  MULW = 4,
  MULH = 4,
  MULHS = 4,
  MULHU = 4,

  DIVW = 50,  // Min:11 Max:50
  DIV = 50,
  DIVU = 50,
  DIVUW = 50,

  FSGNJ_S = 4,
  FSGNJ_D = 4,
  ABS_S = FSGNJ_S,
  ABS_D = FSGNJ_S,
  NEG_S = FSGNJ_S,
  NEG_D = FSGNJ_S,
  ADD_S = 4,
  ADD_D = 4,
  SUB_S = 4,
  SUB_D = 4,
  MAX_S = 4,  // Estimated.
  MIN_S = 4,
  MAX_D = 4,  // Estimated.
  MIN_D = 4,
  C_cond_S = 4,
  C_cond_D = 4,
  MUL_S = 4,

  MADD_S = 4,
  MSUB_S = 4,
  NMADD_S = 4,
  NMSUB_S = 4,

  CABS_cond_S = 4,
  CABS_cond_D = 4,

  CVT_D_S = 4,
  CVT_PS_PW = 4,

  CVT_S_W = 4,
  CVT_S_L = 4,
  CVT_D_W = 4,
  CVT_D_L = 4,

  CVT_S_D = 4,

  CVT_W_S = 4,
  CVT_W_D = 4,
  CVT_L_S = 4,
  CVT_L_D = 4,

  CEIL_W_S = 4,
  CEIL_W_D = 4,
  CEIL_L_S = 4,
  CEIL_L_D = 4,

  FLOOR_W_S = 4,
  FLOOR_W_D = 4,
  FLOOR_L_S = 4,
  FLOOR_L_D = 4,

  ROUND_W_S = 4,
  ROUND_W_D = 4,
  ROUND_L_S = 4,
  ROUND_L_D = 4,

  TRUNC_W_S = 4,
  TRUNC_W_D = 4,
  TRUNC_L_S = 4,
  TRUNC_L_D = 4,

  MOV_S = 4,
  MOV_D = 4,

  MOVF_S = 4,
  MOVF_D = 4,

  MOVN_S = 4,
  MOVN_D = 4,

  MOVT_S = 4,
  MOVT_D = 4,

  MOVZ_S = 4,
  MOVZ_D = 4,

  MUL_D = 5,
  MADD_D = 5,
  MSUB_D = 5,
  NMADD_D = 5,
  NMSUB_D = 5,

  RECIP_S = 13,
  RECIP_D = 26,

  RSQRT_S = 17,
  RSQRT_D = 36,

  DIV_S = 17,
  SQRT_S = 17,

  DIV_D = 32,
  SQRT_D = 32,

  MOVT_FREG = 4,
  MOVT_HIGH_FREG = 4,
  MOVT_DREG = 4,
  LOAD_FLOAT = 4,
  LOAD_DOUBLE = 4,

  MOVF_FREG = 1,
  MOVF_HIGH_FREG = 1,
  MOVF_HIGH_DREG = 1,
  MOVF_HIGH = 1,
  MOVF_LOW = 1,
  STORE_FLOAT = 1,
  STORE_DOUBLE = 1,
};

inline int LoadConstantLatency() {
  return 1;
  // #if V8_TARGET_ARCH_RISCV32
  //   return 2; //lui+aii Estimated max.
  // #elif V8_TARGET_ARCH_RISCV64
  //   return 4;
  // #endif
}

inline int Add64Latency(bool is_operand_register = true) {
  int latency = Latency::ADD;
  if (!is_operand_register) {
    latency += LoadConstantLatency();
  }
  return latency;
}

int Sub64Latency(bool is_operand_register = true) {
  return Add64Latency(is_operand_register);
}

int ShiftLatency(bool is_operand_register = true) {
  return Add64Latency(is_operand_register);
}
int AndLatency(bool is_operand_register = true) {
  return Add64Latency(is_operand_register);
}

int OrLatency(bool is_operand_register = true) {
  return Add64Latency(is_operand_register);
}

int NorLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return 1;
  } else {
    return 1 + LoadConstantLatency();  // Estimated max.
  }
}

int XorLatency(bool is_operand_register = true) {
  return Add64Latency(is_operand_register);
}

int Mul32Latency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::MULW;
  } else {
    return Latency::MULW + 1;
  }
}

int Mul64Latency(bool is_operand_register = true) {
  int latency = Latency::MUL;
  if (!is_operand_register) {
    latency += LoadConstantLatency();
  }
  return latency;
}

int Mulh32Latency(bool is_operand_register = true) {
  int latency = Latency::MULH + ShiftLatency(true);
  if (!is_operand_register) {
    latency += LoadConstantLatency();
  }
  return latency;
}

int Mulhu32Latency(bool is_operand_register = true) {
  int latency = Latency::MULHU + ShiftLatency(true) * 2;
  if (!is_operand_register) {
    latency += LoadConstantLatency();
  } else {
    latency += 1;
  }
  return latency;
}

int Mulh64Latency(bool is_operand_register = true) {
  int latency = Latency::MULH;
  if (!is_operand_register) {
    latency += LoadConstantLatency();
  }
  return latency;
}

int Div32Latency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::DIVW;
  } else {
    return Latency::DIVW + 1;
  }
}

int Divu32Latency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::DIVUW;
  } else {
    return Latency::DIVUW + LoadConstantLatency();
  }
}

int Div64Latency(bool is_operand_register = true) {
  int latency = Latency::DIV;
  if (!is_operand_register) {
    latency += LoadConstantLatency();
  }
  return latency;
}

int Divu64Latency(bool is_operand_register = true) {
  int latency = Latency::DIVU;
  if (!is_operand_register) {
    latency += LoadConstantLatency();
  }
  return latency;
}

int Mod32Latency(bool is_operand_register = true) {
  int latency = Latency::DIVW;
  if (!is_operand_register) {
    latency += LoadConstantLatency();
  }
  return latency;
}

int Modu32Latency(bool is_operand_register = true) {
  int latency = Latency::DIVUW;
  if (!is_operand_register) {
    latency += LoadConstantLatency();
  }
  return latency;
}

int Mod64Latency(bool is_operand_register = true) {
  int latency = Latency::DIV;
  if (!is_operand_register) {
    latency += LoadConstantLatency();
  }
  return latency;
}

int Modu64Latency(bool is_operand_register = true) {
  int latency = Latency::DIV;
  if (!is_operand_register) {
    latency += LoadConstantLatency();
  }
  return latency;
}

int MovzLatency() { return 1; }

int MovnLatency() { return 1; }

int CallLatency() {
  // Estimated.
  return Add64Latency(false) + Latency::BRANCH + 5;
}

int JumpLatency() {
  // Estimated max.
  return 1 + Add64Latency() + Latency::BRANCH + 2;
}

int SmiUntagLatency() { return 1; }

int PrepareForTailCallLatency() {
  // Estimated max.
  return 2 * (Add64Latency() + 1 + Add64Latency(false)) + 2 + Latency::BRANCH +
         Latency::BRANCH + 2 * Sub64Latency(false) + 2 + Latency::BRANCH + 1;
}

int AssemblePopArgumentsAdoptFrameLatency() {
  return 1 + Latency::BRANCH + 1 + SmiUntagLatency() +
         PrepareForTailCallLatency();
}

int AssertLatency() { return 1; }

int PrepareCallCFunctionLatency() {
  int frame_alignment = MacroAssembler::ActivationFrameAlignment();
  if (frame_alignment > kSystemPointerSize) {
    return 1 + Sub64Latency(false) + AndLatency(false) + 1;
  } else {
    return Sub64Latency(false);
  }
}

int AdjustBaseAndOffsetLatency() {
  return 3;  // Estimated max.
}

int AlignedMemoryLatency() { return AdjustBaseAndOffsetLatency() + 1; }

int UlhuLatency() {
  return AdjustBaseAndOffsetLatency() + 2 * AlignedMemoryLatency() + 2;
}

int UlwLatency() {
  // Estimated max.
  return AdjustBaseAndOffsetLatency() + 3;
}

int UlwuLatency() { return UlwLatency() + 1; }

int UldLatency() {
  // Estimated max.
  return AdjustBaseAndOffsetLatency() + 3;
}

int ULoadFloatLatency() { return UlwLatency() + Latency::MOVT_FREG; }

int ULoadDoubleLatency() { return UldLatency() + Latency::MOVT_DREG; }

int UshLatency() {
  // Estimated max.
  return AdjustBaseAndOffsetLatency() + 2 + 2 * AlignedMemoryLatency();
}

int UswLatency() { return AdjustBaseAndOffsetLatency() + 2; }

int UsdLatency() { return AdjustBaseAndOffsetLatency() + 2; }

int UStoreFloatLatency() { return Latency::MOVF_FREG + UswLatency(); }

int UStoreDoubleLatency() { return Latency::MOVF_HIGH_DREG + UsdLatency(); }

int LoadFloatLatency() {
  return AdjustBaseAndOffsetLatency() + Latency::LOAD_FLOAT;
}

int StoreFloatLatency() {
  return AdjustBaseAndOffsetLatency() + Latency::STORE_FLOAT;
}

int StoreDoubleLatency() {
  return AdjustBaseAndOffsetLatency() + Latency::STORE_DOUBLE;
}

int LoadDoubleLatency() {
  return AdjustBaseAndOffsetLatency() + Latency::LOAD_DOUBLE;
}

int MultiPushLatency() {
  int latency = Sub64Latency(false);
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    latency++;
  }
  return latency;
}

int MultiPushFPULatency() {
  int latency = Sub64Latency(false);
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    latency += StoreDoubleLatency();
  }
  return latency;
}

int PushCallerSavedLatency(SaveFPRegsMode fp_mode) {
  int latency = MultiPushLatency();
  if (fp_mode == SaveFPRegsMode::kSave) {
    latency += MultiPushFPULatency();
  }
  return latency;
}

int MultiPopLatency() {
  int latency = Add64Latency(false);
  for (int16_t i = 0; i < kNumRegisters; i++) {
    latency++;
  }
  return latency;
}

int MultiPopFPULatency() {
  int latency = Add64Latency(false);
  for (int16_t i = 0; i < kNumRegisters; i++) {
    latency += LoadDoubleLatency();
  }
  return latency;
}

int PopCallerSavedLatency(SaveFPRegsMode fp_mode) {
  int latency = MultiPopLatency();
  if (fp_mode == SaveFPRegsMode::kSave) {
    latency += MultiPopFPULatency();
  }
  return latency;
}

int CallCFunctionHelperLatency() {
  // Estimated.
  int latency = AndLatency(false) + Latency::BRANCH + 2 + CallLatency();
  if (base::OS::ActivationFrameAlignment() > kSystemPointerSize) {
    latency++;
  } else {
    latency += Add64Latency(false);
  }
  return latency;
}

int CallCFunctionLatency() { return 1 + CallCFunctionHelperLatency(); }

int AssembleArchJumpLatency() {
  // Estimated max.
  return Latency::BRANCH;
}

int GenerateSwitchTableLatency() {
  int latency = 6;
  latency += 2;
  return latency;
}

int AssembleArchTableSwitchLatency() {
  return Latency::BRANCH + GenerateSwitchTableLatency();
}

int DropAndRetLatency() {
  // Estimated max.
  return Add64Latency(false) + JumpLatency();
}

int AssemblerReturnLatency() {
  // Estimated max.
  return Add64Latency(false) + MultiPopLatency() + MultiPopFPULatency() +
         Latency::BRANCH + Add64Latency() + 1 + DropAndRetLatency();
}

int TryInlineTruncateDoubleToILatency() {
  return 2 + Latency::TRUNC_W_D + Latency::MOVF_FREG + 2 + AndLatency(false) +
         Latency::BRANCH;
}

int CallStubDelayedLatency() { return 1 + CallLatency(); }

int TruncateDoubleToIDelayedLatency() {
  // TODO(riscv): This no longer reflects how TruncateDoubleToI is called.
  return TryInlineTruncateDoubleToILatency() + 1 + Sub64Latency(false) +
         StoreDoubleLatency() + CallStubDelayedLatency() + Add64Latency(false) +
         1;
}

int CheckPageFlagLatency() {
  return AndLatency(false) + AlignedMemoryLatency() + AndLatency(false) +
         Latency::BRANCH;
}

int SltuLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return 1;
  } else {
    return 2;  // Estimated max.
  }
}

int BranchShortHelperLatency() {
  return SltuLatency() + 2;  // Estimated max.
}

int BranchShortLatency() { return BranchShortHelperLatency(); }

int MoveLatency() { return 1; }

int MovToFloatParametersLatency() { return 2 * MoveLatency(); }

int MovFromFloatResultLatency() { return MoveLatency(); }

int AddOverflow64Latency() {
  // Estimated max.
  return 6;
}

int SubOverflow64Latency() {
  // Estimated max.
  return 6;
}

int MulOverflow32Latency() {
  // Estimated max.
  return Mul32Latency() + Mulh32Latency() + 2;
}

int MulOverflow64Latency() {
  // Estimated max.
  return Mul64Latency() + Mulh64Latency() + 2;
}

// TODO(RISCV): This is incorrect for RISC-V.
int Clz64Latency() { return 1; }

int Ctz32Latency() {
  return Add64Latency(false) + XorLatency() + AndLatency() + Clz64Latency() +
         1 + Sub64Latency();
}

int Ctz64Latency() {
  return Add64Latency(false) + XorLatency() + AndLatency() + 1 + Sub64Latency();
}

int Popcnt32Latency() {
  return 2 + AndLatency() + Sub64Latency() + 1 + AndLatency() + 1 +
         AndLatency() + Add64Latency() + 1 + Add64Latency() + 1 + AndLatency() +
         1 + Mul32Latency() + 1;
}

#if V8_TARGET_ARCH_RISCV64
int Popcnt64Latency() {
  return 2 + AndLatency() + Sub64Latency() + 1 + AndLatency() + 1 +
         AndLatency() + Add64Latency() + 1 + Add64Latency() + 1 + AndLatency() +
         1 + Mul64Latency() + 1;
}
#endif
int CompareFLatency() { return Latency::C_cond_S; }

int CompareF32Latency() { return CompareFLatency(); }

int CompareF64Latency() { return CompareFLatency(); }

int CompareIsNanFLatency() { return CompareFLatency(); }

int CompareIsNanF32Latency() { return CompareIsNanFLatency(); }

int CompareIsNanF64Latency() { return CompareIsNanFLatency(); }

int NegsLatency() {
  // Estimated.
  return CompareIsNanF32Latency() + 2 * Latency::BRANCH + Latency::NEG_S +
         Latency::MOVF_FREG + 1 + XorLatency() + Latency::MOVT_FREG;
}

int NegdLatency() {
  // Estimated.
  return CompareIsNanF64Latency() + 2 * Latency::BRANCH + Latency::NEG_D +
         Latency::MOVF_HIGH_DREG + 1 + XorLatency() + Latency::MOVT_DREG;
}

#if V8_TARGET_ARCH_RISCV64
int Float64RoundLatency() {
  // For ceil_l_d, floor_l_d, round_l_d, trunc_l_d latency is 4.
  return Latency::MOVF_HIGH_DREG + 1 + Latency::BRANCH + Latency::MOV_D + 4 +
         Latency::MOVF_HIGH_DREG + Latency::BRANCH + Latency::CVT_D_L + 2 +
         Latency::MOVT_HIGH_FREG;
}
#endif
int Float32RoundLatency() {
  // For ceil_w_s, floor_w_s, round_w_s, trunc_w_s latency is 4.
  return Latency::MOVF_FREG + 1 + Latency::BRANCH + Latency::MOV_S + 4 +
         Latency::MOVF_FREG + Latency::BRANCH + Latency::CVT_S_W + 2 +
         Latency::MOVT_FREG;
}

int Float32MaxLatency() {
  // Estimated max.
  int latency = CompareIsNanF32Latency() + Latency::BRANCH;
  return latency + 5 * Latency::BRANCH + 2 * CompareF32Latency() +
         Latency::MOVF_FREG + 1 + Latency::MOV_S;
}

int Float64MaxLatency() {
  // Estimated max.
  int latency = CompareIsNanF64Latency() + Latency::BRANCH;
  return latency + 5 * Latency::BRANCH + 2 * CompareF64Latency() +
         Latency::MOVF_HIGH_DREG + Latency::MOV_D;
}

int Float32MinLatency() {
  // Estimated max.
  int latency = CompareIsNanF32Latency() + Latency::BRANCH;
  return latency + 5 * Latency::BRANCH + 2 * CompareF32Latency() +
         Latency::MOVF_FREG + 1 + Latency::MOV_S;
}

int Float64MinLatency() {
  // Estimated max.
  int latency = CompareIsNanF64Latency() + Latency::BRANCH;
  return latency + 5 * Latency::BRANCH + 2 * CompareF32Latency() +
         Latency::MOVF_HIGH_DREG + Latency::MOV_D;
}

int TruncLSLatency(bool load_status) {
  int latency = Latency::TRUNC_L_S + Latency::MOVF_HIGH_DREG;
  if (load_status) {
    latency += SltuLatency() + 7;
  }
  return latency;
}

int TruncLDLatency(bool load_status) {
  int latency = Latency::TRUNC_L_D + Latency::MOVF_HIGH_DREG;
  if (load_status) {
    latency += SltuLatency() + 7;
  }
  return latency;
}

int TruncUlSLatency() {
  // Estimated max.
  return 2 * CompareF32Latency() + CompareIsNanF32Latency() +
         4 * Latency::BRANCH + Latency::SUB_S + 2 * Latency::TRUNC_L_S +
         3 * Latency::MOVF_HIGH_DREG + OrLatency() + Latency::MOVT_FREG +
         Latency::MOV_S + SltuLatency() + 4;
}

int TruncUlDLatency() {
  // Estimated max.
  return 2 * CompareF64Latency() + CompareIsNanF64Latency() +
         4 * Latency::BRANCH + Latency::SUB_D + 2 * Latency::TRUNC_L_D +
         3 * Latency::MOVF_HIGH_DREG + OrLatency() + Latency::MOVT_DREG +
         Latency::MOV_D + SltuLatency() + 4;
}

int PushLatency() { return Add64Latency() + AlignedMemoryLatency(); }

int ByteSwapSignedLatency() { return 2; }

int LlLatency(int offset) {
  bool is_one_instruction = is_int12(offset);
  if (is_one_instruction) {
    return 1;
  } else {
    return 3;
  }
}

int ExtractBitsLatency(bool sign_extend, int size) {
  int latency = 2;
  if (sign_extend) {
    switch (size) {
      case 8:
      case 16:
      case 32:
        latency += 1;
        break;
      default:
        UNREACHABLE();
    }
  }
  return latency;
}

int InsertBitsLatency() { return 2 + Sub64Latency(false) + 2; }

int ScLatency(int offset) { return 3; }

int Word32AtomicExchangeLatency(bool sign_extend, int size) {
  return Add64Latency(false) + 1 + Sub64Latency() + 2 + LlLatency(0) +
         ExtractBitsLatency(sign_extend, size) + InsertBitsLatency() +
         ScLatency(0) + BranchShortLatency() + 1;
}

int Word32AtomicCompareExchangeLatency(bool sign_extend, int size) {
  return 2 + Sub64Latency() + 2 + LlLatency(0) +
         ExtractBitsLatency(sign_extend, size) + InsertBitsLatency() +
         ScLatency(0) + BranchShortLatency() + 1;
}

int InstructionScheduler::GetInstructionLatency(const Instruction* instr) {
  // TODO(RISCV): Verify these latencies for RISC-V (currently using MIPS
  // numbers).
  switch (instr->arch_opcode()) {
    case kArchCallCodeObject:
    case kArchCallWasmFunction:
      return CallLatency();
    case kArchTailCallCodeObject:
    case kArchTailCallWasm:
    case kArchTailCallAddress:
      return JumpLatency();
    case kArchCallJSFunction: {
      int latency = 0;
      if (v8_flags.debug_code) {
        latency = 1 + AssertLatency();
      }
      return latency + 1 + Add64Latency(false) + CallLatency();
    }
    case kArchPrepareCallCFunction:
      return PrepareCallCFunctionLatency();
    case kArchSaveCallerRegisters: {
      auto fp_mode =
          static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode()));
      return PushCallerSavedLatency(fp_mode);
    }
    case kArchRestoreCallerRegisters: {
      auto fp_mode =
          static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode()));
      return PopCallerSavedLatency(fp_mode);
    }
    case kArchPrepareTailCall:
      return 2;
    case kArchCallCFunction:
      return CallCFunctionLatency();
    case kArchJmp:
      return AssembleArchJumpLatency();
    case kArchTableSwitch:
      return AssembleArchTableSwitchLatency();
    case kArchAbortCSADcheck:
      return CallLatency() + 1;
    case kArchDebugBreak:
      return 1;
    case kArchComment:
    case kArchNop:
    case kArchThrowTerminator:
    case kArchDeoptimize:
      return 0;
    case kArchRet:
      return AssemblerReturnLatency();
    case kArchFramePointer:
      return 1;
    case kArchParentFramePointer:
      // Estimated max.
      return AlignedMemoryLatency();
    case kArchTruncateDoubleToI:
      return TruncateDoubleToIDelayedLatency();
    case kArchStoreWithWriteBarrier:
      return Add64Latency() + 1 + CheckPageFlagLatency();
    case kArchStackSlot:
      // Estimated max.
      return Add64Latency(false) + AndLatency(false) + AssertLatency() +
             Add64Latency(false) + AndLatency(false) + BranchShortLatency() +
             1 + Sub64Latency() + Add64Latency();
    case kIeee754Float64Acos:
    case kIeee754Float64Acosh:
    case kIeee754Float64Asin:
    case kIeee754Float64Asinh:
    case kIeee754Float64Atan:
    case kIeee754Float64Atanh:
    case kIeee754Float64Atan2:
    case kIeee754Float64Cos:
    case kIeee754Float64Cosh:
    case kIeee754Float64Cbrt:
    case kIeee754Float64Exp:
    case kIeee754Float64Expm1:
    case kIeee754Float64Log:
    case kIeee754Float64Log1p:
    case kIeee754Float64Log10:
    case kIeee754Float64Log2:
    case kIeee754Float64Pow:
    case kIeee754Float64Sin:
    case kIeee754Float64Sinh:
    case kIeee754Float64Tan:
    case kIeee754Float64Tanh:
      return PrepareCallCFunctionLatency() + MovToFloatParametersLatency() +
             CallCFunctionLatency() + MovFromFloatResultLatency();
#if V8_TARGET_ARCH_RISCV64
    case kRiscvAdd32:
    case kRiscvAdd64:
      return Add64Latency(instr->InputAt(1)->IsRegister());
    case kRiscvAddOvf64:
      return AddOverflow64Latency();
    case kRiscvSub32:
    case kRiscvSub64:
      return Sub64Latency(instr->InputAt(1)->IsRegister());
    case kRiscvSubOvf64:
      return SubOverflow64Latency();
    case kRiscvMulHigh64:
      return Mulh64Latency();
    case kRiscvMul64:
      return Mul64Latency();
    case kRiscvMulOvf64:
      return MulOverflow64Latency();
    case kRiscvDiv64: {
      int latency = Div64Latency();
      return latency + MovzLatency();
    }
    case kRiscvDivU64: {
      int latency = Divu64Latency();
      return latency + MovzLatency();
    }
    case kRiscvMod64:
      return Mod64Latency();
    case kRiscvModU64:
      return Modu64Latency();
#elif V8_TARGET_ARCH_RISCV32
    case kRiscvAdd32:
      return Add64Latency(instr->InputAt(1)->IsRegister());
    case kRiscvAddOvf:
      return AddOverflow64Latency();
    case kRiscvSub32:
      return Sub64Latency(instr->InputAt(1)->IsRegister());
    case kRiscvSubOvf:
      return SubOverflow64Latency();
#endif
    case kRiscvMul32:
      return Mul32Latency();
    case kRiscvMulOvf32:
      return MulOverflow32Latency();
    case kRiscvMulHigh32:
      return Mulh32Latency();
    case kRiscvMulHighU32:
      return Mulhu32Latency();
    case kRiscvDiv32: {
      int latency = Div32Latency(instr->InputAt(1)->IsRegister());
      return latency + MovzLatency();
    }
    case kRiscvDivU32: {
      int latency = Divu32Latency(instr->InputAt(1)->IsRegister());
      return latency + MovzLatency();
    }
    case kRiscvMod32:
      return Mod32Latency();
    case kRiscvModU32:
      return Modu32Latency();
    case kRiscvAnd:
      return AndLatency(instr->InputAt(1)->IsRegister());
    case kRiscvAnd32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = AndLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kRiscvOr:
      return OrLatency(instr->InputAt(1)->IsRegister());
    case kRiscvOr32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = OrLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kRiscvXor:
      return XorLatency(instr->InputAt(1)->IsRegister());
    case kRiscvXor32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = XorLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kRiscvClz32:
#if V8_TARGET_ARCH_RISCV64
    case kRiscvClz64:
#endif
      return Clz64Latency();
#if V8_TARGET_ARCH_RISCV64
    case kRiscvCtz64:
      return Ctz64Latency();
    case kRiscvPopcnt64:
      return Popcnt64Latency();
#endif
    case kRiscvCtz32:
      return Ctz32Latency();
    case kRiscvPopcnt32:
      return Popcnt32Latency();
    case kRiscvShl32:
      return 1;
    case kRiscvShr32:
    case kRiscvSar32:
#if V8_TARGET_ARCH_RISCV64
    case kRiscvZeroExtendWord:
#endif
      return 2;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvSignExtendWord:
    case kRiscvShl64:
    case kRiscvShr64:
    case kRiscvSar64:
    case kRiscvRor64:
    case kRiscvTst64:
#endif
    case kRiscvTst32:
      return AndLatency(instr->InputAt(0)->IsRegister());
    case kRiscvRor32:
      return 1;
    case kRiscvMov:
      return 1;
    case kRiscvCmpS:
      return MoveLatency() + CompareF32Latency();
    case kRiscvAddS:
      return Latency::ADD_S;
    case kRiscvSubS:
      return Latency::SUB_S;
    case kRiscvMulS:
      return Latency::MUL_S;
    case kRiscvDivS:
      return Latency::DIV_S;
    case kRiscvModS:
      return PrepareCallCFunctionLatency() + MovToFloatParametersLatency() +
             CallCFunctionLatency() + MovFromFloatResultLatency();
    case kRiscvAbsS:
      return Latency::ABS_S;
    case kRiscvNegS:
      return NegdLatency();
    case kRiscvSqrtS:
      return Latency::SQRT_S;
    case kRiscvMaxS:
      return Latency::MAX_S;
    case kRiscvMinS:
      return Latency::MIN_S;
    case kRiscvCmpD:
      return MoveLatency() + CompareF64Latency();
    case kRiscvAddD:
      return Latency::ADD_D;
    case kRiscvSubD:
      return Latency::SUB_D;
    case kRiscvMulD:
      return Latency::MUL_D;
    case kRiscvDivD:
      return Latency::DIV_D;
    case kRiscvModD:
      return PrepareCallCFunctionLatency() + MovToFloatParametersLatency() +
             CallCFunctionLatency() + MovFromFloatResultLatency();
    case kRiscvAbsD:
      return Latency::ABS_D;
    case kRiscvNegD:
      return NegdLatency();
    case kRiscvSqrtD:
      return Latency::SQRT_D;
    case kRiscvMaxD:
      return Latency::MAX_D;
    case kRiscvMinD:
      return Latency::MIN_D;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvFloat64RoundDown:
    case kRiscvFloat64RoundTruncate:
    case kRiscvFloat64RoundUp:
    case kRiscvFloat64RoundTiesEven:
      return Float64RoundLatency();
#endif
    case kRiscvFloat32RoundDown:
    case kRiscvFloat32RoundTruncate:
    case kRiscvFloat32RoundUp:
    case kRiscvFloat32RoundTiesEven:
      return Float32RoundLatency();
    case kRiscvFloat32Max:
      return Float32MaxLatency();
    case kRiscvFloat64Max:
      return Float64MaxLatency();
    case kRiscvFloat32Min:
      return Float32MinLatency();
    case kRiscvFloat64Min:
      return Float64MinLatency();
    case kRiscvFloat64SilenceNaN:
      return Latency::SUB_D;
    case kRiscvCvtSD:
      return Latency::CVT_S_D;
    case kRiscvCvtDS:
      return Latency::CVT_D_S;
    case kRiscvCvtDW:
      return Latency::MOVT_FREG + Latency::CVT_D_W;
    case kRiscvCvtSW:
      return Latency::MOVT_FREG + Latency::CVT_S_W;
    case kRiscvCvtSUw:
      return 1 + Latency::MOVT_DREG + Latency::CVT_S_L;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvCvtSL:
      return Latency::MOVT_DREG + Latency::CVT_S_L;
    case kRiscvCvtDL:
      return Latency::MOVT_DREG + Latency::CVT_D_L;
    case kRiscvCvtDUl:
      return 2 * Latency::BRANCH + 3 + 2 * Latency::MOVT_DREG +
             2 * Latency::CVT_D_L + Latency::ADD_D;
    case kRiscvCvtSUl:
      return 2 * Latency::BRANCH + 3 + 2 * Latency::MOVT_DREG +
             2 * Latency::CVT_S_L + Latency::ADD_S;
#endif
    case kRiscvCvtDUw:
      return 1 + Latency::MOVT_DREG + Latency::CVT_D_L;
    case kRiscvFloorWD:
      return Latency::FLOOR_W_D + Latency::MOVF_FREG;
    case kRiscvCeilWD:
      return Latency::CEIL_W_D + Latency::MOVF_FREG;
    case kRiscvRoundWD:
      return Latency::ROUND_W_D + Latency::MOVF_FREG;
    case kRiscvTruncWD:
      return Latency::TRUNC_W_D + Latency::MOVF_FREG;
    case kRiscvFloorWS:
      return Latency::FLOOR_W_S + Latency::MOVF_FREG;
    case kRiscvCeilWS:
      return Latency::CEIL_W_S + Latency::MOVF_FREG;
    case kRiscvRoundWS:
      return Latency::ROUND_W_S + Latency::MOVF_FREG;
    case kRiscvTruncWS:
      return Latency::TRUNC_W_S + Latency::MOVF_FREG + 2 + MovnLatency();
#if V8_TARGET_ARCH_RISCV64
    case kRiscvTruncLS:
      return TruncLSLatency(instr->OutputCount() > 1);
    case kRiscvTruncLD:
      return TruncLDLatency(instr->OutputCount() > 1);
    case kRiscvTruncUlS:
      return TruncUlSLatency();
    case kRiscvTruncUlD:
      return TruncUlDLatency();
    case kRiscvBitcastDL:
      return Latency::MOVF_HIGH_DREG;
    case kRiscvBitcastLD:
      return Latency::MOVT_DREG;
#endif
    case kRiscvTruncUwD:
      // Estimated max.
      return CompareF64Latency() + 2 * Latency::BRANCH +
             2 * Latency::TRUNC_W_D + Latency::SUB_D + OrLatency() +
             Latency::MOVT_FREG + Latency::MOVF_FREG + Latency::MOVT_HIGH_FREG +
             1;
    case kRiscvTruncUwS:
      // Estimated max.
      return CompareF32Latency() + 2 * Latency::BRANCH +
             2 * Latency::TRUNC_W_S + Latency::SUB_S + OrLatency() +
             Latency::MOVT_FREG + 2 * Latency::MOVF_FREG + 2 + MovzLatency();
    case kRiscvFloat64ExtractLowWord32:
      return Latency::MOVF_FREG;
    case kRiscvFloat64InsertLowWord32:
      return Latency::MOVF_HIGH_FREG + Latency::MOVT_FREG +
             Latency::MOVT_HIGH_FREG;
    case kRiscvFloat64ExtractHighWord32:
      return Latency::MOVF_HIGH_FREG;
    case kRiscvFloat64InsertHighWord32:
      return Latency::MOVT_HIGH_FREG;
    case kRiscvSignExtendByte:
    case kRiscvSignExtendShort:
      return 1;
    case kRiscvLbu:
    case kRiscvLb:
    case kRiscvLhu:
    case kRiscvLh:
    case kRiscvLw:
#if V8_TARGET_ARCH_RISCV64
    case kRiscvLd:
    case kRiscvSd:
    case kRiscvLwu:
#endif
    case kRiscvSb:
    case kRiscvSh:
    case kRiscvSw:
      return AlignedMemoryLatency();
    case kRiscvLoadFloat:
      return ULoadFloatLatency();
    case kRiscvLoadDouble:
      return LoadDoubleLatency();
    case kRiscvStoreFloat:
      return StoreFloatLatency();
    case kRiscvStoreDouble:
      return StoreDoubleLatency();
    case kRiscvUlhu:
    case kRiscvUlh:
      return UlhuLatency();
#if V8_TARGET_ARCH_RISCV64
    case kRiscvUlwu:
      return UlwuLatency();
    case kRiscvUld:
      return UldLatency();
    case kRiscvUsd:
      return UsdLatency();
    case kRiscvByteSwap64:
      return ByteSwapSignedLatency();
#endif
    case kRiscvUlw:
      return UlwLatency();
    case kRiscvULoadFloat:
      return ULoadFloatLatency();
    case kRiscvULoadDouble:
      return ULoadDoubleLatency();
    case kRiscvUsh:
      return UshLatency();
    case kRiscvUsw:
      return UswLatency();
    case kRiscvUStoreFloat:
      return UStoreFloatLatency();
    case kRiscvUStoreDouble:
      return UStoreDoubleLatency();
    case kRiscvPush: {
      int latency = 0;
      if (instr->InputAt(0)->IsFPRegister()) {
        latency = StoreDoubleLatency() + Sub64Latency(false);
      } else {
        latency = PushLatency();
      }
      return latency;
    }
    case kRiscvPeek: {
      int latency = 0;
      if (instr->OutputAt(0)->IsFPRegister()) {
        auto op = LocationOperand::cast(instr->OutputAt(0));
        switch (op->representation()) {
          case MachineRepresentation::kFloat64:
            latency = LoadDoubleLatency();
            break;
          case MachineRepresentation::kFloat32:
            latency = Latency::LOAD_FLOAT;
            break;
          default:
            UNREACHABLE();
        }
      } else {
        latency = AlignedMemoryLatency();
      }
      return latency;
    }
    case kRiscvStackClaim:
      return Sub64Latency(false);
    case kRiscvStoreToStackSlot: {
      int latency = 0;
      if (instr->InputAt(0)->IsFPRegister()) {
        if (instr->InputAt(0)->IsSimd128Register()) {
          latency = 1;  // Estimated value.
        } else {
          latency = StoreDoubleLatency();
        }
      } else {
        latency = AlignedMemoryLatency();
      }
      return latency;
    }
    case kRiscvByteSwap32:
      return ByteSwapSignedLatency();
    case kAtomicLoadInt8:
    case kAtomicLoadUint8:
    case kAtomicLoadInt16:
    case kAtomicLoadUint16:
    case kAtomicLoadWord32:
      return 2;
    case kAtomicStoreWord8:
    case kAtomicStoreWord16:
    case kAtomicStoreWord32:
      return 3;
    case kAtomicExchangeInt8:
      return Word32AtomicExchangeLatency(true, 8);
    case kAtomicExchangeUint8:
      return Word32AtomicExchangeLatency(false, 8);
    case kAtomicExchangeInt16:
      return Word32AtomicExchangeLatency(true, 16);
    case kAtomicExchangeUint16:
      return Word32AtomicExchangeLatency(false, 16);
    case kAtomicExchangeWord32:
      return 2 + LlLatency(0) + 1 + ScLatency(0) + BranchShortLatency() + 1;
    case kAtomicCompareExchangeInt8:
      return Word32AtomicCompareExchangeLatency(true, 8);
    case kAtomicCompareExchangeUint8:
      return Word32AtomicCompareExchangeLatency(false, 8);
    case kAtomicCompareExchangeInt16:
      return Word32AtomicCompareExchangeLatency(true, 16);
    case kAtomicCompareExchangeUint16:
      return Word32AtomicCompareExchangeLatency(false, 16);
    case kAtomicCompareExchangeWord32:
      return 3 + LlLatency(0) + BranchShortLatency() + 1 + ScLatency(0) +
             BranchShortLatency() + 1;
    case kRiscvAssertEqual:
      return AssertLatency();
#ifdef V8_TARGET_ARCH_RISCV64
    case kRiscvLoadDecompressProtected:
      return 11;
#endif
    default:
      return 1;
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
