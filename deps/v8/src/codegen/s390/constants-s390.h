// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_S390_CONSTANTS_S390_H_
#define V8_CODEGEN_S390_CONSTANTS_S390_H_

// Get the standard printf format macros for C99 stdint types.
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

#include <stdint.h>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/common/globals.h"

// UNIMPLEMENTED_ macro for S390.
#ifdef DEBUG
#define UNIMPLEMENTED_S390()                                               \
  v8::internal::PrintF("%s, \tline %d: \tfunction %s not implemented. \n", \
                       __FILE__, __LINE__, __func__)
#else
#define UNIMPLEMENTED_S390()
#endif

#if V8_OS_ZOS
#define ABI_USES_FUNCTION_DESCRIPTORS 1
#define ABI_PASSES_HANDLES_IN_REGS 1
#define ABI_RETURNS_OBJECTPAIR_IN_REGS 1
#ifdef _EXT
// Defined in stdlib.h and conflict with those in S390_RS_A_OPCODE_LIST below:
#undef cs
#undef cds
#endif
#else
#define ABI_USES_FUNCTION_DESCRIPTORS 0
#define ABI_PASSES_HANDLES_IN_REGS 1

// ObjectPair is defined under runtime/runtime-util.h.
// On 31-bit, ObjectPair == uint64_t.  ABI dictates long long
//            be returned with the lower addressed half in r2
//            and the higher addressed half in r3. (Returns in Regs)
// On 64-bit, ObjectPair is a Struct.  ABI dictaes Structs be
//            returned in a storage buffer allocated by the caller,
//            with the address of this buffer passed as a hidden
//            argument in r2. (Does NOT return in Regs)
// For x86 linux, ObjectPair is returned in registers.
#if V8_TARGET_ARCH_S390X
#define ABI_RETURNS_OBJECTPAIR_IN_REGS 0
#else
#define ABI_RETURNS_OBJECTPAIR_IN_REGS 1
#endif
#endif

#define ABI_CALL_VIA_IP 1

namespace v8 {
namespace internal {

// The maximum size of the code range s.t. pc-relative calls are possible
// between all Code objects in the range.
constexpr size_t kMaxPCRelativeCodeRangeInMB = 4096;

#if V8_OS_ZOS
// Used to encode a boolean value when emitting 32 bit
// opcodes which will indicate the presence of function descriptors
constexpr int kHasFunctionDescriptorBitShift = 4;
constexpr int kHasFunctionDescriptorBitMask = 1
                                              << kHasFunctionDescriptorBitShift;
#endif

// Number of registers
const int kNumRegisters = 16;

// FP support.
const int kNumDoubleRegisters = 16;

const int kNoRegister = -1;

// The actual value of the kRootRegister is offset from the IsolateData's start
// to take advantage of negative displacement values.
constexpr int kRootRegisterBias = 128;

// sign-extend the least significant 16-bits of value <imm>
#define SIGN_EXT_IMM16(imm) ((static_cast<int>(imm) << 16) >> 16)

// sign-extend the least significant 26-bits of value <imm>
#define SIGN_EXT_IMM26(imm) ((static_cast<int>(imm) << 6) >> 6)

// -----------------------------------------------------------------------------
// Conditions.

// Defines constants and accessor classes to assemble, disassemble and
// simulate z/Architecture instructions.
//
// Section references in the code refer to the "z/Architecture Principles
// Of Operation" http://publibfi.boulder.ibm.com/epubs/pdf/dz9zr009.pdf
//

// Constants for specific fields are defined in their respective named enums.
// General constants are in an anonymous enum in class Instr.
enum Condition : int {
  kNoCondition = -1,
  eq = 0x8,  // Equal.
  ne = 0x7,  // Not equal.
  ge = 0xa,  // Greater or equal.
  lt = 0x4,  // Less than.
  gt = 0x2,  // Greater than.
  le = 0xc,  // Less then or equal
  al = 0xf,  // Always.

  CC_NOP = 0x0,           // S390 NOP
  CC_EQ = 0x08,           // S390 condition code 0b1000
  CC_LT = 0x04,           // S390 condition code 0b0100
  CC_LE = CC_EQ | CC_LT,  // S390 condition code 0b1100
  CC_GT = 0x02,           // S390 condition code 0b0010
  CC_GE = CC_EQ | CC_GT,  // S390 condition code 0b1010
  CC_OF = 0x01,           // S390 condition code 0b0001
  CC_NOF = 0x0E,          // S390 condition code 0b1110
  CC_ALWAYS = 0x0F,       // S390 always taken branch
  unordered = CC_OF,      // Floating-point unordered
  ordered = CC_NOF,       // floating-point ordered
  overflow = CC_OF,       // Summary overflow
  nooverflow = CC_NOF,

