// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright(c) 2010 - 2017,
//     The Regents of the University of California(Regents).All Rights Reserved.
//
//     Redistribution and use in source and binary forms,
//     with or without modification,
//     are permitted provided that the following
//     conditions are met : 1. Redistributions of source code must retain the
//     above copyright notice, this list of conditions and the following
//     disclaimer.2. Redistributions in binary form must reproduce the above
//     copyright notice, this list of conditions and the following disclaimer in
//     the
//             documentation and /
//         or
//         other materials provided with the distribution.3. Neither the name of
//         the Regents nor the names of its contributors may be used to endorse
//         or
//         promote products derived from
//         this software without specific prior written permission.
//
//         IN NO EVENT SHALL REGENTS BE LIABLE TO ANY PARTY FOR DIRECT,
//     INDIRECT, SPECIAL,
//     INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
//     ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
//     EVEN IF REGENTS HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//     REGENTS SPECIFICALLY DISCLAIMS ANY WARRANTIES,
//     INCLUDING, BUT NOT LIMITED TO,
//     THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
//     PARTICULAR PURPOSE.THE SOFTWARE AND ACCOMPANYING DOCUMENTATION,
//     IF ANY,
//     PROVIDED HEREUNDER IS PROVIDED
//     "AS IS".REGENTS HAS NO OBLIGATION TO PROVIDE MAINTENANCE,
//     SUPPORT, UPDATES, ENHANCEMENTS,
//     OR MODIFICATIONS.

// The original source code covered by the above license above has been
// modified significantly by the v8 project authors.

// Declares a Simulator for RISC-V instructions if we are not generating a
// native RISC-V binary. This Simulator allows us to run and debug RISC-V code
// generation on regular desktop machines. V8 calls into generated code via the
// GeneratedCode wrapper, which will start execution in the Simulator or
// forwards to the real entry on a RISC-V HW platform.

#ifndef V8_EXECUTION_RISCV_SIMULATOR_RISCV_H_
#define V8_EXECUTION_RISCV_SIMULATOR_RISCV_H_

// globals.h defines USE_SIMULATOR.
#include "src/common/globals.h"

template <typename T>
int Compare(const T& a, const T& b) {
  if (a == b)
    return 0;
  else if (a < b)
    return -1;
  else
    return 1;
}

// Returns the negative absolute value of its argument.
template <typename T,
          typename = typename std::enable_if<std::is_signed<T>::value>::type>
T Nabs(T a) {
  return a < 0 ? a : -a;
}

#if defined(USE_SIMULATOR)
typedef signed __int128_t __attribute__((__mode__(__TI__)));
typedef unsigned __uint128_t __attribute__((__mode__(__TI__)));
// Running with a simulator.

#include "src/base/hashmap.h"
#include "src/codegen/assembler.h"
#include "src/codegen/constants-arch.h"
#include "src/execution/simulator-base.h"
#include "src/utils/allocation.h"
#include "src/utils/boxed-float.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// Utility types and functions for RISCV
#ifdef V8_TARGET_ARCH_32_BIT
using sreg_t = int32_t;
using reg_t = uint32_t;
using freg_t = uint64_t;
using sfreg_t = int64_t;
#elif V8_TARGET_ARCH_64_BIT
using sreg_t = int64_t;
using reg_t = uint64_t;
using freg_t = uint64_t;
using sfreg_t = int64_t;
#else
#error "Cannot detect Riscv's bitwidth"
#endif

#define sext32(x) ((sreg_t)(int32_t)(x))
#define zext32(x) ((reg_t)(uint32_t)(x))

#ifdef V8_TARGET_ARCH_64_BIT
#define sext_xlen(x) (((sreg_t)(x) << (64 - xlen)) >> (64 - xlen))
#define zext_xlen(x) (((reg_t)(x) << (64 - xlen)) >> (64 - xlen))
#elif V8_TARGET_ARCH_32_BIT
#define sext_xlen(x) (((sreg_t)(x) << (32 - xlen)) >> (32 - xlen))
#define zext_xlen(x) (((reg_t)(x) << (32 - xlen)) >> (32 - xlen))
#endif

#define BIT(n) (0x1LL << n)
#define QUIET_BIT_S(nan) (base::bit_cast<int32_t>(nan) & BIT(22))
#define QUIET_BIT_D(nan) (base::bit_cast<int64_t>(nan) & BIT(51))
static inline bool isSnan(float fp) { return !QUIET_BIT_S(fp); }
static inline bool isSnan(double fp) { return !QUIET_BIT_D(fp); }
#undef QUIET_BIT_S
#undef QUIET_BIT_D

#ifdef V8_TARGET_ARCH_64_BIT
inline uint64_t mulhu(uint64_t a, uint64_t b) {
  __uint128_t full_result = ((__uint128_t)a) * ((__uint128_t)b);
  return full_result >> 64;
}

inline int64_t mulh(int64_t a, int64_t b) {
  __int128_t full_result = ((__int128_t)a) * ((__int128_t)b);
  return full_result >> 64;
}

inline int64_t mulhsu(int64_t a, uint64_t b) {
  __int128_t full_result = ((__int128_t)a) * ((__uint128_t)b);
  return full_result >> 64;
}
#elif V8_TARGET_ARCH_32_BIT
inline uint32_t mulhu(uint32_t a, uint32_t b) {
  uint64_t full_result = ((uint64_t)a) * ((uint64_t)b);
  uint64_t upper_part = full_result >> 32;
  return (uint32_t)upper_part;
}

inline int32_t mulh(int32_t a, int32_t b) {
  int64_t full_result = ((int64_t)a) * ((int64_t)b);
  int64_t upper_part = full_result >> 32;
  return (int32_t)upper_part;
}

inline int32_t mulhsu(int32_t a, uint32_t b) {
  int64_t full_result = ((int64_t)a) * ((uint64_t)b);
  int64_t upper_part = full_result >> 32;
  return (int32_t)upper_part;
}
#endif

// Floating point helpers
#define F32_SIGN ((uint32_t)1 << 31)
union u32_f32 {
  uint32_t u;
  float f;
};
inline float fsgnj32(float rs1, float rs2, bool n, bool x) {
  u32_f32 a = {.f = rs1}, b = {.f = rs2};
  u32_f32 res;
  res.u = (a.u & ~F32_SIGN) | ((((x)   ? a.u
                                 : (n) ? F32_SIGN
                                       : 0) ^
                                b.u) &
                               F32_SIGN);
  return res.f;
}

