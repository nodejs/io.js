// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Declares a Simulator for PPC instructions if we are not generating a native
// PPC binary. This Simulator allows us to run and debug PPC code generation on
// regular desktop machines.
// V8 calls into generated code via the GeneratedCode wrapper,
// which will start execution in the Simulator or forwards to the real entry
// on a PPC HW platform.

#ifndef V8_EXECUTION_PPC_SIMULATOR_PPC_H_
#define V8_EXECUTION_PPC_SIMULATOR_PPC_H_

// globals.h defines USE_SIMULATOR.
#include "src/common/globals.h"

#if defined(USE_SIMULATOR)
// Running with a simulator.

#include "src/base/hashmap.h"
#include "src/base/lazy-instance.h"
#include "src/base/platform/mutex.h"
#include "src/codegen/assembler.h"
#include "src/codegen/ppc/constants-ppc.h"
#include "src/execution/simulator-base.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

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

class Simulator : public SimulatorBase {
 public:
  friend class PPCDebugger;
  enum Register {
    no_reg = -1,
    r0 = 0,
    sp,
    r2,
    r3,
    r4,
    r5,
    r6,
    r7,
    r8,
    r9,
    r10,
    r11,
    r12,
    r13,
    r14,
    r15,
    r16,
    r17,
    r18,
    r19,
    r20,
    r21,
    r22,
    r23,
    r24,
    r25,
    r26,
    r27,
    r28,
    r29,
    r30,
    fp,
    kNumGPRs = 32,
    d0 = 0,
    d1,
    d2,
    d3,
    d4,
    d5,
    d6,
    d7,
    d8,
    d9,
    d10,
    d11,
    d12,
    d13,
    d14,
    d15,
    d16,
    d17,
    d18,
    d19,
    d20,
    d21,
    d22,
    d23,
    d24,
    d25,
    d26,
    d27,
    d28,
    d29,
    d30,
    d31,
    kNumFPRs = 32,
    // PPC Simd registers are a serapre set from Floating Point registers. Refer
    // to register-ppc.h for more details.
    v0 = 0,
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
    kNumSIMDRs = 32
  };

  explicit Simulator(Isolate* isolate);
  ~Simulator();

  // The currently executing Simulator instance. Potentially there can be one
  // for each native thread.
  static Simulator* current(v8::internal::Isolate* isolate);

  // Accessors for register state.
  void set_register(int reg, intptr_t value);
  intptr_t get_register(int reg) const;
  double get_double_from_register_pair(int reg);
  void set_d_register_from_double(int dreg, const double dbl) {
    DCHECK(dreg >= 0 && dreg < kNumFPRs);
    fp_registers_[dreg] = base::bit_cast<int64_t>(dbl);
  }
  double get_double_from_d_register(int dreg) {
    DCHECK(dreg >= 0 && dreg < kNumFPRs);
    return base::bit_cast<double>(fp_registers_[dreg]);
  }
  void set_d_register(int dreg, int64_t value) {
    DCHECK(dreg >= 0 && dreg < kNumFPRs);
    fp_registers_[dreg] = value;
  }
  int64_t get_d_register(int dreg) {
    DCHECK(dreg >= 0 && dreg < kNumFPRs);
    return fp_registers_[dreg];
  }

  // Special case of set_register and get_register to access the raw PC value.
  void set_pc(intptr_t value);
  intptr_t get_pc() const;

  Address get_sp() const { return static_cast<Address>(get_register(sp)); }

  // Accessor to the internal Link Register
  intptr_t get_lr() const;

  // Accessor to the internal simulator stack area. Adds a safety
  // margin to prevent overflows.
  uintptr_t StackLimit(uintptr_t c_limit) const;
  // Return current stack view, without additional safety margins.
  // Users, for example wasm::StackMemory, can add their own.
  base::Vector<uint8_t> GetCurrentStackView() const;

  // Executes PPC instructions until the PC reaches end_sim_pc.
  void Execute();

  template <typename Return, typename... Args>
  Return Call(Address entry, Args... args) {
    return VariadicCall<Return>(this, &Simulator::CallImpl, entry, args...);
  }

