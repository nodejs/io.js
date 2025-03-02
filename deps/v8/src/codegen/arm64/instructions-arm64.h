// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM64_INSTRUCTIONS_ARM64_H_
#define V8_CODEGEN_ARM64_INSTRUCTIONS_ARM64_H_

#include "src/base/memory.h"
#include "src/codegen/arm64/constants-arm64.h"
#include "src/codegen/arm64/register-arm64.h"
#include "src/codegen/arm64/utils-arm64.h"
#include "src/common/code-memory-access.h"
#include "src/common/globals.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

struct AssemblerOptions;
class Zone;

// ISA constants. --------------------------------------------------------------

using Instr = uint32_t;

#if defined(V8_OS_WIN)
extern "C" {
#endif

extern const float16 kFP16PositiveInfinity;
extern const float16 kFP16NegativeInfinity;
V8_EXPORT_PRIVATE extern const float kFP32PositiveInfinity;
V8_EXPORT_PRIVATE extern const float kFP32NegativeInfinity;
V8_EXPORT_PRIVATE extern const double kFP64PositiveInfinity;
V8_EXPORT_PRIVATE extern const double kFP64NegativeInfinity;

// This value is a signalling NaN as both a double and as a float (taking the
// least-significant word).
V8_EXPORT_PRIVATE extern const double kFP64SignallingNaN;
V8_EXPORT_PRIVATE extern const float kFP32SignallingNaN;

// A similar value, but as a quiet NaN.
V8_EXPORT_PRIVATE extern const double kFP64QuietNaN;
V8_EXPORT_PRIVATE extern const float kFP32QuietNaN;

// The default NaN values (for FPCR.DN=1).
V8_EXPORT_PRIVATE extern const double kFP64DefaultNaN;
V8_EXPORT_PRIVATE extern const float kFP32DefaultNaN;
extern const float16 kFP16DefaultNaN;

#if defined(V8_OS_WIN)
}  // end of extern "C"
#endif

unsigned CalcLSDataSizeLog2(LoadStoreOp op);
unsigned CalcLSPairDataSize(LoadStorePairOp op);

enum ImmBranchType {
  UnknownBranchType = 0,
  CondBranchType = 1,
  UncondBranchType = 2,
  CompareBranchType = 3,
  TestBranchType = 4
};

enum AddrMode { Offset, PreIndex, PostIndex };

enum FPRounding {
  // The first four values are encodable directly by FPCR<RMode>.
  FPTieEven = 0x0,
  FPPositiveInfinity = 0x1,
  FPNegativeInfinity = 0x2,
  FPZero = 0x3,

  // The final rounding modes are only available when explicitly specified by
  // the instruction (such as with fcvta). They cannot be set in FPCR.
  FPTieAway,
  FPRoundOdd
};

enum Reg31Mode { Reg31IsStackPointer, Reg31IsZeroRegister };

// Instructions. ---------------------------------------------------------------

class Instruction {
 public:
  V8_INLINE Instr InstructionBits() const {
    // Usually this is aligned, but when de/serializing that's not guaranteed.
    return base::ReadUnalignedValue<Instr>(reinterpret_cast<Address>(this));
  }

  V8_EXPORT_PRIVATE void SetInstructionBits(
      Instr new_instr, WritableJitAllocation* jit_allocation = nullptr);

  int Bit(int pos) const { return (InstructionBits() >> pos) & 1; }

  uint32_t Bits(int msb, int lsb) const {
    return unsigned_bitextract_32(msb, lsb, InstructionBits());
  }

  int32_t SignedBits(int msb, int lsb) const {
    // Usually this is aligned, but when de/serializing that's not guaranteed.
    int32_t bits =
        base::ReadUnalignedValue<int32_t>(reinterpret_cast<Address>(this));
    return signed_bitextract_32(msb, lsb, bits);
  }

  Instr Mask(uint32_t mask) const { return InstructionBits() & mask; }