inline Float32 fsgnj32(Float32 rs1, Float32 rs2, bool n, bool x) {
  u32_f32 a = {.u = rs1.get_bits()}, b = {.u = rs2.get_bits()};
  u32_f32 res;
  if (x) {  // RO_FSQNJX_S
    res.u = (a.u & ~F32_SIGN) | ((a.u ^ b.u) & F32_SIGN);
  } else {
    if (n) {  // RO_FSGNJN_S
      res.u = (a.u & ~F32_SIGN) | ((F32_SIGN ^ b.u) & F32_SIGN);
    } else {  // RO_FSGNJ_S
      res.u = (a.u & ~F32_SIGN) | ((0 ^ b.u) & F32_SIGN);
    }
  }
  return Float32::FromBits(res.u);
}
#define F64_SIGN ((uint64_t)1 << 63)
union u64_f64 {
  uint64_t u;
  double d;
};
inline double fsgnj64(double rs1, double rs2, bool n, bool x) {
  u64_f64 a = {.d = rs1}, b = {.d = rs2};
  u64_f64 res;
  res.u = (a.u & ~F64_SIGN) | ((((x)   ? a.u
                                 : (n) ? F64_SIGN
                                       : 0) ^
                                b.u) &
                               F64_SIGN);
  return res.d;
}

inline Float64 fsgnj64(Float64 rs1, Float64 rs2, bool n, bool x) {
  u64_f64 a = {.u = rs1.get_bits()}, b = {.u = rs2.get_bits()};
  u64_f64 res;
  if (x) {  // RO_FSQNJX_D
    res.u = (a.u & ~F64_SIGN) | ((a.u ^ b.u) & F64_SIGN);
  } else {
    if (n) {  // RO_FSGNJN_D
      res.u = (a.u & ~F64_SIGN) | ((F64_SIGN ^ b.u) & F64_SIGN);
    } else {  // RO_FSGNJ_D
      res.u = (a.u & ~F64_SIGN) | ((0 ^ b.u) & F64_SIGN);
    }
  }
  return Float64::FromBits(res.u);
}
inline bool is_boxed_float(int64_t v) { return (uint32_t)((v >> 32) + 1) == 0; }
inline int64_t box_float(float v) {
  return (0xFFFFFFFF00000000 | base::bit_cast<int32_t>(v));
}

inline uint64_t box_float(uint32_t v) { return (0xFFFFFFFF00000000 | v); }

// -----------------------------------------------------------------------------
// Utility functions

class CachePage {
 public:
  static const int LINE_VALID = 0;
  static const int LINE_INVALID = 1;

  static const int kPageShift = 12;
  static const int kPageSize = 1 << kPageShift;
  static const int kPageMask = kPageSize - 1;
  static const int kLineShift = 2;  // The cache line is only 4 bytes right now.
  static const int kLineLength = 1 << kLineShift;
  static const int kLineMask = kLineLength - 1;

  CachePage() { memset(&validity_map_, LINE_INVALID, sizeof(validity_map_)); }

  char* ValidityByte(int offset) {
    return &validity_map_[offset >> kLineShift];
  }

  char* CachedData(int offset) { return &data_[offset]; }

 private:
  char data_[kPageSize];  // The cached data.
  static const int kValidityMapSize = kPageSize >> kLineShift;
  char validity_map_[kValidityMapSize];  // One byte per line.
};

class SimInstructionBase : public InstructionBase {
 public:
  Type InstructionType() const { return type_; }
  inline Instruction* instr() const { return instr_; }
  inline int32_t operand() const { return operand_; }

 protected:
  SimInstructionBase() : operand_(-1), instr_(nullptr), type_(kUnsupported) {}
  explicit SimInstructionBase(Instruction* instr) {}

  int32_t operand_;
  Instruction* instr_;
  Type type_;

 private:
  DISALLOW_ASSIGN(SimInstructionBase);
};

class SimInstruction : public InstructionGetters<SimInstructionBase> {
 public:
  SimInstruction() {}

  explicit SimInstruction(Instruction* instr) { *this = instr; }

  SimInstruction& operator=(Instruction* instr) {
    operand_ = *reinterpret_cast<const int32_t*>(instr);
    instr_ = instr;
    type_ = InstructionBase::InstructionType();
    DCHECK(reinterpret_cast<void*>(&operand_) == this);
    return *this;
  }
};

class Simulator : public SimulatorBase {
 public:
  friend class RiscvDebugger;

  // Registers are declared in order. See SMRL chapter 2.
  enum Register {
    no_reg = -1,
    zero_reg = 0,
    ra,
    sp,
    gp,
    tp,
    t0,
    t1,
    t2,
    s0,
    s1,
    a0,
    a1,
    a2,
    a3,
    a4,
    a5,
    a6,
    a7,
    s2,
    s3,
    s4,
    s5,
    s6,
    s7,
    s8,
    s9,
    s10,
    s11,
    t3,
    t4,
    t5,
    t6,
    pc,  // pc must be the last register.
    kNumSimuRegisters,
    // aliases
    fp = s0
  };

  // Coprocessor registers.
  // Generated code will always use doubles. So we will only use even registers.
  enum FPURegister {
    ft0,
    ft1,
    ft2,
    ft3,
    ft4,
    ft5,
    ft6,
    ft7,
    fs0,
    fs1,
    fa0,
    fa1,
    fa2,
    fa3,
    fa4,
    fa5,
    fa6,
    fa7,
    fs2,
    fs3,
    fs4,
    fs5,
    fs6,
    fs7,
    fs8,
    fs9,
    fs10,
    fs11,
    ft8,
    ft9,
    ft10,
    ft11,
    kNumFPURegisters
  };

  enum VRegister {
    v0,
    v1,
    v2,
    v3,
    v4,
    v5,
    v6,
    v7,
    v8,
    v9,
    v10,
    v11,
    v12,
    v13,
    v14,
    v15,
    v16,
    v17,
    v18,
    v19,
    v20,
    v21,
    v22,
    v23,
    v24,
    v25,
    v26,
    v27,
    v28,
    v29,
    v30,
    v31,
    kNumVRegisters
  };

  explicit Simulator(Isolate* isolate);
  ~Simulator();

