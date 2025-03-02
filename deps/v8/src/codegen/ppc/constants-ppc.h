// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_PPC_CONSTANTS_PPC_H_
#define V8_CODEGEN_PPC_CONSTANTS_PPC_H_

#include <stdint.h>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/common/code-memory-access.h"
#include "src/common/globals.h"

// UNIMPLEMENTED_ macro for PPC.
#ifdef DEBUG
#define UNIMPLEMENTED_PPC()                                                \
  v8::internal::PrintF("%s, \tline %d: \tfunction %s not implemented. \n", \
                       __FILE__, __LINE__, __func__)
#else
#define UNIMPLEMENTED_PPC()
#endif

#if V8_HOST_ARCH_PPC64 &&                                          \
    (V8_OS_AIX || (V8_TARGET_ARCH_PPC64 && V8_TARGET_BIG_ENDIAN && \
                   (!defined(_CALL_ELF) || _CALL_ELF == 1)))
#define ABI_USES_FUNCTION_DESCRIPTORS 1
#else
#define ABI_USES_FUNCTION_DESCRIPTORS 0
#endif

#if !V8_HOST_ARCH_PPC64 || V8_OS_AIX || V8_TARGET_ARCH_PPC64
#define ABI_PASSES_HANDLES_IN_REGS 1
#else
#define ABI_PASSES_HANDLES_IN_REGS 0
#endif

#if !V8_HOST_ARCH_PPC64 || !V8_TARGET_ARCH_PPC64 || V8_TARGET_LITTLE_ENDIAN || \
    (defined(_CALL_ELF) && _CALL_ELF == 2)
#define ABI_RETURNS_OBJECT_PAIRS_IN_REGS 1
#else
#define ABI_RETURNS_OBJECT_PAIRS_IN_REGS 0
#endif

#if !V8_HOST_ARCH_PPC64 ||   \
    (V8_TARGET_ARCH_PPC64 && \
     (V8_TARGET_LITTLE_ENDIAN || (defined(_CALL_ELF) && _CALL_ELF == 2)))
#define ABI_CALL_VIA_IP 1
#else
#define ABI_CALL_VIA_IP 0
#endif

#if !V8_HOST_ARCH_PPC64 || V8_OS_AIX || V8_TARGET_ARCH_PPC64
#define ABI_TOC_REGISTER 2
#else
#define ABI_TOC_REGISTER 13
#endif
namespace v8 {
namespace internal {

// TODO(sigurds): Change this value once we use relative jumps.
constexpr size_t kMaxPCRelativeCodeRangeInMB = 0;

// Used to encode a boolean value when emitting 32 bit
// opcodes which will indicate the presence of function descriptors
constexpr int kHasFunctionDescriptorBitShift = 4;
constexpr int kHasFunctionDescriptorBitMask = 1
                                              << kHasFunctionDescriptorBitShift;

// Number of registers
const int kNumRegisters = 32;

// FP support.
const int kNumDoubleRegisters = 32;

const int kNoRegister = -1;

// Used in embedded constant pool builder - max reach in bits for
// various load instructions (one less due to unsigned)
const int kLoadPtrMaxReachBits = 15;
const int kLoadDoubleMaxReachBits = 15;

// The actual value of the kRootRegister is offset from the IsolateData's start
// to take advantage of negative displacement values.
constexpr int kRootRegisterBias = 128;

// sign-extend the least significant 5-bits of value <imm>
#define SIGN_EXT_IMM5(imm) ((static_cast<int>(imm) << 27) >> 27)

// sign-extend the least significant 16-bits of value <imm>
#define SIGN_EXT_IMM16(imm) ((static_cast<int>(imm) << 16) >> 16)

// sign-extend the least significant 14-bits of value <imm>
#define SIGN_EXT_IMM18(imm) ((static_cast<int>(imm) << 14) >> 14)

// sign-extend the least significant 22-bits of value <imm>
#define SIGN_EXT_IMM22(imm) ((static_cast<int>(imm) << 10) >> 10)

// sign-extend the least significant 26-bits of value <imm>
#define SIGN_EXT_IMM26(imm) ((static_cast<int>(imm) << 6) >> 6)

// sign-extend the least significant 34-bits of prefix+suffix value <imm>
#define SIGN_EXT_IMM34(imm) ((static_cast<int64_t>(imm) << 30) >> 30)

// -----------------------------------------------------------------------------
// Conditions.

// Defines constants and accessor classes to assemble, disassemble and
// simulate PPC instructions.
//
// Section references in the code refer to the "PowerPC Microprocessor
// Family: The Programmer.s Reference Guide" from 10/95
// https://www-01.ibm.com/chips/techlib/techlib.nsf/techdocs/852569B20050FF778525699600741775/$file/prg.pdf
//

// Constants for specific fields are defined in their respective named enums.
// General constants are in an anonymous enum in class Instr.
enum Condition : int {
  kNoCondition = -1,
  eq = 0,         // Equal.
  ne = 1,         // Not equal.
  ge = 2,         // Greater or equal.
  lt = 3,         // Less than.
  gt = 4,         // Greater than.
  le = 5,         // Less then or equal
  unordered = 6,  // Floating-point unordered
  ordered = 7,
  overflow = 8,  // Summary overflow
  nooverflow = 9,
  al = 10,  // Always.

