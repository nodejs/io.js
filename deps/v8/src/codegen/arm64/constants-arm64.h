// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM64_CONSTANTS_ARM64_H_
#define V8_CODEGEN_ARM64_CONSTANTS_ARM64_H_

#include "src/base/macros.h"
#include "src/common/globals.h"

// Assert that this is an LP64 system, or LLP64 on Windows.
static_assert(sizeof(int) == sizeof(int32_t));
#if defined(V8_OS_WIN)
static_assert(sizeof(1L) == sizeof(int32_t));
#else
static_assert(sizeof(long) == sizeof(int64_t));  // NOLINT(runtime/int)
static_assert(sizeof(1L) == sizeof(int64_t));
#endif
static_assert(sizeof(void*) == sizeof(int64_t));
static_assert(sizeof(1) == sizeof(int32_t));

// Get the standard printf format macros for C99 stdint types.
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

namespace v8 {
namespace internal {

// The maximum size of the code range s.t. pc-relative calls are possible
// between all Code objects in the range.
constexpr size_t kMaxPCRelativeCodeRangeInMB = 128;

constexpr uint8_t kInstrSize = 4;
constexpr uint8_t kInstrSizeLog2 = 2;
constexpr uint8_t kLoadLiteralScaleLog2 = 2;
constexpr uint8_t kLoadLiteralScale = 1 << kLoadLiteralScaleLog2;
constexpr int kMaxLoadLiteralRange = 1 * MB;

constexpr int kNumberOfRegisters = 32;
constexpr int kNumberOfVRegisters = 32;
// Callee saved registers are x19-x28.
constexpr int kNumberOfCalleeSavedRegisters = 10;
// Callee saved FP registers are d8-d15.
constexpr int kNumberOfCalleeSavedVRegisters = 8;
constexpr int kWRegSizeInBits = 32;
constexpr int kWRegSizeInBitsLog2 = 5;
constexpr int kWRegSize = kWRegSizeInBits >> 3;
constexpr int kWRegSizeLog2 = kWRegSizeInBitsLog2 - 3;
constexpr int kXRegSizeInBits = 64;
constexpr int kXRegSizeInBitsLog2 = 6;
constexpr int kXRegSize = kXRegSizeInBits >> 3;
constexpr int kXRegSizeLog2 = kXRegSizeInBitsLog2 - 3;
constexpr int kSRegSizeInBits = 32;
constexpr int kSRegSizeInBitsLog2 = 5;
constexpr int kSRegSize = kSRegSizeInBits >> 3;
constexpr int kSRegSizeLog2 = kSRegSizeInBitsLog2 - 3;
constexpr int kDRegSizeInBits = 64;
constexpr int kDRegSizeInBitsLog2 = 6;
constexpr int kDRegSize = kDRegSizeInBits >> 3;
constexpr int kDRegSizeLog2 = kDRegSizeInBitsLog2 - 3;
constexpr int kDRegSizeInBytesLog2 = kDRegSizeInBitsLog2 - 3;
constexpr int kBRegSizeInBits = 8;
constexpr int kBRegSize = kBRegSizeInBits >> 3;
constexpr int kHRegSizeInBits = 16;
constexpr int kHRegSize = kHRegSizeInBits >> 3;
constexpr int kQRegSizeInBits = 128;
constexpr int kQRegSizeInBitsLog2 = 7;
constexpr int kQRegSize = kQRegSizeInBits >> 3;
constexpr int kQRegSizeLog2 = kQRegSizeInBitsLog2 - 3;
constexpr int kVRegSizeInBits = kQRegSizeInBits;
constexpr int kVRegSize = kVRegSizeInBits >> 3;
constexpr int64_t kWRegMask = 0x00000000ffffffffL;
constexpr int64_t kXRegMask = 0xffffffffffffffffL;
constexpr int64_t kSRegMask = 0x00000000ffffffffL;
constexpr int64_t kDRegMask = 0xffffffffffffffffL;
// TODO(all) check if the expression below works on all compilers or if it
// triggers an overflow error.
constexpr int64_t kDSignBit = 63;
constexpr int64_t kDSignMask = 0x1LL << kDSignBit;
constexpr int64_t kSSignBit = 31;
constexpr int64_t kSSignMask = 0x1LL << kSSignBit;
constexpr int64_t kXSignBit = 63;
constexpr int64_t kXSignMask = 0x1LL << kXSignBit;
constexpr int64_t kWSignBit = 31;
constexpr int64_t kWSignMask = 0x1LL << kWSignBit;
constexpr int64_t kDQuietNanBit = 51;
constexpr int64_t kDQuietNanMask = 0x1LL << kDQuietNanBit;
constexpr int64_t kSQuietNanBit = 22;
constexpr int64_t kSQuietNanMask = 0x1LL << kSQuietNanBit;
constexpr int64_t kHQuietNanBit = 9;
constexpr int64_t kHQuietNanMask = 0x1LL << kHQuietNanBit;
constexpr int64_t kByteMask = 0xffL;
constexpr int64_t kHalfWordMask = 0xffffL;
constexpr int64_t kWordMask = 0xffffffffL;
constexpr uint64_t kXMaxUInt = 0xffffffffffffffffUL;
constexpr uint64_t kWMaxUInt = 0xffffffffUL;
constexpr int64_t kXMaxInt = 0x7fffffffffffffffL;
constexpr int64_t kXMinInt = 0x8000000000000000L;
constexpr int32_t kWMaxInt = 0x7fffffff;
constexpr int32_t kWMinInt = 0x80000000;
constexpr int kIp0Code = 16;
constexpr int kIp1Code = 17;
constexpr int kFramePointerRegCode = 29;
constexpr int kLinkRegCode = 30;
constexpr int kZeroRegCode = 31;
constexpr int kSPRegInternalCode = 63;
constexpr unsigned kRegCodeMask = 0x1f;
constexpr unsigned kShiftAmountWRegMask = 0x1f;
constexpr unsigned kShiftAmountXRegMask = 0x3f;
// Standard machine types defined by AAPCS64.
constexpr unsigned kHalfWordSize = 16;
constexpr unsigned kHalfWordSizeLog2 = 4;
constexpr unsigned kHalfWordSizeInBytes = kHalfWordSize >> 3;
constexpr unsigned kHalfWordSizeInBytesLog2 = kHalfWordSizeLog2 - 3;
constexpr unsigned kWordSize = 32;
constexpr unsigned kWordSizeLog2 = 5;
constexpr unsigned kWordSizeInBytes = kWordSize >> 3;
constexpr unsigned kWordSizeInBytesLog2 = kWordSizeLog2 - 3;
constexpr unsigned kDoubleWordSize = 64;
constexpr unsigned kDoubleWordSizeInBytes = kDoubleWordSize >> 3;
constexpr unsigned kQuadWordSize = 128;
constexpr unsigned kQuadWordSizeInBytes = kQuadWordSize >> 3;
constexpr int kMaxLanesPerVector = 16;

constexpr unsigned kAddressTagOffset = 56;
constexpr unsigned kAddressTagWidth = 8;
constexpr uint64_t kAddressTagMask = ((UINT64_C(1) << kAddressTagWidth) - 1)
                                     << kAddressTagOffset;
static_assert(kAddressTagMask == UINT64_C(0xff00000000000000),
              "AddressTagMask must represent most-significant eight bits.");

constexpr uint64_t kTTBRMask = UINT64_C(1) << 55;

// AArch64 floating-point specifics. These match IEEE-754.
constexpr unsigned kDoubleMantissaBits = 52;
constexpr unsigned kDoubleExponentBits = 11;
constexpr unsigned kDoubleExponentBias = 1023;
constexpr unsigned kFloatMantissaBits = 23;
constexpr unsigned kFloatExponentBits = 8;
constexpr unsigned kFloatExponentBias = 127;
constexpr unsigned kFloat16MantissaBits = 10;
constexpr unsigned kFloat16ExponentBits = 5;
constexpr unsigned kFloat16ExponentBias = 15;

// The actual value of the kRootRegister is offset from the IsolateData's start
// to take advantage of negative displacement values.
constexpr int kRootRegisterBias = 256;

using float16 = uint16_t;

#define INSTRUCTION_FIELDS_LIST(V_)                     \
  /* Register fields */                                 \
  V_(Rd, 4, 0, Bits)    /* Destination register.     */ \
  V_(Rn, 9, 5, Bits)    /* First source register.    */ \
  V_(Rm, 20, 16, Bits)  /* Second source register.   */ \
  V_(Ra, 14, 10, Bits)  /* Third source register.    */ \
  V_(Rt, 4, 0, Bits)    /* Load dest / store source. */ \
  V_(Rt2, 14, 10, Bits) /* Load second dest /        */ \
                        /* store second source.      */ \
  V_(Rs, 20, 16, Bits)  /* Store-exclusive status    */ \
  V_(PrefetchMode, 4, 0, Bits)                          \
                                                        \
  /* Common bits */                                     \
  V_(SixtyFourBits, 31, 31, Bits)                       \
  V_(FlagsUpdate, 29, 29, Bits)                         \
                                                        \
  /* PC relative addressing */                          \
  V_(ImmPCRelHi, 23, 5, SignedBits)                     \
  V_(ImmPCRelLo, 30, 29, Bits)                          \
                                                        \
  /* Add/subtract/logical shift register */             \
  V_(ShiftDP, 23, 22, Bits)                             \
  V_(ImmDPShift, 15, 10, Bits)                          \
                                                        \
  /* Add/subtract immediate */                          \
  V_(ImmAddSub, 21, 10, Bits)                           \
  V_(ShiftAddSub, 23, 22, Bits)                         \
                                                        \
  /* Add/subtract extend */                             \
  V_(ImmExtendShift, 12, 10, Bits)                      \
  V_(ExtendMode, 15, 13, Bits)                          \
                                                        \
  /* Move wide */                                       \
  V_(ImmMoveWide, 20, 5, Bits)                          \
  V_(ShiftMoveWide, 22, 21, Bits)                       \
                                                        \
  /* Logical immediate, bitfield and extract */         \
  V_(BitN, 22, 22, Bits)                                \
  V_(ImmRotate, 21, 16, Bits)                           \
  V_(ImmSetBits, 15, 10, Bits)                          \
  V_(ImmR, 21, 16, Bits)                                \
  V_(ImmS, 15, 10, Bits)                                \
                                                        \
  /* Test and branch immediate */                       \
  V_(ImmTestBranch, 18, 5, SignedBits)                  \
  V_(ImmTestBranchBit40, 23, 19, Bits)                  \
  V_(ImmTestBranchBit5, 31, 31, Bits)                   \
                                                        \
  /* Conditionals */                                    \
  V_(Condition, 15, 12, Bits)                           \
  V_(ConditionBranch, 3, 0, Bits)                       \
  V_(Nzcv, 3, 0, Bits)                                  \
  V_(ImmCondCmp, 20, 16, Bits)                          \
  V_(ImmCondBranch, 23, 5, SignedBits)                  \
                                                        \
  /* Floating point */                                  \
  V_(FPType, 23, 22, Bits)                              \
  V_(ImmFP, 20, 13, Bits)                               \
  V_(FPScale, 15, 10, Bits)                             \
                                                        \
  /* Load Store */                                      \
  V_(ImmLS, 20, 12, SignedBits)                         \
  V_(ImmLSUnsigned, 21, 10, Bits)                       \
  V_(ImmLSPair, 21, 15, SignedBits)                     \
  V_(ImmShiftLS, 12, 12, Bits)                          \
  V_(LSOpc, 23, 22, Bits)                               \
  V_(LSVector, 26, 26, Bits)                            \
  V_(LSSize, 31, 30, Bits)                              \
                                                        \
  /* NEON generic fields */                             \
  V_(NEONQ, 30, 30, Bits)                               \
  V_(NEONSize, 23, 22, Bits)                            \
  V_(NEONLSSize, 11, 10, Bits)                          \
  V_(NEONS, 12, 12, Bits)                               \
  V_(NEONL, 21, 21, Bits)                               \
  V_(NEONM, 20, 20, Bits)                               \
  V_(NEONH, 11, 11, Bits)                               \
  V_(ImmNEONExt, 14, 11, Bits)                          \
  V_(ImmNEON5, 20, 16, Bits)                            \
  V_(ImmNEON4, 14, 11, Bits)                            \
                                                        \
  /* Other immediates */                                \
  V_(ImmUncondBranch, 25, 0, SignedBits)                \
  V_(ImmCmpBranch, 23, 5, SignedBits)                   \
  V_(ImmLLiteral, 23, 5, SignedBits)                    \
  V_(ImmException, 20, 5, Bits)                         \
  V_(ImmHint, 11, 5, Bits)                              \
  V_(ImmBarrierDomain, 11, 10, Bits)                    \
  V_(ImmBarrierType, 9, 8, Bits)                        \
                                                        \
  /* System (MRS, MSR) */                               \
  V_(ImmSystemRegister, 19, 5, Bits)                    \
  V_(SysO0, 19, 19, Bits)                               \
  V_(SysOp1, 18, 16, Bits)                              \
  V_(SysOp2, 7, 5, Bits)                                \
  V_(CRn, 15, 12, Bits)                                 \
  V_(CRm, 11, 8, Bits)                                  \
                                                        \
  /* Load-/store-exclusive */                           \
  V_(LoadStoreXLoad, 22, 22, Bits)                      \
  V_(LoadStoreXNotExclusive, 23, 23, Bits)              \
  V_(LoadStoreXAcquireRelease, 15, 15, Bits)            \
  V_(LoadStoreXSizeLog2, 31, 30, Bits)                  \
  V_(LoadStoreXPair, 21, 21, Bits)                      \
                                                        \
  /* NEON load/store */                                 \
  V_(NEONLoad, 22, 22, Bits)                            \
                                                        \
  /* NEON Modified Immediate fields */                  \
  V_(ImmNEONabc, 18, 16, Bits)                          \
  V_(ImmNEONdefgh, 9, 5, Bits)                          \
  V_(NEONModImmOp, 29, 29, Bits)                        \
  V_(NEONCmode, 15, 12, Bits)                           \
                                                        \
  /* NEON Shift Immediate fields */                     \
  V_(ImmNEONImmhImmb, 22, 16, Bits)                     \
  V_(ImmNEONImmh, 22, 19, Bits)                         \
  V_(ImmNEONImmb, 18, 16, Bits)

#define SYSTEM_REGISTER_FIELDS_LIST(V_, M_) \
  /* NZCV */                                \
  V_(Flags, 31, 28, Bits, uint32_t)         \
  V_(N, 31, 31, Bits, bool)                 \
  V_(Z, 30, 30, Bits, bool)                 \
  V_(C, 29, 29, Bits, bool)                 \
  V_(V, 28, 28, Bits, bool)                 \
  M_(NZCV, Flags_mask)                      \
                                            \
  /* FPCR */                                \
  V_(AHP, 26, 26, Bits, bool)               \
  V_(DN, 25, 25, Bits, bool)                \
  V_(FZ, 24, 24, Bits, bool)                \
  V_(RMode, 23, 22, Bits, FPRounding)       \
  M_(FPCR, AHP_mask | DN_mask | FZ_mask | RMode_mask)

// Fields offsets.
#define DECLARE_FIELDS_OFFSETS(Name, HighBit, LowBit, unused_1, unused_2) \
  constexpr int Name##_offset = LowBit;                                   \
  constexpr int Name##_width = HighBit - LowBit + 1;                      \
  constexpr uint32_t Name##_mask = ((1 << Name##_width) - 1) << LowBit;
#define DECLARE_INSTRUCTION_FIELDS_OFFSETS(Name, HighBit, LowBit, unused_1) \
  DECLARE_FIELDS_OFFSETS(Name, HighBit, LowBit, unused_1, unused_2)
INSTRUCTION_FIELDS_LIST(DECLARE_INSTRUCTION_FIELDS_OFFSETS)
SYSTEM_REGISTER_FIELDS_LIST(DECLARE_FIELDS_OFFSETS, NOTHING)
#undef DECLARE_FIELDS_OFFSETS
#undef DECLARE_INSTRUCTION_FIELDS_OFFSETS

// ImmPCRel is a compound field (not present in INSTRUCTION_FIELDS_LIST), formed
// from ImmPCRelLo and ImmPCRelHi.
constexpr int ImmPCRel_mask = ImmPCRelLo_mask | ImmPCRelHi_mask;

// Condition codes.
enum Condition : int {
  eq = 0,   // Equal
  ne = 1,   // Not equal
  hs = 2,   // Unsigned higher or same (or carry set)
  cs = hs,  //   --
  lo = 3,   // Unsigned lower (or carry clear)
  cc = lo,  //   --
  mi = 4,   // Negative
  pl = 5,   // Positive or zero
  vs = 6,   // Signed overflow
  vc = 7,   // No signed overflow
  hi = 8,   // Unsigned higher
  ls = 9,   // Unsigned lower or same
  ge = 10,  // Signed greater than or equal
  lt = 11,  // Signed less than
  gt = 12,  // Signed greater than
  le = 13,  // Signed less than or equal
  al = 14,  // Always executed
  nv = 15,  // Behaves as always/al.

  // Unified cross-platform condition names/aliases.
  kEqual = eq,
  kNotEqual = ne,
  kLessThan = lt,
  kGreaterThan = gt,
  kLessThanEqual = le,
  kGreaterThanEqual = ge,
  kUnsignedLessThan = lo,
  kUnsignedGreaterThan = hi,
  kUnsignedLessThanEqual = ls,
  kUnsignedGreaterThanEqual = hs,
  kOverflow = vs,
  kNoOverflow = vc,
  kZero = eq,
  kNotZero = ne,
};

inline Condition NegateCondition(Condition cond) {
  // Conditions al and nv behave identically, as "always true". They can't be
  // inverted, because there is no never condition.
  DCHECK((cond != al) && (cond != nv));
  return static_cast<Condition>(cond ^ 1);
}

enum FlagsUpdate { SetFlags = 1, LeaveFlags = 0 };

enum StatusFlags {
  NoFlag = 0,

  // Derive the flag combinations from the system register bit descriptions.
  NFlag = N_mask,
  ZFlag = Z_mask,
  CFlag = C_mask,
  VFlag = V_mask,
  NZFlag = NFlag | ZFlag,
  NCFlag = NFlag | CFlag,
  NVFlag = NFlag | VFlag,
  ZCFlag = ZFlag | CFlag,
  ZVFlag = ZFlag | VFlag,
  CVFlag = CFlag | VFlag,
  NZCFlag = NFlag | ZFlag | CFlag,
  NZVFlag = NFlag | ZFlag | VFlag,
  NCVFlag = NFlag | CFlag | VFlag,
  ZCVFlag = ZFlag | CFlag | VFlag,
  NZCVFlag = NFlag | ZFlag | CFlag | VFlag,

  // Floating-point comparison results.
  FPEqualFlag = ZCFlag,
  FPLessThanFlag = NFlag,
  FPGreaterThanFlag = CFlag,
  FPUnorderedFlag = CVFlag
};

enum Shift {
  NO_SHIFT = -1,
  LSL = 0x0,
  LSR = 0x1,
  ASR = 0x2,
  ROR = 0x3,
  MSL = 0x4
};

enum Extend {
  NO_EXTEND = -1,
  UXTB = 0,
  UXTH = 1,
  UXTW = 2,
  UXTX = 3,
  SXTB = 4,
  SXTH = 5,
  SXTW = 6,
  SXTX = 7
};

enum SystemHint {
  NOP = 0,
  YIELD = 1,
  WFE = 2,
  WFI = 3,
  SEV = 4,
  SEVL = 5,
  CSDB = 20,
  BTI = 32,
  BTI_c = 34,
  BTI_j = 36,
  BTI_jc = 38
};

// In a guarded page, only BTI and PACI[AB]SP instructions are allowed to be
// the target of indirect branches. Details on which kinds of branches each
// instruction allows follow in the comments below:
enum class BranchTargetIdentifier {
  // Do not emit a BTI instruction.
  kNone,

  // Emit a BTI instruction. Cannot be the target of indirect jumps/calls.
  kBti,

  // Emit a "BTI c" instruction. Can be the target of indirect jumps (BR) with
  // x16/x17 as the target register, or indirect calls (BLR).
  kBtiCall,

  // Emit a "BTI j" instruction. Can be the target of indirect jumps (BR).
  kBtiJump,

  // Emit a "BTI jc" instruction, which is a combination of "BTI j" and "BTI c".
  kBtiJumpCall,