  V8_INLINE const Instruction* following(int count = 1) const {
    return InstructionAtOffset(count * static_cast<int>(kInstrSize));
  }

  V8_INLINE Instruction* following(int count = 1) {
    return InstructionAtOffset(count * static_cast<int>(kInstrSize));
  }

  V8_INLINE const Instruction* preceding(int count = 1) const {
    return following(-count);
  }

  V8_INLINE Instruction* preceding(int count = 1) { return following(-count); }

#define DEFINE_GETTER(Name, HighBit, LowBit, Func) \
  int32_t Name() const { return Func(HighBit, LowBit); }
  INSTRUCTION_FIELDS_LIST(DEFINE_GETTER)
#undef DEFINE_GETTER

  // ImmPCRel is a compound field (not present in INSTRUCTION_FIELDS_LIST),
  // formed from ImmPCRelLo and ImmPCRelHi.
  int ImmPCRel() const {
    DCHECK(IsPCRelAddressing());
    int offset = (static_cast<uint32_t>(ImmPCRelHi()) << ImmPCRelLo_width) |
                 ImmPCRelLo();
    int width = ImmPCRelLo_width + ImmPCRelHi_width;
    return signed_bitextract_32(width - 1, 0, offset);
  }

  uint64_t ImmLogical();
  unsigned ImmNEONabcdefgh() const;
  float ImmFP32();
  double ImmFP64();
  float ImmNEONFP32() const;
  double ImmNEONFP64() const;

  unsigned SizeLS() const {
    return CalcLSDataSizeLog2(static_cast<LoadStoreOp>(Mask(LoadStoreMask)));
  }

  unsigned SizeLSPair() const {
    return CalcLSPairDataSize(
        static_cast<LoadStorePairOp>(Mask(LoadStorePairMask)));
  }

  int NEONLSIndex(int access_size_shift) const {
    int q = NEONQ();
    int s = NEONS();
    int size = NEONLSSize();
    int index = (q << 3) | (s << 2) | size;
    return index >> access_size_shift;
  }

  // Helpers.
  bool IsCondBranchImm() const {
    return Mask(ConditionalBranchFMask) == ConditionalBranchFixed;
  }

  bool IsUncondBranchImm() const {
    return Mask(UnconditionalBranchFMask) == UnconditionalBranchFixed;
  }

  bool IsCompareBranch() const {
    return Mask(CompareBranchFMask) == CompareBranchFixed;
  }

  bool IsTestBranch() const { return Mask(TestBranchFMask) == TestBranchFixed; }

  bool IsImmBranch() const { return BranchType() != UnknownBranchType; }

  static float Imm8ToFP32(uint32_t imm8) {
    //   Imm8: abcdefgh (8 bits)
    // Single: aBbb.bbbc.defg.h000.0000.0000.0000.0000 (32 bits)
    // where B is b ^ 1
    uint32_t bits = imm8;
    uint32_t bit7 = (bits >> 7) & 0x1;
    uint32_t bit6 = (bits >> 6) & 0x1;
    uint32_t bit5_to_0 = bits & 0x3f;
    uint32_t result = (bit7 << 31) | ((32 - bit6) << 25) | (bit5_to_0 << 19);

    return base::bit_cast<float>(result);
  }

  static double Imm8ToFP64(uint32_t imm8) {
    //   Imm8: abcdefgh (8 bits)
    // Double: aBbb.bbbb.bbcd.efgh.0000.0000.0000.0000
    //         0000.0000.0000.0000.0000.0000.0000.0000 (64 bits)
    // where B is b ^ 1
    uint32_t bits = imm8;
    uint64_t bit7 = (bits >> 7) & 0x1;
    uint64_t bit6 = (bits >> 6) & 0x1;
    uint64_t bit5_to_0 = bits & 0x3f;
    uint64_t result = (bit7 << 63) | ((256 - bit6) << 54) | (bit5_to_0 << 48);

    return base::bit_cast<double>(result);
  }

  bool IsLdrLiteral() const {
    return Mask(LoadLiteralFMask) == LoadLiteralFixed;
  }

