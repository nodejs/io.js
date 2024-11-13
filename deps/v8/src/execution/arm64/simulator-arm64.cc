// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arm64/simulator-arm64.h"

#include "src/execution/isolate.h"

#if defined(USE_SIMULATOR)

#include <stdlib.h>

#include <cmath>
#include <cstdarg>
#include <type_traits>

#include "src/base/overflowing-math.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/wrappers.h"
#include "src/base/sanitizer/msan.h"
#include "src/codegen/arm64/decoder-arm64-inl.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/diagnostics/disasm.h"
#include "src/heap/combined-heap.h"
#include "src/objects/objects-inl.h"
#include "src/runtime/runtime-utils.h"
#include "src/snapshot/embedded/embedded-data.h"
#include "src/utils/ostreams.h"

#if V8_OS_WIN
#include <windows.h>
#endif

#if V8_ENABLE_WEBASSEMBLY
#include "src/trap-handler/trap-handler-simulator.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

// This macro provides a platform independent use of sscanf. The reason for
// SScanF not being implemented in a platform independent way through
// ::v8::internal::OS in the same way as SNPrintF is that the
// Windows C Run-Time Library does not provide vsscanf.
#define SScanF sscanf

// Helpers for colors.
#define COLOUR(colour_code) "\033[0;" colour_code "m"
#define COLOUR_BOLD(colour_code) "\033[1;" colour_code "m"
#define NORMAL ""
#define GREY "30"
#define RED "31"
#define GREEN "32"
#define YELLOW "33"
#define BLUE "34"
#define MAGENTA "35"
#define CYAN "36"
#define WHITE "37"

using TEXT_COLOUR = char const* const;
TEXT_COLOUR clr_normal = v8_flags.log_colour ? COLOUR(NORMAL) : "";
TEXT_COLOUR clr_flag_name = v8_flags.log_colour ? COLOUR_BOLD(WHITE) : "";
TEXT_COLOUR clr_flag_value = v8_flags.log_colour ? COLOUR(NORMAL) : "";
TEXT_COLOUR clr_reg_name = v8_flags.log_colour ? COLOUR_BOLD(CYAN) : "";
TEXT_COLOUR clr_reg_value = v8_flags.log_colour ? COLOUR(CYAN) : "";
TEXT_COLOUR clr_vreg_name = v8_flags.log_colour ? COLOUR_BOLD(MAGENTA) : "";
TEXT_COLOUR clr_vreg_value = v8_flags.log_colour ? COLOUR(MAGENTA) : "";
TEXT_COLOUR clr_memory_address = v8_flags.log_colour ? COLOUR_BOLD(BLUE) : "";
TEXT_COLOUR clr_debug_number = v8_flags.log_colour ? COLOUR_BOLD(YELLOW) : "";
TEXT_COLOUR clr_debug_message = v8_flags.log_colour ? COLOUR(YELLOW) : "";
TEXT_COLOUR clr_printf = v8_flags.log_colour ? COLOUR(GREEN) : "";

DEFINE_LAZY_LEAKY_OBJECT_GETTER(Simulator::GlobalMonitor,
                                Simulator::GlobalMonitor::Get)

bool Simulator::ProbeMemory(uintptr_t address, uintptr_t access_size) {
#if V8_ENABLE_WEBASSEMBLY && V8_TRAP_HANDLER_SUPPORTED
  uintptr_t last_accessed_byte = address + access_size - 1;
  uintptr_t current_pc = reinterpret_cast<uintptr_t>(pc_);
  uintptr_t landing_pad =
      trap_handler::ProbeMemory(last_accessed_byte, current_pc);
  if (!landing_pad) return true;
  set_pc(landing_pad);
  set_reg(kWasmTrapHandlerFaultAddressRegister.code(), current_pc);
  return false;
#else
  return true;
#endif
}

// This is basically the same as PrintF, with a guard for v8_flags.trace_sim.
void Simulator::TraceSim(const char* format, ...) {
  if (v8_flags.trace_sim) {
    va_list arguments;
    va_start(arguments, format);
    base::OS::VFPrint(stream_, format, arguments);
    va_end(arguments);
  }
}

const Instruction* Simulator::kEndOfSimAddress = nullptr;

void SimSystemRegister::SetBits(int msb, int lsb, uint32_t bits) {
  int width = msb - lsb + 1;
  DCHECK(is_uintn(bits, width) || is_intn(bits, width));

  bits <<= lsb;
  uint32_t mask = ((1 << width) - 1) << lsb;
  DCHECK_EQ(mask & write_ignore_mask_, 0);

  value_ = (value_ & ~mask) | (bits & mask);
}

SimSystemRegister SimSystemRegister::DefaultValueFor(SystemRegister id) {
  switch (id) {
    case NZCV:
      return SimSystemRegister(0x00000000, NZCVWriteIgnoreMask);
    case FPCR:
      return SimSystemRegister(0x00000000, FPCRWriteIgnoreMask);
    default:
      UNREACHABLE();
  }
}

// Get the active Simulator for the current thread.
Simulator* Simulator::current(Isolate* isolate) {
  Isolate::PerIsolateThreadData* isolate_data =
      isolate->FindOrAllocatePerThreadDataForThisThread();
  DCHECK_NOT_NULL(isolate_data);

  Simulator* sim = isolate_data->simulator();
  if (sim == nullptr) {
    if (v8_flags.trace_sim || v8_flags.debug_sim) {
      sim = new Simulator(new Decoder<DispatchingDecoderVisitor>(), isolate);
    } else {
      sim = new Decoder<Simulator>();
      sim->isolate_ = isolate;
    }
    isolate_data->set_simulator(sim);
  }
  return sim;
}

void Simulator::CallImpl(Address entry, CallArgument* args) {
  int index_x = 0;
  int index_d = 0;

  std::vector<int64_t> stack_args(0);
  for (int i = 0; !args[i].IsEnd(); i++) {
    CallArgument arg = args[i];
    if (arg.IsX() && (index_x < 8)) {
      set_xreg(index_x++, arg.bits());
    } else if (arg.IsD() && (index_d < 8)) {
      set_dreg_bits(index_d++, arg.bits());
    } else {
      DCHECK(arg.IsD() || arg.IsX());
      stack_args.push_back(arg.bits());
    }
  }

  // Process stack arguments, and make sure the stack is suitably aligned.
  uintptr_t original_stack = sp();
  uintptr_t entry_stack =
      original_stack - stack_args.size() * sizeof(stack_args[0]);
  if (base::OS::ActivationFrameAlignment() != 0) {
    entry_stack &= -base::OS::ActivationFrameAlignment();
  }
  char* stack = reinterpret_cast<char*>(entry_stack);
  std::vector<int64_t>::const_iterator it;
  for (it = stack_args.begin(); it != stack_args.end(); it++) {
    memcpy(stack, &(*it), sizeof(*it));
    stack += sizeof(*it);
  }

  DCHECK(reinterpret_cast<uintptr_t>(stack) <= original_stack);
  set_sp(entry_stack);

  // Call the generated code.
  set_pc(entry);
  set_lr(kEndOfSimAddress);
  CheckPCSComplianceAndRun();

  set_sp(original_stack);
}

#ifdef DEBUG
namespace {
int PopLowestIndexAsCode(CPURegList* list) {
  if (list->IsEmpty()) {
    return -1;
  }
  uint64_t reg_list = list->bits();
  int index = base::bits::CountTrailingZeros(reg_list);
  DCHECK((1LL << index) & reg_list);
  list->Remove(index);

  return index;
}
}  // namespace
#endif

void Simulator::CheckPCSComplianceAndRun() {
  // Adjust JS-based stack limit to C-based stack limit.
  isolate_->stack_guard()->AdjustStackLimitForSimulator();

#ifdef DEBUG
  DCHECK_EQ(kNumberOfCalleeSavedRegisters, kCalleeSaved.Count());
  DCHECK_EQ(kNumberOfCalleeSavedVRegisters, kCalleeSavedV.Count());

  int64_t saved_registers[kNumberOfCalleeSavedRegisters];
  uint64_t saved_fpregisters[kNumberOfCalleeSavedVRegisters];

  CPURegList register_list = kCalleeSaved;
  CPURegList fpregister_list = kCalleeSavedV;

  for (int i = 0; i < kNumberOfCalleeSavedRegisters; i++) {
    // x31 is not a caller saved register, so no need to specify if we want
    // the stack or zero.
    saved_registers[i] = xreg(PopLowestIndexAsCode(&register_list));
  }
  for (int i = 0; i < kNumberOfCalleeSavedVRegisters; i++) {
    saved_fpregisters[i] = dreg_bits(PopLowestIndexAsCode(&fpregister_list));
  }
  int64_t original_stack = sp();
  int64_t original_fp = fp();
#endif
  // Start the simulation!
  Run();
#ifdef DEBUG
  DCHECK_EQ(original_stack, sp());
  DCHECK_EQ(original_fp, fp());
  // Check that callee-saved registers have been preserved.
  register_list = kCalleeSaved;
  fpregister_list = kCalleeSavedV;
  for (int i = 0; i < kNumberOfCalleeSavedRegisters; i++) {
    DCHECK_EQ(saved_registers[i], xreg(PopLowestIndexAsCode(&register_list)));
  }
  for (int i = 0; i < kNumberOfCalleeSavedVRegisters; i++) {
    DCHECK(saved_fpregisters[i] ==
           dreg_bits(PopLowestIndexAsCode(&fpregister_list)));
  }

  // Corrupt caller saved register minus the return regiters.

  // In theory x0 to x7 can be used for return values, but V8 only uses x0, x1
  // for now .
  register_list = kCallerSaved;
  register_list.Remove(x0);
  register_list.Remove(x1);

  // In theory d0 to d7 can be used for return values, but V8 only uses d0
  // for now .
  fpregister_list = kCallerSavedV;
  fpregister_list.Remove(d0);

  CorruptRegisters(&register_list, kCallerSavedRegisterCorruptionValue);
  CorruptRegisters(&fpregister_list, kCallerSavedVRegisterCorruptionValue);
#endif
}

#ifdef DEBUG
// The least significant byte of the curruption value holds the corresponding
// register's code.
void Simulator::CorruptRegisters(CPURegList* list, uint64_t value) {
  if (list->type() == CPURegister::kRegister) {
    while (!list->IsEmpty()) {
      unsigned code = PopLowestIndexAsCode(list);
      set_xreg(code, value | code);
    }
  } else {
    DCHECK_EQ(list->type(), CPURegister::kVRegister);
    while (!list->IsEmpty()) {
      unsigned code = PopLowestIndexAsCode(list);
      set_dreg_bits(code, value | code);
    }
  }
}

void Simulator::CorruptAllCallerSavedCPURegisters() {
  // Corrupt alters its parameter so copy them first.
  CPURegList register_list = kCallerSaved;
  CPURegList fpregister_list = kCallerSavedV;

  CorruptRegisters(&register_list, kCallerSavedRegisterCorruptionValue);
  CorruptRegisters(&fpregister_list, kCallerSavedVRegisterCorruptionValue);
}
#endif

// Extending the stack by 2 * 64 bits is required for stack alignment purposes.
uintptr_t Simulator::PushAddress(uintptr_t address) {
  DCHECK(sizeof(uintptr_t) < 2 * kXRegSize);
  intptr_t new_sp = sp() - 2 * kXRegSize;
  uintptr_t* alignment_slot = reinterpret_cast<uintptr_t*>(new_sp + kXRegSize);
  memcpy(alignment_slot, &kSlotsZapValue, kSystemPointerSize);
  uintptr_t* stack_slot = reinterpret_cast<uintptr_t*>(new_sp);
  memcpy(stack_slot, &address, kSystemPointerSize);
  set_sp(new_sp);
  return new_sp;
}

uintptr_t Simulator::PopAddress() {
  intptr_t current_sp = sp();
  uintptr_t* stack_slot = reinterpret_cast<uintptr_t*>(current_sp);
  uintptr_t address = *stack_slot;
  DCHECK_LT(sizeof(uintptr_t), 2 * kXRegSize);
  set_sp(current_sp + 2 * kXRegSize);
  return address;
}

// Returns the limit of the stack area to enable checking for stack overflows.
uintptr_t Simulator::StackLimit(uintptr_t c_limit) const {
  // The simulator uses a separate JS stack. If we have exhausted the C stack,
  // we also drop down the JS limit to reflect the exhaustion on the JS stack.
  if (base::Stack::GetCurrentStackPosition() < c_limit) {
    return get_sp();
  }

  // Otherwise the limit is the JS stack. Leave a safety margin to prevent
  // overrunning the stack when pushing values.
  return stack_limit_ + kAdditionalStackMargin;
}

base::Vector<uint8_t> Simulator::GetCurrentStackView() const {
  // We do not add an additional safety margin as above in
  // Simulator::StackLimit, as users of this method are expected to add their
  // own margin.
  return base::VectorOf(reinterpret_cast<uint8_t*>(stack_limit_),
                        UsableStackSize());
}

void Simulator::SetRedirectInstruction(Instruction* instruction) {
  instruction->SetInstructionBits(
      HLT | Assembler::ImmException(kImmExceptionIsRedirectedCall));
}

Simulator::Simulator(Decoder<DispatchingDecoderVisitor>* decoder,
                     Isolate* isolate, FILE* stream)
    : decoder_(decoder),
      guard_pages_(ENABLE_CONTROL_FLOW_INTEGRITY_BOOL),
      last_debugger_input_(nullptr),
      log_parameters_(NO_PARAM),
      icount_for_stop_sim_at_(0),
      isolate_(isolate) {
  // Setup the decoder.
  decoder_->AppendVisitor(this);

  Init(stream);

  if (v8_flags.trace_sim) {
    decoder_->InsertVisitorBefore(print_disasm_, this);
    log_parameters_ = LOG_ALL;
  }
}

Simulator::Simulator()
    : decoder_(nullptr),
      guard_pages_(ENABLE_CONTROL_FLOW_INTEGRITY_BOOL),
      last_debugger_input_(nullptr),
      log_parameters_(NO_PARAM),
      isolate_(nullptr) {
  Init(stdout);
  CHECK(!v8_flags.trace_sim);
}

void Simulator::Init(FILE* stream) {
  ResetState();

  // Allocate and setup the simulator stack.
  size_t stack_size = AllocatedStackSize();

  stack_ = reinterpret_cast<uintptr_t>(new uint8_t[stack_size]());
  stack_limit_ = stack_ + kStackProtectionSize;
  uintptr_t tos = stack_ + stack_size - kStackProtectionSize;
  // The stack pointer must be 16-byte aligned.
  set_sp(tos & ~0xFULL);

  stream_ = stream;
  print_disasm_ = new PrintDisassembler(stream_);

  // The debugger needs to disassemble code without the simulator executing an
  // instruction, so we create a dedicated decoder.
  disassembler_decoder_ = new Decoder<DispatchingDecoderVisitor>();
  disassembler_decoder_->AppendVisitor(print_disasm_);
}

void Simulator::ResetState() {
  // Reset the system registers.
  nzcv_ = SimSystemRegister::DefaultValueFor(NZCV);
  fpcr_ = SimSystemRegister::DefaultValueFor(FPCR);

  // Reset registers to 0.
  pc_ = nullptr;
  for (unsigned i = 0; i < kNumberOfRegisters; i++) {
    set_xreg(i, 0xBADBEEF);
  }
  for (unsigned i = 0; i < kNumberOfVRegisters; i++) {
    // Set FP registers to a value that is NaN in both 32-bit and 64-bit FP.
    set_dreg_bits(i, 0x7FF000007F800001UL);
  }
  // Returning to address 0 exits the Simulator.
  set_lr(kEndOfSimAddress);

  // Reset debug helpers.
  breakpoints_.clear();
  break_on_next_ = false;

  btype_ = DefaultBType;
}

Simulator::~Simulator() {
  GlobalMonitor::Get()->RemoveProcessor(&global_monitor_processor_);
  delete[] reinterpret_cast<uint8_t*>(stack_);
  delete disassembler_decoder_;
  delete print_disasm_;
  delete decoder_;
}

void Simulator::Run() {
  // Flush any written registers before executing anything, so that
  // manually-set registers are logged _before_ the first instruction.
  LogAllWrittenRegisters();

  pc_modified_ = false;

  if (v8_flags.stop_sim_at == 0) {
    // Fast version of the dispatch loop without checking whether the simulator
    // should be stopping at a particular executed instruction.
    while (pc_ != kEndOfSimAddress) {
      ExecuteInstruction();
    }
  } else {
    // v8_flags.stop_sim_at is at the non-default value. Stop in the debugger
    // when we reach the particular instruction count.
    while (pc_ != kEndOfSimAddress) {
      icount_for_stop_sim_at_ =
          base::AddWithWraparound(icount_for_stop_sim_at_, 1);
      if (icount_for_stop_sim_at_ == v8_flags.stop_sim_at) {
        Debug();
      }
      ExecuteInstruction();
    }
  }
}

void Simulator::RunFrom(Instruction* start) {
  set_pc(start);
  Run();
}

// Calls into the V8 runtime are based on this very simple interface.
// Note: To be able to return two values from some calls the code in runtime.cc
// uses the ObjectPair structure.
// The simulator assumes all runtime calls return two 64-bits values. If they
// don't, register x1 is clobbered. This is fine because x1 is caller-saved.
#if defined(V8_OS_WIN)
using SimulatorRuntimeCall_ReturnPtr = int64_t (*)(
    int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4,
    int64_t arg5, int64_t arg6, int64_t arg7, int64_t arg8, int64_t arg9,
    int64_t arg10, int64_t arg11, int64_t arg12, int64_t arg13, int64_t arg14,
    int64_t arg15, int64_t arg16, int64_t arg17, int64_t arg18, int64_t arg19);
#endif

using SimulatorRuntimeCall = ObjectPair (*)(
    int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4,
    int64_t arg5, int64_t arg6, int64_t arg7, int64_t arg8, int64_t arg9,
    int64_t arg10, int64_t arg11, int64_t arg12, int64_t arg13, int64_t arg14,
    int64_t arg15, int64_t arg16, int64_t arg17, int64_t arg18, int64_t arg19);

using SimulatorRuntimeCompareCall = int64_t (*)(double arg1, double arg2);
using SimulatorRuntimeFPFPCall = double (*)(double arg1, double arg2);
using SimulatorRuntimeFPCall = double (*)(double arg1);
using SimulatorRuntimeFPIntCall = double (*)(double arg1, int32_t arg2);
// Define four args for future flexibility; at the time of this writing only
// one is ever used.
using SimulatorRuntimeFPTaggedCall = double (*)(int64_t arg0, int64_t arg1,
                                                int64_t arg2, int64_t arg3);

// This signature supports direct call in to API function native callback
// (refer to InvocationCallback in v8.h).
using SimulatorRuntimeDirectApiCall = void (*)(int64_t arg0);

// This signature supports direct call to accessor getter callback.
using SimulatorRuntimeDirectGetterCall = void (*)(int64_t arg0, int64_t arg1);

// Separate for fine-grained UBSan blocklisting. Casting any given C++
// function to {SimulatorRuntimeCall} is undefined behavior; but since
// the target function can indeed be any function that's exposed via
// the "fast C call" mechanism, we can't reconstruct its signature here.
ObjectPair UnsafeGenericFunctionCall(
    int64_t function, int64_t arg0, int64_t arg1, int64_t arg2, int64_t arg3,
    int64_t arg4, int64_t arg5, int64_t arg6, int64_t arg7, int64_t arg8,
    int64_t arg9, int64_t arg10, int64_t arg11, int64_t arg12, int64_t arg13,
    int64_t arg14, int64_t arg15, int64_t arg16, int64_t arg17, int64_t arg18,
    int64_t arg19) {
  SimulatorRuntimeCall target =
      reinterpret_cast<SimulatorRuntimeCall>(function);
  return target(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9,
                arg10, arg11, arg12, arg13, arg14, arg15, arg16, arg17, arg18,
                arg19);
}

using MixedRuntimeCall_0 = AnyCType (*)();

#define BRACKETS(ident, N) ident[N]

#define REP_0(expr, FMT)
#define REP_1(expr, FMT) FMT(expr, 0)
#define REP_2(expr, FMT) REP_1(expr, FMT), FMT(expr, 1)
#define REP_3(expr, FMT) REP_2(expr, FMT), FMT(expr, 2)
#define REP_4(expr, FMT) REP_3(expr, FMT), FMT(expr, 3)
#define REP_5(expr, FMT) REP_4(expr, FMT), FMT(expr, 4)
#define REP_6(expr, FMT) REP_5(expr, FMT), FMT(expr, 5)
#define REP_7(expr, FMT) REP_6(expr, FMT), FMT(expr, 6)
#define REP_8(expr, FMT) REP_7(expr, FMT), FMT(expr, 7)
#define REP_9(expr, FMT) REP_8(expr, FMT), FMT(expr, 8)
#define REP_10(expr, FMT) REP_9(expr, FMT), FMT(expr, 9)
#define REP_11(expr, FMT) REP_10(expr, FMT), FMT(expr, 10)
#define REP_12(expr, FMT) REP_11(expr, FMT), FMT(expr, 11)
#define REP_13(expr, FMT) REP_12(expr, FMT), FMT(expr, 12)
#define REP_14(expr, FMT) REP_13(expr, FMT), FMT(expr, 13)
#define REP_15(expr, FMT) REP_14(expr, FMT), FMT(expr, 14)
#define REP_16(expr, FMT) REP_15(expr, FMT), FMT(expr, 15)
#define REP_17(expr, FMT) REP_16(expr, FMT), FMT(expr, 16)
#define REP_18(expr, FMT) REP_17(expr, FMT), FMT(expr, 17)
#define REP_19(expr, FMT) REP_18(expr, FMT), FMT(expr, 18)
#define REP_20(expr, FMT) REP_19(expr, FMT), FMT(expr, 19)

#define GEN_MAX_PARAM_COUNT(V) \
  V(0)                         \
  V(1)                         \
  V(2)                         \
  V(3)                         \
  V(4)                         \
  V(5)                         \
  V(6)                         \
  V(7)                         \
  V(8)                         \
  V(9)                         \
  V(10)                        \
  V(11)                        \
  V(12)                        \
  V(13)                        \
  V(14)                        \
  V(15)                        \
  V(16)                        \
  V(17)                        \
  V(18)                        \
  V(19)                        \
  V(20)

#define MIXED_RUNTIME_CALL(N) \
  using MixedRuntimeCall_##N = AnyCType (*)(REP_##N(AnyCType arg, CONCAT));

GEN_MAX_PARAM_COUNT(MIXED_RUNTIME_CALL)
#undef MIXED_RUNTIME_CALL

#define CALL_ARGS(N) REP_##N(args, BRACKETS)
#define CALL_TARGET_VARARG(N)                                   \
  if (signature.ParameterCount() == N) { /* NOLINT */           \
    MixedRuntimeCall_##N target =                               \
        reinterpret_cast<MixedRuntimeCall_##N>(target_address); \
    result = target(CALL_ARGS(N));                              \
  } else /* NOLINT */

void Simulator::CallAnyCTypeFunction(Address target_address,
                                     const EncodedCSignature& signature) {
  TraceSim("Type: mixed types BUILTIN_CALL\n");

  const int64_t* stack_pointer = reinterpret_cast<int64_t*>(sp());
  const double* double_stack_pointer = reinterpret_cast<double*>(sp());
  int num_gp_params = 0, num_fp_params = 0, num_stack_params = 0;

  CHECK_LE(signature.ParameterCount(), kMaxCParameters);
  static_assert(sizeof(AnyCType) == 8, "AnyCType is assumed to be 64-bit.");
  AnyCType args[kMaxCParameters];
  // The first 8 parameters of each type (GP or FP) are placed in corresponding
  // registers. The rest are expected to be on the stack, where each parameter
  // type counts on its own. For example a function like:
  // foo(int i1, ..., int i9, float f1, float f2) will use up all 8 GP
  // registers, place i9 on the stack, and place f1 and f2 in FP registers.
  // Source: https://developer.arm.com/documentation/ihi0055/d/, section
  // "Parameter Passing".
  for (int i = 0; i < signature.ParameterCount(); ++i) {
    if (signature.IsFloat(i)) {
      if (num_fp_params < 8) {
        args[i].double_value = dreg(num_fp_params++);
      } else {
        args[i].double_value = double_stack_pointer[num_stack_params++];
      }
    } else {
      if (num_gp_params < 8) {
        args[i].int64_value = xreg(num_gp_params++);
      } else {
        args[i].int64_value = stack_pointer[num_stack_params++];
      }
    }
  }
  AnyCType result;
  GEN_MAX_PARAM_COUNT(CALL_TARGET_VARARG)
  /* else */ {
    UNREACHABLE();
  }
  static_assert(20 == kMaxCParameters,
                "If you've changed kMaxCParameters, please change the "
                "GEN_MAX_PARAM_COUNT macro.");

#undef CALL_TARGET_VARARG
#undef CALL_ARGS
#undef GEN_MAX_PARAM_COUNT

#ifdef DEBUG
  CorruptAllCallerSavedCPURegisters();
#endif

  if (signature.IsReturnFloat()) {
    set_dreg(0, result.double_value);
  } else {
    set_xreg(0, result.int64_value);
  }
}

