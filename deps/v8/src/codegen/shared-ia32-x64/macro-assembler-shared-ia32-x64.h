// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_SHARED_IA32_X64_MACRO_ASSEMBLER_SHARED_IA32_X64_H_
#define V8_CODEGEN_SHARED_IA32_X64_MACRO_ASSEMBLER_SHARED_IA32_X64_H_

#include "src/base/macros.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/macro-assembler-base.h"

#if V8_TARGET_ARCH_IA32
#include "src/codegen/ia32/register-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/codegen/x64/register-x64.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {
class Assembler;

// For WebAssembly we care about the full floating point register. If we are not
// running Wasm, we can get away with saving half of those registers.
#if V8_ENABLE_WEBASSEMBLY
constexpr int kStackSavedSavedFPSize = 2 * kDoubleSize;
#else
constexpr int kStackSavedSavedFPSize = kDoubleSize;
#endif  // V8_ENABLE_WEBASSEMBLY

// Base class for SharedMacroAssembler. This class contains macro-assembler
// functions that can be shared across ia32 and x64 without any template
// machinery, i.e. does not require the CRTP pattern that
// SharedMacroAssembler exposes. This allows us to keep the bulk of
// definition inside a separate source file, rather than putting everything
// inside this header.
class V8_EXPORT_PRIVATE SharedMacroAssemblerBase : public MacroAssemblerBase {
 public:
  using MacroAssemblerBase::MacroAssemblerBase;

  void Move(Register dst, uint32_t src);
  // Move if registers are not identical.
  void Move(Register dst, Register src);
  void Add(Register dst, Immediate src);
  void And(Register dst, Immediate src);

  // Will move src1 to dst if AVX is not supported.
  void Movhps(XMMRegister dst, XMMRegister src1, Operand src2);
  void Movlps(XMMRegister dst, XMMRegister src1, Operand src2);

  void Blendvps(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                XMMRegister mask);
  void Blendvpd(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                XMMRegister mask);
  void Pblendvb(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                XMMRegister mask);

  template <typename Op>
  void Pinsrb(XMMRegister dst, XMMRegister src1, Op src2, uint8_t imm8,
              uint32_t* load_pc_offset = nullptr) {
    PinsrHelper(this, &Assembler::vpinsrb, &Assembler::pinsrb, dst, src1, src2,
                imm8, load_pc_offset, {SSE4_1});
  }

  template <typename Op>
  void Pinsrw(XMMRegister dst, XMMRegister src1, Op src2, uint8_t imm8,
              uint32_t* load_pc_offset = nullptr) {
    PinsrHelper(this, &Assembler::vpinsrw, &Assembler::pinsrw, dst, src1, src2,
                imm8, load_pc_offset);
  }

  // Supports both SSE and AVX. Move src1 to dst if they are not equal on SSE.
  template <typename Op>
  void Pshufb(XMMRegister dst, XMMRegister src, Op mask) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vpshufb(dst, src, mask);
    } else {
      // Make sure these are different so that we won't overwrite mask.
      DCHECK_NE(mask, dst);
      if (dst != src) {
        movaps(dst, src);
      }
      CpuFeatureScope sse_scope(this, SSSE3);
      pshufb(dst, mask);
    }
  }

  template <typename Op>
  void Pshufb(XMMRegister dst, Op mask) {
    Pshufb(dst, dst, mask);
  }

  // Shufps that will mov src1 into dst if AVX is not supported.
  void Shufps(XMMRegister dst, XMMRegister src1, XMMRegister src2,
              uint8_t imm8);

  // Helper struct to implement functions that check for AVX support and
  // dispatch to the appropriate AVX/SSE instruction.
  template <typename Dst, typename Arg, typename... Args>
  struct AvxHelper {
    Assembler* assm;
    base::Optional<CpuFeature> feature = base::nullopt;
    // Call a method where the AVX version expects the dst argument to be
    // duplicated.
    // E.g. Andps(x, y) -> vandps(x, x, y)
    //                  -> andps(x, y)
    template <void (Assembler::*avx)(Dst, Dst, Arg, Args...),
              void (Assembler::*no_avx)(Dst, Arg, Args...)>
    void emit(Dst dst, Arg arg, Args... args) {
      if (CpuFeatures::IsSupported(AVX)) {
        CpuFeatureScope scope(assm, AVX);
        (assm->*avx)(dst, dst, arg, args...);
      } else if (feature.has_value()) {
        DCHECK(CpuFeatures::IsSupported(*feature));
        CpuFeatureScope scope(assm, *feature);
        (assm->*no_avx)(dst, arg, args...);
      } else {
        (assm->*no_avx)(dst, arg, args...);
      }
    }

    // Call a method in the AVX form (one more operand), but if unsupported will
    // check that dst == first src.
    // E.g. Andps(x, y, z) -> vandps(x, y, z)
    //                     -> andps(x, z) and check that x == y
    template <void (Assembler::*avx)(Dst, Arg, Args...),
              void (Assembler::*no_avx)(Dst, Args...)>
    void emit(Dst dst, Arg arg, Args... args) {
      if (CpuFeatures::IsSupported(AVX)) {
        CpuFeatureScope scope(assm, AVX);
        (assm->*avx)(dst, arg, args...);
      } else if (feature.has_value()) {
        DCHECK_EQ(dst, arg);
        DCHECK(CpuFeatures::IsSupported(*feature));
        CpuFeatureScope scope(assm, *feature);
        (assm->*no_avx)(dst, args...);
      } else {
        DCHECK_EQ(dst, arg);
        (assm->*no_avx)(dst, args...);
      }
    }

    // Call a method where the AVX version expects no duplicated dst argument.
    // E.g. Movddup(x, y) -> vmovddup(x, y)
    //                    -> movddup(x, y)
    template <void (Assembler::*avx)(Dst, Arg, Args...),
              void (Assembler::*no_avx)(Dst, Arg, Args...)>
    void emit(Dst dst, Arg arg, Args... args) {
      if (CpuFeatures::IsSupported(AVX)) {
        CpuFeatureScope scope(assm, AVX);
        (assm->*avx)(dst, arg, args...);
      } else if (feature.has_value()) {
        DCHECK(CpuFeatures::IsSupported(*feature));
        CpuFeatureScope scope(assm, *feature);
        (assm->*no_avx)(dst, arg, args...);
      } else {
        (assm->*no_avx)(dst, arg, args...);
      }
    }
  };

