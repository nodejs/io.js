// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_CODEGEN_RISCV_BASE_CONSTANTS_RISCV_H_
#define V8_CODEGEN_RISCV_BASE_CONSTANTS_RISCV_H_

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"

#ifdef DEBUG
#define UNIMPLEMENTED_RISCV()                                               \
  v8::internal::PrintF("%s, \tline %d: \tfunction %s  not implemented. \n", \
                       __FILE__, __LINE__, __func__);
#else
#define UNIMPLEMENTED_RISCV()
#endif

#define UNSUPPORTED_RISCV()                                        \
  v8::internal::PrintF("Unsupported instruction %d.\n", __LINE__); \
  UNIMPLEMENTED();

enum Endianness { kLittle, kBig };

#if defined(V8_TARGET_LITTLE_ENDIAN)
static const Endianness kArchEndian = kLittle;
#elif defined(V8_TARGET_BIG_ENDIAN)
static const Endianness kArchEndian = kBig;
#else
#error Unknown endianness
#endif

#if defined(V8_TARGET_LITTLE_ENDIAN)
const uint32_t kLeastSignificantByteInInt32Offset = 0;
const uint32_t kLessSignificantWordInDoublewordOffset = 0;
#elif defined(V8_TARGET_BIG_ENDIAN)
const uint32_t kLeastSignificantByteInInt32Offset = 3;
const uint32_t kLessSignificantWordInDoublewordOffset = 4;
#else
#error Unknown endianness
#endif

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

// Defines constants and accessor classes to assemble, disassemble and
// simulate RISC-V instructions.
//
// See: The RISC-V Instruction Set Manual
//      Volume I: User-Level ISA
// Try https://content.riscv.org/wp-content/uploads/2017/05/riscv-spec-v2.2.pdf.
namespace v8 {
namespace internal {
using Opcode = uint32_t;

// Actual value of root register is offset from the root array's start
// to take advantage of negative displacement values.
constexpr int kRootRegisterBias = 256;

#define RVV_LMUL(V) \
  V(m1)             \
  V(m2)             \
  V(m4)             \
  V(m8)             \
  V(RESERVERD)      \
  V(mf8)            \
  V(mf4)            \
  V(mf2)

enum Vlmul {
#define DEFINE_FLAG(name) name,
  RVV_LMUL(DEFINE_FLAG)
#undef DEFINE_FLAG
      kVlInvalid
};

#define RVV_SEW(V) \
  V(E8)            \
  V(E16)           \
  V(E32)           \
  V(E64)

#define DEFINE_FLAG(name) name,
enum VSew {
  RVV_SEW(DEFINE_FLAG)
#undef DEFINE_FLAG
      kVsInvalid
};

// RISC-V can perform PC-relative jumps within a 32-bit range using the
// following two instructions:
//   auipc   t6, imm20    ; t0 = PC + imm20 * 2^12
//   jalr    ra, t6, imm12; ra = PC + 4, PC = t0 + imm12,
// Both imm20 and imm12 are treated as two's-complement signed values, usually
// calculated as:
//   imm20 = (offset + 0x800) >> 12
//   imm12 = offset & 0xfff
// offset is the signed offset from the auipc instruction. Adding 0x800 handles
// the offset, but if the offset is >= 2^31 - 2^11, it will overflow. Therefore,
// the true 32-bit range is:
//   [-2^31 - 2^11, 2^31 - 2^11)
constexpr size_t kMaxPCRelativeCodeRangeInMB = 2047;

// -----------------------------------------------------------------------------
// Registers and FPURegisters.

// Number of general purpose registers.
const int kNumRegisters = 32;
const int kInvalidRegister = -1;

// Number of registers with pc.
const int kNumSimuRegisters = 33;

// In the simulator, the PC register is simulated as the 34th register.
const int kPCRegister = 34;

// Number coprocessor registers.
const int kNumFPURegisters = 32;
const int kInvalidFPURegister = -1;

// Number vectotr registers
const int kNumVRegisters = 32;
const int kInvalidVRegister = -1;
// 'pref' instruction hints
const int32_t kPrefHintLoad = 0;
const int32_t kPrefHintStore = 1;
const int32_t kPrefHintLoadStreamed = 4;
const int32_t kPrefHintStoreStreamed = 5;
const int32_t kPrefHintLoadRetained = 6;
const int32_t kPrefHintStoreRetained = 7;
const int32_t kPrefHintWritebackInvalidate = 25;
const int32_t kPrefHintPrepareForStore = 30;

// Helper functions for converting between register numbers and names.
class Registers {
 public:
  // Return the name of the register.
  static const char* Name(int reg);

  // Lookup the register number for the name provided.
  static int Number(const char* name);

  struct RegisterAlias {
    int reg;
    const char* name;
  };

 private:
  static const char* names_[kNumSimuRegisters];
  static const RegisterAlias aliases_[];
};

// Helper functions for converting between register numbers and names.
class FPURegisters {
 public:
  // Return the name of the register.
  static const char* Name(int reg);

  // Lookup the register number for the name provided.
  static int Number(const char* name);

  struct RegisterAlias {
    int creg;
    const char* name;
  };

 private:
  static const char* names_[kNumFPURegisters];
  static const RegisterAlias aliases_[];
};

class VRegisters {
 public:
  // Return the name of the register.
  static const char* Name(int reg);

  // Lookup the register number for the name provided.
  static int Number(const char* name);

  struct RegisterAlias {
    int creg;
    const char* name;
  };

