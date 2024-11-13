// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/shared-ia32-x64/macro-assembler-shared-ia32-x64.h"

#include "src/codegen/assembler.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/register.h"

#if V8_TARGET_ARCH_IA32
#include "src/codegen/ia32/register-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/codegen/x64/register-x64.h"
#else
#error Unsupported target architecture.
#endif

// Operand on IA32 can be a wrapper for a single register, in which case they
// should call I8x16Splat |src| being Register.
#if V8_TARGET_ARCH_IA32
#define DCHECK_OPERAND_IS_NOT_REG(op) DCHECK(!op.is_reg_only());
#else
#define DCHECK_OPERAND_IS_NOT_REG(op)
#endif

namespace v8 {
namespace internal {

void SharedMacroAssemblerBase::Move(Register dst, uint32_t src) {
  // Helper to paper over the different assembler function names.
#if V8_TARGET_ARCH_IA32
  mov(dst, Immediate(src));
#elif V8_TARGET_ARCH_X64
  movl(dst, Immediate(src));
#else
#error Unsupported target architecture.
#endif
}

void SharedMacroAssemblerBase::Move(Register dst, Register src) {
  // Helper to paper over the different assembler function names.
  if (dst != src) {
#if V8_TARGET_ARCH_IA32
    mov(dst, src);
#elif V8_TARGET_ARCH_X64
    movq(dst, src);
#else
#error Unsupported target architecture.
#endif
  }
}

void SharedMacroAssemblerBase::Add(Register dst, Immediate src) {
  // Helper to paper over the different assembler function names.
#if V8_TARGET_ARCH_IA32
  add(dst, src);
#elif V8_TARGET_ARCH_X64
  addq(dst, src);
#else
#error Unsupported target architecture.
#endif
}

void SharedMacroAssemblerBase::And(Register dst, Immediate src) {
  // Helper to paper over the different assembler function names.
#if V8_TARGET_ARCH_IA32
  and_(dst, src);
#elif V8_TARGET_ARCH_X64
  if (is_uint32(src.value())) {
    andl(dst, src);
  } else {
    andq(dst, src);
  }
#else
#error Unsupported target architecture.
#endif
}

void SharedMacroAssemblerBase::Movhps(XMMRegister dst, XMMRegister src1,
                                      Operand src2) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vmovhps(dst, src1, src2);
  } else {
    if (dst != src1) {
      movaps(dst, src1);
    }
    movhps(dst, src2);
  }
}

void SharedMacroAssemblerBase::Movlps(XMMRegister dst, XMMRegister src1,
                                      Operand src2) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vmovlps(dst, src1, src2);
  } else {
    if (dst != src1) {
      movaps(dst, src1);
    }
    movlps(dst, src2);
  }
}
void SharedMacroAssemblerBase::Blendvpd(XMMRegister dst, XMMRegister src1,
                                        XMMRegister src2, XMMRegister mask) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vblendvpd(dst, src1, src2, mask);
  } else {
    CpuFeatureScope scope(this, SSE4_1);
    DCHECK_EQ(mask, xmm0);
    DCHECK_EQ(dst, src1);
    blendvpd(dst, src2);
  }
}

void SharedMacroAssemblerBase::Blendvps(XMMRegister dst, XMMRegister src1,
                                        XMMRegister src2, XMMRegister mask) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vblendvps(dst, src1, src2, mask);
  } else {
    CpuFeatureScope scope(this, SSE4_1);
    DCHECK_EQ(mask, xmm0);
    DCHECK_EQ(dst, src1);
    blendvps(dst, src2);
  }
}

void SharedMacroAssemblerBase::Pblendvb(XMMRegister dst, XMMRegister src1,
                                        XMMRegister src2, XMMRegister mask) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vpblendvb(dst, src1, src2, mask);
  } else {
    CpuFeatureScope scope(this, SSE4_1);
    DCHECK_EQ(mask, xmm0);
    DCHECK_EQ(dst, src1);
    pblendvb(dst, src2);
  }
}

void SharedMacroAssemblerBase::Shufps(XMMRegister dst, XMMRegister src1,
                                      XMMRegister src2, uint8_t imm8) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vshufps(dst, src1, src2, imm8);
  } else {
    if (dst != src1) {
      movaps(dst, src1);
    }
    shufps(dst, src2, imm8);
  }
}

void SharedMacroAssemblerBase::F64x2ExtractLane(DoubleRegister dst,
                                                XMMRegister src, uint8_t lane) {
  ASM_CODE_COMMENT(this);
  if (lane == 0) {
    if (dst != src) {
      Movaps(dst, src);
    }
  } else {
    DCHECK_EQ(1, lane);
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      // Pass src as operand to avoid false-dependency on dst.
      vmovhlps(dst, src, src);
    } else {
      movhlps(dst, src);
    }
  }
}

void SharedMacroAssemblerBase::F64x2ReplaceLane(XMMRegister dst,
                                                XMMRegister src,
                                                DoubleRegister rep,
                                                uint8_t lane) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    if (lane == 0) {
      vmovsd(dst, src, rep);
    } else {
      vmovlhps(dst, src, rep);
    }
  } else {
    CpuFeatureScope scope(this, SSE4_1);
    if (dst != src) {
      DCHECK_NE(dst, rep);  // Ensure rep is not overwritten.
      movaps(dst, src);
    }
    if (lane == 0) {
      movsd(dst, rep);
    } else {
      movlhps(dst, rep);
    }
  }
}

void SharedMacroAssemblerBase::F32x4Min(XMMRegister dst, XMMRegister lhs,
                                        XMMRegister rhs, XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  // The minps instruction doesn't propagate NaNs and +0's in its first
  // operand. Perform minps in both orders, merge the results, and adjust.
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vminps(scratch, lhs, rhs);
    vminps(dst, rhs, lhs);
  } else if (dst == lhs || dst == rhs) {
    XMMRegister src = dst == lhs ? rhs : lhs;
    movaps(scratch, src);
    minps(scratch, dst);
    minps(dst, src);
  } else {
    movaps(scratch, lhs);
    minps(scratch, rhs);
    movaps(dst, rhs);
    minps(dst, lhs);
  }
  // Propagate -0's and NaNs, which may be non-canonical.
  Orps(scratch, dst);
  // Canonicalize NaNs by quieting and clearing the payload.
  Cmpunordps(dst, dst, scratch);
  Orps(scratch, dst);
  Psrld(dst, dst, uint8_t{10});
  Andnps(dst, dst, scratch);
}

