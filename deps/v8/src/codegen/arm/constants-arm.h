// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM_CONSTANTS_ARM_H_
#define V8_CODEGEN_ARM_CONSTANTS_ARM_H_

#include <stdint.h>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/common/code-memory-access.h"
#include "src/common/globals.h"
#include "src/utils/boxed-float.h"
#include "src/utils/utils.h"

// ARM EABI is required.
#if defined(__arm__) && !defined(__ARM_EABI__)
#error ARM EABI support is required.
#endif

namespace v8 {
namespace internal {

// Constant pool marker.
// Use UDF, the permanently undefined instruction.
const int kConstantPoolMarkerMask = 0xfff000f0;
const int kConstantPoolMarker = 0xe7f000f0;
const int kConstantPoolLengthMaxMask = 0xffff;
inline int EncodeConstantPoolLength(int length) {
  DCHECK((length & kConstantPoolLengthMaxMask) == length);
  return ((length & 0xfff0) << 4) | (length & 0xf);
}
inline int DecodeConstantPoolLength(int instr) {
  DCHECK_EQ(instr & kConstantPoolMarkerMask, kConstantPoolMarker);
  return ((instr >> 4) & 0xfff0) | (instr & 0xf);
}

// Number of registers in normal ARM mode.
constexpr int kNumRegisters = 16;
constexpr int kRegSizeInBitsLog2 = 5;

// VFP support.
constexpr int kNumVFPSingleRegisters = 32;
constexpr int kNumVFPDoubleRegisters = 32;
constexpr int kNumVFPRegisters =
    kNumVFPSingleRegisters + kNumVFPDoubleRegisters;

// PC is register 15.
constexpr int kPCRegister = 15;
constexpr int kNoRegister = -1;

// Used in embedded constant pool builder - max reach in bits for
// various load instructions (unsigned)
constexpr int kLdrMaxReachBits = 12;
constexpr int kVldrMaxReachBits = 10;

// The actual value of the kRootRegister is offset from the IsolateData's start
// to take advantage of negative displacement values.
//
// Loads allow a uint12 value with a separate sign bit (range [-4095, +4095]),
// so the first root is still addressable with a single load instruction.
constexpr int kRootRegisterBias = 4095;

// TODO(pkasting): For all the enum type aliases below, if overload resolution
// is desired, we could try to add some kind of constexpr class with implicit
// conversion to/from int and operator overloads, then inherit from that.

// -----------------------------------------------------------------------------
// Conditions.

// Defines constants and accessor classes to assemble, disassemble and
// simulate ARM instructions.
//
// Section references in the code refer to the "ARM Architecture Reference
// Manual" from July 2005 (available at http://www.arm.com/miscPDFs/14128.pdf)
//
// Constants for specific fields are defined in their respective named enums.
// General constants are in an anonymous enum in class Instr.

// Values for the condition field as defined in section A3.2
enum Condition : int {
  kNoCondition = -1,

  eq = 0 << 28,   // Z set            Equal.
  ne = 1 << 28,   // Z clear          Not equal.
  cs = 2 << 28,   // C set            Unsigned higher or same.
  cc = 3 << 28,   // C clear          Unsigned lower.
  mi = 4 << 28,   // N set            Negative.
  pl = 5 << 28,   // N clear          Positive or zero.
  vs = 6 << 28,   // V set            Overflow.
  vc = 7 << 28,   // V clear          No overflow.
  hi = 8 << 28,   // C set, Z clear   Unsigned higher.
  ls = 9 << 28,   // C clear or Z set Unsigned lower or same.
  ge = 10 << 28,  // N == V           Greater or equal.
  lt = 11 << 28,  // N != V           Less than.
  gt = 12 << 28,  // Z clear, N == V  Greater than.
  le = 13 << 28,  // Z set or N != V  Less then or equal
  al = 14 << 28,  //                  Always.

  // Special condition (refer to section A3.2.1).
  kSpecialCondition = 15 << 28,
  kNumberOfConditions = 16,