void Simulator::DoRuntimeCall(Instruction* instr) {
  Redirection* redirection = Redirection::FromInstruction(instr);

  // The called C code might itself call simulated code, so any
  // caller-saved registers (including lr) could still be clobbered by a
  // redirected call.
  Instruction* return_address = lr();

  int64_t external =
      reinterpret_cast<int64_t>(redirection->external_function());

  TraceSim("Call to host function at %p\n", redirection->external_function());

  // SP must be 16-byte-aligned at the call interface.
  bool stack_alignment_exception = ((sp() & 0xF) != 0);
  if (stack_alignment_exception) {
    TraceSim("  with unaligned stack 0x%016" PRIx64 ".\n", sp());
    FATAL("ALIGNMENT EXCEPTION");
  }

  Address func_addr =
      reinterpret_cast<Address>(redirection->external_function());
  SimulatorData* simulator_data = isolate_->simulator_data();
  DCHECK_NOT_NULL(simulator_data);
  const EncodedCSignature& signature =
      simulator_data->GetSignatureForTarget(func_addr);
  if (signature.IsValid()) {
    CHECK(redirection->type() == ExternalReference::FAST_C_CALL);
    CallAnyCTypeFunction(external, signature);
    set_lr(return_address);
    set_pc(return_address);
    return;
  }

  int64_t* stack_pointer = reinterpret_cast<int64_t*>(sp());

  const int64_t arg0 = xreg(0);
  const int64_t arg1 = xreg(1);
  const int64_t arg2 = xreg(2);
  const int64_t arg3 = xreg(3);
  const int64_t arg4 = xreg(4);
  const int64_t arg5 = xreg(5);
  const int64_t arg6 = xreg(6);
  const int64_t arg7 = xreg(7);
  const int64_t arg8 = stack_pointer[0];
  const int64_t arg9 = stack_pointer[1];
  const int64_t arg10 = stack_pointer[2];
  const int64_t arg11 = stack_pointer[3];
  const int64_t arg12 = stack_pointer[4];
  const int64_t arg13 = stack_pointer[5];
  const int64_t arg14 = stack_pointer[6];
  const int64_t arg15 = stack_pointer[7];
  const int64_t arg16 = stack_pointer[8];
  const int64_t arg17 = stack_pointer[9];
  const int64_t arg18 = stack_pointer[10];
  const int64_t arg19 = stack_pointer[11];
  static_assert(kMaxCParameters == 20);

#ifdef V8_USE_MEMORY_SANITIZER
  // `UnsafeGenericFunctionCall()` dispatches calls to functions with
  // varying signatures and relies on the fact that the mismatched prototype
  // used by the caller and the prototype used by the callee (defined using
  // the `RUNTIME_FUNCTION*()` macros happen to line up so that things more
  // or less work out [1].
  //
  // Unfortunately, this confuses MSan's uninit tracking with eager checks
  // enabled; it's unclear if these are all false positives or if there are
  // legitimate reports. For now, unconditionally unpoison args to
  // unblock finding and fixing more violations with MSan eager checks.
  //
  // TODO(crbug.com/v8/14712): Fix the MSan violations and migrate to
  // something like crrev.com/c/5422076 instead.
  //
  // [1] Yes, this is undefined behaviour. 🙈🙉🙊
  MSAN_MEMORY_IS_INITIALIZED(&arg0, sizeof(arg0));
  MSAN_MEMORY_IS_INITIALIZED(&arg1, sizeof(arg1));
  MSAN_MEMORY_IS_INITIALIZED(&arg2, sizeof(arg2));
  MSAN_MEMORY_IS_INITIALIZED(&arg3, sizeof(arg3));
  MSAN_MEMORY_IS_INITIALIZED(&arg4, sizeof(arg4));
  MSAN_MEMORY_IS_INITIALIZED(&arg5, sizeof(arg5));
  MSAN_MEMORY_IS_INITIALIZED(&arg6, sizeof(arg6));
  MSAN_MEMORY_IS_INITIALIZED(&arg7, sizeof(arg7));
  MSAN_MEMORY_IS_INITIALIZED(&arg8, sizeof(arg8));
  MSAN_MEMORY_IS_INITIALIZED(&arg9, sizeof(arg9));
  MSAN_MEMORY_IS_INITIALIZED(&arg10, sizeof(arg10));
  MSAN_MEMORY_IS_INITIALIZED(&arg11, sizeof(arg11));
  MSAN_MEMORY_IS_INITIALIZED(&arg12, sizeof(arg12));
  MSAN_MEMORY_IS_INITIALIZED(&arg13, sizeof(arg13));
  MSAN_MEMORY_IS_INITIALIZED(&arg14, sizeof(arg14));
  MSAN_MEMORY_IS_INITIALIZED(&arg15, sizeof(arg15));
  MSAN_MEMORY_IS_INITIALIZED(&arg16, sizeof(arg16));
  MSAN_MEMORY_IS_INITIALIZED(&arg17, sizeof(arg17));
  MSAN_MEMORY_IS_INITIALIZED(&arg18, sizeof(arg18));
  MSAN_MEMORY_IS_INITIALIZED(&arg19, sizeof(arg19));
#endif  // V8_USE_MEMORY_SANITIZER

  switch (redirection->type()) {
    default:
      TraceSim("Type: Unknown.\n");
      UNREACHABLE();

    case ExternalReference::BUILTIN_CALL:
#if defined(V8_OS_WIN)
    {
      // Object f(v8::internal::Arguments).
      TraceSim("Type: BUILTIN_CALL\n");

      // When this simulator runs on Windows x64 host, function with ObjectPair
      // return type accepts an implicit pointer to caller allocated memory for
      // ObjectPair as return value. This diverges the calling convention from
      // function which returns primitive type, so function returns ObjectPair
      // and primitive type cannot share implementation.

      // We don't know how many arguments are being passed, but we can
      // pass 8 without touching the stack. They will be ignored by the
      // host function if they aren't used.
      TraceSim(
          "Arguments: "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64,
          arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10,
          arg11, arg12, arg13, arg14, arg15, arg16, arg17, arg18, arg19);

      SimulatorRuntimeCall_ReturnPtr target =
          reinterpret_cast<SimulatorRuntimeCall_ReturnPtr>(external);

      int64_t result = target(arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7,
                              arg8, arg9, arg10, arg11, arg12, arg13, arg14,
                              arg15, arg16, arg17, arg18, arg19);
      TraceSim("Returned: 0x%16\n", result);
#ifdef DEBUG
      CorruptAllCallerSavedCPURegisters();
#endif
      set_xreg(0, result);

      break;
    }
#endif
    case ExternalReference::BUILTIN_CALL_PAIR: {
      // Object f(v8::internal::Arguments) or
      // ObjectPair f(v8::internal::Arguments).
      TraceSim("Type: BUILTIN_CALL\n");

      // We don't know how many arguments are being passed, but we can
      // pass 8 without touching the stack. They will be ignored by the
      // host function if they aren't used.
      TraceSim(
          "Arguments: "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64
          ", "
          "0x%016" PRIx64 ", 0x%016" PRIx64,
          arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10,
          arg11, arg12, arg13, arg14, arg15, arg16, arg17, arg18, arg19);

      ObjectPair result = UnsafeGenericFunctionCall(
          external, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9,
          arg10, arg11, arg12, arg13, arg14, arg15, arg16, arg17, arg18, arg19);
#ifdef V8_USE_MEMORY_SANITIZER
      // `UnsafeGenericFunctionCall()` dispatches calls to functions with
      // varying signatures and relies on the fact that the mismatched prototype
      // used by the caller and the prototype used by the callee (defined using
      // the `RUNTIME_FUNCTION*()` macros happen to line up so that things more
      // or less work out [1].
      //
      // Unfortunately, this confuses MSan's uninit tracking with eager checks
      // enabled; it's unclear if these are all false positives or if there are
      // legitimate reports. For now, unconditionally unpoison `result` to
      // unblock finding and fixing more violations with MSan eager checks.
      //
      // TODO(crbug.com/v8/14712): Fix the MSan violations and migrate to
      // something like crrev.com/c/5422076 instead.
      //
      // [1] Yes, this is undefined behaviour. 🙈🙉🙊
      MSAN_MEMORY_IS_INITIALIZED(&result, sizeof(result));
#endif
      TraceSim("Returned: {%p, %p}\n", reinterpret_cast<void*>(result.x),
               reinterpret_cast<void*>(result.y));
#ifdef DEBUG
      CorruptAllCallerSavedCPURegisters();
#endif
      set_xreg(0, static_cast<int64_t>(result.x));
      set_xreg(1, static_cast<int64_t>(result.y));
      break;
    }

    case ExternalReference::BUILTIN_COMPARE_CALL: {
      // int f(double, double)
      TraceSim("Type: BUILTIN_COMPARE_CALL\n");
      SimulatorRuntimeCompareCall target =
          reinterpret_cast<SimulatorRuntimeCompareCall>(external);
      TraceSim("Arguments: %f, %f\n", dreg(0), dreg(1));
      int64_t result = target(dreg(0), dreg(1));
      TraceSim("Returned: %" PRId64 "\n", result);
#ifdef DEBUG
      CorruptAllCallerSavedCPURegisters();
#endif
      set_xreg(0, result);
      break;
    }

    case ExternalReference::BUILTIN_FP_CALL: {
      // double f(double)
      TraceSim("Type: BUILTIN_FP_CALL\n");
      SimulatorRuntimeFPCall target =
          reinterpret_cast<SimulatorRuntimeFPCall>(external);
      TraceSim("Argument: %f\n", dreg(0));
      double result = target(dreg(0));
      TraceSim("Returned: %f\n", result);
#ifdef DEBUG
      CorruptAllCallerSavedCPURegisters();
#endif
      set_dreg(0, result);
      break;
    }

    case ExternalReference::BUILTIN_FP_FP_CALL: {
      // double f(double, double)
      TraceSim("Type: BUILTIN_FP_FP_CALL\n");
      SimulatorRuntimeFPFPCall target =
          reinterpret_cast<SimulatorRuntimeFPFPCall>(external);
      TraceSim("Arguments: %f, %f\n", dreg(0), dreg(1));
      double result = target(dreg(0), dreg(1));
      TraceSim("Returned: %f\n", result);
#ifdef DEBUG
      CorruptAllCallerSavedCPURegisters();
#endif
      set_dreg(0, result);
      break;
    }

    case ExternalReference::BUILTIN_FP_INT_CALL: {
      // double f(double, int)
      TraceSim("Type: BUILTIN_FP_INT_CALL\n");
      SimulatorRuntimeFPIntCall target =
          reinterpret_cast<SimulatorRuntimeFPIntCall>(external);
      TraceSim("Arguments: %f, %d\n", dreg(0), wreg(0));
      double result = target(dreg(0), wreg(0));
      TraceSim("Returned: %f\n", result);
#ifdef DEBUG
      CorruptAllCallerSavedCPURegisters();
#endif
      set_dreg(0, result);
      break;
    }

    case ExternalReference::BUILTIN_FP_POINTER_CALL: {
      // double f(Address tagged_ptr)
      TraceSim("Type: BUILTIN_FP_POINTER_CALL\n");
      SimulatorRuntimeFPTaggedCall target =
          reinterpret_cast<SimulatorRuntimeFPTaggedCall>(external);
      TraceSim(
          "Arguments: "
          "0x%016" PRIx64 ", 0x%016" PRIx64 ", 0x%016" PRIx64 ", 0x%016" PRIx64,
          arg0, arg1, arg2, arg3);
      double result = target(arg0, arg1, arg2, arg3);
      TraceSim("Returned: %f\n", result);
#ifdef DEBUG
      CorruptAllCallerSavedCPURegisters();
#endif
      set_dreg(0, result);
      break;
    }

    case ExternalReference::DIRECT_API_CALL: {
      // void f(v8::FunctionCallbackInfo&)
      TraceSim("Type: DIRECT_API_CALL\n");
      TraceSim("Arguments: 0x%016" PRIx64 "\n", arg0);
      SimulatorRuntimeDirectApiCall target =
          reinterpret_cast<SimulatorRuntimeDirectApiCall>(external);
      target(arg0);
      TraceSim("No return value.");
#ifdef DEBUG
      CorruptAllCallerSavedCPURegisters();
#endif
      break;
    }

    case ExternalReference::DIRECT_GETTER_CALL: {
      // void f(v8::Local<String> property, v8::PropertyCallbackInfo& info)
      TraceSim("Type: DIRECT_GETTER_CALL\n");
      TraceSim("Arguments: 0x%016" PRIx64 ", 0x%016" PRIx64 "\n", arg0, arg1);
      SimulatorRuntimeDirectGetterCall target =
          reinterpret_cast<SimulatorRuntimeDirectGetterCall>(external);
      target(arg0, arg1);
      TraceSim("No return value.");
#ifdef DEBUG
      CorruptAllCallerSavedCPURegisters();
#endif
      break;
    }
  }

  set_lr(return_address);
  set_pc(return_address);
}

const char* Simulator::xreg_names[] = {
    "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",  "x8",  "x9",  "x10",
    "x11", "x12", "x13", "x14", "x15", "ip0", "ip1", "x18", "x19", "x20", "x21",
    "x22", "x23", "x24", "x25", "x26", "cp",  "x28", "fp",  "lr",  "xzr", "sp"};

const char* Simulator::wreg_names[] = {
    "w0",  "w1",  "w2",  "w3",  "w4",  "w5",  "w6",  "w7",  "w8",
    "w9",  "w10", "w11", "w12", "w13", "w14", "w15", "w16", "w17",
    "w18", "w19", "w20", "w21", "w22", "w23", "w24", "w25", "w26",
    "wcp", "w28", "wfp", "wlr", "wzr", "wsp"};

const char* Simulator::sreg_names[] = {
    "s0",  "s1",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",  "s8",  "s9",  "s10",
    "s11", "s12", "s13", "s14", "s15", "s16", "s17", "s18", "s19", "s20", "s21",
    "s22", "s23", "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31"};