void SharedMacroAssemblerBase::F32x4Max(XMMRegister dst, XMMRegister lhs,
                                        XMMRegister rhs, XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  // The maxps instruction doesn't propagate NaNs and +0's in its first
  // operand. Perform maxps in both orders, merge the results, and adjust.
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vmaxps(scratch, lhs, rhs);
    vmaxps(dst, rhs, lhs);
  } else if (dst == lhs || dst == rhs) {
    XMMRegister src = dst == lhs ? rhs : lhs;
    movaps(scratch, src);
    maxps(scratch, dst);
    maxps(dst, src);
  } else {
    movaps(scratch, lhs);
    maxps(scratch, rhs);
    movaps(dst, rhs);
    maxps(dst, lhs);
  }
  // Find discrepancies.
  Xorps(dst, scratch);
  // Propagate NaNs, which may be non-canonical.
  Orps(scratch, dst);
  // Propagate sign discrepancy and (subtle) quiet NaNs.
  Subps(scratch, scratch, dst);
  // Canonicalize NaNs by clearing the payload. Sign is non-deterministic.
  Cmpunordps(dst, dst, scratch);
  Psrld(dst, dst, uint8_t{10});
  Andnps(dst, dst, scratch);
}

void SharedMacroAssemblerBase::F64x2Min(XMMRegister dst, XMMRegister lhs,
                                        XMMRegister rhs, XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    // The minpd instruction doesn't propagate NaNs and +0's in its first
    // operand. Perform minpd in both orders, merge the resuls, and adjust.
    vminpd(scratch, lhs, rhs);
    vminpd(dst, rhs, lhs);
    // propagate -0's and NaNs, which may be non-canonical.
    vorpd(scratch, scratch, dst);
    // Canonicalize NaNs by quieting and clearing the payload.
    vcmpunordpd(dst, dst, scratch);
    vorpd(scratch, scratch, dst);
    vpsrlq(dst, dst, uint8_t{13});
    vandnpd(dst, dst, scratch);
  } else {
    // Compare lhs with rhs, and rhs with lhs, and have the results in scratch
    // and dst. If dst overlaps with lhs or rhs, we can save a move.
    if (dst == lhs || dst == rhs) {
      XMMRegister src = dst == lhs ? rhs : lhs;
      movaps(scratch, src);
      minpd(scratch, dst);
      minpd(dst, src);
    } else {
      movaps(scratch, lhs);
      movaps(dst, rhs);
      minpd(scratch, rhs);
      minpd(dst, lhs);
    }
    orpd(scratch, dst);
    cmpunordpd(dst, scratch);
    orpd(scratch, dst);
    psrlq(dst, uint8_t{13});
    andnpd(dst, scratch);
  }
}

void SharedMacroAssemblerBase::F64x2Max(XMMRegister dst, XMMRegister lhs,
                                        XMMRegister rhs, XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    // The maxpd instruction doesn't propagate NaNs and +0's in its first
    // operand. Perform maxpd in both orders, merge the resuls, and adjust.
    vmaxpd(scratch, lhs, rhs);
    vmaxpd(dst, rhs, lhs);
    // Find discrepancies.
    vxorpd(dst, dst, scratch);
    // Propagate NaNs, which may be non-canonical.
    vorpd(scratch, scratch, dst);
    // Propagate sign discrepancy and (subtle) quiet NaNs.
    vsubpd(scratch, scratch, dst);
    // Canonicalize NaNs by clearing the payload. Sign is non-deterministic.
    vcmpunordpd(dst, dst, scratch);
    vpsrlq(dst, dst, uint8_t{13});
    vandnpd(dst, dst, scratch);
  } else {
    if (dst == lhs || dst == rhs) {
      XMMRegister src = dst == lhs ? rhs : lhs;
      movaps(scratch, src);
      maxpd(scratch, dst);
      maxpd(dst, src);
    } else {
      movaps(scratch, lhs);
      movaps(dst, rhs);
      maxpd(scratch, rhs);
      maxpd(dst, lhs);
    }
    xorpd(dst, scratch);
    orpd(scratch, dst);
    subpd(scratch, dst);
    cmpunordpd(dst, scratch);
    psrlq(dst, uint8_t{13});
    andnpd(dst, scratch);
  }
}

void SharedMacroAssemblerBase::F32x4Splat(XMMRegister dst, DoubleRegister src) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX2)) {
    CpuFeatureScope avx2_scope(this, AVX2);
    vbroadcastss(dst, src);
  } else if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vshufps(dst, src, src, 0);
  } else {
    if (dst == src) {
      // 1 byte shorter than pshufd.
      shufps(dst, src, 0);
    } else {
      pshufd(dst, src, 0);
    }
  }
}

void SharedMacroAssemblerBase::F32x4ExtractLane(FloatRegister dst,
                                                XMMRegister src, uint8_t lane) {
  ASM_CODE_COMMENT(this);
  DCHECK_LT(lane, 4);
  // These instructions are shorter than insertps, but will leave junk in
  // the top lanes of dst.
  if (lane == 0) {
    if (dst != src) {
      Movaps(dst, src);
    }
  } else if (lane == 1) {
    Movshdup(dst, src);
  } else if (lane == 2 && dst == src) {
    // Check dst == src to avoid false dependency on dst.
    Movhlps(dst, src);
  } else if (dst == src) {
    Shufps(dst, src, src, lane);
  } else {
    Pshufd(dst, src, lane);
  }
}

void SharedMacroAssemblerBase::S128Store32Lane(Operand dst, XMMRegister src,
                                               uint8_t laneidx) {
  ASM_CODE_COMMENT(this);
  if (laneidx == 0) {
    Movss(dst, src);
  } else {
    DCHECK_GE(3, laneidx);
    Extractps(dst, src, laneidx);
  }
}

