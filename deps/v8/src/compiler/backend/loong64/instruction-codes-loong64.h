// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_LOONG64_INSTRUCTION_CODES_LOONG64_H_
#define V8_COMPILER_BACKEND_LOONG64_INSTRUCTION_CODES_LOONG64_H_

namespace v8 {
namespace internal {
namespace compiler {

// LOONG64-specific opcodes that specify which assembly sequence to emit.
// Most opcodes specify a single instruction.

// Opcodes that support a MemoryAccessMode.
#define TARGET_ARCH_OPCODE_WITH_MEMORY_ACCESS_MODE_LIST(V) \
  V(Loong64Ld_b)                                           \
  V(Loong64Ld_bu)                                          \
  V(Loong64St_b)                                           \
  V(Loong64Ld_h)                                           \
  V(Loong64Ld_hu)                                          \
  V(Loong64St_h)                                           \
  V(Loong64Ld_w)                                           \
  V(Loong64Ld_wu)                                          \
  V(Loong64St_w)                                           \
  V(Loong64Ld_d)                                           \
  V(Loong64St_d)                                           \
  V(Loong64LoadDecompressTaggedSigned)                     \
  V(Loong64LoadDecompressTagged)                           \
  V(Loong64LoadDecompressProtected)                        \
  V(Loong64StoreCompressTagged)                            \
  V(Loong64Fld_s)                                          \
  V(Loong64Fst_s)                                          \
  V(Loong64Fld_d)                                          \
  V(Loong64Fst_d)                                          \
  V(Loong64LoadLane)                                       \
  V(Loong64StoreLane)                                      \
  V(Loong64S128LoadSplat)                                  \
  V(Loong64S128Load8x8S)                                   \
  V(Loong64S128Load8x8U)                                   \
  V(Loong64S128Load16x4S)                                  \
  V(Loong64S128Load16x4U)                                  \
  V(Loong64S128Load32x2S)                                  \
  V(Loong64S128Load32x2U)                                  \
  V(Loong64Word64AtomicLoadUint32)                         \
  V(Loong64Word64AtomicLoadUint64)                         \
  V(Loong64Word64AtomicStoreWord64)

#define TARGET_ARCH_OPCODE_LIST(V)                   \
  TARGET_ARCH_OPCODE_WITH_MEMORY_ACCESS_MODE_LIST(V) \
  V(Loong64Add_d)                                    \
  V(Loong64Add_w)                                    \
  V(Loong64AddOvf_d)                                 \
  V(Loong64Sub_d)                                    \
  V(Loong64Sub_w)                                    \
  V(Loong64SubOvf_d)                                 \
  V(Loong64Mul_d)                                    \
  V(Loong64MulOvf_w)                                 \
  V(Loong64MulOvf_d)                                 \
  V(Loong64Mulh_d)                                   \
  V(Loong64Mulh_w)                                   \
  V(Loong64Mulh_du)                                  \
  V(Loong64Mulh_wu)                                  \
  V(Loong64Mul_w)                                    \
  V(Loong64Div_d)                                    \
  V(Loong64Div_w)                                    \
  V(Loong64Div_du)                                   \
  V(Loong64Div_wu)                                   \
  V(Loong64Mod_d)                                    \
  V(Loong64Mod_w)                                    \
  V(Loong64Mod_du)                                   \
  V(Loong64Mod_wu)                                   \
  V(Loong64And)                                      \
  V(Loong64And32)                                    \
  V(Loong64Or)                                       \
  V(Loong64Or32)                                     \
  V(Loong64Nor)                                      \
  V(Loong64Nor32)                                    \
  V(Loong64Xor)                                      \
  V(Loong64Xor32)                                    \
  V(Loong64Alsl_d)                                   \
  V(Loong64Alsl_w)                                   \
  V(Loong64Sll_d)                                    \
  V(Loong64Sll_w)                                    \
  V(Loong64Srl_d)                                    \
  V(Loong64Srl_w)                                    \
  V(Loong64Sra_d)                                    \
  V(Loong64Sra_w)                                    \
  V(Loong64Rotr_d)                                   \
  V(Loong64Rotr_w)                                   \
  V(Loong64Bstrpick_d)                               \
  V(Loong64Bstrpick_w)                               \
  V(Loong64Bstrins_d)                                \
  V(Loong64Bstrins_w)                                \
  V(Loong64ByteSwap64)                               \
  V(Loong64ByteSwap32)                               \
  V(Loong64Clz_d)                                    \
  V(Loong64Clz_w)                                    \
  V(Loong64Mov)                                      \
  V(Loong64Tst)                                      \
  V(Loong64Cmp32)                                    \
  V(Loong64Cmp64)                                    \
  V(Loong64Float32Cmp)                               \
  V(Loong64Float32Add)                               \
  V(Loong64Float32Sub)                               \
  V(Loong64Float32Mul)                               \
  V(Loong64Float32Div)                               \
  V(Loong64Float32Abs)                               \
  V(Loong64Float32Neg)                               \
  V(Loong64Float32Sqrt)                              \
  V(Loong64Float32Max)                               \
  V(Loong64Float32Min)                               \
  V(Loong64Float32ToFloat64)                         \
  V(Loong64Float32RoundDown)                         \
  V(Loong64Float32RoundUp)                           \
  V(Loong64Float32RoundTruncate)                     \
  V(Loong64Float32RoundTiesEven)                     \
  V(Loong64Float32ToInt32)                           \
  V(Loong64Float32ToInt64)                           \
  V(Loong64Float32ToUint32)                          \
  V(Loong64Float32ToUint64)                          \
  V(Loong64Float64Cmp)                               \
  V(Loong64Float64Add)                               \
  V(Loong64Float64Sub)                               \
  V(Loong64Float64Mul)                               \
  V(Loong64Float64Div)                               \
  V(Loong64Float64Mod)                               \
  V(Loong64Float64Abs)                               \
  V(Loong64Float64Neg)                               \
  V(Loong64Float64Sqrt)                              \
  V(Loong64Float64Max)                               \
  V(Loong64Float64Min)                               \
  V(Loong64Float64ToFloat32)                         \
  V(Loong64Float64RoundDown)                         \
  V(Loong64Float64RoundUp)                           \
  V(Loong64Float64RoundTruncate)                     \
  V(Loong64Float64RoundTiesEven)                     \
  V(Loong64Float64ToInt32)                           \
  V(Loong64Float64ToInt64)                           \
  V(Loong64Float64ToUint32)                          \
  V(Loong64Float64ToUint64)                          \
  V(Loong64Int32ToFloat32)                           \
  V(Loong64Int32ToFloat64)                           \
  V(Loong64Int64ToFloat32)                           \
  V(Loong64Int64ToFloat64)                           \
  V(Loong64Uint32ToFloat32)                          \
  V(Loong64Uint32ToFloat64)                          \
  V(Loong64Uint64ToFloat32)                          \
  V(Loong64Uint64ToFloat64)                          \
  V(Loong64Float64ExtractLowWord32)                  \
  V(Loong64Float64ExtractHighWord32)                 \
  V(Loong64Float64FromWord32Pair)                    \
  V(Loong64Float64InsertLowWord32)                   \
  V(Loong64Float64InsertHighWord32)                  \
  V(Loong64BitcastDL)                                \
  V(Loong64BitcastLD)                                \
  V(Loong64Float64SilenceNaN)                        \
  V(Loong64LoadDecodeSandboxedPointer)               \
  V(Loong64StoreEncodeSandboxedPointer)              \
  V(Loong64StoreIndirectPointer)                     \
  V(Loong64Push)                                     \
  V(Loong64Peek)                                     \
  V(Loong64Poke)                                     \
  V(Loong64StackClaim)                               \
  V(Loong64Ext_w_b)                                  \
  V(Loong64Ext_w_h)                                  \
  V(Loong64Dbar)                                     \
  V(Loong64S128Const)                                \
  V(Loong64S128Zero)                                 \
  V(Loong64S128AllOnes)                              \
  V(Loong64I32x4Splat)                               \
  V(Loong64I32x4ExtractLane)                         \
  V(Loong64I32x4ReplaceLane)                         \
  V(Loong64I32x4Add)                                 \
  V(Loong64I32x4Sub)                                 \
  V(Loong64F64x2Abs)                                 \
  V(Loong64F64x2Neg)                                 \
  V(Loong64F32x4Splat)                               \
  V(Loong64F32x4ExtractLane)                         \
  V(Loong64F32x4ReplaceLane)                         \
  V(Loong64F32x4SConvertI32x4)                       \
  V(Loong64F32x4UConvertI32x4)                       \
  V(Loong64I32x4Mul)                                 \
  V(Loong64I32x4MaxS)                                \
  V(Loong64I32x4MinS)                                \
  V(Loong64I32x4Eq)                                  \
  V(Loong64I32x4Ne)                                  \
  V(Loong64I32x4Shl)                                 \
  V(Loong64I32x4ShrS)                                \
  V(Loong64I32x4ShrU)                                \
  V(Loong64I32x4MaxU)                                \
  V(Loong64I32x4MinU)                                \
  V(Loong64F64x2Sqrt)                                \
  V(Loong64F64x2Add)                                 \
  V(Loong64F64x2Sub)                                 \
  V(Loong64F64x2Mul)                                 \
  V(Loong64F64x2Div)                                 \
  V(Loong64F64x2Min)                                 \
  V(Loong64F64x2Max)                                 \
  V(Loong64F64x2Eq)                                  \
  V(Loong64F64x2Ne)                                  \
  V(Loong64F64x2Lt)                                  \
  V(Loong64F64x2Le)                                  \
  V(Loong64F64x2Splat)                               \
  V(Loong64F64x2ExtractLane)                         \
  V(Loong64F64x2ReplaceLane)                         \
  V(Loong64F64x2Pmin)                                \
  V(Loong64F64x2Pmax)                                \
  V(Loong64F64x2Ceil)                                \
  V(Loong64F64x2Floor)                               \
  V(Loong64F64x2Trunc)                               \
  V(Loong64F64x2NearestInt)                          \
  V(Loong64F64x2ConvertLowI32x4S)                    \
  V(Loong64F64x2ConvertLowI32x4U)                    \
  V(Loong64F64x2PromoteLowF32x4)                     \
  V(Loong64F64x2RelaxedMin)                          \
  V(Loong64F64x2RelaxedMax)                          \
  V(Loong64I64x2Splat)                               \
  V(Loong64I64x2ExtractLane)                         \
  V(Loong64I64x2ReplaceLane)                         \
  V(Loong64I64x2Add)                                 \
  V(Loong64I64x2Sub)                                 \
  V(Loong64I64x2Mul)                                 \
  V(Loong64I64x2Neg)                                 \
  V(Loong64I64x2Shl)                                 \
  V(Loong64I64x2ShrS)                                \
  V(Loong64I64x2ShrU)                                \
  V(Loong64I64x2BitMask)                             \
  V(Loong64I64x2Eq)                                  \
  V(Loong64I64x2Ne)                                  \
  V(Loong64I64x2GtS)                                 \
  V(Loong64I64x2GeS)                                 \
  V(Loong64I64x2Abs)                                 \
  V(Loong64I64x2SConvertI32x4Low)                    \
  V(Loong64I64x2SConvertI32x4High)                   \
  V(Loong64I64x2UConvertI32x4Low)                    \
  V(Loong64I64x2UConvertI32x4High)                   \
  V(Loong64ExtMulLow)                                \
  V(Loong64ExtMulHigh)                               \
  V(Loong64ExtAddPairwise)                           \
  V(Loong64F32x4Abs)                                 \
  V(Loong64F32x4Neg)                                 \
  V(Loong64F32x4Sqrt)                                \
  V(Loong64F32x4Add)                                 \
  V(Loong64F32x4Sub)                                 \
  V(Loong64F32x4Mul)                                 \
  V(Loong64F32x4Div)                                 \
  V(Loong64F32x4Max)                                 \
  V(Loong64F32x4Min)                                 \
  V(Loong64F32x4Eq)                                  \
  V(Loong64F32x4Ne)                                  \
  V(Loong64F32x4Lt)                                  \
  V(Loong64F32x4Le)                                  \
  V(Loong64F32x4Pmin)                                \
  V(Loong64F32x4Pmax)                                \
  V(Loong64F32x4Ceil)                                \
  V(Loong64F32x4Floor)                               \
  V(Loong64F32x4Trunc)                               \
  V(Loong64F32x4NearestInt)                          \
  V(Loong64F32x4DemoteF64x2Zero)                     \
  V(Loong64F32x4RelaxedMin)                          \
  V(Loong64F32x4RelaxedMax)                          \
  V(Loong64I32x4SConvertF32x4)                       \
  V(Loong64I32x4UConvertF32x4)                       \
  V(Loong64I32x4Neg)                                 \
  V(Loong64I32x4GtS)                                 \
  V(Loong64I32x4GeS)                                 \
  V(Loong64I32x4GtU)                                 \
  V(Loong64I32x4GeU)                                 \
  V(Loong64I32x4Abs)                                 \
  V(Loong64I32x4BitMask)                             \
  V(Loong64I32x4DotI16x8S)                           \
  V(Loong64I32x4TruncSatF64x2SZero)                  \
  V(Loong64I32x4TruncSatF64x2UZero)                  \
  V(Loong64I32x4RelaxedTruncF32x4S)                  \
  V(Loong64I32x4RelaxedTruncF32x4U)                  \
  V(Loong64I32x4RelaxedTruncF64x2SZero)              \
  V(Loong64I32x4RelaxedTruncF64x2UZero)              \
  V(Loong64I16x8Splat)                               \
  V(Loong64I16x8ExtractLaneU)                        \
  V(Loong64I16x8ExtractLaneS)                        \
  V(Loong64I16x8ReplaceLane)                         \
  V(Loong64I16x8Neg)                                 \
  V(Loong64I16x8Shl)                                 \
  V(Loong64I16x8ShrS)                                \
  V(Loong64I16x8ShrU)                                \
  V(Loong64I16x8Add)                                 \
  V(Loong64I16x8AddSatS)                             \
  V(Loong64I16x8Sub)                                 \
  V(Loong64I16x8SubSatS)                             \
  V(Loong64I16x8Mul)                                 \
  V(Loong64I16x8MaxS)                                \
  V(Loong64I16x8MinS)                                \
  V(Loong64I16x8Eq)                                  \
  V(Loong64I16x8Ne)                                  \
  V(Loong64I16x8GtS)                                 \
  V(Loong64I16x8GeS)                                 \
  V(Loong64I16x8AddSatU)                             \
  V(Loong64I16x8SubSatU)                             \
  V(Loong64I16x8MaxU)                                \
  V(Loong64I16x8MinU)                                \
  V(Loong64I16x8GtU)                                 \
  V(Loong64I16x8GeU)                                 \
  V(Loong64I16x8RoundingAverageU)                    \
  V(Loong64I16x8Abs)                                 \
  V(Loong64I16x8BitMask)                             \
  V(Loong64I16x8Q15MulRSatS)                         \
  V(Loong64I16x8RelaxedQ15MulRS)                     \
  V(Loong64I8x16Splat)                               \
  V(Loong64I8x16ExtractLaneU)                        \
  V(Loong64I8x16ExtractLaneS)                        \
  V(Loong64I8x16ReplaceLane)                         \
  V(Loong64I8x16Neg)                                 \
  V(Loong64I8x16Shl)                                 \
  V(Loong64I8x16ShrS)                                \
  V(Loong64I8x16Add)                                 \
  V(Loong64I8x16AddSatS)                             \
  V(Loong64I8x16Sub)                                 \
  V(Loong64I8x16SubSatS)                             \
  V(Loong64I8x16MaxS)                                \
  V(Loong64I8x16MinS)                                \
  V(Loong64I8x16Eq)                                  \
  V(Loong64I8x16Ne)                                  \
  V(Loong64I8x16GtS)                                 \
  V(Loong64I8x16GeS)                                 \
  V(Loong64I8x16ShrU)                                \
  V(Loong64I8x16AddSatU)                             \
  V(Loong64I8x16SubSatU)                             \
  V(Loong64I8x16MaxU)                                \
  V(Loong64I8x16MinU)                                \
  V(Loong64I8x16GtU)                                 \
  V(Loong64I8x16GeU)                                 \
  V(Loong64I8x16RoundingAverageU)                    \
  V(Loong64I8x16Abs)                                 \
  V(Loong64I8x16Popcnt)                              \
  V(Loong64I8x16BitMask)                             \
  V(Loong64S128And)                                  \
  V(Loong64S128Or)                                   \
  V(Loong64S128Xor)                                  \
  V(Loong64S128Not)                                  \
  V(Loong64S128Select)                               \
  V(Loong64S128AndNot)                               \
  V(Loong64I64x2AllTrue)                             \
  V(Loong64I32x4AllTrue)                             \
  V(Loong64I16x8AllTrue)                             \
  V(Loong64I8x16AllTrue)                             \
  V(Loong64V128AnyTrue)                              \
  V(Loong64S32x4InterleaveRight)                     \
  V(Loong64S32x4InterleaveLeft)                      \
  V(Loong64S32x4PackEven)                            \
  V(Loong64S32x4PackOdd)                             \
  V(Loong64S32x4InterleaveEven)                      \
  V(Loong64S32x4InterleaveOdd)                       \
  V(Loong64S32x4Shuffle)                             \
  V(Loong64S16x8InterleaveRight)                     \
  V(Loong64S16x8InterleaveLeft)                      \
  V(Loong64S16x8PackEven)                            \
  V(Loong64S16x8PackOdd)                             \
  V(Loong64S16x8InterleaveEven)                      \
  V(Loong64S16x8InterleaveOdd)                       \
  V(Loong64S16x4Reverse)                             \
  V(Loong64S16x2Reverse)                             \
  V(Loong64S8x16InterleaveRight)                     \
  V(Loong64S8x16InterleaveLeft)                      \
  V(Loong64S8x16PackEven)                            \
  V(Loong64S8x16PackOdd)                             \
  V(Loong64S8x16InterleaveEven)                      \
  V(Loong64S8x16InterleaveOdd)                       \
  V(Loong64I8x16Shuffle)                             \
  V(Loong64I8x16Swizzle)                             \
  V(Loong64S8x16Concat)                              \
  V(Loong64S8x8Reverse)                              \
  V(Loong64S8x4Reverse)                              \
  V(Loong64S8x2Reverse)                              \
  V(Loong64S128Load32Zero)                           \
  V(Loong64S128Load64Zero)                           \
  V(Loong64I32x4SConvertI16x8Low)                    \
  V(Loong64I32x4SConvertI16x8High)                   \
  V(Loong64I32x4UConvertI16x8Low)                    \
  V(Loong64I32x4UConvertI16x8High)                   \
  V(Loong64I16x8SConvertI8x16Low)                    \
  V(Loong64I16x8SConvertI8x16High)                   \
  V(Loong64I16x8SConvertI32x4)                       \
  V(Loong64I16x8UConvertI32x4)                       \
  V(Loong64I16x8UConvertI8x16Low)                    \
  V(Loong64I16x8UConvertI8x16High)                   \
  V(Loong64I8x16SConvertI16x8)                       \
  V(Loong64I8x16UConvertI16x8)                       \
  V(Loong64AtomicLoadDecompressTaggedSigned)         \
  V(Loong64AtomicLoadDecompressTagged)               \
  V(Loong64AtomicStoreCompressTagged)                \
  V(Loong64Word64AtomicAddUint64)                    \
  V(Loong64Word64AtomicSubUint64)                    \
  V(Loong64Word64AtomicAndUint64)                    \
  V(Loong64Word64AtomicOrUint64)                     \
  V(Loong64Word64AtomicXorUint64)                    \
  V(Loong64Word64AtomicExchangeUint64)               \
  V(Loong64Word64AtomicCompareExchangeUint64)

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
#define TARGET_ADDRESSING_MODE_LIST(V) \
  V(MRI)  /* [%r0 + K] */              \
  V(MRR)  /* [%r0 + %r1] */            \
  V(Root) /* [%rr + K] */

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_LOONG64_INSTRUCTION_CODES_LOONG64_H_