 private:
  static const char* names_[kNumVRegisters];
  static const RegisterAlias aliases_[];
};

// -----------------------------------------------------------------------------
// Instructions encoding constants.

// On RISCV all instructions are 32 bits, except for RVC.
using Instr = int32_t;
using ShortInstr = int16_t;

// Special Software Interrupt codes when used in the presence of the RISC-V
// simulator.
enum SoftwareInterruptCodes {
  // Transition to C code.
  call_rt_redirected = 0xfffff
};

// On RISC-V Simulator breakpoints can have different codes:
// - Breaks between 0 and kMaxWatchpointCode are treated as simple watchpoints,
//   the simulator will run through them and print the registers.
// - Breaks between kMaxWatchpointCode and kMaxStopCode are treated as stop()
//   instructions (see Assembler::stop()).
// - Breaks larger than kMaxStopCode are simple breaks, dropping you into the
//   debugger.
const uint32_t kMaxTracepointCode = 63;
const uint32_t kMaxWatchpointCode = 31;
const uint32_t kMaxStopCode = 127;
static_assert(kMaxWatchpointCode < kMaxStopCode);
static_assert(kMaxTracepointCode < kMaxStopCode);

// Debug parameters.
//
// For example:
//
// __ Debug(TRACE_ENABLE | LOG_TRACE);
// starts tracing: set v8_flags.trace-sim is true.
// __ Debug(TRACE_ENABLE | LOG_REGS);
// PrintAllregs.
// __ Debug(TRACE_DISABLE | LOG_TRACE);
// stops tracing: set v8_flags.trace-sim is false.
const unsigned kDebuggerTracingDirectivesMask = 0b111 << 3;
enum DebugParameters : uint32_t {
  NO_PARAM = 1 << 5,
  BREAK = 1 << 0,
  LOG_TRACE = 1 << 1,
  LOG_REGS = 1 << 2,
  LOG_ALL = LOG_TRACE,
  // Trace control.
  TRACE_ENABLE = 1 << 3 | NO_PARAM,
  TRACE_DISABLE = 1 << 4 | NO_PARAM,
};

// ----- Fields offset and length.
// RISCV constants
const int kBaseOpcodeShift = 0;
const int kBaseOpcodeBits = 7;
const int kFunct6Shift = 26;
const int kFunct6Bits = 6;
const int kFunct7Shift = 25;
const int kFunct7Bits = 7;
const int kFunct5Shift = 27;
const int kFunct5Bits = 5;
const int kFunct3Shift = 12;
const int kFunct3Bits = 3;
const int kFunct2Shift = 25;
const int kFunct2Bits = 2;
const int kRs1Shift = 15;
const int kRs1Bits = 5;
const int kVs1Shift = 15;
const int kVs1Bits = 5;
const int kVs2Shift = 20;
const int kVs2Bits = 5;
const int kVdShift = 7;
const int kVdBits = 5;
const int kRs2Shift = 20;
const int kRs2Bits = 5;
const int kRs3Shift = 27;
const int kRs3Bits = 5;
const int kRdShift = 7;
const int kRdBits = 5;
const int kRlShift = 25;
const int kAqShift = 26;
const int kImm12Shift = 20;
const int kImm12Bits = 12;
const int kImm11Shift = 2;
const int kImm11Bits = 11;
const int kShamtShift = 20;
const int kShamtBits = 5;
const uint32_t kShamtMask = (((1 << kShamtBits) - 1) << kShamtShift);
const int kShamtWShift = 20;
// FIXME: remove this once we have a proper way to handle the wide shift amount
const int kShamtWBits = 6;
const int kArithShiftShift = 30;
const int kImm20Shift = 12;
const int kImm20Bits = 20;
const int kCsrShift = 20;
const int kCsrBits = 12;
const int kMemOrderBits = 4;
const int kPredOrderShift = 24;
const int kSuccOrderShift = 20;

// for C extension
const int kRvcFunct4Shift = 12;
const int kRvcFunct4Bits = 4;
const int kRvcFunct3Shift = 13;
const int kRvcFunct3Bits = 3;
const int kRvcRs1Shift = 7;
const int kRvcRs1Bits = 5;
const int kRvcRs2Shift = 2;
const int kRvcRs2Bits = 5;
const int kRvcRdShift = 7;
const int kRvcRdBits = 5;
const int kRvcRs1sShift = 7;
const int kRvcRs1sBits = 3;
const int kRvcRs2sShift = 2;
const int kRvcRs2sBits = 3;
const int kRvcFunct2Shift = 5;
const int kRvcFunct2BShift = 10;
const int kRvcFunct2Bits = 2;
const int kRvcFunct6Shift = 10;
const int kRvcFunct6Bits = 6;

const uint32_t kRvcOpcodeMask =
    0b11 | (((1 << kRvcFunct3Bits) - 1) << kRvcFunct3Shift);
const uint32_t kRvcFunct3Mask =
    (((1 << kRvcFunct3Bits) - 1) << kRvcFunct3Shift);
const uint32_t kRvcFunct4Mask =
    (((1 << kRvcFunct4Bits) - 1) << kRvcFunct4Shift);
const uint32_t kRvcFunct6Mask =
    (((1 << kRvcFunct6Bits) - 1) << kRvcFunct6Shift);
const uint32_t kRvcFunct2Mask =
    (((1 << kRvcFunct2Bits) - 1) << kRvcFunct2Shift);
const uint32_t kRvcFunct2BMask =
    (((1 << kRvcFunct2Bits) - 1) << kRvcFunct2BShift);
const uint32_t kCRTypeMask = kRvcOpcodeMask | kRvcFunct4Mask;
const uint32_t kCSTypeMask = kRvcOpcodeMask | kRvcFunct6Mask;
const uint32_t kCATypeMask = kRvcOpcodeMask | kRvcFunct6Mask | kRvcFunct2Mask;
const uint32_t kRvcBImm8Mask = (((1 << 5) - 1) << 2) | (((1 << 3) - 1) << 10);

// for RVV extension
constexpr int kRvvELEN = 64;
#ifdef RVV_VLEN
constexpr int kRvvVLEN = RVV_VLEN;
// TODO(riscv): support rvv 256/512/1024
static_assert(
    kRvvVLEN == 128,
    "RVV extension only supports 128bit wide VLEN at current RISC-V backend.");
#else
constexpr int kRvvVLEN = 128;
#endif
constexpr int kRvvSLEN = kRvvVLEN;

const int kRvvFunct6Shift = 26;
const int kRvvFunct6Bits = 6;
const uint32_t kRvvFunct6Mask =
    (((1 << kRvvFunct6Bits) - 1) << kRvvFunct6Shift);

const int kRvvVmBits = 1;
const int kRvvVmShift = 25;
const uint32_t kRvvVmMask = (((1 << kRvvVmBits) - 1) << kRvvVmShift);

const int kRvvVs2Bits = 5;
const int kRvvVs2Shift = 20;
const uint32_t kRvvVs2Mask = (((1 << kRvvVs2Bits) - 1) << kRvvVs2Shift);

const int kRvvVs1Bits = 5;
const int kRvvVs1Shift = 15;
const uint32_t kRvvVs1Mask = (((1 << kRvvVs1Bits) - 1) << kRvvVs1Shift);

const int kRvvRs1Bits = kRvvVs1Bits;
const int kRvvRs1Shift = kRvvVs1Shift;
const uint32_t kRvvRs1Mask = (((1 << kRvvRs1Bits) - 1) << kRvvRs1Shift);

const int kRvvRs2Bits = 5;
const int kRvvRs2Shift = 20;
const uint32_t kRvvRs2Mask = (((1 << kRvvRs2Bits) - 1) << kRvvRs2Shift);

const int kRvvImm5Bits = kRvvVs1Bits;
const int kRvvImm5Shift = kRvvVs1Shift;
const uint32_t kRvvImm5Mask = (((1 << kRvvImm5Bits) - 1) << kRvvImm5Shift);

const int kRvvVdBits = 5;
const int kRvvVdShift = 7;
const uint32_t kRvvVdMask = (((1 << kRvvVdBits) - 1) << kRvvVdShift);

const int kRvvRdBits = kRvvVdBits;
const int kRvvRdShift = kRvvVdShift;
const uint32_t kRvvRdMask = (((1 << kRvvRdBits) - 1) << kRvvRdShift);

const int kRvvZimmBits = 11;
const int kRvvZimmShift = 20;
const uint32_t kRvvZimmMask = (((1 << kRvvZimmBits) - 1) << kRvvZimmShift);

const int kRvvUimmShift = kRvvRs1Shift;
const int kRvvUimmBits = kRvvRs1Bits;
const uint32_t kRvvUimmMask = (((1 << kRvvUimmBits) - 1) << kRvvUimmShift);

const int kRvvWidthBits = 3;
const int kRvvWidthShift = 12;
const uint32_t kRvvWidthMask = (((1 << kRvvWidthBits) - 1) << kRvvWidthShift);

const int kRvvMopBits = 2;
const int kRvvMopShift = 26;
const uint32_t kRvvMopMask = (((1 << kRvvMopBits) - 1) << kRvvMopShift);

const int kRvvMewBits = 1;
const int kRvvMewShift = 28;
const uint32_t kRvvMewMask = (((1 << kRvvMewBits) - 1) << kRvvMewShift);

const int kRvvNfBits = 3;
const int kRvvNfShift = 29;
const uint32_t kRvvNfMask = (((1 << kRvvNfBits) - 1) << kRvvNfShift);

// RISCV Instruction bit masks
const uint32_t kBaseOpcodeMask = ((1 << kBaseOpcodeBits) - 1)
                                 << kBaseOpcodeShift;
const uint32_t kFunct3Mask = ((1 << kFunct3Bits) - 1) << kFunct3Shift;
const uint32_t kFunct5Mask = ((1 << kFunct5Bits) - 1) << kFunct5Shift;
const uint32_t kFunct6Mask = ((1 << kFunct6Bits) - 1) << kFunct6Shift;
const uint32_t kFunct7Mask = ((1 << kFunct7Bits) - 1) << kFunct7Shift;
const uint32_t kFunct2Mask = 0b11 << kFunct7Shift;
const uint32_t kRTypeMask = kBaseOpcodeMask | kFunct3Mask | kFunct7Mask;
const uint32_t kRATypeMask = kBaseOpcodeMask | kFunct3Mask | kFunct5Mask;
const uint32_t kRFPTypeMask = kBaseOpcodeMask | kFunct7Mask;
const uint32_t kR4TypeMask = kBaseOpcodeMask | kFunct3Mask | kFunct2Mask;
const uint32_t kITypeMask = kBaseOpcodeMask | kFunct3Mask;
const uint32_t kSTypeMask = kBaseOpcodeMask | kFunct3Mask;
const uint32_t kBTypeMask = kBaseOpcodeMask | kFunct3Mask;
const uint32_t kUTypeMask = kBaseOpcodeMask;
const uint32_t kJTypeMask = kBaseOpcodeMask;
const uint32_t kVTypeMask = kRvvFunct6Mask | kFunct3Mask | kBaseOpcodeMask;
const uint32_t kRs1FieldMask = ((1 << kRs1Bits) - 1) << kRs1Shift;
const uint32_t kRs2FieldMask = ((1 << kRs2Bits) - 1) << kRs2Shift;
const uint32_t kRs3FieldMask = ((1 << kRs3Bits) - 1) << kRs3Shift;
const uint32_t kRdFieldMask = ((1 << kRdBits) - 1) << kRdShift;
const uint32_t kBImm12Mask = kFunct7Mask | kRdFieldMask;
const uint32_t kImm20Mask = ((1 << kImm20Bits) - 1) << kImm20Shift;
const uint32_t kImm12Mask = ((1 << kImm12Bits) - 1) << kImm12Shift;
const uint32_t kImm11Mask = ((1 << kImm11Bits) - 1) << kImm11Shift;
const uint32_t kImm31_12Mask = ((1 << 20) - 1) << 12;
const uint32_t kImm19_0Mask = ((1 << 20) - 1);

const int kNopByte = 0x00000013;
// Original MIPS constants
const int kImm16Shift = 0;
const int kImm16Bits = 16;
const uint32_t kImm16Mask = ((1 << kImm16Bits) - 1) << kImm16Shift;

// ----- Emulated conditions.
// On RISC-V we use this enum to abstract from conditional branch instructions.
// The 'U' prefix is used to specify unsigned comparisons.
// Opposite conditions must be paired as odd/even numbers
// because 'NegateCondition' function flips LSB to negate condition.
enum Condition : int {  // Any value < 0 is considered no_condition.
  overflow = 0,
  no_overflow = 1,
  Uless = 2,
  Ugreater_equal = 3,
  Uless_equal = 4,
  Ugreater = 5,
  equal = 6,
  not_equal = 7,  // Unordered or Not Equal.
  less = 8,
  greater_equal = 9,
  less_equal = 10,
  greater = 11,
  cc_always = 12,