  mask0x0 = 0,  // no jumps
  mask0x1 = 1,
  mask0x2 = 2,
  mask0x3 = 3,
  mask0x4 = 4,
  mask0x5 = 5,
  mask0x6 = 6,
  mask0x7 = 7,
  mask0x8 = 8,
  mask0x9 = 9,
  mask0xA = 10,
  mask0xB = 11,
  mask0xC = 12,
  mask0xD = 13,
  mask0xE = 14,
  mask0xF = 15,

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
  kUnsignedLessThan = 16,
  kUnsignedGreaterThan = 17,
  kUnsignedLessThanEqual = 18,
  kUnsignedGreaterThanEqual = 19,
  kOverflow = overflow,
  kNoOverflow = nooverflow,
  kZero = 20,
  kNotZero = 21,
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
  switch (cond) {
    case eq:
      return ne;
    case ne:
      return eq;
    case ge:
      return lt;
    case gt:
      return le;
    case le:
      return gt;
    case lt:
      return ge;
    case lt | gt:
      return eq;
    case le | ge:
      return CC_OF;
    case CC_OF:
      return CC_NOF;
    case kUnsignedLessThan:
      return kUnsignedGreaterThanEqual;
    case kUnsignedGreaterThan:
      return kUnsignedLessThanEqual;
    case kUnsignedLessThanEqual:
      return kUnsignedGreaterThan;
    case kUnsignedGreaterThanEqual:
      return kUnsignedLessThan;
    default:
      DCHECK(false);
  }
  return al;
}

// -----------------------------------------------------------------------------
// Instructions encoding.

// Instr is merely used by the Assembler to distinguish 32bit integers
// representing instructions from usual 32 bit values.
// Instruction objects are pointers to 32bit values, and provide methods to
// access the various ISA fields.
using Instr = int32_t;
using TwoByteInstr = uint16_t;
using FourByteInstr = uint32_t;
using SixByteInstr = uint64_t;

#define S390_RSY_A_OPCODE_LIST(V)                                              \
  V(lmg, LMG, 0xEB04)     /* type = RSY_A LOAD MULTIPLE (64)  */               \
  V(srag, SRAG, 0xEB0A)   /* type = RSY_A SHIFT RIGHT SINGLE (64)  */          \
  V(slag, SLAG, 0xEB0B)   /* type = RSY_A SHIFT LEFT SINGLE (64)  */           \
  V(srlg, SRLG, 0xEB0C)   /* type = RSY_A SHIFT RIGHT SINGLE LOGICAL (64)  */  \
  V(sllg, SLLG, 0xEB0D)   /* type = RSY_A SHIFT LEFT SINGLE LOGICAL (64)  */   \
  V(tracg, TRACG, 0xEB0F) /* type = RSY_A TRACE (64)  */                       \
  V(csy, CSY, 0xEB14)     /* type = RSY_A COMPARE AND SWAP (32)  */            \
  V(rllg, RLLG, 0xEB1C)   /* type = RSY_A ROTATE LEFT SINGLE LOGICAL (64)  */  \
  V(rll, RLL, 0xEB1D)     /* type = RSY_A ROTATE LEFT SINGLE LOGICAL (32)  */  \
  V(stmg, STMG, 0xEB24)   /* type = RSY_A STORE MULTIPLE (64)  */              \
  V(stctg, STCTG, 0xEB25) /* type = RSY_A STORE CONTROL (64)  */               \
  V(stmh, STMH, 0xEB26)   /* type = RSY_A STORE MULTIPLE HIGH (32)  */         \
  V(lctlg, LCTLG, 0xEB2F) /* type = RSY_A LOAD CONTROL (64)  */                \
  V(csg, CSG, 0xEB30)     /* type = RSY_A COMPARE AND SWAP (64)  */            \
  V(cdsy, CDSY, 0xEB31)   /* type = RSY_A COMPARE DOUBLE AND SWAP (32)  */     \
  V(cdsg, CDSG, 0xEB3E)   /* type = RSY_A COMPARE DOUBLE AND SWAP (64)  */     \
  V(bxhg, BXHG, 0xEB44)   /* type = RSY_A BRANCH ON INDEX HIGH (64)  */        \
  V(bxleg, BXLEG, 0xEB45) /* type = RSY_A BRANCH ON INDEX LOW OR EQUAL (64) */ \
  V(ecag, ECAG, 0xEB4C)   /* type = RSY_A EXTRACT CPU ATTRIBUTE  */            \
  V(mvclu, MVCLU, 0xEB8E) /* type = RSY_A MOVE LONG UNICODE  */                \
  V(clclu, CLCLU, 0xEB8F) /* type = RSY_A COMPARE LOGICAL LONG UNICODE  */     \
  V(stmy, STMY, 0xEB90)   /* type = RSY_A STORE MULTIPLE (32)  */              \
  V(lmh, LMH, 0xEB96)     /* type = RSY_A LOAD MULTIPLE HIGH (32)  */          \
  V(lmy, LMY, 0xEB98)     /* type = RSY_A LOAD MULTIPLE (32)  */               \
  V(lamy, LAMY, 0xEB9A)   /* type = RSY_A LOAD ACCESS MULTIPLE  */             \
  V(stamy, STAMY, 0xEB9B) /* type = RSY_A STORE ACCESS MULTIPLE  */            \
  V(srak, SRAK, 0xEBDC)   /* type = RSY_A SHIFT RIGHT SINGLE (32)  */          \
  V(slak, SLAK, 0xEBDD)   /* type = RSY_A SHIFT LEFT SINGLE (32)  */           \
  V(srlk, SRLK, 0xEBDE)   /* type = RSY_A SHIFT RIGHT SINGLE LOGICAL (32)  */  \
  V(sllk, SLLK, 0xEBDF)   /* type = RSY_A SHIFT LEFT SINGLE LOGICAL (32)  */   \
  V(lang, LANG, 0xEBE4)   /* type = RSY_A LOAD AND AND (64)  */                \
  V(laog, LAOG, 0xEBE6)   /* type = RSY_A LOAD AND OR (64)  */                 \
  V(laxg, LAXG, 0xEBE7)   /* type = RSY_A LOAD AND EXCLUSIVE OR (64)  */       \
  V(laag, LAAG, 0xEBE8)   /* type = RSY_A LOAD AND ADD (64)  */                \
  V(laalg, LAALG, 0xEBEA) /* type = RSY_A LOAD AND ADD LOGICAL (64)  */        \
  V(lan, LAN, 0xEBF4)     /* type = RSY_A LOAD AND AND (32)  */                \
  V(lao, LAO, 0xEBF6)     /* type = RSY_A LOAD AND OR (32)  */                 \
  V(lax, LAX, 0xEBF7)     /* type = RSY_A LOAD AND EXCLUSIVE OR (32)  */       \
  V(laa, LAA, 0xEBF8)     /* type = RSY_A LOAD AND ADD (32)  */                \
  V(laal, LAAL, 0xEBFA)   /* type = RSY_A LOAD AND ADD LOGICAL (32)  */

#define S390_RSY_B_OPCODE_LIST(V)                                              \
  V(clmh, CLMH,                                                                \
    0xEB20) /* type = RSY_B COMPARE LOGICAL CHAR. UNDER MASK (high)  */        \
  V(clmy, CLMY,                                                                \
    0xEB21) /* type = RSY_B COMPARE LOGICAL CHAR. UNDER MASK (low)  */         \
  V(clt, CLT, 0xEB23)   /* type = RSY_B COMPARE LOGICAL AND TRAP (32)  */      \
  V(clgt, CLGT, 0xEB2B) /* type = RSY_B COMPARE LOGICAL AND TRAP (64)  */      \
  V(stcmh, STCMH,                                                              \
    0xEB2C) /* type = RSY_B STORE CHARACTERS UNDER MASK (high)  */             \
  V(stcmy, STCMY, 0xEB2D) /* type = RSY_B STORE CHARACTERS UNDER MASK (low) */ \
  V(icmh, ICMH, 0xEB80) /* type = RSY_B INSERT CHARACTERS UNDER MASK (high) */ \
  V(icmy, ICMY, 0xEB81) /* type = RSY_B INSERT CHARACTERS UNDER MASK (low)  */ \
  V(locfh, LOCFH, 0xEBE0)   /* type = RSY_B LOAD HIGH ON CONDITION (32)  */    \
  V(stocfh, STOCFH, 0xEBE1) /* type = RSY_B STORE HIGH ON CONDITION  */        \
  V(locg, LOCG, 0xEBE2)     /* type = RSY_B LOAD ON CONDITION (64)  */         \
  V(stocg, STOCG, 0xEBE3)   /* type = RSY_B STORE ON CONDITION (64)  */        \
  V(loc, LOC, 0xEBF2)       /* type = RSY_B LOAD ON CONDITION (32)  */         \
  V(stoc, STOC, 0xEBF3)     /* type = RSY_B STORE ON CONDITION (32)  */

#define S390_RXE_OPCODE_LIST(V)                                                \
  V(lcbb, LCBB, 0xE727) /* type = RXE   LOAD COUNT TO BLOCK BOUNDARY  */       \
  V(ldeb, LDEB, 0xED04) /* type = RXE   LOAD LENGTHENED (short to long BFP) */ \
  V(lxdb, LXDB,                                                                \
    0xED05) /* type = RXE   LOAD LENGTHENED (long to extended BFP)  */         \
  V(lxeb, LXEB,                                                                \
    0xED06) /* type = RXE   LOAD LENGTHENED (short to extended BFP)  */        \
  V(mxdb, MXDB, 0xED07) /* type = RXE   MULTIPLY (long to extended BFP)  */    \
  V(keb, KEB, 0xED08)   /* type = RXE   COMPARE AND SIGNAL (short BFP)  */     \
  V(ceb, CEB, 0xED09)   /* type = RXE   COMPARE (short BFP)  */                \
  V(aeb, AEB, 0xED0A)   /* type = RXE   ADD (short BFP)  */                    \
  V(seb, SEB, 0xED0B)   /* type = RXE   SUBTRACT (short BFP)  */               \
  V(mdeb, MDEB, 0xED0C) /* type = RXE   MULTIPLY (short to long BFP)  */       \
  V(deb, DEB, 0xED0D)   /* type = RXE   DIVIDE (short BFP)  */                 \
  V(tceb, TCEB, 0xED10) /* type = RXE   TEST DATA CLASS (short BFP)  */        \
  V(tcdb, TCDB, 0xED11) /* type = RXE   TEST DATA CLASS (long BFP)  */         \
  V(tcxb, TCXB, 0xED12) /* type = RXE   TEST DATA CLASS (extended BFP)  */     \
  V(sqeb, SQEB, 0xED14) /* type = RXE   SQUARE ROOT (short BFP)  */            \
  V(sqdb, SQDB, 0xED15) /* type = RXE   SQUARE ROOT (long BFP)  */             \
  V(meeb, MEEB, 0xED17) /* type = RXE   MULTIPLY (short BFP)  */               \
  V(kdb, KDB, 0xED18)   /* type = RXE   COMPARE AND SIGNAL (long BFP)  */      \
  V(cdb, CDB, 0xED19)   /* type = RXE   COMPARE (long BFP)  */                 \
  V(adb, ADB, 0xED1A)   /* type = RXE   ADD (long BFP)  */                     \
  V(sdb, SDB, 0xED1B)   /* type = RXE   SUBTRACT (long BFP)  */                \
  V(mdb, MDB, 0xED1C)   /* type = RXE   MULTIPLY (long BFP)  */                \
  V(ddb, DDB, 0xED1D)   /* type = RXE   DIVIDE (long BFP)  */                  \
  V(lde, LDE, 0xED24) /* type = RXE   LOAD LENGTHENED (short to long HFP)  */  \
  V(lxd, LXD,                                                                  \
    0xED25) /* type = RXE   LOAD LENGTHENED (long to extended HFP)  */         \
  V(lxe, LXE,                                                                  \
    0xED26) /* type = RXE   LOAD LENGTHENED (short to extended HFP)  */        \
  V(sqe, SQE, 0xED34)     /* type = RXE   SQUARE ROOT (short HFP)  */          \
  V(sqd, SQD, 0xED35)     /* type = RXE   SQUARE ROOT (long HFP)  */           \
  V(mee, MEE, 0xED37)     /* type = RXE   MULTIPLY (short HFP)  */             \
  V(tdcet, TDCET, 0xED50) /* type = RXE   TEST DATA CLASS (short DFP)  */      \
  V(tdget, TDGET, 0xED51) /* type = RXE   TEST DATA GROUP (short DFP)  */      \
  V(tdcdt, TDCDT, 0xED54) /* type = RXE   TEST DATA CLASS (long DFP)  */       \
  V(tdgdt, TDGDT, 0xED55) /* type = RXE   TEST DATA GROUP (long DFP)  */       \
  V(tdcxt, TDCXT, 0xED58) /* type = RXE   TEST DATA CLASS (extended DFP)  */   \
  V(tdgxt, TDGXT, 0xED59) /* type = RXE   TEST DATA GROUP (extended DFP)  */

#define S390_RRF_A_OPCODE_LIST(V)                                           \
  V(ipte, IPTE, 0xB221)     /* type = RRF_A INVALIDATE PAGE TABLE ENTRY  */ \
  V(mdtra, MDTRA, 0xB3D0)   /* type = RRF_A MULTIPLY (long DFP)  */         \
  V(ddtra, DDTRA, 0xB3D1)   /* type = RRF_A DIVIDE (long DFP)  */           \
  V(adtra, ADTRA, 0xB3D2)   /* type = RRF_A ADD (long DFP)  */              \
  V(sdtra, SDTRA, 0xB3D3)   /* type = RRF_A SUBTRACT (long DFP)  */         \
  V(mxtra, MXTRA, 0xB3D8)   /* type = RRF_A MULTIPLY (extended DFP)  */     \
  V(msrkc, MSRKC, 0xB9FD)   /* type = RRF_A MULTIPLY (32)*/                 \
  V(msgrkc, MSGRKC, 0xB9ED) /* type = RRF_A MULTIPLY (64)*/                 \
  V(dxtra, DXTRA, 0xB3D9)   /* type = RRF_A DIVIDE (extended DFP)  */       \
  V(axtra, AXTRA, 0xB3DA)   /* type = RRF_A ADD (extended DFP)  */          \
  V(sxtra, SXTRA, 0xB3DB)   /* type = RRF_A SUBTRACT (extended DFP)  */     \
  V(ahhhr, AHHHR, 0xB9C8)   /* type = RRF_A ADD HIGH (32)  */               \
  V(shhhr, SHHHR, 0xB9C9)   /* type = RRF_A SUBTRACT HIGH (32)  */          \
  V(alhhhr, ALHHHR, 0xB9CA) /* type = RRF_A ADD LOGICAL HIGH (32)  */       \
  V(slhhhr, SLHHHR, 0xB9CB) /* type = RRF_A SUBTRACT LOGICAL HIGH (32)  */  \
  V(ahhlr, AHHLR, 0xB9D8)   /* type = RRF_A ADD HIGH (32)  */               \
  V(shhlr, SHHLR, 0xB9D9)   /* type = RRF_A SUBTRACT HIGH (32)  */          \
  V(alhhlr, ALHHLR, 0xB9DA) /* type = RRF_A ADD LOGICAL HIGH (32)  */       \
  V(slhhlr, SLHHLR, 0xB9DB) /* type = RRF_A SUBTRACT LOGICAL HIGH (32)  */  \
  V(ngrk, NGRK, 0xB9E4)     /* type = RRF_A AND (64)  */                    \
  V(ogrk, OGRK, 0xB9E6)     /* type = RRF_A OR (64)  */                     \
  V(xgrk, XGRK, 0xB9E7)     /* type = RRF_A EXCLUSIVE OR (64)  */           \
  V(agrk, AGRK, 0xB9E8)     /* type = RRF_A ADD (64)  */                    \
  V(sgrk, SGRK, 0xB9E9)     /* type = RRF_A SUBTRACT (64)  */               \
  V(mgrk, MGRK, 0xB9EC)     /* type = RRF_A MULTIPLY (64->128)  */          \
  V(algrk, ALGRK, 0xB9EA)   /* type = RRF_A ADD LOGICAL (64)  */            \
  V(slgrk, SLGRK, 0xB9EB)   /* type = RRF_A SUBTRACT LOGICAL (64)  */       \
  V(nrk, NRK, 0xB9F4)       /* type = RRF_A AND (32)  */                    \
  V(ork, ORK, 0xB9F6)       /* type = RRF_A OR (32)  */                     \
  V(xrk, XRK, 0xB9F7)       /* type = RRF_A EXCLUSIVE OR (32)  */           \
  V(ark, ARK, 0xB9F8)       /* type = RRF_A ADD (32)  */                    \
  V(srk, SRK, 0xB9F9)       /* type = RRF_A SUBTRACT (32)  */               \
  V(alrk, ALRK, 0xB9FA)     /* type = RRF_A ADD LOGICAL (32)  */            \
  V(slrk, SLRK, 0xB9FB)     /* type = RRF_A SUBTRACT LOGICAL (32)  */

#define S390_RXF_OPCODE_LIST(V)                                                \
  V(maeb, MAEB, 0xED0E) /* type = RXF   MULTIPLY AND ADD (short BFP)  */       \
  V(mseb, MSEB, 0xED0F) /* type = RXF   MULTIPLY AND SUBTRACT (short BFP)  */  \
  V(madb, MADB, 0xED1E) /* type = RXF   MULTIPLY AND ADD (long BFP)  */        \
  V(msdb, MSDB, 0xED1F) /* type = RXF   MULTIPLY AND SUBTRACT (long BFP)  */   \
  V(mae, MAE, 0xED2E)   /* type = RXF   MULTIPLY AND ADD (short HFP)  */       \
  V(mse, MSE, 0xED2F)   /* type = RXF   MULTIPLY AND SUBTRACT (short HFP)  */  \
  V(mayl, MAYL,                                                                \
    0xED38) /* type = RXF   MULTIPLY AND ADD UNNRM. (long to ext. low HFP)  */ \
  V(myl, MYL,                                                                  \
    0xED39) /* type = RXF   MULTIPLY UNNORM. (long to ext. low HFP)  */        \
  V(may, MAY,                                                                  \
    0xED3A) /* type = RXF   MULTIPLY & ADD UNNORMALIZED (long to ext. HFP)  */ \
  V(my, MY,                                                                    \
    0xED3B) /* type = RXF   MULTIPLY UNNORMALIZED (long to ext. HFP)  */       \
  V(mayh, MAYH,                                                                \
    0xED3C) /* type = RXF   MULTIPLY AND ADD UNNRM. (long to ext. high HFP) */ \
  V(myh, MYH,                                                                  \
    0xED3D) /* type = RXF   MULTIPLY UNNORM. (long to ext. high HFP)  */       \
  V(mad, MAD, 0xED3E)   /* type = RXF   MULTIPLY AND ADD (long HFP)  */        \
  V(msd, MSD, 0xED3F)   /* type = RXF   MULTIPLY AND SUBTRACT (long HFP)  */   \
  V(sldt, SLDT, 0xED40) /* type = RXF   SHIFT SIGNIFICAND LEFT (long DFP)  */  \
  V(srdt, SRDT, 0xED41) /* type = RXF   SHIFT SIGNIFICAND RIGHT (long DFP)  */ \
  V(slxt, SLXT,                                                                \
    0xED48) /* type = RXF   SHIFT SIGNIFICAND LEFT (extended DFP)  */          \
  V(srxt, SRXT,                                                                \
    0xED49) /* type = RXF   SHIFT SIGNIFICAND RIGHT (extended DFP)  */

#define S390_IE_OPCODE_LIST(V) \
  V(niai, NIAI, 0xB2FA) /* type = IE    NEXT INSTRUCTION ACCESS INTENT  */

#define S390_RRF_B_OPCODE_LIST(V)                                           \
  V(diebr, DIEBR, 0xB353) /* type = RRF_B DIVIDE TO INTEGER (short BFP)  */ \
  V(didbr, DIDBR, 0xB35B) /* type = RRF_B DIVIDE TO INTEGER (long BFP)  */  \
  V(cpsdr, CPSDR, 0xB372) /* type = RRF_B COPY SIGN (long)  */              \
  V(qadtr, QADTR, 0xB3F5) /* type = RRF_B QUANTIZE (long DFP)  */           \
  V(iedtr, IEDTR,                                                           \
    0xB3F6) /* type = RRF_B INSERT BIASED EXPONENT (64 to long DFP)  */     \
  V(rrdtr, RRDTR, 0xB3F7) /* type = RRF_B REROUND (long DFP)  */            \
  V(qaxtr, QAXTR, 0xB3FD) /* type = RRF_B QUANTIZE (extended DFP)  */       \
  V(iextr, IEXTR,                                                           \
    0xB3FE) /* type = RRF_B INSERT BIASED EXPONENT (64 to extended DFP)  */ \
  V(rrxtr, RRXTR, 0xB3FF) /* type = RRF_B REROUND (extended DFP)  */        \
  V(kmctr, KMCTR, 0xB92D) /* type = RRF_B CIPHER MESSAGE WITH COUNTER  */   \
  V(idte, IDTE, 0xB98E)   /* type = RRF_B INVALIDATE DAT TABLE ENTRY  */    \
  V(crdte, CRDTE,                                                           \
    0xB98F) /* type = RRF_B COMPARE AND REPLACE DAT TABLE ENTRY  */         \
  V(lptea, LPTEA, 0xB9AA) /* type = RRF_B LOAD PAGE TABLE ENTRY ADDRESS  */

#define S390_RRF_C_OPCODE_LIST(V)                                           \
  V(sske, SSKE, 0xB22B)   /* type = RRF_C SET STORAGE KEY EXTENDED  */      \
  V(cu21, CU21, 0xB2A6)   /* type = RRF_C CONVERT UTF-16 TO UTF-8  */       \
  V(cu12, CU12, 0xB2A7)   /* type = RRF_C CONVERT UTF-8 TO UTF-16  */       \
  V(ppa, PPA, 0xB2E8)     /* type = RRF_C PERFORM PROCESSOR ASSIST  */      \
  V(cgrt, CGRT, 0xB960)   /* type = RRF_C COMPARE AND TRAP (64)  */         \
  V(clgrt, CLGRT, 0xB961) /* type = RRF_C COMPARE LOGICAL AND TRAP (64)  */ \
  V(crt, CRT, 0xB972)     /* type = RRF_C COMPARE AND TRAP (32)  */         \
  V(clrt, CLRT, 0xB973)   /* type = RRF_C COMPARE LOGICAL AND TRAP (32)  */ \
  V(trtt, TRTT, 0xB990)   /* type = RRF_C TRANSLATE TWO TO TWO  */          \
  V(trto, TRTO, 0xB991)   /* type = RRF_C TRANSLATE TWO TO ONE  */          \
  V(trot, TROT, 0xB992)   /* type = RRF_C TRANSLATE ONE TO TWO  */          \
  V(troo, TROO, 0xB993)   /* type = RRF_C TRANSLATE ONE TO ONE  */          \
  V(cu14, CU14, 0xB9B0)   /* type = RRF_C CONVERT UTF-8 TO UTF-32  */       \
  V(cu24, CU24, 0xB9B1)   /* type = RRF_C CONVERT UTF-16 TO UTF-32  */      \
  V(trtre, TRTRE,                                                           \
    0xB9BD) /* type = RRF_C TRANSLATE AND TEST REVERSE EXTENDED  */         \
  V(trte, TRTE, 0xB9BF)     /* type = RRF_C TRANSLATE AND TEST EXTENDED  */ \
  V(locfhr, LOCFHR, 0xB9E0) /* type = RRF_C LOAD HIGH ON CONDITION (32)  */ \
  V(locgr, LOCGR, 0xB9E2)   /* type = RRF_C LOAD ON CONDITION (64)  */      \
  V(locr, LOCR, 0xB9F2)     /* type = RRF_C LOAD ON CONDITION (32)  */

#define S390_MII_OPCODE_LIST(V) \
  V(bprp, BPRP, 0xC5) /* type = MII   BRANCH PREDICTION RELATIVE PRELOAD  */

#define S390_RRF_D_OPCODE_LIST(V)                                         \
  V(ldetr, LDETR,                                                         \
    0xB3D4) /* type = RRF_D LOAD LENGTHENED (short to long DFP)  */       \
  V(lxdtr, LXDTR,                                                         \
    0xB3DC) /* type = RRF_D LOAD LENGTHENED (long to extended DFP)  */    \
  V(csdtr, CSDTR,                                                         \
    0xB3E3) /* type = RRF_D CONVERT TO SIGNED PACKED (long DFP to 64)  */ \
  V(csxtr, CSXTR,                                                         \
    0xB3EB) /* type = RRF_D CONVERT TO SIGNED PACKED (extended DFP to 128)  */

#define S390_RRF_E_OPCODE_LIST(V)                                              \
  V(ledbra, LEDBRA,                                                            \
    0xB344) /* type = RRF_E LOAD ROUNDED (long to short BFP)  */               \
  V(ldxbra, LDXBRA,                                                            \
    0xB345) /* type = RRF_E LOAD ROUNDED (extended to long BFP)  */            \
  V(lexbra, LEXBRA,                                                            \
    0xB346) /* type = RRF_E LOAD ROUNDED (extended to short BFP)  */           \
  V(fixbra, FIXBRA, 0xB347) /* type = RRF_E LOAD FP INTEGER (extended BFP)  */ \
  V(tbedr, TBEDR,                                                              \
    0xB350)             /* type = RRF_E CONVERT HFP TO BFP (long to short)  */ \
  V(tbdr, TBDR, 0xB351) /* type = RRF_E CONVERT HFP TO BFP (long)  */          \
  V(fiebra, FIEBRA, 0xB357) /* type = RRF_E LOAD FP INTEGER (short BFP)  */    \
  V(fidbra, FIDBRA, 0xB35F) /* type = RRF_E LOAD FP INTEGER (long BFP)  */     \
  V(celfbr, CELFBR,                                                            \
    0xB390) /* type = RRF_E CONVERT FROM LOGICAL (32 to short BFP)  */         \
  V(cdlfbr, CDLFBR,                                                            \
    0xB391) /* type = RRF_E CONVERT FROM LOGICAL (32 to long BFP)  */          \
  V(cxlfbr, CXLFBR,                                                            \
    0xB392) /* type = RRF_E CONVERT FROM LOGICAL (32 to extended BFP)  */      \
  V(cefbra, CEFBRA,                                                            \
    0xB394) /* type = RRF_E CONVERT FROM FIXED (32 to short BFP)  */           \
  V(cdfbra, CDFBRA,                                                            \
    0xB395) /* type = RRF_E CONVERT FROM FIXED (32 to long BFP)  */            \
  V(cxfbra, CXFBRA,                                                            \
    0xB396) /* type = RRF_E CONVERT FROM FIXED (32 to extended BFP)  */        \
  V(cfebra, CFEBRA,                                                            \
    0xB398) /* type = RRF_E CONVERT TO FIXED (short BFP to 32)  */             \
  V(cfdbra, CFDBRA,                                                            \
    0xB399) /* type = RRF_E CONVERT TO FIXED (long BFP to 32)  */              \
  V(cfxbra, CFXBRA,                                                            \
    0xB39A) /* type = RRF_E CONVERT TO FIXED (extended BFP to 32)  */          \
  V(clfebr, CLFEBR,                                                            \
    0xB39C) /* type = RRF_E CONVERT TO LOGICAL (short BFP to 32)  */           \
  V(clfdbr, CLFDBR,                                                            \
    0xB39D) /* type = RRF_E CONVERT TO LOGICAL (long BFP to 32)  */            \
  V(clfxbr, CLFXBR,                                                            \
    0xB39E) /* type = RRF_E CONVERT TO LOGICAL (extended BFP to 32)  */        \
  V(celgbr, CELGBR,                                                            \
    0xB3A0) /* type = RRF_E CONVERT FROM LOGICAL (64 to short BFP)  */         \
  V(cdlgbr, CDLGBR,                                                            \
    0xB3A1) /* type = RRF_E CONVERT FROM LOGICAL (64 to long BFP)  */          \
  V(cxlgbr, CXLGBR,                                                            \
    0xB3A2) /* type = RRF_E CONVERT FROM LOGICAL (64 to extended BFP)  */      \
  V(cegbra, CEGBRA,                                                            \
    0xB3A4) /* type = RRF_E CONVERT FROM FIXED (64 to short BFP)  */           \
  V(cdgbra, CDGBRA,                                                            \
    0xB3A5) /* type = RRF_E CONVERT FROM FIXED (64 to long BFP)  */            \
  V(cxgbra, CXGBRA,                                                            \
    0xB3A6) /* type = RRF_E CONVERT FROM FIXED (64 to extended BFP)  */        \
  V(cgebra, CGEBRA,                                                            \
    0xB3A8) /* type = RRF_E CONVERT TO FIXED (short BFP to 64)  */             \
  V(cgdbra, CGDBRA,                                                            \
    0xB3A9) /* type = RRF_E CONVERT TO FIXED (long BFP to 64)  */              \
  V(cgxbra, CGXBRA,                                                            \
    0xB3AA) /* type = RRF_E CONVERT TO FIXED (extended BFP to 64)  */          \
  V(clgebr, CLGEBR,                                                            \
    0xB3AC) /* type = RRF_E CONVERT TO LOGICAL (short BFP to 64)  */           \
  V(clgdbr, CLGDBR,                                                            \
    0xB3AD) /* type = RRF_E CONVERT TO LOGICAL (long BFP to 64)  */            \
  V(clgxbr, CLGXBR,                                                            \
    0xB3AE) /* type = RRF_E CONVERT TO LOGICAL (extended BFP to 64)  */        \
  V(cfer, CFER, 0xB3B8) /* type = RRF_E CONVERT TO FIXED (short HFP to 32)  */ \
  V(cfdr, CFDR, 0xB3B9) /* type = RRF_E CONVERT TO FIXED (long HFP to 32)  */  \
  V(cfxr, CFXR,                                                                \
    0xB3BA) /* type = RRF_E CONVERT TO FIXED (extended HFP to 32)  */          \
  V(cger, CGER, 0xB3C8) /* type = RRF_E CONVERT TO FIXED (short HFP to 64)  */ \
  V(cgdr, CGDR, 0xB3C9) /* type = RRF_E CONVERT TO FIXED (long HFP to 64)  */  \
  V(cgxr, CGXR,                                                                \
    0xB3CA) /* type = RRF_E CONVERT TO FIXED (extended HFP to 64)  */          \
  V(ledtr, LEDTR, 0xB3D5) /* type = RRF_E LOAD ROUNDED (long to short DFP)  */ \
  V(fidtr, FIDTR, 0xB3D7) /* type = RRF_E LOAD FP INTEGER (long DFP)  */       \
  V(ldxtr, LDXTR,                                                              \
    0xB3DD) /* type = RRF_E LOAD ROUNDED (extended to long DFP)  */            \
  V(fixtr, FIXTR, 0xB3DF) /* type = RRF_E LOAD FP INTEGER (extended DFP)  */   \
  V(cgdtra, CGDTRA,                                                            \
    0xB3E1) /* type = RRF_E CONVERT TO FIXED (long DFP to 64)  */              \
  V(cgxtra, CGXTRA,                                                            \
    0xB3E9) /* type = RRF_E CONVERT TO FIXED (extended DFP to 64)  */          \
  V(cdgtra, CDGTRA,                                                            \
    0xB3F1) /* type = RRF_E CONVERT FROM FIXED (64 to long DFP)  */            \
  V(cxgtra, CXGTRA,                                                            \
    0xB3F9) /* type = RRF_E CONVERT FROM FIXED (64 to extended DFP)  */        \
  V(cfdtr, CFDTR, 0xB941) /* type = RRF_E CONVERT TO FIXED (long DFP to 32) */ \
  V(clgdtr, CLGDTR,                                                            \
    0xB942) /* type = RRF_E CONVERT TO LOGICAL (long DFP to 64)  */            \
  V(clfdtr, CLFDTR,                                                            \
    0xB943) /* type = RRF_E CONVERT TO LOGICAL (long DFP to 32)  */            \
  V(cfxtr, CFXTR,                                                              \
    0xB949) /* type = RRF_E CONVERT TO FIXED (extended DFP to 32)  */          \
  V(clgxtr, CLGXTR,                                                            \
    0xB94A) /* type = RRF_E CONVERT TO LOGICAL (extended DFP to 64)  */        \
  V(clfxtr, CLFXTR,                                                            \
    0xB94B) /* type = RRF_E CONVERT TO LOGICAL (extended DFP to 32)  */        \
  V(cdlgtr, CDLGTR,                                                            \
    0xB952) /* type = RRF_E CONVERT FROM LOGICAL (64 to long DFP)  */          \
  V(cdlftr, CDLFTR,                                                            \
    0xB953) /* type = RRF_E CONVERT FROM LOGICAL (32 to long DFP)  */          \
  V(cxlgtr, CXLGTR,                                                            \
    0xB95A) /* type = RRF_E CONVERT FROM LOGICAL (64 to extended DFP)  */      \
  V(cxlftr, CXLFTR,                                                            \
    0xB95B) /* type = RRF_E CONVERT FROM LOGICAL (32 to extended DFP)  */

#define S390_VRR_A_OPCODE_LIST(V)                                              \
  V(vpopct, VPOPCT, 0xE750) /* type = VRR_A VECTOR POPULATION COUNT  */        \
  V(vctz, VCTZ, 0xE752)     /* type = VRR_A VECTOR COUNT TRAILING ZEROS  */    \
  V(vclz, VCLZ, 0xE753)     /* type = VRR_A VECTOR COUNT LEADING ZEROS  */     \
  V(vlr, VLR, 0xE756)       /* type = VRR_A VECTOR LOAD  */                    \
  V(vistr, VISTR, 0xE75C)   /* type = VRR_A VECTOR ISOLATE STRING  */          \
  V(vseg, VSEG, 0xE75F) /* type = VRR_A VECTOR SIGN EXTEND TO DOUBLEWORD  */   \
  V(vclgd, VCLGD,                                                              \
    0xE7C0) /* type = VRR_A VECTOR FP CONVERT TO LOGICAL 64-BIT  */            \
  V(vcdlg, VCDLG,                                                              \
    0xE7C1) /* type = VRR_A VECTOR FP CONVERT FROM LOGICAL 64-BIT  */          \
  V(vcgd, VCGD, 0xE7C2) /* type = VRR_A VECTOR FP CONVERT TO FIXED 64-BIT  */  \
  V(vcdg, VCDG, 0xE7C3) /* type = VRR_A VECTOR FP CONVERT FROM FIXED 64-BIT */ \
  V(vlde, VLDE, 0xE7C4) /* type = VRR_A VECTOR FP LOAD LENGTHENED  */          \
  V(vled, VLED, 0xE7C5) /* type = VRR_A VECTOR FP LOAD ROUNDED  */             \
  V(vfi, VFI, 0xE7C7)   /* type = VRR_A VECTOR LOAD FP INTEGER  */             \
  V(wfk, WFK, 0xE7CA) /* type = VRR_A VECTOR FP COMPARE AND SIGNAL SCALAR  */  \
  V(wfc, WFC, 0xE7CB) /* type = VRR_A VECTOR FP COMPARE SCALAR  */             \
  V(vfpso, VFPSO, 0xE7CC) /* type = VRR_A VECTOR FP PERFORM SIGN OPERATION  */ \
  V(vfsq, VFSQ, 0xE7CE)   /* type = VRR_A VECTOR FP SQUARE ROOT  */            \
  V(vupll, VUPLL, 0xE7D4) /* type = VRR_A VECTOR UNPACK LOGICAL LOW  */        \
  V(vuplh, VUPLH, 0xE7D5) /* type = VRR_A VECTOR UNPACK LOGICAL HIGH  */       \
  V(vupl, VUPL, 0xE7D6)   /* type = VRR_A VECTOR UNPACK LOW  */                \
  V(vuph, VUPH, 0xE7D7)   /* type = VRR_A VECTOR UNPACK HIGH  */               \
  V(vtm, VTM, 0xE7D8)     /* type = VRR_A VECTOR TEST UNDER MASK  */           \
  V(vecl, VECL, 0xE7D9)   /* type = VRR_A VECTOR ELEMENT COMPARE LOGICAL  */   \
  V(vec, VEC, 0xE7DB)     /* type = VRR_A VECTOR ELEMENT COMPARE  */           \
  V(vlc, VLC, 0xE7DE)     /* type = VRR_A VECTOR LOAD COMPLEMENT  */           \
  V(vlp, VLP, 0xE7DF)     /* type = VRR_A VECTOR LOAD POSITIVE  */

#define S390_VRR_B_OPCODE_LIST(V)                                           \
  V(vfee, VFEE, 0xE780)   /* type = VRR_B VECTOR FIND ELEMENT EQUAL  */     \
  V(vfene, VFENE, 0xE781) /* type = VRR_B VECTOR FIND ELEMENT NOT EQUAL  */ \
  V(vfae, VFAE, 0xE782)   /* type = VRR_B VECTOR FIND ANY ELEMENT EQUAL  */ \
  V(vpkls, VPKLS, 0xE795) /* type = VRR_B VECTOR PACK LOGICAL SATURATE  */  \
  V(vpks, VPKS, 0xE797)   /* type = VRR_B VECTOR PACK SATURATE  */          \
  V(vceq, VCEQ, 0xE7F8)   /* type = VRR_B VECTOR COMPARE EQUAL  */          \
  V(vchl, VCHL, 0xE7F9)   /* type = VRR_B VECTOR COMPARE HIGH LOGICAL  */   \
  V(vch, VCH, 0xE7FB)     /* type = VRR_B VECTOR COMPARE HIGH  */

#define S390_VRR_C_OPCODE_LIST(V)                                              \
  V(vmrl, VMRL, 0xE760)   /* type = VRR_C VECTOR MERGE LOW  */                 \
  V(vmrh, VMRH, 0xE761)   /* type = VRR_C VECTOR MERGE HIGH  */                \
  V(vsum, VSUM, 0xE764)   /* type = VRR_C VECTOR SUM ACROSS WORD  */           \
  V(vsumg, VSUMG, 0xE765) /* type = VRR_C VECTOR SUM ACROSS DOUBLEWORD  */     \
  V(vcksm, VCKSM, 0xE766) /* type = VRR_C VECTOR CHECKSUM  */                  \
  V(vsumq, VSUMQ, 0xE767) /* type = VRR_C VECTOR SUM ACROSS QUADWORD  */       \
  V(vn, VN, 0xE768)       /* type = VRR_C VECTOR AND  */                       \
  V(vnc, VNC, 0xE769)     /* type = VRR_C VECTOR AND WITH COMPLEMENT  */       \
  V(vo, VO, 0xE76A)       /* type = VRR_C VECTOR OR  */                        \
  V(vno, VNO, 0xE76B)     /* type = VRR_C VECTOR NOR  */                       \
  V(vx, VX, 0xE76D)       /* type = VRR_C VECTOR EXCLUSIVE OR  */              \
  V(veslv, VESLV, 0xE770) /* type = VRR_C VECTOR ELEMENT SHIFT LEFT  */        \
  V(verllv, VERLLV,                                                            \
    0xE773)             /* type = VRR_C VECTOR ELEMENT ROTATE LEFT LOGICAL  */ \
  V(vsl, VSL, 0xE774)   /* type = VRR_C VECTOR SHIFT LEFT  */                  \
  V(vslb, VSLB, 0xE775) /* type = VRR_C VECTOR SHIFT LEFT BY BYTE  */          \
  V(vesrlv, VESRLV,                                                            \
    0xE778) /* type = VRR_C VECTOR ELEMENT SHIFT RIGHT LOGICAL  */             \
  V(vesrav, VESRAV,                                                            \
    0xE77A) /* type = VRR_C VECTOR ELEMENT SHIFT RIGHT ARITHMETIC  */          \
  V(vsrl, VSRL, 0xE77C) /* type = VRR_C VECTOR SHIFT RIGHT LOGICAL  */         \
  V(vsrlb, VSRLB,                                                              \
    0xE77D)             /* type = VRR_C VECTOR SHIFT RIGHT LOGICAL BY BYTE  */ \
  V(vsra, VSRA, 0xE77E) /* type = VRR_C VECTOR SHIFT RIGHT ARITHMETIC  */      \
  V(vsrab, VSRAB,                                                              \
    0xE77F) /* type = VRR_C VECTOR SHIFT RIGHT ARITHMETIC BY BYTE  */          \
  V(vpdi, VPDI, 0xE784) /* type = VRR_C VECTOR PERMUTE DOUBLEWORD IMMEDIATE */ \
  V(vpk, VPK, 0xE794)   /* type = VRR_C VECTOR PACK  */                        \
  V(vmlh, VMLH, 0xE7A1) /* type = VRR_C VECTOR MULTIPLY LOGICAL HIGH  */       \
  V(vml, VML, 0xE7A2)   /* type = VRR_C VECTOR MULTIPLY LOW  */                \
  V(vmh, VMH, 0xE7A3)   /* type = VRR_C VECTOR MULTIPLY HIGH  */               \
  V(vmle, VMLE, 0xE7A4) /* type = VRR_C VECTOR MULTIPLY LOGICAL EVEN  */       \
  V(vmlo, VMLO, 0xE7A5) /* type = VRR_C VECTOR MULTIPLY LOGICAL ODD  */        \
  V(vme, VME, 0xE7A6)   /* type = VRR_C VECTOR MULTIPLY EVEN  */               \
  V(vmo, VMO, 0xE7A7)   /* type = VRR_C VECTOR MULTIPLY ODD  */                \
  V(vgfm, VGFM, 0xE7B4) /* type = VRR_C VECTOR GALOIS FIELD MULTIPLY SUM  */   \
  V(vfs, VFS, 0xE7E2)   /* type = VRR_C VECTOR FP SUBTRACT  */                 \
  V(vfa, VFA, 0xE7E3)   /* type = VRR_C VECTOR FP ADD  */                      \
  V(vfd, VFD, 0xE7E5)   /* type = VRR_C VECTOR FP DIVIDE  */                   \
  V(vfm, VFM, 0xE7E7)   /* type = VRR_C VECTOR FP MULTIPLY  */                 \
  V(vfce, VFCE, 0xE7E8) /* type = VRR_C VECTOR FP COMPARE EQUAL  */            \
  V(vfche, VFCHE, 0xE7EA) /* type = VRR_C VECTOR FP COMPARE HIGH OR EQUAL  */  \
  V(vfch, VFCH, 0xE7EB)   /* type = VRR_C VECTOR FP COMPARE HIGH  */           \
  V(vfmax, VFMAX, 0xE7EF) /* type = VRR_C VECTOR FP MAXIMUM */                 \
  V(vfmin, VFMIN, 0xE7EE) /* type = VRR_C VECTOR FP MINIMUM */                 \
  V(vavgl, VAVGL, 0xE7F0) /* type = VRR_C VECTOR AVERAGE LOGICAL  */           \
  V(vacc, VACC, 0xE7F1)   /* type = VRR_C VECTOR ADD COMPUTE CARRY  */         \
  V(vavg, VAVG, 0xE7F2)   /* type = VRR_C VECTOR AVERAGE  */                   \
  V(va, VA, 0xE7F3)       /* type = VRR_C VECTOR ADD  */                       \
  V(vscbi, VSCBI,                                                              \
    0xE7F5) /* type = VRR_C VECTOR SUBTRACT COMPUTE BORROW INDICATION  */      \
  V(vs, VS, 0xE7F7)         /* type = VRR_C VECTOR SUBTRACT  */                \
  V(vmnl, VMNL, 0xE7FC)     /* type = VRR_C VECTOR MINIMUM LOGICAL  */         \
  V(vmxl, VMXL, 0xE7FD)     /* type = VRR_C VECTOR MAXIMUM LOGICAL  */         \
  V(vmn, VMN, 0xE7FE)       /* type = VRR_C VECTOR MINIMUM  */                 \
  V(vmx, VMX, 0xE7FF)       /* type = VRR_C VECTOR MAXIMUM  */                 \
  V(vbperm, VBPERM, 0xE785) /* type = VRR_C VECTOR BIT PERMUTE  */

#define S390_VRI_A_OPCODE_LIST(V)                                              \
  V(vleib, VLEIB, 0xE740) /* type = VRI_A VECTOR LOAD ELEMENT IMMEDIATE (8) */ \
  V(vleih, VLEIH,                                                              \
    0xE741) /* type = VRI_A VECTOR LOAD ELEMENT IMMEDIATE (16)  */             \
  V(vleig, VLEIG,                                                              \
    0xE742) /* type = VRI_A VECTOR LOAD ELEMENT IMMEDIATE (64)  */             \
  V(vleif, VLEIF,                                                              \
    0xE743)             /* type = VRI_A VECTOR LOAD ELEMENT IMMEDIATE (32)  */ \
  V(vgbm, VGBM, 0xE744) /* type = VRI_A VECTOR GENERATE BYTE MASK  */          \
  V(vrepi, VREPI, 0xE745) /* type = VRI_A VECTOR REPLICATE IMMEDIATE  */

#define S390_VRR_D_OPCODE_LIST(V)                                              \
  V(vstrc, VSTRC, 0xE78A) /* type = VRR_D VECTOR STRING RANGE COMPARE  */      \
  V(vmalh, VMALH,                                                              \
    0xE7A9) /* type = VRR_D VECTOR MULTIPLY AND ADD LOGICAL HIGH  */           \
  V(vmal, VMAL, 0xE7AA) /* type = VRR_D VECTOR MULTIPLY AND ADD LOW  */        \
  V(vmah, VMAH, 0xE7AB) /* type = VRR_D VECTOR MULTIPLY AND ADD HIGH  */       \
  V(vmale, VMALE,                                                              \
    0xE7AC) /* type = VRR_D VECTOR MULTIPLY AND ADD LOGICAL EVEN  */           \
  V(vmalo, VMALO,                                                              \
    0xE7AD) /* type = VRR_D VECTOR MULTIPLY AND ADD LOGICAL ODD  */            \
  V(vmae, VMAE, 0xE7AE) /* type = VRR_D VECTOR MULTIPLY AND ADD EVEN  */       \
  V(vmao, VMAO, 0xE7AF) /* type = VRR_D VECTOR MULTIPLY AND ADD ODD  */        \
  V(vaccc, VACCC,                                                              \
    0xE7B9)           /* type = VRR_D VECTOR ADD WITH CARRY COMPUTE CARRY  */  \
  V(vac, VAC, 0xE7BB) /* type = VRR_D VECTOR ADD WITH CARRY  */                \
  V(vgfma, VGFMA,                                                              \
    0xE7BC) /* type = VRR_D VECTOR GALOIS FIELD MULTIPLY SUM AND ACCUMULATE */ \
  V(vsbcbi, VSBCBI, 0xE7BD) /* type = VRR_D VECTOR SUBTRACT WITH BORROW     */ \
                            /* COMPUTE BORROW INDICATION  */                   \
  V(vsbi, VSBI,                                                                \
    0xE7BF) /* type = VRR_D VECTOR SUBTRACT WITH BORROW INDICATION  */

#define S390_VRI_B_OPCODE_LIST(V) \
  V(vgm, VGM, 0xE746) /* type = VRI_B VECTOR GENERATE MASK  */

#define S390_VRR_E_OPCODE_LIST(V)                                             \
  V(vperm, VPERM, 0xE78C) /* type = VRR_E VECTOR PERMUTE  */                  \
  V(vsel, VSEL, 0xE78D)   /* type = VRR_E VECTOR SELECT  */                   \
  V(vfms, VFMS, 0xE78E)   /* type = VRR_E VECTOR FP MULTIPLY AND SUBTRACT  */ \
  V(vfnms, VFNMS,                                                             \
    0xE79E) /* type = VRR_E VECTOR FP NEGATIVE MULTIPLY AND SUBTRACT  */      \
  V(vfma, VFMA, 0xE78F) /* type = VRR_E VECTOR FP MULTIPLY AND ADD  */

#define S390_VRI_C_OPCODE_LIST(V) \
  V(vrep, VREP, 0xE74D) /* type = VRI_C VECTOR REPLICATE  */

#define S390_VRI_D_OPCODE_LIST(V)                                           \
  V(verim, VERIM,                                                           \
    0xE772) /* type = VRI_D VECTOR ELEMENT ROTATE AND INSERT UNDER MASK  */ \
  V(vsldb, VSLDB, 0xE777) /* type = VRI_D VECTOR SHIFT LEFT DOUBLE BY BYTE  */

#define S390_VRR_F_OPCODE_LIST(V) \
  V(vlvgp, VLVGP, 0xE762) /* type = VRR_F VECTOR LOAD VR FROM GRS DISJOINT  */

#define S390_RIS_OPCODE_LIST(V)                                                \
  V(cgib, CGIB,                                                                \
    0xECFC) /* type = RIS   COMPARE IMMEDIATE AND BRANCH (64<-8)  */           \
  V(clgib, CLGIB,                                                              \
    0xECFD) /* type = RIS   COMPARE LOGICAL IMMEDIATE AND BRANCH (64<-8)  */   \
  V(cib, CIB, 0xECFE) /* type = RIS   COMPARE IMMEDIATE AND BRANCH (32<-8)  */ \
  V(clib, CLIB,                                                                \
    0xECFF) /* type = RIS   COMPARE LOGICAL IMMEDIATE AND BRANCH (32<-8)  */

#define S390_VRI_E_OPCODE_LIST(V) \
  V(vftci, VFTCI,                 \
    0xE74A) /* type = VRI_E VECTOR FP TEST DATA CLASS IMMEDIATE  */

#define S390_RSL_A_OPCODE_LIST(V) \
  V(tp, TP, 0xEBC0) /* type = RSL_A TEST DECIMAL  */

#define S390_RSL_B_OPCODE_LIST(V)                                             \
  V(cpdt, CPDT, 0xEDAC) /* type = RSL_B CONVERT TO PACKED (from long DFP)  */ \
  V(cpxt, CPXT,                                                               \
    0xEDAD) /* type = RSL_B CONVERT TO PACKED (from extended DFP)  */         \
  V(cdpt, CDPT, 0xEDAE) /* type = RSL_B CONVERT FROM PACKED (to long DFP)  */ \
  V(cxpt, CXPT,                                                               \
    0xEDAF) /* type = RSL_B CONVERT FROM PACKED (to extended DFP)  */         \
  V(czdt, CZDT, 0xEDA8) /* type = RSL CONVERT TO ZONED (from long DFP)  */    \
  V(czxt, CZXT, 0xEDA9) /* type = RSL CONVERT TO ZONED (from extended DFP) */ \
  V(cdzt, CDZT, 0xEDAA) /* type = RSL CONVERT FROM ZONED (to long DFP)  */    \
  V(cxzt, CXZT, 0xEDAB) /* type = RSL CONVERT FROM ZONED (to extended DFP) */

#define S390_SI_OPCODE_LIST(V)                                          \
  V(tm, TM, 0x91)       /* type = SI    TEST UNDER MASK  */             \
  V(mvi, MVI, 0x92)     /* type = SI    MOVE (immediate)  */            \
  V(ni, NI, 0x94)       /* type = SI    AND (immediate)  */             \
  V(cli, CLI, 0x95)     /* type = SI    COMPARE LOGICAL (immediate)  */ \
  V(oi, OI, 0x96)       /* type = SI    OR (immediate)  */              \
  V(xi, XI, 0x97)       /* type = SI    EXCLUSIVE OR (immediate)  */    \
  V(stnsm, STNSM, 0xAC) /* type = SI    STORE THEN AND SYSTEM MASK  */  \
  V(stosm, STOSM, 0xAD) /* type = SI    STORE THEN OR SYSTEM MASK  */   \
  V(mc, MC, 0xAF)       /* type = SI    MONITOR CALL  */

#define S390_SIL_OPCODE_LIST(V)                                                \
  V(mvhhi, MVHHI, 0xE544) /* type = SIL   MOVE (16<-16)  */                    \
  V(mvghi, MVGHI, 0xE548) /* type = SIL   MOVE (64<-16)  */                    \
  V(mvhi, MVHI, 0xE54C)   /* type = SIL   MOVE (32<-16)  */                    \
  V(chhsi, CHHSI,                                                              \
    0xE554) /* type = SIL   COMPARE HALFWORD IMMEDIATE (16<-16)  */            \
  V(clhhsi, CLHHSI,                                                            \
    0xE555) /* type = SIL   COMPARE LOGICAL IMMEDIATE (16<-16)  */             \
  V(cghsi, CGHSI,                                                              \
    0xE558) /* type = SIL   COMPARE HALFWORD IMMEDIATE (64<-16)  */            \
  V(clghsi, CLGHSI,                                                            \
    0xE559)             /* type = SIL   COMPARE LOGICAL IMMEDIATE (64<-16)  */ \
  V(chsi, CHSI, 0xE55C) /* type = SIL   COMPARE HALFWORD IMMEDIATE (32<-16) */ \
  V(clfhsi, CLFHSI,                                                            \
    0xE55D) /* type = SIL   COMPARE LOGICAL IMMEDIATE (32<-16)  */             \
  V(tbegin, TBEGIN,                                                            \
    0xE560) /* type = SIL   TRANSACTION BEGIN (nonconstrained)  */             \
  V(tbeginc, TBEGINC,                                                          \
    0xE561) /* type = SIL   TRANSACTION BEGIN (constrained)  */

#define S390_VRS_A_OPCODE_LIST(V)                                            \
  V(vesl, VESL, 0xE730) /* type = VRS_A VECTOR ELEMENT SHIFT LEFT  */        \
  V(verll, VERLL,                                                            \
    0xE733)           /* type = VRS_A VECTOR ELEMENT ROTATE LEFT LOGICAL  */ \
  V(vlm, VLM, 0xE736) /* type = VRS_A VECTOR LOAD MULTIPLE  */               \
  V(vesrl, VESRL,                                                            \
    0xE738) /* type = VRS_A VECTOR ELEMENT SHIFT RIGHT LOGICAL  */           \
  V(vesra, VESRA,                                                            \
    0xE73A) /* type = VRS_A VECTOR ELEMENT SHIFT RIGHT ARITHMETIC  */        \
  V(vstm, VSTM, 0xE73E) /* type = VRS_A VECTOR STORE MULTIPLE  */

#define S390_RIL_A_OPCODE_LIST(V)                                              \
  V(lgfi, LGFI, 0xC01)   /* type = RIL_A LOAD IMMEDIATE (64<-32)  */           \
  V(xihf, XIHF, 0xC06)   /* type = RIL_A EXCLUSIVE OR IMMEDIATE (high)  */     \
  V(xilf, XILF, 0xC07)   /* type = RIL_A EXCLUSIVE OR IMMEDIATE (low)  */      \
  V(iihf, IIHF, 0xC08)   /* type = RIL_A INSERT IMMEDIATE (high)  */           \
  V(iilf, IILF, 0xC09)   /* type = RIL_A INSERT IMMEDIATE (low)  */            \
  V(nihf, NIHF, 0xC0A)   /* type = RIL_A AND IMMEDIATE (high)  */              \
  V(nilf, NILF, 0xC0B)   /* type = RIL_A AND IMMEDIATE (low)  */               \
  V(oihf, OIHF, 0xC0C)   /* type = RIL_A OR IMMEDIATE (high)  */               \
  V(oilf, OILF, 0xC0D)   /* type = RIL_A OR IMMEDIATE (low)  */                \
  V(llihf, LLIHF, 0xC0E) /* type = RIL_A LOAD LOGICAL IMMEDIATE (high)  */     \
  V(llilf, LLILF, 0xC0F) /* type = RIL_A LOAD LOGICAL IMMEDIATE (low)  */      \
  V(msgfi, MSGFI, 0xC20) /* type = RIL_A MULTIPLY SINGLE IMMEDIATE (64<-32) */ \
  V(msfi, MSFI, 0xC21)   /* type = RIL_A MULTIPLY SINGLE IMMEDIATE (32)  */    \
  V(slgfi, SLGFI,                                                              \
    0xC24)             /* type = RIL_A SUBTRACT LOGICAL IMMEDIATE (64<-32)  */ \
  V(slfi, SLFI, 0xC25) /* type = RIL_A SUBTRACT LOGICAL IMMEDIATE (32)  */     \
  V(agfi, AGFI, 0xC28) /* type = RIL_A ADD IMMEDIATE (64<-32)  */              \
  V(afi, AFI, 0xC29)   /* type = RIL_A ADD IMMEDIATE (32)  */                  \
  V(algfi, ALGFI, 0xC2A) /* type = RIL_A ADD LOGICAL IMMEDIATE (64<-32)  */    \
  V(alfi, ALFI, 0xC2B)   /* type = RIL_A ADD LOGICAL IMMEDIATE (32)  */        \
  V(cgfi, CGFI, 0xC2C)   /* type = RIL_A COMPARE IMMEDIATE (64<-32)  */        \
  V(cfi, CFI, 0xC2D)     /* type = RIL_A COMPARE IMMEDIATE (32)  */            \
  V(clgfi, CLGFI, 0xC2E) /* type = RIL_A COMPARE LOGICAL IMMEDIATE (64<-32) */ \
  V(clfi, CLFI, 0xC2F)   /* type = RIL_A COMPARE LOGICAL IMMEDIATE (32)  */    \
  V(aih, AIH, 0xCC8)     /* type = RIL_A ADD IMMEDIATE HIGH (32)  */           \
  V(alsih, ALSIH,                                                              \
    0xCCA) /* type = RIL_A ADD LOGICAL WITH SIGNED IMMEDIATE HIGH (32)  */     \
  V(alsihn, ALSIHN,                                                            \
    0xCCB) /* type = RIL_A ADD LOGICAL WITH SIGNED IMMEDIATE HIGH (32)  */     \
  V(cih, CIH, 0xCCD)   /* type = RIL_A COMPARE IMMEDIATE HIGH (32)  */         \
  V(clih, CLIH, 0xCCF) /* type = RIL_A COMPARE LOGICAL IMMEDIATE HIGH (32)  */

#define S390_RIL_B_OPCODE_LIST(V)                                              \
  V(larl, LARL, 0xC00)   /* type = RIL_B LOAD ADDRESS RELATIVE LONG  */        \
  V(brasl, BRASL, 0xC05) /* type = RIL_B BRANCH RELATIVE AND SAVE LONG  */     \
  V(llhrl, LLHRL,                                                              \
    0xC42) /* type = RIL_B LOAD LOGICAL HALFWORD RELATIVE LONG (32<-16)  */    \
  V(lghrl, LGHRL,                                                              \
    0xC44) /* type = RIL_B LOAD HALFWORD RELATIVE LONG (64<-16)  */            \
  V(lhrl, LHRL, 0xC45) /* type = RIL_B LOAD HALFWORD RELATIVE LONG (32<-16) */ \
  V(llghrl, LLGHRL,                                                            \
    0xC46) /* type = RIL_B LOAD LOGICAL HALFWORD RELATIVE LONG (64<-16)  */    \
  V(sthrl, STHRL, 0xC47) /* type = RIL_B STORE HALFWORD RELATIVE LONG (16)  */ \
  V(lgrl, LGRL, 0xC48)   /* type = RIL_B LOAD RELATIVE LONG (64)  */           \
  V(stgrl, STGRL, 0xC4B) /* type = RIL_B STORE RELATIVE LONG (64)  */          \
  V(lgfrl, LGFRL, 0xC4C) /* type = RIL_B LOAD RELATIVE LONG (64<-32)  */       \
  V(lrl, LRL, 0xC4D)     /* type = RIL_B LOAD RELATIVE LONG (32)  */           \
  V(llgfrl, LLGFRL,                                                            \
    0xC4E)             /* type = RIL_B LOAD LOGICAL RELATIVE LONG (64<-32)  */ \
  V(strl, STRL, 0xC4F) /* type = RIL_B STORE RELATIVE LONG (32)  */            \
  V(exrl, EXRL, 0xC60) /* type = RIL_B EXECUTE RELATIVE LONG  */               \
  V(cghrl, CGHRL,                                                              \
    0xC64) /* type = RIL_B COMPARE HALFWORD RELATIVE LONG (64<-16)  */         \
  V(chrl, CHRL,                                                                \
    0xC65) /* type = RIL_B COMPARE HALFWORD RELATIVE LONG (32<-16)  */         \
  V(clghrl, CLGHRL,                                                            \
    0xC66) /* type = RIL_B COMPARE LOGICAL RELATIVE LONG (64<-16)  */          \
  V(clhrl, CLHRL,                                                              \
    0xC67) /* type = RIL_B COMPARE LOGICAL RELATIVE LONG (32<-16)  */          \
  V(cgrl, CGRL, 0xC68)   /* type = RIL_B COMPARE RELATIVE LONG (64)  */        \
  V(clgrl, CLGRL, 0xC6A) /* type = RIL_B COMPARE LOGICAL RELATIVE LONG (64) */ \
  V(cgfrl, CGFRL, 0xC6C) /* type = RIL_B COMPARE RELATIVE LONG (64<-32)  */    \
  V(crl, CRL, 0xC6D)     /* type = RIL_B COMPARE RELATIVE LONG (32)  */        \
  V(clgfrl, CLGFRL,                                                            \
    0xC6E) /* type = RIL_B COMPARE LOGICAL RELATIVE LONG (64<-32)  */          \
  V(clrl, CLRL, 0xC6F) /* type = RIL_B COMPARE LOGICAL RELATIVE LONG (32)  */  \
  V(brcth, BRCTH, 0xCC6) /* type = RIL_B BRANCH RELATIVE ON COUNT HIGH (32) */

#define S390_VRS_B_OPCODE_LIST(V)                                          \
  V(vlvg, VLVG, 0xE722) /* type = VRS_B VECTOR LOAD VR ELEMENT FROM GR  */ \
  V(vll, VLL, 0xE737)   /* type = VRS_B VECTOR LOAD WITH LENGTH  */        \
  V(vstl, VSTL, 0xE73F) /* type = VRS_B VECTOR STORE WITH LENGTH  */

#define S390_RIL_C_OPCODE_LIST(V)                                              \
  V(brcl, BRCL, 0xC04)   /* type = RIL_C BRANCH RELATIVE ON CONDITION LONG  */ \
  V(pfdrl, PFDRL, 0xC62) /* type = RIL_C PREFETCH DATA RELATIVE LONG  */

#define S390_VRS_C_OPCODE_LIST(V) \
  V(vlgv, VLGV, 0xE721) /* type = VRS_C VECTOR LOAD GR FROM VR ELEMENT  */

#define S390_RI_A_OPCODE_LIST(V)                                               \
  V(iihh, IIHH, 0xA50)   /* type = RI_A  INSERT IMMEDIATE (high high)  */      \
  V(iihl, IIHL, 0xA51)   /* type = RI_A  INSERT IMMEDIATE (high low)  */       \
  V(iilh, IILH, 0xA52)   /* type = RI_A  INSERT IMMEDIATE (low high)  */       \
  V(iill, IILL, 0xA53)   /* type = RI_A  INSERT IMMEDIATE (low low)  */        \
  V(nihh, NIHH, 0xA54)   /* type = RI_A  AND IMMEDIATE (high high)  */         \
  V(nihl, NIHL, 0xA55)   /* type = RI_A  AND IMMEDIATE (high low)  */          \
  V(nilh, NILH, 0xA56)   /* type = RI_A  AND IMMEDIATE (low high)  */          \
  V(nill, NILL, 0xA57)   /* type = RI_A  AND IMMEDIATE (low low)  */           \
  V(oihh, OIHH, 0xA58)   /* type = RI_A  OR IMMEDIATE (high high)  */          \
  V(oihl, OIHL, 0xA59)   /* type = RI_A  OR IMMEDIATE (high low)  */           \
  V(oilh, OILH, 0xA5A)   /* type = RI_A  OR IMMEDIATE (low high)  */           \
  V(oill, OILL, 0xA5B)   /* type = RI_A  OR IMMEDIATE (low low)  */            \
  V(llihh, LLIHH, 0xA5C) /* type = RI_A  LOAD LOGICAL IMMEDIATE (high high) */ \
  V(llihl, LLIHL, 0xA5D) /* type = RI_A  LOAD LOGICAL IMMEDIATE (high low)  */ \
  V(llilh, LLILH, 0xA5E) /* type = RI_A  LOAD LOGICAL IMMEDIATE (low high)  */ \
  V(llill, LLILL, 0xA5F) /* type = RI_A  LOAD LOGICAL IMMEDIATE (low low)  */  \
  V(tmlh, TMLH, 0xA70)   /* type = RI_A  TEST UNDER MASK (low high)  */        \
  V(tmll, TMLL, 0xA71)   /* type = RI_A  TEST UNDER MASK (low low)  */         \
  V(tmhh, TMHH, 0xA72)   /* type = RI_A  TEST UNDER MASK (high high)  */       \
  V(tmhl, TMHL, 0xA73)   /* type = RI_A  TEST UNDER MASK (high low)  */        \
  V(lhi, LHI, 0xA78)     /* type = RI_A  LOAD HALFWORD IMMEDIATE (32)<-16  */  \
  V(lghi, LGHI, 0xA79)   /* type = RI_A  LOAD HALFWORD IMMEDIATE (64<-16)  */  \
  V(ahi, AHI, 0xA7A)     /* type = RI_A  ADD HALFWORD IMMEDIATE (32<-16)  */   \
  V(aghi, AGHI, 0xA7B)   /* type = RI_A  ADD HALFWORD IMMEDIATE (64<-16)  */   \
  V(mhi, MHI, 0xA7C) /* type = RI_A  MULTIPLY HALFWORD IMMEDIATE (32<-16)  */  \
  V(mghi, MGHI, 0xA7D) /* type = RI_A  MULTIPLY HALFWORD IMMEDIATE (64<-16) */ \
  V(chi, CHI, 0xA7E)   /* type = RI_A  COMPARE HALFWORD IMMEDIATE (32<-16)  */ \
  V(cghi, CGHI, 0xA7F) /* type = RI_A  COMPARE HALFWORD IMMEDIATE (64<-16)  */

#define S390_RSI_OPCODE_LIST(V)                                              \
  V(brxh, BRXH, 0x84) /* type = RSI   BRANCH RELATIVE ON INDEX HIGH (32)  */ \
  V(brxle, BRXLE,                                                            \
    0x85) /* type = RSI   BRANCH RELATIVE ON INDEX LOW OR EQ. (32)  */

#define S390_RI_B_OPCODE_LIST(V)                                           \
  V(bras, BRAS, 0xA75)   /* type = RI_B  BRANCH RELATIVE AND SAVE  */      \
  V(brct, BRCT, 0xA76)   /* type = RI_B  BRANCH RELATIVE ON COUNT (32)  */ \
  V(brctg, BRCTG, 0xA77) /* type = RI_B  BRANCH RELATIVE ON COUNT (64)  */

#define S390_RI_C_OPCODE_LIST(V) \
  V(brc, BRC, 0xA74) /* type = RI_C BRANCH RELATIVE ON CONDITION  */

#define S390_SMI_OPCODE_LIST(V) \
  V(bpp, BPP, 0xC7) /* type = SMI   BRANCH PREDICTION PRELOAD  */

#define S390_RXY_A_OPCODE_LIST(V)                                              \
  V(ltg, LTG, 0xE302)   /* type = RXY_A LOAD AND TEST (64)  */                 \
  V(lrag, LRAG, 0xE303) /* type = RXY_A LOAD REAL ADDRESS (64)  */             \
  V(lg, LG, 0xE304)     /* type = RXY_A LOAD (64)  */                          \
  V(cvby, CVBY, 0xE306) /* type = RXY_A CONVERT TO BINARY (32)  */             \
  V(ag, AG, 0xE308)     /* type = RXY_A ADD (64)  */                           \
  V(sg, SG, 0xE309)     /* type = RXY_A SUBTRACT (64)  */                      \
  V(alg, ALG, 0xE30A)   /* type = RXY_A ADD LOGICAL (64)  */                   \
  V(slg, SLG, 0xE30B)   /* type = RXY_A SUBTRACT LOGICAL (64)  */              \
  V(msg, MSG, 0xE30C)   /* type = RXY_A MULTIPLY SINGLE (64)  */               \
  V(dsg, DSG, 0xE30D)   /* type = RXY_A DIVIDE SINGLE (64)  */                 \
  V(cvbg, CVBG, 0xE30E) /* type = RXY_A CONVERT TO BINARY (64)  */             \
  V(lrvg, LRVG, 0xE30F) /* type = RXY_A LOAD REVERSED (64)  */                 \
  V(lt_z, LT, 0xE312)   /* type = RXY_A LOAD AND TEST (32)  */                 \
  V(lray, LRAY, 0xE313) /* type = RXY_A LOAD REAL ADDRESS (32)  */             \
  V(lgf, LGF, 0xE314)   /* type = RXY_A LOAD (64<-32)  */                      \
  V(lgh, LGH, 0xE315)   /* type = RXY_A LOAD HALFWORD (64<-16)  */             \
  V(llgf, LLGF, 0xE316) /* type = RXY_A LOAD LOGICAL (64<-32)  */              \
  V(llgt, LLGT,                                                                \
    0xE317) /* type = RXY_A LOAD LOGICAL THIRTY ONE BITS (64<-31)  */          \
  V(agf, AGF, 0xE318)     /* type = RXY_A ADD (64<-32)  */                     \
  V(sgf, SGF, 0xE319)     /* type = RXY_A SUBTRACT (64<-32)  */                \
  V(algf, ALGF, 0xE31A)   /* type = RXY_A ADD LOGICAL (64<-32)  */             \
  V(slgf, SLGF, 0xE31B)   /* type = RXY_A SUBTRACT LOGICAL (64<-32)  */        \
  V(msgf, MSGF, 0xE31C)   /* type = RXY_A MULTIPLY SINGLE (64<-32)  */         \
  V(dsgf, DSGF, 0xE31D)   /* type = RXY_A DIVIDE SINGLE (64<-32)  */           \
  V(lrv, LRV, 0xE31E)     /* type = RXY_A LOAD REVERSED (32)  */               \
  V(lrvh, LRVH, 0xE31F)   /* type = RXY_A LOAD REVERSED (16)  */               \
  V(cg, CG, 0xE320)       /* type = RXY_A COMPARE (64)  */                     \
  V(clg, CLG, 0xE321)     /* type = RXY_A COMPARE LOGICAL (64)  */             \
  V(stg, STG, 0xE324)     /* type = RXY_A STORE (64)  */                       \
  V(ntstg, NTSTG, 0xE325) /* type = RXY_A NONTRANSACTIONAL STORE (64)  */      \
  V(cvdy, CVDY, 0xE326)   /* type = RXY_A CONVERT TO DECIMAL (32)  */          \
  V(lzrg, LZRG, 0xE32A) /* type = RXY_A LOAD AND ZERO RIGHTMOST BYTE (64)  */  \
  V(cvdg, CVDG, 0xE32E) /* type = RXY_A CONVERT TO DECIMAL (64)  */            \
  V(strvg, STRVG, 0xE32F) /* type = RXY_A STORE REVERSED (64)  */              \
  V(cgf, CGF, 0xE330)     /* type = RXY_A COMPARE (64<-32)  */                 \
  V(clgf, CLGF, 0xE331)   /* type = RXY_A COMPARE LOGICAL (64<-32)  */         \
  V(ltgf, LTGF, 0xE332)   /* type = RXY_A LOAD AND TEST (64<-32)  */           \
  V(cgh, CGH, 0xE334)     /* type = RXY_A COMPARE HALFWORD (64<-16)  */        \
  V(llzrgf, LLZRGF,                                                            \
    0xE33A) /* type = RXY_A LOAD LOGICAL AND ZERO RIGHTMOST BYTE (64<-32)  */  \
  V(lzrf, LZRF, 0xE33B) /* type = RXY_A LOAD AND ZERO RIGHTMOST BYTE (32)  */  \
  V(strv, STRV, 0xE33E) /* type = RXY_A STORE REVERSED (32)  */                \
  V(strvh, STRVH, 0xE33F) /* type = RXY_A STORE REVERSED (16)  */              \
  V(bctg, BCTG, 0xE346)   /* type = RXY_A BRANCH ON COUNT (64)  */             \
  V(sty, STY, 0xE350)     /* type = RXY_A STORE (32)  */                       \
  V(msy, MSY, 0xE351)     /* type = RXY_A MULTIPLY SINGLE (32)  */             \
  V(ny, NY, 0xE354)       /* type = RXY_A AND (32)  */                         \
  V(cly, CLY, 0xE355)     /* type = RXY_A COMPARE LOGICAL (32)  */             \
  V(oy, OY, 0xE356)       /* type = RXY_A OR (32)  */                          \
  V(xy, XY, 0xE357)       /* type = RXY_A EXCLUSIVE OR (32)  */                \
  V(ly, LY, 0xE358)       /* type = RXY_A LOAD (32)  */                        \
  V(cy, CY, 0xE359)       /* type = RXY_A COMPARE (32)  */                     \
  V(ay, AY, 0xE35A)       /* type = RXY_A ADD (32)  */                         \
  V(sy, SY, 0xE35B)       /* type = RXY_A SUBTRACT (32)  */                    \
  V(mfy, MFY, 0xE35C)     /* type = RXY_A MULTIPLY (64<-32)  */                \
  V(mg, MG, 0xE384)       /* type = RXY_A MULTIPLY (128<-64)  */               \
  V(aly, ALY, 0xE35E)     /* type = RXY_A ADD LOGICAL (32)  */                 \
  V(sly, SLY, 0xE35F)     /* type = RXY_A SUBTRACT LOGICAL (32)  */            \
  V(sthy, STHY, 0xE370)   /* type = RXY_A STORE HALFWORD (16)  */              \
  V(lay, LAY, 0xE371)     /* type = RXY_A LOAD ADDRESS  */                     \
  V(stcy, STCY, 0xE372)   /* type = RXY_A STORE CHARACTER  */                  \
  V(icy, ICY, 0xE373)     /* type = RXY_A INSERT CHARACTER  */                 \
  V(laey, LAEY, 0xE375)   /* type = RXY_A LOAD ADDRESS EXTENDED  */            \
  V(lb, LB, 0xE376)       /* type = RXY_A LOAD BYTE (32<-8)  */                \
  V(lgb, LGB, 0xE377)     /* type = RXY_A LOAD BYTE (64<-8)  */                \
  V(lhy, LHY, 0xE378)     /* type = RXY_A LOAD HALFWORD (32)<-16  */           \
  V(chy, CHY, 0xE379)     /* type = RXY_A COMPARE HALFWORD (32<-16)  */        \
  V(ahy, AHY, 0xE37A)     /* type = RXY_A ADD HALFWORD (32<-16)  */            \
  V(shy, SHY, 0xE37B)     /* type = RXY_A SUBTRACT HALFWORD (32<-16)  */       \
  V(mhy, MHY, 0xE37C)     /* type = RXY_A MULTIPLY HALFWORD (32<-16)  */       \
  V(ng, NG, 0xE380)       /* type = RXY_A AND (64)  */                         \
  V(og, OG, 0xE381)       /* type = RXY_A OR (64)  */                          \
  V(xg, XG, 0xE382)       /* type = RXY_A EXCLUSIVE OR (64)  */                \
  V(lgat, LGAT, 0xE385)   /* type = RXY_A LOAD AND TRAP (64)  */               \
  V(mlg, MLG, 0xE386)     /* type = RXY_A MULTIPLY LOGICAL (128<-64)  */       \
  V(dlg, DLG, 0xE387)     /* type = RXY_A DIVIDE LOGICAL (64<-128)  */         \
  V(alcg, ALCG, 0xE388)   /* type = RXY_A ADD LOGICAL WITH CARRY (64)  */      \
  V(slbg, SLBG, 0xE389) /* type = RXY_A SUBTRACT LOGICAL WITH BORROW (64)  */  \
  V(stpq, STPQ, 0xE38E) /* type = RXY_A STORE PAIR TO QUADWORD  */             \
  V(lpq, LPQ, 0xE38F) /* type = RXY_A LOAD PAIR FROM QUADWORD (64&64<-128)  */ \
  V(llgc, LLGC, 0xE390) /* type = RXY_A LOAD LOGICAL CHARACTER (64<-8)  */     \
  V(llgh, LLGH, 0xE391) /* type = RXY_A LOAD LOGICAL HALFWORD (64<-16)  */     \
  V(llc, LLC, 0xE394)   /* type = RXY_A LOAD LOGICAL CHARACTER (32<-8)  */     \
  V(llh, LLH, 0xE395)   /* type = RXY_A LOAD LOGICAL HALFWORD (32<-16)  */     \
  V(ml, ML, 0xE396)     /* type = RXY_A MULTIPLY LOGICAL (64<-32)  */          \
  V(dl, DL, 0xE397)     /* type = RXY_A DIVIDE LOGICAL (32<-64)  */            \
  V(alc, ALC, 0xE398)   /* type = RXY_A ADD LOGICAL WITH CARRY (32)  */        \
  V(slb, SLB, 0xE399)   /* type = RXY_A SUBTRACT LOGICAL WITH BORROW (32)  */  \
  V(llgtat, LLGTAT,                                                            \
    0xE39C) /* type = RXY_A LOAD LOGICAL THIRTY ONE BITS AND TRAP (64<-31)  */ \
  V(llgfat, LLGFAT, 0xE39D) /* type = RXY_A LOAD LOGICAL AND TRAP (64<-32)  */ \
  V(lat, LAT, 0xE39F)       /* type = RXY_A LOAD AND TRAP (32L<-32)  */        \
  V(lbh, LBH, 0xE3C0)       /* type = RXY_A LOAD BYTE HIGH (32<-8)  */         \
  V(llch, LLCH, 0xE3C2) /* type = RXY_A LOAD LOGICAL CHARACTER HIGH (32<-8) */ \
  V(stch, STCH, 0xE3C3) /* type = RXY_A STORE CHARACTER HIGH (8)  */           \
  V(lhh, LHH, 0xE3C4)   /* type = RXY_A LOAD HALFWORD HIGH (32<-16)  */        \
  V(llhh, LLHH, 0xE3C6) /* type = RXY_A LOAD LOGICAL HALFWORD HIGH (32<-16) */ \
  V(sthh, STHH, 0xE3C7) /* type = RXY_A STORE HALFWORD HIGH (16)  */           \
  V(lfhat, LFHAT, 0xE3C8) /* type = RXY_A LOAD HIGH AND TRAP (32H<-32)  */     \
  V(lfh, LFH, 0xE3CA)     /* type = RXY_A LOAD HIGH (32)  */                   \
  V(stfh, STFH, 0xE3CB)   /* type = RXY_A STORE HIGH (32)  */                  \
  V(chf, CHF, 0xE3CD)     /* type = RXY_A COMPARE HIGH (32)  */                \
  V(clhf, CLHF, 0xE3CF)   /* type = RXY_A COMPARE LOGICAL HIGH (32)  */        \
  V(ley, LEY, 0xED64)     /* type = RXY_A LOAD (short)  */                     \
  V(ldy, LDY, 0xED65)     /* type = RXY_A LOAD (long)  */                      \
  V(stey, STEY, 0xED66)   /* type = RXY_A STORE (short)  */                    \
  V(stdy, STDY, 0xED67)   /* type = RXY_A STORE (long)  */                     \
  V(msc, MSC, 0xE353)     /* type = RSY_A MULTIPLY SINGLE (32)  */             \
  V(msgc, MSGC, 0xE383)   /* type = RSY_A MULTIPLY SINGLE (64)  */

#define S390_RXY_B_OPCODE_LIST(V) \
  V(pfd, PFD, 0xE336) /* type = RXY_B PREFETCH DATA  */

#define S390_SIY_OPCODE_LIST(V)                                           \
  V(tmy, TMY, 0xEB51)   /* type = SIY   TEST UNDER MASK  */               \
  V(mviy, MVIY, 0xEB52) /* type = SIY   MOVE (immediate)  */              \
  V(niy, NIY, 0xEB54)   /* type = SIY   AND (immediate)  */               \
  V(cliy, CLIY, 0xEB55) /* type = SIY   COMPARE LOGICAL (immediate)  */   \
  V(oiy, OIY, 0xEB56)   /* type = SIY   OR (immediate)  */                \
  V(xiy, XIY, 0xEB57)   /* type = SIY   EXCLUSIVE OR (immediate)  */      \
  V(asi, ASI, 0xEB6A)   /* type = SIY   ADD IMMEDIATE (32<-8)  */         \
  V(alsi, ALSI,                                                           \
    0xEB6E) /* type = SIY   ADD LOGICAL WITH SIGNED IMMEDIATE (32<-8)  */ \
  V(agsi, AGSI, 0xEB7A) /* type = SIY   ADD IMMEDIATE (64<-8)  */         \
  V(algsi, ALGSI,                                                         \
    0xEB7E) /* type = SIY   ADD LOGICAL WITH SIGNED IMMEDIATE (64<-8)  */

#define S390_SS_A_OPCODE_LIST(V)                                        \
  V(trtr, TRTR, 0xD0)   /* type = SS_A  TRANSLATE AND TEST REVERSE  */  \
  V(mvn, MVN, 0xD1)     /* type = SS_A  MOVE NUMERICS  */               \
  V(mvc, MVC, 0xD2)     /* type = SS_A  MOVE (character)  */            \
  V(mvz, MVZ, 0xD3)     /* type = SS_A  MOVE ZONES  */                  \
  V(nc, NC, 0xD4)       /* type = SS_A  AND (character)  */             \
  V(clc, CLC, 0xD5)     /* type = SS_A  COMPARE LOGICAL (character)  */ \
  V(oc, OC, 0xD6)       /* type = SS_A  OR (character)  */              \
  V(xc, XC, 0xD7)       /* type = SS_A  EXCLUSIVE OR (character)  */    \
  V(tr, TR, 0xDC)       /* type = SS_A  TRANSLATE  */                   \
  V(trt, TRT, 0xDD)     /* type = SS_A  TRANSLATE AND TEST  */          \
  V(ed, ED, 0xDE)       /* type = SS_A  EDIT  */                        \
  V(edmk, EDMK, 0xDF)   /* type = SS_A  EDIT AND MARK  */               \
  V(unpku, UNPKU, 0xE2) /* type = SS_A  UNPACK UNICODE  */              \
  V(mvcin, MVCIN, 0xE8) /* type = SS_A  MOVE INVERSE  */                \
  V(unpka, UNPKA, 0xEA) /* type = SS_A  UNPACK ASCII  */

#define S390_E_OPCODE_LIST(V)                                                  \
  V(pr, PR, 0x0101)       /* type = E     PROGRAM RETURN  */                   \
  V(upt, UPT, 0x0102)     /* type = E     UPDATE TREE  */                      \
  V(ptff, PTFF, 0x0104)   /* type = E     PERFORM TIMING FACILITY FUNCTION  */ \
  V(sckpf, SCKPF, 0x0107) /* type = E     SET CLOCK PROGRAMMABLE FIELD  */     \
  V(pfpo, PFPO, 0x010A)   /* type = E     PERFORM FLOATING-POINT OPERATION  */ \
  V(tam, TAM, 0x010B)     /* type = E     TEST ADDRESSING MODE  */             \
  V(sam24, SAM24, 0x010C) /* type = E     SET ADDRESSING MODE (24)  */         \
  V(sam31, SAM31, 0x010D) /* type = E     SET ADDRESSING MODE (31)  */         \
  V(sam64, SAM64, 0x010E) /* type = E     SET ADDRESSING MODE (64)  */         \
  V(trap2, TRAP2, 0x01FF) /* type = E     TRAP  */

#define S390_SS_B_OPCODE_LIST(V)                           \
  V(mvo, MVO, 0xF1)   /* type = SS_B  MOVE WITH OFFSET  */ \
  V(pack, PACK, 0xF2) /* type = SS_B  PACK  */             \
  V(unpk, UNPK, 0xF3) /* type = SS_B  UNPACK  */           \
  V(zap, ZAP, 0xF8)   /* type = SS_B  ZERO AND ADD  */     \
  V(cp, CP, 0xF9)     /* type = SS_B  COMPARE DECIMAL  */  \
  V(ap, AP, 0xFA)     /* type = SS_B  ADD DECIMAL  */      \
  V(sp, SP, 0xFB)     /* type = SS_B  SUBTRACT DECIMAL  */ \
  V(mp, MP, 0xFC)     /* type = SS_B  MULTIPLY DECIMAL  */ \
  V(dp, DP, 0xFD)     /* type = SS_B  DIVIDE DECIMAL  */

#define S390_SS_C_OPCODE_LIST(V) \
  V(srp, SRP, 0xF0) /* type = SS_C  SHIFT AND ROUND DECIMAL  */

#define S390_SS_D_OPCODE_LIST(V)                          \
  V(mvck, MVCK, 0xD9) /* type = SS_D  MOVE WITH KEY  */   \
  V(mvcp, MVCP, 0xDA) /* type = SS_D  MOVE TO PRIMARY  */ \
  V(mvcs, MVCS, 0xDB) /* type = SS_D  MOVE TO SECONDARY  */

#define S390_SS_E_OPCODE_LIST(V)                                 \
  V(plo, PLO, 0xEE) /* type = SS_E  PERFORM LOCKED OPERATION  */ \
  V(lmd, LMD, 0xEF) /* type = SS_E  LOAD MULTIPLE DISJOINT (64<-32&32)  */

#define S390_I_OPCODE_LIST(V) \
  V(svc, SVC, 0x0A) /* type = I     SUPERVISOR CALL  */

#define S390_SS_F_OPCODE_LIST(V)                     \
  V(pku, PKU, 0xE1) /* type = SS_F  PACK UNICODE  */ \
  V(pka, PKA, 0xE9) /* type = SS_F  PACK ASCII  */

#define S390_SSE_OPCODE_LIST(V)                                             \
  V(lasp, LASP, 0xE500)   /* type = SSE   LOAD ADDRESS SPACE PARAMETERS  */ \
  V(tprot, TPROT, 0xE501) /* type = SSE   TEST PROTECTION  */               \
  V(strag, STRAG, 0xE502) /* type = SSE   STORE REAL ADDRESS  */            \
  V(mvcsk, MVCSK, 0xE50E) /* type = SSE   MOVE WITH SOURCE KEY  */          \
  V(mvcdk, MVCDK, 0xE50F) /* type = SSE   MOVE WITH DESTINATION KEY  */

#define S390_SSF_OPCODE_LIST(V)                                                \
  V(mvcos, MVCOS, 0xC80) /* type = SSF   MOVE WITH OPTIONAL SPECIFICATIONS  */ \
  V(ectg, ECTG, 0xC81)   /* type = SSF   EXTRACT CPU TIME  */                  \
  V(csst, CSST, 0xC82)   /* type = SSF   COMPARE AND SWAP AND STORE  */        \
  V(lpd, LPD, 0xC84)     /* type = SSF   LOAD PAIR DISJOINT (32)  */           \
  V(lpdg, LPDG, 0xC85)   /* type = SSF   LOAD PAIR DISJOINT (64)  */

#define S390_RS_A_OPCODE_LIST(V)                                              \
  V(bxh, BXH, 0x86)     /* type = RS_A  BRANCH ON INDEX HIGH (32)  */         \
  V(bxle, BXLE, 0x87)   /* type = RS_A  BRANCH ON INDEX LOW OR EQUAL (32)  */ \
  V(srl, SRL, 0x88)     /* type = RS_A  SHIFT RIGHT SINGLE LOGICAL (32)  */   \
  V(sll, SLL, 0x89)     /* type = RS_A  SHIFT LEFT SINGLE LOGICAL (32)  */    \
  V(sra, SRA, 0x8A)     /* type = RS_A  SHIFT RIGHT SINGLE (32)  */           \
  V(sla, SLA, 0x8B)     /* type = RS_A  SHIFT LEFT SINGLE (32)  */            \
  V(srdl, SRDL, 0x8C)   /* type = RS_A  SHIFT RIGHT DOUBLE LOGICAL (64)  */   \
  V(sldl, SLDL, 0x8D)   /* type = RS_A  SHIFT LEFT DOUBLE LOGICAL (64)  */    \
  V(srda, SRDA, 0x8E)   /* type = RS_A  SHIFT RIGHT DOUBLE (64)  */           \
  V(slda, SLDA, 0x8F)   /* type = RS_A  SHIFT LEFT DOUBLE (64)  */            \
  V(stm, STM, 0x90)     /* type = RS_A  STORE MULTIPLE (32)  */               \
  V(lm, LM, 0x98)       /* type = RS_A  LOAD MULTIPLE (32)  */                \
  V(trace, TRACE, 0x99) /* type = RS_A  TRACE (32)  */                        \
  V(lam, LAM, 0x9A)     /* type = RS_A  LOAD ACCESS MULTIPLE  */              \
  V(stam, STAM, 0x9B)   /* type = RS_A  STORE ACCESS MULTIPLE  */             \
  V(mvcle, MVCLE, 0xA8) /* type = RS_A  MOVE LONG EXTENDED  */                \
  V(clcle, CLCLE, 0xA9) /* type = RS_A  COMPARE LOGICAL LONG EXTENDED  */     \
  V(sigp, SIGP, 0xAE)   /* type = RS_A  SIGNAL PROCESSOR  */                  \
  V(stctl, STCTL, 0xB6) /* type = RS_A  STORE CONTROL (32)  */                \
  V(lctl, LCTL, 0xB7)   /* type = RS_A  LOAD CONTROL (32)  */                 \
  V(cs, CS, 0xBA)       /* type = RS_A  COMPARE AND SWAP (32)  */             \
  V(cds, CDS, 0xBB)     /* type = RS_A  COMPARE DOUBLE AND SWAP (32)  */

#define S390_RS_B_OPCODE_LIST(V)                                               \
  V(clm, CLM, 0xBD) /* type = RS_B  COMPARE LOGICAL CHAR. UNDER MASK (low)  */ \
  V(stcm, STCM, 0xBE) /* type = RS_B  STORE CHARACTERS UNDER MASK (low)  */    \
  V(icm, ICM, 0xBF)   /* type = RS_B  INSERT CHARACTERS UNDER MASK (low)  */

#define S390_S_OPCODE_LIST(V)                                                  \
  V(lpsw, LPSW, 0x82)         /* type = S     LOAD PSW  */                     \
  V(diagnose, DIAGNOSE, 0x83) /* type = S     DIAGNOSE  */                     \
  V(ts, TS, 0x93)             /* type = S     TEST AND SET  */                 \
  V(stidp, STIDP, 0xB202)     /* type = S     STORE CPU ID  */                 \
  V(sck, SCK, 0xB204)         /* type = S     SET CLOCK  */                    \
  V(stck, STCK, 0xB205)       /* type = S     STORE CLOCK  */                  \
  V(sckc, SCKC, 0xB206)       /* type = S     SET CLOCK COMPARATOR  */         \
  V(stckc, STCKC, 0xB207)     /* type = S     STORE CLOCK COMPARATOR  */       \
  V(spt, SPT, 0xB208)         /* type = S     SET CPU TIMER  */                \
  V(stpt, STPT, 0xB209)       /* type = S     STORE CPU TIMER  */              \
  V(spka, SPKA, 0xB20A)       /* type = S     SET PSW KEY FROM ADDRESS  */     \
  V(ipk, IPK, 0xB20B)         /* type = S     INSERT PSW KEY  */               \
  V(ptlb, PTLB, 0xB20D)       /* type = S     PURGE TLB  */                    \
  V(spx, SPX, 0xB210)         /* type = S     SET PREFIX  */                   \
  V(stpx, STPX, 0xB211)       /* type = S     STORE PREFIX  */                 \
  V(stap, STAP, 0xB212)       /* type = S     STORE CPU ADDRESS  */            \
  V(pc, PC, 0xB218)           /* type = S     PROGRAM CALL  */                 \
  V(sac, SAC, 0xB219)         /* type = S     SET ADDRESS SPACE CONTROL  */    \
  V(cfc, CFC, 0xB21A)         /* type = S     COMPARE AND FORM CODEWORD  */    \
  V(csch, CSCH, 0xB230)       /* type = S     CLEAR SUBCHANNEL  */             \
  V(hsch, HSCH, 0xB231)       /* type = S     HALT SUBCHANNEL  */              \
  V(msch, MSCH, 0xB232)       /* type = S     MODIFY SUBCHANNEL  */            \
  V(ssch, SSCH, 0xB233)       /* type = S     START SUBCHANNEL  */             \
  V(stsch, STSCH, 0xB234)     /* type = S     STORE SUBCHANNEL  */             \
  V(tsch, TSCH, 0xB235)       /* type = S     TEST SUBCHANNEL  */              \
  V(tpi, TPI, 0xB236)         /* type = S     TEST PENDING INTERRUPTION  */    \
  V(sal, SAL, 0xB237)         /* type = S     SET ADDRESS LIMIT  */            \
  V(rsch, RSCH, 0xB238)       /* type = S     RESUME SUBCHANNEL  */            \
  V(stcrw, STCRW, 0xB239)     /* type = S     STORE CHANNEL REPORT WORD  */    \
  V(stcps, STCPS, 0xB23A)     /* type = S     STORE CHANNEL PATH STATUS  */    \
  V(rchp, RCHP, 0xB23B)       /* type = S     RESET CHANNEL PATH  */           \
  V(schm, SCHM, 0xB23C)       /* type = S     SET CHANNEL MONITOR  */          \
  V(xsch, XSCH, 0xB276)       /* type = S     CANCEL SUBCHANNEL  */            \
  V(rp, RP_Z, 0xB277)         /* type = S     RESUME PROGRAM  */               \
  V(stcke, STCKE, 0xB278)     /* type = S     STORE CLOCK EXTENDED  */         \
  V(sacf, SACF, 0xB279)     /* type = S     SET ADDRESS SPACE CONTROL FAST  */ \
  V(stckf, STCKF, 0xB27C)   /* type = S     STORE CLOCK FAST  */               \
  V(stsi, STSI, 0xB27D)     /* type = S     STORE SYSTEM INFORMATION  */       \
  V(srnm, SRNM, 0xB299)     /* type = S     SET BFP ROUNDING MODE (2 bit)  */  \
  V(stfpc, STFPC, 0xB29C)   /* type = S     STORE FPC  */                      \
  V(lfpc, LFPC, 0xB29D)     /* type = S     LOAD FPC  */                       \
  V(stfle, STFLE, 0xB2B0)   /* type = S     STORE FACILITY LIST EXTENDED  */   \
  V(stfl, STFL, 0xB2B1)     /* type = S     STORE FACILITY LIST  */            \
  V(lpswe, LPSWE, 0xB2B2)   /* type = S     LOAD PSW EXTENDED  */              \
  V(srnmb, SRNMB, 0xB2B8)   /* type = S     SET BFP ROUNDING MODE (3 bit)  */  \
  V(srnmt, SRNMT, 0xB2B9)   /* type = S     SET DFP ROUNDING MODE  */          \
  V(lfas, LFAS, 0xB2BD)     /* type = S     LOAD FPC AND SIGNAL  */            \
  V(tend, TEND, 0xB2F8)     /* type = S     TRANSACTION END  */                \
  V(tabort, TABORT, 0xB2FC) /* type = S     TRANSACTION ABORT  */              \
  V(trap4, TRAP4, 0xB2FF)   /* type = S     TRAP  */

#define S390_RX_A_OPCODE_LIST(V)                                            \
  V(la, LA, 0x41)     /* type = RX_A  LOAD ADDRESS  */                      \
  V(stc, STC, 0x42)   /* type = RX_A  STORE CHARACTER  */                   \
  V(ic_z, IC_z, 0x43) /* type = RX_A  INSERT CHARACTER  */                  \
  V(ex, EX, 0x44)     /* type = RX_A  EXECUTE  */                           \
  V(bal, BAL, 0x45)   /* type = RX_A  BRANCH AND LINK  */                   \
  V(bct, BCT, 0x46)   /* type = RX_A  BRANCH ON COUNT (32)  */              \
  V(lh, LH, 0x48)     /* type = RX_A  LOAD HALFWORD (32<-16)  */            \
  V(ch, CH, 0x49)     /* type = RX_A  COMPARE HALFWORD (32<-16)  */         \
  V(ah, AH, 0x4A)     /* type = RX_A  ADD HALFWORD (32<-16)  */             \
  V(sh, SH, 0x4B)     /* type = RX_A  SUBTRACT HALFWORD (32<-16)  */        \
  V(mh, MH, 0x4C)     /* type = RX_A  MULTIPLY HALFWORD (32<-16)  */        \
  V(bas, BAS, 0x4D)   /* type = RX_A  BRANCH AND SAVE  */                   \
  V(cvd, CVD, 0x4E)   /* type = RX_A  CONVERT TO DECIMAL (32)  */           \
  V(cvb, CVB, 0x4F)   /* type = RX_A  CONVERT TO BINARY (32)  */            \
  V(st, ST, 0x50)     /* type = RX_A  STORE (32)  */                        \
  V(lae, LAE, 0x51)   /* type = RX_A  LOAD ADDRESS EXTENDED  */             \
  V(n, N, 0x54)       /* type = RX_A  AND (32)  */                          \
  V(cl, CL, 0x55)     /* type = RX_A  COMPARE LOGICAL (32)  */              \
  V(o, O, 0x56)       /* type = RX_A  OR (32)  */                           \
  V(x, X, 0x57)       /* type = RX_A  EXCLUSIVE OR (32)  */                 \
  V(l, L, 0x58)       /* type = RX_A  LOAD (32)  */                         \
  V(c, C, 0x59)       /* type = RX_A  COMPARE (32)  */                      \
  V(a, A, 0x5A)       /* type = RX_A  ADD (32)  */                          \
  V(s, S, 0x5B)       /* type = RX_A  SUBTRACT (32)  */                     \
  V(m, M, 0x5C)       /* type = RX_A  MULTIPLY (64<-32)  */                 \
  V(d, D, 0x5D)       /* type = RX_A  DIVIDE (32<-64)  */                   \
  V(al_z, AL, 0x5E)   /* type = RX_A  ADD LOGICAL (32)  */                  \
  V(sl, SL, 0x5F)     /* type = RX_A  SUBTRACT LOGICAL (32)  */             \
  V(std, STD, 0x60)   /* type = RX_A  STORE (long)  */                      \
  V(mxd, MXD, 0x67)   /* type = RX_A  MULTIPLY (long to extended HFP)  */   \
  V(ld, LD, 0x68)     /* type = RX_A  LOAD (long)  */                       \
  V(cd, CD, 0x69)     /* type = RX_A  COMPARE (long HFP)  */                \
  V(ad, AD, 0x6A)     /* type = RX_A  ADD NORMALIZED (long HFP)  */         \
  V(sd, SD, 0x6B)     /* type = RX_A  SUBTRACT NORMALIZED (long HFP)  */    \
  V(md, MD, 0x6C)     /* type = RX_A  MULTIPLY (long HFP)  */               \
  V(dd, DD, 0x6D)     /* type = RX_A  DIVIDE (long HFP)  */                 \
  V(aw, AW, 0x6E)     /* type = RX_A  ADD UNNORMALIZED (long HFP)  */       \
  V(sw, SW, 0x6F)     /* type = RX_A  SUBTRACT UNNORMALIZED (long HFP)  */  \
  V(ste, STE, 0x70)   /* type = RX_A  STORE (short)  */                     \
  V(ms, MS, 0x71)     /* type = RX_A  MULTIPLY SINGLE (32)  */              \
  V(le_z, LE, 0x78)   /* type = RX_A  LOAD (short)  */                      \
  V(ce, CE, 0x79)     /* type = RX_A  COMPARE (short HFP)  */               \
  V(ae, AE, 0x7A)     /* type = RX_A  ADD NORMALIZED (short HFP)  */        \
  V(se, SE, 0x7B)     /* type = RX_A  SUBTRACT NORMALIZED (short HFP)  */   \
  V(mde, MDE, 0x7C)   /* type = RX_A  MULTIPLY (short to long HFP)  */      \
  V(de, DE, 0x7D)     /* type = RX_A  DIVIDE (short HFP)  */                \
  V(au, AU, 0x7E)     /* type = RX_A  ADD UNNORMALIZED (short HFP)  */      \
  V(su, SU, 0x7F)     /* type = RX_A  SUBTRACT UNNORMALIZED (short HFP)  */ \
  V(ssm, SSM, 0x80)   /* type = RX_A  SET SYSTEM MASK  */                   \
  V(lra, LRA, 0xB1)   /* type = RX_A  LOAD REAL ADDRESS (32)  */            \
  V(sth, STH, 0x40)   /* type = RX_A  STORE HALFWORD (16)  */

#define S390_RX_B_OPCODE_LIST(V) \
  V(bc, BC, 0x47) /* type = RX_B  BRANCH ON CONDITION  */

#define S390_RIE_A_OPCODE_LIST(V)                                              \
  V(cgit, CGIT, 0xEC70) /* type = RIE_A COMPARE IMMEDIATE AND TRAP (64<-16) */ \
  V(clgit, CLGIT,                                                              \
    0xEC71) /* type = RIE_A COMPARE LOGICAL IMMEDIATE AND TRAP (64<-16)  */    \
  V(cit, CIT, 0xEC72) /* type = RIE_A COMPARE IMMEDIATE AND TRAP (32<-16)  */  \
  V(clfit, CLFIT,                                                              \
    0xEC73) /* type = RIE_A COMPARE LOGICAL IMMEDIATE AND TRAP (32<-16)  */

#define S390_RRD_OPCODE_LIST(V)                                                \
  V(maebr, MAEBR, 0xB30E) /* type = RRD   MULTIPLY AND ADD (short BFP)  */     \
  V(msebr, MSEBR, 0xB30F) /* type = RRD   MULTIPLY AND SUBTRACT (short BFP) */ \
  V(madbr, MADBR, 0xB31E) /* type = RRD   MULTIPLY AND ADD (long BFP)  */      \
  V(msdbr, MSDBR, 0xB31F) /* type = RRD   MULTIPLY AND SUBTRACT (long BFP)  */ \
  V(maer, MAER, 0xB32E)   /* type = RRD   MULTIPLY AND ADD (short HFP)  */     \
  V(mser, MSER, 0xB32F) /* type = RRD   MULTIPLY AND SUBTRACT (short HFP)  */  \
  V(maylr, MAYLR,                                                              \
    0xB338) /* type = RRD   MULTIPLY AND ADD UNNRM. (long to ext. low HFP)  */ \
  V(mylr, MYLR,                                                                \
    0xB339) /* type = RRD   MULTIPLY UNNORM. (long to ext. low HFP)  */        \
  V(mayr, MAYR,                                                                \
    0xB33A) /* type = RRD   MULTIPLY & ADD UNNORMALIZED (long to ext. HFP)  */ \
  V(myr, MYR,                                                                  \
    0xB33B) /* type = RRD   MULTIPLY UNNORMALIZED (long to ext. HFP)  */       \
  V(mayhr, MAYHR,                                                              \
    0xB33C) /* type = RRD   MULTIPLY AND ADD UNNRM. (long to ext. high HFP) */ \
  V(myhr, MYHR,                                                                \
    0xB33D) /* type = RRD   MULTIPLY UNNORM. (long to ext. high HFP)  */       \
  V(madr, MADR, 0xB33E) /* type = RRD   MULTIPLY AND ADD (long HFP)  */        \
  V(msdr, MSDR, 0xB33F) /* type = RRD   MULTIPLY AND SUBTRACT (long HFP)  */

#define S390_RIE_B_OPCODE_LIST(V)                                            \
  V(cgrj, CGRJ, 0xEC64) /* type = RIE_B COMPARE AND BRANCH RELATIVE (64)  */ \
  V(clgrj, CLGRJ,                                                            \
    0xEC65) /* type = RIE_B COMPARE LOGICAL AND BRANCH RELATIVE (64)  */     \
  V(crj, CRJ, 0xEC76) /* type = RIE_B COMPARE AND BRANCH RELATIVE (32)  */   \
  V(clrj, CLRJ,                                                              \
    0xEC77) /* type = RIE_B COMPARE LOGICAL AND BRANCH RELATIVE (32)  */

#define S390_RRE_OPCODE_LIST(V)                                                \
  V(ipm, IPM, 0xB222)     /* type = RRE   INSERT PROGRAM MASK  */              \
  V(ivsk, IVSK, 0xB223)   /* type = RRE   INSERT VIRTUAL STORAGE KEY  */       \
  V(iac, IAC, 0xB224)     /* type = RRE   INSERT ADDRESS SPACE CONTROL  */     \
  V(ssar, SSAR, 0xB225)   /* type = RRE   SET SECONDARY ASN  */                \
  V(epar, EPAR, 0xB226)   /* type = RRE   EXTRACT PRIMARY ASN  */              \
  V(esar, ESAR, 0xB227)   /* type = RRE   EXTRACT SECONDARY ASN  */            \
  V(pt, PT, 0xB228)       /* type = RRE   PROGRAM TRANSFER  */                 \
  V(iske, ISKE, 0xB229)   /* type = RRE   INSERT STORAGE KEY EXTENDED  */      \
  V(rrbe, RRBE, 0xB22A)   /* type = RRE   RESET REFERENCE BIT EXTENDED  */     \
  V(tb, TB_, 0xB22C)      /* type = RRE   TEST BLOCK  */                       \
  V(dxr, DXR, 0xB22D)     /* type = RRE   DIVIDE (extended HFP)  */            \
  V(pgin, PGIN, 0xB22E)   /* type = RRE   PAGE IN  */                          \
  V(pgout, PGOUT, 0xB22F) /* type = RRE   PAGE OUT  */                         \
  V(bakr, BAKR, 0xB240)   /* type = RRE   BRANCH AND STACK  */                 \
  V(cksm, CKSM, 0xB241)   /* type = RRE   CHECKSUM  */                         \
  V(sqdr, SQDR, 0xB244)   /* type = RRE   SQUARE ROOT (long HFP)  */           \
  V(sqer, SQER, 0xB245)   /* type = RRE   SQUARE ROOT (short HFP)  */          \
  V(stura, STURA, 0xB246) /* type = RRE   STORE USING REAL ADDRESS (32)  */    \
  V(msta, MSTA, 0xB247)   /* type = RRE   MODIFY STACKED STATE  */             \
  V(palb, PALB, 0xB248)   /* type = RRE   PURGE ALB  */                        \
  V(ereg, EREG, 0xB249)   /* type = RRE   EXTRACT STACKED REGISTERS (32)  */   \
  V(esta, ESTA, 0xB24A)   /* type = RRE   EXTRACT STACKED STATE  */            \
  V(lura, LURA, 0xB24B)   /* type = RRE   LOAD USING REAL ADDRESS (32)  */     \
  V(tar, TAR, 0xB24C)     /* type = RRE   TEST ACCESS  */                      \
  V(cpya, CPYA, 0xB24D)   /* type = RRE   COPY ACCESS  */                      \
  V(sar, SAR, 0xB24E)     /* type = RRE   SET ACCESS  */                       \
  V(ear, EAR, 0xB24F)     /* type = RRE   EXTRACT ACCESS  */                   \
  V(csp, CSP, 0xB250)     /* type = RRE   COMPARE AND SWAP AND PURGE (32)  */  \
  V(msr, MSR, 0xB252)     /* type = RRE   MULTIPLY SINGLE (32)  */             \
  V(mvpg, MVPG, 0xB254)   /* type = RRE   MOVE PAGE  */                        \
  V(mvst, MVST, 0xB255)   /* type = RRE   MOVE STRING  */                      \
  V(cuse, CUSE, 0xB257)   /* type = RRE   COMPARE UNTIL SUBSTRING EQUAL  */    \
  V(bsg, BSG, 0xB258)     /* type = RRE   BRANCH IN SUBSPACE GROUP  */         \
  V(bsa, BSA, 0xB25A)     /* type = RRE   BRANCH AND SET AUTHORITY  */         \
  V(clst, CLST, 0xB25D)   /* type = RRE   COMPARE LOGICAL STRING  */           \
  V(srst, SRST, 0xB25E)   /* type = RRE   SEARCH STRING  */                    \
  V(cmpsc, CMPSC, 0xB263) /* type = RRE   COMPRESSION CALL  */                 \
  V(tre, TRE, 0xB2A5)     /* type = RRE   TRANSLATE EXTENDED  */               \
  V(etnd, ETND, 0xB2EC) /* type = RRE   EXTRACT TRANSACTION NESTING DEPTH  */  \
  V(lpebr, LPEBR, 0xB300) /* type = RRE   LOAD POSITIVE (short BFP)  */        \
  V(lnebr, LNEBR, 0xB301) /* type = RRE   LOAD NEGATIVE (short BFP)  */        \
  V(ltebr, LTEBR, 0xB302) /* type = RRE   LOAD AND TEST (short BFP)  */        \
  V(lcebr, LCEBR, 0xB303) /* type = RRE   LOAD COMPLEMENT (short BFP)  */      \
  V(ldebr, LDEBR,                                                              \
    0xB304) /* type = RRE   LOAD LENGTHENED (short to long BFP)  */            \
  V(lxdbr, LXDBR,                                                              \
    0xB305) /* type = RRE   LOAD LENGTHENED (long to extended BFP)  */         \
  V(lxebr, LXEBR,                                                              \
    0xB306) /* type = RRE   LOAD LENGTHENED (short to extended BFP)  */        \
  V(mxdbr, MXDBR, 0xB307) /* type = RRE   MULTIPLY (long to extended BFP)  */  \
  V(kebr, KEBR, 0xB308)   /* type = RRE   COMPARE AND SIGNAL (short BFP)  */   \
  V(cebr, CEBR, 0xB309)   /* type = RRE   COMPARE (short BFP)  */              \
  V(aebr, AEBR, 0xB30A)   /* type = RRE   ADD (short BFP)  */                  \
  V(sebr, SEBR, 0xB30B)   /* type = RRE   SUBTRACT (short BFP)  */             \
  V(mdebr, MDEBR, 0xB30C) /* type = RRE   MULTIPLY (short to long BFP)  */     \
  V(debr, DEBR, 0xB30D)   /* type = RRE   DIVIDE (short BFP)  */               \
  V(lpdbr, LPDBR, 0xB310) /* type = RRE   LOAD POSITIVE (long BFP)  */         \
  V(lndbr, LNDBR, 0xB311) /* type = RRE   LOAD NEGATIVE (long BFP)  */         \
  V(ltdbr, LTDBR, 0xB312) /* type = RRE   LOAD AND TEST (long BFP)  */         \
  V(lcdbr, LCDBR, 0xB313) /* type = RRE   LOAD COMPLEMENT (long BFP)  */       \
  V(sqebr, SQEBR, 0xB314) /* type = RRE   SQUARE ROOT (short BFP)  */          \
  V(sqdbr, SQDBR, 0xB315) /* type = RRE   SQUARE ROOT (long BFP)  */           \
  V(sqxbr, SQXBR, 0xB316) /* type = RRE   SQUARE ROOT (extended BFP)  */       \
  V(meebr, MEEBR, 0xB317) /* type = RRE   MULTIPLY (short BFP)  */             \
  V(kdbr, KDBR, 0xB318)   /* type = RRE   COMPARE AND SIGNAL (long BFP)  */    \
  V(cdbr, CDBR, 0xB319)   /* type = RRE   COMPARE (long BFP)  */               \
  V(adbr, ADBR, 0xB31A)   /* type = RRE   ADD (long BFP)  */                   \
  V(sdbr, SDBR, 0xB31B)   /* type = RRE   SUBTRACT (long BFP)  */              \
  V(mdbr, MDBR, 0xB31C)   /* type = RRE   MULTIPLY (long BFP)  */              \
  V(ddbr, DDBR, 0xB31D)   /* type = RRE   DIVIDE (long BFP)  */                \
  V(lder, LDER, 0xB324) /* type = RRE   LOAD LENGTHENED (short to long HFP) */ \
  V(lxdr, LXDR,                                                                \
    0xB325) /* type = RRE   LOAD LENGTHENED (long to extended HFP)  */         \
  V(lxer, LXER,                                                                \
    0xB326) /* type = RRE   LOAD LENGTHENED (short to extended HFP)  */        \
  V(sqxr, SQXR, 0xB336)   /* type = RRE   SQUARE ROOT (extended HFP)  */       \
  V(meer, MEER, 0xB337)   /* type = RRE   MULTIPLY (short HFP)  */             \
  V(lpxbr, LPXBR, 0xB340) /* type = RRE   LOAD POSITIVE (extended BFP)  */     \
  V(lnxbr, LNXBR, 0xB341) /* type = RRE   LOAD NEGATIVE (extended BFP)  */     \
  V(ltxbr, LTXBR, 0xB342) /* type = RRE   LOAD AND TEST (extended BFP)  */     \
  V(lcxbr, LCXBR, 0xB343) /* type = RRE   LOAD COMPLEMENT (extended BFP)  */   \
  V(kxbr, KXBR, 0xB348) /* type = RRE   COMPARE AND SIGNAL (extended BFP)  */  \
  V(cxbr, CXBR, 0xB349) /* type = RRE   COMPARE (extended BFP)  */             \
  V(axbr, AXBR, 0xB34A) /* type = RRE   ADD (extended BFP)  */                 \
  V(sxbr, SXBR, 0xB34B) /* type = RRE   SUBTRACT (extended BFP)  */            \
  V(mxbr, MXBR, 0xB34C) /* type = RRE   MULTIPLY (extended BFP)  */            \
  V(dxbr, DXBR, 0xB34D) /* type = RRE   DIVIDE (extended BFP)  */              \
  V(thder, THDER,                                                              \
    0xB358)             /* type = RRE   CONVERT BFP TO HFP (short to long)  */ \
  V(thdr, THDR, 0xB359) /* type = RRE   CONVERT BFP TO HFP (long)  */          \
  V(lpxr, LPXR, 0xB360) /* type = RRE   LOAD POSITIVE (extended HFP)  */       \
  V(lnxr, LNXR, 0xB361) /* type = RRE   LOAD NEGATIVE (extended HFP)  */       \
  V(ltxr, LTXR, 0xB362) /* type = RRE   LOAD AND TEST (extended HFP)  */       \
  V(lcxr, LCXR, 0xB363) /* type = RRE   LOAD COMPLEMENT (extended HFP)  */     \
  V(lxr, LXR, 0xB365)   /* type = RRE   LOAD (extended)  */                    \
  V(lexr, LEXR,                                                                \
    0xB366) /* type = RRE   LOAD ROUNDED (extended to short HFP)  */           \
  V(fixr, FIXR, 0xB367)   /* type = RRE   LOAD FP INTEGER (extended HFP)  */   \
  V(cxr, CXR, 0xB369)     /* type = RRE   COMPARE (extended HFP)  */           \
  V(lpdfr, LPDFR, 0xB370) /* type = RRE   LOAD POSITIVE (long)  */             \
  V(lndfr, LNDFR, 0xB371) /* type = RRE   LOAD NEGATIVE (long)  */             \
  V(lcdfr, LCDFR, 0xB373) /* type = RRE   LOAD COMPLEMENT (long)  */           \
  V(lzer, LZER, 0xB374)   /* type = RRE   LOAD ZERO (short)  */                \
  V(lzdr, LZDR, 0xB375)   /* type = RRE   LOAD ZERO (long)  */                 \
  V(lzxr, LZXR, 0xB376)   /* type = RRE   LOAD ZERO (extended)  */             \
  V(fier, FIER, 0xB377)   /* type = RRE   LOAD FP INTEGER (short HFP)  */      \
  V(fidr, FIDR, 0xB37F)   /* type = RRE   LOAD FP INTEGER (long HFP)  */       \
  V(sfpc, SFPC, 0xB384)   /* type = RRE   SET FPC  */                          \
  V(sfasr, SFASR, 0xB385) /* type = RRE   SET FPC AND SIGNAL  */               \
  V(efpc, EFPC, 0xB38C)   /* type = RRE   EXTRACT FPC  */                      \
  V(cefr, CEFR,                                                                \
    0xB3B4) /* type = RRE   CONVERT FROM FIXED (32 to short HFP)  */           \
  V(cdfr, CDFR, 0xB3B5) /* type = RRE   CONVERT FROM FIXED (32 to long HFP) */ \
  V(cxfr, CXFR,                                                                \
    0xB3B6) /* type = RRE   CONVERT FROM FIXED (32 to extended HFP)  */        \
  V(ldgr, LDGR, 0xB3C1) /* type = RRE   LOAD FPR FROM GR (64 to long)  */      \
  V(cegr, CEGR,                                                                \
    0xB3C4) /* type = RRE   CONVERT FROM FIXED (64 to short HFP)  */           \
  V(cdgr, CDGR, 0xB3C5) /* type = RRE   CONVERT FROM FIXED (64 to long HFP) */ \
  V(cxgr, CXGR,                                                                \
    0xB3C6) /* type = RRE   CONVERT FROM FIXED (64 to extended HFP)  */        \
  V(lgdr, LGDR, 0xB3CD)   /* type = RRE   LOAD GR FROM FPR (long to 64)  */    \
  V(ltdtr, LTDTR, 0xB3D6) /* type = RRE   LOAD AND TEST (long DFP)  */         \
  V(ltxtr, LTXTR, 0xB3DE) /* type = RRE   LOAD AND TEST (extended DFP)  */     \
  V(kdtr, KDTR, 0xB3E0)   /* type = RRE   COMPARE AND SIGNAL (long DFP)  */    \
  V(cudtr, CUDTR, 0xB3E2) /* type = RRE   CONVERT TO UNSIGNED PACKED (long */  \
                          /* DFP to 64) CUDTR  */                              \
  V(cdtr, CDTR, 0xB3E4)   /* type = RRE   COMPARE (long DFP)  */               \
  V(eedtr, EEDTR,                                                              \
    0xB3E5) /* type = RRE   EXTRACT BIASED EXPONENT (long DFP to 64)  */       \
  V(esdtr, ESDTR,                                                              \
    0xB3E7) /* type = RRE   EXTRACT SIGNIFICANCE (long DFP to 64)  */          \
  V(kxtr, KXTR, 0xB3E8) /* type = RRE   COMPARE AND SIGNAL (extended DFP)  */  \
  V(cuxtr, CUXTR,                                                              \
    0xB3EA) /* type = RRE   CONVERT TO UNSIGNED PACKED (extended DFP       */  \
            /* CUXTR to 128)  */                                               \
  V(cxtr, CXTR, 0xB3EC) /* type = RRE   COMPARE (extended DFP)  */             \
  V(eextr, EEXTR,                                                              \
    0xB3ED) /* type = RRE   EXTRACT BIASED EXPONENT (extended DFP to 64)  */   \
  V(esxtr, ESXTR,                                                              \
    0xB3EF) /* type = RRE   EXTRACT SIGNIFICANCE (extended DFP to 64)  */      \
  V(cdutr, CDUTR,                                                              \
    0xB3F2) /* type = RRE   CONVERT FROM UNSIGNED PACKED (64 to long DFP)  */  \
  V(cdstr, CDSTR,                                                              \
    0xB3F3) /* type = RRE   CONVERT FROM SIGNED PACKED (64 to long DFP)  */    \
  V(cedtr, CEDTR,                                                              \
    0xB3F4) /* type = RRE   COMPARE BIASED EXPONENT (long DFP)  */             \
  V(cxutr, CXUTR,                                                              \
    0xB3FA) /* type = RRE   CONVERT FROM UNSIGNED PACKED (128 to ext. DFP)  */ \
  V(cxstr, CXSTR, 0xB3FB) /* type = RRE   CONVERT FROM SIGNED PACKED (128 to*/ \
                          /* extended DFP)  */                                 \
  V(cextr, CEXTR,                                                              \
    0xB3FC) /* type = RRE   COMPARE BIASED EXPONENT (extended DFP)  */         \
  V(lpgr, LPGR, 0xB900)   /* type = RRE   LOAD POSITIVE (64)  */               \
  V(lngr, LNGR, 0xB901)   /* type = RRE   LOAD NEGATIVE (64)  */               \
  V(ltgr, LTGR, 0xB902)   /* type = RRE   LOAD AND TEST (64)  */               \
  V(lcgr, LCGR, 0xB903)   /* type = RRE   LOAD COMPLEMENT (64)  */             \
  V(lgr, LGR, 0xB904)     /* type = RRE   LOAD (64)  */                        \
  V(lurag, LURAG, 0xB905) /* type = RRE   LOAD USING REAL ADDRESS (64)  */     \
  V(lgbr, LGBR, 0xB906)   /* type = RRE   LOAD BYTE (64<-8)  */                \
  V(lghr, LGHR, 0xB907)   /* type = RRE   LOAD HALFWORD (64<-16)  */           \
  V(agr, AGR, 0xB908)     /* type = RRE   ADD (64)  */                         \
  V(sgr, SGR, 0xB909)     /* type = RRE   SUBTRACT (64)  */                    \
  V(algr, ALGR, 0xB90A)   /* type = RRE   ADD LOGICAL (64)  */                 \
  V(slgr, SLGR, 0xB90B)   /* type = RRE   SUBTRACT LOGICAL (64)  */            \
  V(msgr, MSGR, 0xB90C)   /* type = RRE   MULTIPLY SINGLE (64)  */             \
  V(dsgr, DSGR, 0xB90D)   /* type = RRE   DIVIDE SINGLE (64)  */               \
  V(eregg, EREGG, 0xB90E) /* type = RRE   EXTRACT STACKED REGISTERS (64)  */   \
  V(lrvgr, LRVGR, 0xB90F) /* type = RRE   LOAD REVERSED (64)  */               \
  V(lpgfr, LPGFR, 0xB910) /* type = RRE   LOAD POSITIVE (64<-32)  */           \
  V(lngfr, LNGFR, 0xB911) /* type = RRE   LOAD NEGATIVE (64<-32)  */           \
  V(ltgfr, LTGFR, 0xB912) /* type = RRE   LOAD AND TEST (64<-32)  */           \
  V(lcgfr, LCGFR, 0xB913) /* type = RRE   LOAD COMPLEMENT (64<-32)  */         \
  V(lgfr, LGFR, 0xB914)   /* type = RRE   LOAD (64<-32)  */                    \
  V(llgfr, LLGFR, 0xB916) /* type = RRE   LOAD LOGICAL (64<-32)  */            \
  V(llgtr, LLGTR,                                                              \
    0xB917) /* type = RRE   LOAD LOGICAL THIRTY ONE BITS (64<-31)  */          \
  V(agfr, AGFR, 0xB918)   /* type = RRE   ADD (64<-32)  */                     \
  V(sgfr, SGFR, 0xB919)   /* type = RRE   SUBTRACT (64<-32)  */                \
  V(algfr, ALGFR, 0xB91A) /* type = RRE   ADD LOGICAL (64<-32)  */             \
  V(slgfr, SLGFR, 0xB91B) /* type = RRE   SUBTRACT LOGICAL (64<-32)  */        \
  V(msgfr, MSGFR, 0xB91C) /* type = RRE   MULTIPLY SINGLE (64<-32)  */         \
  V(dsgfr, DSGFR, 0xB91D) /* type = RRE   DIVIDE SINGLE (64<-32)  */           \
  V(kmac, KMAC, 0xB91E) /* type = RRE   COMPUTE MESSAGE AUTHENTICATION CODE */ \
  V(lrvr, LRVR, 0xB91F) /* type = RRE   LOAD REVERSED (32)  */                 \
  V(cgr, CGR, 0xB920)   /* type = RRE   COMPARE (64)  */                       \
  V(clgr, CLGR, 0xB921) /* type = RRE   COMPARE LOGICAL (64)  */               \
  V(sturg, STURG, 0xB925) /* type = RRE   STORE USING REAL ADDRESS (64)  */    \
  V(lbr, LBR, 0xB926)     /* type = RRE   LOAD BYTE (32<-8)  */                \
  V(lhr, LHR, 0xB927)     /* type = RRE   LOAD HALFWORD (32<-16)  */           \
  V(pckmo, PCKMO,                                                              \
    0xB928) /* type = RRE   PERFORM CRYPTOGRAPHIC KEY MGMT. OPERATIONS  */     \
  V(kmf, KMF, 0xB92A) /* type = RRE   CIPHER MESSAGE WITH CIPHER FEEDBACK  */  \
  V(kmo, KMO, 0xB92B) /* type = RRE   CIPHER MESSAGE WITH OUTPUT FEEDBACK  */  \
  V(pcc, PCC, 0xB92C) /* type = RRE   PERFORM CRYPTOGRAPHIC COMPUTATION  */    \
  V(km, KM, 0xB92E)   /* type = RRE   CIPHER MESSAGE  */                       \
  V(kmc, KMC, 0xB92F) /* type = RRE   CIPHER MESSAGE WITH CHAINING  */         \
  V(cgfr, CGFR, 0xB930)   /* type = RRE   COMPARE (64<-32)  */                 \
  V(clgfr, CLGFR, 0xB931) /* type = RRE   COMPARE LOGICAL (64<-32)  */         \
  V(ppno, PPNO,                                                                \
    0xB93C) /* type = RRE   PERFORM PSEUDORANDOM NUMBER OPERATION  */          \
  V(kimd, KIMD, 0xB93E) /* type = RRE   COMPUTE INTERMEDIATE MESSAGE DIGEST */ \
  V(klmd, KLMD, 0xB93F) /* type = RRE   COMPUTE LAST MESSAGE DIGEST  */        \
  V(bctgr, BCTGR, 0xB946) /* type = RRE   BRANCH ON COUNT (64)  */             \
  V(cdftr, CDFTR,                                                              \
    0xB951) /* type = RRE   CONVERT FROM FIXED (32 to long DFP)  */            \
  V(cxftr, CXFTR,                                                              \
    0xB959) /* type = RRE   CONVERT FROM FIXED (32 to extended DFP)  */        \
  V(ngr, NGR, 0xB980)     /* type = RRE   AND (64)  */                         \
  V(ogr, OGR, 0xB981)     /* type = RRE   OR (64)  */                          \
  V(xgr, XGR, 0xB982)     /* type = RRE   EXCLUSIVE OR (64)  */                \
  V(flogr, FLOGR, 0xB983) /* type = RRE   FIND LEFTMOST ONE  */                \
  V(llgcr, LLGCR, 0xB984) /* type = RRE   LOAD LOGICAL CHARACTER (64<-8)  */   \
  V(llghr, LLGHR, 0xB985) /* type = RRE   LOAD LOGICAL HALFWORD (64<-16)  */   \
  V(mlgr, MLGR, 0xB986)   /* type = RRE   MULTIPLY LOGICAL (128<-64)  */       \
  V(dlgr, DLGR, 0xB987)   /* type = RRE   DIVIDE LOGICAL (64<-128)  */         \
  V(alcgr, ALCGR, 0xB988) /* type = RRE   ADD LOGICAL WITH CARRY (64)  */      \
  V(slbgr, SLBGR, 0xB989) /* type = RRE   SUBTRACT LOGICAL WITH BORROW (64) */ \
  V(cspg, CSPG, 0xB98A)   /* type = RRE   COMPARE AND SWAP AND PURGE (64)  */  \
  V(epsw, EPSW, 0xB98D)   /* type = RRE   EXTRACT PSW  */                      \
  V(llcr, LLCR, 0xB994)   /* type = RRE   LOAD LOGICAL CHARACTER (32<-8)  */   \
  V(llhr, LLHR, 0xB995)   /* type = RRE   LOAD LOGICAL HALFWORD (32<-16)  */   \
  V(mlr, MLR, 0xB996)     /* type = RRE   MULTIPLY LOGICAL (64<-32)  */        \
  V(dlr, DLR, 0xB997)     /* type = RRE   DIVIDE LOGICAL (32<-64)  */          \
  V(alcr, ALCR, 0xB998)   /* type = RRE   ADD LOGICAL WITH CARRY (32)  */      \
  V(slbr, SLBR, 0xB999) /* type = RRE   SUBTRACT LOGICAL WITH BORROW (32)  */  \
  V(epair, EPAIR, 0xB99A) /* type = RRE   EXTRACT PRIMARY ASN AND INSTANCE  */ \
  V(esair, ESAIR,                                                              \
    0xB99B)             /* type = RRE   EXTRACT SECONDARY ASN AND INSTANCE  */ \
  V(esea, ESEA, 0xB99D) /* type = RRE   EXTRACT AND SET EXTENDED AUTHORITY  */ \
  V(pti, PTI, 0xB99E)   /* type = RRE   PROGRAM TRANSFER WITH INSTANCE  */     \
  V(ssair, SSAIR, 0xB99F) /* type = RRE   SET SECONDARY ASN WITH INSTANCE  */  \
  V(ptf, PTF, 0xB9A2)     /* type = RRE   PERFORM TOPOLOGY FUNCTION  */        \
  V(rrbm, RRBM, 0xB9AE)   /* type = RRE   RESET REFERENCE BITS MULTIPLE  */    \
  V(pfmf, PFMF, 0xB9AF) /* type = RRE   PERFORM FRAME MANAGEMENT FUNCTION  */  \
  V(cu41, CU41, 0xB9B2) /* type = RRE   CONVERT UTF-32 TO UTF-8  */            \
  V(cu42, CU42, 0xB9B3) /* type = RRE   CONVERT UTF-32 TO UTF-16  */           \
  V(srstu, SRSTU, 0xB9BE)     /* type = RRE   SEARCH STRING UNICODE  */        \
  V(chhr, CHHR, 0xB9CD)       /* type = RRE   COMPARE HIGH (32)  */            \
  V(clhhr, CLHHR, 0xB9CF)     /* type = RRE   COMPARE LOGICAL HIGH (32)  */    \
  V(chlr, CHLR, 0xB9DD)       /* type = RRE   COMPARE HIGH (32)  */            \
  V(clhlr, CLHLR, 0xB9DF)     /* type = RRE   COMPARE LOGICAL HIGH (32)  */    \
  V(popcnt, POPCNT_Z, 0xB9E1) /* type = RRE   POPULATION COUNT  */

#define S390_RIE_C_OPCODE_LIST(V)                                             \
  V(cgij, CGIJ,                                                               \
    0xEC7C) /* type = RIE_C COMPARE IMMEDIATE AND BRANCH RELATIVE (64<-8)  */ \
  V(clgij, CLGIJ,                                                             \
    0xEC7D) /* type = RIE_C COMPARE LOGICAL IMMEDIATE AND BRANCH RELATIVE  */ \
            /* (64<-8)  */                                                    \
  V(cij, CIJ,                                                                 \
    0xEC7E) /* type = RIE_C COMPARE IMMEDIATE AND BRANCH RELATIVE (32<-8)  */ \
  V(clij, CLIJ, 0xEC7F) /* type = RIE_C COMPARE LOGICAL IMMEDIATE AND      */ \
                        /* BRANCH RELATIVE (32<-8)  */

#define S390_RIE_D_OPCODE_LIST(V)                                          \
  V(ahik, AHIK, 0xECD8)   /* type = RIE_D ADD IMMEDIATE (32<-16)  */       \
  V(aghik, AGHIK, 0xECD9) /* type = RIE_D ADD IMMEDIATE (64<-16)  */       \
  V(alhsik, ALHSIK,                                                        \
    0xECDA) /* type = RIE_D ADD LOGICAL WITH SIGNED IMMEDIATE (32<-16)  */ \
  V(alghsik, ALGHSIK,                                                      \
    0xECDB) /* type = RIE_D ADD LOGICAL WITH SIGNED IMMEDIATE (64<-16)  */

#define S390_VRV_OPCODE_LIST(V)                                           \
  V(vgeg, VGEG, 0xE712)   /* type = VRV   VECTOR GATHER ELEMENT (64)  */  \
  V(vgef, VGEF, 0xE713)   /* type = VRV   VECTOR GATHER ELEMENT (32)  */  \
  V(vsceg, VSCEG, 0xE71A) /* type = VRV   VECTOR SCATTER ELEMENT (64)  */ \
  V(vscef, VSCEF, 0xE71B) /* type = VRV   VECTOR SCATTER ELEMENT (32)  */

#define S390_RIE_E_OPCODE_LIST(V)                                  \
  V(brxhg, BRXHG,                                                  \
    0xEC44) /* type = RIE_E BRANCH RELATIVE ON INDEX HIGH (64)  */ \
  V(brxlg, BRXLG,                                                  \
    0xEC45) /* type = RIE_E BRANCH RELATIVE ON INDEX LOW OR EQ. (64)  */

#define S390_RR_OPCODE_LIST(V)                                                 \
  V(awr, AWR, 0x2E)     /* type = RR    ADD UNNORMALIZED (long HFP)  */        \
  V(spm, SPM, 0x04)     /* type = RR    SET PROGRAM MASK  */                   \
  V(balr, BALR, 0x05)   /* type = RR    BRANCH AND LINK  */                    \
  V(bctr, BCTR, 0x06)   /* type = RR    BRANCH ON COUNT (32)  */               \
  V(bcr, BCR, 0x07)     /* type = RR    BRANCH ON CONDITION  */                \
  V(bsm, BSM, 0x0B)     /* type = RR    BRANCH AND SET MODE  */                \
  V(bassm, BASSM, 0x0C) /* type = RR    BRANCH AND SAVE AND SET MODE  */       \
  V(basr, BASR, 0x0D)   /* type = RR    BRANCH AND SAVE  */                    \
  V(mvcl, MVCL, 0x0E)   /* type = RR    MOVE LONG  */                          \
  V(clcl, CLCL, 0x0F)   /* type = RR    COMPARE LOGICAL LONG  */               \
  V(lpr, LPR, 0x10)     /* type = RR    LOAD POSITIVE (32)  */                 \
  V(lnr, LNR, 0x11)     /* type = RR    LOAD NEGATIVE (32)  */                 \
  V(ltr, LTR, 0x12)     /* type = RR    LOAD AND TEST (32)  */                 \
  V(lcr, LCR, 0x13)     /* type = RR    LOAD COMPLEMENT (32)  */               \
  V(nr, NR, 0x14)       /* type = RR    AND (32)  */                           \
  V(clr, CLR, 0x15)     /* type = RR    COMPARE LOGICAL (32)  */               \
  V(or_z, OR, 0x16)     /* type = RR    OR (32)  */                            \
  V(xr, XR, 0x17)       /* type = RR    EXCLUSIVE OR (32)  */                  \
  V(lr, LR, 0x18)       /* type = RR    LOAD (32)  */                          \
  V(cr_z, CR, 0x19)     /* type = RR    COMPARE (32)  */                       \
  V(ar, AR, 0x1A)       /* type = RR    ADD (32)  */                           \
  V(sr, SR, 0x1B)       /* type = RR    SUBTRACT (32)  */                      \
  V(mr_z, MR, 0x1C)     /* type = RR    MULTIPLY (64<-32)  */                  \
  V(dr, DR, 0x1D)       /* type = RR    DIVIDE (32<-64)  */                    \
  V(alr, ALR, 0x1E)     /* type = RR    ADD LOGICAL (32)  */                   \
  V(slr, SLR, 0x1F)     /* type = RR    SUBTRACT LOGICAL (32)  */              \
  V(lpdr, LPDR, 0x20)   /* type = RR    LOAD POSITIVE (long HFP)  */           \
  V(lndr, LNDR, 0x21)   /* type = RR    LOAD NEGATIVE (long HFP)  */           \
  V(ltdr, LTDR, 0x22)   /* type = RR    LOAD AND TEST (long HFP)  */           \
  V(lcdr, LCDR, 0x23)   /* type = RR    LOAD COMPLEMENT (long HFP)  */         \
  V(hdr, HDR, 0x24)     /* type = RR    HALVE (long HFP)  */                   \
  V(ldxr, LDXR, 0x25) /* type = RR    LOAD ROUNDED (extended to long HFP)  */  \
  V(mxr, MXR, 0x26)   /* type = RR    MULTIPLY (extended HFP)  */              \
  V(mxdr, MXDR, 0x27) /* type = RR    MULTIPLY (long to extended HFP)  */      \
  V(ldr, LDR, 0x28)   /* type = RR    LOAD (long)  */                          \
  V(cdr, CDR, 0x29)   /* type = RR    COMPARE (long HFP)  */                   \
  V(adr, ADR, 0x2A)   /* type = RR    ADD NORMALIZED (long HFP)  */            \
  V(sdr, SDR, 0x2B)   /* type = RR    SUBTRACT NORMALIZED (long HFP)  */       \
  V(mdr, MDR, 0x2C)   /* type = RR    MULTIPLY (long HFP)  */                  \
  V(ddr, DDR, 0x2D)   /* type = RR    DIVIDE (long HFP)  */                    \
  V(swr, SWR, 0x2F)   /* type = RR    SUBTRACT UNNORMALIZED (long HFP)  */     \
  V(lper, LPER, 0x30) /* type = RR    LOAD POSITIVE (short HFP)  */            \
  V(lner, LNER, 0x31) /* type = RR    LOAD NEGATIVE (short HFP)  */            \
  V(lter, LTER, 0x32) /* type = RR    LOAD AND TEST (short HFP)  */            \
  V(lcer, LCER, 0x33) /* type = RR    LOAD COMPLEMENT (short HFP)  */          \
  V(her_z, HER_Z, 0x34) /* type = RR    HALVE (short HFP)  */                  \
  V(ledr, LEDR, 0x35)   /* type = RR    LOAD ROUNDED (long to short HFP)  */   \
  V(axr, AXR, 0x36)     /* type = RR    ADD NORMALIZED (extended HFP)  */      \
  V(sxr, SXR, 0x37)     /* type = RR    SUBTRACT NORMALIZED (extended HFP)  */ \
  V(ler, LER, 0x38)     /* type = RR    LOAD (short)  */                       \
  V(cer, CER, 0x39)     /* type = RR    COMPARE (short HFP)  */                \
  V(aer, AER, 0x3A)     /* type = RR    ADD NORMALIZED (short HFP)  */         \
  V(ser, SER, 0x3B)     /* type = RR    SUBTRACT NORMALIZED (short HFP)  */    \
  V(mder, MDER, 0x3C)   /* type = RR    MULTIPLY (short to long HFP)  */       \
  V(der, DER, 0x3D)     /* type = RR    DIVIDE (short HFP)  */                 \
  V(aur, AUR, 0x3E)     /* type = RR    ADD UNNORMALIZED (short HFP)  */       \
  V(sur, SUR, 0x3F)     /* type = RR    SUBTRACT UNNORMALIZED (short HFP)  */

#define S390_RIE_F_OPCODE_LIST(V)                                              \
  V(risblg, RISBLG,                                                            \
    0xEC51) /* type = RIE_F ROTATE THEN INSERT SELECTED BITS LOW (64)  */      \
  V(rnsbg, RNSBG,                                                              \
    0xEC54) /* type = RIE_F ROTATE THEN AND SELECTED BITS (64)  */             \
  V(risbg, RISBG,                                                              \
    0xEC55) /* type = RIE_F ROTATE THEN INSERT SELECTED BITS (64)  */          \
  V(rosbg, ROSBG, 0xEC56) /* type = RIE_F ROTATE THEN OR SELECTED BITS (64) */ \
  V(rxsbg, RXSBG,                                                              \
    0xEC57) /* type = RIE_F ROTATE THEN EXCLUSIVE OR SELECT. BITS (64)  */     \
  V(risbgn, RISBGN,                                                            \
    0xEC59) /* type = RIE_F ROTATE THEN INSERT SELECTED BITS (64)  */          \
  V(risbhg, RISBHG,                                                            \
    0xEC5D) /* type = RIE_F ROTATE THEN INSERT SELECTED BITS HIGH (64)  */

#define S390_VRX_OPCODE_LIST(V)                                               \
  V(vleb, VLEB, 0xE700) /* type = VRX   VECTOR LOAD ELEMENT (8)  */           \
  V(vleh, VLEH, 0xE701) /* type = VRX   VECTOR LOAD ELEMENT (16)  */          \
  V(vleg, VLEG, 0xE702) /* type = VRX   VECTOR LOAD ELEMENT (64)  */          \
  V(vlef, VLEF, 0xE703) /* type = VRX   VECTOR LOAD ELEMENT (32)  */          \
  V(vllez, VLLEZ,                                                             \
    0xE704) /* type = VRX   VECTOR LOAD LOGICAL ELEMENT AND ZERO  */          \
  V(vlrep, VLREP, 0xE705) /* type = VRX   VECTOR LOAD AND REPLICATE  */       \
  V(vl, VL, 0xE706)       /* type = VRX   VECTOR LOAD  */                     \
  V(vlbb, VLBB, 0xE707)   /* type = VRX   VECTOR LOAD TO BLOCK BOUNDARY  */   \
  V(vlbr, VLBR, 0xE606) /* type = VRX   VECTOR LOAD BYTE REVERSED ELEMENTS */ \
  V(vlbrrep, VLBRREP,                                                         \
    0xE605) /* type = VRX VECTOR LOAD BYTE REVERSED ELEMENT AND REPLICATE */  \
  V(vlebrh, VLEBRH,                                                           \
    0xE601) /* type = VRX VECTOR LOAD BYTE REVERSED ELEMENT (16) */           \
  V(vlebrf, VLEBRF,                                                           \
    0xE603) /* type = VRX VECTOR LOAD BYTE REVERSED ELEMENT (32) */           \
  V(vlebrg, VLEBRG,                                                           \
    0xE602) /* type = VRX VECTOR LOAD BYTE REVERSED ELEMENT (64) */           \
  V(vsteb, VSTEB, 0xE708) /* type = VRX   VECTOR STORE ELEMENT (8)  */        \
  V(vsteh, VSTEH, 0xE709) /* type = VRX   VECTOR STORE ELEMENT (16)  */       \
  V(vsteg, VSTEG, 0xE70A) /* type = VRX   VECTOR STORE ELEMENT (64)  */       \
  V(vstef, VSTEF, 0xE70B) /* type = VRX   VECTOR STORE ELEMENT (32)  */       \
  V(vst, VST, 0xE70E)     /* type = VRX   VECTOR STORE  */                    \
  V(vstbr, VSTBR,                                                             \
    0xE60E) /* type = VRX   VECTOR STORE BYTE REVERSED ELEMENTS */            \
  V(vstebrh, VSTEBRH,                                                         \
    0xE609) /* type = VRX VECTOR STORE BYTE REVERSED ELEMENT (16) */          \
  V(vstebrf, VSTEBRF,                                                         \
    0xE60B) /* type = VRX VECTOR STORE BYTE REVERSED ELEMENT (32) */          \
  V(vstebrg, VSTEBRG,                                                         \
    0xE60A) /* type = VRX VECTOR STORE BYTE REVERSED ELEMENT (64) */

#define S390_RIE_G_OPCODE_LIST(V)                                             \
  V(lochi, LOCHI,                                                             \
    0xEC42) /* type = RIE_G LOAD HALFWORD IMMEDIATE ON CONDITION (32<-16)  */ \
  V(locghi, LOCGHI,                                                           \
    0xEC46) /* type = RIE_G LOAD HALFWORD IMMEDIATE ON CONDITION (64<-16)  */ \
  V(lochhi, LOCHHI, 0xEC4E) /* type = RIE_G LOAD HALFWORD HIGH IMMEDIATE   */ \
                            /* ON CONDITION (32<-16)  */

#define S390_RRS_OPCODE_LIST(V)                                               \
  V(cgrb, CGRB, 0xECE4)   /* type = RRS   COMPARE AND BRANCH (64)  */         \
  V(clgrb, CLGRB, 0xECE5) /* type = RRS   COMPARE LOGICAL AND BRANCH (64)  */ \
  V(crb, CRB, 0xECF6)     /* type = RRS   COMPARE AND BRANCH (32)  */         \
  V(clrb, CLRB, 0xECF7)   /* type = RRS   COMPARE LOGICAL AND BRANCH (32)  */

#define S390_OPCODE_LIST(V) \
  S390_RSY_A_OPCODE_LIST(V) \
  S390_RSY_B_OPCODE_LIST(V) \
  S390_RXE_OPCODE_LIST(V)   \
  S390_RRF_A_OPCODE_LIST(V) \
  S390_RXF_OPCODE_LIST(V)   \
  S390_IE_OPCODE_LIST(V)    \
  S390_RRF_B_OPCODE_LIST(V) \
  S390_RRF_C_OPCODE_LIST(V) \
  S390_MII_OPCODE_LIST(V)   \
  S390_RRF_D_OPCODE_LIST(V) \
  S390_RRF_E_OPCODE_LIST(V) \
  S390_VRR_A_OPCODE_LIST(V) \
  S390_VRR_B_OPCODE_LIST(V) \
  S390_VRR_C_OPCODE_LIST(V) \
  S390_VRI_A_OPCODE_LIST(V) \
  S390_VRR_D_OPCODE_LIST(V) \
  S390_VRI_B_OPCODE_LIST(V) \
  S390_VRR_E_OPCODE_LIST(V) \
  S390_VRI_C_OPCODE_LIST(V) \
  S390_VRI_D_OPCODE_LIST(V) \
  S390_VRR_F_OPCODE_LIST(V) \
  S390_RIS_OPCODE_LIST(V)   \
  S390_VRI_E_OPCODE_LIST(V) \
  S390_RSL_A_OPCODE_LIST(V) \
  S390_RSL_B_OPCODE_LIST(V) \
  S390_SI_OPCODE_LIST(V)    \
  S390_SIL_OPCODE_LIST(V)   \
  S390_VRS_A_OPCODE_LIST(V) \
  S390_RIL_A_OPCODE_LIST(V) \
  S390_RIL_B_OPCODE_LIST(V) \
  S390_VRS_B_OPCODE_LIST(V) \
  S390_RIL_C_OPCODE_LIST(V) \
  S390_VRS_C_OPCODE_LIST(V) \
  S390_RI_A_OPCODE_LIST(V)  \
  S390_RSI_OPCODE_LIST(V)   \
  S390_RI_B_OPCODE_LIST(V)  \
  S390_RI_C_OPCODE_LIST(V)  \
  S390_SMI_OPCODE_LIST(V)   \
  S390_RXY_A_OPCODE_LIST(V) \
  S390_RXY_B_OPCODE_LIST(V) \
  S390_SIY_OPCODE_LIST(V)   \
  S390_SS_A_OPCODE_LIST(V)  \
  S390_E_OPCODE_LIST(V)     \
  S390_SS_B_OPCODE_LIST(V)  \
  S390_SS_C_OPCODE_LIST(V)  \
  S390_SS_D_OPCODE_LIST(V)  \
  S390_SS_E_OPCODE_LIST(V)  \
  S390_I_OPCODE_LIST(V)     \
  S390_SS_F_OPCODE_LIST(V)  \
  S390_SSE_OPCODE_LIST(V)   \
  S390_SSF_OPCODE_LIST(V)   \
  S390_RS_A_OPCODE_LIST(V)  \
  S390_RS_B_OPCODE_LIST(V)  \
  S390_S_OPCODE_LIST(V)     \
  S390_RX_A_OPCODE_LIST(V)  \
  S390_RX_B_OPCODE_LIST(V)  \
  S390_RIE_A_OPCODE_LIST(V) \
  S390_RRD_OPCODE_LIST(V)   \
  S390_RIE_B_OPCODE_LIST(V) \
  S390_RRE_OPCODE_LIST(V)   \
  S390_RIE_C_OPCODE_LIST(V) \
  S390_RIE_D_OPCODE_LIST(V) \
  S390_VRV_OPCODE_LIST(V)   \
  S390_RIE_E_OPCODE_LIST(V) \
  S390_RR_OPCODE_LIST(V)    \
  S390_RIE_F_OPCODE_LIST(V) \
  S390_VRX_OPCODE_LIST(V)   \
  S390_RIE_G_OPCODE_LIST(V) \
  S390_RRS_OPCODE_LIST(V)

// Opcodes as defined in Appendix B-2 table
enum Opcode {
#define DECLARE_OPCODES(name, opcode_name, opcode_value) \
  opcode_name = opcode_value,
  S390_OPCODE_LIST(DECLARE_OPCODES)
#undef DECLARE_OPCODES