  // Alternative: call a 2-argument double function.
  void CallFP(Address entry, double d0, double d1);
  int32_t CallFPReturnsInt(Address entry, double d0, double d1);
  double CallFPReturnsDouble(Address entry, double d0, double d1);

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
  // below (bad_lr, end_sim_pc).
  bool has_bad_pc() const;

  enum special_values {
    // Known bad pc value to ensure that the simulator does not execute
    // without being properly setup.
    bad_lr = -1,
    // A pc value used to signal the simulator to stop execution.  Generally
    // the lr is set to this value on transition from native C code to
    // simulated execution, so that the simulator can "return" to the native
    // C code.
    end_sim_pc = -2
  };

  intptr_t CallImpl(Address entry, int argument_count,
                    const intptr_t* arguments);

  enum BCType { BC_OFFSET, BC_LINK_REG, BC_CTR_REG };

  // Unsupported instructions use Format to print an error and stop execution.
  void Format(Instruction* instr, const char* format);

  // Helper functions to set the conditional flags in the architecture state.
  bool CarryFrom(int32_t left, int32_t right, int32_t carry = 0);
  bool BorrowFrom(int32_t left, int32_t right);
  bool OverflowFrom(int32_t alu_out, int32_t left, int32_t right,
                    bool addition);

  // Helper functions to decode common "addressing" modes
  int32_t GetShiftRm(Instruction* instr, bool* carry_out);
  int32_t GetImm(Instruction* instr, bool* carry_out);
  void ProcessPUW(Instruction* instr, int num_regs, int operand_size,
                  intptr_t* start_address, intptr_t* end_address);
  void HandleRList(Instruction* instr, bool load);
  void HandleVList(Instruction* inst);
  void SoftwareInterrupt(Instruction* instr);
  void DebugAtNextPC();

  // Stop helper functions.
  inline bool isStopInstruction(Instruction* instr);
  inline bool isWatchedStop(uint32_t bkpt_code);
  inline bool isEnabledStop(uint32_t bkpt_code);
  inline void EnableStop(uint32_t bkpt_code);
  inline void DisableStop(uint32_t bkpt_code);
  inline void IncreaseStopCounter(uint32_t bkpt_code);
  void PrintStopInfo(uint32_t code);

  // Read and write memory.
  template <typename T>
  inline void Read(uintptr_t address, T* value) {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    memcpy(value, reinterpret_cast<const char*>(address), sizeof(T));
  }

  template <typename T>
  inline void ReadEx(uintptr_t address, T* value) {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    GlobalMonitor::Get()->NotifyLoadExcl(
        address, static_cast<TransactionSize>(sizeof(T)),
        isolate_->thread_id());
    memcpy(value, reinterpret_cast<const char*>(address), sizeof(T));
  }

  template <typename T>
  inline void Write(uintptr_t address, T value) {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    GlobalMonitor::Get()->NotifyStore(address,
                                      static_cast<TransactionSize>(sizeof(T)),
                                      isolate_->thread_id());
    memcpy(reinterpret_cast<char*>(address), &value, sizeof(T));
  }

  template <typename T>
  inline int32_t WriteEx(uintptr_t address, T value) {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    if (GlobalMonitor::Get()->NotifyStoreExcl(
            address, static_cast<TransactionSize>(sizeof(T)),
            isolate_->thread_id())) {
      memcpy(reinterpret_cast<char*>(address), &value, sizeof(T));
      return 0;
    } else {
      return 1;
    }
  }

  // Byte Reverse.
  static inline __uint128_t __builtin_bswap128(__uint128_t v) {
    union {
      uint64_t u64[2];
      __uint128_t u128;
    } res, val;
    val.u128 = v;
    res.u64[0] = ByteReverse<int64_t>(val.u64[1]);
    res.u64[1] = ByteReverse<int64_t>(val.u64[0]);
    return res.u128;
  }

#define RW_VAR_LIST(V)      \
  V(QWU, unsigned __int128) \
  V(QW, __int128)           \
  V(DWU, uint64_t)          \
  V(DW, int64_t)            \
  V(WU, uint32_t)           \
  V(W, int32_t) V(HU, uint16_t) V(H, int16_t) V(BU, uint8_t) V(B, int8_t)

#define GENERATE_RW_FUNC(size, type)                   \
  inline type Read##size(uintptr_t addr);              \
  inline type ReadEx##size(uintptr_t addr);            \
  inline void Write##size(uintptr_t addr, type value); \
  inline int32_t WriteEx##size(uintptr_t addr, type value);