  // Aliases.
  hs = cs,  // C set            Unsigned higher or same.
  lo = cc,  // C clear          Unsigned lower.

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
  DCHECK(cond != al);
  return static_cast<Condition>(cond ^ ne);
}

// -----------------------------------------------------------------------------
// Instructions encoding.

// Instr is merely used by the Assembler to distinguish 32bit integers
// representing instructions from usual 32 bit values.
// Instruction objects are pointers to 32bit values, and provide methods to
// access the various ISA fields.
using Instr = int32_t;

// Opcodes for Data-processing instructions (instructions with a type 0 and 1)
// as defined in section A3.4
using Opcode = int;
constexpr Opcode AND = 0 << 21;   // Logical AND.
constexpr Opcode EOR = 1 << 21;   // Logical Exclusive OR.
constexpr Opcode SUB = 2 << 21;   // Subtract.
constexpr Opcode RSB = 3 << 21;   // Reverse Subtract.
constexpr Opcode ADD = 4 << 21;   // Add.
constexpr Opcode ADC = 5 << 21;   // Add with Carry.
constexpr Opcode SBC = 6 << 21;   // Subtract with Carry.
constexpr Opcode RSC = 7 << 21;   // Reverse Subtract with Carry.
constexpr Opcode TST = 8 << 21;   // Test.
constexpr Opcode TEQ = 9 << 21;   // Test Equivalence.
constexpr Opcode CMP = 10 << 21;  // Compare.
constexpr Opcode CMN = 11 << 21;  // Compare Negated.
constexpr Opcode ORR = 12 << 21;  // Logical (inclusive) OR.
constexpr Opcode MOV = 13 << 21;  // Move.
constexpr Opcode BIC = 14 << 21;  // Bit Clear.
constexpr Opcode MVN = 15 << 21;  // Move Not.

// The bits for bit 7-4 for some type 0 miscellaneous instructions.
using MiscInstructionsBits74 = int;
// With bits 22-21 01.
constexpr MiscInstructionsBits74 BX = 1 << 4;
constexpr MiscInstructionsBits74 BXJ = 2 << 4;
constexpr MiscInstructionsBits74 BLX = 3 << 4;
constexpr MiscInstructionsBits74 BKPT = 7 << 4;

// With bits 22-21 11.
constexpr MiscInstructionsBits74 CLZ = 1 << 4;

// Instruction encoding bits and masks.
constexpr int H = 1 << 5;   // Halfword (or byte).
constexpr int S6 = 1 << 6;  // Signed (or unsigned).
constexpr int L = 1 << 20;  // Load (or store).
constexpr int S = 1 << 20;  // Set condition code (or leave unchanged).
constexpr int W = 1 << 21;  // Writeback base register (or leave unchanged).
constexpr int A = 1 << 21;  // Accumulate in multiply instruction (or not).
constexpr int B = 1 << 22;  // Unsigned byte (or word).
constexpr int N = 1 << 22;  // Long (or short).
constexpr int U = 1 << 23;  // Positive (or negative) offset/index.
constexpr int P =
    1 << 24;  // Offset/pre-indexed addressing (or post-indexed addressing).
constexpr int I = 1 << 25;  // Immediate shifter operand (or not).
constexpr int B0 = 1 << 0;
constexpr int B4 = 1 << 4;
constexpr int B5 = 1 << 5;
constexpr int B6 = 1 << 6;
constexpr int B7 = 1 << 7;
constexpr int B8 = 1 << 8;
constexpr int B9 = 1 << 9;
constexpr int B10 = 1 << 10;
constexpr int B12 = 1 << 12;
constexpr int B16 = 1 << 16;
constexpr int B17 = 1 << 17;
constexpr int B18 = 1 << 18;
constexpr int B19 = 1 << 19;
constexpr int B20 = 1 << 20;
constexpr int B21 = 1 << 21;
constexpr int B22 = 1 << 22;
constexpr int B23 = 1 << 23;
constexpr int B24 = 1 << 24;
constexpr int B25 = 1 << 25;
constexpr int B26 = 1 << 26;
constexpr int B27 = 1 << 27;
constexpr int B28 = 1 << 28;

// Instruction bit masks.
constexpr int kCondMask = 15 << 28;
constexpr int kALUMask = 0x6f << 21;
constexpr int kRdMask = 15 << 12;  // In str instruction.
constexpr int kCoprocessorMask = 15 << 8;
constexpr int kOpCodeMask = 15 << 21;  // In data-processing instructions.
constexpr int kImm24Mask = (1 << 24) - 1;
constexpr int kImm16Mask = (1 << 16) - 1;
constexpr int kImm8Mask = (1 << 8) - 1;
constexpr int kOff12Mask = (1 << 12) - 1;
constexpr int kOff8Mask = (1 << 8) - 1;

using BarrierOption = int;
constexpr BarrierOption OSHLD = 0x1;
constexpr BarrierOption OSHST = 0x2;
constexpr BarrierOption OSH = 0x3;
constexpr BarrierOption NSHLD = 0x5;
constexpr BarrierOption NSHST = 0x6;
constexpr BarrierOption NSH = 0x7;
constexpr BarrierOption ISHLD = 0x9;
constexpr BarrierOption ISHST = 0xa;
constexpr BarrierOption ISH = 0xb;
constexpr BarrierOption LD = 0xd;
constexpr BarrierOption ST = 0xe;
constexpr BarrierOption SY = 0xf;

// -----------------------------------------------------------------------------
// Addressing modes and instruction variants.

// Condition code updating mode.
using SBit = int;
constexpr SBit SetCC = 1 << 20;    // Set condition code.
constexpr SBit LeaveCC = 0 << 20;  // Leave condition code unchanged.

// Status register selection.
using SRegister = int;
constexpr SRegister CPSR = 0 << 22;
constexpr SRegister SPSR = 1 << 22;

// Shifter types for Data-processing operands as defined in section A5.1.2.
using ShiftOp = int;
constexpr ShiftOp LSL = 0 << 5;  // Logical shift left.
constexpr ShiftOp LSR = 1 << 5;  // Logical shift right.
constexpr ShiftOp ASR = 2 << 5;  // Arithmetic shift right.
constexpr ShiftOp ROR = 3 << 5;  // Rotate right.

// RRX is encoded as ROR with shift_imm == 0.
// Use a special code to make the distinction. The RRX ShiftOp is only used
// as an argument, and will never actually be encoded. The Assembler will
// detect it and emit the correct ROR shift operand with shift_imm == 0.
constexpr ShiftOp RRX = -1;
constexpr ShiftOp kNumberOfShifts = 4;

// Status register fields.
using SRegisterField = int;
constexpr SRegisterField CPSR_c = CPSR | 1 << 16;
constexpr SRegisterField CPSR_x = CPSR | 1 << 17;
constexpr SRegisterField CPSR_s = CPSR | 1 << 18;
constexpr SRegisterField CPSR_f = CPSR | 1 << 19;
constexpr SRegisterField SPSR_c = SPSR | 1 << 16;
constexpr SRegisterField SPSR_x = SPSR | 1 << 17;
constexpr SRegisterField SPSR_s = SPSR | 1 << 18;
constexpr SRegisterField SPSR_f = SPSR | 1 << 19;

// Status register field mask (or'ed SRegisterField enum values).
using SRegisterFieldMask = uint32_t;

// Memory operand addressing mode.
using AddrMode = int;
// Bit encoding P U W.
constexpr AddrMode Offset = (8 | 4 | 0)
                            << 21;  // Offset (without writeback to base).
constexpr AddrMode PreIndex = (8 | 4 | 1)
                              << 21;  // Pre-indexed addressing with writeback.
constexpr AddrMode PostIndex =
    (0 | 4 | 0) << 21;  // Post-indexed addressing with writeback.
constexpr AddrMode NegOffset =
    (8 | 0 | 0) << 21;  // Negative offset (without writeback to base).
constexpr AddrMode NegPreIndex = (8 | 0 | 1)
                                 << 21;  // Negative pre-indexed with writeback.
constexpr AddrMode NegPostIndex =
    (0 | 0 | 0) << 21;  // Negative post-indexed with writeback.

// Load/store multiple addressing mode.
using BlockAddrMode = int;
// Bit encoding P U W .
constexpr BlockAddrMode da = (0 | 0 | 0) << 21;  // Decrement after.
constexpr BlockAddrMode ia = (0 | 4 | 0) << 21;  // Increment after.
constexpr BlockAddrMode db = (8 | 0 | 0) << 21;  // Decrement before.
constexpr BlockAddrMode ib = (8 | 4 | 0) << 21;  // Increment before.
constexpr BlockAddrMode da_w =
    (0 | 0 | 1) << 21;  // Decrement after with writeback to base.
constexpr BlockAddrMode ia_w =
    (0 | 4 | 1) << 21;  // Increment after with writeback to base.
constexpr BlockAddrMode db_w =
    (8 | 0 | 1) << 21;  // Decrement before with writeback to base.
constexpr BlockAddrMode ib_w =
    (8 | 4 | 1) << 21;  // Increment before with writeback to base.

// Alias modes for comparison when writeback does not matter.
constexpr BlockAddrMode da_x = (0 | 0 | 0) << 21;  // Decrement after.
constexpr BlockAddrMode ia_x = (0 | 4 | 0) << 21;  // Increment after.
constexpr BlockAddrMode db_x = (8 | 0 | 0) << 21;  // Decrement before.
constexpr BlockAddrMode ib_x = (8 | 4 | 0) << 21;  // Increment before.

constexpr BlockAddrMode kBlockAddrModeMask = (8 | 4 | 1) << 21;

// Coprocessor load/store operand size.
using LFlag = int;
constexpr LFlag Long = 1 << 22;   // Long load/store coprocessor.
constexpr LFlag Short = 0 << 22;  // Short load/store coprocessor.

// Neon sizes.
using NeonSize = int;
constexpr NeonSize Neon8 = 0x0;
constexpr NeonSize Neon16 = 0x1;
constexpr NeonSize Neon32 = 0x2;
constexpr NeonSize Neon64 = 0x3;

// NEON data type, top bit set for unsigned data types.
using NeonDataType = int;
constexpr NeonDataType NeonS8 = 0;
constexpr NeonDataType NeonS16 = 1;
constexpr NeonDataType NeonS32 = 2;
constexpr NeonDataType NeonS64 = 3;
constexpr NeonDataType NeonU8 = 4;
constexpr NeonDataType NeonU16 = 5;
constexpr NeonDataType NeonU32 = 6;
constexpr NeonDataType NeonU64 = 7;

inline int NeonU(NeonDataType dt) { return static_cast<int>(dt) >> 2; }
inline int NeonSz(NeonDataType dt) { return static_cast<int>(dt) & 0x3; }

// Convert sizes to data types (U bit is clear).
inline NeonDataType NeonSizeToDataType(NeonSize size) {
  DCHECK_NE(Neon64, size);
  return static_cast<NeonDataType>(size);
}

inline NeonSize NeonDataTypeToSize(NeonDataType dt) {
  return static_cast<NeonSize>(NeonSz(dt));
}

using NeonListType = int;
constexpr NeonListType nlt_1 = 0x7;
constexpr NeonListType nlt_2 = 0xA;
constexpr NeonListType nlt_3 = 0x6;
constexpr NeonListType nlt_4 = 0x2;

// -----------------------------------------------------------------------------
// Supervisor Call (svc) specific support.

// Special Software Interrupt codes when used in the presence of the ARM
// simulator.
// svc (formerly swi) provides a 24bit immediate value. Use bits 22:0 for
// standard SoftwareInterrupCode. Bit 23 is reserved for the stop feature.
using SoftwareInterruptCodes = int;
// transition to C code
constexpr SoftwareInterruptCodes kCallRtRedirected = 0x10;
// break point
constexpr SoftwareInterruptCodes kBreakpoint = 0x20;
// stop
constexpr SoftwareInterruptCodes kStopCode = 1 << 23;
constexpr uint32_t kStopCodeMask = kStopCode - 1;
constexpr uint32_t kMaxStopCode = kStopCode - 1;
constexpr int32_t kDefaultStopCode = -1;

// Type of VFP register. Determines register encoding.
using VFPRegPrecision = int;
constexpr VFPRegPrecision kSinglePrecision = 0;
constexpr VFPRegPrecision kDoublePrecision = 1;
constexpr VFPRegPrecision kSimd128Precision = 2;

// VFP FPSCR constants.
using VFPConversionMode = int;
constexpr VFPConversionMode kFPSCRRounding = 0;
constexpr VFPConversionMode kDefaultRoundToZero = 1;

// This mask does not include the "inexact" or "input denormal" cumulative
// exceptions flags, because we usually don't want to check for it.
constexpr uint32_t kVFPExceptionMask = 0xf;
constexpr uint32_t kVFPInvalidOpExceptionBit = 1 << 0;
constexpr uint32_t kVFPOverflowExceptionBit = 1 << 2;
constexpr uint32_t kVFPUnderflowExceptionBit = 1 << 3;
constexpr uint32_t kVFPInexactExceptionBit = 1 << 4;
constexpr uint32_t kVFPFlushToZeroMask = 1 << 24;
constexpr uint32_t kVFPDefaultNaNModeControlBit = 1 << 25;

constexpr uint32_t kVFPNConditionFlagBit = 1 << 31;
constexpr uint32_t kVFPZConditionFlagBit = 1 << 30;
constexpr uint32_t kVFPCConditionFlagBit = 1 << 29;
constexpr uint32_t kVFPVConditionFlagBit = 1 << 28;

// VFP rounding modes. See ARM DDI 0406B Page A2-29.
using VFPRoundingMode = int;
constexpr VFPRoundingMode RN = 0 << 22;  // Round to Nearest.
constexpr VFPRoundingMode RP = 1 << 22;  // Round towards Plus Infinity.
constexpr VFPRoundingMode RM = 2 << 22;  // Round towards Minus Infinity.
constexpr VFPRoundingMode RZ = 3 << 22;  // Round towards zero.

// Aliases.
constexpr VFPRoundingMode kRoundToNearest = RN;
constexpr VFPRoundingMode kRoundToPlusInf = RP;
constexpr VFPRoundingMode kRoundToMinusInf = RM;
constexpr VFPRoundingMode kRoundToZero = RZ;

const uint32_t kVFPRoundingModeMask = 3 << 22;

enum CheckForInexactConversion {
  kCheckForInexactConversion,
  kDontCheckForInexactConversion
};

// -----------------------------------------------------------------------------
// Hints.

// Branch hints are not used on the ARM.  They are defined so that they can
// appear in shared function signatures, but will be ignored in ARM
// implementations.
enum Hint { no_hint };

// Hints are not used on the arm.  Negating is trivial.
inline Hint NegateHint(Hint ignored) { return no_hint; }

// -----------------------------------------------------------------------------
// Instruction abstraction.

// The class Instruction enables access to individual fields defined in the ARM
// architecture instruction set encoding as described in figure A3-1.
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

class Instruction {
 public:
  // Difference between address of current opcode and value read from pc
  // register.
  static constexpr int kPcLoadDelta = 8;

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