template <typename Op>
void SharedMacroAssemblerBase::I8x16SplatPreAvx2(XMMRegister dst, Op src,
                                                 XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  DCHECK(!CpuFeatures::IsSupported(AVX2));
  CpuFeatureScope ssse3_scope(this, SSSE3);
  Movd(dst, src);
  Xorps(scratch, scratch);
  Pshufb(dst, scratch);
}

void SharedMacroAssemblerBase::I8x16Splat(XMMRegister dst, Register src,
                                          XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX2)) {
    CpuFeatureScope avx2_scope(this, AVX2);
    Movd(scratch, src);
    vpbroadcastb(dst, scratch);
  } else {
    I8x16SplatPreAvx2(dst, src, scratch);
  }
}

void SharedMacroAssemblerBase::I8x16Splat(XMMRegister dst, Operand src,
                                          XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  DCHECK_OPERAND_IS_NOT_REG(src);
  if (CpuFeatures::IsSupported(AVX2)) {
    CpuFeatureScope avx2_scope(this, AVX2);
    vpbroadcastb(dst, src);
  } else {
    I8x16SplatPreAvx2(dst, src, scratch);
  }
}

void SharedMacroAssemblerBase::I8x16Shl(XMMRegister dst, XMMRegister src1,
                                        uint8_t src2, Register tmp1,
                                        XMMRegister tmp2) {
  ASM_CODE_COMMENT(this);
  DCHECK_NE(dst, tmp2);
  // Perform 16-bit shift, then mask away low bits.
  if (!CpuFeatures::IsSupported(AVX) && (dst != src1)) {
    movaps(dst, src1);
    src1 = dst;
  }

  uint8_t shift = truncate_to_int3(src2);
  Psllw(dst, src1, uint8_t{shift});

  uint8_t bmask = static_cast<uint8_t>(0xff << shift);
  uint32_t mask = bmask << 24 | bmask << 16 | bmask << 8 | bmask;
  Move(tmp1, mask);
  Movd(tmp2, tmp1);
  Pshufd(tmp2, tmp2, uint8_t{0});
  Pand(dst, tmp2);
}

void SharedMacroAssemblerBase::I8x16Shl(XMMRegister dst, XMMRegister src1,
                                        Register src2, Register tmp1,
                                        XMMRegister tmp2, XMMRegister tmp3) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(dst, tmp2, tmp3));
  DCHECK(!AreAliased(src1, tmp2, tmp3));

  // Take shift value modulo 8.
  Move(tmp1, src2);
  And(tmp1, Immediate(7));
  Add(tmp1, Immediate(8));
  // Create a mask to unset high bits.
  Movd(tmp3, tmp1);
  Pcmpeqd(tmp2, tmp2);
  Psrlw(tmp2, tmp2, tmp3);
  Packuswb(tmp2, tmp2);
  if (!CpuFeatures::IsSupported(AVX) && (dst != src1)) {
    movaps(dst, src1);
    src1 = dst;
  }
  // Mask off the unwanted bits before word-shifting.
  Pand(dst, src1, tmp2);
  Add(tmp1, Immediate(-8));
  Movd(tmp3, tmp1);
  Psllw(dst, dst, tmp3);
}

void SharedMacroAssemblerBase::I8x16ShrS(XMMRegister dst, XMMRegister src1,
                                         uint8_t src2, XMMRegister tmp) {
  ASM_CODE_COMMENT(this);
  // Unpack bytes into words, do word (16-bit) shifts, and repack.
  DCHECK_NE(dst, tmp);
  uint8_t shift = truncate_to_int3(src2) + 8;

  Punpckhbw(tmp, src1);
  Punpcklbw(dst, src1);
  Psraw(tmp, shift);
  Psraw(dst, shift);
  Packsswb(dst, tmp);
}

void SharedMacroAssemblerBase::I8x16ShrS(XMMRegister dst, XMMRegister src1,
                                         Register src2, Register tmp1,
                                         XMMRegister tmp2, XMMRegister tmp3) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(dst, tmp2, tmp3));
  DCHECK_NE(src1, tmp2);

  // Unpack the bytes into words, do arithmetic shifts, and repack.
  Punpckhbw(tmp2, src1);
  Punpcklbw(dst, src1);
  // Prepare shift value
  Move(tmp1, src2);
  // Take shift value modulo 8.
  And(tmp1, Immediate(7));
  Add(tmp1, Immediate(8));
  Movd(tmp3, tmp1);
  Psraw(tmp2, tmp3);
  Psraw(dst, tmp3);
  Packsswb(dst, tmp2);
}

void SharedMacroAssemblerBase::I8x16ShrU(XMMRegister dst, XMMRegister src1,
                                         uint8_t src2, Register tmp1,
                                         XMMRegister tmp2) {
  ASM_CODE_COMMENT(this);
  DCHECK_NE(dst, tmp2);
  if (!CpuFeatures::IsSupported(AVX) && (dst != src1)) {
    movaps(dst, src1);
    src1 = dst;
  }

  // Perform 16-bit shift, then mask away high bits.
  uint8_t shift = truncate_to_int3(src2);
  Psrlw(dst, src1, shift);

  uint8_t bmask = 0xff >> shift;
  uint32_t mask = bmask << 24 | bmask << 16 | bmask << 8 | bmask;
  Move(tmp1, mask);
  Movd(tmp2, tmp1);
  Pshufd(tmp2, tmp2, uint8_t{0});
  Pand(dst, tmp2);
}

void SharedMacroAssemblerBase::I8x16ShrU(XMMRegister dst, XMMRegister src1,
                                         Register src2, Register tmp1,
                                         XMMRegister tmp2, XMMRegister tmp3) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(dst, tmp2, tmp3));
  DCHECK_NE(src1, tmp2);

  // Unpack the bytes into words, do logical shifts, and repack.
  Punpckhbw(tmp2, src1);
  Punpcklbw(dst, src1);
  // Prepare shift value.
  Move(tmp1, src2);
  // Take shift value modulo 8.
  And(tmp1, Immediate(7));
  Add(tmp1, Immediate(8));
  Movd(tmp3, tmp1);
  Psrlw(tmp2, tmp3);
  Psrlw(dst, tmp3);
  Packuswb(dst, tmp2);
}