  // The currently executing Simulator instance. Potentially there can be one
  // for each native thread.
  V8_EXPORT_PRIVATE static Simulator* current(v8::internal::Isolate* isolate);

  // Accessors for register state. Reading the pc value adheres to the RISC-V
  // architecture specification and is off by a 8 from the currently executing
  // instruction.
  void set_register(int reg, sreg_t value);
  void set_register_word(int reg, int32_t value);
  V8_EXPORT_PRIVATE sreg_t get_register(int reg) const;
  double get_double_from_register_pair(int reg);

  // Same for FPURegisters.
  void set_fpu_register(int fpureg, int64_t value);
  void set_fpu_register_word(int fpureg, int32_t value);
  void set_fpu_register_hi_word(int fpureg, int32_t value);
  void set_fpu_register_float(int fpureg, float value);
  void set_fpu_register_float(int fpureg, Float32 value);
  void set_fpu_register_double(int fpureg, double value);
  void set_fpu_register_double(int fpureg, Float64 value);

  int64_t get_fpu_register(int fpureg) const;
  int32_t get_fpu_register_word(int fpureg) const;
  int32_t get_fpu_register_signed_word(int fpureg) const;
  int32_t get_fpu_register_hi_word(int fpureg) const;
  float get_fpu_register_float(int fpureg) const;
  Float32 get_fpu_register_Float32(int fpureg, bool check_nanbox = true) const;
  double get_fpu_register_double(int fpureg) const;
  Float64 get_fpu_register_Float64(int fpureg) const;

  // RV CSR manipulation
  uint32_t read_csr_value(uint32_t csr);
  void write_csr_value(uint32_t csr, reg_t value);
  void set_csr_bits(uint32_t csr, reg_t flags);
  void clear_csr_bits(uint32_t csr, reg_t flags);

  void set_fflags(uint32_t flags) { set_csr_bits(csr_fflags, flags); }
  void clear_fflags(int32_t flags) { clear_csr_bits(csr_fflags, flags); }

#ifdef CAN_USE_RVV_INSTRUCTIONS
  // RVV CSR
  __int128_t get_vregister(int vreg) const;
  inline uint64_t rvv_vlen() const { return kRvvVLEN; }
  inline uint64_t rvv_vtype() const { return vtype_; }
  inline uint64_t rvv_vl() const { return vl_; }
  inline uint64_t rvv_vstart() const { return vstart_; }
  inline uint64_t rvv_vxsat() const { return vxsat_; }
  inline uint64_t rvv_vxrm() const { return vxrm_; }
  inline uint64_t rvv_vcsr() const { return vcsr_; }
  inline uint64_t rvv_vlenb() const { return vlenb_; }
  inline uint32_t rvv_zimm() const { return instr_.Rvvzimm(); }
  inline uint32_t rvv_vlmul() const { return (rvv_vtype() & 0x7); }
  inline float rvv_vflmul() const {
    if ((rvv_vtype() & 0b100) == 0) {
      return static_cast<float>(0x1 << (rvv_vtype() & 0x7));
    } else {
      return 1.0 / static_cast<float>(0x1 << (4 - rvv_vtype() & 0x3));
    }
  }
  inline uint32_t rvv_vsew() const { return ((rvv_vtype() >> 3) & 0x7); }