  bool IsLdrLiteralX() const { return Mask(LoadLiteralMask) == LDR_x_lit; }
  bool IsLdrLiteralW() const { return Mask(LoadLiteralMask) == LDR_w_lit; }

  bool IsPCRelAddressing() const {
    return Mask(PCRelAddressingFMask) == PCRelAddressingFixed;
  }

  bool IsAdr() const { return Mask(PCRelAddressingMask) == ADR; }

  bool IsBrk() const { return Mask(ExceptionMask) == BRK; }

  bool IsUnresolvedInternalReference() const {
    // Unresolved internal references are encoded as two consecutive brk
    // instructions.
    return IsBrk() && following()->IsBrk();
  }

  bool IsLogicalImmediate() const {
    return Mask(LogicalImmediateFMask) == LogicalImmediateFixed;
  }

  bool IsAddSubImmediate() const {
    return Mask(AddSubImmediateFMask) == AddSubImmediateFixed;
  }

  bool IsAddSubShifted() const {
    return Mask(AddSubShiftedFMask) == AddSubShiftedFixed;
  }

  bool IsAddSubExtended() const {
    return Mask(AddSubExtendedFMask) == AddSubExtendedFixed;
  }

  // Match any loads or stores, including pairs.
  bool IsLoadOrStore() const {
    return Mask(LoadStoreAnyFMask) == LoadStoreAnyFixed;
  }

  // Match any loads, including pairs.
  bool IsLoad() const;
  // Match any stores, including pairs.
  bool IsStore() const;

  // Indicate whether Rd can be the stack pointer or the zero register. This
  // does not check that the instruction actually has an Rd field.
  Reg31Mode RdMode() const {
    // The following instructions use sp or wsp as Rd:
    //  Add/sub (immediate) when not setting the flags.
    //  Add/sub (extended) when not setting the flags.
    //  Logical (immediate) when not setting the flags.
    // Otherwise, r31 is the zero register.
    if (IsAddSubImmediate() || IsAddSubExtended()) {
      if (Mask(AddSubSetFlagsBit)) {
        return Reg31IsZeroRegister;
      } else {
        return Reg31IsStackPointer;
      }
    }
    if (IsLogicalImmediate()) {
      // Of the logical (immediate) instructions, only ANDS (and its aliases)
      // can set the flags. The others can all write into sp.
      // Note that some logical operations are not available to
      // immediate-operand instructions, so we have to combine two masks here.
      if (Mask(LogicalImmediateMask & LogicalOpMask) == ANDS) {
        return Reg31IsZeroRegister;
      } else {
        return Reg31IsStackPointer;
      }
    }
    return Reg31IsZeroRegister;
  }

  // Indicate whether Rn can be the stack pointer or the zero register. This
  // does not check that the instruction actually has an Rn field.
  Reg31Mode RnMode() const {
    // The following instructions use sp or wsp as Rn:
    //  All loads and stores.
    //  Add/sub (immediate).
    //  Add/sub (extended).
    // Otherwise, r31 is the zero register.
    if (IsLoadOrStore() || IsAddSubImmediate() || IsAddSubExtended()) {
      return Reg31IsStackPointer;
    }
    return Reg31IsZeroRegister;
  }

  ImmBranchType BranchType() const {
    if (IsCondBranchImm()) {
      return CondBranchType;
    } else if (IsUncondBranchImm()) {
      return UncondBranchType;
    } else if (IsCompareBranch()) {
      return CompareBranchType;
    } else if (IsTestBranch()) {
      return TestBranchType;
    } else {
      return UnknownBranchType;
    }
  }

  static constexpr int ImmBranchRangeBitwidth(ImmBranchType branch_type) {
    switch (branch_type) {
      case UncondBranchType:
        return ImmUncondBranch_width;
      case CondBranchType:
        return ImmCondBranch_width;
      case CompareBranchType:
        return ImmCmpBranch_width;
      case TestBranchType:
        return ImmTestBranch_width;
      default:
        UNREACHABLE();
    }
  }

