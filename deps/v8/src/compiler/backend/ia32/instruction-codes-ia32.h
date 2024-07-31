// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_IA32_INSTRUCTION_CODES_IA32_H_
#define V8_COMPILER_BACKEND_IA32_INSTRUCTION_CODES_IA32_H_

namespace v8 {
namespace internal {
namespace compiler {

// IA32-specific opcodes that specify which assembly sequence to emit.
// Most opcodes specify a single instruction.

#define TARGET_ARCH_OPCODE_LIST(V) \
  V(IA32Add)                       \
  V(IA32And)                       \
  V(IA32Cmp)                       \
  V(IA32Cmp16)                     \
  V(IA32Cmp8)                      \
  V(IA32Test)                      \
  V(IA32Test16)                    \
  V(IA32Test8)                     \
  V(IA32Or)                        \
  V(IA32Xor)                       \
  V(IA32Sub)                       \
  V(IA32Imul)                      \
  V(IA32ImulHigh)                  \
  V(IA32UmulHigh)                  \
  V(IA32Idiv)                      \
  V(IA32Udiv)                      \
  V(IA32Not)                       \
  V(IA32Neg)                       \
  V(IA32Shl)                       \
  V(IA32Shr)                       \
  V(IA32Sar)                       \
  V(IA32AddPair)                   \
  V(IA32SubPair)                   \
  V(IA32MulPair)                   \
  V(IA32ShlPair)                   \
  V(IA32ShrPair)                   \
  V(IA32SarPair)                   \
  V(IA32Rol)                       \
  V(IA32Ror)                       \
  V(IA32Lzcnt)                     \
  V(IA32Tzcnt)                     \
  V(IA32Popcnt)                    \
  V(IA32Bswap)                     \
  V(IA32MFence)                    \
  V(IA32LFence)                    \
  V(IA32Float32Cmp)                \
  V(IA32Float32Sqrt)               \
  V(IA32Float32Round)              \
  V(IA32Float64Cmp)                \
  V(IA32Float64Mod)                \
  V(IA32Float32Max)                \
  V(IA32Float64Max)                \
  V(IA32Float32Min)                \
  V(IA32Float64Min)                \
  V(IA32Float64Sqrt)               \
  V(IA32Float64Round)              \
  V(IA32Float32ToFloat64)          \
  V(IA32Float64ToFloat32)          \
  V(IA32Float32ToInt32)            \
  V(IA32Float32ToUint32)           \
  V(IA32Float64ToInt32)            \
  V(IA32Float64ToUint32)           \
  V(SSEInt32ToFloat32)             \
  V(IA32Uint32ToFloat32)           \
  V(SSEInt32ToFloat64)             \
  V(IA32Uint32ToFloat64)           \
  V(IA32Float64ExtractLowWord32)   \
  V(IA32Float64ExtractHighWord32)  \
  V(IA32Float64InsertLowWord32)    \
  V(IA32Float64InsertHighWord32)   \
  V(IA32Float64FromWord32Pair)     \
  V(IA32Float64LoadLowWord32)      \
  V(IA32Float64SilenceNaN)         \
  V(Float32Add)                    \
  V(Float32Sub)                    \
  V(Float64Add)                    \
  V(Float64Sub)                    \
  V(Float32Mul)                    \
  V(Float32Div)                    \
  V(Float64Mul)                    \
  V(Float64Div)                    \
  V(Float64Abs)                    \
  V(Float64Neg)                    \
  V(Float32Abs)                    \
  V(Float32Neg)                    \
  V(IA32Movsxbl)                   \
  V(IA32Movzxbl)                   \
  V(IA32Movb)                      \
  V(IA32Movsxwl)                   \
  V(IA32Movzxwl)                   \
  V(IA32Movw)                      \
  V(IA32Movl)                      \
  V(IA32Movss)                     \
  V(IA32Movsd)                     \
  V(IA32Movdqu)                    \
  V(IA32Movlps)                    \
  V(IA32Movhps)                    \
  V(IA32BitcastFI)                 \
  V(IA32BitcastIF)                 \
  V(IA32Blendvpd)                  \
  V(IA32Blendvps)                  \
  V(IA32Lea)                       \
  V(IA32Pblendvb)                  \
  V(IA32Push)                      \
  V(IA32Poke)                      \
  V(IA32Peek)                      \
  V(IA32Cvttps2dq)                 \
  V(IA32Cvttpd2dq)                 \
  V(IA32I32x4TruncF32x4U)          \
  V(IA32I32x4TruncF64x2UZero)      \
  V(IA32F64x2Splat)                \
  V(IA32F64x2ExtractLane)          \
  V(IA32F64x2ReplaceLane)          \
  V(IA32F64x2Sqrt)                 \
  V(IA32F64x2Add)                  \
  V(IA32F64x2Sub)                  \
  V(IA32F64x2Mul)                  \
  V(IA32F64x2Div)                  \
  V(IA32F64x2Min)                  \
  V(IA32F64x2Max)                  \
  V(IA32F64x2Eq)                   \
  V(IA32F64x2Ne)                   \
  V(IA32F64x2Lt)                   \
  V(IA32F64x2Le)                   \
  V(IA32F64x2Qfma)                 \
  V(IA32F64x2Qfms)                 \
  V(IA32Minpd)                     \
  V(IA32Maxpd)                     \
  V(IA32F64x2Round)                \
  V(IA32F64x2ConvertLowI32x4S)     \
  V(IA32F64x2ConvertLowI32x4U)     \
  V(IA32F64x2PromoteLowF32x4)      \
  V(IA32I64x2SplatI32Pair)         \
  V(IA32I64x2ReplaceLaneI32Pair)   \
  V(IA32I64x2Abs)                  \
  V(IA32I64x2Neg)                  \
  V(IA32I64x2Shl)                  \
  V(IA32I64x2ShrS)                 \
  V(IA32I64x2Add)                  \
  V(IA32I64x2Sub)                  \
  V(IA32I64x2Mul)                  \
  V(IA32I64x2ShrU)                 \
  V(IA32I64x2BitMask)              \
  V(IA32I64x2Eq)                   \
  V(IA32I64x2Ne)                   \
  V(IA32I64x2GtS)                  \
  V(IA32I64x2GeS)                  \
  V(IA32I64x2ExtMulLowI32x4S)      \
  V(IA32I64x2ExtMulHighI32x4S)     \
  V(IA32I64x2ExtMulLowI32x4U)      \
  V(IA32I64x2ExtMulHighI32x4U)     \
  V(IA32I64x2SConvertI32x4Low)     \
  V(IA32I64x2SConvertI32x4High)    \
  V(IA32I64x2UConvertI32x4Low)     \
  V(IA32I64x2UConvertI32x4High)    \
  V(IA32F32x4Splat)                \
  V(IA32F32x4ExtractLane)          \
  V(IA32Insertps)                  \
  V(IA32F32x4SConvertI32x4)        \
  V(IA32F32x4UConvertI32x4)        \
  V(IA32F32x4Sqrt)                 \
  V(IA32F32x4Add)                  \
  V(IA32F32x4Sub)                  \
  V(IA32F32x4Mul)                  \
  V(IA32F32x4Div)                  \
  V(IA32F32x4Min)                  \
  V(IA32F32x4Max)                  \
  V(IA32F32x4Eq)                   \
  V(IA32F32x4Ne)                   \
  V(IA32F32x4Lt)                   \
  V(IA32F32x4Le)                   \
  V(IA32F32x4Qfma)                 \
  V(IA32F32x4Qfms)                 \
  V(IA32Minps)                     \
  V(IA32Maxps)                     \
  V(IA32F32x4Round)                \
  V(IA32F32x4DemoteF64x2Zero)      \
  V(IA32I32x4Splat)                \
  V(IA32I32x4ExtractLane)          \
  V(IA32I32x4SConvertF32x4)        \
  V(IA32I32x4SConvertI16x8Low)     \
  V(IA32I32x4SConvertI16x8High)    \
  V(IA32I32x4Neg)                  \
  V(IA32I32x4Shl)                  \
  V(IA32I32x4ShrS)                 \
  V(IA32I32x4Add)                  \
  V(IA32I32x4Sub)                  \
  V(IA32I32x4Mul)                  \
  V(IA32I32x4MinS)                 \
  V(IA32I32x4MaxS)                 \
  V(IA32I32x4Eq)                   \
  V(IA32I32x4Ne)                   \
  V(IA32I32x4GtS)                  \
  V(IA32I32x4GeS)                  \
  V(SSEI32x4UConvertF32x4)         \
  V(AVXI32x4UConvertF32x4)         \
  V(IA32I32x4UConvertI16x8Low)     \
  V(IA32I32x4UConvertI16x8High)    \
  V(IA32I32x4ShrU)                 \
  V(IA32I32x4MinU)                 \
  V(IA32I32x4MaxU)                 \
  V(SSEI32x4GtU)                   \
  V(AVXI32x4GtU)                   \
  V(SSEI32x4GeU)                   \
  V(AVXI32x4GeU)                   \
  V(IA32I32x4Abs)                  \
  V(IA32I32x4BitMask)              \
  V(IA32I32x4DotI16x8S)            \
  V(IA32I32x4DotI8x16I7x16AddS)    \
  V(IA32I32x4ExtMulLowI16x8S)      \
  V(IA32I32x4ExtMulHighI16x8S)     \
  V(IA32I32x4ExtMulLowI16x8U)      \
  V(IA32I32x4ExtMulHighI16x8U)     \
  V(IA32I32x4ExtAddPairwiseI16x8S) \
  V(IA32I32x4ExtAddPairwiseI16x8U) \
  V(IA32I32x4TruncSatF64x2SZero)   \
  V(IA32I32x4TruncSatF64x2UZero)   \
  V(IA32I16x8Splat)                \
  V(IA32I16x8ExtractLaneS)         \
  V(IA32I16x8SConvertI8x16Low)     \
  V(IA32I16x8SConvertI8x16High)    \
  V(IA32I16x8Neg)                  \
  V(IA32I16x8Shl)                  \
  V(IA32I16x8ShrS)                 \
  V(IA32I16x8SConvertI32x4)        \
  V(IA32I16x8Add)                  \
  V(IA32I16x8AddSatS)              \
  V(IA32I16x8Sub)                  \
  V(IA32I16x8SubSatS)              \
  V(IA32I16x8Mul)                  \
  V(IA32I16x8MinS)                 \
  V(IA32I16x8MaxS)                 \
  V(IA32I16x8Eq)                   \
  V(SSEI16x8Ne)                    \
  V(AVXI16x8Ne)                    \
  V(IA32I16x8GtS)                  \
  V(SSEI16x8GeS)                   \
  V(AVXI16x8GeS)                   \
  V(IA32I16x8UConvertI8x16Low)     \
  V(IA32I16x8UConvertI8x16High)    \
  V(IA32I16x8ShrU)                 \
  V(IA32I16x8UConvertI32x4)        \
  V(IA32I16x8AddSatU)              \
  V(IA32I16x8SubSatU)              \
  V(IA32I16x8MinU)                 \
  V(IA32I16x8MaxU)                 \
  V(SSEI16x8GtU)                   \
  V(AVXI16x8GtU)                   \
  V(SSEI16x8GeU)                   \
  V(AVXI16x8GeU)                   \
  V(IA32I16x8RoundingAverageU)     \
  V(IA32I16x8Abs)                  \
  V(IA32I16x8BitMask)              \
  V(IA32I16x8ExtMulLowI8x16S)      \
  V(IA32I16x8ExtMulHighI8x16S)     \
  V(IA32I16x8ExtMulLowI8x16U)      \
  V(IA32I16x8ExtMulHighI8x16U)     \
  V(IA32I16x8ExtAddPairwiseI8x16S) \
  V(IA32I16x8ExtAddPairwiseI8x16U) \
  V(IA32I16x8Q15MulRSatS)          \
  V(IA32I16x8RelaxedQ15MulRS)      \
  V(IA32I8x16Splat)                \
  V(IA32I8x16ExtractLaneS)         \
  V(IA32Pinsrb)                    \
  V(IA32Pinsrw)                    \
  V(IA32Pinsrd)                    \
  V(IA32Pextrb)                    \
  V(IA32Pextrw)                    \
  V(IA32S128Store32Lane)           \
  V(IA32I8x16SConvertI16x8)        \
  V(IA32I8x16Neg)                  \
  V(IA32I8x16Shl)                  \
  V(IA32I8x16ShrS)                 \
  V(IA32I8x16Add)                  \
  V(IA32I8x16AddSatS)              \
  V(IA32I8x16Sub)                  \
  V(IA32I8x16SubSatS)              \
  V(IA32I8x16MinS)                 \
  V(IA32I8x16MaxS)                 \
  V(IA32I8x16Eq)                   \
  V(SSEI8x16Ne)                    \
  V(AVXI8x16Ne)                    \
  V(IA32I8x16GtS)                  \
  V(SSEI8x16GeS)                   \
  V(AVXI8x16GeS)                   \
  V(IA32I8x16UConvertI16x8)        \
  V(IA32I8x16AddSatU)              \
  V(IA32I8x16SubSatU)              \
  V(IA32I8x16ShrU)                 \
  V(IA32I8x16MinU)                 \
  V(IA32I8x16MaxU)                 \
  V(SSEI8x16GtU)                   \
  V(AVXI8x16GtU)                   \
  V(SSEI8x16GeU)                   \
  V(AVXI8x16GeU)                   \
  V(IA32I8x16RoundingAverageU)     \
  V(IA32I8x16Abs)                  \
  V(IA32I8x16BitMask)              \
  V(IA32I8x16Popcnt)               \
  V(IA32S128Const)                 \
  V(IA32S128Zero)                  \
  V(IA32S128AllOnes)               \
  V(IA32S128Not)                   \
  V(IA32S128And)                   \
  V(IA32S128Or)                    \
  V(IA32S128Xor)                   \
  V(IA32S128Select)                \
  V(IA32S128AndNot)                \
  V(IA32I8x16Swizzle)              \
  V(IA32I8x16Shuffle)              \
  V(IA32S128Load8Splat)            \
  V(IA32S128Load16Splat)           \
  V(IA32S128Load32Splat)           \
  V(IA32S128Load64Splat)           \
  V(IA32S128Load8x8S)              \
  V(IA32S128Load8x8U)              \
  V(IA32S128Load16x4S)             \
  V(IA32S128Load16x4U)             \
  V(IA32S128Load32x2S)             \
  V(IA32S128Load32x2U)             \
  V(IA32S32x4Rotate)               \
  V(IA32S32x4Swizzle)              \
  V(IA32S32x4Shuffle)              \
  V(IA32S16x8Blend)                \
  V(IA32S16x8HalfShuffle1)         \
  V(IA32S16x8HalfShuffle2)         \
  V(IA32S8x16Alignr)               \
  V(IA32S16x8Dup)                  \
  V(IA32S8x16Dup)                  \
  V(SSES16x8UnzipHigh)             \
  V(AVXS16x8UnzipHigh)             \
  V(SSES16x8UnzipLow)              \
  V(AVXS16x8UnzipLow)              \
  V(SSES8x16UnzipHigh)             \
  V(AVXS8x16UnzipHigh)             \
  V(SSES8x16UnzipLow)              \
  V(AVXS8x16UnzipLow)              \
  V(IA32S64x2UnpackHigh)           \
  V(IA32S32x4UnpackHigh)           \
  V(IA32S16x8UnpackHigh)           \
  V(IA32S8x16UnpackHigh)           \
  V(IA32S64x2UnpackLow)            \
  V(IA32S32x4UnpackLow)            \
  V(IA32S16x8UnpackLow)            \
  V(IA32S8x16UnpackLow)            \
  V(SSES8x16TransposeLow)          \
  V(AVXS8x16TransposeLow)          \
  V(SSES8x16TransposeHigh)         \
  V(AVXS8x16TransposeHigh)         \
  V(SSES8x8Reverse)                \
  V(AVXS8x8Reverse)                \
  V(SSES8x4Reverse)                \
  V(AVXS8x4Reverse)                \
  V(SSES8x2Reverse)                \
  V(AVXS8x2Reverse)                \
  V(IA32S128AnyTrue)               \
  V(IA32I64x2AllTrue)              \
  V(IA32I32x4AllTrue)              \
  V(IA32I16x8AllTrue)              \
  V(IA32I8x16AllTrue)              \
  V(IA32I16x8DotI8x16I7x16S)       \
  V(IA32Word32AtomicPairLoad)      \
  V(IA32Word32ReleasePairStore)    \
  V(IA32Word32SeqCstPairStore)     \
  V(IA32Word32AtomicPairAdd)       \
  V(IA32Word32AtomicPairSub)       \
  V(IA32Word32AtomicPairAnd)       \
  V(IA32Word32AtomicPairOr)        \
  V(IA32Word32AtomicPairXor)       \
  V(IA32Word32AtomicPairExchange)  \
  V(IA32Word32AtomicPairCompareExchange)

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
// I = immediate displacement (int32_t)

#define TARGET_ADDRESSING_MODE_LIST(V) \
  V(MR)   /* [%r1            ] */      \
  V(MRI)  /* [%r1         + K] */      \
  V(MR1)  /* [%r1 + %r2*1    ] */      \
  V(MR2)  /* [%r1 + %r2*2    ] */      \
  V(MR4)  /* [%r1 + %r2*4    ] */      \
  V(MR8)  /* [%r1 + %r2*8    ] */      \
  V(MR1I) /* [%r1 + %r2*1 + K] */      \
  V(MR2I) /* [%r1 + %r2*2 + K] */      \
  V(MR4I) /* [%r1 + %r2*4 + K] */      \
  V(MR8I) /* [%r1 + %r2*8 + K] */      \
  V(M1)   /* [      %r2*1    ] */      \
  V(M2)   /* [      %r2*2    ] */      \
  V(M4)   /* [      %r2*4    ] */      \
  V(M8)   /* [      %r2*8    ] */      \
  V(M1I)  /* [      %r2*1 + K] */      \
  V(M2I)  /* [      %r2*2 + K] */      \
  V(M4I)  /* [      %r2*4 + K] */      \
  V(M8I)  /* [      %r2*8 + K] */      \
  V(MI)   /* [              K] */      \
  V(Root) /* [%root       + K] */

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_IA32_INSTRUCTION_CODES_IA32_H_