#define AVX_OP(macro_name, name)                                        \
  template <typename Dst, typename Arg, typename... Args>               \
  void macro_name(Dst dst, Arg arg, Args... args) {                     \
    AvxHelper<Dst, Arg, Args...>{this}                                  \
        .template emit<&Assembler::v##name, &Assembler::name>(dst, arg, \
                                                              args...); \
  }

// Define a macro which uses |avx_name| when AVX is supported, and |sse_name|
// when AVX is not supported. This is useful for bit-wise instructions like
// andpd/andps, where the behavior is exactly the same, but the *ps
// version is 1 byte shorter, and on SSE-only processors there is no
// performance difference since those processors don't differentiate integer
// and floating-point domains.
// Note: we require |avx_name| to be the AVX instruction without the "v"
// prefix. If we require the full AVX instruction name and the caller
// accidentally passes in a SSE instruction, we compile without any issues and
// generate the SSE instruction. By appending "v" here, we ensure that we will
// generate an AVX instruction.
#define AVX_OP_WITH_DIFF_SSE_INSTR(macro_name, avx_name, sse_name)     \
  template <typename Dst, typename Arg, typename... Args>              \
  void macro_name(Dst dst, Arg arg, Args... args) {                    \
    AvxHelper<Dst, Arg, Args...>{this}                                 \
        .template emit<&Assembler::v##avx_name, &Assembler::sse_name>( \
            dst, arg, args...);                                        \
  }

#define AVX_OP_SSE3(macro_name, name)                                    \
  template <typename Dst, typename Arg, typename... Args>                \
  void macro_name(Dst dst, Arg arg, Args... args) {                      \
    AvxHelper<Dst, Arg, Args...>{this, base::Optional<CpuFeature>(SSE3)} \
        .template emit<&Assembler::v##name, &Assembler::name>(dst, arg,  \
                                                              args...);  \
  }

#define AVX_OP_SSSE3(macro_name, name)                                    \
  template <typename Dst, typename Arg, typename... Args>                 \
  void macro_name(Dst dst, Arg arg, Args... args) {                       \
    AvxHelper<Dst, Arg, Args...>{this, base::Optional<CpuFeature>(SSSE3)} \
        .template emit<&Assembler::v##name, &Assembler::name>(dst, arg,   \
                                                              args...);   \
  }

#define AVX_OP_SSE4_1(macro_name, name)                                    \
  template <typename Dst, typename Arg, typename... Args>                  \
  void macro_name(Dst dst, Arg arg, Args... args) {                        \
    AvxHelper<Dst, Arg, Args...>{this, base::Optional<CpuFeature>(SSE4_1)} \
        .template emit<&Assembler::v##name, &Assembler::name>(dst, arg,    \
                                                              args...);    \
  }