  // Extract a single bit from the instruction bits and return it as bit 0 in
  // the result.
  inline int Bit(int nr) const { return (InstructionBits() >> nr) & 1; }

  // Extract a bit field <hi:lo> from the instruction bits and return it in the
  // least-significant bits of the result.
  inline int Bits(int hi, int lo) const {
    return (InstructionBits() >> lo) & ((2 << (hi - lo)) - 1);
  }

  // Read a bit field <hi:lo>, leaving its position unchanged in the result.
  inline int BitField(int hi, int lo) const {
    return InstructionBits() & (((2 << (hi - lo)) - 1) << lo);
  }

  // Accessors for the different named fields used in the ARM encoding.
  // The naming of these accessor corresponds to figure A3-1.
  //
  // Two kind of accessors are declared:
  // - <Name>Field() will return the raw field, i.e. the field's bits at their
  //   original place in the instruction encoding.
  //   e.g. if instr is the 'addgt r0, r1, r2' instruction, encoded as
  //   0xC0810002 ConditionField(instr) will return 0xC0000000.
  // - <Name>Value() will return the field value, shifted back to bit 0.
  //   e.g. if instr is the 'addgt r0, r1, r2' instruction, encoded as
  //   0xC0810002 ConditionField(instr) will return 0xC.

  // Generally applicable fields
  inline int ConditionValue() const { return Bits(31, 28); }
  inline Condition ConditionField() const {
    return static_cast<Condition>(BitField(31, 28));
  }
  DECLARE_STATIC_TYPED_ACCESSOR(int, ConditionValue)
  DECLARE_STATIC_TYPED_ACCESSOR(Condition, ConditionField)

