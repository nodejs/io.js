// Copyright 2015 the V8 project authors. All rights reserved.
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
    case kMips64AbsD:
    case kMips64AbsS:
    case kMips64Add:
    case kMips64AddD:
    case kMips64AddS:
    case kMips64And:
    case kMips64And32:
    case kMips64AssertEqual:
    case kMips64BitcastDL:
    case kMips64BitcastLD:
    case kMips64ByteSwap32:
    case kMips64ByteSwap64:
    case kMips64CeilWD:
    case kMips64CeilWS:
    case kMips64Clz:
    case kMips64Cmp:
    case kMips64CmpD:
    case kMips64CmpS:
    case kMips64Ctz:
    case kMips64CvtDL:
    case kMips64CvtDS:
    case kMips64CvtDUl:
    case kMips64CvtDUw:
    case kMips64CvtDW:
    case kMips64CvtSD:
    case kMips64CvtSL:
    case kMips64CvtSUl:
    case kMips64CvtSUw:
    case kMips64CvtSW:
    case kMips64DMulHigh:
    case kMips64DMulHighU:
    case kMips64DMulOvf:
    case kMips64MulHighU:
    case kMips64Dadd:
    case kMips64DaddOvf:
    case kMips64Dclz:
    case kMips64Dctz:
    case kMips64Ddiv:
    case kMips64DdivU:
    case kMips64Dext:
    case kMips64Dins:
    case kMips64Div:
    case kMips64DivD:
    case kMips64DivS:
    case kMips64DivU:
    case kMips64Dlsa:
    case kMips64Dmod:
    case kMips64DmodU:
    case kMips64Dmul:
    case kMips64Dpopcnt:
    case kMips64Dror:
    case kMips64Dsar:
    case kMips64Dshl:
    case kMips64Dshr:
    case kMips64Dsub:
    case kMips64DsubOvf:
    case kMips64Ext:
    case kMips64F64x2Abs:
    case kMips64F64x2Neg:
    case kMips64F64x2Sqrt:
    case kMips64F64x2Add:
    case kMips64F64x2Sub:
    case kMips64F64x2Mul:
    case kMips64F64x2Div:
    case kMips64F64x2Min:
    case kMips64F64x2Max:
    case kMips64F64x2Eq:
    case kMips64F64x2Ne:
    case kMips64F64x2Lt:
    case kMips64F64x2Le:
    case kMips64F64x2Pmin:
    case kMips64F64x2Pmax:
    case kMips64F64x2Ceil:
    case kMips64F64x2Floor:
    case kMips64F64x2Trunc:
    case kMips64F64x2NearestInt:
    case kMips64F64x2ConvertLowI32x4S:
    case kMips64F64x2ConvertLowI32x4U:
    case kMips64F64x2PromoteLowF32x4:
    case kMips64I64x2Splat:
    case kMips64I64x2ExtractLane:
    case kMips64I64x2ReplaceLane:
    case kMips64I64x2Add:
    case kMips64I64x2Sub:
    case kMips64I64x2Mul:
    case kMips64I64x2Neg:
    case kMips64I64x2Shl:
    case kMips64I64x2ShrS:
    case kMips64I64x2ShrU:
    case kMips64I64x2BitMask:
    case kMips64I64x2Eq:
    case kMips64I64x2Ne:
    case kMips64I64x2GtS:
    case kMips64I64x2GeS:
    case kMips64I64x2Abs:
    case kMips64I64x2SConvertI32x4Low:
    case kMips64I64x2SConvertI32x4High:
    case kMips64I64x2UConvertI32x4Low:
    case kMips64I64x2UConvertI32x4High:
    case kMips64ExtMulLow:
    case kMips64ExtMulHigh:
    case kMips64ExtAddPairwise:
    case kMips64F32x4Abs:
    case kMips64F32x4Add:
    case kMips64F32x4Eq:
    case kMips64F32x4ExtractLane:
    case kMips64F32x4Lt:
    case kMips64F32x4Le:
    case kMips64F32x4Max:
    case kMips64F32x4Min:
    case kMips64F32x4Mul:
    case kMips64F32x4Div:
    case kMips64F32x4Ne:
    case kMips64F32x4Neg:
    case kMips64F32x4Sqrt:
    case kMips64F32x4ReplaceLane:
    case kMips64F32x4SConvertI32x4:
    case kMips64F32x4Splat:
    case kMips64F32x4Sub:
    case kMips64F32x4UConvertI32x4:
    case kMips64F32x4Pmin:
    case kMips64F32x4Pmax:
    case kMips64F32x4Ceil:
    case kMips64F32x4Floor:
    case kMips64F32x4Trunc:
    case kMips64F32x4NearestInt:
    case kMips64F32x4DemoteF64x2Zero:
    case kMips64F64x2Splat:
    case kMips64F64x2ExtractLane:
    case kMips64F64x2ReplaceLane:
    case kMips64Float32Max:
    case kMips64Float32Min:
    case kMips64Float32RoundDown:
    case kMips64Float32RoundTiesEven:
    case kMips64Float32RoundTruncate:
    case kMips64Float32RoundUp:
    case kMips64Float64ExtractLowWord32:
    case kMips64Float64ExtractHighWord32:
    case kMips64Float64FromWord32Pair:
    case kMips64Float64InsertLowWord32:
    case kMips64Float64InsertHighWord32:
    case kMips64Float64Max:
    case kMips64Float64Min:
    case kMips64Float64RoundDown:
    case kMips64Float64RoundTiesEven:
    case kMips64Float64RoundTruncate:
    case kMips64Float64RoundUp:
    case kMips64Float64SilenceNaN:
    case kMips64FloorWD:
    case kMips64FloorWS:
    case kMips64I16x8Add:
    case kMips64I16x8AddSatS:
    case kMips64I16x8AddSatU:
    case kMips64I16x8Eq:
    case kMips64I16x8ExtractLaneU:
    case kMips64I16x8ExtractLaneS:
    case kMips64I16x8GeS:
    case kMips64I16x8GeU:
    case kMips64I16x8GtS:
    case kMips64I16x8GtU:
    case kMips64I16x8MaxS:
    case kMips64I16x8MaxU:
    case kMips64I16x8MinS:
    case kMips64I16x8MinU:
    case kMips64I16x8Mul:
    case kMips64I16x8Ne:
    case kMips64I16x8Neg:
    case kMips64I16x8ReplaceLane:
    case kMips64I8x16SConvertI16x8:
    case kMips64I16x8SConvertI32x4:
    case kMips64I16x8SConvertI8x16High:
    case kMips64I16x8SConvertI8x16Low:
    case kMips64I16x8Shl:
    case kMips64I16x8ShrS:
    case kMips64I16x8ShrU:
    case kMips64I16x8Splat:
    case kMips64I16x8Sub:
    case kMips64I16x8SubSatS:
    case kMips64I16x8SubSatU:
    case kMips64I8x16UConvertI16x8:
    case kMips64I16x8UConvertI32x4:
    case kMips64I16x8UConvertI8x16High:
    case kMips64I16x8UConvertI8x16Low:
    case kMips64I16x8RoundingAverageU:
    case kMips64I16x8Abs:
    case kMips64I16x8BitMask:
    case kMips64I16x8Q15MulRSatS:
    case kMips64I32x4Add:
    case kMips64I32x4Eq:
    case kMips64I32x4ExtractLane:
    case kMips64I32x4GeS:
    case kMips64I32x4GeU:
    case kMips64I32x4GtS:
    case kMips64I32x4GtU:
    case kMips64I32x4MaxS:
    case kMips64I32x4MaxU:
    case kMips64I32x4MinS:
    case kMips64I32x4MinU:
    case kMips64I32x4Mul:
    case kMips64I32x4Ne:
    case kMips64I32x4Neg:
    case kMips64I32x4ReplaceLane:
    case kMips64I32x4SConvertF32x4:
    case kMips64I32x4SConvertI16x8High:
    case kMips64I32x4SConvertI16x8Low:
    case kMips64I32x4Shl:
    case kMips64I32x4ShrS:
    case kMips64I32x4ShrU:
    case kMips64I32x4Splat:
    case kMips64I32x4Sub:
    case kMips64I32x4UConvertF32x4:
    case kMips64I32x4UConvertI16x8High:
    case kMips64I32x4UConvertI16x8Low:
    case kMips64I32x4Abs:
    case kMips64I32x4BitMask:
    case kMips64I32x4DotI16x8S:
    case kMips64I32x4TruncSatF64x2SZero:
    case kMips64I32x4TruncSatF64x2UZero:
    case kMips64I8x16Add:
    case kMips64I8x16AddSatS:
    case kMips64I8x16AddSatU:
    case kMips64I8x16Eq:
    case kMips64I8x16ExtractLaneU:
    case kMips64I8x16ExtractLaneS:
    case kMips64I8x16GeS:
    case kMips64I8x16GeU:
    case kMips64I8x16GtS:
    case kMips64I8x16GtU:
    case kMips64I8x16MaxS:
    case kMips64I8x16MaxU:
    case kMips64I8x16MinS:
    case kMips64I8x16MinU:
    case kMips64I8x16Ne:
    case kMips64I8x16Neg:
    case kMips64I8x16ReplaceLane:
    case kMips64I8x16Shl:
    case kMips64I8x16ShrS:
    case kMips64I8x16ShrU:
    case kMips64I8x16Splat:
    case kMips64I8x16Sub:
    case kMips64I8x16SubSatS:
    case kMips64I8x16SubSatU:
    case kMips64I8x16RoundingAverageU:
    case kMips64I8x16Abs:
    case kMips64I8x16Popcnt:
    case kMips64I8x16BitMask:
    case kMips64Ins:
    case kMips64Lsa:
    case kMips64MaxD:
    case kMips64MaxS:
    case kMips64MinD:
    case kMips64MinS:
    case kMips64Mod:
    case kMips64ModU:
    case kMips64Mov:
    case kMips64Mul:
    case kMips64MulD:
    case kMips64MulHigh:
    case kMips64MulOvf:
    case kMips64MulS:
    case kMips64NegD:
    case kMips64NegS:
    case kMips64Nor:
    case kMips64Nor32:
    case kMips64Or:
    case kMips64Or32:
    case kMips64Popcnt:
    case kMips64Ror:
    case kMips64RoundWD:
    case kMips64RoundWS:
    case kMips64S128And:
    case kMips64S128Or:
    case kMips64S128Not:
    case kMips64S128Select:
    case kMips64S128AndNot:
    case kMips64S128Xor:
    case kMips64S128Const:
    case kMips64S128Zero:
    case kMips64S128AllOnes:
    case kMips64S16x8InterleaveEven:
    case kMips64S16x8InterleaveOdd:
    case kMips64S16x8InterleaveLeft:
    case kMips64S16x8InterleaveRight:
    case kMips64S16x8PackEven:
    case kMips64S16x8PackOdd:
    case kMips64S16x2Reverse:
    case kMips64S16x4Reverse:
    case kMips64I64x2AllTrue:
    case kMips64I32x4AllTrue:
    case kMips64I16x8AllTrue:
    case kMips64I8x16AllTrue:
    case kMips64V128AnyTrue:
    case kMips64S32x4InterleaveEven:
    case kMips64S32x4InterleaveOdd:
    case kMips64S32x4InterleaveLeft:
    case kMips64S32x4InterleaveRight:
    case kMips64S32x4PackEven:
    case kMips64S32x4PackOdd:
    case kMips64S32x4Shuffle:
    case kMips64S8x16Concat:
    case kMips64S8x16InterleaveEven:
    case kMips64S8x16InterleaveOdd:
    case kMips64S8x16InterleaveLeft:
    case kMips64S8x16InterleaveRight:
    case kMips64S8x16PackEven:
    case kMips64S8x16PackOdd:
    case kMips64S8x2Reverse:
    case kMips64S8x4Reverse:
    case kMips64S8x8Reverse:
    case kMips64I8x16Shuffle:
    case kMips64I8x16Swizzle:
    case kMips64Sar:
    case kMips64Seb:
    case kMips64Seh:
    case kMips64Shl:
    case kMips64Shr:
    case kMips64SqrtD:
    case kMips64SqrtS:
    case kMips64Sub:
    case kMips64SubD:
    case kMips64SubS:
    case kMips64TruncLD:
    case kMips64TruncLS:
    case kMips64TruncUlD:
    case kMips64TruncUlS:
    case kMips64TruncUwD:
    case kMips64TruncUwS:
    case kMips64TruncWD:
    case kMips64TruncWS:
    case kMips64Tst:
    case kMips64Xor:
    case kMips64Xor32:
      return kNoOpcodeFlags;

    case kMips64Lb:
    case kMips64Lbu:
    case kMips64Ld:
    case kMips64Ldc1:
    case kMips64Lh:
    case kMips64Lhu:
    case kMips64Lw:
    case kMips64Lwc1:
    case kMips64Lwu:
    case kMips64MsaLd:
    case kMips64Peek:
    case kMips64Uld:
    case kMips64Uldc1:
    case kMips64Ulh:
    case kMips64Ulhu:
    case kMips64Ulw:
    case kMips64Ulwu:
    case kMips64Ulwc1:
    case kMips64S128LoadSplat:
    case kMips64S128Load8x8S:
    case kMips64S128Load8x8U:
    case kMips64S128Load16x4S:
    case kMips64S128Load16x4U:
    case kMips64S128Load32x2S:
    case kMips64S128Load32x2U:
    case kMips64S128Load32Zero:
    case kMips64S128Load64Zero:
    case kMips64S128LoadLane:
    case kMips64Word64AtomicLoadUint64:

      return kIsLoadOperation;

    case kMips64ModD:
    case kMips64MsaSt:
    case kMips64Push:
    case kMips64Sb:
    case kMips64Sd:
    case kMips64Sdc1:
    case kMips64Sh:
    case kMips64StackClaim:
    case kMips64StoreToStackSlot:
    case kMips64Sw:
    case kMips64Swc1:
    case kMips64Usd:
    case kMips64Usdc1:
    case kMips64Ush:
    case kMips64Usw:
    case kMips64Uswc1:
    case kMips64Sync:
    case kMips64S128StoreLane:
    case kMips64StoreCompressTagged:
    case kMips64Word64AtomicStoreWord64:
    case kMips64Word64AtomicAddUint64:
    case kMips64Word64AtomicSubUint64:
    case kMips64Word64AtomicAndUint64:
    case kMips64Word64AtomicOrUint64:
    case kMips64Word64AtomicXorUint64:
    case kMips64Word64AtomicExchangeUint64:
    case kMips64Word64AtomicCompareExchangeUint64:
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
  BRANCH = 4,  // Estimated max.
  RINT_S = 4,  // Estimated.
  RINT_D = 4,  // Estimated.

  MULT = 4,
  MULTU = 4,
  DMULT = 4,
  DMULTU = 4,

  MUL = 7,
  DMUL = 7,
  MUH = 7,
  MUHU = 7,
  DMUH = 7,
  DMUHU = 7,

  DIV = 50,  // Min:11 Max:50
  DDIV = 50,
  DIVU = 50,
  DDIVU = 50,

  ABS_S = 4,
  ABS_D = 4,
  NEG_S = 4,
  NEG_D = 4,
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

  MTC1 = 4,
  MTHC1 = 4,
  DMTC1 = 4,
  LWC1 = 4,
  LDC1 = 4,

  MFC1 = 1,
  MFHC1 = 1,
  DMFC1 = 1,
  MFHI = 1,
  MFLO = 1,
  SWC1 = 1,
  SDC1 = 1,
};

int DadduLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return 1;
  } else {
    return 2;  // Estimated max.
  }
}

int DsubuLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int AndLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int OrLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int NorLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return 1;
  } else {
    return 2;  // Estimated max.
  }
}

int XorLatency(bool is_operand_register = true) {
  return DadduLatency(is_operand_register);
}

int MulLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::MUL;
  } else {
    return Latency::MUL + 1;
  }
}

int DmulLatency(bool is_operand_register = true) {
  int latency = 0;
  if (kArchVariant >= kMips64r6) {
    latency = Latency::DMUL;
  } else {
    latency = Latency::DMULT + Latency::MFLO;
  }
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int MulhLatency(bool is_operand_register = true) {
  int latency = 0;
  if (kArchVariant >= kMips64r6) {
    latency = Latency::MUH;
  } else {
    latency = Latency::MULT + Latency::MFHI;
  }
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int MulhuLatency(bool is_operand_register = true) {
  int latency = 0;
  if (kArchVariant >= kMips64r6) {
    latency = Latency::MUH;
  } else {
    latency = Latency::MULTU + Latency::MFHI;
  }
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DMulhLatency(bool is_operand_register = true) {
  int latency = 0;
  if (kArchVariant >= kMips64r6) {
    latency = Latency::DMUH;
  } else {
    latency = Latency::DMULT + Latency::MFHI;
  }
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DivLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::DIV;
  } else {
    return Latency::DIV + 1;
  }
}

int DivuLatency(bool is_operand_register = true) {
  if (is_operand_register) {
    return Latency::DIVU;
  } else {
    return Latency::DIVU + 1;
  }
}

int DdivLatency(bool is_operand_register = true) {
  int latency = 0;
  if (kArchVariant >= kMips64r6) {
    latency = Latency::DDIV;
  } else {
    latency = Latency::DDIV + Latency::MFLO;
  }
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DdivuLatency(bool is_operand_register = true) {
  int latency = 0;
  if (kArchVariant >= kMips64r6) {
    latency = Latency::DDIVU;
  } else {
    latency = Latency::DDIVU + Latency::MFLO;
  }
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int ModLatency(bool is_operand_register = true) {
  int latency = 0;
  if (kArchVariant >= kMips64r6) {
    latency = 1;
  } else {
    latency = Latency::DIV + Latency::MFHI;
  }
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int ModuLatency(bool is_operand_register = true) {
  int latency = 0;
  if (kArchVariant >= kMips64r6) {
    latency = 1;
  } else {
    latency = Latency::DIVU + Latency::MFHI;
  }
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DmodLatency(bool is_operand_register = true) {
  int latency = 0;
  if (kArchVariant >= kMips64r6) {
    latency = 1;
  } else {
    latency = Latency::DDIV + Latency::MFHI;
  }
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int DmoduLatency(bool is_operand_register = true) {
  int latency = 0;
  if (kArchVariant >= kMips64r6) {
    latency = 1;
  } else {
    latency = Latency::DDIV + Latency::MFHI;
  }
  if (!is_operand_register) {
    latency += 1;
  }
  return latency;
}

int MovzLatency() {
  if (kArchVariant >= kMips64r6) {
    return Latency::BRANCH + 1;
  } else {
    return 1;
  }
}

int MovnLatency() {
  if (kArchVariant >= kMips64r6) {
    return Latency::BRANCH + 1;
  } else {
    return 1;
  }
}

int DlsaLatency() {
  // Estimated max.
  return DadduLatency() + 1;
}

int CallLatency() {
  // Estimated.
  return DadduLatency(false) + Latency::BRANCH + 5;
}

int JumpLatency() {
  // Estimated max.
  return 1 + DadduLatency() + Latency::BRANCH + 2;
}

int SmiUntagLatency() { return 1; }

int PrepareForTailCallLatency() {
  // Estimated max.
  return 2 * (DlsaLatency() + DadduLatency(false)) + 2 + Latency::BRANCH +
         Latency::BRANCH + 2 * DsubuLatency(false) + 2 + Latency::BRANCH + 1;
}

int AssertLatency() { return 1; }

int PrepareCallCFunctionLatency() {
  int frame_alignment = MacroAssembler::ActivationFrameAlignment();
  if (frame_alignment > kSystemPointerSize) {
    return 1 + DsubuLatency(false) + AndLatency(false) + 1;
  } else {
    return DsubuLatency(false);
  }
}

int AdjustBaseAndOffsetLatency() {
  return 3;  // Estimated max.
}

int AlignedMemoryLatency() { return AdjustBaseAndOffsetLatency() + 1; }

int UlhuLatency() {
  if (kArchVariant >= kMips64r6) {
    return AlignedMemoryLatency();
  } else {
    return AdjustBaseAndOffsetLatency() + 2 * AlignedMemoryLatency() + 2;
  }
}

int UlwLatency() {
  if (kArchVariant >= kMips64r6) {
    return AlignedMemoryLatency();
  } else {
    // Estimated max.
    return AdjustBaseAndOffsetLatency() + 3;
  }
}

int UlwuLatency() {
  if (kArchVariant >= kMips64r6) {
    return AlignedMemoryLatency();
  } else {
    return UlwLatency() + 1;
  }
}

int UldLatency() {
  if (kArchVariant >= kMips64r6) {
    return AlignedMemoryLatency();
  } else {
    // Estimated max.
    return AdjustBaseAndOffsetLatency() + 3;
  }
}

int Ulwc1Latency() {
  if (kArchVariant >= kMips64r6) {
    return AlignedMemoryLatency();
  } else {
    return UlwLatency() + Latency::MTC1;
  }
}

int Uldc1Latency() {
  if (kArchVariant >= kMips64r6) {
    return AlignedMemoryLatency();
  } else {
    return UldLatency() + Latency::DMTC1;
  }
}

int UshLatency() {
  if (kArchVariant >= kMips64r6) {
    return AlignedMemoryLatency();
  } else {
    // Estimated max.
    return AdjustBaseAndOffsetLatency() + 2 + 2 * AlignedMemoryLatency();
  }
}

int UswLatency() {
  if (kArchVariant >= kMips64r6) {
    return AlignedMemoryLatency();
  } else {
    return AdjustBaseAndOffsetLatency() + 2;
  }
}

int UsdLatency() {
  if (kArchVariant >= kMips64r6) {
    return AlignedMemoryLatency();
  } else {
    return AdjustBaseAndOffsetLatency() + 2;
  }
}

int Uswc1Latency() {
  if (kArchVariant >= kMips64r6) {
    return AlignedMemoryLatency();
  } else {
    return Latency::MFC1 + UswLatency();
  }
}

int Usdc1Latency() {
  if (kArchVariant >= kMips64r6) {
    return AlignedMemoryLatency();
  } else {
    return Latency::DMFC1 + UsdLatency();
  }
}

int Lwc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::LWC1; }

int Swc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::SWC1; }

int Sdc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::SDC1; }

int Ldc1Latency() { return AdjustBaseAndOffsetLatency() + Latency::LDC1; }

int MultiPushLatency() {
  int latency = DsubuLatency(false);
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    latency++;
  }
  return latency;
}

int MultiPushFPULatency() {
  int latency = DsubuLatency(false);
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    latency += Sdc1Latency();
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
  int latency = DadduLatency(false);
  for (int16_t i = 0; i < kNumRegisters; i++) {
    latency++;
  }
  return latency;
}

int MultiPopFPULatency() {
  int latency = DadduLatency(false);
  for (int16_t i = 0; i < kNumRegisters; i++) {
    latency += Ldc1Latency();
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
    latency += DadduLatency(false);
  }
  return latency;
}

int CallCFunctionLatency() { return 1 + CallCFunctionHelperLatency(); }

int AssembleArchJumpLatency() {
  // Estimated max.
  return Latency::BRANCH;
}

int GenerateSwitchTableLatency() {
  int latency = 0;
  if (kArchVariant >= kMips64r6) {
    latency = DlsaLatency() + 2;
  } else {
    latency = 6;
  }
  latency += 2;
  return latency;
}

int AssembleArchTableSwitchLatency() {
  return Latency::BRANCH + GenerateSwitchTableLatency();
}

int DropAndRetLatency() {
  // Estimated max.
  return DadduLatency(false) + JumpLatency();
}

int AssemblerReturnLatency() {
  // Estimated max.
  return DadduLatency(false) + MultiPopLatency() + MultiPopFPULatency() +
         Latency::BRANCH + DadduLatency() + 1 + DropAndRetLatency();
}

int TryInlineTruncateDoubleToILatency() {
  return 2 + Latency::TRUNC_W_D + Latency::MFC1 + 2 + AndLatency(false) +
         Latency::BRANCH;
}

int CallStubDelayedLatency() { return 1 + CallLatency(); }

int TruncateDoubleToIDelayedLatency() {
  // TODO(mips): This no longer reflects how TruncateDoubleToI is called.
  return TryInlineTruncateDoubleToILatency() + 1 + DsubuLatency(false) +
         Sdc1Latency() + CallStubDelayedLatency() + DadduLatency(false) + 1;
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

int BranchShortHelperR6Latency() {
  return 2;  // Estimated max.
}

int BranchShortHelperLatency() {
  return SltuLatency() + 2;  // Estimated max.
}

int BranchShortLatency(BranchDelaySlot bdslot = PROTECT) {
  if (kArchVariant >= kMips64r6 && bdslot == PROTECT) {
    return BranchShortHelperR6Latency();
  } else {
    return BranchShortHelperLatency();
  }
}

int MoveLatency() { return 1; }

int MovToFloatParametersLatency() { return 2 * MoveLatency(); }

int MovFromFloatResultLatency() { return MoveLatency(); }

int DaddOverflowLatency() {
  // Estimated max.
  return 6;
}

int DsubOverflowLatency() {
  // Estimated max.
  return 6;
}

int MulOverflowLatency() {
  // Estimated max.
  return MulLatency() + MulhLatency() + 2;
}

int DclzLatency() { return 1; }

int CtzLatency() {
  if (kArchVariant >= kMips64r6) {
    return 3 + DclzLatency();
  } else {
    return DadduLatency(false) + XorLatency() + AndLatency() + DclzLatency() +
           1 + DsubuLatency();
  }
}

int DctzLatency() {
  if (kArchVariant >= kMips64r6) {
    return 4;
  } else {
    return DadduLatency(false) + XorLatency() + AndLatency() + 1 +
           DsubuLatency();
  }
}

int PopcntLatency() {
  return 2 + AndLatency() + DsubuLatency() + 1 + AndLatency() + 1 +
         AndLatency() + DadduLatency() + 1 + DadduLatency() + 1 + AndLatency() +
         1 + MulLatency() + 1;
}

int DpopcntLatency() {
  return 2 + AndLatency() + DsubuLatency() + 1 + AndLatency() + 1 +
         AndLatency() + DadduLatency() + 1 + DadduLatency() + 1 + AndLatency() +
         1 + DmulLatency() + 1;
}

int CompareFLatency() { return Latency::C_cond_S; }

int CompareF32Latency() { return CompareFLatency(); }

int CompareF64Latency() { return CompareFLatency(); }

int CompareIsNanFLatency() { return CompareFLatency(); }

int CompareIsNanF32Latency() { return CompareIsNanFLatency(); }

int CompareIsNanF64Latency() { return CompareIsNanFLatency(); }

int NegsLatency() {
  if (kArchVariant >= kMips64r6) {
    return Latency::NEG_S;
  } else {
    // Estimated.
    return CompareIsNanF32Latency() + 2 * Latency::BRANCH + Latency::NEG_S +
           Latency::MFC1 + 1 + XorLatency() + Latency::MTC1;
  }
}

int NegdLatency() {
  if (kArchVariant >= kMips64r6) {
    return Latency::NEG_D;
  } else {
    // Estimated.
    return CompareIsNanF64Latency() + 2 * Latency::BRANCH + Latency::NEG_D +
           Latency::DMFC1 + 1 + XorLatency() + Latency::DMTC1;
  }
}

int Float64RoundLatency() {
  if (kArchVariant >= kMips64r6) {
    return Latency::RINT_D + 4;
  } else {
    // For ceil_l_d, floor_l_d, round_l_d, trunc_l_d latency is 4.
    return Latency::DMFC1 + 1 + Latency::BRANCH + Latency::MOV_D + 4 +
           Latency::DMFC1 + Latency::BRANCH + Latency::CVT_D_L + 2 +
           Latency::MTHC1;
  }
}

int Float32RoundLatency() {
  if (kArchVariant >= kMips64r6) {
    return Latency::RINT_S + 4;
  } else {
    // For ceil_w_s, floor_w_s, round_w_s, trunc_w_s latency is 4.
    return Latency::MFC1 + 1 + Latency::BRANCH + Latency::MOV_S + 4 +
           Latency::MFC1 + Latency::BRANCH + Latency::CVT_S_W + 2 +
           Latency::MTC1;
  }
}

int Float32MaxLatency() {
  // Estimated max.
  int latency = CompareIsNanF32Latency() + Latency::BRANCH;
  if (kArchVariant >= kMips64r6) {
    return latency + Latency::MAX_S;
  } else {
    return latency + 5 * Latency::BRANCH + 2 * CompareF32Latency() +
           Latency::MFC1 + 1 + Latency::MOV_S;
  }
}

int Float64MaxLatency() {
  // Estimated max.
  int latency = CompareIsNanF64Latency() + Latency::BRANCH;
  if (kArchVariant >= kMips64r6) {
    return latency + Latency::MAX_D;
  } else {
    return latency + 5 * Latency::BRANCH + 2 * CompareF64Latency() +
           Latency::DMFC1 + Latency::MOV_D;
  }
}

int Float32MinLatency() {
  // Estimated max.
  int latency = CompareIsNanF32Latency() + Latency::BRANCH;
  if (kArchVariant >= kMips64r6) {
    return latency + Latency::MIN_S;
  } else {
    return latency + 5 * Latency::BRANCH + 2 * CompareF32Latency() +
           Latency::MFC1 + 1 + Latency::MOV_S;
  }
}

int Float64MinLatency() {
  // Estimated max.
  int latency = CompareIsNanF64Latency() + Latency::BRANCH;
  if (kArchVariant >= kMips64r6) {
    return latency + Latency::MIN_D;
  } else {
    return latency + 5 * Latency::BRANCH + 2 * CompareF32Latency() +
           Latency::DMFC1 + Latency::MOV_D;
  }
}

int TruncLSLatency(bool load_status) {
  int latency = Latency::TRUNC_L_S + Latency::DMFC1;
  if (load_status) {
    latency += SltuLatency() + 7;
  }
  return latency;
}

int TruncLDLatency(bool load_status) {
  int latency = Latency::TRUNC_L_D + Latency::DMFC1;
  if (load_status) {
    latency += SltuLatency() + 7;
  }
  return latency;
}

int TruncUlSLatency() {
  // Estimated max.
  return 2 * CompareF32Latency() + CompareIsNanF32Latency() +
         4 * Latency::BRANCH + Latency::SUB_S + 2 * Latency::TRUNC_L_S +
         3 * Latency::DMFC1 + OrLatency() + Latency::MTC1 + Latency::MOV_S +
         SltuLatency() + 4;
}

int TruncUlDLatency() {
  // Estimated max.
  return 2 * CompareF64Latency() + CompareIsNanF64Latency() +
         4 * Latency::BRANCH + Latency::SUB_D + 2 * Latency::TRUNC_L_D +
         3 * Latency::DMFC1 + OrLatency() + Latency::DMTC1 + Latency::MOV_D +
         SltuLatency() + 4;
}

int PushLatency() { return DadduLatency() + AlignedMemoryLatency(); }

int ByteSwapSignedLatency() { return 2; }

int LlLatency(int offset) {
  bool is_one_instruction =
      (kArchVariant == kMips64r6) ? is_int9(offset) : is_int16(offset);
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

int InsertBitsLatency() { return 2 + DsubuLatency(false) + 2; }

int ScLatency(int offset) {
  bool is_one_instruction =
      (kArchVariant == kMips64r6) ? is_int9(offset) : is_int16(offset);
  if (is_one_instruction) {
    return 1;
  } else {
    return 3;
  }
}

int Word32AtomicExchangeLatency(bool sign_extend, int size) {
  return DadduLatency(false) + 1 + DsubuLatency() + 2 + LlLatency(0) +
         ExtractBitsLatency(sign_extend, size) + InsertBitsLatency() +
         ScLatency(0) + BranchShortLatency() + 1;
}

int Word32AtomicCompareExchangeLatency(bool sign_extend, int size) {
  return 2 + DsubuLatency() + 2 + LlLatency(0) +
         ExtractBitsLatency(sign_extend, size) + InsertBitsLatency() +
         ScLatency(0) + BranchShortLatency() + 1;
}

int InstructionScheduler::GetInstructionLatency(const Instruction* instr) {
  // Basic latency modeling for MIPS64 instructions. They have been determined
  // in empirical way.
  switch (instr->arch_opcode()) {
    case kArchCallCodeObject:
#if V8_ENABLE_WEBASSEMBLY
    case kArchCallWasmFunction:
#endif  // V8_ENABLE_WEBASSEMBLY
      return CallLatency();
    case kArchTailCallCodeObject:
#if V8_ENABLE_WEBASSEMBLY
    case kArchTailCallWasm:
#endif  // V8_ENABLE_WEBASSEMBLY
    case kArchTailCallAddress:
      return JumpLatency();
    case kArchCallJSFunction: {
      int latency = 0;
      if (v8_flags.debug_code) {
        latency = 1 + AssertLatency();
      }
      return latency + 1 + DadduLatency(false) + CallLatency();
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
      return DadduLatency() + 1 + CheckPageFlagLatency();
    case kArchStackSlot:
      // Estimated max.
      return DadduLatency(false) + AndLatency(false) + AssertLatency() +
             DadduLatency(false) + AndLatency(false) + BranchShortLatency() +
             1 + DsubuLatency() + DadduLatency();
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
    case kMips64Add:
    case kMips64Dadd:
      return DadduLatency(instr->InputAt(1)->IsRegister());
    case kMips64DaddOvf:
      return DaddOverflowLatency();
    case kMips64Sub:
    case kMips64Dsub:
      return DsubuLatency(instr->InputAt(1)->IsRegister());
    case kMips64DsubOvf:
      return DsubOverflowLatency();
    case kMips64Mul:
      return MulLatency();
    case kMips64MulOvf:
    case kMips64DMulOvf:
      return MulOverflowLatency();
    case kMips64MulHigh:
      return MulhLatency();
    case kMips64MulHighU:
      return MulhuLatency();
    case kMips64DMulHigh:
      return DMulhLatency();
    case kMips64Div: {
      int latency = DivLatency(instr->InputAt(1)->IsRegister());
      if (kArchVariant >= kMips64r6) {
        return latency++;
      } else {
        return latency + MovzLatency();
      }
    }
    case kMips64DivU: {
      int latency = DivuLatency(instr->InputAt(1)->IsRegister());
      if (kArchVariant >= kMips64r6) {
        return latency++;
      } else {
        return latency + MovzLatency();
      }
    }
    case kMips64Mod:
      return ModLatency();
    case kMips64ModU:
      return ModuLatency();
    case kMips64Dmul:
      return DmulLatency();
    case kMips64Ddiv: {
      int latency = DdivLatency();
      if (kArchVariant >= kMips64r6) {
        return latency++;
      } else {
        return latency + MovzLatency();
      }
    }
    case kMips64DdivU: {
      int latency = DdivuLatency();
      if (kArchVariant >= kMips64r6) {
        return latency++;
      } else {
        return latency + MovzLatency();
      }
    }
    case kMips64Dmod:
      return DmodLatency();
    case kMips64DmodU:
      return DmoduLatency();
    case kMips64Dlsa:
    case kMips64Lsa:
      return DlsaLatency();
    case kMips64And:
      return AndLatency(instr->InputAt(1)->IsRegister());
    case kMips64And32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = AndLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kMips64Or:
      return OrLatency(instr->InputAt(1)->IsRegister());
    case kMips64Or32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = OrLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kMips64Nor:
      return NorLatency(instr->InputAt(1)->IsRegister());
    case kMips64Nor32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = NorLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kMips64Xor:
      return XorLatency(instr->InputAt(1)->IsRegister());
    case kMips64Xor32: {
      bool is_operand_register = instr->InputAt(1)->IsRegister();
      int latency = XorLatency(is_operand_register);
      if (is_operand_register) {
        return latency + 2;
      } else {
        return latency + 1;
      }
    }
    case kMips64Clz:
    case kMips64Dclz:
      return DclzLatency();
    case kMips64Ctz:
      return CtzLatency();
    case kMips64Dctz:
      return DctzLatency();
    case kMips64Popcnt:
      return PopcntLatency();
    case kMips64Dpopcnt:
      return DpopcntLatency();
    case kMips64Shl:
      return 1;
    case kMips64Shr:
    case kMips64Sar:
      return 2;
    case kMips64Ext:
    case kMips64Ins:
    case kMips64Dext:
    case kMips64Dins:
    case kMips64Dshl:
    case kMips64Dshr:
    case kMips64Dsar:
    case kMips64Ror:
    case kMips64Dror:
      return 1;
    case kMips64Tst:
      return AndLatency(instr->InputAt(1)->IsRegister());
    case kMips64Mov:
      return 1;
    case kMips64CmpS:
      return MoveLatency() + CompareF32Latency();
    case kMips64AddS:
      return Latency::ADD_S;
    case kMips64SubS:
      return Latency::SUB_S;
    case kMips64MulS:
      return Latency::MUL_S;
    case kMips64DivS:
      return Latency::DIV_S;
    case kMips64AbsS:
      return Latency::ABS_S;
    case kMips64NegS:
      return NegdLatency();
    case kMips64SqrtS:
      return Latency::SQRT_S;
    case kMips64MaxS:
      return Latency::MAX_S;
    case kMips64MinS:
      return Latency::MIN_S;
    case kMips64CmpD:
      return MoveLatency() + CompareF64Latency();
    case kMips64AddD:
      return Latency::ADD_D;
    case kMips64SubD:
      return Latency::SUB_D;
    case kMips64MulD:
      return Latency::MUL_D;
    case kMips64DivD:
      return Latency::DIV_D;
    case kMips64ModD:
      return PrepareCallCFunctionLatency() + MovToFloatParametersLatency() +
             CallCFunctionLatency() + MovFromFloatResultLatency();
    case kMips64AbsD:
      return Latency::ABS_D;
    case kMips64NegD:
      return NegdLatency();
    case kMips64SqrtD:
      return Latency::SQRT_D;
    case kMips64MaxD:
      return Latency::MAX_D;
    case kMips64MinD:
      return Latency::MIN_D;
    case kMips64Float64RoundDown:
    case kMips64Float64RoundTruncate:
    case kMips64Float64RoundUp:
    case kMips64Float64RoundTiesEven:
      return Float64RoundLatency();
    case kMips64Float32RoundDown:
    case kMips64Float32RoundTruncate:
    case kMips64Float32RoundUp:
    case kMips64Float32RoundTiesEven:
      return Float32RoundLatency();
    case kMips64Float32Max:
      return Float32MaxLatency();
    case kMips64Float64Max:
      return Float64MaxLatency();
    case kMips64Float32Min:
      return Float32MinLatency();
    case kMips64Float64Min:
      return Float64MinLatency();
    case kMips64Float64SilenceNaN:
      return Latency::SUB_D;
    case kMips64CvtSD:
      return Latency::CVT_S_D;
    case kMips64CvtDS:
      return Latency::CVT_D_S;
    case kMips64CvtDW:
      return Latency::MTC1 + Latency::CVT_D_W;
    case kMips64CvtSW:
      return Latency::MTC1 + Latency::CVT_S_W;
    case kMips64CvtSUw:
      return 1 + Latency::DMTC1 + Latency::CVT_S_L;
    case kMips64CvtSL:
      return Latency::DMTC1 + Latency::CVT_S_L;
    case kMips64CvtDL:
      return Latency::DMTC1 + Latency::CVT_D_L;
    case kMips64CvtDUw:
      return 1 + Latency::DMTC1 + Latency::CVT_D_L;
    case kMips64CvtDUl:
      return 2 * Latency::BRANCH + 3 + 2 * Latency::DMTC1 +
             2 * Latency::CVT_D_L + Latency::ADD_D;
    case kMips64CvtSUl:
      return 2 * Latency::BRANCH + 3 + 2 * Latency::DMTC1 +
             2 * Latency::CVT_S_L + Latency::ADD_S;
    case kMips64FloorWD:
      return Latency::FLOOR_W_D + Latency::MFC1;
    case kMips64CeilWD:
      return Latency::CEIL_W_D + Latency::MFC1;
    case kMips64RoundWD:
      return Latency::ROUND_W_D + Latency::MFC1;
    case kMips64TruncWD:
      return Latency::TRUNC_W_D + Latency::MFC1;
    case kMips64FloorWS:
      return Latency::FLOOR_W_S + Latency::MFC1;
    case kMips64CeilWS:
      return Latency::CEIL_W_S + Latency::MFC1;
    case kMips64RoundWS:
      return Latency::ROUND_W_S + Latency::MFC1;
    case kMips64TruncWS:
      return Latency::TRUNC_W_S + Latency::MFC1 + 2 + MovnLatency();
    case kMips64TruncLS:
      return TruncLSLatency(instr->OutputCount() > 1);
    case kMips64TruncLD:
      return TruncLDLatency(instr->OutputCount() > 1);
    case kMips64TruncUwD:
      // Estimated max.
      return CompareF64Latency() + 2 * Latency::BRANCH +
             2 * Latency::TRUNC_W_D + Latency::SUB_D + OrLatency() +
             Latency::MTC1 + Latency::MFC1 + Latency::MTHC1 + 1;
    case kMips64TruncUwS:
      // Estimated max.
      return CompareF32Latency() + 2 * Latency::BRANCH +
             2 * Latency::TRUNC_W_S + Latency::SUB_S + OrLatency() +
             Latency::MTC1 + 2 * Latency::MFC1 + 2 + MovzLatency();
    case kMips64TruncUlS:
      return TruncUlSLatency();
    case kMips64TruncUlD:
      return TruncUlDLatency();
    case kMips64BitcastDL:
      return Latency::DMFC1;
    case kMips64BitcastLD:
      return Latency::DMTC1;
    case kMips64Float64ExtractLowWord32:
      return Latency::MFC1;
    case kMips64Float64InsertLowWord32:
      return Latency::MFHC1 + Latency::MTC1 + Latency::MTHC1;
    case kMips64Float64FromWord32Pair:
      return Latency::MTC1 + Latency::MTHC1;
    case kMips64Float64ExtractHighWord32:
      return Latency::MFHC1;
    case kMips64Float64InsertHighWord32:
      return Latency::MTHC1;
    case kMips64Seb:
    case kMips64Seh:
      return 1;
    case kMips64Lbu:
    case kMips64Lb:
    case kMips64Lhu:
    case kMips64Lh:
    case kMips64Lwu:
    case kMips64Lw:
    case kMips64Ld:
    case kMips64Sb:
    case kMips64Sh:
    case kMips64Sw:
    case kMips64Sd:
      return AlignedMemoryLatency();
    case kMips64Lwc1:
      return Lwc1Latency();
    case kMips64Ldc1:
      return Ldc1Latency();
    case kMips64Swc1:
      return Swc1Latency();
    case kMips64Sdc1:
      return Sdc1Latency();
    case kMips64Ulhu:
    case kMips64Ulh:
      return UlhuLatency();
    case kMips64Ulwu:
      return UlwuLatency();
    case kMips64Ulw:
      return UlwLatency();
    case kMips64Uld:
      return UldLatency();
    case kMips64Ulwc1:
      return Ulwc1Latency();
    case kMips64Uldc1:
      return Uldc1Latency();
    case kMips64Ush:
      return UshLatency();
    case kMips64Usw:
      return UswLatency();
    case kMips64Usd:
      return UsdLatency();
    case kMips64Uswc1:
      return Uswc1Latency();
    case kMips64Usdc1:
      return Usdc1Latency();
    case kMips64Push: {
      int latency = 0;
      if (instr->InputAt(0)->IsFPRegister()) {
        latency = Sdc1Latency() + DsubuLatency(false);
      } else {
        latency = PushLatency();
      }
      return latency;
    }
    case kMips64Peek: {
      int latency = 0;
      if (instr->OutputAt(0)->IsFPRegister()) {
        auto op = LocationOperand::cast(instr->OutputAt(0));
        switch (op->representation()) {
          case MachineRepresentation::kFloat64:
            latency = Ldc1Latency();
            break;
          case MachineRepresentation::kFloat32:
            latency = Latency::LWC1;
            break;
          default:
            UNREACHABLE();
        }
      } else {
        latency = AlignedMemoryLatency();
      }
      return latency;
    }
    case kMips64StackClaim:
      return DsubuLatency(false);
    case kMips64StoreToStackSlot: {
      int latency = 0;
      if (instr->InputAt(0)->IsFPRegister()) {
        if (instr->InputAt(0)->IsSimd128Register()) {
          latency = 1;  // Estimated value.
        } else {
          latency = Sdc1Latency();
        }
      } else {
        latency = AlignedMemoryLatency();
      }
      return latency;
    }
    case kMips64ByteSwap64:
      return ByteSwapSignedLatency();
    case kMips64ByteSwap32:
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
    case kMips64AssertEqual:
      return AssertLatency();
    default:
      return 1;
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