#define AVX_OP_SSE4_2(macro_name, name)                                    \
  template <typename Dst, typename Arg, typename... Args>                  \
  void macro_name(Dst dst, Arg arg, Args... args) {                        \
    AvxHelper<Dst, Arg, Args...>{this, base::Optional<CpuFeature>(SSE4_2)} \
        .template emit<&Assembler::v##name, &Assembler::name>(dst, arg,    \
                                                              args...);    \
  }

  // Keep this list sorted by required extension, then instruction name.
  AVX_OP(Addpd, addpd)
  AVX_OP(Addps, addps)
  AVX_OP(Addsd, addsd)
  AVX_OP(Addss, addss)
  AVX_OP(Andnpd, andnpd)
  AVX_OP(Andnps, andnps)
  AVX_OP(Andpd, andpd)
  AVX_OP(Andps, andps)
  AVX_OP(Cmpeqpd, cmpeqpd)
  AVX_OP(Cmpeqps, cmpeqps)
  AVX_OP(Cmplepd, cmplepd)
  AVX_OP(Cmpleps, cmpleps)
  AVX_OP(Cmpltpd, cmpltpd)
  AVX_OP(Cmpltps, cmpltps)
  AVX_OP(Cmpneqpd, cmpneqpd)
  AVX_OP(Cmpneqps, cmpneqps)
  AVX_OP(Cmpunordpd, cmpunordpd)
  AVX_OP(Cmpunordps, cmpunordps)
  AVX_OP(Cvtdq2pd, cvtdq2pd)
  AVX_OP(Cvtdq2ps, cvtdq2ps)
  AVX_OP(Cvtpd2ps, cvtpd2ps)
  AVX_OP(Cvtps2pd, cvtps2pd)
  AVX_OP(Cvtsd2ss, cvtsd2ss)
  AVX_OP(Cvtss2sd, cvtss2sd)
  AVX_OP(Cvttpd2dq, cvttpd2dq)
  AVX_OP(Cvttps2dq, cvttps2dq)
  AVX_OP(Cvttsd2si, cvttsd2si)
  AVX_OP(Cvttss2si, cvttss2si)
  AVX_OP(Divpd, divpd)
  AVX_OP(Divps, divps)
  AVX_OP(Divsd, divsd)
  AVX_OP(Divss, divss)
  AVX_OP(Maxpd, maxpd)
  AVX_OP(Maxps, maxps)
  AVX_OP(Minpd, minpd)
  AVX_OP(Minps, minps)
  AVX_OP(Movaps, movaps)
  AVX_OP(Movd, movd)
  AVX_OP(Movhlps, movhlps)
  AVX_OP(Movhps, movhps)
  AVX_OP(Movlps, movlps)
  AVX_OP(Movmskpd, movmskpd)
  AVX_OP(Movmskps, movmskps)
  AVX_OP(Movsd, movsd)
  AVX_OP(Movss, movss)
  AVX_OP(Movupd, movupd)
  AVX_OP(Movups, movups)
  AVX_OP(Mulpd, mulpd)
  AVX_OP(Mulps, mulps)
  AVX_OP(Mulsd, mulsd)
  AVX_OP(Mulss, mulss)
  AVX_OP(Orpd, orpd)
  AVX_OP(Orps, orps)
  AVX_OP(Packssdw, packssdw)
  AVX_OP(Packsswb, packsswb)
  AVX_OP(Packuswb, packuswb)
  AVX_OP(Paddb, paddb)
  AVX_OP(Paddd, paddd)
  AVX_OP(Paddq, paddq)
  AVX_OP(Paddsb, paddsb)
  AVX_OP(Paddsw, paddsw)
  AVX_OP(Paddusb, paddusb)
  AVX_OP(Paddusw, paddusw)
  AVX_OP(Paddw, paddw)
  AVX_OP(Pavgb, pavgb)
  AVX_OP(Pavgw, pavgw)
  AVX_OP(Pcmpgtb, pcmpgtb)
  AVX_OP(Pcmpgtd, pcmpgtd)
  AVX_OP(Pcmpgtw, pcmpgtw)
  AVX_OP(Pcmpeqb, pcmpeqb)
  AVX_OP(Pcmpeqd, pcmpeqd)
  AVX_OP(Pcmpeqw, pcmpeqw)
  AVX_OP(Pmaddwd, pmaddwd)
  AVX_OP(Pmaxsw, pmaxsw)
  AVX_OP(Pmaxub, pmaxub)
  AVX_OP(Pminsw, pminsw)
  AVX_OP(Pminub, pminub)
  AVX_OP(Pmovmskb, pmovmskb)
  AVX_OP(Pmullw, pmullw)
  AVX_OP(Pmuludq, pmuludq)
  AVX_OP(Pshufd, pshufd)
  AVX_OP(Pshufhw, pshufhw)
  AVX_OP(Pshuflw, pshuflw)
  AVX_OP(Pslld, pslld)
  AVX_OP(Psllq, psllq)
  AVX_OP(Psllw, psllw)
  AVX_OP(Psrad, psrad)
  AVX_OP(Psraw, psraw)
  AVX_OP(Psrld, psrld)
  AVX_OP(Psrlq, psrlq)
  AVX_OP(Psrlw, psrlw)
  AVX_OP(Psubb, psubb)
  AVX_OP(Psubd, psubd)
  AVX_OP(Psubq, psubq)
  AVX_OP(Psubsb, psubsb)
  AVX_OP(Psubsw, psubsw)
  AVX_OP(Psubusb, psubusb)
  AVX_OP(Psubusw, psubusw)
  AVX_OP(Psubw, psubw)
  AVX_OP(Punpckhbw, punpckhbw)
  AVX_OP(Punpckhdq, punpckhdq)
  AVX_OP(Punpckhqdq, punpckhqdq)
  AVX_OP(Punpckhwd, punpckhwd)
  AVX_OP(Punpcklbw, punpcklbw)
  AVX_OP(Punpckldq, punpckldq)
  AVX_OP(Punpcklqdq, punpcklqdq)
  AVX_OP(Punpcklwd, punpcklwd)
  AVX_OP(Rcpps, rcpps)
  AVX_OP(Rsqrtps, rsqrtps)
  AVX_OP(Sqrtpd, sqrtpd)
  AVX_OP(Sqrtps, sqrtps)
  AVX_OP(Sqrtsd, sqrtsd)
  AVX_OP(Sqrtss, sqrtss)
  AVX_OP(Subpd, subpd)
  AVX_OP(Subps, subps)
  AVX_OP(Subsd, subsd)
  AVX_OP(Subss, subss)
  AVX_OP(Ucomisd, ucomisd)
  AVX_OP(Ucomiss, ucomiss)
  AVX_OP(Unpcklps, unpcklps)
  AVX_OP(Xorpd, xorpd)
  AVX_OP(Xorps, xorps)

  // Many AVX processors have separate integer/floating-point domains, so use
  // vmovaps if AVX is supported. On SSE, movaps is 1 byte shorter than movdqa,
  // and has the same behavior. Most SSE processors also don't have the same
  // delay moving between integer and floating-point domains.
  AVX_OP_WITH_DIFF_SSE_INSTR(Movapd, movapd, movaps)
  AVX_OP_WITH_DIFF_SSE_INSTR(Movdqa, movdqa, movaps)
  AVX_OP_WITH_DIFF_SSE_INSTR(Movdqu, movdqu, movups)
  AVX_OP_WITH_DIFF_SSE_INSTR(Pand, pand, andps)
  AVX_OP_WITH_DIFF_SSE_INSTR(Por, por, orps)
  AVX_OP_WITH_DIFF_SSE_INSTR(Pxor, pxor, xorps)

  AVX_OP_SSE3(Haddps, haddps)
  AVX_OP_SSE3(Movddup, movddup)
  AVX_OP_SSE3(Movshdup, movshdup)

  AVX_OP_SSSE3(Pabsb, pabsb)
  AVX_OP_SSSE3(Pabsd, pabsd)
  AVX_OP_SSSE3(Pabsw, pabsw)
  AVX_OP_SSSE3(Palignr, palignr)
  AVX_OP_SSSE3(Pmulhrsw, pmulhrsw)
  AVX_OP_SSSE3(Psignb, psignb)
  AVX_OP_SSSE3(Psignd, psignd)
  AVX_OP_SSSE3(Psignw, psignw)

  AVX_OP_SSE4_1(Extractps, extractps)
  AVX_OP_SSE4_1(Insertps, insertps)
  AVX_OP_SSE4_1(Packusdw, packusdw)
  AVX_OP_SSE4_1(Pblendw, pblendw)
  AVX_OP_SSE4_1(Pcmpeqq, pcmpeqq)
  AVX_OP_SSE4_1(Pextrb, pextrb)
  AVX_OP_SSE4_1(Pextrw, pextrw)
  AVX_OP_SSE4_1(Pmaxsb, pmaxsb)
  AVX_OP_SSE4_1(Pmaxsd, pmaxsd)
  AVX_OP_SSE4_1(Pmaxud, pmaxud)
  AVX_OP_SSE4_1(Pmaxuw, pmaxuw)
  AVX_OP_SSE4_1(Pminsb, pminsb)
  AVX_OP_SSE4_1(Pminsd, pminsd)
  AVX_OP_SSE4_1(Pminud, pminud)
  AVX_OP_SSE4_1(Pminuw, pminuw)
  AVX_OP_SSE4_1(Pmovsxbw, pmovsxbw)
  AVX_OP_SSE4_1(Pmovsxdq, pmovsxdq)
  AVX_OP_SSE4_1(Pmovsxwd, pmovsxwd)
  AVX_OP_SSE4_1(Pmovzxbw, pmovzxbw)
  AVX_OP_SSE4_1(Pmovzxbd, pmovzxbd)
  AVX_OP_SSE4_1(Pmovzxdq, pmovzxdq)
  AVX_OP_SSE4_1(Pmovzxwd, pmovzxwd)
  AVX_OP_SSE4_1(Pmulld, pmulld)
  AVX_OP_SSE4_1(Ptest, ptest)
  AVX_OP_SSE4_1(Roundpd, roundpd)
  AVX_OP_SSE4_1(Roundps, roundps)
  AVX_OP_SSE4_1(Roundsd, roundsd)
  AVX_OP_SSE4_1(Roundss, roundss)

