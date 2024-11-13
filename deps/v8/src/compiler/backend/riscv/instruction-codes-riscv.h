// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_RISCV_INSTRUCTION_CODES_RISCV_H_
#define V8_COMPILER_BACKEND_RISCV_INSTRUCTION_CODES_RISCV_H_

namespace v8 {
namespace internal {
namespace compiler {

// RISC-V-specific opcodes that specify which assembly sequence to emit.
// Most opcodes specify a single instruction.
#if V8_TARGET_ARCH_RISCV64
// Opcodes that support a MemoryAccessMode.
#define TARGET_ARCH_OPCODE_WITH_MEMORY_ACCESS_MODE_LIST(V) \
  V(RiscvLd)                                               \
  V(RiscvSd)                                               \
  V(RiscvLwu)                                              \
  V(RiscvWord64AtomicLoadUint64)                           \
  V(RiscvWord64AtomicStoreWord64)                          \
  V(RiscvLb)                                               \
  V(RiscvLbu)                                              \
  V(RiscvSb)                                               \
  V(RiscvLh)                                               \
  V(RiscvLhu)                                              \
  V(RiscvSh)                                               \
  V(RiscvLw)                                               \
  V(RiscvSw)                                               \
  V(RiscvLoadDouble)                                       \
  V(RiscvStoreDouble)                                      \
  V(RiscvStoreFloat)                                       \
  V(RiscvLoadFloat)                                        \
  V(RiscvStoreCompressTagged)                              \
  V(RiscvLoadDecompressTaggedSigned)                       \
  V(RiscvLoadDecompressTagged)                             \
  V(RiscvS128LoadSplat)                                    \
  V(RiscvS128Load64ExtendS)                                \
  V(RiscvS128Load64ExtendU)                                \
  V(RiscvS128Load64Zero)                                   \
  V(RiscvS128Load32Zero)                                   \
  V(RiscvS128LoadLane)                                     \
  V(RiscvS128StoreLane)                                    \
  V(RiscvRvvLd)                                            \
  V(RiscvRvvSt)

#define TARGET_ARCH_OPCODE_LIST_SPECAIL(V)           \
  TARGET_ARCH_OPCODE_WITH_MEMORY_ACCESS_MODE_LIST(V) \
  V(RiscvAdd64)                                      \
  V(RiscvAddOvf64)                                   \
  V(RiscvSub64)                                      \
  V(RiscvSubOvf64)                                   \
  V(RiscvMulHigh64)                                  \
  V(RiscvMulHighU64)                                 \
  V(RiscvMul64)                                      \
  V(RiscvMulOvf64)                                   \
  V(RiscvDiv64)                                      \
  V(RiscvDivU64)                                     \
  V(RiscvMod64)                                      \
  V(RiscvModU64)                                     \
  V(RiscvZeroExtendWord)                             \
  V(RiscvSignExtendWord)                             \
  V(RiscvClz64)                                      \
  V(RiscvCtz64)                                      \
  V(RiscvPopcnt64)                                   \
  V(RiscvShl64)                                      \
  V(RiscvShr64)                                      \
  V(RiscvSar64)                                      \
  V(RiscvRor64)                                      \
  V(RiscvFloat64RoundDown)                           \
  V(RiscvFloat64RoundTruncate)                       \
  V(RiscvFloat64RoundUp)                             \
  V(RiscvFloat64RoundTiesEven)                       \
  V(RiscvTruncLS)                                    \
  V(RiscvTruncLD)                                    \
  V(RiscvTruncUlS)                                   \
  V(RiscvTruncUlD)                                   \
  V(RiscvCvtSL)                                      \
  V(RiscvCvtSUl)                                     \
  V(RiscvCvtDL)                                      \
  V(RiscvCvtDUl)                                     \
  V(RiscvUsd)                                        \
  V(RiscvUlwu)                                       \
  V(RiscvBitcastDL)                                  \
  V(RiscvBitcastLD)                                  \
  V(RiscvByteSwap64)                                 \
  V(RiscvWord64AtomicAddUint64)                      \
  V(RiscvWord64AtomicSubUint64)                      \
  V(RiscvWord64AtomicAndUint64)                      \
  V(RiscvWord64AtomicOrUint64)                       \
  V(RiscvWord64AtomicXorUint64)                      \
  V(RiscvWord64AtomicExchangeUint64)                 \
  V(RiscvLoadDecodeSandboxedPointer)                 \
  V(RiscvStoreEncodeSandboxedPointer)                \
  V(RiscvStoreIndirectPointer)                       \
  V(RiscvAtomicLoadDecompressTaggedSigned)           \
  V(RiscvAtomicLoadDecompressTagged)                 \
  V(RiscvLoadDecompressProtected)                    \
  V(RiscvAtomicStoreCompressTagged)                  \
  V(RiscvWord64AtomicCompareExchangeUint64)          \
  V(RiscvCmp32)                                      \
  V(RiscvCmpZero32)                                  \
  V(RiscvTst64)
#elif V8_TARGET_ARCH_RISCV32
#define TARGET_ARCH_OPCODE_LIST_SPECAIL(V) \
  V(RiscvAddOvf)                           \
  V(RiscvSubOvf)                           \
  V(RiscvAddPair)                          \
  V(RiscvSubPair)                          \
  V(RiscvMulPair)                          \
  V(RiscvAndPair)                          \
  V(RiscvOrPair)                           \
  V(RiscvXorPair)                          \
  V(RiscvShlPair)                          \
  V(RiscvShrPair)                          \
  V(RiscvSarPair)                          \
  V(RiscvWord32AtomicPairLoad)             \
  V(RiscvWord32AtomicPairStore)            \
  V(RiscvWord32AtomicPairAdd)              \
  V(RiscvWord32AtomicPairSub)              \
  V(RiscvWord32AtomicPairAnd)              \
  V(RiscvWord32AtomicPairOr)               \
  V(RiscvWord32AtomicPairXor)              \
  V(RiscvWord32AtomicPairExchange)         \
  V(RiscvWord32AtomicPairCompareExchange)  \
  V(RiscvLb)                               \
  V(RiscvLbu)                              \
  V(RiscvSb)                               \
  V(RiscvLh)                               \
  V(RiscvLhu)                              \
  V(RiscvSh)                               \
  V(RiscvLw)                               \
  V(RiscvSw)                               \
  V(RiscvLoadDouble)                       \
  V(RiscvStoreDouble)                      \
  V(RiscvStoreFloat)                       \
  V(RiscvLoadFloat)                        \
  V(RiscvS128LoadSplat)                    \
  V(RiscvS128Load64ExtendS)                \
  V(RiscvS128Load64ExtendU)                \
  V(RiscvS128Load64Zero)                   \
  V(RiscvS128Load32Zero)                   \
  V(RiscvS128LoadLane)                     \
  V(RiscvS128StoreLane)                    \
  V(RiscvRvvLd)                            \
  V(RiscvRvvSt)
#endif

#define TARGET_ARCH_OPCODE_LIST_COMMON(V) \
  V(RiscvAdd32)                           \
  V(RiscvSub32)                           \
  V(RiscvMul32)                           \
  V(RiscvMulOvf32)                        \
  V(RiscvMulHigh32)                       \
  V(RiscvMulHighU32)                      \
  V(RiscvDiv32)                           \
  V(RiscvDivU32)                          \
  V(RiscvMod32)                           \
  V(RiscvModU32)                          \
  V(RiscvAnd)                             \
  V(RiscvAnd32)                           \
  V(RiscvOr)                              \
  V(RiscvOr32)                            \
  V(RiscvXor)                             \
  V(RiscvXor32)                           \
  V(RiscvClz32)                           \
  V(RiscvShl32)                           \
  V(RiscvShr32)                           \
  V(RiscvSar32)                           \
  V(RiscvCtz32)                           \
  V(RiscvPopcnt32)                        \
  V(RiscvRor32)                           \
  V(RiscvMov)                             \
  V(RiscvTst32)                           \
  V(RiscvCmp)                             \
  V(RiscvCmpZero)                         \
  V(RiscvCmpS)                            \
  V(RiscvAddS)                            \
  V(RiscvSubS)                            \
  V(RiscvMulS)                            \
  V(RiscvDivS)                            \
  V(RiscvModS)                            \
  V(RiscvAbsS)                            \
  V(RiscvNegS)                            \
  V(RiscvSqrtS)                           \
  V(RiscvMaxS)                            \
  V(RiscvMinS)                            \
  V(RiscvCmpD)                            \
  V(RiscvAddD)                            \
  V(RiscvSubD)                            \
  V(RiscvMulD)                            \
  V(RiscvDivD)                            \
  V(RiscvModD)                            \
  V(RiscvAbsD)                            \
  V(RiscvNegD)                            \
  V(RiscvSqrtD)                           \
  V(RiscvMaxD)                            \
  V(RiscvMinD)                            \
  V(RiscvFloat32RoundDown)                \
  V(RiscvFloat32RoundTruncate)            \
  V(RiscvFloat32RoundUp)                  \
  V(RiscvFloat32RoundTiesEven)            \
  V(RiscvCvtSD)                           \
  V(RiscvCvtDS)                           \
  V(RiscvTruncWD)                         \
  V(RiscvRoundWD)                         \
  V(RiscvFloorWD)                         \
  V(RiscvCeilWD)                          \
  V(RiscvTruncWS)                         \
  V(RiscvRoundWS)                         \
  V(RiscvFloorWS)                         \
  V(RiscvCeilWS)                          \
  V(RiscvTruncUwD)                        \
  V(RiscvTruncUwS)                        \
  V(RiscvCvtDW)                           \
  V(RiscvCvtSW)                           \
  V(RiscvCvtSUw)                          \
  V(RiscvCvtDUw)                          \
  V(RiscvUlh)                             \
  V(RiscvUlhu)                            \
  V(RiscvUsh)                             \
  V(RiscvUld)                             \
  V(RiscvUlw)                             \
  V(RiscvUsw)                             \
  V(RiscvUStoreFloat)                     \
  V(RiscvULoadFloat)                      \
  V(RiscvULoadDouble)                     \
  V(RiscvUStoreDouble)                    \
  V(RiscvBitcastInt32ToFloat32)           \
  V(RiscvBitcastFloat32ToInt32)           \
  V(RiscvFloat64ExtractLowWord32)         \
  V(RiscvFloat64ExtractHighWord32)        \
  V(RiscvFloat64InsertLowWord32)          \
  V(RiscvFloat64InsertHighWord32)         \
  V(RiscvFloat32Max)                      \
  V(RiscvFloat64Max)                      \
  V(RiscvFloat32Min)                      \
  V(RiscvFloat64Min)                      \
  V(RiscvFloat64SilenceNaN)               \
  V(RiscvPush)                            \
  V(RiscvPeek)                            \
  V(RiscvByteSwap32)                      \
  V(RiscvStoreToStackSlot)                \
  V(RiscvStackClaim)                      \
  V(RiscvSignExtendByte)                  \
  V(RiscvSignExtendShort)                 \
  V(RiscvSync)                            \
  V(RiscvAssertEqual)                     \
  V(RiscvS128Const)                       \
  V(RiscvS128Zero)                        \
  V(RiscvS128AllOnes)                     \
  V(RiscvI32x4ExtractLane)                \
  V(RiscvI32x4ReplaceLane)                \
  V(RiscvF64x2Abs)                        \
  V(RiscvF32x4ExtractLane)                \
  V(RiscvF32x4ReplaceLane)                \
  V(RiscvF32x4SConvertI32x4)              \
  V(RiscvF32x4UConvertI32x4)              \
  V(RiscvI64x2SConvertI32x4Low)           \
  V(RiscvI64x2SConvertI32x4High)          \
  V(RiscvI64x2UConvertI32x4Low)           \
  V(RiscvI64x2UConvertI32x4High)          \
  V(RiscvI32x4Shl)                        \
  V(RiscvI32x4ShrS)                       \
  V(RiscvI32x4ShrU)                       \
  V(RiscvF64x2Sqrt)                       \
  V(RiscvF64x2ConvertLowI32x4S)           \
  V(RiscvF64x2ConvertLowI32x4U)           \
  V(RiscvF64x2PromoteLowF32x4)            \
  V(RiscvF64x2ExtractLane)                \
  V(RiscvF64x2ReplaceLane)                \
  V(RiscvF64x2Pmin)                       \
  V(RiscvF64x2Pmax)                       \
  V(RiscvF64x2Ceil)                       \
  V(RiscvF64x2Floor)                      \
  V(RiscvF64x2Trunc)                      \
  V(RiscvF64x2NearestInt)                 \
  V(RiscvI64x2SplatI32Pair)               \
  V(RiscvI64x2ExtractLane)                \
  V(RiscvI64x2ReplaceLane)                \
  V(RiscvI64x2ReplaceLaneI32Pair)         \
  V(RiscvI64x2Shl)                        \
  V(RiscvI64x2ShrS)                       \
  V(RiscvI64x2ShrU)                       \
  V(RiscvF32x4Abs)                        \
  V(RiscvF32x4Sqrt)                       \
  V(RiscvF32x4Qfma)                       \
  V(RiscvF32x4Qfms)                       \
  V(RiscvF64x2Qfma)                       \
  V(RiscvF64x2Qfms)                       \
  V(RiscvF32x4Pmin)                       \
  V(RiscvF32x4Pmax)                       \
  V(RiscvF32x4DemoteF64x2Zero)            \
  V(RiscvF32x4Ceil)                       \
  V(RiscvF32x4Floor)                      \
  V(RiscvF32x4Trunc)                      \
  V(RiscvF32x4NearestInt)                 \
  V(RiscvI32x4SConvertF32x4)              \
  V(RiscvI32x4UConvertF32x4)              \
  V(RiscvI32x4TruncSatF64x2SZero)         \
  V(RiscvI32x4TruncSatF64x2UZero)         \
  V(RiscvI16x8ExtractLaneU)               \
  V(RiscvI16x8ExtractLaneS)               \
  V(RiscvI16x8ReplaceLane)                \
  V(RiscvI16x8Shl)                        \
  V(RiscvI16x8ShrS)                       \
  V(RiscvI16x8ShrU)                       \
  V(RiscvI8x16ExtractLaneU)               \
  V(RiscvI8x16ExtractLaneS)               \
  V(RiscvI8x16ReplaceLane)                \
  V(RiscvI8x16Shl)                        \
  V(RiscvI8x16ShrS)                       \
  V(RiscvI8x16ShrU)                       \
  V(RiscvI8x16RoundingAverageU)           \
  V(RiscvI8x16Popcnt)                     \
  V(RiscvVnot)                            \
  V(RiscvS128Select)                      \
  V(RiscvV128AnyTrue)                     \
  V(RiscvI8x16Shuffle)                    \
  V(RiscvVmv)                             \
  V(RiscvVandVv)                          \
  V(RiscvVnotVv)                          \
  V(RiscvVorVv)                           \
  V(RiscvVxorVv)                          \
  V(RiscvVwmul)                           \
  V(RiscvVwmulu)                          \
  V(RiscvVmvSx)                           \
  V(RiscvVmvXs)                           \
  V(RiscvVcompress)                       \
  V(RiscvVaddVv)                          \
  V(RiscvVsubVv)                          \
  V(RiscvVwaddVv)                         \
  V(RiscvVwadduVv)                        \
  V(RiscvVwadduWx)                        \
  V(RiscvVrgather)                        \
  V(RiscvVslidedown)                      \
  V(RiscvVAbs)                            \
  V(RiscvVsll)                            \
  V(RiscvVfmvVf)                          \
  V(RiscvVnegVv)                          \
  V(RiscvVfnegVv)                         \
  V(RiscvVmaxuVv)                         \
  V(RiscvVmax)                            \
  V(RiscvVminuVv)                         \
  V(RiscvVminsVv)                         \
  V(RiscvVmulVv)                          \
  V(RiscvVdivu)                           \
  V(RiscvVmslt)                           \
  V(RiscvVgtsVv)                          \
  V(RiscvVgesVv)                          \
  V(RiscvVgeuVv)                          \
  V(RiscvVgtuVv)                          \
  V(RiscvVeqVv)                           \
  V(RiscvVneVv)                           \
  V(RiscvVaddSatSVv)                      \
  V(RiscvVaddSatUVv)                      \
  V(RiscvVsubSatSVv)                      \
  V(RiscvVsubSatUVv)                      \
  V(RiscvVmfeqVv)                         \
  V(RiscvVmfneVv)                         \
  V(RiscvVmfleVv)                         \
  V(RiscvVmfltVv)                         \
  V(RiscvVfaddVv)                         \
  V(RiscvVfsubVv)                         \
  V(RiscvVfmulVv)                         \
  V(RiscvVfdivVv)                         \
  V(RiscvVfminVv)                         \
  V(RiscvVfmaxVv)                         \
  V(RiscvVmergeVx)                        \
  V(RiscvVsmulVv)                         \
  V(RiscvVnclipu)                         \
  V(RiscvVnclip)                          \
  V(RiscvVredminuVs)                      \
  V(RiscvVAllTrue)                        \
  V(RiscvVzextVf2)                        \
  V(RiscvVsextVf2)

#define TARGET_ARCH_OPCODE_LIST_ZBB(V) \
  V(RiscvAndn)                         \
  V(RiscvOrn)                          \
  V(RiscvXnor)                         \
  V(RiscvClz)                          \
  V(RiscvCtz)                          \
  V(RiscvCpop)                         \
  V(RiscvMax)                          \
  V(RiscvMaxu)                         \
  V(RiscvMin)                          \
  V(RiscvMinu)                         \
  V(RiscvSextb)                        \
  V(RiscvSexth)                        \
  V(RiscvZexth)                        \
  V(RiscvRev8)

#ifdef V8_TARGET_ARCH_RISCV64
#define TARGET_ARCH_OPCODE_LIST_ZBB_32(V) \
  V(RiscvClzw)                            \
  V(RiscvCtzw)                            \
  V(RiscvCpopw)
#else
#define TARGET_ARCH_OPCODE_LIST_ZBB_32(V)
#endif

#define TARGET_ARCH_OPCODE_LIST_ZBA(V) \
  V(RiscvSh1add)                       \
  V(RiscvSh2add)                       \
  V(RiscvSh3add)

#ifdef V8_TARGET_ARCH_RISCV64
#define TARGET_ARCH_OPCODE_LIST_ZBA_32(V) \
  V(RiscvAdduw)                           \
  V(RiscvSh1adduw)                        \
  V(RiscvSh2adduw)                        \
  V(RiscvSh3adduw)                        \
  V(RiscvSlliuw)
#else
#define TARGET_ARCH_OPCODE_LIST_ZBA_32(V)
#endif

#define TARGET_ARCH_OPCODE_LIST_ZBS(V) \
  V(RiscvBclr)                         \
  V(RiscvBclri)                        \
  V(RiscvBext)                         \
  V(RiscvBexti)                        \
  V(RiscvBinv)                         \
  V(RiscvBinvi)                        \
  V(RiscvBset)                         \
  V(RiscvBseti)

#define TARGET_ARCH_OPCODE_LIST(V)   \
  TARGET_ARCH_OPCODE_LIST_COMMON(V)  \
  TARGET_ARCH_OPCODE_LIST_SPECAIL(V) \
  TARGET_ARCH_OPCODE_LIST_ZBB(V)     \
  TARGET_ARCH_OPCODE_LIST_ZBS(V)     \
  TARGET_ARCH_OPCODE_LIST_ZBA(V)     \
  TARGET_ARCH_OPCODE_LIST_ZBA_32(V)  \
  TARGET_ARCH_OPCODE_LIST_ZBB_32(V)

// Addressing modes represent the "shape" of inputs to an instruction.
// Many instructions support multiple addressing modes. Addressing modes
// are encoded into the InstructionCode of the instruction and tell the
// code generator after register allocation which assembler method to call.
//
// We use the following local notation for addressing modes:
//
// R = register
// O = register or stack slot
// D = double register
// I = immediate (handle, external, int32)
// MRI = [register + immediate]
// MRR = [register + register]
// Root = [kRootregister + immediate]
// TODO(plind): Add the new r6 address modes.
#define TARGET_ADDRESSING_MODE_LIST(V) \
  V(MRI)  /* [%r0 + K] */              \
  V(MRR)  /* [%r0 + %r1] */            \
  V(Root) /* [root + k] */

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_RISCV_INSTRUCTION_CODES_RISCV_H_