  // Unified cross-platform condition names/aliases.
  // Do not set unsigned constants equal to their signed variants.
  // We need to be able to differentiate between signed and unsigned enum
  // constants in order to emit the right instructions (i.e CmpS64 vs CmpU64).
  kEqual = eq,
  kNotEqual = ne,
  kLessThan = lt,
  kGreaterThan = gt,
  kLessThanEqual = le,
  kGreaterThanEqual = ge,
  kUnsignedLessThan = 11,
  kUnsignedGreaterThan = 12,
  kUnsignedLessThanEqual = 13,
  kUnsignedGreaterThanEqual = 14,
  kOverflow = overflow,
  kNoOverflow = nooverflow,
  kZero = 15,
  kNotZero = 16,
};

inline Condition to_condition(Condition cond) {
  switch (cond) {
    case kUnsignedLessThan:
      return lt;
    case kUnsignedGreaterThan:
      return gt;
    case kUnsignedLessThanEqual:
      return le;
    case kUnsignedGreaterThanEqual:
      return ge;
    case kZero:
      return eq;
    case kNotZero:
      return ne;
    default:
      break;
  }
  return cond;
}

inline bool is_signed(Condition cond) {
  switch (cond) {
    case kEqual:
    case kNotEqual:
    case kLessThan:
    case kGreaterThan:
    case kLessThanEqual:
    case kGreaterThanEqual:
    case kOverflow:
    case kNoOverflow:
    case kZero:
    case kNotZero:
      return true;

    case kUnsignedLessThan:
    case kUnsignedGreaterThan:
    case kUnsignedLessThanEqual:
    case kUnsignedGreaterThanEqual:
      return false;

    default:
      UNREACHABLE();
  }
}

inline Condition NegateCondition(Condition cond) {
  DCHECK(cond != al);
  return static_cast<Condition>(cond ^ ne);
}

// -----------------------------------------------------------------------------
// Instructions encoding.

// Instr is merely used by the Assembler to distinguish 32bit integers
// representing instructions from usual 32 bit values.
// Instruction objects are pointers to 32bit values, and provide methods to
// access the various ISA fields.
using Instr = uint32_t;

#define PPC_XX3_OPCODE_SCALAR_LIST(V)                                 \
  /* VSX Scalar Add Double-Precision */                               \
  V(xsadddp, XSADDDP, 0xF0000100)                                     \
  /* VSX Scalar Add Single-Precision */                               \
  V(xsaddsp, XSADDSP, 0xF0000000)                                     \
  /* VSX Scalar Compare Ordered Double-Precision */                   \
  V(xscmpodp, XSCMPODP, 0xF0000158)                                   \
  /* VSX Scalar Compare Unordered Double-Precision */                 \
  V(xscmpudp, XSCMPUDP, 0xF0000118)                                   \
  /* VSX Scalar Copy Sign Double-Precision */                         \
  V(xscpsgndp, XSCPSGNDP, 0xF0000580)                                 \
  /* VSX Scalar Divide Double-Precision */                            \
  V(xsdivdp, XSDIVDP, 0xF00001C0)                                     \
  /* VSX Scalar Divide Single-Precision */                            \
  V(xsdivsp, XSDIVSP, 0xF00000C0)                                     \
  /* VSX Scalar Multiply-Add Type-A Double-Precision */               \
  V(xsmaddadp, XSMADDADP, 0xF0000108)                                 \
  /* VSX Scalar Multiply-Add Type-A Single-Precision */               \
  V(xsmaddasp, XSMADDASP, 0xF0000008)                                 \
  /* VSX Scalar Multiply-Add Type-M Double-Precision */               \
  V(xsmaddmdp, XSMADDMDP, 0xF0000148)                                 \
  /* VSX Scalar Multiply-Add Type-M Single-Precision */               \
  V(xsmaddmsp, XSMADDMSP, 0xF0000048)                                 \
  /* VSX Scalar Maximum Double-Precision */                           \
  V(xsmaxdp, XSMAXDP, 0xF0000500)                                     \
  /* VSX Scalar Minimum Double-Precision */                           \
  V(xsmindp, XSMINDP, 0xF0000540)                                     \
  /* VSX Scalar Multiply-Subtract Type-A Double-Precision */          \
  V(xsmsubadp, XSMSUBADP, 0xF0000188)                                 \
  /* VSX Scalar Multiply-Subtract Type-A Single-Precision */          \
  V(xsmsubasp, XSMSUBASP, 0xF0000088)                                 \
  /* VSX Scalar Multiply-Subtract Type-M Double-Precision */          \
  V(xsmsubmdp, XSMSUBMDP, 0xF00001C8)                                 \
  /* VSX Scalar Multiply-Subtract Type-M Single-Precision */          \
  V(xsmsubmsp, XSMSUBMSP, 0xF00000C8)                                 \
  /* VSX Scalar Multiply Double-Precision */                          \
  V(xsmuldp, XSMULDP, 0xF0000180)                                     \
  /* VSX Scalar Multiply Single-Precision */                          \
  V(xsmulsp, XSMULSP, 0xF0000080)                                     \
  /* VSX Scalar Negative Multiply-Add Type-A Double-Precision */      \
  V(xsnmaddadp, XSNMADDADP, 0xF0000508)                               \
  /* VSX Scalar Negative Multiply-Add Type-A Single-Precision */      \
  V(xsnmaddasp, XSNMADDASP, 0xF0000408)                               \
  /* VSX Scalar Negative Multiply-Add Type-M Double-Precision */      \
  V(xsnmaddmdp, XSNMADDMDP, 0xF0000548)                               \
  /* VSX Scalar Negative Multiply-Add Type-M Single-Precision */      \
  V(xsnmaddmsp, XSNMADDMSP, 0xF0000448)                               \
  /* VSX Scalar Negative Multiply-Subtract Type-A Double-Precision */ \
  V(xsnmsubadp, XSNMSUBADP, 0xF0000588)                               \
  /* VSX Scalar Negative Multiply-Subtract Type-A Single-Precision */ \
  V(xsnmsubasp, XSNMSUBASP, 0xF0000488)                               \
  /* VSX Scalar Negative Multiply-Subtract Type-M Double-Precision */ \
  V(xsnmsubmdp, XSNMSUBMDP, 0xF00005C8)                               \
  /* VSX Scalar Negative Multiply-Subtract Type-M Single-Precision */ \
  V(xsnmsubmsp, XSNMSUBMSP, 0xF00004C8)                               \
  /* VSX Scalar Reciprocal Estimate Double-Precision */               \
  V(xsredp, XSREDP, 0xF0000168)                                       \
  /* VSX Scalar Subtract Double-Precision */                          \
  V(xssubdp, XSSUBDP, 0xF0000140)                                     \
  /* VSX Scalar Subtract Single-Precision */                          \
  V(xssubsp, XSSUBSP, 0xF0000040)                                     \
  /* VSX Scalar Test for software Divide Double-Precision */          \
  V(xstdivdp, XSTDIVDP, 0xF00001E8)

#define PPC_XX3_OPCODE_VECTOR_A_FORM_LIST(V)         \
  /* VSX Vector Compare Equal To Single-Precision */ \
  V(xvcmpeqsp, XVCMPEQSP, 0xF0000218)                \
  /* VSX Vector Compare Equal To Double-Precision */ \
  V(xvcmpeqdp, XVCMPEQDP, 0xF0000318)

#define PPC_XX3_OPCODE_VECTOR_B_FORM_LIST(V)                                  \
  /* VSX Vector Add Double-Precision */                                       \
  V(xvadddp, XVADDDP, 0xF0000300)                                             \
  /* VSX Vector Add Single-Precision */                                       \
  V(xvaddsp, XVADDSP, 0xF0000200)                                             \
  /* VSX Vector Compare Equal To Double-Precision & record CR6 */             \
  V(xvcmpeqdpx, XVCMPEQDPx, 0xF0000718)                                       \
  /* VSX Vector Compare Equal To Single-Precision & record CR6 */             \
  V(xvcmpeqspx, XVCMPEQSPx, 0xF0000618)                                       \
  /* VSX Vector Compare Greater Than or Equal To Double-Precision */          \
  V(xvcmpgedp, XVCMPGEDP, 0xF0000398)                                         \
  /* VSX Vector Compare Greater Than or Equal To Double-Precision & record */ \
  /* CR6 */                                                                   \
  V(xvcmpgedpx, XVCMPGEDPx, 0xF0000798)                                       \
  /* VSX Vector Compare Greater Than or Equal To Single-Precision */          \
  V(xvcmpgesp, XVCMPGESP, 0xF0000298)                                         \
  /* VSX Vector Compare Greater Than or Equal To Single-Precision & record */ \
  /* CR6 */                                                                   \
  V(xvcmpgespx, XVCMPGESPx, 0xF0000698)                                       \
  /* VSX Vector Compare Greater Than Double-Precision */                      \
  V(xvcmpgtdp, XVCMPGTDP, 0xF0000358)                                         \
  /* VSX Vector Compare Greater Than Double-Precision & record CR6 */         \
  V(xvcmpgtdpx, XVCMPGTDPx, 0xF0000758)                                       \
  /* VSX Vector Compare Greater Than Single-Precision */                      \
  V(xvcmpgtsp, XVCMPGTSP, 0xF0000258)                                         \
  /* VSX Vector Compare Greater Than Single-Precision & record CR6 */         \
  V(xvcmpgtspx, XVCMPGTSPx, 0xF0000658)                                       \
  /* VSX Vector Copy Sign Double-Precision */                                 \
  V(xvcpsgndp, XVCPSGNDP, 0xF0000780)                                         \
  /* VSX Vector Copy Sign Single-Precision */                                 \
  V(xvcpsgnsp, XVCPSGNSP, 0xF0000680)                                         \
  /* VSX Vector Divide Double-Precision */                                    \
  V(xvdivdp, XVDIVDP, 0xF00003C0)                                             \
  /* VSX Vector Divide Single-Precision */                                    \
  V(xvdivsp, XVDIVSP, 0xF00002C0)                                             \
  /* VSX Vector Multiply-Add Type-A Double-Precision */                       \
  V(xvmaddadp, XVMADDADP, 0xF0000308)                                         \
  /* VSX Vector Multiply-Add Type-A Single-Precision */                       \
  V(xvmaddasp, XVMADDASP, 0xF0000208)                                         \
  /* VSX Vector Multiply-Add Type-M Double-Precision */                       \
  V(xvmaddmdp, XVMADDMDP, 0xF0000348)                                         \
  /* VSX Vector Multiply-Add Type-M Single-Precision */                       \
  V(xvmaddmsp, XVMADDMSP, 0xF0000248)                                         \
  /* VSX Vector Maximum Double-Precision */                                   \
  V(xvmaxdp, XVMAXDP, 0xF0000700)                                             \
  /* VSX Vector Maximum Single-Precision */                                   \
  V(xvmaxsp, XVMAXSP, 0xF0000600)                                             \
  /* VSX Vector Minimum Double-Precision */                                   \
  V(xvmindp, XVMINDP, 0xF0000740)                                             \
  /* VSX Vector Minimum Single-Precision */                                   \
  V(xvminsp, XVMINSP, 0xF0000640)                                             \
  /* VSX Vector Multiply-Subtract Type-A Double-Precision */                  \
  V(xvmsubadp, XVMSUBADP, 0xF0000388)                                         \
  /* VSX Vector Multiply-Subtract Type-A Single-Precision */                  \
  V(xvmsubasp, XVMSUBASP, 0xF0000288)                                         \
  /* VSX Vector Multiply-Subtract Type-M Double-Precision */                  \
  V(xvmsubmdp, XVMSUBMDP, 0xF00003C8)                                         \
  /* VSX Vector Multiply-Subtract Type-M Single-Precision */                  \
  V(xvmsubmsp, XVMSUBMSP, 0xF00002C8)                                         \
  /* VSX Vector Multiply Double-Precision */                                  \
  V(xvmuldp, XVMULDP, 0xF0000380)                                             \
  /* VSX Vector Multiply Single-Precision */                                  \
  V(xvmulsp, XVMULSP, 0xF0000280)                                             \
  /* VSX Vector Negative Multiply-Add Type-A Double-Precision */              \
  V(xvnmaddadp, XVNMADDADP, 0xF0000708)                                       \
  /* VSX Vector Negative Multiply-Add Type-A Single-Precision */              \
  V(xvnmaddasp, XVNMADDASP, 0xF0000608)                                       \
  /* VSX Vector Negative Multiply-Add Type-M Double-Precision */              \
  V(xvnmaddmdp, XVNMADDMDP, 0xF0000748)                                       \
  /* VSX Vector Negative Multiply-Add Type-M Single-Precision */              \
  V(xvnmaddmsp, XVNMADDMSP, 0xF0000648)                                       \
  /* VSX Vector Negative Multiply-Subtract Type-A Double-Precision */         \
  V(xvnmsubadp, XVNMSUBADP, 0xF0000788)                                       \
  /* VSX Vector Negative Multiply-Subtract Type-A Single-Precision */         \
  V(xvnmsubasp, XVNMSUBASP, 0xF0000688)                                       \
  /* VSX Vector Negative Multiply-Subtract Type-M Double-Precision */         \
  V(xvnmsubmdp, XVNMSUBMDP, 0xF00007C8)                                       \
  /* VSX Vector Negative Multiply-Subtract Type-M Single-Precision */         \
  V(xvnmsubmsp, XVNMSUBMSP, 0xF00006C8)                                       \
  /* VSX Vector Reciprocal Estimate Double-Precision */                       \
  V(xvredp, XVREDP, 0xF0000368)                                               \
  /* VSX Vector Subtract Double-Precision */                                  \
  V(xvsubdp, XVSUBDP, 0xF0000340)                                             \
  /* VSX Vector Subtract Single-Precision */                                  \
  V(xvsubsp, XVSUBSP, 0xF0000240)                                             \
  /* VSX Vector Test for software Divide Double-Precision */                  \
  V(xvtdivdp, XVTDIVDP, 0xF00003E8)                                           \
  /* VSX Vector Test for software Divide Single-Precision */                  \
  V(xvtdivsp, XVTDIVSP, 0xF00002E8)                                           \
  /* VSX Logical AND */                                                       \
  V(xxland, XXLAND, 0xF0000410)                                               \
  /* VSX Logical AND with Complement */                                       \
  V(xxlandc, XXLANDC, 0xF0000450)                                             \
  /* VSX Logical Equivalence */                                               \
  V(xxleqv, XXLEQV, 0xF00005D0)                                               \
  /* VSX Logical NAND */                                                      \
  V(xxlnand, XXLNAND, 0xF0000590)                                             \
  /* VSX Logical NOR */                                                       \
  V(xxlnor, XXLNOR, 0xF0000510)                                               \
  /* VSX Logical OR */                                                        \
  V(xxlor, XXLOR, 0xF0000490)                                                 \
  /* VSX Logical OR with Complement */                                        \
  V(xxlorc, XXLORC, 0xF0000550)                                               \
  /* VSX Logical XOR */                                                       \
  V(xxlxor, XXLXOR, 0xF00004D0)                                               \
  /* VSX Merge High Word */                                                   \
  V(xxmrghw, XXMRGHW, 0xF0000090)                                             \
  /* VSX Merge Low Word */                                                    \
  V(xxmrglw, XXMRGLW, 0xF0000190)                                             \
  /* VSX Permute Doubleword Immediate */                                      \
  V(xxpermdi, XXPERMDI, 0xF0000050)                                           \
  /* VSX Shift Left Double by Word Immediate */                               \
  V(xxsldwi, XXSLDWI, 0xF0000010)                                             \
  /* VSX Splat Word */                                                        \
  V(xxspltw, XXSPLTW, 0xF0000290)

#define PPC_XX3_OPCODE_VECTOR_LIST(V)  \
  PPC_XX3_OPCODE_VECTOR_A_FORM_LIST(V) \
  PPC_XX3_OPCODE_VECTOR_B_FORM_LIST(V)

#define PPC_Z23_OPCODE_LIST(V)                                    \
  /* Decimal Quantize */                                          \
  V(dqua, DQUA, 0xEC000006)                                       \
  /* Decimal Quantize Immediate */                                \
  V(dquai, DQUAI, 0xEC000086)                                     \
  /* Decimal Quantize Immediate Quad */                           \
  V(dquaiq, DQUAIQ, 0xFC000086)                                   \
  /* Decimal Quantize Quad */                                     \
  V(dquaq, DQUAQ, 0xFC000006)                                     \
  /* Decimal Floating Round To FP Integer Without Inexact */      \
  V(drintn, DRINTN, 0xEC0001C6)                                   \
  /* Decimal Floating Round To FP Integer Without Inexact Quad */ \
  V(drintnq, DRINTNQ, 0xFC0001C6)                                 \
  /* Decimal Floating Round To FP Integer With Inexact */         \
  V(drintx, DRINTX, 0xEC0000C6)                                   \
  /* Decimal Floating Round To FP Integer With Inexact Quad */    \
  V(drintxq, DRINTXQ, 0xFC0000C6)                                 \
  /* Decimal Floating Reround */                                  \
  V(drrnd, DRRND, 0xEC000046)                                     \
  /* Decimal Floating Reround Quad */                             \
  V(drrndq, DRRNDQ, 0xFC000046)

#define PPC_Z22_OPCODE_LIST(V)                                  \
  /* Decimal Floating Shift Coefficient Left Immediate */       \
  V(dscli, DSCLI, 0xEC000084)                                   \
  /* Decimal Floating Shift Coefficient Left Immediate Quad */  \
  V(dscliq, DSCLIQ, 0xFC000084)                                 \
  /* Decimal Floating Shift Coefficient Right Immediate */      \
  V(dscri, DSCRI, 0xEC0000C4)                                   \
  /* Decimal Floating Shift Coefficient Right Immediate Quad */ \
  V(dscriq, DSCRIQ, 0xFC0000C4)                                 \
  /* Decimal Floating Test Data Class */                        \
  V(dtstdc, DTSTDC, 0xEC000184)                                 \
  /* Decimal Floating Test Data Class Quad */                   \
  V(dtstdcq, DTSTDCQ, 0xFC000184)                               \
  /* Decimal Floating Test Data Group */                        \
  V(dtstdg, DTSTDG, 0xEC0001C4)                                 \
  /* Decimal Floating Test Data Group Quad */                   \
  V(dtstdgq, DTSTDGQ, 0xFC0001C4)

#define PPC_XX2_OPCODE_VECTOR_A_FORM_LIST(V)                                 \
  /* VSX Vector Absolute Value Double-Precision */                           \
  V(xvabsdp, XVABSDP, 0xF0000764)                                            \
  /* VSX Vector Negate Double-Precision */                                   \
  V(xvnegdp, XVNEGDP, 0xF00007E4)                                            \
  /* VSX Vector Square Root Double-Precision */                              \
  V(xvsqrtdp, XVSQRTDP, 0xF000032C)                                          \
  /* VSX Vector Absolute Value Single-Precision */                           \
  V(xvabssp, XVABSSP, 0xF0000664)                                            \
  /* VSX Vector Negate Single-Precision */                                   \
  V(xvnegsp, XVNEGSP, 0xF00006E4)                                            \
  /* VSX Vector Reciprocal Estimate Single-Precision */                      \
  V(xvresp, XVRESP, 0xF0000268)                                              \
  /* VSX Vector Reciprocal Square Root Estimate Single-Precision */          \
  V(xvrsqrtesp, XVRSQRTESP, 0xF0000228)                                      \
  /* VSX Vector Square Root Single-Precision */                              \
  V(xvsqrtsp, XVSQRTSP, 0xF000022C)                                          \
  /* VSX Vector Convert Single-Precision to Signed Fixed-Point Word */       \
  /* Saturate */                                                             \
  V(xvcvspsxws, XVCVSPSXWS, 0xF0000260)                                      \
  /* VSX Vector Convert Single-Precision to Unsigned Fixed-Point Word */     \
  /* Saturate */                                                             \
  V(xvcvspuxws, XVCVSPUXWS, 0xF0000220)                                      \
  /* VSX Vector Convert Signed Fixed-Point Word to Single-Precision */       \
  V(xvcvsxwsp, XVCVSXWSP, 0xF00002E0)                                        \
  /* VSX Vector Convert Unsigned Fixed-Point Word to Single-Precision */     \
  V(xvcvuxwsp, XVCVUXWSP, 0xF00002A0)                                        \
  /* VSX Vector Round to Double-Precision Integer toward +Infinity */        \
  V(xvrdpip, XVRDPIP, 0xF00003A4)                                            \
  /* VSX Vector Round to Double-Precision Integer toward -Infinity */        \
  V(xvrdpim, XVRDPIM, 0xF00003E4)                                            \
  /* VSX Vector Round to Double-Precision Integer toward Zero */             \
  V(xvrdpiz, XVRDPIZ, 0xF0000364)                                            \
  /* VSX Vector Round to Double-Precision Integer */                         \
  V(xvrdpi, XVRDPI, 0xF0000324)                                              \
  /* VSX Vector Round to Single-Precision Integer toward +Infinity */        \
  V(xvrspip, XVRSPIP, 0xF00002A4)                                            \
  /* VSX Vector Round to Single-Precision Integer toward -Infinity */        \
  V(xvrspim, XVRSPIM, 0xF00002E4)                                            \
  /* VSX Vector Round to Single-Precision Integer toward Zero */             \
  V(xvrspiz, XVRSPIZ, 0xF0000264)                                            \
  /* VSX Vector Round to Single-Precision Integer */                         \
  V(xvrspi, XVRSPI, 0xF0000224)                                              \
  /* VSX Vector Convert Signed Fixed-Point Doubleword to Double-Precision */ \
  V(xvcvsxddp, XVCVSXDDP, 0xF00007E0)                                        \
  /* VSX Vector Convert Unsigned Fixed-Point Doubleword to Double- */        \
  /* Precision */                                                            \
  V(xvcvuxddp, XVCVUXDDP, 0xF00007A0)                                        \
  /* VSX Vector Convert Single-Precision to Double-Precision */              \
  V(xvcvspdp, XVCVSPDP, 0xF0000724)                                          \
  /* VSX Vector Convert Double-Precision to Single-Precision */              \
  V(xvcvdpsp, XVCVDPSP, 0xF0000624)                                          \
  /* VSX Vector Convert Double-Precision to Signed Fixed-Point Word */       \
  /* Saturate */                                                             \
  V(xvcvdpsxws, XVCVDPSXWS, 0xF0000360)                                      \
  /* VSX Vector Convert Double-Precision to Unsigned Fixed-Point Word */     \
  /* Saturate */                                                             \
  V(xvcvdpuxws, XVCVDPUXWS, 0xF0000320)

#define PPC_XX2_OPCODE_SCALAR_A_FORM_LIST(V)                                \
  /* VSX Scalar Convert Double-Precision to Single-Precision format Non- */ \
  /* signalling */                                                          \
  V(xscvdpspn, XSCVDPSPN, 0xF000042C)                                       \
  /* VSX Scalar Convert Single-Precision to Double-Precision format Non- */ \
  /* signalling */                                                          \
  V(xscvspdpn, XSCVSPDPN, 0xF000052C)

#define PPC_XX2_OPCODE_B_FORM_LIST(V)  \
  /* Vector Byte-Reverse Quadword */   \
  V(xxbrq, XXBRQ, 0xF01F076C)          \
  /* Vector Byte-Reverse Doubleword */ \
  V(xxbrd, XXBRD, 0xF017076C)          \
  /* Vector Byte-Reverse Word */       \
  V(xxbrw, XXBRW, 0xF00F076C)          \
  /* Vector Byte-Reverse Halfword */   \
  V(xxbrh, XXBRH, 0xF007076C)

#define PPC_XX2_OPCODE_UNUSED_LIST(V)                                        \
  /* VSX Scalar Square Root Double-Precision */                              \
  V(xssqrtdp, XSSQRTDP, 0xF000012C)                                          \
  /* VSX Scalar Reciprocal Estimate Single-Precision */                      \
  V(xsresp, XSRESP, 0xF0000068)                                              \
  /* VSX Scalar Reciprocal Square Root Estimate Single-Precision */          \
  V(xsrsqrtesp, XSRSQRTESP, 0xF0000028)                                      \
  /* VSX Scalar Square Root Single-Precision */                              \
  V(xssqrtsp, XSSQRTSP, 0xF000002C)                                          \
  /* VSX Scalar Absolute Value Double-Precision */                           \
  V(xsabsdp, XSABSDP, 0xF0000564)                                            \
  /* VSX Scalar Convert Double-Precision to Single-Precision */              \
  V(xscvdpsp, XSCVDPSP, 0xF0000424)                                          \
  /* VSX Scalar Convert Double-Precision to Signed Fixed-Point Doubleword */ \
  /* Saturate */                                                             \
  V(xscvdpsxds, XSCVDPSXDS, 0xF0000560)                                      \
  /* VSX Scalar Convert Double-Precision to Signed Fixed-Point Word */       \
  /* Saturate */                                                             \
  V(xscvdpsxws, XSCVDPSXWS, 0xF0000160)                                      \
  /* VSX Scalar Convert Double-Precision to Unsigned Fixed-Point */          \
  /* Doubleword Saturate */                                                  \
  V(xscvdpuxds, XSCVDPUXDS, 0xF0000520)                                      \
  /* VSX Scalar Convert Double-Precision to Unsigned Fixed-Point Word */     \
  /* Saturate */                                                             \
  V(xscvdpuxws, XSCVDPUXWS, 0xF0000120)                                      \
  /* VSX Scalar Convert Single-Precision to Double-Precision (p=1) */        \
  V(xscvspdp, XSCVSPDP, 0xF0000524)                                          \
  /* VSX Scalar Convert Signed Fixed-Point Doubleword to Double-Precision */ \
  V(xscvsxddp, XSCVSXDDP, 0xF00005E0)                                        \
  /* VSX Scalar Convert Signed Fixed-Point Doubleword to Single-Precision */ \
  V(xscvsxdsp, XSCVSXDSP, 0xF00004E0)                                        \
  /* VSX Scalar Convert Unsigned Fixed-Point Doubleword to Double- */        \
  /* Precision */                                                            \
  V(xscvuxddp, XSCVUXDDP, 0xF00005A0)                                        \
  /* VSX Scalar Convert Unsigned Fixed-Point Doubleword to Single- */        \
  /* Precision */                                                            \
  V(xscvuxdsp, XSCVUXDSP, 0xF00004A0)                                        \
  /* VSX Scalar Negative Absolute Value Double-Precision */                  \
  V(xsnabsdp, XSNABSDP, 0xF00005A4)                                          \
  /* VSX Scalar Negate Double-Precision */                                   \
  V(xsnegdp, XSNEGDP, 0xF00005E4)                                            \
  /* VSX Scalar Round to Double-Precision Integer */                         \
  V(xsrdpi, XSRDPI, 0xF0000124)                                              \
  /* VSX Scalar Round to Double-Precision Integer using Current rounding */  \
  /* mode */                                                                 \
  V(xsrdpic, XSRDPIC, 0xF00001AC)                                            \
  /* VSX Scalar Round to Double-Precision Integer toward -Infinity */        \
  V(xsrdpim, XSRDPIM, 0xF00001E4)                                            \
  /* VSX Scalar Round to Double-Precision Integer toward +Infinity */        \
  V(xsrdpip, XSRDPIP, 0xF00001A4)                                            \
  /* VSX Scalar Round to Double-Precision Integer toward Zero */             \
  V(xsrdpiz, XSRDPIZ, 0xF0000164)                                            \
  /* VSX Scalar Round to Single-Precision */                                 \
  V(xsrsp, XSRSP, 0xF0000464)                                                \
  /* VSX Scalar Reciprocal Square Root Estimate Double-Precision */          \
  V(xsrsqrtedp, XSRSQRTEDP, 0xF0000128)                                      \
  /* VSX Scalar Test for software Square Root Double-Precision */            \
  V(xstsqrtdp, XSTSQRTDP, 0xF00001A8)                                        \
  /* VSX Vector Convert Double-Precision to Signed Fixed-Point Doubleword */ \
  /* Saturate */                                                             \
  V(xvcvdpsxds, XVCVDPSXDS, 0xF0000760)                                      \
  /* VSX Vector Convert Double-Precision to Unsigned Fixed-Point */          \
  /* Doubleword Saturate */                                                  \
  V(xvcvdpuxds, XVCVDPUXDS, 0xF0000720)                                      \
  /* VSX Vector Convert Single-Precision to Signed Fixed-Point Doubleword */ \
  /* Saturate */                                                             \
  V(xvcvspsxds, XVCVSPSXDS, 0xF0000660)                                      \
  /* VSX Vector Convert Single-Precision to Unsigned Fixed-Point */          \
  /* Doubleword Saturate */                                                  \
  V(xvcvspuxds, XVCVSPUXDS, 0xF0000620)                                      \
  /* VSX Vector Convert Signed Fixed-Point Doubleword to Single-Precision */ \
  V(xvcvsxdsp, XVCVSXDSP, 0xF00006E0)                                        \
  /* VSX Vector Convert Signed Fixed-Point Word to Double-Precision */       \
  V(xvcvsxwdp, XVCVSXWDP, 0xF00003E0)                                        \
  /* VSX Vector Convert Unsigned Fixed-Point Doubleword to Single- */        \
  /* Precision */                                                            \
  V(xvcvuxdsp, XVCVUXDSP, 0xF00006A0)                                        \
  /* VSX Vector Convert Unsigned Fixed-Point Word to Double-Precision */     \
  V(xvcvuxwdp, XVCVUXWDP, 0xF00003A0)                                        \
  /* VSX Vector Negative Absolute Value Double-Precision */                  \
  V(xvnabsdp, XVNABSDP, 0xF00007A4)                                          \
  /* VSX Vector Negative Absolute Value Single-Precision */                  \
  V(xvnabssp, XVNABSSP, 0xF00006A4)                                          \
  /* VSX Vector Round to Double-Precision Integer using Current rounding */  \
  /* mode */                                                                 \
  V(xvrdpic, XVRDPIC, 0xF00003AC)                                            \
  /* VSX Vector Round to Single-Precision Integer using Current rounding */  \
  /* mode */                                                                 \
  V(xvrspic, XVRSPIC, 0xF00002AC)                                            \
  /* VSX Vector Reciprocal Square Root Estimate Double-Precision */          \
  V(xvrsqrtedp, XVRSQRTEDP, 0xF0000328)                                      \
  /* VSX Vector Test for software Square Root Double-Precision */            \
  V(xvtsqrtdp, XVTSQRTDP, 0xF00003A8)                                        \
  /* VSX Vector Test for software Square Root Single-Precision */            \
  V(xvtsqrtsp, XVTSQRTSP, 0xF00002A8)                                        \
  /* Vector Splat Immediate Byte */                                          \
  V(xxspltib, XXSPLTIB, 0xF00002D0)

#define PPC_XX2_OPCODE_LIST(V)         \
  PPC_XX2_OPCODE_VECTOR_A_FORM_LIST(V) \
  PPC_XX2_OPCODE_SCALAR_A_FORM_LIST(V) \
  PPC_XX2_OPCODE_B_FORM_LIST(V)        \
  PPC_XX2_OPCODE_UNUSED_LIST(V)

#define PPC_EVX_OPCODE_LIST(V)                                                \
  /* Vector Load Double Word into Double Word by External PID Indexed */      \
  V(evlddepx, EVLDDEPX, 0x7C00063E)                                           \
  /* Vector Store Double of Double by External PID Indexed */                 \
  V(evstddepx, EVSTDDEPX, 0x7C00073E)                                         \
  /* Bit Reversed Increment */                                                \
  V(brinc, BRINC, 0x1000020F)                                                 \
  /* Vector Absolute Value */                                                 \
  V(evabs, EVABS, 0x10000208)                                                 \
  /* Vector Add Immediate Word */                                             \
  V(evaddiw, EVADDIW, 0x10000202)                                             \
  /* Vector Add Signed, Modulo, Integer to Accumulator Word */                \
  V(evaddsmiaaw, EVADDSMIAAW, 0x100004C9)                                     \
  /* Vector Add Signed, Saturate, Integer to Accumulator Word */              \
  V(evaddssiaaw, EVADDSSIAAW, 0x100004C1)                                     \
  /* Vector Add Unsigned, Modulo, Integer to Accumulator Word */              \
  V(evaddumiaaw, EVADDUMIAAW, 0x100004C8)                                     \
  /* Vector Add Unsigned, Saturate, Integer to Accumulator Word */            \
  V(evaddusiaaw, EVADDUSIAAW, 0x100004C0)                                     \
  /* Vector Add Word */                                                       \
  V(evaddw, EVADDW, 0x10000200)                                               \
  /* Vector AND */                                                            \
  V(evand, EVAND, 0x10000211)                                                 \
  /* Vector AND with Complement */                                            \
  V(evandc, EVANDC, 0x10000212)                                               \
  /* Vector Compare Equal */                                                  \
  V(evcmpeq, EVCMPEQ, 0x10000234)                                             \
  /* Vector Compare Greater Than Signed */                                    \
  V(evcmpgts, EVCMPGTS, 0x10000231)                                           \
  /* Vector Compare Greater Than Unsigned */                                  \
  V(evcmpgtu, EVCMPGTU, 0x10000230)                                           \
  /* Vector Compare Less Than Signed */                                       \
  V(evcmplts, EVCMPLTS, 0x10000233)                                           \
  /* Vector Compare Less Than Unsigned */                                     \
  V(evcmpltu, EVCMPLTU, 0x10000232)                                           \
  /* Vector Count Leading Signed Bits Word */                                 \
  V(evcntlsw, EVCNTLSW, 0x1000020E)                                           \
  /* Vector Count Leading Zeros Word */                                       \
  V(evcntlzw, EVCNTLZW, 0x1000020D)                                           \
  /* Vector Divide Word Signed */                                             \
  V(evdivws, EVDIVWS, 0x100004C6)                                             \
  /* Vector Divide Word Unsigned */                                           \
  V(evdivwu, EVDIVWU, 0x100004C7)                                             \
  /* Vector Equivalent */                                                     \
  V(eveqv, EVEQV, 0x10000219)                                                 \
  /* Vector Extend Sign Byte */                                               \
  V(evextsb, EVEXTSB, 0x1000020A)                                             \
  /* Vector Extend Sign Half Word */                                          \
  V(evextsh, EVEXTSH, 0x1000020B)                                             \
  /* Vector Load Double Word into Double Word */                              \
  V(evldd, EVLDD, 0x10000301)                                                 \
  /* Vector Load Double Word into Double Word Indexed */                      \
  V(evlddx, EVLDDX, 0x10000300)                                               \
  /* Vector Load Double into Four Half Words */                               \
  V(evldh, EVLDH, 0x10000305)                                                 \
  /* Vector Load Double into Four Half Words Indexed */                       \
  V(evldhx, EVLDHX, 0x10000304)                                               \
  /* Vector Load Double into Two Words */                                     \
  V(evldw, EVLDW, 0x10000303)                                                 \
  /* Vector Load Double into Two Words Indexed */                             \
  V(evldwx, EVLDWX, 0x10000302)                                               \
  /* Vector Load Half Word into Half Words Even and Splat */                  \
  V(evlhhesplat, EVLHHESPLAT, 0x10000309)                                     \
  /* Vector Load Half Word into Half Words Even and Splat Indexed */          \
  V(evlhhesplatx, EVLHHESPLATX, 0x10000308)                                   \
  /* Vector Load Half Word into Half Word Odd Signed and Splat */             \
  V(evlhhossplat, EVLHHOSSPLAT, 0x1000030F)                                   \
  /* Vector Load Half Word into Half Word Odd Signed and Splat Indexed */     \
  V(evlhhossplatx, EVLHHOSSPLATX, 0x1000030E)                                 \
  /* Vector Load Half Word into Half Word Odd Unsigned and Splat */           \
  V(evlhhousplat, EVLHHOUSPLAT, 0x1000030D)                                   \
  /* Vector Load Half Word into Half Word Odd Unsigned and Splat Indexed */   \
  V(evlhhousplatx, EVLHHOUSPLATX, 0x1000030C)                                 \
  /* Vector Load Word into Two Half Words Even */                             \
  V(evlwhe, EVLWHE, 0x10000311)                                               \
  /* Vector Load Word into Two Half Words Odd Signed (with sign extension) */ \
  V(evlwhos, EVLWHOS, 0x10000317)                                             \
  /* Vector Load Word into Two Half Words Odd Signed Indexed (with sign */    \
  /* extension) */                                                            \
  V(evlwhosx, EVLWHOSX, 0x10000316)                                           \
  /* Vector Load Word into Two Half Words Odd Unsigned (zero-extended) */     \
  V(evlwhou, EVLWHOU, 0x10000315)                                             \
  /* Vector Load Word into Two Half Words Odd Unsigned Indexed (zero- */      \
  /* extended) */                                                             \
  V(evlwhoux, EVLWHOUX, 0x10000314)                                           \
  /* Vector Load Word into Two Half Words and Splat */                        \
  V(evlwhsplat, EVLWHSPLAT, 0x1000031D)                                       \
  /* Vector Load Word into Two Half Words and Splat Indexed */                \
  V(evlwhsplatx, EVLWHSPLATX, 0x1000031C)                                     \
  /* Vector Load Word into Word and Splat */                                  \
  V(evlwwsplat, EVLWWSPLAT, 0x10000319)                                       \
  /* Vector Load Word into Word and Splat Indexed */                          \
  V(evlwwsplatx, EVLWWSPLATX, 0x10000318)                                     \
  /* Vector Merge High */                                                     \
  V(evmergehi, EVMERGEHI, 0x1000022C)                                         \
  /* Vector Merge High/Low */                                                 \
  V(evmergehilo, EVMERGEHILO, 0x1000022E)                                     \
  /* Vector Merge Low */                                                      \
  V(evmergelo, EVMERGELO, 0x1000022D)                                         \
  /* Vector Merge Low/High */                                                 \
  V(evmergelohi, EVMERGELOHI, 0x1000022F)                                     \
  /* Vector Multiply Half Words, Even, Guarded, Signed, Modulo, Fractional */ \
  /* and Accumulate */                                                        \
  V(evmhegsmfaa, EVMHEGSMFAA, 0x1000052B)                                     \
  /* Vector Multiply Half Words, Even, Guarded, Signed, Modulo, Fractional */ \
  /* and Accumulate Negative */                                               \
  V(evmhegsmfan, EVMHEGSMFAN, 0x100005AB)                                     \
  /* Vector Multiply Half Words, Even, Guarded, Signed, Modulo, Integer */    \
  /* and Accumulate */                                                        \
  V(evmhegsmiaa, EVMHEGSMIAA, 0x10000529)                                     \
  /* Vector Multiply Half Words, Even, Guarded, Signed, Modulo, Integer */    \
  /* and Accumulate Negative */                                               \
  V(evmhegsmian, EVMHEGSMIAN, 0x100005A9)                                     \
  /* Vector Multiply Half Words, Even, Guarded, Unsigned, Modulo, Integer */  \
  /* and Accumulate */                                                        \
  V(evmhegumiaa, EVMHEGUMIAA, 0x10000528)                                     \
  /* Vector Multiply Half Words, Even, Guarded, Unsigned, Modulo, Integer */  \
  /* and Accumulate Negative */                                               \
  V(evmhegumian, EVMHEGUMIAN, 0x100005A8)                                     \
  /* Vector Multiply Half Words, Even, Signed, Modulo, Fractional */          \
  V(evmhesmf, EVMHESMF, 0x1000040B)                                           \
  /* Vector Multiply Half Words, Even, Signed, Modulo, Fractional to */       \
  /* Accumulator */                                                           \
  V(evmhesmfa, EVMHESMFA, 0x1000042B)                                         \
  /* Vector Multiply Half Words, Even, Signed, Modulo, Fractional and */      \
  /* Accumulate into Words */                                                 \
  V(evmhesmfaaw, EVMHESMFAAW, 0x1000050B)                                     \
  /* Vector Multiply Half Words, Even, Signed, Modulo, Fractional and */      \
  /* Accumulate Negative into Words */                                        \
  V(evmhesmfanw, EVMHESMFANW, 0x1000058B)                                     \
  /* Vector Multiply Half Words, Even, Signed, Modulo, Integer */             \
  V(evmhesmi, EVMHESMI, 0x10000409)                                           \
  /* Vector Multiply Half Words, Even, Signed, Modulo, Integer to */          \
  /* Accumulator */                                                           \
  V(evmhesmia, EVMHESMIA, 0x10000429)                                         \
  /* Vector Multiply Half Words, Even, Signed, Modulo, Integer and */         \
  /* Accumulate into Words */                                                 \
  V(evmhesmiaaw, EVMHESMIAAW, 0x10000509)                                     \
  /* Vector Multiply Half Words, Even, Signed, Modulo, Integer and */         \
  /* Accumulate Negative into Words */                                        \
  V(evmhesmianw, EVMHESMIANW, 0x10000589)                                     \
  /* Vector Multiply Half Words, Even, Signed, Saturate, Fractional */        \
  V(evmhessf, EVMHESSF, 0x10000403)                                           \
  /* Vector Multiply Half Words, Even, Signed, Saturate, Fractional to */     \
  /* Accumulator */                                                           \
  V(evmhessfa, EVMHESSFA, 0x10000423)                                         \
  /* Vector Multiply Half Words, Even, Signed, Saturate, Fractional and */    \
  /* Accumulate into Words */                                                 \
  V(evmhessfaaw, EVMHESSFAAW, 0x10000503)                                     \
  /* Vector Multiply Half Words, Even, Signed, Saturate, Fractional and */    \
  /* Accumulate Negative into Words */                                        \
  V(evmhessfanw, EVMHESSFANW, 0x10000583)                                     \
  /* Vector Multiply Half Words, Even, Signed, Saturate, Integer and */       \
  /* Accumulate into Words */                                                 \
  V(evmhessiaaw, EVMHESSIAAW, 0x10000501)                                     \
  /* Vector Multiply Half Words, Even, Signed, Saturate, Integer and */       \
  /* Accumulate Negative into Words */                                        \
  V(evmhessianw, EVMHESSIANW, 0x10000581)                                     \
  /* Vector Multiply Half Words, Even, Unsigned, Modulo, Integer */           \
  V(evmheumi, EVMHEUMI, 0x10000408)                                           \
  /* Vector Multiply Half Words, Even, Unsigned, Modulo, Integer to */        \
  /* Accumulator */                                                           \
  V(evmheumia, EVMHEUMIA, 0x10000428)                                         \
  /* Vector Multiply Half Words, Even, Unsigned, Modulo, Integer and */       \
  /* Accumulate into Words */                                                 \
  V(evmheumiaaw, EVMHEUMIAAW, 0x10000508)                                     \
  /* Vector Multiply Half Words, Even, Unsigned, Modulo, Integer and */       \
  /* Accumulate Negative into Words */                                        \
  V(evmheumianw, EVMHEUMIANW, 0x10000588)                                     \
  /* Vector Multiply Half Words, Even, Unsigned, Saturate, Integer and */     \
  /* Accumulate into Words */                                                 \
  V(evmheusiaaw, EVMHEUSIAAW, 0x10000500)                                     \
  /* Vector Multiply Half Words, Even, Unsigned, Saturate, Integer and */     \
  /* Accumulate Negative into Words */                                        \
  V(evmheusianw, EVMHEUSIANW, 0x10000580)                                     \
  /* Vector Multiply Half Words, Odd, Guarded, Signed, Modulo, Fractional */  \
  /* and Accumulate */                                                        \
  V(evmhogsmfaa, EVMHOGSMFAA, 0x1000052F)                                     \
  /* Vector Multiply Half Words, Odd, Guarded, Signed, Modulo, Fractional */  \
  /* and Accumulate Negative */                                               \
  V(evmhogsmfan, EVMHOGSMFAN, 0x100005AF)                                     \
  /* Vector Multiply Half Words, Odd, Guarded, Signed, Modulo, Integer, */    \
  /* and Accumulate */                                                        \
  V(evmhogsmiaa, EVMHOGSMIAA, 0x1000052D)                                     \
  /* Vector Multiply Half Words, Odd, Guarded, Signed, Modulo, Integer and */ \
  /* Accumulate Negative */                                                   \
  V(evmhogsmian, EVMHOGSMIAN, 0x100005AD)                                     \
  /* Vector Multiply Half Words, Odd, Guarded, Unsigned, Modulo, Integer */   \
  /* and Accumulate */                                                        \
  V(evmhogumiaa, EVMHOGUMIAA, 0x1000052C)                                     \
  /* Vector Multiply Half Words, Odd, Guarded, Unsigned, Modulo, Integer */   \
  /* and Accumulate Negative */                                               \
  V(evmhogumian, EVMHOGUMIAN, 0x100005AC)                                     \
  /* Vector Multiply Half Words, Odd, Signed, Modulo, Fractional */           \
  V(evmhosmf, EVMHOSMF, 0x1000040F)                                           \
  /* Vector Multiply Half Words, Odd, Signed, Modulo, Fractional to */        \
  /* Accumulator */                                                           \
  V(evmhosmfa, EVMHOSMFA, 0x1000042F)                                         \
  /* Vector Multiply Half Words, Odd, Signed, Modulo, Fractional and */       \
  /* Accumulate into Words */                                                 \
  V(evmhosmfaaw, EVMHOSMFAAW, 0x1000050F)                                     \
  /* Vector Multiply Half Words, Odd, Signed, Modulo, Fractional and */       \
  /* Accumulate Negative into Words */                                        \
  V(evmhosmfanw, EVMHOSMFANW, 0x1000058F)                                     \
  /* Vector Multiply Half Words, Odd, Signed, Modulo, Integer */              \
  V(evmhosmi, EVMHOSMI, 0x1000040D)                                           \
  /* Vector Multiply Half Words, Odd, Signed, Modulo, Integer to */           \
  /* Accumulator */                                                           \
  V(evmhosmia, EVMHOSMIA, 0x1000042D)                                         \
  /* Vector Multiply Half Words, Odd, Signed, Modulo, Integer and */          \
  /* Accumulate into Words */                                                 \
  V(evmhosmiaaw, EVMHOSMIAAW, 0x1000050D)                                     \
  /* Vector Multiply Half Words, Odd, Signed, Modulo, Integer and */          \
  /* Accumulate Negative into Words */                                        \
  V(evmhosmianw, EVMHOSMIANW, 0x1000058D)                                     \
  /* Vector Multiply Half Words, Odd, Signed, Saturate, Fractional */         \
  V(evmhossf, EVMHOSSF, 0x10000407)                                           \
  /* Vector Multiply Half Words, Odd, Signed, Saturate, Fractional to */      \
  /* Accumulator */                                                           \
  V(evmhossfa, EVMHOSSFA, 0x10000427)                                         \
  /* Vector Multiply Half Words, Odd, Signed, Saturate, Fractional and */     \
  /* Accumulate into Words */                                                 \
  V(evmhossfaaw, EVMHOSSFAAW, 0x10000507)                                     \
  /* Vector Multiply Half Words, Odd, Signed, Saturate, Fractional and */     \
  /* Accumulate Negative into Words */                                        \
  V(evmhossfanw, EVMHOSSFANW, 0x10000587)                                     \
  /* Vector Multiply Half Words, Odd, Signed, Saturate, Integer and */        \
  /* Accumulate into Words */                                                 \
  V(evmhossiaaw, EVMHOSSIAAW, 0x10000505)                                     \
  /* Vector Multiply Half Words, Odd, Signed, Saturate, Integer and */        \
  /* Accumulate Negative into Words */                                        \
  V(evmhossianw, EVMHOSSIANW, 0x10000585)                                     \
  /* Vector Multiply Half Words, Odd, Unsigned, Modulo, Integer */            \
  V(evmhoumi, EVMHOUMI, 0x1000040C)                                           \
  /* Vector Multiply Half Words, Odd, Unsigned, Modulo, Integer to */         \
  /* Accumulator */                                                           \
  V(evmhoumia, EVMHOUMIA, 0x1000042C)                                         \
  /* Vector Multiply Half Words, Odd, Unsigned, Modulo, Integer and */        \
  /* Accumulate into Words */                                                 \
  V(evmhoumiaaw, EVMHOUMIAAW, 0x1000050C)                                     \
  /* Vector Multiply Half Words, Odd, Unsigned, Modulo, Integer and */        \
  /* Accumulate Negative into Words */                                        \
  V(evmhoumianw, EVMHOUMIANW, 0x1000058C)                                     \
  /* Vector Multiply Half Words, Odd, Unsigned, Saturate, Integer and */      \
  /* Accumulate into Words */                                                 \
  V(evmhousiaaw, EVMHOUSIAAW, 0x10000504)                                     \
  /* Vector Multiply Half Words, Odd, Unsigned, Saturate, Integer and */      \
  /* Accumulate Negative into Words */                                        \
  V(evmhousianw, EVMHOUSIANW, 0x10000584)                                     \
  /* Initialize Accumulator */                                                \
  V(evmra, EVMRA, 0x100004C4)                                                 \
  /* Vector Multiply Word High Signed, Modulo, Fractional */                  \
  V(evmwhsmf, EVMWHSMF, 0x1000044F)                                           \
  /* Vector Multiply Word High Signed, Modulo, Fractional to Accumulator */   \
  V(evmwhsmfa, EVMWHSMFA, 0x1000046F)                                         \
  /* Vector Multiply Word High Signed, Modulo, Integer */                     \
  V(evmwhsmi, EVMWHSMI, 0x1000044D)                                           \
  /* Vector Multiply Word High Signed, Modulo, Integer to Accumulator */      \
  V(evmwhsmia, EVMWHSMIA, 0x1000046D)                                         \
  /* Vector Multiply Word High Signed, Saturate, Fractional */                \
  V(evmwhssf, EVMWHSSF, 0x10000447)                                           \
  /* Vector Multiply Word High Signed, Saturate, Fractional to Accumulator */ \
  V(evmwhssfa, EVMWHSSFA, 0x10000467)                                         \
  /* Vector Multiply Word High Unsigned, Modulo, Integer */                   \
  V(evmwhumi, EVMWHUMI, 0x1000044C)                                           \
  /* Vector Multiply Word High Unsigned, Modulo, Integer to Accumulator */    \
  V(evmwhumia, EVMWHUMIA, 0x1000046C)                                         \
  /* Vector Multiply Word Low Signed, Modulo, Integer and Accumulate in */    \
  /* Words */                                                                 \
  V(evmwlsmiaaw, EVMWLSMIAAW, 0x10000549)                                     \
  /* Vector Multiply Word Low Signed, Modulo, Integer and Accumulate */       \
  /* Negative in Words */                                                     \
  V(evmwlsmianw, EVMWLSMIANW, 0x100005C9)                                     \
  /* Vector Multiply Word Low Signed, Saturate, Integer and Accumulate in */  \
  /* Words */                                                                 \
  V(evmwlssiaaw, EVMWLSSIAAW, 0x10000541)                                     \
  /* Vector Multiply Word Low Signed, Saturate, Integer and Accumulate */     \
  /* Negative in Words */                                                     \
  V(evmwlssianw, EVMWLSSIANW, 0x100005C1)                                     \
  /* Vector Multiply Word Low Unsigned, Modulo, Integer */                    \
  V(evmwlumi, EVMWLUMI, 0x10000448)                                           \
  /* Vector Multiply Word Low Unsigned, Modulo, Integer to Accumulator */     \
  V(evmwlumia, EVMWLUMIA, 0x10000468)                                         \
  /* Vector Multiply Word Low Unsigned, Modulo, Integer and Accumulate in */  \
  /* Words */                                                                 \
  V(evmwlumiaaw, EVMWLUMIAAW, 0x10000548)                                     \
  /* Vector Multiply Word Low Unsigned, Modulo, Integer and Accumulate */     \
  /* Negative in Words */                                                     \
  V(evmwlumianw, EVMWLUMIANW, 0x100005C8)                                     \
  /* Vector Multiply Word Low Unsigned, Saturate, Integer and Accumulate */   \
  /* in Words */                                                              \
  V(evmwlusiaaw, EVMWLUSIAAW, 0x10000540)                                     \
  /* Vector Multiply Word Low Unsigned, Saturate, Integer and Accumulate */   \
  /* Negative in Words */                                                     \
  V(evmwlusianw, EVMWLUSIANW, 0x100005C0)                                     \
  /* Vector Multiply Word Signed, Modulo, Fractional */                       \
  V(evmwsmf, EVMWSMF, 0x1000045B)                                             \
  /* Vector Multiply Word Signed, Modulo, Fractional to Accumulator */        \
  V(evmwsmfa, EVMWSMFA, 0x1000047B)                                           \
  /* Vector Multiply Word Signed, Modulo, Fractional and Accumulate */        \
  V(evmwsmfaa, EVMWSMFAA, 0x1000055B)                                         \
  /* Vector Multiply Word Signed, Modulo, Fractional and Accumulate */        \
  /* Negative */                                                              \
  V(evmwsmfan, EVMWSMFAN, 0x100005DB)                                         \
  /* Vector Multiply Word Signed, Modulo, Integer */                          \
  V(evmwsmi, EVMWSMI, 0x10000459)                                             \
  /* Vector Multiply Word Signed, Modulo, Integer to Accumulator */           \
  V(evmwsmia, EVMWSMIA, 0x10000479)                                           \
  /* Vector Multiply Word Signed, Modulo, Integer and Accumulate */           \
  V(evmwsmiaa, EVMWSMIAA, 0x10000559)                                         \
  /* Vector Multiply Word Signed, Modulo, Integer and Accumulate Negative */  \
  V(evmwsmian, EVMWSMIAN, 0x100005D9)                                         \
  /* Vector Multiply Word Signed, Saturate, Fractional */                     \
  V(evmwssf, EVMWSSF, 0x10000453)                                             \
  /* Vector Multiply Word Signed, Saturate, Fractional to Accumulator */      \
  V(evmwssfa, EVMWSSFA, 0x10000473)                                           \
  /* Vector Multiply Word Signed, Saturate, Fractional and Accumulate */      \
  V(evmwssfaa, EVMWSSFAA, 0x10000553)                                         \
  /* Vector Multiply Word Signed, Saturate, Fractional and Accumulate */      \
  /* Negative */                                                              \
  V(evmwssfan, EVMWSSFAN, 0x100005D3)                                         \
  /* Vector Multiply Word Unsigned, Modulo, Integer */                        \
  V(evmwumi, EVMWUMI, 0x10000458)                                             \
  /* Vector Multiply Word Unsigned, Modulo, Integer to Accumulator */         \
  V(evmwumia, EVMWUMIA, 0x10000478)                                           \
  /* Vector Multiply Word Unsigned, Modulo, Integer and Accumulate */         \
  V(evmwumiaa, EVMWUMIAA, 0x10000558)                                         \
  /* Vector Multiply Word Unsigned, Modulo, Integer and Accumulate */         \
  /* Negative */                                                              \
  V(evmwumian, EVMWUMIAN, 0x100005D8)                                         \
  /* Vector NAND */                                                           \
  V(evnand, EVNAND, 0x1000021E)                                               \
  /* Vector Negate */                                                         \
  V(evneg, EVNEG, 0x10000209)                                                 \
  /* Vector NOR */                                                            \
  V(evnor, EVNOR, 0x10000218)                                                 \
  /* Vector OR */                                                             \
  V(evor, EVOR, 0x10000217)                                                   \
  /* Vector OR with Complement */                                             \
  V(evorc, EVORC, 0x1000021B)                                                 \
  /* Vector Rotate Left Word */                                               \
  V(evrlw, EVRLW, 0x10000228)                                                 \
  /* Vector Rotate Left Word Immediate */                                     \
  V(evrlwi, EVRLWI, 0x1000022A)                                               \
  /* Vector Round Word */                                                     \
  V(evrndw, EVRNDW, 0x1000020C)                                               \
  /* Vector Shift Left Word */                                                \
  V(evslw, EVSLW, 0x10000224)                                                 \
  /* Vector Shift Left Word Immediate */                                      \
  V(evslwi, EVSLWI, 0x10000226)                                               \
  /* Vector Splat Fractional Immediate */                                     \
  V(evsplatfi, EVSPLATFI, 0x1000022B)                                         \
  /* Vector Splat Immediate */                                                \
  V(evsplati, EVSPLATI, 0x10000229)                                           \
  /* Vector Shift Right Word Immediate Signed */                              \
  V(evsrwis, EVSRWIS, 0x10000223)                                             \
  /* Vector Shift Right Word Immediate Unsigned */                            \
  V(evsrwiu, EVSRWIU, 0x10000222)                                             \
  /* Vector Shift Right Word Signed */                                        \
  V(evsrws, EVSRWS, 0x10000221)                                               \
  /* Vector Shift Right Word Unsigned */                                      \
  V(evsrwu, EVSRWU, 0x10000220)                                               \
  /* Vector Store Double of Double */                                         \
  V(evstdd, EVSTDD, 0x10000321)                                               \
  /* Vector Store Double of Double Indexed */                                 \
  V(evstddx, EVSTDDX, 0x10000320)                                             \
  /* Vector Store Double of Four Half Words */                                \
  V(evstdh, EVSTDH, 0x10000325)                                               \
  /* Vector Store Double of Four Half Words Indexed */                        \
  V(evstdhx, EVSTDHX, 0x10000324)                                             \
  /* Vector Store Double of Two Words */                                      \
  V(evstdw, EVSTDW, 0x10000323)                                               \
  /* Vector Store Double of Two Words Indexed */                              \
  V(evstdwx, EVSTDWX, 0x10000322)                                             \
  /* Vector Store Word of Two Half Words from Even */                         \
  V(evstwhe, EVSTWHE, 0x10000331)                                             \
  /* Vector Store Word of Two Half Words from Even Indexed */                 \
  V(evstwhex, EVSTWHEX, 0x10000330)                                           \
  /* Vector Store Word of Two Half Words from Odd */                          \
  V(evstwho, EVSTWHO, 0x10000335)                                             \
  /* Vector Store Word of Two Half Words from Odd Indexed */                  \
  V(evstwhox, EVSTWHOX, 0x10000334)                                           \
  /* Vector Store Word of Word from Even */                                   \
  V(evstwwe, EVSTWWE, 0x10000339)                                             \
  /* Vector Store Word of Word from Even Indexed */                           \
  V(evstwwex, EVSTWWEX, 0x10000338)                                           \
  /* Vector Store Word of Word from Odd */                                    \
  V(evstwwo, EVSTWWO, 0x1000033D)                                             \
  /* Vector Store Word of Word from Odd Indexed */                            \
  V(evstwwox, EVSTWWOX, 0x1000033C)                                           \
  /* Vector Subtract Signed, Modulo, Integer to Accumulator Word */           \
  V(evsubfsmiaaw, EVSUBFSMIAAW, 0x100004CB)                                   \
  /* Vector Subtract Signed, Saturate, Integer to Accumulator Word */         \
  V(evsubfssiaaw, EVSUBFSSIAAW, 0x100004C3)                                   \
  /* Vector Subtract Unsigned, Modulo, Integer to Accumulator Word */         \
  V(evsubfumiaaw, EVSUBFUMIAAW, 0x100004CA)                                   \
  /* Vector Subtract Unsigned, Saturate, Integer to Accumulator Word */       \
  V(evsubfusiaaw, EVSUBFUSIAAW, 0x100004C2)                                   \
  /* Vector Subtract from Word */                                             \
  V(evsubfw, EVSUBFW, 0x10000204)                                             \
  /* Vector Subtract Immediate from Word */                                   \
  V(evsubifw, EVSUBIFW, 0x10000206)                                           \
  /* Vector XOR */                                                            \
  V(evxor, EVXOR, 0x10000216)                                                 \
  /* Floating-Point Double-Precision Absolute Value */                        \
  V(efdabs, EFDABS, 0x100002E4)                                               \
  /* Floating-Point Double-Precision Add */                                   \
  V(efdadd, EFDADD, 0x100002E0)                                               \
  /* Floating-Point Double-Precision Convert from Single-Precision */         \
  V(efdcfs, EFDCFS, 0x100002EF)                                               \
  /* Convert Floating-Point Double-Precision from Signed Fraction */          \
  V(efdcfsf, EFDCFSF, 0x100002F3)                                             \
  /* Convert Floating-Point Double-Precision from Signed Integer */           \
  V(efdcfsi, EFDCFSI, 0x100002F1)                                             \
  /* Convert Floating-Point Double-Precision from Signed Integer */           \
  /* Doubleword */                                                            \
  V(efdcfsid, EFDCFSID, 0x100002E3)                                           \
  /* Convert Floating-Point Double-Precision from Unsigned Fraction */        \
  V(efdcfuf, EFDCFUF, 0x100002F2)                                             \
  /* Convert Floating-Point Double-Precision from Unsigned Integer */         \
  V(efdcfui, EFDCFUI, 0x100002F0)                                             \
  /* Convert Floating-Point Double-Precision fromUnsigned Integer */          \
  /* Doubleword */                                                            \
  V(efdcfuid, EFDCFUID, 0x100002E2)                                           \
  /* Floating-Point Double-Precision Compare Equal */                         \
  V(efdcmpeq, EFDCMPEQ, 0x100002EE)                                           \
  /* Floating-Point Double-Precision Compare Greater Than */                  \
  V(efdcmpgt, EFDCMPGT, 0x100002EC)                                           \
  /* Floating-Point Double-Precision Compare Less Than */                     \
  V(efdcmplt, EFDCMPLT, 0x100002ED)                                           \
  /* Convert Floating-Point Double-Precision to Signed Fraction */            \
  V(efdctsf, EFDCTSF, 0x100002F7)                                             \
  /* Convert Floating-Point Double-Precision to Signed Integer */             \
  V(efdctsi, EFDCTSI, 0x100002F5)                                             \
  /* Convert Floating-Point Double-Precision to Signed Integer Doubleword */  \
  /* with Round toward Zero */                                                \
  V(efdctsidz, EFDCTSIDZ, 0x100002EB)                                         \
  /* Convert Floating-Point Double-Precision to Signed Integer with Round */  \
  /* toward Zero */                                                           \
  V(efdctsiz, EFDCTSIZ, 0x100002FA)                                           \
  /* Convert Floating-Point Double-Precision to Unsigned Fraction */          \
  V(efdctuf, EFDCTUF, 0x100002F6)                                             \
  /* Convert Floating-Point Double-Precision to Unsigned Integer */           \
  V(efdctui, EFDCTUI, 0x100002F4)                                             \
  /* Convert Floating-Point Double-Precision to Unsigned Integer */           \
  /* Doubleword with Round toward Zero */                                     \
  V(efdctuidz, EFDCTUIDZ, 0x100002EA)                                         \
  /* Convert Floating-Point Double-Precision to Unsigned Integer with */      \
  /* Round toward Zero */                                                     \
  V(efdctuiz, EFDCTUIZ, 0x100002F8)                                           \
  /* Floating-Point Double-Precision Divide */                                \
  V(efddiv, EFDDIV, 0x100002E9)                                               \
  /* Floating-Point Double-Precision Multiply */                              \
  V(efdmul, EFDMUL, 0x100002E8)                                               \
  /* Floating-Point Double-Precision Negative Absolute Value */               \
  V(efdnabs, EFDNABS, 0x100002E5)                                             \
  /* Floating-Point Double-Precision Negate */                                \
  V(efdneg, EFDNEG, 0x100002E6)                                               \
  /* Floating-Point Double-Precision Subtract */                              \
  V(efdsub, EFDSUB, 0x100002E1)                                               \
  /* Floating-Point Double-Precision Test Equal */                            \
  V(efdtsteq, EFDTSTEQ, 0x100002FE)                                           \
  /* Floating-Point Double-Precision Test Greater Than */                     \
  V(efdtstgt, EFDTSTGT, 0x100002FC)                                           \
  /* Floating-Point Double-Precision Test Less Than */                        \
  V(efdtstlt, EFDTSTLT, 0x100002FD)                                           \
  /* Floating-Point Single-Precision Convert from Double-Precision */         \
  V(efscfd, EFSCFD, 0x100002CF)                                               \
  /* Floating-Point Absolute Value */                                         \
  V(efsabs, EFSABS, 0x100002C4)                                               \
  /* Floating-Point Add */                                                    \
  V(efsadd, EFSADD, 0x100002C0)                                               \
  /* Convert Floating-Point from Signed Fraction */                           \
  V(efscfsf, EFSCFSF, 0x100002D3)                                             \
  /* Convert Floating-Point from Signed Integer */                            \
  V(efscfsi, EFSCFSI, 0x100002D1)                                             \
  /* Convert Floating-Point from Unsigned Fraction */                         \
  V(efscfuf, EFSCFUF, 0x100002D2)                                             \
  /* Convert Floating-Point from Unsigned Integer */                          \
  V(efscfui, EFSCFUI, 0x100002D0)                                             \
  /* Floating-Point Compare Equal */                                          \
  V(efscmpeq, EFSCMPEQ, 0x100002CE)                                           \
  /* Floating-Point Compare Greater Than */                                   \
  V(efscmpgt, EFSCMPGT, 0x100002CC)                                           \
  /* Floating-Point Compare Less Than */                                      \
  V(efscmplt, EFSCMPLT, 0x100002CD)                                           \
  /* Convert Floating-Point to Signed Fraction */                             \
  V(efsctsf, EFSCTSF, 0x100002D7)                                             \
  /* Convert Floating-Point to Signed Integer */                              \
  V(efsctsi, EFSCTSI, 0x100002D5)                                             \
  /* Convert Floating-Point to Signed Integer with Round toward Zero */       \
  V(efsctsiz, EFSCTSIZ, 0x100002DA)                                           \
  /* Convert Floating-Point to Unsigned Fraction */                           \
  V(efsctuf, EFSCTUF, 0x100002D6)                                             \
  /* Convert Floating-Point to Unsigned Integer */                            \
  V(efsctui, EFSCTUI, 0x100002D4)                                             \
  /* Convert Floating-Point to Unsigned Integer with Round toward Zero */     \
  V(efsctuiz, EFSCTUIZ, 0x100002D8)                                           \
  /* Floating-Point Divide */                                                 \
  V(efsdiv, EFSDIV, 0x100002C9)                                               \
  /* Floating-Point Multiply */                                               \
  V(efsmul, EFSMUL, 0x100002C8)                                               \
  /* Floating-Point Negative Absolute Value */                                \
  V(efsnabs, EFSNABS, 0x100002C5)                                             \
  /* Floating-Point Negate */                                                 \
  V(efsneg, EFSNEG, 0x100002C6)                                               \
  /* Floating-Point Subtract */                                               \
  V(efssub, EFSSUB, 0x100002C1)                                               \
  /* Floating-Point Test Equal */                                             \
  V(efststeq, EFSTSTEQ, 0x100002DE)                                           \
  /* Floating-Point Test Greater Than */                                      \
  V(efststgt, EFSTSTGT, 0x100002DC)                                           \
  /* Floating-Point Test Less Than */                                         \
  V(efststlt, EFSTSTLT, 0x100002DD)                                           \
  /* Vector Floating-Point Absolute Value */                                  \
  V(evfsabs, EVFSABS, 0x10000284)                                             \
  /* Vector Floating-Point Add */                                             \
  V(evfsadd, EVFSADD, 0x10000280)                                             \
  /* Vector Convert Floating-Point from Signed Fraction */                    \
  V(evfscfsf, EVFSCFSF, 0x10000293)                                           \
  /* Vector Convert Floating-Point from Signed Integer */                     \
  V(evfscfsi, EVFSCFSI, 0x10000291)                                           \
  /* Vector Convert Floating-Point from Unsigned Fraction */                  \
  V(evfscfuf, EVFSCFUF, 0x10000292)                                           \
  /* Vector Convert Floating-Point from Unsigned Integer */                   \
  V(evfscfui, EVFSCFUI, 0x10000290)                                           \
  /* Vector Floating-Point Compare Equal */                                   \
  V(evfscmpeq, EVFSCMPEQ, 0x1000028E)                                         \
  /* Vector Floating-Point Compare Greater Than */                            \
  V(evfscmpgt, EVFSCMPGT, 0x1000028C)                                         \
  /* Vector Floating-Point Compare Less Than */                               \
  V(evfscmplt, EVFSCMPLT, 0x1000028D)                                         \
  /* Vector Convert Floating-Point to Signed Fraction */                      \
  V(evfsctsf, EVFSCTSF, 0x10000297)                                           \
  /* Vector Convert Floating-Point to Signed Integer */                       \
  V(evfsctsi, EVFSCTSI, 0x10000295)                                           \
  /* Vector Convert Floating-Point to Signed Integer with Round toward */     \
  /* Zero */                                                                  \
  V(evfsctsiz, EVFSCTSIZ, 0x1000029A)                                         \
  /* Vector Convert Floating-Point to Unsigned Fraction */                    \
  V(evfsctuf, EVFSCTUF, 0x10000296)                                           \
  /* Vector Convert Floating-Point to Unsigned Integer */                     \
  V(evfsctui, EVFSCTUI, 0x10000294)                                           \
  /* Vector Convert Floating-Point to Unsigned Integer with Round toward */   \
  /* Zero */                                                                  \
  V(evfsctuiz, EVFSCTUIZ, 0x10000298)                                         \
  /* Vector Floating-Point Divide */                                          \
  V(evfsdiv, EVFSDIV, 0x10000289)                                             \
  /* Vector Floating-Point Multiply */                                        \
  V(evfsmul, EVFSMUL, 0x10000288)                                             \
  /* Vector Floating-Point Negative Absolute Value */                         \
  V(evfsnabs, EVFSNABS, 0x10000285)                                           \
  /* Vector Floating-Point Negate */                                          \
  V(evfsneg, EVFSNEG, 0x10000286)                                             \
  /* Vector Floating-Point Subtract */                                        \
  V(evfssub, EVFSSUB, 0x10000281)                                             \
  /* Vector Floating-Point Test Equal */                                      \
  V(evfststeq, EVFSTSTEQ, 0x1000029E)                                         \
  /* Vector Floating-Point Test Greater Than */                               \
  V(evfststgt, EVFSTSTGT, 0x1000029C)                                         \
  /* Vector Floating-Point Test Less Than */                                  \
  V(evfststlt, EVFSTSTLT, 0x1000029D)

#define PPC_VC_OPCODE_LIST(V)                                    \
  /* Vector Compare Bounds Single-Precision */                   \
  V(vcmpbfp, VCMPBFP, 0x100003C6)                                \
  /* Vector Compare Equal To Single-Precision */                 \
  V(vcmpeqfp, VCMPEQFP, 0x100000C6)                              \
  /* Vector Compare Equal To Unsigned Byte */                    \
  V(vcmpequb, VCMPEQUB, 0x10000006)                              \
  /* Vector Compare Equal To Unsigned Doubleword */              \
  V(vcmpequd, VCMPEQUD, 0x100000C7)                              \
  /* Vector Compare Equal To Unsigned Halfword */                \
  V(vcmpequh, VCMPEQUH, 0x10000046)                              \
  /* Vector Compare Equal To Unsigned Word */                    \
  V(vcmpequw, VCMPEQUW, 0x10000086)                              \
  /* Vector Compare Greater Than or Equal To Single-Precision */ \
  V(vcmpgefp, VCMPGEFP, 0x100001C6)                              \
  /* Vector Compare Greater Than Single-Precision */             \
  V(vcmpgtfp, VCMPGTFP, 0x100002C6)                              \
  /* Vector Compare Greater Than Signed Byte */                  \
  V(vcmpgtsb, VCMPGTSB, 0x10000306)                              \
  /* Vector Compare Greater Than Signed Doubleword */            \
  V(vcmpgtsd, VCMPGTSD, 0x100003C7)                              \
  /* Vector Compare Greater Than Signed Halfword */              \
  V(vcmpgtsh, VCMPGTSH, 0x10000346)                              \
  /* Vector Compare Greater Than Signed Word */                  \
  V(vcmpgtsw, VCMPGTSW, 0x10000386)                              \
  /* Vector Compare Greater Than Unsigned Byte */                \
  V(vcmpgtub, VCMPGTUB, 0x10000206)                              \
  /* Vector Compare Greater Than Unsigned Doubleword */          \
  V(vcmpgtud, VCMPGTUD, 0x100002C7)                              \
  /* Vector Compare Greater Than Unsigned Halfword */            \
  V(vcmpgtuh, VCMPGTUH, 0x10000246)                              \
  /* Vector Compare Greater Than Unsigned Word */                \
  V(vcmpgtuw, VCMPGTUW, 0x10000286)

#define PPC_X_OPCODE_A_FORM_LIST(V) \
  /* Modulo Signed Dword */         \
  V(modsd, MODSD, 0x7C000612)       \
  /*  Modulo Unsigned Dword */      \
  V(modud, MODUD, 0x7C000212)       \
  /* Modulo Signed Word */          \
  V(modsw, MODSW, 0x7C000616)       \
  /* Modulo Unsigned Word */        \
  V(moduw, MODUW, 0x7C000216)

#define PPC_X_OPCODE_B_FORM_LIST(V)      \
  /* XOR */                              \
  V(xor_, XORX, 0x7C000278)              \
  /* AND */                              \
  V(and_, ANDX, 0x7C000038)              \
  /* AND with Complement */              \
  V(andc, ANDCX, 0x7C000078)             \
  /* OR */                               \
  V(orx, ORX, 0x7C000378)                \
  /* OR with Complement */               \
  V(orc, ORC, 0x7C000338)                \
  /* NOR */                              \
  V(nor, NORX, 0x7C0000F8)               \
  /* Shift Right Word */                 \
  V(srw, SRWX, 0x7C000430)               \
  /* Shift Left Word */                  \
  V(slw, SLWX, 0x7C000030)               \
  /* Shift Right Algebraic Word */       \
  V(sraw, SRAW, 0x7C000630)              \
  /* Shift Left Doubleword */            \
  V(sld, SLDX, 0x7C000036)               \
  /* Shift Right Algebraic Doubleword */ \
  V(srad, SRAD, 0x7C000634)              \
  /* Shift Right Doubleword */           \
  V(srd, SRDX, 0x7C000436)

#define PPC_X_OPCODE_C_FORM_LIST(V)    \
  /* Count Leading Zeros Word */       \
  V(cntlzw, CNTLZWX, 0x7C000034)       \
  /* Count Leading Zeros Doubleword */ \
  V(cntlzd, CNTLZDX, 0x7C000074)       \
  /* Count Tailing Zeros Word */       \
  V(cnttzw, CNTTZWX, 0x7C000434)       \
  /* Count Tailing Zeros Doubleword */ \
  V(cnttzd, CNTTZDX, 0x7C000474)       \
  /* Population Count Byte-wise */     \
  V(popcntb, POPCNTB, 0x7C0000F4)      \
  /* Population Count Words */         \
  V(popcntw, POPCNTW, 0x7C0002F4)      \
  /* Population Count Doubleword */    \
  V(popcntd, POPCNTD, 0x7C0003F4)      \
  /* Extend Sign Byte */               \
  V(extsb, EXTSB, 0x7C000774)          \
  /* Extend Sign Halfword */           \
  V(extsh, EXTSH, 0x7C000734)

#define PPC_X_OPCODE_D_FORM_LIST(V)                     \
  /* Load Halfword Byte-Reverse Indexed */              \
  V(lhbrx, LHBRX, 0x7C00062C)                           \
  /* Load Word Byte-Reverse Indexed */                  \
  V(lwbrx, LWBRX, 0x7C00042C)                           \
  /* Load Doubleword Byte-Reverse Indexed */            \
  V(ldbrx, LDBRX, 0x7C000428)                           \
  /* Load Byte and Zero Indexed */                      \
  V(lbzx, LBZX, 0x7C0000AE)                             \
  /* Load Byte and Zero with Update Indexed */          \
  V(lbzux, LBZUX, 0x7C0000EE)                           \
  /* Load Halfword and Zero Indexed */                  \
  V(lhzx, LHZX, 0x7C00022E)                             \
  /* Load Halfword and Zero with Update Indexed */      \
  V(lhzux, LHZUX, 0x7C00026E)                           \
  /* Load Halfword Algebraic Indexed */                 \
  V(lhax, LHAX, 0x7C0002AE)                             \
  /* Load Word and Zero Indexed */                      \
  V(lwzx, LWZX, 0x7C00002E)                             \
  /* Load Word and Zero with Update Indexed */          \
  V(lwzux, LWZUX, 0x7C00006E)                           \
  /* Load Doubleword Indexed */                         \
  V(ldx, LDX, 0x7C00002A)                               \
  /* Load Doubleword with Update Indexed */             \
  V(ldux, LDUX, 0x7C00006A)                             \
  /* Load Floating-Point Double Indexed */              \
  V(lfdx, LFDX, 0x7C0004AE)                             \
  /* Load Floating-Point Single Indexed */              \
  V(lfsx, LFSX, 0x7C00042E)                             \
  /* Load Floating-Point Double with Update Indexed */  \
  V(lfdux, LFDUX, 0x7C0004EE)                           \
  /* Load Floating-Point Single with Update Indexed */  \
  V(lfsux, LFSUX, 0x7C00046E)                           \
  /* Store Byte with Update Indexed */                  \
  V(stbux, STBUX, 0x7C0001EE)                           \
  /* Store Byte Indexed */                              \
  V(stbx, STBX, 0x7C0001AE)                             \
  /* Store Halfword with Update Indexed */              \
  V(sthux, STHUX, 0x7C00036E)                           \
  /* Store Halfword Indexed */                          \
  V(sthx, STHX, 0x7C00032E)                             \
  /* Store Word with Update Indexed */                  \
  V(stwux, STWUX, 0x7C00016E)                           \
  /* Store Word Indexed */                              \
  V(stwx, STWX, 0x7C00012E)                             \
  /* Store Doubleword with Update Indexed */            \
  V(stdux, STDUX, 0x7C00016A)                           \
  /* Store Doubleword Indexed */                        \
  V(stdx, STDX, 0x7C00012A)                             \
  /* Store Floating-Point Double with Update Indexed */ \
  V(stfdux, STFDUX, 0x7C0005EE)                         \
  /* Store Floating-Point Double Indexed */             \
  V(stfdx, STFDX, 0x7C0005AE)                           \
  /* Store Floating-Point Single with Update Indexed */ \
  V(stfsux, STFSUX, 0x7C00056E)                         \
  /* Store Floating-Point Single Indexed */             \
  V(stfsx, STFSX, 0x7C00052E)                           \
  /* Store Doubleword Byte-Reverse Indexed */           \
  V(stdbrx, STDBRX, 0x7C000528)                         \
  /* Store Word Byte-Reverse Indexed */                 \
  V(stwbrx, STWBRX, 0x7C00052C)                         \
  /* Store Halfword Byte-Reverse Indexed */             \
  V(sthbrx, STHBRX, 0x7C00072C)                         \
  /* Load Vector Indexed */                             \
  V(lvx, LVX, 0x7C0000CE)                               \
  /* Store Vector Indexed */                            \
  V(stvx, STVX, 0x7C0001CE)

#define PPC_X_OPCODE_E_FORM_LIST(V)          \
  /* Shift Right Algebraic Word Immediate */ \
  V(srawi, SRAWIX, 0x7C000670)

#define PPC_X_OPCODE_F_FORM_LIST(V) \
  /* Compare */                     \
  V(cmp, CMP, 0x7C000000)           \
  /* Compare Logical */             \
  V(cmpl, CMPL, 0x7C000040)

#define PPC_X_OPCODE_G_FORM_LIST(V) \
  /* Byte-Reverse Halfword */       \
  V(brh, BRH, 0x7C0001B6)           \
  /* Byte-Reverse Word */           \
  V(brw, BRW, 0x7C000136)           \
  /* Byte-Reverse Doubleword */     \
  V(brd, BRD, 0x7C000176)

#define PPC_X_OPCODE_EH_S_FORM_LIST(V)                    \
  /* Store Byte Conditional Indexed */                    \
  V(stbcx, STBCX, 0x7C00056D)                             \
  /* Store Halfword Conditional Indexed Xform */          \
  V(sthcx, STHCX, 0x7C0005AD)                             \
  /* Store Word Conditional Indexed & record CR0 */       \
  V(stwcx, STWCX, 0x7C00012D)                             \
  /* Store Doubleword Conditional Indexed & record CR0 */ \
  V(stdcx, STDCX, 0x7C0001AD)

#define PPC_X_OPCODE_EH_L_FORM_LIST(V)          \
  /* Load Byte And Reserve Indexed */           \
  V(lbarx, LBARX, 0x7C000068)                   \
  /* Load Halfword And Reserve Indexed Xform */ \
  V(lharx, LHARX, 0x7C0000E8)                   \
  /* Load Word and Reserve Indexed */           \
  V(lwarx, LWARX, 0x7C000028)                   \
  /* Load Doubleword And Reserve Indexed */     \
  V(ldarx, LDARX, 0x7C0000A8)

#define PPC_X_OPCODE_UNUSED_LIST(V)                                           \
  /* Bit Permute Doubleword */                                                \
  V(bpermd, BPERMD, 0x7C0001F8)                                               \
  /* Extend Sign Word */                                                      \
  V(extsw, EXTSW, 0x7C0007B4)                                                 \
  /* Load Word Algebraic with Update Indexed */                               \
  V(lwaux, LWAUX, 0x7C0002EA)                                                 \
  /* Load Word Algebraic Indexed */                                           \
  V(lwax, LWAX, 0x7C0002AA)                                                   \
  /* Parity Doubleword */                                                     \
  V(prtyd, PRTYD, 0x7C000174)                                                 \
  /* Trap Doubleword */                                                       \
  V(td, TD, 0x7C000088)                                                       \
  /* Branch Conditional to Branch Target Address Register */                  \
  V(bctar, BCTAR, 0x4C000460)                                                 \
  /* Compare Byte */                                                          \
  V(cmpb, CMPB, 0x7C0003F8)                                                   \
  /* Data Cache Block Flush */                                                \
  V(dcbf, DCBF, 0x7C0000AC)                                                   \
  /* Data Cache Block Store */                                                \
  V(dcbst, DCBST, 0x7C00006C)                                                 \
  /* Data Cache Block Touch */                                                \
  V(dcbt, DCBT, 0x7C00022C)                                                   \
  /* Data Cache Block Touch for Store */                                      \
  V(dcbtst, DCBTST, 0x7C0001EC)                                               \
  /* Data Cache Block Zero */                                                 \
  V(dcbz, DCBZ, 0x7C0007EC)                                                   \
  /* Equivalent */                                                            \
  V(eqv, EQV, 0x7C000238)                                                     \
  /* Instruction Cache Block Invalidate */                                    \
  V(icbi, ICBI, 0x7C0007AC)                                                   \
  /* NAND */                                                                  \
  V(nand, NAND, 0x7C0003B8)                                                   \
  /* Parity Word */                                                           \
  V(prtyw, PRTYW, 0x7C000134)                                                 \
  /* Synchronize */                                                           \
  V(sync, SYNC, 0x7C0004AC)                                                   \
  /* Trap Word */                                                             \
  V(tw, TW, 0x7C000008)                                                       \
  /* ExecuExecuted No Operation */                                            \
  V(xnop, XNOP, 0x68000000)                                                   \
  /* Convert Binary Coded Decimal To Declets */                               \
  V(cbcdtd, CBCDTD, 0x7C000274)                                               \
  /* Convert Declets To Binary Coded Decimal */                               \
  V(cdtbcd, CDTBCD, 0x7C000234)                                               \
  /* Decimal Floating Add */                                                  \
  V(dadd, DADD, 0xEC000004)                                                   \
  /* Decimal Floating Add Quad */                                             \
  V(daddq, DADDQ, 0xFC000004)                                                 \
  /* Decimal Floating Convert From Fixed */                                   \
  V(dcffix, DCFFIX, 0xEC000644)                                               \
  /* Decimal Floating Convert From Fixed Quad */                              \
  V(dcffixq, DCFFIXQ, 0xFC000644)                                             \
  /* Decimal Floating Compare Ordered */                                      \
  V(dcmpo, DCMPO, 0xEC000104)                                                 \
  /* Decimal Floating Compare Ordered Quad */                                 \
  V(dcmpoq, DCMPOQ, 0xFC000104)                                               \
  /* Decimal Floating Compare Unordered */                                    \
  V(dcmpu, DCMPU, 0xEC000504)                                                 \
  /* Decimal Floating Compare Unordered Quad */                               \
  V(dcmpuq, DCMPUQ, 0xFC000504)                                               \
  /* Decimal Floating Convert To DFP Long */                                  \
  V(dctdp, DCTDP, 0xEC000204)                                                 \
  /* Decimal Floating Convert To Fixed */                                     \
  V(dctfix, DCTFIX, 0xEC000244)                                               \
  /* Decimal Floating Convert To Fixed Quad */                                \
  V(dctfixq, DCTFIXQ, 0xFC000244)                                             \
  /* Decimal Floating Convert To DFP Extended */                              \
  V(dctqpq, DCTQPQ, 0xFC000204)                                               \
  /* Decimal Floating Decode DPD To BCD */                                    \
  V(ddedpd, DDEDPD, 0xEC000284)                                               \
  /* Decimal Floating Decode DPD To BCD Quad */                               \
  V(ddedpdq, DDEDPDQ, 0xFC000284)                                             \
  /* Decimal Floating Divide */                                               \
  V(ddiv, DDIV, 0xEC000444)                                                   \
  /* Decimal Floating Divide Quad */                                          \
  V(ddivq, DDIVQ, 0xFC000444)                                                 \
  /* Decimal Floating Encode BCD To DPD */                                    \
  V(denbcd, DENBCD, 0xEC000684)                                               \
  /* Decimal Floating Encode BCD To DPD Quad */                               \
  V(denbcdq, DENBCDQ, 0xFC000684)                                             \
  /* Decimal Floating Insert Exponent */                                      \
  V(diex, DIEX, 0xEC0006C4)                                                   \
  /* Decimal Floating Insert Exponent Quad */                                 \
  V(diexq, DIEXQ, 0xFC0006C4)                                                 \
  /* Decimal Floating Multiply */                                             \
  V(dmul, DMUL, 0xEC000044)                                                   \
  /* Decimal Floating Multiply Quad */                                        \
  V(dmulq, DMULQ, 0xFC000044)                                                 \
  /* Decimal Floating Round To DFP Long */                                    \
  V(drdpq, DRDPQ, 0xFC000604)                                                 \
  /* Decimal Floating Round To DFP Short */                                   \
  V(drsp, DRSP, 0xEC000604)                                                   \
  /* Decimal Floating Subtract */                                             \
  V(dsub, DSUB, 0xEC000404)                                                   \
  /* Decimal Floating Subtract Quad */                                        \
  V(dsubq, DSUBQ, 0xFC000404)                                                 \
  /* Decimal Floating Test Exponent */                                        \
  V(dtstex, DTSTEX, 0xEC000144)                                               \
  /* Decimal Floating Test Exponent Quad */                                   \
  V(dtstexq, DTSTEXQ, 0xFC000144)                                             \
  /* Decimal Floating Test Significance */                                    \
  V(dtstsf, DTSTSF, 0xEC000544)                                               \
  /* Decimal Floating Test Significance Quad */                               \
  V(dtstsfq, DTSTSFQ, 0xFC000544)                                             \
  /* Decimal Floating Extract Exponent */                                     \
  V(dxex, DXEX, 0xEC0002C4)                                                   \
  /* Decimal Floating Extract Exponent Quad */                                \
  V(dxexq, DXEXQ, 0xFC0002C4)                                                 \
  /* Decorated Storage Notify */                                              \
  V(dsn, DSN, 0x7C0003C6)                                                     \
  /* Load Byte with Decoration Indexed */                                     \
  V(lbdx, LBDX, 0x7C000406)                                                   \
  /* Load Doubleword with Decoration Indexed */                               \
  V(lddx, LDDX, 0x7C0004C6)                                                   \
  /* Load Floating Doubleword with Decoration Indexed */                      \
  V(lfddx, LFDDX, 0x7C000646)                                                 \
  /* Load Halfword with Decoration Indexed */                                 \
  V(lhdx, LHDX, 0x7C000446)                                                   \
  /* Load Word with Decoration Indexed */                                     \
  V(lwdx, LWDX, 0x7C000486)                                                   \
  /* Store Byte with Decoration Indexed */                                    \
  V(stbdx, STBDX, 0x7C000506)                                                 \
  /* Store Doubleword with Decoration Indexed */                              \
  V(stddx, STDDX, 0x7C0005C6)                                                 \
  /* Store Floating Doubleword with Decoration Indexed */                     \
  V(stfddx, STFDDX, 0x7C000746)                                               \
  /* Store Halfword with Decoration Indexed */                                \
  V(sthdx, STHDX, 0x7C000546)                                                 \
  /* Store Word with Decoration Indexed */                                    \
  V(stwdx, STWDX, 0x7C000586)                                                 \
  /* Data Cache Block Allocate */                                             \
  V(dcba, DCBA, 0x7C0005EC)                                                   \
  /* Data Cache Block Invalidate */                                           \
  V(dcbi, DCBI, 0x7C0003AC)                                                   \
  /* Instruction Cache Block Touch */                                         \
  V(icbt, ICBT, 0x7C00002C)                                                   \
  /* Move to Condition Register from XER */                                   \
  V(mcrxr, MCRXR, 0x7C000400)                                                 \
  /* TLB Invalidate Local Indexed */                                          \
  V(tlbilx, TLBILX, 0x7C000024)                                               \
  /* TLB Invalidate Virtual Address Indexed */                                \
  V(tlbivax, TLBIVAX, 0x7C000624)                                             \
  /* TLB Read Entry */                                                        \
  V(tlbre, TLBRE, 0x7C000764)                                                 \
  /* TLB Search Indexed */                                                    \
  V(tlbsx, TLBSX, 0x7C000724)                                                 \
  /* TLB Write Entry */                                                       \
  V(tlbwe, TLBWE, 0x7C0007A4)                                                 \
  /* Write External Enable */                                                 \
  V(wrtee, WRTEE, 0x7C000106)                                                 \
  /* Write External Enable Immediate */                                       \
  V(wrteei, WRTEEI, 0x7C000146)                                               \
  /* Data Cache Read */                                                       \
  V(dcread, DCREAD, 0x7C00028C)                                               \
  /* Instruction Cache Read */                                                \
  V(icread, ICREAD, 0x7C0007CC)                                               \
  /* Data Cache Invalidate */                                                 \
  V(dci, DCI, 0x7C00038C)                                                     \
  /* Instruction Cache Invalidate */                                          \
  V(ici, ICI, 0x7C00078C)                                                     \
  /* Move From Device Control Register User Mode Indexed */                   \
  V(mfdcrux, MFDCRUX, 0x7C000246)                                             \
  /* Move From Device Control Register Indexed */                             \
  V(mfdcrx, MFDCRX, 0x7C000206)                                               \
  /* Move To Device Control Register User Mode Indexed */                     \
  V(mtdcrux, MTDCRUX, 0x7C000346)                                             \
  /* Move To Device Control Register Indexed */                               \
  V(mtdcrx, MTDCRX, 0x7C000306)                                               \
  /* Return From Debug Interrupt */                                           \
  V(rfdi, RFDI, 0x4C00004E)                                                   \
  /* Data Cache Block Flush by External PID */                                \
  V(dcbfep, DCBFEP, 0x7C0000FE)                                               \
  /* Data Cache Block Store by External PID */                                \
  V(dcbstep, DCBSTEP, 0x7C00007E)                                             \
  /* Data Cache Block Touch by External PID */                                \
  V(dcbtep, DCBTEP, 0x7C00027E)                                               \
  /* Data Cache Block Touch for Store by External PID */                      \
  V(dcbtstep, DCBTSTEP, 0x7C0001FE)                                           \
  /* Data Cache Block Zero by External PID */                                 \
  V(dcbzep, DCBZEP, 0x7C0007FE)                                               \
  /* Instruction Cache Block Invalidate by External PID */                    \
  V(icbiep, ICBIEP, 0x7C0007BE)                                               \
  /* Load Byte and Zero by External PID Indexed */                            \
  V(lbepx, LBEPX, 0x7C0000BE)                                                 \
  /* Load Floating-Point Double by External PID Indexed */                    \
  V(lfdepx, LFDEPX, 0x7C0004BE)                                               \
  /* Load Halfword and Zero by External PID Indexed */                        \
  V(lhepx, LHEPX, 0x7C00023E)                                                 \
  /* Load Vector by External PID Indexed */                                   \
  V(lvepx, LVEPX, 0x7C00024E)                                                 \
  /* Load Vector by External PID Indexed Last */                              \
  V(lvepxl, LVEPXL, 0x7C00020E)                                               \
  /* Load Word and Zero by External PID Indexed */                            \
  V(lwepx, LWEPX, 0x7C00003E)                                                 \
  /* Store Byte by External PID Indexed */                                    \
  V(stbepx, STBEPX, 0x7C0001BE)                                               \
  /* Store Floating-Point Double by External PID Indexed */                   \
  V(stfdepx, STFDEPX, 0x7C0005BE)                                             \
  /* Store Halfword by External PID Indexed */                                \
  V(sthepx, STHEPX, 0x7C00033E)                                               \
  /* Store Vector by External PID Indexed */                                  \
  V(stvepx, STVEPX, 0x7C00064E)                                               \
  /* Store Vector by External PID Indexed Last */                             \
  V(stvepxl, STVEPXL, 0x7C00060E)                                             \
  /* Store Word by External PID Indexed */                                    \
  V(stwepx, STWEPX, 0x7C00013E)                                               \
  /* Load Doubleword by External PID Indexed */                               \
  V(ldepx, LDEPX, 0x7C00003A)                                                 \
  /* Store Doubleword by External PID Indexed */                              \
  V(stdepx, STDEPX, 0x7C00013A)                                               \
  /* TLB Search and Reserve Indexed */                                        \
  V(tlbsrx, TLBSRX, 0x7C0006A5)                                               \
  /* External Control In Word Indexed */                                      \
  V(eciwx, ECIWX, 0x7C00026C)                                                 \
  /* External Control Out Word Indexed */                                     \
  V(ecowx, ECOWX, 0x7C00036C)                                                 \
  /* Data Cache Block Lock Clear */                                           \
  V(dcblc, DCBLC, 0x7C00030C)                                                 \
  /* Data Cache Block Lock Query */                                           \
  V(dcblq, DCBLQ, 0x7C00034D)                                                 \
  /* Data Cache Block Touch and Lock Set */                                   \
  V(dcbtls, DCBTLS, 0x7C00014C)                                               \
  /* Data Cache Block Touch for Store and Lock Set */                         \
  V(dcbtstls, DCBTSTLS, 0x7C00010C)                                           \
  /* Instruction Cache Block Lock Clear */                                    \
  V(icblc, ICBLC, 0x7C0001CC)                                                 \
  /* Instruction Cache Block Lock Query */                                    \
  V(icblq, ICBLQ, 0x7C00018D)                                                 \
  /* Instruction Cache Block Touch and Lock Set */                            \
  V(icbtls, ICBTLS, 0x7C0003CC)                                               \
  /* Floating Compare Ordered */                                              \
  V(fcmpo, FCMPO, 0xFC000040)                                                 \
  /* Floating Compare Unordered */                                            \
  V(fcmpu, FCMPU, 0xFC000000)                                                 \
  /* Floating Test for software Divide */                                     \
  V(ftdiv, FTDIV, 0xFC000100)                                                 \
  /* Floating Test for software Square Root */                                \
  V(ftsqrt, FTSQRT, 0xFC000140)                                               \
  /* Load Floating-Point as Integer Word Algebraic Indexed */                 \
  V(lfiwax, LFIWAX, 0x7C0006AE)                                               \
  /* Load Floating-Point as Integer Word and Zero Indexed */                  \
  V(lfiwzx, LFIWZX, 0x7C0006EE)                                               \
  /* Move To Condition Register from FPSCR */                                 \
  V(mcrfs, MCRFS, 0xFC000080)                                                 \
  /* Store Floating-Point as Integer Word Indexed */                          \
  V(stfiwx, STFIWX, 0x7C0007AE)                                               \
  /* Load Floating-Point Double Pair Indexed */                               \
  V(lfdpx, LFDPX, 0x7C00062E)                                                 \
  /* Store Floating-Point Double Pair Indexed */                              \
  V(stfdpx, STFDPX, 0x7C00072E)                                               \
  /* Floating Absolute Value */                                               \
  V(fabs, FABS, 0xFC000210)                                                   \
  /* Floating Convert From Integer Doubleword */                              \
  V(fcfid, FCFID, 0xFC00069C)                                                 \
  /* Floating Convert From Integer Doubleword Single */                       \
  V(fcfids, FCFIDS, 0xEC00069C)                                               \
  /* Floating Convert From Integer Doubleword Unsigned */                     \
  V(fcfidu, FCFIDU, 0xFC00079C)                                               \
  /* Floating Convert From Integer Doubleword Unsigned Single */              \
  V(fcfidus, FCFIDUS, 0xEC00079C)                                             \
  /* Floating Copy Sign */                                                    \
  V(fcpsgn, FCPSGN, 0xFC000010)                                               \
  /* Floating Convert To Integer Doubleword */                                \
  V(fctid, FCTID, 0xFC00065C)                                                 \
  /* Floating Convert To Integer Doubleword Unsigned */                       \
  V(fctidu, FCTIDU, 0xFC00075C)                                               \
  /* Floating Convert To Integer Doubleword Unsigned with round toward */     \
  /* Zero */                                                                  \
  V(fctiduz, FCTIDUZ, 0xFC00075E)                                             \
  /* Floating Convert To Integer Doubleword with round toward Zero */         \
  V(fctidz, FCTIDZ, 0xFC00065E)                                               \
  /* Floating Convert To Integer Word */                                      \
  V(fctiw, FCTIW, 0xFC00001C)                                                 \
  /* Floating Convert To Integer Word Unsigned */                             \
  V(fctiwu, FCTIWU, 0xFC00011C)                                               \
  /* Floating Convert To Integer Word Unsigned with round toward Zero */      \
  V(fctiwuz, FCTIWUZ, 0xFC00011E)                                             \
  /* Floating Convert To Integer Word with round to Zero */                   \
  V(fctiwz, FCTIWZ, 0xFC00001E)                                               \
  /* Floating Move Register */                                                \
  V(fmr, FMR, 0xFC000090)                                                     \
  /* Floating Negative Absolute Value */                                      \
  V(fnabs, FNABS, 0xFC000110)                                                 \
  /* Floating Negate */                                                       \
  V(fneg, FNEG, 0xFC000050)                                                   \
  /* Floating Round to Single-Precision */                                    \
  V(frsp, FRSP, 0xFC000018)                                                   \
  /* Move From FPSCR */                                                       \
  V(mffs, MFFS, 0xFC00048E)                                                   \
  /* Move To FPSCR Bit 0 */                                                   \
  V(mtfsb0, MTFSB0, 0xFC00008C)                                               \
  /* Move To FPSCR Bit 1 */                                                   \
  V(mtfsb1, MTFSB1, 0xFC00004C)                                               \
  /* Move To FPSCR Field Immediate */                                         \
  V(mtfsfi, MTFSFI, 0xFC00010C)                                               \
  /* Floating Round To Integer Minus */                                       \
  V(frim, FRIM, 0xFC0003D0)                                                   \
  /* Floating Round To Integer Nearest */                                     \
  V(frin, FRIN, 0xFC000310)                                                   \
  /* Floating Round To Integer Plus */                                        \
  V(frip, FRIP, 0xFC000390)                                                   \
  /* Floating Round To Integer toward Zero */                                 \
  V(friz, FRIZ, 0xFC000350)                                                   \
  /* Multiply Cross Halfword to Word Signed */                                \
  V(mulchw, MULCHW, 0x10000150)                                               \
  /* Multiply Cross Halfword to Word Unsigned */                              \
  V(mulchwu, MULCHWU, 0x10000110)                                             \
  /* Multiply High Halfword to Word Signed */                                 \
  V(mulhhw, MULHHW, 0x10000050)                                               \
  /* Multiply High Halfword to Word Unsigned */                               \
  V(mulhhwu, MULHHWU, 0x10000010)                                             \
  /* Multiply Low Halfword to Word Signed */                                  \
  V(mullhw, MULLHW, 0x10000350)                                               \
  /* Multiply Low Halfword to Word Unsigned */                                \
  V(mullhwu, MULLHWU, 0x10000310)                                             \
  /* Determine Leftmost Zero Byte DQ 56 E0000000 P 58 LSQ lq Load Quadword */ \
  V(dlmzb, DLMZB, 0x7C00009C)                                                 \
  /* Load Quadword And Reserve Indexed */                                     \
  V(lqarx, LQARX, 0x7C000228)                                                 \
  /* Store Quadword Conditional Indexed and record CR0 */                     \
  V(stqcx, STQCX, 0x7C00016D)                                                 \
  /* Load String Word Immediate */                                            \
  V(lswi, LSWI, 0x7C0004AA)                                                   \
  /* Load String Word Indexed */                                              \
  V(lswx, LSWX, 0x7C00042A)                                                   \
  /* Store String Word Immediate */                                           \
  V(stswi, STSWI, 0x7C0005AA)                                                 \
  /* Store String Word Indexed */                                             \
  V(stswx, STSWX, 0x7C00052A)                                                 \
  /* Clear BHRB */                                                            \
  V(clrbhrb, CLRBHRB, 0x7C00035C)                                             \
  /* Enforce In-order Execution of I/O */                                     \
  V(eieio, EIEIO, 0x7C0006AC)                                                 \
  /* Load Byte and Zero Caching Inhibited Indexed */                          \
  V(lbzcix, LBZCIX, 0x7C0006AA)                                               \
  /* Load Doubleword Caching Inhibited Indexed */                             \
  V(ldcix, LDCIX, 0x7C0006EA)                                                 \
  /* Load Halfword and Zero Caching Inhibited Indexed */                      \
  V(lhzcix, LHZCIX, 0x7C00066A)                                               \
  /* Load Word and Zero Caching Inhibited Indexed */                          \
  V(lwzcix, LWZCIX, 0x7C00062A)                                               \
  /* Move From Segment Register */                                            \
  V(mfsr, MFSR, 0x7C0004A6)                                                   \
  /* Move From Segment Register Indirect */                                   \
  V(mfsrin, MFSRIN, 0x7C000526)                                               \
  /* Move To Machine State Register Doubleword */                             \
  V(mtmsrd, MTMSRD, 0x7C000164)                                               \
  /* Move To Split Little Endian */                                           \
  V(mtsle, MTSLE, 0x7C000126)                                                 \
  /* Move To Segment Register */                                              \
  V(mtsr, MTSR, 0x7C0001A4)                                                   \
  /* Move To Segment Register Indirect */                                     \
  V(mtsrin, MTSRIN, 0x7C0001E4)                                               \
  /* SLB Find Entry ESID */                                                   \
  V(slbfee, SLBFEE, 0x7C0007A7)                                               \
  /* SLB Invalidate All */                                                    \
  V(slbia, SLBIA, 0x7C0003E4)                                                 \
  /* SLB Invalidate Entry */                                                  \
  V(slbie, SLBIE, 0x7C000364)                                                 \
  /* SLB Move From Entry ESID */                                              \
  V(slbmfee, SLBMFEE, 0x7C000726)                                             \
  /* SLB Move From Entry VSID */                                              \
  V(slbmfev, SLBMFEV, 0x7C0006A6)                                             \
  /* SLB Move To Entry */                                                     \
  V(slbmte, SLBMTE, 0x7C000324)                                               \
  /* Store Byte Caching Inhibited Indexed */                                  \
  V(stbcix, STBCIX, 0x7C0007AA)                                               \
  /* Store Doubleword Caching Inhibited Indexed */                            \
  V(stdcix, STDCIX, 0x7C0007EA)                                               \
  /* Store Halfword and Zero Caching Inhibited Indexed */                     \
  V(sthcix, STHCIX, 0x7C00076A)                                               \
  /* Store Word and Zero Caching Inhibited Indexed */                         \
  V(stwcix, STWCIX, 0x7C00072A)                                               \
  /* TLB Invalidate All */                                                    \
  V(tlbia, TLBIA, 0x7C0002E4)                                                 \
  /* TLB Invalidate Entry */                                                  \
  V(tlbie, TLBIE, 0x7C000264)                                                 \
  /* TLB Invalidate Entry Local */                                            \
  V(tlbiel, TLBIEL, 0x7C000224)                                               \
  /* Message Clear Privileged */                                              \
  V(msgclrp, MSGCLRP, 0x7C00015C)                                             \
  /* Message Send Privileged */                                               \
  V(msgsndp, MSGSNDP, 0x7C00011C)                                             \
  /* Message Clear */                                                         \
  V(msgclr, MSGCLR, 0x7C0001DC)                                               \
  /* Message Send */                                                          \
  V(msgsnd, MSGSND, 0x7C00019C)                                               \
  /* Move From Machine State Register */                                      \
  V(mfmsr, MFMSR, 0x7C0000A6)                                                 \
  /* Move To Machine State Register */                                        \
  V(mtmsr, MTMSR, 0x7C000124)                                                 \
  /* TLB Synchronize */                                                       \
  V(tlbsync, TLBSYNC, 0x7C00046C)                                             \
  /* Transaction Abort */                                                     \
  V(tabort, TABORT, 0x7C00071D)                                               \
  /* Transaction Abort Doubleword Conditional */                              \
  V(tabortdc, TABORTDC, 0x7C00065D)                                           \
  /* Transaction Abort Doubleword Conditional Immediate */                    \
  V(tabortdci, TABORTDCI, 0x7C0006DD)                                         \
  /* Transaction Abort Word Conditional */                                    \
  V(tabortwc, TABORTWC, 0x7C00061D)                                           \
  /* Transaction Abort Word Conditional Immediate */                          \
  V(tabortwci, TABORTWCI, 0x7C00069D)                                         \
  /* Transaction Begin */                                                     \
  V(tbegin, TBEGIN, 0x7C00051D)                                               \
  /* Transaction Check */                                                     \
  V(tcheck, TCHECK, 0x7C00059C)                                               \
  /* Transaction End */                                                       \
  V(tend, TEND, 0x7C00055C)                                                   \
  /* Transaction Recheckpoint */                                              \
  V(trechkpt, TRECHKPT, 0x7C0007DD)                                           \
  /* Transaction Reclaim */                                                   \
  V(treclaim, TRECLAIM, 0x7C00075D)                                           \
  /* Transaction Suspend or Resume */                                         \
  V(tsr, TSR, 0x7C0005DC)                                                     \
  /* Load Vector Element Byte Indexed */                                      \
  V(lvebx, LVEBX, 0x7C00000E)                                                 \
  /* Load Vector Element Halfword Indexed */                                  \
  V(lvehx, LVEHX, 0x7C00004E)                                                 \
  /* Load Vector Element Word Indexed */                                      \
  V(lvewx, LVEWX, 0x7C00008E)                                                 \
  /* Load Vector for Shift Left */                                            \
  V(lvsl, LVSL, 0x7C00000C)                                                   \
  /* Load Vector for Shift Right */                                           \
  V(lvsr, LVSR, 0x7C00004C)                                                   \
  /* Load Vector Indexed Last */                                              \
  V(lvxl, LVXL, 0x7C0002CE)                                                   \
  /* Store Vector Element Byte Indexed */                                     \
  V(stvebx, STVEBX, 0x7C00010E)                                               \
  /* Store Vector Element Halfword Indexed */                                 \
  V(stvehx, STVEHX, 0x7C00014E)                                               \
  /* Store Vector Element Word Indexed */                                     \
  V(stvewx, STVEWX, 0x7C00018E)                                               \
  /* Store Vector Indexed Last */                                             \
  V(stvxl, STVXL, 0x7C0003CE)                                                 \
  /* Floating Merge Even Word */                                              \
  V(fmrgew, FMRGEW, 0xFC00078C)                                               \
  /* Floating Merge Odd Word */                                               \
  V(fmrgow, FMRGOW, 0xFC00068C)                                               \
  /* Wait for Interrupt */                                                    \
  V(wait, WAIT, 0x7C00007C)

#define PPC_X_OPCODE_LIST(V)     \
  PPC_X_OPCODE_A_FORM_LIST(V)    \
  PPC_X_OPCODE_B_FORM_LIST(V)    \
  PPC_X_OPCODE_C_FORM_LIST(V)    \
  PPC_X_OPCODE_D_FORM_LIST(V)    \
  PPC_X_OPCODE_E_FORM_LIST(V)    \
  PPC_X_OPCODE_F_FORM_LIST(V)    \
  PPC_X_OPCODE_G_FORM_LIST(V)    \
  PPC_X_OPCODE_EH_L_FORM_LIST(V) \
  PPC_X_OPCODE_UNUSED_LIST(V)

#define PPC_EVS_OPCODE_LIST(V) \
  /* Vector Select */          \
  V(evsel, EVSEL, 0x10000278)

#define PPC_DS_OPCODE_LIST(V)            \
  /* Load Doubleword */                  \
  V(ld, LD, 0xE8000000)                  \
  /* Load Doubleword with Update */      \
  V(ldu, LDU, 0xE8000001)                \
  /* Load Word Algebraic */              \
  V(lwa, LWA, 0xE8000002)                \
  /* Store Doubleword */                 \
  V(std, STD, 0xF8000000)                \
  /* Store Doubleword with Update */     \
  V(stdu, STDU, 0xF8000001)              \
  /* Load Floating-Point Double Pair */  \
  V(lfdp, LFDP, 0xE4000000)              \
  /* Store Floating-Point Double Pair */ \
  V(stfdp, STFDP, 0xF4000000)            \
  /* Store Quadword */                   \
  V(stq, STQ, 0xF8000002)

#define PPC_DQ_OPCODE_LIST(V) V(lsq, LSQ, 0xE0000000)

#define PPC_D_OPCODE_LIST(V)                    \
  /* Trap Doubleword Immediate */               \
  V(tdi, TDI, 0x08000000)                       \
  /* Add Immediate */                           \
  V(addi, ADDI, 0x38000000)                     \
  /* Add Immediate Carrying */                  \
  V(addic, ADDIC, 0x30000000)                   \
  /* Add Immediate Carrying & record CR0 */     \
  V(addicx, ADDICx, 0x34000000)                 \
  /* Add Immediate Shifted */                   \
  V(addis, ADDIS, 0x3C000000)                   \
  /* AND Immediate & record CR0 */              \
  V(andix, ANDIx, 0x70000000)                   \
  /* AND Immediate Shifted & record CR0 */      \
  V(andisx, ANDISx, 0x74000000)                 \
  /* Compare Immediate */                       \
  V(cmpi, CMPI, 0x2C000000)                     \
  /* Compare Logical Immediate */               \
  V(cmpli, CMPLI, 0x28000000)                   \
  /* Load Byte and Zero */                      \
  V(lbz, LBZ, 0x88000000)                       \
  /* Load Byte and Zero with Update */          \
  V(lbzu, LBZU, 0x8C000000)                     \
  /* Load Halfword Algebraic */                 \
  V(lha, LHA, 0xA8000000)                       \
  /* Load Halfword Algebraic with Update */     \
  V(lhau, LHAU, 0xAC000000)                     \
  /* Load Halfword and Zero */                  \
  V(lhz, LHZ, 0xA0000000)                       \
  /* Load Halfword and Zero with Update */      \
  V(lhzu, LHZU, 0xA4000000)                     \
  /* Load Multiple Word */                      \
  V(lmw, LMW, 0xB8000000)                       \
  /* Load Word and Zero */                      \
  V(lwz, LWZ, 0x80000000)                       \
  /* Load Word and Zero with Update */          \
  V(lwzu, LWZU, 0x84000000)                     \
  /* Multiply Low Immediate */                  \
  V(mulli, MULLI, 0x1C000000)                   \
  /* OR Immediate */                            \
  V(ori, ORI, 0x60000000)                       \
  /* OR Immediate Shifted */                    \
  V(oris, ORIS, 0x64000000)                     \
  /* Store Byte */                              \
  V(stb, STB, 0x98000000)                       \
  /* Store Byte with Update */                  \
  V(stbu, STBU, 0x9C000000)                     \
  /* Store Halfword */                          \
  V(sth, STH, 0xB0000000)                       \
  /* Store Halfword with Update */              \
  V(sthu, STHU, 0xB4000000)                     \
  /* Store Multiple Word */                     \
  V(stmw, STMW, 0xBC000000)                     \
  /* Store Word */                              \
  V(stw, STW, 0x90000000)                       \
  /* Store Word with Update */                  \
  V(stwu, STWU, 0x94000000)                     \
  /* Subtract From Immediate Carrying */        \
  V(subfic, SUBFIC, 0x20000000)                 \
  /* Trap Word Immediate */                     \
  V(twi, TWI, 0x0C000000)                       \
  /* XOR Immediate */                           \
  V(xori, XORI, 0x68000000)                     \
  /* XOR Immediate Shifted */                   \
  V(xoris, XORIS, 0x6C000000)                   \
  /* Load Floating-Point Double */              \
  V(lfd, LFD, 0xC8000000)                       \
  /* Load Floating-Point Double with Update */  \
  V(lfdu, LFDU, 0xCC000000)                     \
  /* Load Floating-Point Single */              \
  V(lfs, LFS, 0xC0000000)                       \
  /* Load Floating-Point Single with Update */  \
  V(lfsu, LFSU, 0xC4000000)                     \
  /* Store Floating-Point Double */             \
  V(stfd, STFD, 0xD8000000)                     \
  /* Store Floating-Point Double with Update */ \
  V(stfdu, STFDU, 0xDC000000)                   \
  /* Store Floating-Point Single */             \
  V(stfs, STFS, 0xD0000000)                     \
  /* Store Floating-Point Single with Update */ \
  V(stfsu, STFSU, 0xD4000000)

#define PPC_XFL_OPCODE_LIST(V) \
  /* Move To FPSCR Fields */   \
  V(mtfsf, MTFSF, 0xFC00058E)

#define PPC_XFX_OPCODE_LIST(V)                  \
  /* Move From Condition Register */            \
  V(mfcr, MFCR, 0x7C000026)                     \
  /* Move From One Condition Register Field */  \
  V(mfocrf, MFOCRF, 0x7C100026)                 \
  /* Move From Special Purpose Register */      \
  V(mfspr, MFSPR, 0x7C0002A6)                   \
  /* Move To Condition Register Fields */       \
  V(mtcrf, MTCRF, 0x7C000120)                   \
  /* Move To One Condition Register Field */    \
  V(mtocrf, MTOCRF, 0x7C100120)                 \
  /* Move To Special Purpose Register */        \
  V(mtspr, MTSPR, 0x7C0003A6)                   \
  /* Debugger Notify Halt */                    \
  V(dnh, DNH, 0x4C00018C)                       \
  /* Move From Device Control Register */       \
  V(mfdcr, MFDCR, 0x7C000286)                   \
  /* Move To Device Control Register */         \
  V(mtdcr, MTDCR, 0x7C000386)                   \
  /* Move from Performance Monitor Register */  \
  V(mfpmr, MFPMR, 0x7C00029C)                   \
  /* Move To Performance Monitor Register */    \
  V(mtpmr, MTPMR, 0x7C00039C)                   \
  /* Move From Branch History Rolling Buffer */ \
  V(mfbhrbe, MFBHRBE, 0x7C00025C)               \
  /* Move From Time Base */                     \
  V(mftb, MFTB, 0x7C0002E6)

#define PPC_MDS_OPCODE_LIST(V)                  \
  /* Rotate Left Doubleword then Clear Left */  \
  V(rldcl, RLDCL, 0x78000010)                   \
  /* Rotate Left Doubleword then Clear Right */ \
  V(rldcr, RLDCR, 0x78000012)

#define PPC_A_OPCODE_LIST(V)                            \
  /* Integer Select */                                  \
  V(isel, ISEL, 0x7C00001E)                             \
  /* Floating Add */                                    \
  V(fadd, FADD, 0xFC00002A)                             \
  /* Floating Add Single */                             \
  V(fadds, FADDS, 0xEC00002A)                           \
  /* Floating Divide */                                 \
  V(fdiv, FDIV, 0xFC000024)                             \
  /* Floating Divide Single */                          \
  V(fdivs, FDIVS, 0xEC000024)                           \
  /* Floating Multiply-Add */                           \
  V(fmadd, FMADD, 0xFC00003A)                           \
  /* Floating Multiply-Add Single */                    \
  V(fmadds, FMADDS, 0xEC00003A)                         \
  /* Floating Multiply-Subtract */                      \
  V(fmsub, FMSUB, 0xFC000038)                           \
  /* Floating Multiply-Subtract Single */               \
  V(fmsubs, FMSUBS, 0xEC000038)                         \
  /* Floating Multiply */                               \
  V(fmul, FMUL, 0xFC000032)                             \
  /* Floating Multiply Single */                        \
  V(fmuls, FMULS, 0xEC000032)                           \
  /* Floating Negative Multiply-Add */                  \
  V(fnmadd, FNMADD, 0xFC00003E)                         \
  /* Floating Negative Multiply-Add Single */           \
  V(fnmadds, FNMADDS, 0xEC00003E)                       \
  /* Floating Negative Multiply-Subtract */             \
  V(fnmsub, FNMSUB, 0xFC00003C)                         \
  /* Floating Negative Multiply-Subtract Single */      \
  V(fnmsubs, FNMSUBS, 0xEC00003C)                       \
  /* Floating Reciprocal Estimate Single */             \
  V(fres, FRES, 0xEC000030)                             \
  /* Floating Reciprocal Square Root Estimate */        \
  V(frsqrte, FRSQRTE, 0xFC000034)                       \
  /* Floating Select */                                 \
  V(fsel, FSEL, 0xFC00002E)                             \
  /* Floating Square Root */                            \
  V(fsqrt, FSQRT, 0xFC00002C)                           \
  /* Floating Square Root Single */                     \
  V(fsqrts, FSQRTS, 0xEC00002C)                         \
  /* Floating Subtract */                               \
  V(fsub, FSUB, 0xFC000028)                             \
  /* Floating Subtract Single */                        \
  V(fsubs, FSUBS, 0xEC000028)                           \
  /* Floating Reciprocal Estimate */                    \
  V(fre, FRE, 0xFC000030)                               \
  /* Floating Reciprocal Square Root Estimate Single */ \
  V(frsqrtes, FRSQRTES, 0xEC000034)

#define PPC_VA_OPCODE_A_FORM_LIST(V)                            \
  /* Vector Permute */                                          \
  V(vperm, VPERM, 0x1000002B)                                   \
  /* Vector Multiply-Low-Add Unsigned Halfword Modulo */        \
  V(vmladduhm, VMLADDUHM, 0x10000022)                           \
  /* Vector Select */                                           \
  V(vsel, VSEL, 0x1000002A)                                     \
  /* Vector Multiply-Sum Mixed Byte Modulo */                   \
  V(vmsummbm, VMSUMMBM, 0x10000025)                             \
  /* Vector Multiply-Sum Signed Halfword Modulo */              \
  V(vmsumshm, VMSUMSHM, 0x10000028)                             \
  /* Vector Multiply-High-Round-Add Signed Halfword Saturate */ \
  V(vmhraddshs, VMHRADDSHS, 0x10000021)

#define PPC_VA_OPCODE_UNUSED_LIST(V)                             \
  /* Vector Add Extended & write Carry Unsigned Quadword */      \
  V(vaddecuq, VADDECUQ, 0x1000003D)                              \
  /* Vector Add Extended Unsigned Quadword Modulo */             \
  V(vaddeuqm, VADDEUQM, 0x1000003C)                              \
  /* Vector Multiply-Add Single-Precision */                     \
  V(vmaddfp, VMADDFP, 0x1000002E)                                \
  /* Vector Multiply-High-Add Signed Halfword Saturate */        \
  V(vmhaddshs, VMHADDSHS, 0x10000020)                            \
  /* Vector Multiply-Sum Signed Halfword Saturate */             \
  V(vmsumshs, VMSUMSHS, 0x10000029)                              \
  /* Vector Multiply-Sum Unsigned Byte Modulo */                 \
  V(vmsumubm, VMSUMUBM, 0x10000024)                              \
  /* Vector Multiply-Sum Unsigned Halfword Modulo */             \
  V(vmsumuhm, VMSUMUHM, 0x10000026)                              \
  /* Vector Multiply-Sum Unsigned Halfword Saturate */           \
  V(vmsumuhs, VMSUMUHS, 0x10000027)                              \
  /* Vector Negative Multiply-Subtract Single-Precision */       \
  V(vnmsubfp, VNMSUBFP, 0x1000002F)                              \
  /* Vector Shift Left Double by Octet Immediate */              \
  V(vsldoi, VSLDOI, 0x1000002C)                                  \
  /* Vector Subtract Extended & write Carry Unsigned Quadword */ \
  V(vsubecuq, VSUBECUQ, 0x1000003F)                              \
  /* Vector Subtract Extended Unsigned Quadword Modulo */        \
  V(vsubeuqm, VSUBEUQM, 0x1000003E)                              \
  /* Vector Permute and Exclusive-OR */                          \
  V(vpermxor, VPERMXOR, 0x1000002D)

#define PPC_VA_OPCODE_LIST(V)  \
  PPC_VA_OPCODE_A_FORM_LIST(V) \
  PPC_VA_OPCODE_UNUSED_LIST(V)

#define PPC_XX1_OPCODE_LIST(V)                             \
  /* Load VSR Scalar Doubleword Indexed */                 \
  V(lxsdx, LXSDX, 0x7C000498)                              \
  /* Load VSX Scalar as Integer Word Algebraic Indexed */  \
  V(lxsiwax, LXSIWAX, 0x7C000098)                          \
  /* Load VSX Scalar as Integer Byte & Zero Indexed */     \
  V(lxsibzx, LXSIBZX, 0x7C00061A)                          \
  /* Load VSX Scalar as Integer Halfword & Zero Indexed */ \
  V(lxsihzx, LXSIHZX, 0x7C00065A)                          \
  /* Load VSX Scalar as Integer Word and Zero Indexed */   \
  V(lxsiwzx, LXSIWZX, 0x7C000018)                          \
  /* Load VSX Scalar Single-Precision Indexed */           \
  V(lxsspx, LXSSPX, 0x7C000418)                            \
  /* Load VSR Vector Doubleword*2 Indexed */               \
  V(lxvd, LXVD, 0x7C000698)                                \
  /* Load VSX Vector Indexed */                            \
  V(lxvx, LXVX, 0x7C000218)                                \
  /* Load VSR Vector Doubleword & Splat Indexed */         \
  V(lxvdsx, LXVDSX, 0x7C000298)                            \
  /* Load VSR Vector Word*4 Indexed */                     \
  V(lxvw, LXVW, 0x7C000618)                                \
  /* Move To VSR Doubleword */                             \
  V(mtvsrd, MTVSRD, 0x7C000166)                            \
  /* Move To VSR Double Doubleword */                      \
  V(mtvsrdd, MTVSRDD, 0x7C000366)                          \
  /* Move To VSR Word Algebraic */                         \
  V(mtvsrwa, MTVSRWA, 0x7C0001A6)                          \
  /* Move To VSR Word and Zero */                          \
  V(mtvsrwz, MTVSRWZ, 0x7C0001E6)                          \
  /* Move From VSR Doubleword */                           \
  V(mfvsrd, MFVSRD, 0x7C000066)                            \
  /* Move From VSR Word and Zero */                        \
  V(mfvsrwz, MFVSRWZ, 0x7C0000E6)                          \
  /* Store VSR Scalar Doubleword Indexed */                \
  V(stxsdx, STXSDX, 0x7C000598)                            \
  /* Store VSX Scalar as Integer Word Indexed */           \
  V(stxsiwx, STXSIWX, 0x7C000118)                          \
  /* Store VSX Scalar as Integer Halfword Indexed */       \
  V(stxsihx, STXSIHX, 0x7C00075A)                          \
  /* Store VSX Scalar as Integer Byte Indexed */           \
  V(stxsibx, STXSIBX, 0x7C00071A)                          \
  /* Store VSR Scalar Word Indexed */                      \
  V(stxsspx, STXSSPX, 0x7C000518)                          \
  /* Store VSR Vector Doubleword*2 Indexed */              \
  V(stxvd, STXVD, 0x7C000798)                              \
  /* Store VSX Vector Indexed */                           \
  V(stxvx, STXVX, 0x7C000318)                              \
  /* Store VSR Vector Word*4 Indexed */                    \
  V(stxvw, STXVW, 0x7C000718)

#define PPC_B_OPCODE_LIST(V) \
  /* Branch Conditional */   \
  V(bc, BCX, 0x40000000)

#define PPC_XO_OPCODE_LIST(V)                                               \
  /* Divide Doubleword */                                                   \
  V(divd, DIVD, 0x7C0003D2)                                                 \
  /* Divide Doubleword Extended */                                          \
  V(divde, DIVDE, 0x7C000352)                                               \
  /* Divide Doubleword Extended & record OV */                              \
  V(divdeo, DIVDEO, 0x7C000752)                                             \
  /* Divide Doubleword Extended Unsigned */                                 \
  V(divdeu, DIVDEU, 0x7C000312)                                             \
  /* Divide Doubleword Extended Unsigned & record OV */                     \
  V(divdeuo, DIVDEUO, 0x7C000712)                                           \
  /* Divide Doubleword & record OV */                                       \
  V(divdo, DIVDO, 0x7C0007D2)                                               \
  /* Divide Doubleword Unsigned */                                          \
  V(divdu, DIVDU, 0x7C000392)                                               \
  /* Divide Doubleword Unsigned & record OV */                              \
  V(divduo, DIVDUO, 0x7C000792)                                             \
  /* Multiply High Doubleword */                                            \
  V(mulhd, MULHD, 0x7C000092)                                               \
  /* Multiply High Doubleword Unsigned */                                   \
  V(mulhdu, MULHDU, 0x7C000012)                                             \
  /* Multiply Low Doubleword */                                             \
  V(mulld, MULLD, 0x7C0001D2)                                               \
  /* Multiply Low Doubleword & record OV */                                 \
  V(mulldo, MULLDO, 0x7C0005D2)                                             \
  /* Add */                                                                 \
  V(add, ADDX, 0x7C000214)                                                  \
  /* Add Carrying */                                                        \
  V(addc, ADDCX, 0x7C000014)                                                \
  /* Add Carrying & record OV */                                            \
  V(addco, ADDCO, 0x7C000414)                                               \
  /* Add Extended */                                                        \
  V(adde, ADDEX, 0x7C000114)                                                \
  /* Add Extended & record OV & record OV */                                \
  V(addeo, ADDEO, 0x7C000514)                                               \
  /* Add to Minus One Extended */                                           \
  V(addme, ADDME, 0x7C0001D4)                                               \
  /* Add to Minus One Extended & record OV */                               \
  V(addmeo, ADDMEO, 0x7C0005D4)                                             \
  /* Add & record OV */                                                     \
  V(addo, ADDO, 0x7C000614)                                                 \
  /* Add to Zero Extended */                                                \
  V(addze, ADDZEX, 0x7C000194)                                              \
  /* Add to Zero Extended & record OV */                                    \
  V(addzeo, ADDZEO, 0x7C000594)                                             \
  /* Divide Word Format */                                                  \
  V(divw, DIVW, 0x7C0003D6)                                                 \
  /* Divide Word Extended */                                                \
  V(divwe, DIVWE, 0x7C000356)                                               \
  /* Divide Word Extended & record OV */                                    \
  V(divweo, DIVWEO, 0x7C000756)                                             \
  /* Divide Word Extended Unsigned */                                       \
  V(divweu, DIVWEU, 0x7C000316)                                             \
  /* Divide Word Extended Unsigned & record OV */                           \
  V(divweuo, DIVWEUO, 0x7C000716)                                           \
  /* Divide Word & record OV */                                             \
  V(divwo, DIVWO, 0x7C0007D6)                                               \
  /* Divide Word Unsigned */                                                \
  V(divwu, DIVWU, 0x7C000396)                                               \
  /* Divide Word Unsigned & record OV */                                    \
  V(divwuo, DIVWUO, 0x7C000796)                                             \
  /* Multiply High Word */                                                  \
  V(mulhw, MULHWX, 0x7C000096)                                              \
  /* Multiply High Word Unsigned */                                         \
  V(mulhwu, MULHWUX, 0x7C000016)                                            \
  /* Multiply Low Word */                                                   \
  V(mullw, MULLW, 0x7C0001D6)                                               \
  /* Multiply Low Word & record OV */                                       \
  V(mullwo, MULLWO, 0x7C0005D6)                                             \
  /* Negate */                                                              \
  V(neg, NEGX, 0x7C0000D0)                                                  \
  /* Negate & record OV */                                                  \
  V(nego, NEGO, 0x7C0004D0)                                                 \
  /* Subtract From */                                                       \
  V(subf, SUBFX, 0x7C000050)                                                \
  /* Subtract From Carrying */                                              \
  V(subfc, SUBFCX, 0x7C000010)                                              \
  /* Subtract From Carrying & record OV */                                  \
  V(subfco, SUBFCO, 0x7C000410)                                             \
  /* Subtract From Extended */                                              \
  V(subfe, SUBFEX, 0x7C000110)                                              \
  /* Subtract From Extended & record OV */                                  \
  V(subfeo, SUBFEO, 0x7C000510)                                             \
  /* Subtract From Minus One Extended */                                    \
  V(subfme, SUBFME, 0x7C0001D0)                                             \
  /* Subtract From Minus One Extended & record OV */                        \
  V(subfmeo, SUBFMEO, 0x7C0005D0)                                           \
  /* Subtract From & record OV */                                           \
  V(subfo, SUBFO, 0x7C000450)                                               \
  /* Subtract From Zero Extended */                                         \
  V(subfze, SUBFZE, 0x7C000190)                                             \
  /* Subtract From Zero Extended & record OV */                             \
  V(subfzeo, SUBFZEO, 0x7C000590)                                           \
  /* Add and Generate Sixes */                                              \
  V(addg, ADDG, 0x7C000094)                                                 \
  /* Multiply Accumulate Cross Halfword to Word Modulo Signed */            \
  V(macchw, MACCHW, 0x10000158)                                             \
  /* Multiply Accumulate Cross Halfword to Word Saturate Signed */          \
  V(macchws, MACCHWS, 0x100001D8)                                           \
  /* Multiply Accumulate Cross Halfword to Word Saturate Unsigned */        \
  V(macchwsu, MACCHWSU, 0x10000198)                                         \
  /* Multiply Accumulate Cross Halfword to Word Modulo Unsigned */          \
  V(macchwu, MACCHWU, 0x10000118)                                           \
  /* Multiply Accumulate High Halfword to Word Modulo Signed */             \
  V(machhw, MACHHW, 0x10000058)                                             \
  /* Multiply Accumulate High Halfword to Word Saturate Signed */           \
  V(machhws, MACHHWS, 0x100000D8)                                           \
  /* Multiply Accumulate High Halfword to Word Saturate Unsigned */         \
  V(machhwsu, MACHHWSU, 0x10000098)                                         \
  /* Multiply Accumulate High Halfword to Word Modulo Unsigned */           \
  V(machhwu, MACHHWU, 0x10000018)                                           \
  /* Multiply Accumulate Low Halfword to Word Modulo Signed */              \
  V(maclhw, MACLHW, 0x10000358)                                             \
  /* Multiply Accumulate Low Halfword to Word Saturate Signed */            \
  V(maclhws, MACLHWS, 0x100003D8)                                           \
  /* Multiply Accumulate Low Halfword to Word Saturate Unsigned */          \
  V(maclhwsu, MACLHWSU, 0x10000398)                                         \
  /* Multiply Accumulate Low Halfword to Word Modulo Unsigned */            \
  V(maclhwu, MACLHWU, 0x10000318)                                           \
  /* Negative Multiply Accumulate Cross Halfword to Word Modulo Signed */   \
  V(nmacchw, NMACCHW, 0x1000015C)                                           \
  /* Negative Multiply Accumulate Cross Halfword to Word Saturate Signed */ \
  V(nmacchws, NMACCHWS, 0x100001DC)                                         \
  /* Negative Multiply Accumulate High Halfword to Word Modulo Signed */    \
  V(nmachhw, NMACHHW, 0x1000005C)                                           \
  /* Negative Multiply Accumulate High Halfword to Word Saturate Signed */  \
  V(nmachhws, NMACHHWS, 0x100000DC)                                         \
  /* Negative Multiply Accumulate Low Halfword to Word Modulo Signed */     \
  V(nmaclhw, NMACLHW, 0x1000035C)                                           \
  /* Negative Multiply Accumulate Low Halfword to Word Saturate Signed */   \
  V(nmaclhws, NMACLHWS, 0x100003DC)

#define PPC_XL_OPCODE_LIST(V)                       \
  /* Branch Conditional to Count Register */        \
  V(bcctr, BCCTRX, 0x4C000420)                      \
  /* Branch Conditional to Link Register */         \
  V(bclr, BCLRX, 0x4C000020)                        \
  /* Condition Register AND */                      \
  V(crand, CRAND, 0x4C000202)                       \
  /* Condition Register AND with Complement */      \
  V(crandc, CRANDC, 0x4C000102)                     \
  /* Condition Register Equivalent */               \
  V(creqv, CREQV, 0x4C000242)                       \
  /* Condition Register NAND */                     \
  V(crnand, CRNAND, 0x4C0001C2)                     \
  /* Condition Register NOR */                      \
  V(crnor, CRNOR, 0x4C000042)                       \
  /* Condition Register OR */                       \
  V(cror, CROR, 0x4C000382)                         \
  /* Condition Register OR with Complement */       \
  V(crorc, CRORC, 0x4C000342)                       \
  /* Condition Register XOR */                      \
  V(crxor, CRXOR, 0x4C000182)                       \
  /* Instruction Synchronize */                     \
  V(isync, ISYNC, 0x4C00012C)                       \
  /* Move Condition Register Field */               \
  V(mcrf, MCRF, 0x4C000000)                         \
  /* Return From Critical Interrupt */              \
  V(rfci, RFCI, 0x4C000066)                         \
  /* Return From Interrupt */                       \
  V(rfi, RFI, 0x4C000064)                           \
  /* Return From Machine Check Interrupt */         \
  V(rfmci, RFMCI, 0x4C00004C)                       \
  /* Embedded Hypervisor Privilege */               \
  V(ehpriv, EHPRIV, 0x7C00021C)                     \
  /* Return From Guest Interrupt */                 \
  V(rfgi, RFGI, 0x4C0000CC)                         \
  /* Doze */                                        \
  V(doze, DOZE, 0x4C000324)                         \
  /* Return From Interrupt Doubleword Hypervisor */ \
  V(hrfid, HRFID, 0x4C000224)                       \
  /* Nap */                                         \
  V(nap, NAP, 0x4C000364)                           \
  /* Return from Event Based Branch */              \
  V(rfebb, RFEBB, 0x4C000124)                       \
  /* Return from Interrupt Doubleword */            \
  V(rfid, RFID, 0x4C000024)                         \
  /* Rip Van Winkle */                              \
  V(rvwinkle, RVWINKLE, 0x4C0003E4)                 \
  /* Sleep */                                       \
  V(sleep, SLEEP, 0x4C0003A4)

#define PPC_XX4_OPCODE_LIST(V) \
  /* VSX Select */             \
  V(xxsel, XXSEL, 0xF0000030)

#define PPC_I_OPCODE_LIST(V) \
  /* Branch */               \
  V(b, BX, 0x48000000)

#define PPC_M_OPCODE_LIST(V)                          \
  /* Rotate Left Word Immediate then Mask Insert */   \
  V(rlwimi, RLWIMIX, 0x50000000)                      \
  /* Rotate Left Word Immediate then AND with Mask */ \
  V(rlwinm, RLWINMX, 0x54000000)                      \
  /* Rotate Left Word then AND with Mask */           \
  V(rlwnm, RLWNMX, 0x5C000000)

#define PPC_VX_OPCODE_A_FORM_LIST(V)     \
  /* Vector Splat Byte */                \
  V(vspltb, VSPLTB, 0x1000020C)          \
  /* Vector Splat Word */                \
  V(vspltw, VSPLTW, 0x1000028C)          \
  /* Vector Splat Halfword */            \
  V(vsplth, VSPLTH, 0x1000024C)          \
  /* Vector Extract Unsigned Byte */     \
  V(vextractub, VEXTRACTUB, 0x1000020D)  \
  /* Vector Extract Unsigned Halfword */ \
  V(vextractuh, VEXTRACTUH, 0x1000024D)  \
  /* Vector Extract Unsigned Word */     \
  V(vextractuw, VEXTRACTUW, 0x1000028D)  \
  /* Vector Extract Doubleword */        \
  V(vextractd, VEXTRACTD, 0x100002CD)    \
  /* Vector Insert Byte */               \
  V(vinsertb, VINSERTB, 0x1000030D)      \
  /* Vector Insert Halfword */           \
  V(vinserth, VINSERTH, 0x1000034D)      \
  /* Vector Insert Word */               \
  V(vinsertw, VINSERTW, 0x1000038D)      \
  /* Vector Insert Doubleword */         \
  V(vinsertd, VINSERTD, 0x100003CD)

#define PPC_VX_OPCODE_B_FORM_LIST(V)                       \
  /* Vector Logical OR */                                  \
  V(vor, VOR, 0x10000484)                                  \
  /* Vector Logical XOR */                                 \
  V(vxor, VXOR, 0x100004C4)                                \
  /* Vector Logical NOR */                                 \
  V(vnor, VNOR, 0x10000504)                                \
  /* Vector Shift Right by Octet */                        \
  V(vsro, VSRO, 0x1000044C)                                \
  /* Vector Shift Left by Octet */                         \
  V(vslo, VSLO, 0x1000040C)                                \
  /* Vector Add Unsigned Doubleword Modulo */              \
  V(vaddudm, VADDUDM, 0x100000C0)                          \
  /* Vector Add Unsigned Word Modulo */                    \
  V(vadduwm, VADDUWM, 0x10000080)                          \
  /* Vector Add Unsigned Halfword Modulo */                \
  V(vadduhm, VADDUHM, 0x10000040)                          \
  /* Vector Add Unsigned Byte Modulo */                    \
  V(vaddubm, VADDUBM, 0x10000000)                          \
  /* Vector Add Single-Precision */                        \
  V(vaddfp, VADDFP, 0x1000000A)                            \
  /* Vector Subtract Single-Precision */                   \
  V(vsubfp, VSUBFP, 0x1000004A)                            \
  /* Vector Subtract Unsigned Doubleword Modulo */         \
  V(vsubudm, VSUBUDM, 0x100004C0)                          \
  /* Vector Subtract Unsigned Word Modulo */               \
  V(vsubuwm, VSUBUWM, 0x10000480)                          \
  /* Vector Subtract Unsigned Halfword Modulo */           \
  V(vsubuhm, VSUBUHM, 0x10000440)                          \
  /* Vector Subtract Unsigned Byte Modulo */               \
  V(vsububm, VSUBUBM, 0x10000400)                          \
  /* Vector Multiply Unsigned Word Modulo */               \
  V(vmuluwm, VMULUWM, 0x10000089)                          \
  /* Vector Pack Unsigned Halfword Unsigned Modulo */      \
  V(vpkuhum, VPKUHUM, 0x1000000E)                          \
  /* Vector Multiply Even Signed Byte */                   \
  V(vmulesb, VMULESB, 0x10000308)                          \
  /* Vector Multiply Even Unsigned Byte */                 \
  V(vmuleub, VMULEUB, 0x10000208)                          \
  /* Vector Multiply Odd Signed Byte */                    \
  V(vmulosb, VMULOSB, 0x10000108)                          \
  /* Vector Multiply Odd Unsigned Byte */                  \
  V(vmuloub, VMULOUB, 0x10000008)                          \
  /* Vector Multiply Even Unsigned Halfword */             \
  V(vmuleuh, VMULEUH, 0x10000248)                          \
  /* Vector Multiply Even Signed Halfword */               \
  V(vmulesh, VMULESH, 0x10000348)                          \
  /* Vector Multiply Odd Unsigned Halfword */              \
  V(vmulouh, VMULOUH, 0x10000048)                          \
  /* Vector Multiply Odd Signed Halfword */                \
  V(vmulosh, VMULOSH, 0x10000148)                          \
  /* Vector Multiply Even Signed Word */                   \
  V(vmulesw, VMULESW, 0x10000388)                          \
  /* Vector Multiply Even Unsigned Word */                 \
  V(vmuleuw, VMULEUW, 0x10000288)                          \
  /* Vector Multiply Odd Signed Word */                    \
  V(vmulosw, VMULOSW, 0x10000188)                          \
  /* Vector Multiply Odd Unsigned Word */                  \
  V(vmulouw, VMULOUW, 0x10000088)                          \
  /* Vector Multiply Low Doubleword */                     \
  V(vmulld, VMULLD, 0x100001C9)                            \
  /* Vector Sum across Quarter Signed Halfword Saturate */ \
  V(vsum4shs, VSUM4SHS, 0x10000648)                        \
  /* Vector Pack Unsigned Word Unsigned Saturate */        \
  V(vpkuwus, VPKUWUS, 0x100000CE)                          \
  /* Vector Sum across Half Signed Word Saturate */        \
  V(vsum2sws, VSUM2SWS, 0x10000688)                        \
  /* Vector Pack Unsigned Doubleword Unsigned Modulo */    \
  V(vpkudum, VPKUDUM, 0x1000044E)                          \
  /* Vector Maximum Signed Byte */                         \
  V(vmaxsb, VMAXSB, 0x10000102)                            \
  /* Vector Maximum Unsigned Byte */                       \
  V(vmaxub, VMAXUB, 0x10000002)                            \
  /* Vector Maximum Signed Doubleword */                   \
  V(vmaxsd, VMAXSD, 0x100001C2)                            \
  /* Vector Maximum Unsigned Doubleword */                 \
  V(vmaxud, VMAXUD, 0x100000C2)                            \
  /* Vector Maximum Signed Halfword */                     \
  V(vmaxsh, VMAXSH, 0x10000142)                            \
  /* Vector Maximum Unsigned Halfword */                   \
  V(vmaxuh, VMAXUH, 0x10000042)                            \
  /* Vector Maximum Signed Word */                         \
  V(vmaxsw, VMAXSW, 0x10000182)                            \
  /* Vector Maximum Unsigned Word */                       \
  V(vmaxuw, VMAXUW, 0x10000082)                            \
  /* Vector Minimum Signed Byte */                         \
  V(vminsb, VMINSB, 0x10000302)                            \
  /* Vector Minimum Unsigned Byte */                       \
  V(vminub, VMINUB, 0x10000202)                            \
  /* Vector Minimum Signed Doubleword */                   \
  V(vminsd, VMINSD, 0x100003C2)                            \
  /* Vector Minimum Unsigned Doubleword */                 \
  V(vminud, VMINUD, 0x100002C2)                            \
  /* Vector Minimum Signed Halfword */                     \
  V(vminsh, VMINSH, 0x10000342)                            \
  /* Vector Minimum Unsigned Halfword */                   \
  V(vminuh, VMINUH, 0x10000242)                            \
  /* Vector Minimum Signed Word */                         \
  V(vminsw, VMINSW, 0x10000382)                            \
  /* Vector Minimum Unsigned Word */                       \
  V(vminuw, VMINUW, 0x10000282)                            \
  /* Vector Shift Left Byte */                             \
  V(vslb, VSLB, 0x10000104)                                \
  /* Vector Shift Left Word */                             \
  V(vslw, VSLW, 0x10000184)                                \
  /* Vector Shift Left Halfword */                         \
  V(vslh, VSLH, 0x10000144)                                \
  /* Vector Shift Left Doubleword */                       \
  V(vsld, VSLD, 0x100005C4)                                \
  /* Vector Shift Right Byte */                            \
  V(vsrb, VSRB, 0x10000204)                                \
  /* Vector Shift Right Word */                            \
  V(vsrw, VSRW, 0x10000284)                                \
  /* Vector Shift Right Halfword */                        \
  V(vsrh, VSRH, 0x10000244)                                \
  /* Vector Shift Right Doubleword */                      \
  V(vsrd, VSRD, 0x100006C4)                                \
  /* Vector Shift Right Algebraic Byte */                  \
  V(vsrab, VSRAB, 0x10000304)                              \
  /* Vector Shift Right Algebraic Word */                  \
  V(vsraw, VSRAW, 0x10000384)                              \
  /* Vector Shift Right Algebraic Halfword */              \
  V(vsrah, VSRAH, 0x10000344)                              \
  /* Vector Shift Right Algebraic Doubleword */            \
  V(vsrad, VSRAD, 0x100003C4)                              \
  /* Vector Logical AND */                                 \
  V(vand, VAND, 0x10000404)                                \
  /* Vector Pack Signed Word Signed Saturate */            \
  V(vpkswss, VPKSWSS, 0x100001CE)                          \
  /* Vector Pack Signed Word Unsigned Saturate */          \
  V(vpkswus, VPKSWUS, 0x1000014E)                          \
  /* Vector Pack Signed Halfword Signed Saturate */        \
  V(vpkshss, VPKSHSS, 0x1000018E)                          \
  /* Vector Pack Signed Halfword Unsigned Saturate */      \
  V(vpkshus, VPKSHUS, 0x1000010E)                          \
  /* Vector Add Signed Halfword Saturate */                \
  V(vaddshs, VADDSHS, 0x10000340)                          \
  /* Vector Subtract Signed Halfword Saturate */           \
  V(vsubshs, VSUBSHS, 0x10000740)                          \
  /* Vector Add Unsigned Halfword Saturate */              \
  V(vadduhs, VADDUHS, 0x10000240)                          \
  /* Vector Subtract Unsigned Halfword Saturate */         \
  V(vsubuhs, VSUBUHS, 0x10000640)                          \
  /* Vector Add Signed Byte Saturate */                    \
  V(vaddsbs, VADDSBS, 0x10000300)                          \
  /* Vector Subtract Signed Byte Saturate */               \
  V(vsubsbs, VSUBSBS, 0x10000700)                          \
  /* Vector Add Unsigned Byte Saturate */                  \
  V(vaddubs, VADDUBS, 0x10000200)                          \
  /* Vector Subtract Unsigned Byte Saturate */             \
  V(vsububs, VSUBUBS, 0x10000600)                          \
  /* Vector Average Unsigned Byte */                       \
  V(vavgub, VAVGUB, 0x10000402)                            \
  /* Vector Average Unsigned Halfword */                   \
  V(vavguh, VAVGUH, 0x10000442)                            \
  /* Vector Logical AND with Complement */                 \
  V(vandc, VANDC, 0x10000444)                              \
  /* Vector Minimum Single-Precision */                    \
  V(vminfp, VMINFP, 0x1000044A)                            \
  /* Vector Maximum Single-Precision */                    \
  V(vmaxfp, VMAXFP, 0x1000040A)                            \
  /* Vector Bit Permute Quadword */                        \
  V(vbpermq, VBPERMQ, 0x1000054C)                          \
  /* Vector Merge High Byte */                             \
  V(vmrghb, VMRGHB, 0x1000000C)                            \
  /* Vector Merge High Halfword */                         \
  V(vmrghh, VMRGHH, 0x1000004C)                            \
  /* Vector Merge High Word */                             \
  V(vmrghw, VMRGHW, 0x1000008C)                            \
  /* Vector Merge Low Byte */                              \
  V(vmrglb, VMRGLB, 0x1000010C)                            \
  /* Vector Merge Low Halfword */                          \
  V(vmrglh, VMRGLH, 0x1000014C)                            \
  /* Vector Merge Low Word */                              \
  V(vmrglw, VMRGLW, 0x1000018C)

#define PPC_VX_OPCODE_C_FORM_LIST(V)       \
  /* Vector Unpack Low Signed Word */      \
  V(vupklsw, VUPKLSW, 0x100006CE)          \
  /* Vector Unpack High Signed Word */     \
  V(vupkhsw, VUPKHSW, 0x1000064E)          \
  /* Vector Unpack Low Signed Halfword */  \
  V(vupklsh, VUPKLSH, 0x100002CE)          \
  /* Vector Unpack High Signed Halfword */ \
  V(vupkhsh, VUPKHSH, 0x1000024E)          \
  /* Vector Unpack Low Signed Byte */      \
  V(vupklsb, VUPKLSB, 0x1000028E)          \
  /* Vector Unpack High Signed Byte */     \
  V(vupkhsb, VUPKHSB, 0x1000020E)          \
  /* Vector Population Count Byte */       \
  V(vpopcntb, VPOPCNTB, 0x10000703)

#define PPC_VX_OPCODE_D_FORM_LIST(V) \
  /* Vector Negate Word */           \
  V(vnegw, VNEGW, 0x10060602)        \
  /* Vector Negate Doubleword */     \
  V(vnegd, VNEGD, 0x10070602)

#define PPC_VX_OPCODE_E_FORM_LIST(V)           \
  /* Vector Splat Immediate Signed Byte */     \
  V(vspltisb, VSPLTISB, 0x1000030C)            \
  /* Vector Splat Immediate Signed Halfword */ \
  V(vspltish, VSPLTISH, 0x1000034C)            \
  /* Vector Splat Immediate Signed Word */     \
  V(vspltisw, VSPLTISW, 0x1000038C)

#define PPC_VX_OPCODE_F_FORM_LIST(V)    \
  /* Vector Extract Byte Mask */        \
  V(vextractbm, VEXTRACTBM, 0x10080642) \
  /* Vector Extract Halfword Mask */    \
  V(vextracthm, VEXTRACTHM, 0x10090642) \
  /* Vector Extract Word Mask */        \
  V(vextractwm, VEXTRACTWM, 0x100A0642) \
  /* Vector Extract Doubleword Mask */  \
  V(vextractdm, VEXTRACTDM, 0x100B0642)

#define PPC_VX_OPCODE_G_FORM_LIST(V)         \
  /* Vector Insert Word from GPR using       \
immediate-specified index */                 \
  V(vinsw, VINSW, 0x100000CF)                \
  /* Vector Insert Doubleword from GPR using \
immediate-specified index */                 \
  V(vinsd, VINSD, 0x100001CF)

#define PPC_VX_OPCODE_UNUSED_LIST(V)                                      \
  /* Decimal Add Modulo */                                                \
  V(bcdadd, BCDADD, 0xF0000400)                                           \
  /* Decimal Subtract Modulo */                                           \
  V(bcdsub, BCDSUB, 0xF0000440)                                           \
  /* Move From Vector Status and Control Register */                      \
  V(mfvscr, MFVSCR, 0x10000604)                                           \
  /* Move To Vector Status and Control Register */                        \
  V(mtvscr, MTVSCR, 0x10000644)                                           \
  /* Vector Add & write Carry Unsigned Quadword */                        \
  V(vaddcuq, VADDCUQ, 0x10000140)                                         \
  /* Vector Add and Write Carry-Out Unsigned Word */                      \
  V(vaddcuw, VADDCUW, 0x10000180)                                         \
  /* Vector Add Signed Word Saturate */                                   \
  V(vaddsws, VADDSWS, 0x10000380)                                         \
  /* Vector Add Unsigned Quadword Modulo */                               \
  V(vadduqm, VADDUQM, 0x10000100)                                         \
  /* Vector Add Unsigned Word Saturate */                                 \
  V(vadduws, VADDUWS, 0x10000280)                                         \
  /* Vector Average Signed Byte */                                        \
  V(vavgsb, VAVGSB, 0x10000502)                                           \
  /* Vector Average Signed Halfword */                                    \
  V(vavgsh, VAVGSH, 0x10000542)                                           \
  /* Vector Average Signed Word */                                        \
  V(vavgsw, VAVGSW, 0x10000582)                                           \
  /* Vector Average Unsigned Word */                                      \
  V(vavguw, VAVGUW, 0x10000482)                                           \
  /* Vector Convert From Signed Fixed-Point Word To Single-Precision */   \
  V(vcfsx, VCFSX, 0x1000034A)                                             \
  /* Vector Convert From Unsigned Fixed-Point Word To Single-Precision */ \
  V(vcfux, VCFUX, 0x1000030A)                                             \
  /* Vector Count Leading Zeros Byte */                                   \
  V(vclzb, VCLZB, 0x10000702)                                             \
  /* Vector Count Leading Zeros Doubleword */                             \
  V(vclzd, VCLZD, 0x100007C2)                                             \
  /* Vector Count Leading Zeros Halfword */                               \
  V(vclzh, VCLZH, 0x10000742)                                             \
  /* Vector Count Leading Zeros Word */                                   \
  V(vclzw, VCLZW, 0x10000782)                                             \
  /* Vector Convert From Single-Precision To Signed Fixed-Point Word */   \
  /* Saturate */                                                          \
  V(vctsxs, VCTSXS, 0x100003CA)                                           \
  /* Vector Convert From Single-Precision To Unsigned Fixed-Point Word */ \
  /* Saturate */                                                          \
  V(vctuxs, VCTUXS, 0x1000038A)                                           \
  /* Vector Equivalence */                                                \
  V(veqv, VEQV, 0x10000684)                                               \
  /* Vector 2 Raised to the Exponent Estimate Single-Precision */         \
  V(vexptefp, VEXPTEFP, 0x1000018A)                                       \
  /* Vector Gather Bits by Byte by Doubleword */                          \
  V(vgbbd, VGBBD, 0x1000050C)                                             \
  /* Vector Log Base 2 Estimate Single-Precision */                       \
  V(vlogefp, VLOGEFP, 0x100001CA)                                         \
  /* Vector NAND */                                                       \
  V(vnand, VNAND, 0x10000584)                                             \
  /* Vector OR with Complement */                                         \
  V(vorc, VORC, 0x10000544)                                               \
  /* Vector Pack Pixel */                                                 \
  V(vpkpx, VPKPX, 0x1000030E)                                             \
  /* Vector Pack Signed Doubleword Signed Saturate */                     \
  V(vpksdss, VPKSDSS, 0x100005CE)                                         \
  /* Vector Pack Signed Doubleword Unsigned Saturate */                   \
  V(vpksdus, VPKSDUS, 0x1000054E)                                         \
  /* Vector Pack Unsigned Doubleword Unsigned Saturate */                 \
  V(vpkudus, VPKUDUS, 0x100004CE)                                         \
  /* Vector Pack Unsigned Halfword Unsigned Saturate */                   \
  V(vpkuhus, VPKUHUS, 0x1000008E)                                         \
  /* Vector Pack Unsigned Word Unsigned Modulo */                         \
  V(vpkuwum, VPKUWUM, 0x1000004E)                                         \
  /* Vector Polynomial Multiply-Sum Byte */                               \
  V(vpmsumb, VPMSUMB, 0x10000408)                                         \
  /* Vector Polynomial Multiply-Sum Doubleword */                         \
  V(vpmsumd, VPMSUMD, 0x100004C8)                                         \
  /* Vector Polynomial Multiply-Sum Halfword */                           \
  V(vpmsumh, VPMSUMH, 0x10000448)                                         \
  /* Vector Polynomial Multiply-Sum Word */                               \
  V(vpmsumw, VPMSUMW, 0x10000488)                                         \
  /* Vector Population Count Doubleword */                                \
  V(vpopcntd, VPOPCNTD, 0x100007C3)                                       \
  /* Vector Population Count Halfword */                                  \
  V(vpopcnth, VPOPCNTH, 0x10000743)                                       \
  /* Vector Population Count Word */                                      \
  V(vpopcntw, VPOPCNTW, 0x10000783)                                       \
  /* Vector Reciprocal Estimate Single-Precision */                       \
  V(vrefp, VREFP, 0x1000010A)                                             \
  /* Vector Round to Single-Precision Integer toward -Infinity */         \
  V(vrfim, VRFIM, 0x100002CA)                                             \
  /* Vector Round to Single-Precision Integer Nearest */                  \
  V(vrfin, VRFIN, 0x1000020A)                                             \
  /* Vector Round to Single-Precision Integer toward +Infinity */         \
  V(vrfip, VRFIP, 0x1000028A)                                             \
  /* Vector Round to Single-Precision Integer toward Zero */              \
  V(vrfiz, VRFIZ, 0x1000024A)                                             \
  /* Vector Rotate Left Byte */                                           \
  V(vrlb, VRLB, 0x10000004)                                               \
  /* Vector Rotate Left Doubleword */                                     \
  V(vrld, VRLD, 0x100000C4)                                               \
  /* Vector Rotate Left Halfword */                                       \
  V(vrlh, VRLH, 0x10000044)                                               \
  /* Vector Rotate Left Word */                                           \
  V(vrlw, VRLW, 0x10000084)                                               \
  /* Vector Reciprocal Square Root Estimate Single-Precision */           \
  V(vrsqrtefp, VRSQRTEFP, 0x1000014A)                                     \
  /* Vector Shift Left */                                                 \
  V(vsl, VSL, 0x100001C4)                                                 \
  /* Vector Shift Right */                                                \
  V(vsr, VSR, 0x100002C4)                                                 \
  /* Vector Subtract & write Carry Unsigned Quadword */                   \
  V(vsubcuq, VSUBCUQ, 0x10000540)                                         \
  /* Vector Subtract and Write Carry-Out Unsigned Word */                 \
  V(vsubcuw, VSUBCUW, 0x10000580)                                         \
  /* Vector Subtract Signed Word Saturate */                              \
  V(vsubsws, VSUBSWS, 0x10000780)                                         \
  /* Vector Subtract Unsigned Quadword Modulo */                          \
  V(vsubuqm, VSUBUQM, 0x10000500)                                         \
  /* Vector Subtract Unsigned Word Saturate */                            \
  V(vsubuws, VSUBUWS, 0x10000680)                                         \
  /* Vector Sum across Quarter Signed Byte Saturate */                    \
  V(vsum4sbs, VSUM4SBS, 0x10000708)                                       \
  /* Vector Sum across Quarter Unsigned Byte Saturate */                  \
  V(vsum4bus, VSUM4BUS, 0x10000608)                                       \
  /* Vector Sum across Signed Word Saturate */                            \
  V(vsumsws, VSUMSWS, 0x10000788)                                         \
  /* Vector Unpack High Pixel */                                          \
  V(vupkhpx, VUPKHPX, 0x1000034E)                                         \
  /* Vector Unpack Low Pixel */                                           \
  V(vupklpx, VUPKLPX, 0x100003CE)                                         \
  /* Vector AES Cipher */                                                 \
  V(vcipher, VCIPHER, 0x10000508)                                         \
  /* Vector AES Cipher Last */                                            \
  V(vcipherlast, VCIPHERLAST, 0x10000509)                                 \
  /* Vector AES Inverse Cipher */                                         \
  V(vncipher, VNCIPHER, 0x10000548)                                       \
  /* Vector AES Inverse Cipher Last */                                    \
  V(vncipherlast, VNCIPHERLAST, 0x10000549)                               \
  /* Vector AES S-Box */                                                  \
  V(vsbox, VSBOX, 0x100005C8)                                             \
  /* Vector SHA-512 Sigma Doubleword */                                   \
  V(vshasigmad, VSHASIGMAD, 0x100006C2)                                   \
  /* Vector SHA-256 Sigma Word */                                         \
  V(vshasigmaw, VSHASIGMAW, 0x10000682)                                   \
  /* Vector Merge Even Word */                                            \
  V(vmrgew, VMRGEW, 0x1000078C)                                           \
  /* Vector Merge Odd Word */                                             \
  V(vmrgow, VMRGOW, 0x1000068C)

#define PPC_VX_OPCODE_LIST(V)  \
  PPC_VX_OPCODE_A_FORM_LIST(V) \
  PPC_VX_OPCODE_B_FORM_LIST(V) \
  PPC_VX_OPCODE_C_FORM_LIST(V) \
  PPC_VX_OPCODE_D_FORM_LIST(V) \
  PPC_VX_OPCODE_E_FORM_LIST(V) \
  PPC_VX_OPCODE_F_FORM_LIST(V) \
  PPC_VX_OPCODE_G_FORM_LIST(V) \
  PPC_VX_OPCODE_UNUSED_LIST(V)

#define PPC_XS_OPCODE_LIST(V)                      \
  /* Shift Right Algebraic Doubleword Immediate */ \
  V(sradi, SRADIX, 0x7C000674)

#define PPC_MD_OPCODE_LIST(V)                             \
  /* Rotate Left Doubleword Immediate then Clear */       \
  V(rldic, RLDIC, 0x78000008)                             \
  /* Rotate Left Doubleword Immediate then Clear Left */  \
  V(rldicl, RLDICL, 0x78000000)                           \
  /* Rotate Left Doubleword Immediate then Clear Right */ \
  V(rldicr, RLDICR, 0x78000004)                           \
  /* Rotate Left Doubleword Immediate then Mask Insert */ \
  V(rldimi, RLDIMI, 0x7800000C)

#define PPC_SC_OPCODE_LIST(V) \
  /* System Call */           \
  V(sc, SC, 0x44000002)

#define PPC_PREFIX_OPCODE_TYPE_00_LIST(V)        \
  V(pload_store_8ls, PLOAD_STORE_8LS, 0x4000000) \
  V(pplwa, PPLWA, 0xA4000000)                    \
  V(ppld, PPLD, 0xE4000000)                      \
  V(ppstd, PPSTD, 0xF4000000)

#define PPC_PREFIX_OPCODE_TYPE_10_LIST(V) \
  V(pload_store_mls, PLOAD_STORE_MLS, 0x6000000)

#define PPC_OPCODE_LIST(V)          \
  PPC_X_OPCODE_LIST(V)              \
  PPC_X_OPCODE_EH_S_FORM_LIST(V)    \
  PPC_XO_OPCODE_LIST(V)             \
  PPC_DS_OPCODE_LIST(V)             \
  PPC_DQ_OPCODE_LIST(V)             \
  PPC_MDS_OPCODE_LIST(V)            \
  PPC_MD_OPCODE_LIST(V)             \
  PPC_XS_OPCODE_LIST(V)             \
  PPC_D_OPCODE_LIST(V)              \
  PPC_I_OPCODE_LIST(V)              \
  PPC_B_OPCODE_LIST(V)              \
  PPC_XL_OPCODE_LIST(V)             \
  PPC_A_OPCODE_LIST(V)              \
  PPC_XFX_OPCODE_LIST(V)            \
  PPC_M_OPCODE_LIST(V)              \
  PPC_SC_OPCODE_LIST(V)             \
  PPC_Z23_OPCODE_LIST(V)            \
  PPC_Z22_OPCODE_LIST(V)            \
  PPC_EVX_OPCODE_LIST(V)            \
  PPC_XFL_OPCODE_LIST(V)            \
  PPC_EVS_OPCODE_LIST(V)            \
  PPC_VX_OPCODE_LIST(V)             \
  PPC_VA_OPCODE_LIST(V)             \
  PPC_VC_OPCODE_LIST(V)             \
  PPC_XX1_OPCODE_LIST(V)            \
  PPC_XX2_OPCODE_LIST(V)            \
  PPC_XX3_OPCODE_VECTOR_LIST(V)     \
  PPC_XX3_OPCODE_SCALAR_LIST(V)     \
  PPC_XX4_OPCODE_LIST(V)            \
  PPC_PREFIX_OPCODE_TYPE_00_LIST(V) \
  PPC_PREFIX_OPCODE_TYPE_10_LIST(V)

enum Opcode : uint32_t {
#define DECLARE_INSTRUCTION(name, opcode_name, opcode_value) \
  opcode_name = opcode_value,
  PPC_OPCODE_LIST(DECLARE_INSTRUCTION)
#undef DECLARE_INSTRUCTION
      EXTP = 0x4000000,  // Extended code set prefixed
  EXT0 = 0x10000000,     // Extended code set 0
  EXT1 = 0x4C000000,     // Extended code set 1
  EXT2 = 0x7C000000,     // Extended code set 2
  EXT3 = 0xEC000000,     // Extended code set 3
  EXT4 = 0xFC000000,     // Extended code set 4
  EXT5 = 0x78000000,     // Extended code set 5 - 64bit only
  EXT6 = 0xF0000000,     // Extended code set 6
};

// Instruction encoding bits and masks.
enum {
  // Instruction encoding bit
  B1 = 1 << 1,
  B2 = 1 << 2,
  B3 = 1 << 3,
  B4 = 1 << 4,
  B5 = 1 << 5,
  B7 = 1 << 7,
  B8 = 1 << 8,
  B9 = 1 << 9,
  B12 = 1 << 12,
  B18 = 1 << 18,
  B19 = 1 << 19,
  B20 = 1 << 20,
  B22 = 1 << 22,
  B23 = 1 << 23,
  B24 = 1 << 24,
  B25 = 1 << 25,
  B26 = 1 << 26,
  B27 = 1 << 27,
  B28 = 1 << 28,
  B6 = 1 << 6,
  B10 = 1 << 10,
  B11 = 1 << 11,
  B16 = 1 << 16,
  B17 = 1 << 17,
  B21 = 1 << 21,