#undef AVX_OP
#undef AVX_OP_SSE3
#undef AVX_OP_SSSE3
#undef AVX_OP_SSE4_1
#undef AVX_OP_SSE4_2

  void F64x2ExtractLane(DoubleRegister dst, XMMRegister src, uint8_t lane);
  void F64x2ReplaceLane(XMMRegister dst, XMMRegister src, DoubleRegister rep,
                        uint8_t lane);
  void F64x2Min(XMMRegister dst, XMMRegister lhs, XMMRegister rhs,
                XMMRegister scratch);
  void F64x2Max(XMMRegister dst, XMMRegister lhs, XMMRegister rhs,
                XMMRegister scratch);
  void F32x4Splat(XMMRegister dst, DoubleRegister src);
  void F32x4ExtractLane(FloatRegister dst, XMMRegister src, uint8_t lane);
  void F32x4Min(XMMRegister dst, XMMRegister lhs, XMMRegister rhs,
                XMMRegister scratch);
  void F32x4Max(XMMRegister dst, XMMRegister lhs, XMMRegister rhs,
                XMMRegister scratch);
  void S128Store32Lane(Operand dst, XMMRegister src, uint8_t laneidx);
  void I8x16Splat(XMMRegister dst, Register src, XMMRegister scratch);
  void I8x16Splat(XMMRegister dst, Operand src, XMMRegister scratch);
  void I8x16Shl(XMMRegister dst, XMMRegister src1, uint8_t src2, Register tmp1,
                XMMRegister tmp2);
  void I8x16Shl(XMMRegister dst, XMMRegister src1, Register src2, Register tmp1,
                XMMRegister tmp2, XMMRegister tmp3);
  void I8x16ShrS(XMMRegister dst, XMMRegister src1, uint8_t src2,
                 XMMRegister tmp);
  void I8x16ShrS(XMMRegister dst, XMMRegister src1, Register src2,
                 Register tmp1, XMMRegister tmp2, XMMRegister tmp3);
  void I8x16ShrU(XMMRegister dst, XMMRegister src1, uint8_t src2, Register tmp1,
                 XMMRegister tmp2);
  void I8x16ShrU(XMMRegister dst, XMMRegister src1, Register src2,
                 Register tmp1, XMMRegister tmp2, XMMRegister tmp3);
  void I16x8Splat(XMMRegister dst, Register src);
  void I16x8Splat(XMMRegister dst, Operand src);
  void I16x8ExtMulLow(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                      XMMRegister scrat, bool is_signed);
  void I16x8ExtMulHighS(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                        XMMRegister scratch);
  void I16x8ExtMulHighU(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                        XMMRegister scratch);
  void I16x8SConvertI8x16High(XMMRegister dst, XMMRegister src);
  void I16x8UConvertI8x16High(XMMRegister dst, XMMRegister src,
                              XMMRegister scratch);
  // Will move src1 to dst if AVX is not supported.
  void I16x8Q15MulRSatS(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                        XMMRegister scratch);
  void I16x8DotI8x16I7x16S(XMMRegister dst, XMMRegister src1, XMMRegister src2);
  void I32x4DotI8x16I7x16AddS(XMMRegister dst, XMMRegister src1,
                              XMMRegister src2, XMMRegister src3,
                              XMMRegister scratch, XMMRegister splat_reg);
  void I32x4ExtAddPairwiseI16x8U(XMMRegister dst, XMMRegister src,
                                 XMMRegister tmp);
  // Requires that dst == src1 if AVX is not supported.
  void I32x4ExtMul(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                   XMMRegister scratch, bool low, bool is_signed);
  void I32x4SConvertI16x8High(XMMRegister dst, XMMRegister src);
  void I32x4UConvertI16x8High(XMMRegister dst, XMMRegister src,
                              XMMRegister scratch);
  void I64x2Neg(XMMRegister dst, XMMRegister src, XMMRegister scratch);
  void I64x2Abs(XMMRegister dst, XMMRegister src, XMMRegister scratch);
  void I64x2GtS(XMMRegister dst, XMMRegister src0, XMMRegister src1,
                XMMRegister scratch);
  void I64x2GeS(XMMRegister dst, XMMRegister src0, XMMRegister src1,
                XMMRegister scratch);
  void I64x2ShrS(XMMRegister dst, XMMRegister src, uint8_t shift,
                 XMMRegister xmm_tmp);
  void I64x2ShrS(XMMRegister dst, XMMRegister src, Register shift,
                 XMMRegister xmm_tmp, XMMRegister xmm_shift,
                 Register tmp_shift);
  void I64x2Mul(XMMRegister dst, XMMRegister lhs, XMMRegister rhs,
                XMMRegister tmp1, XMMRegister tmp2);
  void I64x2ExtMul(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                   XMMRegister scratch, bool low, bool is_signed);
  void I64x2SConvertI32x4High(XMMRegister dst, XMMRegister src);
  void I64x2UConvertI32x4High(XMMRegister dst, XMMRegister src,
                              XMMRegister scratch);
  void S128Not(XMMRegister dst, XMMRegister src, XMMRegister scratch);
  // Requires dst == mask when AVX is not supported.
  void S128Select(XMMRegister dst, XMMRegister mask, XMMRegister src1,
                  XMMRegister src2, XMMRegister scratch);
  void S128Load8Splat(XMMRegister dst, Operand src, XMMRegister scratch);
  void S128Load16Splat(XMMRegister dst, Operand src, XMMRegister scratch);
  void S128Load32Splat(XMMRegister dst, Operand src);
  void S128Store64Lane(Operand dst, XMMRegister src, uint8_t laneidx);

  void F64x2Qfma(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                 XMMRegister src3, XMMRegister tmp);
  void F64x2Qfms(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                 XMMRegister src3, XMMRegister tmp);
  void F32x4Qfma(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                 XMMRegister src3, XMMRegister tmp);
  void F32x4Qfms(XMMRegister dst, XMMRegister src1, XMMRegister src2,
                 XMMRegister src3, XMMRegister tmp);

 protected:
  template <typename Op>
  using AvxFn = void (Assembler::*)(XMMRegister, XMMRegister, Op, uint8_t);
  template <typename Op>
  using NoAvxFn = void (Assembler::*)(XMMRegister, Op, uint8_t);

  template <typename Op>
  void PinsrHelper(Assembler* assm, AvxFn<Op> avx, NoAvxFn<Op> noavx,
                   XMMRegister dst, XMMRegister src1, Op src2, uint8_t imm8,
                   uint32_t* load_pc_offset = nullptr,
                   base::Optional<CpuFeature> feature = base::nullopt) {
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope scope(assm, AVX);
      if (load_pc_offset) *load_pc_offset = assm->pc_offset();
      (assm->*avx)(dst, src1, src2, imm8);
      return;
    }

    if (dst != src1) assm->movaps(dst, src1);
    if (load_pc_offset) *load_pc_offset = assm->pc_offset();
    if (feature.has_value()) {
      DCHECK(CpuFeatures::IsSupported(*feature));
      CpuFeatureScope scope(assm, *feature);
      (assm->*noavx)(dst, src2, imm8);
    } else {
      (assm->*noavx)(dst, src2, imm8);
    }
  }

 private:
  template <typename Op>
  void I8x16SplatPreAvx2(XMMRegister dst, Op src, XMMRegister scratch);
  template <typename Op>
  void I16x8SplatPreAvx2(XMMRegister dst, Op src);
};

