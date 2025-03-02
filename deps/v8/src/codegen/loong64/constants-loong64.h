// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_LOONG64_CONSTANTS_LOONG64_H_
#define V8_CODEGEN_LOONG64_CONSTANTS_LOONG64_H_

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/common/code-memory-access.h"
#include "src/common/globals.h"

// Get the standard printf format macros for C99 stdint types.
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

// Defines constants and accessor classes to assemble, disassemble and
// simulate LOONG64 instructions.

namespace v8 {
namespace internal {

constexpr size_t kMaxPCRelativeCodeRangeInMB = 128;

// -----------------------------------------------------------------------------
// Registers and FPURegisters.

// Number of general purpose registers.
const int kNumRegisters = 32;
const int kInvalidRegister = -1;

// Number of registers with pc.
const int kNumSimuRegisters = 33;

// In the simulator, the PC register is simulated as the 33th register.
const int kPCRegister = 32;

// Number of floating point registers.
const int kNumFPURegisters = 32;
const int kInvalidFPURegister = -1;

// FPU control registers.
const int kFCSRRegister = 0;
const int kInvalidFPUControlRegister = -1;
const uint32_t kFPUInvalidResult = static_cast<uint32_t>(1u << 31) - 1;
const int32_t kFPUInvalidResultNegative = static_cast<int32_t>(1u << 31);
const uint64_t kFPU64InvalidResult =
    static_cast<uint64_t>(static_cast<uint64_t>(1) << 63) - 1;
const int64_t kFPU64InvalidResultNegative =
    static_cast<int64_t>(static_cast<uint64_t>(1) << 63);

// FCSR constants.
const uint32_t kFCSRInexactCauseBit = 24;
const uint32_t kFCSRUnderflowCauseBit = 25;
const uint32_t kFCSROverflowCauseBit = 26;
const uint32_t kFCSRDivideByZeroCauseBit = 27;
const uint32_t kFCSRInvalidOpCauseBit = 28;

const uint32_t kFCSRInexactCauseMask = 1 << kFCSRInexactCauseBit;
const uint32_t kFCSRUnderflowCauseMask = 1 << kFCSRUnderflowCauseBit;
const uint32_t kFCSROverflowCauseMask = 1 << kFCSROverflowCauseBit;
const uint32_t kFCSRDivideByZeroCauseMask = 1 << kFCSRDivideByZeroCauseBit;
const uint32_t kFCSRInvalidOpCauseMask = 1 << kFCSRInvalidOpCauseBit;

const uint32_t kFCSRCauseMask =
    kFCSRInexactCauseMask | kFCSRUnderflowCauseMask | kFCSROverflowCauseMask |
    kFCSRDivideByZeroCauseMask | kFCSRInvalidOpCauseMask;

const uint32_t kFCSRExceptionCauseMask = kFCSRCauseMask ^ kFCSRInexactCauseMask;

// Actual value of root register is offset from the root array's start
// to take advantage of negative displacement values.
constexpr int kRootRegisterBias = 256;

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