  // Instruction bit masks
  kCondMask = 0x1F << 21,
  kOff12Mask = (1 << 12) - 1,
  kImm24Mask = (1 << 24) - 1,
  kOff16Mask = (1 << 16) - 1,
  kImm16Mask = (1 << 16) - 1,
  kImm18Mask = (1 << 18) - 1,
  kImm22Mask = (1 << 22) - 1,
  kImm26Mask = (1 << 26) - 1,
  kBOfieldMask = 0x1f << 21,
  kOpcodeMask = 0x3f << 26,
  kExt1OpcodeMask = 0x3ff << 1,
  kExt2OpcodeMask = 0x3ff << 1,
  kExt2OpcodeVariant2Mask = 0x1ff << 2,
  kExt5OpcodeMask = 0x3 << 2,
  kBOMask = 0x1f << 21,
  kBIMask = 0x1F << 16,
  kBDMask = 0x14 << 2,
  kAAMask = 0x01 << 1,
  kLKMask = 0x01,
  kRCMask = 0x01,
  kTOMask = 0x1f << 21
};

// -----------------------------------------------------------------------------
// Addressing modes and instruction variants.

// Overflow Exception
enum OEBit {
  SetOE = 1 << 10,   // Set overflow exception
  LeaveOE = 0 << 10  // No overflow exception
};

// Record bit
enum RCBit {   // Bit 0
  SetRC = 1,   // LT,GT,EQ,SO
  LeaveRC = 0  // None
};
// Exclusive Access hint bit
enum EHBit {   // Bit 0
  SetEH = 1,   // Exclusive Access
  LeaveEH = 0  // Atomic Update
};

// Link bit
enum LKBit {   // Bit 0
  SetLK = 1,   // Load effective address of next instruction
  LeaveLK = 0  // No action
};

// Prefixed R bit.
enum PRBit { SetPR = 1, LeavePR = 0 };

enum BOfield {        // Bits 25-21
  DCBNZF = 0 << 21,   // Decrement CTR; branch if CTR != 0 and condition false
  DCBEZF = 2 << 21,   // Decrement CTR; branch if CTR == 0 and condition false
  BF = 4 << 21,       // Branch if condition false
  DCBNZT = 8 << 21,   // Decrement CTR; branch if CTR != 0 and condition true
  DCBEZT = 10 << 21,  // Decrement CTR; branch if CTR == 0 and condition true
  BT = 12 << 21,      // Branch if condition true
  DCBNZ = 16 << 21,   // Decrement CTR; branch if CTR != 0
  DCBEZ = 18 << 21,   // Decrement CTR; branch if CTR == 0
  BA = 20 << 21       // Branch always
};

#if V8_OS_AIX
#undef CR_LT
#undef CR_GT
#undef CR_EQ
#undef CR_SO
#endif

enum CRBit { CR_LT = 0, CR_GT = 1, CR_EQ = 2, CR_SO = 3, CR_FU = 3 };

#define CRWIDTH 4

// These are the documented bit positions biased down by 32
enum FPSCRBit {
  VXSOFT = 21,  // 53: Software-Defined Condition
  VXSQRT = 22,  // 54: Invalid Square Root
  VXCVI = 23    // 55: Invalid Integer Convert
};

// -----------------------------------------------------------------------------
// Supervisor Call (svc) specific support.

// Special Software Interrupt codes when used in the presence of the PPC
// simulator.
// svc (formerly swi) provides a 24bit immediate value. Use bits 22:0 for
// standard SoftwareInterrupCode. Bit 23 is reserved for the stop feature.
enum SoftwareInterruptCodes {
  // transition to C code
  kCallRtRedirected = 0x10,
  // break point
  kBreakpoint = 0x821008,  // bits23-0 of 0x7d821008 = twge r2, r2
  // stop
  kStopCode = 1 << 23
};
const uint32_t kStopCodeMask = kStopCode - 1;
const uint32_t kMaxStopCode = kStopCode - 1;
const int32_t kDefaultStopCode = -1;

// FP rounding modes.
enum FPRoundingMode {
  RN = 0,  // Round to Nearest.
  RZ = 1,  // Round towards zero.
  RP = 2,  // Round towards Plus Infinity.
  RM = 3,  // Round towards Minus Infinity.