  // Aliases.
  eq = equal,
  ne = not_equal,
  ge = greater_equal,
  lt = less,
  gt = greater,
  le = less_equal,
  al = cc_always,
  ult = Uless,
  uge = Ugreater_equal,
  ule = Uless_equal,
  ugt = Ugreater,

  // Unified cross-platform condition names/aliases.
  kEqual = equal,
  kNotEqual = not_equal,
  kLessThan = less,
  kGreaterThan = greater,
  kLessThanEqual = less_equal,
  kGreaterThanEqual = greater_equal,
  kUnsignedLessThan = Uless,
  kUnsignedGreaterThan = Ugreater,
  kUnsignedLessThanEqual = Uless_equal,
  kUnsignedGreaterThanEqual = Ugreater_equal,
  kOverflow = overflow,
  kNoOverflow = no_overflow,
  kZero = equal,
  kNotZero = not_equal,
};

// Returns the equivalent of !cc.
inline Condition NegateCondition(Condition cc) {
  DCHECK(cc != cc_always);
  return static_cast<Condition>(cc ^ 1);
}

inline Condition NegateFpuCondition(Condition cc) {
  DCHECK(cc != cc_always);
  switch (cc) {
    case ult:
      return ge;
    case ugt:
      return le;
    case uge:
      return lt;
    case ule:
      return gt;
    case lt:
      return uge;
    case gt:
      return ule;
    case ge:
      return ult;
    case le:
      return ugt;
    case eq:
      return ne;
    case ne:
      return eq;
    default:
      return cc;
  }
}

// ----- Coprocessor conditions.
enum FPUCondition {
  kNoFPUCondition = -1,
  EQ = 0x02,  // Ordered and Equal
  NE = 0x03,  // Unordered or Not Equal
  LT = 0x04,  // Ordered and Less Than
  GE = 0x05,  // Ordered and Greater Than or Equal
  LE = 0x06,  // Ordered and Less Than or Equal
  GT = 0x07,  // Ordered and Greater Than
};

enum CheckForInexactConversion {
  kCheckForInexactConversion,
  kDontCheckForInexactConversion
};

enum class MaxMinKind : int { kMin = 0, kMax = 1 };

// ----------------------------------------------------------------------------
// RISCV flags

enum ControlStatusReg {
  csr_fflags = 0x001,   // Floating-Point Accrued Exceptions (RW)
  csr_frm = 0x002,      // Floating-Point Dynamic Rounding Mode (RW)
  csr_fcsr = 0x003,     // Floating-Point Control and Status Register (RW)
  csr_cycle = 0xc00,    // Cycle counter for RDCYCLE instruction (RO)
  csr_time = 0xc01,     // Timer for RDTIME instruction (RO)
  csr_instret = 0xc02,  // Insns-retired counter for RDINSTRET instruction (RO)
  csr_cycleh = 0xc80,   // Upper 32 bits of cycle, RV32I only (RO)
  csr_timeh = 0xc81,    // Upper 32 bits of time, RV32I only (RO)
  csr_instreth = 0xc82  // Upper 32 bits of instret, RV32I only (RO)
};

enum FFlagsMask {
  kInvalidOperation = 0b10000,  // NV: Invalid
  kDivideByZero = 0b1000,       // DZ:  Divide by Zero
  kFPUOverflow = 0b100,         // OF: Overflow
  kUnderflow = 0b10,            // UF: Underflow
  kInexact = 0b1                // NX:  Inexact
};

enum FPURoundingMode {
  RNE = 0b000,  // Round to Nearest, ties to Even
  RTZ = 0b001,  // Round towards Zero
  RDN = 0b010,  // Round Down (towards -infinity)
  RUP = 0b011,  // Round Up (towards +infinity)
  RMM = 0b100,  // Round to Nearest, tiest to Max Magnitude
  DYN = 0b111   // In instruction's rm field, selects dynamic rounding mode;
                // In Rounding Mode register, Invalid
};

enum MemoryOdering {
  PSI = 0b1000,  // PI or SI
  PSO = 0b0100,  // PO or SO
  PSR = 0b0010,  // PR or SR
  PSW = 0b0001,  // PW or SW
  PSIORW = PSI | PSO | PSR | PSW
};

const int kFloat32ExponentBias = 127;
const int kFloat32MantissaBits = 23;
const int kFloat32ExponentBits = 8;
const int kFloat64ExponentBias = 1023;
const int kFloat64MantissaBits = 52;
const int kFloat64ExponentBits = 11;

enum FClassFlag {
  kNegativeInfinity = 1,
  kNegativeNormalNumber = 1 << 1,
  kNegativeSubnormalNumber = 1 << 2,
  kNegativeZero = 1 << 3,
  kPositiveZero = 1 << 4,
  kPositiveSubnormalNumber = 1 << 5,
  kPositiveNormalNumber = 1 << 6,
  kPositiveInfinity = 1 << 7,
  kSignalingNaN = 1 << 8,
  kQuietNaN = 1 << 9
};

enum TailAgnosticType {
  ta = 0x1,  // Tail agnostic
  tu = 0x0,  // Tail undisturbed
};

enum MaskAgnosticType {
  ma = 0x1,  // Mask agnostic
  mu = 0x0,  // Mask undisturbed
};
enum MaskType {
  Mask = 0x0,  // use the mask
  NoMask = 0x1,
};

// -----------------------------------------------------------------------------
// Hints.

// Branch hints are not used on RISC-V.  They are defined so that they can
// appear in shared function signatures, but will be ignored in RISC-V
// implementations.
enum Hint { no_hint = 0 };

inline Hint NegateHint(Hint hint) { return no_hint; }

enum BaseOpcode : uint32_t {
  LOAD = 0b0000011,      // I form: LB LH LW LBU LHU
  LOAD_FP = 0b0000111,   // I form: FLW FLD FLQ
  MISC_MEM = 0b0001111,  // I special form: FENCE FENCE.I
  OP_IMM = 0b0010011,    // I form: ADDI SLTI SLTIU XORI ORI ANDI SLLI SRLI SRAI
  // Note: SLLI/SRLI/SRAI I form first, then func3 001/101 => R type
  AUIPC = 0b0010111,      // U form: AUIPC
  OP_IMM_32 = 0b0011011,  // I form: ADDIW SLLIW SRLIW SRAIW
  // Note:  SRLIW SRAIW I form first, then func3 101 special shift encoding
  STORE = 0b0100011,     // S form: SB SH SW SD
  STORE_FP = 0b0100111,  // S form: FSW FSD FSQ
  AMO = 0b0101111,       // R form: All A instructions
  OP = 0b0110011,      // R: ADD SUB SLL SLT SLTU XOR SRL SRA OR AND and 32M set
  LUI = 0b0110111,     // U form: LUI
  OP_32 = 0b0111011,   // R: ADDW SUBW SLLW SRLW SRAW MULW DIVW DIVUW REMW REMUW
  MADD = 0b1000011,    // R4 type: FMADD.S FMADD.D FMADD.Q
  MSUB = 0b1000111,    // R4 type: FMSUB.S FMSUB.D FMSUB.Q
  NMSUB = 0b1001011,   // R4 type: FNMSUB.S FNMSUB.D FNMSUB.Q
  NMADD = 0b1001111,   // R4 type: FNMADD.S FNMADD.D FNMADD.Q
  OP_FP = 0b1010011,   // R type: Q ext
  BRANCH = 0b1100011,  // B form: BEQ BNE, BLT, BGE, BLTU BGEU
  JALR = 0b1100111,    // I form: JALR
  JAL = 0b1101111,     // J form: JAL
  SYSTEM = 0b1110011,  // I form: ECALL EBREAK Zicsr ext
  OP_V = 0b1010111,    // V form: RVV