  static const int64_t kMaxValue = 0x7fffffffffffffffl;
  static const int64_t kMinValue = 0x8000000000000000l;

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

// -----------------------------------------------------------------------------
// Instructions encoding constants.

// On LoongArch all instructions are 32 bits.
using Instr = int32_t;

// Special Software Interrupt codes when used in the presence of the LOONG64
// simulator.
enum SoftwareInterruptCodes {
  // Transition to C code.
  call_rt_redirected = 0x7fff
};

// On LOONG64 Simulator breakpoints can have different codes:
// - Breaks between 0 and kMaxWatchpointCode are treated as simple watchpoints,
//   the simulator will run through them and print the registers.
// - Breaks between kMaxWatchpointCode and kMaxStopCode are treated as stop()
//   instructions (see Assembler::stop()).
// - Breaks larger than kMaxStopCode are simple breaks, dropping you into the
//   debugger.
const uint32_t kMaxWatchpointCode = 31;
const uint32_t kMaxStopCode = 127;
static_assert(kMaxWatchpointCode < kMaxStopCode);

// ----- Fields offset and length.
const int kRjShift = 5;
const int kRjBits = 5;
const int kRkShift = 10;
const int kRkBits = 5;
const int kRdShift = 0;
const int kRdBits = 5;
const int kSaShift = 15;
const int kSa2Bits = 2;
const int kSa3Bits = 3;
const int kCdShift = 0;
const int kCdBits = 3;
const int kCjShift = 5;
const int kCjBits = 3;
const int kCodeShift = 0;
const int kCodeBits = 15;
const int kCondShift = 15;
const int kCondBits = 5;
const int kUi5Shift = 10;
const int kUi5Bits = 5;
const int kUi6Shift = 10;
const int kUi6Bits = 6;
const int kUi12Shift = 10;
const int kUi12Bits = 12;
const int kSi12Shift = 10;
const int kSi12Bits = 12;
const int kSi14Shift = 10;
const int kSi14Bits = 14;
const int kSi16Shift = 10;
const int kSi16Bits = 16;
const int kSi20Shift = 5;
const int kSi20Bits = 20;
const int kMsbwShift = 16;
const int kMsbwBits = 5;
const int kLsbwShift = 10;
const int kLsbwBits = 5;
const int kMsbdShift = 16;
const int kMsbdBits = 6;
const int kLsbdShift = 10;
const int kLsbdBits = 6;
const int kFdShift = 0;
const int kFdBits = 5;
const int kFjShift = 5;
const int kFjBits = 5;
const int kFkShift = 10;
const int kFkBits = 5;
const int kFaShift = 15;
const int kFaBits = 5;
const int kCaShift = 15;
const int kCaBits = 3;
const int kHint15Shift = 0;
const int kHint15Bits = 15;
const int kHint5Shift = 0;
const int kHint5Bits = 5;
const int kOffsLowShift = 10;
const int kOffsLowBits = 16;
const int kOffs26HighShift = 0;
const int kOffs26HighBits = 10;
const int kOffs21HighShift = 0;
const int kOffs21HighBits = 5;
const int kImm12Shift = 0;
const int kImm12Bits = 12;
const int kImm16Shift = 0;
const int kImm16Bits = 16;
const int kImm26Shift = 0;
const int kImm26Bits = 26;
const int kImm28Shift = 0;
const int kImm28Bits = 28;
const int kImm32Shift = 0;
const int kImm32Bits = 32;

// ----- Miscellaneous useful masks.
// Instruction bit masks.
const int kRjFieldMask = ((1 << kRjBits) - 1) << kRjShift;
const int kRkFieldMask = ((1 << kRkBits) - 1) << kRkShift;
const int kRdFieldMask = ((1 << kRdBits) - 1) << kRdShift;
const int kSa2FieldMask = ((1 << kSa2Bits) - 1) << kSaShift;
const int kSa3FieldMask = ((1 << kSa3Bits) - 1) << kSaShift;
// Misc masks.
const int kHiMaskOf32 = 0xffff << 16;  // Only to be used with 32-bit values
const int kLoMaskOf32 = 0xffff;
const int kSignMaskOf32 = 0x80000000;  // Only to be used with 32-bit values
const int64_t kTop16MaskOf64 = (int64_t)0xffff << 48;
const int64_t kHigher16MaskOf64 = (int64_t)0xffff << 32;
const int64_t kUpper16MaskOf64 = (int64_t)0xffff << 16;

const int kImm12Mask = ((1 << kImm12Bits) - 1) << kImm12Shift;
const int kImm16Mask = ((1 << kImm16Bits) - 1) << kImm16Shift;
const int kImm26Mask = ((1 << kImm26Bits) - 1) << kImm26Shift;
const int kImm28Mask = ((1 << kImm28Bits) - 1) << kImm28Shift;

// ----- LOONG64 Opcodes and Function Fields.
enum Opcode : uint32_t {
  BEQZ = 0x10U << 26,
  BNEZ = 0x11U << 26,
  BCZ = 0x12U << 26,  // BCEQZ & BCNEZ
  JIRL = 0x13U << 26,
  B = 0x14U << 26,
  BL = 0x15U << 26,
  BEQ = 0x16U << 26,
  BNE = 0x17U << 26,
  BLT = 0x18U << 26,
  BGE = 0x19U << 26,
  BLTU = 0x1aU << 26,
  BGEU = 0x1bU << 26,

  ADDU16I_D = 0x4U << 26,

  LU12I_W = 0xaU << 25,
  LU32I_D = 0xbU << 25,
  PCADDI = 0xcU << 25,
  PCALAU12I = 0xdU << 25,
  PCADDU12I = 0xeU << 25,
  PCADDU18I = 0xfU << 25,

  LL_W = 0x20U << 24,
  SC_W = 0x21U << 24,
  LL_D = 0x22U << 24,
  SC_D = 0x23U << 24,
  LDPTR_W = 0x24U << 24,
  STPTR_W = 0x25U << 24,
  LDPTR_D = 0x26U << 24,
  STPTR_D = 0x27U << 24,

  BSTR_W = 0x1U << 22,  // BSTRINS_W & BSTRPICK_W
  BSTRINS_W = BSTR_W,
  BSTRPICK_W = BSTR_W,
  BSTRINS_D = 0x2U << 22,
  BSTRPICK_D = 0x3U << 22,

  SLTI = 0x8U << 22,
  SLTUI = 0x9U << 22,
  ADDI_W = 0xaU << 22,
  ADDI_D = 0xbU << 22,
  LU52I_D = 0xcU << 22,
  ANDI = 0xdU << 22,
  ORI = 0xeU << 22,
  XORI = 0xfU << 22,

  LD_B = 0xa0U << 22,
  LD_H = 0xa1U << 22,
  LD_W = 0xa2U << 22,
  LD_D = 0xa3U << 22,
  ST_B = 0xa4U << 22,
  ST_H = 0xa5U << 22,
  ST_W = 0xa6U << 22,
  ST_D = 0xa7U << 22,
  LD_BU = 0xa8U << 22,
  LD_HU = 0xa9U << 22,
  LD_WU = 0xaaU << 22,
  FLD_S = 0xacU << 22,
  FST_S = 0xadU << 22,
  FLD_D = 0xaeU << 22,
  FST_D = 0xafU << 22,