  // Aliases.
  kRoundToNearest = RN,
  kRoundToZero = RZ,
  kRoundToPlusInf = RP,
  kRoundToMinusInf = RM
};

const uint32_t kFPRoundingModeMask = 3;

enum CheckForInexactConversion {
  kCheckForInexactConversion,
  kDontCheckForInexactConversion
};

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.
// These constants are declared in assembler-arm.cc, as they use named registers
// and other constants.

// add(sp, sp, 4) instruction (aka Pop())
extern const Instr kPopInstruction;

// str(r, MemOperand(sp, 4, NegPreIndex), al) instruction (aka push(r))
// register r is not encoded.
extern const Instr kPushRegPattern;

// ldr(r, MemOperand(sp, 4, PostIndex), al) instruction (aka pop(r))
// register r is not encoded.
extern const Instr kPopRegPattern;

// use TWI to indicate redirection call for simulation mode
const Instr rtCallRedirInstr = TWI;

// -----------------------------------------------------------------------------
// Instruction abstraction.

// The class Instruction enables access to individual fields defined in the PPC
// architecture instruction set encoding.
// Note that the Assembler uses typedef int32_t Instr.
//
// Example: Test whether the instruction at ptr does set the condition code
// bits.
//
// bool InstructionSetsConditionCodes(uint8_t* ptr) {
//   Instruction* instr = Instruction::At(ptr);
//   int type = instr->TypeValue();
//   return ((type == 0) || (type == 1)) && instr->HasS();
// }
//

constexpr uint8_t kInstrSize = 4;
constexpr uint8_t kInstrSizeLog2 = 2;
constexpr uint8_t kPcLoadDelta = 8;

class Instruction {
 public:
// Helper macro to define static accessors.
// We use the cast to char* trick to bypass the strict anti-aliasing rules.
#define DECLARE_STATIC_TYPED_ACCESSOR(return_type, Name) \
  static inline return_type Name(Instr instr) {          \
    char* temp = reinterpret_cast<char*>(&instr);        \
    return reinterpret_cast<Instruction*>(temp)->Name(); \
  }

#define DECLARE_STATIC_ACCESSOR(Name) DECLARE_STATIC_TYPED_ACCESSOR(int, Name)