template <typename Op>
void SharedMacroAssemblerBase::I16x8SplatPreAvx2(XMMRegister dst, Op src) {
  DCHECK(!CpuFeatures::IsSupported(AVX2));
  Movd(dst, src);
  Pshuflw(dst, dst, uint8_t{0x0});
  Punpcklqdq(dst, dst);
}

void SharedMacroAssemblerBase::I16x8Splat(XMMRegister dst, Register src) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX2)) {
    CpuFeatureScope avx2_scope(this, AVX2);
    Movd(dst, src);
    vpbroadcastw(dst, dst);
  } else {
    I16x8SplatPreAvx2(dst, src);
  }
}

void SharedMacroAssemblerBase::I16x8Splat(XMMRegister dst, Operand src) {
  ASM_CODE_COMMENT(this);
  DCHECK_OPERAND_IS_NOT_REG(src);
  if (CpuFeatures::IsSupported(AVX2)) {
    CpuFeatureScope avx2_scope(this, AVX2);
    vpbroadcastw(dst, src);
  } else {
    I16x8SplatPreAvx2(dst, src);
  }
}

void SharedMacroAssemblerBase::I16x8ExtMulLow(XMMRegister dst, XMMRegister src1,
                                              XMMRegister src2,
                                              XMMRegister scratch,
                                              bool is_signed) {
  ASM_CODE_COMMENT(this);
  is_signed ? Pmovsxbw(scratch, src1) : Pmovzxbw(scratch, src1);
  is_signed ? Pmovsxbw(dst, src2) : Pmovzxbw(dst, src2);
  Pmullw(dst, scratch);
}

void SharedMacroAssemblerBase::I16x8ExtMulHighS(XMMRegister dst,
                                                XMMRegister src1,
                                                XMMRegister src2,
                                                XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vpunpckhbw(scratch, src1, src1);
    vpsraw(scratch, scratch, 8);
    vpunpckhbw(dst, src2, src2);
    vpsraw(dst, dst, 8);
    vpmullw(dst, dst, scratch);
  } else {
    if (dst != src1) {
      movaps(dst, src1);
    }
    movaps(scratch, src2);
    punpckhbw(dst, dst);
    psraw(dst, 8);
    punpckhbw(scratch, scratch);
    psraw(scratch, 8);
    pmullw(dst, scratch);
  }
}

void SharedMacroAssemblerBase::I16x8ExtMulHighU(XMMRegister dst,
                                                XMMRegister src1,
                                                XMMRegister src2,
                                                XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  // The logic here is slightly complicated to handle all the cases of register
  // aliasing. This allows flexibility for callers in TurboFan and Liftoff.
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    if (src1 == src2) {
      vpxor(scratch, scratch, scratch);
      vpunpckhbw(dst, src1, scratch);
      vpmullw(dst, dst, dst);
    } else {
      if (dst == src2) {
        // We overwrite dst, then use src2, so swap src1 and src2.
        std::swap(src1, src2);
      }
      vpxor(scratch, scratch, scratch);
      vpunpckhbw(dst, src1, scratch);
      vpunpckhbw(scratch, src2, scratch);
      vpmullw(dst, dst, scratch);
    }
  } else {
    if (src1 == src2) {
      xorps(scratch, scratch);
      if (dst != src1) {
        movaps(dst, src1);
      }
      punpckhbw(dst, scratch);
      pmullw(dst, scratch);
    } else {
      // When dst == src1, nothing special needs to be done.
      // When dst == src2, swap src1 and src2, since we overwrite dst.
      // When dst is unique, copy src1 to dst first.
      if (dst == src2) {
        std::swap(src1, src2);
        // Now, dst == src1.
      } else if (dst != src1) {
        // dst != src1 && dst != src2.
        movaps(dst, src1);
      }
      xorps(scratch, scratch);
      punpckhbw(dst, scratch);
      punpckhbw(scratch, src2);
      psrlw(scratch, 8);
      pmullw(dst, scratch);
    }
  }
}

void SharedMacroAssemblerBase::I16x8SConvertI8x16High(XMMRegister dst,
                                                      XMMRegister src) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // src = |a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|p| (high)
    // dst = |i|i|j|j|k|k|l|l|m|m|n|n|o|o|p|p|
    vpunpckhbw(dst, src, src);
    vpsraw(dst, dst, 8);
  } else {
    CpuFeatureScope sse_scope(this, SSE4_1);
    if (dst == src) {
      // 2 bytes shorter than pshufd, but has depdency on dst.
      movhlps(dst, src);
      pmovsxbw(dst, dst);
    } else {
      // No dependency on dst.
      pshufd(dst, src, 0xEE);
      pmovsxbw(dst, dst);
    }
  }
}

void SharedMacroAssemblerBase::I16x8UConvertI8x16High(XMMRegister dst,
                                                      XMMRegister src,
                                                      XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // tmp = |0|0|0|0|0|0|0|0 | 0|0|0|0|0|0|0|0|
    // src = |a|b|c|d|e|f|g|h | i|j|k|l|m|n|o|p|
    // dst = |0|a|0|b|0|c|0|d | 0|e|0|f|0|g|0|h|
    XMMRegister tmp = dst == src ? scratch : dst;
    vpxor(tmp, tmp, tmp);
    vpunpckhbw(dst, src, tmp);
  } else {
    CpuFeatureScope sse_scope(this, SSE4_1);
    if (dst == src) {
      // xorps can be executed on more ports than pshufd.
      xorps(scratch, scratch);
      punpckhbw(dst, scratch);
    } else {
      // No dependency on dst.
      pshufd(dst, src, 0xEE);
      pmovzxbw(dst, dst);
    }
  }
}