  FMADD_S = 0x81U << 20,
  FMADD_D = 0x82U << 20,
  FMSUB_S = 0x85U << 20,
  FMSUB_D = 0x86U << 20,
  FNMADD_S = 0x89U << 20,
  FNMADD_D = 0x8aU << 20,
  FNMSUB_S = 0x8dU << 20,
  FNMSUB_D = 0x8eU << 20,
  FCMP_COND_S = 0xc1U << 20,
  FCMP_COND_D = 0xc2U << 20,

  BYTEPICK_D = 0x3U << 18,
  BYTEPICK_W = 0x2U << 18,

  FSEL = 0x340U << 18,

  ALSL = 0x1U << 18,
  ALSL_W = ALSL,
  ALSL_WU = ALSL,

  ALSL_D = 0xbU << 18,

  SLLI_W = 0x40U << 16,
  SRLI_W = 0x44U << 16,
  SRAI_W = 0x48U << 16,
  ROTRI_W = 0x4cU << 16,

  SLLI_D = 0x41U << 16,
  SRLI_D = 0x45U << 16,
  SRAI_D = 0x49U << 16,
  ROTRI_D = 0x4dU << 16,

  SLLI = 0x10U << 18,
  SRLI = 0x11U << 18,
  SRAI = 0x12U << 18,
  ROTRI = 0x13U << 18,

  ADD_W = 0x20U << 15,
  ADD_D = 0x21U << 15,
  SUB_W = 0x22U << 15,
  SUB_D = 0x23U << 15,
  SLT = 0x24U << 15,
  SLTU = 0x25U << 15,
  MASKEQZ = 0x26U << 15,
  MASKNEZ = 0x27U << 15,
  NOR = 0x28U << 15,
  AND = 0x29U << 15,
  OR = 0x2aU << 15,
  XOR = 0x2bU << 15,
  ORN = 0x2cU << 15,
  ANDN = 0x2dU << 15,
  SLL_W = 0x2eU << 15,
  SRL_W = 0x2fU << 15,
  SRA_W = 0x30U << 15,
  SLL_D = 0x31U << 15,
  SRL_D = 0x32U << 15,
  SRA_D = 0x33U << 15,
  ROTR_W = 0x36U << 15,
  ROTR_D = 0x37U << 15,
  MUL_W = 0x38U << 15,
  MULH_W = 0x39U << 15,
  MULH_WU = 0x3aU << 15,
  MUL_D = 0x3bU << 15,
  MULH_D = 0x3cU << 15,
  MULH_DU = 0x3dU << 15,
  MULW_D_W = 0x3eU << 15,
  MULW_D_WU = 0x3fU << 15,

  DIV_W = 0x40U << 15,
  MOD_W = 0x41U << 15,
  DIV_WU = 0x42U << 15,
  MOD_WU = 0x43U << 15,
  DIV_D = 0x44U << 15,
  MOD_D = 0x45U << 15,
  DIV_DU = 0x46U << 15,
  MOD_DU = 0x47U << 15,

  BREAK = 0x54U << 15,

  FADD_S = 0x201U << 15,
  FADD_D = 0x202U << 15,
  FSUB_S = 0x205U << 15,
  FSUB_D = 0x206U << 15,
  FMUL_S = 0x209U << 15,
  FMUL_D = 0x20aU << 15,
  FDIV_S = 0x20dU << 15,
  FDIV_D = 0x20eU << 15,
  FMAX_S = 0x211U << 15,
  FMAX_D = 0x212U << 15,
  FMIN_S = 0x215U << 15,
  FMIN_D = 0x216U << 15,
  FMAXA_S = 0x219U << 15,
  FMAXA_D = 0x21aU << 15,
  FMINA_S = 0x21dU << 15,
  FMINA_D = 0x21eU << 15,
  FSCALEB_S = 0x221U << 15,
  FSCALEB_D = 0x222U << 15,
  FCOPYSIGN_S = 0x225U << 15,
  FCOPYSIGN_D = 0x226U << 15,

  LDX_B = 0x7000U << 15,
  LDX_H = 0x7008U << 15,
  LDX_W = 0x7010U << 15,
  LDX_D = 0x7018U << 15,
  STX_B = 0x7020U << 15,
  STX_H = 0x7028U << 15,
  STX_W = 0x7030U << 15,
  STX_D = 0x7038U << 15,
  LDX_BU = 0x7040U << 15,
  LDX_HU = 0x7048U << 15,
  LDX_WU = 0x7050U << 15,
  FLDX_S = 0x7060U << 15,
  FLDX_D = 0x7068U << 15,
  FSTX_S = 0x7070U << 15,
  FSTX_D = 0x7078U << 15,

