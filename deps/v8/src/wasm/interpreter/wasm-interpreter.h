// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_INTERPRETER_WASM_INTERPRETER_H_
#define V8_WASM_INTERPRETER_WASM_INTERPRETER_H_

#include <atomic>
#include <memory>
#include <vector>

#include "src/base/platform/time.h"
#include "src/base/platform/wrappers.h"
#include "src/base/small-vector.h"
#include "src/base/vector.h"
#include "src/common/simd128.h"
#include "src/logging/counters.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/interpreter/instruction-handlers.h"
#include "src/wasm/interpreter/wasm-interpreter-objects.h"
#include "src/wasm/wasm-value.h"

////////////////////////////////////////////////////////////////////////////////
//
// DrumBrake: An interpreter for WebAssembly.
//
////////////////////////////////////////////////////////////////////////////////

// Uncomment to enable profiling.
// #define DRUMBRAKE_ENABLE_PROFILING true
//

#ifdef V8_HOST_ARCH_ARM64
#define VECTORCALL
#else
#if defined(__clang__)
#define VECTORCALL __vectorcall
#else  // GCC or MSVC
#define VECTORCALL
#endif  // __clang__
#endif  // V8_HOST_ARCH_ARM64

typedef void InstrHandlerRetType;
#define INSTRUCTION_HANDLER_FUNC \
  static DISABLE_CFI_ICALL InstrHandlerRetType VECTORCALL

namespace v8 {

namespace internal {
class Cell;
class FixedArray;
class WasmInstanceObject;

namespace wasm {

// Forward declarations.
class Decoder;
struct InterpreterCode;
class InterpreterHandle;
struct ModuleWireBytes;
class WasmBytecode;
class WasmBytecodeGenerator;
class WasmCode;
struct WasmFunction;
struct WasmModule;
class WasmInterpreterRuntime;
class WasmInterpreterThread;

using pc_t = size_t;
using CodeOffset = size_t;
using WasmRef = Handle<Object>;

// We are using sizeof(WasmRef) and kSystemPointerSize interchangeably in the
// interpreter code.
static_assert(sizeof(WasmRef) == kSystemPointerSize);

// Code and metadata needed to execute a function.
struct InterpreterCode {
  InterpreterCode(const WasmFunction* function, BodyLocalDecls locals,
                  const uint8_t* start, const uint8_t* end)
      : function(function), locals(locals), start(start), end(end) {}

  const uint8_t* at(pc_t pc) { return start + pc; }

  const WasmFunction* function;  // wasm function
  BodyLocalDecls locals;         // local declarations
  const uint8_t* start;          // start of code
  const uint8_t* end;            // end of code
  std::unique_ptr<WasmBytecode> bytecode;
};

struct FrameState {
  FrameState()
      : current_function_(nullptr),
        previous_frame_(nullptr),
        current_bytecode_(nullptr),
        current_sp_(nullptr),
        thread_(nullptr),
        ref_array_current_sp_(0),
        ref_array_length_(0),
        handle_scope_(nullptr)
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
        ,
        current_stack_height_(0),
        current_stack_start_args_(0),
        current_stack_start_locals_(0),
        current_stack_start_stack_(0)
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
  {
  }

  const WasmBytecode* current_function_;
  const FrameState* previous_frame_;
  const uint8_t* current_bytecode_;
  uint8_t* current_sp_;
  WasmInterpreterThread* thread_;
  uint32_t ref_array_current_sp_;
  uint32_t ref_array_length_;
  HandleScope* handle_scope_;

  // Maintains a reference to the exceptions caught by each catch handler.
  void SetCaughtException(Isolate* isolate, uint32_t catch_block_index,
                          Handle<Object> exception);
  Handle<Object> GetCaughtException(Isolate* isolate,
                                    uint32_t catch_block_index) const;
  void DisposeCaughtExceptionsArray(Isolate* isolate);
  Handle<FixedArray> caught_exceptions_;

  inline void ResetHandleScope(Isolate* isolate);

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  uint32_t current_stack_height_;
  uint32_t current_stack_start_args_;
  uint32_t current_stack_start_locals_;
  uint32_t current_stack_start_stack_;
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
};

// Manages the calculations of the
// V8.WasmInterpreterExecutionInTenSecondsPercentage histogram, which measures
// the percentage of time spent executing in the Wasm interpreter in a 10
// seconds window and it is useful to detect applications that are CPU-bound
// and that could be visibly slowed down by the interpreter. Only about one
// sample per minute is generated.
class WasmExecutionTimer {
 public:
  WasmExecutionTimer(Isolate* isolate, bool track_jitless_wasm);

  V8_INLINE void Start() {
    if (execute_ratio_histogram_->Enabled()) StartInternal();
  }

  V8_INLINE void Stop() {
    if (execute_ratio_histogram_->Enabled()) StopInternal();
  }

  void Terminate();

 private:
  void StartInternal();
  void StopInternal();

  void BeginInterval(bool start_timer);
  void EndInterval();

  void AddSample(int running_ratio);

  Histogram* execute_ratio_histogram_;
  Histogram* slow_wasm_histogram_;
  base::ElapsedTimer window_execute_timer_;
  bool window_has_started_;
  base::TimeTicks next_interval_time_;
  base::TimeTicks start_interval_time_;
  base::TimeDelta window_running_time_;
  const base::TimeDelta sample_duration_;
  base::TimeDelta cooldown_interval_;  // Pause between samples.
  const int slow_threshold_;
  const size_t slow_threshold_samples_count_;
  std::vector<int> samples_;
  Isolate* isolate_;

  static const int kMaxPercentValue = 100000;
};

class V8_EXPORT_PRIVATE WasmInterpreterThreadMap {
 public:
  WasmInterpreterThread* GetCurrentInterpreterThread(Isolate* isolate);

  void NotifyIsolateDisposal(Isolate* isolate);

 private:
  typedef std::unordered_map<int, std::unique_ptr<WasmInterpreterThread>>
      ThreadInterpreterMap;
  ThreadInterpreterMap map_;
  base::Mutex mutex_;
};

// Representation of a thread in the interpreter.
class V8_EXPORT_PRIVATE WasmInterpreterThread {
 public:
  // State machine for a WasmInterpreterThread:
  //
  //               STOPPED
  //                  |
  //             Run()|
  //                  V
  //               RUNNING <-----------------------------------+
  //                  |                                        |
  //                  |                                        |
  //    +-------------+---------------+---------------+        |
  //    |Stop()       |Trap()         |Finish()       |        |
  //    V             V               V               V        |
  // STOPPED <---- TRAPPED         FINISHED     EH_UNWINDING --+
  //    ^                                             |
  //    +---------------------------------------------+
  //
  // In more detail:
  // - For each loaded instance, an InterpreterHandler is created that owns a
  //   WasmInterpreter that owns a WasmInterpreterRuntime object.
  //
  // - The WasmInterpreterThread is created in STOPPED state.
  //
  // - InterpreterHandle::Execute(func_index, ...) executes Wasm code in
  // the interpreter:
  //   - WasmInterpreter::BeginExecution ->
  //       WasmInterpreterRuntime::BeginExecution ->
  //         WasmInterpreterThread::StartActivation() -> Run() -> RUNNING
  //         state.
  //   - WasmInterpreter::ContinueExecution ->
  //       WasmInterpreterRuntime::ContinueExecution ->
  //         WasmInterpreterRuntime::ExecuteFunction
  //
  // WasmInterpreterRuntime::ExecuteFunction(..., func_index, ...) executes a
  // specific Wasm function.
  // If 'func_index' indicates an imported function, and the call fails ->
  //   Stop() -> STOPPED state.
  // If 'func_index' indicates an not-imported function, we start executing a
  // sequence of instruction handlers. One of these handlers can cause a
  //   Trap()  -> TRAPPED state.
  // From these instructions sequence we can make several kinds of direct or
  // indirect wasm calls to:
  //  . An external JS function ->
  //      WasmInterpreterRuntime::CallExternalJSFunction() ->
  //      If the call fails -> Stop() -> STOPPED state.
  //  . A Wasm function in the same module instance, recursively calling
  //      WasmInterpreterRuntime::ExecuteFunction().
  //  . A Wasm function in a different module instance. In this case we
  //      recusively call InterpreterHandle::Execute with the
  //      InterpreterHandle of that different instance. If the call fails ->
  //      Stop() -> STOPPED state.
  //
  // After WasmInterpreterRuntime::ExecuteFunction() completes, if we ended up
  // in the TRAPPED state we raise a JS exception  -> RaiseException() ->
  // Stop() -> STOPPED state.
  //
  // If an exception can be handled by Wasm code, according to the Wasm
  // Exception Handling proposal, the thread can go to the EH_UNWINDING state
  // while looking for a Wasm function in the call stack that has a {catch}
  // instruction that can handle that exception. If no such catch handler is
  // found, the thread goes to STOPPED.
  //
  // If we are running the WasmInterpreter of instance A and we can call
  // from a function of a different instance B (via
  // InterpreterHandle::Execute()) the execution of instance A "suspends"
  // waiting for the execution in the WasmInterpreter of instance B to
  // complete. Instance B can call back into instance A, and so on... This
  // means that in the call stack we might have a sequence of stack frames for
  // the WasmInterpreter A followed by frames for instance B followed by
  // more frames of instance A.
  // To manage this case WasmInterpreterThread maintains a stack of
  // Activations, which represents the set of StackFrames for a given module
  // instance. Only when the last active Activation terminates we call
  // Finish() -> FINISHED state.