      BKPT = 0x0001,  // GDB Software Breakpoint
  DUMY = 0xE352       // Special dummy opcode
};

// Instruction encoding bits and masks.
enum {
  // Instruction encoding bit
  B1 = 1 << 1,
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
  kImm26Mask = (1 << 26) - 1,
  kBOfieldMask = 0x1f << 21,
  kOpcodeMask = 0x3f << 26,
  kExt2OpcodeMask = 0x1f << 1,
  kExt5OpcodeMask = 0x3 << 2,
  kBIMask = 0x1F << 16,
  kBDMask = 0x14 << 2,
  kAAMask = 0x01 << 1,
  kLKMask = 0x01,
  kRCMask = 0x01,
  kTOMask = 0x1f << 21
};

// S390 instructions requires bigger shifts,
// make them macros instead of enum because of the typing issue
#define B32 ((uint64_t)1 << 32)
#define B36 ((uint64_t)1 << 36)
#define B40 ((uint64_t)1 << 40)
const FourByteInstr kFourByteBrCondMask = 0xF << 20;
const SixByteInstr kSixByteBrCondMask = static_cast<SixByteInstr>(0xF) << 36;

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

// Link bit
enum LKBit {   // Bit 0
  SetLK = 1,   // Load effective address of next instruction
  LeaveLK = 0  // No action
};

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

#ifdef _AIX
#undef CR_LT
#undef CR_GT
#undef CR_EQ
#undef CR_SO
#endif

enum CRBit { CR_LT = 0, CR_GT = 1, CR_EQ = 2, CR_SO = 3, CR_FU = 3 };

#define CRWIDTH 4

// -----------------------------------------------------------------------------
// Supervisor Call (svc) specific support.

// Special Software Interrupt codes when used in the presence of the S390
// simulator.
// SVC provides a 24bit immediate value. Use bits 22:0 for standard
// SoftwareInterrupCode. Bit 23 is reserved for the stop feature.
enum SoftwareInterruptCodes {
  // Transition to C code
  kCallRtRedirected = 0x0010,
  // Breakpoint
  kBreakpoint = 0x0000,
  // Stop
  kStopCode = 1 << 23
};
const uint32_t kStopCodeMask = kStopCode - 1;
const uint32_t kMaxStopCode = kStopCode - 1;
const int32_t kDefaultStopCode = -1;

// FP rounding modes.
enum FPRoundingMode {
  CURRENT_ROUNDING_MODE = 0,
  ROUND_TO_NEAREST_AWAY_FROM_0 = 1,
  ROUND_TO_NEAREST_TO_EVEN = 4,
  ROUND_TOWARD_0 = 5,
  ROUND_TOWARD_POS_INF = 6,
  ROUND_TOWARD_NEG_INF = 7,