  // The range of the branch instruction, expressed as 'instr +- range'.
  static constexpr int32_t ImmBranchRange(ImmBranchType branch_type) {
    return (1 << (ImmBranchRangeBitwidth(branch_type) + kInstrSizeLog2)) / 2 -
           kInstrSize;
  }

  int ImmBranch() const {
    switch (BranchType()) {
      case CondBranchType:
        return ImmCondBranch();
      case UncondBranchType:
        return ImmUncondBranch();
      case CompareBranchType:
        return ImmCmpBranch();
      case TestBranchType:
        return ImmTestBranch();
      default:
        UNREACHABLE();
    }
    return 0;
  }

  int ImmUnresolvedInternalReference() const {
    DCHECK(IsUnresolvedInternalReference());
    // Unresolved references are encoded as two consecutive brk instructions.
    // The associated immediate is made of the two 16-bit payloads.
    int32_t high16 = ImmException();
    int32_t low16 = following()->ImmException();
    return (high16 << 16) | low16;
  }

  bool IsUnconditionalBranch() const {
    return Mask(UnconditionalBranchMask) == B;
  }

  bool IsBranchAndLink() const { return Mask(UnconditionalBranchMask) == BL; }

  bool IsBranchAndLinkToRegister() const {
    return Mask(UnconditionalBranchToRegisterMask) == BLR;
  }

  bool IsMovz() const {
    return (Mask(MoveWideImmediateMask) == MOVZ_x) ||
           (Mask(MoveWideImmediateMask) == MOVZ_w);
  }

  bool IsMovk() const {
    return (Mask(MoveWideImmediateMask) == MOVK_x) ||
           (Mask(MoveWideImmediateMask) == MOVK_w);
  }

  bool IsMovn() const {
    return (Mask(MoveWideImmediateMask) == MOVN_x) ||
           (Mask(MoveWideImmediateMask) == MOVN_w);
  }

  bool IsException() const { return Mask(ExceptionFMask) == ExceptionFixed; }

  bool IsPAuth() const { return Mask(SystemPAuthFMask) == SystemPAuthFixed; }

  bool IsBti() const {
    if (Mask(SystemHintFMask) == SystemHintFixed) {
      int imm_hint = ImmHint();
      switch (imm_hint) {
        case BTI:
        case BTI_c:
        case BTI_j:
        case BTI_jc:
          return true;
      }
    }
    return false;
  }

  bool IsNop(int n) {
    // A marking nop is an instruction
    //   mov r<n>,  r<n>
    // which is encoded as
    //   orr r<n>, xzr, r<n>
    return (Mask(LogicalShiftedMask) == ORR_x) && (Rd() == Rm()) && (Rd() == n);
  }

  // Find the PC offset encoded in this instruction. 'this' may be a branch or
  // a PC-relative addressing instruction.
  // The offset returned is unscaled.
  V8_EXPORT_PRIVATE int64_t ImmPCOffset();

  // Find the target of this instruction. 'this' may be a branch or a
  // PC-relative addressing instruction.
  V8_EXPORT_PRIVATE Instruction* ImmPCOffsetTarget();

  // Check if the offset is in range of a given branch type. The offset is
  // a byte offset, unscaled.
  static constexpr bool IsValidImmPCOffset(ImmBranchType branch_type,
                                           ptrdiff_t offset) {
    DCHECK_EQ(offset % kInstrSize, 0);
    return is_intn(offset / kInstrSize, ImmBranchRangeBitwidth(branch_type));
  }

  bool IsTargetInImmPCOffsetRange(Instruction* target);
  // Patch a PC-relative offset to refer to 'target'. 'this' may be a branch or
  // a PC-relative addressing instruction.
  void SetImmPCOffsetTarget(Zone* zone, AssemblerOptions options,
                            Instruction* target);
  void SetUnresolvedInternalReferenceImmTarget(Zone* zone,
                                               AssemblerOptions options,
                                               Instruction* target);
  // Patch a literal load instruction to load from 'source'.
  void SetImmLLiteral(Instruction* source);