  AMSWAP_W = 0x70c0U << 15,
  AMSWAP_D = 0x70c1U << 15,
  AMADD_W = 0x70c2U << 15,
  AMADD_D = 0x70c3U << 15,
  AMAND_W = 0x70c4U << 15,
  AMAND_D = 0x70c5U << 15,
  AMOR_W = 0x70c6U << 15,
  AMOR_D = 0x70c7U << 15,
  AMXOR_W = 0x70c8U << 15,
  AMXOR_D = 0x70c9U << 15,
  AMMAX_W = 0x70caU << 15,
  AMMAX_D = 0x70cbU << 15,
  AMMIN_W = 0x70ccU << 15,
  AMMIN_D = 0x70cdU << 15,
  AMMAX_WU = 0x70ceU << 15,
  AMMAX_DU = 0x70cfU << 15,
  AMMIN_WU = 0x70d0U << 15,
  AMMIN_DU = 0x70d1U << 15,
  AMSWAP_DB_W = 0x70d2U << 15,
  AMSWAP_DB_D = 0x70d3U << 15,
  AMADD_DB_W = 0x70d4U << 15,
  AMADD_DB_D = 0x70d5U << 15,
  AMAND_DB_W = 0x70d6U << 15,
  AMAND_DB_D = 0x70d7U << 15,
  AMOR_DB_W = 0x70d8U << 15,
  AMOR_DB_D = 0x70d9U << 15,
  AMXOR_DB_W = 0x70daU << 15,
  AMXOR_DB_D = 0x70dbU << 15,
  AMMAX_DB_W = 0x70dcU << 15,
  AMMAX_DB_D = 0x70ddU << 15,
  AMMIN_DB_W = 0x70deU << 15,
  AMMIN_DB_D = 0x70dfU << 15,
  AMMAX_DB_WU = 0x70e0U << 15,
  AMMAX_DB_DU = 0x70e1U << 15,
  AMMIN_DB_WU = 0x70e2U << 15,
  AMMIN_DB_DU = 0x70e3U << 15,

  DBAR = 0x70e4U << 15,
  IBAR = 0x70e5U << 15,

  CLO_W = 0X4U << 10,
  CLZ_W = 0X5U << 10,
  CTO_W = 0X6U << 10,
  CTZ_W = 0X7U << 10,
  CLO_D = 0X8U << 10,
  CLZ_D = 0X9U << 10,
  CTO_D = 0XaU << 10,
  CTZ_D = 0XbU << 10,
  REVB_2H = 0XcU << 10,
  REVB_4H = 0XdU << 10,
  REVB_2W = 0XeU << 10,
  REVB_D = 0XfU << 10,
  REVH_2W = 0X10U << 10,
  REVH_D = 0X11U << 10,
  BITREV_4B = 0X12U << 10,
  BITREV_8B = 0X13U << 10,
  BITREV_W = 0X14U << 10,
  BITREV_D = 0X15U << 10,
  EXT_W_H = 0X16U << 10,
  EXT_W_B = 0X17U << 10,

  FABS_S = 0X4501U << 10,
  FABS_D = 0X4502U << 10,
  FNEG_S = 0X4505U << 10,
  FNEG_D = 0X4506U << 10,
  FLOGB_S = 0X4509U << 10,
  FLOGB_D = 0X450aU << 10,
  FCLASS_S = 0X450dU << 10,
  FCLASS_D = 0X450eU << 10,
  FSQRT_S = 0X4511U << 10,
  FSQRT_D = 0X4512U << 10,
  FRECIP_S = 0X4515U << 10,
  FRECIP_D = 0X4516U << 10,
  FRSQRT_S = 0X4519U << 10,
  FRSQRT_D = 0X451aU << 10,
  FMOV_S = 0X4525U << 10,
  FMOV_D = 0X4526U << 10,
  MOVGR2FR_W = 0X4529U << 10,
  MOVGR2FR_D = 0X452aU << 10,
  MOVGR2FRH_W = 0X452bU << 10,
  MOVFR2GR_S = 0X452dU << 10,
  MOVFR2GR_D = 0X452eU << 10,
  MOVFRH2GR_S = 0X452fU << 10,
  MOVGR2FCSR = 0X4530U << 10,
  MOVFCSR2GR = 0X4532U << 10,
  MOVFR2CF = 0X4534U << 10,
  MOVGR2CF = 0X4536U << 10,