  // Emit a PACIBSP instruction, which acts like a "BTI c" or a "BTI jc",
  // based on the value of SCTLR_EL1.BT0.
  kPacibsp
};

enum BarrierDomain {
  OuterShareable = 0,
  NonShareable = 1,
  InnerShareable = 2,
  FullSystem = 3
};

enum BarrierType {
  BarrierOther = 0,
  BarrierReads = 1,
  BarrierWrites = 2,
  BarrierAll = 3
};

// System/special register names.
// This information is not encoded as one field but as the concatenation of
// multiple fields (Op0<0>, Op1, Crn, Crm, Op2).
enum SystemRegister {
  NZCV = ((0x1 << SysO0_offset) | (0x3 << SysOp1_offset) | (0x4 << CRn_offset) |
          (0x2 << CRm_offset) | (0x0 << SysOp2_offset)) >>
         ImmSystemRegister_offset,
  FPCR = ((0x1 << SysO0_offset) | (0x3 << SysOp1_offset) | (0x4 << CRn_offset) |
          (0x4 << CRm_offset) | (0x0 << SysOp2_offset)) >>
         ImmSystemRegister_offset
};

// Instruction enumerations.
//
// These are the masks that define a class of instructions, and the list of
// instructions within each class. Each enumeration has a Fixed, FMask and
// Mask value.
//
// Fixed: The fixed bits in this instruction class.
// FMask: The mask used to extract the fixed bits in the class.
// Mask:  The mask used to identify the instructions within a class.
//
// The enumerations can be used like this:
//
// DCHECK(instr->Mask(PCRelAddressingFMask) == PCRelAddressingFixed);
// switch(instr->Mask(PCRelAddressingMask)) {
//   case ADR:  Format("adr 'Xd, 'AddrPCRelByte"); break;
//   case ADRP: Format("adrp 'Xd, 'AddrPCRelPage"); break;
//   default:   printf("Unknown instruction\n");
// }

// Used to corrupt encodings by setting all bits when orred. Although currently
// unallocated in AArch64, this encoding is not guaranteed to be undefined
// indefinitely.
constexpr uint32_t kUnallocatedInstruction = 0xffffffff;

// Generic fields.
using GenericInstrField = uint32_t;
constexpr GenericInstrField SixtyFourBits = 0x80000000;
constexpr GenericInstrField ThirtyTwoBits = 0x00000000;
constexpr GenericInstrField FP32 = 0x00000000;
constexpr GenericInstrField FP64 = 0x00400000;

using NEONFormatField = uint32_t;
constexpr NEONFormatField NEONFormatFieldMask = 0x40C00000;
constexpr NEONFormatField NEON_Q = 0x40000000;
constexpr NEONFormatField NEON_sz = 0x00400000;
constexpr NEONFormatField NEON_8B = 0x00000000;
constexpr NEONFormatField NEON_16B = NEON_8B | NEON_Q;
constexpr NEONFormatField NEON_4H = 0x00400000;
constexpr NEONFormatField NEON_8H = NEON_4H | NEON_Q;
constexpr NEONFormatField NEON_2S = 0x00800000;
constexpr NEONFormatField NEON_4S = NEON_2S | NEON_Q;
constexpr NEONFormatField NEON_1D = 0x00C00000;
constexpr NEONFormatField NEON_2D = 0x00C00000 | NEON_Q;

using NEONFPFormatField = uint32_t;
constexpr NEONFPFormatField NEONFPFormatFieldMask = 0x40400000;
constexpr NEONFPFormatField NEON_FP_4H = 0x00000000;
constexpr NEONFPFormatField NEON_FP_8H = NEON_Q;
constexpr NEONFPFormatField NEON_FP_2S = FP32;
constexpr NEONFPFormatField NEON_FP_4S = FP32 | NEON_Q;
constexpr NEONFPFormatField NEON_FP_2D = FP64 | NEON_Q;

using NEONLSFormatField = uint32_t;
constexpr NEONLSFormatField NEONLSFormatFieldMask = 0x40000C00;
constexpr NEONLSFormatField LS_NEON_8B = 0x00000000;
constexpr NEONLSFormatField LS_NEON_16B = LS_NEON_8B | NEON_Q;
constexpr NEONLSFormatField LS_NEON_4H = 0x00000400;
constexpr NEONLSFormatField LS_NEON_8H = LS_NEON_4H | NEON_Q;
constexpr NEONLSFormatField LS_NEON_2S = 0x00000800;
constexpr NEONLSFormatField LS_NEON_4S = LS_NEON_2S | NEON_Q;
constexpr NEONLSFormatField LS_NEON_1D = 0x00000C00;
constexpr NEONLSFormatField LS_NEON_2D = LS_NEON_1D | NEON_Q;

using NEONScalarFormatField = uint32_t;
constexpr NEONScalarFormatField NEONScalarFormatFieldMask = 0x00C00000;
constexpr NEONScalarFormatField NEONScalar = 0x10000000;
constexpr NEONScalarFormatField NEON_B = 0x00000000;
constexpr NEONScalarFormatField NEON_H = 0x00400000;
constexpr NEONScalarFormatField NEON_S = 0x00800000;
constexpr NEONScalarFormatField NEON_D = 0x00C00000;

// PC relative addressing.
using PCRelAddressingOp = uint32_t;
constexpr PCRelAddressingOp PCRelAddressingFixed = 0x10000000;
constexpr PCRelAddressingOp PCRelAddressingFMask = 0x1F000000;
constexpr PCRelAddressingOp PCRelAddressingMask = 0x9F000000;
constexpr PCRelAddressingOp ADR = PCRelAddressingFixed | 0x00000000;
constexpr PCRelAddressingOp ADRP = PCRelAddressingFixed | 0x80000000;

// Add/sub (immediate, shifted and extended.)
constexpr int kSFOffset = 31;
using AddSubOp = uint32_t;
constexpr AddSubOp AddSubOpMask = 0x60000000;
constexpr AddSubOp AddSubSetFlagsBit = 0x20000000;
constexpr AddSubOp ADD = 0x00000000;
constexpr AddSubOp ADDS = ADD | AddSubSetFlagsBit;
constexpr AddSubOp SUB = 0x40000000;
constexpr AddSubOp SUBS = SUB | AddSubSetFlagsBit;

#define ADD_SUB_OP_LIST(V) \
  V(ADD);                  \
  V(ADDS);                 \
  V(SUB);                  \
  V(SUBS)

using AddSubImmediateOp = uint32_t;
constexpr AddSubImmediateOp AddSubImmediateFixed = 0x11000000;
constexpr AddSubImmediateOp AddSubImmediateFMask = 0x1F000000;
constexpr AddSubImmediateOp AddSubImmediateMask = 0xFF000000;
#define ADD_SUB_IMMEDIATE(A)                                        \
  constexpr AddSubImmediateOp A##_w_imm = AddSubImmediateFixed | A; \
  constexpr AddSubImmediateOp A##_x_imm =                           \
      AddSubImmediateFixed | A | SixtyFourBits
ADD_SUB_OP_LIST(ADD_SUB_IMMEDIATE);
#undef ADD_SUB_IMMEDIATE

using AddSubShiftedOp = uint32_t;
constexpr AddSubShiftedOp AddSubShiftedFixed = 0x0B000000;
constexpr AddSubShiftedOp AddSubShiftedFMask = 0x1F200000;
constexpr AddSubShiftedOp AddSubShiftedMask = 0xFF200000;
#define ADD_SUB_SHIFTED(A)                                        \
  constexpr AddSubShiftedOp A##_w_shift = AddSubShiftedFixed | A; \
  constexpr AddSubShiftedOp A##_x_shift = AddSubShiftedFixed | A | SixtyFourBits
ADD_SUB_OP_LIST(ADD_SUB_SHIFTED);
#undef ADD_SUB_SHIFTED

using AddSubExtendedOp = uint32_t;
constexpr AddSubExtendedOp AddSubExtendedFixed = 0x0B200000;
constexpr AddSubExtendedOp AddSubExtendedFMask = 0x1F200000;
constexpr AddSubExtendedOp AddSubExtendedMask = 0xFFE00000;
#define ADD_SUB_EXTENDED(A)                                       \
  constexpr AddSubExtendedOp A##_w_ext = AddSubExtendedFixed | A; \
  constexpr AddSubExtendedOp A##_x_ext = AddSubExtendedFixed | A | SixtyFourBits
ADD_SUB_OP_LIST(ADD_SUB_EXTENDED);
#undef ADD_SUB_EXTENDED

// Add/sub with carry.
using AddSubWithCarryOp = uint32_t;
constexpr AddSubWithCarryOp AddSubWithCarryFixed = 0x1A000000;
constexpr AddSubWithCarryOp AddSubWithCarryFMask = 0x1FE00000;
constexpr AddSubWithCarryOp AddSubWithCarryMask = 0xFFE0FC00;
constexpr AddSubWithCarryOp ADC_w = AddSubWithCarryFixed | ADD;
constexpr AddSubWithCarryOp ADC_x = AddSubWithCarryFixed | ADD | SixtyFourBits;
constexpr AddSubWithCarryOp ADC = ADC_w;
constexpr AddSubWithCarryOp ADCS_w = AddSubWithCarryFixed | ADDS;
constexpr AddSubWithCarryOp ADCS_x =
    AddSubWithCarryFixed | ADDS | SixtyFourBits;
constexpr AddSubWithCarryOp SBC_w = AddSubWithCarryFixed | SUB;
constexpr AddSubWithCarryOp SBC_x = AddSubWithCarryFixed | SUB | SixtyFourBits;
constexpr AddSubWithCarryOp SBC = SBC_w;
constexpr AddSubWithCarryOp SBCS_w = AddSubWithCarryFixed | SUBS;
constexpr AddSubWithCarryOp SBCS_x =
    AddSubWithCarryFixed | SUBS | SixtyFourBits;

// Logical (immediate and shifted register).
using LogicalOp = uint32_t;
constexpr LogicalOp LogicalOpMask = 0x60200000;
constexpr LogicalOp NOT = 0x00200000;
constexpr LogicalOp AND = 0x00000000;
constexpr LogicalOp BIC = AND | NOT;
constexpr LogicalOp ORR = 0x20000000;
constexpr LogicalOp ORN = ORR | NOT;
constexpr LogicalOp EOR = 0x40000000;
constexpr LogicalOp EON = EOR | NOT;
constexpr LogicalOp ANDS = 0x60000000;
constexpr LogicalOp BICS = ANDS | NOT;

// Logical immediate.
using LogicalImmediateOp = uint32_t;
constexpr LogicalImmediateOp LogicalImmediateFixed = 0x12000000;
constexpr LogicalImmediateOp LogicalImmediateFMask = 0x1F800000;
constexpr LogicalImmediateOp LogicalImmediateMask = 0xFF800000;
constexpr LogicalImmediateOp AND_w_imm = LogicalImmediateFixed | AND;
constexpr LogicalImmediateOp AND_x_imm =
    LogicalImmediateFixed | AND | SixtyFourBits;
constexpr LogicalImmediateOp ORR_w_imm = LogicalImmediateFixed | ORR;
constexpr LogicalImmediateOp ORR_x_imm =
    LogicalImmediateFixed | ORR | SixtyFourBits;
constexpr LogicalImmediateOp EOR_w_imm = LogicalImmediateFixed | EOR;
constexpr LogicalImmediateOp EOR_x_imm =
    LogicalImmediateFixed | EOR | SixtyFourBits;
constexpr LogicalImmediateOp ANDS_w_imm = LogicalImmediateFixed | ANDS;
constexpr LogicalImmediateOp ANDS_x_imm =
    LogicalImmediateFixed | ANDS | SixtyFourBits;

// Logical shifted register.
using LogicalShiftedOp = uint32_t;
constexpr LogicalShiftedOp LogicalShiftedFixed = 0x0A000000;
constexpr LogicalShiftedOp LogicalShiftedFMask = 0x1F000000;
constexpr LogicalShiftedOp LogicalShiftedMask = 0xFF200000;
constexpr LogicalShiftedOp AND_w = LogicalShiftedFixed | AND;
constexpr LogicalShiftedOp AND_x = LogicalShiftedFixed | AND | SixtyFourBits;
constexpr LogicalShiftedOp AND_shift = AND_w;
constexpr LogicalShiftedOp BIC_w = LogicalShiftedFixed | BIC;
constexpr LogicalShiftedOp BIC_x = LogicalShiftedFixed | BIC | SixtyFourBits;
constexpr LogicalShiftedOp BIC_shift = BIC_w;
constexpr LogicalShiftedOp ORR_w = LogicalShiftedFixed | ORR;
constexpr LogicalShiftedOp ORR_x = LogicalShiftedFixed | ORR | SixtyFourBits;
constexpr LogicalShiftedOp ORR_shift = ORR_w;
constexpr LogicalShiftedOp ORN_w = LogicalShiftedFixed | ORN;
constexpr LogicalShiftedOp ORN_x = LogicalShiftedFixed | ORN | SixtyFourBits;
constexpr LogicalShiftedOp ORN_shift = ORN_w;
constexpr LogicalShiftedOp EOR_w = LogicalShiftedFixed | EOR;
constexpr LogicalShiftedOp EOR_x = LogicalShiftedFixed | EOR | SixtyFourBits;
constexpr LogicalShiftedOp EOR_shift = EOR_w;
constexpr LogicalShiftedOp EON_w = LogicalShiftedFixed | EON;
constexpr LogicalShiftedOp EON_x = LogicalShiftedFixed | EON | SixtyFourBits;
constexpr LogicalShiftedOp EON_shift = EON_w;
constexpr LogicalShiftedOp ANDS_w = LogicalShiftedFixed | ANDS;
constexpr LogicalShiftedOp ANDS_x = LogicalShiftedFixed | ANDS | SixtyFourBits;
constexpr LogicalShiftedOp ANDS_shift = ANDS_w;
constexpr LogicalShiftedOp BICS_w = LogicalShiftedFixed | BICS;
constexpr LogicalShiftedOp BICS_x = LogicalShiftedFixed | BICS | SixtyFourBits;
constexpr LogicalShiftedOp BICS_shift = BICS_w;

// Move wide immediate.
using MoveWideImmediateOp = uint32_t;
constexpr MoveWideImmediateOp MoveWideImmediateFixed = 0x12800000;
constexpr MoveWideImmediateOp MoveWideImmediateFMask = 0x1F800000;
constexpr MoveWideImmediateOp MoveWideImmediateMask = 0xFF800000;
constexpr MoveWideImmediateOp MOVN = 0x00000000;
constexpr MoveWideImmediateOp MOVZ = 0x40000000;
constexpr MoveWideImmediateOp MOVK = 0x60000000;
constexpr MoveWideImmediateOp MOVN_w = MoveWideImmediateFixed | MOVN;
constexpr MoveWideImmediateOp MOVN_x =
    MoveWideImmediateFixed | MOVN | SixtyFourBits;
constexpr MoveWideImmediateOp MOVZ_w = MoveWideImmediateFixed | MOVZ;
constexpr MoveWideImmediateOp MOVZ_x =
    MoveWideImmediateFixed | MOVZ | SixtyFourBits;
constexpr MoveWideImmediateOp MOVK_w = MoveWideImmediateFixed | MOVK;
constexpr MoveWideImmediateOp MOVK_x =
    MoveWideImmediateFixed | MOVK | SixtyFourBits;

// Bitfield.
constexpr int kBitfieldNOffset = 22;
using BitfieldOp = uint32_t;
constexpr BitfieldOp BitfieldFixed = 0x13000000;
constexpr BitfieldOp BitfieldFMask = 0x1F800000;
constexpr BitfieldOp BitfieldMask = 0xFF800000;
constexpr BitfieldOp SBFM_w = BitfieldFixed | 0x00000000;
constexpr BitfieldOp SBFM_x = BitfieldFixed | 0x80000000;
constexpr BitfieldOp SBFM = SBFM_w;
constexpr BitfieldOp BFM_w = BitfieldFixed | 0x20000000;
constexpr BitfieldOp BFM_x = BitfieldFixed | 0xA0000000;
constexpr BitfieldOp BFM = BFM_w;
constexpr BitfieldOp UBFM_w = BitfieldFixed | 0x40000000;
constexpr BitfieldOp UBFM_x = BitfieldFixed | 0xC0000000;
constexpr BitfieldOp UBFM = UBFM_w;
// Bitfield N field.

// Extract.
using ExtractOp = uint32_t;
constexpr ExtractOp ExtractFixed = 0x13800000;
constexpr ExtractOp ExtractFMask = 0x1F800000;
constexpr ExtractOp ExtractMask = 0xFFA00000;
constexpr ExtractOp EXTR_w = ExtractFixed | 0x00000000;
constexpr ExtractOp EXTR_x = ExtractFixed | 0x80000000;
constexpr ExtractOp EXTR = EXTR_w;

// Unconditional branch.
using UnconditionalBranchOp = uint32_t;
constexpr UnconditionalBranchOp UnconditionalBranchFixed = 0x14000000;
constexpr UnconditionalBranchOp UnconditionalBranchFMask = 0x7C000000;
constexpr UnconditionalBranchOp UnconditionalBranchMask = 0xFC000000;
constexpr UnconditionalBranchOp B = UnconditionalBranchFixed | 0x00000000;
constexpr UnconditionalBranchOp BL = UnconditionalBranchFixed | 0x80000000;

// Unconditional branch to register.
using UnconditionalBranchToRegisterOp = uint32_t;
constexpr UnconditionalBranchToRegisterOp UnconditionalBranchToRegisterFixed =
    0xD6000000;
constexpr UnconditionalBranchToRegisterOp UnconditionalBranchToRegisterFMask =
    0xFE000000;
constexpr UnconditionalBranchToRegisterOp UnconditionalBranchToRegisterMask =
    0xFFFFFC1F;
constexpr UnconditionalBranchToRegisterOp BR =
    UnconditionalBranchToRegisterFixed | 0x001F0000;
constexpr UnconditionalBranchToRegisterOp BLR =
    UnconditionalBranchToRegisterFixed | 0x003F0000;
constexpr UnconditionalBranchToRegisterOp RET =
    UnconditionalBranchToRegisterFixed | 0x005F0000;

// Compare and branch.
using CompareBranchOp = uint32_t;
constexpr CompareBranchOp CompareBranchFixed = 0x34000000;
constexpr CompareBranchOp CompareBranchFMask = 0x7E000000;
constexpr CompareBranchOp CompareBranchMask = 0xFF000000;
constexpr CompareBranchOp CBZ_w = CompareBranchFixed | 0x00000000;
constexpr CompareBranchOp CBZ_x = CompareBranchFixed | 0x80000000;
constexpr CompareBranchOp CBZ = CBZ_w;
constexpr CompareBranchOp CBNZ_w = CompareBranchFixed | 0x01000000;
constexpr CompareBranchOp CBNZ_x = CompareBranchFixed | 0x81000000;
constexpr CompareBranchOp CBNZ = CBNZ_w;

// Test and branch.
using TestBranchOp = uint32_t;
constexpr TestBranchOp TestBranchFixed = 0x36000000;
constexpr TestBranchOp TestBranchFMask = 0x7E000000;
constexpr TestBranchOp TestBranchMask = 0x7F000000;
constexpr TestBranchOp TBZ = TestBranchFixed | 0x00000000;
constexpr TestBranchOp TBNZ = TestBranchFixed | 0x01000000;

// Conditional branch.
using ConditionalBranchOp = uint32_t;
constexpr ConditionalBranchOp ConditionalBranchFixed = 0x54000000;
constexpr ConditionalBranchOp ConditionalBranchFMask = 0xFE000000;
constexpr ConditionalBranchOp ConditionalBranchMask = 0xFF000010;
constexpr ConditionalBranchOp B_cond = ConditionalBranchFixed | 0x00000000;

// System.
// System instruction encoding is complicated because some instructions use op
// and CR fields to encode parameters. To handle this cleanly, the system
// instructions are split into more than one group.

using SystemOp = uint32_t;
constexpr SystemOp SystemFixed = 0xD5000000;
constexpr SystemOp SystemFMask = 0xFFC00000;

using SystemSysRegOp = uint32_t;
constexpr SystemSysRegOp SystemSysRegFixed = 0xD5100000;
constexpr SystemSysRegOp SystemSysRegFMask = 0xFFD00000;
constexpr SystemSysRegOp SystemSysRegMask = 0xFFF00000;
constexpr SystemSysRegOp MRS = SystemSysRegFixed | 0x00200000;
constexpr SystemSysRegOp MSR = SystemSysRegFixed | 0x00000000;

using SystemHintOp = uint32_t;
constexpr SystemHintOp SystemHintFixed = 0xD503201F;
constexpr SystemHintOp SystemHintFMask = 0xFFFFF01F;
constexpr SystemHintOp SystemHintMask = 0xFFFFF01F;
constexpr SystemHintOp HINT = SystemHintFixed | 0x00000000;

// Exception.
using ExceptionOp = uint32_t;
constexpr ExceptionOp ExceptionFixed = 0xD4000000;
constexpr ExceptionOp ExceptionFMask = 0xFF000000;
constexpr ExceptionOp ExceptionMask = 0xFFE0001F;
constexpr ExceptionOp HLT = ExceptionFixed | 0x00400000;
constexpr ExceptionOp BRK = ExceptionFixed | 0x00200000;
constexpr ExceptionOp SVC = ExceptionFixed | 0x00000001;
constexpr ExceptionOp HVC = ExceptionFixed | 0x00000002;
constexpr ExceptionOp SMC = ExceptionFixed | 0x00000003;
constexpr ExceptionOp DCPS1 = ExceptionFixed | 0x00A00001;
constexpr ExceptionOp DCPS2 = ExceptionFixed | 0x00A00002;
constexpr ExceptionOp DCPS3 = ExceptionFixed | 0x00A00003;
// Code used to spot hlt instructions that should not be hit.
constexpr int kHltBadCode = 0xbad;

using MemBarrierOp = uint32_t;
constexpr MemBarrierOp MemBarrierFixed = 0xD503309F;
constexpr MemBarrierOp MemBarrierFMask = 0xFFFFF09F;
constexpr MemBarrierOp MemBarrierMask = 0xFFFFF0FF;
constexpr MemBarrierOp DSB = MemBarrierFixed | 0x00000000;
constexpr MemBarrierOp DMB = MemBarrierFixed | 0x00000020;
constexpr MemBarrierOp ISB = MemBarrierFixed | 0x00000040;

using SystemPAuthOp = uint32_t;
constexpr SystemPAuthOp SystemPAuthFixed = 0xD503211F;
constexpr SystemPAuthOp SystemPAuthFMask = 0xFFFFFD1F;
constexpr SystemPAuthOp SystemPAuthMask = 0xFFFFFFFF;
constexpr SystemPAuthOp PACIB1716 = SystemPAuthFixed | 0x00000140;
constexpr SystemPAuthOp AUTIB1716 = SystemPAuthFixed | 0x000001C0;
constexpr SystemPAuthOp PACIBSP = SystemPAuthFixed | 0x00000360;
constexpr SystemPAuthOp AUTIBSP = SystemPAuthFixed | 0x000003E0;

// Any load or store (including pair).
using LoadStoreAnyOp = uint32_t;
constexpr LoadStoreAnyOp LoadStoreAnyFMask = 0x0A000000;
constexpr LoadStoreAnyOp LoadStoreAnyFixed = 0x08000000;

// Any load pair or store pair.
using LoadStorePairAnyOp = uint32_t;
constexpr LoadStorePairAnyOp LoadStorePairAnyFMask = 0x3A000000;
constexpr LoadStorePairAnyOp LoadStorePairAnyFixed = 0x28000000;

#define LOAD_STORE_PAIR_OP_LIST(V) \
  V(STP, w, 0x00000000);           \
  V(LDP, w, 0x00400000);           \
  V(LDPSW, x, 0x40400000);         \
  V(STP, x, 0x80000000);           \
  V(LDP, x, 0x80400000);           \
  V(STP, s, 0x04000000);           \
  V(LDP, s, 0x04400000);           \
  V(STP, d, 0x44000000);           \
  V(LDP, d, 0x44400000);           \
  V(STP, q, 0x84000000);           \
  V(LDP, q, 0x84400000)

// Load/store pair (post, pre and offset.)
using LoadStorePairOp = uint32_t;
constexpr LoadStorePairOp LoadStorePairMask = 0xC4400000;
constexpr LoadStorePairOp LoadStorePairLBit = 1 << 22;
#define LOAD_STORE_PAIR(A, B, C) constexpr LoadStorePairOp A##_##B = C
LOAD_STORE_PAIR_OP_LIST(LOAD_STORE_PAIR);
#undef LOAD_STORE_PAIR

using LoadStorePairPostIndexOp = uint32_t;
constexpr LoadStorePairPostIndexOp LoadStorePairPostIndexFixed = 0x28800000;
constexpr LoadStorePairPostIndexOp LoadStorePairPostIndexFMask = 0x3B800000;
constexpr LoadStorePairPostIndexOp LoadStorePairPostIndexMask = 0xFFC00000;
#define LOAD_STORE_PAIR_POST_INDEX(A, B, C)           \
  constexpr LoadStorePairPostIndexOp A##_##B##_post = \
      LoadStorePairPostIndexFixed | A##_##B
LOAD_STORE_PAIR_OP_LIST(LOAD_STORE_PAIR_POST_INDEX);
#undef LOAD_STORE_PAIR_POST_INDEX

using LoadStorePairPreIndexOp = uint32_t;
constexpr LoadStorePairPreIndexOp LoadStorePairPreIndexFixed = 0x29800000;
constexpr LoadStorePairPreIndexOp LoadStorePairPreIndexFMask = 0x3B800000;
constexpr LoadStorePairPreIndexOp LoadStorePairPreIndexMask = 0xFFC00000;
#define LOAD_STORE_PAIR_PRE_INDEX(A, B, C)          \
  constexpr LoadStorePairPreIndexOp A##_##B##_pre = \
      LoadStorePairPreIndexFixed | A##_##B
LOAD_STORE_PAIR_OP_LIST(LOAD_STORE_PAIR_PRE_INDEX);
#undef LOAD_STORE_PAIR_PRE_INDEX

using LoadStorePairOffsetOp = uint32_t;
constexpr LoadStorePairOffsetOp LoadStorePairOffsetFixed = 0x29000000;
constexpr LoadStorePairOffsetOp LoadStorePairOffsetFMask = 0x3B800000;
constexpr LoadStorePairOffsetOp LoadStorePairOffsetMask = 0xFFC00000;
#define LOAD_STORE_PAIR_OFFSET(A, B, C)           \
  constexpr LoadStorePairOffsetOp A##_##B##_off = \
      LoadStorePairOffsetFixed | A##_##B
LOAD_STORE_PAIR_OP_LIST(LOAD_STORE_PAIR_OFFSET);
#undef LOAD_STORE_PAIR_OFFSET

// Load literal.
using LoadLiteralOp = uint32_t;
constexpr LoadLiteralOp LoadLiteralFixed = 0x18000000;
constexpr LoadLiteralOp LoadLiteralFMask = 0x3B000000;
constexpr LoadLiteralOp LoadLiteralMask = 0xFF000000;
constexpr LoadLiteralOp LDR_w_lit = LoadLiteralFixed | 0x00000000;
constexpr LoadLiteralOp LDR_x_lit = LoadLiteralFixed | 0x40000000;
constexpr LoadLiteralOp LDRSW_x_lit = LoadLiteralFixed | 0x80000000;
constexpr LoadLiteralOp PRFM_lit = LoadLiteralFixed | 0xC0000000;
constexpr LoadLiteralOp LDR_s_lit = LoadLiteralFixed | 0x04000000;
constexpr LoadLiteralOp LDR_d_lit = LoadLiteralFixed | 0x44000000;

#define LOAD_STORE_OP_LIST(V) \
  V(ST, RB, w, 0x00000000);   \
  V(ST, RH, w, 0x40000000);   \
  V(ST, R, w, 0x80000000);    \
  V(ST, R, x, 0xC0000000);    \
  V(LD, RB, w, 0x00400000);   \
  V(LD, RH, w, 0x40400000);   \
  V(LD, R, w, 0x80400000);    \
  V(LD, R, x, 0xC0400000);    \
  V(LD, RSB, x, 0x00800000);  \
  V(LD, RSH, x, 0x40800000);  \
  V(LD, RSW, x, 0x80800000);  \
  V(LD, RSB, w, 0x00C00000);  \
  V(LD, RSH, w, 0x40C00000);  \
  V(ST, R, b, 0x04000000);    \
  V(ST, R, h, 0x44000000);    \
  V(ST, R, s, 0x84000000);    \
  V(ST, R, d, 0xC4000000);    \
  V(ST, R, q, 0x04800000);    \
  V(LD, R, b, 0x04400000);    \
  V(LD, R, h, 0x44400000);    \
  V(LD, R, s, 0x84400000);    \
  V(LD, R, d, 0xC4400000);    \
  V(LD, R, q, 0x04C00000)

// Load/store unscaled offset.
using LoadStoreUnscaledOffsetOp = uint32_t;
constexpr LoadStoreUnscaledOffsetOp LoadStoreUnscaledOffsetFixed = 0x38000000;
constexpr LoadStoreUnscaledOffsetOp LoadStoreUnscaledOffsetFMask = 0x3B200C00;
constexpr LoadStoreUnscaledOffsetOp LoadStoreUnscaledOffsetMask = 0xFFE00C00;
#define LOAD_STORE_UNSCALED(A, B, C, D)               \
  constexpr LoadStoreUnscaledOffsetOp A##U##B##_##C = \
      LoadStoreUnscaledOffsetFixed | D
LOAD_STORE_OP_LIST(LOAD_STORE_UNSCALED);
#undef LOAD_STORE_UNSCALED

// Load/store (post, pre, offset and unsigned.)
using LoadStoreOp = uint32_t;
constexpr LoadStoreOp LoadStoreMask = 0xC4C00000;
#define LOAD_STORE(A, B, C, D) constexpr LoadStoreOp A##B##_##C = D
LOAD_STORE_OP_LIST(LOAD_STORE);
#undef LOAD_STORE
constexpr LoadStoreOp PRFM = 0xC0800000;

// Load/store post index.
using LoadStorePostIndex = uint32_t;
constexpr LoadStorePostIndex LoadStorePostIndexFixed = 0x38000400;
constexpr LoadStorePostIndex LoadStorePostIndexFMask = 0x3B200C00;
constexpr LoadStorePostIndex LoadStorePostIndexMask = 0xFFE00C00;
#define LOAD_STORE_POST_INDEX(A, B, C, D) \
  constexpr LoadStorePostIndex A##B##_##C##_post = LoadStorePostIndexFixed | D
LOAD_STORE_OP_LIST(LOAD_STORE_POST_INDEX);
#undef LOAD_STORE_POST_INDEX

// Load/store pre index.
using LoadStorePreIndex = uint32_t;
constexpr LoadStorePreIndex LoadStorePreIndexFixed = 0x38000C00;
constexpr LoadStorePreIndex LoadStorePreIndexFMask = 0x3B200C00;
constexpr LoadStorePreIndex LoadStorePreIndexMask = 0xFFE00C00;
#define LOAD_STORE_PRE_INDEX(A, B, C, D) \
  constexpr LoadStorePreIndex A##B##_##C##_pre = LoadStorePreIndexFixed | D
LOAD_STORE_OP_LIST(LOAD_STORE_PRE_INDEX);
#undef LOAD_STORE_PRE_INDEX

// Load/store unsigned offset.
using LoadStoreUnsignedOffset = uint32_t;
constexpr LoadStoreUnsignedOffset LoadStoreUnsignedOffsetFixed = 0x39000000;
constexpr LoadStoreUnsignedOffset LoadStoreUnsignedOffsetFMask = 0x3B000000;
constexpr LoadStoreUnsignedOffset LoadStoreUnsignedOffsetMask = 0xFFC00000;
constexpr LoadStoreUnsignedOffset PRFM_unsigned =
    LoadStoreUnsignedOffsetFixed | PRFM;
#define LOAD_STORE_UNSIGNED_OFFSET(A, B, C, D)              \
  constexpr LoadStoreUnsignedOffset A##B##_##C##_unsigned = \
      LoadStoreUnsignedOffsetFixed | D
LOAD_STORE_OP_LIST(LOAD_STORE_UNSIGNED_OFFSET);
#undef LOAD_STORE_UNSIGNED_OFFSET

// Load/store register offset.
using LoadStoreRegisterOffset = uint32_t;
constexpr LoadStoreRegisterOffset LoadStoreRegisterOffsetFixed = 0x38200800;
constexpr LoadStoreRegisterOffset LoadStoreRegisterOffsetFMask = 0x3B200C00;
constexpr LoadStoreRegisterOffset LoadStoreRegisterOffsetMask = 0xFFE00C00;
constexpr LoadStoreRegisterOffset PRFM_reg =
    LoadStoreRegisterOffsetFixed | PRFM;
#define LOAD_STORE_REGISTER_OFFSET(A, B, C, D)         \
  constexpr LoadStoreRegisterOffset A##B##_##C##_reg = \
      LoadStoreRegisterOffsetFixed | D
LOAD_STORE_OP_LIST(LOAD_STORE_REGISTER_OFFSET);
#undef LOAD_STORE_REGISTER_OFFSET

// Load/store acquire/release.
using LoadStoreAcquireReleaseOp = uint32_t;
constexpr LoadStoreAcquireReleaseOp LoadStoreAcquireReleaseFixed = 0x08000000;
constexpr LoadStoreAcquireReleaseOp LoadStoreAcquireReleaseFMask = 0x3F000000;
constexpr LoadStoreAcquireReleaseOp LoadStoreAcquireReleaseMask = 0xCFE08000;
constexpr LoadStoreAcquireReleaseOp STLXR_b =
    LoadStoreAcquireReleaseFixed | 0x00008000;
constexpr LoadStoreAcquireReleaseOp LDAXR_b =
    LoadStoreAcquireReleaseFixed | 0x00408000;
constexpr LoadStoreAcquireReleaseOp STLR_b =
    LoadStoreAcquireReleaseFixed | 0x00808000;
constexpr LoadStoreAcquireReleaseOp LDAR_b =
    LoadStoreAcquireReleaseFixed | 0x00C08000;
constexpr LoadStoreAcquireReleaseOp STLXR_h =
    LoadStoreAcquireReleaseFixed | 0x40008000;
constexpr LoadStoreAcquireReleaseOp LDAXR_h =
    LoadStoreAcquireReleaseFixed | 0x40408000;
constexpr LoadStoreAcquireReleaseOp STLR_h =
    LoadStoreAcquireReleaseFixed | 0x40808000;
constexpr LoadStoreAcquireReleaseOp LDAR_h =
    LoadStoreAcquireReleaseFixed | 0x40C08000;
constexpr LoadStoreAcquireReleaseOp STLXR_w =
    LoadStoreAcquireReleaseFixed | 0x80008000;
constexpr LoadStoreAcquireReleaseOp LDAXR_w =
    LoadStoreAcquireReleaseFixed | 0x80408000;
constexpr LoadStoreAcquireReleaseOp STLR_w =
    LoadStoreAcquireReleaseFixed | 0x80808000;
constexpr LoadStoreAcquireReleaseOp LDAR_w =
    LoadStoreAcquireReleaseFixed | 0x80C08000;
constexpr LoadStoreAcquireReleaseOp STLXR_x =
    LoadStoreAcquireReleaseFixed | 0xC0008000;
constexpr LoadStoreAcquireReleaseOp LDAXR_x =
    LoadStoreAcquireReleaseFixed | 0xC0408000;
constexpr LoadStoreAcquireReleaseOp STLR_x =
    LoadStoreAcquireReleaseFixed | 0xC0808000;
constexpr LoadStoreAcquireReleaseOp LDAR_x =
    LoadStoreAcquireReleaseFixed | 0xC0C08000;

// Compare and swap acquire/release [Armv8.1].
constexpr LoadStoreAcquireReleaseOp LSEBit_l = 0x00400000;
constexpr LoadStoreAcquireReleaseOp LSEBit_o0 = 0x00008000;
constexpr LoadStoreAcquireReleaseOp LSEBit_sz = 0x40000000;
constexpr LoadStoreAcquireReleaseOp CASFixed =
    LoadStoreAcquireReleaseFixed | 0x80A00000;
constexpr LoadStoreAcquireReleaseOp CASBFixed =
    LoadStoreAcquireReleaseFixed | 0x00A00000;
constexpr LoadStoreAcquireReleaseOp CASHFixed =
    LoadStoreAcquireReleaseFixed | 0x40A00000;
constexpr LoadStoreAcquireReleaseOp CASPFixed =
    LoadStoreAcquireReleaseFixed | 0x00200000;
constexpr LoadStoreAcquireReleaseOp CAS_w = CASFixed;
constexpr LoadStoreAcquireReleaseOp CAS_x = CASFixed | LSEBit_sz;
constexpr LoadStoreAcquireReleaseOp CASA_w = CASFixed | LSEBit_l;
constexpr LoadStoreAcquireReleaseOp CASA_x = CASFixed | LSEBit_l | LSEBit_sz;
constexpr LoadStoreAcquireReleaseOp CASL_w = CASFixed | LSEBit_o0;
constexpr LoadStoreAcquireReleaseOp CASL_x = CASFixed | LSEBit_o0 | LSEBit_sz;
constexpr LoadStoreAcquireReleaseOp CASAL_w = CASFixed | LSEBit_l | LSEBit_o0;
constexpr LoadStoreAcquireReleaseOp CASAL_x =
    CASFixed | LSEBit_l | LSEBit_o0 | LSEBit_sz;
constexpr LoadStoreAcquireReleaseOp CASB = CASBFixed;
constexpr LoadStoreAcquireReleaseOp CASAB = CASBFixed | LSEBit_l;
constexpr LoadStoreAcquireReleaseOp CASLB = CASBFixed | LSEBit_o0;
constexpr LoadStoreAcquireReleaseOp CASALB = CASBFixed | LSEBit_l | LSEBit_o0;
constexpr LoadStoreAcquireReleaseOp CASH = CASHFixed;
constexpr LoadStoreAcquireReleaseOp CASAH = CASHFixed | LSEBit_l;
constexpr LoadStoreAcquireReleaseOp CASLH = CASHFixed | LSEBit_o0;
constexpr LoadStoreAcquireReleaseOp CASALH = CASHFixed | LSEBit_l | LSEBit_o0;
constexpr LoadStoreAcquireReleaseOp CASP_w = CASPFixed;
constexpr LoadStoreAcquireReleaseOp CASP_x = CASPFixed | LSEBit_sz;
constexpr LoadStoreAcquireReleaseOp CASPA_w = CASPFixed | LSEBit_l;
constexpr LoadStoreAcquireReleaseOp CASPA_x = CASPFixed | LSEBit_l | LSEBit_sz;
constexpr LoadStoreAcquireReleaseOp CASPL_w = CASPFixed | LSEBit_o0;
constexpr LoadStoreAcquireReleaseOp CASPL_x = CASPFixed | LSEBit_o0 | LSEBit_sz;
constexpr LoadStoreAcquireReleaseOp CASPAL_w = CASPFixed | LSEBit_l | LSEBit_o0;
constexpr LoadStoreAcquireReleaseOp CASPAL_x =
    CASPFixed | LSEBit_l | LSEBit_o0 | LSEBit_sz;

#define ATOMIC_MEMORY_SIMPLE_OPC_LIST(V) \
  V(LDADD, 0x00000000);                  \
  V(LDCLR, 0x00001000);                  \
  V(LDEOR, 0x00002000);                  \
  V(LDSET, 0x00003000);                  \
  V(LDSMAX, 0x00004000);                 \
  V(LDSMIN, 0x00005000);                 \
  V(LDUMAX, 0x00006000);                 \
  V(LDUMIN, 0x00007000)

// Atomic memory operations [Armv8.1].
using AtomicMemoryOp = uint32_t;
constexpr AtomicMemoryOp AtomicMemoryFixed = 0x38200000;
constexpr AtomicMemoryOp AtomicMemoryFMask = 0x3B200C00;
constexpr AtomicMemoryOp AtomicMemoryMask = 0xFFE0FC00;
constexpr AtomicMemoryOp SWPB = AtomicMemoryFixed | 0x00008000;
constexpr AtomicMemoryOp SWPAB = AtomicMemoryFixed | 0x00808000;
constexpr AtomicMemoryOp SWPLB = AtomicMemoryFixed | 0x00408000;
constexpr AtomicMemoryOp SWPALB = AtomicMemoryFixed | 0x00C08000;
constexpr AtomicMemoryOp SWPH = AtomicMemoryFixed | 0x40008000;
constexpr AtomicMemoryOp SWPAH = AtomicMemoryFixed | 0x40808000;
constexpr AtomicMemoryOp SWPLH = AtomicMemoryFixed | 0x40408000;
constexpr AtomicMemoryOp SWPALH = AtomicMemoryFixed | 0x40C08000;
constexpr AtomicMemoryOp SWP_w = AtomicMemoryFixed | 0x80008000;
constexpr AtomicMemoryOp SWPA_w = AtomicMemoryFixed | 0x80808000;
constexpr AtomicMemoryOp SWPL_w = AtomicMemoryFixed | 0x80408000;
constexpr AtomicMemoryOp SWPAL_w = AtomicMemoryFixed | 0x80C08000;
constexpr AtomicMemoryOp SWP_x = AtomicMemoryFixed | 0xC0008000;
constexpr AtomicMemoryOp SWPA_x = AtomicMemoryFixed | 0xC0808000;
constexpr AtomicMemoryOp SWPL_x = AtomicMemoryFixed | 0xC0408000;
constexpr AtomicMemoryOp SWPAL_x = AtomicMemoryFixed | 0xC0C08000;

constexpr AtomicMemoryOp AtomicMemorySimpleFMask = 0x3B208C00;
constexpr AtomicMemoryOp AtomicMemorySimpleOpMask = 0x00007000;
#define ATOMIC_MEMORY_SIMPLE(N, OP)                                       \
  constexpr AtomicMemoryOp N##Op = OP;                                    \
  constexpr AtomicMemoryOp N##B = AtomicMemoryFixed | OP;                 \
  constexpr AtomicMemoryOp N##AB = AtomicMemoryFixed | OP | 0x00800000;   \
  constexpr AtomicMemoryOp N##LB = AtomicMemoryFixed | OP | 0x00400000;   \
  constexpr AtomicMemoryOp N##ALB = AtomicMemoryFixed | OP | 0x00C00000;  \
  constexpr AtomicMemoryOp N##H = AtomicMemoryFixed | OP | 0x40000000;    \
  constexpr AtomicMemoryOp N##AH = AtomicMemoryFixed | OP | 0x40800000;   \
  constexpr AtomicMemoryOp N##LH = AtomicMemoryFixed | OP | 0x40400000;   \
  constexpr AtomicMemoryOp N##ALH = AtomicMemoryFixed | OP | 0x40C00000;  \
  constexpr AtomicMemoryOp N##_w = AtomicMemoryFixed | OP | 0x80000000;   \
  constexpr AtomicMemoryOp N##A_w = AtomicMemoryFixed | OP | 0x80800000;  \
  constexpr AtomicMemoryOp N##L_w = AtomicMemoryFixed | OP | 0x80400000;  \
  constexpr AtomicMemoryOp N##AL_w = AtomicMemoryFixed | OP | 0x80C00000; \
  constexpr AtomicMemoryOp N##_x = AtomicMemoryFixed | OP | 0xC0000000;   \
  constexpr AtomicMemoryOp N##A_x = AtomicMemoryFixed | OP | 0xC0800000;  \
  constexpr AtomicMemoryOp N##L_x = AtomicMemoryFixed | OP | 0xC0400000;  \
  constexpr AtomicMemoryOp N##AL_x = AtomicMemoryFixed | OP | 0xC0C00000

ATOMIC_MEMORY_SIMPLE_OPC_LIST(ATOMIC_MEMORY_SIMPLE);
#undef ATOMIC_MEMORY_SIMPLE

// Conditional compare.
using ConditionalCompareOp = uint32_t;
constexpr ConditionalCompareOp ConditionalCompareMask = 0x60000000;
constexpr ConditionalCompareOp CCMN = 0x20000000;
constexpr ConditionalCompareOp CCMP = 0x60000000;

// Conditional compare register.
using ConditionalCompareRegisterOp = uint32_t;
constexpr ConditionalCompareRegisterOp ConditionalCompareRegisterFixed =
    0x1A400000;
constexpr ConditionalCompareRegisterOp ConditionalCompareRegisterFMask =
    0x1FE00800;
constexpr ConditionalCompareRegisterOp ConditionalCompareRegisterMask =
    0xFFE00C10;
constexpr ConditionalCompareRegisterOp CCMN_w =
    ConditionalCompareRegisterFixed | CCMN;
constexpr ConditionalCompareRegisterOp CCMN_x =
    ConditionalCompareRegisterFixed | SixtyFourBits | CCMN;
constexpr ConditionalCompareRegisterOp CCMP_w =
    ConditionalCompareRegisterFixed | CCMP;
constexpr ConditionalCompareRegisterOp CCMP_x =
    ConditionalCompareRegisterFixed | SixtyFourBits | CCMP;

// Conditional compare immediate.
using ConditionalCompareImmediateOp = uint32_t;
constexpr ConditionalCompareImmediateOp ConditionalCompareImmediateFixed =
    0x1A400800;
constexpr ConditionalCompareImmediateOp ConditionalCompareImmediateFMask =
    0x1FE00800;
constexpr ConditionalCompareImmediateOp ConditionalCompareImmediateMask =
    0xFFE00C10;
constexpr ConditionalCompareImmediateOp CCMN_w_imm =
    ConditionalCompareImmediateFixed | CCMN;
constexpr ConditionalCompareImmediateOp CCMN_x_imm =
    ConditionalCompareImmediateFixed | SixtyFourBits | CCMN;
constexpr ConditionalCompareImmediateOp CCMP_w_imm =
    ConditionalCompareImmediateFixed | CCMP;
constexpr ConditionalCompareImmediateOp CCMP_x_imm =
    ConditionalCompareImmediateFixed | SixtyFourBits | CCMP;

// Conditional select.
using ConditionalSelectOp = uint32_t;
constexpr ConditionalSelectOp ConditionalSelectFixed = 0x1A800000;
constexpr ConditionalSelectOp ConditionalSelectFMask = 0x1FE00000;
constexpr ConditionalSelectOp ConditionalSelectMask = 0xFFE00C00;
constexpr ConditionalSelectOp CSEL_w = ConditionalSelectFixed | 0x00000000;
constexpr ConditionalSelectOp CSEL_x = ConditionalSelectFixed | 0x80000000;
constexpr ConditionalSelectOp CSEL = CSEL_w;
constexpr ConditionalSelectOp CSINC_w = ConditionalSelectFixed | 0x00000400;
constexpr ConditionalSelectOp CSINC_x = ConditionalSelectFixed | 0x80000400;
constexpr ConditionalSelectOp CSINC = CSINC_w;
constexpr ConditionalSelectOp CSINV_w = ConditionalSelectFixed | 0x40000000;
constexpr ConditionalSelectOp CSINV_x = ConditionalSelectFixed | 0xC0000000;
constexpr ConditionalSelectOp CSINV = CSINV_w;
constexpr ConditionalSelectOp CSNEG_w = ConditionalSelectFixed | 0x40000400;
constexpr ConditionalSelectOp CSNEG_x = ConditionalSelectFixed | 0xC0000400;
constexpr ConditionalSelectOp CSNEG = CSNEG_w;

// Data processing 1 source.
using DataProcessing1SourceOp = uint32_t;
constexpr DataProcessing1SourceOp DataProcessing1SourceFixed = 0x5AC00000;
constexpr DataProcessing1SourceOp DataProcessing1SourceFMask = 0x5FE00000;
constexpr DataProcessing1SourceOp DataProcessing1SourceMask = 0xFFFFFC00;
constexpr DataProcessing1SourceOp RBIT =
    DataProcessing1SourceFixed | 0x00000000;
constexpr DataProcessing1SourceOp RBIT_w = RBIT;
constexpr DataProcessing1SourceOp RBIT_x = RBIT | SixtyFourBits;
constexpr DataProcessing1SourceOp REV16 =
    DataProcessing1SourceFixed | 0x00000400;
constexpr DataProcessing1SourceOp REV16_w = REV16;
constexpr DataProcessing1SourceOp REV16_x = REV16 | SixtyFourBits;
constexpr DataProcessing1SourceOp REV = DataProcessing1SourceFixed | 0x00000800;
constexpr DataProcessing1SourceOp REV_w = REV;
constexpr DataProcessing1SourceOp REV32_x = REV | SixtyFourBits;
constexpr DataProcessing1SourceOp REV_x =
    DataProcessing1SourceFixed | SixtyFourBits | 0x00000C00;
constexpr DataProcessing1SourceOp CLZ = DataProcessing1SourceFixed | 0x00001000;
constexpr DataProcessing1SourceOp CLZ_w = CLZ;
constexpr DataProcessing1SourceOp CLZ_x = CLZ | SixtyFourBits;
constexpr DataProcessing1SourceOp CLS = DataProcessing1SourceFixed | 0x00001400;
constexpr DataProcessing1SourceOp CLS_w = CLS;
constexpr DataProcessing1SourceOp CLS_x = CLS | SixtyFourBits;

// Data processing 2 source.
using DataProcessing2SourceOp = uint32_t;
constexpr DataProcessing2SourceOp DataProcessing2SourceFixed = 0x1AC00000;
constexpr DataProcessing2SourceOp DataProcessing2SourceFMask = 0x5FE00000;
constexpr DataProcessing2SourceOp DataProcessing2SourceMask = 0xFFE0FC00;
constexpr DataProcessing2SourceOp UDIV_w =
    DataProcessing2SourceFixed | 0x00000800;
constexpr DataProcessing2SourceOp UDIV_x =
    DataProcessing2SourceFixed | 0x80000800;
constexpr DataProcessing2SourceOp UDIV = UDIV_w;
constexpr DataProcessing2SourceOp SDIV_w =
    DataProcessing2SourceFixed | 0x00000C00;
constexpr DataProcessing2SourceOp SDIV_x =
    DataProcessing2SourceFixed | 0x80000C00;
constexpr DataProcessing2SourceOp SDIV = SDIV_w;
constexpr DataProcessing2SourceOp LSLV_w =
    DataProcessing2SourceFixed | 0x00002000;
constexpr DataProcessing2SourceOp LSLV_x =
    DataProcessing2SourceFixed | 0x80002000;
constexpr DataProcessing2SourceOp LSLV = LSLV_w;
constexpr DataProcessing2SourceOp LSRV_w =
    DataProcessing2SourceFixed | 0x00002400;
constexpr DataProcessing2SourceOp LSRV_x =
    DataProcessing2SourceFixed | 0x80002400;
constexpr DataProcessing2SourceOp LSRV = LSRV_w;
constexpr DataProcessing2SourceOp ASRV_w =
    DataProcessing2SourceFixed | 0x00002800;
constexpr DataProcessing2SourceOp ASRV_x =
    DataProcessing2SourceFixed | 0x80002800;
constexpr DataProcessing2SourceOp ASRV = ASRV_w;
constexpr DataProcessing2SourceOp RORV_w =
    DataProcessing2SourceFixed | 0x00002C00;
constexpr DataProcessing2SourceOp RORV_x =
    DataProcessing2SourceFixed | 0x80002C00;
constexpr DataProcessing2SourceOp RORV = RORV_w;
constexpr DataProcessing2SourceOp CRC32B =
    DataProcessing2SourceFixed | 0x00004000;
constexpr DataProcessing2SourceOp CRC32H =
    DataProcessing2SourceFixed | 0x00004400;
constexpr DataProcessing2SourceOp CRC32W =
    DataProcessing2SourceFixed | 0x00004800;
constexpr DataProcessing2SourceOp CRC32X =
    DataProcessing2SourceFixed | SixtyFourBits | 0x00004C00;
constexpr DataProcessing2SourceOp CRC32CB =
    DataProcessing2SourceFixed | 0x00005000;
constexpr DataProcessing2SourceOp CRC32CH =
    DataProcessing2SourceFixed | 0x00005400;
constexpr DataProcessing2SourceOp CRC32CW =
    DataProcessing2SourceFixed | 0x00005800;
constexpr DataProcessing2SourceOp CRC32CX =
    DataProcessing2SourceFixed | SixtyFourBits | 0x00005C00;

// Data processing 3 source.
using DataProcessing3SourceOp = uint32_t;
constexpr DataProcessing3SourceOp DataProcessing3SourceFixed = 0x1B000000;
constexpr DataProcessing3SourceOp DataProcessing3SourceFMask = 0x1F000000;
constexpr DataProcessing3SourceOp DataProcessing3SourceMask = 0xFFE08000;
constexpr DataProcessing3SourceOp MADD_w =
    DataProcessing3SourceFixed | 0x00000000;
constexpr DataProcessing3SourceOp MADD_x =
    DataProcessing3SourceFixed | 0x80000000;
constexpr DataProcessing3SourceOp MADD = MADD_w;
constexpr DataProcessing3SourceOp MSUB_w =
    DataProcessing3SourceFixed | 0x00008000;
constexpr DataProcessing3SourceOp MSUB_x =
    DataProcessing3SourceFixed | 0x80008000;
constexpr DataProcessing3SourceOp MSUB = MSUB_w;
constexpr DataProcessing3SourceOp SMADDL_x =
    DataProcessing3SourceFixed | 0x80200000;
constexpr DataProcessing3SourceOp SMSUBL_x =
    DataProcessing3SourceFixed | 0x80208000;
constexpr DataProcessing3SourceOp SMULH_x =
    DataProcessing3SourceFixed | 0x80400000;
constexpr DataProcessing3SourceOp UMADDL_x =
    DataProcessing3SourceFixed | 0x80A00000;
constexpr DataProcessing3SourceOp UMSUBL_x =
    DataProcessing3SourceFixed | 0x80A08000;
constexpr DataProcessing3SourceOp UMULH_x =
    DataProcessing3SourceFixed | 0x80C00000;

// Floating point compare.
using FPCompareOp = uint32_t;
constexpr FPCompareOp FPCompareFixed = 0x1E202000;
constexpr FPCompareOp FPCompareFMask = 0x5F203C00;
constexpr FPCompareOp FPCompareMask = 0xFFE0FC1F;
constexpr FPCompareOp FCMP_s = FPCompareFixed | 0x00000000;
constexpr FPCompareOp FCMP_d = FPCompareFixed | FP64 | 0x00000000;
constexpr FPCompareOp FCMP = FCMP_s;
constexpr FPCompareOp FCMP_s_zero = FPCompareFixed | 0x00000008;
constexpr FPCompareOp FCMP_d_zero = FPCompareFixed | FP64 | 0x00000008;
constexpr FPCompareOp FCMP_zero = FCMP_s_zero;
constexpr FPCompareOp FCMPE_s = FPCompareFixed | 0x00000010;
constexpr FPCompareOp FCMPE_d = FPCompareFixed | FP64 | 0x00000010;
constexpr FPCompareOp FCMPE_s_zero = FPCompareFixed | 0x00000018;
constexpr FPCompareOp FCMPE_d_zero = FPCompareFixed | FP64 | 0x00000018;

// Floating point conditional compare.
using FPConditionalCompareOp = uint32_t;
constexpr FPConditionalCompareOp FPConditionalCompareFixed = 0x1E200400;
constexpr FPConditionalCompareOp FPConditionalCompareFMask = 0x5F200C00;
constexpr FPConditionalCompareOp FPConditionalCompareMask = 0xFFE00C10;
constexpr FPConditionalCompareOp FCCMP_s =
    FPConditionalCompareFixed | 0x00000000;
constexpr FPConditionalCompareOp FCCMP_d =
    FPConditionalCompareFixed | FP64 | 0x00000000;
constexpr FPConditionalCompareOp FCCMP = FCCMP_s;
constexpr FPConditionalCompareOp FCCMPE_s =
    FPConditionalCompareFixed | 0x00000010;
constexpr FPConditionalCompareOp FCCMPE_d =
    FPConditionalCompareFixed | FP64 | 0x00000010;
constexpr FPConditionalCompareOp FCCMPE = FCCMPE_s;

// Floating point conditional select.
using FPConditionalSelectOp = uint32_t;
constexpr FPConditionalSelectOp FPConditionalSelectFixed = 0x1E200C00;
constexpr FPConditionalSelectOp FPConditionalSelectFMask = 0x5F200C00;
constexpr FPConditionalSelectOp FPConditionalSelectMask = 0xFFE00C00;
constexpr FPConditionalSelectOp FCSEL_s = FPConditionalSelectFixed | 0x00000000;
constexpr FPConditionalSelectOp FCSEL_d =
    FPConditionalSelectFixed | FP64 | 0x00000000;
constexpr FPConditionalSelectOp FCSEL = FCSEL_s;

// Floating point immediate.
using FPImmediateOp = uint32_t;
constexpr FPImmediateOp FPImmediateFixed = 0x1E201000;
constexpr FPImmediateOp FPImmediateFMask = 0x5F201C00;
constexpr FPImmediateOp FPImmediateMask = 0xFFE01C00;
constexpr FPImmediateOp FMOV_s_imm = FPImmediateFixed | 0x00000000;
constexpr FPImmediateOp FMOV_d_imm = FPImmediateFixed | FP64 | 0x00000000;

// Floating point data processing 1 source.
using FPDataProcessing1SourceOp = uint32_t;
constexpr FPDataProcessing1SourceOp FPDataProcessing1SourceFixed = 0x1E204000;
constexpr FPDataProcessing1SourceOp FPDataProcessing1SourceFMask = 0x5F207C00;
constexpr FPDataProcessing1SourceOp FPDataProcessing1SourceMask = 0xFFFFFC00;
constexpr FPDataProcessing1SourceOp FMOV_s =
    FPDataProcessing1SourceFixed | 0x00000000;
constexpr FPDataProcessing1SourceOp FMOV_d =
    FPDataProcessing1SourceFixed | FP64 | 0x00000000;
constexpr FPDataProcessing1SourceOp FMOV = FMOV_s;
constexpr FPDataProcessing1SourceOp FABS_s =
    FPDataProcessing1SourceFixed | 0x00008000;
constexpr FPDataProcessing1SourceOp FABS_d =
    FPDataProcessing1SourceFixed | FP64 | 0x00008000;
constexpr FPDataProcessing1SourceOp FABS = FABS_s;
constexpr FPDataProcessing1SourceOp FNEG_s =
    FPDataProcessing1SourceFixed | 0x00010000;
constexpr FPDataProcessing1SourceOp FNEG_d =
    FPDataProcessing1SourceFixed | FP64 | 0x00010000;
constexpr FPDataProcessing1SourceOp FNEG = FNEG_s;
constexpr FPDataProcessing1SourceOp FSQRT_s =
    FPDataProcessing1SourceFixed | 0x00018000;
constexpr FPDataProcessing1SourceOp FSQRT_d =
    FPDataProcessing1SourceFixed | FP64 | 0x00018000;
constexpr FPDataProcessing1SourceOp FSQRT = FSQRT_s;
constexpr FPDataProcessing1SourceOp FCVT_ds =
    FPDataProcessing1SourceFixed | 0x00028000;
constexpr FPDataProcessing1SourceOp FCVT_sd =
    FPDataProcessing1SourceFixed | FP64 | 0x00020000;
constexpr FPDataProcessing1SourceOp FCVT_hs =
    FPDataProcessing1SourceFixed | 0x00038000;
constexpr FPDataProcessing1SourceOp FCVT_hd =
    FPDataProcessing1SourceFixed | FP64 | 0x00038000;
constexpr FPDataProcessing1SourceOp FCVT_sh =
    FPDataProcessing1SourceFixed | 0x00C20000;
constexpr FPDataProcessing1SourceOp FCVT_dh =
    FPDataProcessing1SourceFixed | 0x00C28000;
constexpr FPDataProcessing1SourceOp FRINTN_s =
    FPDataProcessing1SourceFixed | 0x00040000;
constexpr FPDataProcessing1SourceOp FRINTN_d =
    FPDataProcessing1SourceFixed | FP64 | 0x00040000;
constexpr FPDataProcessing1SourceOp FRINTN = FRINTN_s;
constexpr FPDataProcessing1SourceOp FRINTP_s =
    FPDataProcessing1SourceFixed | 0x00048000;
constexpr FPDataProcessing1SourceOp FRINTP_d =
    FPDataProcessing1SourceFixed | FP64 | 0x00048000;
constexpr FPDataProcessing1SourceOp FRINTP = FRINTP_s;
constexpr FPDataProcessing1SourceOp FRINTM_s =
    FPDataProcessing1SourceFixed | 0x00050000;
constexpr FPDataProcessing1SourceOp FRINTM_d =
    FPDataProcessing1SourceFixed | FP64 | 0x00050000;
constexpr FPDataProcessing1SourceOp FRINTM = FRINTM_s;
constexpr FPDataProcessing1SourceOp FRINTZ_s =
    FPDataProcessing1SourceFixed | 0x00058000;
constexpr FPDataProcessing1SourceOp FRINTZ_d =
    FPDataProcessing1SourceFixed | FP64 | 0x00058000;
constexpr FPDataProcessing1SourceOp FRINTZ = FRINTZ_s;
constexpr FPDataProcessing1SourceOp FRINTA_s =
    FPDataProcessing1SourceFixed | 0x00060000;
constexpr FPDataProcessing1SourceOp FRINTA_d =
    FPDataProcessing1SourceFixed | FP64 | 0x00060000;
constexpr FPDataProcessing1SourceOp FRINTA = FRINTA_s;
constexpr FPDataProcessing1SourceOp FRINTX_s =
    FPDataProcessing1SourceFixed | 0x00070000;
constexpr FPDataProcessing1SourceOp FRINTX_d =
    FPDataProcessing1SourceFixed | FP64 | 0x00070000;
constexpr FPDataProcessing1SourceOp FRINTX = FRINTX_s;
constexpr FPDataProcessing1SourceOp FRINTI_s =
    FPDataProcessing1SourceFixed | 0x00078000;
constexpr FPDataProcessing1SourceOp FRINTI_d =
    FPDataProcessing1SourceFixed | FP64 | 0x00078000;
constexpr FPDataProcessing1SourceOp FRINTI = FRINTI_s;

// Floating point data processing 2 source.
using FPDataProcessing2SourceOp = uint32_t;
constexpr FPDataProcessing2SourceOp FPDataProcessing2SourceFixed = 0x1E200800;
constexpr FPDataProcessing2SourceOp FPDataProcessing2SourceFMask = 0x5F200C00;
constexpr FPDataProcessing2SourceOp FPDataProcessing2SourceMask = 0xFFE0FC00;
constexpr FPDataProcessing2SourceOp FMUL =
    FPDataProcessing2SourceFixed | 0x00000000;
constexpr FPDataProcessing2SourceOp FMUL_s = FMUL;
constexpr FPDataProcessing2SourceOp FMUL_d = FMUL | FP64;
constexpr FPDataProcessing2SourceOp FDIV =
    FPDataProcessing2SourceFixed | 0x00001000;
constexpr FPDataProcessing2SourceOp FDIV_s = FDIV;
constexpr FPDataProcessing2SourceOp FDIV_d = FDIV | FP64;
constexpr FPDataProcessing2SourceOp FADD =
    FPDataProcessing2SourceFixed | 0x00002000;
constexpr FPDataProcessing2SourceOp FADD_s = FADD;
constexpr FPDataProcessing2SourceOp FADD_d = FADD | FP64;
constexpr FPDataProcessing2SourceOp FSUB =
    FPDataProcessing2SourceFixed | 0x00003000;
constexpr FPDataProcessing2SourceOp FSUB_s = FSUB;
constexpr FPDataProcessing2SourceOp FSUB_d = FSUB | FP64;
constexpr FPDataProcessing2SourceOp FMAX =
    FPDataProcessing2SourceFixed | 0x00004000;
constexpr FPDataProcessing2SourceOp FMAX_s = FMAX;
constexpr FPDataProcessing2SourceOp FMAX_d = FMAX | FP64;
constexpr FPDataProcessing2SourceOp FMIN =
    FPDataProcessing2SourceFixed | 0x00005000;
constexpr FPDataProcessing2SourceOp FMIN_s = FMIN;
constexpr FPDataProcessing2SourceOp FMIN_d = FMIN | FP64;
constexpr FPDataProcessing2SourceOp FMAXNM =
    FPDataProcessing2SourceFixed | 0x00006000;
constexpr FPDataProcessing2SourceOp FMAXNM_s = FMAXNM;
constexpr FPDataProcessing2SourceOp FMAXNM_d = FMAXNM | FP64;
constexpr FPDataProcessing2SourceOp FMINNM =
    FPDataProcessing2SourceFixed | 0x00007000;
constexpr FPDataProcessing2SourceOp FMINNM_s = FMINNM;
constexpr FPDataProcessing2SourceOp FMINNM_d = FMINNM | FP64;
constexpr FPDataProcessing2SourceOp FNMUL =
    FPDataProcessing2SourceFixed | 0x00008000;
constexpr FPDataProcessing2SourceOp FNMUL_s = FNMUL;
constexpr FPDataProcessing2SourceOp FNMUL_d = FNMUL | FP64;

// Floating point data processing 3 source.
using FPDataProcessing3SourceOp = uint32_t;
constexpr FPDataProcessing3SourceOp FPDataProcessing3SourceFixed = 0x1F000000;
constexpr FPDataProcessing3SourceOp FPDataProcessing3SourceFMask = 0x5F000000;
constexpr FPDataProcessing3SourceOp FPDataProcessing3SourceMask = 0xFFE08000;
constexpr FPDataProcessing3SourceOp FMADD_s =
    FPDataProcessing3SourceFixed | 0x00000000;
constexpr FPDataProcessing3SourceOp FMSUB_s =
    FPDataProcessing3SourceFixed | 0x00008000;
constexpr FPDataProcessing3SourceOp FNMADD_s =
    FPDataProcessing3SourceFixed | 0x00200000;
constexpr FPDataProcessing3SourceOp FNMSUB_s =
    FPDataProcessing3SourceFixed | 0x00208000;
constexpr FPDataProcessing3SourceOp FMADD_d =
    FPDataProcessing3SourceFixed | 0x00400000;
constexpr FPDataProcessing3SourceOp FMSUB_d =
    FPDataProcessing3SourceFixed | 0x00408000;
constexpr FPDataProcessing3SourceOp FNMADD_d =
    FPDataProcessing3SourceFixed | 0x00600000;
constexpr FPDataProcessing3SourceOp FNMSUB_d =
    FPDataProcessing3SourceFixed | 0x00608000;

// Conversion between floating point and integer.
using FPIntegerConvertOp = uint32_t;
constexpr FPIntegerConvertOp FPIntegerConvertFixed = 0x1E200000;
constexpr FPIntegerConvertOp FPIntegerConvertFMask = 0x5F20FC00;
constexpr FPIntegerConvertOp FPIntegerConvertMask = 0xFFFFFC00;
constexpr FPIntegerConvertOp FCVTNS = FPIntegerConvertFixed | 0x00000000;
constexpr FPIntegerConvertOp FCVTNS_ws = FCVTNS;
constexpr FPIntegerConvertOp FCVTNS_xs = FCVTNS | SixtyFourBits;
constexpr FPIntegerConvertOp FCVTNS_wd = FCVTNS | FP64;
constexpr FPIntegerConvertOp FCVTNS_xd = FCVTNS | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FCVTNU = FPIntegerConvertFixed | 0x00010000;
constexpr FPIntegerConvertOp FCVTNU_ws = FCVTNU;
constexpr FPIntegerConvertOp FCVTNU_xs = FCVTNU | SixtyFourBits;
constexpr FPIntegerConvertOp FCVTNU_wd = FCVTNU | FP64;
constexpr FPIntegerConvertOp FCVTNU_xd = FCVTNU | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FCVTPS = FPIntegerConvertFixed | 0x00080000;
constexpr FPIntegerConvertOp FCVTPS_ws = FCVTPS;
constexpr FPIntegerConvertOp FCVTPS_xs = FCVTPS | SixtyFourBits;
constexpr FPIntegerConvertOp FCVTPS_wd = FCVTPS | FP64;
constexpr FPIntegerConvertOp FCVTPS_xd = FCVTPS | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FCVTPU = FPIntegerConvertFixed | 0x00090000;
constexpr FPIntegerConvertOp FCVTPU_ws = FCVTPU;
constexpr FPIntegerConvertOp FCVTPU_xs = FCVTPU | SixtyFourBits;
constexpr FPIntegerConvertOp FCVTPU_wd = FCVTPU | FP64;
constexpr FPIntegerConvertOp FCVTPU_xd = FCVTPU | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FCVTMS = FPIntegerConvertFixed | 0x00100000;
constexpr FPIntegerConvertOp FCVTMS_ws = FCVTMS;
constexpr FPIntegerConvertOp FCVTMS_xs = FCVTMS | SixtyFourBits;
constexpr FPIntegerConvertOp FCVTMS_wd = FCVTMS | FP64;
constexpr FPIntegerConvertOp FCVTMS_xd = FCVTMS | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FCVTMU = FPIntegerConvertFixed | 0x00110000;
constexpr FPIntegerConvertOp FCVTMU_ws = FCVTMU;
constexpr FPIntegerConvertOp FCVTMU_xs = FCVTMU | SixtyFourBits;
constexpr FPIntegerConvertOp FCVTMU_wd = FCVTMU | FP64;
constexpr FPIntegerConvertOp FCVTMU_xd = FCVTMU | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FCVTZS = FPIntegerConvertFixed | 0x00180000;
constexpr FPIntegerConvertOp FCVTZS_ws = FCVTZS;
constexpr FPIntegerConvertOp FCVTZS_xs = FCVTZS | SixtyFourBits;
constexpr FPIntegerConvertOp FCVTZS_wd = FCVTZS | FP64;
constexpr FPIntegerConvertOp FCVTZS_xd = FCVTZS | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FCVTZU = FPIntegerConvertFixed | 0x00190000;
constexpr FPIntegerConvertOp FCVTZU_ws = FCVTZU;
constexpr FPIntegerConvertOp FCVTZU_xs = FCVTZU | SixtyFourBits;
constexpr FPIntegerConvertOp FCVTZU_wd = FCVTZU | FP64;
constexpr FPIntegerConvertOp FCVTZU_xd = FCVTZU | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp SCVTF = FPIntegerConvertFixed | 0x00020000;
constexpr FPIntegerConvertOp SCVTF_sw = SCVTF;
constexpr FPIntegerConvertOp SCVTF_sx = SCVTF | SixtyFourBits;
constexpr FPIntegerConvertOp SCVTF_dw = SCVTF | FP64;
constexpr FPIntegerConvertOp SCVTF_dx = SCVTF | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp UCVTF = FPIntegerConvertFixed | 0x00030000;
constexpr FPIntegerConvertOp UCVTF_sw = UCVTF;
constexpr FPIntegerConvertOp UCVTF_sx = UCVTF | SixtyFourBits;
constexpr FPIntegerConvertOp UCVTF_dw = UCVTF | FP64;
constexpr FPIntegerConvertOp UCVTF_dx = UCVTF | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FCVTAS = FPIntegerConvertFixed | 0x00040000;
constexpr FPIntegerConvertOp FCVTAS_ws = FCVTAS;
constexpr FPIntegerConvertOp FCVTAS_xs = FCVTAS | SixtyFourBits;
constexpr FPIntegerConvertOp FCVTAS_wd = FCVTAS | FP64;
constexpr FPIntegerConvertOp FCVTAS_xd = FCVTAS | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FCVTAU = FPIntegerConvertFixed | 0x00050000;
constexpr FPIntegerConvertOp FCVTAU_ws = FCVTAU;
constexpr FPIntegerConvertOp FCVTAU_xs = FCVTAU | SixtyFourBits;
constexpr FPIntegerConvertOp FCVTAU_wd = FCVTAU | FP64;
constexpr FPIntegerConvertOp FCVTAU_xd = FCVTAU | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FMOV_ws = FPIntegerConvertFixed | 0x00060000;
constexpr FPIntegerConvertOp FMOV_sw = FPIntegerConvertFixed | 0x00070000;
constexpr FPIntegerConvertOp FMOV_xd = FMOV_ws | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FMOV_dx = FMOV_sw | SixtyFourBits | FP64;
constexpr FPIntegerConvertOp FMOV_d1_x =
    FPIntegerConvertFixed | SixtyFourBits | 0x008F0000;
constexpr FPIntegerConvertOp FMOV_x_d1 =
    FPIntegerConvertFixed | SixtyFourBits | 0x008E0000;
constexpr FPIntegerConvertOp FJCVTZS =
    FPIntegerConvertFixed | FP64 | 0x001E0000;

// Conversion between fixed point and floating point.
using FPFixedPointConvertOp = uint32_t;
constexpr FPFixedPointConvertOp FPFixedPointConvertFixed = 0x1E000000;
constexpr FPFixedPointConvertOp FPFixedPointConvertFMask = 0x5F200000;
constexpr FPFixedPointConvertOp FPFixedPointConvertMask = 0xFFFF0000;
constexpr FPFixedPointConvertOp FCVTZS_fixed =
    FPFixedPointConvertFixed | 0x00180000;
constexpr FPFixedPointConvertOp FCVTZS_ws_fixed = FCVTZS_fixed;
constexpr FPFixedPointConvertOp FCVTZS_xs_fixed = FCVTZS_fixed | SixtyFourBits;
constexpr FPFixedPointConvertOp FCVTZS_wd_fixed = FCVTZS_fixed | FP64;
constexpr FPFixedPointConvertOp FCVTZS_xd_fixed =
    FCVTZS_fixed | SixtyFourBits | FP64;
constexpr FPFixedPointConvertOp FCVTZU_fixed =
    FPFixedPointConvertFixed | 0x00190000;
constexpr FPFixedPointConvertOp FCVTZU_ws_fixed = FCVTZU_fixed;
constexpr FPFixedPointConvertOp FCVTZU_xs_fixed = FCVTZU_fixed | SixtyFourBits;
constexpr FPFixedPointConvertOp FCVTZU_wd_fixed = FCVTZU_fixed | FP64;
constexpr FPFixedPointConvertOp FCVTZU_xd_fixed =
    FCVTZU_fixed | SixtyFourBits | FP64;
constexpr FPFixedPointConvertOp SCVTF_fixed =
    FPFixedPointConvertFixed | 0x00020000;
constexpr FPFixedPointConvertOp SCVTF_sw_fixed = SCVTF_fixed;
constexpr FPFixedPointConvertOp SCVTF_sx_fixed = SCVTF_fixed | SixtyFourBits;
constexpr FPFixedPointConvertOp SCVTF_dw_fixed = SCVTF_fixed | FP64;
constexpr FPFixedPointConvertOp SCVTF_dx_fixed =
    SCVTF_fixed | SixtyFourBits | FP64;
constexpr FPFixedPointConvertOp UCVTF_fixed =
    FPFixedPointConvertFixed | 0x00030000;
constexpr FPFixedPointConvertOp UCVTF_sw_fixed = UCVTF_fixed;
constexpr FPFixedPointConvertOp UCVTF_sx_fixed = UCVTF_fixed | SixtyFourBits;
constexpr FPFixedPointConvertOp UCVTF_dw_fixed = UCVTF_fixed | FP64;
constexpr FPFixedPointConvertOp UCVTF_dx_fixed =
    UCVTF_fixed | SixtyFourBits | FP64;

// NEON instructions with two register operands.
using NEON2RegMiscOp = uint32_t;
constexpr NEON2RegMiscOp NEON2RegMiscFixed = 0x0E200800;
constexpr NEON2RegMiscOp NEON2RegMiscFMask = 0x9F260C00;
constexpr NEON2RegMiscOp NEON2RegMiscHPFixed = 0x00180000;
constexpr NEON2RegMiscOp NEON2RegMiscMask = 0xBF3FFC00;
constexpr NEON2RegMiscOp NEON2RegMiscUBit = 0x20000000;
constexpr NEON2RegMiscOp NEON_REV64 = NEON2RegMiscFixed | 0x00000000;
constexpr NEON2RegMiscOp NEON_REV32 = NEON2RegMiscFixed | 0x20000000;
constexpr NEON2RegMiscOp NEON_REV16 = NEON2RegMiscFixed | 0x00001000;
constexpr NEON2RegMiscOp NEON_SADDLP = NEON2RegMiscFixed | 0x00002000;
constexpr NEON2RegMiscOp NEON_UADDLP = NEON_SADDLP | NEON2RegMiscUBit;
constexpr NEON2RegMiscOp NEON_SUQADD = NEON2RegMiscFixed | 0x00003000;
constexpr NEON2RegMiscOp NEON_USQADD = NEON_SUQADD | NEON2RegMiscUBit;
constexpr NEON2RegMiscOp NEON_CLS = NEON2RegMiscFixed | 0x00004000;
constexpr NEON2RegMiscOp NEON_CLZ = NEON2RegMiscFixed | 0x20004000;
constexpr NEON2RegMiscOp NEON_CNT = NEON2RegMiscFixed | 0x00005000;
constexpr NEON2RegMiscOp NEON_RBIT_NOT = NEON2RegMiscFixed | 0x20005000;
constexpr NEON2RegMiscOp NEON_SADALP = NEON2RegMiscFixed | 0x00006000;
constexpr NEON2RegMiscOp NEON_UADALP = NEON_SADALP | NEON2RegMiscUBit;
constexpr NEON2RegMiscOp NEON_SQABS = NEON2RegMiscFixed | 0x00007000;
constexpr NEON2RegMiscOp NEON_SQNEG = NEON2RegMiscFixed | 0x20007000;
constexpr NEON2RegMiscOp NEON_CMGT_zero = NEON2RegMiscFixed | 0x00008000;
constexpr NEON2RegMiscOp NEON_CMGE_zero = NEON2RegMiscFixed | 0x20008000;
constexpr NEON2RegMiscOp NEON_CMEQ_zero = NEON2RegMiscFixed | 0x00009000;
constexpr NEON2RegMiscOp NEON_CMLE_zero = NEON2RegMiscFixed | 0x20009000;
constexpr NEON2RegMiscOp NEON_CMLT_zero = NEON2RegMiscFixed | 0x0000A000;
constexpr NEON2RegMiscOp NEON_ABS = NEON2RegMiscFixed | 0x0000B000;
constexpr NEON2RegMiscOp NEON_NEG = NEON2RegMiscFixed | 0x2000B000;
constexpr NEON2RegMiscOp NEON_XTN = NEON2RegMiscFixed | 0x00012000;
constexpr NEON2RegMiscOp NEON_SQXTUN = NEON2RegMiscFixed | 0x20012000;
constexpr NEON2RegMiscOp NEON_SHLL = NEON2RegMiscFixed | 0x20013000;
constexpr NEON2RegMiscOp NEON_SQXTN = NEON2RegMiscFixed | 0x00014000;
constexpr NEON2RegMiscOp NEON_UQXTN = NEON_SQXTN | NEON2RegMiscUBit;

constexpr NEON2RegMiscOp NEON2RegMiscOpcode = 0x0001F000;
constexpr NEON2RegMiscOp NEON_RBIT_NOT_opcode =
    NEON_RBIT_NOT & NEON2RegMiscOpcode;
constexpr NEON2RegMiscOp NEON_NEG_opcode = NEON_NEG & NEON2RegMiscOpcode;
constexpr NEON2RegMiscOp NEON_XTN_opcode = NEON_XTN & NEON2RegMiscOpcode;
constexpr NEON2RegMiscOp NEON_UQXTN_opcode = NEON_UQXTN & NEON2RegMiscOpcode;

// These instructions use only one bit of the size field. The other bit is
// used to distinguish between instructions.
constexpr NEON2RegMiscOp NEON2RegMiscFPMask = NEON2RegMiscMask | 0x00800000;
constexpr NEON2RegMiscOp NEON_FABS = NEON2RegMiscFixed | 0x0080F000;
constexpr NEON2RegMiscOp NEON_FNEG = NEON2RegMiscFixed | 0x2080F000;
constexpr NEON2RegMiscOp NEON_FCVTN = NEON2RegMiscFixed | 0x00016000;
constexpr NEON2RegMiscOp NEON_FCVTXN = NEON2RegMiscFixed | 0x20016000;
constexpr NEON2RegMiscOp NEON_FCVTL = NEON2RegMiscFixed | 0x00017000;
constexpr NEON2RegMiscOp NEON_FRINTN = NEON2RegMiscFixed | 0x00018000;
constexpr NEON2RegMiscOp NEON_FRINTA = NEON2RegMiscFixed | 0x20018000;
constexpr NEON2RegMiscOp NEON_FRINTP = NEON2RegMiscFixed | 0x00818000;
constexpr NEON2RegMiscOp NEON_FRINTM = NEON2RegMiscFixed | 0x00019000;
constexpr NEON2RegMiscOp NEON_FRINTX = NEON2RegMiscFixed | 0x20019000;
constexpr NEON2RegMiscOp NEON_FRINTZ = NEON2RegMiscFixed | 0x00819000;
constexpr NEON2RegMiscOp NEON_FRINTI = NEON2RegMiscFixed | 0x20819000;
constexpr NEON2RegMiscOp NEON_FCVTNS = NEON2RegMiscFixed | 0x0001A000;
constexpr NEON2RegMiscOp NEON_FCVTNU = NEON_FCVTNS | NEON2RegMiscUBit;
constexpr NEON2RegMiscOp NEON_FCVTPS = NEON2RegMiscFixed | 0x0081A000;
constexpr NEON2RegMiscOp NEON_FCVTPU = NEON_FCVTPS | NEON2RegMiscUBit;
constexpr NEON2RegMiscOp NEON_FCVTMS = NEON2RegMiscFixed | 0x0001B000;
constexpr NEON2RegMiscOp NEON_FCVTMU = NEON_FCVTMS | NEON2RegMiscUBit;
constexpr NEON2RegMiscOp NEON_FCVTZS = NEON2RegMiscFixed | 0x0081B000;
constexpr NEON2RegMiscOp NEON_FCVTZU = NEON_FCVTZS | NEON2RegMiscUBit;
constexpr NEON2RegMiscOp NEON_FCVTAS = NEON2RegMiscFixed | 0x0001C000;
constexpr NEON2RegMiscOp NEON_FCVTAU = NEON_FCVTAS | NEON2RegMiscUBit;
constexpr NEON2RegMiscOp NEON_FSQRT = NEON2RegMiscFixed | 0x2081F000;
constexpr NEON2RegMiscOp NEON_SCVTF = NEON2RegMiscFixed | 0x0001D000;
constexpr NEON2RegMiscOp NEON_UCVTF = NEON_SCVTF | NEON2RegMiscUBit;
constexpr NEON2RegMiscOp NEON_URSQRTE = NEON2RegMiscFixed | 0x2081C000;
constexpr NEON2RegMiscOp NEON_URECPE = NEON2RegMiscFixed | 0x0081C000;
constexpr NEON2RegMiscOp NEON_FRSQRTE = NEON2RegMiscFixed | 0x2081D000;
constexpr NEON2RegMiscOp NEON_FRECPE = NEON2RegMiscFixed | 0x0081D000;
constexpr NEON2RegMiscOp NEON_FCMGT_zero = NEON2RegMiscFixed | 0x0080C000;
constexpr NEON2RegMiscOp NEON_FCMGE_zero = NEON2RegMiscFixed | 0x2080C000;
constexpr NEON2RegMiscOp NEON_FCMEQ_zero = NEON2RegMiscFixed | 0x0080D000;
constexpr NEON2RegMiscOp NEON_FCMLE_zero = NEON2RegMiscFixed | 0x2080D000;
constexpr NEON2RegMiscOp NEON_FCMLT_zero = NEON2RegMiscFixed | 0x0080E000;

constexpr NEON2RegMiscOp NEON_FCVTL_opcode = NEON_FCVTL & NEON2RegMiscOpcode;
constexpr NEON2RegMiscOp NEON_FCVTN_opcode = NEON_FCVTN & NEON2RegMiscOpcode;

// NEON instructions with three same-type operands.
using NEON3SameOp = uint32_t;
constexpr NEON3SameOp NEON3SameFixed = 0x0E200400;
constexpr NEON3SameOp NEON3SameFMask = 0x9F200400;
constexpr NEON3SameOp NEON3SameMask = 0xBF20FC00;
constexpr NEON3SameOp NEON3SameUBit = 0x20000000;
constexpr NEON3SameOp NEON_ADD = NEON3SameFixed | 0x00008000;
constexpr NEON3SameOp NEON_ADDP = NEON3SameFixed | 0x0000B800;
constexpr NEON3SameOp NEON_SHADD = NEON3SameFixed | 0x00000000;
constexpr NEON3SameOp NEON_SHSUB = NEON3SameFixed | 0x00002000;
constexpr NEON3SameOp NEON_SRHADD = NEON3SameFixed | 0x00001000;
constexpr NEON3SameOp NEON_CMEQ = NEON3SameFixed | NEON3SameUBit | 0x00008800;
constexpr NEON3SameOp NEON_CMGE = NEON3SameFixed | 0x00003800;
constexpr NEON3SameOp NEON_CMGT = NEON3SameFixed | 0x00003000;
constexpr NEON3SameOp NEON_CMHI = NEON3SameFixed | NEON3SameUBit | NEON_CMGT;
constexpr NEON3SameOp NEON_CMHS = NEON3SameFixed | NEON3SameUBit | NEON_CMGE;
constexpr NEON3SameOp NEON_CMTST = NEON3SameFixed | 0x00008800;
constexpr NEON3SameOp NEON_MLA = NEON3SameFixed | 0x00009000;
constexpr NEON3SameOp NEON_MLS = NEON3SameFixed | 0x20009000;
constexpr NEON3SameOp NEON_MUL = NEON3SameFixed | 0x00009800;
constexpr NEON3SameOp NEON_PMUL = NEON3SameFixed | 0x20009800;
constexpr NEON3SameOp NEON_SRSHL = NEON3SameFixed | 0x00005000;
constexpr NEON3SameOp NEON_SQSHL = NEON3SameFixed | 0x00004800;
constexpr NEON3SameOp NEON_SQRSHL = NEON3SameFixed | 0x00005800;
constexpr NEON3SameOp NEON_SSHL = NEON3SameFixed | 0x00004000;
constexpr NEON3SameOp NEON_SMAX = NEON3SameFixed | 0x00006000;
constexpr NEON3SameOp NEON_SMAXP = NEON3SameFixed | 0x0000A000;
constexpr NEON3SameOp NEON_SMIN = NEON3SameFixed | 0x00006800;
constexpr NEON3SameOp NEON_SMINP = NEON3SameFixed | 0x0000A800;
constexpr NEON3SameOp NEON_SABD = NEON3SameFixed | 0x00007000;
constexpr NEON3SameOp NEON_SABA = NEON3SameFixed | 0x00007800;
constexpr NEON3SameOp NEON_UABD = NEON3SameFixed | NEON3SameUBit | NEON_SABD;
constexpr NEON3SameOp NEON_UABA = NEON3SameFixed | NEON3SameUBit | NEON_SABA;
constexpr NEON3SameOp NEON_SQADD = NEON3SameFixed | 0x00000800;
constexpr NEON3SameOp NEON_SQSUB = NEON3SameFixed | 0x00002800;
constexpr NEON3SameOp NEON_SUB = NEON3SameFixed | NEON3SameUBit | 0x00008000;
constexpr NEON3SameOp NEON_UHADD = NEON3SameFixed | NEON3SameUBit | NEON_SHADD;
constexpr NEON3SameOp NEON_UHSUB = NEON3SameFixed | NEON3SameUBit | NEON_SHSUB;
constexpr NEON3SameOp NEON_URHADD =
    NEON3SameFixed | NEON3SameUBit | NEON_SRHADD;
constexpr NEON3SameOp NEON_UMAX = NEON3SameFixed | NEON3SameUBit | NEON_SMAX;
constexpr NEON3SameOp NEON_UMAXP = NEON3SameFixed | NEON3SameUBit | NEON_SMAXP;
constexpr NEON3SameOp NEON_UMIN = NEON3SameFixed | NEON3SameUBit | NEON_SMIN;
constexpr NEON3SameOp NEON_UMINP = NEON3SameFixed | NEON3SameUBit | NEON_SMINP;
constexpr NEON3SameOp NEON_URSHL = NEON3SameFixed | NEON3SameUBit | NEON_SRSHL;
constexpr NEON3SameOp NEON_UQADD = NEON3SameFixed | NEON3SameUBit | NEON_SQADD;
constexpr NEON3SameOp NEON_UQRSHL =
    NEON3SameFixed | NEON3SameUBit | NEON_SQRSHL;
constexpr NEON3SameOp NEON_UQSHL = NEON3SameFixed | NEON3SameUBit | NEON_SQSHL;
constexpr NEON3SameOp NEON_UQSUB = NEON3SameFixed | NEON3SameUBit | NEON_SQSUB;
constexpr NEON3SameOp NEON_USHL = NEON3SameFixed | NEON3SameUBit | NEON_SSHL;
constexpr NEON3SameOp NEON_SQDMULH = NEON3SameFixed | 0x0000B000;
constexpr NEON3SameOp NEON_SQRDMULH = NEON3SameFixed | 0x2000B000;

// NEON floating point instructions with three same-type operands.
constexpr NEON3SameOp NEON3SameFPFixed = NEON3SameFixed | 0x0000C000;
constexpr NEON3SameOp NEON3SameFPFMask = NEON3SameFMask | 0x0000C000;
constexpr NEON3SameOp NEON3SameFPMask = NEON3SameMask | 0x00800000;
constexpr NEON3SameOp NEON_FADD = NEON3SameFixed | 0x0000D000;
constexpr NEON3SameOp NEON_FSUB = NEON3SameFixed | 0x0080D000;
constexpr NEON3SameOp NEON_FMUL = NEON3SameFixed | 0x2000D800;
constexpr NEON3SameOp NEON_FDIV = NEON3SameFixed | 0x2000F800;
constexpr NEON3SameOp NEON_FMAX = NEON3SameFixed | 0x0000F000;
constexpr NEON3SameOp NEON_FMAXNM = NEON3SameFixed | 0x0000C000;
constexpr NEON3SameOp NEON_FMAXP = NEON3SameFixed | 0x2000F000;
constexpr NEON3SameOp NEON_FMAXNMP = NEON3SameFixed | 0x2000C000;
constexpr NEON3SameOp NEON_FMIN = NEON3SameFixed | 0x0080F000;
constexpr NEON3SameOp NEON_FMINNM = NEON3SameFixed | 0x0080C000;
constexpr NEON3SameOp NEON_FMINP = NEON3SameFixed | 0x2080F000;
constexpr NEON3SameOp NEON_FMINNMP = NEON3SameFixed | 0x2080C000;
constexpr NEON3SameOp NEON_FMLA = NEON3SameFixed | 0x0000C800;
constexpr NEON3SameOp NEON_FMLS = NEON3SameFixed | 0x0080C800;
constexpr NEON3SameOp NEON_FMULX = NEON3SameFixed | 0x0000D800;
constexpr NEON3SameOp NEON_FRECPS = NEON3SameFixed | 0x0000F800;
constexpr NEON3SameOp NEON_FRSQRTS = NEON3SameFixed | 0x0080F800;
constexpr NEON3SameOp NEON_FABD = NEON3SameFixed | 0x2080D000;
constexpr NEON3SameOp NEON_FADDP = NEON3SameFixed | 0x2000D000;
constexpr NEON3SameOp NEON_FCMEQ = NEON3SameFixed | 0x0000E000;
constexpr NEON3SameOp NEON_FCMGE = NEON3SameFixed | 0x2000E000;
constexpr NEON3SameOp NEON_FCMGT = NEON3SameFixed | 0x2080E000;
constexpr NEON3SameOp NEON_FACGE = NEON3SameFixed | 0x2000E800;
constexpr NEON3SameOp NEON_FACGT = NEON3SameFixed | 0x2080E800;

constexpr NEON3SameOp NEON3SameHPMask = 0x0020C000;
constexpr NEON3SameOp NEON3SameHPFixed = 0x0E400400;
constexpr NEON3SameOp NEON3SameHPFMask = 0x9F400400;

// NEON logical instructions with three same-type operands.
constexpr NEON3SameOp NEON3SameLogicalFixed = NEON3SameFixed | 0x00001800;
constexpr NEON3SameOp NEON3SameLogicalFMask = NEON3SameFMask | 0x0000F800;
constexpr NEON3SameOp NEON3SameLogicalMask = 0xBFE0FC00;
constexpr NEON3SameOp NEON3SameLogicalFormatMask = NEON_Q;
constexpr NEON3SameOp NEON_AND = NEON3SameLogicalFixed | 0x00000000;
constexpr NEON3SameOp NEON_ORR = NEON3SameLogicalFixed | 0x00A00000;
constexpr NEON3SameOp NEON_ORN = NEON3SameLogicalFixed | 0x00C00000;
constexpr NEON3SameOp NEON_EOR = NEON3SameLogicalFixed | 0x20000000;
constexpr NEON3SameOp NEON_BIC = NEON3SameLogicalFixed | 0x00400000;
constexpr NEON3SameOp NEON_BIF = NEON3SameLogicalFixed | 0x20C00000;
constexpr NEON3SameOp NEON_BIT = NEON3SameLogicalFixed | 0x20800000;
constexpr NEON3SameOp NEON_BSL = NEON3SameLogicalFixed | 0x20400000;

// NEON instructions with three different-type operands.
using NEON3DifferentOp = uint32_t;
constexpr NEON3DifferentOp NEON3DifferentFixed = 0x0E200000;
constexpr NEON3DifferentOp NEON3DifferentDot = 0x0E800000;
constexpr NEON3DifferentOp NEON3DifferentFMask = 0x9F200C00;
constexpr NEON3DifferentOp NEON3DifferentMask = 0xFF20FC00;
constexpr NEON3DifferentOp NEON_ADDHN = NEON3DifferentFixed | 0x00004000;
constexpr NEON3DifferentOp NEON_ADDHN2 = NEON_ADDHN | NEON_Q;
constexpr NEON3DifferentOp NEON_PMULL = NEON3DifferentFixed | 0x0000E000;
constexpr NEON3DifferentOp NEON_PMULL2 = NEON_PMULL | NEON_Q;
constexpr NEON3DifferentOp NEON_RADDHN = NEON3DifferentFixed | 0x20004000;
constexpr NEON3DifferentOp NEON_RADDHN2 = NEON_RADDHN | NEON_Q;
constexpr NEON3DifferentOp NEON_RSUBHN = NEON3DifferentFixed | 0x20006000;
constexpr NEON3DifferentOp NEON_RSUBHN2 = NEON_RSUBHN | NEON_Q;
constexpr NEON3DifferentOp NEON_SABAL = NEON3DifferentFixed | 0x00005000;
constexpr NEON3DifferentOp NEON_SABAL2 = NEON_SABAL | NEON_Q;
constexpr NEON3DifferentOp NEON_SABDL = NEON3DifferentFixed | 0x00007000;
constexpr NEON3DifferentOp NEON_SABDL2 = NEON_SABDL | NEON_Q;
constexpr NEON3DifferentOp NEON_SADDL = NEON3DifferentFixed | 0x00000000;
constexpr NEON3DifferentOp NEON_SADDL2 = NEON_SADDL | NEON_Q;
constexpr NEON3DifferentOp NEON_SADDW = NEON3DifferentFixed | 0x00001000;
constexpr NEON3DifferentOp NEON_SADDW2 = NEON_SADDW | NEON_Q;
constexpr NEON3DifferentOp NEON_SMLAL = NEON3DifferentFixed | 0x00008000;
constexpr NEON3DifferentOp NEON_SMLAL2 = NEON_SMLAL | NEON_Q;
constexpr NEON3DifferentOp NEON_SMLSL = NEON3DifferentFixed | 0x0000A000;
constexpr NEON3DifferentOp NEON_SMLSL2 = NEON_SMLSL | NEON_Q;
constexpr NEON3DifferentOp NEON_SMULL = NEON3DifferentFixed | 0x0000C000;
constexpr NEON3DifferentOp NEON_SMULL2 = NEON_SMULL | NEON_Q;
constexpr NEON3DifferentOp NEON_SSUBL = NEON3DifferentFixed | 0x00002000;
constexpr NEON3DifferentOp NEON_SSUBL2 = NEON_SSUBL | NEON_Q;
constexpr NEON3DifferentOp NEON_SSUBW = NEON3DifferentFixed | 0x00003000;
constexpr NEON3DifferentOp NEON_SSUBW2 = NEON_SSUBW | NEON_Q;
constexpr NEON3DifferentOp NEON_SQDMLAL = NEON3DifferentFixed | 0x00009000;
constexpr NEON3DifferentOp NEON_SQDMLAL2 = NEON_SQDMLAL | NEON_Q;
constexpr NEON3DifferentOp NEON_SQDMLSL = NEON3DifferentFixed | 0x0000B000;
constexpr NEON3DifferentOp NEON_SQDMLSL2 = NEON_SQDMLSL | NEON_Q;
constexpr NEON3DifferentOp NEON_SQDMULL = NEON3DifferentFixed | 0x0000D000;
constexpr NEON3DifferentOp NEON_SQDMULL2 = NEON_SQDMULL | NEON_Q;
constexpr NEON3DifferentOp NEON_SUBHN = NEON3DifferentFixed | 0x00006000;
constexpr NEON3DifferentOp NEON_SUBHN2 = NEON_SUBHN | NEON_Q;
constexpr NEON3DifferentOp NEON_UABAL = NEON_SABAL | NEON3SameUBit;
constexpr NEON3DifferentOp NEON_UABAL2 = NEON_UABAL | NEON_Q;
constexpr NEON3DifferentOp NEON_UABDL = NEON_SABDL | NEON3SameUBit;
constexpr NEON3DifferentOp NEON_UABDL2 = NEON_UABDL | NEON_Q;
constexpr NEON3DifferentOp NEON_UADDL = NEON_SADDL | NEON3SameUBit;
constexpr NEON3DifferentOp NEON_UADDL2 = NEON_UADDL | NEON_Q;
constexpr NEON3DifferentOp NEON_UADDW = NEON_SADDW | NEON3SameUBit;
constexpr NEON3DifferentOp NEON_UADDW2 = NEON_UADDW | NEON_Q;
constexpr NEON3DifferentOp NEON_UMLAL = NEON_SMLAL | NEON3SameUBit;
constexpr NEON3DifferentOp NEON_UMLAL2 = NEON_UMLAL | NEON_Q;
constexpr NEON3DifferentOp NEON_UMLSL = NEON_SMLSL | NEON3SameUBit;
constexpr NEON3DifferentOp NEON_UMLSL2 = NEON_UMLSL | NEON_Q;
constexpr NEON3DifferentOp NEON_UMULL = NEON_SMULL | NEON3SameUBit;
constexpr NEON3DifferentOp NEON_UMULL2 = NEON_UMULL | NEON_Q;
constexpr NEON3DifferentOp NEON_USUBL = NEON_SSUBL | NEON3SameUBit;
constexpr NEON3DifferentOp NEON_USUBL2 = NEON_USUBL | NEON_Q;
constexpr NEON3DifferentOp NEON_USUBW = NEON_SSUBW | NEON3SameUBit;
constexpr NEON3DifferentOp NEON_USUBW2 = NEON_USUBW | NEON_Q;

// NEON instructions with three operands and extension.
using NEON3ExtensionOp = uint32_t;
constexpr NEON3ExtensionOp NEON3ExtensionFixed = 0x0E008400;
constexpr NEON3ExtensionOp NEON3ExtensionFMask = 0x9F208400;
constexpr NEON3ExtensionOp NEON3ExtensionMask = 0xBF20FC00;
constexpr NEON3ExtensionOp NEON_SDOT = NEON3ExtensionFixed | 0x00001000;

// NEON instructions operating across vectors.
using NEONAcrossLanesOp = uint32_t;
constexpr NEONAcrossLanesOp NEONAcrossLanesFixed = 0x0E300800;
constexpr NEONAcrossLanesOp NEONAcrossLanesFMask = 0x9F3E0C00;
constexpr NEONAcrossLanesOp NEONAcrossLanesMask = 0xBF3FFC00;
constexpr NEONAcrossLanesOp NEON_ADDV = NEONAcrossLanesFixed | 0x0001B000;
constexpr NEONAcrossLanesOp NEON_SADDLV = NEONAcrossLanesFixed | 0x00003000;
constexpr NEONAcrossLanesOp NEON_UADDLV = NEONAcrossLanesFixed | 0x20003000;
constexpr NEONAcrossLanesOp NEON_SMAXV = NEONAcrossLanesFixed | 0x0000A000;
constexpr NEONAcrossLanesOp NEON_SMINV = NEONAcrossLanesFixed | 0x0001A000;
constexpr NEONAcrossLanesOp NEON_UMAXV = NEONAcrossLanesFixed | 0x2000A000;
constexpr NEONAcrossLanesOp NEON_UMINV = NEONAcrossLanesFixed | 0x2001A000;

// NEON floating point across instructions.
constexpr NEONAcrossLanesOp NEONAcrossLanesFPFixed =
    NEONAcrossLanesFixed | 0x0000C000;
constexpr NEONAcrossLanesOp NEONAcrossLanesFPFMask =
    NEONAcrossLanesFMask | 0x0000C000;
constexpr NEONAcrossLanesOp NEONAcrossLanesFPMask =
    NEONAcrossLanesMask | 0x00800000;

constexpr NEONAcrossLanesOp NEON_FMAXV = NEONAcrossLanesFPFixed | 0x2000F000;
constexpr NEONAcrossLanesOp NEON_FMINV = NEONAcrossLanesFPFixed | 0x2080F000;
constexpr NEONAcrossLanesOp NEON_FMAXNMV = NEONAcrossLanesFPFixed | 0x2000C000;
constexpr NEONAcrossLanesOp NEON_FMINNMV = NEONAcrossLanesFPFixed | 0x2080C000;

// NEON instructions with indexed element operand.
using NEONByIndexedElementOp = uint32_t;
constexpr NEONByIndexedElementOp NEONByIndexedElementFixed = 0x0F000000;
constexpr NEONByIndexedElementOp NEONByIndexedElementFMask = 0x9F000400;
constexpr NEONByIndexedElementOp NEONByIndexedElementMask = 0xBF00F400;
constexpr NEONByIndexedElementOp NEON_MUL_byelement =
    NEONByIndexedElementFixed | 0x00008000;
constexpr NEONByIndexedElementOp NEON_MLA_byelement =
    NEONByIndexedElementFixed | 0x20000000;
constexpr NEONByIndexedElementOp NEON_MLS_byelement =
    NEONByIndexedElementFixed | 0x20004000;
constexpr NEONByIndexedElementOp NEON_SMULL_byelement =
    NEONByIndexedElementFixed | 0x0000A000;
constexpr NEONByIndexedElementOp NEON_SMLAL_byelement =
    NEONByIndexedElementFixed | 0x00002000;
constexpr NEONByIndexedElementOp NEON_SMLSL_byelement =
    NEONByIndexedElementFixed | 0x00006000;
constexpr NEONByIndexedElementOp NEON_UMULL_byelement =
    NEONByIndexedElementFixed | 0x2000A000;
constexpr NEONByIndexedElementOp NEON_UMLAL_byelement =
    NEONByIndexedElementFixed | 0x20002000;
constexpr NEONByIndexedElementOp NEON_UMLSL_byelement =
    NEONByIndexedElementFixed | 0x20006000;
constexpr NEONByIndexedElementOp NEON_SQDMULL_byelement =
    NEONByIndexedElementFixed | 0x0000B000;
constexpr NEONByIndexedElementOp NEON_SQDMLAL_byelement =
    NEONByIndexedElementFixed | 0x00003000;
constexpr NEONByIndexedElementOp NEON_SQDMLSL_byelement =
    NEONByIndexedElementFixed | 0x00007000;
constexpr NEONByIndexedElementOp NEON_SQDMULH_byelement =
    NEONByIndexedElementFixed | 0x0000C000;
constexpr NEONByIndexedElementOp NEON_SQRDMULH_byelement =
    NEONByIndexedElementFixed | 0x0000D000;

// Floating point instructions.
constexpr NEONByIndexedElementOp NEONByIndexedElementFPFixed =
    NEONByIndexedElementFixed | 0x00800000;
constexpr NEONByIndexedElementOp NEONByIndexedElementFPMask =
    NEONByIndexedElementMask | 0x00800000;
constexpr NEONByIndexedElementOp NEON_FMLA_byelement =
    NEONByIndexedElementFPFixed | 0x00001000;
constexpr NEONByIndexedElementOp NEON_FMLS_byelement =
    NEONByIndexedElementFPFixed | 0x00005000;
constexpr NEONByIndexedElementOp NEON_FMUL_byelement =
    NEONByIndexedElementFPFixed | 0x00009000;
constexpr NEONByIndexedElementOp NEON_FMULX_byelement =
    NEONByIndexedElementFPFixed | 0x20009000;

// NEON modified immediate.
using NEONModifiedImmediateOp = uint32_t;
constexpr NEONModifiedImmediateOp NEONModifiedImmediateFixed = 0x0F000400;
constexpr NEONModifiedImmediateOp NEONModifiedImmediateFMask = 0x9FF80400;
constexpr NEONModifiedImmediateOp NEONModifiedImmediateOpBit = 0x20000000;
constexpr NEONModifiedImmediateOp NEONModifiedImmediate_MOVI =
    NEONModifiedImmediateFixed | 0x00000000;
constexpr NEONModifiedImmediateOp NEONModifiedImmediate_MVNI =
    NEONModifiedImmediateFixed | 0x20000000;
constexpr NEONModifiedImmediateOp NEONModifiedImmediate_ORR =
    NEONModifiedImmediateFixed | 0x00001000;
constexpr NEONModifiedImmediateOp NEONModifiedImmediate_BIC =
    NEONModifiedImmediateFixed | 0x20001000;

// NEON extract.
using NEONExtractOp = uint32_t;
constexpr NEONExtractOp NEONExtractFixed = 0x2E000000;
constexpr NEONExtractOp NEONExtractFMask = 0xBF208400;
constexpr NEONExtractOp NEONExtractMask = 0xBFE08400;
constexpr NEONExtractOp NEON_EXT = NEONExtractFixed | 0x00000000;

using NEONLoadStoreMultiOp = uint32_t;
constexpr NEONLoadStoreMultiOp NEONLoadStoreMultiL = 0x00400000;
constexpr NEONLoadStoreMultiOp NEONLoadStoreMulti1_1v = 0x00007000;
constexpr NEONLoadStoreMultiOp NEONLoadStoreMulti1_2v = 0x0000A000;
constexpr NEONLoadStoreMultiOp NEONLoadStoreMulti1_3v = 0x00006000;
constexpr NEONLoadStoreMultiOp NEONLoadStoreMulti1_4v = 0x00002000;
constexpr NEONLoadStoreMultiOp NEONLoadStoreMulti2 = 0x00008000;
constexpr NEONLoadStoreMultiOp NEONLoadStoreMulti3 = 0x00004000;
constexpr NEONLoadStoreMultiOp NEONLoadStoreMulti4 = 0x00000000;

// NEON load/store multiple structures.
using NEONLoadStoreMultiStructOp = uint32_t;
constexpr NEONLoadStoreMultiStructOp NEONLoadStoreMultiStructFixed = 0x0C000000;
constexpr NEONLoadStoreMultiStructOp NEONLoadStoreMultiStructFMask = 0xBFBF0000;
constexpr NEONLoadStoreMultiStructOp NEONLoadStoreMultiStructMask = 0xBFFFF000;
constexpr NEONLoadStoreMultiStructOp NEONLoadStoreMultiStructStore =
    NEONLoadStoreMultiStructFixed;
constexpr NEONLoadStoreMultiStructOp NEONLoadStoreMultiStructLoad =
    NEONLoadStoreMultiStructFixed | NEONLoadStoreMultiL;
constexpr NEONLoadStoreMultiStructOp NEON_LD1_1v =
    NEONLoadStoreMultiStructLoad | NEONLoadStoreMulti1_1v;
constexpr NEONLoadStoreMultiStructOp NEON_LD1_2v =
    NEONLoadStoreMultiStructLoad | NEONLoadStoreMulti1_2v;
constexpr NEONLoadStoreMultiStructOp NEON_LD1_3v =
    NEONLoadStoreMultiStructLoad | NEONLoadStoreMulti1_3v;
constexpr NEONLoadStoreMultiStructOp NEON_LD1_4v =
    NEONLoadStoreMultiStructLoad | NEONLoadStoreMulti1_4v;
constexpr NEONLoadStoreMultiStructOp NEON_LD2 =
    NEONLoadStoreMultiStructLoad | NEONLoadStoreMulti2;
constexpr NEONLoadStoreMultiStructOp NEON_LD3 =
    NEONLoadStoreMultiStructLoad | NEONLoadStoreMulti3;
constexpr NEONLoadStoreMultiStructOp NEON_LD4 =
    NEONLoadStoreMultiStructLoad | NEONLoadStoreMulti4;
constexpr NEONLoadStoreMultiStructOp NEON_ST1_1v =
    NEONLoadStoreMultiStructStore | NEONLoadStoreMulti1_1v;
constexpr NEONLoadStoreMultiStructOp NEON_ST1_2v =
    NEONLoadStoreMultiStructStore | NEONLoadStoreMulti1_2v;
constexpr NEONLoadStoreMultiStructOp NEON_ST1_3v =
    NEONLoadStoreMultiStructStore | NEONLoadStoreMulti1_3v;
constexpr NEONLoadStoreMultiStructOp NEON_ST1_4v =
    NEONLoadStoreMultiStructStore | NEONLoadStoreMulti1_4v;
constexpr NEONLoadStoreMultiStructOp NEON_ST2 =
    NEONLoadStoreMultiStructStore | NEONLoadStoreMulti2;
constexpr NEONLoadStoreMultiStructOp NEON_ST3 =
    NEONLoadStoreMultiStructStore | NEONLoadStoreMulti3;
constexpr NEONLoadStoreMultiStructOp NEON_ST4 =
    NEONLoadStoreMultiStructStore | NEONLoadStoreMulti4;

// NEON load/store multiple structures with post-index addressing.
using NEONLoadStoreMultiStructPostIndexOp = uint32_t;
constexpr NEONLoadStoreMultiStructPostIndexOp
    NEONLoadStoreMultiStructPostIndexFixed = 0x0C800000;
constexpr NEONLoadStoreMultiStructPostIndexOp
    NEONLoadStoreMultiStructPostIndexFMask = 0xBFA00000;
constexpr NEONLoadStoreMultiStructPostIndexOp
    NEONLoadStoreMultiStructPostIndexMask = 0xBFE0F000;
constexpr NEONLoadStoreMultiStructPostIndexOp
    NEONLoadStoreMultiStructPostIndex = 0x00800000;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_LD1_1v_post =
    NEON_LD1_1v | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_LD1_2v_post =
    NEON_LD1_2v | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_LD1_3v_post =
    NEON_LD1_3v | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_LD1_4v_post =
    NEON_LD1_4v | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_LD2_post =
    NEON_LD2 | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_LD3_post =
    NEON_LD3 | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_LD4_post =
    NEON_LD4 | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_ST1_1v_post =
    NEON_ST1_1v | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_ST1_2v_post =
    NEON_ST1_2v | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_ST1_3v_post =
    NEON_ST1_3v | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_ST1_4v_post =
    NEON_ST1_4v | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_ST2_post =
    NEON_ST2 | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_ST3_post =
    NEON_ST3 | NEONLoadStoreMultiStructPostIndex;
constexpr NEONLoadStoreMultiStructPostIndexOp NEON_ST4_post =
    NEON_ST4 | NEONLoadStoreMultiStructPostIndex;

using NEONLoadStoreSingleOp = uint32_t;
constexpr NEONLoadStoreSingleOp NEONLoadStoreSingle1 = 0x00000000;
constexpr NEONLoadStoreSingleOp NEONLoadStoreSingle2 = 0x00200000;
constexpr NEONLoadStoreSingleOp NEONLoadStoreSingle3 = 0x00002000;
constexpr NEONLoadStoreSingleOp NEONLoadStoreSingle4 = 0x00202000;
constexpr NEONLoadStoreSingleOp NEONLoadStoreSingleL = 0x00400000;
constexpr NEONLoadStoreSingleOp NEONLoadStoreSingle_b = 0x00000000;
constexpr NEONLoadStoreSingleOp NEONLoadStoreSingle_h = 0x00004000;
constexpr NEONLoadStoreSingleOp NEONLoadStoreSingle_s = 0x00008000;
constexpr NEONLoadStoreSingleOp NEONLoadStoreSingle_d = 0x00008400;
constexpr NEONLoadStoreSingleOp NEONLoadStoreSingleAllLanes = 0x0000C000;
constexpr NEONLoadStoreSingleOp NEONLoadStoreSingleLenMask = 0x00202000;

// NEON load/store single structure.
using NEONLoadStoreSingleStructOp = uint32_t;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructFixed =
    0x0D000000;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructFMask =
    0xBF9F0000;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructMask =
    0xBFFFE000;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructStore =
    NEONLoadStoreSingleStructFixed;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructLoad =
    NEONLoadStoreSingleStructFixed | NEONLoadStoreSingleL;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructLoad1 =
    NEONLoadStoreSingle1 | NEONLoadStoreSingleStructLoad;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructLoad2 =
    NEONLoadStoreSingle2 | NEONLoadStoreSingleStructLoad;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructLoad3 =
    NEONLoadStoreSingle3 | NEONLoadStoreSingleStructLoad;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructLoad4 =
    NEONLoadStoreSingle4 | NEONLoadStoreSingleStructLoad;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructStore1 =
    NEONLoadStoreSingle1 | NEONLoadStoreSingleStructFixed;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructStore2 =
    NEONLoadStoreSingle2 | NEONLoadStoreSingleStructFixed;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructStore3 =
    NEONLoadStoreSingle3 | NEONLoadStoreSingleStructFixed;
constexpr NEONLoadStoreSingleStructOp NEONLoadStoreSingleStructStore4 =
    NEONLoadStoreSingle4 | NEONLoadStoreSingleStructFixed;
constexpr NEONLoadStoreSingleStructOp NEON_LD1_b =
    NEONLoadStoreSingleStructLoad1 | NEONLoadStoreSingle_b;
constexpr NEONLoadStoreSingleStructOp NEON_LD1_h =
    NEONLoadStoreSingleStructLoad1 | NEONLoadStoreSingle_h;
constexpr NEONLoadStoreSingleStructOp NEON_LD1_s =
    NEONLoadStoreSingleStructLoad1 | NEONLoadStoreSingle_s;
constexpr NEONLoadStoreSingleStructOp NEON_LD1_d =
    NEONLoadStoreSingleStructLoad1 | NEONLoadStoreSingle_d;
constexpr NEONLoadStoreSingleStructOp NEON_LD1R =
    NEONLoadStoreSingleStructLoad1 | NEONLoadStoreSingleAllLanes;
constexpr NEONLoadStoreSingleStructOp NEON_ST1_b =
    NEONLoadStoreSingleStructStore1 | NEONLoadStoreSingle_b;
constexpr NEONLoadStoreSingleStructOp NEON_ST1_h =
    NEONLoadStoreSingleStructStore1 | NEONLoadStoreSingle_h;
constexpr NEONLoadStoreSingleStructOp NEON_ST1_s =
    NEONLoadStoreSingleStructStore1 | NEONLoadStoreSingle_s;
constexpr NEONLoadStoreSingleStructOp NEON_ST1_d =
    NEONLoadStoreSingleStructStore1 | NEONLoadStoreSingle_d;

constexpr NEONLoadStoreSingleStructOp NEON_LD2_b =
    NEONLoadStoreSingleStructLoad2 | NEONLoadStoreSingle_b;
constexpr NEONLoadStoreSingleStructOp NEON_LD2_h =
    NEONLoadStoreSingleStructLoad2 | NEONLoadStoreSingle_h;
constexpr NEONLoadStoreSingleStructOp NEON_LD2_s =
    NEONLoadStoreSingleStructLoad2 | NEONLoadStoreSingle_s;
constexpr NEONLoadStoreSingleStructOp NEON_LD2_d =
    NEONLoadStoreSingleStructLoad2 | NEONLoadStoreSingle_d;
constexpr NEONLoadStoreSingleStructOp NEON_LD2R =
    NEONLoadStoreSingleStructLoad2 | NEONLoadStoreSingleAllLanes;
constexpr NEONLoadStoreSingleStructOp NEON_ST2_b =
    NEONLoadStoreSingleStructStore2 | NEONLoadStoreSingle_b;
constexpr NEONLoadStoreSingleStructOp NEON_ST2_h =
    NEONLoadStoreSingleStructStore2 | NEONLoadStoreSingle_h;
constexpr NEONLoadStoreSingleStructOp NEON_ST2_s =
    NEONLoadStoreSingleStructStore2 | NEONLoadStoreSingle_s;
constexpr NEONLoadStoreSingleStructOp NEON_ST2_d =
    NEONLoadStoreSingleStructStore2 | NEONLoadStoreSingle_d;

constexpr NEONLoadStoreSingleStructOp NEON_LD3_b =
    NEONLoadStoreSingleStructLoad3 | NEONLoadStoreSingle_b;
constexpr NEONLoadStoreSingleStructOp NEON_LD3_h =
    NEONLoadStoreSingleStructLoad3 | NEONLoadStoreSingle_h;
constexpr NEONLoadStoreSingleStructOp NEON_LD3_s =
    NEONLoadStoreSingleStructLoad3 | NEONLoadStoreSingle_s;
constexpr NEONLoadStoreSingleStructOp NEON_LD3_d =
    NEONLoadStoreSingleStructLoad3 | NEONLoadStoreSingle_d;
constexpr NEONLoadStoreSingleStructOp NEON_LD3R =
    NEONLoadStoreSingleStructLoad3 | NEONLoadStoreSingleAllLanes;
constexpr NEONLoadStoreSingleStructOp NEON_ST3_b =
    NEONLoadStoreSingleStructStore3 | NEONLoadStoreSingle_b;
constexpr NEONLoadStoreSingleStructOp NEON_ST3_h =
    NEONLoadStoreSingleStructStore3 | NEONLoadStoreSingle_h;
constexpr NEONLoadStoreSingleStructOp NEON_ST3_s =
    NEONLoadStoreSingleStructStore3 | NEONLoadStoreSingle_s;
constexpr NEONLoadStoreSingleStructOp NEON_ST3_d =
    NEONLoadStoreSingleStructStore3 | NEONLoadStoreSingle_d;

constexpr NEONLoadStoreSingleStructOp NEON_LD4_b =
    NEONLoadStoreSingleStructLoad4 | NEONLoadStoreSingle_b;
constexpr NEONLoadStoreSingleStructOp NEON_LD4_h =
    NEONLoadStoreSingleStructLoad4 | NEONLoadStoreSingle_h;
constexpr NEONLoadStoreSingleStructOp NEON_LD4_s =
    NEONLoadStoreSingleStructLoad4 | NEONLoadStoreSingle_s;
constexpr NEONLoadStoreSingleStructOp NEON_LD4_d =
    NEONLoadStoreSingleStructLoad4 | NEONLoadStoreSingle_d;
constexpr NEONLoadStoreSingleStructOp NEON_LD4R =
    NEONLoadStoreSingleStructLoad4 | NEONLoadStoreSingleAllLanes;
constexpr NEONLoadStoreSingleStructOp NEON_ST4_b =
    NEONLoadStoreSingleStructStore4 | NEONLoadStoreSingle_b;
constexpr NEONLoadStoreSingleStructOp NEON_ST4_h =
    NEONLoadStoreSingleStructStore4 | NEONLoadStoreSingle_h;
constexpr NEONLoadStoreSingleStructOp NEON_ST4_s =
    NEONLoadStoreSingleStructStore4 | NEONLoadStoreSingle_s;
constexpr NEONLoadStoreSingleStructOp NEON_ST4_d =
    NEONLoadStoreSingleStructStore4 | NEONLoadStoreSingle_d;

// NEON load/store single structure with post-index addressing.
using NEONLoadStoreSingleStructPostIndexOp = uint32_t;
constexpr NEONLoadStoreSingleStructPostIndexOp
    NEONLoadStoreSingleStructPostIndexFixed = 0x0D800000;
constexpr NEONLoadStoreSingleStructPostIndexOp
    NEONLoadStoreSingleStructPostIndexFMask = 0xBF800000;
constexpr NEONLoadStoreSingleStructPostIndexOp
    NEONLoadStoreSingleStructPostIndexMask = 0xBFE0E000;
constexpr NEONLoadStoreSingleStructPostIndexOp
    NEONLoadStoreSingleStructPostIndex = 0x00800000;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD1_b_post =
    NEON_LD1_b | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD1_h_post =
    NEON_LD1_h | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD1_s_post =
    NEON_LD1_s | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD1_d_post =
    NEON_LD1_d | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD1R_post =
    NEON_LD1R | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST1_b_post =
    NEON_ST1_b | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST1_h_post =
    NEON_ST1_h | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST1_s_post =
    NEON_ST1_s | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST1_d_post =
    NEON_ST1_d | NEONLoadStoreSingleStructPostIndex;

constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD2_b_post =
    NEON_LD2_b | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD2_h_post =
    NEON_LD2_h | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD2_s_post =
    NEON_LD2_s | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD2_d_post =
    NEON_LD2_d | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD2R_post =
    NEON_LD2R | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST2_b_post =
    NEON_ST2_b | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST2_h_post =
    NEON_ST2_h | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST2_s_post =
    NEON_ST2_s | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST2_d_post =
    NEON_ST2_d | NEONLoadStoreSingleStructPostIndex;

constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD3_b_post =
    NEON_LD3_b | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD3_h_post =
    NEON_LD3_h | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD3_s_post =
    NEON_LD3_s | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD3_d_post =
    NEON_LD3_d | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD3R_post =
    NEON_LD3R | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST3_b_post =
    NEON_ST3_b | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST3_h_post =
    NEON_ST3_h | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST3_s_post =
    NEON_ST3_s | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST3_d_post =
    NEON_ST3_d | NEONLoadStoreSingleStructPostIndex;

constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD4_b_post =
    NEON_LD4_b | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD4_h_post =
    NEON_LD4_h | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD4_s_post =
    NEON_LD4_s | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD4_d_post =
    NEON_LD4_d | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_LD4R_post =
    NEON_LD4R | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST4_b_post =
    NEON_ST4_b | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST4_h_post =
    NEON_ST4_h | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST4_s_post =
    NEON_ST4_s | NEONLoadStoreSingleStructPostIndex;
constexpr NEONLoadStoreSingleStructPostIndexOp NEON_ST4_d_post =
    NEON_ST4_d | NEONLoadStoreSingleStructPostIndex;

// NEON register copy.
using NEONCopyOp = uint32_t;
constexpr NEONCopyOp NEONCopyFixed = 0x0E000400;
constexpr NEONCopyOp NEONCopyFMask = 0x9FE08400;
constexpr NEONCopyOp NEONCopyMask = 0x3FE08400;
constexpr NEONCopyOp NEONCopyInsElementMask = NEONCopyMask | 0x40000000;
constexpr NEONCopyOp NEONCopyInsGeneralMask = NEONCopyMask | 0x40007800;
constexpr NEONCopyOp NEONCopyDupElementMask = NEONCopyMask | 0x20007800;
constexpr NEONCopyOp NEONCopyDupGeneralMask = NEONCopyDupElementMask;
constexpr NEONCopyOp NEONCopyUmovMask = NEONCopyMask | 0x20007800;
constexpr NEONCopyOp NEONCopySmovMask = NEONCopyMask | 0x20007800;
constexpr NEONCopyOp NEON_INS_ELEMENT = NEONCopyFixed | 0x60000000;
constexpr NEONCopyOp NEON_INS_GENERAL = NEONCopyFixed | 0x40001800;
constexpr NEONCopyOp NEON_DUP_ELEMENT = NEONCopyFixed | 0x00000000;
constexpr NEONCopyOp NEON_DUP_GENERAL = NEONCopyFixed | 0x00000800;
constexpr NEONCopyOp NEON_SMOV = NEONCopyFixed | 0x00002800;
constexpr NEONCopyOp NEON_UMOV = NEONCopyFixed | 0x00003800;

// NEON scalar instructions with indexed element operand.
using NEONScalarByIndexedElementOp = uint32_t;
constexpr NEONScalarByIndexedElementOp NEONScalarByIndexedElementFixed =
    0x5F000000;
constexpr NEONScalarByIndexedElementOp NEONScalarByIndexedElementFMask =
    0xDF000400;
constexpr NEONScalarByIndexedElementOp NEONScalarByIndexedElementMask =
    0xFF00F400;
constexpr NEONScalarByIndexedElementOp NEON_SQDMLAL_byelement_scalar =
    NEON_Q | NEONScalar | NEON_SQDMLAL_byelement;
constexpr NEONScalarByIndexedElementOp NEON_SQDMLSL_byelement_scalar =
    NEON_Q | NEONScalar | NEON_SQDMLSL_byelement;
constexpr NEONScalarByIndexedElementOp NEON_SQDMULL_byelement_scalar =
    NEON_Q | NEONScalar | NEON_SQDMULL_byelement;
constexpr NEONScalarByIndexedElementOp NEON_SQDMULH_byelement_scalar =
    NEON_Q | NEONScalar | NEON_SQDMULH_byelement;
constexpr NEONScalarByIndexedElementOp NEON_SQRDMULH_byelement_scalar =
    NEON_Q | NEONScalar | NEON_SQRDMULH_byelement;

// Floating point instructions.
constexpr NEONScalarByIndexedElementOp NEONScalarByIndexedElementFPFixed =
    NEONScalarByIndexedElementFixed | 0x00800000;
constexpr NEONScalarByIndexedElementOp NEONScalarByIndexedElementFPMask =
    NEONScalarByIndexedElementMask | 0x00800000;
constexpr NEONScalarByIndexedElementOp NEON_FMLA_byelement_scalar =
    NEON_Q | NEONScalar | NEON_FMLA_byelement;
constexpr NEONScalarByIndexedElementOp NEON_FMLS_byelement_scalar =
    NEON_Q | NEONScalar | NEON_FMLS_byelement;
constexpr NEONScalarByIndexedElementOp NEON_FMUL_byelement_scalar =
    NEON_Q | NEONScalar | NEON_FMUL_byelement;
constexpr NEONScalarByIndexedElementOp NEON_FMULX_byelement_scalar =
    NEON_Q | NEONScalar | NEON_FMULX_byelement;

// NEON shift immediate.
using NEONShiftImmediateOp = uint32_t;
constexpr NEONShiftImmediateOp NEONShiftImmediateFixed = 0x0F000400;
constexpr NEONShiftImmediateOp NEONShiftImmediateFMask = 0x9F800400;
constexpr NEONShiftImmediateOp NEONShiftImmediateMask = 0xBF80FC00;
constexpr NEONShiftImmediateOp NEONShiftImmediateUBit = 0x20000000;
constexpr NEONShiftImmediateOp NEON_SHL = NEONShiftImmediateFixed | 0x00005000;
constexpr NEONShiftImmediateOp NEON_SSHLL =
    NEONShiftImmediateFixed | 0x0000A000;
constexpr NEONShiftImmediateOp NEON_USHLL =
    NEONShiftImmediateFixed | 0x2000A000;
constexpr NEONShiftImmediateOp NEON_SLI = NEONShiftImmediateFixed | 0x20005000;
constexpr NEONShiftImmediateOp NEON_SRI = NEONShiftImmediateFixed | 0x20004000;
constexpr NEONShiftImmediateOp NEON_SHRN = NEONShiftImmediateFixed | 0x00008000;
constexpr NEONShiftImmediateOp NEON_RSHRN =
    NEONShiftImmediateFixed | 0x00008800;
constexpr NEONShiftImmediateOp NEON_UQSHRN =
    NEONShiftImmediateFixed | 0x20009000;
constexpr NEONShiftImmediateOp NEON_UQRSHRN =
    NEONShiftImmediateFixed | 0x20009800;
constexpr NEONShiftImmediateOp NEON_SQSHRN =
    NEONShiftImmediateFixed | 0x00009000;
constexpr NEONShiftImmediateOp NEON_SQRSHRN =
    NEONShiftImmediateFixed | 0x00009800;
constexpr NEONShiftImmediateOp NEON_SQSHRUN =
    NEONShiftImmediateFixed | 0x20008000;
constexpr NEONShiftImmediateOp NEON_SQRSHRUN =
    NEONShiftImmediateFixed | 0x20008800;
constexpr NEONShiftImmediateOp NEON_SSHR = NEONShiftImmediateFixed | 0x00000000;
constexpr NEONShiftImmediateOp NEON_SRSHR =
    NEONShiftImmediateFixed | 0x00002000;
constexpr NEONShiftImmediateOp NEON_USHR = NEONShiftImmediateFixed | 0x20000000;
constexpr NEONShiftImmediateOp NEON_URSHR =
    NEONShiftImmediateFixed | 0x20002000;
constexpr NEONShiftImmediateOp NEON_SSRA = NEONShiftImmediateFixed | 0x00001000;
constexpr NEONShiftImmediateOp NEON_SRSRA =
    NEONShiftImmediateFixed | 0x00003000;
constexpr NEONShiftImmediateOp NEON_USRA = NEONShiftImmediateFixed | 0x20001000;
constexpr NEONShiftImmediateOp NEON_URSRA =
    NEONShiftImmediateFixed | 0x20003000;
constexpr NEONShiftImmediateOp NEON_SQSHLU =
    NEONShiftImmediateFixed | 0x20006000;
constexpr NEONShiftImmediateOp NEON_SCVTF_imm =
    NEONShiftImmediateFixed | 0x0000E000;
constexpr NEONShiftImmediateOp NEON_UCVTF_imm =
    NEONShiftImmediateFixed | 0x2000E000;
constexpr NEONShiftImmediateOp NEON_FCVTZS_imm =
    NEONShiftImmediateFixed | 0x0000F800;
constexpr NEONShiftImmediateOp NEON_FCVTZU_imm =
    NEONShiftImmediateFixed | 0x2000F800;
constexpr NEONShiftImmediateOp NEON_SQSHL_imm =
    NEONShiftImmediateFixed | 0x00007000;
constexpr NEONShiftImmediateOp NEON_UQSHL_imm =
    NEONShiftImmediateFixed | 0x20007000;

// NEON scalar register copy.
using NEONScalarCopyOp = uint32_t;
constexpr NEONScalarCopyOp NEONScalarCopyFixed = 0x5E000400;
constexpr NEONScalarCopyOp NEONScalarCopyFMask = 0xDFE08400;
constexpr NEONScalarCopyOp NEONScalarCopyMask = 0xFFE0FC00;
constexpr NEONScalarCopyOp NEON_DUP_ELEMENT_scalar =
    NEON_Q | NEONScalar | NEON_DUP_ELEMENT;

// NEON scalar pairwise instructions.
using NEONScalarPairwiseOp = uint32_t;
constexpr NEONScalarPairwiseOp NEONScalarPairwiseFixed = 0x5E300800;
constexpr NEONScalarPairwiseOp NEONScalarPairwiseFMask = 0xDF3E0C00;
constexpr NEONScalarPairwiseOp NEONScalarPairwiseMask = 0xFFB1F800;
constexpr NEONScalarPairwiseOp NEON_ADDP_scalar =
    NEONScalarPairwiseFixed | 0x0081B000;
constexpr NEONScalarPairwiseOp NEON_FMAXNMP_scalar =
    NEONScalarPairwiseFixed | 0x2000C000;
constexpr NEONScalarPairwiseOp NEON_FMINNMP_scalar =
    NEONScalarPairwiseFixed | 0x2080C000;
constexpr NEONScalarPairwiseOp NEON_FADDP_scalar =
    NEONScalarPairwiseFixed | 0x2000D000;
constexpr NEONScalarPairwiseOp NEON_FMAXP_scalar =
    NEONScalarPairwiseFixed | 0x2000F000;
constexpr NEONScalarPairwiseOp NEON_FMINP_scalar =
    NEONScalarPairwiseFixed | 0x2080F000;

// NEON scalar shift immediate.
using NEONScalarShiftImmediateOp = uint32_t;
constexpr NEONScalarShiftImmediateOp NEONScalarShiftImmediateFixed = 0x5F000400;
constexpr NEONScalarShiftImmediateOp NEONScalarShiftImmediateFMask = 0xDF800400;
constexpr NEONScalarShiftImmediateOp NEONScalarShiftImmediateMask = 0xFF80FC00;
constexpr NEONScalarShiftImmediateOp NEON_SHL_scalar =
    NEON_Q | NEONScalar | NEON_SHL;
constexpr NEONScalarShiftImmediateOp NEON_SLI_scalar =
    NEON_Q | NEONScalar | NEON_SLI;
constexpr NEONScalarShiftImmediateOp NEON_SRI_scalar =
    NEON_Q | NEONScalar | NEON_SRI;
constexpr NEONScalarShiftImmediateOp NEON_SSHR_scalar =
    NEON_Q | NEONScalar | NEON_SSHR;
constexpr NEONScalarShiftImmediateOp NEON_USHR_scalar =
    NEON_Q | NEONScalar | NEON_USHR;
constexpr NEONScalarShiftImmediateOp NEON_SRSHR_scalar =
    NEON_Q | NEONScalar | NEON_SRSHR;
constexpr NEONScalarShiftImmediateOp NEON_URSHR_scalar =
    NEON_Q | NEONScalar | NEON_URSHR;
constexpr NEONScalarShiftImmediateOp NEON_SSRA_scalar =
    NEON_Q | NEONScalar | NEON_SSRA;
constexpr NEONScalarShiftImmediateOp NEON_USRA_scalar =
    NEON_Q | NEONScalar | NEON_USRA;
constexpr NEONScalarShiftImmediateOp NEON_SRSRA_scalar =
    NEON_Q | NEONScalar | NEON_SRSRA;
constexpr NEONScalarShiftImmediateOp NEON_URSRA_scalar =
    NEON_Q | NEONScalar | NEON_URSRA;
constexpr NEONScalarShiftImmediateOp NEON_UQSHRN_scalar =
    NEON_Q | NEONScalar | NEON_UQSHRN;
constexpr NEONScalarShiftImmediateOp NEON_UQRSHRN_scalar =
    NEON_Q | NEONScalar | NEON_UQRSHRN;
constexpr NEONScalarShiftImmediateOp NEON_SQSHRN_scalar =
    NEON_Q | NEONScalar | NEON_SQSHRN;
constexpr NEONScalarShiftImmediateOp NEON_SQRSHRN_scalar =
    NEON_Q | NEONScalar | NEON_SQRSHRN;
constexpr NEONScalarShiftImmediateOp NEON_SQSHRUN_scalar =
    NEON_Q | NEONScalar | NEON_SQSHRUN;
constexpr NEONScalarShiftImmediateOp NEON_SQRSHRUN_scalar =
    NEON_Q | NEONScalar | NEON_SQRSHRUN;
constexpr NEONScalarShiftImmediateOp NEON_SQSHLU_scalar =
    NEON_Q | NEONScalar | NEON_SQSHLU;
constexpr NEONScalarShiftImmediateOp NEON_SQSHL_imm_scalar =
    NEON_Q | NEONScalar | NEON_SQSHL_imm;
constexpr NEONScalarShiftImmediateOp NEON_UQSHL_imm_scalar =
    NEON_Q | NEONScalar | NEON_UQSHL_imm;
constexpr NEONScalarShiftImmediateOp NEON_SCVTF_imm_scalar =
    NEON_Q | NEONScalar | NEON_SCVTF_imm;
constexpr NEONScalarShiftImmediateOp NEON_UCVTF_imm_scalar =
    NEON_Q | NEONScalar | NEON_UCVTF_imm;
constexpr NEONScalarShiftImmediateOp NEON_FCVTZS_imm_scalar =
    NEON_Q | NEONScalar | NEON_FCVTZS_imm;
constexpr NEONScalarShiftImmediateOp NEON_FCVTZU_imm_scalar =
    NEON_Q | NEONScalar | NEON_FCVTZU_imm;

// NEON table.
using NEONTableOp = uint32_t;
constexpr NEONTableOp NEONTableFixed = 0x0E000000;
constexpr NEONTableOp NEONTableFMask = 0xBF208C00;
constexpr NEONTableOp NEONTableExt = 0x00001000;
constexpr NEONTableOp NEONTableMask = 0xBF20FC00;
constexpr NEONTableOp NEON_TBL_1v = NEONTableFixed | 0x00000000;
constexpr NEONTableOp NEON_TBL_2v = NEONTableFixed | 0x00002000;
constexpr NEONTableOp NEON_TBL_3v = NEONTableFixed | 0x00004000;
constexpr NEONTableOp NEON_TBL_4v = NEONTableFixed | 0x00006000;
constexpr NEONTableOp NEON_TBX_1v = NEON_TBL_1v | NEONTableExt;
constexpr NEONTableOp NEON_TBX_2v = NEON_TBL_2v | NEONTableExt;
constexpr NEONTableOp NEON_TBX_3v = NEON_TBL_3v | NEONTableExt;
constexpr NEONTableOp NEON_TBX_4v = NEON_TBL_4v | NEONTableExt;

// NEON perm.
using NEONPermOp = uint32_t;
constexpr NEONPermOp NEONPermFixed = 0x0E000800;
constexpr NEONPermOp NEONPermFMask = 0xBF208C00;
constexpr NEONPermOp NEONPermMask = 0x3F20FC00;
constexpr NEONPermOp NEON_UZP1 = NEONPermFixed | 0x00001000;
constexpr NEONPermOp NEON_TRN1 = NEONPermFixed | 0x00002000;
constexpr NEONPermOp NEON_ZIP1 = NEONPermFixed | 0x00003000;
constexpr NEONPermOp NEON_UZP2 = NEONPermFixed | 0x00005000;
constexpr NEONPermOp NEON_TRN2 = NEONPermFixed | 0x00006000;
constexpr NEONPermOp NEON_ZIP2 = NEONPermFixed | 0x00007000;

// NEON scalar instructions with two register operands.
using NEONScalar2RegMiscOp = uint32_t;
constexpr NEONScalar2RegMiscOp NEONScalar2RegMiscFixed = 0x5E200800;
constexpr NEONScalar2RegMiscOp NEONScalar2RegMiscFMask = 0xDF3E0C00;
constexpr NEONScalar2RegMiscOp NEONScalar2RegMiscMask =
    NEON_Q | NEONScalar | NEON2RegMiscMask;
constexpr NEONScalar2RegMiscOp NEON_CMGT_zero_scalar =
    NEON_Q | NEONScalar | NEON_CMGT_zero;
constexpr NEONScalar2RegMiscOp NEON_CMEQ_zero_scalar =
    NEON_Q | NEONScalar | NEON_CMEQ_zero;
constexpr NEONScalar2RegMiscOp NEON_CMLT_zero_scalar =
    NEON_Q | NEONScalar | NEON_CMLT_zero;
constexpr NEONScalar2RegMiscOp NEON_CMGE_zero_scalar =
    NEON_Q | NEONScalar | NEON_CMGE_zero;
constexpr NEONScalar2RegMiscOp NEON_CMLE_zero_scalar =
    NEON_Q | NEONScalar | NEON_CMLE_zero;
constexpr NEONScalar2RegMiscOp NEON_ABS_scalar = NEON_Q | NEONScalar | NEON_ABS;
constexpr NEONScalar2RegMiscOp NEON_SQABS_scalar =
    NEON_Q | NEONScalar | NEON_SQABS;
constexpr NEONScalar2RegMiscOp NEON_NEG_scalar = NEON_Q | NEONScalar | NEON_NEG;
constexpr NEONScalar2RegMiscOp NEON_SQNEG_scalar =
    NEON_Q | NEONScalar | NEON_SQNEG;
constexpr NEONScalar2RegMiscOp NEON_SQXTN_scalar =
    NEON_Q | NEONScalar | NEON_SQXTN;
constexpr NEONScalar2RegMiscOp NEON_UQXTN_scalar =
    NEON_Q | NEONScalar | NEON_UQXTN;
constexpr NEONScalar2RegMiscOp NEON_SQXTUN_scalar =
    NEON_Q | NEONScalar | NEON_SQXTUN;
constexpr NEONScalar2RegMiscOp NEON_SUQADD_scalar =
    NEON_Q | NEONScalar | NEON_SUQADD;
constexpr NEONScalar2RegMiscOp NEON_USQADD_scalar =
    NEON_Q | NEONScalar | NEON_USQADD;

constexpr NEONScalar2RegMiscOp NEONScalar2RegMiscOpcode = NEON2RegMiscOpcode;
constexpr NEONScalar2RegMiscOp NEON_NEG_scalar_opcode =
    NEON_NEG_scalar & NEONScalar2RegMiscOpcode;

constexpr NEONScalar2RegMiscOp NEONScalar2RegMiscFPMask =
    NEONScalar2RegMiscMask | 0x00800000;
constexpr NEONScalar2RegMiscOp NEON_FRSQRTE_scalar =
    NEON_Q | NEONScalar | NEON_FRSQRTE;
constexpr NEONScalar2RegMiscOp NEON_FRECPE_scalar =
    NEON_Q | NEONScalar | NEON_FRECPE;
constexpr NEONScalar2RegMiscOp NEON_SCVTF_scalar =
    NEON_Q | NEONScalar | NEON_SCVTF;
constexpr NEONScalar2RegMiscOp NEON_UCVTF_scalar =
    NEON_Q | NEONScalar | NEON_UCVTF;
constexpr NEONScalar2RegMiscOp NEON_FCMGT_zero_scalar =
    NEON_Q | NEONScalar | NEON_FCMGT_zero;
constexpr NEONScalar2RegMiscOp NEON_FCMEQ_zero_scalar =
    NEON_Q | NEONScalar | NEON_FCMEQ_zero;
constexpr NEONScalar2RegMiscOp NEON_FCMLT_zero_scalar =
    NEON_Q | NEONScalar | NEON_FCMLT_zero;
constexpr NEONScalar2RegMiscOp NEON_FCMGE_zero_scalar =
    NEON_Q | NEONScalar | NEON_FCMGE_zero;
constexpr NEONScalar2RegMiscOp NEON_FCMLE_zero_scalar =
    NEON_Q | NEONScalar | NEON_FCMLE_zero;
constexpr NEONScalar2RegMiscOp NEON_FRECPX_scalar =
    NEONScalar2RegMiscFixed | 0x0081F000;
constexpr NEONScalar2RegMiscOp NEON_FCVTNS_scalar =
    NEON_Q | NEONScalar | NEON_FCVTNS;
constexpr NEONScalar2RegMiscOp NEON_FCVTNU_scalar =
    NEON_Q | NEONScalar | NEON_FCVTNU;
constexpr NEONScalar2RegMiscOp NEON_FCVTPS_scalar =
    NEON_Q | NEONScalar | NEON_FCVTPS;
constexpr NEONScalar2RegMiscOp NEON_FCVTPU_scalar =
    NEON_Q | NEONScalar | NEON_FCVTPU;
constexpr NEONScalar2RegMiscOp NEON_FCVTMS_scalar =
    NEON_Q | NEONScalar | NEON_FCVTMS;
constexpr NEONScalar2RegMiscOp NEON_FCVTMU_scalar =
    NEON_Q | NEONScalar | NEON_FCVTMU;
constexpr NEONScalar2RegMiscOp NEON_FCVTZS_scalar =
    NEON_Q | NEONScalar | NEON_FCVTZS;
constexpr NEONScalar2RegMiscOp NEON_FCVTZU_scalar =
    NEON_Q | NEONScalar | NEON_FCVTZU;
constexpr NEONScalar2RegMiscOp NEON_FCVTAS_scalar =
    NEON_Q | NEONScalar | NEON_FCVTAS;
constexpr NEONScalar2RegMiscOp NEON_FCVTAU_scalar =
    NEON_Q | NEONScalar | NEON_FCVTAU;
constexpr NEONScalar2RegMiscOp NEON_FCVTXN_scalar =
    NEON_Q | NEONScalar | NEON_FCVTXN;

// NEON scalar instructions with three same-type operands.
using NEONScalar3SameOp = uint32_t;
constexpr NEONScalar3SameOp NEONScalar3SameFixed = 0x5E200400;
constexpr NEONScalar3SameOp NEONScalar3SameFMask = 0xDF200400;
constexpr NEONScalar3SameOp NEONScalar3SameMask = 0xFF20FC00;
constexpr NEONScalar3SameOp NEON_ADD_scalar = NEON_Q | NEONScalar | NEON_ADD;
constexpr NEONScalar3SameOp NEON_CMEQ_scalar = NEON_Q | NEONScalar | NEON_CMEQ;
constexpr NEONScalar3SameOp NEON_CMGE_scalar = NEON_Q | NEONScalar | NEON_CMGE;
constexpr NEONScalar3SameOp NEON_CMGT_scalar = NEON_Q | NEONScalar | NEON_CMGT;
constexpr NEONScalar3SameOp NEON_CMHI_scalar = NEON_Q | NEONScalar | NEON_CMHI;
constexpr NEONScalar3SameOp NEON_CMHS_scalar = NEON_Q | NEONScalar | NEON_CMHS;
constexpr NEONScalar3SameOp NEON_CMTST_scalar =
    NEON_Q | NEONScalar | NEON_CMTST;
constexpr NEONScalar3SameOp NEON_SUB_scalar = NEON_Q | NEONScalar | NEON_SUB;
constexpr NEONScalar3SameOp NEON_UQADD_scalar =
    NEON_Q | NEONScalar | NEON_UQADD;
constexpr NEONScalar3SameOp NEON_SQADD_scalar =
    NEON_Q | NEONScalar | NEON_SQADD;
constexpr NEONScalar3SameOp NEON_UQSUB_scalar =
    NEON_Q | NEONScalar | NEON_UQSUB;
constexpr NEONScalar3SameOp NEON_SQSUB_scalar =
    NEON_Q | NEONScalar | NEON_SQSUB;
constexpr NEONScalar3SameOp NEON_USHL_scalar = NEON_Q | NEONScalar | NEON_USHL;
constexpr NEONScalar3SameOp NEON_SSHL_scalar = NEON_Q | NEONScalar | NEON_SSHL;
constexpr NEONScalar3SameOp NEON_UQSHL_scalar =
    NEON_Q | NEONScalar | NEON_UQSHL;
constexpr NEONScalar3SameOp NEON_SQSHL_scalar =
    NEON_Q | NEONScalar | NEON_SQSHL;
constexpr NEONScalar3SameOp NEON_URSHL_scalar =
    NEON_Q | NEONScalar | NEON_URSHL;
constexpr NEONScalar3SameOp NEON_SRSHL_scalar =
    NEON_Q | NEONScalar | NEON_SRSHL;
constexpr NEONScalar3SameOp NEON_UQRSHL_scalar =
    NEON_Q | NEONScalar | NEON_UQRSHL;
constexpr NEONScalar3SameOp NEON_SQRSHL_scalar =
    NEON_Q | NEONScalar | NEON_SQRSHL;
constexpr NEONScalar3SameOp NEON_SQDMULH_scalar =
    NEON_Q | NEONScalar | NEON_SQDMULH;
constexpr NEONScalar3SameOp NEON_SQRDMULH_scalar =
    NEON_Q | NEONScalar | NEON_SQRDMULH;

// NEON floating point scalar instructions with three same-type operands.
constexpr NEONScalar3SameOp NEONScalar3SameFPFixed =
    NEONScalar3SameFixed | 0x0000C000;
constexpr NEONScalar3SameOp NEONScalar3SameFPFMask =
    NEONScalar3SameFMask | 0x0000C000;
constexpr NEONScalar3SameOp NEONScalar3SameFPMask =
    NEONScalar3SameMask | 0x00800000;
constexpr NEONScalar3SameOp NEON_FACGE_scalar =
    NEON_Q | NEONScalar | NEON_FACGE;
constexpr NEONScalar3SameOp NEON_FACGT_scalar =
    NEON_Q | NEONScalar | NEON_FACGT;
constexpr NEONScalar3SameOp NEON_FCMEQ_scalar =
    NEON_Q | NEONScalar | NEON_FCMEQ;
constexpr NEONScalar3SameOp NEON_FCMGE_scalar =
    NEON_Q | NEONScalar | NEON_FCMGE;
constexpr NEONScalar3SameOp NEON_FCMGT_scalar =
    NEON_Q | NEONScalar | NEON_FCMGT;
constexpr NEONScalar3SameOp NEON_FMULX_scalar =
    NEON_Q | NEONScalar | NEON_FMULX;
constexpr NEONScalar3SameOp NEON_FRECPS_scalar =
    NEON_Q | NEONScalar | NEON_FRECPS;
constexpr NEONScalar3SameOp NEON_FRSQRTS_scalar =
    NEON_Q | NEONScalar | NEON_FRSQRTS;
constexpr NEONScalar3SameOp NEON_FABD_scalar = NEON_Q | NEONScalar | NEON_FABD;

// NEON scalar instructions with three different-type operands.
using NEONScalar3DiffOp = uint32_t;
constexpr NEONScalar3DiffOp NEONScalar3DiffFixed = 0x5E200000;
constexpr NEONScalar3DiffOp NEONScalar3DiffFMask = 0xDF200C00;
constexpr NEONScalar3DiffOp NEONScalar3DiffMask =
    NEON_Q | NEONScalar | NEON3DifferentMask;
constexpr NEONScalar3DiffOp NEON_SQDMLAL_scalar =
    NEON_Q | NEONScalar | NEON_SQDMLAL;
constexpr NEONScalar3DiffOp NEON_SQDMLSL_scalar =
    NEON_Q | NEONScalar | NEON_SQDMLSL;
constexpr NEONScalar3DiffOp NEON_SQDMULL_scalar =
    NEON_Q | NEONScalar | NEON_SQDMULL;

// Unimplemented and unallocated instructions. These are defined to make fixed
// bit assertion easier.
using UnimplementedOp = uint32_t;
constexpr UnimplementedOp UnimplementedFixed = 0x00000000;
constexpr UnimplementedOp UnimplementedFMask = 0x00000000;

using UnallocatedOp = uint32_t;
constexpr UnallocatedOp UnallocatedFixed = 0x00000000;
constexpr UnallocatedOp UnallocatedFMask = 0x00000000;

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM64_CONSTANTS_ARM64_H_