void SharedMacroAssemblerBase::I16x8Q15MulRSatS(XMMRegister dst,
                                                XMMRegister src1,
                                                XMMRegister src2,
                                                XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  // k = i16x8.splat(0x8000)
  Pcmpeqd(scratch, scratch);
  Psllw(scratch, scratch, uint8_t{15});

  if (!CpuFeatures::IsSupported(AVX) && (dst != src1)) {
    movaps(dst, src1);
    src1 = dst;
  }

  Pmulhrsw(dst, src1, src2);
  Pcmpeqw(scratch, dst);
  Pxor(dst, scratch);
}

void SharedMacroAssemblerBase::I16x8DotI8x16I7x16S(XMMRegister dst,
                                                   XMMRegister src1,
                                                   XMMRegister src2) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vpmaddubsw(dst, src2, src1);
  } else {
    if (dst != src2) {
      movdqa(dst, src2);
    }
    pmaddubsw(dst, src1);
  }
}

void SharedMacroAssemblerBase::I32x4DotI8x16I7x16AddS(
    XMMRegister dst, XMMRegister src1, XMMRegister src2, XMMRegister src3,
    XMMRegister scratch, XMMRegister splat_reg) {
  ASM_CODE_COMMENT(this);
#if V8_TARGET_ARCH_X64
  if (CpuFeatures::IsSupported(AVX_VNNI)) {
    CpuFeatureScope avx_scope(this, AVX_VNNI);
    if (dst == src3) {
      vpdpbusd(dst, src2, src1);
    } else {
      DCHECK_NE(dst, src1);
      DCHECK_NE(dst, src2);
      Movdqa(dst, src3);
      vpdpbusd(dst, src2, src1);
    }
    return;
  }
#endif

  // k = i16x8.splat(1)
  Pcmpeqd(splat_reg, splat_reg);
  Psrlw(splat_reg, splat_reg, uint8_t{15});

  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vpmaddubsw(scratch, src2, src1);
  } else {
    movdqa(scratch, src2);
    pmaddubsw(scratch, src1);
  }
  Pmaddwd(scratch, splat_reg);
  if (dst == src3) {
    Paddd(dst, scratch);
  } else {
    Movdqa(dst, src3);
    Paddd(dst, scratch);
  }
}

void SharedMacroAssemblerBase::I32x4ExtAddPairwiseI16x8U(XMMRegister dst,
                                                         XMMRegister src,
                                                         XMMRegister tmp) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // src = |a|b|c|d|e|f|g|h| (low)
    // scratch = |0|a|0|c|0|e|0|g|
    vpsrld(tmp, src, 16);
    // dst = |0|b|0|d|0|f|0|h|
    vpblendw(dst, src, tmp, 0xAA);
    // dst = |a+b|c+d|e+f|g+h|
    vpaddd(dst, tmp, dst);
  } else if (CpuFeatures::IsSupported(SSE4_1)) {
    CpuFeatureScope sse_scope(this, SSE4_1);
    // There is a potentially better lowering if we get rip-relative
    // constants, see https://github.com/WebAssembly/simd/pull/380.
    movaps(tmp, src);
    psrld(tmp, 16);
    if (dst != src) {
      movaps(dst, src);
    }
    pblendw(dst, tmp, 0xAA);
    paddd(dst, tmp);
  } else {
    // src = |a|b|c|d|e|f|g|h|
    // tmp = i32x4.splat(0x0000FFFF)
    pcmpeqd(tmp, tmp);
    psrld(tmp, uint8_t{16});
    // tmp =|0|b|0|d|0|f|0|h|
    andps(tmp, src);
    // dst = |0|a|0|c|0|e|0|g|
    if (dst != src) {
      movaps(dst, src);
    }
    psrld(dst, uint8_t{16});
    // dst = |a+b|c+d|e+f|g+h|
    paddd(dst, tmp);
  }
}

// 1. Multiply low word into scratch.
// 2. Multiply high word (can be signed or unsigned) into dst.
// 3. Unpack and interleave scratch and dst into dst.
void SharedMacroAssemblerBase::I32x4ExtMul(XMMRegister dst, XMMRegister src1,
                                           XMMRegister src2,
                                           XMMRegister scratch, bool low,
                                           bool is_signed) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vpmullw(scratch, src1, src2);
    is_signed ? vpmulhw(dst, src1, src2) : vpmulhuw(dst, src1, src2);
    low ? vpunpcklwd(dst, scratch, dst) : vpunpckhwd(dst, scratch, dst);
  } else {
    DCHECK_EQ(dst, src1);
    movaps(scratch, src1);
    pmullw(dst, src2);
    is_signed ? pmulhw(scratch, src2) : pmulhuw(scratch, src2);
    low ? punpcklwd(dst, scratch) : punpckhwd(dst, scratch);
  }
}

void SharedMacroAssemblerBase::I32x4SConvertI16x8High(XMMRegister dst,
                                                      XMMRegister src) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // src = |a|b|c|d|e|f|g|h| (high)
    // dst = |e|e|f|f|g|g|h|h|
    vpunpckhwd(dst, src, src);
    vpsrad(dst, dst, 16);
  } else {
    CpuFeatureScope sse_scope(this, SSE4_1);
    if (dst == src) {
      // 2 bytes shorter than pshufd, but has depdency on dst.
      movhlps(dst, src);
      pmovsxwd(dst, dst);
    } else {
      // No dependency on dst.
      pshufd(dst, src, 0xEE);
      pmovsxwd(dst, dst);
    }
  }
}

void SharedMacroAssemblerBase::I32x4UConvertI16x8High(XMMRegister dst,
                                                      XMMRegister src,
                                                      XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // scratch = |0|0|0|0|0|0|0|0|
    // src     = |a|b|c|d|e|f|g|h|
    // dst     = |0|a|0|b|0|c|0|d|
    XMMRegister tmp = dst == src ? scratch : dst;
    vpxor(tmp, tmp, tmp);
    vpunpckhwd(dst, src, tmp);
  } else {
    if (dst == src) {
      // xorps can be executed on more ports than pshufd.
      xorps(scratch, scratch);
      punpckhwd(dst, scratch);
    } else {
      CpuFeatureScope sse_scope(this, SSE4_1);
      // No dependency on dst.
      pshufd(dst, src, 0xEE);
      pmovzxwd(dst, dst);
    }
  }
}