  FCVT_S_D = 0x4646U << 10,
  FCVT_D_S = 0x4649U << 10,
  FTINTRM_W_S = 0x4681U << 10,
  FTINTRM_W_D = 0x4682U << 10,
  FTINTRM_L_S = 0x4689U << 10,
  FTINTRM_L_D = 0x468aU << 10,
  FTINTRP_W_S = 0x4691U << 10,
  FTINTRP_W_D = 0x4692U << 10,
  FTINTRP_L_S = 0x4699U << 10,
  FTINTRP_L_D = 0x469aU << 10,
  FTINTRZ_W_S = 0x46a1U << 10,
  FTINTRZ_W_D = 0x46a2U << 10,
  FTINTRZ_L_S = 0x46a9U << 10,
  FTINTRZ_L_D = 0x46aaU << 10,
  FTINTRNE_W_S = 0x46b1U << 10,
  FTINTRNE_W_D = 0x46b2U << 10,
  FTINTRNE_L_S = 0x46b9U << 10,
  FTINTRNE_L_D = 0x46baU << 10,
  FTINT_W_S = 0x46c1U << 10,
  FTINT_W_D = 0x46c2U << 10,
  FTINT_L_S = 0x46c9U << 10,
  FTINT_L_D = 0x46caU << 10,
  FFINT_S_W = 0x4744U << 10,
  FFINT_S_L = 0x4746U << 10,
  FFINT_D_W = 0x4748U << 10,
  FFINT_D_L = 0x474aU << 10,
  FRINT_S = 0x4791U << 10,
  FRINT_D = 0x4792U << 10,

  MOVCF2FR = 0x4535U << 10,
  MOVCF2GR = 0x4537U << 10
};

// ----- Emulated conditions.
// On LOONG64 we use this enum to abstract from conditional branch instructions.
// The 'U' prefix is used to specify unsigned comparisons.
enum Condition : int {
  overflow = 0,
  no_overflow = 1,
  Uless = 2,
  Ugreater_equal = 3,
  Uless_equal = 4,
  Ugreater = 5,
  equal = 6,
  not_equal = 7,  // Unordered or Not Equal.
  negative = 8,
  positive = 9,
  parity_even = 10,
  parity_odd = 11,
  less = 12,
  greater_equal = 13,
  less_equal = 14,
  greater = 15,
  ueq = 16,  // Unordered or Equal.
  ogl = 17,  // Ordered and Not Equal.
  cc_always = 18,

  // Aliases.
  carry = Uless,
  not_carry = Ugreater_equal,
  zero = equal,
  eq = equal,
  not_zero = not_equal,
  ne = not_equal,
  nz = not_equal,
  sign = negative,
  not_sign = positive,
  mi = negative,
  pl = positive,
  hi = Ugreater,
  ls = Uless_equal,
  ge = greater_equal,
  lt = less,
  gt = greater,
  le = less_equal,
  hs = Ugreater_equal,
  lo = Uless,
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
    case ueq:
      return ogl;
    case ogl:
      return ueq;
    default:
      return cc;
  }
}

// ----- Coprocessor conditions.
enum FPUCondition {
  kNoFPUCondition = -1,

  CAF = 0x00,  // False.
  SAF = 0x01,  // False.
  CLT = 0x02,  // Less Than quiet
               //  SLT  = 0x03,    // Less Than signaling
  CEQ = 0x04,
  SEQ = 0x05,
  CLE = 0x06,
  SLE = 0x07,
  CUN = 0x08,
  SUN = 0x09,
  CULT = 0x0a,
  SULT = 0x0b,
  CUEQ = 0x0c,
  SUEQ = 0x0d,
  CULE = 0x0e,
  SULE = 0x0f,
  CNE = 0x10,
  SNE = 0x11,
  COR = 0x14,
  SOR = 0x15,
  CUNE = 0x18,
  SUNE = 0x19,
};

const uint32_t kFPURoundingModeShift = 8;
const uint32_t kFPURoundingModeMask = 0b11 << kFPURoundingModeShift;

// FPU rounding modes.
enum FPURoundingMode {
  RN = 0b00 << kFPURoundingModeShift,  // Round to Nearest.
  RZ = 0b01 << kFPURoundingModeShift,  // Round towards zero.
  RP = 0b10 << kFPURoundingModeShift,  // Round towards Plus Infinity.
  RM = 0b11 << kFPURoundingModeShift,  // Round towards Minus Infinity.

  // Aliases.
  kRoundToNearest = RN,
  kRoundToZero = RZ,
  kRoundToPlusInf = RP,
  kRoundToMinusInf = RM,

  mode_round = RN,
  mode_ceil = RP,
  mode_floor = RM,
  mode_trunc = RZ
};

enum CheckForInexactConversion {
  kCheckForInexactConversion,
  kDontCheckForInexactConversion
};

enum class MaxMinKind : int { kMin = 0, kMax = 1 };

// -----------------------------------------------------------------------------
// Hints.

// Branch hints are not used on the LOONG64.  They are defined so that they can
// appear in shared function signatures, but will be ignored in LOONG64
// implementations.
enum Hint { no_hint = 0 };

inline Hint NegateHint(Hint hint) { return no_hint; }

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.
// These constants are declared in assembler-loong64.cc, as they use named
// registers and other constants.

// Break 0xfffff, reserved for redirected real time call.
const Instr rtCallRedirInstr =
    static_cast<uint32_t>(BREAK) | call_rt_redirected;
// A nop instruction. (Encoding of addi_w 0 0 0).
const Instr nopInstr = ADDI_W;

constexpr uint8_t kInstrSize = 4;
constexpr uint8_t kInstrSizeLog2 = 2;

class InstructionBase {
 public:
  enum Type {
    kOp6Type,
    kOp7Type,
    kOp8Type,
    kOp10Type,
    kOp12Type,
    kOp14Type,
    kOp17Type,
    kOp22Type,
    kUnsupported = -1
  };