  // Aliases.
  kRoundToNearest = ROUND_TO_NEAREST_TO_EVEN,
  kRoundToZero = ROUND_TOWARD_0,
  kRoundToPlusInf = ROUND_TOWARD_POS_INF,
  kRoundToMinusInf = ROUND_TOWARD_NEG_INF
};

const uint32_t kFPRoundingModeMask = 3;

enum CheckForInexactConversion {
  kCheckForInexactConversion,
  kDontCheckForInexactConversion
};

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.

// use TRAP4 to indicate redirection call for simulation mode
const Instr rtCallRedirInstr = TRAP4;

// -----------------------------------------------------------------------------
// Instruction abstraction.

// The class Instruction enables access to individual fields defined in the
// z/Architecture instruction set encoding.
class Instruction {
 public:
  // S390 Opcode Format Types
  //   Based on the first byte of the opcode, we can determine how to extract
  //   the entire opcode of the instruction.  The various favours include:
  enum OpcodeFormatType {
    ONE_BYTE_OPCODE,           // One Byte - Bits 0 to 7
    TWO_BYTE_OPCODE,           // Two Bytes - Bits 0 to 15
    TWO_BYTE_DISJOINT_OPCODE,  // Two Bytes - Bits 0 to 7, 40 to 47
    THREE_NIBBLE_OPCODE        // Three Nibbles - Bits 0 to 7, 12 to 15
  };