  // C extension
  C0 = 0b00,
  C1 = 0b01,
  C2 = 0b10,
  FUNCT2_0 = 0b00,
  FUNCT2_1 = 0b01,
  FUNCT2_2 = 0b10,
  FUNCT2_3 = 0b11,
};

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.
// These constants are declared in assembler-riscv64.cc, as they use named
// registers and other constants.

// An Illegal instruction
const Instr kIllegalInstr = 0;  // All other bits are 0s (i.e., ecall)
// An ECALL instruction, used for redirected real time call
const Instr rtCallRedirInstr = SYSTEM;  // All other bits are 0s (i.e., ecall)
// An EBreak instruction, used for debugging and semi-hosting
const Instr kBreakInstr = SYSTEM | 1 << kImm12Shift;  // ebreak

constexpr uint8_t kInstrSize = 4;
constexpr uint8_t kShortInstrSize = 2;
constexpr uint8_t kInstrSizeLog2 = 2;

class InstructionBase {
 public:
  enum {
    // On RISC-V, PC cannot actually be directly accessed. We behave as if PC
    // was always the value of the current instruction being executed.
    kPCReadOffset = 0
  };

  // Instruction type.
  enum Type {
    kRType,
    kR4Type,  // Special R4 for Q extension
    kIType,
    kSType,
    kBType,
    kUType,
    kJType,
    // C extension
    kCRType,
    kCIType,
    kCSSType,
    kCIWType,
    kCLType,
    kCSType,
    kCAType,
    kCBType,
    kCJType,
    // V extension
    kVType,
    kVLType,
    kVSType,
    kVAMOType,
    kVIVVType,
    kVFVVType,
    kVMVVType,
    kVIVIType,
    kVIVXType,
    kVFVFType,
    kVMVXType,
    kVSETType,
    kUnsupported = -1
  };