  // Get the raw instruction bits.
  inline Instr InstructionBits() const {
    return *reinterpret_cast<const Instr*>(this);
  }

  // Set the raw instruction bits to value.
  V8_EXPORT_PRIVATE void SetInstructionBits(
      Instr new_instr, WritableJitAllocation* jit_allocation = nullptr);

  // Read one particular bit out of the instruction bits.
  inline int Bit(int nr) const { return (InstructionBits() >> nr) & 1; }

  // Read a bit field out of the instruction bits.
  inline int Bits(int hi, int lo) const {
    return (InstructionBits() >> lo) & ((2U << (hi - lo)) - 1);
  }

  // Safe to call within InstructionType().
  inline int RjFieldRawNoAssert() const {
    return InstructionBits() & kRjFieldMask;
  }

  // Get the encoding type of the instruction.
  inline Type InstructionType() const;

 protected:
  InstructionBase() {}
};

template <class T>
class InstructionGetters : public T {
 public:
  inline int RjValue() const {
    return this->Bits(kRjShift + kRjBits - 1, kRjShift);
  }

  inline int RkValue() const {
    return this->Bits(kRkShift + kRkBits - 1, kRkShift);
  }

  inline int RdValue() const {
    return this->Bits(kRdShift + kRdBits - 1, kRdShift);
  }

  inline int Sa2Value() const {
    return this->Bits(kSaShift + kSa2Bits - 1, kSaShift);
  }

  inline int Sa3Value() const {
    return this->Bits(kSaShift + kSa3Bits - 1, kSaShift);
  }

  inline int Ui5Value() const {
    return this->Bits(kUi5Shift + kUi5Bits - 1, kUi5Shift);
  }

  inline int Ui6Value() const {
    return this->Bits(kUi6Shift + kUi6Bits - 1, kUi6Shift);
  }

  inline int Ui12Value() const {
    return this->Bits(kUi12Shift + kUi12Bits - 1, kUi12Shift);
  }

  inline int LsbwValue() const {
    return this->Bits(kLsbwShift + kLsbwBits - 1, kLsbwShift);
  }

  inline int MsbwValue() const {
    return this->Bits(kMsbwShift + kMsbwBits - 1, kMsbwShift);
  }

  inline int LsbdValue() const {
    return this->Bits(kLsbdShift + kLsbdBits - 1, kLsbdShift);
  }

  inline int MsbdValue() const {
    return this->Bits(kMsbdShift + kMsbdBits - 1, kMsbdShift);
  }

  inline int CondValue() const {
    return this->Bits(kCondShift + kCondBits - 1, kCondShift);
  }

  inline int Si12Value() const {
    return this->Bits(kSi12Shift + kSi12Bits - 1, kSi12Shift);
  }

  inline int Si14Value() const {
    return this->Bits(kSi14Shift + kSi14Bits - 1, kSi14Shift);
  }

  inline int Si16Value() const {
    return this->Bits(kSi16Shift + kSi16Bits - 1, kSi16Shift);
  }

  inline int Si20Value() const {
    return this->Bits(kSi20Shift + kSi20Bits - 1, kSi20Shift);
  }

  inline int FdValue() const {
    return this->Bits(kFdShift + kFdBits - 1, kFdShift);
  }

  inline int FaValue() const {
    return this->Bits(kFaShift + kFaBits - 1, kFaShift);
  }

  inline int FjValue() const {
    return this->Bits(kFjShift + kFjBits - 1, kFjShift);
  }

  inline int FkValue() const {
    return this->Bits(kFkShift + kFkBits - 1, kFkShift);
  }

  inline int CjValue() const {
    return this->Bits(kCjShift + kCjBits - 1, kCjShift);
  }

  inline int CdValue() const {
    return this->Bits(kCdShift + kCdBits - 1, kCdShift);
  }

  inline int CaValue() const {
    return this->Bits(kCaShift + kCaBits - 1, kCaShift);
  }

  inline int CodeValue() const {
    return this->Bits(kCodeShift + kCodeBits - 1, kCodeShift);
  }

  inline int Hint5Value() const {
    return this->Bits(kHint5Shift + kHint5Bits - 1, kHint5Shift);
  }

  inline int Hint15Value() const {
    return this->Bits(kHint15Shift + kHint15Bits - 1, kHint15Shift);
  }

  inline int Offs16Value() const {
    return this->Bits(kOffsLowShift + kOffsLowBits - 1, kOffsLowShift);
  }

  inline int Offs21Value() const {
    int low = this->Bits(kOffsLowShift + kOffsLowBits - 1, kOffsLowShift);
    int high =
        this->Bits(kOffs21HighShift + kOffs21HighBits - 1, kOffs21HighShift);
    return ((high << kOffsLowBits) + low);
  }