  uintptr_t LiteralAddress() {
    int offset = ImmLLiteral() * kLoadLiteralScale;
    return reinterpret_cast<uintptr_t>(this) + offset;
  }

  enum CheckAlignment { NO_CHECK, CHECK_ALIGNMENT };

  V8_INLINE const Instruction* InstructionAtOffset(
      int64_t offset, CheckAlignment check = CHECK_ALIGNMENT) const {
    // The FUZZ_disasm test relies on no check being done.
    DCHECK(check == NO_CHECK || IsAligned(offset, kInstrSize));
    return this + offset;
  }

  V8_INLINE Instruction* InstructionAtOffset(
      int64_t offset, CheckAlignment check = CHECK_ALIGNMENT) {
    // The FUZZ_disasm test relies on no check being done.
    DCHECK(check == NO_CHECK || IsAligned(offset, kInstrSize));
    return this + offset;
  }

  template <typename T>
  V8_INLINE static Instruction* Cast(T src) {
    return reinterpret_cast<Instruction*>(src);
  }

  V8_INLINE ptrdiff_t DistanceTo(Instruction* target) {
    return reinterpret_cast<Address>(target) - reinterpret_cast<Address>(this);
  }

  static const int ImmPCRelRangeBitwidth = 21;
  static bool IsValidPCRelOffset(ptrdiff_t offset) { return is_int21(offset); }
  void SetPCRelImmTarget(Zone* zone, AssemblerOptions options,
                         Instruction* target);

  template <ImmBranchType branch_type>
  void SetBranchImmTarget(Instruction* target,
                          WritableJitAllocation* jit_allocation = nullptr) {
    DCHECK(IsAligned(DistanceTo(target), kInstrSize));
    DCHECK(IsValidImmPCOffset(branch_type, DistanceTo(target)));
    int offset = static_cast<int>(DistanceTo(target) >> kInstrSizeLog2);
    Instr branch_imm = 0;
    uint32_t imm_mask = 0;
    switch (branch_type) {
      case CondBranchType:
      case CompareBranchType:
        static_assert(ImmCondBranch_mask == ImmCmpBranch_mask);
        static_assert(ImmCondBranch_offset == ImmCmpBranch_offset);
        // We use a checked truncation here to catch certain bugs where we fail
        // to check whether a veneer is required. See e.g. crbug.com/1485829.
        branch_imm = checked_truncate_to_int19(offset) << ImmCondBranch_offset;
        imm_mask = ImmCondBranch_mask;
        break;
      case UncondBranchType:
        branch_imm = checked_truncate_to_int26(offset)
                     << ImmUncondBranch_offset;
        imm_mask = ImmUncondBranch_mask;
        break;
      case TestBranchType:
        branch_imm = checked_truncate_to_int14(offset) << ImmTestBranch_offset;
        imm_mask = ImmTestBranch_mask;
        break;
      default:
        UNREACHABLE();
    }
    SetInstructionBits(Mask(~imm_mask) | branch_imm, jit_allocation);
  }
};

// Simulator/Debugger debug instructions ---------------------------------------
// Each debug marker is represented by a HLT instruction. The immediate comment
// field in the instruction is used to identify the type of debug marker. Each
// marker encodes arguments in a different way, as described below.

// Indicate to the Debugger that the instruction is a redirected call.
const Instr kImmExceptionIsRedirectedCall = 0xca11;

// Represent unreachable code. This is used as a guard in parts of the code that
// should not be reachable, such as in data encoded inline in the instructions.
const Instr kImmExceptionIsUnreachable = 0xdebf;

// Indicate that the stack is being switched, so the simulator must update its
// stack limit. The new stack limit is passed in x16.
const Instr kImmExceptionIsSwitchStackLimit = 0x5915;