  inline int TypeValue() const { return Bits(27, 25); }
  inline int SpecialValue() const { return Bits(27, 23); }

  inline int RnValue() const { return Bits(19, 16); }
  DECLARE_STATIC_ACCESSOR(RnValue)
  inline int RdValue() const { return Bits(15, 12); }
  DECLARE_STATIC_ACCESSOR(RdValue)

  inline int CoprocessorValue() const { return Bits(11, 8); }
  // Support for VFP.
  // Vn(19-16) | Vd(15-12) |  Vm(3-0)
  inline int VnValue() const { return Bits(19, 16); }
  inline int VmValue() const { return Bits(3, 0); }
  inline int VdValue() const { return Bits(15, 12); }
  inline int NValue() const { return Bit(7); }
  inline int MValue() const { return Bit(5); }
  inline int DValue() const { return Bit(22); }
  inline int RtValue() const { return Bits(15, 12); }
  inline int PValue() const { return Bit(24); }
  inline int UValue() const { return Bit(23); }
  inline int Opc1Value() const { return (Bit(23) << 2) | Bits(21, 20); }
  inline int Opc2Value() const { return Bits(19, 16); }
  inline int Opc3Value() const { return Bits(7, 6); }
  inline int SzValue() const { return Bit(8); }
  inline int VLValue() const { return Bit(20); }
  inline int VCValue() const { return Bit(8); }
  inline int VAValue() const { return Bits(23, 21); }
  inline int VBValue() const { return Bits(6, 5); }
  inline int VFPNRegValue(VFPRegPrecision pre) {
    return VFPGlueRegValue(pre, 16, 7);
  }
  inline int VFPMRegValue(VFPRegPrecision pre) {
    return VFPGlueRegValue(pre, 0, 5);
  }
  inline int VFPDRegValue(VFPRegPrecision pre) {
    return VFPGlueRegValue(pre, 12, 22);
  }