  inline bool IsIllegalInstruction() const {
    uint16_t FirstHalfWord = *reinterpret_cast<const uint16_t*>(this);
    return FirstHalfWord == 0;
  }

  bool IsShortInstruction() const;

  inline uint8_t InstructionSize() const {
    return (v8_flags.riscv_c_extension && this->IsShortInstruction())
               ? kShortInstrSize
               : kInstrSize;
  }

  // Get the raw instruction bits.
  inline Instr InstructionBits() const {
    if (v8_flags.riscv_c_extension && this->IsShortInstruction()) {
      return 0x0000FFFF & (*reinterpret_cast<const ShortInstr*>(this));
    }
    return *reinterpret_cast<const Instr*>(this);
  }

  // Set the raw instruction bits to value.
  inline void SetInstructionBits(Instr value) {
    *reinterpret_cast<Instr*>(this) = value;
  }

  // Read one particular bit out of the instruction bits.
  inline int Bit(int nr) const { return (InstructionBits() >> nr) & 1; }

  // Read a bit field out of the instruction bits.
  inline int Bits(int hi, int lo) const {
    return (InstructionBits() >> lo) & ((2U << (hi - lo)) - 1);
  }

  // Accessors for the different named fields used in the RISC-V encoding.
  inline BaseOpcode BaseOpcodeValue() const {
    return static_cast<BaseOpcode>(
        Bits(kBaseOpcodeShift + kBaseOpcodeBits - 1, kBaseOpcodeShift));
  }

  // Return the fields at their original place in the instruction encoding.
  inline BaseOpcode BaseOpcodeFieldRaw() const {
    return static_cast<BaseOpcode>(InstructionBits() & kBaseOpcodeMask);
  }

