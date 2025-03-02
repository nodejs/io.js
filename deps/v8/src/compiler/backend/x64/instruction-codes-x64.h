// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_X64_INSTRUCTION_CODES_X64_H_
#define V8_COMPILER_BACKEND_X64_INSTRUCTION_CODES_X64_H_

namespace v8 {
namespace internal {
namespace compiler {

// X64-specific opcodes that specify which assembly sequence to emit.
// Most opcodes specify a single instruction.

// Opcodes that support a MemoryAccessMode.
#define TARGET_ARCH_OPCODE_WITH_MEMORY_ACCESS_MODE_LIST(V) \
  V(X64F64x2PromoteLowF32x4)                               \
  V(X64Movb)                                               \
  V(X64Movdqu)                                             \
  V(X64Movl)                                               \
  V(X64Movq)                                               \
  V(X64Movsd)                                              \
  V(X64Movss)                                              \
  V(X64Movsh)                                              \
  V(X64Movsxbl)                                            \
  V(X64Movsxbq)                                            \
  V(X64Movsxlq)                                            \
  V(X64Movsxwl)                                            \
  V(X64Movsxwq)                                            \
  V(X64Movw)                                               \
  V(X64Movzxbl)                                            \
  V(X64Movzxbq)                                            \
  V(X64Movzxwl)                                            \
  V(X64Movzxwq)                                            \
  V(X64Pextrb)                                             \
  V(X64Pextrw)                                             \
  V(X64Pinsrb)                                             \
  V(X64Pinsrd)                                             \
  V(X64Pinsrq)                                             \
  V(X64Pinsrw)                                             \
  V(X64S128Load16Splat)                                    \
  V(X64S128Load16x4S)                                      \
  V(X64S128Load16x4U)                                      \
  V(X64S128Load32Splat)                                    \
  V(X64S128Load32x2S)                                      \
  V(X64S128Load32x2U)                                      \
  V(X64S128Load64Splat)                                    \
  V(X64S128Load8Splat)                                     \
  V(X64S128Load8x8S)                                       \
  V(X64S128Load8x8U)                                       \
  V(X64S128Store32Lane)                                    \
  V(X64S128Store64Lane)                                    \
  V(X64Word64AtomicStoreWord64)                            \
  V(X64Word64AtomicAddUint64)                              \
  V(X64Word64AtomicSubUint64)                              \
  V(X64Word64AtomicAndUint64)                              \
  V(X64Word64AtomicOrUint64)                               \
  V(X64Word64AtomicXorUint64)                              \
  V(X64Word64AtomicExchangeUint64)                         \
  V(X64Word64AtomicCompareExchangeUint64)                  \
  V(X64Movdqu256)                                          \
  V(X64MovqDecompressTaggedSigned)                         \
  V(X64MovqDecompressTagged)                               \
  V(X64MovqCompressTagged)                                 \
  V(X64MovqDecompressProtected)                            \
  V(X64S256Load8Splat)                                     \
  V(X64S256Load16Splat)                                    \
  V(X64S256Load32Splat)                                    \
  V(X64S256Load64Splat)                                    \
  V(X64S256Load8x16S)                                      \
  V(X64S256Load8x16U)                                      \
  V(X64S256Load8x8U)                                       \
  V(X64S256Load16x8S)                                      \
  V(X64S256Load16x8U)                                      \
  V(X64S256Load32x4S)                                      \
  V(X64S256Load32x4U)                                      \
  V(SSEFloat32Add)                                         \
  V(SSEFloat32Sub)                                         \
  V(SSEFloat32Mul)                                         \
  V(SSEFloat32Div)                                         \
  V(SSEFloat64Add)                                         \
  V(SSEFloat64Sub)                                         \
  V(SSEFloat64Mul)                                         \
  V(SSEFloat64Div)                                         \
  V(AVXFloat32Add)                                         \
  V(AVXFloat32Sub)                                         \
  V(AVXFloat32Mul)                                         \
  V(AVXFloat32Div)                                         \
  V(AVXFloat64Add)                                         \
  V(AVXFloat64Sub)                                         \
  V(AVXFloat64Mul)                                         \
  V(AVXFloat64Div)

#define TARGET_ARCH_OPCODE_LIST(V)                   \
  TARGET_ARCH_OPCODE_WITH_MEMORY_ACCESS_MODE_LIST(V) \
  V(X64Add)                                          \
  V(X64Add32)                                        \
  V(X64And)                                          \
  V(X64And32)                                        \
  V(X64Cmp)                                          \
  V(X64Cmp32)                                        \
  V(X64Cmp16)                                        \
  V(X64Cmp8)                                         \
  V(X64Test)                                         \
  V(X64Test32)                                       \
  V(X64Test16)                                       \
  V(X64Test8)                                        \
  V(X64Or)                                           \
  V(X64Or32)                                         \
  V(X64Xor)                                          \
  V(X64Xor32)                                        \
  V(X64Sub)                                          \
  V(X64Sub32)                                        \
  V(X64Imul)                                         \
  V(X64Imul32)                                       \
  V(X64ImulHigh32)                                   \
  V(X64ImulHigh64)                                   \
  V(X64UmulHigh32)                                   \
  V(X64UmulHigh64)                                   \
  V(X64Idiv)                                         \
  V(X64Idiv32)                                       \
  V(X64Udiv)                                         \
  V(X64Udiv32)                                       \
  V(X64Not)                                          \
  V(X64Not32)                                        \
  V(X64Neg)                                          \
  V(X64Neg32)                                        \
  V(X64Shl)                                          \
  V(X64Shl32)                                        \
  V(X64Shr)                                          \
  V(X64Shr32)                                        \
  V(X64Sar)                                          \
  V(X64Sar32)                                        \
  V(X64Rol)                                          \
  V(X64Rol32)                                        \
  V(X64Ror)                                          \
  V(X64Ror32)                                        \
  V(X64Lzcnt)                                        \
  V(X64Lzcnt32)                                      \
  V(X64Tzcnt)                                        \
  V(X64Tzcnt32)                                      \
  V(X64Popcnt)                                       \
  V(X64Popcnt32)                                     \
  V(X64Bswap)                                        \
  V(X64Bswap32)                                      \
  V(X64MFence)                                       \
  V(X64LFence)                                       \
  V(SSEFloat32Cmp)                                   \
  V(SSEFloat32Sqrt)                                  \
  V(SSEFloat32ToFloat64)                             \
  V(SSEFloat32ToInt32)                               \
  V(SSEFloat32ToUint32)                              \
  V(SSEFloat32Round)                                 \
  V(SSEFloat64Cmp)                                   \
  V(SSEFloat64Mod)                                   \
  V(SSEFloat64Sqrt)                                  \
  V(SSEFloat64Round)                                 \
  V(SSEFloat32Max)                                   \
  V(SSEFloat64Max)                                   \
  V(SSEFloat32Min)                                   \
  V(SSEFloat64Min)                                   \
  V(SSEFloat64ToFloat32)                             \
  V(SSEFloat64ToFloat16RawBits)                      \
  V(SSEFloat16RawBitsToFloat64)                      \
  V(SSEFloat64ToInt32)                               \
  V(SSEFloat64ToUint32)                              \
  V(SSEFloat32ToInt64)                               \
  V(SSEFloat64ToInt64)                               \
  V(SSEFloat32ToUint64)                              \
  V(SSEFloat64ToUint64)                              \
  V(SSEInt32ToFloat64)                               \
  V(SSEInt32ToFloat32)                               \
  V(SSEInt64ToFloat32)                               \
  V(SSEInt64ToFloat64)                               \
  V(SSEUint64ToFloat32)                              \
  V(SSEUint64ToFloat64)                              \
  V(SSEUint32ToFloat64)                              \
  V(SSEUint32ToFloat32)                              \
  V(SSEFloat64ExtractLowWord32)                      \
  V(SSEFloat64ExtractHighWord32)                     \
  V(SSEFloat64InsertLowWord32)                       \
  V(SSEFloat64InsertHighWord32)                      \
  V(SSEFloat64LoadLowWord32)                         \
  V(SSEFloat64SilenceNaN)                            \
  V(AVXFloat32Cmp)                                   \
  V(AVXFloat64Cmp)                                   \
  V(X64Float64Abs)                                   \
  V(X64Float64Neg)                                   \
  V(X64Float32Abs)                                   \
  V(X64Float32Neg)                                   \
  V(X64MovqStoreIndirectPointer)                     \
  V(X64MovqEncodeSandboxedPointer)                   \
  V(X64MovqDecodeSandboxedPointer)                   \
  V(X64BitcastFI)                                    \
  V(X64BitcastDL)                                    \
  V(X64BitcastIF)                                    \
  V(X64BitcastLD)                                    \
  V(X64Lea32)                                        \
  V(X64Lea)                                          \
  V(X64Dec32)                                        \
  V(X64Inc32)                                        \
  V(X64Push)                                         \
  V(X64Poke)                                         \
  V(X64Peek)                                         \
  V(X64Cvttps2dq)                                    \
  V(X64Cvttpd2dq)                                    \
  V(X64I32x4TruncF64x2UZero)                         \
  V(X64I32x4TruncF32x4U)                             \
  V(X64I32x8TruncF32x8U)                             \
  V(X64FSplat)                                       \
  V(X64FExtractLane)                                 \
  V(X64FReplaceLane)                                 \
  V(X64FAbs)                                         \
  V(X64FNeg)                                         \
  V(X64FSqrt)                                        \
  V(X64FAdd)                                         \
  V(X64FSub)                                         \
  V(X64FMul)                                         \
  V(X64FDiv)                                         \
  V(X64FMin)                                         \
  V(X64FMax)                                         \
  V(X64FEq)                                          \
  V(X64FNe)                                          \
  V(X64FLt)                                          \
  V(X64FLe)                                          \
  V(X64F64x2Qfma)                                    \
  V(X64F64x2Qfms)                                    \
  V(X64Minpd)                                        \
  V(X64Maxpd)                                        \
  V(X64F64x2Round)                                   \
  V(X64F64x2ConvertLowI32x4S)                        \
  V(X64F64x4ConvertI32x4S)                           \
  V(X64F64x2ConvertLowI32x4U)                        \
  V(X64F32x4SConvertI32x4)                           \
  V(X64F32x8SConvertI32x8)                           \
  V(X64F32x4UConvertI32x4)                           \
  V(X64F32x8UConvertI32x8)                           \
  V(X64F32x4Qfma)                                    \
  V(X64F32x4Qfms)                                    \
  V(X64Minps)                                        \
  V(X64Maxps)                                        \
  V(X64F32x4Round)                                   \
  V(X64F32x4DemoteF64x2Zero)                         \
  V(X64F32x4DemoteF64x4)                             \
  V(X64F16x8Round)                                   \
  V(X64I16x8SConvertF16x8)                           \
  V(X64I16x8UConvertF16x8)                           \
  V(X64F16x8SConvertI16x8)                           \
  V(X64F16x8UConvertI16x8)                           \
  V(X64F16x8DemoteF32x4Zero)                         \
  V(X64F16x8DemoteF64x2Zero)                         \
  V(X64F32x4PromoteLowF16x8)                         \
  V(X64F16x8Qfma)                                    \
  V(X64F16x8Qfms)                                    \
  V(X64Minph)                                        \
  V(X64Maxph)                                        \
  V(X64ISplat)                                       \
  V(X64IExtractLane)                                 \
  V(X64IAbs)                                         \
  V(X64INeg)                                         \
  V(X64IBitMask)                                     \
  V(X64IShl)                                         \
  V(X64IShrS)                                        \
  V(X64IAdd)                                         \
  V(X64ISub)                                         \
  V(X64IMul)                                         \
  V(X64IEq)                                          \
  V(X64IGtS)                                         \
  V(X64IGeS)                                         \
  V(X64INe)                                          \
  V(X64IShrU)                                        \
  V(X64I64x2ExtMulLowI32x4S)                         \
  V(X64I64x2ExtMulHighI32x4S)                        \
  V(X64I64x2ExtMulLowI32x4U)                         \
  V(X64I64x2ExtMulHighI32x4U)                        \
  V(X64I64x2SConvertI32x4Low)                        \
  V(X64I64x2SConvertI32x4High)                       \
  V(X64I64x4SConvertI32x4)                           \
  V(X64I64x2UConvertI32x4Low)                        \
  V(X64I64x2UConvertI32x4High)                       \
  V(X64I64x4UConvertI32x4)                           \
  V(X64I32x4SConvertF32x4)                           \
  V(X64I32x8SConvertF32x8)                           \
  V(X64I32x4SConvertI16x8Low)                        \
  V(X64I32x4SConvertI16x8High)                       \
  V(X64I32x8SConvertI16x8)                           \
  V(X64IMinS)                                        \
  V(X64IMaxS)                                        \
  V(X64I32x4UConvertF32x4)                           \
  V(X64I32x8UConvertF32x8)                           \
  V(X64I32x4UConvertI16x8Low)                        \
  V(X64I32x4UConvertI16x8High)                       \
  V(X64I32x8UConvertI16x8)                           \
  V(X64IMinU)                                        \
  V(X64IMaxU)                                        \
  V(X64IGtU)                                         \
  V(X64IGeU)                                         \
  V(X64I32x4DotI16x8S)                               \
  V(X64I32x8DotI16x16S)                              \
  V(X64I32x4DotI8x16I7x16AddS)                       \
  V(X64I32x4ExtMulLowI16x8S)                         \
  V(X64I32x4ExtMulHighI16x8S)                        \
  V(X64I32x4ExtMulLowI16x8U)                         \
  V(X64I32x4ExtMulHighI16x8U)                        \
  V(X64I32x4ExtAddPairwiseI16x8S)                    \
  V(X64I32x8ExtAddPairwiseI16x16S)                   \
  V(X64I32x4ExtAddPairwiseI16x8U)                    \
  V(X64I32x8ExtAddPairwiseI16x16U)                   \
  V(X64I32x4TruncSatF64x2SZero)                      \
  V(X64I32x4TruncSatF64x2UZero)                      \
  V(X64I32X4ShiftZeroExtendI8x16)                    \
  V(X64IExtractLaneS)                                \
  V(X64I16x8SConvertI8x16Low)                        \
  V(X64I16x8SConvertI8x16High)                       \
  V(X64I16x16SConvertI8x16)                          \
  V(X64I16x8SConvertI32x4)                           \
  V(X64I16x16SConvertI32x8)                          \
  V(X64IAddSatS)                                     \
  V(X64ISubSatS)                                     \
  V(X64I16x8UConvertI8x16Low)                        \
  V(X64I16x8UConvertI8x16High)                       \
  V(X64I16x16UConvertI8x16)                          \
  V(X64I16x8UConvertI32x4)                           \
  V(X64I16x16UConvertI32x8)                          \
  V(X64IAddSatU)                                     \
  V(X64ISubSatU)                                     \
  V(X64IRoundingAverageU)                            \
  V(X64I16x8ExtMulLowI8x16S)                         \
  V(X64I16x8ExtMulHighI8x16S)                        \
  V(X64I16x8ExtMulLowI8x16U)                         \
  V(X64I16x8ExtMulHighI8x16U)                        \
  V(X64I16x8ExtAddPairwiseI8x16S)                    \
  V(X64I16x16ExtAddPairwiseI8x32S)                   \
  V(X64I16x8ExtAddPairwiseI8x16U)                    \
  V(X64I16x16ExtAddPairwiseI8x32U)                   \
  V(X64I16x8Q15MulRSatS)                             \
  V(X64I16x8RelaxedQ15MulRS)                         \
  V(X64I16x8DotI8x16I7x16S)                          \
  V(X64I8x16SConvertI16x8)                           \
  V(X64I8x32SConvertI16x16)                          \
  V(X64I8x16UConvertI16x8)                           \
  V(X64I8x32UConvertI16x16)                          \
  V(X64S128Const)                                    \
  V(X64S256Const)                                    \
  V(X64SZero)                                        \
  V(X64SAllOnes)                                     \
  V(X64SNot)                                         \
  V(X64SAnd)                                         \
  V(X64SOr)                                          \
  V(X64SXor)                                         \
  V(X64SSelect)                                      \
  V(X64SAndNot)                                      \
  V(X64I8x16Swizzle)                                 \
  V(X64I8x16Shuffle)                                 \
  V(X64Vpshufd)                                      \
  V(X64I8x16Popcnt)                                  \
  V(X64Shufps)                                       \
  V(X64S32x4Rotate)                                  \
  V(X64S32x4Swizzle)                                 \
  V(X64S32x4Shuffle)                                 \
  V(X64S16x8Blend)                                   \
  V(X64S16x8HalfShuffle1)                            \
  V(X64S16x8HalfShuffle2)                            \
  V(X64S8x16Alignr)                                  \
  V(X64S16x8Dup)                                     \
  V(X64S8x16Dup)                                     \
  V(X64S16x8UnzipHigh)                               \
  V(X64S16x8UnzipLow)                                \
  V(X64S8x16UnzipHigh)                               \
  V(X64S8x16UnzipLow)                                \
  V(X64S64x2UnpackHigh)                              \
  V(X64S32x4UnpackHigh)                              \
  V(X64S16x8UnpackHigh)                              \
  V(X64S8x16UnpackHigh)                              \
  V(X64S32x8UnpackHigh)                              \
  V(X64S64x2UnpackLow)                               \
  V(X64S32x4UnpackLow)                               \
  V(X64S16x8UnpackLow)                               \
  V(X64S8x16UnpackLow)                               \
  V(X64S32x8UnpackLow)                               \
  V(X64S8x16TransposeLow)                            \
  V(X64S8x16TransposeHigh)                           \
  V(X64S8x8Reverse)                                  \
  V(X64S8x4Reverse)                                  \
  V(X64S8x2Reverse)                                  \
  V(X64V128AnyTrue)                                  \
  V(X64IAllTrue)                                     \
  V(X64Blendvpd)                                     \
  V(X64Blendvps)                                     \
  V(X64Pblendvb)                                     \
  V(X64I64x4ExtMulI32x4S)                            \
  V(X64I64x4ExtMulI32x4U)                            \
  V(X64I32x8ExtMulI16x8S)                            \
  V(X64I32x8ExtMulI16x8U)                            \
  V(X64I16x16ExtMulI8x16S)                           \
  V(X64I16x16ExtMulI8x16U)                           \
  V(X64TraceInstruction)                             \
  V(X64F32x8Pmin)                                    \
  V(X64F32x8Pmax)                                    \
  V(X64F64x4Pmin)                                    \
  V(X64F64x4Pmax)                                    \
  V(X64ExtractF128)                                  \
  V(X64F32x8Qfma)                                    \
  V(X64F32x8Qfms)                                    \
  V(X64F64x4Qfma)                                    \
  V(X64F64x4Qfms)                                    \
  V(X64InsertI128)                                   \
  V(X64I32x8DotI8x32I7x32AddS)                       \
  V(X64I16x16DotI8x32I7x32S)

// Addressing modes represent the "shape" of inputs to an instruction.
// Many instructions support multiple addressing modes. Addressing modes
// are encoded into the InstructionCode of the instruction and tell the
// code generator after register allocation which assembler method to call.
//
// We use the following local notation for addressing modes:
//
// M = memory operand
// R = base register
// N = index register * N for N in {1, 2, 4, 8}
// I = immediate displacement (32-bit signed integer)

#define TARGET_ADDRESSING_MODE_LIST(V)   \
  V(MR)   /* [%r1            ] */        \
  V(MRI)  /* [%r1         + K] */        \
  V(MR1)  /* [%r1 + %r2*1    ] */        \
  V(MR2)  /* [%r1 + %r2*2    ] */        \
  V(MR4)  /* [%r1 + %r2*4    ] */        \
  V(MR8)  /* [%r1 + %r2*8    ] */        \
  V(MR1I) /* [%r1 + %r2*1 + K] */        \
  V(MR2I) /* [%r1 + %r2*2 + K] */        \
  V(MR4I) /* [%r1 + %r2*4 + K] */        \
  V(MR8I) /* [%r1 + %r2*8 + K] */        \
  V(M1)   /* [      %r2*1    ] */        \
  V(M2)   /* [      %r2*2    ] */        \
  V(M4)   /* [      %r2*4    ] */        \
  V(M8)   /* [      %r2*8    ] */        \
  V(M1I)  /* [      %r2*1 + K] */        \
  V(M2I)  /* [      %r2*2 + K] */        \
  V(M4I)  /* [      %r2*4 + K] */        \
  V(M8I)  /* [      %r2*8 + K] */        \
  V(Root) /* [%root       + K] */        \
  V(MCR)  /* [%compressed_base + %r1] */ \
  V(MCRI) /* [%compressed_base + %r1 + K] */

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_X64_INSTRUCTION_CODES_X64_H_
