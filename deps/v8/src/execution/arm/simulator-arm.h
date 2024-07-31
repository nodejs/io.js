// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Declares a Simulator for ARM instructions if we are not generating a native
// ARM binary. This Simulator allows us to run and debug ARM code generation on
// regular desktop machines.
// V8 calls into generated code by using the GeneratedCode class,
// which will start execution in the Simulator or forwards to the real entry
// on a ARM HW platform.

#ifndef V8_EXECUTION_ARM_SIMULATOR_ARM_H_
#define V8_EXECUTION_ARM_SIMULATOR_ARM_H_

// globals.h defines USE_SIMULATOR.
#include "src/common/globals.h"

#if defined(USE_SIMULATOR)
// Running with a simulator.

#include "src/base/hashmap.h"
#include "src/base/lazy-instance.h"
#include "src/base/platform/mutex.h"
#include "src/codegen/arm/constants-arm.h"
#include "src/execution/simulator-base.h"
#include "src/utils/allocation.h"
#include "src/utils/boxed-float.h"

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
  friend class ArmDebugger;
  enum Register {
    no_reg = -1,
    r0 = 0,
    r1,
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
    num_registers,
    fp = 11,
    ip = 12,
    sp = 13,
    lr = 14,
    pc = 15,
    s0 = 0,
    s1,
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
    s12,
    s13,
    s14,
    s15,
    s16,
    s17,
    s18,
    s19,
    s20,
    s21,
    s22,
    s23,
    s24,
    s25,
    s26,
    s27,
    s28,
    s29,
    s30,
    s31,
    num_s_registers = 32,
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
    num_d_registers = 32,
    q0 = 0,
    q1,
    q2,
    q3,
    q4,
    q5,
    q6,
    q7,
    q8,
    q9,
    q10,
    q11,
    q12,
    q13,
    q14,
    q15,
    num_q_registers = 16
  };

  explicit Simulator(Isolate* isolate);
  ~Simulator();

  // The currently executing Simulator instance. Potentially there can be one
  // for each native thread.
  V8_EXPORT_PRIVATE static Simulator* current(v8::internal::Isolate* isolate);

  // Accessors for register state. Reading the pc value adheres to the ARM
  // architecture specification and is off by a 8 from the currently executing
  // instruction.
  void set_register(int reg, int32_t value);
  V8_EXPORT_PRIVATE int32_t get_register(int reg) const;
  double get_double_from_register_pair(int reg);
  void set_register_pair_from_double(int reg, double* value);
  void set_dw_register(int dreg, const int* dbl);

  // Support for VFP.
  void get_d_register(int dreg, uint64_t* value);
  void set_d_register(int dreg, const uint64_t* value);
  void get_d_register(int dreg, uint32_t* value);
  void set_d_register(int dreg, const uint32_t* value);
  // Support for NEON.
  template <typename T, int SIZE = kSimd128Size>
  void get_neon_register(int reg, T (&value)[SIZE / sizeof(T)]);
  template <typename T, int SIZE = kSimd128Size>
  void set_neon_register(int reg, const T (&value)[SIZE / sizeof(T)]);

  void set_s_register(int reg, unsigned int value);
  unsigned int get_s_register(int reg) const;

  void set_d_register_from_double(int dreg, const Float64 dbl) {
    SetVFPRegister<Float64, 2>(dreg, dbl);
  }
  void set_d_register_from_double(int dreg, const double dbl) {
    SetVFPRegister<double, 2>(dreg, dbl);
  }

  Float64 get_double_from_d_register(int dreg) {
    return GetFromVFPRegister<Float64, 2>(dreg);
  }

  void set_s_register_from_float(int sreg, const Float32 flt) {
    SetVFPRegister<Float32, 1>(sreg, flt);
  }
  void set_s_register_from_float(int sreg, const float flt) {
    SetVFPRegister<float, 1>(sreg, flt);
  }

  Float32 get_float_from_s_register(int sreg) {
    return GetFromVFPRegister<Float32, 1>(sreg);
  }

  void set_s_register_from_sinteger(int sreg, const int sint) {
    SetVFPRegister<int, 1>(sreg, sint);
  }

  int get_sinteger_from_s_register(int sreg) {
    return GetFromVFPRegister<int, 1>(sreg);
  }

  // Special case of set_register and get_register to access the raw PC value.
  void set_pc(int32_t value);
  V8_EXPORT_PRIVATE int32_t get_pc() const;

  Address get_sp() const { return static_cast<Address>(get_register(sp)); }

  // Accessor to the internal simulator stack area. Adds a safety
  // margin to prevent overflows (kAdditionalStackMargin).
  uintptr_t StackLimit(uintptr_t c_limit) const;

  // Return current stack view, without additional safety margins.
  // Users, for example wasm::StackMemory, can add their own.
  base::Vector<uint8_t> GetCurrentStackView() const;

  // Executes ARM instructions until the PC reaches end_sim_pc.
  void Execute();

  template <typename Return, typename... Args>
  Return Call(Address entry, Args... args) {
    return VariadicCall<Return>(this, &Simulator::CallImpl, entry, args...);
  }

  // Alternative: call a 2-argument double function.
  template <typename Return>
  Return CallFP(Address entry, double d0, double d1) {
    return ConvertReturn<Return>(CallFPImpl(entry, d0, d1));
  }

  // Push an address onto the JS stack.
  uintptr_t PushAddress(uintptr_t address);

  // Pop an address from the JS stack.
  uintptr_t PopAddress();

  // Debugger input.
  void set_last_debugger_input(ArrayUniquePtr<char> input) {
    last_debugger_input_ = std::move(input);
  }
  const char* last_debugger_input() { return last_debugger_input_.get(); }

  // Redirection support.
  static void SetRedirectInstruction(Instruction* instruction);

  // ICache checking.
  static bool ICacheMatch(void* one, void* two);
  static void FlushICache(base::CustomMatcherHashMap* i_cache, void* start,
                          size_t size);

  // Returns true if pc register contains one of the 'special_values' defined
  // below (bad_lr, end_sim_pc).
  bool has_bad_pc() const;

  // EABI variant for double arguments in use.
  bool use_eabi_hardfloat() {
#if USE_EABI_HARDFLOAT
    return true;
#else
    return false;
#endif
  }

  // Manage instruction tracing.
  bool InstructionTracingEnabled();

  void ToggleInstructionTracing();

 private:
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

  V8_EXPORT_PRIVATE intptr_t CallImpl(Address entry, int argument_count,
                                      const intptr_t* arguments);
  intptr_t CallFPImpl(Address entry, double d0, double d1);

  // Unsupported instructions use Format to print an error and stop execution.
  void Format(Instruction* instr, const char* format);

  // Checks if the current instruction should be executed based on its
  // condition bits.
  inline bool ConditionallyExecute(Instruction* instr);

  // Helper functions to set the conditional flags in the architecture state.
  void SetNZFlags(int32_t val);
  void SetCFlag(bool val);
  void SetVFlag(bool val);
  bool CarryFrom(int32_t left, int32_t right, int32_t carry = 0);
  bool BorrowFrom(int32_t left, int32_t right, int32_t carry = 1);
  bool OverflowFrom(int32_t alu_out, int32_t left, int32_t right,
                    bool addition);

  inline int GetCarry() { return c_flag_ ? 1 : 0; }

  // Support for VFP.
  void Compute_FPSCR_Flags(float val1, float val2);
  void Compute_FPSCR_Flags(double val1, double val2);
  void Copy_FPSCR_to_APSR();
  inline float canonicalizeNaN(float value);
  inline double canonicalizeNaN(double value);
  inline Float32 canonicalizeNaN(Float32 value);
  inline Float64 canonicalizeNaN(Float64 value);

  // Helper functions to decode common "addressing" modes
  int32_t GetShiftRm(Instruction* instr, bool* carry_out);
  int32_t GetImm(Instruction* instr, bool* carry_out);
  int32_t ProcessPU(Instruction* instr, int num_regs, int operand_size,
                    intptr_t* start_address, intptr_t* end_address);
  void HandleRList(Instruction* instr, bool load);
  void HandleVList(Instruction* inst);
  void SoftwareInterrupt(Instruction* instr);
  void DebugAtNextPC();

  // Take a copy of v8 simulator tracing flag because flags are frozen after
  // start.
  bool instruction_tracing_ = v8_flags.trace_sim;

  // Helper to write back values to register.
  void AdvancedSIMDElementOrStructureLoadStoreWriteback(int Rn, int Rm,
                                                        int ebytes);

  // Stop helper functions.
  inline bool isWatchedStop(uint32_t bkpt_code);
  inline bool isEnabledStop(uint32_t bkpt_code);
  inline void EnableStop(uint32_t bkpt_code);
  inline void DisableStop(uint32_t bkpt_code);
  inline void IncreaseStopCounter(uint32_t bkpt_code);
  void PrintStopInfo(uint32_t code);

  // Read and write memory.
  // The *Ex functions are exclusive access. The writes return the strex status:
  // 0 if the write succeeds, and 1 if the write fails.
  inline uint8_t ReadBU(int32_t addr);
  inline int8_t ReadB(int32_t addr);
  uint8_t ReadExBU(int32_t addr);
  inline void WriteB(int32_t addr, uint8_t value);
  inline void WriteB(int32_t addr, int8_t value);
  int WriteExB(int32_t addr, uint8_t value);

  inline uint16_t ReadHU(int32_t addr);
  inline int16_t ReadH(int32_t addr);
  uint16_t ReadExHU(int32_t addr);
  // Note: Overloaded on the sign of the value.
  inline void WriteH(int32_t addr, uint16_t value);
  inline void WriteH(int32_t addr, int16_t value);
  int WriteExH(int32_t addr, uint16_t value);

  inline int ReadW(int32_t addr);
  int ReadExW(int32_t addr);
  inline void WriteW(int32_t addr, int value);
  int WriteExW(int32_t addr, int value);

  int32_t* ReadDW(int32_t addr);
  void WriteDW(int32_t addr, int32_t value1, int32_t value2);
  int32_t* ReadExDW(int32_t addr);
  int WriteExDW(int32_t addr, int32_t value1, int32_t value2);

  // Executing is handled based on the instruction type.
  // Both type 0 and type 1 rolled into one.
  void DecodeType01(Instruction* instr);
  void DecodeType2(Instruction* instr);
  void DecodeType3(Instruction* instr);
  void DecodeType4(Instruction* instr);
  void DecodeType5(Instruction* instr);
  void DecodeType6(Instruction* instr);
  void DecodeType7(Instruction* instr);

  // CP15 coprocessor instructions.
  void DecodeTypeCP15(Instruction* instr);

  // Support for VFP.
  void DecodeTypeVFP(Instruction* instr);
  void DecodeType6CoprocessorIns(Instruction* instr);
  void DecodeSpecialCondition(Instruction* instr);

  void DecodeFloatingPointDataProcessing(Instruction* instr);
  void DecodeUnconditional(Instruction* instr);
  void DecodeAdvancedSIMDDataProcessing(Instruction* instr);
  void DecodeMemoryHintsAndBarriers(Instruction* instr);
  void DecodeAdvancedSIMDElementOrStructureLoadStore(Instruction* instr);
  void DecodeAdvancedSIMDLoadStoreMultipleStructures(Instruction* instr);
  void DecodeAdvancedSIMDLoadSingleStructureToAllLanes(Instruction* instr);
  void DecodeAdvancedSIMDLoadStoreSingleStructureToOneLane(Instruction* instr);
  void DecodeAdvancedSIMDTwoOrThreeRegisters(Instruction* instr);

  void DecodeVMOVBetweenCoreAndSinglePrecisionRegisters(Instruction* instr);
  void DecodeVCMP(Instruction* instr);
  void DecodeVCVTBetweenDoubleAndSingle(Instruction* instr);
  int32_t ConvertDoubleToInt(double val, bool unsigned_integer,
                             VFPRoundingMode mode);
  void DecodeVCVTBetweenFloatingPointAndInteger(Instruction* instr);

  // Executes one instruction.
  void InstructionDecode(Instruction* instr);

  // ICache.
  static void CheckICache(base::CustomMatcherHashMap* i_cache,
                          Instruction* instr);
  static void FlushOnePage(base::CustomMatcherHashMap* i_cache, intptr_t start,
                           int size);
  static CachePage* GetCachePage(base::CustomMatcherHashMap* i_cache,
                                 void* page);

  // Handle arguments and return value for runtime FP functions.
  void GetFpArgs(double* x, double* y, int32_t* z);
  void SetFpResult(const double& result);
  void TrashCallerSaveRegisters();

  template <class ReturnType, int register_size>
  ReturnType GetFromVFPRegister(int reg_index);

  template <class InputType, int register_size>
  void SetVFPRegister(int reg_index, const InputType& value);

  void SetSpecialRegister(SRegisterFieldMask reg_and_mask, uint32_t value);
  uint32_t GetFromSpecialRegister(SRegister reg);

  void CallInternal(Address entry);

  // Architecture state.
  // Saturating instructions require a Q flag to indicate saturation.
  // There is currently no way to read the CPSR directly, and thus read the Q
  // flag, so this is left unimplemented.
  int32_t registers_[16];
  bool n_flag_;
  bool z_flag_;
  bool c_flag_;
  bool v_flag_;

  // VFP architecture state.
  unsigned int vfp_registers_[num_d_registers * 2];
  bool n_flag_FPSCR_;
  bool z_flag_FPSCR_;
  bool c_flag_FPSCR_;
  bool v_flag_FPSCR_;

  // VFP rounding mode. See ARM DDI 0406B Page A2-29.
  VFPRoundingMode FPSCR_rounding_mode_;
  bool FPSCR_default_NaN_mode_;

  // VFP FP exception flags architecture state.
  bool inv_op_vfp_flag_;
  bool div_zero_vfp_flag_;
  bool overflow_vfp_flag_;
  bool underflow_vfp_flag_;
  bool inexact_vfp_flag_;

  // Simulator support for the stack.
  uint8_t* stack_;
  static const size_t kAllocatedStackSize = 1 * MB;
  // We leave a small buffer below the usable stack to protect against potential
  // stack underflows.
  static const int kStackMargin = 64;
  // Added in Simulator::StackLimit()
  static const int kAdditionalStackMargin = 4 * KB;
  static const size_t kUsableStackSize = kAllocatedStackSize - kStackMargin;
  bool pc_modified_;
  int icount_;

  // Debugger input.
  ArrayUniquePtr<char> last_debugger_input_;

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
    DoubleWord = 8,
  };

  // The least-significant bits of the address are ignored. The number of bits
  // is implementation-defined, between 3 and 11. See ARM DDI 0406C.b, A3.4.3.
  static const int32_t kExclusiveTaggedAddrMask = ~((1 << 11) - 1);

  class LocalMonitor {
   public:
    LocalMonitor();

    // These functions manage the state machine for the local monitor, but do
    // not actually perform loads and stores. NotifyStoreExcl only returns
    // true if the exclusive store is allowed; the global monitor will still
    // have to be checked to see whether the memory should be updated.
    void NotifyLoad(int32_t addr);
    void NotifyLoadExcl(int32_t addr, TransactionSize size);
    void NotifyStore(int32_t addr);
    bool NotifyStoreExcl(int32_t addr, TransactionSize size);

   private:
    void Clear();

    MonitorAccess access_state_;
    int32_t tagged_addr_;
    TransactionSize size_;
  };

  class GlobalMonitor {
   public:
    class Processor {
     public:
      Processor();

     private:
      friend class GlobalMonitor;
      // These functions manage the state machine for the global monitor, but do
      // not actually perform loads and stores.
      void Clear_Locked();
      void NotifyLoadExcl_Locked(int32_t addr);
      void NotifyStore_Locked(int32_t addr, bool is_requesting_processor);
      bool NotifyStoreExcl_Locked(int32_t addr, bool is_requesting_processor);

      MonitorAccess access_state_;
      int32_t tagged_addr_;
      Processor* next_;
      Processor* prev_;
      // A strex can fail due to background cache evictions. Rather than
      // simulating this, we'll just occasionally introduce cases where an
      // exclusive store fails. This will happen once after every
      // kMaxFailureCounter exclusive stores.
      static const int kMaxFailureCounter = 5;
      int failure_counter_;
    };

    // Exposed so it can be accessed by Simulator::{Read,Write}Ex*.
    base::Mutex mutex;

    void NotifyLoadExcl_Locked(int32_t addr, Processor* processor);
    void NotifyStore_Locked(int32_t addr, Processor* processor);
    bool NotifyStoreExcl_Locked(int32_t addr, Processor* processor);

    // Called when the simulator is destroyed.
    void RemoveProcessor(Processor* processor);

    static GlobalMonitor* Get();

   private:
    // Private constructor. Call {GlobalMonitor::Get()} to get the singleton.
    GlobalMonitor() = default;
    friend class base::LeakyObject<GlobalMonitor>;

    bool IsProcessorInLinkedList_Locked(Processor* processor) const;
    void PrependProcessor_Locked(Processor* processor);

    Processor* head_ = nullptr;
  };

  LocalMonitor local_monitor_;
  GlobalMonitor::Processor global_monitor_processor_;
};

}  // namespace internal
}  // namespace v8

#endif  // defined(USE_SIMULATOR)
#endif  // V8_EXECUTION_ARM_SIMULATOR_ARM_H_
