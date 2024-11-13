// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDED_FROM_MACRO_ASSEMBLER_H
#error This header must be included via macro-assembler.h
#endif

#ifndef V8_CODEGEN_ARM64_MACRO_ASSEMBLER_ARM64_H_
#define V8_CODEGEN_ARM64_MACRO_ASSEMBLER_ARM64_H_

#include <optional>

#include "src/base/bits.h"
#include "src/codegen/arm64/assembler-arm64.h"
#include "src/codegen/bailout-reason.h"
#include "src/common/globals.h"
#include "src/objects/tagged-index.h"

// Simulator specific helpers.
#if USE_SIMULATOR
#if DEBUG
#define ASM_LOCATION(message) __ Debug("LOCATION: " message, __LINE__, NO_PARAM)
#define ASM_LOCATION_IN_ASSEMBLER(message) \
  Debug("LOCATION: " message, __LINE__, NO_PARAM)
#else
#define ASM_LOCATION(message)
#define ASM_LOCATION_IN_ASSEMBLER(message)
#endif
#else
#define ASM_LOCATION(message)
#define ASM_LOCATION_IN_ASSEMBLER(message)
#endif

namespace v8 {
namespace internal {

#define LS_MACRO_LIST(V)                                     \
  V(Ldrb, Register&, rt, LDRB_w)                             \
  V(Strb, Register&, rt, STRB_w)                             \
  V(Ldrsb, Register&, rt, rt.Is64Bits() ? LDRSB_x : LDRSB_w) \
  V(Ldrh, Register&, rt, LDRH_w)                             \
  V(Strh, Register&, rt, STRH_w)                             \
  V(Ldrsh, Register&, rt, rt.Is64Bits() ? LDRSH_x : LDRSH_w) \
  V(Ldr, CPURegister&, rt, LoadOpFor(rt))                    \
  V(Str, CPURegister&, rt, StoreOpFor(rt))                   \
  V(Ldrsw, Register&, rt, LDRSW_x)

#define LSPAIR_MACRO_LIST(V)                             \
  V(Ldp, CPURegister&, rt, rt2, LoadPairOpFor(rt, rt2))  \
  V(Stp, CPURegister&, rt, rt2, StorePairOpFor(rt, rt2)) \
  V(Ldpsw, CPURegister&, rt, rt2, LDPSW_x)

#define LDA_STL_MACRO_LIST(V) \
  V(Ldarb, ldarb)             \
  V(Ldarh, ldarh)             \
  V(Ldar, ldar)               \
  V(Ldaxrb, ldaxrb)           \
  V(Ldaxrh, ldaxrh)           \
  V(Ldaxr, ldaxr)             \
  V(Stlrb, stlrb)             \
  V(Stlrh, stlrh)             \
  V(Stlr, stlr)

#define STLX_MACRO_LIST(V) \
  V(Stlxrb, stlxrb)        \
  V(Stlxrh, stlxrh)        \
  V(Stlxr, stlxr)

#define CAS_SINGLE_MACRO_LIST(V) \
  V(Cas, cas)                    \
  V(Casa, casa)                  \
  V(Casl, casl)                  \
  V(Casal, casal)                \
  V(Casb, casb)                  \
  V(Casab, casab)                \
  V(Caslb, caslb)                \
  V(Casalb, casalb)              \
  V(Cash, cash)                  \
  V(Casah, casah)                \
  V(Caslh, caslh)                \
  V(Casalh, casalh)

#define CAS_PAIR_MACRO_LIST(V) \
  V(Casp, casp)                \
  V(Caspa, caspa)              \
  V(Caspl, caspl)              \
  V(Caspal, caspal)

// These macros generate all the variations of the atomic memory operations,
// e.g. ldadd, ldadda, ldaddb, staddl, etc.

#define ATOMIC_MEMORY_SIMPLE_MACRO_LIST(V, DEF, MASM_PRE, ASM_PRE) \
  V(DEF, MASM_PRE##add, ASM_PRE##add)                              \
  V(DEF, MASM_PRE##clr, ASM_PRE##clr)                              \
  V(DEF, MASM_PRE##eor, ASM_PRE##eor)                              \
  V(DEF, MASM_PRE##set, ASM_PRE##set)                              \
  V(DEF, MASM_PRE##smax, ASM_PRE##smax)                            \
  V(DEF, MASM_PRE##smin, ASM_PRE##smin)                            \
  V(DEF, MASM_PRE##umax, ASM_PRE##umax)                            \
  V(DEF, MASM_PRE##umin, ASM_PRE##umin)

#define ATOMIC_MEMORY_STORE_MACRO_MODES(V, MASM, ASM) \
  V(MASM, ASM)                                        \
  V(MASM##l, ASM##l)                                  \
  V(MASM##b, ASM##b)                                  \
  V(MASM##lb, ASM##lb)                                \
  V(MASM##h, ASM##h)                                  \
  V(MASM##lh, ASM##lh)

#define ATOMIC_MEMORY_LOAD_MACRO_MODES(V, MASM, ASM) \
  ATOMIC_MEMORY_STORE_MACRO_MODES(V, MASM, ASM)      \
  V(MASM##a, ASM##a)                                 \
  V(MASM##al, ASM##al)                               \
  V(MASM##ab, ASM##ab)                               \
  V(MASM##alb, ASM##alb)                             \
  V(MASM##ah, ASM##ah)                               \
  V(MASM##alh, ASM##alh)

// ----------------------------------------------------------------------------
// Static helper functions

// Generate a MemOperand for loading a field from an object.
inline MemOperand FieldMemOperand(Register object, int offset);

// ----------------------------------------------------------------------------
// MacroAssembler

enum BranchType {
  // Copies of architectural conditions.
  // The associated conditions can be used in place of those, the code will
  // take care of reinterpreting them with the correct type.
  integer_eq = eq,
  integer_ne = ne,
  integer_hs = hs,
  integer_lo = lo,
  integer_mi = mi,
  integer_pl = pl,
  integer_vs = vs,
  integer_vc = vc,
  integer_hi = hi,
  integer_ls = ls,
  integer_ge = ge,
  integer_lt = lt,
  integer_gt = gt,
  integer_le = le,
  integer_al = al,
  integer_nv = nv,

  // These two are *different* from the architectural codes al and nv.
  // 'always' is used to generate unconditional branches.
  // 'never' is used to not generate a branch (generally as the inverse
  // branch type of 'always).
  always,
  never,
  // cbz and cbnz
  reg_zero,
  reg_not_zero,
  // tbz and tbnz
  reg_bit_clear,
  reg_bit_set,

  // Aliases.
  kBranchTypeFirstCondition = eq,
  kBranchTypeLastCondition = nv,
  kBranchTypeFirstUsingReg = reg_zero,
  kBranchTypeFirstUsingBit = reg_bit_clear
};

inline BranchType InvertBranchType(BranchType type) {
  if (kBranchTypeFirstCondition <= type && type <= kBranchTypeLastCondition) {
    return static_cast<BranchType>(
        NegateCondition(static_cast<Condition>(type)));
  } else {
    return static_cast<BranchType>(type ^ 1);
  }
}

enum LinkRegisterStatus { kLRHasNotBeenSaved, kLRHasBeenSaved };
enum DiscardMoveMode { kDontDiscardForSameWReg, kDiscardForSameWReg };

// The macro assembler supports moving automatically pre-shifted immediates for
// arithmetic and logical instructions, and then applying a post shift in the
// instruction to undo the modification, in order to reduce the code emitted for
// an operation. For example:
//
//  Add(x0, x0, 0x1f7de) => movz x16, 0xfbef; add x0, x0, x16, lsl #1.
//
// This optimisation can be only partially applied when the stack pointer is an
// operand or destination, so this enumeration is used to control the shift.
enum PreShiftImmMode {
  kNoShift,          // Don't pre-shift.
  kLimitShiftForSP,  // Limit pre-shift for add/sub extend use.
  kAnyShift          // Allow any pre-shift.
};

// TODO(victorgomes): Move definition to macro-assembler.h, once all other
// platforms are updated.
enum class StackLimitKind { kInterruptStackLimit, kRealStackLimit };

class V8_EXPORT_PRIVATE MacroAssembler : public MacroAssemblerBase {
 public:
  using MacroAssemblerBase::MacroAssemblerBase;

#if DEBUG
  void set_allow_macro_instructions(bool value) {
    allow_macro_instructions_ = value;
  }
  bool allow_macro_instructions() const { return allow_macro_instructions_; }
#endif

  // We should not use near calls or jumps for calls to external references,
  // since the code spaces are not guaranteed to be close to each other.
  bool CanUseNearCallOrJump(RelocInfo::Mode rmode) {
    return rmode != RelocInfo::EXTERNAL_REFERENCE;
  }

  static bool IsNearCallOffset(int64_t offset);

  // Activation support.
  void EnterFrame(StackFrame::Type type);
  void EnterFrame(StackFrame::Type type, bool load_constant_pool_pointer_reg) {
    // Out-of-line constant pool not implemented on arm64.
    UNREACHABLE();
  }
  void LeaveFrame(StackFrame::Type type);

  inline void InitializeRootRegister();

  void Mov(const Register& rd, const Operand& operand,
           DiscardMoveMode discard_mode = kDontDiscardForSameWReg);
  void Mov(const Register& rd, uint64_t imm);
  void Mov(const Register& rd, ExternalReference reference);
  void LoadIsolateField(const Register& rd, IsolateFieldId id);
  void Mov(const VRegister& vd, int vd_index, const VRegister& vn,
           int vn_index) {
    DCHECK(allow_macro_instructions());
    mov(vd, vd_index, vn, vn_index);
  }
  void Mov(const Register& rd, Tagged<Smi> smi);
  void Mov(const VRegister& vd, const VRegister& vn, int index) {
    DCHECK(allow_macro_instructions());
    mov(vd, vn, index);
  }
  void Mov(const VRegister& vd, int vd_index, const Register& rn) {
    DCHECK(allow_macro_instructions());
    mov(vd, vd_index, rn);
  }
  void Mov(const Register& rd, const VRegister& vn, int vn_index) {
    DCHECK(allow_macro_instructions());
    mov(rd, vn, vn_index);
  }

  // These are required for compatibility with architecture independent code.
  // Remove if not needed.
  void Move(Register dst, Tagged<Smi> src);
  void Move(Register dst, MemOperand src);
  void Move(Register dst, Register src);

  // Move src0 to dst0 and src1 to dst1, handling possible overlaps.
  void MovePair(Register dst0, Register src0, Register dst1, Register src1);

  // Register swap. Note that the register operands should be distinct.
  void Swap(Register lhs, Register rhs);
  void Swap(VRegister lhs, VRegister rhs);

// NEON by element instructions.
#define NEON_BYELEMENT_MACRO_LIST(V) \
  V(fmla, Fmla)                      \
  V(fmls, Fmls)                      \
  V(fmul, Fmul)                      \
  V(fmulx, Fmulx)                    \
  V(mul, Mul)                        \
  V(mla, Mla)                        \
  V(mls, Mls)                        \
  V(sqdmulh, Sqdmulh)                \
  V(sqrdmulh, Sqrdmulh)              \
  V(sqdmull, Sqdmull)                \
  V(sqdmull2, Sqdmull2)              \
  V(sqdmlal, Sqdmlal)                \
  V(sqdmlal2, Sqdmlal2)              \
  V(sqdmlsl, Sqdmlsl)                \
  V(sqdmlsl2, Sqdmlsl2)              \
  V(smull, Smull)                    \
  V(smull2, Smull2)                  \
  V(smlal, Smlal)                    \
  V(smlal2, Smlal2)                  \
  V(smlsl, Smlsl)                    \
  V(smlsl2, Smlsl2)                  \
  V(umull, Umull)                    \
  V(umull2, Umull2)                  \
  V(umlal, Umlal)                    \
  V(umlal2, Umlal2)                  \
  V(umlsl, Umlsl)                    \
  V(umlsl2, Umlsl2)

#define DEFINE_MACRO_ASM_FUNC(ASM, MASM)                                   \
  void MASM(const VRegister& vd, const VRegister& vn, const VRegister& vm, \
            int vm_index) {                                                \
    DCHECK(allow_macro_instructions());                                    \
    ASM(vd, vn, vm, vm_index);                                             \
  }
  NEON_BYELEMENT_MACRO_LIST(DEFINE_MACRO_ASM_FUNC)
#undef DEFINE_MACRO_ASM_FUNC

// NEON 2 vector register instructions.
#define NEON_2VREG_MACRO_LIST(V) \
  V(abs, Abs)                    \
  V(addp, Addp)                  \
  V(addv, Addv)                  \
  V(cls, Cls)                    \
  V(clz, Clz)                    \
  V(cnt, Cnt)                    \
  V(faddp, Faddp)                \
  V(fcvtas, Fcvtas)              \
  V(fcvtau, Fcvtau)              \
  V(fcvtl, Fcvtl)                \
  V(fcvtms, Fcvtms)              \
  V(fcvtmu, Fcvtmu)              \
  V(fcvtn, Fcvtn)                \
  V(fcvtns, Fcvtns)              \
  V(fcvtnu, Fcvtnu)              \
  V(fcvtps, Fcvtps)              \
  V(fcvtpu, Fcvtpu)              \
  V(fmaxnmp, Fmaxnmp)            \
  V(fmaxnmv, Fmaxnmv)            \
  V(fmaxp, Fmaxp)                \
  V(fmaxv, Fmaxv)                \
  V(fminnmp, Fminnmp)            \
  V(fminnmv, Fminnmv)            \
  V(fminp, Fminp)                \
  V(fminv, Fminv)                \
  V(fneg, Fneg)                  \
  V(frecpe, Frecpe)              \
  V(frecpx, Frecpx)              \
  V(frinta, Frinta)              \
  V(frinti, Frinti)              \
  V(frintm, Frintm)              \
  V(frintn, Frintn)              \
  V(frintp, Frintp)              \
  V(frintx, Frintx)              \
  V(frintz, Frintz)              \
  V(frsqrte, Frsqrte)            \
  V(fsqrt, Fsqrt)                \
  V(mov, Mov)                    \
  V(mvn, Mvn)                    \
  V(neg, Neg)                    \
  V(not_, Not)                   \
  V(rbit, Rbit)                  \
  V(rev16, Rev16)                \
  V(rev32, Rev32)                \
  V(rev64, Rev64)                \
  V(sadalp, Sadalp)              \
  V(saddlp, Saddlp)              \
  V(saddlv, Saddlv)              \
  V(smaxv, Smaxv)                \
  V(sminv, Sminv)                \
  V(sqabs, Sqabs)                \
  V(sqneg, Sqneg)                \
  V(sqxtn2, Sqxtn2)              \
  V(sqxtn, Sqxtn)                \
  V(sqxtun2, Sqxtun2)            \
  V(sqxtun, Sqxtun)              \
  V(suqadd, Suqadd)              \
  V(sxtl2, Sxtl2)                \
  V(sxtl, Sxtl)                  \
  V(uadalp, Uadalp)              \
  V(uaddlp, Uaddlp)              \
  V(uaddlv, Uaddlv)              \
  V(umaxv, Umaxv)                \
  V(uminv, Uminv)                \
  V(uqxtn2, Uqxtn2)              \
  V(uqxtn, Uqxtn)                \
  V(urecpe, Urecpe)              \
  V(ursqrte, Ursqrte)            \
  V(usqadd, Usqadd)              \
  V(uxtl2, Uxtl2)                \
  V(uxtl, Uxtl)                  \
  V(xtn2, Xtn2)                  \
  V(xtn, Xtn)

#define DEFINE_MACRO_ASM_FUNC(ASM, MASM)                \
  void MASM(const VRegister& vd, const VRegister& vn) { \
    DCHECK(allow_macro_instructions());                 \
    ASM(vd, vn);                                        \
  }
  NEON_2VREG_MACRO_LIST(DEFINE_MACRO_ASM_FUNC)
#undef DEFINE_MACRO_ASM_FUNC
#undef NEON_2VREG_MACRO_LIST

// NEON 2 vector register with immediate instructions.
#define NEON_2VREG_FPIMM_MACRO_LIST(V) \
  V(fcmeq, Fcmeq)                      \
  V(fcmge, Fcmge)                      \
  V(fcmgt, Fcmgt)                      \
  V(fcmle, Fcmle)                      \
  V(fcmlt, Fcmlt)

#define DEFINE_MACRO_ASM_FUNC(ASM, MASM)                            \
  void MASM(const VRegister& vd, const VRegister& vn, double imm) { \
    DCHECK(allow_macro_instructions());                             \
    ASM(vd, vn, imm);                                               \
  }
  NEON_2VREG_FPIMM_MACRO_LIST(DEFINE_MACRO_ASM_FUNC)
#undef DEFINE_MACRO_ASM_FUNC

// NEON 3 vector register instructions.
#define NEON_3VREG_MACRO_LIST(V) \
  V(add, Add)                    \
  V(addhn2, Addhn2)              \
  V(addhn, Addhn)                \
  V(addp, Addp)                  \
  V(and_, And)                   \
  V(bic, Bic)                    \
  V(bif, Bif)                    \
  V(bit, Bit)                    \
  V(bsl, Bsl)                    \
  V(cmeq, Cmeq)                  \
  V(cmge, Cmge)                  \
  V(cmgt, Cmgt)                  \
  V(cmhi, Cmhi)                  \
  V(cmhs, Cmhs)                  \
  V(cmtst, Cmtst)                \
  V(eor, Eor)                    \
  V(fabd, Fabd)                  \
  V(facge, Facge)                \
  V(facgt, Facgt)                \
  V(faddp, Faddp)                \
  V(fcmeq, Fcmeq)                \
  V(fcmge, Fcmge)                \
  V(fcmgt, Fcmgt)                \
  V(fmaxnmp, Fmaxnmp)            \
  V(fmaxp, Fmaxp)                \
  V(fminnmp, Fminnmp)            \
  V(fminp, Fminp)                \
  V(fmla, Fmla)                  \
  V(fmls, Fmls)                  \
  V(fmulx, Fmulx)                \
  V(fnmul, Fnmul)                \
  V(frecps, Frecps)              \
  V(frsqrts, Frsqrts)            \
  V(mla, Mla)                    \
  V(mls, Mls)                    \
  V(mul, Mul)                    \
  V(orn, Orn)                    \
  V(orr, Orr)                    \
  V(pmull2, Pmull2)              \
  V(pmull, Pmull)                \
  V(pmul, Pmul)                  \
  V(raddhn2, Raddhn2)            \
  V(raddhn, Raddhn)              \
  V(rsubhn2, Rsubhn2)            \
  V(rsubhn, Rsubhn)              \
  V(sabal2, Sabal2)              \
  V(sabal, Sabal)                \
  V(saba, Saba)                  \
  V(sabdl2, Sabdl2)              \
  V(sabdl, Sabdl)                \
  V(sabd, Sabd)                  \
  V(saddl2, Saddl2)              \
  V(saddl, Saddl)                \
  V(saddw2, Saddw2)              \
  V(saddw, Saddw)                \
  V(sdot, Sdot)                  \
  V(shadd, Shadd)                \
  V(shsub, Shsub)                \
  V(smaxp, Smaxp)                \
  V(smax, Smax)                  \
  V(sminp, Sminp)                \
  V(smin, Smin)                  \
  V(smlal2, Smlal2)              \
  V(smlal, Smlal)                \
  V(smlsl2, Smlsl2)              \
  V(smlsl, Smlsl)                \
  V(smull2, Smull2)              \
  V(smull, Smull)                \
  V(sqadd, Sqadd)                \
  V(sqdmlal2, Sqdmlal2)          \
  V(sqdmlal, Sqdmlal)            \
  V(sqdmlsl2, Sqdmlsl2)          \
  V(sqdmlsl, Sqdmlsl)            \
  V(sqdmulh, Sqdmulh)            \
  V(sqdmull2, Sqdmull2)          \
  V(sqdmull, Sqdmull)            \
  V(sqrdmulh, Sqrdmulh)          \
  V(sqrshl, Sqrshl)              \
  V(sqshl, Sqshl)                \
  V(sqsub, Sqsub)                \
  V(srhadd, Srhadd)              \
  V(srshl, Srshl)                \
  V(sshl, Sshl)                  \
  V(ssubl2, Ssubl2)              \
  V(ssubl, Ssubl)                \
  V(ssubw2, Ssubw2)              \
  V(ssubw, Ssubw)                \
  V(subhn2, Subhn2)              \
  V(subhn, Subhn)                \
  V(sub, Sub)                    \
  V(trn1, Trn1)                  \
  V(trn2, Trn2)                  \
  V(uabal2, Uabal2)              \
  V(uabal, Uabal)                \
  V(uaba, Uaba)                  \
  V(uabdl2, Uabdl2)              \
  V(uabdl, Uabdl)                \
  V(uabd, Uabd)                  \
  V(uaddl2, Uaddl2)              \
  V(uaddl, Uaddl)                \
  V(uaddw2, Uaddw2)              \
  V(uaddw, Uaddw)                \
  V(uhadd, Uhadd)                \
  V(uhsub, Uhsub)                \
  V(umaxp, Umaxp)                \
  V(umax, Umax)                  \
  V(uminp, Uminp)                \
  V(umin, Umin)                  \
  V(umlal2, Umlal2)              \
  V(umlal, Umlal)                \
  V(umlsl2, Umlsl2)              \
  V(umlsl, Umlsl)                \
  V(umull2, Umull2)              \
  V(umull, Umull)                \
  V(uqadd, Uqadd)                \
  V(uqrshl, Uqrshl)              \
  V(uqshl, Uqshl)                \
  V(uqsub, Uqsub)                \
  V(urhadd, Urhadd)              \
  V(urshl, Urshl)                \
  V(ushl, Ushl)                  \
  V(usubl2, Usubl2)              \
  V(usubl, Usubl)                \
  V(usubw2, Usubw2)              \
  V(usubw, Usubw)                \
  V(uzp1, Uzp1)                  \
  V(uzp2, Uzp2)                  \
  V(zip1, Zip1)                  \
  V(zip2, Zip2)

#define DEFINE_MACRO_ASM_FUNC(ASM, MASM)                                     \
  void MASM(const VRegister& vd, const VRegister& vn, const VRegister& vm) { \
    DCHECK(allow_macro_instructions());                                      \
    ASM(vd, vn, vm);                                                         \
  }
  NEON_3VREG_MACRO_LIST(DEFINE_MACRO_ASM_FUNC)
#undef DEFINE_MACRO_ASM_FUNC

  void Bic(const VRegister& vd, const int imm8, const int left_shift = 0) {
    DCHECK(allow_macro_instructions());
    bic(vd, imm8, left_shift);
  }

  // This is required for compatibility in architecture independent code.
  inline void jmp(Label* L);

  void B(Label* label, BranchType type, Register reg = NoReg, int bit = -1);
  inline void B(Label* label);
  inline void B(Condition cond, Label* label);
  void B(Label* label, Condition cond);

  void Tbnz(const Register& rt, unsigned bit_pos, Label* label);
  void Tbz(const Register& rt, unsigned bit_pos, Label* label);

  void Cbnz(const Register& rt, Label* label);
  void Cbz(const Register& rt, Label* label);

  void Pacibsp() {
    DCHECK(allow_macro_instructions_);
    pacibsp();
  }
  void Autibsp() {
    DCHECK(allow_macro_instructions_);
    autibsp();
  }

  // The 1716 pac and aut instructions encourage people to use x16 and x17
  // directly, perhaps without realising that this is forbidden. For example:
  //
  //     UseScratchRegisterScope temps(&masm);
  //     Register temp = temps.AcquireX();  // temp will be x16
  //     __ Mov(x17, ptr);
  //     __ Mov(x16, modifier);  // Will override temp!
  //     __ Pacib1716();
  //
  // To work around this issue, you must exclude x16 and x17 from the scratch
  // register list. You may need to replace them with other registers:
  //
  //     UseScratchRegisterScope temps(&masm);
  //     temps.Exclude(x16, x17);
  //     temps.Include(x10, x11);
  //     __ Mov(x17, ptr);
  //     __ Mov(x16, modifier);
  //     __ Pacib1716();
  void Pacib1716() {
    DCHECK(allow_macro_instructions_);
    DCHECK(!TmpList()->IncludesAliasOf(x16));
    DCHECK(!TmpList()->IncludesAliasOf(x17));
    pacib1716();
  }
  void Autib1716() {
    DCHECK(allow_macro_instructions_);
    DCHECK(!TmpList()->IncludesAliasOf(x16));
    DCHECK(!TmpList()->IncludesAliasOf(x17));
    autib1716();
  }

  inline void Dmb(BarrierDomain domain, BarrierType type);
  inline void Dsb(BarrierDomain domain, BarrierType type);
  inline void Isb();
  inline void Csdb();

  inline void SmiUntag(Register dst, Register src);
  inline void SmiUntag(Register dst, const MemOperand& src);
  inline void SmiUntag(Register smi);

  inline void SmiTag(Register dst, Register src);
  inline void SmiTag(Register smi);

  inline void SmiToInt32(Register smi);
  inline void SmiToInt32(Register dst, Register smi);

  // Calls Abort(msg) if the condition cond is not satisfied.
  // Use --debug_code to enable.
  void Assert(Condition cond, AbortReason reason) NOOP_UNLESS_DEBUG_CODE;

  // Like Assert(), but without condition.
  // Use --debug_code to enable.
  void AssertUnreachable(AbortReason reason) NOOP_UNLESS_DEBUG_CODE;

  void AssertSmi(Register object,
                 AbortReason reason = AbortReason::kOperandIsNotASmi)
      NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is a smi, enabled via --debug-code.
  void AssertNotSmi(Register object,
                    AbortReason reason = AbortReason::kOperandIsASmi)
      NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if a 64 bit register containing a 32 bit payload does
  // not have zeros in the top 32 bits, enabled via --debug-code.
  void AssertZeroExtended(Register int32_register) NOOP_UNLESS_DEBUG_CODE;

  void AssertJSAny(Register object, Register map_tmp, Register tmp,
                   AbortReason abort_reason) NOOP_UNLESS_DEBUG_CODE;

  // Like Assert(), but always enabled.
  void Check(Condition cond, AbortReason reason);

  // Functions performing a check on a known or potential smi. Returns
  // a condition that is satisfied if the check is successful.
  Condition CheckSmi(Register src);

  inline void Debug(const char* message, uint32_t code, Instr params = BREAK);

  void Trap();
  void DebugBreak();

  // Print a message to stderr and abort execution.
  void Abort(AbortReason reason);

  // Like printf, but print at run-time from generated code.
  //
  // The caller must ensure that arguments for floating-point placeholders
  // (such as %e, %f or %g) are VRegisters, and that arguments for integer
  // placeholders are Registers.
  //
  // Format placeholders that refer to more than one argument, or to a specific
  // argument, are not supported. This includes formats like "%1$d" or "%.*d".
  //
  // This function automatically preserves caller-saved registers so that
  // calling code can use Printf at any point without having to worry about
  // corruption. The preservation mechanism generates a lot of code. If this is
  // a problem, preserve the important registers manually and then call
  // PrintfNoPreserve. Callee-saved registers are not used by Printf, and are
  // implicitly preserved.
  void Printf(const char* format, CPURegister arg0 = NoCPUReg,
              CPURegister arg1 = NoCPUReg, CPURegister arg2 = NoCPUReg,
              CPURegister arg3 = NoCPUReg);

  // Like Printf, but don't preserve any caller-saved registers, not even 'lr'.
  //
  // The return code from the system printf call will be returned in x0.
  void PrintfNoPreserve(const char* format, const CPURegister& arg0 = NoCPUReg,
                        const CPURegister& arg1 = NoCPUReg,
                        const CPURegister& arg2 = NoCPUReg,
                        const CPURegister& arg3 = NoCPUReg);

  // Remaining instructions are simple pass-through calls to the assembler.
  inline void Asr(const Register& rd, const Register& rn, unsigned shift);
  inline void Asr(const Register& rd, const Register& rn, const Register& rm);

  // Try to move an immediate into the destination register in a single
  // instruction. Returns true for success, and updates the contents of dst.
  // Returns false, otherwise.
  bool TryOneInstrMoveImmediate(const Register& dst, int64_t imm);

  inline void Bind(Label* label,
                   BranchTargetIdentifier id = BranchTargetIdentifier::kNone);

  // Control-flow integrity:

  // Define a function entrypoint.
  inline void CodeEntry();
  // Define an exception handler.
  inline void ExceptionHandler();
  // Define an exception handler and bind a label.
  inline void BindExceptionHandler(Label* label);

  // Control-flow integrity:

  // Define a jump (BR) target.
  inline void JumpTarget();
  // Define a jump (BR) target and bind a label.
  inline void BindJumpTarget(Label* label);
  // Define a call (BLR) target. The target also allows tail calls (via BR)
  // when the target is x16 or x17.
  inline void CallTarget();
  // Define a jump/call target and bind a label.
  inline void BindCallTarget(Label* label);
  // Define a jump/call target.
  inline void JumpOrCallTarget();
  // Define a jump/call target and bind a label.
  inline void BindJumpOrCallTarget(Label* label);

  static unsigned CountSetHalfWords(uint64_t imm, unsigned reg_size);

  CPURegList* TmpList() { return &tmp_list_; }
  CPURegList* FPTmpList() { return &fptmp_list_; }

  static CPURegList DefaultTmpList();
  static CPURegList DefaultFPTmpList();

  // Move macros.
  inline void Mvn(const Register& rd, uint64_t imm);
  void Mvn(const Register& rd, const Operand& operand);
  static bool IsImmMovn(uint64_t imm, unsigned reg_size);
  static bool IsImmMovz(uint64_t imm, unsigned reg_size);

  void LogicalMacro(const Register& rd, const Register& rn,
                    const Operand& operand, LogicalOp op);
  void AddSubMacro(const Register& rd, const Register& rn,
                   const Operand& operand, FlagsUpdate S, AddSubOp op);
  inline void Orr(const Register& rd, const Register& rn,
                  const Operand& operand);
  void Orr(const VRegister& vd, const int imm8, const int left_shift = 0) {
    DCHECK(allow_macro_instructions());
    orr(vd, imm8, left_shift);
  }
  inline void Orn(const Register& rd, const Register& rn,
                  const Operand& operand);
  inline void Eor(const Register& rd, const Register& rn,
                  const Operand& operand);
  inline void Eon(const Register& rd, const Register& rn,
                  const Operand& operand);
  inline void And(const Register& rd, const Register& rn,
                  const Operand& operand);
  inline void Ands(const Register& rd, const Register& rn,
                   const Operand& operand);
  inline void Tst(const Register& rn, const Operand& operand);
  inline void Bic(const Register& rd, const Register& rn,
                  const Operand& operand);
  inline void Blr(const Register& xn);
  inline void Cmp(const Register& rn, const Operand& operand);
  inline void CmpTagged(const Register& rn, const Operand& operand);
  inline void Subs(const Register& rd, const Register& rn,
                   const Operand& operand);
  void Csel(const Register& rd, const Register& rn, const Operand& operand,
            Condition cond);
  inline void Fcsel(const VRegister& fd, const VRegister& fn,
                    const VRegister& fm, Condition cond);

  // Checks if value is in range [lower_limit, higher_limit] using a single
  // comparison. Condition `ls` indicates the value is in the range.
  void CompareRange(Register value, Register scratch, unsigned lower_limit,
                    unsigned higher_limit);
  void JumpIfIsInRange(Register value, Register scratch, unsigned lower_limit,
                       unsigned higher_limit, Label* on_in_range);

  // Emits a runtime assert that the stack pointer is aligned.
  void AssertSpAligned() NOOP_UNLESS_DEBUG_CODE;

  // Copy slot_count stack slots from the stack offset specified by src to
  // the stack offset specified by dst. The offsets and count are expressed in
  // slot-sized units. Offset dst must be less than src, or the gap between
  // them must be greater than or equal to slot_count, otherwise the result is
  // unpredictable. The function may corrupt its register arguments. The
  // registers must not alias each other.
  void CopySlots(int dst, Register src, Register slot_count);
  void CopySlots(Register dst, Register src, Register slot_count);

  // Copy count double words from the address in register src to the address
  // in register dst. There are three modes for this function:
  // 1) Address dst must be less than src, or the gap between them must be
  //    greater than or equal to count double words, otherwise the result is
  //    unpredictable. This is the default mode.
  // 2) Address src must be less than dst, or the gap between them must be
  //    greater than or equal to count double words, otherwise the result is
  //    undpredictable. In this mode, src and dst specify the last (highest)
  //    address of the regions to copy from and to.
  // 3) The same as mode 1, but the words are copied in the reversed order.
  // The case where src == dst is not supported.
  // The function may corrupt its register arguments. The registers must not
  // alias each other.
  enum CopyDoubleWordsMode {
    kDstLessThanSrc,
    kSrcLessThanDst,
    kDstLessThanSrcAndReverse
  };
  void CopyDoubleWords(Register dst, Register src, Register count,
                       CopyDoubleWordsMode mode = kDstLessThanSrc);

  // Calculate the address of a double word-sized slot at slot_offset from the
  // stack pointer, and write it to dst. Positive slot_offsets are at addresses
  // greater than sp, with slot zero at sp.
  void SlotAddress(Register dst, int slot_offset);
  void SlotAddress(Register dst, Register slot_offset);

  // Load a literal from the inline constant pool.
  inline void Ldr(const CPURegister& rt, const Operand& imm);

  // Claim or drop stack space.
  //
  // On Windows, Claim will write a value every 4k, as is required by the stack
  // expansion mechanism.
  //
  // The stack pointer must be aligned to 16 bytes and the size claimed or
  // dropped must be a multiple of 16 bytes.
  //
  // Note that unit_size must be specified in bytes. For variants which take a
  // Register count, the unit size must be a power of two.
  inline void Claim(int64_t count, uint64_t unit_size = kXRegSize);
  inline void Claim(const Register& count, uint64_t unit_size = kXRegSize,
                    bool assume_sp_aligned = true);
  inline void Drop(int64_t count, uint64_t unit_size = kXRegSize);
  inline void Drop(const Register& count, uint64_t unit_size = kXRegSize);

  // Drop 'count' + 'extra_slots' arguments from the stack, rounded up to
  // a multiple of two, without actually accessing memory.
  // We assume the size of the arguments is the pointer size.
  inline void DropArguments(const Register& count, int extra_slots = 0);
  inline void DropArguments(int64_t count);

  // Drop 'count' slots from stack, rounded up to a multiple of two, without
  // actually accessing memory.
  inline void DropSlots(int64_t count);

  // Push a single argument, with padding, to the stack.
  inline void PushArgument(const Register& arg);

  // Add and sub macros.
  inline void Add(const Register& rd, const Register& rn,
                  const Operand& operand);
  inline void Adds(const Register& rd, const Register& rn,
                   const Operand& operand);
  inline void Sub(const Register& rd, const Register& rn,
                  const Operand& operand);

  // Abort execution if argument is not a positive or zero integer, enabled via
  // --debug-code.
  void AssertPositiveOrZero(Register value) NOOP_UNLESS_DEBUG_CODE;

#define DECLARE_FUNCTION(FN, REGTYPE, REG, OP) \
  inline void FN(const REGTYPE REG, const MemOperand& addr);
      LS_MACRO_LIST(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION

  // Caution: if {value} is a 32-bit negative int, it should be sign-extended
  // to 64-bit before calling this function.
  void Switch(Register scratch, Register value, int case_value_base,
              Label** labels, int num_labels);

  // Push or pop up to 4 registers of the same width to or from the stack.
  //
  // If an argument register is 'NoReg', all further arguments are also assumed
  // to be 'NoReg', and are thus not pushed or popped.
  //
  // Arguments are ordered such that "Push(a, b);" is functionally equivalent
  // to "Push(a); Push(b);".
  //
  // It is valid to push the same register more than once, and there is no
  // restriction on the order in which registers are specified.
  //
  // It is not valid to pop into the same register more than once in one
  // operation, not even into the zero register.
  //
  // The stack pointer must be aligned to 16 bytes on entry and the total size
  // of the specified registers must also be a multiple of 16 bytes.
  //
  // Other than the registers passed into Pop, the stack pointer, (possibly)
  // the system stack pointer and (possibly) the link register, these methods
  // do not modify any other registers.
  //
  // Some of the methods take an optional LoadLRMode or StoreLRMode template
  // argument, which specifies whether we need to sign the link register at the
  // start of the operation, or authenticate it at the end of the operation,
  // when control flow integrity measures are enabled.
  // When the mode is kDontLoadLR or kDontStoreLR, LR must not be passed as an
  // argument to the operation.
  enum LoadLRMode { kAuthLR, kDontLoadLR };
  enum StoreLRMode { kSignLR, kDontStoreLR };
  template <StoreLRMode lr_mode = kDontStoreLR>
  void Push(const CPURegister& src0, const CPURegister& src1 = NoReg,
            const CPURegister& src2 = NoReg, const CPURegister& src3 = NoReg);
  void Push(const CPURegister& src0, const CPURegister& src1,
            const CPURegister& src2, const CPURegister& src3,
            const CPURegister& src4, const CPURegister& src5 = NoReg,
            const CPURegister& src6 = NoReg, const CPURegister& src7 = NoReg);
  template <LoadLRMode lr_mode = kDontLoadLR>
  void Pop(const CPURegister& dst0, const CPURegister& dst1 = NoReg,
           const CPURegister& dst2 = NoReg, const CPURegister& dst3 = NoReg);
  void Pop(const CPURegister& dst0, const CPURegister& dst1,
           const CPURegister& dst2, const CPURegister& dst3,
           const CPURegister& dst4, const CPURegister& dst5 = NoReg,
           const CPURegister& dst6 = NoReg, const CPURegister& dst7 = NoReg);
  template <StoreLRMode lr_mode = kDontStoreLR>
  void Push(const Register& src0, const VRegister& src1);

  void MaybeSaveRegisters(RegList registers);
  void MaybeRestoreRegisters(RegList registers);

  void CallEphemeronKeyBarrier(Register object, Operand offset,
                               SaveFPRegsMode fp_mode);

  void CallIndirectPointerBarrier(Register object, Operand offset,
                                  SaveFPRegsMode fp_mode,
                                  IndirectPointerTag tag);

  void CallRecordWriteStubSaveRegisters(
      Register object, Operand offset, SaveFPRegsMode fp_mode,
      StubCallMode mode = StubCallMode::kCallBuiltinPointer);
  void CallRecordWriteStub(
      Register object, Register slot_address, SaveFPRegsMode fp_mode,
      StubCallMode mode = StubCallMode::kCallBuiltinPointer);

  // For a given |object| and |offset|:
  //   - Move |object| to |dst_object|.
  //   - Compute the address of the slot pointed to by |offset| in |object| and
  //     write it to |dst_slot|.
  // This method makes sure |object| and |offset| are allowed to overlap with
  // the destination registers.
  void MoveObjectAndSlot(Register dst_object, Register dst_slot,
                         Register object, Operand offset);

  // Alternative forms of Push and Pop, taking a RegList or CPURegList that
  // specifies the registers that are to be pushed or popped. Higher-numbered
  // registers are associated with higher memory addresses (as in the A32 push
  // and pop instructions).
  //
  // (Push|Pop)SizeRegList allow you to specify the register size as a
  // parameter. Only kXRegSizeInBits, kWRegSizeInBits, kDRegSizeInBits and
  // kSRegSizeInBits are supported.
  //
  // Otherwise, (Push|Pop)(CPU|X|W|D|S)RegList is preferred.
  void PushCPURegList(CPURegList registers);
  void PopCPURegList(CPURegList registers);

  // Calculate how much stack space (in bytes) are required to store caller
  // registers excluding those specified in the arguments.
  int RequiredStackSizeForCallerSaved(SaveFPRegsMode fp_mode,
                                      Register exclusion) const;

  // Push caller saved registers on the stack, and return the number of bytes
  // stack pointer is adjusted.
  int PushCallerSaved(SaveFPRegsMode fp_mode, Register exclusion = no_reg);

  // Restore caller saved registers from the stack, and return the number of
  // bytes stack pointer is adjusted.
  int PopCallerSaved(SaveFPRegsMode fp_mode, Register exclusion = no_reg);

  // Move an immediate into register dst, and return an Operand object for use
  // with a subsequent instruction that accepts a shift. The value moved into
  // dst is not necessarily equal to imm; it may have had a shifting operation
  // applied to it that will be subsequently undone by the shift applied in the
  // Operand.
  Operand MoveImmediateForShiftedOp(const Register& dst, int64_t imm,
                                    PreShiftImmMode mode);

  void CheckPageFlag(const Register& object, int mask, Condition cc,
                     Label* condition_met);

  void CheckPageFlag(const Register& object, Register scratch, int mask,
                     Condition cc, Label* condition_met) {
    CheckPageFlag(object, mask, cc, condition_met);
  }

  // Compare a register with an operand, and branch to label depending on the
  // condition. May corrupt the status flags.
  inline void CompareAndBranch(const Register& lhs, const Operand& rhs,
                               Condition cond, Label* label);
  inline void CompareTaggedAndBranch(const Register& lhs, const Operand& rhs,
                                     Condition cond, Label* label);

  // Test the bits of register defined by bit_pattern, and branch if ANY of
  // those bits are set. May corrupt the status flags.
  inline void TestAndBranchIfAnySet(const Register& reg,
                                    const uint64_t bit_pattern, Label* label);

  // Test the bits of register defined by bit_pattern, and branch if ALL of
  // those bits are clear (ie. not set.) May corrupt the status flags.
  inline void TestAndBranchIfAllClear(const Register& reg,
                                      const uint64_t bit_pattern, Label* label);

  inline void Brk(int code);

  inline void JumpIfSmi(Register value, Label* smi_label,
                        Label* not_smi_label = nullptr);

  inline void JumpIfEqual(Register x, int32_t y, Label* dest);
  inline void JumpIfLessThan(Register x, int32_t y, Label* dest);

  void JumpIfMarking(Label* is_marking,
                     Label::Distance condition_met_distance = Label::kFar);
  void JumpIfNotMarking(Label* not_marking,
                        Label::Distance condition_met_distance = Label::kFar);

  void LoadMap(Register dst, Register object);
  void LoadCompressedMap(Register dst, Register object);

  void LoadFeedbackVector(Register dst, Register closure, Register scratch,
                          Label* fbv_undef);

  inline void Fmov(VRegister fd, VRegister fn);
  inline void Fmov(VRegister fd, Register rn);
  // Provide explicit double and float interfaces for FP immediate moves, rather
  // than relying on implicit C++ casts. This allows signalling NaNs to be
  // preserved when the immediate matches the format of fd. Most systems convert
  // signalling NaNs to quiet NaNs when converting between float and double.
  inline void Fmov(VRegister fd, double imm);
  inline void Fmov(VRegister fd, float imm);
  // Provide a template to allow other types to be converted automatically.
  template <typename T>
  void Fmov(VRegister fd, T imm) {
    DCHECK(allow_macro_instructions());
    Fmov(fd, static_cast<double>(imm));
  }
  inline void Fmov(Register rd, VRegister fn);

  void Movi(const VRegister& vd, uint64_t imm, Shift shift = LSL,
            int shift_amount = 0);
  void Movi(const VRegister& vd, uint64_t hi, uint64_t lo);

  void LoadFromConstantsTable(Register destination, int constant_index) final;
  void LoadRootRegisterOffset(Register destination, intptr_t offset) final;
  void LoadRootRelative(Register destination, int32_t offset) final;
  void StoreRootRelative(int32_t offset, Register value) final;

  // Operand pointing to an external reference.
  // May emit code to set up the scratch register. The operand is
  // only guaranteed to be correct as long as the scratch register
  // isn't changed.
  // If the operand is used more than once, use a scratch register
  // that is guaranteed not to be clobbered.
  MemOperand ExternalReferenceAsOperand(ExternalReference reference,
                                        Register scratch);
  MemOperand ExternalReferenceAsOperand(IsolateFieldId id) {
    return ExternalReferenceAsOperand(ExternalReference::Create(id), no_reg);
  }

  void Jump(Register target, Condition cond = al);
  void Jump(Address target, RelocInfo::Mode rmode, Condition cond = al);
  void Jump(Handle<Code> code, RelocInfo::Mode rmode, Condition cond = al);
  void Jump(const ExternalReference& reference);

  void Call(Register target);
  void Call(Address target, RelocInfo::Mode rmode);
  void Call(Handle<Code> code, RelocInfo::Mode rmode = RelocInfo::CODE_TARGET);
  void Call(ExternalReference target);

  // Generate an indirect call (for when a direct call's range is not adequate).
  void IndirectCall(Address target, RelocInfo::Mode rmode);

  // Load the builtin given by the Smi in |builtin| into |target|.
  void LoadEntryFromBuiltinIndex(Register builtin, Register target);
  void LoadEntryFromBuiltin(Builtin builtin, Register destination);
  MemOperand EntryFromBuiltinAsOperand(Builtin builtin);
  void CallBuiltinByIndex(Register builtin, Register target);
  void CallBuiltin(Builtin builtin);
  void TailCallBuiltin(Builtin builtin, Condition cond = al);

  // Load code entry point from the Code object.
  void LoadCodeInstructionStart(Register destination, Register code_object,
                                CodeEntrypointTag tag);
  void CallCodeObject(Register code_object, CodeEntrypointTag tag);
  void JumpCodeObject(Register code_object, CodeEntrypointTag tag,
                      JumpMode jump_mode = JumpMode::kJump);

  // Convenience functions to call/jmp to the code of a JSFunction object.
  void CallJSFunction(Register function_object);
  void JumpJSFunction(Register function_object,
                      JumpMode jump_mode = JumpMode::kJump);

  // Generates an instruction sequence s.t. the return address points to the
  // instruction following the call.
  // The return address on the stack is used by frame iteration.
  void StoreReturnAddressAndCall(Register target);

  void BailoutIfDeoptimized();
  void CallForDeoptimization(Builtin target, int deopt_id, Label* exit,
                             DeoptimizeKind kind, Label* ret,
                             Label* jump_deoptimization_entry_label);

  // Calls a C function and cleans up the space for arguments allocated
  // by PrepareCallCFunction. The called function is not allowed to trigger a
  // garbage collection, since that might move the code and invalidate the
  // return address (unless this is somehow accounted for by the called
  // function).
  int CallCFunction(
      ExternalReference function, int num_reg_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);
  int CallCFunction(
      ExternalReference function, int num_reg_arguments,
      int num_double_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);
  int CallCFunction(
      Register function, int num_reg_arguments, int num_double_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);

  // Performs a truncating conversion of a floating point number as used by
  // the JS bitwise operations. See ECMA-262 9.5: ToInt32.
  // Exits with 'result' holding the answer.
  void TruncateDoubleToI(Isolate* isolate, Zone* zone, Register result,
                         DoubleRegister double_input, StubCallMode stub_mode,
                         LinkRegisterStatus lr_status);

  inline void Mul(const Register& rd, const Register& rn, const Register& rm);

  inline void Fcvtzs(const Register& rd, const VRegister& fn);
  void Fcvtzs(const VRegister& vd, const VRegister& vn, int fbits = 0) {
    DCHECK(allow_macro_instructions());
    fcvtzs(vd, vn, fbits);
  }

  void Fjcvtzs(const Register& rd, const VRegister& vn) {
    DCHECK(allow_macro_instructions());
    DCHECK(!rd.IsZero());
    fjcvtzs(rd, vn);
  }

  inline void Fcvtzu(const Register& rd, const VRegister& fn);
  void Fcvtzu(const VRegister& vd, const VRegister& vn, int fbits = 0) {
    DCHECK(allow_macro_instructions());
    fcvtzu(vd, vn, fbits);
  }

  inline void Madd(const Register& rd, const Register& rn, const Register& rm,
                   const Register& ra);
  inline void Mneg(const Register& rd, const Register& rn, const Register& rm);
  inline void Sdiv(const Register& rd, const Register& rn, const Register& rm);
  inline void Udiv(const Register& rd, const Register& rn, const Register& rm);
  inline void Msub(const Register& rd, const Register& rn, const Register& rm,
                   const Register& ra);

  inline void Lsl(const Register& rd, const Register& rn, unsigned shift);
  inline void Lsl(const Register& rd, const Register& rn, const Register& rm);
  inline void Umull(const Register& rd, const Register& rn, const Register& rm);
  inline void Umulh(const Register& rd, const Register& rn, const Register& rm);
  inline void Smull(const Register& rd, const Register& rn, const Register& rm);
  inline void Smulh(const Register& rd, const Register& rn, const Register& rm);

  inline void Sxtb(const Register& rd, const Register& rn);
  inline void Sxth(const Register& rd, const Register& rn);
  inline void Sxtw(const Register& rd, const Register& rn);
  inline void Ubfiz(const Register& rd, const Register& rn, unsigned lsb,
                    unsigned width);
  inline void Sbfiz(const Register& rd, const Register& rn, unsigned lsb,
                    unsigned width);
  inline void Ubfx(const Register& rd, const Register& rn, unsigned lsb,
                   unsigned width);
  inline void Lsr(const Register& rd, const Register& rn, unsigned shift);
  inline void Lsr(const Register& rd, const Register& rn, const Register& rm);
  inline void Ror(const Register& rd, const Register& rs, unsigned shift);
  inline void Ror(const Register& rd, const Register& rn, const Register& rm);
  inline void Cmn(const Register& rn, const Operand& operand);
  inline void Fadd(const VRegister& fd, const VRegister& fn,
                   const VRegister& fm);
  inline void Fcmp(const VRegister& fn, const VRegister& fm);
  inline void Fcmp(const VRegister& fn, double value);
  inline void Fabs(const VRegister& fd, const VRegister& fn);
  inline void Fmul(const VRegister& fd, const VRegister& fn,
                   const VRegister& fm);
  inline void Fsub(const VRegister& fd, const VRegister& fn,
                   const VRegister& fm);
  inline void Fdiv(const VRegister& fd, const VRegister& fn,
                   const VRegister& fm);
  inline void Fmax(const VRegister& fd, const VRegister& fn,
                   const VRegister& fm);
  inline void Fmin(const VRegister& fd, const VRegister& fn,
                   const VRegister& fm);
  inline void Rbit(const Register& rd, const Register& rn);
  inline void Rev(const Register& rd, const Register& rn);

  enum AdrHint {
    // The target must be within the immediate range of adr.
    kAdrNear,
    // The target may be outside of the immediate range of adr. Additional
    // instructions may be emitted.
    kAdrFar
  };
  void Adr(const Register& rd, Label* label, AdrHint = kAdrNear);

  // Add/sub with carry macros.
  inline void Adc(const Register& rd, const Register& rn,
                  const Operand& operand);

  // Conditional macros.
  inline void Ccmp(const Register& rn, const Operand& operand, StatusFlags nzcv,
                   Condition cond);
  inline void CcmpTagged(const Register& rn, const Operand& operand,
                         StatusFlags nzcv, Condition cond);
  inline void Ccmn(const Register& rn, const Operand& operand, StatusFlags nzcv,
                   Condition cond);

  inline void Clz(const Register& rd, const Register& rn);

  // Poke 'src' onto the stack. The offset is in bytes. The stack pointer must
  // be 16 byte aligned.
  // When the optional template argument is kSignLR and control flow integrity
  // measures are enabled, we sign the link register before poking it onto the
  // stack. 'src' must be lr in this case.
  template <StoreLRMode lr_mode = kDontStoreLR>
  void Poke(const CPURegister& src, const Operand& offset);

  // Peek at a value on the stack, and put it in 'dst'. The offset is in bytes.
  // The stack pointer must be aligned to 16 bytes.
  // When the optional template argument is kAuthLR and control flow integrity
  // measures are enabled, we authenticate the link register after peeking the
  // value. 'dst' must be lr in this case.
  template <LoadLRMode lr_mode = kDontLoadLR>
  void Peek(const CPURegister& dst, const Operand& offset);

  // Poke 'src1' and 'src2' onto the stack. The values written will be adjacent
  // with 'src2' at a higher address than 'src1'. The offset is in bytes. The
  // stack pointer must be 16 byte aligned.
  void PokePair(const CPURegister& src1, const CPURegister& src2, int offset);

  inline void Sbfx(const Register& rd, const Register& rn, unsigned lsb,
                   unsigned width);

  inline void Bfi(const Register& rd, const Register& rn, unsigned lsb,
                  unsigned width);

  inline void Scvtf(const VRegister& fd, const Register& rn,
                    unsigned fbits = 0);
  void Scvtf(const VRegister& vd, const VRegister& vn, int fbits = 0) {
    DCHECK(allow_macro_instructions());
    scvtf(vd, vn, fbits);
  }
  inline void Ucvtf(const VRegister& fd, const Register& rn,
                    unsigned fbits = 0);
  void Ucvtf(const VRegister& vd, const VRegister& vn, int fbits = 0) {
    DCHECK(allow_macro_instructions());
    ucvtf(vd, vn, fbits);
  }

  void AssertFPCRState(Register fpcr = NoReg) NOOP_UNLESS_DEBUG_CODE;
  void CanonicalizeNaN(const VRegister& dst, const VRegister& src);
  void CanonicalizeNaN(const VRegister& reg) { CanonicalizeNaN(reg, reg); }

  inline void CmovX(const Register& rd, const Register& rn, Condition cond);
  inline void Cset(const Register& rd, Condition cond);
  inline void Csetm(const Register& rd, Condition cond);
  inline void Fccmp(const VRegister& fn, const VRegister& fm, StatusFlags nzcv,
                    Condition cond);
  inline void Fccmp(const VRegister& fn, const double value, StatusFlags nzcv,
                    Condition cond);
  inline void Csinc(const Register& rd, const Register& rn, const Register& rm,
                    Condition cond);

  inline void Fcvt(const VRegister& fd, const VRegister& fn);

  int ActivationFrameAlignment();

  void Ins(const VRegister& vd, int vd_index, const VRegister& vn,
           int vn_index) {
    DCHECK(allow_macro_instructions());
    ins(vd, vd_index, vn, vn_index);
  }
  void Ins(const VRegister& vd, int vd_index, const Register& rn) {
    DCHECK(allow_macro_instructions());
    ins(vd, vd_index, rn);
  }

  inline void Bl(Label* label);
  inline void Br(const Register& xn);

  inline void Uxtb(const Register& rd, const Register& rn);
  inline void Uxth(const Register& rd, const Register& rn);
  inline void Uxtw(const Register& rd, const Register& rn);

  void Dup(const VRegister& vd, const VRegister& vn, int index) {
    DCHECK(allow_macro_instructions());
    dup(vd, vn, index);
  }
  void Dup(const VRegister& vd, const Register& rn) {
    DCHECK(allow_macro_instructions());
    dup(vd, rn);
  }

#define DECLARE_FUNCTION(FN, REGTYPE, REG, REG2, OP) \
  inline void FN(const REGTYPE REG, const REGTYPE REG2, const MemOperand& addr);
  LSPAIR_MACRO_LIST(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION

  void St1(const VRegister& vt, const MemOperand& dst) {
    DCHECK(allow_macro_instructions());
    st1(vt, dst);
  }
  void St1(const VRegister& vt, const VRegister& vt2, const MemOperand& dst) {
    DCHECK(allow_macro_instructions());
    st1(vt, vt2, dst);
  }
  void St1(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const MemOperand& dst) {
    DCHECK(allow_macro_instructions());
    st1(vt, vt2, vt3, dst);
  }
  void St1(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, const MemOperand& dst) {
    DCHECK(allow_macro_instructions());
    st1(vt, vt2, vt3, vt4, dst);
  }
  void St1(const VRegister& vt, int lane, const MemOperand& dst) {
    DCHECK(allow_macro_instructions());
    st1(vt, lane, dst);
  }

#define NEON_2VREG_SHIFT_MACRO_LIST(V) \
  V(rshrn, Rshrn)                      \
  V(rshrn2, Rshrn2)                    \
  V(shl, Shl)                          \
  V(shll, Shll)                        \
  V(shll2, Shll2)                      \
  V(shrn, Shrn)                        \
  V(shrn2, Shrn2)                      \
  V(sli, Sli)                          \
  V(sqrshrn, Sqrshrn)                  \
  V(sqrshrn2, Sqrshrn2)                \
  V(sqrshrun, Sqrshrun)                \
  V(sqrshrun2, Sqrshrun2)              \
  V(sqshl, Sqshl)                      \
  V(sqshlu, Sqshlu)                    \
  V(sqshrn, Sqshrn)                    \
  V(sqshrn2, Sqshrn2)                  \
  V(sqshrun, Sqshrun)                  \
  V(sqshrun2, Sqshrun2)                \
  V(sri, Sri)                          \
  V(srshr, Srshr)                      \
  V(srsra, Srsra)                      \
  V(sshll, Sshll)                      \
  V(sshll2, Sshll2)                    \
  V(sshr, Sshr)                        \
  V(ssra, Ssra)                        \
  V(uqrshrn, Uqrshrn)                  \
  V(uqrshrn2, Uqrshrn2)                \
  V(uqshl, Uqshl)                      \
  V(uqshrn, Uqshrn)                    \
  V(uqshrn2, Uqshrn2)                  \
  V(urshr, Urshr)                      \
  V(ursra, Ursra)                      \
  V(ushll, Ushll)                      \
  V(ushll2, Ushll2)                    \
  V(ushr, Ushr)                        \
  V(usra, Usra)

#define DEFINE_MACRO_ASM_FUNC(ASM, MASM)                           \
  void MASM(const VRegister& vd, const VRegister& vn, int shift) { \
    DCHECK(allow_macro_instructions());                            \
    ASM(vd, vn, shift);                                            \
  }
  NEON_2VREG_SHIFT_MACRO_LIST(DEFINE_MACRO_ASM_FUNC)
#undef DEFINE_MACRO_ASM_FUNC

  void Umov(const Register& rd, const VRegister& vn, int vn_index) {
    DCHECK(allow_macro_instructions());
    umov(rd, vn, vn_index);
  }
  void Tbl(const VRegister& vd, const VRegister& vn, const VRegister& vm) {
    DCHECK(allow_macro_instructions());
    tbl(vd, vn, vm);
  }
  void Tbl(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vm) {
    DCHECK(allow_macro_instructions());
    tbl(vd, vn, vn2, vm);
  }
  void Tbl(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vn3, const VRegister& vm) {
    DCHECK(allow_macro_instructions());
    tbl(vd, vn, vn2, vn3, vm);
  }
  void Tbl(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vn3, const VRegister& vn4, const VRegister& vm) {
    DCHECK(allow_macro_instructions());
    tbl(vd, vn, vn2, vn3, vn4, vm);
  }
  void Ext(const VRegister& vd, const VRegister& vn, const VRegister& vm,
           int index) {
    DCHECK(allow_macro_instructions());
    ext(vd, vn, vm, index);
  }

  void Smov(const Register& rd, const VRegister& vn, int vn_index) {
    DCHECK(allow_macro_instructions());
    smov(rd, vn, vn_index);
  }

// Load-acquire/store-release macros.
#define DECLARE_FUNCTION(FN, OP) \
  inline void FN(const Register& rt, const Register& rn);
  LDA_STL_MACRO_LIST(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION

#define DECLARE_FUNCTION(FN, OP) \
  inline void FN(const Register& rs, const Register& rt, const MemOperand& src);
  CAS_SINGLE_MACRO_LIST(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION

#define DECLARE_FUNCTION(FN, OP)                                              \
  inline void FN(const Register& rs, const Register& rs2, const Register& rt, \
                 const Register& rt2, const MemOperand& src);
  CAS_PAIR_MACRO_LIST(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION

#define DECLARE_LOAD_FUNCTION(FN, OP) \
  inline void FN(const Register& rs, const Register& rt, const MemOperand& src);
#define DECLARE_STORE_FUNCTION(FN, OP) \
  inline void FN(const Register& rs, const MemOperand& src);

  ATOMIC_MEMORY_SIMPLE_MACRO_LIST(ATOMIC_MEMORY_LOAD_MACRO_MODES,
                                  DECLARE_LOAD_FUNCTION, Ld, ld)
  ATOMIC_MEMORY_SIMPLE_MACRO_LIST(ATOMIC_MEMORY_STORE_MACRO_MODES,
                                  DECLARE_STORE_FUNCTION, St, st)

#define DECLARE_SWP_FUNCTION(FN, OP) \
  inline void FN(const Register& rs, const Register& rt, const MemOperand& src);

  ATOMIC_MEMORY_LOAD_MACRO_MODES(DECLARE_SWP_FUNCTION, Swp, swp)

#undef DECLARE_LOAD_FUNCTION
#undef DECLARE_STORE_FUNCTION
#undef DECLARE_SWP_FUNCTION

  // Load an object from the root table.
  void LoadRoot(Register destination, RootIndex index) final;
  void LoadTaggedRoot(Register destination, RootIndex index);
  void PushRoot(RootIndex index);

  inline void Ret(const Register& xn = lr);

  // Perform a conversion from a double to a signed int64. If the input fits in
  // range of the 64-bit result, execution branches to done. Otherwise,
  // execution falls through, and the sign of the result can be used to
  // determine if overflow was towards positive or negative infinity.
  //
  // On successful conversion, the least significant 32 bits of the result are
  // equivalent to the ECMA-262 operation "ToInt32".
  void TryConvertDoubleToInt64(Register result, DoubleRegister input,
                               Label* done);

  inline void Mrs(const Register& rt, SystemRegister sysreg);
  inline void Msr(SystemRegister sysreg, const Register& rt);

  // Prologue claims an extra slot due to arm64's alignement constraints.
  static constexpr int kExtraSlotClaimedByPrologue = 1;
  // Generates function prologue code.
  void Prologue();

  void Cmgt(const VRegister& vd, const VRegister& vn, int imm) {
    DCHECK(allow_macro_instructions());
    cmgt(vd, vn, imm);
  }
  void Cmge(const VRegister& vd, const VRegister& vn, int imm) {
    DCHECK(allow_macro_instructions());
    cmge(vd, vn, imm);
  }
  void Cmeq(const VRegister& vd, const VRegister& vn, int imm) {
    DCHECK(allow_macro_instructions());
    cmeq(vd, vn, imm);
  }
  void Cmlt(const VRegister& vd, const VRegister& vn, int imm) {
    DCHECK(allow_macro_instructions());
    cmlt(vd, vn, imm);
  }
  void Cmle(const VRegister& vd, const VRegister& vn, int imm) {
    DCHECK(allow_macro_instructions());
    cmle(vd, vn, imm);
  }

  inline void Neg(const Register& rd, const Operand& operand);
  inline void Negs(const Register& rd, const Operand& operand);

  // Compute rd = abs(rm).
  // This function clobbers the condition flags. On output the overflow flag is
  // set iff the negation overflowed.
  //
  // If rm is the minimum representable value, the result is not representable.
  // Handlers for each case can be specified using the relevant labels.
  void Abs(const Register& rd, const Register& rm,
           Label* is_not_representable = nullptr,
           Label* is_representable = nullptr);

  inline void Cls(const Register& rd, const Register& rn);
  inline void Cneg(const Register& rd, const Register& rn, Condition cond);
  inline void Rev16(const Register& rd, const Register& rn);
  inline void Rev32(const Register& rd, const Register& rn);
  inline void Fcvtns(const Register& rd, const VRegister& fn);
  inline void Fcvtnu(const Register& rd, const VRegister& fn);
  inline void Fcvtms(const Register& rd, const VRegister& fn);
  inline void Fcvtmu(const Register& rd, const VRegister& fn);
  inline void Fcvtas(const Register& rd, const VRegister& fn);
  inline void Fcvtau(const Register& rd, const VRegister& fn);

  // Compute the start of the generated instruction stream from the current PC.
  // This is an alternative to embedding the {CodeObject} handle as a reference.
  void ComputeCodeStartAddress(const Register& rd);

  // ---------------------------------------------------------------------------
  // Pointer compression Support

  // Loads a field containing any tagged value and decompresses it if necessary.
  void LoadTaggedField(const Register& destination,
                       const MemOperand& field_operand);

  // Loads a field containing any tagged value but never decompresses it.
  void LoadTaggedFieldWithoutDecompressing(const Register& destination,
                                           const MemOperand& field_operand);

  // Loads a field containing a tagged signed value and decompresses it if
  // necessary.
  void LoadTaggedSignedField(const Register& destination,
                             const MemOperand& field_operand);

  // Loads a field containing smi value and untags it.
  void SmiUntagField(Register dst, const MemOperand& src);

  // Compresses and stores tagged value to given on-heap location.
  void StoreTaggedField(const Register& value,
                        const MemOperand& dst_field_operand);
  void StoreTwoTaggedFields(const Register& value,
                            const MemOperand& dst_field_operand);

  // For compatibility with platform-independent code.
  void StoreTaggedField(const MemOperand& dst_field_operand,
                        const Register& value) {
    StoreTaggedField(value, dst_field_operand);
  }

  void AtomicStoreTaggedField(const Register& value, const Register& dst_base,
                              const Register& dst_index, const Register& temp);

  void DecompressTaggedSigned(const Register& destination,
                              const MemOperand& field_operand);
  void DecompressTagged(const Register& destination,
                        const MemOperand& field_operand);
  void DecompressTagged(const Register& destination, const Register& source);
  void DecompressTagged(const Register& destination, Tagged_t immediate);
  void DecompressProtected(const Register& destination,
                           const MemOperand& field_operand);

  void AtomicDecompressTaggedSigned(const Register& destination,
                                    const Register& base, const Register& index,
                                    const Register& temp);
  void AtomicDecompressTagged(const Register& destination, const Register& base,
                              const Register& index, const Register& temp);

  // Restore FP and LR from the values stored in the current frame. This will
  // authenticate the LR when pointer authentication is enabled.
  void RestoreFPAndLR();

#if V8_ENABLE_WEBASSEMBLY
  void StoreReturnAddressInWasmExitFrame(Label* return_location);
#endif  // V8_ENABLE_WEBASSEMBLY

  // Wasm helpers. These instructions don't have direct lowering
  // to native instructions. These helpers allow us to define the optimal code
  // sequence, and be used in both TurboFan and Liftoff.
  void PopcntHelper(Register dst, Register src);
  void I8x16BitMask(Register dst, VRegister src, VRegister temp = NoVReg);
  void I16x8BitMask(Register dst, VRegister src);
  void I32x4BitMask(Register dst, VRegister src);
  void I64x2BitMask(Register dst, VRegister src);
  void I64x2AllTrue(Register dst, VRegister src);

  // ---------------------------------------------------------------------------
  // V8 Sandbox support

  // Transform a SandboxedPointer from/to its encoded form, which is used when
  // the pointer is stored on the heap and ensures that the pointer will always
  // point into the sandbox.
  void DecodeSandboxedPointer(Register value);
  void LoadSandboxedPointerField(Register destination,
                                 MemOperand field_operand);
  void StoreSandboxedPointerField(Register value, MemOperand dst_field_operand);

  // Loads a field containing an off-heap ("external") pointer and does
  // necessary decoding if the sandbox is enabled.
  void LoadExternalPointerField(Register destination, MemOperand field_operand,
                                ExternalPointerTag tag,
                                Register isolate_root = Register::no_reg());

  // Load a trusted pointer field.
  // When the sandbox is enabled, these are indirect pointers using the trusted
  // pointer table. Otherwise they are regular tagged fields.
  void LoadTrustedPointerField(Register destination, MemOperand field_operand,
                               IndirectPointerTag tag);
  // Store a trusted pointer field.
  void StoreTrustedPointerField(Register value, MemOperand dst_field_operand);

  // Load a code pointer field.
  // These are special versions of trusted pointers that, when the sandbox is
  // enabled, reference code objects through the code pointer table.
  void LoadCodePointerField(Register destination, MemOperand field_operand) {
    LoadTrustedPointerField(destination, field_operand,
                            kCodeIndirectPointerTag);
  }
  // Store a code pointer field.
  void StoreCodePointerField(Register value, MemOperand dst_field_operand) {
    StoreTrustedPointerField(value, dst_field_operand);
  }

  // Load an indirect pointer field.
  // Only available when the sandbox is enabled, but always visible to avoid
  // having to place the #ifdefs into the caller.
  void LoadIndirectPointerField(Register destination, MemOperand field_operand,
                                IndirectPointerTag tag);

  // Store an indirect pointer field.
  // Only available when the sandbox is enabled, but always visible to avoid
  // having to place the #ifdefs into the caller.
  void StoreIndirectPointerField(Register value, MemOperand dst_field_operand);

#ifdef V8_ENABLE_SANDBOX
  // Retrieve the heap object referenced by the given indirect pointer handle,
  // which can either be a trusted pointer handle or a code pointer handle.
  void ResolveIndirectPointerHandle(Register destination, Register handle,
                                    IndirectPointerTag tag);

  // Retrieve the heap object referenced by the given trusted pointer handle.
  void ResolveTrustedPointerHandle(Register destination, Register handle,
                                   IndirectPointerTag tag);

  // Retrieve the Code object referenced by the given code pointer handle.
  void ResolveCodePointerHandle(Register destination, Register handle);

  // Load the pointer to a Code's entrypoint via a code pointer.
  // Only available when the sandbox is enabled as it requires the code pointer
  // table.
  void LoadCodeEntrypointViaCodePointer(Register destination,
                                        MemOperand field_operand,
                                        CodeEntrypointTag tag);
#endif

  // Load a protected pointer field.
  void LoadProtectedPointerField(Register destination,
                                 MemOperand field_operand);

  // Instruction set functions ------------------------------------------------
  // Logical macros.
  inline void Bics(const Register& rd, const Register& rn,
                   const Operand& operand);

  inline void Adcs(const Register& rd, const Register& rn,
                   const Operand& operand);
  inline void Sbc(const Register& rd, const Register& rn,
                  const Operand& operand);
  inline void Sbcs(const Register& rd, const Register& rn,
                   const Operand& operand);
  inline void Ngc(const Register& rd, const Operand& operand);
  inline void Ngcs(const Register& rd, const Operand& operand);

#define DECLARE_FUNCTION(FN, OP) \
  inline void FN(const Register& rs, const Register& rt, const Register& rn);
  STLX_MACRO_LIST(DECLARE_FUNCTION)
#undef DECLARE_FUNCTION

  // Branch type inversion relies on these relations.
  static_assert((reg_zero == (reg_not_zero ^ 1)) &&
                (reg_bit_clear == (reg_bit_set ^ 1)) &&
                (always == (never ^ 1)));

  inline void Bfxil(const Register& rd, const Register& rn, unsigned lsb,
                    unsigned width);
  inline void Cinc(const Register& rd, const Register& rn, Condition cond);
  inline void Cinv(const Register& rd, const Register& rn, Condition cond);
  inline void CzeroX(const Register& rd, Condition cond);
  inline void Csinv(const Register& rd, const Register& rn, const Register& rm,
                    Condition cond);
  inline void Csneg(const Register& rd, const Register& rn, const Register& rm,
                    Condition cond);
  inline void Extr(const Register& rd, const Register& rn, const Register& rm,
                   unsigned lsb);
  void Fcvtl2(const VRegister& vd, const VRegister& vn) {
    DCHECK(allow_macro_instructions());
    fcvtl2(vd, vn);
  }
  void Fcvtn2(const VRegister& vd, const VRegister& vn) {
    DCHECK(allow_macro_instructions());
    fcvtn2(vd, vn);
  }
  void Fcvtxn(const VRegister& vd, const VRegister& vn) {
    DCHECK(allow_macro_instructions());
    fcvtxn(vd, vn);
  }
  void Fcvtxn2(const VRegister& vd, const VRegister& vn) {
    DCHECK(allow_macro_instructions());
    fcvtxn2(vd, vn);
  }
  inline void Fmadd(const VRegister& fd, const VRegister& fn,
                    const VRegister& fm, const VRegister& fa);
  inline void Fmaxnm(const VRegister& fd, const VRegister& fn,
                     const VRegister& fm);
  inline void Fminnm(const VRegister& fd, const VRegister& fn,
                     const VRegister& fm);
  inline void Fmsub(const VRegister& fd, const VRegister& fn,
                    const VRegister& fm, const VRegister& fa);
  inline void Fnmadd(const VRegister& fd, const VRegister& fn,
                     const VRegister& fm, const VRegister& fa);
  inline void Fnmsub(const VRegister& fd, const VRegister& fn,
                     const VRegister& fm, const VRegister& fa);
  inline void Hint(SystemHint code);
  inline void Hlt(int code);
  inline void Ldnp(const CPURegister& rt, const CPURegister& rt2,
                   const MemOperand& src);
  inline void Movk(const Register& rd, uint64_t imm, int shift = -1);
  inline void Nop() { nop(); }
  void Mvni(const VRegister& vd, const int imm8, Shift shift = LSL,
            const int shift_amount = 0) {
    DCHECK(allow_macro_instructions());
    mvni(vd, imm8, shift, shift_amount);
  }
  inline void Smaddl(const Register& rd, const Register& rn, const Register& rm,
                     const Register& ra);
  inline void Smsubl(const Register& rd, const Register& rn, const Register& rm,
                     const Register& ra);
  inline void Stnp(const CPURegister& rt, const CPURegister& rt2,
                   const MemOperand& dst);
  inline void Umaddl(const Register& rd, const Register& rn, const Register& rm,
                     const Register& ra);
  inline void Umsubl(const Register& rd, const Register& rn, const Register& rm,
                     const Register& ra);

  void Ld1(const VRegister& vt, const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld1(vt, src);
  }
  void Ld1(const VRegister& vt, const VRegister& vt2, const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld1(vt, vt2, src);
  }
  void Ld1(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld1(vt, vt2, vt3, src);
  }
  void Ld1(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld1(vt, vt2, vt3, vt4, src);
  }
  void Ld1(const VRegister& vt, int lane, const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld1(vt, lane, src);
  }
  void Ld1r(const VRegister& vt, const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld1r(vt, src);
  }
  void Ld2(const VRegister& vt, const VRegister& vt2, const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld2(vt, vt2, src);
  }
  void Ld2(const VRegister& vt, const VRegister& vt2, int lane,
           const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld2(vt, vt2, lane, src);
  }
  void Ld2r(const VRegister& vt, const VRegister& vt2, const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld2r(vt, vt2, src);
  }
  void Ld3(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld3(vt, vt2, vt3, src);
  }
  void Ld3(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           int lane, const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld3(vt, vt2, vt3, lane, src);
  }
  void Ld3r(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
            const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld3r(vt, vt2, vt3, src);
  }
  void Ld4(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld4(vt, vt2, vt3, vt4, src);
  }
  void Ld4(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, int lane, const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld4(vt, vt2, vt3, vt4, lane, src);
  }
  void Ld4r(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
            const VRegister& vt4, const MemOperand& src) {
    DCHECK(allow_macro_instructions());
    ld4r(vt, vt2, vt3, vt4, src);
  }
  void St2(const VRegister& vt, const VRegister& vt2, const MemOperand& dst) {
    DCHECK(allow_macro_instructions());
    st2(vt, vt2, dst);
  }
  void St3(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const MemOperand& dst) {
    DCHECK(allow_macro_instructions());
    st3(vt, vt2, vt3, dst);
  }
  void St4(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, const MemOperand& dst) {
    DCHECK(allow_macro_instructions());
    st4(vt, vt2, vt3, vt4, dst);
  }
  void St2(const VRegister& vt, const VRegister& vt2, int lane,
           const MemOperand& dst) {
    DCHECK(allow_macro_instructions());
    st2(vt, vt2, lane, dst);
  }
  void St3(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           int lane, const MemOperand& dst) {
    DCHECK(allow_macro_instructions());
    st3(vt, vt2, vt3, lane, dst);
  }
  void St4(const VRegister& vt, const VRegister& vt2, const VRegister& vt3,
           const VRegister& vt4, int lane, const MemOperand& dst) {
    DCHECK(allow_macro_instructions());
    st4(vt, vt2, vt3, vt4, lane, dst);
  }
  void Tbx(const VRegister& vd, const VRegister& vn, const VRegister& vm) {
    DCHECK(allow_macro_instructions());
    tbx(vd, vn, vm);
  }
  void Tbx(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vm) {
    DCHECK(allow_macro_instructions());
    tbx(vd, vn, vn2, vm);
  }
  void Tbx(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vn3, const VRegister& vm) {
    DCHECK(allow_macro_instructions());
    tbx(vd, vn, vn2, vn3, vm);
  }
  void Tbx(const VRegister& vd, const VRegister& vn, const VRegister& vn2,
           const VRegister& vn3, const VRegister& vn4, const VRegister& vm) {
    DCHECK(allow_macro_instructions());
    tbx(vd, vn, vn2, vn3, vn4, vm);
  }

  inline void PushSizeRegList(RegList registers, unsigned reg_size) {
    PushCPURegList(CPURegList(reg_size, registers));
  }
  inline void PushSizeRegList(DoubleRegList registers, unsigned reg_size) {
    PushCPURegList(CPURegList(reg_size, registers));
  }
  inline void PopSizeRegList(RegList registers, unsigned reg_size) {
    PopCPURegList(CPURegList(reg_size, registers));
  }
  inline void PopSizeRegList(DoubleRegList registers, unsigned reg_size) {
    PopCPURegList(CPURegList(reg_size, registers));
  }
  inline void PushXRegList(RegList regs) {
    PushSizeRegList(regs, kXRegSizeInBits);
  }
  inline void PopXRegList(RegList regs) {
    PopSizeRegList(regs, kXRegSizeInBits);
  }
  inline void PushWRegList(RegList regs) {
    PushSizeRegList(regs, kWRegSizeInBits);
  }
  inline void PopWRegList(RegList regs) {
    PopSizeRegList(regs, kWRegSizeInBits);
  }
  inline void PushQRegList(DoubleRegList regs) {
    PushSizeRegList(regs, kQRegSizeInBits);
  }
  inline void PopQRegList(DoubleRegList regs) {
    PopSizeRegList(regs, kQRegSizeInBits);
  }
  inline void PushDRegList(DoubleRegList regs) {
    PushSizeRegList(regs, kDRegSizeInBits);
  }
  inline void PopDRegList(DoubleRegList regs) {
    PopSizeRegList(regs, kDRegSizeInBits);
  }
  inline void PushSRegList(DoubleRegList regs) {
    PushSizeRegList(regs, kSRegSizeInBits);
  }
  inline void PopSRegList(DoubleRegList regs) {
    PopSizeRegList(regs, kSRegSizeInBits);
  }

  // These PushAll/PopAll respect the order of the registers in the stack from
  // low index to high.
  void PushAll(RegList registers);
  void PopAll(RegList registers);

  inline void PushAll(DoubleRegList registers,
                      int stack_slot_size = kDoubleSize) {
    if (registers.Count() % 2 != 0) {
      DCHECK(!registers.has(fp_zero));
      registers.set(fp_zero);
    }
    PushDRegList(registers);
  }
  inline void PopAll(DoubleRegList registers,
                     int stack_slot_size = kDoubleSize) {
    if (registers.Count() % 2 != 0) {
      DCHECK(!registers.has(fp_zero));
      registers.set(fp_zero);
    }
    PopDRegList(registers);
  }

  // Push the specified register 'count' times.
  void PushMultipleTimes(CPURegister src, Register count);

  // Peek at two values on the stack, and put them in 'dst1' and 'dst2'. The
  // values peeked will be adjacent, with the value in 'dst2' being from a
  // higher address than 'dst1'. The offset is in bytes. The stack pointer must
  // be aligned to 16 bytes.
  void PeekPair(const CPURegister& dst1, const CPURegister& dst2, int offset);

  // Preserve the callee-saved registers (as defined by AAPCS64).
  //
  // Higher-numbered registers are pushed before lower-numbered registers, and
  // thus get higher addresses.
  // Floating-point registers are pushed before general-purpose registers, and
  // thus get higher addresses.
  //
  // When control flow integrity measures are enabled, this method signs the
  // link register before pushing it.
  //
  // Note that registers are not checked for invalid values. Use this method
  // only if you know that the GC won't try to examine the values on the stack.
  void PushCalleeSavedRegisters();

  // Restore the callee-saved registers (as defined by AAPCS64).
  //
  // Higher-numbered registers are popped after lower-numbered registers, and
  // thus come from higher addresses.
  // Floating-point registers are popped after general-purpose registers, and
  // thus come from higher addresses.
  //
  // When control flow integrity measures are enabled, this method
  // authenticates the link register after popping it.
  void PopCalleeSavedRegisters();

  // Tiering support.
  void AssertFeedbackCell(Register object,
                          Register scratch) NOOP_UNLESS_DEBUG_CODE;
  inline void AssertFeedbackVector(Register object);
  void AssertFeedbackVector(Register object,
                            Register scratch) NOOP_UNLESS_DEBUG_CODE;
  void ReplaceClosureCodeWithOptimizedCode(Register optimized_code,
                                           Register closure);
  void GenerateTailCallToReturnedCode(Runtime::FunctionId function_id);
  Condition LoadFeedbackVectorFlagsAndCheckIfNeedsProcessing(
      Register flags, Register feedback_vector, CodeKind current_code_kind);
  void LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      Register flags, Register feedback_vector, CodeKind current_code_kind,
      Label* flags_need_processing);
  void OptimizeCodeOrTailCallOptimizedCodeSlot(Register flags,
                                               Register feedback_vector);

  // Helpers ------------------------------------------------------------------

  template <typename Field>
  void DecodeField(Register dst, Register src) {
    static const int shift = Field::kShift;
    static const int setbits = CountSetBits(Field::kMask, 32);
    Ubfx(dst, src, shift, setbits);
  }

  template <typename Field>
  void DecodeField(Register reg) {
    DecodeField<Field>(reg, reg);
  }

  void JumpIfCodeIsMarkedForDeoptimization(Register code, Register scratch,
                                           Label* if_marked_for_deoptimization);
  void JumpIfCodeIsTurbofanned(Register code, Register scratch,
                               Label* if_marked_for_deoptimization);
  Operand ClearedValue() const;

  Operand ReceiverOperand();

  // ---- SMI and Number Utilities ----

  inline void JumpIfNotSmi(Register value, Label* not_smi_label);

  // Abort execution if argument is not a Map, enabled via
  // --debug-code.
  void AssertMap(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a Code, enabled via
  // --debug-code.
  void AssertCode(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a Constructor, enabled via
  // --debug-code.
  void AssertConstructor(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a JSFunction, enabled via
  // --debug-code.
  void AssertFunction(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a callable JSFunction, enabled via
  // --debug-code.
  void AssertCallableFunction(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a JSGeneratorObject (or subclass),
  // enabled via --debug-code.
  void AssertGeneratorObject(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a JSBoundFunction,
  // enabled via --debug-code.
  void AssertBoundFunction(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not undefined or an AllocationSite,
  // enabled via --debug-code.
  void AssertUndefinedOrAllocationSite(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not smi nor in the pointer compresssion
  // cage, enabled via --debug-code.
  void AssertSmiOrHeapObjectInMainCompressionCage(Register object)
      NOOP_UNLESS_DEBUG_CODE;

  // ---- Calling / Jumping helpers ----

  void CallRuntime(const Runtime::Function* f, int num_arguments);

  // Convenience function: Same as above, but takes the fid instead.
  void CallRuntime(Runtime::FunctionId fid, int num_arguments) {
    CallRuntime(Runtime::FunctionForId(fid), num_arguments);
  }

  // Convenience function: Same as above, but takes the fid instead.
  void CallRuntime(Runtime::FunctionId fid) {
    const Runtime::Function* function = Runtime::FunctionForId(fid);
    CallRuntime(function, function->nargs);
  }

  void TailCallRuntime(Runtime::FunctionId fid);

  // Jump to a runtime routine.
  void JumpToExternalReference(const ExternalReference& builtin,
                               bool builtin_exit_frame = false);

  // Registers used through the invocation chain are hard-coded.
  // We force passing the parameters to ensure the contracts are correctly
  // honoured by the caller.
  // 'function' must be x1.
  // 'actual' must use an immediate or x0.
  // 'expected' must use an immediate or x2.
  // 'call_kind' must be x5.
  void InvokePrologue(Register expected_parameter_count,
                      Register actual_parameter_count, Label* done,
                      InvokeType type);

  // On function call, call into the debugger.
  void CallDebugOnFunctionCall(Register fun, Register new_target,
                               Register expected_parameter_count,
                               Register actual_parameter_count);
  void InvokeFunctionCode(Register function, Register new_target,
                          Register expected_parameter_count,
                          Register actual_parameter_count, InvokeType type);
  // Invoke the JavaScript function in the given register.
  // Changes the current context to the context in the function before invoking.
  void InvokeFunctionWithNewTarget(Register function, Register new_target,
                                   Register actual_parameter_count,
                                   InvokeType type);
  void InvokeFunction(Register function, Register expected_parameter_count,
                      Register actual_parameter_count, InvokeType type);

  // ---- InstructionStream generation helpers ----

  // ---------------------------------------------------------------------------
  // Support functions.

  // Compare object type for heap object.  heap_object contains a non-Smi
  // whose object type should be compared with the given type.  This both
  // sets the flags and leaves the object type in the type_reg register.
  // It leaves the map in the map register (unless the type_reg and map register
  // are the same register).  It leaves the heap object in the heap_object
  // register unless the heap_object register is the same register as one of the
  // other registers.
  void CompareObjectType(Register heap_object, Register map, Register type_reg,
                         InstanceType type);
  // Variant of the above, which only guarantees to set the correct eq/ne flag.
  // Neither map, nor type_reg might be set to any particular value.
  void IsObjectType(Register heap_object, Register scratch1, Register scratch2,
                    InstanceType type);
  // Variant of the above, which compares against a type range rather than a
  // single type (lower_limit and higher_limit are inclusive).
  //
  // Always use unsigned comparisons: ls for a positive result.
  void IsObjectTypeInRange(Register heap_object, Register scratch,
                           InstanceType lower_limit, InstanceType higher_limit);
#if V8_STATIC_ROOTS_BOOL
  // Fast variant which is guaranteed to not actually load the instance type
  // from the map.
  void IsObjectTypeFast(Register heap_object, Register compressed_map_scratch,
                        InstanceType type);
  void CompareInstanceTypeWithUniqueCompressedMap(Register map,
                                                  Register scratch,
                                                  InstanceType type);
#endif  // V8_STATIC_ROOTS_BOOL

  // Compare object type for heap object, and branch if equal (or not.)
  // heap_object contains a non-Smi whose object type should be compared with
  // the given type.  This both sets the flags and leaves the object type in
  // the type_reg register. It leaves the map in the map register (unless the
  // type_reg and map register are the same register).  It leaves the heap
  // object in the heap_object register unless the heap_object register is the
  // same register as one of the other registers.
  void JumpIfObjectType(Register object, Register map, Register type_reg,
                        InstanceType type, Label* if_cond_pass,
                        Condition cond = eq);

  // Fast check if the object is a js receiver type. Assumes only primitive
  // objects or js receivers are passed.
  void JumpIfJSAnyIsNotPrimitive(
      Register heap_object, Register scratch, Label* target,
      Label::Distance distance = Label::kFar,
      Condition condition = Condition::kUnsignedGreaterThanEqual);
  void JumpIfJSAnyIsPrimitive(Register heap_object, Register scratch,
                              Label* target,
                              Label::Distance distance = Label::kFar) {
    return JumpIfJSAnyIsNotPrimitive(heap_object, scratch, target, distance,
                                     Condition::kUnsignedLessThan);
  }

  // Compare instance type in a map.  map contains a valid map object whose
  // object type should be compared with the given type.  This both
  // sets the flags and leaves the object type in the type_reg register.
  void CompareInstanceType(Register map, Register type_reg, InstanceType type);

  // Compare instance type ranges for a map (lower_limit and higher_limit
  // inclusive).
  //
  // Always use unsigned comparisons: ls for a positive result.
  void CompareInstanceTypeRange(Register map, Register type_reg,
                                InstanceType lower_limit,
                                InstanceType higher_limit);

  // Load the elements kind field from a map, and return it in the result
  // register.
  void LoadElementsKindFromMap(Register result, Register map);

  // Compare the object in a register to a value from the root list.
  void CompareRoot(const Register& obj, RootIndex index,
                   ComparisonMode mode = ComparisonMode::kDefault);
  void CompareTaggedRoot(const Register& with, RootIndex index);

  // Compare the object in a register to a value and jump if they are equal.
  void JumpIfRoot(const Register& obj, RootIndex index, Label* if_equal);

  // Compare the object in a register to a value and jump if they are not equal.
  void JumpIfNotRoot(const Register& obj, RootIndex index, Label* if_not_equal);

  // Checks if value is in range [lower_limit, higher_limit] using a single
  // comparison.
  void JumpIfIsInRange(const Register& value, unsigned lower_limit,
                       unsigned higher_limit, Label* on_in_range);

  // ---------------------------------------------------------------------------
  // Frames.

  // Enter exit frame. Exit frames are used when calling C code from generated
  // (JavaScript) code.
  //
  // The only registers modified by this function are the provided scratch
  // register, the frame pointer and the stack pointer.
  //
  // The 'extra_space' argument can be used to allocate some space in the exit
  // frame that will be ignored by the GC. This space will be reserved in the
  // bottom of the frame immediately above the return address slot.
  //
  // Set up a stack frame and registers as follows:
  //         fp[8]: CallerPC (lr)
  //   fp -> fp[0]: CallerFP (old fp)
  //         fp[-8]: SPOffset (new sp)
  //         fp[-16]: CodeObject()
  //         fp[-16 - fp-size]: Saved doubles, if saved_doubles is true.
  //         sp[8]: Memory reserved for the caller if extra_space != 0.
  //                 Alignment padding, if necessary.
  //   sp -> sp[0]: Space reserved for the return address.
  //
  // This function also stores the new frame information in the top frame, so
  // that the new frame becomes the current frame.
  void EnterExitFrame(const Register& scratch, int extra_space,
                      StackFrame::Type frame_type);

  // Leave the current exit frame, after a C function has returned to generated
  // (JavaScript) code.
  //
  // This effectively unwinds the operation of EnterExitFrame:
  //  * The frame information is removed from the top frame.
  //  * The exit frame is dropped.
  void LeaveExitFrame(const Register& scratch, const Register& scratch2);

  // Load the global proxy from the current context.
  void LoadGlobalProxy(Register dst);

  // ---------------------------------------------------------------------------
  // In-place weak references.
  void LoadWeakValue(Register out, Register in, Label* target_if_cleared);

  // ---------------------------------------------------------------------------
  // StatsCounter support

  void IncrementCounter(StatsCounter* counter, int value, Register scratch1,
                        Register scratch2) {
    if (!v8_flags.native_code_counters) return;
    EmitIncrementCounter(counter, value, scratch1, scratch2);
  }
  void EmitIncrementCounter(StatsCounter* counter, int value, Register scratch1,
                            Register scratch2);
  void DecrementCounter(StatsCounter* counter, int value, Register scratch1,
                        Register scratch2) {
    if (!v8_flags.native_code_counters) return;
    EmitIncrementCounter(counter, -value, scratch1, scratch2);
  }

  // ---------------------------------------------------------------------------
  // Stack limit utilities
  void LoadStackLimit(Register destination, StackLimitKind kind);
  void StackOverflowCheck(Register num_args, Label* stack_overflow);

  // ---------------------------------------------------------------------------
  // Garbage collector support (GC).

  // Notify the garbage collector that we wrote a pointer into an object.
  // |object| is the object being stored into, |value| is the object being
  // stored.
  // The offset is the offset from the start of the object, not the offset from
  // the tagged HeapObject pointer.  For use with FieldMemOperand(reg, off).
  void RecordWriteField(
      Register object, int offset, Register value, LinkRegisterStatus lr_status,
      SaveFPRegsMode save_fp, SmiCheck smi_check = SmiCheck::kInline,
      SlotDescriptor slot = SlotDescriptor::ForDirectPointerSlot());

  // For a given |object| notify the garbage collector that the slot at |offset|
  // has been written. |value| is the object being stored.
  void RecordWrite(
      Register object, Operand offset, Register value,
      LinkRegisterStatus lr_status, SaveFPRegsMode save_fp,
      SmiCheck smi_check = SmiCheck::kInline,
      SlotDescriptor slot = SlotDescriptor::ForDirectPointerSlot());

  // ---------------------------------------------------------------------------
  // Debugging.

  void LoadNativeContextSlot(Register dst, int index);

  // Falls through and sets scratch_and_result to 0 on failure, jumps to
  // on_result on success.
  void TryLoadOptimizedOsrCode(Register scratch_and_result,
                               CodeKind min_opt_level, Register feedback_vector,
                               FeedbackSlot slot, Label* on_result,
                               Label::Distance distance);

 protected:
  // The actual Push and Pop implementations. These don't generate any code
  // other than that required for the push or pop. This allows
  // (Push|Pop)CPURegList to bundle together run-time assertions for a large
  // block of registers.
  //
  // Note that size is per register, and is specified in bytes.
  void PushHelper(int count, int size, const CPURegister& src0,
                  const CPURegister& src1, const CPURegister& src2,
                  const CPURegister& src3);
  void PopHelper(int count, int size, const CPURegister& dst0,
                 const CPURegister& dst1, const CPURegister& dst2,
                 const CPURegister& dst3);

  void ConditionalCompareMacro(const Register& rn, const Operand& operand,
                               StatusFlags nzcv, Condition cond,
                               ConditionalCompareOp op);

  void AddSubWithCarryMacro(const Register& rd, const Register& rn,
                            const Operand& operand, FlagsUpdate S,
                            AddSubWithCarryOp op);

  // Call Printf. On a native build, a simple call will be generated, but if the
  // simulator is being used then a suitable pseudo-instruction is used. The
  // arguments and stack must be prepared by the caller as for a normal AAPCS64
  // call to 'printf'.
  //
  // The 'args' argument should point to an array of variable arguments in their
  // proper PCS registers (and in calling order). The argument registers can
  // have mixed types. The format string (x0) should not be included.
  void CallPrintf(int arg_count = 0, const CPURegister* args = nullptr);

 private:
#if DEBUG
  // Tell whether any of the macro instruction can be used. When false the
  // MacroAssembler will assert if a method which can emit a variable number
  // of instructions is called.
  bool allow_macro_instructions_ = true;
#endif

  // Scratch registers available for use by the MacroAssembler.
  CPURegList tmp_list_ = DefaultTmpList();
  CPURegList fptmp_list_ = DefaultFPTmpList();

  // Helps resolve branching to labels potentially out of range.
  // If the label is not bound, it registers the information necessary to later
  // be able to emit a veneer for this branch if necessary.
  // If the label is bound, it returns true if the label (or the previous link
  // in the label chain) is out of range. In that case the caller is responsible
  // for generating appropriate code.
  // Otherwise it returns false.
  // This function also checks wether veneers need to be emitted.
  template <ImmBranchType branch_type>
  bool NeedExtraInstructionsOrRegisterBranch(Label* label) {
    static_assert((branch_type == CondBranchType) ||
                  (branch_type == CompareBranchType) ||
                  (branch_type == TestBranchType));

    bool need_longer_range = false;
    // There are two situations in which we care about the offset being out of
    // range:
    //  - The label is bound but too far away.
    //  - The label is not bound but linked, and the previous branch
    //    instruction in the chain is too far away.
    if (label->is_bound() || label->is_linked()) {
      need_longer_range = !Instruction::IsValidImmPCOffset(
          branch_type, label->pos() - pc_offset());
    }
    if (!need_longer_range && !label->is_bound()) {
      int max_reachable_pc =
          pc_offset() + Instruction::ImmBranchRange(branch_type);

      // Use the LSB of the max_reachable_pc (always four-byte aligned) to
      // encode the branch type. We need only distinguish between TB[N]Z and
      // CB[N]Z/conditional branch, as the ranges for the latter are the same.
      int branch_type_tag = (branch_type == TestBranchType) ? 1 : 0;

      unresolved_branches_.insert(
          std::pair<int, Label*>(max_reachable_pc + branch_type_tag, label));
      // Also maintain the next pool check.
      next_veneer_pool_check_ =
          std::min(next_veneer_pool_check_,
                   max_reachable_pc - kVeneerDistanceCheckMargin);
    }
    return need_longer_range;
  }

  void Movi16bitHelper(const VRegister& vd, uint64_t imm);
  void Movi32bitHelper(const VRegister& vd, uint64_t imm);
  void Movi64bitHelper(const VRegister& vd, uint64_t imm);

  void LoadStoreMacro(const CPURegister& rt, const MemOperand& addr,
                      LoadStoreOp op);
  void LoadStoreMacroComplex(const CPURegister& rt, const MemOperand& addr,
                             LoadStoreOp op);

  void LoadStorePairMacro(const CPURegister& rt, const CPURegister& rt2,
                          const MemOperand& addr, LoadStorePairOp op);

  int64_t CalculateTargetOffset(Address target, RelocInfo::Mode rmode,
                                uint8_t* pc);

  void JumpHelper(int64_t offset, RelocInfo::Mode rmode, Condition cond = al);

  DISALLOW_IMPLICIT_CONSTRUCTORS(MacroAssembler);
};

// Use this scope when you need a one-to-one mapping between methods and
// instructions. This scope prevents the MacroAssembler from being called and
// literal pools from being emitted. It also asserts the number of instructions
// emitted is what you specified when creating the scope.
class V8_NODISCARD InstructionAccurateScope {
 public:
  explicit InstructionAccurateScope(MacroAssembler* masm, size_t count = 0)
      : masm_(masm),
        block_pool_(masm, count * kInstrSize)
#ifdef DEBUG
        ,
        size_(count * kInstrSize)
#endif
  {
    masm_->CheckVeneerPool(false, true, count * kInstrSize);
    masm_->StartBlockVeneerPool();
#ifdef DEBUG
    if (count != 0) {
      masm_->bind(&start_);
    }
    previous_allow_macro_instructions_ = masm_->allow_macro_instructions();
    masm_->set_allow_macro_instructions(false);
#endif
  }

  ~InstructionAccurateScope() {
    masm_->EndBlockVeneerPool();
#ifdef DEBUG
    if (start_.is_bound()) {
      DCHECK(masm_->SizeOfCodeGeneratedSince(&start_) == size_);
    }
    masm_->set_allow_macro_instructions(previous_allow_macro_instructions_);
#endif
  }

 private:
  MacroAssembler* masm_;
  MacroAssembler::BlockConstPoolScope block_pool_;
#ifdef DEBUG
  size_t size_;
  Label start_;
  bool previous_allow_macro_instructions_;
#endif
};

// This scope utility allows scratch registers to be managed safely. The
// MacroAssembler's TmpList() (and FPTmpList()) is used as a pool of scratch
// registers. These registers can be allocated on demand, and will be returned
// at the end of the scope.
//
// When the scope ends, the MacroAssembler's lists will be restored to their
// original state, even if the lists were modified by some other means. Note
// that this scope can be nested but the destructors need to run in the opposite
// order as the constructors. We do not have assertions for this.
class V8_NODISCARD UseScratchRegisterScope {
 public:
  explicit UseScratchRegisterScope(MacroAssembler* masm)
      : available_(masm->TmpList()),
        availablefp_(masm->FPTmpList()),
        old_available_(available_->bits()),
        old_availablefp_(availablefp_->bits()) {
    DCHECK_EQ(available_->type(), CPURegister::kRegister);
    DCHECK_EQ(availablefp_->type(), CPURegister::kVRegister);
  }

  V8_EXPORT_PRIVATE ~UseScratchRegisterScope() {
    available_->set_bits(old_available_);
    availablefp_->set_bits(old_availablefp_);
  }

  // Take a register from the appropriate temps list. It will be returned
  // automatically when the scope ends.
  Register AcquireW() { return AcquireNextAvailable(available_).W(); }
  Register AcquireX() { return AcquireNextAvailable(available_).X(); }
  VRegister AcquireS() { return AcquireNextAvailable(availablefp_).S(); }
  VRegister AcquireD() { return AcquireNextAvailable(availablefp_).D(); }
  VRegister AcquireQ() { return AcquireNextAvailable(availablefp_).Q(); }
  VRegister AcquireV(VectorFormat format) {
    return VRegister::Create(AcquireNextAvailable(availablefp_).code(), format);
  }

  bool CanAcquire() const { return !available_->IsEmpty(); }
  bool CanAcquireFP() const { return !availablefp_->IsEmpty(); }

  Register AcquireSameSizeAs(const Register& reg) {
    int code = AcquireNextAvailable(available_).code();
    return Register::Create(code, reg.SizeInBits());
  }

  V8_EXPORT_PRIVATE VRegister AcquireSameSizeAs(const VRegister& reg) {
    int code = AcquireNextAvailable(availablefp_).code();
    return VRegister::Create(code, reg.SizeInBits());
  }

  void Include(const CPURegList& list) { available_->Combine(list); }
  void IncludeFP(const CPURegList& list) { availablefp_->Combine(list); }
  void Exclude(const CPURegList& list) {
#if DEBUG
    CPURegList copy(list);
    while (!copy.IsEmpty()) {
      const CPURegister& reg = copy.PopHighestIndex();
      DCHECK(available_->IncludesAliasOf(reg));
    }
#endif
    available_->Remove(list);
  }
  void ExcludeFP(const CPURegList& list) {
#if DEBUG
    CPURegList copy(list);
    while (!copy.IsEmpty()) {
      const CPURegister& reg = copy.PopHighestIndex();
      DCHECK(availablefp_->IncludesAliasOf(reg));
    }
#endif
    availablefp_->Remove(list);
  }
  void Include(const Register& reg1, const Register& reg2 = NoReg) {
    CPURegList list(reg1, reg2);
    Include(list);
  }
  void Exclude(const Register& reg1, const Register& reg2 = NoReg) {
    CPURegList list(reg1, reg2);
    Exclude(list);
  }
  void ExcludeFP(const VRegister& reg) { ExcludeFP(CPURegList(reg)); }

  CPURegList* Available() { return available_; }
  void SetAvailable(const CPURegList& list) { *available_ = list; }

  CPURegList* AvailableFP() { return availablefp_; }
  void SetAvailableFP(const CPURegList& list) { *availablefp_ = list; }

 private:
  V8_EXPORT_PRIVATE static CPURegister AcquireNextAvailable(
      CPURegList* available) {
    CHECK(!available->IsEmpty());
    CPURegister result = available->PopLowestIndex();
    DCHECK(!AreAliased(result, xzr, sp));
    return result;
  }

  // Available scratch registers.
  CPURegList* available_;    // kRegister
  CPURegList* availablefp_;  // kVRegister

  // The state of the available lists at the start of this scope.
  uint64_t old_available_;    // kRegister
  uint64_t old_availablefp_;  // kVRegister
};

struct MoveCycleState {
  // List of scratch registers reserved for pending moves in a move cycle, and
  // which should therefore not be used as a temporary location by
  // {MoveToTempLocation}.
  RegList scratch_regs;
  DoubleRegList scratch_fp_regs;
  // Available scratch registers during the move cycle resolution scope.
  std::optional<UseScratchRegisterScope> temps;
  // Scratch register picked by {MoveToTempLocation}.
  std::optional<CPURegister> scratch_reg;
};

// Provides access to exit frame parameters (GC-ed).
inline MemOperand ExitFrameStackSlotOperand(int offset);

// Provides access to exit frame parameters (GC-ed).
inline MemOperand ExitFrameCallerStackSlotOperand(int index);

// Calls an API function. Allocates HandleScope, extracts returned value
// from handle and propagates exceptions. Clobbers C argument registers
// and C caller-saved registers. Restores context. On return removes
//   (*argc_operand + slots_to_drop_on_return) * kSystemPointerSize
// (GCed, includes the call JS arguments space and the additional space
// allocated for the fast call).
void CallApiFunctionAndReturn(MacroAssembler* masm, bool with_profiling,
                              Register function_address,
                              ExternalReference thunk_ref, Register thunk_arg,
                              int slots_to_drop_on_return,
                              MemOperand* argc_operand,
                              MemOperand return_value_operand);

}  // namespace internal
}  // namespace v8

#define ACCESS_MASM(masm) masm->

#endif  // V8_CODEGEN_ARM64_MACRO_ASSEMBLER_ARM64_H_