void SharedMacroAssemblerBase::I64x2Neg(XMMRegister dst, XMMRegister src,
                                        XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vpxor(scratch, scratch, scratch);
    vpsubq(dst, scratch, src);
  } else {
    if (dst == src) {
      movaps(scratch, src);
      std::swap(src, scratch);
    }
    pxor(dst, dst);
    psubq(dst, src);
  }
}

void SharedMacroAssemblerBase::I64x2Abs(XMMRegister dst, XMMRegister src,
                                        XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    XMMRegister tmp = dst == src ? scratch : dst;
    vpxor(tmp, tmp, tmp);
    vpsubq(tmp, tmp, src);
    vblendvpd(dst, src, tmp, src);
  } else {
    CpuFeatureScope sse_scope(this, SSE3);
    movshdup(scratch, src);
    if (dst != src) {
      movaps(dst, src);
    }
    psrad(scratch, 31);
    xorps(dst, scratch);
    psubq(dst, scratch);
  }
}

void SharedMacroAssemblerBase::I64x2GtS(XMMRegister dst, XMMRegister src0,
                                        XMMRegister src1, XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vpcmpgtq(dst, src0, src1);
  } else if (CpuFeatures::IsSupported(SSE4_2)) {
    CpuFeatureScope sse_scope(this, SSE4_2);
    if (dst == src0) {
      pcmpgtq(dst, src1);
    } else if (dst == src1) {
      movaps(scratch, src0);
      pcmpgtq(scratch, src1);
      movaps(dst, scratch);
    } else {
      movaps(dst, src0);
      pcmpgtq(dst, src1);
    }
  } else {
    CpuFeatureScope sse_scope(this, SSE3);
    DCHECK_NE(dst, src0);
    DCHECK_NE(dst, src1);
    movaps(dst, src1);
    movaps(scratch, src0);
    psubq(dst, src0);
    pcmpeqd(scratch, src1);
    andps(dst, scratch);
    movaps(scratch, src0);
    pcmpgtd(scratch, src1);
    orps(dst, scratch);
    movshdup(dst, dst);
  }
}

void SharedMacroAssemblerBase::I64x2GeS(XMMRegister dst, XMMRegister src0,
                                        XMMRegister src1, XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vpcmpgtq(dst, src1, src0);
    vpcmpeqd(scratch, scratch, scratch);
    vpxor(dst, dst, scratch);
  } else if (CpuFeatures::IsSupported(SSE4_2)) {
    CpuFeatureScope sse_scope(this, SSE4_2);
    DCHECK_NE(dst, src0);
    if (dst != src1) {
      movaps(dst, src1);
    }
    pcmpgtq(dst, src0);
    pcmpeqd(scratch, scratch);
    xorps(dst, scratch);
  } else {
    CpuFeatureScope sse_scope(this, SSE3);
    DCHECK_NE(dst, src0);
    DCHECK_NE(dst, src1);
    movaps(dst, src0);
    movaps(scratch, src1);
    psubq(dst, src1);
    pcmpeqd(scratch, src0);
    andps(dst, scratch);
    movaps(scratch, src1);
    pcmpgtd(scratch, src0);
    orps(dst, scratch);
    movshdup(dst, dst);
    pcmpeqd(scratch, scratch);
    xorps(dst, scratch);
  }
}

void SharedMacroAssemblerBase::I64x2ShrS(XMMRegister dst, XMMRegister src,
                                         uint8_t shift, XMMRegister xmm_tmp) {
  ASM_CODE_COMMENT(this);
  DCHECK_GT(64, shift);
  DCHECK_NE(xmm_tmp, dst);
  DCHECK_NE(xmm_tmp, src);
  // Use logical right shift to emulate arithmetic right shifts:
  // Given:
  // signed >> c
  //   == (signed + 2^63 - 2^63) >> c
  //   == ((signed + 2^63) >> c) - (2^63 >> c)
  //                                ^^^^^^^^^
  //                                 xmm_tmp
  // signed + 2^63 is an unsigned number, so we can use logical right shifts.

  // xmm_tmp = wasm_i64x2_const(0x80000000'00000000).
  Pcmpeqd(xmm_tmp, xmm_tmp);
  Psllq(xmm_tmp, uint8_t{63});

  if (!CpuFeatures::IsSupported(AVX) && (dst != src)) {
    movaps(dst, src);
    src = dst;
  }
  // Add a bias of 2^63 to convert signed to unsigned.
  // Since only highest bit changes, use pxor instead of paddq.
  Pxor(dst, src, xmm_tmp);
  // Logically shift both value and bias.
  Psrlq(dst, shift);
  Psrlq(xmm_tmp, shift);
  // Subtract shifted bias to convert back to signed value.
  Psubq(dst, xmm_tmp);
}

void SharedMacroAssemblerBase::I64x2ShrS(XMMRegister dst, XMMRegister src,
                                         Register shift, XMMRegister xmm_tmp,
                                         XMMRegister xmm_shift,
                                         Register tmp_shift) {
  ASM_CODE_COMMENT(this);
  DCHECK_NE(xmm_tmp, dst);
  DCHECK_NE(xmm_tmp, src);
  DCHECK_NE(xmm_shift, dst);
  DCHECK_NE(xmm_shift, src);
  // tmp_shift can alias shift since we don't use shift after masking it.

  // See I64x2ShrS with constant shift for explanation of this algorithm.
  Pcmpeqd(xmm_tmp, xmm_tmp);
  Psllq(xmm_tmp, uint8_t{63});

  // Shift modulo 64.
  Move(tmp_shift, shift);
  And(tmp_shift, Immediate(0x3F));
  Movd(xmm_shift, tmp_shift);

  if (!CpuFeatures::IsSupported(AVX) && (dst != src)) {
    movaps(dst, src);
    src = dst;
  }
  Pxor(dst, src, xmm_tmp);
  Psrlq(dst, xmm_shift);
  Psrlq(xmm_tmp, xmm_shift);
  Psubq(dst, xmm_tmp);
}