  enum State { STOPPED, RUNNING, FINISHED, TRAPPED, EH_UNWINDING };

  enum ExceptionHandlingResult { HANDLED, UNWOUND };

  struct TrapStatus {
    //  bool has_trapped;
    int trap_function_index;
    int trap_pc;
  };

  class Activation {
   public:
    Activation(WasmInterpreterThread* thread,
               WasmInterpreterRuntime* wasm_runtime, Address frame_pointer,
               uint8_t* start_fp, const FrameState& callee_frame_state)
        : thread_(thread),
          wasm_runtime_(wasm_runtime),
          frame_pointer_(frame_pointer),
          current_frame_size_(0),
          ref_stack_size_(0),
          current_fp_(start_fp),
          current_frame_state_(callee_frame_state)
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
          ,
          current_stack_start_(callee_frame_state.current_stack_start_args_ +
                               thread->CurrentStackFrameSize()),
          current_stack_size_(0)
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
    {
    }

    WasmInterpreterThread* thread() const { return thread_; }

    inline Isolate* GetIsolate() const;

    Address GetFramePointer() const { return frame_pointer_; }

    void SetCurrentFrame(const FrameState& frame_state) {
      current_frame_state_ = frame_state;
    }
    const FrameState& GetCurrentFrame() const { return current_frame_state_; }

    void SetCurrentActivationFrame(uint8_t* current_fp,
                                   uint32_t current_frame_size,
                                   uint32_t current_stack_size,
                                   uint32_t ref_stack_size) {
      current_fp_ = current_fp;
      current_frame_size_ = current_frame_size;
      ref_stack_size_ = ref_stack_size;

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
      current_stack_size_ = current_stack_size;
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
    }

    uint8_t* NextFrameAddress() const {
      return current_fp_ + current_frame_size_;
    }

    uint32_t NextRefStackOffset() const { return ref_stack_size_; }

    void SetTrapped(int trap_function_index, int trap_pc) {
      // Capture the call stack at the moment of the trap and store it to be
      // retrieved later. This works because, once an Activation has trapped,
      // execution will never resume in it, given that Wasm EH is not
      // supported yet.
      TrapStatus trap_status{trap_function_index, trap_pc};
      trap_stack_trace_ =
          std::make_unique<std::vector<WasmInterpreterStackEntry>>(
              CaptureStackTrace(&trap_status));
    }

    std::vector<WasmInterpreterStackEntry> GetStackTrace() {
      if (trap_stack_trace_) {
        return *trap_stack_trace_;
      }

      // If the Activation has not trapped, it is still executing so we need
      // to capture the current call stack.
      return CaptureStackTrace();
    }

    int GetFunctionIndex(int index) const;

    const WasmInterpreterRuntime* GetWasmRuntime() const {
      return wasm_runtime_;
    }

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    uint32_t CurrentStackFrameStart() const { return current_stack_start_; }
    uint32_t CurrentStackFrameSize() const { return current_stack_size_; }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

   private:
    std::vector<WasmInterpreterStackEntry> CaptureStackTrace(
        const TrapStatus* trap_status = nullptr) const;

    WasmInterpreterThread* thread_;
    WasmInterpreterRuntime* wasm_runtime_;
    Address frame_pointer_;
    uint32_t current_frame_size_;
    uint32_t ref_stack_size_;
    uint8_t* current_fp_;
    FrameState current_frame_state_;
    std::unique_ptr<std::vector<WasmInterpreterStackEntry>> trap_stack_trace_;
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    uint32_t current_stack_start_;
    uint32_t current_stack_size_;
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
  };

  explicit WasmInterpreterThread(Isolate* isolate)
      : isolate_(isolate),
        state_(State::STOPPED),
        trap_reason_(TrapReason::kTrapUnreachable),
        current_stack_size_(kInitialStackSize),
        stack_mem_(nullptr),
        execution_timer_(isolate, true) {
    PageAllocator* page_allocator = GetPlatformPageAllocator();
    stack_mem_ = AllocatePages(page_allocator, nullptr, kMaxStackSize,
                               page_allocator->AllocatePageSize(),
                               PageAllocator::kNoAccess);
    if (!stack_mem_ ||
        !SetPermissions(page_allocator, stack_mem_, current_stack_size_,
                        PageAllocator::Permission::kReadWrite)) {
      V8::FatalProcessOutOfMemory(
          nullptr, "WasmInterpreterThread::WasmInterpreterThread",
          "Cannot allocate Wasm interpreter stack");
      UNREACHABLE();
    }
  }

  ~WasmInterpreterThread() {
    FreePages(GetPlatformPageAllocator(), stack_mem_, kMaxStackSize);
  }

  bool ExpandStack(size_t additional_required_size) {
    if (current_stack_size_ + additional_required_size > kMaxStackSize) {
      return false;
    }

    uint32_t new_size = current_stack_size_;
    while (new_size < current_stack_size_ + additional_required_size) {
      new_size = std::min(new_size + kStackSizeIncrement, kMaxStackSize);
    }

    if (SetPermissions(GetPlatformPageAllocator(), stack_mem_, new_size,
                       PageAllocator::Permission::kReadWrite)) {
      current_stack_size_ = new_size;
      return true;
    }
    return false;
  }

  static void Initialize() {
    // This function can be called multiple times by fuzzers.
    if (thread_interpreter_map_s) return;
    thread_interpreter_map_s = new WasmInterpreterThreadMap();
  }

  static void Terminate() {
    delete thread_interpreter_map_s;
    thread_interpreter_map_s = nullptr;
  }

  static void NotifyIsolateDisposal(Isolate* isolate) {
    thread_interpreter_map_s->NotifyIsolateDisposal(isolate);
  }

  static WasmInterpreterThread* GetCurrentInterpreterThread(Isolate* isolate) {
    DCHECK_NOT_NULL(thread_interpreter_map_s);
    return thread_interpreter_map_s->GetCurrentInterpreterThread(isolate);
  }

  const Isolate* GetIsolate() const { return isolate_; }

  State state() const { return state_; }

  void Run() { state_ = State::RUNNING; }
  void Stop() { state_ = State::STOPPED; }

  void Trap(TrapReason trap_reason, int trap_function_index, int trap_pc,
            const FrameState& current_frame) {
    state_ = State::TRAPPED;
    trap_reason_ = trap_reason;

    DCHECK(!activations_.empty());
    activations_.back()->SetCurrentFrame(current_frame);
    activations_.back()->SetTrapped(trap_function_index, trap_pc);
  }
  TrapReason GetTrapReason() const { return trap_reason_; }

  void Unwinding() { state_ = State::EH_UNWINDING; }

  inline WasmInterpreterThread::Activation* StartActivation(
      WasmInterpreterRuntime* wasm_runtime, Address frame_pointer,
      uint8_t* interpreter_fp, const FrameState& frame_state);
  inline void FinishActivation();
  inline const FrameState* GetCurrentActivationFor(
      const WasmInterpreterRuntime* wasm_runtime) const;

  inline void SetCurrentFrame(const FrameState& frame_state) {
    DCHECK(!activations_.empty());
    activations_.back()->SetCurrentFrame(frame_state);
  }

  inline void SetCurrentActivationFrame(uint32_t* fp,
                                        uint32_t current_frame_size,
                                        uint32_t current_stack_size,
                                        uint32_t ref_stack_size) {
    DCHECK(!activations_.empty());
    activations_.back()->SetCurrentActivationFrame(
        reinterpret_cast<uint8_t*>(fp), current_frame_size, current_stack_size,
        ref_stack_size);
  }

  WasmInterpreterThread::Activation* GetActivation(
      Address frame_pointer) const {
    for (size_t i = 0; i < activations_.size(); i++) {
      if (activations_[i]->GetFramePointer() == frame_pointer) {
        return activations_[i].get();
      }
    }
    return nullptr;
  }

  uint8_t* NextFrameAddress() const {
    if (activations_.empty()) {
      return stack_mem();
    } else {
      return activations_.back()->NextFrameAddress();
    }
  }

  uint32_t NextRefStackOffset() const {
    if (activations_.empty()) {
      return 0;
    } else {
      return activations_.back()->NextRefStackOffset();
    }
  }
  const uint8_t* StackLimitAddress() const {
    return stack_mem() + current_stack_size_;
  }

  void StartExecutionTimer();
  void StopExecutionTimer();
  void TerminateExecutionTimers();

  static void SetRuntimeLastWasmError(Isolate* isolate,
                                      MessageTemplate message);
  static TrapReason GetRuntimeLastWasmError(Isolate* isolate);

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  uint32_t CurrentStackFrameStart() const {
    if (activations_.empty()) {
      return 0;
    } else {
      return activations_.back()->CurrentStackFrameStart();
    }
  }

  uint32_t CurrentStackFrameSize() const {
    if (activations_.empty()) {
      return 0;
    } else {
      return activations_.back()->CurrentStackFrameSize();
    }
  }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

  void RaiseException(Isolate* isolate, MessageTemplate message);

 private:
  void Finish() { state_ = State::FINISHED; }

  inline uint8_t* stack_mem() const {
    return reinterpret_cast<uint8_t*>(stack_mem_);
  }

  static WasmInterpreterThreadMap* thread_interpreter_map_s;

  Isolate* isolate_;
  State state_;
  TrapReason trap_reason_;