  // Safe to call within R-type instructions
  inline int Funct7FieldRaw() const { return InstructionBits() & kFunct7Mask; }

  // Safe to call within R-type instructions
  inline int Funct6FieldRaw() const { return InstructionBits() & kFunct6Mask; }

  // Safe to call within R-, I-, S-, or B-type instructions
  inline int Funct3FieldRaw() const { return InstructionBits() & kFunct3Mask; }

  // Safe to call within R-, I-, S-, or B-type instructions
  inline int Rs1FieldRawNoAssert() const {
    return InstructionBits() & kRs1FieldMask;
  }

  // Safe to call within R-, S-, or B-type instructions
  inline int Rs2FieldRawNoAssert() const {
    return InstructionBits() & kRs2FieldMask;
  }

  // Safe to call within R4-type instructions
  inline int Rs3FieldRawNoAssert() const {
    return InstructionBits() & kRs3FieldMask;
  }

  inline int32_t ITypeBits() const { return InstructionBits() & kITypeMask; }

  inline int32_t InstructionOpcodeType() const {
    if (IsShortInstruction()) {
      return InstructionBits() & kRvcOpcodeMask;
    } else {
      return InstructionBits() & kBaseOpcodeMask;
    }
  }

  // Get the encoding type of the instruction.
  Type InstructionType() const;

 protected:
  InstructionBase() {}
};

template <class T>
class InstructionGetters : public T {
 public:
  uint32_t OperandFunct3() const {
    return this->InstructionBits() & (kBaseOpcodeMask | kFunct3Mask);
  }
  bool IsLoad();
  bool IsStore();
  inline int BaseOpcode() const {
    return this->InstructionBits() & kBaseOpcodeMask;
  }

  inline int RvcOpcode() const {
    DCHECK(this->IsShortInstruction());
    return this->InstructionBits() & kRvcOpcodeMask;
  }

  inline int Rs1Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kRType ||
           this->InstructionType() == InstructionBase::kR4Type ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kSType ||
           this->InstructionType() == InstructionBase::kBType ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kVType);
    return this->Bits(kRs1Shift + kRs1Bits - 1, kRs1Shift);
  }

