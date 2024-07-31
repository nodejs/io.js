// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Declares a Simulator for loongisa instructions if we are not generating a
// native loongisa binary. This Simulator allows us to run and debug loongisa
// code generation on regular desktop machines. V8 calls into generated code via
// the GeneratedCode wrapper, which will start execution in the Simulator or
// forwards to the real entry on a loongisa HW platform.

#ifndef V8_EXECUTION_LOONG64_SIMULATOR_LOONG64_H_
#define V8_EXECUTION_LOONG64_SIMULATOR_LOONG64_H_

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
// Running with a simulator.

#include "src/base/hashmap.h"
#include "src/base/strings.h"
#include "src/codegen/assembler.h"
#include "src/codegen/loong64/constants-loong64.h"
#include "src/execution/simulator-base.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

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
  friend class Loong64Debugger;

  // Registers are declared in order.
  enum Register {
    no_reg = -1,
    zero_reg = 0,
    ra,
    gp,
    sp,
    a0,
    a1,
    a2,
    a3,
    a4,
    a5,
    a6,
    a7,
    t0,
    t1,
    t2,
    t3,
    t4,
    t5,
    t6,
    t7,
    t8,
    tp,
    fp,
    s0,
    s1,
    s2,
    s3,
    s4,
    s5,
    s6,
    s7,
    s8,
    pc,  // pc must be the last register.
    kNumSimuRegisters,
    // aliases
    v0 = a0,
    v1 = a1
  };

  // Condition flag registers.
  enum CFRegister {
    fcc0,
    fcc1,
    fcc2,
    fcc3,
    fcc4,
    fcc5,
    fcc6,
    fcc7,
    kNumCFRegisters
  };

  // Floating point registers.
  enum FPURegister {
    f0,
    f1,
    f2,
    f3,
    f4,
    f5,
    f6,
    f7,
    f8,
    f9,
    f10,
    f11,
    f12,
    f13,
    f14,
    f15,
    f16,
    f17,
    f18,
    f19,
    f20,
    f21,
    f22,
    f23,
    f24,
    f25,
    f26,
    f27,
    f28,
    f29,
    f30,
    f31,
    kNumFPURegisters
  };

  explicit Simulator(Isolate* isolate);
  ~Simulator();

  // The currently executing Simulator instance. Potentially there can be one
  // for each native thread.
  V8_EXPORT_PRIVATE static Simulator* current(v8::internal::Isolate* isolate);

  float ceil(float value);
  float floor(float value);
  float trunc(float value);
  double ceil(double value);
  double floor(double value);
  double trunc(double value);

  // Accessors for register state. Reading the pc value adheres to the LOONG64
  // architecture specification and is off by a 8 from the currently executing
  // instruction.
  void set_register(int reg, int64_t value);
  void set_register_word(int reg, int32_t value);
  void set_dw_register(int dreg, const int* dbl);
  V8_EXPORT_PRIVATE int64_t get_register(int reg) const;
  double get_double_from_register_pair(int reg);
  // Same for FPURegisters.
  void set_fpu_register(int fpureg, int64_t value);
  void set_fpu_register_word(int fpureg, int32_t value);
  void set_fpu_register_hi_word(int fpureg, int32_t value);
  void set_fpu_register_float(int fpureg, float value);
  void set_fpu_register_double(int fpureg, double value);
  void set_fpu_register_invalid_result64(float original, float rounded);
  void set_fpu_register_invalid_result(float original, float rounded);
  void set_fpu_register_word_invalid_result(float original, float rounded);
  void set_fpu_register_invalid_result64(double original, double rounded);
  void set_fpu_register_invalid_result(double original, double rounded);
  void set_fpu_register_word_invalid_result(double original, double rounded);
  int64_t get_fpu_register(int fpureg) const;
  int32_t get_fpu_register_word(int fpureg) const;
  int32_t get_fpu_register_signed_word(int fpureg) const;
  int32_t get_fpu_register_hi_word(int fpureg) const;
  float get_fpu_register_float(int fpureg) const;
  double get_fpu_register_double(int fpureg) const;
  void set_cf_register(int cfreg, bool value);
  bool get_cf_register(int cfreg) const;
  void set_fcsr_rounding_mode(FPURoundingMode mode);
  unsigned int get_fcsr_rounding_mode();
  void set_fcsr_bit(uint32_t cc, bool value);
  bool test_fcsr_bit(uint32_t cc);
  bool set_fcsr_round_error(double original, double rounded);
  bool set_fcsr_round64_error(double original, double rounded);
  bool set_fcsr_round_error(float original, float rounded);
  bool set_fcsr_round64_error(float original, float rounded);
  void round_according_to_fcsr(double toRound, double* rounded,
                               int32_t* rounded_int);
  void round64_according_to_fcsr(double toRound, double* rounded,
                                 int64_t* rounded_int);
  void round_according_to_fcsr(float toRound, float* rounded,
                               int32_t* rounded_int);
  void round64_according_to_fcsr(float toRound, float* rounded,
                                 int64_t* rounded_int);
  // Special case of set_register and get_register to access the raw PC value.
  void set_pc(int64_t value);
  V8_EXPORT_PRIVATE int64_t get_pc() const;

  Address get_sp() const { return static_cast<Address>(get_register(sp)); }

  // Accessor to the internal simulator stack area. Adds a safety
  // margin to prevent overflows (kAdditionalStackMargin).
  uintptr_t StackLimit(uintptr_t c_limit) const;

  // Return current stack view, without additional safety margins.
  // Users, for example wasm::StackMemory, can add their own.
  base::Vector<uint8_t> GetCurrentStackView() const;

  // Executes LOONG64 instructions until the PC reaches end_sim_pc.
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
    // Convert all arguments to CallArgument.
    CallArgument call_args[] = {CallArgument(args)..., CallArgument::End()};
    CallImpl(entry, call_args);
    return ReadReturn<Return>();
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

  V8_EXPORT_PRIVATE void CallImpl(Address entry, CallArgument* args);

  void CallAnyCTypeFunction(Address target_address,
                            const EncodedCSignature& signature);

  // Read floating point return values.
  template <typename T>
  typename std::enable_if<std::is_floating_point<T>::value, T>::type
  ReadReturn() {
    return static_cast<T>(get_fpu_register_double(f0));
  }
  // Read non-float return values.
  template <typename T>
  typename std::enable_if<!std::is_floating_point<T>::value, T>::type
  ReadReturn() {
    return ConvertReturn<T>(get_register(a0));
  }

  // Unsupported instructions use Format to print an error and stop execution.
  void Format(Instruction* instr, const char* format);

  // Helpers for data value tracing.
  enum TraceType {
    BYTE,
    HALF,
    WORD,
    DWORD,
    FLOAT,
    DOUBLE,
    FLOAT_DOUBLE,
    WORD_DWORD
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

  // Read and write memory.
  inline uint32_t ReadBU(int64_t addr);
  inline int32_t ReadB(int64_t addr);
  inline void WriteB(int64_t addr, uint8_t value);
  inline void WriteB(int64_t addr, int8_t value);

  inline uint16_t ReadHU(int64_t addr, Instruction* instr);
  inline int16_t ReadH(int64_t addr, Instruction* instr);
  // Note: Overloaded on the sign of the value.
  inline void WriteH(int64_t addr, uint16_t value, Instruction* instr);
  inline void WriteH(int64_t addr, int16_t value, Instruction* instr);

  inline uint32_t ReadWU(int64_t addr, Instruction* instr);
  inline int32_t ReadW(int64_t addr, Instruction* instr, TraceType t = WORD);
  inline void WriteW(int64_t addr, int32_t value, Instruction* instr);
  void WriteConditionalW(int64_t addr, int32_t value, Instruction* instr,
                         int32_t* done);
  inline int64_t Read2W(int64_t addr, Instruction* instr);
  inline void Write2W(int64_t addr, int64_t value, Instruction* instr);
  inline void WriteConditional2W(int64_t addr, int64_t value,
                                 Instruction* instr, int32_t* done);

  inline double ReadD(int64_t addr, Instruction* instr);
  inline void WriteD(int64_t addr, double value, Instruction* instr);

  template <typename T>
  T ReadMem(int64_t addr, Instruction* instr);
  template <typename T>
  void WriteMem(int64_t addr, T value, Instruction* instr);

  // Helper for debugging memory access.
  inline void DieOrDebug();

  void TraceRegWr(int64_t value, TraceType t = DWORD);
  void TraceMemWr(int64_t addr, int64_t value, TraceType t);
  void TraceMemRd(int64_t addr, int64_t value, TraceType t = DWORD);
  template <typename T>
  void TraceMemRd(int64_t addr, T value);
  template <typename T>
  void TraceMemWr(int64_t addr, T value);

  SimInstruction instr_;

  // Executing is handled based on the instruction type.
  void DecodeTypeOp6();
  void DecodeTypeOp7();
  void DecodeTypeOp8();
  void DecodeTypeOp10();
  void DecodeTypeOp12();
  void DecodeTypeOp14();
  void DecodeTypeOp17();
  void DecodeTypeOp22();

  inline int32_t rj_reg() const { return instr_.RjValue(); }
  inline int64_t rj() const { return get_register(rj_reg()); }
  inline uint64_t rj_u() const {
    return static_cast<uint64_t>(get_register(rj_reg()));
  }
  inline int32_t rk_reg() const { return instr_.RkValue(); }
  inline int64_t rk() const { return get_register(rk_reg()); }
  inline uint64_t rk_u() const {
    return static_cast<uint64_t>(get_register(rk_reg()));
  }
  inline int32_t rd_reg() const { return instr_.RdValue(); }
  inline int64_t rd() const { return get_register(rd_reg()); }
  inline uint64_t rd_u() const {
    return static_cast<uint64_t>(get_register(rd_reg()));
  }
  inline int32_t fa_reg() const { return instr_.FaValue(); }
  inline float fa_float() const { return get_fpu_register_float(fa_reg()); }
  inline double fa_double() const { return get_fpu_register_double(fa_reg()); }
  inline int32_t fj_reg() const { return instr_.FjValue(); }
  inline float fj_float() const { return get_fpu_register_float(fj_reg()); }
  inline double fj_double() const { return get_fpu_register_double(fj_reg()); }
  inline int32_t fk_reg() const { return instr_.FkValue(); }
  inline float fk_float() const { return get_fpu_register_float(fk_reg()); }
  inline double fk_double() const { return get_fpu_register_double(fk_reg()); }
  inline int32_t fd_reg() const { return instr_.FdValue(); }
  inline float fd_float() const { return get_fpu_register_float(fd_reg()); }
  inline double fd_double() const { return get_fpu_register_double(fd_reg()); }
  inline int32_t cj_reg() const { return instr_.CjValue(); }
  inline bool cj() const { return get_cf_register(cj_reg()); }
  inline int32_t cd_reg() const { return instr_.CdValue(); }
  inline bool cd() const { return get_cf_register(cd_reg()); }
  inline int32_t ca_reg() const { return instr_.CaValue(); }
  inline bool ca() const { return get_cf_register(ca_reg()); }
  inline uint32_t sa2() const { return instr_.Sa2Value(); }
  inline uint32_t sa3() const { return instr_.Sa3Value(); }
  inline uint32_t ui5() const { return instr_.Ui5Value(); }
  inline uint32_t ui6() const { return instr_.Ui6Value(); }
  inline uint32_t lsbw() const { return instr_.LsbwValue(); }
  inline uint32_t msbw() const { return instr_.MsbwValue(); }
  inline uint32_t lsbd() const { return instr_.LsbdValue(); }
  inline uint32_t msbd() const { return instr_.MsbdValue(); }
  inline uint32_t cond() const { return instr_.CondValue(); }
  inline int32_t si12() const { return (instr_.Si12Value() << 20) >> 20; }
  inline uint32_t ui12() const { return instr_.Ui12Value(); }
  inline int32_t si14() const { return (instr_.Si14Value() << 18) >> 18; }
  inline int32_t si16() const { return (instr_.Si16Value() << 16) >> 16; }
  inline int32_t si20() const { return (instr_.Si20Value() << 12) >> 12; }

  inline void SetResult(const int32_t rd_reg, const int64_t alu_out) {
    set_register(rd_reg, alu_out);
    TraceRegWr(alu_out);
  }

  inline void SetFPUWordResult(int32_t fd_reg, int32_t alu_out) {
    set_fpu_register_word(fd_reg, alu_out);
    TraceRegWr(get_fpu_register(fd_reg), WORD);
  }

  inline void SetFPUWordResult2(int32_t fd_reg, int32_t alu_out) {
    set_fpu_register_word(fd_reg, alu_out);
    TraceRegWr(get_fpu_register(fd_reg));
  }

  inline void SetFPUResult(int32_t fd_reg, int64_t alu_out) {
    set_fpu_register(fd_reg, alu_out);
    TraceRegWr(get_fpu_register(fd_reg));
  }

  inline void SetFPUResult2(int32_t fd_reg, int64_t alu_out) {
    set_fpu_register(fd_reg, alu_out);
    TraceRegWr(get_fpu_register(fd_reg), DOUBLE);
  }

  inline void SetFPUFloatResult(int32_t fd_reg, float alu_out) {
    set_fpu_register_float(fd_reg, alu_out);
    TraceRegWr(get_fpu_register(fd_reg), FLOAT);
  }

  inline void SetFPUDoubleResult(int32_t fd_reg, double alu_out) {
    set_fpu_register_double(fd_reg, alu_out);
    TraceRegWr(get_fpu_register(fd_reg), DOUBLE);
  }

  // Used for breakpoints.
  void SoftwareInterrupt();

  // Stop helper functions.
  bool IsWatchpoint(uint64_t code);
  void PrintWatchpoint(uint64_t code);
  void HandleStop(uint64_t code, Instruction* instr);
  bool IsStopInstruction(Instruction* instr);
  bool IsEnabledStop(uint64_t code);
  void EnableStop(uint64_t code);
  void DisableStop(uint64_t code);
  void IncreaseStopCounter(uint64_t code);
  void PrintStopInfo(uint64_t code);

  // Executes one instruction.
  void InstructionDecode(Instruction* instr);
  // Execute one instruction placed in a branch delay slot.

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
    kNumExceptions
  };

  // Exceptions.
  void SignalException(Exception e);

  // Handle arguments and return value for runtime FP functions.
  void GetFpArgs(double* x, double* y, int32_t* z);
  void SetFpResult(const double& result);

  void CallInternal(Address entry);

  // Architecture state.
  // Registers.
  int64_t registers_[kNumSimuRegisters];
  // Floating point Registers.
  int64_t FPUregisters_[kNumFPURegisters];
  // Condition flags Registers.
  bool CFregisters_[kNumCFRegisters];
  // FPU control register.
  uint32_t FCSR_;

  // Simulator support.
  uintptr_t stack_;
  static const size_t kStackProtectionSize = KB;
  // This includes a protection margin at each end of the stack area.
  static size_t AllocatedStackSize() {
    return (v8_flags.sim_stack_size * KB) + (2 * kStackProtectionSize);
  }
  static size_t UsableStackSize() { return v8_flags.sim_stack_size * KB; }
  uintptr_t stack_limit_;
  // Added in Simulator::StackLimit()
  static const int kAdditionalStackMargin = 4 * KB;

  bool pc_modified_;
  int64_t icount_;
  int break_count_;
  base::EmbeddedVector<char, 128> trace_buf_;

  // Debugger input.
  char* last_debugger_input_;

  v8::internal::Isolate* isolate_;

  // Registered breakpoints.
  Instruction* break_pc_;
  Instr break_instr_;

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
#endif  // V8_EXECUTION_LOONG64_SIMULATOR_LOONG64_H_