  static OpcodeFormatType OpcodeFormatTable[256];

  // Get the raw instruction bits.
  template <typename T>
  inline T InstructionBits() const {
    return Instruction::InstructionBits<T>(
        reinterpret_cast<const uint8_t*>(this));
  }
  inline Instr InstructionBits() const {
    return *reinterpret_cast<const Instr*>(this);
  }

  // Set the raw instruction bits to value.
  template <typename T>
  inline void SetInstructionBits(T value) const {
    Instruction::SetInstructionBits<T>(reinterpret_cast<const uint8_t*>(this),
                                       value);
  }
  inline void SetInstructionBits(Instr value) {
    *reinterpret_cast<Instr*>(this) = value;
  }

  // Read one particular bit out of the instruction bits.
  inline int Bit(int nr) const { return (InstructionBits() >> nr) & 1; }

  // Read a bit field's value out of the instruction bits.
  inline int Bits(int hi, int lo) const {
    return (InstructionBits() >> lo) & ((2 << (hi - lo)) - 1);
  }

  // Read bits according to instruction type
  template <typename T, typename U>
  inline U Bits(int hi, int lo) const {
    return (InstructionBits<T>() >> lo) & ((2 << (hi - lo)) - 1);
  }

  // Read a bit field out of the instruction bits.
  inline int BitField(int hi, int lo) const {
    return InstructionBits() & (((2 << (hi - lo)) - 1) << lo);
  }

