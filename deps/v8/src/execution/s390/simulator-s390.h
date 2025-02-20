// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Declares a Simulator for S390 instructions if we are not generating a native
// S390 binary. This Simulator allows us to run and debug S390 code generation
// on regular desktop machines.
// V8 calls into generated code via the GeneratedCode wrapper,
// which will start execution in the Simulator or forwards to the real entry
// on a S390 hardware platform.

#ifndef V8_EXECUTION_S390_SIMULATOR_S390_H_
#define V8_EXECUTION_S390_SIMULATOR_S390_H_

// globals.h defines USE_SIMULATOR.
#include "src/common/globals.h"

#if defined(USE_SIMULATOR)
// Running with a simulator.

#include "src/base/hashmap.h"
#include "src/codegen/assembler.h"
#include "src/codegen/s390/constants-s390.h"
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

template <class T>
static T ComputeRounding(T a, int mode) {
  switch (mode) {
    case ROUND_TO_NEAREST_AWAY_FROM_0:
      return std::round(a);
    case ROUND_TO_NEAREST_TO_EVEN:
      return std::nearbyint(a);
    case ROUND_TOWARD_0:
      return std::trunc(a);
    case ROUND_TOWARD_POS_INF:
      return std::ceil(a);
    case ROUND_TOWARD_NEG_INF:
      return std::floor(a);
    default:
      UNIMPLEMENTED();
  }
  return 0;
}

class Simulator : public SimulatorBase {
 public:
  friend class S390Debugger;
  enum Register {
    no_reg = -1,
    r0 = 0,
    r1 = 1,
    r2 = 2,
    r3 = 3,
    r4 = 4,
    r5 = 5,
    r6 = 6,
    r7 = 7,
    r8 = 8,
    r9 = 9,
    r10 = 10,
    r11 = 11,
    r12 = 12,
    r13 = 13,
    r14 = 14,
    r15 = 15,
    fp = r11,
    ip = r12,
    cp = r13,
    ra = r14,
    sp = r15,  // name aliases
    kNumGPRs = 16,
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
    kNumFPRs = 16
  };

  explicit Simulator(Isolate* isolate);
  ~Simulator();

  // The currently executing Simulator instance. Potentially there can be one
  // for each native thread.
  static Simulator* current(v8::internal::Isolate* isolate);

  // Accessors for register state.
  void set_register(int reg, uint64_t value);
  const uint64_t& get_register(int reg) const;
  uint64_t& get_register(int reg);
  template <typename T>
  T get_low_register(int reg) const;
  template <typename T>
  T get_high_register(int reg) const;
  void set_low_register(int reg, uint32_t value);
  void set_high_register(int reg, uint32_t value);

  double get_double_from_register_pair(int reg);

  // Unlike Integer values, Floating Point values are located on the left most
  // side of a native 64 bit register. As FP registers are a subset of vector
  // registers, 64 and 32 bit FP values need to be located on first lane (lane
  // number 0) of a vector register.
  template <class T>
  T get_fpr(int dreg) {
    DCHECK(dreg >= 0 && dreg < kNumFPRs);
    return get_simd_register_by_lane<T>(dreg, 0);
  }

  template <class T>
  void set_fpr(int dreg, const T val) {
    DCHECK(dreg >= 0 && dreg < kNumFPRs);
    set_simd_register_by_lane<T>(dreg, 0, val);
  }

  // Special case of set_register and get_register to access the raw PC value.
  void set_pc(intptr_t value);
  intptr_t get_pc() const;

  Address get_sp() const { return static_cast<Address>(get_register(sp)); }

  // Accessor to the internal simulator stack area. Adds a safety
  // margin to prevent overflows.
  uintptr_t StackLimit(uintptr_t c_limit) const;
  // Return central stack view, without additional safety margins.
  // Users, for example wasm::StackMemory, can add their own.
  base::Vector<uint8_t> GetCentralStackView() const;

  // Executes S390 instructions until the PC reaches end_sim_pc.
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
  V8_EXPORT_PRIVATE uintptr_t PushAddress(uintptr_t address);

  // Pop an address from the JS stack.
  V8_EXPORT_PRIVATE uintptr_t PopAddress();

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

  // Unsupported instructions use Format to print an error and stop execution.
  void Format(Instruction* instr, const char* format);

  // Helper functions to set the conditional flags in the architecture state.
  bool CarryFrom(int32_t left, int32_t right, int32_t carry = 0);
  bool BorrowFrom(int32_t left, int32_t right);
  template <typename T1>
  inline bool OverflowFromSigned(T1 alu_out, T1 left, T1 right, bool addition);

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
  inline uint8_t ReadBU(intptr_t addr);
  inline int8_t ReadB(intptr_t addr);
  inline void WriteB(intptr_t addr, uint8_t value);
  inline void WriteB(intptr_t addr, int8_t value);