void SharedMacroAssemblerBase::I64x2Mul(XMMRegister dst, XMMRegister lhs,
                                        XMMRegister rhs, XMMRegister tmp1,
                                        XMMRegister tmp2) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(dst, tmp1, tmp2));
  DCHECK(!AreAliased(lhs, tmp1, tmp2));
  DCHECK(!AreAliased(rhs, tmp1, tmp2));

  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // 1. Multiply high dword of each qword of left with right.
    vpsrlq(tmp1, lhs, uint8_t{32});
    vpmuludq(tmp1, tmp1, rhs);
    // 2. Multiply high dword of each qword of right with left.
    vpsrlq(tmp2, rhs, uint8_t{32});
    vpmuludq(tmp2, tmp2, lhs);
    // 3. Add 1 and 2, then shift left by 32 (this is the high dword of result).
    vpaddq(tmp2, tmp2, tmp1);
    vpsllq(tmp2, tmp2, uint8_t{32});
    // 4. Multiply low dwords (this is the low dword of result).
    vpmuludq(dst, lhs, rhs);
    // 5. Add 3 and 4.
    vpaddq(dst, dst, tmp2);
  } else {
    // Same algorithm as AVX version, but with moves to not overwrite inputs.
    movaps(tmp1, lhs);
    movaps(tmp2, rhs);
    psrlq(tmp1, uint8_t{32});
    pmuludq(tmp1, rhs);
    psrlq(tmp2, uint8_t{32});
    pmuludq(tmp2, lhs);
    paddq(tmp2, tmp1);
    psllq(tmp2, uint8_t{32});
    if (dst == rhs) {
      // pmuludq is commutative
      pmuludq(dst, lhs);
    } else {
      if (dst != lhs) {
        movaps(dst, lhs);
      }
      pmuludq(dst, rhs);
    }
    paddq(dst, tmp2);
  }
}

// 1. Unpack src0, src1 into even-number elements of scratch.
// 2. Unpack src1, src0 into even-number elements of dst.
// 3. Multiply 1. with 2.
// For non-AVX, use non-destructive pshufd instead of punpckldq/punpckhdq.
void SharedMacroAssemblerBase::I64x2ExtMul(XMMRegister dst, XMMRegister src1,
                                           XMMRegister src2,
                                           XMMRegister scratch, bool low,
                                           bool is_signed) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    if (low) {
      vpunpckldq(scratch, src1, src1);
      vpunpckldq(dst, src2, src2);
    } else {
      vpunpckhdq(scratch, src1, src1);
      vpunpckhdq(dst, src2, src2);
    }
    if (is_signed) {
      vpmuldq(dst, scratch, dst);
    } else {
      vpmuludq(dst, scratch, dst);
    }
  } else {
    uint8_t mask = low ? 0x50 : 0xFA;
    pshufd(scratch, src1, mask);
    pshufd(dst, src2, mask);
    if (is_signed) {
      CpuFeatureScope sse4_scope(this, SSE4_1);
      pmuldq(dst, scratch);
    } else {
      pmuludq(dst, scratch);
    }
  }
}

void SharedMacroAssemblerBase::I64x2SConvertI32x4High(XMMRegister dst,
                                                      XMMRegister src) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vpunpckhqdq(dst, src, src);
    vpmovsxdq(dst, dst);
  } else {
    CpuFeatureScope sse_scope(this, SSE4_1);
    if (dst == src) {
      movhlps(dst, src);
    } else {
      pshufd(dst, src, 0xEE);
    }
    pmovsxdq(dst, dst);
  }
}

void SharedMacroAssemblerBase::I64x2UConvertI32x4High(XMMRegister dst,
                                                      XMMRegister src,
                                                      XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vpxor(scratch, scratch, scratch);
    vpunpckhdq(dst, src, scratch);
  } else {
    if (dst == src) {
      // xorps can be executed on more ports than pshufd.
      xorps(scratch, scratch);
      punpckhdq(dst, scratch);
    } else {
      CpuFeatureScope sse_scope(this, SSE4_1);
      // No dependency on dst.
      pshufd(dst, src, 0xEE);
      pmovzxdq(dst, dst);
    }
  }
}

void SharedMacroAssemblerBase::S128Not(XMMRegister dst, XMMRegister src,
                                       XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  if (dst == src) {
    Pcmpeqd(scratch, scratch);
    Pxor(dst, scratch);
  } else {
    Pcmpeqd(dst, dst);
    Pxor(dst, src);
  }
}

void SharedMacroAssemblerBase::S128Select(XMMRegister dst, XMMRegister mask,
                                          XMMRegister src1, XMMRegister src2,
                                          XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  // v128.select = v128.or(v128.and(v1, c), v128.andnot(v2, c)).
  // pandn(x, y) = !x & y, so we have to flip the mask and input.
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vpandn(scratch, mask, src2);
    vpand(dst, src1, mask);
    vpor(dst, dst, scratch);
  } else {
    DCHECK_EQ(dst, mask);
    // Use float ops as they are 1 byte shorter than int ops.
    movaps(scratch, mask);
    andnps(scratch, src2);
    andps(dst, src1);
    orps(dst, scratch);
  }
}

void SharedMacroAssemblerBase::S128Load8Splat(XMMRegister dst, Operand src,
                                              XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  // The trap handler uses the current pc to creating a landing, so that it can
  // determine if a trap occured in Wasm code due to a OOB load. Make sure the
  // first instruction in each case below is the one that loads.
  if (CpuFeatures::IsSupported(AVX2)) {
    CpuFeatureScope avx2_scope(this, AVX2);
    vpbroadcastb(dst, src);
  } else if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // Avoid dependency on previous value of dst.
    vpinsrb(dst, scratch, src, uint8_t{0});
    vpxor(scratch, scratch, scratch);
    vpshufb(dst, dst, scratch);
  } else {
    CpuFeatureScope ssse4_scope(this, SSE4_1);
    pinsrb(dst, src, uint8_t{0});
    xorps(scratch, scratch);
    pshufb(dst, scratch);
  }
}