  RW_VAR_LIST(GENERATE_RW_FUNC)
#undef GENERATE_RW_FUNC

  void Trace(Instruction* instr);
  void SetCR0(intptr_t result, bool setSO = false);
  void SetCR6(bool true_for_all);
  void ExecuteBranchConditional(Instruction* instr, BCType type);
  void ExecuteGeneric(Instruction* instr);

  void SetFPSCR(int bit) { fp_condition_reg_ |= (1 << (31 - bit)); }
  void ClearFPSCR(int bit) { fp_condition_reg_ &= ~(1 << (31 - bit)); }

  // Executes one instruction.
  void ExecuteInstruction(Instruction* instr);

  // ICache.
  static void CheckICache(base::CustomMatcherHashMap* i_cache,
                          Instruction* instr);
  static void FlushOnePage(base::CustomMatcherHashMap* i_cache, intptr_t start,
                           int size);
  static CachePage* GetCachePage(base::CustomMatcherHashMap* i_cache,
                                 void* page);

  // Handle arguments and return value for runtime FP functions.
  void GetFpArgs(double* x, double* y, intptr_t* z);
  void SetFpResult(const double& result);
  void TrashCallerSaveRegisters();

  void CallInternal(Address entry);

  // Architecture state.
  // Saturating instructions require a Q flag to indicate saturation.
  // There is currently no way to read the CPSR directly, and thus read the Q
  // flag, so this is left unimplemented.
  intptr_t registers_[kNumGPRs];
  int32_t condition_reg_;
  int32_t fp_condition_reg_;
  intptr_t special_reg_lr_;
  intptr_t special_reg_pc_;
  intptr_t special_reg_ctr_;
  int32_t special_reg_xer_;

  int64_t fp_registers_[kNumFPRs];

  // Simd registers.
  union simdr_t {
    int8_t int8[16];
    uint8_t uint8[16];
    int16_t int16[8];
    uint16_t uint16[8];
    int32_t int32[4];
    uint32_t uint32[4];
    int64_t int64[2];
    uint64_t uint64[2];
    float f32[4];
    double f64[2];
  };
  simdr_t simd_registers_[kNumSIMDRs];

  // Vector register lane numbers on IBM machines are reversed compared to
  // x64. For example, doing an I32x4 extract_lane with lane number 0 on x64
  // will be equal to lane number 3 on IBM machines. Vector registers are only
  // used for compiling Wasm code at the moment. To keep the Wasm
  // simulation accurate, we need to make sure accessing a lane is correctly
  // simulated and as such we reverse the lane number on the getters and setters
  // below. We need to be careful when getting/setting values on the Low or High
  // side of a simulated register. In the simulation, "Low" is equal to the MSB
  // and "High" is equal to the LSB in memory. "force_ibm_lane_numbering" could
  // be used to disabled automatic lane number reversal and help with accessing
  // the Low or High side of a simulated register.
  template <class T>
  T get_simd_register_by_lane(int reg, int lane,
                              bool force_ibm_lane_numbering = true) {
    if (force_ibm_lane_numbering) {
      lane = (kSimd128Size / sizeof(T)) - 1 - lane;
    }
    CHECK_LE(lane, kSimd128Size / sizeof(T));
    CHECK_LT(reg, kNumSIMDRs);
    CHECK_GE(lane, 0);
    CHECK_GE(reg, 0);
    return (reinterpret_cast<T*>(&simd_registers_[reg]))[lane];
  }

  template <class T>
  T get_simd_register_bytes(int reg, int byte_from) {
    // Byte location is reversed in memory.
    int from = kSimd128Size - 1 - (byte_from + sizeof(T) - 1);
    void* src = reinterpret_cast<uint8_t*>(&simd_registers_[reg]) + from;
    T dst;
    memcpy(&dst, src, sizeof(T));
    return dst;
  }