  inline int Rs2Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kRType ||
           this->InstructionType() == InstructionBase::kR4Type ||
           this->InstructionType() == InstructionBase::kSType ||
           this->InstructionType() == InstructionBase::kBType ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kVType);
    return this->Bits(kRs2Shift + kRs2Bits - 1, kRs2Shift);
  }

  inline int Rs3Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kR4Type);
    return this->Bits(kRs3Shift + kRs3Bits - 1, kRs3Shift);
  }

  inline int Vs1Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kVType ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kSType);
    return this->Bits(kVs1Shift + kVs1Bits - 1, kVs1Shift);
  }

  inline int Vs2Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kVType ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kSType);
    return this->Bits(kVs2Shift + kVs2Bits - 1, kVs2Shift);
  }

  inline int VdValue() const {
    DCHECK(this->InstructionType() == InstructionBase::kVType ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kSType);
    return this->Bits(kVdShift + kVdBits - 1, kVdShift);
  }

  inline int RdValue() const {
    DCHECK(this->InstructionType() == InstructionBase::kRType ||
           this->InstructionType() == InstructionBase::kR4Type ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kSType ||
           this->InstructionType() == InstructionBase::kUType ||
           this->InstructionType() == InstructionBase::kJType ||
           this->InstructionType() == InstructionBase::kVType);
    return this->Bits(kRdShift + kRdBits - 1, kRdShift);
  }

  inline int RvcRs1Value() const { return this->RvcRdValue(); }

  int RvcRdValue() const;

  int RvcRs2Value() const;

  int RvcRs1sValue() const;

  int RvcRs2sValue() const;

  int Funct7Value() const;

  inline int Funct3Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kRType ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kSType ||
           this->InstructionType() == InstructionBase::kBType);
    return this->Bits(kFunct3Shift + kFunct3Bits - 1, kFunct3Shift);
  }

  inline int Funct5Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kRType &&
           this->BaseOpcode() == OP_FP);
    return this->Bits(kFunct5Shift + kFunct5Bits - 1, kFunct5Shift);
  }

  int RvcFunct6Value() const;

  int RvcFunct4Value() const;

  int RvcFunct3Value() const;

  int RvcFunct2Value() const;

  int RvcFunct2BValue() const;

  inline int CsrValue() const {
    DCHECK(this->InstructionType() == InstructionBase::kIType &&
           this->BaseOpcode() == SYSTEM);
    return (this->Bits(kCsrShift + kCsrBits - 1, kCsrShift));
  }

  inline int RoundMode() const {
    DCHECK((this->InstructionType() == InstructionBase::kRType ||
            this->InstructionType() == InstructionBase::kR4Type) &&
           this->BaseOpcode() == OP_FP);
    return this->Bits(kFunct3Shift + kFunct3Bits - 1, kFunct3Shift);
  }

  inline int MemoryOrder(bool is_pred) const {
    DCHECK((this->InstructionType() == InstructionBase::kIType &&
            this->BaseOpcode() == MISC_MEM));
    if (is_pred) {
      return this->Bits(kPredOrderShift + kMemOrderBits - 1, kPredOrderShift);
    } else {
      return this->Bits(kSuccOrderShift + kMemOrderBits - 1, kSuccOrderShift);
    }
  }

  inline int Imm12Value() const {
    DCHECK(this->InstructionType() == InstructionBase::kIType);
    int Value = this->Bits(kImm12Shift + kImm12Bits - 1, kImm12Shift);
    return Value << 20 >> 20;
  }

  inline int32_t Imm12SExtValue() const {
    int32_t Value = this->Imm12Value() << 20 >> 20;
    return Value;
  }

  inline int BranchOffset() const {
    DCHECK(this->InstructionType() == InstructionBase::kBType);
    // | imm[12|10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode |
    //  31          25                      11          7
    uint32_t Bits = this->InstructionBits();
    int16_t imm13 = ((Bits & 0xf00) >> 7) | ((Bits & 0x7e000000) >> 20) |
                    ((Bits & 0x80) << 4) | ((Bits & 0x80000000) >> 19);
    return imm13 << 19 >> 19;
  }

  inline int StoreOffset() const {
    DCHECK(this->InstructionType() == InstructionBase::kSType);
    // | imm[11:5] | rs2 | rs1 | funct3 | imm[4:0] | opcode |
    //  31       25                      11       7
    uint32_t Bits = this->InstructionBits();
    int16_t imm12 = ((Bits & 0xf80) >> 7) | ((Bits & 0xfe000000) >> 20);
    return imm12 << 20 >> 20;
  }

  inline int Imm20UValue() const {
    DCHECK(this->InstructionType() == InstructionBase::kUType);
    // | imm[31:12] | rd | opcode |
    //  31        12
    int32_t Bits = this->InstructionBits();
    return Bits >> 12;
  }

  inline int Imm20JValue() const {
    DCHECK(this->InstructionType() == InstructionBase::kJType);
    // | imm[20|10:1|11|19:12] | rd | opcode |
    //  31                   12
    uint32_t Bits = this->InstructionBits();
    int32_t imm20 = ((Bits & 0x7fe00000) >> 20) | ((Bits & 0x100000) >> 9) |
                    (Bits & 0xff000) | ((Bits & 0x80000000) >> 11);
    return imm20 << 11 >> 11;
  }

  inline bool IsArithShift() const {
    // Valid only for right shift operations
    DCHECK((this->BaseOpcode() == OP || this->BaseOpcode() == OP_32 ||
            this->BaseOpcode() == OP_IMM || this->BaseOpcode() == OP_IMM_32) &&
           this->Funct3Value() == 0b101);
    return this->InstructionBits() & 0x40000000;
  }

  inline int Shamt() const {
    // Valid only for shift instructions (SLLI, SRLI, SRAI)
    DCHECK(((this->InstructionBits() & kBaseOpcodeMask) == OP_IMM ||
            (this->InstructionBits() & kBaseOpcodeMask) == OP_IMM_32) &&
           (this->Funct3Value() == 0b001 || this->Funct3Value() == 0b101));
    // | 0A0000 | shamt | rs1 | funct3 | rd | opcode |
    //  31       25    20
    return this->Bits(kImm12Shift + 5, kImm12Shift);
  }

  inline int Shamt32() const {
    // Valid only for shift instructions (SLLIW, SRLIW, SRAIW)
    DCHECK((this->InstructionBits() & kBaseOpcodeMask) == OP_IMM_32 &&
           (this->Funct3Value() == 0b001 || this->Funct3Value() == 0b101));
    // | 0A00000 | shamt | rs1 | funct3 | rd | opcode |
    //  31        24   20
    return this->Bits(kImm12Shift + 4, kImm12Shift);
  }

  inline int RvcImm6Value() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | imm[5] | rs1/rd | imm[4:0] | opcode |
    //  15         12              6        2
    uint32_t Bits = this->InstructionBits();
    int32_t imm6 = ((Bits & 0x1000) >> 7) | ((Bits & 0x7c) >> 2);
    return imm6 << 26 >> 26;
  }

  inline int RvcImm6Addi16spValue() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | nzimm[9] | 2 | nzimm[4|6|8:7|5] | opcode |
    //  15         12           6                2
    uint32_t Bits = this->InstructionBits();
    int32_t imm10 = ((Bits & 0x1000) >> 3) | ((Bits & 0x40) >> 2) |
                    ((Bits & 0x20) << 1) | ((Bits & 0x18) << 4) |
                    ((Bits & 0x4) << 3);
    DCHECK_NE(imm10, 0);
    return imm10 << 22 >> 22;
  }

  inline int RvcImm8Addi4spnValue() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | nzimm[11]  | rd' | opcode |
    //  15      13           5     2
    uint32_t Bits = this->InstructionBits();
    int32_t uimm10 = ((Bits & 0x20) >> 2) | ((Bits & 0x40) >> 4) |
                     ((Bits & 0x780) >> 1) | ((Bits & 0x1800) >> 7);
    DCHECK_NE(uimm10, 0);
    return uimm10;
  }

  inline int RvcShamt6() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | nzuimm[5] | rs1/rd | nzuimm[4:0] | opcode |
    //  15         12                 6           2
    int32_t imm6 = this->RvcImm6Value();
    return imm6 & 0x3f;
  }

  inline int RvcImm6LwspValue() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | uimm[5] | rs1 | uimm[4:2|7:6] | opcode |
    //  15         12            6             2
    uint32_t Bits = this->InstructionBits();
    int32_t imm8 =
        ((Bits & 0x1000) >> 7) | ((Bits & 0x70) >> 2) | ((Bits & 0xc) << 4);
    return imm8;
  }

  inline int RvcImm6LdspValue() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | uimm[5] | rs1 | uimm[4:3|8:6] | opcode |
    //  15         12            6             2
    uint32_t Bits = this->InstructionBits();
    int32_t imm9 =
        ((Bits & 0x1000) >> 7) | ((Bits & 0x60) >> 2) | ((Bits & 0x1c) << 4);
    return imm9;
  }

  inline int RvcImm6SwspValue() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | uimm[5:2|7:6] | rs2 | opcode |
    //  15       12            7
    uint32_t Bits = this->InstructionBits();
    int32_t imm8 = ((Bits & 0x1e00) >> 7) | ((Bits & 0x180) >> 1);
    return imm8;
  }

  inline int RvcImm6SdspValue() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | uimm[5:3|8:6] | rs2 | opcode |
    //  15       12            7
    uint32_t Bits = this->InstructionBits();
    int32_t imm9 = ((Bits & 0x1c00) >> 7) | ((Bits & 0x380) >> 1);
    return imm9;
  }

  inline int RvcImm5WValue() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | imm[5:3] | rs1 | imm[2|6] | rd | opcode |
    //  15       12       10     6          4     2
    uint32_t Bits = this->InstructionBits();
    int32_t imm7 =
        ((Bits & 0x1c00) >> 7) | ((Bits & 0x40) >> 4) | ((Bits & 0x20) << 1);
    return imm7;
  }

  inline int RvcImm5DValue() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | imm[5:3] | rs1 | imm[7:6] | rd | opcode |
    //  15       12        10    6          4     2
    uint32_t Bits = this->InstructionBits();
    int32_t imm8 = ((Bits & 0x1c00) >> 7) | ((Bits & 0x60) << 1);
    return imm8;
  }

  inline int RvcImm11CJValue() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | [11|4|9:8|10|6|7|3:1|5] | opcode |
    //  15      12                        2
    uint32_t Bits = this->InstructionBits();
    int32_t imm12 = ((Bits & 0x4) << 3) | ((Bits & 0x38) >> 2) |
                    ((Bits & 0x40) << 1) | ((Bits & 0x80) >> 1) |
                    ((Bits & 0x100) << 2) | ((Bits & 0x600) >> 1) |
                    ((Bits & 0x800) >> 7) | ((Bits & 0x1000) >> 1);
    return imm12 << 20 >> 20;
  }

  inline int RvcImm8BValue() const {
    DCHECK(this->IsShortInstruction());
    // | funct3 | imm[8|4:3] | rs1` | imm[7:6|2:1|5]  | opcode |
    //  15       12        10       7                 2
    uint32_t Bits = this->InstructionBits();
    int32_t imm9 = ((Bits & 0x4) << 3) | ((Bits & 0x18) >> 2) |
                   ((Bits & 0x60) << 1) | ((Bits & 0xc00) >> 7) |
                   ((Bits & 0x1000) >> 4);
    return imm9 << 23 >> 23;
  }

  inline int vl_vs_width() {
    int width = 0;
    if ((this->InstructionBits() & kBaseOpcodeMask) != LOAD_FP &&
        (this->InstructionBits() & kBaseOpcodeMask) != STORE_FP)
      return -1;
    switch (this->InstructionBits() & (kRvvWidthMask | kRvvMewMask)) {
      case 0x0:
        width = 8;
        break;
      case 0x00005000:
        width = 16;
        break;
      case 0x00006000:
        width = 32;
        break;
      case 0x00007000:
        width = 64;
        break;
      case 0x10000000:
        width = 128;
        break;
      case 0x10005000:
        width = 256;
        break;
      case 0x10006000:
        width = 512;
        break;
      case 0x10007000:
        width = 1024;
        break;
      default:
        width = -1;
        break;
    }
    return width;
  }

  uint32_t Rvvzimm() const;

  uint32_t Rvvuimm() const;

  inline uint32_t RvvVsew() const {
    uint32_t zimm = this->Rvvzimm();
    uint32_t vsew = (zimm >> 3) & 0x7;
    return vsew;
  }

  inline uint32_t RvvVlmul() const {
    uint32_t zimm = this->Rvvzimm();
    uint32_t vlmul = zimm & 0x7;
    return vlmul;
  }

  inline uint8_t RvvVM() const {
    DCHECK(this->InstructionType() == InstructionBase::kVType ||
           this->InstructionType() == InstructionBase::kIType ||
           this->InstructionType() == InstructionBase::kSType);
    return this->Bits(kRvvVmShift + kRvvVmBits - 1, kRvvVmShift);
  }

  inline const char* RvvSEW() const {
    uint32_t vsew = this->RvvVsew();
    switch (vsew) {
#define CAST_VSEW(name) \
  case name:            \
    return #name;
      RVV_SEW(CAST_VSEW)
      default:
        return "unknown";
#undef CAST_VSEW
    }
  }

  inline const char* RvvLMUL() const {
    uint32_t vlmul = this->RvvVlmul();
    switch (vlmul) {
#define CAST_VLMUL(name) \
  case name:             \
    return #name;
      RVV_LMUL(CAST_VLMUL)
      default:
        return "unknown";
#undef CAST_VLMUL
    }
  }