// Common base class template shared by ia32 and x64 MacroAssembler. This uses
// the Curiously Recurring Template Pattern (CRTP), where Impl is the actual
// class (subclass of SharedMacroAssembler instantiated with the actual
// class). This allows static polymorphism, where member functions can be move
// into SharedMacroAssemblerBase, and we can also call into member functions
// defined in ia32 or x64 specific MacroAssembler from within this template
// class, via Impl.
//
// Note: all member functions must be defined in this header file so that the
// compiler can generate code for the function definitions. See
// https://isocpp.org/wiki/faq/templates#templates-defn-vs-decl for rationale.
// If a function does not need polymorphism, move it into
// SharedMacroAssemblerBase, and define it outside of this header.
template <typename Impl>
class V8_EXPORT_PRIVATE SharedMacroAssembler : public SharedMacroAssemblerBase {
  using SharedMacroAssemblerBase::SharedMacroAssemblerBase;

 public:
  void Abspd(XMMRegister dst, XMMRegister src, Register tmp) {
    FloatUnop(dst, src, tmp, &SharedMacroAssemblerBase::Andps,
              ExternalReference::address_of_double_abs_constant());
  }

  void Absps(XMMRegister dst, XMMRegister src, Register tmp) {
    FloatUnop(dst, src, tmp, &SharedMacroAssemblerBase::Andps,
              ExternalReference::address_of_float_abs_constant());
  }