  // Fields used in Data processing instructions
  inline int OpcodeValue() const { return static_cast<Opcode>(Bits(24, 21)); }
  inline Opcode OpcodeField() const {
    return static_cast<Opcode>(BitField(24, 21));
  }
  inline int SValue() const { return Bit(20); }
  // with register
  inline int RmValue() const { return Bits(3, 0); }
  DECLARE_STATIC_ACCESSOR(RmValue)
  inline int ShiftValue() const { return static_cast<ShiftOp>(Bits(6, 5)); }
  inline ShiftOp ShiftField() const {
    return static_cast<ShiftOp>(BitField(6, 5));
  }
  inline int RegShiftValue() const { return Bit(4); }
  inline int RsValue() const { return Bits(11, 8); }
  inline int ShiftAmountValue() const { return Bits(11, 7); }
  // with immediate
  inline int RotateValue() const { return Bits(11, 8); }
  DECLARE_STATIC_ACCESSOR(RotateValue)
  inline int Immed8Value() const { return Bits(7, 0); }
  DECLARE_STATIC_ACCESSOR(Immed8Value)
  inline int Immed4Value() const { return Bits(19, 16); }
  inline int ImmedMovwMovtValue() const {
    return Immed4Value() << 12 | Offset12Value();
  }
  DECLARE_STATIC_ACCESSOR(ImmedMovwMovtValue)