  static constexpr uint32_t kInitialStackSize = 1 * MB;
  static constexpr uint32_t kStackSizeIncrement = 1 * MB;
  static constexpr uint32_t kMaxStackSize = 32 * MB;
  uint32_t current_stack_size_;
  void* stack_mem_;

  std::vector<std::unique_ptr<Activation>> activations_;

  WasmExecutionTimer execution_timer_;
};

// The interpreter interface.
class V8_EXPORT_PRIVATE WasmInterpreter {
 public:
  // The main storage for interpreter code. It maps {WasmFunction} to the
  // metadata needed to execute each function.
  class CodeMap {
   public:
    CodeMap(Isolate* isolate, const WasmModule* module,
            const uint8_t* module_start, Zone* zone);

    const WasmModule* module() const { return module_; }

    inline InterpreterCode* GetCode(uint32_t function_index);

    inline WasmBytecode* GetFunctionBytecode(uint32_t func_index);

    inline void AddFunction(const WasmFunction* function,
                            const uint8_t* code_start, const uint8_t* code_end);

    void SetFunctionCode(const WasmFunction* function, const uint8_t* start,
                         const uint8_t* end);

    size_t TotalBytecodeSize() {
      return generated_code_size_.load(std::memory_order_relaxed);
    }

   private:
    void Preprocess(uint32_t function_index);

    Zone* zone_;
    Isolate* isolate_;
    const WasmModule* module_;
    ZoneVector<InterpreterCode> interpreter_code_;

    base::TimeDelta bytecode_generation_time_;
    std::atomic<size_t> generated_code_size_;
  };

  WasmInterpreter(Isolate* isolate, const WasmModule* module,
                  const ModuleWireBytes& wire_bytes,
                  Handle<WasmInstanceObject> instance);

  static void InitializeOncePerProcess();
  static void GlobalTearDown();
  static void NotifyIsolateDisposal(Isolate* isolate);

  inline void BeginExecution(WasmInterpreterThread* thread,
                             uint32_t function_index, Address frame_pointer,
                             uint8_t* interpreter_fp, uint32_t ref_stack_offset,
                             const std::vector<WasmValue>& argument_values);
  inline void BeginExecution(WasmInterpreterThread* thread,
                             uint32_t function_index, Address frame_pointer,
                             uint8_t* interpreter_fp);

  WasmInterpreterThread::State ContinueExecution(WasmInterpreterThread* thread,
                                                 bool called_from_js);

  inline WasmValue GetReturnValue(int index) const;

  inline std::vector<WasmInterpreterStackEntry> GetInterpretedStack(
      Address frame_pointer);

  inline int GetFunctionIndex(Address frame_pointer, int index) const;

  inline void SetTrapFunctionIndex(int32_t func_index);

  inline WasmInterpreterRuntime* GetWasmRuntime() {
    return wasm_runtime_.get();
  }

 private:
  // This {Zone} has the lifespan of this {WasmInterpreter}, which should
  // have the lifespan of the corresponding {WasmInstanceObject}.
  // The zone is used to allocate the {module_bytes_} vector below and the
  // {InterpreterCode} vector in the {CodeMap}. It is also passed to
  // {WasmDecoder} used to parse the 'locals' in a Wasm function.
  Zone zone_;
  Handle<WasmInstanceObject> instance_object_;

  // Create a copy of the module bytes for the interpreter, since the passed
  // pointer might be invalidated after constructing the interpreter.
  const ZoneVector<uint8_t> module_bytes_;

  CodeMap codemap_;

  // DrumBrake
  std::shared_ptr<WasmInterpreterRuntime> wasm_runtime_;

  WasmInterpreter(const WasmInterpreter&) = delete;
  WasmInterpreter& operator=(const WasmInterpreter&) = delete;
};

typedef InstrHandlerRetType(VECTORCALL PWasmOp)(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0);
#ifdef __clang__
#define MUSTTAIL [[clang::musttail]]
#else
#define MUSTTAIL
#endif  // __clang__

extern PWasmOp* kInstructionTable[];

// {OperatorMode}s are used for the
// v8_flags.drumbrake_register_optimization. The prototype of instruction
// handlers contains two arguments int64_t r0 and double fp0 that can be used to
// pass in an integer or floating-point register the values that is at the top
// of the Wasm execution stack.
//
// For this reasons, whenever possible we define four different versions of each
// instruction handler, all identified by the following prefixes:
//
// - r2r_*: Wasm instruction handlers called when the stack top value is in a
//          register and that put the result in a register.
// - r2s_*: Wasm instruction handlers called when the stack top value is in a
//          register and that push the result on the stack.
// - s2r_*: Wasm instruction handlers called when the stack top value is not in
//          a register and that put the result in a register.
// - s2s_*: Wasm instruction handlers called when the stack top value is not in
//          a register and that push the result on the stack.
//
enum OperatorMode { kR2R = 0, kR2S, kS2R, kS2S };
static const size_t kOperatorModeCount = 4;

// {RegMode} and {RegModeTransform} specifies how an instruction handler can
// leverage the --drumbrake-register-optimization.
//
// {RegModeTransform} defines a pair of {RegMode}s, that specify whether an
// instruction handler can take its input or provide its output from the stack
// or from registers.
//
// For example:
//    {kF32Reg, kI32Reg},  // 0x5b F32Eq
// declares that the F32Eq instruction handler can read the stack top value from
// a floating point register as a F32 and pass the result to the next handler in
// an integer register as an I32, so saving one stack pop and one stack push
// operations.
enum class RegMode {
  kNoReg,  // The instruction handler only gets inputs from stack slots or
           // provide the result into a stack slot.

  kI32Reg,  // The instruction handler can be optimized to work with the integer
  kI64Reg,  // register 'r0'.

  kF32Reg,  // The instruction handler can be optimized to work with the
  kF64Reg,  // floating point register 'fp0'.