void SharedMacroAssemblerBase::S128Load16Splat(XMMRegister dst, Operand src,
                                               XMMRegister scratch) {
  ASM_CODE_COMMENT(this);
  // The trap handler uses the current pc to creating a landing, so that it can
  // determine if a trap occured in Wasm code due to a OOB load. Make sure the
  // first instruction in each case below is the one that loads.
  if (CpuFeatures::IsSupported(AVX2)) {
    CpuFeatureScope avx2_scope(this, AVX2);
    vpbroadcastw(dst, src);
  } else if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    // Avoid dependency on previous value of dst.
    vpinsrw(dst, scratch, src, uint8_t{0});
    vpshuflw(dst, dst, uint8_t{0});
    vpunpcklqdq(dst, dst, dst);
  } else {
    pinsrw(dst, src, uint8_t{0});
    pshuflw(dst, dst, uint8_t{0});
    movlhps(dst, dst);
  }
}

void SharedMacroAssemblerBase::S128Load32Splat(XMMRegister dst, Operand src) {
  ASM_CODE_COMMENT(this);
  // The trap handler uses the current pc to creating a landing, so that it can
  // determine if a trap occured in Wasm code due to a OOB load. Make sure the
  // first instruction in each case below is the one that loads.
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope avx_scope(this, AVX);
    vbroadcastss(dst, src);
  } else {
    movss(dst, src);
    shufps(dst, dst, uint8_t{0});
  }
}

void SharedMacroAssemblerBase::S128Store64Lane(Operand dst, XMMRegister src,
                                               uint8_t laneidx) {
  ASM_CODE_COMMENT(this);
  if (laneidx == 0) {
    Movlps(dst, src);
  } else {
    DCHECK_EQ(1, laneidx);
    Movhps(dst, src);
  }
}

// Helper macro to define qfma macro-assembler. This takes care of every
// possible case of register aliasing to minimize the number of instructions.
#define QFMA(ps_or_pd)                        \
  if (CpuFeatures::IsSupported(FMA3)) {       \
    CpuFeatureScope fma3_scope(this, FMA3);   \
    if (dst == src1) {                        \
      vfmadd213##ps_or_pd(dst, src2, src3);   \
    } else if (dst == src2) {                 \
      vfmadd213##ps_or_pd(dst, src1, src3);   \
    } else if (dst == src3) {                 \
      vfmadd231##ps_or_pd(dst, src2, src1);   \
    } else {                                  \
      CpuFeatureScope avx_scope(this, AVX);   \
      vmovups(dst, src1);                     \
      vfmadd213##ps_or_pd(dst, src2, src3);   \
    }                                         \
  } else if (CpuFeatures::IsSupported(AVX)) { \
    CpuFeatureScope avx_scope(this, AVX);     \
    vmul##ps_or_pd(tmp, src1, src2);          \
    vadd##ps_or_pd(dst, tmp, src3);           \
  } else {                                    \
    if (dst == src1) {                        \
      mul##ps_or_pd(dst, src2);               \
      add##ps_or_pd(dst, src3);               \
    } else if (dst == src2) {                 \
      DCHECK_NE(src2, src1);                  \
      mul##ps_or_pd(dst, src1);               \
      add##ps_or_pd(dst, src3);               \
    } else if (dst == src3) {                 \
      DCHECK_NE(src3, src1);                  \
      movaps(tmp, src1);                      \
      mul##ps_or_pd(tmp, src2);               \
      add##ps_or_pd(dst, tmp);                \
    } else {                                  \
      movaps(dst, src1);                      \
      mul##ps_or_pd(dst, src2);               \
      add##ps_or_pd(dst, src3);               \
    }                                         \
  }

// Helper macro to define qfms macro-assembler. This takes care of every
// possible case of register aliasing to minimize the number of instructions.
#define QFMS(ps_or_pd)                        \
  if (CpuFeatures::IsSupported(FMA3)) {       \
    CpuFeatureScope fma3_scope(this, FMA3);   \
    if (dst == src1) {                        \
      vfnmadd213##ps_or_pd(dst, src2, src3);  \
    } else if (dst == src2) {                 \
      vfnmadd213##ps_or_pd(dst, src1, src3);  \
    } else if (dst == src3) {                 \
      vfnmadd231##ps_or_pd(dst, src2, src1);  \
    } else {                                  \
      CpuFeatureScope avx_scope(this, AVX);   \
      vmovups(dst, src1);                     \
      vfnmadd213##ps_or_pd(dst, src2, src3);  \
    }                                         \
  } else if (CpuFeatures::IsSupported(AVX)) { \
    CpuFeatureScope avx_scope(this, AVX);     \
    vmul##ps_or_pd(tmp, src1, src2);          \
    vsub##ps_or_pd(dst, src3, tmp);           \
  } else {                                    \
    movaps(tmp, src1);                        \
    mul##ps_or_pd(tmp, src2);                 \
    if (dst != src3) {                        \
      movaps(dst, src3);                      \
    }                                         \
    sub##ps_or_pd(dst, tmp);                  \
  }

void SharedMacroAssemblerBase::F32x4Qfma(XMMRegister dst, XMMRegister src1,
                                         XMMRegister src2, XMMRegister src3,
                                         XMMRegister tmp) {
  QFMA(ps)
}

void SharedMacroAssemblerBase::F32x4Qfms(XMMRegister dst, XMMRegister src1,
                                         XMMRegister src2, XMMRegister src3,
                                         XMMRegister tmp) {
  QFMS(ps)
}

void SharedMacroAssemblerBase::F64x2Qfma(XMMRegister dst, XMMRegister src1,
                                         XMMRegister src2, XMMRegister src3,
                                         XMMRegister tmp) {
  QFMA(pd);
}

void SharedMacroAssemblerBase::F64x2Qfms(XMMRegister dst, XMMRegister src1,
                                         XMMRegister src2, XMMRegister src3,
                                         XMMRegister tmp) {
  QFMS(pd);
}

#undef QFMOP

}  // namespace internal
}  // namespace v8

#undef DCHECK_OPERAND_IS_NOT_REG