  template <class T>
  void set_simd_register_by_lane(int reg, int lane, const T& value,
                                 bool force_ibm_lane_numbering = true) {
    if (force_ibm_lane_numbering) {
      lane = (kSimd128Size / sizeof(T)) - 1 - lane;
    }
    CHECK_LE(lane, kSimd128Size / sizeof(T));
    CHECK_LT(reg, kNumSIMDRs);
    CHECK_GE(lane, 0);
    CHECK_GE(reg, 0);
    (reinterpret_cast<T*>(&simd_registers_[reg]))[lane] = value;
  }

  template <class T>
  void set_simd_register_bytes(int reg, int byte_from, T value) {
    // Byte location is reversed in memory.
    int from = kSimd128Size - 1 - (byte_from + sizeof(T) - 1);
    void* dst = reinterpret_cast<uint8_t*>(&simd_registers_[reg]) + from;
    memcpy(dst, &value, sizeof(T));
  }

  simdr_t& get_simd_register(int reg) { return simd_registers_[reg]; }

  void set_simd_register(int reg, const simdr_t& value) {
    simd_registers_[reg] = value;
  }

  // Simulator support.
  uint8_t* stack_;
  static const size_t kStackProtectionSize = 256 * kSystemPointerSize;
  // This includes a protection margin at each end of the stack area.
  static size_t AllocatedStackSize() {
#if V8_TARGET_ARCH_PPC64
    size_t stack_size = v8_flags.sim_stack_size * KB;
#else
    size_t stack_size = MB;  // allocate 1MB for stack
#endif
    return stack_size + (2 * kStackProtectionSize);
  }
  static size_t UsableStackSize() {
    return AllocatedStackSize() - kStackProtectionSize;
  }
  bool pc_modified_;
  int icount_;

  // Debugger input.
  char* last_debugger_input_;

  // Registered breakpoints.
  Instruction* break_pc_;
  Instr break_instr_;

  v8::internal::Isolate* isolate_;

  // A stop is watched if its code is less than kNumOfWatchedStops.
  // Only watched stops support enabling/disabling and the counter feature.
  static const uint32_t kNumOfWatchedStops = 256;

  // Breakpoint is disabled if bit 31 is set.
  static const uint32_t kStopDisabledBit = 1 << 31;

  // A stop is enabled, meaning the simulator will stop when meeting the
  // instruction, if bit 31 of watched_stops_[code].count is unset.
  // The value watched_stops_[code].count & ~(1 << 31) indicates how many times
  // the breakpoint was hit or gone through.
  struct StopCountAndDesc {
    uint32_t count;
    char* desc;
  };
  StopCountAndDesc watched_stops_[kNumOfWatchedStops];

  // Synchronization primitives. See ARM DDI 0406C.b, A2.9.
  enum class MonitorAccess {
    Open,
    Exclusive,
  };

  enum class TransactionSize {
    None = 0,
    Byte = 1,
    HalfWord = 2,
    Word = 4,
    DWord = 8,
  };

  class GlobalMonitor {
   public:
    // Exposed so it can be accessed by Simulator::{Read,Write}Ex*.
    base::Mutex mutex;

    void NotifyLoadExcl(uintptr_t addr, TransactionSize size,
                        ThreadId thread_id);
    void NotifyStore(uintptr_t addr, TransactionSize size, ThreadId thread_id);
    bool NotifyStoreExcl(uintptr_t addr, TransactionSize size,
                         ThreadId thread_id);

    static GlobalMonitor* Get();

   private:
    // Private constructor. Call {GlobalMonitor::Get()} to get the singleton.
    GlobalMonitor() = default;
    friend class base::LeakyObject<GlobalMonitor>;

    void Clear();

    MonitorAccess access_state_ = MonitorAccess::Open;
    uintptr_t tagged_addr_ = 0;
    TransactionSize size_ = TransactionSize::None;
    ThreadId thread_id_ = ThreadId::Invalid();
  };
};

}  // namespace internal
}  // namespace v8

#endif  // defined(USE_SIMULATOR)
#endif  // V8_EXECUTION_PPC_SIMULATOR_PPC_H_