  // Get the raw instruction bits.
  inline Instr InstructionBits() const {
    return *reinterpret_cast<const Instr*>(this);
  }

  // Set the raw instruction bits to value.
  V8_EXPORT_PRIVATE void SetInstructionBits(
      Instr value, WritableJitAllocation* jit_allocation = nullptr);

  // Read one particular bit out of the instruction bits.
  inline int Bit(int nr) const { return (InstructionBits() >> nr) & 1; }

  // Read a bit field's value out of the instruction bits.
  inline int Bits(int hi, int lo) const {
    return (InstructionBits() >> lo) & ((2 << (hi - lo)) - 1);
  }

  // Read a bit field out of the instruction bits.
  inline uint32_t BitField(int hi, int lo) const {
    return InstructionBits() & (((2 << (hi - lo)) - 1) << lo);
  }

  // Static support.

  // Read one particular bit out of the instruction bits.
  static inline int Bit(Instr instr, int nr) { return (instr >> nr) & 1; }

  // Read the value of a bit field out of the instruction bits.
  static inline int Bits(Instr instr, int hi, int lo) {
    return (instr >> lo) & ((2 << (hi - lo)) - 1);
  }

  // Read a bit field out of the instruction bits.
  static inline uint32_t BitField(Instr instr, int hi, int lo) {
    return instr & (((2 << (hi - lo)) - 1) << lo);
  }