  // Determine the instruction length
  inline int InstructionLength() {
    return Instruction::InstructionLength(
        reinterpret_cast<const uint8_t*>(this));
  }
  // Extract the Instruction Opcode
  inline Opcode S390OpcodeValue() {
    return Instruction::S390OpcodeValue(reinterpret_cast<const uint8_t*>(this));
  }

  // Static support.

  // Read one particular bit out of the instruction bits.
  static inline int Bit(Instr instr, int nr) { return (instr >> nr) & 1; }

  // Read the value of a bit field out of the instruction bits.
  static inline int Bits(Instr instr, int hi, int lo) {
    return (instr >> lo) & ((2 << (hi - lo)) - 1);
  }

  // Read a bit field out of the instruction bits.
  static inline int BitField(Instr instr, int hi, int lo) {
    return instr & (((2 << (hi - lo)) - 1) << lo);
  }

  // Determine the instruction length of the given instruction
  static inline int InstructionLength(const uint8_t* instr) {
    // Length can be determined by the first nibble.
    // 0x0 to 0x3 => 2-bytes
    // 0x4 to 0xB => 4-bytes
    // 0xC to 0xF => 6-bytes
    uint8_t topNibble = (*instr >> 4) & 0xF;
    if (topNibble <= 3)
      return 2;
    else if (topNibble <= 0xB)
      return 4;
    return 6;
  }