  inline const char* rvv_sew_s() const {
    uint32_t vsew = rvv_vsew();
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

  inline const char* rvv_lmul_s() const {
    uint32_t vlmul = rvv_vlmul();
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

  // return size of lane.8 16 32 64
  inline uint32_t rvv_sew() const {
    DCHECK_EQ(rvv_vsew() & (~0x7), 0x0);
    return (0x1 << rvv_vsew()) * 8;
  }
  inline uint64_t rvv_vlmax() const {
    if ((rvv_vlmul() & 0b100) != 0) {
      return (rvv_vlen() / rvv_sew()) >> (4 - (rvv_vlmul() & 0b11));
    } else {
      return ((rvv_vlen() << rvv_vlmul()) / rvv_sew());
    }
  }
#endif

  inline uint32_t get_dynamic_rounding_mode();
  inline bool test_fflags_bits(uint32_t mask);

  float RoundF2FHelper(float input_val, int rmode);
  double RoundF2FHelper(double input_val, int rmode);
  template <typename I_TYPE, typename F_TYPE>
  I_TYPE RoundF2IHelper(F_TYPE original, int rmode);

  template <typename T>
  T FMaxMinHelper(T a, T b, MaxMinKind kind);

  template <typename T>
  bool CompareFHelper(T input1, T input2, FPUCondition cc);

  // Special case of set_register and get_register to access the raw PC value.
  void set_pc(sreg_t value);
  V8_EXPORT_PRIVATE sreg_t get_pc() const;

  Address get_sp() const { return static_cast<Address>(get_register(sp)); }

  // Accessor to the internal simulator stack area. Adds a safety
  // margin to prevent overflows (kAdditionalStackMargin).
  uintptr_t StackLimit(uintptr_t c_limit) const;
  // Return current stack view, without additional safety margins.
  // Users, for example wasm::StackMemory, can add their own.
  base::Vector<uint8_t> GetCurrentStackView() const;

  // Executes RISC-V instructions until the PC reaches end_sim_pc.
  void Execute();

  // Only arguments up to 64 bits in size are supported.
  class CallArgument {
   public:
    template <typename T>
    explicit CallArgument(T argument) {
      bits_ = 0;
      DCHECK(sizeof(argument) <= sizeof(bits_));
      bits_ = ConvertArg(argument);
      type_ = GP_ARG;
    }
    explicit CallArgument(double argument) {
      DCHECK(sizeof(argument) == sizeof(bits_));
      memcpy(&bits_, &argument, sizeof(argument));
      type_ = FP_ARG;
    }
    explicit CallArgument(float argument) {
      // TODO(all): CallArgument(float) is untested.
      UNIMPLEMENTED();
    }
    // This indicates the end of the arguments list, so that CallArgument
    // objects can be passed into varargs functions.
    static CallArgument End() { return CallArgument(); }
    int64_t bits() const { return bits_; }
    bool IsEnd() const { return type_ == NO_ARG; }
    bool IsGP() const { return type_ == GP_ARG; }
    bool IsFP() const { return type_ == FP_ARG; }

   private:
    enum CallArgumentType { GP_ARG, FP_ARG, NO_ARG };
    // All arguments are aligned to at least 64 bits and we don't support
    // passing bigger arguments, so the payload size can be fixed at 64 bits.
    int64_t bits_;
    CallArgumentType type_;
    CallArgument() { type_ = NO_ARG; }
  };

  template <typename Return, typename... Args>
  Return Call(Address entry, Args... args) {
#ifdef V8_TARGET_ARCH_RISCV64
    // Convert all arguments to CallArgument.
    CallArgument call_args[] = {CallArgument(args)..., CallArgument::End()};
    CallImpl(entry, call_args);
    return ReadReturn<Return>();
#else
    return VariadicCall<Return>(this, &Simulator::CallImpl, entry, args...);
#endif
  }
  // Alternative: call a 2-argument double function.
  double CallFP(Address entry, double d0, double d1);

  // Push an address onto the JS stack.
  uintptr_t PushAddress(uintptr_t address);

  // Pop an address from the JS stack.
  uintptr_t PopAddress();

  // Debugger input.
  void set_last_debugger_input(char* input);
  char* last_debugger_input() { return last_debugger_input_; }

  // Redirection support.
  static void SetRedirectInstruction(Instruction* instruction);

  // ICache checking.
  static bool ICacheMatch(void* one, void* two);
  static void FlushICache(base::CustomMatcherHashMap* i_cache, void* start,
                          size_t size);

  // Returns true if pc register contains one of the 'special_values' defined
  // below (bad_ra, end_sim_pc).
  bool has_bad_pc() const;

 private:
  enum special_values {
    // Known bad pc value to ensure that the simulator does not execute
    // without being properly setup.
    bad_ra = -1,
    // A pc value used to signal the simulator to stop execution.  Generally
    // the ra is set to this value on transition from native C code to
    // simulated execution, so that the simulator can "return" to the native
    // C code.
    end_sim_pc = -2,
    // Unpredictable value.
    Unpredictable = 0xbadbeaf
  };

#ifdef V8_TARGET_ARCH_RISCV64
  V8_EXPORT_PRIVATE void CallImpl(Address entry, CallArgument* args);
  void CallAnyCTypeFunction(Address target_address,
                            const EncodedCSignature& signature);
  // Read floating point return values.
  template <typename T>
  typename std::enable_if<std::is_floating_point<T>::value, T>::type
  ReadReturn() {
    return static_cast<T>(get_fpu_register_double(fa0));
  }
  // Read non-float return values.
  template <typename T>
  typename std::enable_if<!std::is_floating_point<T>::value, T>::type
  ReadReturn() {
    return ConvertReturn<T>(get_register(a0));
  }
#else
  V8_EXPORT_PRIVATE intptr_t CallImpl(Address entry, int argument_count,
                                      const intptr_t* arguments);
#endif
  // Unsupported instructions use Format to print an error and stop execution.
  void Format(Instruction* instr, const char* format);

  // Helpers for data value tracing.
  enum TraceType {
    BYTE,
    HALF,
    WORD,
#if V8_TARGET_ARCH_RISCV64
    DWORD,
#endif
    FLOAT,
    DOUBLE,
    // FLOAT_DOUBLE,
    // WORD_DWORD
  };

  // "Probe" if an address range can be read. This is currently implemented
  // by doing a 1-byte read of the last accessed byte, since the assumption is
  // that if the last byte is accessible, also all lower bytes are accessible
  // (which holds true for Wasm).
  // Returns true if the access was successful, false if the access raised a
  // signal which was then handled by the trap handler (also see
  // {trap_handler::ProbeMemory}). If the access raises a signal which is not
  // handled by the trap handler (e.g. because the current PC is not registered
  // as a protected instruction), the signal will propagate and make the process
  // crash. If no trap handler is available, this always returns true.
  bool ProbeMemory(uintptr_t address, uintptr_t access_size);

  // RISCV Memory read/write methods
  template <typename T>
  T ReadMem(sreg_t addr, Instruction* instr);
  template <typename T>
  void WriteMem(sreg_t addr, T value, Instruction* instr);
  template <typename T, typename OP>
  T amo(sreg_t addr, OP f, Instruction* instr, TraceType t) {
    auto lhs = ReadMem<T>(addr, instr);
    // TODO(RISCV): trace memory read for AMO
    WriteMem<T>(addr, (T)f(lhs), instr);
    return lhs;
  }

  // Helper for debugging memory access.
  inline void DieOrDebug();

#if V8_TARGET_ARCH_RISCV32
  template <typename T>
  void TraceRegWr(T value, TraceType t = WORD);
#elif V8_TARGET_ARCH_RISCV64
  void TraceRegWr(sreg_t value, TraceType t = DWORD);
#endif
  void TraceMemWr(sreg_t addr, sreg_t value, TraceType t);
  template <typename T>
  void TraceMemRd(sreg_t addr, T value, sreg_t reg_value);
  void TraceMemRdDouble(sreg_t addr, double value, int64_t reg_value);
  void TraceMemRdDouble(sreg_t addr, Float64 value, int64_t reg_value);
  void TraceMemRdFloat(sreg_t addr, Float32 value, int64_t reg_value);

  template <typename T>
  void TraceMemWr(sreg_t addr, T value);
  void TraceMemWrDouble(sreg_t addr, double value);

  SimInstruction instr_;

  // RISCV utlity API to access register value
  inline int32_t rs1_reg() const { return instr_.Rs1Value(); }
  inline sreg_t rs1() const { return get_register(rs1_reg()); }
  inline float frs1() const { return get_fpu_register_float(rs1_reg()); }
  inline double drs1() const { return get_fpu_register_double(rs1_reg()); }
  inline Float32 frs1_boxed() const {
    return get_fpu_register_Float32(rs1_reg());
  }
  inline Float64 drs1_boxed() const {
    return get_fpu_register_Float64(rs1_reg());
  }
  inline int32_t rs2_reg() const { return instr_.Rs2Value(); }
  inline sreg_t rs2() const { return get_register(rs2_reg()); }
  inline float frs2() const { return get_fpu_register_float(rs2_reg()); }
  inline double drs2() const { return get_fpu_register_double(rs2_reg()); }
  inline Float32 frs2_boxed() const {
    return get_fpu_register_Float32(rs2_reg());
  }
  inline Float64 drs2_boxed() const {
    return get_fpu_register_Float64(rs2_reg());
  }
  inline int32_t rs3_reg() const { return instr_.Rs3Value(); }
  inline sreg_t rs3() const { return get_register(rs3_reg()); }
  inline float frs3() const { return get_fpu_register_float(rs3_reg()); }
  inline double drs3() const { return get_fpu_register_double(rs3_reg()); }
  inline Float32 frs3_boxed() const {
    return get_fpu_register_Float32(rs3_reg());
  }
  inline Float64 drs3_boxed() const {
    return get_fpu_register_Float64(rs3_reg());
  }
  inline int32_t rd_reg() const { return instr_.RdValue(); }
  inline int32_t frd_reg() const { return instr_.RdValue(); }
  inline int32_t rvc_rs1_reg() const { return instr_.RvcRs1Value(); }
  inline sreg_t rvc_rs1() const { return get_register(rvc_rs1_reg()); }
  inline int32_t rvc_rs2_reg() const { return instr_.RvcRs2Value(); }
  inline sreg_t rvc_rs2() const { return get_register(rvc_rs2_reg()); }
  inline double rvc_drs2() const {
    return get_fpu_register_double(rvc_rs2_reg());
  }
  inline int32_t rvc_rs1s_reg() const { return instr_.RvcRs1sValue(); }
  inline sreg_t rvc_rs1s() const { return get_register(rvc_rs1s_reg()); }
  inline int32_t rvc_rs2s_reg() const { return instr_.RvcRs2sValue(); }
  inline sreg_t rvc_rs2s() const { return get_register(rvc_rs2s_reg()); }
  inline double rvc_drs2s() const {
    return get_fpu_register_double(rvc_rs2s_reg());
  }
  inline int32_t rvc_rd_reg() const { return instr_.RvcRdValue(); }
  inline int32_t rvc_frd_reg() const { return instr_.RvcRdValue(); }
  inline int16_t boffset() const { return instr_.BranchOffset(); }
  inline int16_t imm12() const { return instr_.Imm12Value(); }
  inline int32_t imm20J() const { return instr_.Imm20JValue(); }
  inline int32_t imm5CSR() const { return instr_.Rs1Value(); }
  inline int16_t csr_reg() const { return instr_.CsrValue(); }
  inline int16_t rvc_imm6() const { return instr_.RvcImm6Value(); }
  inline int16_t rvc_imm6_addi16sp() const {
    return instr_.RvcImm6Addi16spValue();
  }
  inline int16_t rvc_imm8_addi4spn() const {
    return instr_.RvcImm8Addi4spnValue();
  }
  inline int16_t rvc_imm6_lwsp() const { return instr_.RvcImm6LwspValue(); }
  inline int16_t rvc_imm6_ldsp() const { return instr_.RvcImm6LdspValue(); }
  inline int16_t rvc_imm6_swsp() const { return instr_.RvcImm6SwspValue(); }
  inline int16_t rvc_imm6_sdsp() const { return instr_.RvcImm6SdspValue(); }
  inline int16_t rvc_imm5_w() const { return instr_.RvcImm5WValue(); }
  inline int16_t rvc_imm5_d() const { return instr_.RvcImm5DValue(); }
  inline int16_t rvc_imm8_b() const { return instr_.RvcImm8BValue(); }

  inline void set_rd(sreg_t value, bool trace = true) {
    set_register(rd_reg(), value);
#if V8_TARGET_ARCH_RISCV64
    if (trace) TraceRegWr(get_register(rd_reg()), DWORD);
#elif V8_TARGET_ARCH_RISCV32
    if (trace) TraceRegWr(get_register(rd_reg()), WORD);
#endif
  }
  inline void set_frd(float value, bool trace = true) {
    set_fpu_register_float(rd_reg(), value);
    if (trace) TraceRegWr(get_fpu_register_word(rd_reg()), FLOAT);
  }
  inline void set_frd(Float32 value, bool trace = true) {
    set_fpu_register_float(rd_reg(), value);
    if (trace) TraceRegWr(get_fpu_register_word(rd_reg()), FLOAT);
  }
  inline void set_drd(double value, bool trace = true) {
    set_fpu_register_double(rd_reg(), value);
    if (trace) TraceRegWr(get_fpu_register(rd_reg()), DOUBLE);
  }
  inline void set_drd(Float64 value, bool trace = true) {
    set_fpu_register_double(rd_reg(), value);
    if (trace) TraceRegWr(get_fpu_register(rd_reg()), DOUBLE);
  }
  inline void set_rvc_rd(sreg_t value, bool trace = true) {
    set_register(rvc_rd_reg(), value);
#if V8_TARGET_ARCH_RISCV64
    if (trace) TraceRegWr(get_register(rvc_rd_reg()), DWORD);
#elif V8_TARGET_ARCH_RISCV32
    if (trace) TraceRegWr(get_register(rvc_rd_reg()), WORD);
#endif
  }
  inline void set_rvc_rs1s(sreg_t value, bool trace = true) {
    set_register(rvc_rs1s_reg(), value);
#if V8_TARGET_ARCH_RISCV64
    if (trace) TraceRegWr(get_register(rvc_rs1s_reg()), DWORD);
#elif V8_TARGET_ARCH_RISCV32
    if (trace) TraceRegWr(get_register(rvc_rs1s_reg()), WORD);
#endif
  }
  inline void set_rvc_rs2(sreg_t value, bool trace = true) {
    set_register(rvc_rs2_reg(), value);
#if V8_TARGET_ARCH_RISCV64
    if (trace) TraceRegWr(get_register(rvc_rs2_reg()), DWORD);
#elif V8_TARGET_ARCH_RISCV32
    if (trace) TraceRegWr(get_register(rvc_rs2_reg()), WORD);
#endif
  }
  inline void set_rvc_drd(double value, bool trace = true) {
    set_fpu_register_double(rvc_rd_reg(), value);
    if (trace) TraceRegWr(get_fpu_register(rvc_rd_reg()), DOUBLE);
  }
  inline void set_rvc_drd(Float64 value, bool trace = true) {
    set_fpu_register_double(rvc_rd_reg(), value);
    if (trace) TraceRegWr(get_fpu_register(rvc_rd_reg()), DOUBLE);
  }
  inline void set_rvc_frd(Float32 value, bool trace = true) {
    set_fpu_register_float(rvc_rd_reg(), value);
    if (trace) TraceRegWr(get_fpu_register(rvc_rd_reg()), DOUBLE);
  }
  inline void set_rvc_rs2s(sreg_t value, bool trace = true) {
    set_register(rvc_rs2s_reg(), value);
#if V8_TARGET_ARCH_RISCV64
    if (trace) TraceRegWr(get_register(rvc_rs2s_reg()), DWORD);
#elif V8_TARGET_ARCH_RISCV32
    if (trace) TraceRegWr(get_register(rvc_rs2s_reg()), WORD);
#endif
  }
  inline void set_rvc_drs2s(double value, bool trace = true) {
    set_fpu_register_double(rvc_rs2s_reg(), value);
    if (trace) TraceRegWr(get_fpu_register(rvc_rs2s_reg()), DOUBLE);
  }
  inline void set_rvc_drs2s(Float64 value, bool trace = true) {
    set_fpu_register_double(rvc_rs2s_reg(), value);
    if (trace) TraceRegWr(get_fpu_register(rvc_rs2s_reg()), DOUBLE);
  }

  inline void set_rvc_frs2s(Float32 value, bool trace = true) {
    set_fpu_register_float(rvc_rs2s_reg(), value);
    if (trace) TraceRegWr(get_fpu_register(rvc_rs2s_reg()), FLOAT);
  }
  inline int16_t shamt6() const { return (imm12() & 0x3F); }
  inline int16_t shamt5() const { return (imm12() & 0x1F); }
  inline int16_t rvc_shamt6() const { return instr_.RvcShamt6(); }
  inline int32_t s_imm12() const { return instr_.StoreOffset(); }
  inline int32_t u_imm20() const { return instr_.Imm20UValue() << 12; }
  inline int32_t rvc_u_imm6() const { return instr_.RvcImm6Value() << 12; }
  inline void require(bool check) {
    if (!check) {
      SignalException(kIllegalInstruction);
    }
  }

#ifdef CAN_USE_RVV_INSTRUCTIONS
  inline void rvv_trace_vd() {
    if (v8_flags.trace_sim) {
      __int128_t value = Vregister_[rvv_vd_reg()];
      SNPrintF(trace_buf_, "%016" PRIx64 "%016" PRIx64 " (%" PRId64 ")",
               *(reinterpret_cast<int64_t*>(&value) + 1),
               *reinterpret_cast<int64_t*>(&value), icount_);
    }
  }

  inline void rvv_trace_vs1() {
    if (v8_flags.trace_sim) {
      PrintF("\t%s:0x%016" PRIx64 "%016" PRIx64 "\n",
             v8::internal::VRegisters::Name(static_cast<int>(rvv_vs1_reg())),
             (uint64_t)(get_vregister(static_cast<int>(rvv_vs1_reg())) >> 64),
             (uint64_t)get_vregister(static_cast<int>(rvv_vs1_reg())));
    }
  }

  inline void rvv_trace_vs2() {
    if (v8_flags.trace_sim) {
      PrintF("\t%s:0x%016" PRIx64 "%016" PRIx64 "\n",
             v8::internal::VRegisters::Name(static_cast<int>(rvv_vs2_reg())),
             (uint64_t)(get_vregister(static_cast<int>(rvv_vs2_reg())) >> 64),
             (uint64_t)get_vregister(static_cast<int>(rvv_vs2_reg())));
    }
  }
  inline void rvv_trace_v0() {
    if (v8_flags.trace_sim) {
      PrintF("\t%s:0x%016" PRIx64 "%016" PRIx64 "\n",
             v8::internal::VRegisters::Name(v0),
             (uint64_t)(get_vregister(v0) >> 64), (uint64_t)get_vregister(v0));
    }
  }

  inline void rvv_trace_rs1() {
    if (v8_flags.trace_sim) {
      PrintF("\t%s:0x%016" PRIx64 "\n",
             v8::internal::Registers::Name(static_cast<int>(rs1_reg())),
             (uint64_t)(get_register(rs1_reg())));
    }
  }

  inline void rvv_trace_status() {
    if (v8_flags.trace_sim) {
      int i = 0;
      for (; i < trace_buf_.length(); i++) {
        if (trace_buf_[i] == '\0') break;
      }
      SNPrintF(trace_buf_.SubVector(i, trace_buf_.length()),
               "  sew:%s lmul:%s vstart:%" PRId64 "vl:%" PRId64, rvv_sew_s(),
               rvv_lmul_s(), rvv_vstart(), rvv_vl());
    }
  }

  template <class T>
  T& Rvvelt(reg_t vReg, uint64_t n, bool is_write = false) {
    CHECK_NE(rvv_sew(), 0);
    CHECK_GT((rvv_vlen() >> 3) / sizeof(T), 0);
    reg_t elts_per_reg = (rvv_vlen() >> 3) / (sizeof(T));
    vReg += n / elts_per_reg;
    n = n % elts_per_reg;
    T* regStart = reinterpret_cast<T*>(reinterpret_cast<char*>(Vregister_) +
                                       vReg * (rvv_vlen() >> 3));
    return regStart[n];
  }

  inline int32_t rvv_vs1_reg() { return instr_.Vs1Value(); }
  inline reg_t rvv_vs1() { UNIMPLEMENTED(); }
  inline int32_t rvv_vs2_reg() { return instr_.Vs2Value(); }
  inline reg_t rvv_vs2() { UNIMPLEMENTED(); }
  inline int32_t rvv_vd_reg() { return instr_.VdValue(); }
  inline int32_t rvv_vs3_reg() { return instr_.VdValue(); }
  inline reg_t rvv_vd() { UNIMPLEMENTED(); }
  inline int32_t rvv_nf() {
    return (instr_.InstructionBits() & kRvvNfMask) >> kRvvNfShift;
  }

  inline void set_vrd() { UNIMPLEMENTED(); }

  inline void set_rvv_vtype(uint64_t value, bool trace = true) {
    vtype_ = value;
  }
  inline void set_rvv_vl(uint64_t value, bool trace = true) { vl_ = value; }
  inline void set_rvv_vstart(uint64_t value, bool trace = true) {
    vstart_ = value;
  }
  inline void set_rvv_vxsat(uint64_t value, bool trace = true) {
    vxsat_ = value;
  }
  inline void set_rvv_vxrm(uint64_t value, bool trace = true) { vxrm_ = value; }
  inline void set_rvv_vcsr(uint64_t value, bool trace = true) { vcsr_ = value; }
  inline void set_rvv_vlenb(uint64_t value, bool trace = true) {
    vlenb_ = value;
  }
#endif

  template <typename T, typename Func>
  inline T CanonicalizeFPUOpFMA(Func fn, T dst, T src1, T src2) {
    static_assert(std::is_floating_point<T>::value);
    auto alu_out = fn(dst, src1, src2);
    // if any input or result is NaN, the result is quiet_NaN
    if (std::isnan(alu_out) || std::isnan(src1) || std::isnan(src2) ||
        std::isnan(dst)) {
      // signaling_nan sets kInvalidOperation bit
      if (isSnan(alu_out) || isSnan(src1) || isSnan(src2) || isSnan(dst))
        set_fflags(kInvalidOperation);
      alu_out = std::numeric_limits<T>::quiet_NaN();
    }
    return alu_out;
  }

  template <typename T, typename Func>
  inline T CanonicalizeFPUOp3(Func fn) {
    static_assert(std::is_floating_point<T>::value);
    T src1 = std::is_same<float, T>::value ? frs1() : drs1();
    T src2 = std::is_same<float, T>::value ? frs2() : drs2();
    T src3 = std::is_same<float, T>::value ? frs3() : drs3();
    auto alu_out = fn(src1, src2, src3);
    // if any input or result is NaN, the result is quiet_NaN
    if (std::isnan(alu_out) || std::isnan(src1) || std::isnan(src2) ||
        std::isnan(src3)) {
      // signaling_nan sets kInvalidOperation bit
      if (isSnan(alu_out) || isSnan(src1) || isSnan(src2) || isSnan(src3))
        set_fflags(kInvalidOperation);
      alu_out = std::numeric_limits<T>::quiet_NaN();
    }
    return alu_out;
  }

  template <typename T, typename Func>
  inline T CanonicalizeFPUOp2(Func fn) {
    static_assert(std::is_floating_point<T>::value);
    T src1 = std::is_same<float, T>::value ? frs1() : drs1();
    T src2 = std::is_same<float, T>::value ? frs2() : drs2();
    auto alu_out = fn(src1, src2);
    // if any input or result is NaN, the result is quiet_NaN
    if (std::isnan(alu_out) || std::isnan(src1) || std::isnan(src2)) {
      // signaling_nan sets kInvalidOperation bit
      if (isSnan(alu_out) || isSnan(src1) || isSnan(src2))
        set_fflags(kInvalidOperation);
      alu_out = std::numeric_limits<T>::quiet_NaN();
    }
    return alu_out;
  }

  template <typename T, typename Func>
  inline T CanonicalizeFPUOp1(Func fn) {
    static_assert(std::is_floating_point<T>::value);
    T src1 = std::is_same<float, T>::value ? frs1() : drs1();
    auto alu_out = fn(src1);
    // if any input or result is NaN, the result is quiet_NaN
    if (std::isnan(alu_out) || std::isnan(src1)) {
      // signaling_nan sets kInvalidOperation bit
      if (isSnan(alu_out) || isSnan(src1)) set_fflags(kInvalidOperation);
      alu_out = std::numeric_limits<T>::quiet_NaN();
    }
    return alu_out;
  }

  template <typename Func>
  inline float CanonicalizeDoubleToFloatOperation(Func fn) {
    float alu_out = fn(drs1());
    if (std::isnan(alu_out) || std::isnan(drs1()))
      alu_out = std::numeric_limits<float>::quiet_NaN();
    return alu_out;
  }

  template <typename Func>
  inline float CanonicalizeDoubleToFloatOperation(Func fn, double frs) {
    float alu_out = fn(frs);
    if (std::isnan(alu_out) || std::isnan(drs1()))
      alu_out = std::numeric_limits<float>::quiet_NaN();
    return alu_out;
  }

  template <typename Func>
  inline float CanonicalizeFloatToDoubleOperation(Func fn, float frs) {
    double alu_out = fn(frs);
    if (std::isnan(alu_out) || std::isnan(frs1()))
      alu_out = std::numeric_limits<double>::quiet_NaN();
    return alu_out;
  }

  template <typename Func>
  inline float CanonicalizeFloatToDoubleOperation(Func fn) {
    double alu_out = fn(frs1());
    if (std::isnan(alu_out) || std::isnan(frs1()))
      alu_out = std::numeric_limits<double>::quiet_NaN();
    return alu_out;
  }

  Builtin LookUp(Address pc);
  // RISCV decoding routine
  void DecodeRVRType();
  void DecodeRVR4Type();
  void DecodeRVRFPType();  // Special routine for R/OP_FP type
  void DecodeRVRAType();   // Special routine for R/AMO type
  void DecodeRVIType();
  void DecodeRVSType();
  void DecodeRVBType();
  void DecodeRVUType();
  void DecodeRVJType();
  void DecodeCRType();
  void DecodeCAType();
  void DecodeCIType();
  void DecodeCIWType();
  void DecodeCSSType();
  void DecodeCLType();
  void DecodeCSType();
  void DecodeCJType();
  void DecodeCBType();
#ifdef CAN_USE_RVV_INSTRUCTIONS
  void DecodeVType();
  void DecodeRvvIVV();
  void DecodeRvvIVI();
  void DecodeRvvIVX();
  void DecodeRvvMVV();
  void DecodeRvvMVX();
  void DecodeRvvFVV();
  void DecodeRvvFVF();
  bool DecodeRvvVL();
  bool DecodeRvvVS();
#endif

  // Used for breakpoints and traps.
  void SoftwareInterrupt();

  // Debug helpers

  // Simulator breakpoints.
  struct Breakpoint {
    Instruction* location;
    bool enabled;
    bool is_tbreak;
  };
  std::vector<Breakpoint> breakpoints_;
  void SetBreakpoint(Instruction* breakpoint, bool is_tbreak);
  void ListBreakpoints();
  void CheckBreakpoints();

  // Stop helper functions.
  bool IsWatchpoint(reg_t code);
  bool IsTracepoint(reg_t code);
  void PrintWatchpoint(reg_t code);
  void HandleStop(reg_t code);
  bool IsStopInstruction(Instruction* instr);
  bool IsEnabledStop(reg_t code);
  void EnableStop(reg_t code);
  void DisableStop(reg_t code);
  void IncreaseStopCounter(reg_t code);
  void PrintStopInfo(reg_t code);

  // Executes one instruction.
  void InstructionDecode(Instruction* instr);

  // ICache.
  static void CheckICache(base::CustomMatcherHashMap* i_cache,
                          Instruction* instr);
  static void FlushOnePage(base::CustomMatcherHashMap* i_cache, intptr_t start,
                           size_t size);
  static CachePage* GetCachePage(base::CustomMatcherHashMap* i_cache,
                                 void* page);

  enum Exception {
    none,
    kIntegerOverflow,
    kIntegerUnderflow,
    kDivideByZero,
    kNumExceptions,
    // RISCV illegual instruction exception
    kIllegalInstruction,
  };

  // Exceptions.
  void SignalException(Exception e);

  // Handle arguments and return value for runtime FP functions.
  void GetFpArgs(double* x, double* y, int32_t* z);
  void SetFpResult(const double& result);

  void CallInternal(Address entry);

  // Architecture state.
  // Registers.
  sreg_t registers_[kNumSimuRegisters];
  // Coprocessor Registers.
  sfreg_t FPUregisters_[kNumFPURegisters];
  // Floating-point control and status register.
  uint32_t FCSR_;

#ifdef CAN_USE_RVV_INSTRUCTIONS
  // RVV registers
  __int128_t Vregister_[kNumVRegisters];
  static_assert(sizeof(__int128_t) == kRvvVLEN / 8, "unmatch vlen");
  uint64_t vstart_, vxsat_, vxrm_, vcsr_, vtype_, vl_, vlenb_;
#endif
  // Simulator support.
  // Allocate 1MB for stack.
  uint8_t* stack_;
  static const size_t kStackProtectionSize = 256 * kSystemPointerSize;
  // This includes a protection margin at each end of the stack area.
  static size_t AllocatedStackSize() {
#if V8_TARGET_ARCH_RISCV64
    size_t stack_size = v8_flags.sim_stack_size * KB;
#else
    size_t stack_size = 1 * MB;  // allocate 1MB for stack
#endif
    return stack_size + (2 * kStackProtectionSize);
  }
  static size_t UsableStackSize() {
    return AllocatedStackSize() - kStackProtectionSize;
  }

  bool pc_modified_;
  int64_t icount_;
  sreg_t* watch_address_ = nullptr;
  sreg_t watch_value_ = 0;
  int break_count_;
  base::EmbeddedVector<char, 256> trace_buf_;

  // Debugger input.
  char* last_debugger_input_;

  v8::internal::Isolate* isolate_;
  v8::internal::Builtins builtins_;

  // Stop is disabled if bit 31 is set.
  static const uint32_t kStopDisabledBit = 1 << 31;

  // A stop is enabled, meaning the simulator will stop when meeting the
  // instruction, if bit 31 of watched_stops_[code].count is unset.
  // The value watched_stops_[code].count & ~(1 << 31) indicates how many times
  // the breakpoint was hit or gone through.
  struct StopCountAndDesc {
    uint32_t count;
    char* desc;
  };
  StopCountAndDesc watched_stops_[kMaxStopCode + 1];

  // Synchronization primitives.
  enum class MonitorAccess {
    Open,
    RMW,
  };

  enum class TransactionSize {
    None = 0,
    Word = 4,
    DoubleWord = 8,
  };

  // The least-significant bits of the address are ignored. The number of bits
  // is implementation-defined, between 3 and minimum page size.
  static const uintptr_t kExclusiveTaggedAddrMask = ~((1 << 3) - 1);

  class LocalMonitor {
   public:
    LocalMonitor();

    // These functions manage the state machine for the local monitor, but do
    // not actually perform loads and stores. NotifyStoreConditional only
    // returns true if the store conditional is allowed; the global monitor will
    // still have to be checked to see whether the memory should be updated.
    void NotifyLoad();
    void NotifyLoadLinked(uintptr_t addr, TransactionSize size);
    void NotifyStore();
    bool NotifyStoreConditional(uintptr_t addr, TransactionSize size);

   private:
    void Clear();

    MonitorAccess access_state_;
    uintptr_t tagged_addr_;
    TransactionSize size_;
  };

  class GlobalMonitor {
   public:
    class LinkedAddress {
     public:
      LinkedAddress();

     private:
      friend class GlobalMonitor;
      // These functions manage the state machine for the global monitor, but do
      // not actually perform loads and stores.
      void Clear_Locked();
      void NotifyLoadLinked_Locked(uintptr_t addr);
      void NotifyStore_Locked();
      bool NotifyStoreConditional_Locked(uintptr_t addr,
                                         bool is_requesting_thread);

      MonitorAccess access_state_;
      uintptr_t tagged_addr_;
      LinkedAddress* next_;
      LinkedAddress* prev_;
      // A scd can fail due to background cache evictions. Rather than
      // simulating this, we'll just occasionally introduce cases where an
      // store conditional fails. This will happen once after every
      // kMaxFailureCounter exclusive stores.
      static const int kMaxFailureCounter = 5;
      int failure_counter_;
    };

    // Exposed so it can be accessed by Simulator::{Read,Write}Ex*.
    base::Mutex mutex;

    void NotifyLoadLinked_Locked(uintptr_t addr, LinkedAddress* linked_address);
    void NotifyStore_Locked(LinkedAddress* linked_address);
    bool NotifyStoreConditional_Locked(uintptr_t addr,
                                       LinkedAddress* linked_address);

    // Called when the simulator is destroyed.
    void RemoveLinkedAddress(LinkedAddress* linked_address);

    static GlobalMonitor* Get();

   private:
    // Private constructor. Call {GlobalMonitor::Get()} to get the singleton.
    GlobalMonitor() = default;
    friend class base::LeakyObject<GlobalMonitor>;

    bool IsProcessorInLinkedList_Locked(LinkedAddress* linked_address) const;
    void PrependProcessor_Locked(LinkedAddress* linked_address);

    LinkedAddress* head_ = nullptr;
  };

  LocalMonitor local_monitor_;
  GlobalMonitor::LinkedAddress global_monitor_thread_;
};
}  // namespace internal
}  // namespace v8

#endif  // defined(USE_SIMULATOR)
#endif  // V8_EXECUTION_RISCV_SIMULATOR_RISCV_H_