// A pseudo 'printf' instruction. The arguments will be passed to the platform
// printf method.
const Instr kImmExceptionIsPrintf = 0xdeb1;
// Most parameters are stored in ARM64 registers as if the printf
// pseudo-instruction was a call to the real printf method:
//      x0: The format string.
//   x1-x7: Optional arguments.
//   d0-d7: Optional arguments.
//
// Also, the argument layout is described inline in the instructions:
//  - arg_count: The number of arguments.
//  - arg_pattern: A set of PrintfArgPattern values, packed into two-bit fields.
//
// Floating-point and integer arguments are passed in separate sets of registers
// in AAPCS64 (even for varargs functions), so it is not possible to determine
// the type of each argument without some information about the values that were
// passed in. This information could be retrieved from the printf format string,
// but the format string is not trivial to parse so we encode the relevant
// information with the HLT instruction.
const unsigned kPrintfArgCountOffset = 1 * kInstrSize;
const unsigned kPrintfArgPatternListOffset = 2 * kInstrSize;
const unsigned kPrintfLength = 3 * kInstrSize;

const unsigned kPrintfMaxArgCount = 4;

// The argument pattern is a set of two-bit-fields, each with one of the
// following values:
enum PrintfArgPattern {
  kPrintfArgW = 1,
  kPrintfArgX = 2,
  // There is no kPrintfArgS because floats are always converted to doubles in C
  // varargs calls.
  kPrintfArgD = 3
};
static const unsigned kPrintfArgPatternBits = 2;

// A pseudo 'debug' instruction.
const Instr kImmExceptionIsDebug = 0xdeb0;
// Parameters are inlined in the code after a debug pseudo-instruction:
// - Debug code.
// - Debug parameters.
// - Debug message string. This is a nullptr-terminated ASCII string, padded to
//   kInstrSize so that subsequent instructions are correctly aligned.
// - A kImmExceptionIsUnreachable marker, to catch accidental execution of the
//   string data.
const unsigned kDebugCodeOffset = 1 * kInstrSize;
const unsigned kDebugParamsOffset = 2 * kInstrSize;
const unsigned kDebugMessageOffset = 3 * kInstrSize;

// Debug parameters.
// Used without a TRACE_ option, the Debugger will print the arguments only
// once. Otherwise TRACE_ENABLE and TRACE_DISABLE will enable or disable tracing
// before every instruction for the specified LOG_ parameters.
//
// TRACE_OVERRIDE enables the specified LOG_ parameters, and disabled any
// others that were not specified.
//
// For example:
//
// __ debug("print registers and fp registers", 0, LOG_REGS | LOG_VREGS);
// will print the registers and fp registers only once.
//
// __ debug("trace disasm", 1, TRACE_ENABLE | LOG_DISASM);
// starts disassembling the code.
//
// __ debug("trace rets", 2, TRACE_ENABLE | LOG_REGS);
// adds the general purpose registers to the trace.
//
// __ debug("stop regs", 3, TRACE_DISABLE | LOG_REGS);
// stops tracing the registers.
const unsigned kDebuggerTracingDirectivesMask = 3 << 6;
enum DebugParameters {
  NO_PARAM = 0,
  BREAK = 1 << 0,
  LOG_DISASM = 1 << 1,    // Use only with TRACE. Disassemble the code.
  LOG_REGS = 1 << 2,      // Log general purpose registers.
  LOG_VREGS = 1 << 3,     // Log NEON and floating-point registers.
  LOG_SYS_REGS = 1 << 4,  // Log the status flags.
  LOG_WRITE = 1 << 5,     // Log any memory write.

  LOG_NONE = 0,
  LOG_STATE = LOG_REGS | LOG_VREGS | LOG_SYS_REGS,
  LOG_ALL = LOG_DISASM | LOG_STATE | LOG_WRITE,

  // Trace control.
  TRACE_ENABLE = 1 << 6,
  TRACE_DISABLE = 2 << 6,
  TRACE_OVERRIDE = 3 << 6
};