  // Returns the instruction bits of the given instruction
  static inline uint64_t InstructionBits(const uint8_t* instr) {
    int length = InstructionLength(instr);
    if (2 == length)
      return static_cast<uint64_t>(InstructionBits<TwoByteInstr>(instr));
    else if (4 == length)
      return static_cast<uint64_t>(InstructionBits<FourByteInstr>(instr));
    else
      return InstructionBits<SixByteInstr>(instr);
  }

  // Extract the raw instruction bits
  template <typename T>
  static inline T InstructionBits(const uint8_t* instr) {
#if !V8_TARGET_LITTLE_ENDIAN
    if (sizeof(T) <= 4) {
      return *reinterpret_cast<const T*>(instr);
    } else {
      // We cannot read 8-byte instructon address directly, because for a
      // six-byte instruction, the extra 2-byte address might not be
      // allocated.
      uint64_t fourBytes = *reinterpret_cast<const uint32_t*>(instr);
      uint16_t twoBytes = *reinterpret_cast<const uint16_t*>(instr + 4);
      return (fourBytes << 16 | twoBytes);
    }
#else
    // Even on little endian hosts (simulation), the instructions
    // are stored as big-endian in order to decode the opcode and
    // instruction length.
    T instr_bits = 0;

    // 6-byte instrs are represented by uint64_t
    uint32_t size = (sizeof(T) == 8) ? 6 : sizeof(T);

    for (T i = 0; i < size; i++) {
      instr_bits <<= 8;
      instr_bits |= *(instr + i);
    }
    return instr_bits;
#endif
  }