#define sext(x, len) (((int32_t)(x) << (32 - len)) >> (32 - len))
#define zext(x, len) (((uint32_t)(x) << (32 - len)) >> (32 - len))

  inline int32_t RvvSimm5() const {
    DCHECK(this->InstructionType() == InstructionBase::kVType);
    return sext(this->Bits(kRvvImm5Shift + kRvvImm5Bits - 1, kRvvImm5Shift),
                kRvvImm5Bits);
  }

  inline uint32_t RvvUimm5() const {
    DCHECK(this->InstructionType() == InstructionBase::kVType);
    uint32_t imm = this->Bits(kRvvImm5Shift + kRvvImm5Bits - 1, kRvvImm5Shift);
    return zext(imm, kRvvImm5Bits);
  }
#undef sext
#undef zext
  inline bool AqValue() const { return this->Bits(kAqShift, kAqShift); }

  inline bool RlValue() const { return this->Bits(kRlShift, kRlShift); }

  // Say if the instruction is a break or a trap.
  bool IsTrap() const;
};

class Instruction : public InstructionGetters<InstructionBase> {
 public:
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

// -----------------------------------------------------------------------------
// RISC-V assembly various constants.

// C/C++ argument slots size.
const int kCArgSlotCount = 0;

// TODO(plind): below should be based on kSystemPointerSize
// TODO(plind): find all usages and remove the needless instructions for n64.
const int kCArgsSlotsSize = kCArgSlotCount * kInstrSize * 2;

const int kInvalidStackOffset = -1;
const int kBranchReturnOffset = 2 * kInstrSize;

static const int kNegOffset = 0x00008000;

// -----------------------------------------------------------------------------
// Instructions.

template <class P>
bool InstructionGetters<P>::IsTrap() const {
  return (this->InstructionBits() == kBreakInstr);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_RISCV_BASE_CONSTANTS_RISCV_H_