enum NEONFormat {
  NF_UNDEF = 0,
  NF_8B = 1,
  NF_16B = 2,
  NF_4H = 3,
  NF_8H = 4,
  NF_2S = 5,
  NF_4S = 6,
  NF_1D = 7,
  NF_2D = 8,
  NF_B = 9,
  NF_H = 10,
  NF_S = 11,
  NF_D = 12
};

static const unsigned kNEONFormatMaxBits = 6;

struct NEONFormatMap {
  // The bit positions in the instruction to consider.
  uint8_t bits[kNEONFormatMaxBits];

  // Mapping from concatenated bits to format.
  NEONFormat map[1 << kNEONFormatMaxBits];
};

class NEONFormatDecoder {
 public:
  enum SubstitutionMode { kPlaceholder, kFormat };

  // Construct a format decoder with increasingly specific format maps for each
  // substitution. If no format map is specified, the default is the integer
  // format map.
  explicit NEONFormatDecoder(const Instruction* instr);
  NEONFormatDecoder(const Instruction* instr, const NEONFormatMap* format);
  NEONFormatDecoder(const Instruction* instr, const NEONFormatMap* format0,
                    const NEONFormatMap* format1);
  NEONFormatDecoder(const Instruction* instr, const NEONFormatMap* format0,
                    const NEONFormatMap* format1, const NEONFormatMap* format2);

  // Set the format mapping for all or individual substitutions.
  void SetFormatMaps(const NEONFormatMap* format0,
                     const NEONFormatMap* format1 = nullptr,
                     const NEONFormatMap* format2 = nullptr);
  void SetFormatMap(unsigned index, const NEONFormatMap* format);

  // Substitute %s in the input string with the placeholder string for each
  // register, ie. "'B", "'H", etc.
  const char* SubstitutePlaceholders(const char* string);

  // Substitute %s in the input string with a new string based on the
  // substitution mode.
  const char* Substitute(const char* string, SubstitutionMode mode0 = kFormat,
                         SubstitutionMode mode1 = kFormat,
                         SubstitutionMode mode2 = kFormat,
                         SubstitutionMode mode3 = kFormat);

  // Append a "2" to a mnemonic string based of the state of the Q bit.
  const char* Mnemonic(const char* mnemonic);

  VectorFormat GetVectorFormat(int format_index = 0);
  VectorFormat GetVectorFormat(const NEONFormatMap* format_map);

  // Built in mappings for common cases.

  // The integer format map uses three bits (Q, size<1:0>) to encode the
  // "standard" set of NEON integer vector formats.
  static const NEONFormatMap* IntegerFormatMap() {
    static const NEONFormatMap map = {
        {23, 22, 30},
        {NF_8B, NF_16B, NF_4H, NF_8H, NF_2S, NF_4S, NF_UNDEF, NF_2D}};
    return &map;
  }

  // The long integer format map uses two bits (size<1:0>) to encode the
  // long set of NEON integer vector formats. These are used in narrow, wide
  // and long operations.
  static const NEONFormatMap* LongIntegerFormatMap() {
    static const NEONFormatMap map = {{23, 22}, {NF_8H, NF_4S, NF_2D}};
    return &map;
  }

  // The FP format map uses two bits (Q, size<0>) to encode the NEON FP vector
  // formats: NF_2S, NF_4S, NF_2D.
  static const NEONFormatMap* FPFormatMap() {
    // The FP format map assumes two bits (Q, size<0>) are used to encode the
    // NEON FP vector formats: NF_2S, NF_4S, NF_2D.
    static const NEONFormatMap map = {{22, 30},
                                      {NF_2S, NF_4S, NF_UNDEF, NF_2D}};
    return &map;
  }

  // The FP half-precision format map uses one Q bit to encode the
  // NEON FP vector formats: NF_4H, NF_8H.
  static const NEONFormatMap* FPHPFormatMap() {
    static const NEONFormatMap map = {{30}, {NF_4H, NF_8H}};
    return &map;
  }