  void Negpd(XMMRegister dst, XMMRegister src, Register tmp) {
    FloatUnop(dst, src, tmp, &SharedMacroAssemblerBase::Xorps,
              ExternalReference::address_of_double_neg_constant());
  }

  void Negps(XMMRegister dst, XMMRegister src, Register tmp) {
    FloatUnop(dst, src, tmp, &SharedMacroAssemblerBase::Xorps,
              ExternalReference::address_of_float_neg_constant());
  }
#undef FLOAT_UNOP

  void Pextrd(Register dst, XMMRegister src, uint8_t imm8) {
    if (imm8 == 0) {
      Movd(dst, src);
      return;
    }

    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope scope(this, AVX);
      vpextrd(dst, src, imm8);
    } else if (CpuFeatures::IsSupported(SSE4_1)) {
      CpuFeatureScope sse_scope(this, SSE4_1);
      pextrd(dst, src, imm8);
    } else {
      DCHECK_LT(imm8, 2);
      impl()->PextrdPreSse41(dst, src, imm8);
    }
  }

  template <typename Op>
  void Pinsrd(XMMRegister dst, XMMRegister src1, Op src2, uint8_t imm8,
              uint32_t* load_pc_offset = nullptr) {
    if (CpuFeatures::IsSupported(SSE4_1)) {
      PinsrHelper(this, &Assembler::vpinsrd, &Assembler::pinsrd, dst, src1,
                  src2, imm8, load_pc_offset,
                  base::Optional<CpuFeature>(SSE4_1));
    } else {
      if (dst != src1) {
        movaps(dst, src1);
      }
      impl()->PinsrdPreSse41(dst, src2, imm8, load_pc_offset);
    }
  }

  template <typename Op>
  void Pinsrd(XMMRegister dst, Op src, uint8_t imm8,
              uint32_t* load_pc_offset = nullptr) {
    Pinsrd(dst, dst, src, imm8, load_pc_offset);
  }

  void F64x2ConvertLowI32x4U(XMMRegister dst, XMMRegister src,
                             Register scratch) {
    ASM_CODE_COMMENT(this);
    // dst = [ src_low, 0x43300000, src_high, 0x4330000 ];
    // 0x43300000'00000000 is a special double where the significand bits
    // precisely represents all uint32 numbers.
    if (!CpuFeatures::IsSupported(AVX) && dst != src) {
      movaps(dst, src);
      src = dst;
    }
    Unpcklps(dst, src,
             ExternalReferenceAsOperand(
                 ExternalReference::
                     address_of_wasm_f64x2_convert_low_i32x4_u_int_mask(),
                 scratch));
    Subpd(dst,
          ExternalReferenceAsOperand(
              ExternalReference::address_of_wasm_double_2_power_52(), scratch));
  }

  void I32x4SConvertF32x4(XMMRegister dst, XMMRegister src, XMMRegister tmp,
                          Register scratch) {
    ASM_CODE_COMMENT(this);
    Operand op = ExternalReferenceAsOperand(
        ExternalReference::address_of_wasm_int32_overflow_as_float(), scratch);

    // This algorithm works by:
    // 1. lanes with NaNs are zero-ed
    // 2. lanes ge than 2147483648.0f (MAX_INT32+1) set to 0xffff'ffff
    // 3. cvttps2dq sets all out of range lanes to 0x8000'0000
    //   a. correct for underflows (< MIN_INT32)
    //   b. wrong for overflow, and we know which lanes overflow from 2.
    // 4. adjust for 3b by xor-ing 2 and 3
    //   a. 0x8000'0000 xor 0xffff'ffff = 0x7fff'ffff (MAX_INT32)
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope scope(this, AVX);
      vcmpeqps(tmp, src, src);
      vandps(dst, src, tmp);
      vcmpgeps(tmp, src, op);
      vcvttps2dq(dst, dst);
      vpxor(dst, dst, tmp);
    } else {
      if (src == dst) {
        movaps(tmp, src);
        cmpeqps(tmp, tmp);
        andps(dst, tmp);
        movaps(tmp, op);
        cmpleps(tmp, dst);
        cvttps2dq(dst, dst);
        xorps(dst, tmp);
      } else {
        movaps(tmp, op);
        cmpleps(tmp, src);
        cvttps2dq(dst, src);
        xorps(dst, tmp);
        movaps(tmp, src);
        cmpeqps(tmp, tmp);
        andps(dst, tmp);
      }
    }
  }

  void I32x4TruncSatF64x2SZero(XMMRegister dst, XMMRegister src,
                               XMMRegister scratch, Register tmp) {
    ASM_CODE_COMMENT(this);
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      XMMRegister original_dst = dst;
      // Make sure we don't overwrite src.
      if (dst == src) {
        DCHECK_NE(src, scratch);
        dst = scratch;
      }
      // dst = 0 if src == NaN, else all ones.
      vcmpeqpd(dst, src, src);
      // dst = 0 if src == NaN, else INT32_MAX as double.
      vandpd(
          dst, dst,
          ExternalReferenceAsOperand(
              ExternalReference::address_of_wasm_int32_max_as_double(), tmp));
      // dst = 0 if src == NaN, src is saturated to INT32_MAX as double.
      vminpd(dst, src, dst);
      // Values > INT32_MAX already saturated, values < INT32_MIN raises an
      // exception, which is masked and returns 0x80000000.
      vcvttpd2dq(original_dst, dst);
    } else {
      if (dst != src) {
        movaps(dst, src);
      }
      movaps(scratch, dst);
      cmpeqpd(scratch, dst);
      andps(scratch,
            ExternalReferenceAsOperand(
                ExternalReference::address_of_wasm_int32_max_as_double(), tmp));
      minpd(dst, scratch);
      cvttpd2dq(dst, dst);
    }
  }

  void I32x4TruncSatF64x2UZero(XMMRegister dst, XMMRegister src,
                               XMMRegister scratch, Register tmp) {
    ASM_CODE_COMMENT(this);
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vxorpd(scratch, scratch, scratch);
      // Saturate to 0.
      vmaxpd(dst, src, scratch);
      // Saturate to UINT32_MAX.
      vminpd(
          dst, dst,
          ExternalReferenceAsOperand(
              ExternalReference::address_of_wasm_uint32_max_as_double(), tmp));
      // Truncate.
      vroundpd(dst, dst, kRoundToZero);
      // Add to special double where significant bits == uint32.
      vaddpd(dst, dst,
             ExternalReferenceAsOperand(
                 ExternalReference::address_of_wasm_double_2_power_52(), tmp));
      // Extract low 32 bits of each double's significand, zero top lanes.
      // dst = [dst[0], dst[2], 0, 0]
      vshufps(dst, dst, scratch, 0x88);
    } else {
      CpuFeatureScope scope(this, SSE4_1);
      if (dst != src) {
        movaps(dst, src);
      }
      xorps(scratch, scratch);
      maxpd(dst, scratch);
      minpd(dst, ExternalReferenceAsOperand(
                     ExternalReference::address_of_wasm_uint32_max_as_double(),
                     tmp));
      roundpd(dst, dst, kRoundToZero);
      addpd(dst,
            ExternalReferenceAsOperand(
                ExternalReference::address_of_wasm_double_2_power_52(), tmp));
      shufps(dst, scratch, 0x88);
    }
  }

  void I32x4TruncF32x4U(XMMRegister dst, XMMRegister src, XMMRegister scratch1,
                        XMMRegister scratch2) {
    // NAN->0, negative->0.
    Pxor(scratch1, scratch1);
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope scope(this, AVX);
      vmaxps(dst, src, scratch1);
    } else {
      if (dst != src) movaps(dst, src);
      maxps(dst, scratch1);
    }
    // scratch: float representation of max_signed.
    Pcmpeqd(scratch1, scratch1);
    Psrld(scratch1, uint8_t{1});   // 0x7fffffff
    Cvtdq2ps(scratch1, scratch1);  // 0x4f000000
    // scratch2: convert (src-max_signed).
    // Set positive overflow lanes to 0x7FFFFFFF.
    // Set negative lanes to 0.
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope scope(this, AVX);
      vsubps(scratch2, dst, scratch1);
    } else {
      movaps(scratch2, dst);
      subps(scratch2, scratch1);
    }
    Cmpleps(scratch1, scratch2);
    Cvttps2dq(scratch2, scratch2);
    Pxor(scratch2, scratch1);
    Pxor(scratch1, scratch1);
    Pmaxsd(scratch2, scratch1);
    // Convert to int. Overflow lanes above max_signed will be 0x80000000.
    Cvttps2dq(dst, dst);
    // Add (src-max_signed) for overflow lanes.
    Paddd(dst, scratch2);
  }

  void I32x4ExtAddPairwiseI16x8S(XMMRegister dst, XMMRegister src,
                                 Register scratch) {
    ASM_CODE_COMMENT(this);
    Operand op = ExternalReferenceAsOperand(
        ExternalReference::address_of_wasm_i16x8_splat_0x0001(), scratch);
    // pmaddwd multiplies signed words in src and op, producing
    // signed doublewords, then adds pairwise.
    // src = |a|b|c|d|e|f|g|h|
    // dst = | a*1 + b*1 | c*1 + d*1 | e*1 + f*1 | g*1 + h*1 |
    if (!CpuFeatures::IsSupported(AVX) && (dst != src)) {
      movaps(dst, src);
      src = dst;
    }

    Pmaddwd(dst, src, op);
  }

  void I16x8ExtAddPairwiseI8x16S(XMMRegister dst, XMMRegister src,
                                 XMMRegister scratch, Register tmp) {
    ASM_CODE_COMMENT(this);
    // pmaddubsw treats the first operand as unsigned, so pass the external
    // reference to it as the first operand.
    Operand op = ExternalReferenceAsOperand(
        ExternalReference::address_of_wasm_i8x16_splat_0x01(), tmp);
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vmovdqa(scratch, op);
      vpmaddubsw(dst, scratch, src);
    } else {
      CpuFeatureScope sse_scope(this, SSSE3);
      if (dst == src) {
        movaps(scratch, op);
        pmaddubsw(scratch, src);
        movaps(dst, scratch);
      } else {
        movaps(dst, op);
        pmaddubsw(dst, src);
      }
    }
  }

  void I16x8ExtAddPairwiseI8x16U(XMMRegister dst, XMMRegister src,
                                 Register scratch) {
    ASM_CODE_COMMENT(this);
    Operand op = ExternalReferenceAsOperand(
        ExternalReference::address_of_wasm_i8x16_splat_0x01(), scratch);
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vpmaddubsw(dst, src, op);
    } else {
      CpuFeatureScope sse_scope(this, SSSE3);
      if (dst != src) {
        movaps(dst, src);
      }
      pmaddubsw(dst, op);
    }
  }

  void I8x16Swizzle(XMMRegister dst, XMMRegister src, XMMRegister mask,
                    XMMRegister scratch, Register tmp, bool omit_add = false) {
    ASM_CODE_COMMENT(this);
    if (omit_add) {
      // We have determined that the indices are immediates, and they are either
      // within bounds, or the top bit is set, so we can omit the add.
      Pshufb(dst, src, mask);
      return;
    }

    // Out-of-range indices should return 0, add 112 so that any value > 15
    // saturates to 128 (top bit set), so pshufb will zero that lane.
    Operand op = ExternalReferenceAsOperand(
        ExternalReference::address_of_wasm_i8x16_swizzle_mask(), tmp);
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vpaddusb(scratch, mask, op);
      vpshufb(dst, src, scratch);
    } else {
      CpuFeatureScope sse_scope(this, SSSE3);
      movaps(scratch, op);
      if (dst != src) {
        DCHECK_NE(dst, mask);
        movaps(dst, src);
      }
      paddusb(scratch, mask);
      pshufb(dst, scratch);
    }
  }

  void I8x16Popcnt(XMMRegister dst, XMMRegister src, XMMRegister tmp1,
                   XMMRegister tmp2, Register scratch) {
    ASM_CODE_COMMENT(this);
    DCHECK_NE(dst, tmp1);
    DCHECK_NE(src, tmp1);
    DCHECK_NE(dst, tmp2);
    DCHECK_NE(src, tmp2);
    if (CpuFeatures::IsSupported(AVX)) {
      CpuFeatureScope avx_scope(this, AVX);
      vmovdqa(tmp1, ExternalReferenceAsOperand(
                        ExternalReference::address_of_wasm_i8x16_splat_0x0f(),
                        scratch));
      vpandn(tmp2, tmp1, src);
      vpand(dst, tmp1, src);
      vmovdqa(tmp1, ExternalReferenceAsOperand(
                        ExternalReference::address_of_wasm_i8x16_popcnt_mask(),
                        scratch));
      vpsrlw(tmp2, tmp2, 4);
      vpshufb(dst, tmp1, dst);
      vpshufb(tmp2, tmp1, tmp2);
      vpaddb(dst, dst, tmp2);
    } else if (CpuFeatures::IsSupported(INTEL_ATOM)) {
      // Pre-Goldmont low-power Intel microarchitectures have very slow
      // PSHUFB instruction, thus use PSHUFB-free divide-and-conquer
      // algorithm on these processors. ATOM CPU feature captures exactly
      // the right set of processors.
      movaps(tmp1, src);
      psrlw(tmp1, 1);
      if (dst != src) {
        movaps(dst, src);
      }
      andps(tmp1, ExternalReferenceAsOperand(
                      ExternalReference::address_of_wasm_i8x16_splat_0x55(),
                      scratch));
      psubb(dst, tmp1);
      Operand splat_0x33 = ExternalReferenceAsOperand(
          ExternalReference::address_of_wasm_i8x16_splat_0x33(), scratch);
      movaps(tmp1, dst);
      andps(dst, splat_0x33);
      psrlw(tmp1, 2);
      andps(tmp1, splat_0x33);
      paddb(dst, tmp1);
      movaps(tmp1, dst);
      psrlw(dst, 4);
      paddb(dst, tmp1);
      andps(dst, ExternalReferenceAsOperand(
                     ExternalReference::address_of_wasm_i8x16_splat_0x0f(),
                     scratch));
    } else {
      CpuFeatureScope sse_scope(this, SSSE3);
      movaps(tmp1, ExternalReferenceAsOperand(
                       ExternalReference::address_of_wasm_i8x16_splat_0x0f(),
                       scratch));
      Operand mask = ExternalReferenceAsOperand(
          ExternalReference::address_of_wasm_i8x16_popcnt_mask(), scratch);
      if (tmp2 != tmp1) {
        movaps(tmp2, tmp1);
      }
      andps(tmp1, src);
      andnps(tmp2, src);
      psrlw(tmp2, 4);
      movaps(dst, mask);
      pshufb(dst, tmp1);
      movaps(tmp1, mask);
      pshufb(tmp1, tmp2);
      paddb(dst, tmp1);
    }
  }

 private:
  // All implementation-specific methods must be called through this.
  Impl* impl() { return static_cast<Impl*>(this); }

  Operand ExternalReferenceAsOperand(ExternalReference reference,
                                     Register scratch) {
    return impl()->ExternalReferenceAsOperand(reference, scratch);
  }

  using FloatInstruction = void (SharedMacroAssemblerBase::*)(XMMRegister,
                                                              XMMRegister,
                                                              Operand);
  void FloatUnop(XMMRegister dst, XMMRegister src, Register tmp,
                 FloatInstruction op, ExternalReference ext) {
    if (!CpuFeatures::IsSupported(AVX) && (dst != src)) {
      movaps(dst, src);
      src = dst;
    }
    SharedMacroAssemblerBase* assm = this;
    (assm->*op)(dst, src, ExternalReferenceAsOperand(ext, tmp));
  }
};

}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_SHARED_IA32_X64_MACRO_ASSEMBLER_SHARED_IA32_X64_H_