  // Fields used in Load/Store instructions
  inline int PUValue() const { return Bits(24, 23); }
  inline int PUField() const { return BitField(24, 23); }
  inline int BValue() const { return Bit(22); }
  inline int WValue() const { return Bit(21); }
  inline int LValue() const { return Bit(20); }
  // with register uses same fields as Data processing instructions above
  // with immediate
  inline int Offset12Value() const { return Bits(11, 0); }
  // multiple
  inline int RlistValue() const { return Bits(15, 0); }
  // extra loads and stores
  inline int SignValue() const { return Bit(6); }
  inline int HValue() const { return Bit(5); }
  inline int ImmedHValue() const { return Bits(11, 8); }
  inline int ImmedLValue() const { return Bits(3, 0); }

  // Fields used in Branch instructions
  inline int LinkValue() const { return Bit(24); }
  inline int SImmed24Value() const {
    return signed_bitextract_32(23, 0, InstructionBits());
  }

  bool IsBranch() { return Bit(27) == 1 && Bit(25) == 1; }

  int GetBranchOffset() {
    DCHECK(IsBranch());
    return SImmed24Value() * kInstrSize;
  }

  inline void SetBranchOffset(int32_t branch_offset,
                              WritableJitAllocation* jit_allocation) {
    DCHECK(IsBranch());
    DCHECK_EQ(branch_offset % kInstrSize, 0);
    int32_t new_imm24 = branch_offset / kInstrSize;
    CHECK(is_int24(new_imm24));
    SetInstructionBits(
        (InstructionBits() & ~(kImm24Mask)) | (new_imm24 & kImm24Mask),
        jit_allocation);
  }