  kAnyReg,  // The instruction handler can be optimized to work either with the
            // integer or fp register; the specific register depends on the
            // type of the type of the value at the top of the stack. This is
            // used for instructions like 'drop', 'select' and 'local.set.
};

inline RegMode GetRegMode(ValueKind kind) {
  switch (kind) {
    case kI32:
      return RegMode::kI32Reg;
    case kI64:
      return RegMode::kI64Reg;
    case kF32:
      return RegMode::kF32Reg;
    case kF64:
      return RegMode::kF64Reg;
    default:
      UNREACHABLE();
  }
}

struct RegModeTransform {
  RegMode from;
  RegMode to;
};

static const RegModeTransform kRegModes[256] = {
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x00 Unreachable
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x01 Nop
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x02 Block
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x03 Loop
    {RegMode::kI32Reg, RegMode::kNoReg},  // 0x04 If
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x05 Else
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x06 Try - eh_prototype
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x07 Catch - eh_prototype
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x08 Throw - eh_prototype
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x09 Rethrow - eh_prototype
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x0a (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x0b End
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x0c Br
    {RegMode::kI32Reg, RegMode::kNoReg},  // 0x0d BrIf
    {RegMode::kI32Reg, RegMode::kNoReg},  // 0x0e BrTable
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x0f Return
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x10 CallFunction
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x11 CallIndirect
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x12 ReturnCall
    {RegMode::kNoReg, RegMode::kNoReg},   // 0x13 ReturnCallIndirect

    {RegMode::kNoReg,
     RegMode::kNoReg},  // 0x14 CallRef - typed_funcref prototype - NOTIMPL
    {RegMode::kNoReg, RegMode::kNoReg},  // 0x15 ReturnCallRef - typed_funcref
                                         // prototype - NOTIMPL
    {RegMode::kNoReg, RegMode::kNoReg},  // 0x16 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0x17 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0x18 Delegate - eh_prototype
    {RegMode::kNoReg, RegMode::kNoReg},  // 0x19 CatchAll - eh_prototype

    {RegMode::kAnyReg, RegMode::kNoReg},   // 0x1a Drop
    {RegMode::kI32Reg, RegMode::kAnyReg},  // 0x1b Select
    {RegMode::kI32Reg, RegMode::kAnyReg},  // 0x1c SelectWithType

    {RegMode::kNoReg, RegMode::kNoReg},    // 0x1d (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},    // 0x1e (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},    // 0x1f (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},    // 0x20 LocalGet
    {RegMode::kAnyReg, RegMode::kNoReg},   // 0x21 LocalSet
    {RegMode::kNoReg, RegMode::kNoReg},    // 0x22 LocalTee
    {RegMode::kNoReg, RegMode::kAnyReg},   // 0x23 GlobalGet
    {RegMode::kAnyReg, RegMode::kNoReg},   // 0x24 GlobalSet
    {RegMode::kNoReg, RegMode::kNoReg},    // 0x25 TableGet
    {RegMode::kNoReg, RegMode::kNoReg},    // 0x26 TableSet
    {RegMode::kNoReg, RegMode::kNoReg},    // 0x27 (reserved)
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x28 I32LoadMem
    {RegMode::kI32Reg, RegMode::kI64Reg},  // 0x29 I64LoadMem
    {RegMode::kI32Reg, RegMode::kF32Reg},  // 0x2a F32LoadMem
    {RegMode::kI32Reg, RegMode::kF64Reg},  // 0x2b F64LoadMem
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x2c I32LoadMem8S
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x2d I32LoadMem8U
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x2e I32LoadMem16S
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x2f I32LoadMem16U
    {RegMode::kI32Reg, RegMode::kI64Reg},  // 0x30 I64LoadMem8S
    {RegMode::kI32Reg, RegMode::kI64Reg},  // 0x31 I64LoadMem8U
    {RegMode::kI32Reg, RegMode::kI64Reg},  // 0x32 I64LoadMem16S
    {RegMode::kI32Reg, RegMode::kI64Reg},  // 0x33 I64LoadMem16U
    {RegMode::kI32Reg, RegMode::kI64Reg},  // 0x34 I64LoadMem32S
    {RegMode::kI32Reg, RegMode::kI64Reg},  // 0x35 I64LoadMem32U
    {RegMode::kI32Reg, RegMode::kNoReg},   // 0x36 I32StoreMem
    {RegMode::kI64Reg, RegMode::kNoReg},   // 0x37 I64StoreMem
    {RegMode::kF32Reg, RegMode::kNoReg},   // 0x38 F32StoreMem
    {RegMode::kF64Reg, RegMode::kNoReg},   // 0x39 F64StoreMem
    {RegMode::kI32Reg, RegMode::kNoReg},   // 0x3a I32StoreMem8
    {RegMode::kI32Reg, RegMode::kNoReg},   // 0x3b I32StoreMem16
    {RegMode::kI64Reg, RegMode::kNoReg},   // 0x3c I64StoreMem8
    {RegMode::kI64Reg, RegMode::kNoReg},   // 0x3d I64StoreMem16
    {RegMode::kI64Reg, RegMode::kNoReg},   // 0x3e I64StoreMem32
    {RegMode::kNoReg, RegMode::kNoReg},    // 0x3f MemorySize
    {RegMode::kNoReg, RegMode::kNoReg},    // 0x40 MemoryGrow

    {RegMode::kNoReg, RegMode::kNoReg},  // 0x41 I32Const
    {RegMode::kNoReg, RegMode::kNoReg},  // 0x42 I64Const
    {RegMode::kNoReg, RegMode::kNoReg},  // 0x43 F32Const
    {RegMode::kNoReg, RegMode::kNoReg},  // 0x44 F64Const

    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x45 I32Eqz
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x46 I32Eq
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x47 I32Ne
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x48 I32LtS
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x49 I32LtU
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x4a I32GtS
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x4b I32GtU
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x4c I32LeS
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x4d I32LeU
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x4e I32GeS
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x4f I32GeU
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x50 I64Eqz
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x51 I64Eq
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x52 I64Ne
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x53 I64LtS
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x54 I64LtU
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x55 I64GtS
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x56 I64GtU
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x57 I64LeS
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x58 I64LeU
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x59 I64GeS
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x5a I64GeU
    {RegMode::kF32Reg, RegMode::kI32Reg},  // 0x5b F32Eq
    {RegMode::kF32Reg, RegMode::kI32Reg},  // 0x5c F32Ne
    {RegMode::kF32Reg, RegMode::kI32Reg},  // 0x5d F32Lt
    {RegMode::kF32Reg, RegMode::kI32Reg},  // 0x5e F32Gt
    {RegMode::kF32Reg, RegMode::kI32Reg},  // 0x5f F32Le
    {RegMode::kF32Reg, RegMode::kI32Reg},  // 0x60 F32Ge
    {RegMode::kF64Reg, RegMode::kI32Reg},  // 0x61 F64Eq
    {RegMode::kF64Reg, RegMode::kI32Reg},  // 0x62 F64Ne
    {RegMode::kF64Reg, RegMode::kI32Reg},  // 0x63 F64Lt
    {RegMode::kF64Reg, RegMode::kI32Reg},  // 0x64 F64Gt
    {RegMode::kF64Reg, RegMode::kI32Reg},  // 0x65 F64Le
    {RegMode::kF64Reg, RegMode::kI32Reg},  // 0x66 F64Ge

    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x67 I32Clz
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x68 I32Ctz
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x69 I32Popcnt
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x6a I32Add
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x6b I32Sub
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x6c I32Mul
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x6d I32DivS
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x6e I32DivU
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x6f I32RemS
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x70 I32RemU
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x71 I32And
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x72 I32Ior
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x73 I32Xor
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x74 I32Shl
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x75 I32ShrS
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x76 I32ShrU
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x77 I32Rol
    {RegMode::kI32Reg, RegMode::kI32Reg},  // 0x78 I32Ror

    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x79 I64Clz
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x7a I64Ctz
    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0x7b I64Popcnt
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x7c I64Add
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x7d I64Sub
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x7e I64Mul
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x7f I64DivS
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x80 I64DivU
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x81 I64RemS
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x82 I64RemU
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x83 I64And
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x84 I64Ior
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x85 I64Xor
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x86 I64Shl
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x87 I64ShrS
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x88 I64ShrU
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x89 I64Rol
    {RegMode::kI64Reg, RegMode::kI64Reg},  // 0x8a I64Ror

    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x8b F32Abs
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x8c F32Neg
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x8d F32Ceil
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x8e F32Floor
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x8f F32Trunc
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x90 F32NearestInt
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x91 F32Sqrt
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x92 F32Add
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x93 F32Sub
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x94 F32Mul
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x95 F32Div
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x96 F32Min
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x97 F32Max
    {RegMode::kF32Reg, RegMode::kF32Reg},  // 0x98 F32CopySign

    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0x99 F64Abs
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0x9a F64Neg
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0x9b F64Ceil
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0x9c F64Floor
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0x9d F64Trunc
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0x9e F64NearestInt
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0x9f F64Sqrt
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0xa0 F64Add
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0xa1 F64Sub
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0xa2 F64Mul
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0xa3 F64Div
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0xa4 F64Min
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0xa5 F64Max
    {RegMode::kF64Reg, RegMode::kF64Reg},  // 0xa6 F64CopySign

    {RegMode::kI64Reg, RegMode::kI32Reg},  // 0xa7 I32ConvertI64
    {RegMode::kF32Reg, RegMode::kI32Reg},  // 0xa8 I32SConvertF32
    {RegMode::kF32Reg, RegMode::kI32Reg},  // 0xa9 I32UConvertF32
    {RegMode::kF64Reg, RegMode::kI32Reg},  // 0xaa I32SConvertF64
    {RegMode::kF64Reg, RegMode::kI32Reg},  // 0xab I32UConvertF64
    {RegMode::kI32Reg, RegMode::kI64Reg},  // 0xac I64SConvertI32
    {RegMode::kI32Reg, RegMode::kI64Reg},  // 0xad I64UConvertI32
    {RegMode::kF32Reg, RegMode::kI64Reg},  // 0xae I64SConvertF32
    {RegMode::kF32Reg, RegMode::kI64Reg},  // 0xaf I64UConvertF32
    {RegMode::kF64Reg, RegMode::kI64Reg},  // 0xb0 I64SConvertF64
    {RegMode::kF64Reg, RegMode::kI64Reg},  // 0xb1 I64UConvertF64
    {RegMode::kI32Reg, RegMode::kF32Reg},  // 0xb2 F32SConvertI32
    {RegMode::kI32Reg, RegMode::kF32Reg},  // 0xb3 F32UConvertI32
    {RegMode::kI64Reg, RegMode::kF32Reg},  // 0xb4 F32SConvertI64
    {RegMode::kI64Reg, RegMode::kF32Reg},  // 0xb5 F32UConvertI64
    {RegMode::kF64Reg, RegMode::kF32Reg},  // 0xb6 F32ConvertF64
    {RegMode::kI32Reg, RegMode::kF64Reg},  // 0xb7 F64SConvertI32
    {RegMode::kI32Reg, RegMode::kF64Reg},  // 0xb8 F64UConvertI32
    {RegMode::kI64Reg, RegMode::kF64Reg},  // 0xb9 F64SConvertI64
    {RegMode::kI64Reg, RegMode::kF64Reg},  // 0xba F64UConvertI64
    {RegMode::kF32Reg, RegMode::kF64Reg},  // 0xbb F64ConvertF32
    {RegMode::kF32Reg, RegMode::kI32Reg},  // 0xbc I32ReinterpretF32
    {RegMode::kF64Reg, RegMode::kI64Reg},  // 0xbd I64ReinterpretF64
    {RegMode::kI32Reg, RegMode::kF32Reg},  // 0xbe F32ReinterpretI32
    {RegMode::kI64Reg, RegMode::kF64Reg},  // 0xbf F64ReinterpretI64

    {RegMode::kNoReg, RegMode::kNoReg},  // 0xc0 I32SExtendI8
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xc1 I32SExtendI16
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xc2 I64SExtendI8
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xc3 I64SExtendI16
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xc4 I64SExtendI32

    {RegMode::kNoReg, RegMode::kNoReg},  // 0xc5 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xc6 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xc7 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xc8 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xc9 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xca (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xcb (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xcc (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xcd (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xce (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xcf (reserved)

    {RegMode::kNoReg, RegMode::kNoReg},  // 0xd0 RefNull - ref
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xd1 RefIsNull - ref
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xd2 RefFunc - ref
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xd3 RefEq - ref
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xd4 RefAsNonNull
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xd5 BrOnNull
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xd6 BrOnNonNull
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xd7 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xd8 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xd9 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xda (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xdb (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xdc (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xdd (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xde (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xdf (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xe0 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xe1 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xe2 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xe3 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xe4 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xe5 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xe6 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xe7 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xe8 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xe9 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xea (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xeb (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xec (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xed (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xee (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xef (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xf0 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xf1 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xf2 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xf3 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xf4 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xf5 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xf6 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xf7 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xf8 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xf9 (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xfa (reserved)
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xfb - GC prefix
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xfc - Numeric prefix
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xfd - Simd prefix
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xfe - Atomic prefix
    {RegMode::kNoReg, RegMode::kNoReg},  // 0xff (reserved)
};

static const size_t kSlotSize = sizeof(int32_t);
static const ptrdiff_t kCodeOffsetSize = sizeof(int32_t);

enum ExternalCallResult {
  // The function was executed and returned normally.
  EXTERNAL_RETURNED,
  // The function was executed, threw an exception.
  EXTERNAL_EXCEPTION
};

struct BranchOnCastData {
  uint32_t label_depth;
  uint32_t src_is_null : 1;   //  BrOnCastFlags
  uint32_t res_is_null : 1;   //  BrOnCastFlags
  uint32_t target_type : 30;  //  HeapType
};

struct WasmInstruction {
  union Optional {
    uint32_t index;  // global/local/label/memory/table index
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
    uint64_t offset;
    uint32_t depth;
    struct IndirectCall {
      uint32_t table_index;
      uint32_t sig_index;
    } indirect_call;
    struct BrTable {
      uint32_t table_count;
      uint32_t labels_index;
    } br_table;
    struct Block {
      uint32_t sig_index;
      uint32_t value_type_bitfield;  // return type or kVoid if no return type
                                     // or kBottom if sig_index is valid.
      constexpr ValueType value_type() const {
        return ValueType::FromRawBitField(value_type_bitfield);
      }
    } block;
    struct TableInit {
      uint32_t table_index;
      uint32_t element_segment_index;
    } table_init;
    struct TableCopy {
      uint32_t dst_table_index;
      uint32_t src_table_index;
    } table_copy;
    uint8_t simd_lane : 4;
    struct SimdLaneLoad {
      uint8_t lane : 4;
      uint8_t : 0;
      uint64_t offset : 48;
    } simd_loadstore_lane;
    struct GC_FieldImmediate {
      uint32_t struct_index;
      uint32_t field_index;
    } gc_field_immediate;
    struct GC_MemoryImmediate {
      uint32_t memory_index;
      uint32_t length;
    } gc_memory_immediate;
    struct GC_HeapTypeImmediate {
      uint32_t length;
      HeapType::Representation type_representation;
      constexpr HeapType type() const { return HeapType(type_representation); }
    } gc_heap_type_immediate;
    struct GC_ArrayNewFixed {
      uint32_t array_index;
      uint32_t length;
    } gc_array_new_fixed;
    struct GC_ArrayNewOrInitData {
      uint32_t array_index;
      uint32_t data_index;
    } gc_array_new_or_init_data;
    struct GC_ArrayCopy {
      uint32_t dest_array_index;
      uint32_t src_array_index;
    } gc_array_copy;
    BranchOnCastData br_on_cast_data;
    size_t simd_immediate_index;
    HeapType::Representation ref_type;
  };

  WasmInstruction()
      : orig(0x00), opcode(kExprUnreachable), length(0), pc(0), optional({}) {}
  WasmInstruction(uint8_t orig, WasmOpcode opcode, int length, uint32_t pc,
                  Optional optional)
      : orig(orig),
        opcode(opcode),
        length(length),
        pc(pc),
        optional(optional) {}

  operator bool() const { return length > 0; }

  RegMode InputRegMode() const { return kRegModes[orig].from; }
  bool SupportsToRegister() const {
    return kRegModes[orig].to != RegMode::kNoReg;
  }
  uint8_t orig;
  WasmOpcode opcode;
  uint32_t length;
  uint32_t pc;
  Optional optional;
};

struct Slot {
  ValueType value_type;
  uint32_t slot_offset;
  uint32_t ref_stack_index;

  constexpr ValueKind kind() const { return value_type.kind(); }
};

template <typename T>
INSTRUCTION_HANDLER_FUNC trace_PushSlot(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0);

template <typename T>
static inline ValueType value_type() {
  UNREACHABLE();
}
template <>
inline ValueType value_type<int32_t>() {
  return kWasmI32;
}
template <>
inline ValueType value_type<uint32_t>() {
  return kWasmI32;
}
template <>
inline ValueType value_type<int64_t>() {
  return kWasmI64;
}
template <>
inline ValueType value_type<uint64_t>() {
  return kWasmI64;
}
template <>
inline ValueType value_type<float>() {
  return kWasmF32;
}
template <>
inline ValueType value_type<double>() {
  return kWasmF64;
}
template <>
inline ValueType value_type<Simd128>() {
  return kWasmS128;
}
template <>
inline ValueType value_type<WasmRef>() {
  return kWasmAnyRef;  // TODO(paolosev@microsoft.com)
}

static constexpr uint32_t kInstructionTableSize = 2048;
static constexpr uint32_t kInstructionTableMask = kInstructionTableSize - 1;

#define DEFINE_INSTR_HANDLER(name) k_##name,
enum InstructionHandler : uint16_t {
  FOREACH_INSTR_HANDLER(DEFINE_INSTR_HANDLER)
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
      FOREACH_TRACE_INSTR_HANDLER(DEFINE_INSTR_HANDLER)
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
          kInstructionCount
};
#undef DEFINE_INSTR_HANDLER

inline InstructionHandler ReadFnId(const uint8_t*& code) {
  InstructionHandler result = base::ReadUnalignedValue<InstructionHandler>(
      reinterpret_cast<Address>(code));
  code += sizeof(InstructionHandler);
  return result;
}

extern PWasmOp* s_unwind_func_addr;
extern InstructionHandler s_unwind_code;

class WasmEHData {
 public:
  static const int kCatchAllTagIndex = -1;

  // Zero is always the id of a function main block, so it cannot identify a
  // try block.
  static const int kDelegateToCallerIndex = 0;

  typedef int BlockIndex;

  struct CatchHandler {
    BlockIndex catch_block_index;
    int tag_index;
    CodeOffset code_offset;
  };

  struct TryBlock {
    TryBlock(BlockIndex parent_or_matching_try_block,
             BlockIndex ancestor_try_index)
        : ancestor_try_index(ancestor_try_index),
          parent_or_matching_try_block(parent_or_matching_try_block),
          delegate_try_index(-1),
          end_instruction_code_offset(0) {}

    void SetDelegated(BlockIndex delegate_try_index) {
      this->delegate_try_index = delegate_try_index;
    }
    bool IsTryDelegate() const { return delegate_try_index >= 0; }

    // The index of the first TryBlock that is a direct ancestor of this
    // TryBlock.
    BlockIndex ancestor_try_index;

    // If this TryBlock is contained in a CatchBlock, this is the matching
    // TryBlock index of the CatchBlock. Otherwise it matches
    // ancestor_try_index.
    BlockIndex parent_or_matching_try_block;

    BlockIndex delegate_try_index;
    std::vector<CatchHandler> catch_handlers;
    size_t end_instruction_code_offset;
  };

  struct CatchBlock {
    BlockIndex try_block_index;
    uint32_t first_param_slot_offset;
    uint32_t first_param_ref_stack_index;
  };

  const TryBlock* GetTryBlock(CodeOffset code_offset) const;
  const TryBlock* GetParentTryBlock(const TryBlock* try_block) const;
  const TryBlock* GetDelegateTryBlock(const TryBlock* try_block) const;

  size_t GetEndInstructionOffsetFor(BlockIndex catch_block_index) const;

  struct ExceptionPayloadSlotOffsets {
    uint32_t first_param_slot_offset;
    uint32_t first_param_ref_stack_index;
  };
  ExceptionPayloadSlotOffsets GetExceptionPayloadStartSlotOffsets(
      BlockIndex catch_block_index) const;

  void SetCaughtException(Isolate* isolate, BlockIndex catch_block_index,
                          Handle<Object> exception);
  Handle<Object> GetCaughtException(Isolate* isolate,
                                    BlockIndex catch_block_index) const;

 protected:
  BlockIndex GetTryBranchOf(BlockIndex catch_block_index) const;

  std::unordered_map<CodeOffset, BlockIndex> code_trycatch_map_;
  std::unordered_map<BlockIndex, TryBlock> try_blocks_;
  std::unordered_map<BlockIndex, CatchBlock> catch_blocks_;
};

class WasmEHDataGenerator : public WasmEHData {
 public:
  WasmEHDataGenerator() : current_try_block_index_(-1) {}

  void AddTryBlock(BlockIndex try_block_index,
                   BlockIndex parent_or_matching_try_block_index,
                   BlockIndex ancestor_try_block_index);
  void AddCatchBlock(BlockIndex catch_block_index, int tag_index,
                     uint32_t first_param_slot_offset,
                     uint32_t first_param_ref_stack_index,
                     CodeOffset code_offset);
  void AddDelegatedBlock(BlockIndex delegated_try_block_index);
  BlockIndex EndTryCatchBlocks(BlockIndex block_index, CodeOffset code_offset);
  void RecordPotentialExceptionThrowingInstruction(WasmOpcode opcode,
                                                   CodeOffset code_offset);

  BlockIndex GetCurrentTryBlockIndex() const {
    return current_try_block_index_;
  }

 private:
  BlockIndex current_try_block_index_;
};

class WasmBytecode {
 public:
  WasmBytecode(int func_index, const uint8_t* code_data, size_t code_length,
               uint32_t stack_frame_size, const FunctionSig* signature,
               const InterpreterCode* interpreter_code, size_t blocks_count,
               const uint8_t* const_slots_data, size_t const_slots_length,
               uint32_t ref_slots_count, const WasmEHData&& eh_data,
               const std::map<CodeOffset, pc_t>&& code_pc_map);

  inline const uint8_t* GetCode() const { return code_bytes_; }
  inline size_t GetCodeSize() const { return code_.size(); }

  inline bool InitializeSlots(uint8_t* sp, size_t stack_space) const;

  pc_t GetPcFromTrapCode(const uint8_t* current_code) const;

  inline int GetFunctionIndex() const { return func_index_; }

  inline uint32_t GetBlocksCount() const { return blocks_count_; }

  inline const FunctionSig* GetFunctionSignature() const { return signature_; }
  inline ValueType return_type(size_t index) const;
  inline ValueType arg_type(size_t index) const;
  inline ValueType local_type(size_t index) const;

  inline uint32_t args_count() const { return args_count_; }
  inline uint32_t args_slots_size() const { return args_slots_size_; }
  inline uint32_t return_count() const { return return_count_; }
  inline uint32_t rets_slots_size() const { return rets_slots_size_; }
  inline uint32_t locals_count() const { return locals_count_; }
  inline uint32_t locals_slots_size() const { return locals_slots_size_; }
  inline uint32_t const_slots_size_in_bytes() const {
    return static_cast<uint32_t>(const_slots_values_.size());
  }

  inline uint32_t ref_args_count() const { return ref_args_count_; }
  inline uint32_t ref_rets_count() const { return ref_rets_count_; }
  inline uint32_t ref_locals_count() const { return ref_locals_count_; }
  inline uint32_t ref_slots_count() const { return ref_slots_count_; }
  inline uint32_t internal_ref_slots_count() const {
    // Ref slots for arguments and return value are allocated by the caller and
    // not counted in internal_ref_slots_count().
    return ref_slots_count_ - ref_rets_count_ - ref_args_count_;
  }

  inline uint32_t frame_size() { return total_frame_size_in_bytes_; }

  static inline uint32_t ArgsSizeInSlots(const FunctionSig* sig);
  static inline uint32_t RetsSizeInSlots(const FunctionSig* sig);
  static inline uint32_t RefArgsCount(const FunctionSig* sig);
  static inline uint32_t RefRetsCount(const FunctionSig* sig);
  static inline bool ContainsSimd(const FunctionSig* sig);
  static inline bool HasRefOrSimdArgs(const FunctionSig* sig);
  static inline uint32_t JSToWasmWrapperPackedArraySize(const FunctionSig* sig);
  static inline uint32_t RefLocalsCount(const InterpreterCode* wasm_code);
  static inline uint32_t LocalsSizeInSlots(const InterpreterCode* wasm_code);

  const WasmEHData::TryBlock* GetTryBlock(CodeOffset code_offset) const {
    return eh_data_.GetTryBlock(code_offset);
  }
  const WasmEHData::TryBlock* GetParentTryBlock(
      const WasmEHData::TryBlock* try_block) const {
    return eh_data_.GetParentTryBlock(try_block);
  }
  WasmEHData::ExceptionPayloadSlotOffsets GetExceptionPayloadStartSlotOffsets(
      WasmEHData::BlockIndex catch_block_index) const {
    return eh_data_.GetExceptionPayloadStartSlotOffsets(catch_block_index);
  }
  Handle<Object> GetCaughtException(Isolate* isolate,
                                    uint32_t catch_block_index) const {
    return eh_data_.GetCaughtException(isolate, catch_block_index);
  }

 private:
  std::vector<uint8_t> code_;
  const uint8_t* code_bytes_;
  const FunctionSig* signature_;
  const InterpreterCode* interpreter_code_;
  std::vector<uint8_t> const_slots_values_;

  int func_index_;
  uint32_t blocks_count_;
  uint32_t args_count_;
  uint32_t args_slots_size_;
  uint32_t return_count_;
  uint32_t rets_slots_size_;
  uint32_t locals_count_;
  uint32_t locals_slots_size_;
  uint32_t total_frame_size_in_bytes_;
  uint32_t ref_args_count_;
  uint32_t ref_rets_count_;
  uint32_t ref_locals_count_;
  uint32_t ref_slots_count_;

  WasmEHData eh_data_;

  // TODO(paolosev@microsoft.com) slow! Use std::unordered_map ?
  std::map<CodeOffset, pc_t> code_pc_map_;
};

class WasmBytecodeGenerator {
 public:
  WasmBytecodeGenerator(uint32_t function_index, InterpreterCode* wasm_code,
                        const WasmModule* module);

  std::unique_ptr<WasmBytecode> GenerateBytecode();

 private:
  struct BlockData {
    BlockData(WasmOpcode opcode, uint32_t begin_code_offset,
              int32_t parent_block_index, uint32_t stack_size,
              WasmInstruction::Optional::Block signature,
              uint32_t first_block_index, uint32_t rets_slots_count,
              uint32_t params_slots_count, int32_t parent_try_block_index)
        : opcode_(opcode),
          stack_size_(stack_size),
          begin_code_offset_(begin_code_offset),
          end_code_offset_(0),
          parent_block_index_(parent_block_index),
          if_else_block_index_(-1),
          signature_(signature),
          first_block_index_(first_block_index),
          rets_slots_count_(rets_slots_count),
          params_slots_count_(params_slots_count),
          parent_try_block_index_(parent_try_block_index),
          is_unreachable_(false) {}

    bool IsRootBlock() const { return parent_block_index_ < 0; }
    bool IsBlock() const { return opcode_ == kExprBlock; }
    bool IsLoop() const { return opcode_ == kExprLoop; }
    bool IsIf() const { return opcode_ == kExprIf; }
    bool IsElse() const { return opcode_ == kExprElse; }
    bool HasElseBranch() const { return if_else_block_index_ > 0; }
    bool IsTry() const { return opcode_ == kExprTry; }
    bool IsCatch() const { return opcode_ == kExprCatch; }
    bool IsCatchAll() const { return opcode_ == kExprCatchAll; }

    void SaveParams(uint32_t* from, size_t params_count) {
      DCHECK(IsIf());
      if_block_params_ = base::SmallVector<uint32_t, 4>(params_count);
      for (size_t i = 0; i < params_count; i++) {
        if_block_params_[i] = from[i];
      }
    }
    uint32_t GetParam(size_t index) const {
      DCHECK(IsIf());
      DCHECK_LE(index, if_block_params_.size());
      return if_block_params_[index];
    }

    WasmOpcode opcode_;
    uint32_t stack_size_;
    uint32_t begin_code_offset_;
    uint32_t end_code_offset_;
    int32_t parent_block_index_;
    int32_t if_else_block_index_;
    base::SmallVector<uint32_t, 4> branch_code_offsets_;
    WasmInstruction::Optional::Block signature_;
    uint32_t first_block_index_;
    uint32_t rets_slots_count_;
    uint32_t params_slots_count_;
    int32_t parent_try_block_index_;
    bool is_unreachable_;
    base::SmallVector<uint32_t, 4> if_block_params_;
  };

  uint32_t const_slots_start() const {
    return rets_slots_size_ + args_slots_size_;
  }

  inline uint32_t GetStackFrameSize() const { return slot_offset_; }

  uint32_t CurrentCodePos() const {
    return static_cast<uint32_t>(code_.size());
  }

  WasmInstruction DecodeInstruction(pc_t pc, Decoder& decoder);
  void DecodeGCOp(WasmOpcode opcode, WasmInstruction::Optional* optional,
                  Decoder* decoder, InterpreterCode* code, pc_t pc,
                  int* const len);
  void DecodeNumericOp(WasmOpcode opcode, WasmInstruction::Optional* optional,
                       Decoder* decoder, InterpreterCode* code, pc_t pc,
                       int* const len);
  void DecodeAtomicOp(WasmOpcode opcode, WasmInstruction::Optional* optional,
                      Decoder* decoder, InterpreterCode* code, pc_t pc,
                      int* const len);
  bool DecodeSimdOp(WasmOpcode opcode, WasmInstruction::Optional* optional,
                    Decoder* decoder, InterpreterCode* code, pc_t pc,
                    int* const len);

  inline bool ToRegisterIsAllowed(const WasmInstruction& instr);
  RegMode EncodeInstruction(const WasmInstruction& instr, RegMode curr_reg_mode,
                            RegMode next_reg_mode);

  bool EncodeSuperInstruction(RegMode& reg_mode,
                              const WasmInstruction& curr_instr,
                              const WasmInstruction& next_instr);

  uint32_t ScanConstInstructions() const;

  void Emit(const void* buff, size_t len) {
    code_.insert(code_.end(), static_cast<const uint8_t*>(buff),
                 static_cast<const uint8_t*>(buff) + len);
  }

  inline void I32Push(bool emit = true);
  inline void I64Push(bool emit = true);
  inline void F32Push(bool emit = true);
  inline void F64Push(bool emit = true);
  inline void S128Push(bool emit = true);
  inline void RefPush(ValueType type, bool emit = true);
  inline void Push(ValueType type);

  inline void I32Pop(bool emit = true) { Pop(kI32, emit); }
  inline void I64Pop(bool emit = true) { Pop(kI64, emit); }
  inline void F32Pop(bool emit = true) { Pop(kF32, emit); }
  inline void F64Pop(bool emit = true) { Pop(kF64, emit); }
  inline void S128Pop(bool emit = true) { Pop(kS128, emit); }

  inline ValueType RefPop(bool emit = true) {
    DCHECK(wasm::is_reference(slots_[stack_.back()].kind()));
    uint32_t ref_index = slots_[stack_.back()].ref_stack_index;
    ValueType value_type = slots_[stack_.back()].value_type;
    DCHECK(value_type.is_object_reference());
    PopSlot();
    if (emit) Emit(&ref_index, sizeof(uint32_t));
    return value_type;
  }

#ifdef DEBUG
  bool CheckEqualKind(ValueKind value_kind, ValueKind stack_slot_kind) {
    if (is_reference(value_kind)) {
      return is_reference(stack_slot_kind);
    } else if (value_kind == kI8 || value_kind == kI16) {
      return stack_slot_kind == kI32;
    } else {
      return value_kind == stack_slot_kind;
    }
  }
#endif  // DEBUG

  inline void Pop(ValueKind kind, bool emit = true) {
    if (kind == kRefNull || kind == kRef) {
      RefPop(emit);
      return;
    }
    DCHECK(CheckEqualKind(kind, slots_[stack_.back()].kind()));
    uint32_t slot_offset = PopSlot();
    if (emit) Emit(&slot_offset, sizeof(uint32_t));
  }

  void EmitI16Const(int16_t value) { Emit(&value, sizeof(value)); }
  void EmitI32Const(int32_t value) { Emit(&value, sizeof(value)); }
  void EmitI64Const(int64_t value) { Emit(&value, sizeof(value)); }
  void EmitF32Const(float value) { Emit(&value, sizeof(value)); }
  void EmitF64Const(double value) { Emit(&value, sizeof(value)); }

  inline void EmitFnId(InstructionHandler func, uint32_t pc = UINT_MAX) {
    // If possible, compacts two consecutive CopySlot32 or CopySlot64
    // instructions into a single instruction, to save one dispatch.
    if (TryCompactInstructionHandler(func)) return;

    if (pc != UINT_MAX) {
      code_pc_map_[code_.size()] = pc;
    }

    last_instr_offset_ = CurrentCodePos();

    Emit(&func, sizeof(func));
  }

  void EmitCopySlot(ValueType value_type, uint32_t from_slot_index,
                    uint32_t to_slot_index, bool copy_from_reg = false);

  inline bool IsMemory64() const;
  inline bool IsMultiMemory() const;

  inline ValueKind GetGlobalType(uint32_t index) const;
  inline void EmitGlobalIndex(uint32_t index);

  uint32_t ReserveBlockSlots(uint8_t opcode,
                             const WasmInstruction::Optional::Block& block_data,
                             size_t* rets_slots_count,
                             size_t* params_slots_count);
  void StoreBlockParamsIntoSlots(uint32_t target_block_index,
                                 bool update_stack);
  void StoreBlockParamsAndResultsIntoSlots(uint32_t target_block_index,
                                           WasmOpcode opcode);

  inline bool HasVoidSignature(
      const WasmBytecodeGenerator::BlockData& block_data) const;
  inline uint32_t ParamsCount(
      const WasmBytecodeGenerator::BlockData& block_data) const;
  inline ValueType GetParamType(
      const WasmBytecodeGenerator::BlockData& block_data, size_t index) const;
  inline uint32_t ReturnsCount(
      const WasmBytecodeGenerator::BlockData& block_data) const;
  inline ValueType GetReturnType(
      const WasmBytecodeGenerator::BlockData& block_data, size_t index) const;

  void PreserveArgsAndLocals();

  int32_t BeginBlock(WasmOpcode opcode,
                     const WasmInstruction::Optional::Block signature);
  inline void BeginElseBlock(uint32_t if_block_index, bool dummy);
  int32_t EndBlock(WasmOpcode opcode);

  void Return();
  inline void EmitBranchOffset(uint32_t delta);
  inline void EmitIfElseBranchOffset();
  inline void EmitTryCatchBranchOffset();
  inline void EmitBranchTableOffset(uint32_t delta, uint32_t code_pos);
  inline uint32_t GetCurrentBranchDepth() const;
  inline int32_t GetTargetBranch(uint32_t delta) const;
  int GetCurrentTryBlockIndex(bool return_matching_try_for_catch_blocks) const;
  void PatchBranchOffsets();
  void PatchLoopJumpInstructions();
  void RestoreIfElseParams(uint32_t if_block_index);

  bool HasSharedSlot(uint32_t stack_index) const;
  bool FindSharedSlot(uint32_t stack_index, uint32_t* new_slot_index);

  inline const FunctionSig* GetFunctionSignature(uint32_t function_index) const;

  inline ValueKind GetTopStackType(RegMode reg_mode) const;

  inline uint32_t function_index() const { return function_index_; }

  std::vector<Slot> slots_;

  inline uint32_t CreateSlot(ValueType value_type) {
    switch (value_type.kind()) {
      case kI32:
        return CreateSlot<int32_t>(value_type);
      case kI64:
        return CreateSlot<int64_t>(value_type);
      case kF32:
        return CreateSlot<float>(value_type);
      case kF64:
        return CreateSlot<double>(value_type);
      case kS128:
        return CreateSlot<Simd128>(value_type);
      case kRef:
      case kRefNull:
        return CreateSlot<WasmRef>(value_type);
      default:
        UNREACHABLE();
    }
  }

  template <typename T>
  inline uint32_t CreateSlot(ValueType value_type) {
    // A gcc bug causes "error: explicit specialization in non-namespace scope"
    // with explicit specializations here:
    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85282
    if constexpr (std::is_same_v<T, WasmRef>) {
      return CreateWasmRefSlot(value_type);
    }
    uint32_t slot_index = static_cast<uint32_t>(slots_.size());
    slots_.push_back({value_type, slot_offset_, 0});
    slot_offset_ += sizeof(T) / kSlotSize;
    return slot_index;
  }
  inline uint32_t CreateWasmRefSlot(ValueType value_type) {
    uint32_t slot_index = static_cast<uint32_t>(slots_.size());
    slots_.push_back({value_type, slot_offset_, ref_slots_count_});
    slot_offset_ += sizeof(WasmRef) / kSlotSize;
    ref_slots_count_++;
    return slot_index;
  }

  template <typename T>
  inline uint32_t GetConstSlot(T value) {
    if constexpr (std::is_same_v<T, int32_t>) {
      return GetI32ConstSlot(value);
    }
    if constexpr (std::is_same_v<T, int64_t>) {
      return GetI64ConstSlot(value);
    }
    if constexpr (std::is_same_v<T, float>) {
      return GetF32ConstSlot(value);
    }
    if constexpr (std::is_same_v<T, double>) {
      return GetF64ConstSlot(value);
    }
    if constexpr (std::is_same_v<T, Simd128>) {
      return GetS128ConstSlot(value);
    }
    UNREACHABLE();
  }
  inline uint32_t GetI32ConstSlot(int32_t value) {
    auto it = i32_const_cache_.find(value);
    if (it != i32_const_cache_.end()) {
      return it->second;
    }
    return UINT_MAX;
  }
  inline uint32_t GetI64ConstSlot(int64_t value) {
    auto it = i64_const_cache_.find(value);
    if (it != i64_const_cache_.end()) {
      return it->second;
    }
    return UINT_MAX;
  }
  inline uint32_t GetF32ConstSlot(float value) {
    auto it = f32_const_cache_.find(value);
    if (it != f32_const_cache_.end()) {
      return it->second;
    }
    return UINT_MAX;
  }
  inline uint32_t GetF64ConstSlot(double value) {
    auto it = f64_const_cache_.find(value);
    if (it != f64_const_cache_.end()) {
      return it->second;
    }
    return UINT_MAX;
  }
  inline uint32_t GetS128ConstSlot(Simd128 value) {
    auto it = s128_const_cache_.find(reinterpret_cast<Simd128&>(value));
    if (it != s128_const_cache_.end()) {
      return it->second;
    }
    return UINT_MAX;
  }

  template <typename T>
  inline uint32_t CreateConstSlot(T value) {
    if constexpr (std::is_same_v<T, WasmRef>) {
      UNREACHABLE();
    }
    uint32_t slot_index = GetConstSlot(value);
    if (slot_index == UINT_MAX) {
      uint32_t offset = const_slot_offset_ * sizeof(uint32_t);
      DCHECK_LE(offset + sizeof(T), const_slots_values_.size());

      slot_index = static_cast<uint32_t>(slots_.size());
      slots_.push_back(
          {value_type<T>(), const_slots_start() + const_slot_offset_, 0});
      base::WriteUnalignedValue<T>(
          reinterpret_cast<Address>(const_slots_values_.data() + offset),
          value);
      const_slot_offset_ += sizeof(T) / kSlotSize;
    }
    return slot_index;
  }

  template <typename T>
  inline uint32_t PushConstSlot(T value) {
    uint32_t new_slot_index = CreateConstSlot(value);
    PushConstSlot(new_slot_index);
    return new_slot_index;
  }
  inline void PushConstSlot(uint32_t slot_index);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  void TracePushConstSlot(uint32_t slot_index);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

  inline void PushSlot(uint32_t slot_index) {
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_bytecode_generator &&
        v8_flags.trace_drumbrake_execution_verbose) {
      printf("    push - slot[%d] = %d\n", stack_size(), slot_index);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    stack_.push_back(slot_index);
  }

  inline uint32_t _PushSlot(ValueType value_type) {
    PushSlot(static_cast<uint32_t>(slots_.size()));
    return CreateSlot(value_type);
  }

  inline void PushCopySlot(uint32_t from);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  void TracePushCopySlot(uint32_t from);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

  inline uint32_t PopSlot() {
    // TODO(paolosev@microsoft.com) - We should try to mark as 'invalid' and
    // later reuse slots in the stack once we are sure they won't be referred
    // again, which should be the case once a slot is popped. This could make
    // the stack frame size smaller, especially for large Wasm functions.
    uint32_t slot_offset = slots_[stack_.back()].slot_offset;

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_bytecode_generator &&
        v8_flags.trace_drumbrake_execution_verbose) {
      printf("    pop  - slot[%d] = %d\n", stack_size() - 1, stack_.back());
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    stack_.pop_back();
    return slot_offset;
  }

  void CopyToSlot(ValueType value_type, uint32_t from_slot_index,
                  uint32_t to_stack_index, bool copy_from_reg);
  void CopyToSlotAndPop(ValueType value_type, uint32_t to, bool is_tee,
                        bool copy_from_reg);

  inline void SetSlotType(uint32_t stack_index, ValueType type) {
    DCHECK_LT(stack_index, stack_.size());

    uint32_t slot_index = stack_[stack_index];
    slots_[slot_index].value_type = type;

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    TraceSetSlotType(stack_index, type);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
  }

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  void TraceSetSlotType(uint32_t stack_index, ValueType typo);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

  inline void UpdateStack(uint32_t index, uint32_t slot_index) {
    DCHECK_LT(index, stack_.size());
    stack_[index] = slot_index;
  }
  inline void UpdateStack(uint32_t index, uint32_t slot_index,
                          ValueType value_type) {
    DCHECK_LT(index, stack_.size());
    stack_[index] = slot_index;
    SetSlotType(index, value_type);
  }

  inline uint32_t stack_top_index() const {
    DCHECK(!stack_.empty());
    return static_cast<uint32_t>(stack_.size() - 1);
  }
  inline uint32_t stack_size() const {
    return static_cast<uint32_t>(stack_.size());
  }

  inline void SetUnreachableMode() {
    is_instruction_reachable_ = false;
    unreachable_block_count_ = 1;

    CHECK_GE(current_block_index_, 0);
    blocks_[current_block_index_].is_unreachable_ = true;
  }

  // Create slots for arguments and generates run-time commands to initialize
  // their values.
  void InitSlotsForFunctionArgs(const FunctionSig* sig, bool is_indirect_call);

  bool TryCompactInstructionHandler(InstructionHandler func_addr);

  bool TypeCheckAlwaysSucceeds(ValueType obj_type, HeapType type) const;
  bool TypeCheckAlwaysFails(ValueType obj_type, HeapType expected_type,
                            bool null_succeeds) const;

  std::vector<uint8_t> const_slots_values_;
  uint32_t const_slot_offset_;
  std::unordered_map<int32_t, uint32_t> i32_const_cache_;
  std::unordered_map<int64_t, uint32_t> i64_const_cache_;
  std::unordered_map<float, uint32_t> f32_const_cache_;
  std::unordered_map<double, uint32_t> f64_const_cache_;

  struct Simd128Hash {
    size_t operator()(const Simd128& s128) const;
  };
  std::unordered_map<Simd128, uint32_t, Simd128Hash> s128_const_cache_;

  std::vector<Simd128> simd_immediates_;
  uint32_t slot_offset_;  // TODO(paolosev@microsoft.com): manage holes
  std::vector<uint32_t> stack_;
  uint32_t ref_slots_count_;

  uint32_t function_index_;
  InterpreterCode* wasm_code_;
  uint32_t args_count_;
  uint32_t args_slots_size_;
  uint32_t return_count_;
  uint32_t rets_slots_size_;
  uint32_t locals_count_;

  std::vector<uint8_t> code_;

  std::vector<BlockData> blocks_;
  int32_t current_block_index_;

  bool is_instruction_reachable_;
  uint32_t unreachable_block_count_;
#ifdef DEBUG
  bool was_current_instruction_reachable_;
#endif  // DEBUG

  base::SmallVector<uint32_t, 8> br_table_labels_;
  base::SmallVector<uint32_t, 16> loop_end_code_offsets_;

  const WasmModule* module_;

  // TODO(paolosev@microsoft.com) - Using a map is relatively slow because of
  // all the insertions that cause a ~10% performance hit in the generation of
  // the interpreter bytecode. The bytecode generation time is not a huge factor
  // when we run in purely jitless mode, because it is almost always dwarfed by
  // the interpreter execution time. It could be an important factor, however,
  // if we implemented a multi-tier strategy with the interpreter as a first
  // tier. It would probably be better to replace this with a plain vector and
  // use binary search for lookups.
  std::map<CodeOffset, pc_t> code_pc_map_;

  static const CodeOffset kInvalidCodeOffset = (CodeOffset)-1;
  CodeOffset last_instr_offset_;

  WasmEHDataGenerator eh_data_;

  WasmBytecodeGenerator(const WasmBytecodeGenerator&) = delete;
  WasmBytecodeGenerator& operator=(const WasmBytecodeGenerator&) = delete;
};

// TODO(paolosev@microsoft.com) Duplicated from src/runtime/runtime-wasm.cc
class V8_NODISCARD ClearThreadInWasmScope {
 public:
  explicit ClearThreadInWasmScope(Isolate* isolate);
  ~ClearThreadInWasmScope();

 private:
  Isolate* isolate_;
};

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
class InterpreterTracer final : public Malloced {
 public:
  explicit InterpreterTracer(int isolate_id)
      : isolate_id_(isolate_id),
        file_(nullptr),
        current_chunk_index_(0),
        write_count_(0) {
    if (0 != strcmp(v8_flags.trace_drumbrake_filter.value(), "*")) {
      std::stringstream s(v8_flags.trace_drumbrake_filter.value());
      for (int i; s >> i;) {
        traced_functions_.insert(i);
        if (s.peek() == ',') s.ignore();
      }
    }

    OpenFile();
  }

  ~InterpreterTracer() { CloseFile(); }

  void OpenFile() {
    if (!ShouldRedirect()) {
      file_ = stdout;
      return;
    }

    if (isolate_id_ >= 0) {
      base::SNPrintF(filename_, "trace-%d-%d-%d.dbt",
                     base::OS::GetCurrentProcessId(), isolate_id_,
                     current_chunk_index_);
    } else {
      base::SNPrintF(filename_, "trace-%d-%d.dbt",
                     base::OS::GetCurrentProcessId(), current_chunk_index_);
    }
    WriteChars(filename_.begin(), "", 0, false);

    if (file_ == nullptr) {
      file_ = base::OS::FOpen(filename_.begin(), "w");
      CHECK_WITH_MSG(file_ != nullptr, "could not open file.");
    }
  }

  void CloseFile() {
    if (!ShouldRedirect()) {
      return;
    }

    DCHECK_NOT_NULL(file_);
    base::Fclose(file_);
    file_ = nullptr;
  }

  bool ShouldTraceFunction(int function_index) const {
    return traced_functions_.empty() ||
           traced_functions_.find(function_index) != traced_functions_.end();
  }

  void PrintF(const char* format, ...);

  void CheckFileSize() {
    if (!ShouldRedirect()) {
      return;
    }

    ::fflush(file_);
    if (++write_count_ >= kWriteCountCheckInterval) {
      write_count_ = 0;
      ::fseek(file_, 0L, SEEK_END);
      if (::ftell(file_) > kMaxFileSize) {
        CloseFile();
        current_chunk_index_ = (current_chunk_index_ + 1) % kFileChunksCount;
        OpenFile();
      }
    }
  }

  FILE* file() const { return file_; }

 private:
  static bool ShouldRedirect() { return v8_flags.redirect_drumbrake_traces; }

  int isolate_id_;
  base::EmbeddedVector<char, 128> filename_;
  FILE* file_;
  std::unordered_set<int> traced_functions_;
  int current_chunk_index_;
  int64_t write_count_;

  static const int64_t kWriteCountCheckInterval = 1000;
  static const int kFileChunksCount = 10;
  static const int64_t kMaxFileSize = 100 * MB;
};

class ShadowStack {
 public:
  void TracePop() { stack_.pop_back(); }

  void TraceSetSlotType(uint32_t index, uint32_t type) {
    if (stack_.size() <= index) stack_.resize(index + 1);
    stack_[index].type_ = ValueType::FromRawBitField(type);
  }

  template <typename T>
  void TracePush(uint32_t slot_offset) {
    stack_.push_back({value_type<T>(), slot_offset});
  }

  void TracePushCopy(uint32_t index) { stack_.push_back(stack_[index]); }

  void TraceUpdate(uint32_t stack_index, uint32_t slot_offset) {
    if (stack_.size() <= stack_index) stack_.resize(stack_index + 1);
    stack_[stack_index].slot_offset_ = slot_offset;
  }

  void Print(WasmInterpreterRuntime* wasm_runtime, const uint32_t* sp,
             size_t start_params, size_t start_locals, size_t start_stack,
             RegMode reg_mode, int64_t r0, double fp0) const;

  struct Slot {
    static void Print(WasmInterpreterRuntime* wasm_runtime, ValueType type,
                      size_t index, char kind, const uint8_t* addr);
    void Print(WasmInterpreterRuntime* wasm_runtime, size_t index, char kind,
               const uint8_t* addr) const {
      return Print(wasm_runtime, type_, index, kind, addr);
    }

    ValueType type_;
    uint32_t slot_offset_;
  };

 private:
  std::vector<Slot> stack_;
};
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_INTERPRETER_WASM_INTERPRETER_H_