  inline int Offs26Value() const {
    int low = this->Bits(kOffsLowShift + kOffsLowBits - 1, kOffsLowShift);
    int high =
        this->Bits(kOffs26HighShift + kOffs26HighBits - 1, kOffs26HighShift);
    return ((high << kOffsLowBits) + low);
  }

  inline int RjFieldRaw() const {
    return this->InstructionBits() & kRjFieldMask;
  }

  inline int RkFieldRaw() const {
    return this->InstructionBits() & kRkFieldMask;
  }

  inline int RdFieldRaw() const {
    return this->InstructionBits() & kRdFieldMask;
  }

  inline int32_t ImmValue(int bits) const { return this->Bits(bits - 1, 0); }

  /*TODO*/
  inline int32_t Imm12Value() const { abort(); }

  inline int32_t Imm14Value() const { abort(); }

  inline int32_t Imm16Value() const { abort(); }

  // Say if the instruction is a break.
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
// LOONG64 assembly various constants.

const int kInvalidStackOffset = -1;

static const int kNegOffset = 0x00008000;

InstructionBase::Type InstructionBase::InstructionType() const {
  InstructionBase::Type kType = kUnsupported;

  // Check for kOp6Type
  switch (Bits(31, 26) << 26) {
    case ADDU16I_D:
    case BEQZ:
    case BNEZ:
    case BCZ:
    case JIRL:
    case B:
    case BL:
    case BEQ:
    case BNE:
    case BLT:
    case BGE:
    case BLTU:
    case BGEU:
      kType = kOp6Type;
      break;
    default:
      kType = kUnsupported;
  }

  if (kType == kUnsupported) {
    // Check for kOp7Type
    switch (Bits(31, 25) << 25) {
      case LU12I_W:
      case LU32I_D:
      case PCADDI:
      case PCALAU12I:
      case PCADDU12I:
      case PCADDU18I:
        kType = kOp7Type;
        break;
      default:
        kType = kUnsupported;
    }
  }

  if (kType == kUnsupported) {
    // Check for kOp8Type
    switch (Bits(31, 24) << 24) {
      case LDPTR_W:
      case STPTR_W:
      case LDPTR_D:
      case STPTR_D:
      case LL_W:
      case SC_W:
      case LL_D:
      case SC_D:
        kType = kOp8Type;
        break;
      default:
        kType = kUnsupported;
    }
  }

  if (kType == kUnsupported) {
    // Check for kOp10Type
    switch (Bits(31, 22) << 22) {
      case BSTR_W: {
        // If Bit(21) = 0, then the Opcode is not BSTR_W.
        if (Bit(21) == 0)
          kType = kUnsupported;
        else
          kType = kOp10Type;
        break;
      }
      case BSTRINS_D:
      case BSTRPICK_D:
      case SLTI:
      case SLTUI:
      case ADDI_W:
      case ADDI_D:
      case LU52I_D:
      case ANDI:
      case ORI:
      case XORI:
      case LD_B:
      case LD_H:
      case LD_W:
      case LD_D:
      case ST_B:
      case ST_H:
      case ST_W:
      case ST_D:
      case LD_BU:
      case LD_HU:
      case LD_WU:
      case FLD_S:
      case FST_S:
      case FLD_D:
      case FST_D:
        kType = kOp10Type;
        break;
      default:
        kType = kUnsupported;
    }
  }

  if (kType == kUnsupported) {
    // Check for kOp12Type
    switch (Bits(31, 20) << 20) {
      case FMADD_S:
      case FMADD_D:
      case FMSUB_S:
      case FMSUB_D:
      case FNMADD_S:
      case FNMADD_D:
      case FNMSUB_S:
      case FNMSUB_D:
      case FCMP_COND_S:
      case FCMP_COND_D:
      case FSEL:
        kType = kOp12Type;
        break;
      default:
        kType = kUnsupported;
    }
  }

  if (kType == kUnsupported) {
    // Check for kOp14Type
    switch (Bits(31, 18) << 18) {
      case ALSL:
      case BYTEPICK_W:
      case BYTEPICK_D:
      case ALSL_D:
      case SLLI:
      case SRLI:
      case SRAI:
      case ROTRI:
        kType = kOp14Type;
        break;
      default:
        kType = kUnsupported;
    }
  }

  if (kType == kUnsupported) {
    // Check for kOp17Type
    switch (Bits(31, 15) << 15) {
      case ADD_W:
      case ADD_D:
      case SUB_W:
      case SUB_D:
      case SLT:
      case SLTU:
      case MASKEQZ:
      case MASKNEZ:
      case NOR:
      case AND:
      case OR:
      case XOR:
      case ORN:
      case ANDN:
      case SLL_W:
      case SRL_W:
      case SRA_W:
      case SLL_D:
      case SRL_D:
      case SRA_D:
      case ROTR_D:
      case ROTR_W:
      case MUL_W:
      case MULH_W:
      case MULH_WU:
      case MUL_D:
      case MULH_D:
      case MULH_DU:
      case MULW_D_W:
      case MULW_D_WU:
      case DIV_W:
      case MOD_W:
      case DIV_WU:
      case MOD_WU:
      case DIV_D:
      case MOD_D:
      case DIV_DU:
      case MOD_DU:
      case BREAK:
      case FADD_S:
      case FADD_D:
      case FSUB_S:
      case FSUB_D:
      case FMUL_S:
      case FMUL_D:
      case FDIV_S:
      case FDIV_D:
      case FMAX_S:
      case FMAX_D:
      case FMIN_S:
      case FMIN_D:
      case FMAXA_S:
      case FMAXA_D:
      case FMINA_S:
      case FMINA_D:
      case LDX_B:
      case LDX_H:
      case LDX_W:
      case LDX_D:
      case STX_B:
      case STX_H:
      case STX_W:
      case STX_D:
      case LDX_BU:
      case LDX_HU:
      case LDX_WU:
      case FLDX_S:
      case FLDX_D:
      case FSTX_S:
      case FSTX_D:
      case AMSWAP_W:
      case AMSWAP_D:
      case AMADD_W:
      case AMADD_D:
      case AMAND_W:
      case AMAND_D:
      case AMOR_W:
      case AMOR_D:
      case AMXOR_W:
      case AMXOR_D:
      case AMMAX_W:
      case AMMAX_D:
      case AMMIN_W:
      case AMMIN_D:
      case AMMAX_WU:
      case AMMAX_DU:
      case AMMIN_WU:
      case AMMIN_DU:
      case AMSWAP_DB_W:
      case AMSWAP_DB_D:
      case AMADD_DB_W:
      case AMADD_DB_D:
      case AMAND_DB_W:
      case AMAND_DB_D:
      case AMOR_DB_W:
      case AMOR_DB_D:
      case AMXOR_DB_W:
      case AMXOR_DB_D:
      case AMMAX_DB_W:
      case AMMAX_DB_D:
      case AMMIN_DB_W:
      case AMMIN_DB_D:
      case AMMAX_DB_WU:
      case AMMAX_DB_DU:
      case AMMIN_DB_WU:
      case AMMIN_DB_DU:
      case DBAR:
      case IBAR:
      case FSCALEB_S:
      case FSCALEB_D:
      case FCOPYSIGN_S:
      case FCOPYSIGN_D:
        kType = kOp17Type;
        break;
      default:
        kType = kUnsupported;
    }
  }

  if (kType == kUnsupported) {
    // Check for kOp22Type
    switch (Bits(31, 10) << 10) {
      case CLZ_W:
      case CTZ_W:
      case CLZ_D:
      case CTZ_D:
      case REVB_2H:
      case REVB_4H:
      case REVB_2W:
      case REVB_D:
      case REVH_2W:
      case REVH_D:
      case BITREV_4B:
      case BITREV_8B:
      case BITREV_W:
      case BITREV_D:
      case EXT_W_B:
      case EXT_W_H:
      case FABS_S:
      case FABS_D:
      case FNEG_S:
      case FNEG_D:
      case FSQRT_S:
      case FSQRT_D:
      case FMOV_S:
      case FMOV_D:
      case MOVGR2FR_W:
      case MOVGR2FR_D:
      case MOVGR2FRH_W:
      case MOVFR2GR_S:
      case MOVFR2GR_D:
      case MOVFRH2GR_S:
      case MOVGR2FCSR:
      case MOVFCSR2GR:
      case FCVT_S_D:
      case FCVT_D_S:
      case FTINTRM_W_S:
      case FTINTRM_W_D:
      case FTINTRM_L_S:
      case FTINTRM_L_D:
      case FTINTRP_W_S:
      case FTINTRP_W_D:
      case FTINTRP_L_S:
      case FTINTRP_L_D:
      case FTINTRZ_W_S:
      case FTINTRZ_W_D:
      case FTINTRZ_L_S:
      case FTINTRZ_L_D:
      case FTINTRNE_W_S:
      case FTINTRNE_W_D:
      case FTINTRNE_L_S:
      case FTINTRNE_L_D:
      case FTINT_W_S:
      case FTINT_W_D:
      case FTINT_L_S:
      case FTINT_L_D:
      case FFINT_S_W:
      case FFINT_S_L:
      case FFINT_D_W:
      case FFINT_D_L:
      case FRINT_S:
      case FRINT_D:
      case MOVFR2CF:
      case MOVCF2FR:
      case MOVGR2CF:
      case MOVCF2GR:
      case FRECIP_S:
      case FRECIP_D:
      case FRSQRT_S:
      case FRSQRT_D:
      case FCLASS_S:
      case FCLASS_D:
      case FLOGB_S:
      case FLOGB_D:
      case CLO_W:
      case CTO_W:
      case CLO_D:
      case CTO_D:
        kType = kOp22Type;
        break;
      default:
        kType = kUnsupported;
    }
  }

  return kType;
}

// -----------------------------------------------------------------------------
// Instructions.

template <class P>
bool InstructionGetters<P>::IsTrap() const {
  if ((this->Bits(31, 15) << 15) == BREAK) return true;
  return false;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_LOONG64_CONSTANTS_LOONG64_H_