  // Fields used in Software interrupt instructions
  inline SoftwareInterruptCodes SvcValue() const {
    return static_cast<SoftwareInterruptCodes>(Bits(23, 0));
  }

  // Test for special encodings of type 0 instructions (extra loads and stores,
  // as well as multiplications).
  inline bool IsSpecialType0() const { return (Bit(7) == 1) && (Bit(4) == 1); }

  // Test for miscellaneous instructions encodings of type 0 instructions.
  inline bool IsMiscType0() const {
    return (Bit(24) == 1) && (Bit(23) == 0) && (Bit(20) == 0) &&
           ((Bit(7) == 0));
  }

  // Test for nop-like instructions which fall under type 1.
  inline bool IsNopLikeType1() const { return Bits(24, 8) == 0x120F0; }

  // Test for a stop instruction.
  inline bool IsStop() const {
    return (TypeValue() == 7) && (Bit(24) == 1) && (SvcValue() >= kStopCode);
  }

  // Special accessors that test for existence of a value.
  inline bool HasS() const { return SValue() == 1; }
  inline bool HasB() const { return BValue() == 1; }
  inline bool HasW() const { return WValue() == 1; }
  inline bool HasL() const { return LValue() == 1; }
  inline bool HasU() const { return UValue() == 1; }
  inline bool HasSign() const { return SignValue() == 1; }
  inline bool HasH() const { return HValue() == 1; }
  inline bool HasLink() const { return LinkValue() == 1; }

  // Decode the double immediate from a vmov instruction.
  Float64 DoubleImmedVmov() const;

  // Instructions are read of out a code stream. The only way to get a
  // reference to an instruction is to convert a pointer. There is no way
  // to allocate or create instances of class Instruction.
  // Use the At(pc) function to create references to Instruction.
  static Instruction* At(Address pc) {
    return reinterpret_cast<Instruction*>(pc);
  }

 private:
  // Join split register codes, depending on register precision.
  // four_bit is the position of the least-significant bit of the four
  // bit specifier. one_bit is the position of the additional single bit
  // specifier.
  inline int VFPGlueRegValue(VFPRegPrecision pre, int four_bit, int one_bit) {
    if (pre == kSinglePrecision) {
      return (Bits(four_bit + 3, four_bit) << 1) | Bit(one_bit);
    } else {
      int reg_num = (Bit(one_bit) << 4) | Bits(four_bit + 3, four_bit);
      if (pre == kDoublePrecision) {
        return reg_num;
      }
      DCHECK_EQ(kSimd128Precision, pre);
      DCHECK_EQ(reg_num & 1, 0);
      return reg_num / 2;
    }
  }

  // We need to prevent the creation of instances of class Instruction.
  DISALLOW_IMPLICIT_CONSTRUCTORS(Instruction);
};

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
  static const char* names_[kNumRegisters];
  static const RegisterAlias aliases_[];
};

// Helper functions for converting between VFP register numbers and names.
class VFPRegisters {
 public:
  // Return the name of the register.
  static const char* Name(int reg, bool is_double);

  // Lookup the register number for the name provided.
  // Set flag pointed by is_double to true if register
  // is double-precision.
  static int Number(const char* name, bool* is_double);

 private:
  static const char* names_[kNumVFPRegisters];
};

// The maximum size of the code range s.t. pc-relative calls are possible
// between all Code objects in the range.
//
// Relative jumps on ARM can address ±32 MB.
constexpr size_t kMaxPCRelativeCodeRangeInMB = 32;

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM_CONSTANTS_ARM_H_