  inline uint16_t ReadHU(intptr_t addr);
  inline int16_t ReadH(intptr_t addr);
  // Note: Overloaded on the sign of the value.
  inline void WriteH(intptr_t addr, uint16_t value);
  inline void WriteH(intptr_t addr, int16_t value);

  inline uint32_t ReadWU(intptr_t addr);
  inline int32_t ReadW(intptr_t addr);
  inline int64_t ReadW64(intptr_t addr);
  inline void WriteW(intptr_t addr, uint32_t value);
  inline void WriteW(intptr_t addr, int32_t value);

  inline int64_t ReadDW(intptr_t addr);
  inline double ReadDouble(intptr_t addr);
  inline float ReadFloat(intptr_t addr);
  inline void WriteDW(intptr_t addr, int64_t value);

  // S390
  void Trace(Instruction* instr);

  template <typename T>
  void SetS390ConditionCode(T lhs, T rhs) {
    condition_reg_ = 0;
    if (lhs == rhs) {
      condition_reg_ |= CC_EQ;
    } else if (lhs < rhs) {
      condition_reg_ |= CC_LT;
    } else if (lhs > rhs) {
      condition_reg_ |= CC_GT;
    }

    // We get down here only for floating point
    // comparisons and the values are unordered
    // i.e. NaN
    if (condition_reg_ == 0) condition_reg_ = unordered;
  }

  // Used by arithmetic operations that use carry.
  template <typename T>
  void SetS390ConditionCodeCarry(T result, bool overflow) {
    condition_reg_ = 0;
    bool zero_result = (result == static_cast<T>(0));
    if (zero_result && !overflow) {
      condition_reg_ |= 8;
    } else if (!zero_result && !overflow) {
      condition_reg_ |= 4;
    } else if (zero_result && overflow) {
      condition_reg_ |= 2;
    } else if (!zero_result && overflow) {
      condition_reg_ |= 1;
    }
    if (condition_reg_ == 0) UNREACHABLE();
  }

  bool isNaN(double value) { return (value != value); }

  // Set the condition code for bitwise operations
  // CC0 is set if value == 0.
  // CC1 is set if value != 0.
  // CC2/CC3 are not set.
  template <typename T>
  void SetS390BitWiseConditionCode(T value) {
    condition_reg_ = 0;

    if (value == 0)
      condition_reg_ |= CC_EQ;
    else
      condition_reg_ |= CC_LT;
  }

  void SetS390OverflowCode(bool isOF) {
    if (isOF) condition_reg_ = CC_OF;
  }

  bool TestConditionCode(Condition mask) {
    // Check for unconditional branch
    if (mask == 0xf) return true;

    return (condition_reg_ & mask) != 0;
  }

  // Executes one instruction.
  void ExecuteInstruction(Instruction* instr, bool auto_incr_pc = true);

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

  void CallInternal(Address entry, int reg_arg_count = 3);

  // Architecture state.
  // On z9 and higher and supported Linux on z Systems platforms, all registers
  // are 64-bit, even in 31-bit mode.
  uint64_t registers_[kNumGPRs];
  union fpr_t {
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
  fpr_t fp_registers_[kNumFPRs];

  static constexpr fpr_t fp_zero = {{0}};

  fpr_t get_simd_register(int reg) { return fp_registers_[reg]; }

  void set_simd_register(int reg, const fpr_t& value) {
    fp_registers_[reg] = value;
  }

  // Vector register lane numbers on IBM machines are reversed compared to
  // x64. For example, doing an I32x4 extract_lane with lane number 0 on x64
  // will be equal to lane number 3 on IBM machines. Vector registers are only
  // used for compiling Wasm code at the moment. Wasm is also little endian
  // enforced. On s390 native, we manually do a reverse byte whenever values are
  // loaded/stored from memory to a Simd register. On the simulator however, we
  // do not reverse the bytes and data is just copied as is from one memory
  // location to another location which represents a register. To keep the Wasm
  // simulation accurate, we need to make sure accessing a lane is correctly
  // simulated and as such we reverse the lane number on the getters and setters
  // below. We need to be careful when getting/setting values on the Low or High
  // side of a simulated register. In the simulation, "Low" is equal to the MSB
  // and "High" is equal to the LSB on memory. "force_ibm_lane_numbering" could
  // be used to disabled automatic lane number reversal and help with accessing
  // the Low or High side of a simulated register.
  template <class T>
  T get_simd_register_by_lane(int reg, int lane,
                              bool force_ibm_lane_numbering = true) {
    if (force_ibm_lane_numbering) {
      lane = (kSimd128Size / sizeof(T)) - 1 - lane;
    }
    CHECK_LE(lane, kSimd128Size / sizeof(T));
    CHECK_LT(reg, kNumFPRs);
    CHECK_GE(lane, 0);
    CHECK_GE(reg, 0);
    return (reinterpret_cast<T*>(&fp_registers_[reg]))[lane];
  }