  // Set the Instruction Bits to value
  template <typename T>
  static inline void SetInstructionBits(uint8_t* instr, T value) {
#if V8_TARGET_LITTLE_ENDIAN
    // The instruction bits are stored in big endian format even on little
    // endian hosts, in order to decode instruction length and opcode.
    // The following code will reverse the bytes so that the stores later
    // (which are in native endianess) will effectively save the instruction
    // in big endian.
    if (sizeof(T) == 2) {
      // Two Byte Instruction
      value = ((value & 0x00FF) << 8) | ((value & 0xFF00) >> 8);
    } else if (sizeof(T) == 4) {
      // Four Byte Instruction
      value = ((value & 0x000000FF) << 24) | ((value & 0x0000FF00) << 8) |
              ((value & 0x00FF0000) >> 8) | ((value & 0xFF000000) >> 24);
    } else if (sizeof(T) == 8) {
      // Six Byte Instruction
      uint64_t orig_value = static_cast<uint64_t>(value);
      value = (static_cast<uint64_t>(orig_value & 0xFF) << 40) |
              (static_cast<uint64_t>((orig_value >> 8) & 0xFF) << 32) |
              (static_cast<uint64_t>((orig_value >> 16) & 0xFF) << 24) |
              (static_cast<uint64_t>((orig_value >> 24) & 0xFF) << 16) |
              (static_cast<uint64_t>((orig_value >> 32) & 0xFF) << 8) |
              (static_cast<uint64_t>((orig_value >> 40) & 0xFF));
    }
#endif
    if (sizeof(T) <= 4) {
      *reinterpret_cast<T*>(instr) = value;
    } else {
#if V8_TARGET_LITTLE_ENDIAN
      uint64_t orig_value = static_cast<uint64_t>(value);
      *reinterpret_cast<uint32_t*>(instr) = static_cast<uint32_t>(value);
      *reinterpret_cast<uint16_t*>(instr + 4) =
          static_cast<uint16_t>((orig_value >> 32) & 0xFFFF);
#else
      *reinterpret_cast<uint32_t*>(instr) = static_cast<uint32_t>(value >> 16);
      *reinterpret_cast<uint16_t*>(instr + 4) =
          static_cast<uint16_t>(value & 0xFFFF);
#endif
    }
  }

  // Get Instruction Format Type
  static OpcodeFormatType getOpcodeFormatType(const uint8_t* instr) {
    const uint8_t firstByte = *instr;
    return OpcodeFormatTable[firstByte];
  }

  // Extract the full opcode from the instruction.
  static inline Opcode S390OpcodeValue(const uint8_t* instr) {
    OpcodeFormatType opcodeType = getOpcodeFormatType(instr);

    // The native instructions are encoded in big-endian format
    // even if running on little-endian host.  Hence, we need
    // to ensure we use uint8_t* based bit-wise logic.
    switch (opcodeType) {
      case ONE_BYTE_OPCODE:
        // One Byte - Bits 0 to 7
        return static_cast<Opcode>(*instr);
      case TWO_BYTE_OPCODE:
        // Two Bytes - Bits 0 to 15
        return static_cast<Opcode>((*instr << 8) | (*(instr + 1)));
      case TWO_BYTE_DISJOINT_OPCODE:
        // Two Bytes - Bits 0 to 7, 40 to 47
        return static_cast<Opcode>((*instr << 8) | (*(instr + 5) & 0xFF));
      default:
        // case THREE_NIBBLE_OPCODE:
        // Three Nibbles - Bits 0 to 7, 12 to 15
        return static_cast<Opcode>((*instr << 4) | (*(instr + 1) & 0xF));
    }

    UNREACHABLE();
  }

  // Fields used in Software interrupt instructions
  inline SoftwareInterruptCodes SvcValue() const {
    return static_cast<SoftwareInterruptCodes>(Bits<FourByteInstr, int>(15, 0));
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

#define DECLARE_FIELD_FOR_TWO_BYTE_INSTR(name, T, lo, hi)   \
  inline int name() const {                                 \
    return Bits<TwoByteInstr, T>(15 - (lo), 15 - (hi) + 1); \
  }

#define DECLARE_FIELD_FOR_FOUR_BYTE_INSTR(name, T, lo, hi)   \
  inline int name() const {                                  \
    return Bits<FourByteInstr, T>(31 - (lo), 31 - (hi) + 1); \
  }

#define DECLARE_FIELD_FOR_SIX_BYTE_INSTR(name, T, lo, hi)   \
  inline int name() const {                                 \
    return Bits<SixByteInstr, T>(47 - (lo), 47 - (hi) + 1); \
  }

class TwoByteInstruction : public Instruction {
 public:
  inline int size() const { return 2; }
};

class FourByteInstruction : public Instruction {
 public:
  inline int size() const { return 4; }
};

class SixByteInstruction : public Instruction {
 public:
  inline int size() const { return 6; }
};

// I Instruction
class IInstruction : public TwoByteInstruction {
 public:
  DECLARE_FIELD_FOR_TWO_BYTE_INSTR(IValue, int, 8, 16)
};

// E Instruction
class EInstruction : public TwoByteInstruction {};

// IE Instruction
class IEInstruction : public FourByteInstruction {
 public:
  DECLARE_FIELD_FOR_FOUR_BYTE_INSTR(I1Value, int, 24, 28)
  DECLARE_FIELD_FOR_FOUR_BYTE_INSTR(I2Value, int, 28, 32)
};

// MII Instruction
class MIIInstruction : public SixByteInstruction {
 public:
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M1Value, uint32_t, 8, 12)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(RI2Value, int, 12, 24)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(RI3Value, int, 24, 47)
};

// RI Instruction
class RIInstruction : public FourByteInstruction {
 public:
  DECLARE_FIELD_FOR_FOUR_BYTE_INSTR(R1Value, int, 8, 12)
  DECLARE_FIELD_FOR_FOUR_BYTE_INSTR(I2Value, int, 16, 32)
  DECLARE_FIELD_FOR_FOUR_BYTE_INSTR(I2UnsignedValue, uint32_t, 16, 32)
  DECLARE_FIELD_FOR_FOUR_BYTE_INSTR(M1Value, uint32_t, 8, 12)
};

// RR Instruction
class RRInstruction : Instruction {
 public:
  inline int R1Value() const {
    // the high and low parameters of Bits is the number of bits from
    // rightmost place
    return Bits<TwoByteInstr, int>(7, 4);
  }
  inline int R2Value() const { return Bits<TwoByteInstr, int>(3, 0); }
  inline Condition M1Value() const {
    return static_cast<Condition>(Bits<TwoByteInstr, int>(7, 4));
  }

  inline int size() const { return 2; }
};

// RRE Instruction
class RREInstruction : Instruction {
 public:
  inline int R1Value() const { return Bits<FourByteInstr, int>(7, 4); }
  inline int R2Value() const { return Bits<FourByteInstr, int>(3, 0); }
  inline int M3Value() const { return Bits<FourByteInstr, int>(15, 12); }
  inline int M4Value() const { return Bits<FourByteInstr, int>(19, 16); }
  inline int size() const { return 4; }
};

// RRF Instruction
class RRFInstruction : Instruction {
 public:
  inline int R1Value() const { return Bits<FourByteInstr, int>(7, 4); }
  inline int R2Value() const { return Bits<FourByteInstr, int>(3, 0); }
  inline int R3Value() const { return Bits<FourByteInstr, int>(15, 12); }
  inline int M3Value() const { return Bits<FourByteInstr, int>(15, 12); }
  inline int M4Value() const { return Bits<FourByteInstr, int>(11, 8); }
  inline int size() const { return 4; }
};

// RRD Isntruction
class RRDInstruction : Instruction {
 public:
  inline int R1Value() const { return Bits<FourByteInstr, int>(15, 12); }
  inline int R2Value() const { return Bits<FourByteInstr, int>(3, 0); }
  inline int R3Value() const { return Bits<FourByteInstr, int>(7, 4); }
  inline int size() const { return 4; }
};

// RS Instruction
class RSInstruction : Instruction {
 public:
  inline int R1Value() const { return Bits<FourByteInstr, int>(23, 20); }
  inline int R3Value() const { return Bits<FourByteInstr, int>(19, 16); }
  inline int B2Value() const { return Bits<FourByteInstr, int>(15, 12); }
  inline unsigned int D2Value() const {
    return Bits<FourByteInstr, unsigned int>(11, 0);
  }
  inline int size() const { return 4; }
};

// RSI Instruction
class RSIInstruction : Instruction {
 public:
  inline int R1Value() const { return Bits<FourByteInstr, int>(23, 20); }
  inline int R3Value() const { return Bits<FourByteInstr, int>(19, 16); }
  inline int I2Value() const {
    return static_cast<int32_t>(Bits<FourByteInstr, int16_t>(15, 0));
  }
  inline int size() const { return 4; }
};

// RSY Instruction
class RSYInstruction : Instruction {
 public:
  inline int R1Value() const { return Bits<SixByteInstr, int>(39, 36); }
  inline int R3Value() const { return Bits<SixByteInstr, int>(35, 32); }
  inline int B2Value() const { return Bits<SixByteInstr, int>(31, 28); }
  inline int32_t D2Value() const {
    int32_t value = Bits<SixByteInstr, int32_t>(27, 16);
    value += Bits<SixByteInstr, int8_t>(15, 8) << 12;
    return value;
  }
  inline int size() const { return 6; }
};

// RX Instruction
class RXInstruction : Instruction {
 public:
  inline int R1Value() const { return Bits<FourByteInstr, int>(23, 20); }
  inline int X2Value() const { return Bits<FourByteInstr, int>(19, 16); }
  inline int B2Value() const { return Bits<FourByteInstr, int>(15, 12); }
  inline uint32_t D2Value() const {
    return Bits<FourByteInstr, uint32_t>(11, 0);
  }
  inline int size() const { return 4; }
};

// RXY Instruction
class RXYInstruction : Instruction {
 public:
  inline int R1Value() const { return Bits<SixByteInstr, int>(39, 36); }
  inline int X2Value() const { return Bits<SixByteInstr, int>(35, 32); }
  inline int B2Value() const { return Bits<SixByteInstr, int>(31, 28); }
  inline int32_t D2Value() const {
    int32_t value = Bits<SixByteInstr, uint32_t>(27, 16);
    value += Bits<SixByteInstr, int8_t>(15, 8) << 12;
    return value;
  }
  inline int size() const { return 6; }
};

// RIL Instruction
class RILInstruction : Instruction {
 public:
  inline int R1Value() const { return Bits<SixByteInstr, int>(39, 36); }
  inline int32_t I2Value() const { return Bits<SixByteInstr, int32_t>(31, 0); }
  inline uint32_t I2UnsignedValue() const {
    return Bits<SixByteInstr, uint32_t>(31, 0);
  }
  inline int size() const { return 6; }
};

// SI Instruction
class SIInstruction : Instruction {
 public:
  inline int B1Value() const { return Bits<FourByteInstr, int>(15, 12); }
  inline uint32_t D1Value() const {
    return Bits<FourByteInstr, uint32_t>(11, 0);
  }
  inline uint8_t I2Value() const {
    return Bits<FourByteInstr, uint8_t>(23, 16);
  }
  inline int size() const { return 4; }
};

// SIY Instruction
class SIYInstruction : Instruction {
 public:
  inline int B1Value() const { return Bits<SixByteInstr, int>(31, 28); }
  inline int32_t D1Value() const {
    int32_t value = Bits<SixByteInstr, uint32_t>(27, 16);
    value += Bits<SixByteInstr, int8_t>(15, 8) << 12;
    return value;
  }
  inline uint8_t I2Value() const { return Bits<SixByteInstr, uint8_t>(39, 32); }
  inline int size() const { return 6; }
};

// SIL Instruction
class SILInstruction : Instruction {
 public:
  inline int B1Value() const { return Bits<SixByteInstr, int>(31, 28); }
  inline int D1Value() const { return Bits<SixByteInstr, int>(27, 16); }
  inline int I2Value() const { return Bits<SixByteInstr, int>(15, 0); }
  inline int size() const { return 6; }
};

// SS Instruction
class SSInstruction : Instruction {
 public:
  inline int B1Value() const { return Bits<SixByteInstr, int>(31, 28); }
  inline int B2Value() const { return Bits<SixByteInstr, int>(15, 12); }
  inline int D1Value() const { return Bits<SixByteInstr, int>(27, 16); }
  inline int D2Value() const { return Bits<SixByteInstr, int>(11, 0); }
  inline int Length() const { return Bits<SixByteInstr, int>(39, 32); }
  inline int size() const { return 6; }
};

// RXE Instruction
class RXEInstruction : Instruction {
 public:
  inline int R1Value() const { return Bits<SixByteInstr, int>(39, 36); }
  inline int X2Value() const { return Bits<SixByteInstr, int>(35, 32); }
  inline int B2Value() const { return Bits<SixByteInstr, int>(31, 28); }
  inline int D2Value() const { return Bits<SixByteInstr, int>(27, 16); }
  inline int size() const { return 6; }
};

// RIE Instruction
class RIEInstruction : Instruction {
 public:
  inline int R1Value() const { return Bits<SixByteInstr, int>(39, 36); }
  inline int R2Value() const { return Bits<SixByteInstr, int>(35, 32); }
  inline int I3Value() const { return Bits<SixByteInstr, uint32_t>(31, 24); }
  inline int I4Value() const { return Bits<SixByteInstr, uint32_t>(23, 16); }
  inline int I5Value() const { return Bits<SixByteInstr, uint32_t>(15, 8); }
  inline int I6Value() const {
    return static_cast<int32_t>(Bits<SixByteInstr, int16_t>(31, 16));
  }
  inline int size() const { return 6; }
};

// VRR Instruction
class VRR_A_Instruction : SixByteInstruction {
 public:
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R1Value, int, 8, 12)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R2Value, int, 12, 16)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M5Value, uint32_t, 24, 28)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M4Value, uint32_t, 28, 32)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M3Value, uint32_t, 32, 36)
};

class VRR_B_Instruction : SixByteInstruction {
 public:
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R1Value, int, 8, 12)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R2Value, int, 12, 16)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R3Value, int, 16, 20)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M5Value, uint32_t, 24, 28)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M4Value, uint32_t, 32, 36)
};

class VRR_C_Instruction : SixByteInstruction {
 public:
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R1Value, int, 8, 12)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R2Value, int, 12, 16)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R3Value, int, 16, 20)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M6Value, uint32_t, 24, 28)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M5Value, uint32_t, 28, 32)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M4Value, uint32_t, 32, 36)
};

class VRR_E_Instruction : SixByteInstruction {
 public:
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R1Value, int, 8, 12)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R2Value, int, 12, 16)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R3Value, int, 16, 20)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R4Value, int, 32, 36)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M6Value, uint32_t, 20, 24)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M5Value, uint32_t, 28, 32)
};

class VRR_F_Instruction : SixByteInstruction {
 public:
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R1Value, int, 8, 12)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R2Value, int, 12, 16)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R3Value, int, 16, 20)
};

class VRX_Instruction : SixByteInstruction {
 public:
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R1Value, int, 8, 12)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(X2Value, int, 12, 16)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(B2Value, int, 16, 20)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(D2Value, int, 20, 32)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M3Value, uint32_t, 32, 36)
};

class VRS_Instruction : SixByteInstruction {
 public:
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R1Value, int, 8, 12)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R3Value, int, 12, 16)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(B2Value, int, 16, 20)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(D2Value, int, 20, 32)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M4Value, uint32_t, 32, 36)
};

class VRI_A_Instruction : SixByteInstruction {
 public:
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R1Value, int, 8, 12)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(I2Value, int, 16, 32)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M3Value, uint32_t, 32, 36)
};

class VRI_C_Instruction : SixByteInstruction {
 public:
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R1Value, int, 8, 12)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(R3Value, int, 12, 16)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(I2Value, int, 16, 32)
  DECLARE_FIELD_FOR_SIX_BYTE_INSTR(M4Value, uint32_t, 32, 36)
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

#endif  // V8_CODEGEN_S390_CONSTANTS_S390_H_