const char* Simulator::dreg_names[] = {
    "d0",  "d1",  "d2",  "d3",  "d4",  "d5",  "d6",  "d7",  "d8",  "d9",  "d10",
    "d11", "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21",
    "d22", "d23", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31"};

const char* Simulator::vreg_names[] = {
    "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",  "v8",  "v9",  "v10",
    "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21",
    "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"};

const char* Simulator::WRegNameForCode(unsigned code, Reg31Mode mode) {
  static_assert(arraysize(Simulator::wreg_names) == (kNumberOfRegisters + 1),
                "Array must be large enough to hold all register names.");
  DCHECK_LT(code, static_cast<unsigned>(kNumberOfRegisters));
  // The modulo operator has no effect here, but it silences a broken GCC
  // warning about out-of-bounds array accesses.
  code %= kNumberOfRegisters;

  // If the code represents the stack pointer, index the name after zr.
  if ((code == kZeroRegCode) && (mode == Reg31IsStackPointer)) {
    code = kZeroRegCode + 1;
  }
  return wreg_names[code];
}

const char* Simulator::XRegNameForCode(unsigned code, Reg31Mode mode) {
  static_assert(arraysize(Simulator::xreg_names) == (kNumberOfRegisters + 1),
                "Array must be large enough to hold all register names.");
  DCHECK_LT(code, static_cast<unsigned>(kNumberOfRegisters));
  code %= kNumberOfRegisters;

  // If the code represents the stack pointer, index the name after zr.
  if ((code == kZeroRegCode) && (mode == Reg31IsStackPointer)) {
    code = kZeroRegCode + 1;
  }
  return xreg_names[code];
}

const char* Simulator::SRegNameForCode(unsigned code) {
  static_assert(arraysize(Simulator::sreg_names) == kNumberOfVRegisters,
                "Array must be large enough to hold all register names.");
  DCHECK_LT(code, static_cast<unsigned>(kNumberOfVRegisters));
  return sreg_names[code % kNumberOfVRegisters];
}

const char* Simulator::DRegNameForCode(unsigned code) {
  static_assert(arraysize(Simulator::dreg_names) == kNumberOfVRegisters,
                "Array must be large enough to hold all register names.");
  DCHECK_LT(code, static_cast<unsigned>(kNumberOfVRegisters));
  return dreg_names[code % kNumberOfVRegisters];
}

const char* Simulator::VRegNameForCode(unsigned code) {
  static_assert(arraysize(Simulator::vreg_names) == kNumberOfVRegisters,
                "Array must be large enough to hold all register names.");
  DCHECK_LT(code, static_cast<unsigned>(kNumberOfVRegisters));
  return vreg_names[code % kNumberOfVRegisters];
}

void LogicVRegister::ReadUintFromMem(VectorFormat vform, int index,
                                     uint64_t addr) const {
  switch (LaneSizeInBitsFromFormat(vform)) {
    case 8:
      register_.Insert(index, SimMemory::Read<uint8_t>(addr));
      break;
    case 16:
      register_.Insert(index, SimMemory::Read<uint16_t>(addr));
      break;
    case 32:
      register_.Insert(index, SimMemory::Read<uint32_t>(addr));
      break;
    case 64:
      register_.Insert(index, SimMemory::Read<uint64_t>(addr));
      break;
    default:
      UNREACHABLE();
  }
}

void LogicVRegister::WriteUintToMem(VectorFormat vform, int index,
                                    uint64_t addr) const {
  switch (LaneSizeInBitsFromFormat(vform)) {
    case 8:
      SimMemory::Write<uint8_t>(addr, static_cast<uint8_t>(Uint(vform, index)));
      break;
    case 16:
      SimMemory::Write<uint16_t>(addr,
                                 static_cast<uint16_t>(Uint(vform, index)));
      break;
    case 32:
      SimMemory::Write<uint32_t>(addr,
                                 static_cast<uint32_t>(Uint(vform, index)));
      break;
    case 64:
      SimMemory::Write<uint64_t>(addr, Uint(vform, index));
      break;
    default:
      UNREACHABLE();
  }
}

int Simulator::CodeFromName(const char* name) {
  for (unsigned i = 0; i < kNumberOfRegisters; i++) {
    if ((strcmp(xreg_names[i], name) == 0) ||
        (strcmp(wreg_names[i], name) == 0)) {
      return i;
    }
  }
  for (unsigned i = 0; i < kNumberOfVRegisters; i++) {
    if ((strcmp(vreg_names[i], name) == 0) ||
        (strcmp(dreg_names[i], name) == 0) ||
        (strcmp(sreg_names[i], name) == 0)) {
      return i;
    }
  }
  if ((strcmp("sp", name) == 0) || (strcmp("wsp", name) == 0)) {
    return kSPRegInternalCode;
  }
  if (strcmp("x16", name) == 0) return CodeFromName("ip0");
  if (strcmp("x17", name) == 0) return CodeFromName("ip1");
  if (strcmp("x29", name) == 0) return CodeFromName("fp");
  if (strcmp("x30", name) == 0) return CodeFromName("lr");
  return -1;
}

// Helpers ---------------------------------------------------------------------
template <typename T>
T Simulator::AddWithCarry(bool set_flags, T left, T right, int carry_in) {
  // Use unsigned types to avoid implementation-defined overflow behaviour.
  static_assert(std::is_unsigned<T>::value, "operands must be unsigned");
  static_assert((sizeof(T) == kWRegSize) || (sizeof(T) == kXRegSize),
                "Only W- or X-sized operands are tested");

  DCHECK((carry_in == 0) || (carry_in == 1));
  T result = left + right + carry_in;

  if (set_flags) {
    nzcv().SetN(CalcNFlag(result));
    nzcv().SetZ(CalcZFlag(result));

    // Compute the C flag by comparing the result to the max unsigned integer.
    T max_uint_2op = std::numeric_limits<T>::max() - carry_in;
    nzcv().SetC((left > max_uint_2op) || ((max_uint_2op - left) < right));

    // Overflow iff the sign bit is the same for the two inputs and different
    // for the result.
    T sign_mask = T(1) << (sizeof(T) * 8 - 1);
    T left_sign = left & sign_mask;
    T right_sign = right & sign_mask;
    T result_sign = result & sign_mask;
    nzcv().SetV((left_sign == right_sign) && (left_sign != result_sign));

    LogSystemRegister(NZCV);
  }
  return result;
}

template <typename T>
void Simulator::AddSubWithCarry(Instruction* instr) {
  // Use unsigned types to avoid implementation-defined overflow behaviour.
  static_assert(std::is_unsigned<T>::value, "operands must be unsigned");

  T op2 = reg<T>(instr->Rm());
  T new_val;

  if ((instr->Mask(AddSubOpMask) == SUB) || instr->Mask(AddSubOpMask) == SUBS) {
    op2 = ~op2;
  }

  new_val = AddWithCarry<T>(instr->FlagsUpdate(), reg<T>(instr->Rn()), op2,
                            nzcv().C());

  set_reg<T>(instr->Rd(), new_val);
}

sim_uint128_t Simulator::PolynomialMult128(uint64_t op1, uint64_t op2,
                                           int lane_size_in_bits) const {
  DCHECK_LE(static_cast<unsigned>(lane_size_in_bits), kDRegSizeInBits);
  sim_uint128_t result = std::make_pair(0, 0);
  sim_uint128_t op2q = std::make_pair(0, op2);
  for (int i = 0; i < lane_size_in_bits; i++) {
    if ((op1 >> i) & 1) {
      result = Eor128(result, Lsl128(op2q, i));
    }
  }
  return result;
}

sim_uint128_t Simulator::Lsl128(sim_uint128_t x, unsigned shift) const {
  DCHECK_LE(shift, 64);
  if (shift == 0) return x;
  if (shift == 64) return std::make_pair(x.second, 0);
  uint64_t lo = x.second << shift;
  uint64_t hi = (x.first << shift) | (x.second >> (64 - shift));
  return std::make_pair(hi, lo);
}

sim_uint128_t Simulator::Eor128(sim_uint128_t x, sim_uint128_t y) const {
  return std::make_pair(x.first ^ y.first, x.second ^ y.second);
}

template <typename T>
T Simulator::ShiftOperand(T value, Shift shift_type, unsigned amount) {
  using unsignedT = typename std::make_unsigned<T>::type;

  if (amount == 0) {
    return value;
  }
  // Larger shift {amount}s would be undefined behavior in C++.
  DCHECK(amount < sizeof(value) * kBitsPerByte);

  switch (shift_type) {
    case LSL:
      return static_cast<unsignedT>(value) << amount;
    case LSR:
      return static_cast<unsignedT>(value) >> amount;
    case ASR:
      return value >> amount;
    case ROR: {
      unsignedT mask = (static_cast<unsignedT>(1) << amount) - 1;
      return (static_cast<unsignedT>(value) >> amount) |
             ((value & mask) << (sizeof(mask) * 8 - amount));
    }
    default:
      UNIMPLEMENTED();
      return 0;
  }
}

template <typename T>
T Simulator::ExtendValue(T value, Extend extend_type, unsigned left_shift) {
  const unsigned kSignExtendBShift = (sizeof(T) - 1) * 8;
  const unsigned kSignExtendHShift = (sizeof(T) - 2) * 8;
  const unsigned kSignExtendWShift = (sizeof(T) - 4) * 8;
  using unsignedT = typename std::make_unsigned<T>::type;

  switch (extend_type) {
    case UXTB:
      value &= kByteMask;
      break;
    case UXTH:
      value &= kHalfWordMask;
      break;
    case UXTW:
      value &= kWordMask;
      break;
    case SXTB:
      value =
          static_cast<T>(static_cast<unsignedT>(value) << kSignExtendBShift) >>
          kSignExtendBShift;
      break;
    case SXTH:
      value =
          static_cast<T>(static_cast<unsignedT>(value) << kSignExtendHShift) >>
          kSignExtendHShift;
      break;
    case SXTW:
      value =
          static_cast<T>(static_cast<unsignedT>(value) << kSignExtendWShift) >>
          kSignExtendWShift;
      break;
    case UXTX:
    case SXTX:
      break;
    default:
      UNREACHABLE();
  }
  return static_cast<T>(static_cast<unsignedT>(value) << left_shift);
}

template <typename T>
void Simulator::Extract(Instruction* instr) {
  unsigned lsb = instr->ImmS();
  T op2 = reg<T>(instr->Rm());
  T result = op2;

  if (lsb) {
    T op1 = reg<T>(instr->Rn());
    result = op2 >> lsb | (op1 << ((sizeof(T) * 8) - lsb));
  }
  set_reg<T>(instr->Rd(), result);
}

void Simulator::FPCompare(double val0, double val1) {
  AssertSupportedFPCR();

  // TODO(jbramley): This assumes that the C++ implementation handles
  // comparisons in the way that we expect (as per AssertSupportedFPCR()).
  if ((std::isnan(val0) != 0) || (std::isnan(val1) != 0)) {
    nzcv().SetRawValue(FPUnorderedFlag);
  } else if (val0 < val1) {
    nzcv().SetRawValue(FPLessThanFlag);
  } else if (val0 > val1) {
    nzcv().SetRawValue(FPGreaterThanFlag);
  } else if (val0 == val1) {
    nzcv().SetRawValue(FPEqualFlag);
  } else {
    UNREACHABLE();
  }
  LogSystemRegister(NZCV);
}

Simulator::PrintRegisterFormat Simulator::GetPrintRegisterFormatForSize(
    size_t reg_size, size_t lane_size) {
  DCHECK_GE(reg_size, lane_size);

  uint32_t format = 0;
  if (reg_size != lane_size) {
    switch (reg_size) {
      default:
        UNREACHABLE();
      case kQRegSize:
        format = kPrintRegAsQVector;
        break;
      case kDRegSize:
        format = kPrintRegAsDVector;
        break;
    }
  }

  switch (lane_size) {
    default:
      UNREACHABLE();
    case kQRegSize:
      format |= kPrintReg1Q;
      break;
    case kDRegSize:
      format |= kPrintReg1D;
      break;
    case kSRegSize:
      format |= kPrintReg1S;
      break;
    case kHRegSize:
      format |= kPrintReg1H;
      break;
    case kBRegSize:
      format |= kPrintReg1B;
      break;
  }

  // These sizes would be duplicate case labels.
  static_assert(kXRegSize == kDRegSize, "X and D registers must be same size.");
  static_assert(kWRegSize == kSRegSize, "W and S registers must be same size.");
  static_assert(kPrintXReg == kPrintReg1D,
                "X and D register printing code is shared.");
  static_assert(kPrintWReg == kPrintReg1S,
                "W and S register printing code is shared.");

  return static_cast<PrintRegisterFormat>(format);
}

Simulator::PrintRegisterFormat Simulator::GetPrintRegisterFormat(
    VectorFormat vform) {
  switch (vform) {
    default:
      UNREACHABLE();
    case kFormat16B:
      return kPrintReg16B;
    case kFormat8B:
      return kPrintReg8B;
    case kFormat8H:
      return kPrintReg8H;
    case kFormat4H:
      return kPrintReg4H;
    case kFormat4S:
      return kPrintReg4S;
    case kFormat2S:
      return kPrintReg2S;
    case kFormat2D:
      return kPrintReg2D;
    case kFormat1D:
      return kPrintReg1D;

    case kFormatB:
      return kPrintReg1B;
    case kFormatH:
      return kPrintReg1H;
    case kFormatS:
      return kPrintReg1S;
    case kFormatD:
      return kPrintReg1D;
  }
}

Simulator::PrintRegisterFormat Simulator::GetPrintRegisterFormatFP(
    VectorFormat vform) {
  switch (vform) {
    default:
      UNREACHABLE();
    case kFormat4S:
      return kPrintReg4SFP;
    case kFormat2S:
      return kPrintReg2SFP;
    case kFormat2D:
      return kPrintReg2DFP;
    case kFormat1D:
      return kPrintReg1DFP;

    case kFormatS:
      return kPrintReg1SFP;
    case kFormatD:
      return kPrintReg1DFP;
  }
}

void Simulator::SetBreakpoint(Instruction* location) {
  for (unsigned i = 0; i < breakpoints_.size(); i++) {
    if (breakpoints_.at(i).location == location) {
      PrintF(stream_, "Existing breakpoint at %p was %s\n",
             reinterpret_cast<void*>(location),
             breakpoints_.at(i).enabled ? "disabled" : "enabled");
      breakpoints_.at(i).enabled = !breakpoints_.at(i).enabled;
      return;
    }
  }
  Breakpoint new_breakpoint = {location, true};
  breakpoints_.push_back(new_breakpoint);
  PrintF(stream_, "Set a breakpoint at %p\n",
         reinterpret_cast<void*>(location));
}

void Simulator::ListBreakpoints() {
  PrintF(stream_, "Breakpoints:\n");
  for (unsigned i = 0; i < breakpoints_.size(); i++) {
    PrintF(stream_, "%p  : %s\n",
           reinterpret_cast<void*>(breakpoints_.at(i).location),
           breakpoints_.at(i).enabled ? "enabled" : "disabled");
  }
}

void Simulator::CheckBreakpoints() {
  bool hit_a_breakpoint = false;
  for (unsigned i = 0; i < breakpoints_.size(); i++) {
    if ((breakpoints_.at(i).location == pc_) && breakpoints_.at(i).enabled) {
      hit_a_breakpoint = true;
      // Disable this breakpoint.
      breakpoints_.at(i).enabled = false;
    }
  }
  if (hit_a_breakpoint) {
    PrintF(stream_, "Hit and disabled a breakpoint at %p.\n",
           reinterpret_cast<void*>(pc_));
    Debug();
  }
}

void Simulator::CheckBreakNext() {
  // If the current instruction is a BL, insert a breakpoint just after it.
  if (break_on_next_ && pc_->IsBranchAndLinkToRegister()) {
    SetBreakpoint(pc_->following());
    break_on_next_ = false;
  }
}

void Simulator::PrintInstructionsAt(Instruction* start, uint64_t count) {
  Instruction* end = start->InstructionAtOffset(count * kInstrSize);
  for (Instruction* pc = start; pc < end; pc = pc->following()) {
    disassembler_decoder_->Decode(pc);
  }
}

void Simulator::PrintWrittenRegisters() {
  for (unsigned i = 0; i < kNumberOfRegisters; i++) {
    if (registers_[i].WrittenSinceLastLog()) PrintRegister(i);
  }
}

void Simulator::PrintWrittenVRegisters() {
  for (unsigned i = 0; i < kNumberOfVRegisters; i++) {
    // At this point there is no type information, so print as a raw 1Q.
    if (vregisters_[i].WrittenSinceLastLog()) PrintVRegister(i, kPrintReg1Q);
  }
}

void Simulator::PrintSystemRegisters() {
  PrintSystemRegister(NZCV);
  PrintSystemRegister(FPCR);
}

void Simulator::PrintRegisters() {
  for (unsigned i = 0; i < kNumberOfRegisters; i++) {
    PrintRegister(i);
  }
}

void Simulator::PrintVRegisters() {
  for (unsigned i = 0; i < kNumberOfVRegisters; i++) {
    // At this point there is no type information, so print as a raw 1Q.
    PrintVRegister(i, kPrintReg1Q);
  }
}

void Simulator::PrintRegister(unsigned code, Reg31Mode r31mode) {
  registers_[code].NotifyRegisterLogged();

  // Don't print writes into xzr.
  if ((code == kZeroRegCode) && (r31mode == Reg31IsZeroRegister)) {
    return;
  }

  // The template for all x and w registers:
  //   "# x{code}: 0x{value}"
  //   "# w{code}: 0x{value}"

  PrintRegisterRawHelper(code, r31mode);
  fprintf(stream_, "\n");
}

// Print a register's name and raw value.
//
// The `bytes` and `lsb` arguments can be used to limit the bytes that are
// printed. These arguments are intended for use in cases where register hasn't
// actually been updated (such as in PrintVWrite).
//
// No newline is printed. This allows the caller to print more details (such as
// a floating-point interpretation or a memory access annotation).
void Simulator::PrintVRegisterRawHelper(unsigned code, int bytes, int lsb) {
  // The template for vector types:
  //   "# v{code}: 0xFFEEDDCCBBAA99887766554433221100".
  // An example with bytes=4 and lsb=8:
  //   "# v{code}:         0xBBAA9988                ".
  fprintf(stream_, "# %s%5s: %s", clr_vreg_name, VRegNameForCode(code),
          clr_vreg_value);

  int msb = lsb + bytes - 1;
  int byte = kQRegSize - 1;

  // Print leading padding spaces. (Two spaces per byte.)
  while (byte > msb) {
    fprintf(stream_, "  ");
    byte--;
  }

  // Print the specified part of the value, byte by byte.
  qreg_t rawbits = qreg(code);
  fprintf(stream_, "0x");
  while (byte >= lsb) {
    fprintf(stream_, "%02x", rawbits.val[byte]);
    byte--;
  }

  // Print trailing padding spaces.
  while (byte >= 0) {
    fprintf(stream_, "  ");
    byte--;
  }
  fprintf(stream_, "%s", clr_normal);
}

// Print each of the specified lanes of a register as a float or double value.
//
// The `lane_count` and `lslane` arguments can be used to limit the lanes that
// are printed. These arguments are intended for use in cases where register
// hasn't actually been updated (such as in PrintVWrite).
//
// No newline is printed. This allows the caller to print more details (such as
// a memory access annotation).
void Simulator::PrintVRegisterFPHelper(unsigned code,
                                       unsigned lane_size_in_bytes,
                                       int lane_count, int rightmost_lane) {
  DCHECK((lane_size_in_bytes == kSRegSize) ||
         (lane_size_in_bytes == kDRegSize));

  unsigned msb = (lane_count + rightmost_lane) * lane_size_in_bytes;
  DCHECK_LE(msb, static_cast<unsigned>(kQRegSize));

  // For scalar types ((lane_count == 1) && (rightmost_lane == 0)), a register
  // name is used:
  //   " (s{code}: {value})"
  //   " (d{code}: {value})"
  // For vector types, "..." is used to represent one or more omitted lanes.
  //   " (..., {value}, {value}, ...)"
  if ((lane_count == 1) && (rightmost_lane == 0)) {
    const char* name = (lane_size_in_bytes == kSRegSize)
                           ? SRegNameForCode(code)
                           : DRegNameForCode(code);
    fprintf(stream_, " (%s%s: ", clr_vreg_name, name);
  } else {
    if (msb < (kQRegSize - 1)) {
      fprintf(stream_, " (..., ");
    } else {
      fprintf(stream_, " (");
    }
  }

  // Print the list of values.
  const char* separator = "";
  int leftmost_lane = rightmost_lane + lane_count - 1;
  for (int lane = leftmost_lane; lane >= rightmost_lane; lane--) {
    double value = (lane_size_in_bytes == kSRegSize)
                       ? vreg(code).Get<float>(lane)
                       : vreg(code).Get<double>(lane);
    fprintf(stream_, "%s%s%#g%s", separator, clr_vreg_value, value, clr_normal);
    separator = ", ";
  }

  if (rightmost_lane > 0) {
    fprintf(stream_, ", ...");
  }
  fprintf(stream_, ")");
}

// Print a register's name and raw value.
//
// Only the least-significant `size_in_bytes` bytes of the register are printed,
// but the value is aligned as if the whole register had been printed.
//
// For typical register updates, size_in_bytes should be set to kXRegSize
// -- the default -- so that the whole register is printed. Other values of
// size_in_bytes are intended for use when the register hasn't actually been
// updated (such as in PrintWrite).
//
// No newline is printed. This allows the caller to print more details (such as
// a memory access annotation).
void Simulator::PrintRegisterRawHelper(unsigned code, Reg31Mode r31mode,
                                       int size_in_bytes) {
  // The template for all supported sizes.
  //   "# x{code}: 0xFFEEDDCCBBAA9988"
  //   "# w{code}:         0xBBAA9988"
  //   "# w{code}<15:0>:       0x9988"
  //   "# w{code}<7:0>:          0x88"
  unsigned padding_chars = (kXRegSize - size_in_bytes) * 2;

  const char* name = "";
  const char* suffix = "";
  switch (size_in_bytes) {
    case kXRegSize:
      name = XRegNameForCode(code, r31mode);
      break;
    case kWRegSize:
      name = WRegNameForCode(code, r31mode);
      break;
    case 2:
      name = WRegNameForCode(code, r31mode);
      suffix = "<15:0>";
      padding_chars -= strlen(suffix);
      break;
    case 1:
      name = WRegNameForCode(code, r31mode);
      suffix = "<7:0>";
      padding_chars -= strlen(suffix);
      break;
    default:
      UNREACHABLE();
  }
  fprintf(stream_, "# %s%5s%s: ", clr_reg_name, name, suffix);

  // Print leading padding spaces.
  DCHECK_LT(padding_chars, kXRegSize * 2U);
  for (unsigned i = 0; i < padding_chars; i++) {
    putc(' ', stream_);
  }

  // Print the specified bits in hexadecimal format.
  uint64_t bits = reg<uint64_t>(code, r31mode);
  bits &= kXRegMask >> ((kXRegSize - size_in_bytes) * 8);
  static_assert(sizeof(bits) == kXRegSize,
                "X registers and uint64_t must be the same size.");

  int chars = size_in_bytes * 2;
  fprintf(stream_, "%s0x%0*" PRIx64 "%s", clr_reg_value, chars, bits,
          clr_normal);
}

void Simulator::PrintVRegister(unsigned code, PrintRegisterFormat format) {
  vregisters_[code].NotifyRegisterLogged();

  int lane_size_log2 = format & kPrintRegLaneSizeMask;

  int reg_size_log2;
  if (format & kPrintRegAsQVector) {
    reg_size_log2 = kQRegSizeLog2;
  } else if (format & kPrintRegAsDVector) {
    reg_size_log2 = kDRegSizeLog2;
  } else {
    // Scalar types.
    reg_size_log2 = lane_size_log2;
  }

  int lane_count = 1 << (reg_size_log2 - lane_size_log2);
  int lane_size = 1 << lane_size_log2;

  // The template for vector types:
  //   "# v{code}: 0x{rawbits} (..., {value}, ...)".
  // The template for scalar types:
  //   "# v{code}: 0x{rawbits} ({reg}:{value})".
  // The values in parentheses after the bit representations are floating-point
  // interpretations. They are displayed only if the kPrintVRegAsFP bit is set.

  PrintVRegisterRawHelper(code);
  if (format & kPrintRegAsFP) {
    PrintVRegisterFPHelper(code, lane_size, lane_count);
  }

  fprintf(stream_, "\n");
}

void Simulator::PrintSystemRegister(SystemRegister id) {
  switch (id) {
    case NZCV:
      fprintf(stream_, "# %sNZCV: %sN:%d Z:%d C:%d V:%d%s\n", clr_flag_name,
              clr_flag_value, nzcv().N(), nzcv().Z(), nzcv().C(), nzcv().V(),
              clr_normal);
      break;
    case FPCR: {
      static const char* rmode[] = {
          "0b00 (Round to Nearest)", "0b01 (Round towards Plus Infinity)",
          "0b10 (Round towards Minus Infinity)", "0b11 (Round towards Zero)"};
      DCHECK(fpcr().RMode() < arraysize(rmode));
      fprintf(stream_, "# %sFPCR: %sAHP:%d DN:%d FZ:%d RMode:%s%s\n",
              clr_flag_name, clr_flag_value, fpcr().AHP(), fpcr().DN(),
              fpcr().FZ(), rmode[fpcr().RMode()], clr_normal);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void Simulator::PrintRead(uintptr_t address, unsigned reg_code,
                          PrintRegisterFormat format) {
  registers_[reg_code].NotifyRegisterLogged();

  USE(format);

  // The template is "# {reg}: 0x{value} <- {address}".
  PrintRegisterRawHelper(reg_code, Reg31IsZeroRegister);
  fprintf(stream_, " <- %s0x%016" PRIxPTR "%s\n", clr_memory_address, address,
          clr_normal);
}

void Simulator::PrintVRead(uintptr_t address, unsigned reg_code,
                           PrintRegisterFormat format, unsigned lane) {
  vregisters_[reg_code].NotifyRegisterLogged();

  // The template is "# v{code}: 0x{rawbits} <- address".
  PrintVRegisterRawHelper(reg_code);
  if (format & kPrintRegAsFP) {
    PrintVRegisterFPHelper(reg_code, GetPrintRegLaneSizeInBytes(format),
                           GetPrintRegLaneCount(format), lane);
  }
  fprintf(stream_, " <- %s0x%016" PRIxPTR "%s\n", clr_memory_address, address,
          clr_normal);
}

void Simulator::PrintWrite(uintptr_t address, unsigned reg_code,
                           PrintRegisterFormat format) {
  DCHECK_EQ(GetPrintRegLaneCount(format), 1U);

  // The template is "# v{code}: 0x{value} -> {address}". To keep the trace tidy
  // and readable, the value is aligned with the values in the register trace.
  PrintRegisterRawHelper(reg_code, Reg31IsZeroRegister,
                         GetPrintRegSizeInBytes(format));
  fprintf(stream_, " -> %s0x%016" PRIxPTR "%s\n", clr_memory_address, address,
          clr_normal);
}

void Simulator::PrintVWrite(uintptr_t address, unsigned reg_code,
                            PrintRegisterFormat format, unsigned lane) {
  // The templates:
  //   "# v{code}: 0x{rawbits} -> {address}"
  //   "# v{code}: 0x{rawbits} (..., {value}, ...) -> {address}".
  //   "# v{code}: 0x{rawbits} ({reg}:{value}) -> {address}"
  // Because this trace doesn't represent a change to the source register's
  // value, only the relevant part of the value is printed. To keep the trace
  // tidy and readable, the raw value is aligned with the other values in the
  // register trace.
  int lane_count = GetPrintRegLaneCount(format);
  int lane_size = GetPrintRegLaneSizeInBytes(format);
  int reg_size = GetPrintRegSizeInBytes(format);
  PrintVRegisterRawHelper(reg_code, reg_size, lane_size * lane);
  if (format & kPrintRegAsFP) {
    PrintVRegisterFPHelper(reg_code, lane_size, lane_count, lane);
  }
  fprintf(stream_, " -> %s0x%016" PRIxPTR "%s\n", clr_memory_address, address,
          clr_normal);
}

// Visitors---------------------------------------------------------------------

void Simulator::VisitUnimplemented(Instruction* instr) {
  fprintf(stream_, "Unimplemented instruction at %p: 0x%08" PRIx32 "\n",
          reinterpret_cast<void*>(instr), instr->InstructionBits());
  UNIMPLEMENTED();
}

void Simulator::VisitUnallocated(Instruction* instr) {
  fprintf(stream_, "Unallocated instruction at %p: 0x%08" PRIx32 "\n",
          reinterpret_cast<void*>(instr), instr->InstructionBits());
  UNIMPLEMENTED();
}

void Simulator::VisitPCRelAddressing(Instruction* instr) {
  switch (instr->Mask(PCRelAddressingMask)) {
    case ADR:
      set_reg(instr->Rd(), instr->ImmPCOffsetTarget());
      break;
    case ADRP:  // Not implemented in the assembler.
      UNIMPLEMENTED();
    default:
      UNREACHABLE();
  }
}

void Simulator::VisitUnconditionalBranch(Instruction* instr) {
  switch (instr->Mask(UnconditionalBranchMask)) {
    case BL:
      set_lr(instr->following());
      [[fallthrough]];
    case B:
      set_pc(instr->ImmPCOffsetTarget());
      break;
    default:
      UNREACHABLE();
  }
}

void Simulator::VisitConditionalBranch(Instruction* instr) {
  DCHECK(instr->Mask(ConditionalBranchMask) == B_cond);
  if (ConditionPassed(static_cast<Condition>(instr->ConditionBranch()))) {
    set_pc(instr->ImmPCOffsetTarget());
  }
}

Simulator::BType Simulator::GetBTypeFromInstruction(
    const Instruction* instr) const {
  switch (instr->Mask(UnconditionalBranchToRegisterMask)) {
    case BLR:
      return BranchAndLink;
    case BR:
      if (!PcIsInGuardedPage() || (instr->Rn() == 16) || (instr->Rn() == 17)) {
        return BranchFromUnguardedOrToIP;
      }
      return BranchFromGuardedNotToIP;
  }
  return DefaultBType;
}

void Simulator::VisitUnconditionalBranchToRegister(Instruction* instr) {
  Instruction* target = reg<Instruction*>(instr->Rn());
  switch (instr->Mask(UnconditionalBranchToRegisterMask)) {
    case BLR: {
      set_lr(instr->following());
      if (instr->Rn() == 31) {
        // BLR XZR is used as a guard for the constant pool. We should never hit
        // this, but if we do trap to allow debugging.
        Debug();
      }
      [[fallthrough]];
    }
    case BR:
    case RET:
      set_pc(target);
      break;
    default:
      UNIMPLEMENTED();
  }
  set_btype(GetBTypeFromInstruction(instr));
}

void Simulator::VisitTestBranch(Instruction* instr) {
  unsigned bit_pos =
      (instr->ImmTestBranchBit5() << 5) | instr->ImmTestBranchBit40();
  bool take_branch = ((xreg(instr->Rt()) & (1ULL << bit_pos)) == 0);
  switch (instr->Mask(TestBranchMask)) {
    case TBZ:
      break;
    case TBNZ:
      take_branch = !take_branch;
      break;
    default:
      UNIMPLEMENTED();
  }
  if (take_branch) {
    set_pc(instr->ImmPCOffsetTarget());
  }
}

void Simulator::VisitCompareBranch(Instruction* instr) {
  unsigned rt = instr->Rt();
  bool take_branch = false;
  switch (instr->Mask(CompareBranchMask)) {
    case CBZ_w:
      take_branch = (wreg(rt) == 0);
      break;
    case CBZ_x:
      take_branch = (xreg(rt) == 0);
      break;
    case CBNZ_w:
      take_branch = (wreg(rt) != 0);
      break;
    case CBNZ_x:
      take_branch = (xreg(rt) != 0);
      break;
    default:
      UNIMPLEMENTED();
  }
  if (take_branch) {
    set_pc(instr->ImmPCOffsetTarget());
  }
}

template <typename T>
void Simulator::AddSubHelper(Instruction* instr, T op2) {
  // Use unsigned types to avoid implementation-defined overflow behaviour.
  static_assert(std::is_unsigned<T>::value, "operands must be unsigned");

  bool set_flags = instr->FlagsUpdate();
  T new_val = 0;
  Instr operation = instr->Mask(AddSubOpMask);

  switch (operation) {
    case ADD:
    case ADDS: {
      new_val =
          AddWithCarry<T>(set_flags, reg<T>(instr->Rn(), instr->RnMode()), op2);
      break;
    }
    case SUB:
    case SUBS: {
      new_val = AddWithCarry<T>(set_flags, reg<T>(instr->Rn(), instr->RnMode()),
                                ~op2, 1);
      break;
    }
    default:
      UNREACHABLE();
  }

  set_reg<T>(instr->Rd(), new_val, instr->RdMode());
}

void Simulator::VisitAddSubShifted(Instruction* instr) {
  Shift shift_type = static_cast<Shift>(instr->ShiftDP());
  unsigned shift_amount = instr->ImmDPShift();

  if (instr->SixtyFourBits()) {
    uint64_t op2 = ShiftOperand(xreg(instr->Rm()), shift_type, shift_amount);
    AddSubHelper(instr, op2);
  } else {
    uint32_t op2 = ShiftOperand(wreg(instr->Rm()), shift_type, shift_amount);
    AddSubHelper(instr, op2);
  }
}

void Simulator::VisitAddSubImmediate(Instruction* instr) {
  int64_t op2 = instr->ImmAddSub() << ((instr->ShiftAddSub() == 1) ? 12 : 0);
  if (instr->SixtyFourBits()) {
    AddSubHelper(instr, static_cast<uint64_t>(op2));
  } else {
    AddSubHelper(instr, static_cast<uint32_t>(op2));
  }
}

void Simulator::VisitAddSubExtended(Instruction* instr) {
  Extend ext = static_cast<Extend>(instr->ExtendMode());
  unsigned left_shift = instr->ImmExtendShift();
  if (instr->SixtyFourBits()) {
    uint64_t op2 = ExtendValue(xreg(instr->Rm()), ext, left_shift);
    AddSubHelper(instr, op2);
  } else {
    uint32_t op2 = ExtendValue(wreg(instr->Rm()), ext, left_shift);
    AddSubHelper(instr, op2);
  }
}

void Simulator::VisitAddSubWithCarry(Instruction* instr) {
  if (instr->SixtyFourBits()) {
    AddSubWithCarry<uint64_t>(instr);
  } else {
    AddSubWithCarry<uint32_t>(instr);
  }
}

void Simulator::VisitLogicalShifted(Instruction* instr) {
  Shift shift_type = static_cast<Shift>(instr->ShiftDP());
  unsigned shift_amount = instr->ImmDPShift();

  if (instr->SixtyFourBits()) {
    uint64_t op2 = ShiftOperand(xreg(instr->Rm()), shift_type, shift_amount);
    op2 = (instr->Mask(NOT) == NOT) ? ~op2 : op2;
    LogicalHelper(instr, op2);
  } else {
    uint32_t op2 = ShiftOperand(wreg(instr->Rm()), shift_type, shift_amount);
    op2 = (instr->Mask(NOT) == NOT) ? ~op2 : op2;
    LogicalHelper(instr, op2);
  }
}

void Simulator::VisitLogicalImmediate(Instruction* instr) {
  if (instr->SixtyFourBits()) {
    LogicalHelper(instr, static_cast<uint64_t>(instr->ImmLogical()));
  } else {
    LogicalHelper(instr, static_cast<uint32_t>(instr->ImmLogical()));
  }
}

template <typename T>
void Simulator::LogicalHelper(Instruction* instr, T op2) {
  T op1 = reg<T>(instr->Rn());
  T result = 0;
  bool update_flags = false;

  // Switch on the logical operation, stripping out the NOT bit, as it has a
  // different meaning for logical immediate instructions.
  switch (instr->Mask(LogicalOpMask & ~NOT)) {
    case ANDS:
      update_flags = true;
      [[fallthrough]];
    case AND:
      result = op1 & op2;
      break;
    case ORR:
      result = op1 | op2;
      break;
    case EOR:
      result = op1 ^ op2;
      break;
    default:
      UNIMPLEMENTED();
  }

  if (update_flags) {
    nzcv().SetN(CalcNFlag(result));
    nzcv().SetZ(CalcZFlag(result));
    nzcv().SetC(0);
    nzcv().SetV(0);
    LogSystemRegister(NZCV);
  }

  set_reg<T>(instr->Rd(), result, instr->RdMode());
}

void Simulator::VisitConditionalCompareRegister(Instruction* instr) {
  if (instr->SixtyFourBits()) {
    ConditionalCompareHelper(instr, static_cast<uint64_t>(xreg(instr->Rm())));
  } else {
    ConditionalCompareHelper(instr, static_cast<uint32_t>(wreg(instr->Rm())));
  }
}

void Simulator::VisitConditionalCompareImmediate(Instruction* instr) {
  if (instr->SixtyFourBits()) {
    ConditionalCompareHelper(instr, static_cast<uint64_t>(instr->ImmCondCmp()));
  } else {
    ConditionalCompareHelper(instr, static_cast<uint32_t>(instr->ImmCondCmp()));
  }
}

template <typename T>
void Simulator::ConditionalCompareHelper(Instruction* instr, T op2) {
  // Use unsigned types to avoid implementation-defined overflow behaviour.
  static_assert(std::is_unsigned<T>::value, "operands must be unsigned");

  T op1 = reg<T>(instr->Rn());

  if (ConditionPassed(static_cast<Condition>(instr->Condition()))) {
    // If the condition passes, set the status flags to the result of comparing
    // the operands.
    if (instr->Mask(ConditionalCompareMask) == CCMP) {
      AddWithCarry<T>(true, op1, ~op2, 1);
    } else {
      DCHECK(instr->Mask(ConditionalCompareMask) == CCMN);
      AddWithCarry<T>(true, op1, op2, 0);
    }
  } else {
    // If the condition fails, set the status flags to the nzcv immediate.
    nzcv().SetFlags(instr->Nzcv());
    LogSystemRegister(NZCV);
  }
}

void Simulator::VisitLoadStoreUnsignedOffset(Instruction* instr) {
  int offset = instr->ImmLSUnsigned() << instr->SizeLS();
  LoadStoreHelper(instr, offset, Offset);
}

void Simulator::VisitLoadStoreUnscaledOffset(Instruction* instr) {
  LoadStoreHelper(instr, instr->ImmLS(), Offset);
}

void Simulator::VisitLoadStorePreIndex(Instruction* instr) {
  LoadStoreHelper(instr, instr->ImmLS(), PreIndex);
}

void Simulator::VisitLoadStorePostIndex(Instruction* instr) {
  LoadStoreHelper(instr, instr->ImmLS(), PostIndex);
}

void Simulator::VisitLoadStoreRegisterOffset(Instruction* instr) {
  Extend ext = static_cast<Extend>(instr->ExtendMode());
  DCHECK((ext == UXTW) || (ext == UXTX) || (ext == SXTW) || (ext == SXTX));
  unsigned shift_amount = instr->ImmShiftLS() * instr->SizeLS();

  int64_t offset = ExtendValue(xreg(instr->Rm()), ext, shift_amount);
  LoadStoreHelper(instr, offset, Offset);
}

void Simulator::LoadStoreHelper(Instruction* instr, int64_t offset,
                                AddrMode addrmode) {
  unsigned srcdst = instr->Rt();
  unsigned addr_reg = instr->Rn();
  uintptr_t address = LoadStoreAddress(addr_reg, offset, addrmode);
  uintptr_t stack = 0;

  unsigned access_size = 1 << instr->SizeLS();
  // First, check whether the memory is accessible (for wasm trap handling).
  if (!ProbeMemory(address, access_size)) return;

  {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    if (instr->IsLoad()) {
      local_monitor_.NotifyLoad();
    } else {
      local_monitor_.NotifyStore();
      GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_processor_);
    }
  }

  // Handle the writeback for stores before the store. On a CPU the writeback
  // and the store are atomic, but when running on the simulator it is possible
  // to be interrupted in between. The simulator is not thread safe and V8 does
  // not require it to be to run JavaScript therefore the profiler may sample
  // the "simulated" CPU in the middle of load/store with writeback. The code
  // below ensures that push operations are safe even when interrupted: the
  // stack pointer will be decremented before adding an element to the stack.
  if (instr->IsStore()) {
    LoadStoreWriteBack(addr_reg, offset, addrmode);

    // For store the address post writeback is used to check access below the
    // stack.
    stack = sp();
  }

  LoadStoreOp op = static_cast<LoadStoreOp>(instr->Mask(LoadStoreMask));
  switch (op) {
    // Use _no_log variants to suppress the register trace (LOG_REGS,
    // LOG_VREGS). We will print a more detailed log.
    case LDRB_w:
      set_wreg_no_log(srcdst, MemoryRead<uint8_t>(address));
      break;
    case LDRH_w:
      set_wreg_no_log(srcdst, MemoryRead<uint16_t>(address));
      break;
    case LDR_w:
      set_wreg_no_log(srcdst, MemoryRead<uint32_t>(address));
      break;
    case LDR_x:
      set_xreg_no_log(srcdst, MemoryRead<uint64_t>(address));
      break;
    case LDRSB_w:
      set_wreg_no_log(srcdst, MemoryRead<int8_t>(address));
      break;
    case LDRSH_w:
      set_wreg_no_log(srcdst, MemoryRead<int16_t>(address));
      break;
    case LDRSB_x:
      set_xreg_no_log(srcdst, MemoryRead<int8_t>(address));
      break;
    case LDRSH_x:
      set_xreg_no_log(srcdst, MemoryRead<int16_t>(address));
      break;
    case LDRSW_x:
      set_xreg_no_log(srcdst, MemoryRead<int32_t>(address));
      break;
    case LDR_b:
      set_breg_no_log(srcdst, MemoryRead<uint8_t>(address));
      break;
    case LDR_h:
      set_hreg_no_log(srcdst, MemoryRead<uint16_t>(address));
      break;
    case LDR_s:
      set_sreg_no_log(srcdst, MemoryRead<float>(address));
      break;
    case LDR_d:
      set_dreg_no_log(srcdst, MemoryRead<double>(address));
      break;
    case LDR_q:
      set_qreg_no_log(srcdst, MemoryRead<qreg_t>(address));
      break;

    case STRB_w:
      MemoryWrite<uint8_t>(address, wreg(srcdst));
      break;
    case STRH_w:
      MemoryWrite<uint16_t>(address, wreg(srcdst));
      break;
    case STR_w:
      MemoryWrite<uint32_t>(address, wreg(srcdst));
      break;
    case STR_x:
      MemoryWrite<uint64_t>(address, xreg(srcdst));
      break;
    case STR_b:
      MemoryWrite<uint8_t>(address, breg(srcdst));
      break;
    case STR_h:
      MemoryWrite<uint16_t>(address, hreg(srcdst));
      break;
    case STR_s:
      MemoryWrite<float>(address, sreg(srcdst));
      break;
    case STR_d:
      MemoryWrite<double>(address, dreg(srcdst));
      break;
    case STR_q:
      MemoryWrite<qreg_t>(address, qreg(srcdst));
      break;

    default:
      UNIMPLEMENTED();
  }

  // Print a detailed trace (including the memory address) instead of the basic
  // register:value trace generated by set_*reg().
  if (instr->IsLoad()) {
    if ((op == LDR_s) || (op == LDR_d)) {
      LogVRead(address, srcdst, GetPrintRegisterFormatForSizeFP(access_size));
    } else if ((op == LDR_b) || (op == LDR_h) || (op == LDR_q)) {
      LogVRead(address, srcdst, GetPrintRegisterFormatForSize(access_size));
    } else {
      LogRead(address, srcdst, GetPrintRegisterFormatForSize(access_size));
    }
  } else {
    if ((op == STR_s) || (op == STR_d)) {
      LogVWrite(address, srcdst, GetPrintRegisterFormatForSizeFP(access_size));
    } else if ((op == STR_b) || (op == STR_h) || (op == STR_q)) {
      LogVWrite(address, srcdst, GetPrintRegisterFormatForSize(access_size));
    } else {
      LogWrite(address, srcdst, GetPrintRegisterFormatForSize(access_size));
    }
  }

  // Handle the writeback for loads after the load to ensure safe pop
  // operation even when interrupted in the middle of it. The stack pointer
  // is only updated after the load so pop(fp) will never break the invariant
  // sp <= fp expected while walking the stack in the sampler.
  if (instr->IsLoad()) {
    // For loads the address pre writeback is used to check access below the
    // stack.
    stack = sp();

    LoadStoreWriteBack(addr_reg, offset, addrmode);
  }

  // Accesses below the stack pointer (but above the platform stack limit) are
  // not allowed in the ABI.
  CheckMemoryAccess(address, stack);
}

void Simulator::VisitLoadStorePairOffset(Instruction* instr) {
  LoadStorePairHelper(instr, Offset);
}

void Simulator::VisitLoadStorePairPreIndex(Instruction* instr) {
  LoadStorePairHelper(instr, PreIndex);
}

void Simulator::VisitLoadStorePairPostIndex(Instruction* instr) {
  LoadStorePairHelper(instr, PostIndex);
}

void Simulator::LoadStorePairHelper(Instruction* instr, AddrMode addrmode) {
  unsigned rt = instr->Rt();
  unsigned rt2 = instr->Rt2();
  unsigned addr_reg = instr->Rn();
  size_t access_size = 1ULL << instr->SizeLSPair();
  int64_t offset = instr->ImmLSPair() * access_size;
  uintptr_t address = LoadStoreAddress(addr_reg, offset, addrmode);
  uintptr_t address2 = address + access_size;
  uintptr_t stack = 0;

  {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    if (instr->IsLoad()) {
      local_monitor_.NotifyLoad();
    } else {
      local_monitor_.NotifyStore();
      GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_processor_);
    }
  }

  // Handle the writeback for stores before the store. On a CPU the writeback
  // and the store are atomic, but when running on the simulator it is possible
  // to be interrupted in between. The simulator is not thread safe and V8 does
  // not require it to be to run JavaScript therefore the profiler may sample
  // the "simulated" CPU in the middle of load/store with writeback. The code
  // below ensures that push operations are safe even when interrupted: the
  // stack pointer will be decremented before adding an element to the stack.
  if (instr->IsStore()) {
    LoadStoreWriteBack(addr_reg, offset, addrmode);

    // For store the address post writeback is used to check access below the
    // stack.
    stack = sp();
  }

  LoadStorePairOp op =
      static_cast<LoadStorePairOp>(instr->Mask(LoadStorePairMask));

  // 'rt' and 'rt2' can only be aliased for stores.
  DCHECK(((op & LoadStorePairLBit) == 0) || (rt != rt2));

  switch (op) {
    // Use _no_log variants to suppress the register trace (LOG_REGS,
    // LOG_VREGS). We will print a more detailed log.
    case LDP_w: {
      DCHECK_EQ(access_size, static_cast<unsigned>(kWRegSize));
      set_wreg_no_log(rt, MemoryRead<uint32_t>(address));
      set_wreg_no_log(rt2, MemoryRead<uint32_t>(address2));
      break;
    }
    case LDP_s: {
      DCHECK_EQ(access_size, static_cast<unsigned>(kSRegSize));
      set_sreg_no_log(rt, MemoryRead<float>(address));
      set_sreg_no_log(rt2, MemoryRead<float>(address2));
      break;
    }
    case LDP_x: {
      DCHECK_EQ(access_size, static_cast<unsigned>(kXRegSize));
      set_xreg_no_log(rt, MemoryRead<uint64_t>(address));
      set_xreg_no_log(rt2, MemoryRead<uint64_t>(address2));
      break;
    }
    case LDP_d: {
      DCHECK_EQ(access_size, static_cast<unsigned>(kDRegSize));
      set_dreg_no_log(rt, MemoryRead<double>(address));
      set_dreg_no_log(rt2, MemoryRead<double>(address2));
      break;
    }
    case LDP_q: {
      DCHECK_EQ(access_size, static_cast<unsigned>(kQRegSize));
      set_qreg(rt, MemoryRead<qreg_t>(address), NoRegLog);
      set_qreg(rt2, MemoryRead<qreg_t>(address2), NoRegLog);
      break;
    }
    case LDPSW_x: {
      DCHECK_EQ(access_size, static_cast<unsigned>(kWRegSize));
      set_xreg_no_log(rt, MemoryRead<int32_t>(address));
      set_xreg_no_log(rt2, MemoryRead<int32_t>(address2));
      break;
    }
    case STP_w: {
      DCHECK_EQ(access_size, static_cast<unsigned>(kWRegSize));
      MemoryWrite<uint32_t>(address, wreg(rt));
      MemoryWrite<uint32_t>(address2, wreg(rt2));
      break;
    }
    case STP_s: {
      DCHECK_EQ(access_size, static_cast<unsigned>(kSRegSize));
      MemoryWrite<float>(address, sreg(rt));
      MemoryWrite<float>(address2, sreg(rt2));
      break;
    }
    case STP_x: {
      DCHECK_EQ(access_size, static_cast<unsigned>(kXRegSize));
      MemoryWrite<uint64_t>(address, xreg(rt));
      MemoryWrite<uint64_t>(address2, xreg(rt2));
      break;
    }
    case STP_d: {
      DCHECK_EQ(access_size, static_cast<unsigned>(kDRegSize));
      MemoryWrite<double>(address, dreg(rt));
      MemoryWrite<double>(address2, dreg(rt2));
      break;
    }
    case STP_q: {
      DCHECK_EQ(access_size, static_cast<unsigned>(kQRegSize));
      MemoryWrite<qreg_t>(address, qreg(rt));
      MemoryWrite<qreg_t>(address2, qreg(rt2));
      break;
    }
    default:
      UNREACHABLE();
  }

  // Print a detailed trace (including the memory address) instead of the basic
  // register:value trace generated by set_*reg().
  if (instr->IsLoad()) {
    if ((op == LDP_s) || (op == LDP_d)) {
      LogVRead(address, rt, GetPrintRegisterFormatForSizeFP(access_size));
      LogVRead(address2, rt2, GetPrintRegisterFormatForSizeFP(access_size));
    } else if (op == LDP_q) {
      LogVRead(address, rt, GetPrintRegisterFormatForSize(access_size));
      LogVRead(address2, rt2, GetPrintRegisterFormatForSize(access_size));
    } else {
      LogRead(address, rt, GetPrintRegisterFormatForSize(access_size));
      LogRead(address2, rt2, GetPrintRegisterFormatForSize(access_size));
    }
  } else {
    if ((op == STP_s) || (op == STP_d)) {
      LogVWrite(address, rt, GetPrintRegisterFormatForSizeFP(access_size));
      LogVWrite(address2, rt2, GetPrintRegisterFormatForSizeFP(access_size));
    } else if (op == STP_q) {
      LogVWrite(address, rt, GetPrintRegisterFormatForSize(access_size));
      LogVWrite(address2, rt2, GetPrintRegisterFormatForSize(access_size));
    } else {
      LogWrite(address, rt, GetPrintRegisterFormatForSize(access_size));
      LogWrite(address2, rt2, GetPrintRegisterFormatForSize(access_size));
    }
  }

  // Handle the writeback for loads after the load to ensure safe pop
  // operation even when interrupted in the middle of it. The stack pointer
  // is only updated after the load so pop(fp) will never break the invariant
  // sp <= fp expected while walking the stack in the sampler.
  if (instr->IsLoad()) {
    // For loads the address pre writeback is used to check access below the
    // stack.
    stack = sp();

    LoadStoreWriteBack(addr_reg, offset, addrmode);
  }

  // Accesses below the stack pointer (but above the platform stack limit) are
  // not allowed in the ABI.
  CheckMemoryAccess(address, stack);
}

void Simulator::VisitLoadLiteral(Instruction* instr) {
  uintptr_t address = instr->LiteralAddress();
  unsigned rt = instr->Rt();

  {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    local_monitor_.NotifyLoad();
  }

  switch (instr->Mask(LoadLiteralMask)) {
    // Use _no_log variants to suppress the register trace (LOG_REGS,
    // LOG_VREGS), then print a more detailed log.
    case LDR_w_lit:
      set_wreg_no_log(rt, MemoryRead<uint32_t>(address));
      LogRead(address, rt, kPrintWReg);
      break;
    case LDR_x_lit:
      set_xreg_no_log(rt, MemoryRead<uint64_t>(address));
      LogRead(address, rt, kPrintXReg);
      break;
    case LDR_s_lit:
      set_sreg_no_log(rt, MemoryRead<float>(address));
      LogVRead(address, rt, kPrintSReg);
      break;
    case LDR_d_lit:
      set_dreg_no_log(rt, MemoryRead<double>(address));
      LogVRead(address, rt, kPrintDReg);
      break;
    default:
      UNREACHABLE();
  }
}

uintptr_t Simulator::LoadStoreAddress(unsigned addr_reg, int64_t offset,
                                      AddrMode addrmode) {
  const unsigned kSPRegCode = kSPRegInternalCode & kRegCodeMask;
  uint64_t address = xreg(addr_reg, Reg31IsStackPointer);
  if ((addr_reg == kSPRegCode) && ((address % 16) != 0)) {
    // When the base register is SP the stack pointer is required to be
    // quadword aligned prior to the address calculation and write-backs.
    // Misalignment will cause a stack alignment fault.
    FATAL("ALIGNMENT EXCEPTION");
  }

  if ((addrmode == Offset) || (addrmode == PreIndex)) {
    address += offset;
  }

  return address;
}

void Simulator::LoadStoreWriteBack(unsigned addr_reg, int64_t offset,
                                   AddrMode addrmode) {
  if ((addrmode == PreIndex) || (addrmode == PostIndex)) {
    DCHECK_NE(offset, 0);
    uint64_t address = xreg(addr_reg, Reg31IsStackPointer);
    set_reg(addr_reg, address + offset, Reg31IsStackPointer);
  }
}

Simulator::TransactionSize Simulator::get_transaction_size(unsigned size) {
  switch (size) {
    case 0:
      return TransactionSize::None;
    case 1:
      return TransactionSize::Byte;
    case 2:
      return TransactionSize::HalfWord;
    case 4:
      return TransactionSize::Word;
    case 8:
      return TransactionSize::DoubleWord;
    default:
      UNREACHABLE();
  }
}

void Simulator::VisitLoadStoreAcquireRelease(Instruction* instr) {
  unsigned rt = instr->Rt();
  unsigned rn = instr->Rn();
  LoadStoreAcquireReleaseOp op = static_cast<LoadStoreAcquireReleaseOp>(
      instr->Mask(LoadStoreAcquireReleaseMask));

  switch (op) {
    case CAS_w:
    case CASA_w:
    case CASL_w:
    case CASAL_w:
      CompareAndSwapHelper<uint32_t>(instr);
      return;
    case CAS_x:
    case CASA_x:
    case CASL_x:
    case CASAL_x:
      CompareAndSwapHelper<uint64_t>(instr);
      return;
    case CASB:
    case CASAB:
    case CASLB:
    case CASALB:
      CompareAndSwapHelper<uint8_t>(instr);
      return;
    case CASH:
    case CASAH:
    case CASLH:
    case CASALH:
      CompareAndSwapHelper<uint16_t>(instr);
      return;
    case CASP_w:
    case CASPA_w:
    case CASPL_w:
    case CASPAL_w:
      CompareAndSwapPairHelper<uint32_t>(instr);
      return;
    case CASP_x:
    case CASPA_x:
    case CASPL_x:
    case CASPAL_x:
      CompareAndSwapPairHelper<uint64_t>(instr);
      return;
    default:
      break;
  }

  int32_t is_acquire_release = instr->LoadStoreXAcquireRelease();
  int32_t is_exclusive = (instr->LoadStoreXNotExclusive() == 0);
  int32_t is_load = instr->LoadStoreXLoad();
  int32_t is_pair = instr->LoadStoreXPair();
  USE(is_acquire_release);
  USE(is_pair);
  DCHECK_NE(is_acquire_release, 0);  // Non-acquire/release unimplemented.
  DCHECK_EQ(is_pair, 0);             // Pair unimplemented.
  unsigned access_size = 1 << instr->LoadStoreXSizeLog2();
  uintptr_t address = LoadStoreAddress(rn, 0, AddrMode::Offset);
  DCHECK_EQ(address % access_size, 0);
  // First, check whether the memory is accessible (for wasm trap handling).
  if (!ProbeMemory(address, access_size)) return;
  base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
  if (is_load != 0) {
    if (is_exclusive) {
      local_monitor_.NotifyLoadExcl(address, get_transaction_size(access_size));
      GlobalMonitor::Get()->NotifyLoadExcl_Locked(address,
                                                  &global_monitor_processor_);
    } else {
      local_monitor_.NotifyLoad();
    }
    switch (op) {
      case LDAR_b:
      case LDAXR_b:
        set_wreg_no_log(rt, MemoryRead<uint8_t>(address));
        break;
      case LDAR_h:
      case LDAXR_h:
        set_wreg_no_log(rt, MemoryRead<uint16_t>(address));
        break;
      case LDAR_w:
      case LDAXR_w:
        set_wreg_no_log(rt, MemoryRead<uint32_t>(address));
        break;
      case LDAR_x:
      case LDAXR_x:
        set_xreg_no_log(rt, MemoryRead<uint64_t>(address));
        break;
      default:
        UNIMPLEMENTED();
    }
    LogRead(address, rt, GetPrintRegisterFormatForSize(access_size));
  } else {
    if (is_exclusive) {
      unsigned rs = instr->Rs();
      DCHECK_NE(rs, rt);
      DCHECK_NE(rs, rn);
      if (local_monitor_.NotifyStoreExcl(address,
                                         get_transaction_size(access_size)) &&
          GlobalMonitor::Get()->NotifyStoreExcl_Locked(
              address, &global_monitor_processor_)) {
        switch (op) {
          case STLXR_b:
            MemoryWrite<uint8_t>(address, wreg(rt));
            break;
          case STLXR_h:
            MemoryWrite<uint16_t>(address, wreg(rt));
            break;
          case STLXR_w:
            MemoryWrite<uint32_t>(address, wreg(rt));
            break;
          case STLXR_x:
            MemoryWrite<uint64_t>(address, xreg(rt));
            break;
          default:
            UNIMPLEMENTED();
        }
        LogWrite(address, rt, GetPrintRegisterFormatForSize(access_size));
        set_wreg(rs, 0);
      } else {
        set_wreg(rs, 1);
      }
    } else {
      local_monitor_.NotifyStore();
      GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_processor_);
      switch (op) {
        case STLR_b:
          MemoryWrite<uint8_t>(address, wreg(rt));
          break;
        case STLR_h:
          MemoryWrite<uint16_t>(address, wreg(rt));
          break;
        case STLR_w:
          MemoryWrite<uint32_t>(address, wreg(rt));
          break;
        case STLR_x:
          MemoryWrite<uint64_t>(address, xreg(rt));
          break;
        default:
          UNIMPLEMENTED();
      }
    }
  }
}

template <typename T>
void Simulator::CompareAndSwapHelper(const Instruction* instr) {
  unsigned rs = instr->Rs();
  unsigned rt = instr->Rt();
  unsigned rn = instr->Rn();

  unsigned element_size = sizeof(T);
  uint64_t address = reg<uint64_t>(rn, Reg31IsStackPointer);

  // First, check whether the memory is accessible (for wasm trap handling).
  if (!ProbeMemory(address, element_size)) return;

  bool is_acquire = instr->Bit(22) == 1;
  bool is_release = instr->Bit(15) == 1;

  T comparevalue = reg<T>(rs);
  T newvalue = reg<T>(rt);

  // The architecture permits that the data read clears any exclusive monitors
  // associated with that location, even if the compare subsequently fails.
  local_monitor_.NotifyLoad();

  T data = MemoryRead<T>(address);
  if (is_acquire) {
    // Approximate load-acquire by issuing a full barrier after the load.
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

  if (data == comparevalue) {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);

    if (is_release) {
      local_monitor_.NotifyStore();
      GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_processor_);
      // Approximate store-release by issuing a full barrier before the store.
      std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    MemoryWrite<T>(address, newvalue);
    LogWrite(address, rt, GetPrintRegisterFormatForSize(element_size));
  }

  set_reg<T>(rs, data);
  LogRead(address, rs, GetPrintRegisterFormatForSize(element_size));
}

template <typename T>
void Simulator::CompareAndSwapPairHelper(const Instruction* instr) {
  DCHECK((sizeof(T) == 4) || (sizeof(T) == 8));
  unsigned rs = instr->Rs();
  unsigned rt = instr->Rt();
  unsigned rn = instr->Rn();

  DCHECK((rs % 2 == 0) && (rt % 2 == 0));

  unsigned element_size = sizeof(T);
  uint64_t address = reg<uint64_t>(rn, Reg31IsStackPointer);

  uint64_t address2 = address + element_size;

  // First, check whether the memory is accessible (for wasm trap handling).
  if (!ProbeMemory(address, element_size)) return;
  if (!ProbeMemory(address2, element_size)) return;

  bool is_acquire = instr->Bit(22) == 1;
  bool is_release = instr->Bit(15) == 1;

  T comparevalue_high = reg<T>(rs + 1);
  T comparevalue_low = reg<T>(rs);
  T newvalue_high = reg<T>(rt + 1);
  T newvalue_low = reg<T>(rt);

  // The architecture permits that the data read clears any exclusive monitors
  // associated with that location, even if the compare subsequently fails.
  local_monitor_.NotifyLoad();

  T data_low = MemoryRead<T>(address);
  T data_high = MemoryRead<T>(address2);

  if (is_acquire) {
    // Approximate load-acquire by issuing a full barrier after the load.
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

  bool same =
      (data_high == comparevalue_high) && (data_low == comparevalue_low);
  if (same) {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);

    if (is_release) {
      local_monitor_.NotifyStore();
      GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_processor_);
      // Approximate store-release by issuing a full barrier before the store.
      std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    MemoryWrite<T>(address, newvalue_low);
    MemoryWrite<T>(address2, newvalue_high);
  }

  set_reg<T>(rs + 1, data_high);
  set_reg<T>(rs, data_low);

  PrintRegisterFormat format = GetPrintRegisterFormatForSize(element_size);
  LogRead(address, rs, format);
  LogRead(address2, rs + 1, format);

  if (same) {
    LogWrite(address, rt, format);
    LogWrite(address2, rt + 1, format);
  }
}

template <typename T>
void Simulator::AtomicMemorySimpleHelper(const Instruction* instr) {
  unsigned rs = instr->Rs();
  unsigned rt = instr->Rt();
  unsigned rn = instr->Rn();

  bool is_acquire = (instr->Bit(23) == 1) && (rt != kZeroRegCode);
  bool is_release = instr->Bit(22) == 1;

  unsigned element_size = sizeof(T);
  uint64_t address = xreg(rn, Reg31IsStackPointer);
  DCHECK_EQ(address % element_size, 0);

  // First, check whether the memory is accessible (for wasm trap handling).
  if (!ProbeMemory(address, element_size)) return;

  local_monitor_.NotifyLoad();

  T value = reg<T>(rs);

  T data = MemoryRead<T>(address);

  if (is_acquire) {
    // Approximate load-acquire by issuing a full barrier after the load.
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

  T result = 0;
  switch (instr->Mask(AtomicMemorySimpleOpMask)) {
    case LDADDOp:
      result = data + value;
      break;
    case LDCLROp:
      DCHECK(!std::numeric_limits<T>::is_signed);
      result = data & ~value;
      break;
    case LDEOROp:
      DCHECK(!std::numeric_limits<T>::is_signed);
      result = data ^ value;
      break;
    case LDSETOp:
      DCHECK(!std::numeric_limits<T>::is_signed);
      result = data | value;
      break;

    // Signed/Unsigned difference is done via the templated type T.
    case LDSMAXOp:
    case LDUMAXOp:
      result = (data > value) ? data : value;
      break;
    case LDSMINOp:
    case LDUMINOp:
      result = (data > value) ? value : data;
      break;
  }

  if (is_release) {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    local_monitor_.NotifyStore();
    GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_processor_);
    // Approximate store-release by issuing a full barrier before the store.
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

  MemoryWrite<T>(address, result);
  set_reg<T>(rt, data);

  PrintRegisterFormat format = GetPrintRegisterFormatForSize(element_size);
  LogRead(address, rt, format);
  LogWrite(address, rs, format);
}

template <typename T>
void Simulator::AtomicMemorySwapHelper(const Instruction* instr) {
  unsigned rs = instr->Rs();
  unsigned rt = instr->Rt();
  unsigned rn = instr->Rn();

  bool is_acquire = (instr->Bit(23) == 1) && (rt != kZeroRegCode);
  bool is_release = instr->Bit(22) == 1;

  unsigned element_size = sizeof(T);
  uint64_t address = xreg(rn, Reg31IsStackPointer);

  // First, check whether the memory is accessible (for wasm trap handling).
  if (!ProbeMemory(address, element_size)) return;

  local_monitor_.NotifyLoad();

  T data = MemoryRead<T>(address);
  if (is_acquire) {
    // Approximate load-acquire by issuing a full barrier after the load.
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

  if (is_release) {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    local_monitor_.NotifyStore();
    GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_processor_);
    // Approximate store-release by issuing a full barrier before the store.
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }
  MemoryWrite<T>(address, reg<T>(rs));

  set_reg<T>(rt, data);

  PrintRegisterFormat format = GetPrintRegisterFormatForSize(element_size);
  LogRead(address, rt, format);
  LogWrite(address, rs, format);
}

#define ATOMIC_MEMORY_SIMPLE_UINT_LIST(V) \
  V(LDADD)                                \
  V(LDCLR)                                \
  V(LDEOR)                                \
  V(LDSET)                                \
  V(LDUMAX)                               \
  V(LDUMIN)

#define ATOMIC_MEMORY_SIMPLE_INT_LIST(V) \
  V(LDSMAX)                              \
  V(LDSMIN)

void Simulator::VisitAtomicMemory(Instruction* instr) {
  switch (instr->Mask(AtomicMemoryMask)) {
// clang-format off
#define SIM_FUNC_B(A) \
    case A##B:        \
    case A##AB:       \
    case A##LB:       \
    case A##ALB:
#define SIM_FUNC_H(A) \
    case A##H:        \
    case A##AH:       \
    case A##LH:       \
    case A##ALH:
#define SIM_FUNC_w(A) \
    case A##_w:       \
    case A##A_w:      \
    case A##L_w:      \
    case A##AL_w:
#define SIM_FUNC_x(A) \
    case A##_x:       \
    case A##A_x:      \
    case A##L_x:      \
    case A##AL_x:

    ATOMIC_MEMORY_SIMPLE_UINT_LIST(SIM_FUNC_B)
      AtomicMemorySimpleHelper<uint8_t>(instr);
      break;
    ATOMIC_MEMORY_SIMPLE_INT_LIST(SIM_FUNC_B)
      AtomicMemorySimpleHelper<int8_t>(instr);
      break;
    ATOMIC_MEMORY_SIMPLE_UINT_LIST(SIM_FUNC_H)
      AtomicMemorySimpleHelper<uint16_t>(instr);
      break;
    ATOMIC_MEMORY_SIMPLE_INT_LIST(SIM_FUNC_H)
      AtomicMemorySimpleHelper<int16_t>(instr);
      break;
    ATOMIC_MEMORY_SIMPLE_UINT_LIST(SIM_FUNC_w)
      AtomicMemorySimpleHelper<uint32_t>(instr);
      break;
    ATOMIC_MEMORY_SIMPLE_INT_LIST(SIM_FUNC_w)
      AtomicMemorySimpleHelper<int32_t>(instr);
      break;
    ATOMIC_MEMORY_SIMPLE_UINT_LIST(SIM_FUNC_x)
      AtomicMemorySimpleHelper<uint64_t>(instr);
      break;
    ATOMIC_MEMORY_SIMPLE_INT_LIST(SIM_FUNC_x)
      AtomicMemorySimpleHelper<int64_t>(instr);
      break;
      // clang-format on

    case SWPB:
    case SWPAB:
    case SWPLB:
    case SWPALB:
      AtomicMemorySwapHelper<uint8_t>(instr);
      break;
    case SWPH:
    case SWPAH:
    case SWPLH:
    case SWPALH:
      AtomicMemorySwapHelper<uint16_t>(instr);
      break;
    case SWP_w:
    case SWPA_w:
    case SWPL_w:
    case SWPAL_w:
      AtomicMemorySwapHelper<uint32_t>(instr);
      break;
    case SWP_x:
    case SWPA_x:
    case SWPL_x:
    case SWPAL_x:
      AtomicMemorySwapHelper<uint64_t>(instr);
      break;
  }
}

void Simulator::CheckMemoryAccess(uintptr_t address, uintptr_t stack) {
  if ((address >= stack_limit_) && (address < stack)) {
    fprintf(stream_, "ACCESS BELOW STACK POINTER:\n");
    fprintf(stream_, "  sp is here:          0x%016" PRIx64 "\n",
            static_cast<uint64_t>(stack));
    fprintf(stream_, "  access was here:     0x%016" PRIx64 "\n",
            static_cast<uint64_t>(address));
    fprintf(stream_, "  stack limit is here: 0x%016" PRIx64 "\n",
            static_cast<uint64_t>(stack_limit_));
    fprintf(stream_, "\n");
    FATAL("ACCESS BELOW STACK POINTER");
  }
}

void Simulator::VisitMoveWideImmediate(Instruction* instr) {
  MoveWideImmediateOp mov_op =
      static_cast<MoveWideImmediateOp>(instr->Mask(MoveWideImmediateMask));
  int64_t new_xn_val = 0;

  bool is_64_bits = instr->SixtyFourBits() == 1;
  // Shift is limited for W operations.
  DCHECK(is_64_bits || (instr->ShiftMoveWide() < 2));

  // Get the shifted immediate.
  int64_t shift = instr->ShiftMoveWide() * 16;
  int64_t shifted_imm16 = static_cast<int64_t>(instr->ImmMoveWide()) << shift;

  // Compute the new value.
  switch (mov_op) {
    case MOVN_w:
    case MOVN_x: {
      new_xn_val = ~shifted_imm16;
      if (!is_64_bits) new_xn_val &= kWRegMask;
      break;
    }
    case MOVK_w:
    case MOVK_x: {
      unsigned reg_code = instr->Rd();
      int64_t prev_xn_val = is_64_bits ? xreg(reg_code) : wreg(reg_code);
      new_xn_val = (prev_xn_val & ~(INT64_C(0xFFFF) << shift)) | shifted_imm16;
      break;
    }
    case MOVZ_w:
    case MOVZ_x: {
      new_xn_val = shifted_imm16;
      break;
    }
    default:
      UNREACHABLE();
  }

  // Update the destination register.
  set_xreg(instr->Rd(), new_xn_val);
}

void Simulator::VisitConditionalSelect(Instruction* instr) {
  uint64_t new_val = xreg(instr->Rn());
  if (ConditionFailed(static_cast<Condition>(instr->Condition()))) {
    new_val = xreg(instr->Rm());
    switch (instr->Mask(ConditionalSelectMask)) {
      case CSEL_w:
      case CSEL_x:
        break;
      case CSINC_w:
      case CSINC_x:
        new_val++;
        break;
      case CSINV_w:
      case CSINV_x:
        new_val = ~new_val;
        break;
      case CSNEG_w:
      case CSNEG_x:
        // Simulate two's complement (instead of casting to signed and negating)
        // to avoid undefined behavior on signed overflow.
        new_val = (~new_val) + 1;
        break;
      default:
        UNIMPLEMENTED();
    }
  }
  if (instr->SixtyFourBits()) {
    set_xreg(instr->Rd(), new_val);
  } else {
    set_wreg(instr->Rd(), static_cast<uint32_t>(new_val));
  }
}

void Simulator::VisitDataProcessing1Source(Instruction* instr) {
  unsigned dst = instr->Rd();
  unsigned src = instr->Rn();

  switch (instr->Mask(DataProcessing1SourceMask)) {
    case RBIT_w:
      set_wreg(dst, base::bits::ReverseBits(wreg(src)));
      break;
    case RBIT_x:
      set_xreg(dst, base::bits::ReverseBits(xreg(src)));
      break;
    case REV16_w:
      set_wreg(dst, ReverseBytes(wreg(src), 1));
      break;
    case REV16_x:
      set_xreg(dst, ReverseBytes(xreg(src), 1));
      break;
    case REV_w:
      set_wreg(dst, ReverseBytes(wreg(src), 2));
      break;
    case REV32_x:
      set_xreg(dst, ReverseBytes(xreg(src), 2));
      break;
    case REV_x:
      set_xreg(dst, ReverseBytes(xreg(src), 3));
      break;
    case CLZ_w:
      set_wreg(dst, CountLeadingZeros(wreg(src), kWRegSizeInBits));
      break;
    case CLZ_x:
      set_xreg(dst, CountLeadingZeros(xreg(src), kXRegSizeInBits));
      break;
    case CLS_w: {
      set_wreg(dst, CountLeadingSignBits(wreg(src), kWRegSizeInBits));
      break;
    }
    case CLS_x: {
      set_xreg(dst, CountLeadingSignBits(xreg(src), kXRegSizeInBits));
      break;
    }
    default:
      UNIMPLEMENTED();
  }
}

template <typename T>
void Simulator::DataProcessing2Source(Instruction* instr) {
  Shift shift_op = NO_SHIFT;
  T result = 0;
  switch (instr->Mask(DataProcessing2SourceMask)) {
    case SDIV_w:
    case SDIV_x: {
      T rn = reg<T>(instr->Rn());
      T rm = reg<T>(instr->Rm());
      if ((rn == std::numeric_limits<T>::min()) && (rm == -1)) {
        result = std::numeric_limits<T>::min();
      } else if (rm == 0) {
        // Division by zero can be trapped, but not on A-class processors.
        result = 0;
      } else {
        result = rn / rm;
      }
      break;
    }
    case UDIV_w:
    case UDIV_x: {
      using unsignedT = typename std::make_unsigned<T>::type;
      unsignedT rn = static_cast<unsignedT>(reg<T>(instr->Rn()));
      unsignedT rm = static_cast<unsignedT>(reg<T>(instr->Rm()));
      if (rm == 0) {
        // Division by zero can be trapped, but not on A-class processors.
        result = 0;
      } else {
        result = rn / rm;
      }
      break;
    }
    case LSLV_w:
    case LSLV_x:
      shift_op = LSL;
      break;
    case LSRV_w:
    case LSRV_x:
      shift_op = LSR;
      break;
    case ASRV_w:
    case ASRV_x:
      shift_op = ASR;
      break;
    case RORV_w:
    case RORV_x:
      shift_op = ROR;
      break;
    default:
      UNIMPLEMENTED();
  }

  if (shift_op != NO_SHIFT) {
    // Shift distance encoded in the least-significant five/six bits of the
    // register.
    unsigned shift = wreg(instr->Rm());
    if (sizeof(T) == kWRegSize) {
      shift &= kShiftAmountWRegMask;
    } else {
      shift &= kShiftAmountXRegMask;
    }
    result = ShiftOperand(reg<T>(instr->Rn()), shift_op, shift);
  }
  set_reg<T>(instr->Rd(), result);
}

void Simulator::VisitDataProcessing2Source(Instruction* instr) {
  if (instr->SixtyFourBits()) {
    DataProcessing2Source<int64_t>(instr);
  } else {
    DataProcessing2Source<int32_t>(instr);
  }
}

void Simulator::VisitDataProcessing3Source(Instruction* instr) {
  int64_t result = 0;
  // Extract and sign- or zero-extend 32-bit arguments for widening operations.
  uint64_t rn_u32 = reg<uint32_t>(instr->Rn());
  uint64_t rm_u32 = reg<uint32_t>(instr->Rm());
  int64_t rn_s32 = reg<int32_t>(instr->Rn());
  int64_t rm_s32 = reg<int32_t>(instr->Rm());
  switch (instr->Mask(DataProcessing3SourceMask)) {
    case MADD_w:
    case MADD_x:
      result = base::AddWithWraparound(
          xreg(instr->Ra()),
          base::MulWithWraparound(xreg(instr->Rn()), xreg(instr->Rm())));
      break;
    case MSUB_w:
    case MSUB_x:
      result = base::SubWithWraparound(
          xreg(instr->Ra()),
          base::MulWithWraparound(xreg(instr->Rn()), xreg(instr->Rm())));
      break;
    case SMADDL_x:
      result = base::AddWithWraparound(xreg(instr->Ra()), (rn_s32 * rm_s32));
      break;
    case SMSUBL_x:
      result = base::SubWithWraparound(xreg(instr->Ra()), (rn_s32 * rm_s32));
      break;
    case UMADDL_x:
      result = static_cast<uint64_t>(xreg(instr->Ra())) + (rn_u32 * rm_u32);
      break;
    case UMSUBL_x:
      result = static_cast<uint64_t>(xreg(instr->Ra())) - (rn_u32 * rm_u32);
      break;
    case SMULH_x:
      DCHECK_EQ(instr->Ra(), kZeroRegCode);
      result =
          base::bits::SignedMulHigh64(xreg(instr->Rn()), xreg(instr->Rm()));
      break;
    case UMULH_x:
      DCHECK_EQ(instr->Ra(), kZeroRegCode);
      result =
          base::bits::UnsignedMulHigh64(xreg(instr->Rn()), xreg(instr->Rm()));
      break;
    default:
      UNIMPLEMENTED();
  }

  if (instr->SixtyFourBits()) {
    set_xreg(instr->Rd(), result);
  } else {
    set_wreg(instr->Rd(), static_cast<int32_t>(result));
  }
}

template <typename T>
void Simulator::BitfieldHelper(Instruction* instr) {
  using unsignedT = typename std::make_unsigned<T>::type;
  T reg_size = sizeof(T) * 8;
  T R = instr->ImmR();
  T S = instr->ImmS();
  T diff = S - R;
  T mask;
  if (diff >= 0) {
    mask = diff < reg_size - 1 ? (static_cast<unsignedT>(1) << (diff + 1)) - 1
                               : static_cast<T>(-1);
  } else {
    uint64_t umask = ((1ULL << (S + 1)) - 1);
    umask = (umask >> R) | (umask << (reg_size - R));
    mask = static_cast<T>(umask);
    diff += reg_size;
  }

  // inzero indicates if the extracted bitfield is inserted into the
  // destination register value or in zero.
  // If extend is true, extend the sign of the extracted bitfield.
  bool inzero = false;
  bool extend = false;
  switch (instr->Mask(BitfieldMask)) {
    case BFM_x:
    case BFM_w:
      break;
    case SBFM_x:
    case SBFM_w:
      inzero = true;
      extend = true;
      break;
    case UBFM_x:
    case UBFM_w:
      inzero = true;
      break;
    default:
      UNIMPLEMENTED();
  }

  T dst = inzero ? 0 : reg<T>(instr->Rd());
  T src = reg<T>(instr->Rn());
  // Rotate source bitfield into place.
  T result = R == 0 ? src
                    : (static_cast<unsignedT>(src) >> R) |
                          (static_cast<unsignedT>(src) << (reg_size - R));
  // Determine the sign extension.
  T topbits_preshift = (static_cast<unsignedT>(1) << (reg_size - diff - 1)) - 1;
  T signbits =
      diff >= reg_size - 1
          ? 0
          : ((extend && ((src >> S) & 1) ? topbits_preshift : 0) << (diff + 1));

  // Merge sign extension, dest/zero and bitfield.
  result = signbits | (result & mask) | (dst & ~mask);

  set_reg<T>(instr->Rd(), result);
}

void Simulator::VisitBitfield(Instruction* instr) {
  if (instr->SixtyFourBits()) {
    BitfieldHelper<int64_t>(instr);
  } else {
    BitfieldHelper<int32_t>(instr);
  }
}

void Simulator::VisitExtract(Instruction* instr) {
  if (instr->SixtyFourBits()) {
    Extract<uint64_t>(instr);
  } else {
    Extract<uint32_t>(instr);
  }
}

void Simulator::VisitFPImmediate(Instruction* instr) {
  AssertSupportedFPCR();

  unsigned dest = instr->Rd();
  switch (instr->Mask(FPImmediateMask)) {
    case FMOV_s_imm:
      set_sreg(dest, instr->ImmFP32());
      break;
    case FMOV_d_imm:
      set_dreg(dest, instr->ImmFP64());
      break;
    default:
      UNREACHABLE();
  }
}

void Simulator::VisitFPIntegerConvert(Instruction* instr) {
  AssertSupportedFPCR();

  unsigned dst = instr->Rd();
  unsigned src = instr->Rn();

  FPRounding round = fpcr().RMode();

  switch (instr->Mask(FPIntegerConvertMask)) {
    case FCVTAS_ws:
      set_wreg(dst, FPToInt32(sreg(src), FPTieAway));
      break;
    case FCVTAS_xs:
      set_xreg(dst, FPToInt64(sreg(src), FPTieAway));
      break;
    case FCVTAS_wd:
      set_wreg(dst, FPToInt32(dreg(src), FPTieAway));
      break;
    case FCVTAS_xd:
      set_xreg(dst, FPToInt64(dreg(src), FPTieAway));
      break;
    case FCVTAU_ws:
      set_wreg(dst, FPToUInt32(sreg(src), FPTieAway));
      break;
    case FCVTAU_xs:
      set_xreg(dst, FPToUInt64(sreg(src), FPTieAway));
      break;
    case FCVTAU_wd:
      set_wreg(dst, FPToUInt32(dreg(src), FPTieAway));
      break;
    case FCVTAU_xd:
      set_xreg(dst, FPToUInt64(dreg(src), FPTieAway));
      break;
    case FCVTMS_ws:
      set_wreg(dst, FPToInt32(sreg(src), FPNegativeInfinity));
      break;
    case FCVTMS_xs:
      set_xreg(dst, FPToInt64(sreg(src), FPNegativeInfinity));
      break;
    case FCVTMS_wd:
      set_wreg(dst, FPToInt32(dreg(src), FPNegativeInfinity));
      break;
    case FCVTMS_xd:
      set_xreg(dst, FPToInt64(dreg(src), FPNegativeInfinity));
      break;
    case FCVTMU_ws:
      set_wreg(dst, FPToUInt32(sreg(src), FPNegativeInfinity));
      break;
    case FCVTMU_xs:
      set_xreg(dst, FPToUInt64(sreg(src), FPNegativeInfinity));
      break;
    case FCVTMU_wd:
      set_wreg(dst, FPToUInt32(dreg(src), FPNegativeInfinity));
      break;
    case FCVTMU_xd:
      set_xreg(dst, FPToUInt64(dreg(src), FPNegativeInfinity));
      break;
    case FCVTNS_ws:
      set_wreg(dst, FPToInt32(sreg(src), FPTieEven));
      break;
    case FCVTNS_xs:
      set_xreg(dst, FPToInt64(sreg(src), FPTieEven));
      break;
    case FCVTNS_wd:
      set_wreg(dst, FPToInt32(dreg(src), FPTieEven));
      break;
    case FCVTNS_xd:
      set_xreg(dst, FPToInt64(dreg(src), FPTieEven));
      break;
    case FCVTNU_ws:
      set_wreg(dst, FPToUInt32(sreg(src), FPTieEven));
      break;
    case FCVTNU_xs:
      set_xreg(dst, FPToUInt64(sreg(src), FPTieEven));
      break;
    case FCVTNU_wd:
      set_wreg(dst, FPToUInt32(dreg(src), FPTieEven));
      break;
    case FCVTNU_xd:
      set_xreg(dst, FPToUInt64(dreg(src), FPTieEven));
      break;
    case FCVTZS_ws:
      set_wreg(dst, FPToInt32(sreg(src), FPZero));
      break;
    case FCVTZS_xs:
      set_xreg(dst, FPToInt64(sreg(src), FPZero));
      break;
    case FCVTZS_wd:
      set_wreg(dst, FPToInt32(dreg(src), FPZero));
      break;
    case FCVTZS_xd:
      set_xreg(dst, FPToInt64(dreg(src), FPZero));
      break;
    case FCVTZU_ws:
      set_wreg(dst, FPToUInt32(sreg(src), FPZero));
      break;
    case FCVTZU_xs:
      set_xreg(dst, FPToUInt64(sreg(src), FPZero));
      break;
    case FCVTZU_wd:
      set_wreg(dst, FPToUInt32(dreg(src), FPZero));
      break;
    case FCVTZU_xd:
      set_xreg(dst, FPToUInt64(dreg(src), FPZero));
      break;
    case FJCVTZS:
      set_wreg(dst, FPToFixedJS(dreg(src)));
      break;
    case FMOV_ws:
      set_wreg(dst, sreg_bits(src));
      break;
    case FMOV_xd:
      set_xreg(dst, dreg_bits(src));
      break;
    case FMOV_sw:
      set_sreg_bits(dst, wreg(src));
      break;
    case FMOV_dx:
      set_dreg_bits(dst, xreg(src));
      break;

    // A 32-bit input can be handled in the same way as a 64-bit input, since
    // the sign- or zero-extension will not affect the conversion.
    case SCVTF_dx:
      set_dreg(dst, FixedToDouble(xreg(src), 0, round));
      break;
    case SCVTF_dw:
      set_dreg(dst, FixedToDouble(wreg(src), 0, round));
      break;
    case UCVTF_dx:
      set_dreg(dst, UFixedToDouble(xreg(src), 0, round));
      break;
    case UCVTF_dw: {
      set_dreg(dst, UFixedToDouble(reg<uint32_t>(src), 0, round));
      break;
    }
    case SCVTF_sx:
      set_sreg(dst, FixedToFloat(xreg(src), 0, round));
      break;
    case SCVTF_sw:
      set_sreg(dst, FixedToFloat(wreg(src), 0, round));
      break;
    case UCVTF_sx:
      set_sreg(dst, UFixedToFloat(xreg(src), 0, round));
      break;
    case UCVTF_sw: {
      set_sreg(dst, UFixedToFloat(reg<uint32_t>(src), 0, round));
      break;
    }

    default:
      UNREACHABLE();
  }
}

void Simulator::VisitFPFixedPointConvert(Instruction* instr) {
  AssertSupportedFPCR();

  unsigned dst = instr->Rd();
  unsigned src = instr->Rn();
  int fbits = 64 - instr->FPScale();

  FPRounding round = fpcr().RMode();

  switch (instr->Mask(FPFixedPointConvertMask)) {
    // A 32-bit input can be handled in the same way as a 64-bit input, since
    // the sign- or zero-extension will not affect the conversion.
    case SCVTF_dx_fixed:
      set_dreg(dst, FixedToDouble(xreg(src), fbits, round));
      break;
    case SCVTF_dw_fixed:
      set_dreg(dst, FixedToDouble(wreg(src), fbits, round));
      break;
    case UCVTF_dx_fixed:
      set_dreg(dst, UFixedToDouble(xreg(src), fbits, round));
      break;
    case UCVTF_dw_fixed: {
      set_dreg(dst, UFixedToDouble(reg<uint32_t>(src), fbits, round));
      break;
    }
    case SCVTF_sx_fixed:
      set_sreg(dst, FixedToFloat(xreg(src), fbits, round));
      break;
    case SCVTF_sw_fixed:
      set_sreg(dst, FixedToFloat(wreg(src), fbits, round));
      break;
    case UCVTF_sx_fixed:
      set_sreg(dst, UFixedToFloat(xreg(src), fbits, round));
      break;
    case UCVTF_sw_fixed: {
      set_sreg(dst, UFixedToFloat(reg<uint32_t>(src), fbits, round));
      break;
    }
    default:
      UNREACHABLE();
  }
}

void Simulator::VisitFPCompare(Instruction* instr) {
  AssertSupportedFPCR();

  switch (instr->Mask(FPCompareMask)) {
    case FCMP_s:
      FPCompare(sreg(instr->Rn()), sreg(instr->Rm()));
      break;
    case FCMP_d:
      FPCompare(dreg(instr->Rn()), dreg(instr->Rm()));
      break;
    case FCMP_s_zero:
      FPCompare(sreg(instr->Rn()), 0.0f);
      break;
    case FCMP_d_zero:
      FPCompare(dreg(instr->Rn()), 0.0);
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitFPConditionalCompare(Instruction* instr) {
  AssertSupportedFPCR();

  switch (instr->Mask(FPConditionalCompareMask)) {
    case FCCMP_s:
      if (ConditionPassed(static_cast<Condition>(instr->Condition()))) {
        FPCompare(sreg(instr->Rn()), sreg(instr->Rm()));
      } else {
        nzcv().SetFlags(instr->Nzcv());
        LogSystemRegister(NZCV);
      }
      break;
    case FCCMP_d: {
      if (ConditionPassed(static_cast<Condition>(instr->Condition()))) {
        FPCompare(dreg(instr->Rn()), dreg(instr->Rm()));
      } else {
        // If the condition fails, set the status flags to the nzcv immediate.
        nzcv().SetFlags(instr->Nzcv());
        LogSystemRegister(NZCV);
      }
      break;
    }
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitFPConditionalSelect(Instruction* instr) {
  AssertSupportedFPCR();

  Instr selected;
  if (ConditionPassed(static_cast<Condition>(instr->Condition()))) {
    selected = instr->Rn();
  } else {
    selected = instr->Rm();
  }

  switch (instr->Mask(FPConditionalSelectMask)) {
    case FCSEL_s:
      set_sreg(instr->Rd(), sreg(selected));
      break;
    case FCSEL_d:
      set_dreg(instr->Rd(), dreg(selected));
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitFPDataProcessing1Source(Instruction* instr) {
  AssertSupportedFPCR();

  FPRounding fpcr_rounding = static_cast<FPRounding>(fpcr().RMode());
  VectorFormat vform = (instr->Mask(FP64) == FP64) ? kFormatD : kFormatS;
  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  bool inexact_exception = false;

  unsigned fd = instr->Rd();
  unsigned fn = instr->Rn();

  switch (instr->Mask(FPDataProcessing1SourceMask)) {
    case FMOV_s:
      set_sreg(fd, sreg(fn));
      return;
    case FMOV_d:
      set_dreg(fd, dreg(fn));
      return;
    case FABS_s:
    case FABS_d:
      fabs_(vform, vreg(fd), vreg(fn));
      // Explicitly log the register update whilst we have type information.
      LogVRegister(fd, GetPrintRegisterFormatFP(vform));
      return;
    case FNEG_s:
    case FNEG_d:
      fneg(vform, vreg(fd), vreg(fn));
      // Explicitly log the register update whilst we have type information.
      LogVRegister(fd, GetPrintRegisterFormatFP(vform));
      return;
    case FCVT_ds:
      set_dreg(fd, FPToDouble(sreg(fn)));
      return;
    case FCVT_sd:
      set_sreg(fd, FPToFloat(dreg(fn), FPTieEven));
      return;
    case FCVT_hs:
      set_hreg(fd, FPToFloat16(sreg(fn), FPTieEven));
      return;
    case FCVT_sh:
      set_sreg(fd, FPToFloat(hreg(fn)));
      return;
    case FCVT_dh:
      set_dreg(fd, FPToDouble(FPToFloat(hreg(fn))));
      return;
    case FCVT_hd:
      set_hreg(fd, FPToFloat16(dreg(fn), FPTieEven));
      return;
    case FSQRT_s:
    case FSQRT_d:
      fsqrt(vform, rd, rn);
      // Explicitly log the register update whilst we have type information.
      LogVRegister(fd, GetPrintRegisterFormatFP(vform));
      return;
    case FRINTI_s:
    case FRINTI_d:
      break;  // Use FPCR rounding mode.
    case FRINTX_s:
    case FRINTX_d:
      inexact_exception = true;
      break;
    case FRINTA_s:
    case FRINTA_d:
      fpcr_rounding = FPTieAway;
      break;
    case FRINTM_s:
    case FRINTM_d:
      fpcr_rounding = FPNegativeInfinity;
      break;
    case FRINTN_s:
    case FRINTN_d:
      fpcr_rounding = FPTieEven;
      break;
    case FRINTP_s:
    case FRINTP_d:
      fpcr_rounding = FPPositiveInfinity;
      break;
    case FRINTZ_s:
    case FRINTZ_d:
      fpcr_rounding = FPZero;
      break;
    default:
      UNIMPLEMENTED();
  }

  // Only FRINT* instructions fall through the switch above.
  frint(vform, rd, rn, fpcr_rounding, inexact_exception);
  // Explicitly log the register update whilst we have type information
  LogVRegister(fd, GetPrintRegisterFormatFP(vform));
}

void Simulator::VisitFPDataProcessing2Source(Instruction* instr) {
  AssertSupportedFPCR();

  VectorFormat vform = (instr->Mask(FP64) == FP64) ? kFormatD : kFormatS;
  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  SimVRegister& rm = vreg(instr->Rm());

  switch (instr->Mask(FPDataProcessing2SourceMask)) {
    case FADD_s:
    case FADD_d:
      fadd(vform, rd, rn, rm);
      break;
    case FSUB_s:
    case FSUB_d:
      fsub(vform, rd, rn, rm);
      break;
    case FMUL_s:
    case FMUL_d:
      fmul(vform, rd, rn, rm);
      break;
    case FNMUL_s:
    case FNMUL_d:
      fnmul(vform, rd, rn, rm);
      break;
    case FDIV_s:
    case FDIV_d:
      fdiv(vform, rd, rn, rm);
      break;
    case FMAX_s:
    case FMAX_d:
      fmax(vform, rd, rn, rm);
      break;
    case FMIN_s:
    case FMIN_d:
      fmin(vform, rd, rn, rm);
      break;
    case FMAXNM_s:
    case FMAXNM_d:
      fmaxnm(vform, rd, rn, rm);
      break;
    case FMINNM_s:
    case FMINNM_d:
      fminnm(vform, rd, rn, rm);
      break;
    default:
      UNREACHABLE();
  }
  // Explicitly log the register update whilst we have type information.
  LogVRegister(instr->Rd(), GetPrintRegisterFormatFP(vform));
}

void Simulator::VisitFPDataProcessing3Source(Instruction* instr) {
  AssertSupportedFPCR();

  unsigned fd = instr->Rd();
  unsigned fn = instr->Rn();
  unsigned fm = instr->Rm();
  unsigned fa = instr->Ra();

  switch (instr->Mask(FPDataProcessing3SourceMask)) {
    // fd = fa +/- (fn * fm)
    case FMADD_s:
      set_sreg(fd, FPMulAdd(sreg(fa), sreg(fn), sreg(fm)));
      break;
    case FMSUB_s:
      set_sreg(fd, FPMulAdd(sreg(fa), -sreg(fn), sreg(fm)));
      break;
    case FMADD_d:
      set_dreg(fd, FPMulAdd(dreg(fa), dreg(fn), dreg(fm)));
      break;
    case FMSUB_d:
      set_dreg(fd, FPMulAdd(dreg(fa), -dreg(fn), dreg(fm)));
      break;
    // Negated variants of the above.
    case FNMADD_s:
      set_sreg(fd, FPMulAdd(-sreg(fa), -sreg(fn), sreg(fm)));
      break;
    case FNMSUB_s:
      set_sreg(fd, FPMulAdd(-sreg(fa), sreg(fn), sreg(fm)));
      break;
    case FNMADD_d:
      set_dreg(fd, FPMulAdd(-dreg(fa), -dreg(fn), dreg(fm)));
      break;
    case FNMSUB_d:
      set_dreg(fd, FPMulAdd(-dreg(fa), dreg(fn), dreg(fm)));
      break;
    default:
      UNIMPLEMENTED();
  }
}

bool Simulator::FPProcessNaNs(Instruction* instr) {
  unsigned fd = instr->Rd();
  unsigned fn = instr->Rn();
  unsigned fm = instr->Rm();
  bool done = false;

  if (instr->Mask(FP64) == FP64) {
    double result = FPProcessNaNs(dreg(fn), dreg(fm));
    if (std::isnan(result)) {
      set_dreg(fd, result);
      done = true;
    }
  } else {
    float result = FPProcessNaNs(sreg(fn), sreg(fm));
    if (std::isnan(result)) {
      set_sreg(fd, result);
      done = true;
    }
  }

  return done;
}

// clang-format off
#define PAUTH_SYSTEM_MODES(V)                            \
  V(B1716, 17, xreg(16),                      kPACKeyIB) \
  V(BSP,   30, xreg(31, Reg31IsStackPointer), kPACKeyIB)
// clang-format on

void Simulator::VisitSystem(Instruction* instr) {
  // Some system instructions hijack their Op and Cp fields to represent a
  // range of immediates instead of indicating a different instruction. This
  // makes the decoding tricky.
  if (instr->Mask(SystemPAuthFMask) == SystemPAuthFixed) {
    // The BType check for PACIBSP happens in CheckBType().
    switch (instr->Mask(SystemPAuthMask)) {
#define DEFINE_PAUTH_FUNCS(SUFFIX, DST, MOD, KEY)                     \
  case PACI##SUFFIX:                                                  \
    set_xreg(DST, AddPAC(xreg(DST), MOD, KEY, kInstructionPointer));  \
    break;                                                            \
  case AUTI##SUFFIX:                                                  \
    set_xreg(DST, AuthPAC(xreg(DST), MOD, KEY, kInstructionPointer)); \
    break;

      PAUTH_SYSTEM_MODES(DEFINE_PAUTH_FUNCS)
#undef DEFINE_PAUTH_FUNCS
#undef PAUTH_SYSTEM_MODES
    }
  } else if (instr->Mask(SystemSysRegFMask) == SystemSysRegFixed) {
    switch (instr->Mask(SystemSysRegMask)) {
      case MRS: {
        switch (instr->ImmSystemRegister()) {
          case NZCV:
            set_xreg(instr->Rt(), nzcv().RawValue());
            break;
          case FPCR:
            set_xreg(instr->Rt(), fpcr().RawValue());
            break;
          default:
            UNIMPLEMENTED();
        }
        break;
      }
      case MSR: {
        switch (instr->ImmSystemRegister()) {
          case NZCV:
            nzcv().SetRawValue(wreg(instr->Rt()));
            LogSystemRegister(NZCV);
            break;
          case FPCR:
            fpcr().SetRawValue(wreg(instr->Rt()));
            LogSystemRegister(FPCR);
            break;
          default:
            UNIMPLEMENTED();
        }
        break;
      }
    }
  } else if (instr->Mask(SystemHintFMask) == SystemHintFixed) {
    DCHECK(instr->Mask(SystemHintMask) == HINT);
    switch (instr->ImmHint()) {
      case NOP:
      case YIELD:
      case CSDB:
      case BTI_jc:
      case BTI:
      case BTI_c:
      case BTI_j:
        // The BType checks happen in CheckBType().
        break;
      default:
        UNIMPLEMENTED();
    }
  } else if (instr->Mask(MemBarrierFMask) == MemBarrierFixed) {
    std::atomic_thread_fence(std::memory_order_seq_cst);
  } else {
    UNIMPLEMENTED();
  }
}

bool Simulator::GetValue(const char* desc, int64_t* value) {
  int regnum = CodeFromName(desc);
  if (regnum >= 0) {
    unsigned code = regnum;
    if (code == kZeroRegCode) {
      // Catch the zero register and return 0.
      *value = 0;
      return true;
    } else if (code == kSPRegInternalCode) {
      // Translate the stack pointer code to 31, for Reg31IsStackPointer.
      code = 31;
    }
    if (desc[0] == 'w') {
      *value = wreg(code, Reg31IsStackPointer);
    } else {
      *value = xreg(code, Reg31IsStackPointer);
    }
    return true;
  } else if (strncmp(desc, "0x", 2) == 0) {
    return SScanF(desc + 2, "%" SCNx64, reinterpret_cast<uint64_t*>(value)) ==
           1;
  } else {
    return SScanF(desc, "%" SCNu64, reinterpret_cast<uint64_t*>(value)) == 1;
  }
}

bool Simulator::PrintValue(const char* desc) {
  if (strcmp(desc, "sp") == 0) {
    DCHECK(CodeFromName(desc) == static_cast<int>(kSPRegInternalCode));
    PrintF(stream_, "%s sp:%s 0x%016" PRIx64 "%s\n", clr_reg_name,
           clr_reg_value, xreg(31, Reg31IsStackPointer), clr_normal);
    return true;
  } else if (strcmp(desc, "wsp") == 0) {
    DCHECK(CodeFromName(desc) == static_cast<int>(kSPRegInternalCode));
    PrintF(stream_, "%s wsp:%s 0x%08" PRIx32 "%s\n", clr_reg_name,
           clr_reg_value, wreg(31, Reg31IsStackPointer), clr_normal);
    return true;
  }

  int i = CodeFromName(desc);
  static_assert(kNumberOfRegisters == kNumberOfVRegisters,
                "Must be same number of Registers as VRegisters.");
  if (i < 0 || static_cast<unsigned>(i) >= kNumberOfVRegisters) return false;

  if (desc[0] == 'v') {
    PrintF(stream_, "%s %s:%s 0x%016" PRIx64 "%s (%s%s:%s %g%s %s:%s %g%s)\n",
           clr_vreg_name, VRegNameForCode(i), clr_vreg_value,
           base::bit_cast<uint64_t>(dreg(i)), clr_normal, clr_vreg_name,
           DRegNameForCode(i), clr_vreg_value, dreg(i), clr_vreg_name,
           SRegNameForCode(i), clr_vreg_value, sreg(i), clr_normal);
    return true;
  } else if (desc[0] == 'd') {
    PrintF(stream_, "%s %s:%s %g%s\n", clr_vreg_name, DRegNameForCode(i),
           clr_vreg_value, dreg(i), clr_normal);
    return true;
  } else if (desc[0] == 's') {
    PrintF(stream_, "%s %s:%s %g%s\n", clr_vreg_name, SRegNameForCode(i),
           clr_vreg_value, sreg(i), clr_normal);
    return true;
  } else if (desc[0] == 'w') {
    PrintF(stream_, "%s %s:%s 0x%08" PRIx32 "%s\n", clr_reg_name,
           WRegNameForCode(i), clr_reg_value, wreg(i), clr_normal);
    return true;
  } else {
    // X register names have a wide variety of starting characters, but anything
    // else will be an X register.
    PrintF(stream_, "%s %s:%s 0x%016" PRIx64 "%s\n", clr_reg_name,
           XRegNameForCode(i), clr_reg_value, xreg(i), clr_normal);
    return true;
  }
}

void Simulator::Debug() {
  if (v8_flags.correctness_fuzzer_suppressions) {
    PrintF("Debugger disabled for differential fuzzing.\n");
    return;
  }
  bool done = false;
  while (!done) {
    // Disassemble the next instruction to execute before doing anything else.
    PrintInstructionsAt(pc_, 1);
    // Read the command line.
    ArrayUniquePtr<char> line(ReadLine("sim> "));
    done = ExecDebugCommand(std::move(line));
  }
}

bool Simulator::ExecDebugCommand(ArrayUniquePtr<char> line_ptr) {
#define COMMAND_SIZE 63
#define ARG_SIZE 255

#define STR(a) #a
#define XSTR(a) STR(a)

  char cmd[COMMAND_SIZE + 1];
  char arg1[ARG_SIZE + 1];
  char arg2[ARG_SIZE + 1];
  char* argv[3] = {cmd, arg1, arg2};

  // Make sure to have a proper terminating character if reaching the limit.
  cmd[COMMAND_SIZE] = 0;
  arg1[ARG_SIZE] = 0;
  arg2[ARG_SIZE] = 0;

  bool cleared_log_disasm_bit = false;

  if (line_ptr == nullptr) return false;

  // Repeat last command by default.
  const char* line = line_ptr.get();
  const char* last_input = last_debugger_input();
  if (strcmp(line, "\n") == 0 && (last_input != nullptr)) {
    line_ptr.reset();
    line = last_input;
  } else {
    // Update the latest command ran
    set_last_debugger_input(std::move(line_ptr));
  }

  // Use sscanf to parse the individual parts of the command line. At the
  // moment no command expects more than two parameters.
  int argc = SScanF(line,
                      "%" XSTR(COMMAND_SIZE) "s "
                      "%" XSTR(ARG_SIZE) "s "
                      "%" XSTR(ARG_SIZE) "s",
                      cmd, arg1, arg2);

  // stepi / si ------------------------------------------------------------
  if ((strcmp(cmd, "si") == 0) || (strcmp(cmd, "stepi") == 0)) {
    // We are about to execute instructions, after which by default we
    // should increment the pc_. If it was set when reaching this debug
    // instruction, it has not been cleared because this instruction has not
    // completed yet. So clear it manually.
    pc_modified_ = false;

    if (argc == 1) {
      ExecuteInstruction();
    } else {
      int64_t number_of_instructions_to_execute = 1;
      GetValue(arg1, &number_of_instructions_to_execute);

      set_log_parameters(log_parameters() | LOG_DISASM);
      while (number_of_instructions_to_execute-- > 0) {
        ExecuteInstruction();
      }
      set_log_parameters(log_parameters() & ~LOG_DISASM);
      PrintF("\n");
    }

    // If it was necessary, the pc has already been updated or incremented
    // when executing the instruction. So we do not want it to be updated
    // again. It will be cleared when exiting.
    pc_modified_ = true;

    // next / n
    // --------------------------------------------------------------
  } else if ((strcmp(cmd, "next") == 0) || (strcmp(cmd, "n") == 0)) {
    // Tell the simulator to break after the next executed BL.
    break_on_next_ = true;
    // Continue.
    return true;

    // continue / cont / c
    // ---------------------------------------------------
  } else if ((strcmp(cmd, "continue") == 0) || (strcmp(cmd, "cont") == 0) ||
             (strcmp(cmd, "c") == 0)) {
    // Leave the debugger shell.
    return true;

    // disassemble / disasm / di
    // ---------------------------------------------
  } else if (strcmp(cmd, "disassemble") == 0 || strcmp(cmd, "disasm") == 0 ||
             strcmp(cmd, "di") == 0) {
    int64_t n_of_instrs_to_disasm = 10;                // default value.
    int64_t address = reinterpret_cast<int64_t>(pc_);  // default value.
    if (argc >= 2) {                                   // disasm <n of instrs>
      GetValue(arg1, &n_of_instrs_to_disasm);
    }
    if (argc >= 3) {  // disasm <n of instrs> <address>
      GetValue(arg2, &address);
    }

    // Disassemble.
    PrintInstructionsAt(reinterpret_cast<Instruction*>(address),
                        n_of_instrs_to_disasm);
    PrintF("\n");

    // print / p
    // -------------------------------------------------------------
  } else if ((strcmp(cmd, "print") == 0) || (strcmp(cmd, "p") == 0)) {
    if (argc == 2) {
      if (strcmp(arg1, "all") == 0) {
        PrintRegisters();
        PrintVRegisters();
      } else {
        if (!PrintValue(arg1)) {
          PrintF("%s unrecognized\n", arg1);
        }
      }
    } else {
      PrintF(
          "print <register>\n"
          "    Print the content of a register. (alias 'p')\n"
          "    'print all' will print all registers.\n"
          "    Use 'printobject' to get more details about the value.\n");
    }

    // printobject / po
    // ------------------------------------------------------
  } else if ((strcmp(cmd, "printobject") == 0) || (strcmp(cmd, "po") == 0)) {
    if (argc == 2) {
      int64_t value;
      StdoutStream os;
      if (GetValue(arg1, &value)) {
        Tagged<Object> obj(value);
        os << arg1 << ": \n";
#ifdef DEBUG
        Print(obj, os);
        os << "\n";
#else
        os << Brief(obj) << "\n";
#endif
      } else {
        os << arg1 << " unrecognized\n";
      }
    } else {
      PrintF(
          "printobject <value>\n"
          "printobject <register>\n"
          "    Print details about the value. (alias 'po')\n");
    }

    // stack / mem
    // ----------------------------------------------------------
  } else if (strcmp(cmd, "stack") == 0 || strcmp(cmd, "mem") == 0 ||
             strcmp(cmd, "dump") == 0) {
    int64_t* cur = nullptr;
    int64_t* end = nullptr;
    int next_arg = 1;

    if (strcmp(cmd, "stack") == 0) {
      cur = reinterpret_cast<int64_t*>(sp());

    } else {  // "mem"
      int64_t value;
      if (!GetValue(arg1, &value)) {
        PrintF("%s unrecognized\n", arg1);
        return false;
      }
      cur = reinterpret_cast<int64_t*>(value);
      next_arg++;
    }

    int64_t words = 0;
    if (argc == next_arg) {
      words = 10;
    } else if (argc == next_arg + 1) {
      if (!GetValue(argv[next_arg], &words)) {
        PrintF("%s unrecognized\n", argv[next_arg]);
        PrintF("Printing 10 double words by default");
        words = 10;
      }
    } else {
      UNREACHABLE();
    }
    end = cur + words;

    bool skip_obj_print = (strcmp(cmd, "dump") == 0);
    while (cur < end) {
      PrintF("  0x%016" PRIx64 ":  0x%016" PRIx64 " %10" PRId64,
             reinterpret_cast<uint64_t>(cur), *cur, *cur);
      if (!skip_obj_print) {
        Tagged<Object> obj(*cur);
        Heap* current_heap = isolate_->heap();
        if (IsSmi(obj) ||
            IsValidHeapObject(current_heap, Cast<HeapObject>(obj))) {
          PrintF(" (");
          if (IsSmi(obj)) {
            PrintF("smi %" PRId32, Smi::ToInt(obj));
          } else {
            ShortPrint(obj);
          }
          PrintF(")");
        }
      }
      PrintF("\n");
      cur++;
    }

    // trace / t
    // -------------------------------------------------------------
  } else if (strcmp(cmd, "trace") == 0 || strcmp(cmd, "t") == 0) {
    if ((log_parameters() & LOG_ALL) != LOG_ALL) {
      PrintF("Enabling disassembly, registers and memory write tracing\n");
      set_log_parameters(log_parameters() | LOG_ALL);
    } else {
      PrintF("Disabling disassembly, registers and memory write tracing\n");
      set_log_parameters(log_parameters() & ~LOG_ALL);
    }

    // break / b
    // -------------------------------------------------------------
  } else if (strcmp(cmd, "break") == 0 || strcmp(cmd, "b") == 0) {
    if (argc == 2) {
      int64_t value;
      if (GetValue(arg1, &value)) {
        SetBreakpoint(reinterpret_cast<Instruction*>(value));
      } else {
        PrintF("%s unrecognized\n", arg1);
      }
    } else {
      ListBreakpoints();
      PrintF("Use `break <address>` to set or disable a breakpoint\n");
    }

    // backtrace / bt
    // ---------------------------------------------------------------
  } else if (strcmp(cmd, "backtrace") == 0 || strcmp(cmd, "bt") == 0) {
    Address pc = reinterpret_cast<Address>(pc_);
    Address lr = reinterpret_cast<Address>(this->lr());
    Address sp = static_cast<Address>(this->sp());
    Address fp = static_cast<Address>(this->fp());

    int i = 0;
    while (true) {
      PrintF("#%d: " V8PRIxPTR_FMT " (sp=" V8PRIxPTR_FMT ", fp=" V8PRIxPTR_FMT
             ")\n",
             i, pc, sp, fp);
      pc = lr;
      sp = fp;
      if (pc == reinterpret_cast<Address>(kEndOfSimAddress)) {
        break;
      }
      lr = *(reinterpret_cast<Address*>(fp) + 1);
      fp = *reinterpret_cast<Address*>(fp);
      i++;
      if (i > 100) {
        PrintF("Too many frames\n");
        break;
      }
    }

    // gdb
    // -------------------------------------------------------------------
  } else if (strcmp(cmd, "gdb") == 0) {
    PrintF("Relinquishing control to gdb.\n");
    base::OS::DebugBreak();
    PrintF("Regaining control from gdb.\n");

    // sysregs
    // ---------------------------------------------------------------
  } else if (strcmp(cmd, "sysregs") == 0) {
    PrintSystemRegisters();

    // help / h
    // --------------------------------------------------------------
  } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
    PrintF(
        "stepi / si\n"
        "    stepi <n>\n"
        "    Step <n> instructions.\n"
        "next / n\n"
        "    Continue execution until a BL instruction is reached.\n"
        "    At this point a breakpoint is set just after this BL.\n"
        "    Then execution is resumed. It will probably later hit the\n"
        "    breakpoint just set.\n"
        "continue / cont / c\n"
        "    Continue execution from here.\n"
        "disassemble / disasm / di\n"
        "    disassemble <n> <address>\n"
        "    Disassemble <n> instructions from current <address>.\n"
        "    By default <n> is 20 and <address> is the current pc.\n"
        "print / p\n"
        "    print <register>\n"
        "    Print the content of a register.\n"
        "    'print all' will print all registers.\n"
        "    Use 'printobject' to get more details about the value.\n"
        "printobject / po\n"
        "    printobject <value>\n"
        "    printobject <register>\n"
        "    Print details about the value.\n"
        "stack\n"
        "    stack [<words>]\n"
        "    Dump stack content, default dump 10 words\n"
        "mem\n"
        "    mem <address> [<words>]\n"
        "    Dump memory content, default dump 10 words\n"
        "dump\n"
        "    dump <address> [<words>]\n"
        "    Dump memory content without pretty printing JS objects, "
        "default dump 10 words\n"
        "trace / t\n"
        "    Toggle disassembly and register tracing\n"
        "break / b\n"
        "    break : list all breakpoints\n"
        "    break <address> : set / enable / disable a breakpoint.\n"
        "backtrace / bt\n"
        "    Walk the frame pointers, dumping the pc/sp/fp for each frame.\n"
        "gdb\n"
        "    Enter gdb.\n"
        "sysregs\n"
        "    Print all system registers (including NZCV).\n");
  } else {
    PrintF("Unknown command: %s\n", cmd);
    PrintF("Use 'help' for more information.\n");
  }

  if (cleared_log_disasm_bit == true) {
    set_log_parameters(log_parameters_ | LOG_DISASM);
  }
  return false;
}

void Simulator::VisitException(Instruction* instr) {
  switch (instr->Mask(ExceptionMask)) {
    case HLT: {
      if (instr->ImmException() == kImmExceptionIsDebug) {
        // Read the arguments encoded inline in the instruction stream.
        uint32_t code;
        uint32_t parameters;

        memcpy(&code, pc_->InstructionAtOffset(kDebugCodeOffset), sizeof(code));
        memcpy(&parameters, pc_->InstructionAtOffset(kDebugParamsOffset),
               sizeof(parameters));
        char const* message = reinterpret_cast<char const*>(
            pc_->InstructionAtOffset(kDebugMessageOffset));

        // Always print something when we hit a debug point that breaks.
        // We are going to break, so printing something is not an issue in
        // terms of speed.
        if (v8_flags.trace_sim_messages || v8_flags.trace_sim ||
            (parameters & BREAK)) {
          if (message != nullptr) {
            PrintF(stream_, "# %sDebugger hit %d: %s%s%s\n", clr_debug_number,
                   code, clr_debug_message, message, clr_normal);
          } else {
            PrintF(stream_, "# %sDebugger hit %d.%s\n", clr_debug_number, code,
                   clr_normal);
          }
          Builtin maybe_builtin = OffHeapInstructionStream::TryLookupCode(
              Isolate::Current(), reinterpret_cast<Address>(pc_));
          if (Builtins::IsBuiltinId(maybe_builtin)) {
            char const* name = Builtins::name(maybe_builtin);
            PrintF(stream_, "# %s                %sLOCATION: %s%s\n",
                   clr_debug_number, clr_debug_message, name, clr_normal);
          }
        }

        // Other options.
        switch (parameters & kDebuggerTracingDirectivesMask) {
          case TRACE_ENABLE:
            set_log_parameters(log_parameters() | parameters);
            if (parameters & LOG_SYS_REGS) {
              PrintSystemRegisters();
            }
            if (parameters & LOG_REGS) {
              PrintRegisters();
            }
            if (parameters & LOG_VREGS) {
              PrintVRegisters();
            }
            break;
          case TRACE_DISABLE:
            set_log_parameters(log_parameters() & ~parameters);
            break;
          case TRACE_OVERRIDE:
            set_log_parameters(parameters);
            break;
          default:
            // We don't support a one-shot LOG_DISASM.
            DCHECK_EQ(parameters & LOG_DISASM, 0);
            // Don't print information that is already being traced.
            parameters &= ~log_parameters();
            // Print the requested information.
            if (parameters & LOG_SYS_REGS) PrintSystemRegisters();
            if (parameters & LOG_REGS) PrintRegisters();
            if (parameters & LOG_VREGS) PrintVRegisters();
        }

        // The stop parameters are inlined in the code. Skip them:
        //  - Skip to the end of the message string.
        size_t size = kDebugMessageOffset + strlen(message) + 1;
        pc_ = pc_->InstructionAtOffset(RoundUp(size, kInstrSize));
        //  - Verify that the unreachable marker is present.
        DCHECK(pc_->Mask(ExceptionMask) == HLT);
        DCHECK_EQ(pc_->ImmException(), kImmExceptionIsUnreachable);
        //  - Skip past the unreachable marker.
        set_pc(pc_->following());

        // Check if the debugger should break.
        if (parameters & BREAK) Debug();

      } else if (instr->ImmException() == kImmExceptionIsRedirectedCall) {
        DoRuntimeCall(instr);
      } else if (instr->ImmException() == kImmExceptionIsPrintf) {
        DoPrintf(instr);
      } else if (instr->ImmException() == kImmExceptionIsSwitchStackLimit) {
        DoSwitchStackLimit(instr);
      } else if (instr->ImmException() == kImmExceptionIsUnreachable) {
        fprintf(stream_, "Hit UNREACHABLE marker at PC=%p.\n",
                reinterpret_cast<void*>(pc_));
        abort();

      } else {
        base::OS::DebugBreak();
      }
      break;
    }
    case BRK:
      base::OS::DebugBreak();
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitNEON2RegMisc(Instruction* instr) {
  NEONFormatDecoder nfd(instr);
  VectorFormat vf = nfd.GetVectorFormat();

  // Format mapping for "long pair" instructions, [su]addlp, [su]adalp.
  static const NEONFormatMap map_lp = {
      {23, 22, 30}, {NF_4H, NF_8H, NF_2S, NF_4S, NF_1D, NF_2D}};
  VectorFormat vf_lp = nfd.GetVectorFormat(&map_lp);

  static const NEONFormatMap map_fcvtl = {{22}, {NF_4S, NF_2D}};
  VectorFormat vf_fcvtl = nfd.GetVectorFormat(&map_fcvtl);

  static const NEONFormatMap map_fcvtn = {{22, 30},
                                          {NF_4H, NF_8H, NF_2S, NF_4S}};
  VectorFormat vf_fcvtn = nfd.GetVectorFormat(&map_fcvtn);

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());

  if (instr->Mask(NEON2RegMiscOpcode) <= NEON_NEG_opcode) {
    // These instructions all use a two bit size field, except NOT and RBIT,
    // which use the field to encode the operation.
    switch (instr->Mask(NEON2RegMiscMask)) {
      case NEON_REV64:
        rev64(vf, rd, rn);
        break;
      case NEON_REV32:
        rev32(vf, rd, rn);
        break;
      case NEON_REV16:
        rev16(vf, rd, rn);
        break;
      case NEON_SUQADD:
        suqadd(vf, rd, rn);
        break;
      case NEON_USQADD:
        usqadd(vf, rd, rn);
        break;
      case NEON_CLS:
        cls(vf, rd, rn);
        break;
      case NEON_CLZ:
        clz(vf, rd, rn);
        break;
      case NEON_CNT:
        cnt(vf, rd, rn);
        break;
      case NEON_SQABS:
        abs(vf, rd, rn).SignedSaturate(vf);
        break;
      case NEON_SQNEG:
        neg(vf, rd, rn).SignedSaturate(vf);
        break;
      case NEON_CMGT_zero:
        cmp(vf, rd, rn, 0, gt);
        break;
      case NEON_CMGE_zero:
        cmp(vf, rd, rn, 0, ge);
        break;
      case NEON_CMEQ_zero:
        cmp(vf, rd, rn, 0, eq);
        break;
      case NEON_CMLE_zero:
        cmp(vf, rd, rn, 0, le);
        break;
      case NEON_CMLT_zero:
        cmp(vf, rd, rn, 0, lt);
        break;
      case NEON_ABS:
        abs(vf, rd, rn);
        break;
      case NEON_NEG:
        neg(vf, rd, rn);
        break;
      case NEON_SADDLP:
        saddlp(vf_lp, rd, rn);
        break;
      case NEON_UADDLP:
        uaddlp(vf_lp, rd, rn);
        break;
      case NEON_SADALP:
        sadalp(vf_lp, rd, rn);
        break;
      case NEON_UADALP:
        uadalp(vf_lp, rd, rn);
        break;
      case NEON_RBIT_NOT:
        vf = nfd.GetVectorFormat(nfd.LogicalFormatMap());
        switch (instr->FPType()) {
          case 0:
            not_(vf, rd, rn);
            break;
          case 1:
            rbit(vf, rd, rn);
            break;
          default:
            UNIMPLEMENTED();
        }
        break;
    }
  } else {
    VectorFormat fpf = nfd.GetVectorFormat(instr->Mask(NEON2RegMiscHPFixed) ==
                                                   NEON2RegMiscHPFixed
                                               ? nfd.FPHPFormatMap()
                                               : nfd.FPFormatMap());
    FPRounding fpcr_rounding = static_cast<FPRounding>(fpcr().RMode());
    bool inexact_exception = false;

    // These instructions all use a one bit size field, except XTN, SQXTUN,
    // SHLL, SQXTN and UQXTN, which use a two bit size field.
    switch (instr->Mask(NEON2RegMiscFPMask ^ NEON2RegMiscHPFixed)) {
      case NEON_FABS:
        fabs_(fpf, rd, rn);
        return;
      case NEON_FNEG:
        fneg(fpf, rd, rn);
        return;
      case NEON_FSQRT:
        fsqrt(fpf, rd, rn);
        return;
      case NEON_FCVTL:
        if (instr->Mask(NEON_Q)) {
          fcvtl2(vf_fcvtl, rd, rn);
        } else {
          fcvtl(vf_fcvtl, rd, rn);
        }
        return;
      case NEON_FCVTN:
        if (instr->Mask(NEON_Q)) {
          fcvtn2(vf_fcvtn, rd, rn);
        } else {
          fcvtn(vf_fcvtn, rd, rn);
        }
        return;
      case NEON_FCVTXN:
        if (instr->Mask(NEON_Q)) {
          fcvtxn2(vf_fcvtn, rd, rn);
        } else {
          fcvtxn(vf_fcvtn, rd, rn);
        }
        return;

      // The following instructions break from the switch statement, rather
      // than return.
      case NEON_FRINTI:
        break;  // Use FPCR rounding mode.
      case NEON_FRINTX:
        inexact_exception = true;
        break;
      case NEON_FRINTA:
        fpcr_rounding = FPTieAway;
        break;
      case NEON_FRINTM:
        fpcr_rounding = FPNegativeInfinity;
        break;
      case NEON_FRINTN:
        fpcr_rounding = FPTieEven;
        break;
      case NEON_FRINTP:
        fpcr_rounding = FPPositiveInfinity;
        break;
      case NEON_FRINTZ:
        fpcr_rounding = FPZero;
        break;

      // The remaining cases return to the caller.
      case NEON_FCVTNS:
        fcvts(fpf, rd, rn, FPTieEven);
        return;
      case NEON_FCVTNU:
        fcvtu(fpf, rd, rn, FPTieEven);
        return;
      case NEON_FCVTPS:
        fcvts(fpf, rd, rn, FPPositiveInfinity);
        return;
      case NEON_FCVTPU:
        fcvtu(fpf, rd, rn, FPPositiveInfinity);
        return;
      case NEON_FCVTMS:
        fcvts(fpf, rd, rn, FPNegativeInfinity);
        return;
      case NEON_FCVTMU:
        fcvtu(fpf, rd, rn, FPNegativeInfinity);
        return;
      case NEON_FCVTZS:
        fcvts(fpf, rd, rn, FPZero);
        return;
      case NEON_FCVTZU:
        fcvtu(fpf, rd, rn, FPZero);
        return;
      case NEON_FCVTAS:
        fcvts(fpf, rd, rn, FPTieAway);
        return;
      case NEON_FCVTAU:
        fcvtu(fpf, rd, rn, FPTieAway);
        return;
      case NEON_SCVTF:
        scvtf(fpf, rd, rn, 0, fpcr_rounding);
        return;
      case NEON_UCVTF:
        ucvtf(fpf, rd, rn, 0, fpcr_rounding);
        return;
      case NEON_URSQRTE:
        ursqrte(fpf, rd, rn);
        return;
      case NEON_URECPE:
        urecpe(fpf, rd, rn);
        return;
      case NEON_FRSQRTE:
        frsqrte(fpf, rd, rn);
        return;
      case NEON_FRECPE:
        frecpe(fpf, rd, rn, fpcr_rounding);
        return;
      case NEON_FCMGT_zero:
        fcmp_zero(fpf, rd, rn, gt);
        return;
      case NEON_FCMGE_zero:
        fcmp_zero(fpf, rd, rn, ge);
        return;
      case NEON_FCMEQ_zero:
        fcmp_zero(fpf, rd, rn, eq);
        return;
      case NEON_FCMLE_zero:
        fcmp_zero(fpf, rd, rn, le);
        return;
      case NEON_FCMLT_zero:
        fcmp_zero(fpf, rd, rn, lt);
        return;
      default:
        if ((NEON_XTN_opcode <= instr->Mask(NEON2RegMiscOpcode)) &&
            (instr->Mask(NEON2RegMiscOpcode) <= NEON_UQXTN_opcode)) {
          switch (instr->Mask(NEON2RegMiscMask)) {
            case NEON_XTN:
              xtn(vf, rd, rn);
              return;
            case NEON_SQXTN:
              sqxtn(vf, rd, rn);
              return;
            case NEON_UQXTN:
              uqxtn(vf, rd, rn);
              return;
            case NEON_SQXTUN:
              sqxtun(vf, rd, rn);
              return;
            case NEON_SHLL:
              vf = nfd.GetVectorFormat(nfd.LongIntegerFormatMap());
              if (instr->Mask(NEON_Q)) {
                shll2(vf, rd, rn);
              } else {
                shll(vf, rd, rn);
              }
              return;
            default:
              UNIMPLEMENTED();
          }
        } else {
          UNIMPLEMENTED();
        }
    }

    // Only FRINT* instructions fall through the switch above.
    frint(fpf, rd, rn, fpcr_rounding, inexact_exception);
  }
}

void Simulator::VisitNEON3SameFP(NEON3SameOp op, VectorFormat vf,
                                 SimVRegister& rd, SimVRegister& rn,
                                 SimVRegister& rm) {
  switch (op) {
    case NEON_FADD:
      fadd(vf, rd, rn, rm);
      break;
    case NEON_FSUB:
      fsub(vf, rd, rn, rm);
      break;
    case NEON_FMUL:
      fmul(vf, rd, rn, rm);
      break;
    case NEON_FDIV:
      fdiv(vf, rd, rn, rm);
      break;
    case NEON_FMAX:
      fmax(vf, rd, rn, rm);
      break;
    case NEON_FMIN:
      fmin(vf, rd, rn, rm);
      break;
    case NEON_FMAXNM:
      fmaxnm(vf, rd, rn, rm);
      break;
    case NEON_FMINNM:
      fminnm(vf, rd, rn, rm);
      break;
    case NEON_FMLA:
      fmla(vf, rd, rn, rm);
      break;
    case NEON_FMLS:
      fmls(vf, rd, rn, rm);
      break;
    case NEON_FMULX:
      fmulx(vf, rd, rn, rm);
      break;
    case NEON_FACGE:
      fabscmp(vf, rd, rn, rm, ge);
      break;
    case NEON_FACGT:
      fabscmp(vf, rd, rn, rm, gt);
      break;
    case NEON_FCMEQ:
      fcmp(vf, rd, rn, rm, eq);
      break;
    case NEON_FCMGE:
      fcmp(vf, rd, rn, rm, ge);
      break;
    case NEON_FCMGT:
      fcmp(vf, rd, rn, rm, gt);
      break;
    case NEON_FRECPS:
      frecps(vf, rd, rn, rm);
      break;
    case NEON_FRSQRTS:
      frsqrts(vf, rd, rn, rm);
      break;
    case NEON_FABD:
      fabd(vf, rd, rn, rm);
      break;
    case NEON_FADDP:
      faddp(vf, rd, rn, rm);
      break;
    case NEON_FMAXP:
      fmaxp(vf, rd, rn, rm);
      break;
    case NEON_FMAXNMP:
      fmaxnmp(vf, rd, rn, rm);
      break;
    case NEON_FMINP:
      fminp(vf, rd, rn, rm);
      break;
    case NEON_FMINNMP:
      fminnmp(vf, rd, rn, rm);
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitNEON3Same(Instruction* instr) {
  NEONFormatDecoder nfd(instr);
  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  SimVRegister& rm = vreg(instr->Rm());

  if (instr->Mask(NEON3SameLogicalFMask) == NEON3SameLogicalFixed) {
    VectorFormat vf = nfd.GetVectorFormat(nfd.LogicalFormatMap());
    switch (instr->Mask(NEON3SameLogicalMask)) {
      case NEON_AND:
        and_(vf, rd, rn, rm);
        break;
      case NEON_ORR:
        orr(vf, rd, rn, rm);
        break;
      case NEON_ORN:
        orn(vf, rd, rn, rm);
        break;
      case NEON_EOR:
        eor(vf, rd, rn, rm);
        break;
      case NEON_BIC:
        bic(vf, rd, rn, rm);
        break;
      case NEON_BIF:
        bif(vf, rd, rn, rm);
        break;
      case NEON_BIT:
        bit(vf, rd, rn, rm);
        break;
      case NEON_BSL:
        bsl(vf, rd, rn, rm);
        break;
      default:
        UNIMPLEMENTED();
    }
  } else if (instr->Mask(NEON3SameFPFMask) == NEON3SameFPFixed) {
    VectorFormat vf = nfd.GetVectorFormat(nfd.FPFormatMap());
    VisitNEON3SameFP(instr->Mask(NEON3SameFPMask), vf, rd, rn, rm);
  } else {
    VectorFormat vf = nfd.GetVectorFormat();
    switch (instr->Mask(NEON3SameMask)) {
      case NEON_ADD:
        add(vf, rd, rn, rm);
        break;
      case NEON_ADDP:
        addp(vf, rd, rn, rm);
        break;
      case NEON_CMEQ:
        cmp(vf, rd, rn, rm, eq);
        break;
      case NEON_CMGE:
        cmp(vf, rd, rn, rm, ge);
        break;
      case NEON_CMGT:
        cmp(vf, rd, rn, rm, gt);
        break;
      case NEON_CMHI:
        cmp(vf, rd, rn, rm, hi);
        break;
      case NEON_CMHS:
        cmp(vf, rd, rn, rm, hs);
        break;
      case NEON_CMTST:
        cmptst(vf, rd, rn, rm);
        break;
      case NEON_MLS:
        mls(vf, rd, rn, rm);
        break;
      case NEON_MLA:
        mla(vf, rd, rn, rm);
        break;
      case NEON_MUL:
        mul(vf, rd, rn, rm);
        break;
      case NEON_PMUL:
        pmul(vf, rd, rn, rm);
        break;
      case NEON_SMAX:
        smax(vf, rd, rn, rm);
        break;
      case NEON_SMAXP:
        smaxp(vf, rd, rn, rm);
        break;
      case NEON_SMIN:
        smin(vf, rd, rn, rm);
        break;
      case NEON_SMINP:
        sminp(vf, rd, rn, rm);
        break;
      case NEON_SUB:
        sub(vf, rd, rn, rm);
        break;
      case NEON_UMAX:
        umax(vf, rd, rn, rm);
        break;
      case NEON_UMAXP:
        umaxp(vf, rd, rn, rm);
        break;
      case NEON_UMIN:
        umin(vf, rd, rn, rm);
        break;
      case NEON_UMINP:
        uminp(vf, rd, rn, rm);
        break;
      case NEON_SSHL:
        sshl(vf, rd, rn, rm);
        break;
      case NEON_USHL:
        ushl(vf, rd, rn, rm);
        break;
      case NEON_SABD:
        AbsDiff(vf, rd, rn, rm, true);
        break;
      case NEON_UABD:
        AbsDiff(vf, rd, rn, rm, false);
        break;
      case NEON_SABA:
        saba(vf, rd, rn, rm);
        break;
      case NEON_UABA:
        uaba(vf, rd, rn, rm);
        break;
      case NEON_UQADD:
        add(vf, rd, rn, rm).UnsignedSaturate(vf);
        break;
      case NEON_SQADD:
        add(vf, rd, rn, rm).SignedSaturate(vf);
        break;
      case NEON_UQSUB:
        sub(vf, rd, rn, rm).UnsignedSaturate(vf);
        break;
      case NEON_SQSUB:
        sub(vf, rd, rn, rm).SignedSaturate(vf);
        break;
      case NEON_SQDMULH:
        sqdmulh(vf, rd, rn, rm);
        break;
      case NEON_SQRDMULH:
        sqrdmulh(vf, rd, rn, rm);
        break;
      case NEON_UQSHL:
        ushl(vf, rd, rn, rm).UnsignedSaturate(vf);
        break;
      case NEON_SQSHL:
        sshl(vf, rd, rn, rm).SignedSaturate(vf);
        break;
      case NEON_URSHL:
        ushl(vf, rd, rn, rm).Round(vf);
        break;
      case NEON_SRSHL:
        sshl(vf, rd, rn, rm).Round(vf);
        break;
      case NEON_UQRSHL:
        ushl(vf, rd, rn, rm).Round(vf).UnsignedSaturate(vf);
        break;
      case NEON_SQRSHL:
        sshl(vf, rd, rn, rm).Round(vf).SignedSaturate(vf);
        break;
      case NEON_UHADD:
        add(vf, rd, rn, rm).Uhalve(vf);
        break;
      case NEON_URHADD:
        add(vf, rd, rn, rm).Uhalve(vf).Round(vf);
        break;
      case NEON_SHADD:
        add(vf, rd, rn, rm).Halve(vf);
        break;
      case NEON_SRHADD:
        add(vf, rd, rn, rm).Halve(vf).Round(vf);
        break;
      case NEON_UHSUB:
        sub(vf, rd, rn, rm).Uhalve(vf);
        break;
      case NEON_SHSUB:
        sub(vf, rd, rn, rm).Halve(vf);
        break;
      default:
        UNIMPLEMENTED();
    }
  }
}

void Simulator::VisitNEON3SameHP(Instruction* instr) {
  NEONFormatDecoder nfd(instr);
  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  SimVRegister& rm = vreg(instr->Rm());
  VectorFormat vf = nfd.GetVectorFormat(nfd.FPHPFormatMap());
  VisitNEON3SameFP(instr->Mask(NEON3SameFPMask) | NEON3SameHPMask, vf, rd, rn,
                   rm);
}

void Simulator::VisitNEON3Different(Instruction* instr) {
  NEONFormatDecoder nfd(instr);
  VectorFormat vf = nfd.GetVectorFormat();
  VectorFormat vf_l = nfd.GetVectorFormat(nfd.LongIntegerFormatMap());

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  SimVRegister& rm = vreg(instr->Rm());
  int size = instr->NEONSize();

  switch (instr->Mask(NEON3DifferentMask)) {
    case NEON_PMULL:
      if ((size == 1) || (size == 2)) {  // S/D reserved.
        VisitUnallocated(instr);
      } else {
        if (size == 3) vf_l = kFormat1Q;
        pmull(vf_l, rd, rn, rm);
      }
      break;
    case NEON_PMULL2:
      if ((size == 1) || (size == 2)) {  // S/D reserved.
        VisitUnallocated(instr);
      } else {
        if (size == 3) vf_l = kFormat1Q;
        pmull2(vf_l, rd, rn, rm);
      }
      break;
    case NEON_UADDL:
      uaddl(vf_l, rd, rn, rm);
      break;
    case NEON_UADDL2:
      uaddl2(vf_l, rd, rn, rm);
      break;
    case NEON_SADDL:
      saddl(vf_l, rd, rn, rm);
      break;
    case NEON_SADDL2:
      saddl2(vf_l, rd, rn, rm);
      break;
    case NEON_USUBL:
      usubl(vf_l, rd, rn, rm);
      break;
    case NEON_USUBL2:
      usubl2(vf_l, rd, rn, rm);
      break;
    case NEON_SSUBL:
      ssubl(vf_l, rd, rn, rm);
      break;
    case NEON_SSUBL2:
      ssubl2(vf_l, rd, rn, rm);
      break;
    case NEON_SABAL:
      sabal(vf_l, rd, rn, rm);
      break;
    case NEON_SABAL2:
      sabal2(vf_l, rd, rn, rm);
      break;
    case NEON_UABAL:
      uabal(vf_l, rd, rn, rm);
      break;
    case NEON_UABAL2:
      uabal2(vf_l, rd, rn, rm);
      break;
    case NEON_SABDL:
      sabdl(vf_l, rd, rn, rm);
      break;
    case NEON_SABDL2:
      sabdl2(vf_l, rd, rn, rm);
      break;
    case NEON_UABDL:
      uabdl(vf_l, rd, rn, rm);
      break;
    case NEON_UABDL2:
      uabdl2(vf_l, rd, rn, rm);
      break;
    case NEON_SMLAL:
      smlal(vf_l, rd, rn, rm);
      break;
    case NEON_SMLAL2:
      smlal2(vf_l, rd, rn, rm);
      break;
    case NEON_UMLAL:
      umlal(vf_l, rd, rn, rm);
      break;
    case NEON_UMLAL2:
      umlal2(vf_l, rd, rn, rm);
      break;
    case NEON_SMLSL:
      smlsl(vf_l, rd, rn, rm);
      break;
    case NEON_SMLSL2:
      smlsl2(vf_l, rd, rn, rm);
      break;
    case NEON_UMLSL:
      umlsl(vf_l, rd, rn, rm);
      break;
    case NEON_UMLSL2:
      umlsl2(vf_l, rd, rn, rm);
      break;
    case NEON_SMULL:
      smull(vf_l, rd, rn, rm);
      break;
    case NEON_SMULL2:
      smull2(vf_l, rd, rn, rm);
      break;
    case NEON_UMULL:
      umull(vf_l, rd, rn, rm);
      break;
    case NEON_UMULL2:
      umull2(vf_l, rd, rn, rm);
      break;
    case NEON_SQDMLAL:
      sqdmlal(vf_l, rd, rn, rm);
      break;
    case NEON_SQDMLAL2:
      sqdmlal2(vf_l, rd, rn, rm);
      break;
    case NEON_SQDMLSL:
      sqdmlsl(vf_l, rd, rn, rm);
      break;
    case NEON_SQDMLSL2:
      sqdmlsl2(vf_l, rd, rn, rm);
      break;
    case NEON_SQDMULL:
      sqdmull(vf_l, rd, rn, rm);
      break;
    case NEON_SQDMULL2:
      sqdmull2(vf_l, rd, rn, rm);
      break;
    case NEON_UADDW:
      uaddw(vf_l, rd, rn, rm);
      break;
    case NEON_UADDW2:
      uaddw2(vf_l, rd, rn, rm);
      break;
    case NEON_SADDW:
      saddw(vf_l, rd, rn, rm);
      break;
    case NEON_SADDW2:
      saddw2(vf_l, rd, rn, rm);
      break;
    case NEON_USUBW:
      usubw(vf_l, rd, rn, rm);
      break;
    case NEON_USUBW2:
      usubw2(vf_l, rd, rn, rm);
      break;
    case NEON_SSUBW:
      ssubw(vf_l, rd, rn, rm);
      break;
    case NEON_SSUBW2:
      ssubw2(vf_l, rd, rn, rm);
      break;
    case NEON_ADDHN:
      addhn(vf, rd, rn, rm);
      break;
    case NEON_ADDHN2:
      addhn2(vf, rd, rn, rm);
      break;
    case NEON_RADDHN:
      raddhn(vf, rd, rn, rm);
      break;
    case NEON_RADDHN2:
      raddhn2(vf, rd, rn, rm);
      break;
    case NEON_SUBHN:
      subhn(vf, rd, rn, rm);
      break;
    case NEON_SUBHN2:
      subhn2(vf, rd, rn, rm);
      break;
    case NEON_RSUBHN:
      rsubhn(vf, rd, rn, rm);
      break;
    case NEON_RSUBHN2:
      rsubhn2(vf, rd, rn, rm);
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitNEON3Extension(Instruction* instr) {
  NEONFormatDecoder nfd(instr);
  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rm = vreg(instr->Rm());
  SimVRegister& rn = vreg(instr->Rn());
  VectorFormat vf = nfd.GetVectorFormat();

  switch (instr->Mask(NEON3ExtensionMask)) {
    case NEON_SDOT:
      if (vf == kFormat4S || vf == kFormat2S) {
        sdot(vf, rd, rn, rm);
      } else {
        VisitUnallocated(instr);
      }

      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitNEONAcrossLanes(Instruction* instr) {
  NEONFormatDecoder nfd(instr);

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());

  // The input operand's VectorFormat is passed for these instructions.
  if (instr->Mask(NEONAcrossLanesFPFMask) == NEONAcrossLanesFPFixed) {
    VectorFormat vf = nfd.GetVectorFormat(nfd.FPFormatMap());

    switch (instr->Mask(NEONAcrossLanesFPMask)) {
      case NEON_FMAXV:
        fmaxv(vf, rd, rn);
        break;
      case NEON_FMINV:
        fminv(vf, rd, rn);
        break;
      case NEON_FMAXNMV:
        fmaxnmv(vf, rd, rn);
        break;
      case NEON_FMINNMV:
        fminnmv(vf, rd, rn);
        break;
      default:
        UNIMPLEMENTED();
    }
  } else {
    VectorFormat vf = nfd.GetVectorFormat();

    switch (instr->Mask(NEONAcrossLanesMask)) {
      case NEON_ADDV:
        addv(vf, rd, rn);
        break;
      case NEON_SMAXV:
        smaxv(vf, rd, rn);
        break;
      case NEON_SMINV:
        sminv(vf, rd, rn);
        break;
      case NEON_UMAXV:
        umaxv(vf, rd, rn);
        break;
      case NEON_UMINV:
        uminv(vf, rd, rn);
        break;
      case NEON_SADDLV:
        saddlv(vf, rd, rn);
        break;
      case NEON_UADDLV:
        uaddlv(vf, rd, rn);
        break;
      default:
        UNIMPLEMENTED();
    }
  }
}

void Simulator::VisitNEONByIndexedElement(Instruction* instr) {
  NEONFormatDecoder nfd(instr);
  VectorFormat vf_r = nfd.GetVectorFormat();
  VectorFormat vf = nfd.GetVectorFormat(nfd.LongIntegerFormatMap());

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());

  ByElementOp Op = nullptr;

  int rm_reg = instr->Rm();
  int index = (instr->NEONH() << 1) | instr->NEONL();
  if (instr->NEONSize() == 1) {
    rm_reg &= 0xF;
    index = (index << 1) | instr->NEONM();
  }

  switch (instr->Mask(NEONByIndexedElementMask)) {
    case NEON_MUL_byelement:
      Op = &Simulator::mul;
      vf = vf_r;
      break;
    case NEON_MLA_byelement:
      Op = &Simulator::mla;
      vf = vf_r;
      break;
    case NEON_MLS_byelement:
      Op = &Simulator::mls;
      vf = vf_r;
      break;
    case NEON_SQDMULH_byelement:
      Op = &Simulator::sqdmulh;
      vf = vf_r;
      break;
    case NEON_SQRDMULH_byelement:
      Op = &Simulator::sqrdmulh;
      vf = vf_r;
      break;
    case NEON_SMULL_byelement:
      if (instr->Mask(NEON_Q)) {
        Op = &Simulator::smull2;
      } else {
        Op = &Simulator::smull;
      }
      break;
    case NEON_UMULL_byelement:
      if (instr->Mask(NEON_Q)) {
        Op = &Simulator::umull2;
      } else {
        Op = &Simulator::umull;
      }
      break;
    case NEON_SMLAL_byelement:
      if (instr->Mask(NEON_Q)) {
        Op = &Simulator::smlal2;
      } else {
        Op = &Simulator::smlal;
      }
      break;
    case NEON_UMLAL_byelement:
      if (instr->Mask(NEON_Q)) {
        Op = &Simulator::umlal2;
      } else {
        Op = &Simulator::umlal;
      }
      break;
    case NEON_SMLSL_byelement:
      if (instr->Mask(NEON_Q)) {
        Op = &Simulator::smlsl2;
      } else {
        Op = &Simulator::smlsl;
      }
      break;
    case NEON_UMLSL_byelement:
      if (instr->Mask(NEON_Q)) {
        Op = &Simulator::umlsl2;
      } else {
        Op = &Simulator::umlsl;
      }
      break;
    case NEON_SQDMULL_byelement:
      if (instr->Mask(NEON_Q)) {
        Op = &Simulator::sqdmull2;
      } else {
        Op = &Simulator::sqdmull;
      }
      break;
    case NEON_SQDMLAL_byelement:
      if (instr->Mask(NEON_Q)) {
        Op = &Simulator::sqdmlal2;
      } else {
        Op = &Simulator::sqdmlal;
      }
      break;
    case NEON_SQDMLSL_byelement:
      if (instr->Mask(NEON_Q)) {
        Op = &Simulator::sqdmlsl2;
      } else {
        Op = &Simulator::sqdmlsl;
      }
      break;
    default:
      index = instr->NEONH();
      if ((instr->FPType() & 1) == 0) {
        index = (index << 1) | instr->NEONL();
      }

      vf = nfd.GetVectorFormat(nfd.FPFormatMap());

      switch (instr->Mask(NEONByIndexedElementFPMask)) {
        case NEON_FMUL_byelement:
          Op = &Simulator::fmul;
          break;
        case NEON_FMLA_byelement:
          Op = &Simulator::fmla;
          break;
        case NEON_FMLS_byelement:
          Op = &Simulator::fmls;
          break;
        case NEON_FMULX_byelement:
          Op = &Simulator::fmulx;
          break;
        default:
          UNIMPLEMENTED();
      }
  }

  (this->*Op)(vf, rd, rn, vreg(rm_reg), index);
}

void Simulator::VisitNEONCopy(Instruction* instr) {
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::TriangularFormatMap());
  VectorFormat vf = nfd.GetVectorFormat();

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  int imm5 = instr->ImmNEON5();
  int lsb = LowestSetBitPosition(imm5);
  int reg_index = imm5 >> lsb;

  if (instr->Mask(NEONCopyInsElementMask) == NEON_INS_ELEMENT) {
    int imm4 = instr->ImmNEON4();
    DCHECK_GE(lsb, 1);
    int rn_index = imm4 >> (lsb - 1);
    ins_element(vf, rd, reg_index, rn, rn_index);
  } else if (instr->Mask(NEONCopyInsGeneralMask) == NEON_INS_GENERAL) {
    ins_immediate(vf, rd, reg_index, xreg(instr->Rn()));
  } else if (instr->Mask(NEONCopyUmovMask) == NEON_UMOV) {
    uint64_t value = LogicVRegister(rn).Uint(vf, reg_index);
    value &= MaxUintFromFormat(vf);
    set_xreg(instr->Rd(), value);
  } else if (instr->Mask(NEONCopyUmovMask) == NEON_SMOV) {
    int64_t value = LogicVRegister(rn).Int(vf, reg_index);
    if (instr->NEONQ()) {
      set_xreg(instr->Rd(), value);
    } else {
      DCHECK(is_int32(value));
      set_wreg(instr->Rd(), static_cast<int32_t>(value));
    }
  } else if (instr->Mask(NEONCopyDupElementMask) == NEON_DUP_ELEMENT) {
    dup_element(vf, rd, rn, reg_index);
  } else if (instr->Mask(NEONCopyDupGeneralMask) == NEON_DUP_GENERAL) {
    dup_immediate(vf, rd, xreg(instr->Rn()));
  } else {
    UNIMPLEMENTED();
  }
}

void Simulator::VisitNEONExtract(Instruction* instr) {
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LogicalFormatMap());
  VectorFormat vf = nfd.GetVectorFormat();
  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  SimVRegister& rm = vreg(instr->Rm());
  if (instr->Mask(NEONExtractMask) == NEON_EXT) {
    int index = instr->ImmNEONExt();
    ext(vf, rd, rn, rm, index);
  } else {
    UNIMPLEMENTED();
  }
}

void Simulator::NEONLoadStoreMultiStructHelper(const Instruction* instr,
                                               AddrMode addr_mode) {
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LoadStoreFormatMap());
  VectorFormat vf = nfd.GetVectorFormat();

  uint64_t addr_base = xreg(instr->Rn(), Reg31IsStackPointer);
  int reg_size = RegisterSizeInBytesFromFormat(vf);

  int reg[4];
  uint64_t addr[4];
  for (int i = 0; i < 4; i++) {
    reg[i] = (instr->Rt() + i) % kNumberOfVRegisters;
    addr[i] = addr_base + (i * reg_size);
  }
  int count = 1;
  bool log_read = true;

  // Bit 23 determines whether this is an offset or post-index addressing mode.
  // In offset mode, bits 20 to 16 should be zero; these bits encode the
  // register of immediate in post-index mode.
  if ((instr->Bit(23) == 0) && (instr->Bits(20, 16) != 0)) {
    UNREACHABLE();
  }

  // We use the PostIndex mask here, as it works in this case for both Offset
  // and PostIndex addressing.
  switch (instr->Mask(NEONLoadStoreMultiStructPostIndexMask)) {
    case NEON_LD1_4v:
    case NEON_LD1_4v_post:
      ld1(vf, vreg(reg[3]), addr[3]);
      count++;
      [[fallthrough]];
    case NEON_LD1_3v:
    case NEON_LD1_3v_post:
      ld1(vf, vreg(reg[2]), addr[2]);
      count++;
      [[fallthrough]];
    case NEON_LD1_2v:
    case NEON_LD1_2v_post:
      ld1(vf, vreg(reg[1]), addr[1]);
      count++;
      [[fallthrough]];
    case NEON_LD1_1v:
    case NEON_LD1_1v_post:
      ld1(vf, vreg(reg[0]), addr[0]);
      break;
    case NEON_ST1_4v:
    case NEON_ST1_4v_post:
      st1(vf, vreg(reg[3]), addr[3]);
      count++;
      [[fallthrough]];
    case NEON_ST1_3v:
    case NEON_ST1_3v_post:
      st1(vf, vreg(reg[2]), addr[2]);
      count++;
      [[fallthrough]];
    case NEON_ST1_2v:
    case NEON_ST1_2v_post:
      st1(vf, vreg(reg[1]), addr[1]);
      count++;
      [[fallthrough]];
    case NEON_ST1_1v:
    case NEON_ST1_1v_post:
      st1(vf, vreg(reg[0]), addr[0]);
      log_read = false;
      break;
    case NEON_LD2_post:
    case NEON_LD2:
      ld2(vf, vreg(reg[0]), vreg(reg[1]), addr[0]);
      count = 2;
      break;
    case NEON_ST2:
    case NEON_ST2_post:
      st2(vf, vreg(reg[0]), vreg(reg[1]), addr[0]);
      count = 2;
      log_read = false;
      break;
    case NEON_LD3_post:
    case NEON_LD3:
      ld3(vf, vreg(reg[0]), vreg(reg[1]), vreg(reg[2]), addr[0]);
      count = 3;
      break;
    case NEON_ST3:
    case NEON_ST3_post:
      st3(vf, vreg(reg[0]), vreg(reg[1]), vreg(reg[2]), addr[0]);
      count = 3;
      log_read = false;
      break;
    case NEON_LD4_post:
    case NEON_LD4:
      ld4(vf, vreg(reg[0]), vreg(reg[1]), vreg(reg[2]), vreg(reg[3]), addr[0]);
      count = 4;
      break;
    case NEON_ST4:
    case NEON_ST4_post:
      st4(vf, vreg(reg[0]), vreg(reg[1]), vreg(reg[2]), vreg(reg[3]), addr[0]);
      count = 4;
      log_read = false;
      break;
    default:
      UNIMPLEMENTED();
  }

  {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    if (log_read) {
      local_monitor_.NotifyLoad();
    } else {
      local_monitor_.NotifyStore();
      GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_processor_);
    }
  }

  // Explicitly log the register update whilst we have type information.
  for (int i = 0; i < count; i++) {
    // For de-interleaving loads, only print the base address.
    int lane_size = LaneSizeInBytesFromFormat(vf);
    PrintRegisterFormat format = GetPrintRegisterFormatTryFP(
        GetPrintRegisterFormatForSize(reg_size, lane_size));
    if (log_read) {
      LogVRead(addr_base, reg[i], format);
    } else {
      LogVWrite(addr_base, reg[i], format);
    }
  }

  if (addr_mode == PostIndex) {
    int rm = instr->Rm();
    // The immediate post index addressing mode is indicated by rm = 31.
    // The immediate is implied by the number of vector registers used.
    addr_base +=
        (rm == 31) ? RegisterSizeInBytesFromFormat(vf) * count : xreg(rm);
    set_xreg(instr->Rn(), addr_base);
  } else {
    DCHECK_EQ(addr_mode, Offset);
  }
}

void Simulator::VisitNEONLoadStoreMultiStruct(Instruction* instr) {
  NEONLoadStoreMultiStructHelper(instr, Offset);
}

void Simulator::VisitNEONLoadStoreMultiStructPostIndex(Instruction* instr) {
  NEONLoadStoreMultiStructHelper(instr, PostIndex);
}

void Simulator::NEONLoadStoreSingleStructHelper(const Instruction* instr,
                                                AddrMode addr_mode) {
  uint64_t addr = xreg(instr->Rn(), Reg31IsStackPointer);
  int rt = instr->Rt();

  // Bit 23 determines whether this is an offset or post-index addressing mode.
  // In offset mode, bits 20 to 16 should be zero; these bits encode the
  // register of immediate in post-index mode.
  DCHECK_IMPLIES(instr->Bit(23) == 0, instr->Bits(20, 16) == 0);

  bool do_load = false;

  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LoadStoreFormatMap());
  VectorFormat vf_t = nfd.GetVectorFormat();

  VectorFormat vf = kFormat16B;
  // We use the PostIndex mask here, as it works in this case for both Offset
  // and PostIndex addressing.
  switch (instr->Mask(NEONLoadStoreSingleStructPostIndexMask)) {
    case NEON_LD1_b:
    case NEON_LD1_b_post:
    case NEON_LD2_b:
    case NEON_LD2_b_post:
    case NEON_LD3_b:
    case NEON_LD3_b_post:
    case NEON_LD4_b:
    case NEON_LD4_b_post:
      do_load = true;
      [[fallthrough]];
    case NEON_ST1_b:
    case NEON_ST1_b_post:
    case NEON_ST2_b:
    case NEON_ST2_b_post:
    case NEON_ST3_b:
    case NEON_ST3_b_post:
    case NEON_ST4_b:
    case NEON_ST4_b_post:
      break;

    case NEON_LD1_h:
    case NEON_LD1_h_post:
    case NEON_LD2_h:
    case NEON_LD2_h_post:
    case NEON_LD3_h:
    case NEON_LD3_h_post:
    case NEON_LD4_h:
    case NEON_LD4_h_post:
      do_load = true;
      [[fallthrough]];
    case NEON_ST1_h:
    case NEON_ST1_h_post:
    case NEON_ST2_h:
    case NEON_ST2_h_post:
    case NEON_ST3_h:
    case NEON_ST3_h_post:
    case NEON_ST4_h:
    case NEON_ST4_h_post:
      vf = kFormat8H;
      break;

    case NEON_LD1_s:
    case NEON_LD1_s_post:
    case NEON_LD2_s:
    case NEON_LD2_s_post:
    case NEON_LD3_s:
    case NEON_LD3_s_post:
    case NEON_LD4_s:
    case NEON_LD4_s_post:
      do_load = true;
      [[fallthrough]];
    case NEON_ST1_s:
    case NEON_ST1_s_post:
    case NEON_ST2_s:
    case NEON_ST2_s_post:
    case NEON_ST3_s:
    case NEON_ST3_s_post:
    case NEON_ST4_s:
    case NEON_ST4_s_post: {
      static_assert((NEON_LD1_s | (1 << NEONLSSize_offset)) == NEON_LD1_d,
                    "LSB of size distinguishes S and D registers.");
      static_assert(
          (NEON_LD1_s_post | (1 << NEONLSSize_offset)) == NEON_LD1_d_post,
          "LSB of size distinguishes S and D registers.");
      static_assert((NEON_ST1_s | (1 << NEONLSSize_offset)) == NEON_ST1_d,
                    "LSB of size distinguishes S and D registers.");
      static_assert(
          (NEON_ST1_s_post | (1 << NEONLSSize_offset)) == NEON_ST1_d_post,
          "LSB of size distinguishes S and D registers.");
      vf = ((instr->NEONLSSize() & 1) == 0) ? kFormat4S : kFormat2D;
      break;
    }

    case NEON_LD1R:
    case NEON_LD1R_post: {
      vf = vf_t;
      if (!ProbeMemory(addr, LaneSizeInBytesFromFormat(vf))) return;
      ld1r(vf, vreg(rt), addr);
      do_load = true;
      break;
    }

    case NEON_LD2R:
    case NEON_LD2R_post: {
      vf = vf_t;
      if (!ProbeMemory(addr, 2 * LaneSizeInBytesFromFormat(vf))) return;
      int rt2 = (rt + 1) % kNumberOfVRegisters;
      ld2r(vf, vreg(rt), vreg(rt2), addr);
      do_load = true;
      break;
    }

    case NEON_LD3R:
    case NEON_LD3R_post: {
      vf = vf_t;
      if (!ProbeMemory(addr, 3 * LaneSizeInBytesFromFormat(vf))) return;
      int rt2 = (rt + 1) % kNumberOfVRegisters;
      int rt3 = (rt2 + 1) % kNumberOfVRegisters;
      ld3r(vf, vreg(rt), vreg(rt2), vreg(rt3), addr);
      do_load = true;
      break;
    }

    case NEON_LD4R:
    case NEON_LD4R_post: {
      vf = vf_t;
      if (!ProbeMemory(addr, 4 * LaneSizeInBytesFromFormat(vf))) return;
      int rt2 = (rt + 1) % kNumberOfVRegisters;
      int rt3 = (rt2 + 1) % kNumberOfVRegisters;
      int rt4 = (rt3 + 1) % kNumberOfVRegisters;
      ld4r(vf, vreg(rt), vreg(rt2), vreg(rt3), vreg(rt4), addr);
      do_load = true;
      break;
    }
    default:
      UNIMPLEMENTED();
  }

  PrintRegisterFormat print_format =
      GetPrintRegisterFormatTryFP(GetPrintRegisterFormat(vf));
  // Make sure that the print_format only includes a single lane.
  print_format =
      static_cast<PrintRegisterFormat>(print_format & ~kPrintRegAsVectorMask);

  int esize = LaneSizeInBytesFromFormat(vf);
  int index_shift = LaneSizeInBytesLog2FromFormat(vf);
  int lane = instr->NEONLSIndex(index_shift);
  int scale = 0;
  int rt2 = (rt + 1) % kNumberOfVRegisters;
  int rt3 = (rt2 + 1) % kNumberOfVRegisters;
  int rt4 = (rt3 + 1) % kNumberOfVRegisters;
  switch (instr->Mask(NEONLoadStoreSingleLenMask)) {
    case NEONLoadStoreSingle1:
      scale = 1;
      if (!ProbeMemory(addr, scale * esize)) return;
      if (do_load) {
        ld1(vf, vreg(rt), lane, addr);
        LogVRead(addr, rt, print_format, lane);
      } else {
        st1(vf, vreg(rt), lane, addr);
        LogVWrite(addr, rt, print_format, lane);
      }
      break;
    case NEONLoadStoreSingle2:
      scale = 2;
      if (!ProbeMemory(addr, scale * esize)) return;
      if (do_load) {
        ld2(vf, vreg(rt), vreg(rt2), lane, addr);
        LogVRead(addr, rt, print_format, lane);
        LogVRead(addr + esize, rt2, print_format, lane);
      } else {
        st2(vf, vreg(rt), vreg(rt2), lane, addr);
        LogVWrite(addr, rt, print_format, lane);
        LogVWrite(addr + esize, rt2, print_format, lane);
      }
      break;
    case NEONLoadStoreSingle3:
      scale = 3;
      if (!ProbeMemory(addr, scale * esize)) return;
      if (do_load) {
        ld3(vf, vreg(rt), vreg(rt2), vreg(rt3), lane, addr);
        LogVRead(addr, rt, print_format, lane);
        LogVRead(addr + esize, rt2, print_format, lane);
        LogVRead(addr + (2 * esize), rt3, print_format, lane);
      } else {
        st3(vf, vreg(rt), vreg(rt2), vreg(rt3), lane, addr);
        LogVWrite(addr, rt, print_format, lane);
        LogVWrite(addr + esize, rt2, print_format, lane);
        LogVWrite(addr + (2 * esize), rt3, print_format, lane);
      }
      break;
    case NEONLoadStoreSingle4:
      scale = 4;
      if (!ProbeMemory(addr, scale * esize)) return;
      if (do_load) {
        ld4(vf, vreg(rt), vreg(rt2), vreg(rt3), vreg(rt4), lane, addr);
        LogVRead(addr, rt, print_format, lane);
        LogVRead(addr + esize, rt2, print_format, lane);
        LogVRead(addr + (2 * esize), rt3, print_format, lane);
        LogVRead(addr + (3 * esize), rt4, print_format, lane);
      } else {
        st4(vf, vreg(rt), vreg(rt2), vreg(rt3), vreg(rt4), lane, addr);
        LogVWrite(addr, rt, print_format, lane);
        LogVWrite(addr + esize, rt2, print_format, lane);
        LogVWrite(addr + (2 * esize), rt3, print_format, lane);
        LogVWrite(addr + (3 * esize), rt4, print_format, lane);
      }
      break;
    default:
      UNIMPLEMENTED();
  }

  {
    base::MutexGuard lock_guard(&GlobalMonitor::Get()->mutex);
    if (do_load) {
      local_monitor_.NotifyLoad();
    } else {
      local_monitor_.NotifyStore();
      GlobalMonitor::Get()->NotifyStore_Locked(&global_monitor_processor_);
    }
  }

  if (addr_mode == PostIndex) {
    int rm = instr->Rm();
    int lane_size = LaneSizeInBytesFromFormat(vf);
    set_xreg(instr->Rn(), addr + ((rm == 31) ? (scale * lane_size) : xreg(rm)));
  }
}

void Simulator::VisitNEONLoadStoreSingleStruct(Instruction* instr) {
  NEONLoadStoreSingleStructHelper(instr, Offset);
}

void Simulator::VisitNEONLoadStoreSingleStructPostIndex(Instruction* instr) {
  NEONLoadStoreSingleStructHelper(instr, PostIndex);
}

void Simulator::VisitNEONModifiedImmediate(Instruction* instr) {
  SimVRegister& rd = vreg(instr->Rd());
  int cmode = instr->NEONCmode();
  int cmode_3_1 = (cmode >> 1) & 7;
  int cmode_3 = (cmode >> 3) & 1;
  int cmode_2 = (cmode >> 2) & 1;
  int cmode_1 = (cmode >> 1) & 1;
  int cmode_0 = cmode & 1;
  int q = instr->NEONQ();
  int op_bit = instr->NEONModImmOp();
  uint64_t imm8 = instr->ImmNEONabcdefgh();

  // Find the format and immediate value
  uint64_t imm = 0;
  VectorFormat vform = kFormatUndefined;
  switch (cmode_3_1) {
    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
      vform = (q == 1) ? kFormat4S : kFormat2S;
      imm = imm8 << (8 * cmode_3_1);
      break;
    case 0x4:
    case 0x5:
      vform = (q == 1) ? kFormat8H : kFormat4H;
      imm = imm8 << (8 * cmode_1);
      break;
    case 0x6:
      vform = (q == 1) ? kFormat4S : kFormat2S;
      if (cmode_0 == 0) {
        imm = imm8 << 8 | 0x000000FF;
      } else {
        imm = imm8 << 16 | 0x0000FFFF;
      }
      break;
    case 0x7:
      if (cmode_0 == 0 && op_bit == 0) {
        vform = q ? kFormat16B : kFormat8B;
        imm = imm8;
      } else if (cmode_0 == 0 && op_bit == 1) {
        vform = q ? kFormat2D : kFormat1D;
        imm = 0;
        for (int i = 0; i < 8; ++i) {
          if (imm8 & (1ULL << i)) {
            imm |= (UINT64_C(0xFF) << (8 * i));
          }
        }
      } else {  // cmode_0 == 1, cmode == 0xF.
        if (op_bit == 0) {
          vform = q ? kFormat4S : kFormat2S;
          imm = base::bit_cast<uint32_t>(instr->ImmNEONFP32());
        } else if (q == 1) {
          vform = kFormat2D;
          imm = base::bit_cast<uint64_t>(instr->ImmNEONFP64());
        } else {
          DCHECK((q == 0) && (op_bit == 1) && (cmode == 0xF));
          VisitUnallocated(instr);
        }
      }
      break;
    default:
      UNREACHABLE();
  }

  // Find the operation.
  NEONModifiedImmediateOp op;
  if (cmode_3 == 0) {
    if (cmode_0 == 0) {
      op = op_bit ? NEONModifiedImmediate_MVNI : NEONModifiedImmediate_MOVI;
    } else {  // cmode<0> == '1'
      op = op_bit ? NEONModifiedImmediate_BIC : NEONModifiedImmediate_ORR;
    }
  } else {  // cmode<3> == '1'
    if (cmode_2 == 0) {
      if (cmode_0 == 0) {
        op = op_bit ? NEONModifiedImmediate_MVNI : NEONModifiedImmediate_MOVI;
      } else {  // cmode<0> == '1'
        op = op_bit ? NEONModifiedImmediate_BIC : NEONModifiedImmediate_ORR;
      }
    } else {  // cmode<2> == '1'
      if (cmode_1 == 0) {
        op = op_bit ? NEONModifiedImmediate_MVNI : NEONModifiedImmediate_MOVI;
      } else {  // cmode<1> == '1'
        if (cmode_0 == 0) {
          op = NEONModifiedImmediate_MOVI;
        } else {  // cmode<0> == '1'
          op = NEONModifiedImmediate_MOVI;
        }
      }
    }
  }

  // Call the logic function.
  switch (op) {
    case NEONModifiedImmediate_ORR:
      orr(vform, rd, rd, imm);
      break;
    case NEONModifiedImmediate_BIC:
      bic(vform, rd, rd, imm);
      break;
    case NEONModifiedImmediate_MOVI:
      movi(vform, rd, imm);
      break;
    case NEONModifiedImmediate_MVNI:
      mvni(vform, rd, imm);
      break;
    default:
      VisitUnimplemented(instr);
  }
}

void Simulator::VisitNEONScalar2RegMisc(Instruction* instr) {
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::ScalarFormatMap());
  VectorFormat vf = nfd.GetVectorFormat();

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());

  if (instr->Mask(NEON2RegMiscOpcode) <= NEON_NEG_scalar_opcode) {
    // These instructions all use a two bit size field, except NOT and RBIT,
    // which use the field to encode the operation.
    switch (instr->Mask(NEONScalar2RegMiscMask)) {
      case NEON_CMEQ_zero_scalar:
        cmp(vf, rd, rn, 0, eq);
        break;
      case NEON_CMGE_zero_scalar:
        cmp(vf, rd, rn, 0, ge);
        break;
      case NEON_CMGT_zero_scalar:
        cmp(vf, rd, rn, 0, gt);
        break;
      case NEON_CMLT_zero_scalar:
        cmp(vf, rd, rn, 0, lt);
        break;
      case NEON_CMLE_zero_scalar:
        cmp(vf, rd, rn, 0, le);
        break;
      case NEON_ABS_scalar:
        abs(vf, rd, rn);
        break;
      case NEON_SQABS_scalar:
        abs(vf, rd, rn).SignedSaturate(vf);
        break;
      case NEON_NEG_scalar:
        neg(vf, rd, rn);
        break;
      case NEON_SQNEG_scalar:
        neg(vf, rd, rn).SignedSaturate(vf);
        break;
      case NEON_SUQADD_scalar:
        suqadd(vf, rd, rn);
        break;
      case NEON_USQADD_scalar:
        usqadd(vf, rd, rn);
        break;
      default:
        UNIMPLEMENTED();
    }
  } else {
    VectorFormat fpf = nfd.GetVectorFormat(nfd.FPScalarFormatMap());
    FPRounding fpcr_rounding = static_cast<FPRounding>(fpcr().RMode());

    // These instructions all use a one bit size field, except SQXTUN, SQXTN
    // and UQXTN, which use a two bit size field.
    switch (instr->Mask(NEONScalar2RegMiscFPMask)) {
      case NEON_FRECPE_scalar:
        frecpe(fpf, rd, rn, fpcr_rounding);
        break;
      case NEON_FRECPX_scalar:
        frecpx(fpf, rd, rn);
        break;
      case NEON_FRSQRTE_scalar:
        frsqrte(fpf, rd, rn);
        break;
      case NEON_FCMGT_zero_scalar:
        fcmp_zero(fpf, rd, rn, gt);
        break;
      case NEON_FCMGE_zero_scalar:
        fcmp_zero(fpf, rd, rn, ge);
        break;
      case NEON_FCMEQ_zero_scalar:
        fcmp_zero(fpf, rd, rn, eq);
        break;
      case NEON_FCMLE_zero_scalar:
        fcmp_zero(fpf, rd, rn, le);
        break;
      case NEON_FCMLT_zero_scalar:
        fcmp_zero(fpf, rd, rn, lt);
        break;
      case NEON_SCVTF_scalar:
        scvtf(fpf, rd, rn, 0, fpcr_rounding);
        break;
      case NEON_UCVTF_scalar:
        ucvtf(fpf, rd, rn, 0, fpcr_rounding);
        break;
      case NEON_FCVTNS_scalar:
        fcvts(fpf, rd, rn, FPTieEven);
        break;
      case NEON_FCVTNU_scalar:
        fcvtu(fpf, rd, rn, FPTieEven);
        break;
      case NEON_FCVTPS_scalar:
        fcvts(fpf, rd, rn, FPPositiveInfinity);
        break;
      case NEON_FCVTPU_scalar:
        fcvtu(fpf, rd, rn, FPPositiveInfinity);
        break;
      case NEON_FCVTMS_scalar:
        fcvts(fpf, rd, rn, FPNegativeInfinity);
        break;
      case NEON_FCVTMU_scalar:
        fcvtu(fpf, rd, rn, FPNegativeInfinity);
        break;
      case NEON_FCVTZS_scalar:
        fcvts(fpf, rd, rn, FPZero);
        break;
      case NEON_FCVTZU_scalar:
        fcvtu(fpf, rd, rn, FPZero);
        break;
      case NEON_FCVTAS_scalar:
        fcvts(fpf, rd, rn, FPTieAway);
        break;
      case NEON_FCVTAU_scalar:
        fcvtu(fpf, rd, rn, FPTieAway);
        break;
      case NEON_FCVTXN_scalar:
        // Unlike all of the other FP instructions above, fcvtxn encodes dest
        // size S as size<0>=1. There's only one case, so we ignore the form.
        DCHECK_EQ(instr->Bit(22), 1);
        fcvtxn(kFormatS, rd, rn);
        break;
      default:
        switch (instr->Mask(NEONScalar2RegMiscMask)) {
          case NEON_SQXTN_scalar:
            sqxtn(vf, rd, rn);
            break;
          case NEON_UQXTN_scalar:
            uqxtn(vf, rd, rn);
            break;
          case NEON_SQXTUN_scalar:
            sqxtun(vf, rd, rn);
            break;
          default:
            UNIMPLEMENTED();
        }
    }
  }
}

void Simulator::VisitNEONScalar3Diff(Instruction* instr) {
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LongScalarFormatMap());
  VectorFormat vf = nfd.GetVectorFormat();

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  SimVRegister& rm = vreg(instr->Rm());
  switch (instr->Mask(NEONScalar3DiffMask)) {
    case NEON_SQDMLAL_scalar:
      sqdmlal(vf, rd, rn, rm);
      break;
    case NEON_SQDMLSL_scalar:
      sqdmlsl(vf, rd, rn, rm);
      break;
    case NEON_SQDMULL_scalar:
      sqdmull(vf, rd, rn, rm);
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitNEONScalar3Same(Instruction* instr) {
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::ScalarFormatMap());
  VectorFormat vf = nfd.GetVectorFormat();

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  SimVRegister& rm = vreg(instr->Rm());

  if (instr->Mask(NEONScalar3SameFPFMask) == NEONScalar3SameFPFixed) {
    vf = nfd.GetVectorFormat(nfd.FPScalarFormatMap());
    switch (instr->Mask(NEONScalar3SameFPMask)) {
      case NEON_FMULX_scalar:
        fmulx(vf, rd, rn, rm);
        break;
      case NEON_FACGE_scalar:
        fabscmp(vf, rd, rn, rm, ge);
        break;
      case NEON_FACGT_scalar:
        fabscmp(vf, rd, rn, rm, gt);
        break;
      case NEON_FCMEQ_scalar:
        fcmp(vf, rd, rn, rm, eq);
        break;
      case NEON_FCMGE_scalar:
        fcmp(vf, rd, rn, rm, ge);
        break;
      case NEON_FCMGT_scalar:
        fcmp(vf, rd, rn, rm, gt);
        break;
      case NEON_FRECPS_scalar:
        frecps(vf, rd, rn, rm);
        break;
      case NEON_FRSQRTS_scalar:
        frsqrts(vf, rd, rn, rm);
        break;
      case NEON_FABD_scalar:
        fabd(vf, rd, rn, rm);
        break;
      default:
        UNIMPLEMENTED();
    }
  } else {
    switch (instr->Mask(NEONScalar3SameMask)) {
      case NEON_ADD_scalar:
        add(vf, rd, rn, rm);
        break;
      case NEON_SUB_scalar:
        sub(vf, rd, rn, rm);
        break;
      case NEON_CMEQ_scalar:
        cmp(vf, rd, rn, rm, eq);
        break;
      case NEON_CMGE_scalar:
        cmp(vf, rd, rn, rm, ge);
        break;
      case NEON_CMGT_scalar:
        cmp(vf, rd, rn, rm, gt);
        break;
      case NEON_CMHI_scalar:
        cmp(vf, rd, rn, rm, hi);
        break;
      case NEON_CMHS_scalar:
        cmp(vf, rd, rn, rm, hs);
        break;
      case NEON_CMTST_scalar:
        cmptst(vf, rd, rn, rm);
        break;
      case NEON_USHL_scalar:
        ushl(vf, rd, rn, rm);
        break;
      case NEON_SSHL_scalar:
        sshl(vf, rd, rn, rm);
        break;
      case NEON_SQDMULH_scalar:
        sqdmulh(vf, rd, rn, rm);
        break;
      case NEON_SQRDMULH_scalar:
        sqrdmulh(vf, rd, rn, rm);
        break;
      case NEON_UQADD_scalar:
        add(vf, rd, rn, rm).UnsignedSaturate(vf);
        break;
      case NEON_SQADD_scalar:
        add(vf, rd, rn, rm).SignedSaturate(vf);
        break;
      case NEON_UQSUB_scalar:
        sub(vf, rd, rn, rm).UnsignedSaturate(vf);
        break;
      case NEON_SQSUB_scalar:
        sub(vf, rd, rn, rm).SignedSaturate(vf);
        break;
      case NEON_UQSHL_scalar:
        ushl(vf, rd, rn, rm).UnsignedSaturate(vf);
        break;
      case NEON_SQSHL_scalar:
        sshl(vf, rd, rn, rm).SignedSaturate(vf);
        break;
      case NEON_URSHL_scalar:
        ushl(vf, rd, rn, rm).Round(vf);
        break;
      case NEON_SRSHL_scalar:
        sshl(vf, rd, rn, rm).Round(vf);
        break;
      case NEON_UQRSHL_scalar:
        ushl(vf, rd, rn, rm).Round(vf).UnsignedSaturate(vf);
        break;
      case NEON_SQRSHL_scalar:
        sshl(vf, rd, rn, rm).Round(vf).SignedSaturate(vf);
        break;
      default:
        UNIMPLEMENTED();
    }
  }
}

void Simulator::VisitNEONScalarByIndexedElement(Instruction* instr) {
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LongScalarFormatMap());
  VectorFormat vf = nfd.GetVectorFormat();
  VectorFormat vf_r = nfd.GetVectorFormat(nfd.ScalarFormatMap());

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  ByElementOp Op = nullptr;

  int rm_reg = instr->Rm();
  int index = (instr->NEONH() << 1) | instr->NEONL();
  if (instr->NEONSize() == 1) {
    rm_reg &= 0xF;
    index = (index << 1) | instr->NEONM();
  }

  switch (instr->Mask(NEONScalarByIndexedElementMask)) {
    case NEON_SQDMULL_byelement_scalar:
      Op = &Simulator::sqdmull;
      break;
    case NEON_SQDMLAL_byelement_scalar:
      Op = &Simulator::sqdmlal;
      break;
    case NEON_SQDMLSL_byelement_scalar:
      Op = &Simulator::sqdmlsl;
      break;
    case NEON_SQDMULH_byelement_scalar:
      Op = &Simulator::sqdmulh;
      vf = vf_r;
      break;
    case NEON_SQRDMULH_byelement_scalar:
      Op = &Simulator::sqrdmulh;
      vf = vf_r;
      break;
    default:
      vf = nfd.GetVectorFormat(nfd.FPScalarFormatMap());
      index = instr->NEONH();
      if ((instr->FPType() & 1) == 0) {
        index = (index << 1) | instr->NEONL();
      }
      switch (instr->Mask(NEONScalarByIndexedElementFPMask)) {
        case NEON_FMUL_byelement_scalar:
          Op = &Simulator::fmul;
          break;
        case NEON_FMLA_byelement_scalar:
          Op = &Simulator::fmla;
          break;
        case NEON_FMLS_byelement_scalar:
          Op = &Simulator::fmls;
          break;
        case NEON_FMULX_byelement_scalar:
          Op = &Simulator::fmulx;
          break;
        default:
          UNIMPLEMENTED();
      }
  }

  (this->*Op)(vf, rd, rn, vreg(rm_reg), index);
}

void Simulator::VisitNEONScalarCopy(Instruction* instr) {
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::TriangularScalarFormatMap());
  VectorFormat vf = nfd.GetVectorFormat();

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());

  if (instr->Mask(NEONScalarCopyMask) == NEON_DUP_ELEMENT_scalar) {
    int imm5 = instr->ImmNEON5();
    int lsb = LowestSetBitPosition(imm5);
    int rn_index = imm5 >> lsb;
    dup_element(vf, rd, rn, rn_index);
  } else {
    UNIMPLEMENTED();
  }
}

void Simulator::VisitNEONScalarPairwise(Instruction* instr) {
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::FPScalarFormatMap());
  VectorFormat vf = nfd.GetVectorFormat();

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  switch (instr->Mask(NEONScalarPairwiseMask)) {
    case NEON_ADDP_scalar:
      addp(vf, rd, rn);
      break;
    case NEON_FADDP_scalar:
      faddp(vf, rd, rn);
      break;
    case NEON_FMAXP_scalar:
      fmaxp(vf, rd, rn);
      break;
    case NEON_FMAXNMP_scalar:
      fmaxnmp(vf, rd, rn);
      break;
    case NEON_FMINP_scalar:
      fminp(vf, rd, rn);
      break;
    case NEON_FMINNMP_scalar:
      fminnmp(vf, rd, rn);
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitNEONScalarShiftImmediate(Instruction* instr) {
  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  FPRounding fpcr_rounding = static_cast<FPRounding>(fpcr().RMode());

  static const NEONFormatMap map = {
      {22, 21, 20, 19},
      {NF_UNDEF, NF_B, NF_H, NF_H, NF_S, NF_S, NF_S, NF_S, NF_D, NF_D, NF_D,
       NF_D, NF_D, NF_D, NF_D, NF_D}};
  NEONFormatDecoder nfd(instr, &map);
  VectorFormat vf = nfd.GetVectorFormat();

  int highestSetBit = HighestSetBitPosition(instr->ImmNEONImmh());
  int immhimmb = instr->ImmNEONImmhImmb();
  int right_shift = (16 << highestSetBit) - immhimmb;
  int left_shift = immhimmb - (8 << highestSetBit);
  switch (instr->Mask(NEONScalarShiftImmediateMask)) {
    case NEON_SHL_scalar:
      shl(vf, rd, rn, left_shift);
      break;
    case NEON_SLI_scalar:
      sli(vf, rd, rn, left_shift);
      break;
    case NEON_SQSHL_imm_scalar:
      sqshl(vf, rd, rn, left_shift);
      break;
    case NEON_UQSHL_imm_scalar:
      uqshl(vf, rd, rn, left_shift);
      break;
    case NEON_SQSHLU_scalar:
      sqshlu(vf, rd, rn, left_shift);
      break;
    case NEON_SRI_scalar:
      sri(vf, rd, rn, right_shift);
      break;
    case NEON_SSHR_scalar:
      sshr(vf, rd, rn, right_shift);
      break;
    case NEON_USHR_scalar:
      ushr(vf, rd, rn, right_shift);
      break;
    case NEON_SRSHR_scalar:
      sshr(vf, rd, rn, right_shift).Round(vf);
      break;
    case NEON_URSHR_scalar:
      ushr(vf, rd, rn, right_shift).Round(vf);
      break;
    case NEON_SSRA_scalar:
      ssra(vf, rd, rn, right_shift);
      break;
    case NEON_USRA_scalar:
      usra(vf, rd, rn, right_shift);
      break;
    case NEON_SRSRA_scalar:
      srsra(vf, rd, rn, right_shift);
      break;
    case NEON_URSRA_scalar:
      ursra(vf, rd, rn, right_shift);
      break;
    case NEON_UQSHRN_scalar:
      uqshrn(vf, rd, rn, right_shift);
      break;
    case NEON_UQRSHRN_scalar:
      uqrshrn(vf, rd, rn, right_shift);
      break;
    case NEON_SQSHRN_scalar:
      sqshrn(vf, rd, rn, right_shift);
      break;
    case NEON_SQRSHRN_scalar:
      sqrshrn(vf, rd, rn, right_shift);
      break;
    case NEON_SQSHRUN_scalar:
      sqshrun(vf, rd, rn, right_shift);
      break;
    case NEON_SQRSHRUN_scalar:
      sqrshrun(vf, rd, rn, right_shift);
      break;
    case NEON_FCVTZS_imm_scalar:
      fcvts(vf, rd, rn, FPZero, right_shift);
      break;
    case NEON_FCVTZU_imm_scalar:
      fcvtu(vf, rd, rn, FPZero, right_shift);
      break;
    case NEON_SCVTF_imm_scalar:
      scvtf(vf, rd, rn, right_shift, fpcr_rounding);
      break;
    case NEON_UCVTF_imm_scalar:
      ucvtf(vf, rd, rn, right_shift, fpcr_rounding);
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitNEONShiftImmediate(Instruction* instr) {
  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  FPRounding fpcr_rounding = static_cast<FPRounding>(fpcr().RMode());

  // 00010->8B, 00011->16B, 001x0->4H, 001x1->8H,
  // 01xx0->2S, 01xx1->4S, 1xxx1->2D, all others undefined.
  static const NEONFormatMap map = {
      {22, 21, 20, 19, 30},
      {NF_UNDEF, NF_UNDEF, NF_8B,    NF_16B, NF_4H,    NF_8H, NF_4H,    NF_8H,
       NF_2S,    NF_4S,    NF_2S,    NF_4S,  NF_2S,    NF_4S, NF_2S,    NF_4S,
       NF_UNDEF, NF_2D,    NF_UNDEF, NF_2D,  NF_UNDEF, NF_2D, NF_UNDEF, NF_2D,
       NF_UNDEF, NF_2D,    NF_UNDEF, NF_2D,  NF_UNDEF, NF_2D, NF_UNDEF, NF_2D}};
  NEONFormatDecoder nfd(instr, &map);
  VectorFormat vf = nfd.GetVectorFormat();

  // 0001->8H, 001x->4S, 01xx->2D, all others undefined.
  static const NEONFormatMap map_l = {
      {22, 21, 20, 19},
      {NF_UNDEF, NF_8H, NF_4S, NF_4S, NF_2D, NF_2D, NF_2D, NF_2D}};
  VectorFormat vf_l = nfd.GetVectorFormat(&map_l);

  int highestSetBit = HighestSetBitPosition(instr->ImmNEONImmh());
  int immhimmb = instr->ImmNEONImmhImmb();
  int right_shift = (16 << highestSetBit) - immhimmb;
  int left_shift = immhimmb - (8 << highestSetBit);

  switch (instr->Mask(NEONShiftImmediateMask)) {
    case NEON_SHL:
      shl(vf, rd, rn, left_shift);
      break;
    case NEON_SLI:
      sli(vf, rd, rn, left_shift);
      break;
    case NEON_SQSHLU:
      sqshlu(vf, rd, rn, left_shift);
      break;
    case NEON_SRI:
      sri(vf, rd, rn, right_shift);
      break;
    case NEON_SSHR:
      sshr(vf, rd, rn, right_shift);
      break;
    case NEON_USHR:
      ushr(vf, rd, rn, right_shift);
      break;
    case NEON_SRSHR:
      sshr(vf, rd, rn, right_shift).Round(vf);
      break;
    case NEON_URSHR:
      ushr(vf, rd, rn, right_shift).Round(vf);
      break;
    case NEON_SSRA:
      ssra(vf, rd, rn, right_shift);
      break;
    case NEON_USRA:
      usra(vf, rd, rn, right_shift);
      break;
    case NEON_SRSRA:
      srsra(vf, rd, rn, right_shift);
      break;
    case NEON_URSRA:
      ursra(vf, rd, rn, right_shift);
      break;
    case NEON_SQSHL_imm:
      sqshl(vf, rd, rn, left_shift);
      break;
    case NEON_UQSHL_imm:
      uqshl(vf, rd, rn, left_shift);
      break;
    case NEON_SCVTF_imm:
      scvtf(vf, rd, rn, right_shift, fpcr_rounding);
      break;
    case NEON_UCVTF_imm:
      ucvtf(vf, rd, rn, right_shift, fpcr_rounding);
      break;
    case NEON_FCVTZS_imm:
      fcvts(vf, rd, rn, FPZero, right_shift);
      break;
    case NEON_FCVTZU_imm:
      fcvtu(vf, rd, rn, FPZero, right_shift);
      break;
    case NEON_SSHLL:
      vf = vf_l;
      if (instr->Mask(NEON_Q)) {
        sshll2(vf, rd, rn, left_shift);
      } else {
        sshll(vf, rd, rn, left_shift);
      }
      break;
    case NEON_USHLL:
      vf = vf_l;
      if (instr->Mask(NEON_Q)) {
        ushll2(vf, rd, rn, left_shift);
      } else {
        ushll(vf, rd, rn, left_shift);
      }
      break;
    case NEON_SHRN:
      if (instr->Mask(NEON_Q)) {
        shrn2(vf, rd, rn, right_shift);
      } else {
        shrn(vf, rd, rn, right_shift);
      }
      break;
    case NEON_RSHRN:
      if (instr->Mask(NEON_Q)) {
        rshrn2(vf, rd, rn, right_shift);
      } else {
        rshrn(vf, rd, rn, right_shift);
      }
      break;
    case NEON_UQSHRN:
      if (instr->Mask(NEON_Q)) {
        uqshrn2(vf, rd, rn, right_shift);
      } else {
        uqshrn(vf, rd, rn, right_shift);
      }
      break;
    case NEON_UQRSHRN:
      if (instr->Mask(NEON_Q)) {
        uqrshrn2(vf, rd, rn, right_shift);
      } else {
        uqrshrn(vf, rd, rn, right_shift);
      }
      break;
    case NEON_SQSHRN:
      if (instr->Mask(NEON_Q)) {
        sqshrn2(vf, rd, rn, right_shift);
      } else {
        sqshrn(vf, rd, rn, right_shift);
      }
      break;
    case NEON_SQRSHRN:
      if (instr->Mask(NEON_Q)) {
        sqrshrn2(vf, rd, rn, right_shift);
      } else {
        sqrshrn(vf, rd, rn, right_shift);
      }
      break;
    case NEON_SQSHRUN:
      if (instr->Mask(NEON_Q)) {
        sqshrun2(vf, rd, rn, right_shift);
      } else {
        sqshrun(vf, rd, rn, right_shift);
      }
      break;
    case NEON_SQRSHRUN:
      if (instr->Mask(NEON_Q)) {
        sqrshrun2(vf, rd, rn, right_shift);
      } else {
        sqrshrun(vf, rd, rn, right_shift);
      }
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitNEONTable(Instruction* instr) {
  NEONFormatDecoder nfd(instr, NEONFormatDecoder::LogicalFormatMap());
  VectorFormat vf = nfd.GetVectorFormat();

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  SimVRegister& rn2 = vreg((instr->Rn() + 1) % kNumberOfVRegisters);
  SimVRegister& rn3 = vreg((instr->Rn() + 2) % kNumberOfVRegisters);
  SimVRegister& rn4 = vreg((instr->Rn() + 3) % kNumberOfVRegisters);
  SimVRegister& rm = vreg(instr->Rm());

  switch (instr->Mask(NEONTableMask)) {
    case NEON_TBL_1v:
      tbl(vf, rd, rn, rm);
      break;
    case NEON_TBL_2v:
      tbl(vf, rd, rn, rn2, rm);
      break;
    case NEON_TBL_3v:
      tbl(vf, rd, rn, rn2, rn3, rm);
      break;
    case NEON_TBL_4v:
      tbl(vf, rd, rn, rn2, rn3, rn4, rm);
      break;
    case NEON_TBX_1v:
      tbx(vf, rd, rn, rm);
      break;
    case NEON_TBX_2v:
      tbx(vf, rd, rn, rn2, rm);
      break;
    case NEON_TBX_3v:
      tbx(vf, rd, rn, rn2, rn3, rm);
      break;
    case NEON_TBX_4v:
      tbx(vf, rd, rn, rn2, rn3, rn4, rm);
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::VisitNEONPerm(Instruction* instr) {
  NEONFormatDecoder nfd(instr);
  VectorFormat vf = nfd.GetVectorFormat();

  SimVRegister& rd = vreg(instr->Rd());
  SimVRegister& rn = vreg(instr->Rn());
  SimVRegister& rm = vreg(instr->Rm());

  switch (instr->Mask(NEONPermMask)) {
    case NEON_TRN1:
      trn1(vf, rd, rn, rm);
      break;
    case NEON_TRN2:
      trn2(vf, rd, rn, rm);
      break;
    case NEON_UZP1:
      uzp1(vf, rd, rn, rm);
      break;
    case NEON_UZP2:
      uzp2(vf, rd, rn, rm);
      break;
    case NEON_ZIP1:
      zip1(vf, rd, rn, rm);
      break;
    case NEON_ZIP2:
      zip2(vf, rd, rn, rm);
      break;
    default:
      UNIMPLEMENTED();
  }
}

void Simulator::DoSwitchStackLimit(Instruction* instr) {
  const int64_t stack_limit = xreg(16);
  stack_limit_ = static_cast<uintptr_t>(stack_limit);
}

void Simulator::DoPrintf(Instruction* instr) {
  DCHECK((instr->Mask(ExceptionMask) == HLT) &&
         (instr->ImmException() == kImmExceptionIsPrintf));

  // Read the arguments encoded inline in the instruction stream.
  uint32_t arg_count;
  uint32_t arg_pattern_list;
  static_assert(sizeof(*instr) == 1);
  memcpy(&arg_count, instr + kPrintfArgCountOffset, sizeof(arg_count));
  memcpy(&arg_pattern_list, instr + kPrintfArgPatternListOffset,
         sizeof(arg_pattern_list));

  DCHECK_LE(arg_count, kPrintfMaxArgCount);
  DCHECK_EQ(arg_pattern_list >> (kPrintfArgPatternBits * arg_count), 0);

  // We need to call the host printf function with a set of arguments defined by
  // arg_pattern_list. Because we don't know the types and sizes of the
  // arguments, this is very difficult to do in a robust and portable way. To
  // work around the problem, we pick apart the format string, and print one
  // format placeholder at a time.

  // Allocate space for the format string. We take a copy, so we can modify it.
  // Leave enough space for one extra character per expected argument (plus the
  // '\0' termination).
  const char* format_base = reg<const char*>(0);
  DCHECK_NOT_NULL(format_base);
  size_t length = strlen(format_base) + 1;
  char* const format = new char[length + arg_count];

  // A list of chunks, each with exactly one format placeholder.
  const char* chunks[kPrintfMaxArgCount];

  // Copy the format string and search for format placeholders.
  uint32_t placeholder_count = 0;
  char* format_scratch = format;
  for (size_t i = 0; i < length; i++) {
    if (format_base[i] != '%') {
      *format_scratch++ = format_base[i];
    } else {
      if (format_base[i + 1] == '%') {
        // Ignore explicit "%%" sequences.
        *format_scratch++ = format_base[i];

        if (placeholder_count == 0) {
          // The first chunk is passed to printf using "%s", so we need to
          // unescape "%%" sequences in this chunk. (Just skip the next '%'.)
          i++;
        } else {
          // Otherwise, pass through "%%" unchanged.
          *format_scratch++ = format_base[++i];
        }
      } else {
        CHECK(placeholder_count < arg_count);
        // Insert '\0' before placeholders, and store their locations.
        *format_scratch++ = '\0';
        chunks[placeholder_count++] = format_scratch;
        *format_scratch++ = format_base[i];
      }
    }
  }
  DCHECK(format_scratch <= (format + length + arg_count));
  CHECK(placeholder_count == arg_count);

  // Finally, call printf with each chunk, passing the appropriate register
  // argument. Normally, printf returns the number of bytes transmitted, so we
  // can emulate a single printf call by adding the result from each chunk. If
  // any call returns a negative (error) value, though, just return that value.

  fprintf(stream_, "%s", clr_printf);

  // Because '\0' is inserted before each placeholder, the first string in
  // 'format' contains no format placeholders and should be printed literally.
  int result = fprintf(stream_, "%s", format);
  int pcs_r = 1;  // Start at x1. x0 holds the format string.
  int pcs_f = 0;  // Start at d0.
  if (result >= 0) {
    for (uint32_t i = 0; i < placeholder_count; i++) {
      int part_result = -1;

      uint32_t arg_pattern = arg_pattern_list >> (i * kPrintfArgPatternBits);
      arg_pattern &= (1 << kPrintfArgPatternBits) - 1;
      switch (arg_pattern) {
        case kPrintfArgW:
          part_result = fprintf(stream_, chunks[i], wreg(pcs_r++));
          break;
        case kPrintfArgX:
          part_result = fprintf(stream_, chunks[i], xreg(pcs_r++));
          break;
        case kPrintfArgD:
          part_result = fprintf(stream_, chunks[i], dreg(pcs_f++));
          break;
        default:
          UNREACHABLE();
      }

      if (part_result < 0) {
        // Handle error values.
        result = part_result;
        break;
      }

      result += part_result;
    }
  }

  fprintf(stream_, "%s", clr_normal);

#ifdef DEBUG
  CorruptAllCallerSavedCPURegisters();
#endif

  // Printf returns its result in x0 (just like the C library's printf).
  set_xreg(0, result);

  // The printf parameters are inlined in the code, so skip them.
  set_pc(instr->InstructionAtOffset(kPrintfLength));

  // Set LR as if we'd just called a native printf function.
  set_lr(pc());

  delete[] format;
}

Simulator::LocalMonitor::LocalMonitor()
    : access_state_(MonitorAccess::Open),
      tagged_addr_(0),
      size_(TransactionSize::None) {}

void Simulator::LocalMonitor::Clear() {
  access_state_ = MonitorAccess::Open;
  tagged_addr_ = 0;
  size_ = TransactionSize::None;
}

void Simulator::LocalMonitor::NotifyLoad() {
  if (access_state_ == MonitorAccess::Exclusive) {
    // A non exclusive load could clear the local monitor. As a result, it's
    // most strict to unconditionally clear the local monitor on load.
    Clear();
  }
}

void Simulator::LocalMonitor::NotifyLoadExcl(uintptr_t addr,
                                             TransactionSize size) {
  access_state_ = MonitorAccess::Exclusive;
  tagged_addr_ = addr;
  size_ = size;
}

void Simulator::LocalMonitor::NotifyStore() {
  if (access_state_ == MonitorAccess::Exclusive) {
    // A non exclusive store could clear the local monitor. As a result, it's
    // most strict to unconditionally clear the local monitor on store.
    Clear();
  }
}

bool Simulator::LocalMonitor::NotifyStoreExcl(uintptr_t addr,
                                              TransactionSize size) {
  if (access_state_ == MonitorAccess::Exclusive) {
    // It is allowed for a processor to require that the address matches
    // exactly (B2.10.1), so this comparison does not mask addr.
    if (addr == tagged_addr_ && size_ == size) {
      Clear();
      return true;
    } else {
      // It is implementation-defined whether an exclusive store to a
      // non-tagged address will update memory. As a result, it's most strict
      // to unconditionally clear the local monitor.
      Clear();
      return false;
    }
  } else {
    DCHECK(access_state_ == MonitorAccess::Open);
    return false;
  }
}

Simulator::GlobalMonitor::Processor::Processor()
    : access_state_(MonitorAccess::Open),
      tagged_addr_(0),
      next_(nullptr),
      prev_(nullptr),
      failure_counter_(0) {}

void Simulator::GlobalMonitor::Processor::Clear_Locked() {
  access_state_ = MonitorAccess::Open;
  tagged_addr_ = 0;
}

void Simulator::GlobalMonitor::Processor::NotifyLoadExcl_Locked(
    uintptr_t addr) {
  access_state_ = MonitorAccess::Exclusive;
  tagged_addr_ = addr;
}

void Simulator::GlobalMonitor::Processor::NotifyStore_Locked(
    bool is_requesting_processor) {
  if (access_state_ == MonitorAccess::Exclusive) {
    // A non exclusive store could clear the global monitor. As a result, it's
    // most strict to unconditionally clear global monitors on store.
    Clear_Locked();
  }
}

bool Simulator::GlobalMonitor::Processor::NotifyStoreExcl_Locked(
    uintptr_t addr, bool is_requesting_processor) {
  if (access_state_ == MonitorAccess::Exclusive) {
    if (is_requesting_processor) {
      // It is allowed for a processor to require that the address matches
      // exactly (B2.10.2), so this comparison does not mask addr.
      if (addr == tagged_addr_) {
        Clear_Locked();
        // Introduce occasional stxr failures. This is to simulate the
        // behavior of hardware, which can randomly fail due to background
        // cache evictions.
        if (failure_counter_++ >= kMaxFailureCounter) {
          failure_counter_ = 0;
          return false;
        } else {
          return true;
        }
      }
    } else if ((addr & kExclusiveTaggedAddrMask) ==
               (tagged_addr_ & kExclusiveTaggedAddrMask)) {
      // Check the masked addresses when responding to a successful lock by
      // another processor so the implementation is more conservative (i.e. the
      // granularity of locking is as large as possible.)
      Clear_Locked();
      return false;
    }
  }
  return false;
}

void Simulator::GlobalMonitor::NotifyLoadExcl_Locked(uintptr_t addr,
                                                     Processor* processor) {
  processor->NotifyLoadExcl_Locked(addr);
  PrependProcessor_Locked(processor);
}

void Simulator::GlobalMonitor::NotifyStore_Locked(Processor* processor) {
  // Notify each processor of the store operation.
  for (Processor* iter = head_; iter; iter = iter->next_) {
    bool is_requesting_processor = iter == processor;
    iter->NotifyStore_Locked(is_requesting_processor);
  }
}

bool Simulator::GlobalMonitor::NotifyStoreExcl_Locked(uintptr_t addr,
                                                      Processor* processor) {
  DCHECK(IsProcessorInLinkedList_Locked(processor));
  if (processor->NotifyStoreExcl_Locked(addr, true)) {
    // Notify the other processors that this StoreExcl succeeded.
    for (Processor* iter = head_; iter; iter = iter->next_) {
      if (iter != processor) {
        iter->NotifyStoreExcl_Locked(addr, false);
      }
    }
    return true;
  } else {
    return false;
  }
}

bool Simulator::GlobalMonitor::IsProcessorInLinkedList_Locked(
    Processor* processor) const {
  return head_ == processor || processor->next_ || processor->prev_;
}

void Simulator::GlobalMonitor::PrependProcessor_Locked(Processor* processor) {
  if (IsProcessorInLinkedList_Locked(processor)) {
    return;
  }

  if (head_) {
    head_->prev_ = processor;
  }
  processor->prev_ = nullptr;
  processor->next_ = head_;
  head_ = processor;
}

void Simulator::GlobalMonitor::RemoveProcessor(Processor* processor) {
  base::MutexGuard lock_guard(&mutex);
  if (!IsProcessorInLinkedList_Locked(processor)) {
    return;
  }

  if (processor->prev_) {
    processor->prev_->next_ = processor->next_;
  } else {
    head_ = processor->next_;
  }
  if (processor->next_) {
    processor->next_->prev_ = processor->prev_;
  }
  processor->prev_ = nullptr;
  processor->next_ = nullptr;
}

#undef SScanF
#undef COLOUR
#undef COLOUR_BOLD
#undef NORMAL
#undef GREY
#undef RED
#undef GREEN
#undef YELLOW
#undef BLUE
#undef MAGENTA
#undef CYAN
#undef WHITE
#undef COMMAND_SIZE
#undef ARG_SIZE
#undef STR
#undef XSTR

}  // namespace internal
}  // namespace v8

//
// The following functions are used by our gdb macros.
//
V8_DONT_STRIP_SYMBOL
V8_EXPORT_PRIVATE extern bool _v8_internal_Simulator_ExecDebugCommand(
    const char* command) {
  i::Isolate* isolate = i::Isolate::Current();
  if (!isolate) {
    fprintf(stderr, "No V8 Isolate found\n");
    return false;
  }
  i::Simulator* simulator = i::Simulator::current(isolate);
  if (!simulator) {
    fprintf(stderr, "No Arm64 simulator found\n");
    return false;
  }
  // Copy the command so that the simulator can take ownership of it.
  size_t len = strlen(command);
  i::ArrayUniquePtr<char> command_copy(i::NewArray<char>(len + 1));
  i::MemCopy(command_copy.get(), command, len + 1);
  return simulator->ExecDebugCommand(std::move(command_copy));
}

#undef BRACKETS

#endif  // USE_SIMULATOR