  template <class T>
  void set_simd_register_by_lane(int reg, int lane, const T& value,
                                 bool force_ibm_lane_numbering = true) {
    if (force_ibm_lane_numbering) {
      lane = (kSimd128Size / sizeof(T)) - 1 - lane;
    }
    CHECK_LE(lane, kSimd128Size / sizeof(T));
    CHECK_LT(reg, kNumFPRs);
    CHECK_GE(lane, 0);
    CHECK_GE(reg, 0);
    (reinterpret_cast<T*>(&fp_registers_[reg]))[lane] = value;
  }

  // Condition Code register. In S390, the last 4 bits are used.
  int32_t condition_reg_;
  // Special register to track PC.
  intptr_t special_reg_pc_;

  // Simulator support.
  uint8_t* stack_;
  static const size_t kStackProtectionSize = 256 * kSystemPointerSize;
  // This includes a protection margin at each end of the stack area.
  static size_t AllocatedStackSize() {
    size_t stack_size = v8_flags.sim_stack_size * KB;
    return stack_size + (2 * kStackProtectionSize);
  }
  static size_t UsableStackSize() {
    return AllocatedStackSize() - kStackProtectionSize;
  }
  bool pc_modified_;
  int64_t icount_;

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
  void DebugStart();

  int DecodeInstructionOriginal(Instruction* instr);
  int DecodeInstruction(Instruction* instr);
  int Evaluate_Unknown(Instruction* instr);
#define MAX_NUM_OPCODES (1 << 16)
  using EvaluateFuncType = int (Simulator::*)(Instruction*);

  static EvaluateFuncType EvalTable[MAX_NUM_OPCODES];
  static void EvalTableInit();

#define EVALUATE(name) int Evaluate_##name(Instruction* instr)
#define EVALUATE_VR_INSTRUCTIONS(name, op_name, op_value) EVALUATE(op_name);
  S390_VRR_A_OPCODE_LIST(EVALUATE_VR_INSTRUCTIONS)
  S390_VRR_C_OPCODE_LIST(EVALUATE_VR_INSTRUCTIONS)
  S390_VRR_E_OPCODE_LIST(EVALUATE_VR_INSTRUCTIONS)
  S390_VRR_F_OPCODE_LIST(EVALUATE_VR_INSTRUCTIONS)
  S390_VRX_OPCODE_LIST(EVALUATE_VR_INSTRUCTIONS)
  S390_VRS_A_OPCODE_LIST(EVALUATE_VR_INSTRUCTIONS)
  S390_VRS_B_OPCODE_LIST(EVALUATE_VR_INSTRUCTIONS)
  S390_VRS_C_OPCODE_LIST(EVALUATE_VR_INSTRUCTIONS)
  S390_VRR_B_OPCODE_LIST(EVALUATE_VR_INSTRUCTIONS)
  S390_VRI_A_OPCODE_LIST(EVALUATE_VR_INSTRUCTIONS)
  S390_VRI_C_OPCODE_LIST(EVALUATE_VR_INSTRUCTIONS)
#undef EVALUATE_VR_INSTRUCTIONS