  inline int RSValue() const { return Bits(25, 21); }
  inline int RTValue() const { return Bits(25, 21); }
  inline int RAValue() const { return Bits(20, 16); }
  DECLARE_STATIC_ACCESSOR(RAValue)
  inline int RBValue() const { return Bits(15, 11); }
  DECLARE_STATIC_ACCESSOR(RBValue)
  inline int RCValue() const { return Bits(10, 6); }
  DECLARE_STATIC_ACCESSOR(RCValue)

  inline int OpcodeValue() const { return static_cast<Opcode>(Bits(31, 26)); }
  inline uint32_t OpcodeField() const {
    return static_cast<Opcode>(BitField(31, 26));
  }
  inline uint32_t PrefixOpcodeField() const {
    return static_cast<Opcode>(BitField(31, 25));
  }

#define OPCODE_CASES(name, opcode_name, opcode_value) case opcode_name:

  inline Opcode OpcodeBase() const {
    uint32_t opcode = PrefixOpcodeField();
    uint32_t extcode = PrefixOpcodeField();
    // Check for prefix.
    switch (opcode) {
      PPC_PREFIX_OPCODE_TYPE_00_LIST(OPCODE_CASES)
      PPC_PREFIX_OPCODE_TYPE_10_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = OpcodeField();
    extcode = OpcodeField();
    // Check for suffix.
    switch (opcode) {
      PPC_PREFIX_OPCODE_TYPE_00_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    switch (opcode) {
      PPC_D_OPCODE_LIST(OPCODE_CASES)
      PPC_I_OPCODE_LIST(OPCODE_CASES)
      PPC_B_OPCODE_LIST(OPCODE_CASES)
      PPC_M_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(5, 0);
    switch (opcode) {
      PPC_VA_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    // Some VX opcodes have integers hard coded in the middle, handle those
    // first.
    opcode = extcode | BitField(20, 16) | BitField(10, 0);
    switch (opcode) {
      PPC_VX_OPCODE_D_FORM_LIST(OPCODE_CASES)
      PPC_VX_OPCODE_F_FORM_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(10, 0);
    switch (opcode) {
      PPC_VX_OPCODE_A_FORM_LIST(OPCODE_CASES)
      PPC_VX_OPCODE_B_FORM_LIST(OPCODE_CASES)
      PPC_VX_OPCODE_C_FORM_LIST(OPCODE_CASES)
      PPC_VX_OPCODE_E_FORM_LIST(OPCODE_CASES)
      PPC_VX_OPCODE_G_FORM_LIST(OPCODE_CASES)
      PPC_VX_OPCODE_UNUSED_LIST(OPCODE_CASES)
      PPC_X_OPCODE_EH_S_FORM_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(9, 0);
    switch (opcode) {
      PPC_VC_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(10, 1) | BitField(20, 20);
    switch (opcode) {
      PPC_XFX_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    // Some XX2 opcodes have integers hard coded in the middle, handle those
    // first.
    opcode = extcode | BitField(20, 16) | BitField(10, 2);
    switch (opcode) {
      PPC_XX2_OPCODE_B_FORM_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(10, 2);
    switch (opcode) {
      PPC_XX2_OPCODE_VECTOR_A_FORM_LIST(OPCODE_CASES)
      PPC_XX2_OPCODE_SCALAR_A_FORM_LIST(OPCODE_CASES)
      PPC_XX2_OPCODE_UNUSED_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(10, 1);
    switch (opcode) {
      PPC_X_OPCODE_LIST(OPCODE_CASES)
      PPC_XL_OPCODE_LIST(OPCODE_CASES)
      PPC_XFL_OPCODE_LIST(OPCODE_CASES)
      PPC_XX1_OPCODE_LIST(OPCODE_CASES)
      PPC_EVX_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(9, 1);
    switch (opcode) {
      PPC_XO_OPCODE_LIST(OPCODE_CASES)
      PPC_Z22_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(10, 2);
    switch (opcode) {
      PPC_XS_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(9, 3);
    switch (opcode) {
      PPC_XX3_OPCODE_VECTOR_A_FORM_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(10, 3);
    switch (opcode) {
      PPC_EVS_OPCODE_LIST(OPCODE_CASES)
      PPC_XX3_OPCODE_VECTOR_B_FORM_LIST(OPCODE_CASES)
      PPC_XX3_OPCODE_SCALAR_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(8, 1);
    switch (opcode) {
      PPC_Z23_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(5, 1);
    switch (opcode) {
      PPC_A_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(4, 1);
    switch (opcode) {
      PPC_MDS_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(4, 2);
    switch (opcode) {
      PPC_MD_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(5, 4);
    switch (opcode) {
      PPC_XX4_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(2, 0);
    switch (opcode) {
      PPC_DQ_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(1, 0);
    switch (opcode) {
      PPC_DS_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    opcode = extcode | BitField(1, 1);
    switch (opcode) {
      PPC_SC_OPCODE_LIST(OPCODE_CASES)
      return static_cast<Opcode>(opcode);
    }
    UNIMPLEMENTED();
    return static_cast<Opcode>(0);
  }

#undef OPCODE_CASES

  // Fields used in Software interrupt instructions
  inline SoftwareInterruptCodes SvcValue() const {
    return static_cast<SoftwareInterruptCodes>(Bits(23, 0));
  }

  // Instructions are read of out a code stream. The only way to get a
  // reference to an instruction is to convert a pointer. There is no way
  // to allocate or create instances of class Instruction.
  // Use the At(pc) function to create references to Instruction.
  static Instruction* At(uint8_t* pc) {
    return reinterpret_cast<Instruction*>(pc);
  }

 private:
  // We need to prevent the creation of instances of class Instruction.
  DISALLOW_IMPLICIT_CONSTRUCTORS(Instruction);
};

// Helper functions for converting between register numbers and names.
class Registers {
 public:
  // Lookup the register number for the name provided.
  static int Number(const char* name);

 private:
  static const char* names_[kNumRegisters];
};

// Helper functions for converting between FP register numbers and names.
class DoubleRegisters {
 public:
  // Lookup the register number for the name provided.
  static int Number(const char* name);

 private:
  static const char* names_[kNumDoubleRegisters];
};
}  // namespace internal
}  // namespace v8

static constexpr int kR0DwarfCode = 0;
static constexpr int kFpDwarfCode = 31;  // frame-pointer
static constexpr int kLrDwarfCode = 65;  // return-address(lr)
static constexpr int kSpDwarfCode = 1;   // stack-pointer (sp)

#endif  // V8_CODEGEN_PPC_CONSTANTS_PPC_H_