  // The load/store format map uses three bits (Q, 11, 10) to encode the
  // set of NEON vector formats.
  static const NEONFormatMap* LoadStoreFormatMap() {
    static const NEONFormatMap map = {
        {11, 10, 30},
        {NF_8B, NF_16B, NF_4H, NF_8H, NF_2S, NF_4S, NF_1D, NF_2D}};
    return &map;
  }

  // The logical format map uses one bit (Q) to encode the NEON vector format:
  // NF_8B, NF_16B.
  static const NEONFormatMap* LogicalFormatMap() {
    static const NEONFormatMap map = {{30}, {NF_8B, NF_16B}};
    return &map;
  }

  // The triangular format map uses between two and five bits to encode the NEON
  // vector format:
  // xxx10->8B, xxx11->16B, xx100->4H, xx101->8H
  // x1000->2S, x1001->4S,  10001->2D, all others undefined.
  static const NEONFormatMap* TriangularFormatMap() {
    static const NEONFormatMap map = {
        {19, 18, 17, 16, 30},
        {NF_UNDEF, NF_UNDEF, NF_8B, NF_16B, NF_4H, NF_8H, NF_8B, NF_16B,
         NF_2S,    NF_4S,    NF_8B, NF_16B, NF_4H, NF_8H, NF_8B, NF_16B,
         NF_UNDEF, NF_2D,    NF_8B, NF_16B, NF_4H, NF_8H, NF_8B, NF_16B,
         NF_2S,    NF_4S,    NF_8B, NF_16B, NF_4H, NF_8H, NF_8B, NF_16B}};
    return &map;
  }

  // The scalar format map uses two bits (size<1:0>) to encode the NEON scalar
  // formats: NF_B, NF_H, NF_S, NF_D.
  static const NEONFormatMap* ScalarFormatMap() {
    static const NEONFormatMap map = {{23, 22}, {NF_B, NF_H, NF_S, NF_D}};
    return &map;
  }

  // The long scalar format map uses two bits (size<1:0>) to encode the longer
  // NEON scalar formats: NF_H, NF_S, NF_D.
  static const NEONFormatMap* LongScalarFormatMap() {
    static const NEONFormatMap map = {{23, 22}, {NF_H, NF_S, NF_D}};
    return &map;
  }

  // The FP scalar format map assumes one bit (size<0>) is used to encode the
  // NEON FP scalar formats: NF_S, NF_D.
  static const NEONFormatMap* FPScalarFormatMap() {
    static const NEONFormatMap map = {{22}, {NF_S, NF_D}};
    return &map;
  }

  // The triangular scalar format map uses between one and four bits to encode
  // the NEON FP scalar formats:
  // xxx1->B, xx10->H, x100->S, 1000->D, all others undefined.
  static const NEONFormatMap* TriangularScalarFormatMap() {
    static const NEONFormatMap map = {
        {19, 18, 17, 16},
        {NF_UNDEF, NF_B, NF_H, NF_B, NF_S, NF_B, NF_H, NF_B, NF_D, NF_B, NF_H,
         NF_B, NF_S, NF_B, NF_H, NF_B}};
    return &map;
  }

 private:
  // Get a pointer to a string that represents the format or placeholder for
  // the specified substitution index, based on the format map and instruction.
  const char* GetSubstitute(int index, SubstitutionMode mode);

  // Get the NEONFormat enumerated value for bits obtained from the
  // instruction based on the specified format mapping.
  NEONFormat GetNEONFormat(const NEONFormatMap* format_map);

  // Convert a NEONFormat into a string.
  static const char* NEONFormatAsString(NEONFormat format);

  // Convert a NEONFormat into a register placeholder string.
  static const char* NEONFormatAsPlaceholder(NEONFormat format);

  // Select bits from instrbits_ defined by the bits array, concatenate them,
  // and return the value.
  uint8_t PickBits(const uint8_t bits[]);

  Instr instrbits_;
  const NEONFormatMap* formats_[4];
  char form_buffer_[64];
  char mne_buffer_[16];
};
}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM64_INSTRUCTIONS_ARM64_H_