  EVALUATE(DUMY);
  EVALUATE(BKPT);
  EVALUATE(SPM);
  EVALUATE(BALR);
  EVALUATE(BCTR);
  EVALUATE(BCR);
  EVALUATE(SVC);
  EVALUATE(BSM);
  EVALUATE(BASSM);
  EVALUATE(BASR);
  EVALUATE(MVCL);
  EVALUATE(CLCL);
  EVALUATE(LPR);
  EVALUATE(LNR);
  EVALUATE(LTR);
  EVALUATE(LCR);
  EVALUATE(NR);
  EVALUATE(CLR);
  EVALUATE(OR);
  EVALUATE(XR);
  EVALUATE(LR);
  EVALUATE(CR);
  EVALUATE(AR);
  EVALUATE(SR);
  EVALUATE(MR);
  EVALUATE(DR);
  EVALUATE(ALR);
  EVALUATE(SLR);
  EVALUATE(LDR);
  EVALUATE(CDR);
  EVALUATE(LER);
  EVALUATE(STH);
  EVALUATE(LA);
  EVALUATE(STC);
  EVALUATE(IC_z);
  EVALUATE(EX);
  EVALUATE(BAL);
  EVALUATE(BCT);
  EVALUATE(BC);
  EVALUATE(LH);
  EVALUATE(CH);
  EVALUATE(AH);
  EVALUATE(SH);
  EVALUATE(MH);
  EVALUATE(BAS);
  EVALUATE(CVD);
  EVALUATE(CVB);
  EVALUATE(ST);
  EVALUATE(LAE);
  EVALUATE(N);
  EVALUATE(CL);
  EVALUATE(O);
  EVALUATE(X);
  EVALUATE(L);
  EVALUATE(C);
  EVALUATE(A);
  EVALUATE(S);
  EVALUATE(M);
  EVALUATE(D);
  EVALUATE(AL);
  EVALUATE(SL);
  EVALUATE(STD);
  EVALUATE(LD);
  EVALUATE(CD);
  EVALUATE(STE);
  EVALUATE(MS);
  EVALUATE(LE);
  EVALUATE(BRXH);
  EVALUATE(BRXLE);
  EVALUATE(BXH);
  EVALUATE(BXLE);
  EVALUATE(SRL);
  EVALUATE(SLL);
  EVALUATE(SRA);
  EVALUATE(SLA);
  EVALUATE(SRDL);
  EVALUATE(SLDL);
  EVALUATE(SRDA);
  EVALUATE(SLDA);
  EVALUATE(STM);
  EVALUATE(TM);
  EVALUATE(MVI);
  EVALUATE(TS);
  EVALUATE(NI);
  EVALUATE(CLI);
  EVALUATE(OI);
  EVALUATE(XI);
  EVALUATE(LM);
  EVALUATE(CS);
  EVALUATE(MVCLE);
  EVALUATE(CLCLE);
  EVALUATE(MC);
  EVALUATE(CDS);
  EVALUATE(STCM);
  EVALUATE(ICM);
  EVALUATE(BPRP);
  EVALUATE(BPP);
  EVALUATE(TRTR);
  EVALUATE(MVN);
  EVALUATE(MVC);
  EVALUATE(MVZ);
  EVALUATE(NC);
  EVALUATE(CLC);
  EVALUATE(OC);
  EVALUATE(XC);
  EVALUATE(MVCP);
  EVALUATE(TR);
  EVALUATE(TRT);
  EVALUATE(ED);
  EVALUATE(EDMK);
  EVALUATE(PKU);
  EVALUATE(UNPKU);
  EVALUATE(MVCIN);
  EVALUATE(PKA);
  EVALUATE(UNPKA);
  EVALUATE(PLO);
  EVALUATE(LMD);
  EVALUATE(SRP);
  EVALUATE(MVO);
  EVALUATE(PACK);
  EVALUATE(UNPK);
  EVALUATE(ZAP);
  EVALUATE(AP);
  EVALUATE(SP);
  EVALUATE(MP);
  EVALUATE(DP);
  EVALUATE(UPT);
  EVALUATE(PFPO);
  EVALUATE(IIHH);
  EVALUATE(IIHL);
  EVALUATE(IILH);
  EVALUATE(IILL);
  EVALUATE(NIHH);
  EVALUATE(NIHL);
  EVALUATE(NILH);
  EVALUATE(NILL);
  EVALUATE(OIHH);
  EVALUATE(OIHL);
  EVALUATE(OILH);
  EVALUATE(OILL);
  EVALUATE(LLIHH);
  EVALUATE(LLIHL);
  EVALUATE(LLILH);
  EVALUATE(LLILL);
  EVALUATE(TMLH);
  EVALUATE(TMLL);
  EVALUATE(TMHH);
  EVALUATE(TMHL);
  EVALUATE(BRC);
  EVALUATE(BRAS);
  EVALUATE(BRCT);
  EVALUATE(BRCTG);
  EVALUATE(LHI);
  EVALUATE(LGHI);
  EVALUATE(AHI);
  EVALUATE(AGHI);
  EVALUATE(MHI);
  EVALUATE(MGHI);
  EVALUATE(CHI);
  EVALUATE(CGHI);
  EVALUATE(LARL);
  EVALUATE(LGFI);
  EVALUATE(BRCL);
  EVALUATE(BRASL);
  EVALUATE(XIHF);
  EVALUATE(XILF);
  EVALUATE(IIHF);
  EVALUATE(IILF);
  EVALUATE(NIHF);
  EVALUATE(NILF);
  EVALUATE(OIHF);
  EVALUATE(OILF);
  EVALUATE(LLIHF);
  EVALUATE(LLILF);
  EVALUATE(MSGFI);
  EVALUATE(MSFI);
  EVALUATE(SLGFI);
  EVALUATE(SLFI);
  EVALUATE(AGFI);
  EVALUATE(AFI);
  EVALUATE(ALGFI);
  EVALUATE(ALFI);
  EVALUATE(CGFI);
  EVALUATE(CFI);
  EVALUATE(CLGFI);
  EVALUATE(CLFI);
  EVALUATE(LLHRL);
  EVALUATE(LGHRL);
  EVALUATE(LHRL);
  EVALUATE(LLGHRL);
  EVALUATE(STHRL);
  EVALUATE(LGRL);
  EVALUATE(STGRL);
  EVALUATE(LGFRL);
  EVALUATE(LRL);
  EVALUATE(LLGFRL);
  EVALUATE(STRL);
  EVALUATE(EXRL);
  EVALUATE(PFDRL);
  EVALUATE(CGHRL);
  EVALUATE(CHRL);
  EVALUATE(CGRL);
  EVALUATE(CGFRL);
  EVALUATE(ECTG);
  EVALUATE(CSST);
  EVALUATE(LPD);
  EVALUATE(LPDG);
  EVALUATE(BRCTH);
  EVALUATE(AIH);
  EVALUATE(ALSIH);
  EVALUATE(ALSIHN);
  EVALUATE(CIH);
  EVALUATE(CLIH);
  EVALUATE(STCK);
  EVALUATE(CFC);
  EVALUATE(IPM);
  EVALUATE(HSCH);
  EVALUATE(MSCH);
  EVALUATE(SSCH);
  EVALUATE(STSCH);
  EVALUATE(TSCH);
  EVALUATE(TPI);
  EVALUATE(SAL);
  EVALUATE(RSCH);
  EVALUATE(STCRW);
  EVALUATE(STCPS);
  EVALUATE(RCHP);
  EVALUATE(SCHM);
  EVALUATE(CKSM);
  EVALUATE(SAR);
  EVALUATE(EAR);
  EVALUATE(MSR);
  EVALUATE(MSRKC);
  EVALUATE(MVST);
  EVALUATE(CUSE);
  EVALUATE(SRST);
  EVALUATE(XSCH);
  EVALUATE(STCKE);
  EVALUATE(STCKF);
  EVALUATE(SRNM);
  EVALUATE(STFPC);
  EVALUATE(LFPC);
  EVALUATE(TRE);
  EVALUATE(CUUTF);
  EVALUATE(CUTFU);
  EVALUATE(STFLE);
  EVALUATE(SRNMB);
  EVALUATE(SRNMT);
  EVALUATE(LFAS);
  EVALUATE(PPA);
  EVALUATE(ETND);
  EVALUATE(TEND);
  EVALUATE(NIAI);
  EVALUATE(TABORT);
  EVALUATE(TRAP4);
  EVALUATE(LPEBR);
  EVALUATE(LNEBR);
  EVALUATE(LTEBR);
  EVALUATE(LCEBR);
  EVALUATE(LDEBR);
  EVALUATE(LXDBR);
  EVALUATE(LXEBR);
  EVALUATE(MXDBR);
  EVALUATE(KEBR);
  EVALUATE(CEBR);
  EVALUATE(AEBR);
  EVALUATE(SEBR);
  EVALUATE(MDEBR);
  EVALUATE(DEBR);
  EVALUATE(MAEBR);
  EVALUATE(MSEBR);
  EVALUATE(LPDBR);
  EVALUATE(LNDBR);
  EVALUATE(LTDBR);
  EVALUATE(LCDBR);
  EVALUATE(SQEBR);
  EVALUATE(SQDBR);
  EVALUATE(SQXBR);
  EVALUATE(MEEBR);
  EVALUATE(KDBR);
  EVALUATE(CDBR);
  EVALUATE(ADBR);
  EVALUATE(SDBR);
  EVALUATE(MDBR);
  EVALUATE(DDBR);
  EVALUATE(MADBR);
  EVALUATE(MSDBR);
  EVALUATE(LPXBR);
  EVALUATE(LNXBR);
  EVALUATE(LTXBR);
  EVALUATE(LCXBR);
  EVALUATE(LEDBRA);
  EVALUATE(LDXBRA);
  EVALUATE(LEXBRA);
  EVALUATE(FIXBRA);
  EVALUATE(KXBR);
  EVALUATE(CXBR);
  EVALUATE(AXBR);
  EVALUATE(SXBR);
  EVALUATE(MXBR);
  EVALUATE(DXBR);
  EVALUATE(TBEDR);
  EVALUATE(TBDR);
  EVALUATE(DIEBR);
  EVALUATE(FIEBRA);
  EVALUATE(THDER);
  EVALUATE(THDR);
  EVALUATE(DIDBR);
  EVALUATE(FIDBRA);
  EVALUATE(LXR);
  EVALUATE(LPDFR);
  EVALUATE(LNDFR);
  EVALUATE(LCDFR);
  EVALUATE(LZER);
  EVALUATE(LZDR);
  EVALUATE(LZXR);
  EVALUATE(SFPC);
  EVALUATE(SFASR);
  EVALUATE(EFPC);
  EVALUATE(CELFBR);
  EVALUATE(CDLFBR);
  EVALUATE(CXLFBR);
  EVALUATE(CEFBRA);
  EVALUATE(CDFBRA);
  EVALUATE(CXFBRA);
  EVALUATE(CFEBRA);
  EVALUATE(CFDBRA);
  EVALUATE(CFXBRA);
  EVALUATE(CLFEBR);
  EVALUATE(CLFDBR);
  EVALUATE(CLFXBR);
  EVALUATE(CELGBR);
  EVALUATE(CDLGBR);
  EVALUATE(CXLGBR);
  EVALUATE(CEGBRA);
  EVALUATE(CDGBRA);
  EVALUATE(CXGBRA);
  EVALUATE(CGEBRA);
  EVALUATE(CGDBRA);
  EVALUATE(CGXBRA);
  EVALUATE(CLGEBR);
  EVALUATE(CLGDBR);
  EVALUATE(CFER);
  EVALUATE(CFDR);
  EVALUATE(CFXR);
  EVALUATE(LDGR);
  EVALUATE(CGER);
  EVALUATE(CGDR);
  EVALUATE(CGXR);
  EVALUATE(LGDR);
  EVALUATE(MDTR);
  EVALUATE(MDTRA);
  EVALUATE(DDTRA);
  EVALUATE(ADTRA);
  EVALUATE(SDTRA);
  EVALUATE(LDETR);
  EVALUATE(LEDTR);
  EVALUATE(LTDTR);
  EVALUATE(FIDTR);
  EVALUATE(MXTRA);
  EVALUATE(DXTRA);
  EVALUATE(AXTRA);
  EVALUATE(SXTRA);
  EVALUATE(LXDTR);
  EVALUATE(LDXTR);
  EVALUATE(LTXTR);
  EVALUATE(FIXTR);
  EVALUATE(KDTR);
  EVALUATE(CGDTRA);
  EVALUATE(CUDTR);
  EVALUATE(CDTR);
  EVALUATE(EEDTR);
  EVALUATE(ESDTR);
  EVALUATE(KXTR);
  EVALUATE(CGXTRA);
  EVALUATE(CUXTR);
  EVALUATE(CSXTR);
  EVALUATE(CXTR);
  EVALUATE(EEXTR);
  EVALUATE(ESXTR);
  EVALUATE(CDGTRA);
  EVALUATE(CDUTR);
  EVALUATE(CDSTR);
  EVALUATE(CEDTR);
  EVALUATE(QADTR);
  EVALUATE(IEDTR);
  EVALUATE(RRDTR);
  EVALUATE(CXGTRA);
  EVALUATE(CXUTR);
  EVALUATE(CXSTR);
  EVALUATE(CEXTR);
  EVALUATE(QAXTR);
  EVALUATE(IEXTR);
  EVALUATE(RRXTR);
  EVALUATE(LPGR);
  EVALUATE(LNGR);
  EVALUATE(LTGR);
  EVALUATE(LCGR);
  EVALUATE(LGR);
  EVALUATE(LGBR);
  EVALUATE(LGHR);
  EVALUATE(AGR);
  EVALUATE(SGR);
  EVALUATE(ALGR);
  EVALUATE(SLGR);
  EVALUATE(MSGR);
  EVALUATE(MSGRKC);
  EVALUATE(DSGR);
  EVALUATE(LRVGR);
  EVALUATE(LPGFR);
  EVALUATE(LNGFR);
  EVALUATE(LTGFR);
  EVALUATE(LCGFR);
  EVALUATE(LGFR);
  EVALUATE(LLGFR);
  EVALUATE(LLGTR);
  EVALUATE(AGFR);
  EVALUATE(SGFR);
  EVALUATE(ALGFR);
  EVALUATE(SLGFR);
  EVALUATE(MSGFR);
  EVALUATE(DSGFR);
  EVALUATE(KMAC);
  EVALUATE(LRVR);
  EVALUATE(CGR);
  EVALUATE(CLGR);
  EVALUATE(LBR);
  EVALUATE(LHR);
  EVALUATE(KMF);
  EVALUATE(KMO);
  EVALUATE(PCC);
  EVALUATE(KMCTR);
  EVALUATE(KM);
  EVALUATE(KMC);
  EVALUATE(CGFR);
  EVALUATE(KIMD);
  EVALUATE(KLMD);
  EVALUATE(CFDTR);
  EVALUATE(CLGDTR);
  EVALUATE(CLFDTR);
  EVALUATE(BCTGR);
  EVALUATE(CFXTR);
  EVALUATE(CLFXTR);
  EVALUATE(CDFTR);
  EVALUATE(CDLGTR);
  EVALUATE(CDLFTR);
  EVALUATE(CXFTR);
  EVALUATE(CXLGTR);
  EVALUATE(CXLFTR);
  EVALUATE(CGRT);
  EVALUATE(NGR);
  EVALUATE(OGR);
  EVALUATE(XGR);
  EVALUATE(FLOGR);
  EVALUATE(LLGCR);
  EVALUATE(LLGHR);
  EVALUATE(MLGR);
  EVALUATE(DLGR);
  EVALUATE(ALCGR);
  EVALUATE(SLBGR);
  EVALUATE(EPSW);
  EVALUATE(TRTT);
  EVALUATE(TRTO);
  EVALUATE(TROT);
  EVALUATE(TROO);
  EVALUATE(LLCR);
  EVALUATE(LLHR);
  EVALUATE(MLR);
  EVALUATE(DLR);
  EVALUATE(ALCR);
  EVALUATE(SLBR);
  EVALUATE(CU14);
  EVALUATE(CU24);
  EVALUATE(CU41);
  EVALUATE(CU42);
  EVALUATE(TRTRE);
  EVALUATE(SRSTU);
  EVALUATE(TRTE);
  EVALUATE(AHHHR);
  EVALUATE(SHHHR);
  EVALUATE(ALHHHR);
  EVALUATE(SLHHHR);
  EVALUATE(CHHR);
  EVALUATE(AHHLR);
  EVALUATE(SHHLR);
  EVALUATE(ALHHLR);
  EVALUATE(SLHHLR);
  EVALUATE(CHLR);
  EVALUATE(POPCNT_Z);
  EVALUATE(LOCGR);
  EVALUATE(NGRK);
  EVALUATE(OGRK);
  EVALUATE(XGRK);
  EVALUATE(AGRK);
  EVALUATE(SGRK);
  EVALUATE(ALGRK);
  EVALUATE(SLGRK);
  EVALUATE(LOCR);
  EVALUATE(NRK);
  EVALUATE(ORK);
  EVALUATE(XRK);
  EVALUATE(ARK);
  EVALUATE(SRK);
  EVALUATE(ALRK);
  EVALUATE(SLRK);
  EVALUATE(LTG);
  EVALUATE(LG);
  EVALUATE(CVBY);
  EVALUATE(AG);
  EVALUATE(SG);
  EVALUATE(ALG);
  EVALUATE(SLG);
  EVALUATE(MSG);
  EVALUATE(DSG);
  EVALUATE(CVBG);
  EVALUATE(LRVG);
  EVALUATE(LT);
  EVALUATE(LGF);
  EVALUATE(LGH);
  EVALUATE(LLGF);
  EVALUATE(LLGT);
  EVALUATE(AGF);
  EVALUATE(SGF);
  EVALUATE(ALGF);
  EVALUATE(SLGF);
  EVALUATE(MSGF);
  EVALUATE(DSGF);
  EVALUATE(LRV);
  EVALUATE(LRVH);
  EVALUATE(CG);
  EVALUATE(CLG);
  EVALUATE(STG);
  EVALUATE(NTSTG);
  EVALUATE(CVDY);
  EVALUATE(CVDG);
  EVALUATE(STRVG);
  EVALUATE(CGF);
  EVALUATE(CLGF);
  EVALUATE(LTGF);
  EVALUATE(CGH);
  EVALUATE(PFD);
  EVALUATE(STRV);
  EVALUATE(STRVH);
  EVALUATE(BCTG);
  EVALUATE(STY);
  EVALUATE(MSY);
  EVALUATE(MSC);
  EVALUATE(NY);
  EVALUATE(CLY);
  EVALUATE(OY);
  EVALUATE(XY);
  EVALUATE(LY);
  EVALUATE(CY);
  EVALUATE(AY);
  EVALUATE(SY);
  EVALUATE(MFY);
  EVALUATE(ALY);
  EVALUATE(SLY);
  EVALUATE(STHY);
  EVALUATE(LAY);
  EVALUATE(STCY);
  EVALUATE(ICY);
  EVALUATE(LAEY);
  EVALUATE(LB);
  EVALUATE(LGB);
  EVALUATE(LHY);
  EVALUATE(CHY);
  EVALUATE(AHY);
  EVALUATE(SHY);
  EVALUATE(MHY);
  EVALUATE(NG);
  EVALUATE(OG);
  EVALUATE(XG);
  EVALUATE(LGAT);
  EVALUATE(MLG);
  EVALUATE(DLG);
  EVALUATE(ALCG);
  EVALUATE(SLBG);
  EVALUATE(STPQ);
  EVALUATE(LPQ);
  EVALUATE(LLGC);
  EVALUATE(LLGH);
  EVALUATE(LLC);
  EVALUATE(LLH);
  EVALUATE(ML);
  EVALUATE(DL);
  EVALUATE(ALC);
  EVALUATE(SLB);
  EVALUATE(LLGTAT);
  EVALUATE(LLGFAT);
  EVALUATE(LAT);
  EVALUATE(LBH);
  EVALUATE(LLCH);
  EVALUATE(STCH);
  EVALUATE(LHH);
  EVALUATE(LLHH);
  EVALUATE(STHH);
  EVALUATE(LFHAT);
  EVALUATE(LFH);
  EVALUATE(STFH);
  EVALUATE(CHF);
  EVALUATE(MVCDK);
  EVALUATE(MVHHI);
  EVALUATE(MVGHI);
  EVALUATE(MVHI);
  EVALUATE(CHHSI);
  EVALUATE(CGHSI);
  EVALUATE(CHSI);
  EVALUATE(CLFHSI);
  EVALUATE(TBEGIN);
  EVALUATE(TBEGINC);
  EVALUATE(LMG);
  EVALUATE(SRAG);
  EVALUATE(SLAG);
  EVALUATE(SRLG);
  EVALUATE(SLLG);
  EVALUATE(CSY);
  EVALUATE(CSG);
  EVALUATE(RLLG);
  EVALUATE(RLL);
  EVALUATE(STMG);
  EVALUATE(STMH);
  EVALUATE(STCMH);
  EVALUATE(STCMY);
  EVALUATE(CDSY);
  EVALUATE(CDSG);
  EVALUATE(BXHG);
  EVALUATE(BXLEG);
  EVALUATE(ECAG);
  EVALUATE(TMY);
  EVALUATE(MVIY);
  EVALUATE(NIY);
  EVALUATE(CLIY);
  EVALUATE(OIY);
  EVALUATE(XIY);
  EVALUATE(ASI);
  EVALUATE(ALSI);
  EVALUATE(AGSI);
  EVALUATE(ALGSI);
  EVALUATE(ICMH);
  EVALUATE(ICMY);
  EVALUATE(MVCLU);
  EVALUATE(CLCLU);
  EVALUATE(STMY);
  EVALUATE(LMH);
  EVALUATE(LMY);
  EVALUATE(TP);
  EVALUATE(SRAK);
  EVALUATE(SLAK);
  EVALUATE(SRLK);
  EVALUATE(SLLK);
  EVALUATE(LOCG);
  EVALUATE(STOCG);
  EVALUATE(LANG);
  EVALUATE(LAOG);
  EVALUATE(LAXG);
  EVALUATE(LAAG);
  EVALUATE(LAALG);
  EVALUATE(LOC);
  EVALUATE(STOC);
  EVALUATE(LAN);
  EVALUATE(LAO);
  EVALUATE(LAX);
  EVALUATE(LAA);
  EVALUATE(LAAL);
  EVALUATE(BRXHG);
  EVALUATE(BRXLG);
  EVALUATE(RISBLG);
  EVALUATE(RNSBG);
  EVALUATE(RISBG);
  EVALUATE(ROSBG);
  EVALUATE(RXSBG);
  EVALUATE(RISBGN);
  EVALUATE(RISBHG);
  EVALUATE(CGRJ);
  EVALUATE(CGIT);
  EVALUATE(CIT);
  EVALUATE(CLFIT);
  EVALUATE(CGIJ);
  EVALUATE(CIJ);
  EVALUATE(AHIK);
  EVALUATE(AGHIK);
  EVALUATE(ALHSIK);
  EVALUATE(ALGHSIK);
  EVALUATE(CGRB);
  EVALUATE(CGIB);
  EVALUATE(CIB);
  EVALUATE(LDEB);
  EVALUATE(LXDB);
  EVALUATE(LXEB);
  EVALUATE(MXDB);
  EVALUATE(KEB);
  EVALUATE(CEB);
  EVALUATE(AEB);
  EVALUATE(SEB);
  EVALUATE(MDEB);
  EVALUATE(DEB);
  EVALUATE(MAEB);
  EVALUATE(MSEB);
  EVALUATE(TCEB);
  EVALUATE(TCDB);
  EVALUATE(TCXB);
  EVALUATE(SQEB);
  EVALUATE(SQDB);
  EVALUATE(MEEB);
  EVALUATE(KDB);
  EVALUATE(CDB);
  EVALUATE(ADB);
  EVALUATE(SDB);
  EVALUATE(MDB);
  EVALUATE(DDB);
  EVALUATE(MADB);
  EVALUATE(MSDB);
  EVALUATE(SLDT);
  EVALUATE(SRDT);
  EVALUATE(SLXT);
  EVALUATE(SRXT);
  EVALUATE(TDCET);
  EVALUATE(TDGET);
  EVALUATE(TDCDT);
  EVALUATE(TDGDT);
  EVALUATE(TDCXT);
  EVALUATE(TDGXT);
  EVALUATE(LEY);
  EVALUATE(LDY);
  EVALUATE(STEY);
  EVALUATE(STDY);
  EVALUATE(CZDT);
  EVALUATE(CZXT);
  EVALUATE(CDZT);
  EVALUATE(CXZT);
  EVALUATE(MG);
  EVALUATE(MGRK);

#undef EVALUATE
};

}  // namespace internal
}  // namespace v8

#endif  // defined(USE_SIMULATOR)
#endif  // V8_EXECUTION_S390_SIMULATOR_S390_H_
