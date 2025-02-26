// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/interpreter/wasm-interpreter.h"

#include <atomic>
#include <limits>
#include <optional>
#include <type_traits>

#include "include/v8-metrics.h"
#include "src/base/overflowing-math.h"
#include "src/builtins/builtins.h"
#include "src/handles/global-handles-inl.h"
#include "src/heap/heap-write-barrier.h"
#include "src/objects/object-macros.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/wasm/decoder.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/interpreter/wasm-interpreter-inl.h"
#include "src/wasm/interpreter/wasm-interpreter-runtime-inl.h"
#include "src/wasm/object-access.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

#define EMIT_INSTR_HANDLER(name) EmitFnId(k_##name);
#define EMIT_INSTR_HANDLER_WITH_PC(name, pc) EmitFnId(k_##name, pc);

#define START_EMIT_INSTR_HANDLER()                       \
  {                                                      \
    size_t _current_code_offset = code_.size();          \
    size_t _current_slots_size = slots_.size();          \
    DCHECK(!no_nested_emit_instr_handler_guard_);        \
    no_nested_emit_instr_handler_guard_ = true;          \
    stack_.clear_history();                              \
    if (v8_flags.drumbrake_compact_bytecode) {           \
      handler_size_ = InstrHandlerSize::Small;           \
    } else {                                             \
      DCHECK_EQ(handler_size_, InstrHandlerSize::Large); \
    }                                                    \
    while (true) {                                       \
      current_instr_encoding_failed_ = false;

#define START_EMIT_INSTR_HANDLER_WITH_ID(name) \
  START_EMIT_INSTR_HANDLER()                   \
  EMIT_INSTR_HANDLER(name)

#define START_EMIT_INSTR_HANDLER_WITH_PC(name, pc) \
  START_EMIT_INSTR_HANDLER()                       \
  EMIT_INSTR_HANDLER_WITH_PC(name, pc)

#define END_EMIT_INSTR_HANDLER()                                               \
  if (v8_flags.drumbrake_compact_bytecode && current_instr_encoding_failed_) { \
    code_.resize(_current_code_offset);                                        \
    slots_.resize(_current_slots_size);                                        \
    stack_.rollback();                                                         \
    current_instr_encoding_failed_ = false;                                    \
    handler_size_ = InstrHandlerSize::Large;                                   \
    continue;                                                                  \
  }                                                                            \
  break;                                                                       \
  }                                                                            \
  DCHECK(!current_instr_encoding_failed_);                                     \
  no_nested_emit_instr_handler_guard_ = false;                                 \
  }

////////////////////////////////////////////////////////////////////////////////
// Memory64 macros

#define EMIT_MEM64_INSTR_HANDLER(name, mem64_name, is_memory64) \
  if (V8_UNLIKELY(is_memory64)) {                               \
    EMIT_INSTR_HANDLER(mem64_name);                             \
  } else {                                                      \
    EMIT_INSTR_HANDLER(name);                                   \
  }

#define EMIT_MEM64_INSTR_HANDLER_WITH_PC(name, mem64_name, is_memory64, pc) \
  if (V8_UNLIKELY(is_memory64)) {                                           \
    EMIT_INSTR_HANDLER_WITH_PC(mem64_name, pc);                             \
  } else {                                                                  \
    EMIT_INSTR_HANDLER_WITH_PC(name, pc);                                   \
  }

////////////////////////////////////////////////////////////////////////////////

WasmInterpreter::CodeMap::CodeMap(Isolate* isolate, const WasmModule* module,
                                  const uint8_t* module_start, Zone* zone)
    : zone_(zone),
      isolate_(isolate),
      module_(module),
      interpreter_code_(zone),
      bytecode_generation_time_(),
      generated_code_size_(0) {
  if (module == nullptr) return;
  interpreter_code_.reserve(module->functions.size());
  for (const WasmFunction& function : module->functions) {
    if (function.imported) {
      DCHECK(!function.code.is_set());
      AddFunction(&function, nullptr, nullptr);
    } else {
      AddFunction(&function, module_start + function.code.offset(),
                  module_start + function.code.end_offset());
    }
  }
}

void WasmInterpreter::CodeMap::Preprocess(uint32_t function_index) {
  InterpreterCode* code = &interpreter_code_[function_index];
  DCHECK_EQ(code->function->imported, code->start == nullptr);
  DCHECK(!code->bytecode && code->start);

  base::TimeTicks start_time = base::TimeTicks::Now();

  // Compute the control targets map and the local declarations.
  BytecodeIterator it(code->start, code->end, &code->locals, zone_);

  WasmBytecodeGenerator bytecode_generator(function_index, code, module_);
  code->bytecode = bytecode_generator.GenerateBytecode();

  // Generate histogram sample to measure the time spent generating the
  // bytecode. Reuse the WasmCompileModuleMicroSeconds.wasm that is currently
  // obsolete.
  if (base::TimeTicks::IsHighResolution()) {
    base::TimeDelta duration = base::TimeTicks::Now() - start_time;
    bytecode_generation_time_ += duration;
    int bytecode_generation_time_usecs =
        static_cast<int>(bytecode_generation_time_.InMicroseconds());

    // TODO(paolosev@microsoft.com) Do not add a sample for each function!
    isolate_->counters()->wasm_compile_wasm_module_time()->AddSample(
        bytecode_generation_time_usecs);
  }

  // Generate histogram sample to measure the bytecode size. Reuse the
  // V8.WasmModuleCodeSizeMiB (see {NativeModule::SampleCodeSize}).
  int prev_code_size_mb = generated_code_size_ == 0
                              ? -1
                              : static_cast<int>(generated_code_size_ / MB);
  generated_code_size_.fetch_add(code->bytecode->GetCodeSize());
  int code_size_mb = static_cast<int>(generated_code_size_ / MB);
  if (prev_code_size_mb < code_size_mb) {
    Histogram* histogram = isolate_->counters()->wasm_module_code_size_mb();
    histogram->AddSample(code_size_mb);
  }
}

// static
WasmInterpreterThreadMap* WasmInterpreterThread::thread_interpreter_map_s =
    nullptr;

WasmInterpreterThread* WasmInterpreterThreadMap::GetCurrentInterpreterThread(
    Isolate* isolate) {
  const int current_thread_id = ThreadId::Current().ToInteger();
  {
    base::SpinningMutexGuard guard(&mutex_);

    auto it = map_.find(current_thread_id);
    if (it == map_.end()) {
      map_[current_thread_id] =
          std::make_unique<WasmInterpreterThread>(isolate);
      it = map_.find(current_thread_id);
    }
    return it->second.get();
  }
}

void WasmInterpreterThreadMap::NotifyIsolateDisposal(Isolate* isolate) {
  base::SpinningMutexGuard guard(&mutex_);

  auto it = map_.begin();
  while (it != map_.end()) {
    WasmInterpreterThread* thread = it->second.get();
    if (thread->GetIsolate() == isolate) {
      thread->TerminateExecutionTimers();
      it = map_.erase(it);
    } else {
      ++it;
    }
  }
}

void FrameState::SetCaughtException(Isolate* isolate,
                                    uint32_t catch_block_index,
                                    Handle<Object> exception) {
  if (caught_exceptions_.is_null()) {
    DCHECK_NOT_NULL(current_function_);
    uint32_t blocks_count = current_function_->GetBlocksCount();
    Handle<FixedArray> caught_exceptions =
        isolate->factory()->NewFixedArrayWithHoles(blocks_count);
    caught_exceptions_ = isolate->global_handles()->Create(*caught_exceptions);
  }
  caught_exceptions_->set(catch_block_index, *exception);
}

Handle<Object> FrameState::GetCaughtException(
    Isolate* isolate, uint32_t catch_block_index) const {
  Handle<Object> exception =
      handle(caught_exceptions_->get(catch_block_index), isolate);
  DCHECK(!IsTheHole(*exception));
  return exception;
}

void FrameState::DisposeCaughtExceptionsArray(Isolate* isolate) {
  if (!caught_exceptions_.is_null()) {
    isolate->global_handles()->Destroy(caught_exceptions_.location());
    caught_exceptions_ = Handle<FixedArray>::null();
  }
}

WasmExecutionTimer::WasmExecutionTimer(Isolate* isolate,
                                       bool track_jitless_wasm)
    : execute_ratio_histogram_(
          track_jitless_wasm
              ? isolate->counters()->wasm_jitless_execution_ratio()
              : isolate->counters()->wasm_jit_execution_ratio()),
      slow_wasm_histogram_(
          track_jitless_wasm
              ? isolate->counters()->wasm_jitless_execution_too_slow()
              : isolate->counters()->wasm_jit_execution_too_slow()),
      window_has_started_(false),
      next_interval_time_(),
      start_interval_time_(),
      window_running_time_(),
      sample_duration_(base::TimeDelta::FromMilliseconds(std::max(
          0, v8_flags.wasm_exec_time_histogram_sample_duration.value()))),
      slow_threshold_(v8_flags.wasm_exec_time_histogram_slow_threshold.value()),
      slow_threshold_samples_count_(std::max(
          1, v8_flags.wasm_exec_time_slow_threshold_samples_count.value())),
      isolate_(isolate) {
  int cooldown_interval_in_msec = std::max(
      0, v8_flags.wasm_exec_time_histogram_sample_period.value() -
             v8_flags.wasm_exec_time_histogram_sample_duration.value());
  cooldown_interval_ =
      base::TimeDelta::FromMilliseconds(cooldown_interval_in_msec);
}

void WasmExecutionTimer::BeginInterval(bool start_timer) {
  window_has_started_ = true;
  start_interval_time_ = base::TimeTicks::Now();
  window_running_time_ = base::TimeDelta();
  if (start_timer) {
    window_execute_timer_.Start();
  }
}

void WasmExecutionTimer::EndInterval() {
  window_has_started_ = false;
  base::TimeTicks now = base::TimeTicks::Now();
  next_interval_time_ = now + cooldown_interval_;
  int running_ratio = kMaxPercentValue *
                      window_running_time_.TimesOf(now - start_interval_time_);
  AddSample(running_ratio);
}

void WasmExecutionTimer::AddSample(int running_ratio) {
  DCHECK(v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms);

  execute_ratio_histogram_->AddSample(running_ratio);

  // Emit a Jit[less]WasmExecutionTooSlow sample if the average of the last
  // {v8_flags.wasm_exec_time_slow_threshold_samples_count} samples is above
  // {v8_flags.wasm_exec_time_histogram_slow_threshold}.
  samples_.push_back(running_ratio);
  if (samples_.size() == slow_threshold_samples_count_) {
    int sum = 0;
    for (int sample : samples_) sum += sample;
    int average = sum / slow_threshold_samples_count_;
    if (average >= slow_threshold_) {
      slow_wasm_histogram_->AddSample(average);

      if (isolate_ && !isolate_->context().is_null()) {
        // Skip this event because not(yet) supported by Chromium.

        // HandleScope scope(isolate_);
        // v8::metrics::WasmInterpreterSlowExecution event;
        // event.slow_execution = true;
        // event.jitless = v8_flags.wasm_jitless;
        // event.cpu_percentage = average;
        // v8::metrics::Recorder::ContextId context_id =
        //     isolate_->GetOrRegisterRecorderContextId(
        //         isolate_->native_context());
        // isolate_->metrics_recorder()->DelayMainThreadEvent(event,
        // context_id);
      }
    }

    samples_.clear();
  }
}

void WasmExecutionTimer::StartInternal() {
  DCHECK(v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms);
  DCHECK(!window_execute_timer_.IsStarted());

  base::TimeTicks now = base::TimeTicks::Now();
  if (window_has_started_) {
    if (now - start_interval_time_ > sample_duration_) {
      EndInterval();
    } else {
      window_execute_timer_.Start();
    }
  } else {
    if (now >= next_interval_time_) {
      BeginInterval(true);
    } else {
      // Ignore this start event.
    }
  }
}

void WasmExecutionTimer::StopInternal() {
  DCHECK(v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms);

  base::TimeTicks now = base::TimeTicks::Now();
  if (window_has_started_) {
    DCHECK(window_execute_timer_.IsStarted());
    base::TimeDelta elapsed = window_execute_timer_.Elapsed();
    window_running_time_ += elapsed;
    window_execute_timer_.Stop();
    if (now - start_interval_time_ > sample_duration_) {
      EndInterval();
    }
  } else {
    if (now >= next_interval_time_) {
      BeginInterval(false);
    } else {
      // Ignore this stop event.
    }
  }
}

void WasmExecutionTimer::Terminate() {
  if (execute_ratio_histogram_->Enabled()) {
    if (window_has_started_) {
      if (window_execute_timer_.IsStarted()) {
        window_execute_timer_.Stop();
      }
      EndInterval();
    }
  }
}

namespace {
void NopFinalizer(const v8::WeakCallbackInfo<void>& data) {
  Address* global_handle_location =
      reinterpret_cast<Address*>(data.GetParameter());
  GlobalHandles::Destroy(global_handle_location);
}

Handle<WasmInstanceObject> MakeWeak(
    Isolate* isolate, Handle<WasmInstanceObject> instance_object) {
  Handle<WasmInstanceObject> weak_instance =
      isolate->global_handles()->Create<WasmInstanceObject>(*instance_object);
  Address* global_handle_location = weak_instance.location();
  GlobalHandles::MakeWeak(global_handle_location, global_handle_location,
                          &NopFinalizer, v8::WeakCallbackType::kParameter);
  return weak_instance;
}

std::optional<wasm::ValueType> GetWasmReturnTypeFromSignature(
    const FunctionSig* wasm_signature) {
  if (wasm_signature->return_count() == 0) return {};

  DCHECK_EQ(wasm_signature->return_count(), 1);
  return wasm_signature->GetReturn(0);
}

}  // namespace

// Build the interpreter call stack for the current activation. For each stack
// frame we need to calculate the Wasm function index and the original Wasm
// bytecode location, calculated from the current WasmBytecode offset.
std::vector<WasmInterpreterStackEntry>
WasmInterpreterThread::Activation::CaptureStackTrace(
    const TrapStatus* trap_status) const {
  std::vector<WasmInterpreterStackEntry> stack_trace;
  const FrameState* frame_state = &current_frame_state_;
  DCHECK_NOT_NULL(frame_state);

  if (trap_status) {
    stack_trace.push_back(WasmInterpreterStackEntry{
        trap_status->trap_function_index, trap_status->trap_pc});
  } else {
    if (frame_state->current_function_) {
      stack_trace.push_back(WasmInterpreterStackEntry{
          frame_state->current_function_->GetFunctionIndex(),
          frame_state->current_bytecode_
              ? static_cast<int>(
                    frame_state->current_function_->GetPcFromTrapCode(
                        frame_state->current_bytecode_))
              : 0});
    }
  }

  frame_state = frame_state->previous_frame_;
  while (frame_state && frame_state->current_function_) {
    stack_trace.insert(
        stack_trace.begin(),
        WasmInterpreterStackEntry{
            frame_state->current_function_->GetFunctionIndex(),
            frame_state->current_bytecode_
                ? static_cast<int>(
                      frame_state->current_function_->GetPcFromTrapCode(
                          frame_state->current_bytecode_))
                : 0});
    frame_state = frame_state->previous_frame_;
  }

  // It is possible to have a WasmInterpreterEntryFrame without having a Wasm
  // current_function_. This can happen if we call from JS a JS function
  // imported and exported by Wasm. In this case let's add a dummy stack entry.
  if (stack_trace.empty()) {
    stack_trace.push_back(WasmInterpreterStackEntry{0, 0});
  }

  return stack_trace;
}

int WasmInterpreterThread::Activation::GetFunctionIndex(int index) const {
  std::vector<int> function_indexes;
  const FrameState* frame_state = &current_frame_state_;
  // TODO(paolosev@microsoft.com) - Too slow?
  while (frame_state->current_function_) {
    function_indexes.push_back(
        frame_state->current_function_->GetFunctionIndex());
    frame_state = frame_state->previous_frame_;
  }

  if (static_cast<size_t>(index) < function_indexes.size()) {
    return function_indexes[function_indexes.size() - index - 1];
  }
  return -1;
}

WasmInterpreterThread::WasmInterpreterThread(Isolate* isolate)
    : isolate_(isolate),
      state_(State::STOPPED),
      trap_reason_(TrapReason::kTrapUnreachable),
      current_stack_size_(kInitialStackSize),
      stack_mem_(nullptr),
      reference_stack_(isolate_->global_handles()->Create(
          ReadOnlyRoots(isolate_).empty_fixed_array())),
      current_ref_stack_size_(0),
      execution_timer_(isolate, true) {
  PageAllocator* page_allocator = GetPlatformPageAllocator();
  stack_mem_ = AllocatePages(page_allocator, nullptr, kMaxStackSize,
                             page_allocator->AllocatePageSize(),
                             PageAllocator::kNoAccess);
  if (!stack_mem_ ||
      !SetPermissions(page_allocator, stack_mem_, current_stack_size_,
                      PageAllocator::Permission::kReadWrite)) {
    V8::FatalProcessOutOfMemory(nullptr,
                                "WasmInterpreterThread::WasmInterpreterThread",
                                "Cannot allocate Wasm interpreter stack");
    UNREACHABLE();
  }
}

WasmInterpreterThread::~WasmInterpreterThread() {
  GlobalHandles::Destroy(reference_stack_.location());
  FreePages(GetPlatformPageAllocator(), stack_mem_, kMaxStackSize);
}

void WasmInterpreterThread::EnsureRefStackSpace(size_t new_size) {
  if (V8_LIKELY(current_ref_stack_size_ >= new_size)) return;
  size_t requested_size = base::bits::RoundUpToPowerOfTwo64(new_size);
  new_size = std::max(size_t{8},
                      std::max(2 * current_ref_stack_size_, requested_size));
  int grow_by = static_cast<int>(new_size - current_ref_stack_size_);
  HandleScope handle_scope(isolate_);  // Avoid leaking handles.
  Handle<FixedArray> new_ref_stack =
      isolate_->factory()->CopyFixedArrayAndGrow(reference_stack_, grow_by);
  new_ref_stack->FillWithHoles(static_cast<int>(current_ref_stack_size_),
                               static_cast<int>(new_size));
  isolate_->global_handles()->Destroy(reference_stack_.location());
  reference_stack_ = isolate_->global_handles()->Create(*new_ref_stack);
  current_ref_stack_size_ = new_size;
}

void WasmInterpreterThread::ClearRefStackValues(size_t index, size_t count) {
  reference_stack_->FillWithHoles(static_cast<int>(index),
                                  static_cast<int>(index + count));
}

void WasmInterpreterThread::RaiseException(Isolate* isolate,
                                           MessageTemplate message) {
  DCHECK_EQ(WasmInterpreterThread::TRAPPED, state_);
  if (!isolate->has_exception()) {
    ClearThreadInWasmScope wasm_flag(isolate);
    Handle<JSObject> error_obj =
        isolate->factory()->NewWasmRuntimeError(message);
    JSObject::AddProperty(isolate, error_obj,
                          isolate->factory()->wasm_uncatchable_symbol(),
                          isolate->factory()->true_value(), NONE);
    isolate->Throw(*error_obj);
  }
}

// static
void WasmInterpreterThread::SetRuntimeLastWasmError(Isolate* isolate,
                                                    MessageTemplate message) {
  WasmInterpreterThread* current_thread = GetCurrentInterpreterThread(isolate);
  current_thread->trap_reason_ = WasmOpcodes::MessageIdToTrapReason(message);
}

// static
TrapReason WasmInterpreterThread::GetRuntimeLastWasmError(Isolate* isolate) {
  WasmInterpreterThread* current_thread = GetCurrentInterpreterThread(isolate);
  // TODO(paolosev@microsoft.com): store in new data member?
  return current_thread->trap_reason_;
}

void WasmInterpreterThread::StartExecutionTimer() {
  if (v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms) {
    execution_timer_.Start();
  }
}

void WasmInterpreterThread::StopExecutionTimer() {
  if (v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms) {
    execution_timer_.Stop();
  }
}

void WasmInterpreterThread::TerminateExecutionTimers() {
  if (v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms) {
    execution_timer_.Terminate();
  }
}

#if !defined(V8_DRUMBRAKE_BOUNDS_CHECKS)

enum BoundsCheckedHandlersCounter {
#define ITEM_ENUM_DEFINE(name) name##counter,
  FOREACH_LOAD_STORE_INSTR_HANDLER(ITEM_ENUM_DEFINE)
#undef ITEM_ENUM_DEFINE
      kTotalItems
};

V8_DECLARE_ONCE(init_instruction_table_once);
V8_DECLARE_ONCE(init_trap_handlers_once);

// A subset of the Wasm instruction handlers is implemented as ASM builtins, and
// not with normal C++ functions. This is done only for LoadMem and StoreMem
// builtins, which can trap for out of bounds accesses.
// V8 already implements out of bounds trap handling for compiled Wasm code and
// allocates two large guard pages before and after each Wasm memory region to
// detect out of bounds memory accesses. Once an access violation exception
// arises, the V8 exception filter intercepts the exception and checks whether
// it originates from Wasm code.
// The Wasm interpreter reuses the same logic, and
// WasmInterpreter::HandleWasmTrap is called by the SEH exception handler to
// check whether the access violation was caused by an interpreter instruction
// handler. It is necessary that these handlers are Wasm builtins for two
// reasons:
// 1. We want to know precisely the start and end address of each handler to
// verify if the AV happened inside one of the Load/Store builtins and can be
// handled with a Wasm trap.
// 2. If the exception is handled, we interrupt the execution of
// TrapMemOutOfBounds, which sets the TRAPPED state and breaks the execution of
// the chain of instruction handlers with a x64 'ret'. This only works if there
// is no stack cleanup to do in the handler that caused the failure (no
// registers to pop from the stack before the 'ret'). Therefore we cannot rely
// on the compiler, we can only make sure that this is the case if we implement
// the handlers in assembly.

// Forward declaration
INSTRUCTION_HANDLER_FUNC TrapMemOutOfBounds(
    const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
    int64_t r0, double fp0);

void InitTrapHandlersOnce(Isolate* isolate) {
  CHECK_LE(kInstructionCount, kInstructionTableSize);

  ClearThreadInWasmScope wasm_flag(isolate);

  // Overwrites the instruction handlers that access memory and can cause an
  // out-of-bounds trap with builtin versions that don't have explicit bounds
  // check but rely on a trap handler to intercept the access violation and
  // transform it into a trap.
  EmbeddedData embedded_data = EmbeddedData::FromBlob();
#define V(name)                                                             \
  if (v8_flags.drumbrake_compact_bytecode) {                                \
    trap_handler::RegisterHandlerData(                                      \
        reinterpret_cast<Address>(kInstructionTable[k_##name]),             \
        embedded_data.InstructionSizeOf(Builtin::k##name##_s), 0, nullptr); \
  }                                                                         \
  trap_handler::RegisterHandlerData(                                        \
      reinterpret_cast<Address>(                                            \
          kInstructionTable[k_##name + kInstructionCount]),                 \
      embedded_data.InstructionSizeOf(Builtin::k##name##_l), 0, nullptr);
  FOREACH_LOAD_STORE_INSTR_HANDLER(V)
#undef V
}

void InitInstructionTableOnce(Isolate* isolate) {
  size_t index = 0;
#define V(name)                                                                \
  if (v8_flags.drumbrake_compact_bytecode) {                                   \
    kInstructionTable[index] = reinterpret_cast<PWasmOp*>(                     \
        isolate->builtins()->code(Builtin::k##name##_s)->instruction_start()); \
  }                                                                            \
  kInstructionTable[kInstructionCount + index++] = reinterpret_cast<PWasmOp*>( \
      isolate->builtins()->code(Builtin::k##name##_l)->instruction_start());

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-calling-convention"
#endif  // __clang__
  FOREACH_LOAD_STORE_INSTR_HANDLER(V)
#ifdef __clang__
#pragma clang diagnostic pop
#endif  // __clang__
#undef V

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  if (v8_flags.trace_drumbrake_compact_bytecode) {
    index = 0;
#define DEFINE_INSTR_HANDLER(name) kInstructionHandlerNames[index++] = #name;
    FOREACH_INSTR_HANDLER(DEFINE_INSTR_HANDLER)
    FOREACH_TRACE_INSTR_HANDLER(DEFINE_INSTR_HANDLER)
#undef DEFINE_INSTR_HANDLER
  }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
}
#endif  // !V8_DRUMBRAKE_BOUNDS_CHECKS

WasmInterpreter::WasmInterpreter(Isolate* isolate, const WasmModule* module,
                                 const ModuleWireBytes& wire_bytes,
                                 Handle<WasmInstanceObject> instance_object)
    : zone_(isolate->allocator(), ZONE_NAME),
      instance_object_(MakeWeak(isolate, instance_object)),
      module_bytes_(wire_bytes.start(), wire_bytes.end(), &zone_),
      codemap_(isolate, module, module_bytes_.data(), &zone_) {
  wasm_runtime_ = std::make_shared<WasmInterpreterRuntime>(
      module, isolate, instance_object_, &codemap_);
  module->SetWasmInterpreter(wasm_runtime_);

#if !defined(V8_DRUMBRAKE_BOUNDS_CHECKS)
  // TODO(paolosev@microsoft.com) - For modules that have 64-bit Wasm memory we
  // need to use explicit bound checks; memory guard pages only work with 32-bit
  // memories. This could be implemented by allocating a different dispatch
  // table for each instance (probably in the WasmInterpreterRuntime object) and
  // patching the entries of Load/Store instructions with bultin handlers only
  // for instances related to modules that have 32-bit memories. 64-bit memories
  // are not supported yet by DrumBrake.
  base::CallOnce(&init_instruction_table_once, &InitInstructionTableOnce,
                 isolate);
  base::CallOnce(&init_trap_handlers_once, &InitTrapHandlersOnce, isolate);

  trap_handler::SetLandingPad(reinterpret_cast<Address>(TrapMemOutOfBounds));
#endif  // !V8_DRUMBRAKE_BOUNDS_CHECKS
}

WasmInterpreterThread::State WasmInterpreter::ContinueExecution(
    WasmInterpreterThread* thread, bool called_from_js) {
  wasm_runtime_->ContinueExecution(thread, called_from_js);
  return thread->state();
}

////////////////////////////////////////////////////////////////////////////////
//
// DrumBrake: implementation of an interpreter for WebAssembly.
//
////////////////////////////////////////////////////////////////////////////////

constexpr uint32_t kFloat32SignBitMask = uint32_t{1} << 31;
constexpr uint64_t kFloat64SignBitMask = uint64_t{1} << 63;

#ifdef DRUMBRAKE_ENABLE_PROFILING

static const char* prev_op_name_s = nullptr;
static std::map<std::pair<const char*, const char*>, uint64_t>*
    ops_pairs_count_s = nullptr;
static std::map<const char*, uint64_t>* ops_count_s = nullptr;
static void ProfileOp(const char* op_name) {
  if (!ops_pairs_count_s) {
    ops_pairs_count_s =
        new std::map<std::pair<const char*, const char*>, uint64_t>();
    ops_count_s = new std::map<const char*, uint64_t>();
  }
  if (prev_op_name_s) {
    (*ops_pairs_count_s)[{prev_op_name_s, op_name}]++;
  }
  (*ops_count_s)[op_name]++;
  prev_op_name_s = op_name;
}

template <typename A, typename B>
std::pair<B, A> flip_pair(const std::pair<A, B>& p) {
  return std::pair<B, A>(p.second, p.first);
}
template <typename A, typename B>
std::multimap<B, A> flip_map(const std::map<A, B>& src) {
  std::multimap<B, A> dst;
  std::transform(src.begin(), src.end(), std::inserter(dst, dst.begin()),
                 flip_pair<A, B>);
  return dst;
}

static void PrintOpsCount() {
  std::multimap<uint64_t, const char*> count_ops_map = flip_map(*ops_count_s);
  uint64_t total_count = 0;
  for (auto& pair : count_ops_map) {
    printf("%10lld, %s\n", pair.first, pair.second);
    total_count += pair.first;
  }
  printf("Total count: %10lld\n\n", total_count);

  std::multimap<uint64_t, std::pair<const char*, const char*>>
      count_pairs_ops_map = flip_map(*ops_pairs_count_s);
  for (auto& pair : count_pairs_ops_map) {
    printf("%10lld, %s -> %s\n", pair.first, pair.second.first,
           pair.second.second);
  }
}

static void PrintAndClearProfilingData() {
  PrintOpsCount();
  delete ops_count_s;
  ops_count_s = nullptr;
  delete ops_pairs_count_s;
  ops_pairs_count_s = nullptr;
}

#define NextOp()                                                             \
  ProfileOp(__FUNCTION__);                                                   \
  MUSTTAIL return kInstructionTable[ReadFnId(code) & kInstructionTableMask]( \
      code, sp, wasm_runtime, r0, fp0)

#else  // DRUMBRAKE_ENABLE_PROFILING

#define NextOp()                                                             \
  MUSTTAIL return kInstructionTable[ReadFnId(code) & kInstructionTableMask]( \
      code, sp, wasm_runtime, r0, fp0)

#endif  // DRUMBRAKE_ENABLE_PROFILING

static int StructFieldOffset(const StructType* struct_type, int field_index) {
  return wasm::ObjectAccess::ToTagged(WasmStruct::kHeaderSize +
                                      struct_type->field_offset(field_index));
}

InstructionHandler s_unwind_code = InstructionHandler::k_s2s_Unwind;

class HandlersBase {
 public:
  INSTRUCTION_HANDLER_FUNC s2s_Unreachable(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    MUSTTAIL return HandlersBase::Trap(code, sp, wasm_runtime,
                                       TrapReason::kTrapUnreachable, fp0);
  }

  INSTRUCTION_HANDLER_FUNC
  s2s_Unwind(const uint8_t* code, uint32_t* sp,
             WasmInterpreterRuntime* wasm_runtime, int64_t r0, double fp0) {
    // Break the chain of calls.
  }

  INSTRUCTION_HANDLER_FUNC Trap(const uint8_t* code, uint32_t* sp,
                                WasmInterpreterRuntime* wasm_runtime,
                                int64_t r0, double fp0) {
    TrapReason trap_reason = static_cast<TrapReason>(r0);
    wasm_runtime->SetTrap(trap_reason, code);
    MUSTTAIL return s_unwind_func_addr(code, sp, wasm_runtime, trap_reason, .0);
  }

  static constexpr PWasmOp* s_unwind_func_addr = HandlersBase::s2s_Unwind;
};

#define TRAP(trap_reason) \
  MUSTTAIL return HandlersBase::Trap(code, sp, wasm_runtime, trap_reason, fp0);

#define INLINED_TRAP(trap_reason)           \
  wasm_runtime->SetTrap(trap_reason, code); \
  MUSTTAIL return s_unwind_func_addr(code, sp, wasm_runtime, trap_reason, .0);

template <bool Compressed>
class Handlers : public HandlersBase {
  using traits = handler_traits<Compressed>;
  using slot_offset_t = traits::slot_offset_t;
  using memory_offset32_t = traits::memory_offset32_t;
  using memory_offset64_t = traits::memory_offset64_t;

 public:
  template <typename T>
  static inline T Read(const uint8_t*& code) {
    T res = base::ReadUnalignedValue<T>(reinterpret_cast<Address>(code));
    code += sizeof(T);
    return res;
  }
  template <>
  slot_offset_t Read<slot_offset_t>(const uint8_t*& code) {
    slot_offset_t res = base::ReadUnalignedValue<slot_offset_t>(
        reinterpret_cast<Address>(code));
#if V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_compact_bytecode) {
      printf("Read slot_offset_t %d\n", res);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
    code += sizeof(slot_offset_t);
    return res;
  }

  // Returns the maximum of the two parameters according to JavaScript
  // semantics.
  template <typename T>
  static inline T JSMax(T x, T y) {
    if (std::isnan(x) || std::isnan(y)) {
      return std::numeric_limits<T>::quiet_NaN();
    }
    if (std::signbit(x) < std::signbit(y)) return x;
    return x > y ? x : y;
  }

  // Returns the minimum of the two parameters according to JavaScript
  // semantics.
  template <typename T>
  static inline T JSMin(T x, T y) {
    if (std::isnan(x) || std::isnan(y)) {
      return std::numeric_limits<T>::quiet_NaN();
    }
    if (std::signbit(x) < std::signbit(y)) return y;
    return x > y ? y : x;
  }

  static inline uint8_t* ReadMemoryAddress(uint8_t*& code) {
    Address res =
        base::ReadUnalignedValue<Address>(reinterpret_cast<Address>(code));
    code += sizeof(Address);
    return reinterpret_cast<uint8_t*>(res);
  }

  static inline uint32_t ReadGlobalIndex(const uint8_t*& code) {
    uint32_t res =
        base::ReadUnalignedValue<uint32_t>(reinterpret_cast<Address>(code));
    code += sizeof(uint32_t);
    return res;
  }

  template <typename T>
  static inline void push(uint32_t*& sp, const uint8_t*& code,
                          WasmInterpreterRuntime* wasm_runtime, T val) {
    slot_offset_t offset = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<T>(reinterpret_cast<Address>(sp + offset), val);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution)
      wasm_runtime->TracePush<T>(offset * kSlotSize);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
  }

  template <>
  inline void push(uint32_t*& sp, const uint8_t*& code,
                   WasmInterpreterRuntime* wasm_runtime, WasmRef ref) {
    slot_offset_t offset = Read<slot_offset_t>(code);
    uint32_t ref_stack_index = Read<int32_t>(code);
    base::WriteUnalignedValue<uint64_t>(reinterpret_cast<Address>(sp + offset),
                                        kSlotsZapValue);
    wasm_runtime->StoreWasmRef(ref_stack_index, ref);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution)
      wasm_runtime->TracePush<WasmRef>(offset * kSlotSize);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
  }

  template <typename T>
  static inline T pop(uint32_t*& sp, const uint8_t*& code,
                      WasmInterpreterRuntime* wasm_runtime) {
    slot_offset_t offset = Read<slot_offset_t>(code);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) wasm_runtime->TracePop();
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
    return base::ReadUnalignedValue<T>(reinterpret_cast<Address>(sp + offset));
  }

  template <>
  inline WasmRef pop(uint32_t*& sp, const uint8_t*& code,
                     WasmInterpreterRuntime* wasm_runtime) {
    uint32_t ref_stack_index = Read<int32_t>(code);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) wasm_runtime->TracePop();
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
    return wasm_runtime->ExtractWasmRef(ref_stack_index);
  }

  template <typename T>
  static inline T ExecuteRemS(T lval, T rval) {
    if (rval == -1) return 0;
    return lval % rval;
  }

  template <typename T>
  static inline T ExecuteRemU(T lval, T rval) {
    return lval % rval;
  }

  //////////////////////////////////////////////////////////////////////////////
  // GlobalGet

  template <typename IntT>
  INSTRUCTION_HANDLER_FUNC s2r_GlobalGetI(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    uint32_t index = ReadGlobalIndex(code);
    uint8_t* src_addr = wasm_runtime->GetGlobalAddress(index);
    r0 = base::ReadUnalignedValue<IntT>(reinterpret_cast<Address>(src_addr));

    NextOp();
  }
  static auto constexpr s2r_I32GlobalGet = s2r_GlobalGetI<int32_t>;
  static auto constexpr s2r_I64GlobalGet = s2r_GlobalGetI<int64_t>;

  template <typename FloatT>
  INSTRUCTION_HANDLER_FUNC s2r_GlobalGetF(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    uint32_t index = ReadGlobalIndex(code);
    uint8_t* src_addr = wasm_runtime->GetGlobalAddress(index);
    fp0 = base::ReadUnalignedValue<FloatT>(reinterpret_cast<Address>(src_addr));

    NextOp();
  }
  static auto constexpr s2r_F32GlobalGet = s2r_GlobalGetF<float>;
  static auto constexpr s2r_F64GlobalGet = s2r_GlobalGetF<double>;

  template <typename T>
  INSTRUCTION_HANDLER_FUNC s2s_GlobalGet(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    uint32_t index = ReadGlobalIndex(code);
    uint8_t* src_addr = wasm_runtime->GetGlobalAddress(index);
    push<T>(sp, code, wasm_runtime,
            base::ReadUnalignedValue<T>(reinterpret_cast<Address>(src_addr)));

    NextOp();
  }
  static auto constexpr s2s_I32GlobalGet = s2s_GlobalGet<int32_t>;
  static auto constexpr s2s_I64GlobalGet = s2s_GlobalGet<int64_t>;
  static auto constexpr s2s_F32GlobalGet = s2s_GlobalGet<float>;
  static auto constexpr s2s_F64GlobalGet = s2s_GlobalGet<double>;
  static auto constexpr s2s_S128GlobalGet = s2s_GlobalGet<Simd128>;

  INSTRUCTION_HANDLER_FUNC s2s_RefGlobalGet(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t index = ReadGlobalIndex(code);
    push<WasmRef>(sp, code, wasm_runtime, wasm_runtime->GetGlobalRef(index));

    NextOp();
  }

  //////////////////////////////////////////////////////////////////////////////
  // GlobalSet

  template <typename IntT>
  INSTRUCTION_HANDLER_FUNC r2s_GlobalSetI(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    uint32_t index = ReadGlobalIndex(code);
    uint8_t* dst_addr = wasm_runtime->GetGlobalAddress(index);
    base::WriteUnalignedValue<IntT>(reinterpret_cast<Address>(dst_addr),
                                    static_cast<IntT>(r0));  // r0: value
    NextOp();
  }
  static auto constexpr r2s_I32GlobalSet = r2s_GlobalSetI<int32_t>;
  static auto constexpr r2s_I64GlobalSet = r2s_GlobalSetI<int64_t>;

  template <typename FloatT>
  INSTRUCTION_HANDLER_FUNC r2s_GlobalSetF(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    uint32_t index = ReadGlobalIndex(code);
    uint8_t* dst_addr = wasm_runtime->GetGlobalAddress(index);
    base::WriteUnalignedValue<FloatT>(reinterpret_cast<Address>(dst_addr),
                                      static_cast<FloatT>(fp0));  // fp0: value
    NextOp();
  }
  static auto constexpr r2s_F32GlobalSet = r2s_GlobalSetF<float>;
  static auto constexpr r2s_F64GlobalSet = r2s_GlobalSetF<double>;

  template <typename T>
  INSTRUCTION_HANDLER_FUNC s2s_GlobalSet(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    uint32_t index = ReadGlobalIndex(code);
    uint8_t* dst_addr = wasm_runtime->GetGlobalAddress(index);
    base::WriteUnalignedValue<T>(reinterpret_cast<Address>(dst_addr),
                                 pop<T>(sp, code, wasm_runtime));
    NextOp();
  }
  static auto constexpr s2s_I32GlobalSet = s2s_GlobalSet<int32_t>;
  static auto constexpr s2s_I64GlobalSet = s2s_GlobalSet<int64_t>;
  static auto constexpr s2s_F32GlobalSet = s2s_GlobalSet<float>;
  static auto constexpr s2s_F64GlobalSet = s2s_GlobalSet<double>;
  static auto constexpr s2s_S128GlobalSet = s2s_GlobalSet<Simd128>;

  INSTRUCTION_HANDLER_FUNC s2s_RefGlobalSet(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t index = ReadGlobalIndex(code);
    wasm_runtime->SetGlobalRef(index, pop<WasmRef>(sp, code, wasm_runtime));

    NextOp();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Drop

  template <typename T>
  INSTRUCTION_HANDLER_FUNC r2s_Drop(const uint8_t* code, uint32_t* sp,
                                    WasmInterpreterRuntime* wasm_runtime,
                                    int64_t r0, double fp0) {
    NextOp();
  }
  static auto constexpr r2s_I32Drop = r2s_Drop<int32_t>;
  static auto constexpr r2s_I64Drop = r2s_Drop<int64_t>;
  static auto constexpr r2s_F32Drop = r2s_Drop<float>;
  static auto constexpr r2s_F64Drop = r2s_Drop<double>;

  INSTRUCTION_HANDLER_FUNC r2s_RefDrop(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    UNREACHABLE();
  }

  template <typename T>
  INSTRUCTION_HANDLER_FUNC s2s_Drop(const uint8_t* code, uint32_t* sp,
                                    WasmInterpreterRuntime* wasm_runtime,
                                    int64_t r0, double fp0) {
    pop<T>(sp, code, wasm_runtime);

    NextOp();
  }
  static auto constexpr s2s_I32Drop = s2s_Drop<int32_t>;
  static auto constexpr s2s_I64Drop = s2s_Drop<int64_t>;
  static auto constexpr s2s_F32Drop = s2s_Drop<float>;
  static auto constexpr s2s_F64Drop = s2s_Drop<double>;
  static auto constexpr s2s_S128Drop = s2s_Drop<Simd128>;

  INSTRUCTION_HANDLER_FUNC s2s_RefDrop(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    pop<WasmRef>(sp, code, wasm_runtime);

    NextOp();
  }

  //////////////////////////////////////////////////////////////////////////////
  // LoadMem

  template <typename IntT, typename IntU = IntT, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC r2r_LoadMemI(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    uint64_t offset = Read<MemOffsetT>(code);
    MemIdx index = static_cast<MemIdx>(r0);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(IntU),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    IntU value =
        base::ReadUnalignedValue<IntU>(reinterpret_cast<Address>(address));
    r0 = static_cast<IntT>(value);

    NextOp();
  }
  static auto constexpr r2r_I32LoadMem8S_Idx64 =
      r2r_LoadMemI<int32_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_I32LoadMem8U_Idx64 =
      r2r_LoadMemI<int32_t, uint8_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_I32LoadMem16S_Idx64 =
      r2r_LoadMemI<int32_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_I32LoadMem16U_Idx64 =
      r2r_LoadMemI<int32_t, uint16_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_I64LoadMem8S_Idx64 =
      r2r_LoadMemI<int64_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_I64LoadMem8U_Idx64 =
      r2r_LoadMemI<int64_t, uint8_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_I64LoadMem16S_Idx64 =
      r2r_LoadMemI<int64_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_I64LoadMem16U_Idx64 =
      r2r_LoadMemI<int64_t, uint16_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_I64LoadMem32S_Idx64 =
      r2r_LoadMemI<int64_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_I64LoadMem32U_Idx64 =
      r2r_LoadMemI<int64_t, uint32_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_I32LoadMem_Idx64 =
      r2r_LoadMemI<int32_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_I64LoadMem_Idx64 =
      r2r_LoadMemI<int64_t, int64_t, uint64_t, memory_offset64_t>;

  template <typename FloatT, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC r2r_LoadMemF(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    MemOffsetT offset = Read<MemOffsetT>(code);
    MemIdx index = static_cast<MemIdx>(r0);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(FloatT),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    fp0 = base::ReadUnalignedValue<FloatT>(reinterpret_cast<Address>(address));

    NextOp();
  }
  static auto constexpr r2r_F32LoadMem_Idx64 =
      r2r_LoadMemF<float, uint64_t, memory_offset64_t>;
  static auto constexpr r2r_F64LoadMem_Idx64 =
      r2r_LoadMemF<double, uint64_t, memory_offset64_t>;

  template <typename T, typename U = T, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC r2s_LoadMem(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    MemOffsetT offset = Read<MemOffsetT>(code);
    MemIdx index = static_cast<MemIdx>(r0);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(U),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    U value = base::ReadUnalignedValue<U>(reinterpret_cast<Address>(address));

    push<T>(sp, code, wasm_runtime, value);

    NextOp();
  }
  static auto constexpr r2s_I32LoadMem8S_Idx64 =
      r2s_LoadMem<int32_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I32LoadMem8U_Idx64 =
      r2s_LoadMem<int32_t, uint8_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I32LoadMem16S_Idx64 =
      r2s_LoadMem<int32_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I32LoadMem16U_Idx64 =
      r2s_LoadMem<int32_t, uint16_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64LoadMem8S_Idx64 =
      r2s_LoadMem<int64_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64LoadMem8U_Idx64 =
      r2s_LoadMem<int64_t, uint8_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64LoadMem16S_Idx64 =
      r2s_LoadMem<int64_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64LoadMem16U_Idx64 =
      r2s_LoadMem<int64_t, uint16_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64LoadMem32S_Idx64 =
      r2s_LoadMem<int64_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64LoadMem32U_Idx64 =
      r2s_LoadMem<int64_t, uint32_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I32LoadMem_Idx64 =
      r2s_LoadMem<int32_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64LoadMem_Idx64 =
      r2s_LoadMem<int64_t, int64_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_F32LoadMem_Idx64 =
      r2s_LoadMem<float, float, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_F64LoadMem_Idx64 =
      r2s_LoadMem<double, double, uint64_t, memory_offset64_t>;

  template <typename IntT, typename IntU = IntT, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC s2r_LoadMemI(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    MemOffsetT offset = Read<MemOffsetT>(code);
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(IntU),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    r0 = static_cast<IntT>(
        base::ReadUnalignedValue<IntU>(reinterpret_cast<Address>(address)));

    NextOp();
  }
  static auto constexpr s2r_I32LoadMem8S_Idx64 =
      s2r_LoadMemI<int32_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_I32LoadMem8U_Idx64 =
      s2r_LoadMemI<int32_t, uint8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_I32LoadMem16S_Idx64 =
      s2r_LoadMemI<int32_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_I32LoadMem16U_Idx64 =
      s2r_LoadMemI<int32_t, uint16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_I64LoadMem8S_Idx64 =
      s2r_LoadMemI<int64_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_I64LoadMem8U_Idx64 =
      s2r_LoadMemI<int64_t, uint8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_I64LoadMem16S_Idx64 =
      s2r_LoadMemI<int64_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_I64LoadMem16U_Idx64 =
      s2r_LoadMemI<int64_t, uint16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_I64LoadMem32S_Idx64 =
      s2r_LoadMemI<int64_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_I64LoadMem32U_Idx64 =
      s2r_LoadMemI<int64_t, uint32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_I32LoadMem_Idx64 =
      s2r_LoadMemI<int32_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_I64LoadMem_Idx64 =
      s2r_LoadMemI<int64_t, int64_t, uint64_t, memory_offset64_t>;

  template <typename FloatT, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC s2r_LoadMemF(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    MemOffsetT offset = Read<MemOffsetT>(code);
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(FloatT),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    fp0 = static_cast<FloatT>(
        base::ReadUnalignedValue<FloatT>(reinterpret_cast<Address>(address)));

    NextOp();
  }
  static auto constexpr s2r_F32LoadMem_Idx64 =
      s2r_LoadMemF<float, uint64_t, memory_offset64_t>;
  static auto constexpr s2r_F64LoadMem_Idx64 =
      s2r_LoadMemF<double, uint64_t, memory_offset64_t>;

  template <typename T, typename U = T, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC s2s_LoadMem(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    MemOffsetT offset = Read<MemOffsetT>(code);
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(U),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    U value = base::ReadUnalignedValue<U>(reinterpret_cast<Address>(address));

    push<T>(sp, code, wasm_runtime, value);

    NextOp();
  }
  static auto constexpr s2s_I32LoadMem8S_Idx64 =
      s2s_LoadMem<int32_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I32LoadMem8U_Idx64 =
      s2s_LoadMem<int32_t, uint8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I32LoadMem16S_Idx64 =
      s2s_LoadMem<int32_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I32LoadMem16U_Idx64 =
      s2s_LoadMem<int32_t, uint16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem8S_Idx64 =
      s2s_LoadMem<int64_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem8U_Idx64 =
      s2s_LoadMem<int64_t, uint8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem16S_Idx64 =
      s2s_LoadMem<int64_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem16U_Idx64 =
      s2s_LoadMem<int64_t, uint16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem32S_Idx64 =
      s2s_LoadMem<int64_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem32U_Idx64 =
      s2s_LoadMem<int64_t, uint32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I32LoadMem_Idx64 =
      s2s_LoadMem<int32_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem_Idx64 =
      s2s_LoadMem<int64_t, int64_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_F32LoadMem_Idx64 =
      s2s_LoadMem<float, float, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_F64LoadMem_Idx64 =
      s2s_LoadMem<double, double, uint64_t, memory_offset64_t>;

  // LoadMem_LocalSet
  template <typename T, typename U = T, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC s2s_LoadMem_LocalSet(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    MemOffsetT offset = Read<MemOffsetT>(code);
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(U),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    U value = base::ReadUnalignedValue<U>(reinterpret_cast<Address>(address));

    slot_offset_t to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<T>(reinterpret_cast<Address>(sp + to),
                                 static_cast<T>(value));

    NextOp();
  }
  static auto constexpr s2s_I32LoadMem8S_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int32_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I32LoadMem8U_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int32_t, uint8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I32LoadMem16S_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int32_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I32LoadMem16U_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int32_t, uint16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem8S_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int64_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem8U_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int64_t, uint8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem16S_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int64_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem16U_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int64_t, uint16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem32S_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int64_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem32U_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int64_t, uint32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I32LoadMem_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int32_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadMem_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<int64_t, int64_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_F32LoadMem_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<float, float, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_F64LoadMem_LocalSet_Idx64 =
      s2s_LoadMem_LocalSet<double, double, uint64_t, memory_offset64_t>;

  // StoreMem
  template <typename IntT, typename IntU = IntT, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC r2s_StoreMemI(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    IntT value = static_cast<IntT>(r0);

    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    MemOffsetT offset = Read<MemOffsetT>(code);
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(IntU),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    base::WriteUnalignedValue<IntU>(
        reinterpret_cast<Address>(address),
        base::ReadUnalignedValue<IntU>(reinterpret_cast<Address>(&value)));

    NextOp();
  }
  static auto constexpr r2s_I32StoreMem8_Idx64 =
      r2s_StoreMemI<int32_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I32StoreMem16_Idx64 =
      r2s_StoreMemI<int32_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64StoreMem8_Idx64 =
      r2s_StoreMemI<int64_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64StoreMem16_Idx64 =
      r2s_StoreMemI<int64_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64StoreMem32_Idx64 =
      r2s_StoreMemI<int64_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I32StoreMem_Idx64 =
      r2s_StoreMemI<int32_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64StoreMem_Idx64 =
      r2s_StoreMemI<int64_t, int64_t, uint64_t, memory_offset64_t>;

  template <typename FloatT, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC r2s_StoreMemF(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    FloatT value = static_cast<FloatT>(fp0);

    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    MemOffsetT offset = Read<MemOffsetT>(code);
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(FloatT),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    base::WriteUnalignedValue<FloatT>(
        reinterpret_cast<Address>(address),
        base::ReadUnalignedValue<FloatT>(reinterpret_cast<Address>(&value)));

    NextOp();
  }
  static auto constexpr r2s_F32StoreMem_Idx64 =
      r2s_StoreMemF<float, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_F64StoreMem_Idx64 =
      r2s_StoreMemF<double, uint64_t, memory_offset64_t>;

  template <typename T, typename U = T, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC s2s_StoreMem(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    T value = pop<T>(sp, code, wasm_runtime);

    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    MemOffsetT offset = Read<MemOffsetT>(code);
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(U),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    base::WriteUnalignedValue<U>(
        reinterpret_cast<Address>(address),
        base::ReadUnalignedValue<U>(reinterpret_cast<Address>(&value)));

    NextOp();
  }
  static auto constexpr s2s_I32StoreMem8_Idx64 =
      s2s_StoreMem<int32_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I32StoreMem16_Idx64 =
      s2s_StoreMem<int32_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64StoreMem8_Idx64 =
      s2s_StoreMem<int64_t, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64StoreMem16_Idx64 =
      s2s_StoreMem<int64_t, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64StoreMem32_Idx64 =
      s2s_StoreMem<int64_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I32StoreMem_Idx64 =
      s2s_StoreMem<int32_t, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64StoreMem_Idx64 =
      s2s_StoreMem<int64_t, int64_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_F32StoreMem_Idx64 =
      s2s_StoreMem<float, float, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_F64StoreMem_Idx64 =
      s2s_StoreMem<double, double, uint64_t, memory_offset64_t>;

  // LoadStoreMem
  template <typename T, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC r2s_LoadStoreMem(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();

    MemOffsetT load_offset = Read<MemOffsetT>(code);
    uint64_t load_index = r0;
    uint64_t effective_load_index = load_offset + load_index;

    MemOffsetT store_offset = Read<MemOffsetT>(code);
    uint64_t store_index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_store_index = store_offset + store_index;

    if (V8_UNLIKELY(
            effective_load_index < load_index ||
            !base::IsInBounds<uint64_t>(effective_load_index, sizeof(T),
                                        wasm_runtime->GetMemorySize()) ||
            effective_store_index < store_offset ||
            !base::IsInBounds<uint64_t>(effective_store_index, sizeof(T),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* load_address = memory_start + effective_load_index;
    uint8_t* store_address = memory_start + effective_store_index;

    base::WriteUnalignedValue<T>(
        reinterpret_cast<Address>(store_address),
        base::ReadUnalignedValue<T>(reinterpret_cast<Address>(load_address)));

    NextOp();
  }
  static auto constexpr r2s_I32LoadStoreMem_Idx64 =
      r2s_LoadStoreMem<int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_I64LoadStoreMem_Idx64 =
      r2s_LoadStoreMem<int64_t, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_F32LoadStoreMem_Idx64 =
      r2s_LoadStoreMem<float, uint64_t, memory_offset64_t>;
  static auto constexpr r2s_F64LoadStoreMem_Idx64 =
      r2s_LoadStoreMem<double, uint64_t, memory_offset64_t>;

  template <typename T, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC s2s_LoadStoreMem(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();

    MemOffsetT load_offset = Read<MemOffsetT>(code);
    uint64_t load_index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_load_index = load_offset + load_index;

    MemOffsetT store_offset = Read<MemOffsetT>(code);
    uint64_t store_index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_store_index = store_offset + store_index;

    if (V8_UNLIKELY(
            effective_load_index < load_index ||
            !base::IsInBounds<uint64_t>(effective_load_index, sizeof(T),
                                        wasm_runtime->GetMemorySize()) ||
            effective_store_index < store_offset ||
            !base::IsInBounds<uint64_t>(effective_store_index, sizeof(T),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* load_address = memory_start + effective_load_index;
    uint8_t* store_address = memory_start + effective_store_index;

    base::WriteUnalignedValue<T>(
        reinterpret_cast<Address>(store_address),
        base::ReadUnalignedValue<T>(reinterpret_cast<Address>(load_address)));

    NextOp();
  }
  static auto constexpr s2s_I32LoadStoreMem_Idx64 =
      s2s_LoadStoreMem<int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_I64LoadStoreMem_Idx64 =
      s2s_LoadStoreMem<int64_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_F32LoadStoreMem_Idx64 =
      s2s_LoadStoreMem<float, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_F64LoadStoreMem_Idx64 =
      s2s_LoadStoreMem<double, uint64_t, memory_offset64_t>;

#if defined(V8_DRUMBRAKE_BOUNDS_CHECKS)
  static auto constexpr r2r_I32LoadMem8S = r2r_LoadMemI<int32_t, int8_t>;
  static auto constexpr r2r_I32LoadMem8U = r2r_LoadMemI<int32_t, uint8_t>;
  static auto constexpr r2r_I32LoadMem16S = r2r_LoadMemI<int32_t, int16_t>;
  static auto constexpr r2r_I32LoadMem16U = r2r_LoadMemI<int32_t, uint16_t>;
  static auto constexpr r2r_I64LoadMem8S = r2r_LoadMemI<int64_t, int8_t>;
  static auto constexpr r2r_I64LoadMem8U = r2r_LoadMemI<int64_t, uint8_t>;
  static auto constexpr r2r_I64LoadMem16S = r2r_LoadMemI<int64_t, int16_t>;
  static auto constexpr r2r_I64LoadMem16U = r2r_LoadMemI<int64_t, uint16_t>;
  static auto constexpr r2r_I64LoadMem32S = r2r_LoadMemI<int64_t, int32_t>;
  static auto constexpr r2r_I64LoadMem32U = r2r_LoadMemI<int64_t, uint32_t>;
  static auto constexpr r2r_I32LoadMem = r2r_LoadMemI<int32_t>;
  static auto constexpr r2r_I64LoadMem = r2r_LoadMemI<int64_t>;

  static auto constexpr r2r_F32LoadMem = r2r_LoadMemF<float>;
  static auto constexpr r2r_F64LoadMem = r2r_LoadMemF<double>;

  static auto constexpr r2s_I32LoadMem8S = r2s_LoadMem<int32_t, int8_t>;
  static auto constexpr r2s_I32LoadMem8U = r2s_LoadMem<int32_t, uint8_t>;
  static auto constexpr r2s_I32LoadMem16S = r2s_LoadMem<int32_t, int16_t>;
  static auto constexpr r2s_I32LoadMem16U = r2s_LoadMem<int32_t, uint16_t>;
  static auto constexpr r2s_I64LoadMem8S = r2s_LoadMem<int64_t, int8_t>;
  static auto constexpr r2s_I64LoadMem8U = r2s_LoadMem<int64_t, uint8_t>;
  static auto constexpr r2s_I64LoadMem16S = r2s_LoadMem<int64_t, int16_t>;
  static auto constexpr r2s_I64LoadMem16U = r2s_LoadMem<int64_t, uint16_t>;
  static auto constexpr r2s_I64LoadMem32S = r2s_LoadMem<int64_t, int32_t>;
  static auto constexpr r2s_I64LoadMem32U = r2s_LoadMem<int64_t, uint32_t>;
  static auto constexpr r2s_I32LoadMem = r2s_LoadMem<int32_t>;
  static auto constexpr r2s_I64LoadMem = r2s_LoadMem<int64_t>;
  static auto constexpr r2s_F32LoadMem = r2s_LoadMem<float>;
  static auto constexpr r2s_F64LoadMem = r2s_LoadMem<double>;

  static auto constexpr s2r_I32LoadMem8S = s2r_LoadMemI<int32_t, int8_t>;
  static auto constexpr s2r_I32LoadMem8U = s2r_LoadMemI<int32_t, uint8_t>;
  static auto constexpr s2r_I32LoadMem16S = s2r_LoadMemI<int32_t, int16_t>;
  static auto constexpr s2r_I32LoadMem16U = s2r_LoadMemI<int32_t, uint16_t>;
  static auto constexpr s2r_I64LoadMem8S = s2r_LoadMemI<int64_t, int8_t>;
  static auto constexpr s2r_I64LoadMem8U = s2r_LoadMemI<int64_t, uint8_t>;
  static auto constexpr s2r_I64LoadMem16S = s2r_LoadMemI<int64_t, int16_t>;
  static auto constexpr s2r_I64LoadMem16U = s2r_LoadMemI<int64_t, uint16_t>;
  static auto constexpr s2r_I64LoadMem32S = s2r_LoadMemI<int64_t, int32_t>;
  static auto constexpr s2r_I64LoadMem32U = s2r_LoadMemI<int64_t, uint32_t>;
  static auto constexpr s2r_I32LoadMem = s2r_LoadMemI<int32_t>;
  static auto constexpr s2r_I64LoadMem = s2r_LoadMemI<int64_t>;

  static auto constexpr s2r_F32LoadMem = s2r_LoadMemF<float>;
  static auto constexpr s2r_F64LoadMem = s2r_LoadMemF<double>;

  static auto constexpr s2s_I32LoadMem8S = s2s_LoadMem<int32_t, int8_t>;
  static auto constexpr s2s_I32LoadMem8U = s2s_LoadMem<int32_t, uint8_t>;
  static auto constexpr s2s_I32LoadMem16S = s2s_LoadMem<int32_t, int16_t>;
  static auto constexpr s2s_I32LoadMem16U = s2s_LoadMem<int32_t, uint16_t>;
  static auto constexpr s2s_I64LoadMem8S = s2s_LoadMem<int64_t, int8_t>;
  static auto constexpr s2s_I64LoadMem8U = s2s_LoadMem<int64_t, uint8_t>;
  static auto constexpr s2s_I64LoadMem16S = s2s_LoadMem<int64_t, int16_t>;
  static auto constexpr s2s_I64LoadMem16U = s2s_LoadMem<int64_t, uint16_t>;
  static auto constexpr s2s_I64LoadMem32S = s2s_LoadMem<int64_t, int32_t>;
  static auto constexpr s2s_I64LoadMem32U = s2s_LoadMem<int64_t, uint32_t>;
  static auto constexpr s2s_I32LoadMem = s2s_LoadMem<int32_t>;
  static auto constexpr s2s_I64LoadMem = s2s_LoadMem<int64_t>;
  static auto constexpr s2s_F32LoadMem = s2s_LoadMem<float>;
  static auto constexpr s2s_F64LoadMem = s2s_LoadMem<double>;

  //////////////////////////////////////////////////////////////////////////////
  // LoadMem_LocalSet

  template <typename T, typename U = T>
  INSTRUCTION_HANDLER_FUNC r2s_LoadMem_LocalSet(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    memory_offset32_t offset = Read<memory_offset32_t>(code);
    uint64_t index = static_cast<uint32_t>(r0);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(U),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    U value = base::ReadUnalignedValue<U>(reinterpret_cast<Address>(address));

    slot_offset_t to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<T>(reinterpret_cast<Address>(sp + to),
                                 static_cast<T>(value));

    NextOp();
  }
  static auto constexpr r2s_I32LoadMem8S_LocalSet =
      r2s_LoadMem_LocalSet<int32_t, int8_t>;
  static auto constexpr r2s_I32LoadMem8U_LocalSet =
      r2s_LoadMem_LocalSet<int32_t, uint8_t>;
  static auto constexpr r2s_I32LoadMem16S_LocalSet =
      r2s_LoadMem_LocalSet<int32_t, int16_t>;
  static auto constexpr r2s_I32LoadMem16U_LocalSet =
      r2s_LoadMem_LocalSet<int32_t, uint16_t>;
  static auto constexpr r2s_I64LoadMem8S_LocalSet =
      r2s_LoadMem_LocalSet<int64_t, int8_t>;
  static auto constexpr r2s_I64LoadMem8U_LocalSet =
      r2s_LoadMem_LocalSet<int64_t, uint8_t>;
  static auto constexpr r2s_I64LoadMem16S_LocalSet =
      r2s_LoadMem_LocalSet<int64_t, int16_t>;
  static auto constexpr r2s_I64LoadMem16U_LocalSet =
      r2s_LoadMem_LocalSet<int64_t, uint16_t>;
  static auto constexpr r2s_I64LoadMem32S_LocalSet =
      r2s_LoadMem_LocalSet<int64_t, int32_t>;
  static auto constexpr r2s_I64LoadMem32U_LocalSet =
      r2s_LoadMem_LocalSet<int64_t, uint32_t>;
  static auto constexpr r2s_I32LoadMem_LocalSet = r2s_LoadMem_LocalSet<int32_t>;
  static auto constexpr r2s_I64LoadMem_LocalSet = r2s_LoadMem_LocalSet<int64_t>;
  static auto constexpr r2s_F32LoadMem_LocalSet = r2s_LoadMem_LocalSet<float>;
  static auto constexpr r2s_F64LoadMem_LocalSet = r2s_LoadMem_LocalSet<double>;

  static auto constexpr s2s_I32LoadMem8S_LocalSet =
      s2s_LoadMem_LocalSet<int32_t, int8_t>;
  static auto constexpr s2s_I32LoadMem8U_LocalSet =
      s2s_LoadMem_LocalSet<int32_t, uint8_t>;
  static auto constexpr s2s_I32LoadMem16S_LocalSet =
      s2s_LoadMem_LocalSet<int32_t, int16_t>;
  static auto constexpr s2s_I32LoadMem16U_LocalSet =
      s2s_LoadMem_LocalSet<int32_t, uint16_t>;
  static auto constexpr s2s_I64LoadMem8S_LocalSet =
      s2s_LoadMem_LocalSet<int64_t, int8_t>;
  static auto constexpr s2s_I64LoadMem8U_LocalSet =
      s2s_LoadMem_LocalSet<int64_t, uint8_t>;
  static auto constexpr s2s_I64LoadMem16S_LocalSet =
      s2s_LoadMem_LocalSet<int64_t, int16_t>;
  static auto constexpr s2s_I64LoadMem16U_LocalSet =
      s2s_LoadMem_LocalSet<int64_t, uint16_t>;
  static auto constexpr s2s_I64LoadMem32S_LocalSet =
      s2s_LoadMem_LocalSet<int64_t, int32_t>;
  static auto constexpr s2s_I64LoadMem32U_LocalSet =
      s2s_LoadMem_LocalSet<int64_t, uint32_t>;
  static auto constexpr s2s_I32LoadMem_LocalSet = s2s_LoadMem_LocalSet<int32_t>;
  static auto constexpr s2s_I64LoadMem_LocalSet = s2s_LoadMem_LocalSet<int64_t>;
  static auto constexpr s2s_F32LoadMem_LocalSet = s2s_LoadMem_LocalSet<float>;
  static auto constexpr s2s_F64LoadMem_LocalSet = s2s_LoadMem_LocalSet<double>;

  //////////////////////////////////////////////////////////////////////////////
  // StoreMem
  static auto constexpr r2s_I32StoreMem8 = r2s_StoreMemI<int32_t, int8_t>;
  static auto constexpr r2s_I32StoreMem16 = r2s_StoreMemI<int32_t, int16_t>;
  static auto constexpr r2s_I64StoreMem8 = r2s_StoreMemI<int64_t, int8_t>;
  static auto constexpr r2s_I64StoreMem16 = r2s_StoreMemI<int64_t, int16_t>;
  static auto constexpr r2s_I64StoreMem32 = r2s_StoreMemI<int64_t, int32_t>;
  static auto constexpr r2s_I32StoreMem = r2s_StoreMemI<int32_t>;
  static auto constexpr r2s_I64StoreMem = r2s_StoreMemI<int64_t>;

  static auto constexpr r2s_F32StoreMem = r2s_StoreMemF<float>;
  static auto constexpr r2s_F64StoreMem = r2s_StoreMemF<double>;

  static auto constexpr s2s_I32StoreMem8 = s2s_StoreMem<int32_t, int8_t>;
  static auto constexpr s2s_I32StoreMem16 = s2s_StoreMem<int32_t, int16_t>;
  static auto constexpr s2s_I64StoreMem8 = s2s_StoreMem<int64_t, int8_t>;
  static auto constexpr s2s_I64StoreMem16 = s2s_StoreMem<int64_t, int16_t>;
  static auto constexpr s2s_I64StoreMem32 = s2s_StoreMem<int64_t, int32_t>;
  static auto constexpr s2s_I32StoreMem = s2s_StoreMem<int32_t>;
  static auto constexpr s2s_I64StoreMem = s2s_StoreMem<int64_t>;
  static auto constexpr s2s_F32StoreMem = s2s_StoreMem<float>;
  static auto constexpr s2s_F64StoreMem = s2s_StoreMem<double>;

  //////////////////////////////////////////////////////////////////////////////
  // LocalGet_StoreMem

  template <typename T, typename U = T>
  INSTRUCTION_HANDLER_FUNC s2s_LocalGet_StoreMem(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t from = Read<slot_offset_t>(code);
    T value = base::ReadUnalignedValue<T>(reinterpret_cast<Address>(sp + from));

    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    memory_offset32_t offset = Read<memory_offset32_t>(code);
    uint64_t index = pop<uint32_t>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(U),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;

    base::WriteUnalignedValue<U>(
        reinterpret_cast<Address>(address),
        base::ReadUnalignedValue<U>(reinterpret_cast<Address>(&value)));

    NextOp();
  }
  static auto constexpr s2s_LocalGet_I32StoreMem8 =
      s2s_LocalGet_StoreMem<int32_t, int8_t>;
  static auto constexpr s2s_LocalGet_I32StoreMem16 =
      s2s_LocalGet_StoreMem<int32_t, int16_t>;
  static auto constexpr s2s_LocalGet_I64StoreMem8 =
      s2s_LocalGet_StoreMem<int64_t, int8_t>;
  static auto constexpr s2s_LocalGet_I64StoreMem16 =
      s2s_LocalGet_StoreMem<int64_t, int16_t>;
  static auto constexpr s2s_LocalGet_I64StoreMem32 =
      s2s_LocalGet_StoreMem<int64_t, int32_t>;
  static auto constexpr s2s_LocalGet_I32StoreMem =
      s2s_LocalGet_StoreMem<int32_t>;
  static auto constexpr s2s_LocalGet_I64StoreMem =
      s2s_LocalGet_StoreMem<int64_t>;
  static auto constexpr s2s_LocalGet_F32StoreMem = s2s_LocalGet_StoreMem<float>;
  static auto constexpr s2s_LocalGet_F64StoreMem =
      s2s_LocalGet_StoreMem<double>;

  //////////////////////////////////////////////////////////////////////////////
  // LoadStoreMem
  static auto constexpr r2s_I32LoadStoreMem = r2s_LoadStoreMem<int32_t>;
  static auto constexpr r2s_I64LoadStoreMem = r2s_LoadStoreMem<int64_t>;
  static auto constexpr r2s_F32LoadStoreMem = r2s_LoadStoreMem<float>;
  static auto constexpr r2s_F64LoadStoreMem = r2s_LoadStoreMem<double>;

  static auto constexpr s2s_I32LoadStoreMem = s2s_LoadStoreMem<int32_t>;
  static auto constexpr s2s_I64LoadStoreMem = s2s_LoadStoreMem<int64_t>;
  static auto constexpr s2s_F32LoadStoreMem = s2s_LoadStoreMem<float>;
  static auto constexpr s2s_F64LoadStoreMem = s2s_LoadStoreMem<double>;

#endif  // V8_DRUMBRAKE_BOUNDS_CHECKS

  //////////////////////////////////////////////////////////////////////////////
  // Select

  template <typename IntT>
  INSTRUCTION_HANDLER_FUNC r2r_SelectI(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    IntT val2 = pop<IntT>(sp, code, wasm_runtime);
    IntT val1 = pop<IntT>(sp, code, wasm_runtime);

    // r0: condition
    r0 = r0 ? val1 : val2;

    NextOp();
  }
  static auto constexpr r2r_I32Select = r2r_SelectI<int32_t>;
  static auto constexpr r2r_I64Select = r2r_SelectI<int64_t>;

  template <typename FloatT>
  INSTRUCTION_HANDLER_FUNC r2r_SelectF(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    FloatT val2 = pop<FloatT>(sp, code, wasm_runtime);
    FloatT val1 = pop<FloatT>(sp, code, wasm_runtime);

    // r0: condition
    fp0 = r0 ? val1 : val2;

    NextOp();
  }
  static auto constexpr r2r_F32Select = r2r_SelectF<float>;
  static auto constexpr r2r_F64Select = r2r_SelectF<double>;

  template <typename T>
  INSTRUCTION_HANDLER_FUNC r2s_Select(const uint8_t* code, uint32_t* sp,
                                      WasmInterpreterRuntime* wasm_runtime,
                                      int64_t r0, double fp0) {
    T val2 = pop<T>(sp, code, wasm_runtime);
    T val1 = pop<T>(sp, code, wasm_runtime);

    push<T>(sp, code, wasm_runtime, r0 ? val1 : val2);

    NextOp();
  }
  static auto constexpr r2s_I32Select = r2s_Select<int32_t>;
  static auto constexpr r2s_I64Select = r2s_Select<int64_t>;
  static auto constexpr r2s_F32Select = r2s_Select<float>;
  static auto constexpr r2s_F64Select = r2s_Select<double>;
  static auto constexpr r2s_S128Select = r2s_Select<Simd128>;

  INSTRUCTION_HANDLER_FUNC r2s_RefSelect(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    WasmRef val2 = pop<WasmRef>(sp, code, wasm_runtime);
    WasmRef val1 = pop<WasmRef>(sp, code, wasm_runtime);
    push<WasmRef>(sp, code, wasm_runtime, r0 ? val1 : val2);

    NextOp();
  }

  template <typename IntT>
  INSTRUCTION_HANDLER_FUNC s2r_SelectI(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    int32_t cond = pop<int32_t>(sp, code, wasm_runtime);
    IntT val2 = pop<IntT>(sp, code, wasm_runtime);
    IntT val1 = pop<IntT>(sp, code, wasm_runtime);

    r0 = cond ? val1 : val2;

    NextOp();
  }
  static auto constexpr s2r_I32Select = s2r_SelectI<int32_t>;
  static auto constexpr s2r_I64Select = s2r_SelectI<int64_t>;

  template <typename FloatT>
  INSTRUCTION_HANDLER_FUNC s2r_SelectF(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    int32_t cond = pop<int32_t>(sp, code, wasm_runtime);
    FloatT val2 = pop<FloatT>(sp, code, wasm_runtime);
    FloatT val1 = pop<FloatT>(sp, code, wasm_runtime);

    fp0 = cond ? val1 : val2;

    NextOp();
  }
  static auto constexpr s2r_F32Select = s2r_SelectF<float>;
  static auto constexpr s2r_F64Select = s2r_SelectF<double>;

  template <typename T>
  INSTRUCTION_HANDLER_FUNC s2s_Select(const uint8_t* code, uint32_t* sp,
                                      WasmInterpreterRuntime* wasm_runtime,
                                      int64_t r0, double fp0) {
    int32_t cond = pop<int32_t>(sp, code, wasm_runtime);
    T val2 = pop<T>(sp, code, wasm_runtime);
    T val1 = pop<T>(sp, code, wasm_runtime);

    push<T>(sp, code, wasm_runtime, cond ? val1 : val2);

    NextOp();
  }
  static auto constexpr s2s_I32Select = s2s_Select<int32_t>;
  static auto constexpr s2s_I64Select = s2s_Select<int64_t>;
  static auto constexpr s2s_F32Select = s2s_Select<float>;
  static auto constexpr s2s_F64Select = s2s_Select<double>;
  static auto constexpr s2s_S128Select = s2s_Select<Simd128>;

  INSTRUCTION_HANDLER_FUNC s2s_RefSelect(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    int32_t cond = pop<int32_t>(sp, code, wasm_runtime);
    WasmRef val2 = pop<WasmRef>(sp, code, wasm_runtime);
    WasmRef val1 = pop<WasmRef>(sp, code, wasm_runtime);
    push<WasmRef>(sp, code, wasm_runtime, cond ? val1 : val2);

    NextOp();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Binary arithmetic operators

#define FOREACH_ARITHMETIC_BINOP(V) \
  V(I32Add, uint32_t, r0, +, I32)   \
  V(I32Sub, uint32_t, r0, -, I32)   \
  V(I32Mul, uint32_t, r0, *, I32)   \
  V(I32And, uint32_t, r0, &, I32)   \
  V(I32Ior, uint32_t, r0, |, I32)   \
  V(I32Xor, uint32_t, r0, ^, I32)   \
  V(I64Add, uint64_t, r0, +, I64)   \
  V(I64Sub, uint64_t, r0, -, I64)   \
  V(I64Mul, uint64_t, r0, *, I64)   \
  V(I64And, uint64_t, r0, &, I64)   \
  V(I64Ior, uint64_t, r0, |, I64)   \
  V(I64Xor, uint64_t, r0, ^, I64)   \
  V(F32Add, float, fp0, +, F32)     \
  V(F32Sub, float, fp0, -, F32)     \
  V(F32Mul, float, fp0, *, F32)     \
  V(F32Div, float, fp0, /, F32)     \
  V(F64Add, double, fp0, +, F64)    \
  V(F64Sub, double, fp0, -, F64)    \
  V(F64Mul, double, fp0, *, F64)    \
  V(F64Div, double, fp0, /, F64)

#define DEFINE_BINOP(name, ctype, reg, op, type)                            \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    reg = static_cast<ctype>(lval op rval);                                 \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    push<ctype>(sp, code, wasm_runtime, lval op rval);                      \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    reg = static_cast<ctype>(lval op rval);                                 \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    push<ctype>(sp, code, wasm_runtime, lval op rval);                      \
    NextOp();                                                               \
  }
  FOREACH_ARITHMETIC_BINOP(DEFINE_BINOP)
#undef DEFINE_BINOP

  //////////////////////////////////////////////////////////////////////////////
  // Binary arithmetic operators that can trap

#define FOREACH_SIGNED_DIV_BINOP(V) \
  V(I32DivS, int32_t, r0, /, I32)   \
  V(I64DivS, int64_t, r0, /, I64)

#define FOREACH_UNSIGNED_DIV_BINOP(V) \
  V(I32DivU, uint32_t, r0, /, I32)    \
  V(I64DivU, uint64_t, r0, /, I64)

#define FOREACH_REM_BINOP(V)                 \
  V(I32RemS, int32_t, r0, ExecuteRemS, I32)  \
  V(I64RemS, int64_t, r0, ExecuteRemS, I64)  \
  V(I32RemU, uint32_t, r0, ExecuteRemU, I32) \
  V(I64RemU, uint64_t, r0, ExecuteRemU, I64)

#define FOREACH_TRAPPING_BINOP(V) \
  FOREACH_SIGNED_DIV_BINOP(V)     \
  FOREACH_UNSIGNED_DIV_BINOP(V)   \
  FOREACH_REM_BINOP(V)

#define DEFINE_BINOP(name, ctype, reg, op, type)                            \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapDivByZero)                                      \
    } else if (rval == -1 && lval == std::numeric_limits<ctype>::min()) {   \
      TRAP(TrapReason::kTrapDivUnrepresentable)                             \
    } else {                                                                \
      reg = static_cast<ctype>(lval op rval);                               \
    }                                                                       \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapDivByZero)                                      \
    } else if (rval == -1 && lval == std::numeric_limits<ctype>::min()) {   \
      TRAP(TrapReason::kTrapDivUnrepresentable)                             \
    } else {                                                                \
      push<ctype>(sp, code, wasm_runtime, lval op rval);                    \
    }                                                                       \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapDivByZero)                                      \
    } else if (rval == -1 && lval == std::numeric_limits<ctype>::min()) {   \
      TRAP(TrapReason::kTrapDivUnrepresentable)                             \
    } else {                                                                \
      reg = static_cast<ctype>(lval op rval);                               \
    }                                                                       \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapDivByZero)                                      \
    } else if (rval == -1 && lval == std::numeric_limits<ctype>::min()) {   \
      TRAP(TrapReason::kTrapDivUnrepresentable)                             \
    } else {                                                                \
      push<ctype>(sp, code, wasm_runtime, lval op rval);                    \
    }                                                                       \
    NextOp();                                                               \
  }
  FOREACH_SIGNED_DIV_BINOP(DEFINE_BINOP)
#undef DEFINE_BINOP

#define DEFINE_BINOP(name, ctype, reg, op, type)                            \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapDivByZero)                                      \
    } else {                                                                \
      reg = static_cast<ctype>(lval op rval);                               \
    }                                                                       \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapDivByZero)                                      \
    } else {                                                                \
      push<ctype>(sp, code, wasm_runtime, lval op rval);                    \
    }                                                                       \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapDivByZero)                                      \
    } else {                                                                \
      reg = static_cast<ctype>(lval op rval);                               \
    }                                                                       \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapDivByZero)                                      \
    } else {                                                                \
      push<ctype>(sp, code, wasm_runtime, lval op rval);                    \
    }                                                                       \
    NextOp();                                                               \
  }
  FOREACH_UNSIGNED_DIV_BINOP(DEFINE_BINOP)
#undef DEFINE_BINOP

#define DEFINE_BINOP(name, ctype, reg, op, type)                            \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapRemByZero)                                      \
    } else {                                                                \
      reg = static_cast<ctype>(op(lval, rval));                             \
    }                                                                       \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapRemByZero)                                      \
    } else {                                                                \
      push<ctype>(sp, code, wasm_runtime, op(lval, rval));                  \
    }                                                                       \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapRemByZero);                                     \
    } else {                                                                \
      reg = static_cast<ctype>(op(lval, rval));                             \
    }                                                                       \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    if (rval == 0) {                                                        \
      TRAP(TrapReason::kTrapRemByZero)                                      \
    } else {                                                                \
      push<ctype>(sp, code, wasm_runtime, op(lval, rval));                  \
    }                                                                       \
    NextOp();                                                               \
  }
  FOREACH_REM_BINOP(DEFINE_BINOP)
#undef DEFINE_BINOP

  //////////////////////////////////////////////////////////////////////////////
  // Comparison operators

#define FOREACH_COMPARISON_BINOP(V) \
  V(I32Eq, uint32_t, r0, ==, I32)   \
  V(I32Ne, uint32_t, r0, !=, I32)   \
  V(I32LtU, uint32_t, r0, <, I32)   \
  V(I32LeU, uint32_t, r0, <=, I32)  \
  V(I32GtU, uint32_t, r0, >, I32)   \
  V(I32GeU, uint32_t, r0, >=, I32)  \
  V(I32LtS, int32_t, r0, <, I32)    \
  V(I32LeS, int32_t, r0, <=, I32)   \
  V(I32GtS, int32_t, r0, >, I32)    \
  V(I32GeS, int32_t, r0, >=, I32)   \
  V(I64Eq, uint64_t, r0, ==, I64)   \
  V(I64Ne, uint64_t, r0, !=, I64)   \
  V(I64LtU, uint64_t, r0, <, I64)   \
  V(I64LeU, uint64_t, r0, <=, I64)  \
  V(I64GtU, uint64_t, r0, >, I64)   \
  V(I64GeU, uint64_t, r0, >=, I64)  \
  V(I64LtS, int64_t, r0, <, I64)    \
  V(I64LeS, int64_t, r0, <=, I64)   \
  V(I64GtS, int64_t, r0, >, I64)    \
  V(I64GeS, int64_t, r0, >=, I64)   \
  V(F32Eq, float, fp0, ==, F32)     \
  V(F32Ne, float, fp0, !=, F32)     \
  V(F32Lt, float, fp0, <, F32)      \
  V(F32Le, float, fp0, <=, F32)     \
  V(F32Gt, float, fp0, >, F32)      \
  V(F32Ge, float, fp0, >=, F32)     \
  V(F64Eq, double, fp0, ==, F64)    \
  V(F64Ne, double, fp0, !=, F64)    \
  V(F64Lt, double, fp0, <, F64)     \
  V(F64Le, double, fp0, <=, F64)    \
  V(F64Gt, double, fp0, >, F64)     \
  V(F64Ge, double, fp0, >=, F64)

#define DEFINE_BINOP(name, ctype, reg, op, type)                            \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    r0 = (lval op rval) ? 1 : 0;                                            \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    push<int32_t>(sp, code, wasm_runtime, lval op rval ? 1 : 0);            \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    r0 = (lval op rval) ? 1 : 0;                                            \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    push<int32_t>(sp, code, wasm_runtime, lval op rval ? 1 : 0);            \
    NextOp();                                                               \
  }
  FOREACH_COMPARISON_BINOP(DEFINE_BINOP)
#undef DEFINE_BINOP

  //////////////////////////////////////////////////////////////////////////////
  // More binary operators

#define FOREACH_MORE_BINOP(V)                                                \
  V(I32Shl, uint32_t, r0, (lval << (rval & 31)), I32)                        \
  V(I32ShrU, uint32_t, r0, (lval >> (rval & 31)), I32)                       \
  V(I32ShrS, int32_t, r0, (lval >> (rval & 31)), I32)                        \
  V(I64Shl, uint64_t, r0, (lval << (rval & 63)), I64)                        \
  V(I64ShrU, uint64_t, r0, (lval >> (rval & 63)), I64)                       \
  V(I64ShrS, int64_t, r0, (lval >> (rval & 63)), I64)                        \
  V(I32Rol, uint32_t, r0, (base::bits::RotateLeft32(lval, rval & 31)), I32)  \
  V(I32Ror, uint32_t, r0, (base::bits::RotateRight32(lval, rval & 31)), I32) \
  V(I64Rol, uint64_t, r0, (base::bits::RotateLeft64(lval, rval & 63)), I64)  \
  V(I64Ror, uint64_t, r0, (base::bits::RotateRight64(lval, rval & 63)), I64) \
  V(F32Min, float, fp0, (JSMin<float>(lval, rval)), F32)                     \
  V(F32Max, float, fp0, (JSMax<float>(lval, rval)), F32)                     \
  V(F64Min, double, fp0, (JSMin<double>(lval, rval)), F64)                   \
  V(F64Max, double, fp0, (JSMax<double>(lval, rval)), F64)                   \
  V(F32CopySign, float, fp0,                                                 \
    Float32::FromBits((base::ReadUnalignedValue<uint32_t>(                   \
                           reinterpret_cast<Address>(&lval)) &               \
                       ~kFloat32SignBitMask) |                               \
                      (base::ReadUnalignedValue<uint32_t>(                   \
                           reinterpret_cast<Address>(&rval)) &               \
                       kFloat32SignBitMask))                                 \
        .get_scalar(),                                                       \
    F32)                                                                     \
  V(F64CopySign, double, fp0,                                                \
    Float64::FromBits((base::ReadUnalignedValue<uint64_t>(                   \
                           reinterpret_cast<Address>(&lval)) &               \
                       ~kFloat64SignBitMask) |                               \
                      (base::ReadUnalignedValue<uint64_t>(                   \
                           reinterpret_cast<Address>(&rval)) &               \
                       kFloat64SignBitMask))                                 \
        .get_scalar(),                                                       \
    F64)

#define DEFINE_BINOP(name, ctype, reg, op, type)                            \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    reg = static_cast<ctype>(op);                                           \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = static_cast<ctype>(reg);                                   \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    push<ctype>(sp, code, wasm_runtime, op);                                \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    reg = static_cast<ctype>(op);                                           \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype rval = pop<ctype>(sp, code, wasm_runtime);                        \
    ctype lval = pop<ctype>(sp, code, wasm_runtime);                        \
    push<ctype>(sp, code, wasm_runtime, op);                                \
    NextOp();                                                               \
  }
  FOREACH_MORE_BINOP(DEFINE_BINOP)
#undef DEFINE_BINOP

  //////////////////////////////////////////////////////////////////////////////
  // Unary operators

#define FOREACH_SIMPLE_UNOP(V)                       \
  V(F32Abs, float, fp0, abs(val), F32)               \
  V(F32Neg, float, fp0, -val, F32)                   \
  V(F32Ceil, float, fp0, ceilf(val), F32)            \
  V(F32Floor, float, fp0, floorf(val), F32)          \
  V(F32Trunc, float, fp0, truncf(val), F32)          \
  V(F32NearestInt, float, fp0, nearbyintf(val), F32) \
  V(F32Sqrt, float, fp0, sqrt(val), F32)             \
  V(F64Abs, double, fp0, abs(val), F64)              \
  V(F64Neg, double, fp0, (-val), F64)                \
  V(F64Ceil, double, fp0, ceil(val), F64)            \
  V(F64Floor, double, fp0, floor(val), F64)          \
  V(F64Trunc, double, fp0, trunc(val), F64)          \
  V(F64NearestInt, double, fp0, nearbyint(val), F64) \
  V(F64Sqrt, double, fp0, sqrt(val), F64)

#define DEFINE_UNOP(name, ctype, reg, op, type)                             \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype val = static_cast<ctype>(reg);                                    \
    reg = static_cast<ctype>(op);                                           \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype val = static_cast<ctype>(reg);                                    \
    push<ctype>(sp, code, wasm_runtime, op);                                \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype val = pop<ctype>(sp, code, wasm_runtime);                         \
    reg = static_cast<ctype>(op);                                           \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    ctype val = pop<ctype>(sp, code, wasm_runtime);                         \
    push<ctype>(sp, code, wasm_runtime, op);                                \
    NextOp();                                                               \
  }
  FOREACH_SIMPLE_UNOP(DEFINE_UNOP)
#undef DEFINE_UNOP

  //////////////////////////////////////////////////////////////////////////////
  // Numeric conversion operators

#define FOREACH_ADDITIONAL_CONVERT_UNOP(V) \
  V(I32ConvertI64, int64_t, I64, r0, int32_t, I32, r0)

  INSTRUCTION_HANDLER_FUNC r2r_I32ConvertI64(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    r0 &= 0xffffffff;
    NextOp();
  }
  INSTRUCTION_HANDLER_FUNC r2s_I32ConvertI64(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    push<int32_t>(sp, code, wasm_runtime, r0 & 0xffffffff);
    NextOp();
  }
  INSTRUCTION_HANDLER_FUNC s2r_I32ConvertI64(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    r0 = 0xffffffff & pop<int64_t>(sp, code, wasm_runtime);
    NextOp();
  }
  INSTRUCTION_HANDLER_FUNC s2s_I32ConvertI64(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    push<int32_t>(sp, code, wasm_runtime,
                  0xffffffff & pop<int64_t>(sp, code, wasm_runtime));
    NextOp();
  }

#define FOREACH_I64_CONVERT_FROM_FLOAT_UNOP(V)          \
  V(I64SConvertF32, float, F32, fp0, int64_t, I64, r0)  \
  V(I64SConvertF64, double, F64, fp0, int64_t, I64, r0) \
  V(I64UConvertF32, float, F32, fp0, uint64_t, I64, r0) \
  V(I64UConvertF64, double, F64, fp0, uint64_t, I64, r0)

#define FOREACH_I32_CONVERT_FROM_FLOAT_UNOP(V)          \
  V(I32SConvertF32, float, F32, fp0, int32_t, I32, r0)  \
  V(I32UConvertF32, float, F32, fp0, uint32_t, I32, r0) \
  V(I32SConvertF64, double, F64, fp0, int32_t, I32, r0) \
  V(I32UConvertF64, double, F64, fp0, uint32_t, I32, r0)

#define FOREACH_OTHER_CONVERT_UNOP(V)                     \
  V(I64SConvertI32, int32_t, I32, r0, int64_t, I64, r0)   \
  V(I64UConvertI32, uint32_t, I32, r0, uint64_t, I64, r0) \
  V(F32SConvertI32, int32_t, I32, r0, float, F32, fp0)    \
  V(F32UConvertI32, uint32_t, I32, r0, float, F32, fp0)   \
  V(F32SConvertI64, int64_t, I64, r0, float, F32, fp0)    \
  V(F32UConvertI64, uint64_t, I64, r0, float, F32, fp0)   \
  V(F32ConvertF64, double, F64, fp0, float, F32, fp0)     \
  V(F64SConvertI32, int32_t, I32, r0, double, F64, fp0)   \
  V(F64UConvertI32, uint32_t, I32, r0, double, F64, fp0)  \
  V(F64SConvertI64, int64_t, I64, r0, double, F64, fp0)   \
  V(F64UConvertI64, uint64_t, I64, r0, double, F64, fp0)  \
  V(F64ConvertF32, float, F32, fp0, double, F64, fp0)

#define FOREACH_CONVERT_UNOP(V)          \
  FOREACH_I64_CONVERT_FROM_FLOAT_UNOP(V) \
  FOREACH_I32_CONVERT_FROM_FLOAT_UNOP(V) \
  FOREACH_OTHER_CONVERT_UNOP(V)

#define DEFINE_UNOP(name, from_ctype, from_type, from_reg, to_ctype, to_type, \
                    to_reg)                                                   \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    if (!base::IsValueInRangeForNumericType<to_ctype>(from_reg)) {            \
      TRAP(TrapReason::kTrapFloatUnrepresentable)                             \
    } else {                                                                  \
      to_reg = static_cast<to_ctype>(static_cast<from_ctype>(from_reg));      \
    }                                                                         \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    if (!base::IsValueInRangeForNumericType<to_ctype>(from_reg)) {            \
      TRAP(TrapReason::kTrapFloatUnrepresentable)                             \
    } else {                                                                  \
      to_ctype val = static_cast<from_ctype>(from_reg);                       \
      push<to_ctype>(sp, code, wasm_runtime, val);                            \
    }                                                                         \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    from_ctype from_val = pop<from_ctype>(sp, code, wasm_runtime);            \
    if (!base::IsValueInRangeForNumericType<to_ctype>(from_val)) {            \
      TRAP(TrapReason::kTrapFloatUnrepresentable)                             \
    } else {                                                                  \
      to_reg = static_cast<to_ctype>(from_val);                               \
    }                                                                         \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    from_ctype from_val = pop<from_ctype>(sp, code, wasm_runtime);            \
    if (!base::IsValueInRangeForNumericType<to_ctype>(from_val)) {            \
      TRAP(TrapReason::kTrapFloatUnrepresentable)                             \
    } else {                                                                  \
      to_ctype val = static_cast<to_ctype>(from_val);                         \
      push<to_ctype>(sp, code, wasm_runtime, val);                            \
    }                                                                         \
    NextOp();                                                                 \
  }
  FOREACH_I64_CONVERT_FROM_FLOAT_UNOP(DEFINE_UNOP)
#undef DEFINE_UNOP

#define DEFINE_UNOP(name, from_ctype, from_type, from_reg, to_ctype, to_type, \
                    to_reg)                                                   \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    if (!is_inbounds<to_ctype>(from_reg)) {                                   \
      TRAP(TrapReason::kTrapFloatUnrepresentable)                             \
    } else {                                                                  \
      to_reg = static_cast<to_ctype>(static_cast<from_ctype>(from_reg));      \
    }                                                                         \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    if (!is_inbounds<to_ctype>(from_reg)) {                                   \
      TRAP(TrapReason::kTrapFloatUnrepresentable)                             \
    } else {                                                                  \
      to_ctype val = static_cast<from_ctype>(from_reg);                       \
      push<to_ctype>(sp, code, wasm_runtime, val);                            \
    }                                                                         \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    from_ctype from_val = pop<from_ctype>(sp, code, wasm_runtime);            \
    if (!is_inbounds<to_ctype>(from_val)) {                                   \
      TRAP(TrapReason::kTrapFloatUnrepresentable)                             \
    } else {                                                                  \
      to_reg = static_cast<to_ctype>(from_val);                               \
    }                                                                         \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    from_ctype from_val = pop<from_ctype>(sp, code, wasm_runtime);            \
    if (!is_inbounds<to_ctype>(from_val)) {                                   \
      TRAP(TrapReason::kTrapFloatUnrepresentable)                             \
    } else {                                                                  \
      to_ctype val = static_cast<to_ctype>(from_val);                         \
      push<to_ctype>(sp, code, wasm_runtime, val);                            \
    }                                                                         \
    NextOp();                                                                 \
  }
  FOREACH_I32_CONVERT_FROM_FLOAT_UNOP(DEFINE_UNOP)
#undef DEFINE_UNOP

#define DEFINE_UNOP(name, from_ctype, from_type, from_reg, to_ctype, to_type, \
                    to_reg)                                                   \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    to_reg = static_cast<to_ctype>(static_cast<from_ctype>(from_reg));        \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    to_ctype val = static_cast<from_ctype>(from_reg);                         \
    push<to_ctype>(sp, code, wasm_runtime, val);                              \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    to_reg = static_cast<to_ctype>(pop<from_ctype>(sp, code, wasm_runtime));  \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    to_ctype val = pop<from_ctype>(sp, code, wasm_runtime);                   \
    push<to_ctype>(sp, code, wasm_runtime, val);                              \
    NextOp();                                                                 \
  }
  FOREACH_OTHER_CONVERT_UNOP(DEFINE_UNOP)
#undef DEFINE_UNOP

  //////////////////////////////////////////////////////////////////////////////
  // Numeric reinterpret operators

#define FOREACH_REINTERPRET_UNOP(V)                        \
  V(F32ReinterpretI32, int32_t, I32, r0, float, F32, fp0)  \
  V(F64ReinterpretI64, int64_t, I64, r0, double, F64, fp0) \
  V(I32ReinterpretF32, float, F32, fp0, int32_t, I32, r0)  \
  V(I64ReinterpretF64, double, F64, fp0, int64_t, I64, r0)

#define DEFINE_UNOP(name, from_ctype, from_type, from_reg, to_ctype, to_type,  \
                    to_reg)                                                    \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,       \
                                      WasmInterpreterRuntime* wasm_runtime,    \
                                      int64_t r0, double fp0) {                \
    from_ctype value = static_cast<from_ctype>(from_reg);                      \
    to_reg =                                                                   \
        base::ReadUnalignedValue<to_ctype>(reinterpret_cast<Address>(&value)); \
    NextOp();                                                                  \
  }                                                                            \
                                                                               \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,       \
                                      WasmInterpreterRuntime* wasm_runtime,    \
                                      int64_t r0, double fp0) {                \
    from_ctype val = static_cast<from_ctype>(from_reg);                        \
    push<to_ctype>(                                                            \
        sp, code, wasm_runtime,                                                \
        base::ReadUnalignedValue<to_ctype>(reinterpret_cast<Address>(&val)));  \
    NextOp();                                                                  \
  }                                                                            \
                                                                               \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,       \
                                      WasmInterpreterRuntime* wasm_runtime,    \
                                      int64_t r0, double fp0) {                \
    from_ctype val = pop<from_ctype>(sp, code, wasm_runtime);                  \
    to_reg =                                                                   \
        base::ReadUnalignedValue<to_ctype>(reinterpret_cast<Address>(&val));   \
    NextOp();                                                                  \
  }                                                                            \
                                                                               \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,       \
                                      WasmInterpreterRuntime* wasm_runtime,    \
                                      int64_t r0, double fp0) {                \
    from_ctype val = pop<from_ctype>(sp, code, wasm_runtime);                  \
    push<to_ctype>(                                                            \
        sp, code, wasm_runtime,                                                \
        base::ReadUnalignedValue<to_ctype>(reinterpret_cast<Address>(&val)));  \
    NextOp();                                                                  \
  }
  FOREACH_REINTERPRET_UNOP(DEFINE_UNOP)
#undef DEFINE_UNOP

  //////////////////////////////////////////////////////////////////////////////
  // Bit operators

#define FOREACH_BITS_UNOP(V)                                                   \
  V(I32Clz, uint32_t, I32, uint32_t, I32, base::bits::CountLeadingZeros(val))  \
  V(I32Ctz, uint32_t, I32, uint32_t, I32, base::bits::CountTrailingZeros(val)) \
  V(I32Popcnt, uint32_t, I32, uint32_t, I32, base::bits::CountPopulation(val)) \
  V(I32Eqz, uint32_t, I32, int32_t, I32, val == 0 ? 1 : 0)                     \
  V(I64Clz, uint64_t, I64, uint64_t, I64, base::bits::CountLeadingZeros(val))  \
  V(I64Ctz, uint64_t, I64, uint64_t, I64, base::bits::CountTrailingZeros(val)) \
  V(I64Popcnt, uint64_t, I64, uint64_t, I64, base::bits::CountPopulation(val)) \
  V(I64Eqz, uint64_t, I64, int32_t, I32, val == 0 ? 1 : 0)

#define DEFINE_REG_BINOP(name, from_ctype, from_type, to_ctype, to_type, op) \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,     \
                                      WasmInterpreterRuntime* wasm_runtime,  \
                                      int64_t r0, double fp0) {              \
    from_ctype val = static_cast<from_ctype>(r0);                            \
    r0 = static_cast<to_ctype>(op);                                          \
    NextOp();                                                                \
  }                                                                          \
                                                                             \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,     \
                                      WasmInterpreterRuntime* wasm_runtime,  \
                                      int64_t r0, double fp0) {              \
    from_ctype val = static_cast<from_ctype>(r0);                            \
    push<to_ctype>(sp, code, wasm_runtime, op);                              \
    NextOp();                                                                \
  }                                                                          \
                                                                             \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,     \
                                      WasmInterpreterRuntime* wasm_runtime,  \
                                      int64_t r0, double fp0) {              \
    from_ctype val = pop<from_ctype>(sp, code, wasm_runtime);                \
    r0 = op;                                                                 \
    NextOp();                                                                \
  }                                                                          \
                                                                             \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,     \
                                      WasmInterpreterRuntime* wasm_runtime,  \
                                      int64_t r0, double fp0) {              \
    from_ctype val = pop<from_ctype>(sp, code, wasm_runtime);                \
    push<to_ctype>(sp, code, wasm_runtime, op);                              \
    NextOp();                                                                \
  }
  FOREACH_BITS_UNOP(DEFINE_REG_BINOP)
#undef DEFINE_REG_BINOP

  //////////////////////////////////////////////////////////////////////////////
  // Sign extension operators

#define FOREACH_EXTENSION_UNOP(V)              \
  V(I32SExtendI8, int8_t, I32, int32_t, I32)   \
  V(I32SExtendI16, int16_t, I32, int32_t, I32) \
  V(I64SExtendI8, int8_t, I64, int64_t, I64)   \
  V(I64SExtendI16, int16_t, I64, int64_t, I64) \
  V(I64SExtendI32, int32_t, I64, int64_t, I64)

#define DEFINE_UNOP(name, from_ctype, from_type, to_ctype, to_type)         \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    from_ctype val = static_cast<from_ctype>(static_cast<to_ctype>(r0));    \
    r0 = static_cast<to_ctype>(val);                                        \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    from_ctype val = static_cast<from_ctype>(static_cast<to_ctype>(r0));    \
    push<to_ctype>(sp, code, wasm_runtime, val);                            \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    from_ctype val =                                                        \
        static_cast<from_ctype>(pop<to_ctype>(sp, code, wasm_runtime));     \
    r0 = static_cast<to_ctype>(val);                                        \
    NextOp();                                                               \
  }                                                                         \
                                                                            \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,    \
                                      WasmInterpreterRuntime* wasm_runtime, \
                                      int64_t r0, double fp0) {             \
    from_ctype val =                                                        \
        static_cast<from_ctype>(pop<to_ctype>(sp, code, wasm_runtime));     \
    push<to_ctype>(sp, code, wasm_runtime, val);                            \
    NextOp();                                                               \
  }
  FOREACH_EXTENSION_UNOP(DEFINE_UNOP)
#undef DEFINE_UNOP

  //////////////////////////////////////////////////////////////////////////////
  // Saturated truncation operators

#define FOREACH_TRUNCSAT_UNOP(V)                            \
  V(I32SConvertSatF32, float, F32, fp0, int32_t, I32, r0)   \
  V(I32UConvertSatF32, float, F32, fp0, uint32_t, I32, r0)  \
  V(I32SConvertSatF64, double, F64, fp0, int32_t, I32, r0)  \
  V(I32UConvertSatF64, double, F64, fp0, uint32_t, I32, r0) \
  V(I64SConvertSatF32, float, F32, fp0, int64_t, I64, r0)   \
  V(I64UConvertSatF32, float, F32, fp0, uint64_t, I64, r0)  \
  V(I64SConvertSatF64, double, F64, fp0, int64_t, I64, r0)  \
  V(I64UConvertSatF64, double, F64, fp0, uint64_t, I64, r0)

#define DEFINE_UNOP(name, from_ctype, from_type, from_reg, to_ctype, to_type, \
                    to_reg)                                                   \
  INSTRUCTION_HANDLER_FUNC r2r_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    to_reg =                                                                  \
        base::saturated_cast<to_ctype>(static_cast<from_ctype>(from_reg));    \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC r2s_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    to_ctype val =                                                            \
        base::saturated_cast<to_ctype>(static_cast<from_ctype>(from_reg));    \
    push<to_ctype>(sp, code, wasm_runtime, val);                              \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC s2r_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    to_reg = base::saturated_cast<to_ctype>(                                  \
        pop<from_ctype>(sp, code, wasm_runtime));                             \
    NextOp();                                                                 \
  }                                                                           \
                                                                              \
  INSTRUCTION_HANDLER_FUNC s2s_##name(const uint8_t* code, uint32_t* sp,      \
                                      WasmInterpreterRuntime* wasm_runtime,   \
                                      int64_t r0, double fp0) {               \
    to_ctype val = base::saturated_cast<to_ctype>(                            \
        pop<from_ctype>(sp, code, wasm_runtime));                             \
    push<to_ctype>(sp, code, wasm_runtime, val);                              \
    NextOp();                                                                 \
  }
  FOREACH_TRUNCSAT_UNOP(DEFINE_UNOP)
#undef DEFINE_UNOP

  //////////////////////////////////////////////////////////////////////////////

  INSTRUCTION_HANDLER_FUNC s2s_MemoryGrow(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    uint32_t delta_pages = pop<uint32_t>(sp, code, wasm_runtime);

    int32_t result = wasm_runtime->MemoryGrow(delta_pages);

    push<int32_t>(sp, code, wasm_runtime, result);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Memory64Grow(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int64_t result = -1;

    uint64_t delta_pages = pop<uint64_t>(sp, code, wasm_runtime);

    if (delta_pages <= std::numeric_limits<uint32_t>::max()) {
      result = wasm_runtime->MemoryGrow(static_cast<uint32_t>(delta_pages));
    }

    push<int64_t>(sp, code, wasm_runtime, result);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_MemorySize(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    uint64_t result = wasm_runtime->MemorySize();
    push<uint32_t>(sp, code, wasm_runtime, static_cast<uint32_t>(result));

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Memory64Size(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint64_t result = wasm_runtime->MemorySize();
    push<uint64_t>(sp, code, wasm_runtime, result);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Return(const uint8_t* code, uint32_t* sp,
                                      WasmInterpreterRuntime* wasm_runtime,
                                      int64_t r0, double fp0) {
    // Break the chain of calls.
    Read<int32_t>(code);
  }

  INSTRUCTION_HANDLER_FUNC s2s_Branch(const uint8_t* code, uint32_t* sp,
                                      WasmInterpreterRuntime* wasm_runtime,
                                      int64_t r0, double fp0) {
    int32_t target_offset = Read<int32_t>(code);
    code += (target_offset - kCodeOffsetSize);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_BranchIf(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    int64_t cond = r0;

    int32_t if_true_offset = Read<int32_t>(code);
    if (cond) {
      // If condition is true, jump to the target branch.
      code += (if_true_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_BranchIf(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    int32_t cond = pop<int32_t>(sp, code, wasm_runtime);

    int32_t if_true_offset = Read<int32_t>(code);
    if (cond) {
      // If condition is true, jump to the target branch.
      code += (if_true_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_BranchIfWithParams(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int64_t cond = r0;

    int32_t if_false_offset = Read<int32_t>(code);
    if (!cond) {
      // If condition is not true, jump to the false branch.
      code += (if_false_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_BranchIfWithParams(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int32_t cond = pop<int32_t>(sp, code, wasm_runtime);

    int32_t if_false_offset = Read<int32_t>(code);
    if (!cond) {
      // If condition is not true, jump to the false branch.
      code += (if_false_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_If(const uint8_t* code, uint32_t* sp,
                                  WasmInterpreterRuntime* wasm_runtime,
                                  int64_t r0, double fp0) {
    int64_t cond = r0;

    int32_t target_offset = Read<int32_t>(code);
    if (!cond) {
      code += (target_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_If(const uint8_t* code, uint32_t* sp,
                                  WasmInterpreterRuntime* wasm_runtime,
                                  int64_t r0, double fp0) {
    int32_t cond = pop<int32_t>(sp, code, wasm_runtime);

    int32_t target_offset = Read<int32_t>(code);
    if (!cond) {
      code += (target_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Else(const uint8_t* code, uint32_t* sp,
                                    WasmInterpreterRuntime* wasm_runtime,
                                    int64_t r0, double fp0) {
    int32_t target_offset = Read<int32_t>(code);
    code += (target_offset - kCodeOffsetSize);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Catch(const uint8_t* code, uint32_t* sp,
                                     WasmInterpreterRuntime* wasm_runtime,
                                     int64_t r0, double fp0) {
    int32_t target_offset = Read<int32_t>(code);
    code += (target_offset - kCodeOffsetSize);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CallFunction(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t function_index = Read<int32_t>(code);
    uint32_t stack_pos = Read<int32_t>(code);
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    uint32_t ref_stack_fp_offset = Read<int32_t>(code);
    slot_offset_t return_slot_offset = 0;
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      return_slot_offset = Read<slot_offset_t>(code);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    wasm_runtime->ExecuteFunction(code, function_index, stack_pos,
                                  ref_stack_fp_offset, slot_offset,
                                  return_slot_offset);
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_ReturnCall(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    slot_offset_t rets_size = Read<slot_offset_t>(code);
    slot_offset_t args_size = Read<slot_offset_t>(code);
    uint32_t rets_refs = Read<int32_t>(code);
    uint32_t args_refs = Read<int32_t>(code);
    uint32_t function_index = Read<int32_t>(code);
    uint32_t stack_pos = Read<int32_t>(code);
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    uint32_t ref_stack_fp_offset = Read<int32_t>(code);
    slot_offset_t return_slot_offset = 0;
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      return_slot_offset = Read<slot_offset_t>(code);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    // Moves back the stack frame to the caller stack frame.
    wasm_runtime->UnwindCurrentStackFrame(sp, slot_offset, rets_size, args_size,
                                          rets_refs, args_refs,
                                          ref_stack_fp_offset);

    // Do not call wasm_runtime->ExecuteFunction(), which would add a
    // new C++ stack frame.
    wasm_runtime->PrepareTailCall(code, function_index, stack_pos,
                                  return_slot_offset);
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CallImportedFunction(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t function_index = Read<int32_t>(code);
    uint32_t stack_pos = Read<int32_t>(code);
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    uint32_t ref_stack_fp_offset = Read<int32_t>(code);
    slot_offset_t return_slot_offset = 0;
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      return_slot_offset = Read<slot_offset_t>(code);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    wasm_runtime->ExecuteImportedFunction(code, function_index, stack_pos,
                                          ref_stack_fp_offset, slot_offset,
                                          return_slot_offset);
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_ReturnCallImportedFunction(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t rets_size = Read<slot_offset_t>(code);
    slot_offset_t args_size = Read<slot_offset_t>(code);
    uint32_t rets_refs = Read<int32_t>(code);
    uint32_t args_refs = Read<int32_t>(code);
    uint32_t function_index = Read<int32_t>(code);
    uint32_t stack_pos = Read<int32_t>(code);
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    uint32_t ref_stack_fp_offset = Read<int32_t>(code);
    slot_offset_t return_slot_offset = 0;
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      return_slot_offset = Read<slot_offset_t>(code);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    // Moves back the stack frame to the caller stack frame.
    wasm_runtime->UnwindCurrentStackFrame(sp, slot_offset, rets_size, args_size,
                                          rets_refs, args_refs,
                                          ref_stack_fp_offset);

    wasm_runtime->ExecuteImportedFunction(code, function_index, stack_pos, 0, 0,
                                          return_slot_offset, true);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CallIndirect(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t entry_index = pop<uint32_t>(sp, code, wasm_runtime);
    uint32_t table_index = Read<int32_t>(code);
    uint32_t sig_index = Read<int32_t>(code);
    uint32_t stack_pos = Read<int32_t>(code);
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    uint32_t ref_stack_fp_offset = Read<int32_t>(code);
    slot_offset_t return_slot_offset = 0;
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      return_slot_offset = Read<slot_offset_t>(code);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    // This function can trap.
    wasm_runtime->ExecuteIndirectCall(code, table_index, sig_index, entry_index,
                                      stack_pos, sp, ref_stack_fp_offset,
                                      slot_offset, return_slot_offset, false);
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CallIndirect64(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint64_t entry_index_64 = pop<uint64_t>(sp, code, wasm_runtime);
    if (entry_index_64 > std::numeric_limits<uint32_t>::max()) {
      TRAP(TrapReason::kTrapTableOutOfBounds)
    }
    uint32_t entry_index = static_cast<uint32_t>(entry_index_64);
    uint32_t table_index = Read<int32_t>(code);
    uint32_t sig_index = Read<int32_t>(code);
    uint32_t stack_pos = Read<int32_t>(code);
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    uint32_t ref_stack_fp_offset = Read<int32_t>(code);
    slot_offset_t return_slot_offset = 0;
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      return_slot_offset = Read<slot_offset_t>(code);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    // This function can trap.
    wasm_runtime->ExecuteIndirectCall(code, table_index, sig_index, entry_index,
                                      stack_pos, sp, ref_stack_fp_offset,
                                      slot_offset, return_slot_offset, false);
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_ReturnCallIndirect(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t rets_size = Read<slot_offset_t>(code);
    slot_offset_t args_size = Read<slot_offset_t>(code);
    uint32_t rets_refs = Read<int32_t>(code);
    uint32_t args_refs = Read<int32_t>(code);
    uint32_t entry_index = pop<uint32_t>(sp, code, wasm_runtime);
    uint32_t table_index = Read<int32_t>(code);
    uint32_t sig_index = Read<int32_t>(code);
    uint32_t stack_pos = Read<int32_t>(code);
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    uint32_t ref_stack_fp_offset = Read<int32_t>(code);
    slot_offset_t return_slot_offset = 0;
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      return_slot_offset = Read<slot_offset_t>(code);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    // Moves back the stack frame to the caller stack frame.
    wasm_runtime->UnwindCurrentStackFrame(sp, slot_offset, rets_size, args_size,
                                          rets_refs, args_refs,
                                          ref_stack_fp_offset);

    // This function can trap.
    wasm_runtime->ExecuteIndirectCall(code, table_index, sig_index, entry_index,
                                      stack_pos, sp, 0, 0, return_slot_offset,
                                      true);
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_ReturnCallIndirect64(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t rets_size = Read<slot_offset_t>(code);
    slot_offset_t args_size = Read<slot_offset_t>(code);
    uint32_t rets_refs = Read<int32_t>(code);
    uint32_t args_refs = Read<int32_t>(code);
    uint64_t entry_index_64 = pop<uint64_t>(sp, code, wasm_runtime);
    if (entry_index_64 > std::numeric_limits<uint32_t>::max()) {
      TRAP(TrapReason::kTrapTableOutOfBounds)
    }
    uint32_t entry_index = static_cast<uint32_t>(entry_index_64);
    uint32_t table_index = Read<int32_t>(code);
    uint32_t sig_index = Read<int32_t>(code);
    uint32_t stack_pos = Read<int32_t>(code);
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    uint32_t ref_stack_fp_offset = Read<int32_t>(code);
    slot_offset_t return_slot_offset = 0;
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      return_slot_offset = Read<slot_offset_t>(code);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    // Moves back the stack frame to the caller stack frame.
    wasm_runtime->UnwindCurrentStackFrame(sp, slot_offset, rets_size, args_size,
                                          rets_refs, args_refs,
                                          ref_stack_fp_offset);

    // This function can trap.
    wasm_runtime->ExecuteIndirectCall(code, table_index, sig_index, entry_index,
                                      stack_pos, sp, 0, 0, return_slot_offset,
                                      true);
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_BrTable(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    uint32_t cond = static_cast<int32_t>(r0);

    uint32_t table_length = Read<int32_t>(code);
    uint32_t index = cond < table_length ? cond : table_length;

    int32_t target_offset = base::ReadUnalignedValue<int32_t>(
        reinterpret_cast<Address>(code + index * kCodeOffsetSize));
    code += (target_offset + index * kCodeOffsetSize);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_BrTable(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    uint32_t cond = pop<uint32_t>(sp, code, wasm_runtime);

    uint32_t table_length = Read<int32_t>(code);
    uint32_t index = cond < table_length ? cond : table_length;

    int32_t target_offset = base::ReadUnalignedValue<int32_t>(
        reinterpret_cast<Address>(code + index * kCodeOffsetSize));
    code += (target_offset + index * kCodeOffsetSize);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CopySlotMulti(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t params_count = Read<int32_t>(code);
    DCHECK(params_count > 1 && params_count < 32);

    uint32_t arg_size_mask = Read<int32_t>(code);

    slot_offset_t to = Read<slot_offset_t>(code);
    for (uint32_t i = 0; i < params_count; i++) {
      slot_offset_t from = Read<slot_offset_t>(code);
      bool is_64 = arg_size_mask & (1 << i);
      if (is_64) {
        base::WriteUnalignedValue<uint64_t>(
            reinterpret_cast<Address>(sp + to),
            base::ReadUnalignedValue<uint64_t>(
                reinterpret_cast<Address>(sp + from)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
        if (v8_flags.trace_drumbrake_execution &&
            v8_flags.trace_drumbrake_execution_verbose) {
          wasm_runtime->Trace("COPYSLOT64 %d %d %" PRIx64 "\n", from, to,
                              base::ReadUnalignedValue<uint64_t>(
                                  reinterpret_cast<Address>(sp + to)));
        }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

        to += sizeof(uint64_t) / sizeof(uint32_t);
      } else {
        base::WriteUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(sp + to),
            base::ReadUnalignedValue<uint32_t>(
                reinterpret_cast<Address>(sp + from)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
        if (v8_flags.trace_drumbrake_execution &&
            v8_flags.trace_drumbrake_execution_verbose) {
          wasm_runtime->Trace("COPYSLOT32 %d %d %08x\n", from, to,
                              *reinterpret_cast<int32_t*>(sp + to));
        }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

        to += sizeof(uint32_t) / sizeof(uint32_t);
      }
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CopySlot_ll(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    slot_offset_t from0 = Read<slot_offset_t>(code);
    slot_offset_t from1 = Read<slot_offset_t>(code);

    base::WriteUnalignedValue<uint32_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(sp + from0)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT32 %d %d %08x\n", from0, to,
                          *reinterpret_cast<int32_t*>(sp + to));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    to += sizeof(uint32_t) / sizeof(uint32_t);

    base::WriteUnalignedValue<uint32_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(sp + from1)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT32 %d %d %08x\n", from1, to,
                          *reinterpret_cast<int32_t*>(sp + to));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CopySlot_lq(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    slot_offset_t from0 = Read<slot_offset_t>(code);
    slot_offset_t from1 = Read<slot_offset_t>(code);

    base::WriteUnalignedValue<uint64_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint64_t>(
            reinterpret_cast<Address>(sp + from0)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT64 %d %d %" PRIx64 "\n", from0, to,
                          base::ReadUnalignedValue<uint64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    to += sizeof(uint64_t) / sizeof(uint32_t);

    base::WriteUnalignedValue<uint32_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(sp + from1)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT32 %d %d %08x\n", from1, to,
                          *reinterpret_cast<int32_t*>(sp + to));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CopySlot_ql(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    slot_offset_t from0 = Read<slot_offset_t>(code);
    slot_offset_t from1 = Read<slot_offset_t>(code);

    base::WriteUnalignedValue<uint32_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(sp + from0)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT32 %d %d %08x\n", from0, to,
                          *reinterpret_cast<int32_t*>(sp + to));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    to += sizeof(uint32_t) / sizeof(uint32_t);

    base::WriteUnalignedValue<uint64_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint64_t>(
            reinterpret_cast<Address>(sp + from1)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT64 %d %d %" PRIx64 "\n", from1, to,
                          base::ReadUnalignedValue<uint64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CopySlot_qq(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    slot_offset_t from0 = Read<slot_offset_t>(code);
    slot_offset_t from1 = Read<slot_offset_t>(code);

    base::WriteUnalignedValue<uint64_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint64_t>(
            reinterpret_cast<Address>(sp + from0)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT64 %d %d %" PRIx64 "\n", from0, to,
                          base::ReadUnalignedValue<uint64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    to += sizeof(uint64_t) / sizeof(uint32_t);

    base::WriteUnalignedValue<uint64_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint64_t>(
            reinterpret_cast<Address>(sp + from1)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT64 %d %d %" PRIx64 "\n", from1, to,
                          base::ReadUnalignedValue<uint64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CopySlot32(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    slot_offset_t from = Read<slot_offset_t>(code);
    slot_offset_t to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<uint32_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(sp + from)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT32 %d %d %08x\n", from, to,
                          *reinterpret_cast<int32_t*>(sp + to));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CopySlot32x2(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t from = Read<slot_offset_t>(code);
    slot_offset_t to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<uint32_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(sp + from)));
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT32 %d %d %08x\n", from, to,
                          base::ReadUnalignedValue<int32_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    from = Read<slot_offset_t>(code);
    to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<uint32_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(sp + from)));
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT32 %d %d %08x\n", from, to,
                          base::ReadUnalignedValue<int32_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CopySlot64(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    slot_offset_t from = Read<slot_offset_t>(code);
    slot_offset_t to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<uint64_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint64_t>(
            reinterpret_cast<Address>(sp + from)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT64 %d %d %" PRIx64 "\n", from, to,
                          base::ReadUnalignedValue<uint64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CopySlot128(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    slot_offset_t from = Read<slot_offset_t>(code);
    slot_offset_t to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<Simd128>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<Simd128>(
            reinterpret_cast<Address>(sp + from)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace(
          "COPYSLOT128 %d %d %" PRIx64 "`%" PRIx64 "\n", from, to,
          base::ReadUnalignedValue<uint64_t>(
              reinterpret_cast<Address>(sp + to)),
          base::ReadUnalignedValue<uint64_t>(
              reinterpret_cast<Address>(sp + to + sizeof(uint64_t))));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CopySlot64x2(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t from = Read<slot_offset_t>(code);
    slot_offset_t to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<uint64_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint64_t>(
            reinterpret_cast<Address>(sp + from)));
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT64 %d %d %" PRIx64 "\n", from, to,
                          base::ReadUnalignedValue<uint64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    from = Read<slot_offset_t>(code);
    to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<uint64_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint64_t>(
            reinterpret_cast<Address>(sp + from)));
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOT64 %d %d %" PRIx64 "\n", from, to,
                          base::ReadUnalignedValue<uint64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CopySlotRef(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    uint32_t from = Read<int32_t>(code);
    uint32_t to = Read<int32_t>(code);
    wasm_runtime->StoreWasmRef(to, wasm_runtime->ExtractWasmRef(from));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYSLOTREF %d %d\n", from, to);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_PreserveCopySlot32(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t from = Read<slot_offset_t>(code);
    slot_offset_t to = Read<slot_offset_t>(code);
    slot_offset_t preserve = Read<slot_offset_t>(code);

    base::WriteUnalignedValue<uint32_t>(
        reinterpret_cast<Address>(sp + preserve),
        base::ReadUnalignedValue<uint32_t>(reinterpret_cast<Address>(sp + to)));
    base::WriteUnalignedValue<uint32_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(sp + from)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("PRESERVECOPYSLOT32 %d %d %08x\n", from, to,
                          base::ReadUnalignedValue<int32_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_PreserveCopySlot64(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t from = Read<slot_offset_t>(code);
    slot_offset_t to = Read<slot_offset_t>(code);
    slot_offset_t preserve = Read<slot_offset_t>(code);

    base::WriteUnalignedValue<uint64_t>(
        reinterpret_cast<Address>(sp + preserve),
        base::ReadUnalignedValue<uint64_t>(reinterpret_cast<Address>(sp + to)));
    base::WriteUnalignedValue<uint64_t>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<uint64_t>(
            reinterpret_cast<Address>(sp + from)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("PRESERVECOPYSLOT64 %d %d %" PRIx64 "\n", from, to,
                          base::ReadUnalignedValue<uint64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_PreserveCopySlot128(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t from = Read<slot_offset_t>(code);
    slot_offset_t to = Read<slot_offset_t>(code);
    slot_offset_t preserve = Read<slot_offset_t>(code);

    base::WriteUnalignedValue<Simd128>(
        reinterpret_cast<Address>(sp + preserve),
        base::ReadUnalignedValue<Simd128>(reinterpret_cast<Address>(sp + to)));
    base::WriteUnalignedValue<Simd128>(
        reinterpret_cast<Address>(sp + to),
        base::ReadUnalignedValue<Simd128>(
            reinterpret_cast<Address>(sp + from)));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace(
          "PRESERVECOPYSLOT64 %d %d %" PRIx64 "`%" PRIx64 "\n", from, to,
          base::ReadUnalignedValue<uint64_t>(
              reinterpret_cast<Address>(sp + to)),
          base::ReadUnalignedValue<uint64_t>(
              reinterpret_cast<Address>(sp + to + sizeof(uint64_t))));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_CopyR0ToSlot32(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<int32_t>(reinterpret_cast<Address>(sp + to),
                                       static_cast<int32_t>(r0));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYR0TOSLOT32 %d %08x\n", to,
                          base::ReadUnalignedValue<int32_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_CopyR0ToSlot64(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<int64_t>(reinterpret_cast<Address>(sp + to), r0);

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYR0TOSLOT64 %d %" PRIx64 "\n", to,
                          base::ReadUnalignedValue<int64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_CopyFp0ToSlot32(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<float>(reinterpret_cast<Address>(sp + to),
                                     static_cast<float>(fp0));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYFP0TOSLOT32 %d %08x\n", to,
                          base::ReadUnalignedValue<uint32_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_CopyFp0ToSlot64(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<double>(reinterpret_cast<Address>(sp + to), fp0);

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("COPYFP0TOSLOT64 %d %" PRIx64 "\n", to,
                          base::ReadUnalignedValue<uint64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_PreserveCopyR0ToSlot32(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    slot_offset_t preserve = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<int32_t>(
        reinterpret_cast<Address>(sp + preserve),
        base::ReadUnalignedValue<int32_t>(reinterpret_cast<Address>(sp + to)));
    base::WriteUnalignedValue<int32_t>(reinterpret_cast<Address>(sp + to),
                                       static_cast<int32_t>(r0));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("PRESERVECOPYR0TOSLOT32 %d %d %08x\n", to, preserve,
                          base::ReadUnalignedValue<uint32_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_PreserveCopyR0ToSlot64(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    slot_offset_t preserve = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<int64_t>(
        reinterpret_cast<Address>(sp + preserve),
        base::ReadUnalignedValue<int64_t>(reinterpret_cast<Address>(sp + to)));
    base::WriteUnalignedValue<int64_t>(reinterpret_cast<Address>(sp + to), r0);

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("PRESERVECOPYR0TOSLOT64 %d %d %" PRIx64 "\n", to,
                          preserve,
                          base::ReadUnalignedValue<uint64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_PreserveCopyFp0ToSlot32(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    slot_offset_t preserve = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<float>(
        reinterpret_cast<Address>(sp + preserve),
        base::ReadUnalignedValue<float>(reinterpret_cast<Address>(sp + to)));
    base::WriteUnalignedValue<float>(reinterpret_cast<Address>(sp + to),
                                     static_cast<float>(fp0));

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("PRESERVECOPYFP0TOSLOT32 %d %d %08x\n", to, preserve,
                          base::ReadUnalignedValue<uint32_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC r2s_PreserveCopyFp0ToSlot64(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t to = Read<slot_offset_t>(code);
    slot_offset_t preserve = Read<slot_offset_t>(code);
    base::WriteUnalignedValue<double>(
        reinterpret_cast<Address>(sp + preserve),
        base::ReadUnalignedValue<double>(reinterpret_cast<Address>(sp + to)));
    base::WriteUnalignedValue<double>(reinterpret_cast<Address>(sp + to), fp0);

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution &&
        v8_flags.trace_drumbrake_execution_verbose) {
      wasm_runtime->Trace("PRESERVECOPYFP0TOSLOT64 %d %d %" PRIx64 "\n", to,
                          preserve,
                          base::ReadUnalignedValue<uint64_t>(
                              reinterpret_cast<Address>(sp + to)));
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_RefNull(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    const uint32_t ref_bitfield = Read<int32_t>(code);
    ValueType ref_type = ValueType::FromRawBitField(ref_bitfield);

    push<WasmRef>(sp, code, wasm_runtime,
                  handle(wasm_runtime->GetNullValue(ref_type),
                         wasm_runtime->GetIsolate()));

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_RefIsNull(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
    push<int32_t>(sp, code, wasm_runtime, wasm_runtime->IsRefNull(ref) ? 1 : 0);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_RefFunc(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    uint32_t index = Read<int32_t>(code);
    push<WasmRef>(sp, code, wasm_runtime, wasm_runtime->GetFunctionRef(index));

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_RefEq(const uint8_t* code, uint32_t* sp,
                                     WasmInterpreterRuntime* wasm_runtime,
                                     int64_t r0, double fp0) {
    WasmRef lhs = pop<WasmRef>(sp, code, wasm_runtime);
    WasmRef rhs = pop<WasmRef>(sp, code, wasm_runtime);
    push<int32_t>(sp, code, wasm_runtime, lhs.is_identical_to(rhs) ? 1 : 0);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_MemoryInit(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    uint32_t data_segment_index = Read<int32_t>(code);
    uint64_t size = pop<uint32_t>(sp, code, wasm_runtime);
    uint64_t src = pop<uint32_t>(sp, code, wasm_runtime);
    uint64_t dst = pop<uint32_t>(sp, code, wasm_runtime);

    // This function can trap.
    wasm_runtime->MemoryInit(code, data_segment_index, dst, src, size);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Memory64Init(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t data_segment_index = Read<int32_t>(code);
    uint64_t size = pop<uint32_t>(sp, code, wasm_runtime);
    uint64_t src = pop<uint32_t>(sp, code, wasm_runtime);
    uint64_t dst = pop<uint64_t>(sp, code, wasm_runtime);

    // This function can trap.
    wasm_runtime->MemoryInit(code, data_segment_index, dst, src, size);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_DataDrop(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    uint32_t index = Read<int32_t>(code);

    wasm_runtime->DataDrop(index);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_MemoryCopy(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    uint64_t size = pop<uint32_t>(sp, code, wasm_runtime);
    uint64_t src = pop<uint32_t>(sp, code, wasm_runtime);
    uint64_t dst = pop<uint32_t>(sp, code, wasm_runtime);

    // This function can trap.
    wasm_runtime->MemoryCopy(code, dst, src, size);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Memory64Copy(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint64_t size = pop<uint64_t>(sp, code, wasm_runtime);
    uint64_t value = pop<uint64_t>(sp, code, wasm_runtime);
    uint64_t dst = pop<uint64_t>(sp, code, wasm_runtime);

    // This function can trap.
    wasm_runtime->MemoryCopy(code, dst, value, size);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_MemoryFill(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    uint64_t size = pop<uint32_t>(sp, code, wasm_runtime);
    uint32_t value = pop<uint32_t>(sp, code, wasm_runtime);
    uint64_t dst = pop<uint32_t>(sp, code, wasm_runtime);

    // This function can trap.
    wasm_runtime->MemoryFill(code, dst, value, size);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Memory64Fill(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint64_t size = pop<uint64_t>(sp, code, wasm_runtime);
    uint32_t value = pop<uint32_t>(sp, code, wasm_runtime);
    uint64_t dst = pop<uint64_t>(sp, code, wasm_runtime);

    // This function can trap.
    wasm_runtime->MemoryFill(code, dst, value, size);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_TableGet(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);
    uint32_t entry_index = pop<uint32_t>(sp, code, wasm_runtime);

    // This function can trap.
    WasmRef ref;
    if (wasm_runtime->TableGet(code, table_index, entry_index, &ref)) {
      push<WasmRef>(sp, code, wasm_runtime, ref);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Table64Get(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);
    uint64_t entry_index_64 = pop<uint64_t>(sp, code, wasm_runtime);

    if (entry_index_64 > std::numeric_limits<uint32_t>::max()) {
      TRAP(TrapReason::kTrapTableOutOfBounds)
    }

    uint32_t entry_index = static_cast<uint32_t>(entry_index_64);

    // This function can trap.
    WasmRef ref;
    if (wasm_runtime->TableGet(code, table_index, entry_index, &ref)) {
      push<WasmRef>(sp, code, wasm_runtime, ref);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_TableSet(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
    uint32_t entry_index = pop<uint32_t>(sp, code, wasm_runtime);

    // This function can trap.
    wasm_runtime->TableSet(code, table_index, entry_index, ref);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Table64Set(const uint8_t* code, uint32_t* sp,
                                          WasmInterpreterRuntime* wasm_runtime,
                                          int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
    uint64_t entry_index_64 = pop<uint64_t>(sp, code, wasm_runtime);

    if (entry_index_64 > std::numeric_limits<uint32_t>::max()) {
      TRAP(TrapReason::kTrapTableOutOfBounds)
    }

    uint32_t entry_index = static_cast<uint32_t>(entry_index_64);

    // This function can trap.
    wasm_runtime->TableSet(code, table_index, entry_index, ref);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_TableInit(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);
    uint32_t element_segment_index = Read<int32_t>(code);
    uint32_t size = pop<uint32_t>(sp, code, wasm_runtime);
    uint32_t src = pop<uint32_t>(sp, code, wasm_runtime);
    uint32_t dst = pop<uint32_t>(sp, code, wasm_runtime);

    // This function can trap.
    wasm_runtime->TableInit(code, table_index, element_segment_index, dst, src,
                            size);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Table64Init(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);
    uint32_t element_segment_index = Read<int32_t>(code);
    uint32_t size = pop<uint32_t>(sp, code, wasm_runtime);
    uint32_t src = pop<uint32_t>(sp, code, wasm_runtime);
    uint64_t dst_64 = pop<uint64_t>(sp, code, wasm_runtime);

    if (dst_64 > std::numeric_limits<uint32_t>::max()) {
      TRAP(TrapReason::kTrapTableOutOfBounds)
    }

    uint32_t dst = static_cast<uint32_t>(dst_64);

    // This function can trap.
    wasm_runtime->TableInit(code, table_index, element_segment_index, dst, src,
                            size);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_ElemDrop(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    uint32_t index = Read<int32_t>(code);

    wasm_runtime->ElemDrop(index);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_TableCopy(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    uint32_t dst_table_index = Read<int32_t>(code);
    uint32_t src_table_index = Read<int32_t>(code);
    auto size = pop<uint32_t>(sp, code, wasm_runtime);
    auto src = pop<uint32_t>(sp, code, wasm_runtime);
    auto dst = pop<uint32_t>(sp, code, wasm_runtime);

    // This function can trap.
    wasm_runtime->TableCopy(code, dst_table_index, src_table_index, dst, src,
                            size);

    NextOp();
  }

  template <typename IntN, typename IntM, typename IntK>
  INSTRUCTION_HANDLER_FUNC s2s_Table64CopyImpl(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t dst_table_index = Read<int32_t>(code);
    uint32_t src_table_index = Read<int32_t>(code);
    auto size_64 = pop<IntK>(sp, code, wasm_runtime);
    auto src_64 = pop<IntM>(sp, code, wasm_runtime);
    auto dst_64 = pop<IntN>(sp, code, wasm_runtime);

    if (src_64 > std::numeric_limits<uint32_t>::max() ||
        dst_64 > std::numeric_limits<uint32_t>::max() ||
        size_64 > std::numeric_limits<uint32_t>::max()) {
      TRAP(TrapReason::kTrapTableOutOfBounds)
    }

    uint32_t size = static_cast<uint32_t>(size_64);
    uint32_t src = static_cast<uint32_t>(src_64);
    uint32_t dst = static_cast<uint32_t>(dst_64);

    // This function can trap.
    wasm_runtime->TableCopy(code, dst_table_index, src_table_index, dst, src,
                            size);

    NextOp();
  }
  static auto constexpr s2s_Table64Copy_32_64_32 =
      s2s_Table64CopyImpl<uint32_t, uint64_t, uint32_t>;
  static auto constexpr s2s_Table64Copy_64_32_32 =
      s2s_Table64CopyImpl<uint64_t, uint32_t, uint32_t>;
  static auto constexpr s2s_Table64Copy_64_64_64 =
      s2s_Table64CopyImpl<uint64_t, uint64_t, uint64_t>;

  INSTRUCTION_HANDLER_FUNC s2s_TableGrow(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);
    uint32_t delta = pop<uint32_t>(sp, code, wasm_runtime);
    WasmRef value = pop<WasmRef>(sp, code, wasm_runtime);

    uint32_t result = wasm_runtime->TableGrow(table_index, delta, value);
    push<int32_t>(sp, code, wasm_runtime, result);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Table64Grow(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);
    uint64_t delta_64 = pop<uint64_t>(sp, code, wasm_runtime);
    WasmRef value = pop<WasmRef>(sp, code, wasm_runtime);

    if (delta_64 > std::numeric_limits<uint32_t>::max()) {
      push<int64_t>(sp, code, wasm_runtime, -1);
    } else {
      uint32_t delta = static_cast<uint32_t>(delta_64);
      uint32_t result = wasm_runtime->TableGrow(table_index, delta, value);
      push<int64_t>(sp, code, wasm_runtime,
                    static_cast<int64_t>(static_cast<int32_t>(result)));
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_TableSize(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);

    uint32_t size = wasm_runtime->TableSize(table_index);
    push<int32_t>(sp, code, wasm_runtime, size);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Table64Size(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);

    uint64_t size = wasm_runtime->TableSize(table_index);
    push<uint64_t>(sp, code, wasm_runtime, size);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_TableFill(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);
    uint32_t count = pop<uint32_t>(sp, code, wasm_runtime);
    WasmRef value = pop<WasmRef>(sp, code, wasm_runtime);
    uint32_t start = pop<uint32_t>(sp, code, wasm_runtime);

    // This function can trap.
    wasm_runtime->TableFill(code, table_index, count, value, start);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Table64Fill(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    uint32_t table_index = Read<int32_t>(code);
    uint64_t count_64 = pop<uint64_t>(sp, code, wasm_runtime);
    WasmRef value = pop<WasmRef>(sp, code, wasm_runtime);
    uint64_t start_64 = pop<uint64_t>(sp, code, wasm_runtime);

    if (count_64 > std::numeric_limits<uint32_t>::max() ||
        start_64 > std::numeric_limits<uint32_t>::max()) {
      TRAP(TrapReason::kTrapTableOutOfBounds)
    }

    uint32_t count = static_cast<uint32_t>(count_64);
    uint32_t start = static_cast<uint32_t>(start_64);

    // This function can trap.
    wasm_runtime->TableFill(code, table_index, count, value, start);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_OnLoopBegin(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    wasm_runtime->WasmStackCheck(code, code);
    wasm_runtime->ResetCurrentHandleScope();

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_OnLoopBeginNoRefSlots(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    wasm_runtime->WasmStackCheck(code, code);

    NextOp();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Atomics operators

  template <typename MemIdx = uint32_t, typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC s2s_AtomicNotify(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int32_t val = pop<int32_t>(sp, code, wasm_runtime);

    uint64_t offset = Read<MemOffsetT>(code);
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;
    // Check alignment.
    const uint32_t align_mask = sizeof(int32_t) - 1;
    if (V8_UNLIKELY((effective_index & align_mask) != 0)) {
      TRAP(TrapReason::kTrapUnalignedAccess)
    }
    // Check bounds.
    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(uint64_t),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    int32_t result = wasm_runtime->AtomicNotify(effective_index, val);
    push<int32_t>(sp, code, wasm_runtime, result);

    NextOp();
  }
  static auto constexpr s2s_AtomicNotify_Idx64 =
      s2s_AtomicNotify<uint64_t, memory_offset64_t>;

  template <typename MemIdx = uint32_t, typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC s2s_I32AtomicWait(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int64_t timeout = pop<int64_t>(sp, code, wasm_runtime);
    int32_t val = pop<int32_t>(sp, code, wasm_runtime);

    uint64_t offset = Read<MemOffsetT>(code);
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;
    // Check alignment.
    const uint32_t align_mask = sizeof(int32_t) - 1;
    if (V8_UNLIKELY((effective_index & align_mask) != 0)) {
      TRAP(TrapReason::kTrapUnalignedAccess)
    }
    // Check bounds.
    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(uint64_t),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }
    // Check atomics wait allowed.
    if (!wasm_runtime->AllowsAtomicsWait()) {
      TRAP(TrapReason::kTrapUnreachable)
    }

    int32_t result = wasm_runtime->I32AtomicWait(effective_index, val, timeout);
    push<int32_t>(sp, code, wasm_runtime, result);

    NextOp();
  }
  static auto constexpr s2s_I32AtomicWait_Idx64 =
      s2s_I32AtomicWait<uint64_t, memory_offset64_t>;

  template <typename MemIdx = uint32_t, typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC s2s_I64AtomicWait(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int64_t timeout = pop<int64_t>(sp, code, wasm_runtime);
    int64_t val = pop<int64_t>(sp, code, wasm_runtime);

    uint64_t offset = Read<MemOffsetT>(code);
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;
    // Check alignment.
    const uint32_t align_mask = sizeof(int64_t) - 1;
    if (V8_UNLIKELY((effective_index & align_mask) != 0)) {
      TRAP(TrapReason::kTrapUnalignedAccess)
    }
    // Check bounds.
    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(uint64_t),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }
    // Check atomics wait allowed.
    if (!wasm_runtime->AllowsAtomicsWait()) {
      TRAP(TrapReason::kTrapUnreachable)
    }

    int32_t result = wasm_runtime->I64AtomicWait(effective_index, val, timeout);
    push<int32_t>(sp, code, wasm_runtime, result);

    NextOp();
  }
  static auto constexpr s2s_I64AtomicWait_Idx64 =
      s2s_I64AtomicWait<uint64_t, memory_offset64_t>;

  INSTRUCTION_HANDLER_FUNC s2s_AtomicFence(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    NextOp();
  }

#define FOREACH_ATOMIC_BINOP(V)                                                \
  V(I32AtomicAdd, Uint32, uint32_t, I32, uint32_t, I32, std::atomic_fetch_add) \
  V(I32AtomicAdd8U, Uint8, uint8_t, I32, uint32_t, I32, std::atomic_fetch_add) \
  V(I32AtomicAdd16U, Uint16, uint16_t, I32, uint32_t, I32,                     \
    std::atomic_fetch_add)                                                     \
  V(I32AtomicSub, Uint32, uint32_t, I32, uint32_t, I32, std::atomic_fetch_sub) \
  V(I32AtomicSub8U, Uint8, uint8_t, I32, uint32_t, I32, std::atomic_fetch_sub) \
  V(I32AtomicSub16U, Uint16, uint16_t, I32, uint32_t, I32,                     \
    std::atomic_fetch_sub)                                                     \
  V(I32AtomicAnd, Uint32, uint32_t, I32, uint32_t, I32, std::atomic_fetch_and) \
  V(I32AtomicAnd8U, Uint8, uint8_t, I32, uint32_t, I32, std::atomic_fetch_and) \
  V(I32AtomicAnd16U, Uint16, uint16_t, I32, uint32_t, I32,                     \
    std::atomic_fetch_and)                                                     \
  V(I32AtomicOr, Uint32, uint32_t, I32, uint32_t, I32, std::atomic_fetch_or)   \
  V(I32AtomicOr8U, Uint8, uint8_t, I32, uint32_t, I32, std::atomic_fetch_or)   \
  V(I32AtomicOr16U, Uint16, uint16_t, I32, uint32_t, I32,                      \
    std::atomic_fetch_or)                                                      \
  V(I32AtomicXor, Uint32, uint32_t, I32, uint32_t, I32, std::atomic_fetch_xor) \
  V(I32AtomicXor8U, Uint8, uint8_t, I32, uint32_t, I32, std::atomic_fetch_xor) \
  V(I32AtomicXor16U, Uint16, uint16_t, I32, uint32_t, I32,                     \
    std::atomic_fetch_xor)                                                     \
  V(I32AtomicExchange, Uint32, uint32_t, I32, uint32_t, I32,                   \
    std::atomic_exchange)                                                      \
  V(I32AtomicExchange8U, Uint8, uint8_t, I32, uint32_t, I32,                   \
    std::atomic_exchange)                                                      \
  V(I32AtomicExchange16U, Uint16, uint16_t, I32, uint32_t, I32,                \
    std::atomic_exchange)                                                      \
  V(I64AtomicAdd, Uint64, uint64_t, I64, uint64_t, I64, std::atomic_fetch_add) \
  V(I64AtomicAdd8U, Uint8, uint8_t, I32, uint64_t, I64, std::atomic_fetch_add) \
  V(I64AtomicAdd16U, Uint16, uint16_t, I32, uint64_t, I64,                     \
    std::atomic_fetch_add)                                                     \
  V(I64AtomicAdd32U, Uint32, uint32_t, I32, uint64_t, I64,                     \
    std::atomic_fetch_add)                                                     \
  V(I64AtomicSub, Uint64, uint64_t, I64, uint64_t, I64, std::atomic_fetch_sub) \
  V(I64AtomicSub8U, Uint8, uint8_t, I32, uint64_t, I64, std::atomic_fetch_sub) \
  V(I64AtomicSub16U, Uint16, uint16_t, I32, uint64_t, I64,                     \
    std::atomic_fetch_sub)                                                     \
  V(I64AtomicSub32U, Uint32, uint32_t, I32, uint64_t, I64,                     \
    std::atomic_fetch_sub)                                                     \
  V(I64AtomicAnd, Uint64, uint64_t, I64, uint64_t, I64, std::atomic_fetch_and) \
  V(I64AtomicAnd8U, Uint8, uint8_t, I32, uint64_t, I64, std::atomic_fetch_and) \
  V(I64AtomicAnd16U, Uint16, uint16_t, I32, uint64_t, I64,                     \
    std::atomic_fetch_and)                                                     \
  V(I64AtomicAnd32U, Uint32, uint32_t, I32, uint64_t, I64,                     \
    std::atomic_fetch_and)                                                     \
  V(I64AtomicOr, Uint64, uint64_t, I64, uint64_t, I64, std::atomic_fetch_or)   \
  V(I64AtomicOr8U, Uint8, uint8_t, I32, uint64_t, I64, std::atomic_fetch_or)   \
  V(I64AtomicOr16U, Uint16, uint16_t, I32, uint64_t, I64,                      \
    std::atomic_fetch_or)                                                      \
  V(I64AtomicOr32U, Uint32, uint32_t, I32, uint64_t, I64,                      \
    std::atomic_fetch_or)                                                      \
  V(I64AtomicXor, Uint64, uint64_t, I64, uint64_t, I64, std::atomic_fetch_xor) \
  V(I64AtomicXor8U, Uint8, uint8_t, I32, uint64_t, I64, std::atomic_fetch_xor) \
  V(I64AtomicXor16U, Uint16, uint16_t, I32, uint64_t, I64,                     \
    std::atomic_fetch_xor)                                                     \
  V(I64AtomicXor32U, Uint32, uint32_t, I32, uint64_t, I64,                     \
    std::atomic_fetch_xor)                                                     \
  V(I64AtomicExchange, Uint64, uint64_t, I64, uint64_t, I64,                   \
    std::atomic_exchange)                                                      \
  V(I64AtomicExchange8U, Uint8, uint8_t, I32, uint64_t, I64,                   \
    std::atomic_exchange)                                                      \
  V(I64AtomicExchange16U, Uint16, uint16_t, I32, uint64_t, I64,                \
    std::atomic_exchange)                                                      \
  V(I64AtomicExchange32U, Uint32, uint32_t, I32, uint64_t, I64,                \
    std::atomic_exchange)

#define ATOMIC_BINOP(name, Type, ctype, type, op_ctype, op_type, operation)    \
  template <typename MemIdx, typename MemOffsetT>                              \
  INSTRUCTION_HANDLER_FUNC s2s_##name##I(const uint8_t* code, uint32_t* sp,    \
                                         WasmInterpreterRuntime* wasm_runtime, \
                                         int64_t r0, double fp0) {             \
    ctype val = static_cast<ctype>(pop<op_ctype>(sp, code, wasm_runtime));     \
                                                                               \
    uint64_t offset = Read<MemOffsetT>(code);                                  \
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);                      \
    uint64_t effective_index = offset + index;                                 \
    /* Check alignment. */                                                     \
    if (V8_UNLIKELY(!IsAligned(effective_index, sizeof(ctype)))) {             \
      TRAP(TrapReason::kTrapUnalignedAccess)                                   \
    }                                                                          \
    /* Check bounds. */                                                        \
    if (V8_UNLIKELY(                                                           \
            effective_index < index ||                                         \
            !base::IsInBounds<uint64_t>(effective_index, sizeof(ctype),        \
                                        wasm_runtime->GetMemorySize()))) {     \
      TRAP(TrapReason::kTrapMemOutOfBounds)                                    \
    }                                                                          \
    static_assert(sizeof(std::atomic<ctype>) == sizeof(ctype),                 \
                  "Size mismatch for types std::atomic<" #ctype                \
                  ">, and " #ctype);                                           \
                                                                               \
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();                    \
    uint8_t* address = memory_start + effective_index;                         \
    op_ctype result = static_cast<op_ctype>(                                   \
        operation(reinterpret_cast<std::atomic<ctype>*>(address), val));       \
    push<op_ctype>(sp, code, wasm_runtime, result);                            \
    NextOp();                                                                  \
  }                                                                            \
  static auto constexpr s2s_##name =                                           \
      s2s_##name##I<uint32_t, memory_offset32_t>;                              \
  static auto constexpr s2s_##name##_Idx64 =                                   \
      s2s_##name##I<uint64_t, memory_offset64_t>;
  FOREACH_ATOMIC_BINOP(ATOMIC_BINOP)
#undef ATOMIC_BINOP

#define FOREACH_ATOMIC_COMPARE_EXCHANGE_OP(V)                          \
  V(I32AtomicCompareExchange, Uint32, uint32_t, I32, uint32_t, I32)    \
  V(I32AtomicCompareExchange8U, Uint8, uint8_t, I32, uint32_t, I32)    \
  V(I32AtomicCompareExchange16U, Uint16, uint16_t, I32, uint32_t, I32) \
  V(I64AtomicCompareExchange, Uint64, uint64_t, I64, uint64_t, I64)    \
  V(I64AtomicCompareExchange8U, Uint8, uint8_t, I32, uint64_t, I64)    \
  V(I64AtomicCompareExchange16U, Uint16, uint16_t, I32, uint64_t, I64) \
  V(I64AtomicCompareExchange32U, Uint32, uint32_t, I32, uint64_t, I64)

#define ATOMIC_COMPARE_EXCHANGE_OP(name, Type, ctype, type, op_ctype, op_type) \
  template <typename MemIdx = uint32_t, typename MemOffsetT>                   \
  INSTRUCTION_HANDLER_FUNC s2s_##name##I(const uint8_t* code, uint32_t* sp,    \
                                         WasmInterpreterRuntime* wasm_runtime, \
                                         int64_t r0, double fp0) {             \
    ctype new_val = static_cast<ctype>(pop<op_ctype>(sp, code, wasm_runtime)); \
    ctype old_val = static_cast<ctype>(pop<op_ctype>(sp, code, wasm_runtime)); \
                                                                               \
    uint64_t offset = Read<MemOffsetT>(code);                                  \
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);                      \
    uint64_t effective_index = offset + index;                                 \
    /* Check alignment. */                                                     \
    if (V8_UNLIKELY(!IsAligned(effective_index, sizeof(ctype)))) {             \
      TRAP(TrapReason::kTrapUnalignedAccess)                                   \
    }                                                                          \
    /* Check bounds. */                                                        \
    if (V8_UNLIKELY(                                                           \
            effective_index < index ||                                         \
            !base::IsInBounds<uint64_t>(effective_index, sizeof(ctype),        \
                                        wasm_runtime->GetMemorySize()))) {     \
      TRAP(TrapReason::kTrapMemOutOfBounds)                                    \
    }                                                                          \
    static_assert(sizeof(std::atomic<ctype>) == sizeof(ctype),                 \
                  "Size mismatch for types std::atomic<" #ctype                \
                  ">, and " #ctype);                                           \
                                                                               \
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();                    \
    uint8_t* address = memory_start + effective_index;                         \
                                                                               \
    std::atomic_compare_exchange_strong(                                       \
        reinterpret_cast<std::atomic<ctype>*>(address), &old_val, new_val);    \
    push<op_ctype>(sp, code, wasm_runtime, static_cast<op_ctype>(old_val));    \
    NextOp();                                                                  \
  }                                                                            \
  static auto constexpr s2s_##name =                                           \
      s2s_##name##I<uint32_t, memory_offset32_t>;                              \
  static auto constexpr s2s_##name##_Idx64 =                                   \
      s2s_##name##I<uint64_t, memory_offset64_t>;
  FOREACH_ATOMIC_COMPARE_EXCHANGE_OP(ATOMIC_COMPARE_EXCHANGE_OP)
#undef ATOMIC_COMPARE_EXCHANGE_OP

#define FOREACH_ATOMIC_LOAD_OP(V)                           \
  V(I32AtomicLoad, Uint32, uint32_t, I32, uint32_t, I32)    \
  V(I32AtomicLoad8U, Uint8, uint8_t, I32, uint32_t, I32)    \
  V(I32AtomicLoad16U, Uint16, uint16_t, I32, uint32_t, I32) \
  V(I64AtomicLoad, Uint64, uint64_t, I64, uint64_t, I64)    \
  V(I64AtomicLoad8U, Uint8, uint8_t, I32, uint64_t, I64)    \
  V(I64AtomicLoad16U, Uint16, uint16_t, I32, uint64_t, I64) \
  V(I64AtomicLoad32U, Uint32, uint32_t, I32, uint64_t, I64)

#define ATOMIC_LOAD_OP(name, Type, ctype, type, op_ctype, op_type)             \
  template <typename MemIdx, typename MemOffsetT>                              \
  INSTRUCTION_HANDLER_FUNC s2s_##name##I(const uint8_t* code, uint32_t* sp,    \
                                         WasmInterpreterRuntime* wasm_runtime, \
                                         int64_t r0, double fp0) {             \
    uint64_t offset = Read<MemOffsetT>(code);                                  \
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);                      \
    uint64_t effective_index = offset + index;                                 \
    /* Check alignment. */                                                     \
    if (V8_UNLIKELY(!IsAligned(effective_index, sizeof(ctype)))) {             \
      TRAP(TrapReason::kTrapUnalignedAccess)                                   \
    }                                                                          \
    /* Check bounds. */                                                        \
    if (V8_UNLIKELY(                                                           \
            effective_index < index ||                                         \
            !base::IsInBounds<uint64_t>(effective_index, sizeof(ctype),        \
                                        wasm_runtime->GetMemorySize()))) {     \
      TRAP(TrapReason::kTrapMemOutOfBounds)                                    \
    }                                                                          \
    static_assert(sizeof(std::atomic<ctype>) == sizeof(ctype),                 \
                  "Size mismatch for types std::atomic<" #ctype                \
                  ">, and " #ctype);                                           \
                                                                               \
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();                    \
    uint8_t* address = memory_start + effective_index;                         \
                                                                               \
    ctype val =                                                                \
        std::atomic_load(reinterpret_cast<std::atomic<ctype>*>(address));      \
    push<op_ctype>(sp, code, wasm_runtime, static_cast<op_ctype>(val));        \
    NextOp();                                                                  \
  }                                                                            \
  static auto constexpr s2s_##name =                                           \
      s2s_##name##I<uint32_t, memory_offset32_t>;                              \
  static auto constexpr s2s_##name##_Idx64 =                                   \
      s2s_##name##I<uint64_t, memory_offset64_t>;
  FOREACH_ATOMIC_LOAD_OP(ATOMIC_LOAD_OP)
#undef ATOMIC_LOAD_OP

#define FOREACH_ATOMIC_STORE_OP(V)                           \
  V(I32AtomicStore, Uint32, uint32_t, I32, uint32_t, I32)    \
  V(I32AtomicStore8U, Uint8, uint8_t, I32, uint32_t, I32)    \
  V(I32AtomicStore16U, Uint16, uint16_t, I32, uint32_t, I32) \
  V(I64AtomicStore, Uint64, uint64_t, I64, uint64_t, I64)    \
  V(I64AtomicStore8U, Uint8, uint8_t, I32, uint64_t, I64)    \
  V(I64AtomicStore16U, Uint16, uint16_t, I32, uint64_t, I64) \
  V(I64AtomicStore32U, Uint32, uint32_t, I32, uint64_t, I64)

#define ATOMIC_STORE_OP(name, Type, ctype, type, op_ctype, op_type)            \
  template <typename MemIdx = uint32_t, typename MemOffsetT>                   \
  INSTRUCTION_HANDLER_FUNC s2s_##name##I(const uint8_t* code, uint32_t* sp,    \
                                         WasmInterpreterRuntime* wasm_runtime, \
                                         int64_t r0, double fp0) {             \
    ctype val = static_cast<ctype>(pop<op_ctype>(sp, code, wasm_runtime));     \
                                                                               \
    uint64_t offset = Read<MemOffsetT>(code);                                  \
    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);                      \
    uint64_t effective_index = offset + index;                                 \
    /* Check alignment. */                                                     \
    if (V8_UNLIKELY(!IsAligned(effective_index, sizeof(ctype)))) {             \
      TRAP(TrapReason::kTrapUnalignedAccess)                                   \
    }                                                                          \
    /* Check bounds. */                                                        \
    if (V8_UNLIKELY(                                                           \
            effective_index < index ||                                         \
            !base::IsInBounds<uint64_t>(effective_index, sizeof(ctype),        \
                                        wasm_runtime->GetMemorySize()))) {     \
      TRAP(TrapReason::kTrapMemOutOfBounds)                                    \
    }                                                                          \
    static_assert(sizeof(std::atomic<ctype>) == sizeof(ctype),                 \
                  "Size mismatch for types std::atomic<" #ctype                \
                  ">, and " #ctype);                                           \
                                                                               \
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();                    \
    uint8_t* address = memory_start + effective_index;                         \
                                                                               \
    std::atomic_store(reinterpret_cast<std::atomic<ctype>*>(address), val);    \
    NextOp();                                                                  \
  }                                                                            \
  static auto constexpr s2s_##name =                                           \
      s2s_##name##I<uint32_t, memory_offset32_t>;                              \
  static auto constexpr s2s_##name##_Idx64 =                                   \
      s2s_##name##I<uint64_t, memory_offset64_t>;
  FOREACH_ATOMIC_STORE_OP(ATOMIC_STORE_OP)
#undef ATOMIC_STORE_OP

  //////////////////////////////////////////////////////////////////////////////
  // SIMD instructions.

#if V8_TARGET_BIG_ENDIAN
#define LANE(i, type) ((sizeof(type.val) / sizeof(type.val[0])) - (i)-1)
#else
#define LANE(i, type) (i)
#endif

#define SPLAT_CASE(format, stype, valType, op_type, num)                       \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##format##Splat(                            \
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime, \
      int64_t r0, double fp0) {                                                \
    valType v = pop<valType>(sp, code, wasm_runtime);                          \
    stype s;                                                                   \
    for (int i = 0; i < num; i++) s.val[i] = v;                                \
    push<Simd128>(sp, code, wasm_runtime, Simd128(s));                         \
    NextOp();                                                                  \
  }
  SPLAT_CASE(F64x2, float64x2, double, F64, 2)
  SPLAT_CASE(F32x4, float32x4, float, F32, 4)
  SPLAT_CASE(I64x2, int64x2, int64_t, I64, 2)
  SPLAT_CASE(I32x4, int32x4, int32_t, I32, 4)
  SPLAT_CASE(I16x8, int16x8, int32_t, I32, 8)
  SPLAT_CASE(I8x16, int8x16, int32_t, I32, 16)
#undef SPLAT_CASE

#define EXTRACT_LANE_CASE(format, stype, op_type, name)                        \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##format##ExtractLane(                      \
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime, \
      int64_t r0, double fp0) {                                                \
    uint16_t lane = Read<int16_t>(code);                                       \
    DCHECK_LT(lane, 4);                                                        \
    Simd128 v = pop<Simd128>(sp, code, wasm_runtime);                          \
    stype s = v.to_##name();                                                   \
    push(sp, code, wasm_runtime, s.val[LANE(lane, s)]);                        \
    NextOp();                                                                  \
  }
  EXTRACT_LANE_CASE(F64x2, float64x2, F64, f64x2)
  EXTRACT_LANE_CASE(F32x4, float32x4, F32, f32x4)
  EXTRACT_LANE_CASE(I64x2, int64x2, I64, i64x2)
  EXTRACT_LANE_CASE(I32x4, int32x4, I32, i32x4)
#undef EXTRACT_LANE_CASE

// Unsigned extracts require a bit more care. The underlying array in Simd128 is
// signed (see wasm-value.h), so when casted to uint32_t it will be signed
// extended, e.g. int8_t -> int32_t -> uint32_t. So for unsigned extracts, we
// will cast it int8_t -> uint8_t -> uint32_t. We add the DCHECK to ensure that
// if the array type changes, we know to change this function.
#define EXTRACT_LANE_EXTEND_CASE(format, stype, name, sign, extended_type)     \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##format##ExtractLane##sign(                \
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime, \
      int64_t r0, double fp0) {                                                \
    uint16_t lane = Read<int16_t>(code);                                       \
    DCHECK_LT(lane, 16);                                                       \
    Simd128 s = pop<Simd128>(sp, code, wasm_runtime);                          \
    stype ss = s.to_##name();                                                  \
    auto res = ss.val[LANE(lane, ss)];                                         \
    DCHECK(std::is_signed<decltype(res)>::value);                              \
    if (std::is_unsigned<extended_type>::value) {                              \
      using unsigned_type = std::make_unsigned<decltype(res)>::type;           \
      push(sp, code, wasm_runtime,                                             \
           static_cast<extended_type>(static_cast<unsigned_type>(res)));       \
    } else {                                                                   \
      push(sp, code, wasm_runtime, static_cast<extended_type>(res));           \
    }                                                                          \
    NextOp();                                                                  \
  }
  EXTRACT_LANE_EXTEND_CASE(I16x8, int16x8, i16x8, S, int32_t)
  EXTRACT_LANE_EXTEND_CASE(I16x8, int16x8, i16x8, U, uint32_t)
  EXTRACT_LANE_EXTEND_CASE(I8x16, int8x16, i8x16, S, int32_t)
  EXTRACT_LANE_EXTEND_CASE(I8x16, int8x16, i8x16, U, uint32_t)
#undef EXTRACT_LANE_EXTEND_CASE

#define BINOP_CASE(op, name, stype, count, expr)                              \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype s2 = pop<Simd128>(sp, code, wasm_runtime).to_##name();              \
    stype s1 = pop<Simd128>(sp, code, wasm_runtime).to_##name();              \
    stype res;                                                                \
    for (size_t i = 0; i < count; ++i) {                                      \
      auto a = s1.val[LANE(i, s1)];                                           \
      auto b = s2.val[LANE(i, s2)];                                           \
      res.val[LANE(i, res)] = expr;                                           \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
  }
  BINOP_CASE(F64x2Add, f64x2, float64x2, 2, a + b)
  BINOP_CASE(F64x2Sub, f64x2, float64x2, 2, a - b)
  BINOP_CASE(F64x2Mul, f64x2, float64x2, 2, a* b)
  BINOP_CASE(F64x2Div, f64x2, float64x2, 2, base::Divide(a, b))
  BINOP_CASE(F64x2Min, f64x2, float64x2, 2, JSMin(a, b))
  BINOP_CASE(F64x2Max, f64x2, float64x2, 2, JSMax(a, b))
  BINOP_CASE(F64x2Pmin, f64x2, float64x2, 2, std::min(a, b))
  BINOP_CASE(F64x2Pmax, f64x2, float64x2, 2, std::max(a, b))
  BINOP_CASE(F32x4RelaxedMin, f32x4, float32x4, 4, std::min(a, b))
  BINOP_CASE(F32x4RelaxedMax, f32x4, float32x4, 4, std::max(a, b))
  BINOP_CASE(F64x2RelaxedMin, f64x2, float64x2, 2, std::min(a, b))
  BINOP_CASE(F64x2RelaxedMax, f64x2, float64x2, 2, std::max(a, b))
  BINOP_CASE(F32x4Add, f32x4, float32x4, 4, a + b)
  BINOP_CASE(F32x4Sub, f32x4, float32x4, 4, a - b)
  BINOP_CASE(F32x4Mul, f32x4, float32x4, 4, a* b)
  BINOP_CASE(F32x4Div, f32x4, float32x4, 4, a / b)
  BINOP_CASE(F32x4Min, f32x4, float32x4, 4, JSMin(a, b))
  BINOP_CASE(F32x4Max, f32x4, float32x4, 4, JSMax(a, b))
  BINOP_CASE(F32x4Pmin, f32x4, float32x4, 4, std::min(a, b))
  BINOP_CASE(F32x4Pmax, f32x4, float32x4, 4, std::max(a, b))
  BINOP_CASE(I64x2Add, i64x2, int64x2, 2, base::AddWithWraparound(a, b))
  BINOP_CASE(I64x2Sub, i64x2, int64x2, 2, base::SubWithWraparound(a, b))
  BINOP_CASE(I64x2Mul, i64x2, int64x2, 2, base::MulWithWraparound(a, b))
  BINOP_CASE(I32x4Add, i32x4, int32x4, 4, base::AddWithWraparound(a, b))
  BINOP_CASE(I32x4Sub, i32x4, int32x4, 4, base::SubWithWraparound(a, b))
  BINOP_CASE(I32x4Mul, i32x4, int32x4, 4, base::MulWithWraparound(a, b))
  BINOP_CASE(I32x4MinS, i32x4, int32x4, 4, a < b ? a : b)
  BINOP_CASE(I32x4MinU, i32x4, int32x4, 4,
             static_cast<uint32_t>(a) < static_cast<uint32_t>(b) ? a : b)
  BINOP_CASE(I32x4MaxS, i32x4, int32x4, 4, a > b ? a : b)
  BINOP_CASE(I32x4MaxU, i32x4, int32x4, 4,
             static_cast<uint32_t>(a) > static_cast<uint32_t>(b) ? a : b)
  BINOP_CASE(S128And, i32x4, int32x4, 4, a& b)
  BINOP_CASE(S128Or, i32x4, int32x4, 4, a | b)
  BINOP_CASE(S128Xor, i32x4, int32x4, 4, a ^ b)
  BINOP_CASE(S128AndNot, i32x4, int32x4, 4, a & ~b)
  BINOP_CASE(I16x8Add, i16x8, int16x8, 8, base::AddWithWraparound(a, b))
  BINOP_CASE(I16x8Sub, i16x8, int16x8, 8, base::SubWithWraparound(a, b))
  BINOP_CASE(I16x8Mul, i16x8, int16x8, 8, base::MulWithWraparound(a, b))
  BINOP_CASE(I16x8MinS, i16x8, int16x8, 8, a < b ? a : b)
  BINOP_CASE(I16x8MinU, i16x8, int16x8, 8,
             static_cast<uint16_t>(a) < static_cast<uint16_t>(b) ? a : b)
  BINOP_CASE(I16x8MaxS, i16x8, int16x8, 8, a > b ? a : b)
  BINOP_CASE(I16x8MaxU, i16x8, int16x8, 8,
             static_cast<uint16_t>(a) > static_cast<uint16_t>(b) ? a : b)
  BINOP_CASE(I16x8AddSatS, i16x8, int16x8, 8, SaturateAdd<int16_t>(a, b))
  BINOP_CASE(I16x8AddSatU, i16x8, int16x8, 8, SaturateAdd<uint16_t>(a, b))
  BINOP_CASE(I16x8SubSatS, i16x8, int16x8, 8, SaturateSub<int16_t>(a, b))
  BINOP_CASE(I16x8SubSatU, i16x8, int16x8, 8, SaturateSub<uint16_t>(a, b))
  BINOP_CASE(I16x8RoundingAverageU, i16x8, int16x8, 8,
             RoundingAverageUnsigned<uint16_t>(a, b))
  BINOP_CASE(I16x8Q15MulRSatS, i16x8, int16x8, 8,
             SaturateRoundingQMul<int16_t>(a, b))
  BINOP_CASE(I16x8RelaxedQ15MulRS, i16x8, int16x8, 8,
             SaturateRoundingQMul<int16_t>(a, b))
  BINOP_CASE(I8x16Add, i8x16, int8x16, 16, base::AddWithWraparound(a, b))
  BINOP_CASE(I8x16Sub, i8x16, int8x16, 16, base::SubWithWraparound(a, b))
  BINOP_CASE(I8x16MinS, i8x16, int8x16, 16, a < b ? a : b)
  BINOP_CASE(I8x16MinU, i8x16, int8x16, 16,
             static_cast<uint8_t>(a) < static_cast<uint8_t>(b) ? a : b)
  BINOP_CASE(I8x16MaxS, i8x16, int8x16, 16, a > b ? a : b)
  BINOP_CASE(I8x16MaxU, i8x16, int8x16, 16,
             static_cast<uint8_t>(a) > static_cast<uint8_t>(b) ? a : b)
  BINOP_CASE(I8x16AddSatS, i8x16, int8x16, 16, SaturateAdd<int8_t>(a, b))
  BINOP_CASE(I8x16AddSatU, i8x16, int8x16, 16, SaturateAdd<uint8_t>(a, b))
  BINOP_CASE(I8x16SubSatS, i8x16, int8x16, 16, SaturateSub<int8_t>(a, b))
  BINOP_CASE(I8x16SubSatU, i8x16, int8x16, 16, SaturateSub<uint8_t>(a, b))
  BINOP_CASE(I8x16RoundingAverageU, i8x16, int8x16, 16,
             RoundingAverageUnsigned<uint8_t>(a, b))
#undef BINOP_CASE

#define UNOP_CASE(op, name, stype, count, expr)                               \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype s = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    stype res;                                                                \
    for (size_t i = 0; i < count; ++i) {                                      \
      auto a = s.val[LANE(i, s)];                                             \
      res.val[LANE(i, res)] = expr;                                           \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
  }
  UNOP_CASE(F64x2Abs, f64x2, float64x2, 2, std::abs(a))
  UNOP_CASE(F64x2Neg, f64x2, float64x2, 2, -a)
  UNOP_CASE(F64x2Sqrt, f64x2, float64x2, 2, std::sqrt(a))
  UNOP_CASE(F64x2Ceil, f64x2, float64x2, 2, ceil(a))
  UNOP_CASE(F64x2Floor, f64x2, float64x2, 2, floor(a))
  UNOP_CASE(F64x2Trunc, f64x2, float64x2, 2, trunc(a))
  UNOP_CASE(F64x2NearestInt, f64x2, float64x2, 2, nearbyint(a))
  UNOP_CASE(F32x4Abs, f32x4, float32x4, 4, std::abs(a))
  UNOP_CASE(F32x4Neg, f32x4, float32x4, 4, -a)
  UNOP_CASE(F32x4Sqrt, f32x4, float32x4, 4, std::sqrt(a))
  UNOP_CASE(F32x4Ceil, f32x4, float32x4, 4, ceilf(a))
  UNOP_CASE(F32x4Floor, f32x4, float32x4, 4, floorf(a))
  UNOP_CASE(F32x4Trunc, f32x4, float32x4, 4, truncf(a))
  UNOP_CASE(F32x4NearestInt, f32x4, float32x4, 4, nearbyintf(a))
  UNOP_CASE(I64x2Neg, i64x2, int64x2, 2, base::NegateWithWraparound(a))
  UNOP_CASE(I32x4Neg, i32x4, int32x4, 4, base::NegateWithWraparound(a))
  // Use llabs which will work correctly on both 64-bit and 32-bit.
  UNOP_CASE(I64x2Abs, i64x2, int64x2, 2, std::llabs(a))
  UNOP_CASE(I32x4Abs, i32x4, int32x4, 4, std::abs(a))
  UNOP_CASE(S128Not, i32x4, int32x4, 4, ~a)
  UNOP_CASE(I16x8Neg, i16x8, int16x8, 8, base::NegateWithWraparound(a))
  UNOP_CASE(I16x8Abs, i16x8, int16x8, 8, std::abs(a))
  UNOP_CASE(I8x16Neg, i8x16, int8x16, 16, base::NegateWithWraparound(a))
  UNOP_CASE(I8x16Abs, i8x16, int8x16, 16, std::abs(a))
  UNOP_CASE(I8x16Popcnt, i8x16, int8x16, 16,
            base::bits::CountPopulation<uint8_t>(a))
#undef UNOP_CASE

#define BITMASK_CASE(op, name, stype, count)                                  \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype s = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    int32_t res = 0;                                                          \
    for (size_t i = 0; i < count; ++i) {                                      \
      bool sign = std::signbit(static_cast<double>(s.val[LANE(i, s)]));       \
      res |= (sign << i);                                                     \
    }                                                                         \
    push<int32_t>(sp, code, wasm_runtime, res);                               \
    NextOp();                                                                 \
  }
  BITMASK_CASE(I8x16BitMask, i8x16, int8x16, 16)
  BITMASK_CASE(I16x8BitMask, i16x8, int16x8, 8)
  BITMASK_CASE(I32x4BitMask, i32x4, int32x4, 4)
  BITMASK_CASE(I64x2BitMask, i64x2, int64x2, 2)
#undef BITMASK_CASE

#define CMPOP_CASE(op, name, stype, out_stype, count, expr)                   \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype s2 = pop<Simd128>(sp, code, wasm_runtime).to_##name();              \
    stype s1 = pop<Simd128>(sp, code, wasm_runtime).to_##name();              \
    out_stype res;                                                            \
    for (size_t i = 0; i < count; ++i) {                                      \
      auto a = s1.val[LANE(i, s1)];                                           \
      auto b = s2.val[LANE(i, s2)];                                           \
      auto result = expr;                                                     \
      res.val[LANE(i, res)] = result ? -1 : 0;                                \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
  }

  CMPOP_CASE(F64x2Eq, f64x2, float64x2, int64x2, 2, a == b)
  CMPOP_CASE(F64x2Ne, f64x2, float64x2, int64x2, 2, a != b)
  CMPOP_CASE(F64x2Gt, f64x2, float64x2, int64x2, 2, a > b)
  CMPOP_CASE(F64x2Ge, f64x2, float64x2, int64x2, 2, a >= b)
  CMPOP_CASE(F64x2Lt, f64x2, float64x2, int64x2, 2, a < b)
  CMPOP_CASE(F64x2Le, f64x2, float64x2, int64x2, 2, a <= b)
  CMPOP_CASE(F32x4Eq, f32x4, float32x4, int32x4, 4, a == b)
  CMPOP_CASE(F32x4Ne, f32x4, float32x4, int32x4, 4, a != b)
  CMPOP_CASE(F32x4Gt, f32x4, float32x4, int32x4, 4, a > b)
  CMPOP_CASE(F32x4Ge, f32x4, float32x4, int32x4, 4, a >= b)
  CMPOP_CASE(F32x4Lt, f32x4, float32x4, int32x4, 4, a < b)
  CMPOP_CASE(F32x4Le, f32x4, float32x4, int32x4, 4, a <= b)
  CMPOP_CASE(I64x2Eq, i64x2, int64x2, int64x2, 2, a == b)
  CMPOP_CASE(I64x2Ne, i64x2, int64x2, int64x2, 2, a != b)
  CMPOP_CASE(I64x2LtS, i64x2, int64x2, int64x2, 2, a < b)
  CMPOP_CASE(I64x2GtS, i64x2, int64x2, int64x2, 2, a > b)
  CMPOP_CASE(I64x2LeS, i64x2, int64x2, int64x2, 2, a <= b)
  CMPOP_CASE(I64x2GeS, i64x2, int64x2, int64x2, 2, a >= b)
  CMPOP_CASE(I32x4Eq, i32x4, int32x4, int32x4, 4, a == b)
  CMPOP_CASE(I32x4Ne, i32x4, int32x4, int32x4, 4, a != b)
  CMPOP_CASE(I32x4GtS, i32x4, int32x4, int32x4, 4, a > b)
  CMPOP_CASE(I32x4GeS, i32x4, int32x4, int32x4, 4, a >= b)
  CMPOP_CASE(I32x4LtS, i32x4, int32x4, int32x4, 4, a < b)
  CMPOP_CASE(I32x4LeS, i32x4, int32x4, int32x4, 4, a <= b)
  CMPOP_CASE(I32x4GtU, i32x4, int32x4, int32x4, 4,
             static_cast<uint32_t>(a) > static_cast<uint32_t>(b))
  CMPOP_CASE(I32x4GeU, i32x4, int32x4, int32x4, 4,
             static_cast<uint32_t>(a) >= static_cast<uint32_t>(b))
  CMPOP_CASE(I32x4LtU, i32x4, int32x4, int32x4, 4,
             static_cast<uint32_t>(a) < static_cast<uint32_t>(b))
  CMPOP_CASE(I32x4LeU, i32x4, int32x4, int32x4, 4,
             static_cast<uint32_t>(a) <= static_cast<uint32_t>(b))
  CMPOP_CASE(I16x8Eq, i16x8, int16x8, int16x8, 8, a == b)
  CMPOP_CASE(I16x8Ne, i16x8, int16x8, int16x8, 8, a != b)
  CMPOP_CASE(I16x8GtS, i16x8, int16x8, int16x8, 8, a > b)
  CMPOP_CASE(I16x8GeS, i16x8, int16x8, int16x8, 8, a >= b)
  CMPOP_CASE(I16x8LtS, i16x8, int16x8, int16x8, 8, a < b)
  CMPOP_CASE(I16x8LeS, i16x8, int16x8, int16x8, 8, a <= b)
  CMPOP_CASE(I16x8GtU, i16x8, int16x8, int16x8, 8,
             static_cast<uint16_t>(a) > static_cast<uint16_t>(b))
  CMPOP_CASE(I16x8GeU, i16x8, int16x8, int16x8, 8,
             static_cast<uint16_t>(a) >= static_cast<uint16_t>(b))
  CMPOP_CASE(I16x8LtU, i16x8, int16x8, int16x8, 8,
             static_cast<uint16_t>(a) < static_cast<uint16_t>(b))
  CMPOP_CASE(I16x8LeU, i16x8, int16x8, int16x8, 8,
             static_cast<uint16_t>(a) <= static_cast<uint16_t>(b))
  CMPOP_CASE(I8x16Eq, i8x16, int8x16, int8x16, 16, a == b)
  CMPOP_CASE(I8x16Ne, i8x16, int8x16, int8x16, 16, a != b)
  CMPOP_CASE(I8x16GtS, i8x16, int8x16, int8x16, 16, a > b)
  CMPOP_CASE(I8x16GeS, i8x16, int8x16, int8x16, 16, a >= b)
  CMPOP_CASE(I8x16LtS, i8x16, int8x16, int8x16, 16, a < b)
  CMPOP_CASE(I8x16LeS, i8x16, int8x16, int8x16, 16, a <= b)
  CMPOP_CASE(I8x16GtU, i8x16, int8x16, int8x16, 16,
             static_cast<uint8_t>(a) > static_cast<uint8_t>(b))
  CMPOP_CASE(I8x16GeU, i8x16, int8x16, int8x16, 16,
             static_cast<uint8_t>(a) >= static_cast<uint8_t>(b))
  CMPOP_CASE(I8x16LtU, i8x16, int8x16, int8x16, 16,
             static_cast<uint8_t>(a) < static_cast<uint8_t>(b))
  CMPOP_CASE(I8x16LeU, i8x16, int8x16, int8x16, 16,
             static_cast<uint8_t>(a) <= static_cast<uint8_t>(b))
#undef CMPOP_CASE

#define REPLACE_LANE_CASE(format, name, stype, ctype, op_type)                 \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##format##ReplaceLane(                      \
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime, \
      int64_t r0, double fp0) {                                                \
    uint16_t lane = Read<int16_t>(code);                                       \
    DCHECK_LT(lane, 16);                                                       \
    ctype new_val = pop<ctype>(sp, code, wasm_runtime);                        \
    Simd128 simd_val = pop<Simd128>(sp, code, wasm_runtime);                   \
    stype s = simd_val.to_##name();                                            \
    s.val[LANE(lane, s)] = new_val;                                            \
    push<Simd128>(sp, code, wasm_runtime, Simd128(s));                         \
    NextOp();                                                                  \
  }
  REPLACE_LANE_CASE(F64x2, f64x2, float64x2, double, F64)
  REPLACE_LANE_CASE(F32x4, f32x4, float32x4, float, F32)
  REPLACE_LANE_CASE(I64x2, i64x2, int64x2, int64_t, I64)
  REPLACE_LANE_CASE(I32x4, i32x4, int32x4, int32_t, I32)
  REPLACE_LANE_CASE(I16x8, i16x8, int16x8, int32_t, I32)
  REPLACE_LANE_CASE(I8x16, i8x16, int8x16, int32_t, I32)
#undef REPLACE_LANE_CASE

  template <typename MemIdx, typename MemOffsetT>
  INSTRUCTION_HANDLER_FUNC s2s_SimdS128LoadMemI(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    uint64_t offset = Read<MemOffsetT>(code);

    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(Simd128),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;
    Simd128 s =
        base::ReadUnalignedValue<Simd128>(reinterpret_cast<Address>(address));
    push<Simd128>(sp, code, wasm_runtime, Simd128(s));

    NextOp();
  }
  static auto constexpr s2s_SimdS128LoadMem =
      s2s_SimdS128LoadMemI<uint32_t, memory_offset32_t>;
  static auto constexpr s2s_SimdS128LoadMem_Idx64 =
      s2s_SimdS128LoadMemI<uint64_t, memory_offset64_t>;

  template <typename MemIdx, typename MemOffsetT>
  INSTRUCTION_HANDLER_FUNC s2s_SimdS128StoreMemI(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    Simd128 val = pop<Simd128>(sp, code, wasm_runtime);

    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    uint64_t offset = Read<MemOffsetT>(code);

    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(Simd128),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;
    base::WriteUnalignedValue<Simd128>(reinterpret_cast<Address>(address), val);

    NextOp();
  }
  static auto constexpr s2s_SimdS128StoreMem =
      s2s_SimdS128StoreMemI<uint32_t, memory_offset32_t>;
  static auto constexpr s2s_SimdS128StoreMem_Idx64 =
      s2s_SimdS128StoreMemI<uint64_t, memory_offset64_t>;

#define SHIFT_CASE(op, name, stype, count, expr)                              \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    uint32_t shift = pop<uint32_t>(sp, code, wasm_runtime);                   \
    stype s = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    stype res;                                                                \
    for (size_t i = 0; i < count; ++i) {                                      \
      auto a = s.val[LANE(i, s)];                                             \
      res.val[LANE(i, res)] = expr;                                           \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
  }
  SHIFT_CASE(I64x2Shl, i64x2, int64x2, 2,
             static_cast<uint64_t>(a) << (shift % 64))
  SHIFT_CASE(I64x2ShrS, i64x2, int64x2, 2, a >> (shift % 64))
  SHIFT_CASE(I64x2ShrU, i64x2, int64x2, 2,
             static_cast<uint64_t>(a) >> (shift % 64))
  SHIFT_CASE(I32x4Shl, i32x4, int32x4, 4,
             static_cast<uint32_t>(a) << (shift % 32))
  SHIFT_CASE(I32x4ShrS, i32x4, int32x4, 4, a >> (shift % 32))
  SHIFT_CASE(I32x4ShrU, i32x4, int32x4, 4,
             static_cast<uint32_t>(a) >> (shift % 32))
  SHIFT_CASE(I16x8Shl, i16x8, int16x8, 8,
             static_cast<uint16_t>(a) << (shift % 16))
  SHIFT_CASE(I16x8ShrS, i16x8, int16x8, 8, a >> (shift % 16))
  SHIFT_CASE(I16x8ShrU, i16x8, int16x8, 8,
             static_cast<uint16_t>(a) >> (shift % 16))
  SHIFT_CASE(I8x16Shl, i8x16, int8x16, 16,
             static_cast<uint8_t>(a) << (shift % 8))
  SHIFT_CASE(I8x16ShrS, i8x16, int8x16, 16, a >> (shift % 8))
  SHIFT_CASE(I8x16ShrU, i8x16, int8x16, 16,
             static_cast<uint8_t>(a) >> (shift % 8))
#undef SHIFT_CASE

  template <typename s_type, typename d_type, typename narrow, typename wide,
            uint32_t start>
  INSTRUCTION_HANDLER_FUNC s2s_DoSimdExtMul(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    s_type s2 = pop<Simd128>(sp, code, wasm_runtime).template to<s_type>();
    s_type s1 = pop<Simd128>(sp, code, wasm_runtime).template to<s_type>();
    auto end = start + (kSimd128Size / sizeof(wide));
    d_type res;
    uint32_t i = start;
    for (size_t dst = 0; i < end; ++i, ++dst) {
      // Need static_cast for unsigned narrow types.
      res.val[LANE(dst, res)] =
          MultiplyLong<wide>(static_cast<narrow>(s1.val[LANE(start, s1)]),
                             static_cast<narrow>(s2.val[LANE(start, s2)]));
    }
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));
    NextOp();
  }
  static auto constexpr s2s_SimdI16x8ExtMulLowI8x16S =
      s2s_DoSimdExtMul<int8x16, int16x8, int8_t, int16_t, 0>;
  static auto constexpr s2s_SimdI16x8ExtMulHighI8x16S =
      s2s_DoSimdExtMul<int8x16, int16x8, int8_t, int16_t, 8>;
  static auto constexpr s2s_SimdI16x8ExtMulLowI8x16U =
      s2s_DoSimdExtMul<int8x16, int16x8, uint8_t, uint16_t, 0>;
  static auto constexpr s2s_SimdI16x8ExtMulHighI8x16U =
      s2s_DoSimdExtMul<int8x16, int16x8, uint8_t, uint16_t, 8>;
  static auto constexpr s2s_SimdI32x4ExtMulLowI16x8S =
      s2s_DoSimdExtMul<int16x8, int32x4, int16_t, int32_t, 0>;
  static auto constexpr s2s_SimdI32x4ExtMulHighI16x8S =
      s2s_DoSimdExtMul<int16x8, int32x4, int16_t, int32_t, 4>;
  static auto constexpr s2s_SimdI32x4ExtMulLowI16x8U =
      s2s_DoSimdExtMul<int16x8, int32x4, uint16_t, uint32_t, 0>;
  static auto constexpr s2s_SimdI32x4ExtMulHighI16x8U =
      s2s_DoSimdExtMul<int16x8, int32x4, uint16_t, uint32_t, 4>;
  static auto constexpr s2s_SimdI64x2ExtMulLowI32x4S =
      s2s_DoSimdExtMul<int32x4, int64x2, int32_t, int64_t, 0>;
  static auto constexpr s2s_SimdI64x2ExtMulHighI32x4S =
      s2s_DoSimdExtMul<int32x4, int64x2, int32_t, int64_t, 2>;
  static auto constexpr s2s_SimdI64x2ExtMulLowI32x4U =
      s2s_DoSimdExtMul<int32x4, int64x2, uint32_t, uint64_t, 0>;
  static auto constexpr s2s_SimdI64x2ExtMulHighI32x4U =
      s2s_DoSimdExtMul<int32x4, int64x2, uint32_t, uint64_t, 2>;
#undef EXT_MUL_CASE

#define CONVERT_CASE(op, src_type, name, dst_type, count, start_index, ctype, \
                     expr)                                                    \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    src_type s = pop<Simd128>(sp, code, wasm_runtime).to_##name();            \
    dst_type res = {0};                                                       \
    for (size_t i = 0; i < count; ++i) {                                      \
      ctype a = s.val[LANE(start_index + i, s)];                              \
      res.val[LANE(i, res)] = expr;                                           \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
  }
  CONVERT_CASE(F32x4SConvertI32x4, int32x4, i32x4, float32x4, 4, 0, int32_t,
               static_cast<float>(a))
  CONVERT_CASE(F32x4UConvertI32x4, int32x4, i32x4, float32x4, 4, 0, uint32_t,
               static_cast<float>(a))
  CONVERT_CASE(I32x4SConvertF32x4, float32x4, f32x4, int32x4, 4, 0, float,
               base::saturated_cast<int32_t>(a))
  CONVERT_CASE(I32x4UConvertF32x4, float32x4, f32x4, int32x4, 4, 0, float,
               base::saturated_cast<uint32_t>(a))
  CONVERT_CASE(I32x4RelaxedTruncF32x4S, float32x4, f32x4, int32x4, 4, 0, float,
               base::saturated_cast<int32_t>(a))
  CONVERT_CASE(I32x4RelaxedTruncF32x4U, float32x4, f32x4, int32x4, 4, 0, float,
               base::saturated_cast<uint32_t>(a))
  CONVERT_CASE(I64x2SConvertI32x4Low, int32x4, i32x4, int64x2, 2, 0, int32_t, a)
  CONVERT_CASE(I64x2SConvertI32x4High, int32x4, i32x4, int64x2, 2, 2, int32_t,
               a)
  CONVERT_CASE(I64x2UConvertI32x4Low, int32x4, i32x4, int64x2, 2, 0, uint32_t,
               a)
  CONVERT_CASE(I64x2UConvertI32x4High, int32x4, i32x4, int64x2, 2, 2, uint32_t,
               a)
  CONVERT_CASE(I32x4SConvertI16x8High, int16x8, i16x8, int32x4, 4, 4, int16_t,
               a)
  CONVERT_CASE(I32x4UConvertI16x8High, int16x8, i16x8, int32x4, 4, 4, uint16_t,
               a)
  CONVERT_CASE(I32x4SConvertI16x8Low, int16x8, i16x8, int32x4, 4, 0, int16_t, a)
  CONVERT_CASE(I32x4UConvertI16x8Low, int16x8, i16x8, int32x4, 4, 0, uint16_t,
               a)
  CONVERT_CASE(I16x8SConvertI8x16High, int8x16, i8x16, int16x8, 8, 8, int8_t, a)
  CONVERT_CASE(I16x8UConvertI8x16High, int8x16, i8x16, int16x8, 8, 8, uint8_t,
               a)
  CONVERT_CASE(I16x8SConvertI8x16Low, int8x16, i8x16, int16x8, 8, 0, int8_t, a)
  CONVERT_CASE(I16x8UConvertI8x16Low, int8x16, i8x16, int16x8, 8, 0, uint8_t, a)
  CONVERT_CASE(F64x2ConvertLowI32x4S, int32x4, i32x4, float64x2, 2, 0, int32_t,
               static_cast<double>(a))
  CONVERT_CASE(F64x2ConvertLowI32x4U, int32x4, i32x4, float64x2, 2, 0, uint32_t,
               static_cast<double>(a))
  CONVERT_CASE(I32x4TruncSatF64x2SZero, float64x2, f64x2, int32x4, 2, 0, double,
               base::saturated_cast<int32_t>(a))
  CONVERT_CASE(I32x4TruncSatF64x2UZero, float64x2, f64x2, int32x4, 2, 0, double,
               base::saturated_cast<uint32_t>(a))
  CONVERT_CASE(I32x4RelaxedTruncF64x2SZero, float64x2, f64x2, int32x4, 2, 0,
               double, base::saturated_cast<int32_t>(a))
  CONVERT_CASE(I32x4RelaxedTruncF64x2UZero, float64x2, f64x2, int32x4, 2, 0,
               double, base::saturated_cast<uint32_t>(a))
  CONVERT_CASE(F32x4DemoteF64x2Zero, float64x2, f64x2, float32x4, 2, 0, float,
               DoubleToFloat32(a))
  CONVERT_CASE(F64x2PromoteLowF32x4, float32x4, f32x4, float64x2, 2, 0, float,
               static_cast<double>(a))
#undef CONVERT_CASE

#define PACK_CASE(op, src_type, name, dst_type, count, dst_ctype)             \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    src_type s2 = pop<Simd128>(sp, code, wasm_runtime).to_##name();           \
    src_type s1 = pop<Simd128>(sp, code, wasm_runtime).to_##name();           \
    dst_type res;                                                             \
    for (size_t i = 0; i < count; ++i) {                                      \
      int64_t v = i < count / 2 ? s1.val[LANE(i, s1)]                         \
                                : s2.val[LANE(i - count / 2, s2)];            \
      res.val[LANE(i, res)] = base::saturated_cast<dst_ctype>(v);             \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
  }
  PACK_CASE(I16x8SConvertI32x4, int32x4, i32x4, int16x8, 8, int16_t)
  PACK_CASE(I16x8UConvertI32x4, int32x4, i32x4, int16x8, 8, uint16_t)
  PACK_CASE(I8x16SConvertI16x8, int16x8, i16x8, int8x16, 16, int8_t)
  PACK_CASE(I8x16UConvertI16x8, int16x8, i16x8, int8x16, 16, uint8_t)
#undef PACK_CASE

  INSTRUCTION_HANDLER_FUNC s2s_DoSimdSelect(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int32x4 bool_val = pop<Simd128>(sp, code, wasm_runtime).to_i32x4();
    int32x4 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i32x4();
    int32x4 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i32x4();
    int32x4 res;
    for (size_t i = 0; i < 4; ++i) {
      res.val[LANE(i, res)] =
          v2.val[LANE(i, v2)] ^ ((v1.val[LANE(i, v1)] ^ v2.val[LANE(i, v2)]) &
                                 bool_val.val[LANE(i, bool_val)]);
    }
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));
    NextOp();
  }
  // Do these 5 instructions really have the same implementation?
  static constexpr auto s2s_SimdI8x16RelaxedLaneSelect = s2s_DoSimdSelect;
  static constexpr auto s2s_SimdI16x8RelaxedLaneSelect = s2s_DoSimdSelect;
  static constexpr auto s2s_SimdI32x4RelaxedLaneSelect = s2s_DoSimdSelect;
  static constexpr auto s2s_SimdI64x2RelaxedLaneSelect = s2s_DoSimdSelect;
  static constexpr auto s2s_SimdS128Select = s2s_DoSimdSelect;

  INSTRUCTION_HANDLER_FUNC s2s_SimdI32x4DotI16x8S(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int16x8 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i16x8();
    int16x8 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i16x8();
    int32x4 res;
    for (size_t i = 0; i < 4; i++) {
      int32_t lo = (v1.val[LANE(i * 2, v1)] * v2.val[LANE(i * 2, v2)]);
      int32_t hi = (v1.val[LANE(i * 2 + 1, v1)] * v2.val[LANE(i * 2 + 1, v2)]);
      res.val[LANE(i, res)] = base::AddWithWraparound(lo, hi);
    }
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_SimdI16x8DotI8x16I7x16S(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int8x16 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
    int8x16 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
    int16x8 res;
    for (size_t i = 0; i < 8; i++) {
      int16_t lo = (v1.val[LANE(i * 2, v1)] * v2.val[LANE(i * 2, v2)]);
      int16_t hi = (v1.val[LANE(i * 2 + 1, v1)] * v2.val[LANE(i * 2 + 1, v2)]);
      res.val[LANE(i, res)] = base::AddWithWraparound(lo, hi);
    }
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_SimdI32x4DotI8x16I7x16AddS(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int32x4 v3 = pop<Simd128>(sp, code, wasm_runtime).to_i32x4();
    int8x16 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
    int8x16 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
    int32x4 res;
    for (size_t i = 0; i < 4; i++) {
      int32_t a = (v1.val[LANE(i * 4, v1)] * v2.val[LANE(i * 4, v2)]);
      int32_t b = (v1.val[LANE(i * 4 + 1, v1)] * v2.val[LANE(i * 4 + 1, v2)]);
      int32_t c = (v1.val[LANE(i * 4 + 2, v1)] * v2.val[LANE(i * 4 + 2, v2)]);
      int32_t d = (v1.val[LANE(i * 4 + 3, v1)] * v2.val[LANE(i * 4 + 3, v2)]);
      int32_t acc = v3.val[LANE(i, v3)];
      // a + b + c + d should not wrap
      res.val[LANE(i, res)] = base::AddWithWraparound(a + b + c + d, acc);
    }
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_SimdI8x16Swizzle(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int8x16 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
    int8x16 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
    int8x16 res;
    for (size_t i = 0; i < kSimd128Size; ++i) {
      int lane = v2.val[LANE(i, v2)];
      res.val[LANE(i, res)] =
          lane < kSimd128Size && lane >= 0 ? v1.val[LANE(lane, v1)] : 0;
    }
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));
    NextOp();
  }
  static constexpr auto s2s_SimdI8x16RelaxedSwizzle = s2s_SimdI8x16Swizzle;

  INSTRUCTION_HANDLER_FUNC s2s_SimdI8x16Shuffle(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int8x16 value = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
    int8x16 v2 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
    int8x16 v1 = pop<Simd128>(sp, code, wasm_runtime).to_i8x16();
    int8x16 res;
    for (size_t i = 0; i < kSimd128Size; ++i) {
      int lane = value.val[i];
      res.val[LANE(i, res)] = lane < kSimd128Size
                                  ? v1.val[LANE(lane, v1)]
                                  : v2.val[LANE(lane - kSimd128Size, v2)];
    }
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_SimdV128AnyTrue(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int32x4 s = pop<Simd128>(sp, code, wasm_runtime).to_i32x4();
    bool res = s.val[LANE(0, s)] | s.val[LANE(1, s)] | s.val[LANE(2, s)] |
               s.val[LANE(3, s)];
    push<int32_t>(sp, code, wasm_runtime, res);
    NextOp();
  }

#define REDUCTION_CASE(op, name, stype, count)                                \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype s = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    bool res = true;                                                          \
    for (size_t i = 0; i < count; ++i) {                                      \
      res = res & static_cast<bool>(s.val[LANE(i, s)]);                       \
    }                                                                         \
    push<int32_t>(sp, code, wasm_runtime, res);                               \
    NextOp();                                                                 \
  }
  REDUCTION_CASE(I64x2AllTrue, i64x2, int64x2, 2)
  REDUCTION_CASE(I32x4AllTrue, i32x4, int32x4, 4)
  REDUCTION_CASE(I16x8AllTrue, i16x8, int16x8, 8)
  REDUCTION_CASE(I8x16AllTrue, i8x16, int8x16, 16)
#undef REDUCTION_CASE

#define QFM_CASE(op, name, stype, count, operation)                           \
  INSTRUCTION_HANDLER_FUNC s2s_Simd##op(const uint8_t* code, uint32_t* sp,    \
                                        WasmInterpreterRuntime* wasm_runtime, \
                                        int64_t r0, double fp0) {             \
    stype c = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    stype b = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    stype a = pop<Simd128>(sp, code, wasm_runtime).to_##name();               \
    stype res;                                                                \
    for (size_t i = 0; i < count; i++) {                                      \
      res.val[LANE(i, res)] =                                                 \
          operation(a.val[LANE(i, a)] * b.val[LANE(i, b)]) +                  \
          c.val[LANE(i, c)];                                                  \
    }                                                                         \
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));                      \
    NextOp();                                                                 \
  }
  QFM_CASE(F32x4Qfma, f32x4, float32x4, 4, +)
  QFM_CASE(F32x4Qfms, f32x4, float32x4, 4, -)
  QFM_CASE(F64x2Qfma, f64x2, float64x2, 2, +)
  QFM_CASE(F64x2Qfms, f64x2, float64x2, 2, -)
#undef QFM_CASE

  template <typename s_type, typename load_type, typename MemIdx,
            typename MemOffsetT>
  INSTRUCTION_HANDLER_FUNC s2s_DoSimdLoadSplat(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    uint64_t offset = Read<MemOffsetT>(code);

    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(load_type),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;
    load_type v =
        base::ReadUnalignedValue<load_type>(reinterpret_cast<Address>(address));
    s_type s;
    for (size_t i = 0; i < arraysize(s.val); i++) {
      s.val[LANE(i, s)] = v;
    }
    push<Simd128>(sp, code, wasm_runtime, Simd128(s));

    NextOp();
  }
  static auto constexpr s2s_SimdS128Load8Splat =
      s2s_DoSimdLoadSplat<int8x16, int8_t, uint32_t, memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load8Splat_Idx64 =
      s2s_DoSimdLoadSplat<int8x16, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_SimdS128Load16Splat =
      s2s_DoSimdLoadSplat<int16x8, int16_t, uint32_t, memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load16Splat_Idx64 =
      s2s_DoSimdLoadSplat<int16x8, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_SimdS128Load32Splat =
      s2s_DoSimdLoadSplat<int32x4, int32_t, uint32_t, memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load32Splat_Idx64 =
      s2s_DoSimdLoadSplat<int32x4, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_SimdS128Load64Splat =
      s2s_DoSimdLoadSplat<int64x2, int64_t, uint32_t, memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load64Splat_Idx64 =
      s2s_DoSimdLoadSplat<int64x2, int64_t, uint64_t, memory_offset64_t>;

  template <typename s_type, typename wide_type, typename narrow_type,
            typename MemIdx, typename MemOffsetT>
  INSTRUCTION_HANDLER_FUNC s2s_DoSimdLoadExtend(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    static_assert(sizeof(wide_type) == sizeof(narrow_type) * 2,
                  "size mismatch for wide and narrow types");
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    uint64_t offset = Read<MemOffsetT>(code);

    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    // Load 8 bytes and sign/zero extend to 16 bytes.
    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(uint64_t),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;
    uint64_t v =
        base::ReadUnalignedValue<uint64_t>(reinterpret_cast<Address>(address));
    constexpr int lanes = kSimd128Size / sizeof(wide_type);
    s_type s;
    for (int i = 0; i < lanes; i++) {
      uint8_t shift = i * (sizeof(narrow_type) * 8);
      narrow_type el = static_cast<narrow_type>(v >> shift);
      s.val[LANE(i, s)] = static_cast<wide_type>(el);
    }
    push<Simd128>(sp, code, wasm_runtime, Simd128(s));

    NextOp();
  }
  static auto constexpr s2s_SimdS128Load8x8S =
      s2s_DoSimdLoadExtend<int16x8, int16_t, int8_t, uint32_t,
                           memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load8x8S_Idx64 =
      s2s_DoSimdLoadExtend<int16x8, int16_t, int8_t, uint32_t,
                           memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load8x8U =
      s2s_DoSimdLoadExtend<int16x8, uint16_t, uint8_t, uint32_t,
                           memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load8x8U_Idx64 =
      s2s_DoSimdLoadExtend<int16x8, uint16_t, uint8_t, uint32_t,
                           memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load16x4S =
      s2s_DoSimdLoadExtend<int32x4, int32_t, int16_t, uint32_t,
                           memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load16x4S_Idx64 =
      s2s_DoSimdLoadExtend<int32x4, int32_t, int16_t, uint32_t,
                           memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load16x4U =
      s2s_DoSimdLoadExtend<int32x4, uint32_t, uint16_t, uint32_t,
                           memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load16x4U_Idx64 =
      s2s_DoSimdLoadExtend<int32x4, uint32_t, uint16_t, uint32_t,
                           memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load32x2S =
      s2s_DoSimdLoadExtend<int64x2, int64_t, int32_t, uint32_t,
                           memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load32x2S_Idx64 =
      s2s_DoSimdLoadExtend<int64x2, int64_t, int32_t, uint32_t,
                           memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load32x2U =
      s2s_DoSimdLoadExtend<int64x2, uint64_t, uint32_t, uint32_t,
                           memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load32x2U_Idx64 =
      s2s_DoSimdLoadExtend<int64x2, uint64_t, uint32_t, uint32_t,
                           memory_offset32_t>;

  template <typename s_type, typename load_type, typename MemIdx,
            typename MemOffsetT>
  INSTRUCTION_HANDLER_FUNC s2s_DoSimdLoadZeroExtend(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    uint64_t offset = Read<MemOffsetT>(code);

    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    // Load a single 32-bit or 64-bit element into the lowest bits of a v128
    // vector, and initialize all other bits of the v128 vector to zero.
    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(load_type),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;
    load_type v =
        base::ReadUnalignedValue<load_type>(reinterpret_cast<Address>(address));
    s_type s;
    // All lanes are 0.
    for (size_t i = 0; i < arraysize(s.val); i++) {
      s.val[LANE(i, s)] = 0;
    }
    // Lane 0 is set to the loaded value.
    s.val[LANE(0, s)] = v;
    push<Simd128>(sp, code, wasm_runtime, Simd128(s));

    NextOp();
  }
  static auto constexpr s2s_SimdS128Load32Zero =
      s2s_DoSimdLoadZeroExtend<int32x4, uint32_t, uint32_t, memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load32Zero_Idx64 =
      s2s_DoSimdLoadZeroExtend<int32x4, uint32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_SimdS128Load64Zero =
      s2s_DoSimdLoadZeroExtend<int64x2, uint64_t, uint32_t, memory_offset32_t>;
  static auto constexpr s2s_SimdS128Load64Zero_Idx64 =
      s2s_DoSimdLoadZeroExtend<int64x2, uint64_t, uint64_t, memory_offset64_t>;

  template <typename s_type, typename memory_type, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC s2s_DoSimdLoadLane(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    s_type value = pop<Simd128>(sp, code, wasm_runtime).template to<s_type>();

    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    uint64_t offset = Read<MemOffsetT>(code);

    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(memory_type),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }

    uint8_t* address = memory_start + effective_index;
    memory_type loaded = base::ReadUnalignedValue<memory_type>(
        reinterpret_cast<Address>(address));
    uint16_t lane = Read<uint16_t>(code);
    value.val[LANE(lane, value)] = loaded;
    push<Simd128>(sp, code, wasm_runtime, Simd128(value));

    NextOp();
  }
  static auto constexpr s2s_SimdS128Load8Lane =
      s2s_DoSimdLoadLane<int8x16, int8_t>;
  static auto constexpr s2s_SimdS128Load16Lane =
      s2s_DoSimdLoadLane<int16x8, int16_t>;
  static auto constexpr s2s_SimdS128Load32Lane =
      s2s_DoSimdLoadLane<int32x4, int32_t>;
  static auto constexpr s2s_SimdS128Load64Lane =
      s2s_DoSimdLoadLane<int64x2, uint64_t>;
  static auto constexpr s2s_SimdS128Load8Lane_Idx64 =
      s2s_DoSimdLoadLane<int8x16, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_SimdS128Load16Lane_Idx64 =
      s2s_DoSimdLoadLane<int16x8, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_SimdS128Load32Lane_Idx64 =
      s2s_DoSimdLoadLane<int32x4, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_SimdS128Load64Lane_Idx64 =
      s2s_DoSimdLoadLane<int64x2, int64_t, uint64_t, memory_offset64_t>;

  template <typename s_type, typename memory_type, typename MemIdx = uint32_t,
            typename MemOffsetT = memory_offset32_t>
  INSTRUCTION_HANDLER_FUNC s2s_DoSimdStoreLane(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    // Extract a single lane, push it onto the stack, then store the lane.
    s_type value = pop<Simd128>(sp, code, wasm_runtime).template to<s_type>();

    uint8_t* memory_start = wasm_runtime->GetMemoryStart();
    uint64_t offset = Read<MemOffsetT>(code);

    uint64_t index = pop<MemIdx>(sp, code, wasm_runtime);
    uint64_t effective_index = offset + index;

    if (V8_UNLIKELY(
            effective_index < index ||
            !base::IsInBounds<uint64_t>(effective_index, sizeof(memory_type),
                                        wasm_runtime->GetMemorySize()))) {
      TRAP(TrapReason::kTrapMemOutOfBounds)
    }
    uint8_t* address = memory_start + effective_index;

    uint16_t lane = Read<uint16_t>(code);
    memory_type res = value.val[LANE(lane, value)];
    base::WriteUnalignedValue<memory_type>(reinterpret_cast<Address>(address),
                                           res);

    NextOp();
  }
  static auto constexpr s2s_SimdS128Store8Lane =
      s2s_DoSimdStoreLane<int8x16, int8_t>;
  static auto constexpr s2s_SimdS128Store16Lane =
      s2s_DoSimdStoreLane<int16x8, int16_t>;
  static auto constexpr s2s_SimdS128Store32Lane =
      s2s_DoSimdStoreLane<int32x4, int32_t>;
  static auto constexpr s2s_SimdS128Store64Lane =
      s2s_DoSimdStoreLane<int64x2, uint64_t>;
  static auto constexpr s2s_SimdS128Store8Lane_Idx64 =
      s2s_DoSimdStoreLane<int8x16, int8_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_SimdS128Store16Lane_Idx64 =
      s2s_DoSimdStoreLane<int16x8, int16_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_SimdS128Store32Lane_Idx64 =
      s2s_DoSimdStoreLane<int32x4, int32_t, uint64_t, memory_offset64_t>;
  static auto constexpr s2s_SimdS128Store64Lane_Idx64 =
      s2s_DoSimdStoreLane<int64x2, int64_t, uint64_t, memory_offset64_t>;

  template <typename DstSimdType, typename SrcSimdType, typename Wide,
            typename Narrow>
  INSTRUCTION_HANDLER_FUNC s2s_DoSimdExtAddPairwise(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    constexpr int lanes = kSimd128Size / sizeof(DstSimdType::val[0]);
    auto v = pop<Simd128>(sp, code, wasm_runtime).template to<SrcSimdType>();
    DstSimdType res;
    for (int i = 0; i < lanes; ++i) {
      res.val[LANE(i, res)] =
          AddLong<Wide>(static_cast<Narrow>(v.val[LANE(i * 2, v)]),
                        static_cast<Narrow>(v.val[LANE(i * 2 + 1, v)]));
    }
    push<Simd128>(sp, code, wasm_runtime, Simd128(res));

    NextOp();
  }
  static auto constexpr s2s_SimdI32x4ExtAddPairwiseI16x8S =
      s2s_DoSimdExtAddPairwise<int32x4, int16x8, int32_t, int16_t>;
  static auto constexpr s2s_SimdI32x4ExtAddPairwiseI16x8U =
      s2s_DoSimdExtAddPairwise<int32x4, int16x8, uint32_t, uint16_t>;
  static auto constexpr s2s_SimdI16x8ExtAddPairwiseI8x16S =
      s2s_DoSimdExtAddPairwise<int16x8, int8x16, int16_t, int8_t>;
  static auto constexpr s2s_SimdI16x8ExtAddPairwiseI8x16U =
      s2s_DoSimdExtAddPairwise<int16x8, int8x16, uint16_t, uint8_t>;

  ////////////////////////////////////////////////////////////////////////////////

  // Allocate, initialize and throw a new exception. The exception values are
  // being popped off the operand stack.
  INSTRUCTION_HANDLER_FUNC s2s_Throw(const uint8_t* code, uint32_t* sp,
                                     WasmInterpreterRuntime* wasm_runtime,
                                     int64_t r0, double fp0) {
    Isolate* isolate = wasm_runtime->GetIsolate();
    {
      HandleScope handle_scope(isolate);  // Avoid leaking handles.

      uint32_t tag_index = Read<int32_t>(code);

      Handle<WasmExceptionPackage> exception_object =
          wasm_runtime->CreateWasmExceptionPackage(tag_index);
      Handle<FixedArray> encoded_values = Cast<FixedArray>(
          WasmExceptionPackage::GetExceptionValues(isolate, exception_object));

      // Encode the exception values on the operand stack into the exception
      // package allocated above.
      const WasmTagSig* sig = wasm_runtime->GetWasmTag(tag_index).sig;
      uint32_t encoded_index = 0;
      for (size_t index = 0; index < sig->parameter_count(); index++) {
        switch (sig->GetParam(index).kind()) {
          case kI32: {
            uint32_t u32 = pop<uint32_t>(sp, code, wasm_runtime);
            EncodeI32ExceptionValue(encoded_values, &encoded_index, u32);
            break;
          }
          case kF32: {
            float f32 = pop<float>(sp, code, wasm_runtime);
            EncodeI32ExceptionValue(encoded_values, &encoded_index,
                                    *reinterpret_cast<uint32_t*>(&f32));
            break;
          }
          case kI64: {
            uint64_t u64 = pop<uint64_t>(sp, code, wasm_runtime);
            EncodeI64ExceptionValue(encoded_values, &encoded_index, u64);
            break;
          }
          case kF64: {
            double f64 = pop<double>(sp, code, wasm_runtime);
            EncodeI64ExceptionValue(encoded_values, &encoded_index,
                                    *reinterpret_cast<uint64_t*>(&f64));
            break;
          }
          case kS128: {
            int32x4 s128 = pop<Simd128>(sp, code, wasm_runtime).to_i32x4();
            EncodeI32ExceptionValue(encoded_values, &encoded_index,
                                    s128.val[0]);
            EncodeI32ExceptionValue(encoded_values, &encoded_index,
                                    s128.val[1]);
            EncodeI32ExceptionValue(encoded_values, &encoded_index,
                                    s128.val[2]);
            EncodeI32ExceptionValue(encoded_values, &encoded_index,
                                    s128.val[3]);
            break;
          }
          case kRef:
          case kRefNull: {
            Handle<Object> ref = pop<WasmRef>(sp, code, wasm_runtime);
            if (IsWasmNull(*ref, isolate)) {
              ref = handle(ReadOnlyRoots(isolate).null_value(), isolate);
            }
            encoded_values->set(encoded_index++, *ref);
            break;
          }
          default:
            UNREACHABLE();
        }
      }

      wasm_runtime->ThrowException(code, sp, *exception_object);
    }
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_Rethrow(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    uint32_t catch_block_index = Read<int32_t>(code);
    wasm_runtime->RethrowException(code, sp, catch_block_index);

    NextOp();
  }

  //////////////////////////////////////////////////////////////////////////////
  // GC instruction handlers.

  INSTRUCTION_HANDLER_FUNC s2s_BranchOnNull(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    // TODO(paolosev@microsoft.com): Implement peek<T>?
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);

    const uint32_t ref_bitfield = Read<int32_t>(code);
    ValueType ref_type = ValueType::FromRawBitField(ref_bitfield);

    push<WasmRef>(sp, code, wasm_runtime, ref);

    int32_t if_null_offset = Read<int32_t>(code);
    if (wasm_runtime->IsNullTypecheck(ref, ref_type)) {
      // If condition is true (ref is null), jump to the target branch.
      code += (if_null_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  /*
   * Notice that in s2s_BranchOnNullWithParams the branch happens when the
   * condition is false, not true, as follows:
   *
   *   > s2s_BranchOnNullWithParams
   *       pop - ref
   *       i32: ref value_tye
   *       push - ref
   *       branch_offset (if NOT NULL)  ----+
   *   > s2s_CopySlot                       |
   *       ....                             |
   *   > s2s_Branch (gets here if NULL)     |
   *       branch_offset                    |
   *   > (next instruction) <---------------+
   */
  INSTRUCTION_HANDLER_FUNC s2s_BranchOnNullWithParams(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    // TO(paolosev@microsoft.com): Implement peek<T>?
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);

    const uint32_t ref_bitfield = Read<int32_t>(code);
    ValueType ref_type = ValueType::FromRawBitField(ref_bitfield);

    push<WasmRef>(sp, code, wasm_runtime, ref);

    int32_t if_null_offset = Read<int32_t>(code);
    if (!wasm_runtime->IsNullTypecheck(ref, ref_type)) {
      // If condition is false (ref is not null), jump to the false branch.
      code += (if_null_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_BranchOnNonNull(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    // TO(paolosev@microsoft.com): Implement peek<T>?
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);

    const uint32_t ref_bitfield = Read<int32_t>(code);
    ValueType ref_type = ValueType::FromRawBitField(ref_bitfield);

    push<WasmRef>(sp, code, wasm_runtime, ref);

    int32_t if_non_null_offset = Read<int32_t>(code);
    if (!wasm_runtime->IsNullTypecheck(ref, ref_type)) {
      // If condition is true (ref is not null), jump to the target branch.
      code += (if_non_null_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_BranchOnNonNullWithParams(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    // TO(paolosev@microsoft.com): Implement peek<T>?
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);

    const uint32_t ref_bitfield = Read<int32_t>(code);
    ValueType ref_type = ValueType::FromRawBitField(ref_bitfield);

    push<WasmRef>(sp, code, wasm_runtime, ref);

    int32_t if_non_null_offset = Read<int32_t>(code);
    if (wasm_runtime->IsNullTypecheck(ref, ref_type)) {
      // If condition is false (ref is null), jump to the false branch.
      code += (if_non_null_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  static bool DoRefCast(WasmRef ref, ValueType ref_type, HeapType target_type,
                        bool null_succeeds,
                        WasmInterpreterRuntime* wasm_runtime) {
    if (target_type.is_index()) {
      Handle<Map> rtt = wasm_runtime->RttCanon(target_type.ref_index().index);
      return wasm_runtime->SubtypeCheck(ref, ref_type, rtt,
                                        ValueType::Rtt(target_type.ref_index()),
                                        null_succeeds);
    } else {
      switch (target_type.representation()) {
        case HeapType::kEq:
          return wasm_runtime->RefIsEq(ref, ref_type, null_succeeds);
        case HeapType::kI31:
          return wasm_runtime->RefIsI31(ref, ref_type, null_succeeds);
        case HeapType::kStruct:
          return wasm_runtime->RefIsStruct(ref, ref_type, null_succeeds);
        case HeapType::kArray:
          return wasm_runtime->RefIsArray(ref, ref_type, null_succeeds);
        case HeapType::kString:
          return wasm_runtime->RefIsString(ref, ref_type, null_succeeds);
        case HeapType::kNone:
        case HeapType::kNoExtern:
        case HeapType::kNoFunc:
          DCHECK(null_succeeds);
          return wasm_runtime->IsNullTypecheck(ref, ref_type);
        case HeapType::kAny:
          // Any may never need a cast as it is either implicitly convertible or
          // never convertible for any given type.
        default:
          UNREACHABLE();
      }
    }
  }

  /*
   * Notice that in s2s_BranchOnCast the branch happens when the condition is
   * false, not true, as follows:
   *
   *   > s2s_BranchOnCast
   *       i32: null_succeeds
   *       i32: target_type HeapType representation
   *       pop - ref
   *       i32: ref value_tye
   *       push - ref
   *       branch_offset (if CAST FAILS) --------+
   *   > s2s_CopySlot                            |
   *       ....                                  |
   *   > s2s_Branch (gets here if CAST SUCCEEDS) |
   *       branch_offset                         |
   *   > (next instruction) <--------------------+
   */
  INSTRUCTION_HANDLER_FUNC s2s_BranchOnCast(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    bool null_succeeds = Read<int32_t>(code);
    HeapType target_type(
        ModuleTypeIndex({static_cast<uint32_t>(Read<int32_t>(code))}));

    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
    const uint32_t ref_bitfield = Read<int32_t>(code);
    ValueType ref_type = ValueType::FromRawBitField(ref_bitfield);
    push<WasmRef>(sp, code, wasm_runtime, ref);
    int32_t no_branch_offset = Read<int32_t>(code);

    if (!DoRefCast(ref, ref_type, target_type, null_succeeds, wasm_runtime)) {
      // If condition is not true, jump to the 'false' branch.
      code += (no_branch_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  /*
   * Notice that in s2s_BranchOnCastFail the branch happens when the condition
   * is false, not true, as follows:
   *
   *   > s2s_BranchOnCastFail
   *       i32: null_succeeds
   *       i32: target_type HeapType representation
   *       pop - ref
   *       i32: ref value_tye
   *       push - ref
   *       branch_offset (if CAST SUCCEEDS) --+
   *   > s2s_CopySlot                         |
   *       ....                               |
   *   > s2s_Branch (gets here if CAST FAILS) |
   *       branch_offset                      |
   *   > (next instruction) <-----------------+
   */
  INSTRUCTION_HANDLER_FUNC s2s_BranchOnCastFail(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    bool null_succeeds = Read<int32_t>(code);
    HeapType target_type(
        ModuleTypeIndex({static_cast<uint32_t>(Read<int32_t>(code))}));

    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
    const uint32_t ref_bitfield = Read<int32_t>(code);
    ValueType ref_type = ValueType::FromRawBitField(ref_bitfield);
    push<WasmRef>(sp, code, wasm_runtime, ref);
    int32_t branch_offset = Read<int32_t>(code);

    if (DoRefCast(ref, ref_type, target_type, null_succeeds, wasm_runtime)) {
      // If condition is true, jump to the 'true' branch.
      code += (branch_offset - kCodeOffsetSize);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_CallRef(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    WasmRef func_ref = pop<WasmRef>(sp, code, wasm_runtime);
    uint32_t sig_index = Read<int32_t>(code);
    uint32_t stack_pos = Read<int32_t>(code);
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    uint32_t ref_stack_fp_offset = Read<int32_t>(code);
    slot_offset_t return_slot_offset = 0;
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      return_slot_offset = Read<slot_offset_t>(code);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    if (V8_UNLIKELY(wasm_runtime->IsRefNull(func_ref))) {
      TRAP(TrapReason::kTrapNullDereference)
    }

    // This can trap.
    wasm_runtime->ExecuteCallRef(code, func_ref, sig_index, stack_pos, sp,
                                 ref_stack_fp_offset, slot_offset,
                                 return_slot_offset, false);
    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_ReturnCallRef(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t rets_size = Read<slot_offset_t>(code);
    slot_offset_t args_size = Read<slot_offset_t>(code);
    uint32_t rets_refs = Read<int32_t>(code);
    uint32_t args_refs = Read<int32_t>(code);

    WasmRef func_ref = pop<WasmRef>(sp, code, wasm_runtime);
    uint32_t sig_index = Read<int32_t>(code);
    uint32_t stack_pos = Read<int32_t>(code);
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    uint32_t ref_stack_fp_offset = Read<int32_t>(code);
    slot_offset_t return_slot_offset = 0;
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      return_slot_offset = Read<slot_offset_t>(code);
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

    if (V8_UNLIKELY(wasm_runtime->IsRefNull(func_ref))) {
      TRAP(TrapReason::kTrapNullDereference)
    }

    // Moves back the stack frame to the caller stack frame.
    wasm_runtime->UnwindCurrentStackFrame(sp, slot_offset, rets_size, args_size,
                                          rets_refs, args_refs,
                                          ref_stack_fp_offset);

    // TODO(paolosev@microsoft.com) - This calls adds a new C++ stack frame,
    // which is not ideal in a tail-call.
    wasm_runtime->ExecuteCallRef(code, func_ref, sig_index, stack_pos, sp, 0, 0,
                                 return_slot_offset, true);

    NextOp();
  }

  static void StoreRefIntoMemory(Tagged<HeapObject> host, Address dst_addr,
                                 uint32_t offset, Tagged<Object> value,
                                 WriteBarrierMode mode) {
    DCHECK_EQ(dst_addr, host.ptr() + offset - kHeapObjectTag);

    // Only stores the lower 32-bit.
    base::WriteUnalignedValue<Tagged_t>(
        dst_addr, V8HeapCompressionScheme::CompressObject(value.ptr()));

    // Need to generate a GC write barrier.
    CONDITIONAL_WRITE_BARRIER(host, offset, value, mode);
  }

  INSTRUCTION_HANDLER_FUNC s2s_StructNew(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    uint32_t index = Read<int32_t>(code);
    std::pair<Handle<WasmStruct>, const StructType*> struct_new_result =
        wasm_runtime->StructNewUninitialized(index);
    Handle<HeapObject> struct_obj = struct_new_result.first;
    const StructType* struct_type = struct_new_result.second;

    {
      // The new struct is uninitialized, which means GC might fail until
      // initialization.
      DisallowHeapAllocation no_gc;

      for (uint32_t i = struct_type->field_count(); i > 0;) {
        i--;
        int field_offset = StructFieldOffset(struct_type, i);
        Address field_addr = (*struct_obj).ptr() + field_offset;

        ValueKind kind = struct_type->field(i).kind();
        switch (kind) {
          case kI8:
            *reinterpret_cast<int8_t*>(field_addr) =
                pop<int32_t>(sp, code, wasm_runtime);
            break;
          case kI16:
            base::WriteUnalignedValue<int16_t>(
                field_addr, pop<int32_t>(sp, code, wasm_runtime));
            break;
          case kI32:
            base::WriteUnalignedValue<int32_t>(
                field_addr, pop<int32_t>(sp, code, wasm_runtime));
            break;
          case kI64:
            base::WriteUnalignedValue<int64_t>(
                field_addr, pop<int64_t>(sp, code, wasm_runtime));
            break;
          case kF32:
            base::WriteUnalignedValue<float>(
                field_addr, pop<float>(sp, code, wasm_runtime));
            break;
          case kF64:
            base::WriteUnalignedValue<double>(
                field_addr, pop<double>(sp, code, wasm_runtime));
            break;
          case kS128:
            base::WriteUnalignedValue<Simd128>(
                field_addr, pop<Simd128>(sp, code, wasm_runtime));
            break;
          case kRef:
          case kRefNull: {
            WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
            StoreRefIntoMemory(
                *struct_obj, field_addr,
                field_offset + kHeapObjectTag,  // field_offset is offset into
                                                // tagged object.
                *ref, SKIP_WRITE_BARRIER);
            break;
          }
          default:
            UNREACHABLE();
        }
      }
    }

    push<WasmRef>(sp, code, wasm_runtime, struct_obj);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_StructNewDefault(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t index = Read<int32_t>(code);
    std::pair<Handle<WasmStruct>, const StructType*> struct_new_result =
        wasm_runtime->StructNewUninitialized(index);
    Handle<HeapObject> struct_obj = struct_new_result.first;
    const StructType* struct_type = struct_new_result.second;

    {
      // The new struct is uninitialized, which means GC might fail until
      // initialization.
      DisallowHeapAllocation no_gc;

      for (uint32_t i = struct_type->field_count(); i > 0;) {
        i--;
        int field_offset = StructFieldOffset(struct_type, i);
        Address field_addr = (*struct_obj).ptr() + field_offset;

        const ValueType value_type = struct_type->field(i);
        const ValueKind kind = value_type.kind();
        switch (kind) {
          case kI8:
            *reinterpret_cast<int8_t*>(field_addr) = int8_t{};
            break;
          case kI16:
            base::WriteUnalignedValue<int16_t>(field_addr, int16_t{});
            break;
          case kI32:
            base::WriteUnalignedValue<int32_t>(field_addr, int32_t{});
            break;
          case kI64:
            base::WriteUnalignedValue<int64_t>(field_addr, int64_t{});
            break;
          case kF32:
            base::WriteUnalignedValue<float>(field_addr, float{});
            break;
          case kF64:
            base::WriteUnalignedValue<double>(field_addr, double{});
            break;
          case kS128:
            base::WriteUnalignedValue<Simd128>(field_addr, Simd128{});
            break;
          case kRef:
          case kRefNull:
            StoreRefIntoMemory(
                *struct_obj, field_addr,
                field_offset + kHeapObjectTag,  // field_offset is offset into
                                                // tagged object.
                wasm_runtime->GetNullValue(value_type), SKIP_WRITE_BARRIER);
            break;
          default:
            UNREACHABLE();
        }
      }
    }

    push<WasmRef>(sp, code, wasm_runtime, struct_obj);

    NextOp();
  }

  template <typename T, typename U = T>
  INSTRUCTION_HANDLER_FUNC s2s_StructGet(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    WasmRef struct_obj = pop<WasmRef>(sp, code, wasm_runtime);

    if (V8_UNLIKELY(wasm_runtime->IsRefNull(struct_obj))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    int offset = Read<int32_t>(code);
    Address field_addr = (*struct_obj).ptr() + offset;
    push<T>(sp, code, wasm_runtime, base::ReadUnalignedValue<U>(field_addr));

    NextOp();
  }
  static auto constexpr s2s_I8SStructGet = s2s_StructGet<int32_t, int8_t>;
  static auto constexpr s2s_I8UStructGet = s2s_StructGet<uint32_t, uint8_t>;
  static auto constexpr s2s_I16SStructGet = s2s_StructGet<int32_t, int16_t>;
  static auto constexpr s2s_I16UStructGet = s2s_StructGet<uint32_t, uint16_t>;
  static auto constexpr s2s_I32StructGet = s2s_StructGet<int32_t>;
  static auto constexpr s2s_I64StructGet = s2s_StructGet<int64_t>;
  static auto constexpr s2s_F32StructGet = s2s_StructGet<float>;
  static auto constexpr s2s_F64StructGet = s2s_StructGet<double>;
  static auto constexpr s2s_S128StructGet = s2s_StructGet<Simd128>;

  INSTRUCTION_HANDLER_FUNC s2s_RefStructGet(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    WasmRef struct_obj = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(struct_obj))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    int offset = Read<int32_t>(code);
    Address field_addr = (*struct_obj).ptr() + offset;
    // DrumBrake expects pointer compression.
    Tagged_t ref_tagged = base::ReadUnalignedValue<uint32_t>(field_addr);
    Isolate* isolate = wasm_runtime->GetIsolate();
    Tagged<Object> ref_uncompressed(
        V8HeapCompressionScheme::DecompressTagged(isolate, ref_tagged));
    WasmRef ref_handle = handle(ref_uncompressed, isolate);
    push<WasmRef>(sp, code, wasm_runtime, ref_handle);

    NextOp();
  }

  template <typename T, typename U = T>
  INSTRUCTION_HANDLER_FUNC s2s_StructSet(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    int offset = Read<int32_t>(code);
    T value = pop<T>(sp, code, wasm_runtime);
    WasmRef struct_obj = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(struct_obj))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    Address field_addr = (*struct_obj).ptr() + offset;
    base::WriteUnalignedValue<U>(field_addr, value);

    NextOp();
  }
  static auto constexpr s2s_I8StructSet = s2s_StructSet<int32_t, int8_t>;
  static auto constexpr s2s_I16StructSet = s2s_StructSet<int32_t, int16_t>;
  static auto constexpr s2s_I32StructSet = s2s_StructSet<int32_t>;
  static auto constexpr s2s_I64StructSet = s2s_StructSet<int64_t>;
  static auto constexpr s2s_F32StructSet = s2s_StructSet<float>;
  static auto constexpr s2s_F64StructSet = s2s_StructSet<double>;
  static auto constexpr s2s_S128StructSet = s2s_StructSet<Simd128>;

  INSTRUCTION_HANDLER_FUNC s2s_RefStructSet(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    int field_offset = Read<int32_t>(code);
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
    WasmRef struct_obj = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(struct_obj))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    Address field_addr = (*struct_obj).ptr() + field_offset;
    StoreRefIntoMemory(
        Cast<HeapObject>(*struct_obj), field_addr,
        field_offset +
            kHeapObjectTag,  // field_offset is offset into tagged object.
        *ref, UPDATE_WRITE_BARRIER);

    NextOp();
  }

  template <typename T, typename U = T>
  INSTRUCTION_HANDLER_FUNC s2s_ArrayNew(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    const uint32_t array_index = Read<int32_t>(code);
    const uint32_t elem_count = pop<int32_t>(sp, code, wasm_runtime);
    const T value = pop<T>(sp, code, wasm_runtime);

    std::pair<Handle<WasmArray>, const ArrayType*> array_new_result =
        wasm_runtime->ArrayNewUninitialized(elem_count, array_index);
    Handle<WasmArray> array = array_new_result.first;
    if (V8_UNLIKELY(array.is_null())) {
      TRAP(TrapReason::kTrapArrayTooLarge)
    }

    {
      // The new array is uninitialized, which means GC might fail until
      // initialization.
      DisallowHeapAllocation no_gc;

      const ArrayType* array_type = array_new_result.second;
      const ValueKind kind = array_type->element_type().kind();
      const uint32_t element_size = value_kind_size(kind);
      DCHECK_EQ(element_size, sizeof(U));

      Address element_addr = array->ElementAddress(0);
      for (uint32_t i = 0; i < elem_count; i++) {
        base::WriteUnalignedValue<U>(element_addr, value);
        element_addr += element_size;
      }
    }

    push<WasmRef>(sp, code, wasm_runtime, array);

    NextOp();
  }
  static auto constexpr s2s_I8ArrayNew = s2s_ArrayNew<int32_t, int8_t>;
  static auto constexpr s2s_I16ArrayNew = s2s_ArrayNew<int32_t, int16_t>;
  static auto constexpr s2s_I32ArrayNew = s2s_ArrayNew<int32_t>;
  static auto constexpr s2s_I64ArrayNew = s2s_ArrayNew<int64_t>;
  static auto constexpr s2s_F32ArrayNew = s2s_ArrayNew<float>;
  static auto constexpr s2s_F64ArrayNew = s2s_ArrayNew<double>;
  static auto constexpr s2s_S128ArrayNew = s2s_ArrayNew<Simd128>;

  INSTRUCTION_HANDLER_FUNC s2s_RefArrayNew(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    const uint32_t array_index = Read<int32_t>(code);
    const uint32_t elem_count = pop<int32_t>(sp, code, wasm_runtime);
    const WasmRef value = pop<WasmRef>(sp, code, wasm_runtime);

    std::pair<Handle<WasmArray>, const ArrayType*> array_new_result =
        wasm_runtime->ArrayNewUninitialized(elem_count, array_index);
    Handle<WasmArray> array = array_new_result.first;
    if (V8_UNLIKELY(array.is_null())) {
      TRAP(TrapReason::kTrapArrayTooLarge)
    }

#if DEBUG
    const ArrayType* array_type = array_new_result.second;
    DCHECK_EQ(value_kind_size(array_type->element_type().kind()),
              sizeof(Tagged_t));
#endif

    {
      // The new array is uninitialized, which means GC might fail until
      // initialization.
      DisallowHeapAllocation no_gc;

      Address element_addr = array->ElementAddress(0);
      uint32_t element_offset = array->element_offset(0);
      for (uint32_t i = 0; i < elem_count; i++) {
        StoreRefIntoMemory(Cast<HeapObject>(*array), element_addr,
                           element_offset, *value, SKIP_WRITE_BARRIER);
        element_addr += sizeof(Tagged_t);
        element_offset += sizeof(Tagged_t);
      }
    }

    push<WasmRef>(sp, code, wasm_runtime, array);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_ArrayNewFixed(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    const uint32_t array_index = Read<int32_t>(code);
    const uint32_t elem_count = Read<int32_t>(code);

    std::pair<Handle<WasmArray>, const ArrayType*> array_new_result =
        wasm_runtime->ArrayNewUninitialized(elem_count, array_index);
    Handle<WasmArray> array = array_new_result.first;
    if (V8_UNLIKELY(array.is_null())) {
      TRAP(TrapReason::kTrapArrayTooLarge)
    }

    {
      // The new array is uninitialized, which means GC might fail until
      // initialization.
      DisallowHeapAllocation no_gc;

      if (elem_count > 0) {
        const ArrayType* array_type = array_new_result.second;
        const ValueKind kind = array_type->element_type().kind();
        const uint32_t element_size = value_kind_size(kind);

        Address element_addr = array->ElementAddress(elem_count - 1);
        uint32_t element_offset = array->element_offset(elem_count - 1);
        for (uint32_t i = 0; i < elem_count; i++) {
          switch (kind) {
            case kI8:
              *reinterpret_cast<int8_t*>(element_addr) =
                  pop<int32_t>(sp, code, wasm_runtime);
              break;
            case kI16:
              base::WriteUnalignedValue<int16_t>(
                  element_addr, pop<int32_t>(sp, code, wasm_runtime));
              break;
            case kI32:
              base::WriteUnalignedValue<int32_t>(
                  element_addr, pop<int32_t>(sp, code, wasm_runtime));
              break;
            case kI64:
              base::WriteUnalignedValue<int64_t>(
                  element_addr, pop<int64_t>(sp, code, wasm_runtime));
              break;
            case kF32:
              base::WriteUnalignedValue<float>(
                  element_addr, pop<float>(sp, code, wasm_runtime));
              break;
            case kF64:
              base::WriteUnalignedValue<double>(
                  element_addr, pop<double>(sp, code, wasm_runtime));
              break;
            case kS128:
              base::WriteUnalignedValue<Simd128>(
                  element_addr, pop<Simd128>(sp, code, wasm_runtime));
              break;
            case kRef:
            case kRefNull: {
              WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
              StoreRefIntoMemory(Cast<HeapObject>(*array), element_addr,
                                 element_offset, *ref, SKIP_WRITE_BARRIER);
              break;
            }
            default:
              UNREACHABLE();
          }
          element_addr -= element_size;
          element_offset -= element_size;
        }
      }
    }

    push<WasmRef>(sp, code, wasm_runtime, array);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC
  s2s_ArrayNewDefault(const uint8_t* code, uint32_t* sp,
                      WasmInterpreterRuntime* wasm_runtime, int64_t r0,
                      double fp0) {
    const uint32_t array_index = Read<int32_t>(code);
    const uint32_t elem_count = pop<int32_t>(sp, code, wasm_runtime);

    std::pair<Handle<WasmArray>, const ArrayType*> array_new_result =
        wasm_runtime->ArrayNewUninitialized(elem_count, array_index);
    Handle<WasmArray> array = array_new_result.first;
    if (V8_UNLIKELY(array.is_null())) {
      TRAP(TrapReason::kTrapArrayTooLarge)
    }

    {
      // The new array is uninitialized, which means GC might fail until
      // initialization.
      DisallowHeapAllocation no_gc;

      const ArrayType* array_type = array_new_result.second;
      const ValueType element_type = array_type->element_type();
      const ValueKind kind = element_type.kind();
      const uint32_t element_size = value_kind_size(kind);

      Address element_addr = array->ElementAddress(0);
      uint32_t element_offset = array->element_offset(0);
      for (uint32_t i = 0; i < elem_count; i++) {
        switch (kind) {
          case kI8:
            *reinterpret_cast<int8_t*>(element_addr) = int8_t{};
            break;
          case kI16:
            base::WriteUnalignedValue<int16_t>(element_addr, int16_t{});
            break;
          case kI32:
            base::WriteUnalignedValue<int32_t>(element_addr, int32_t{});
            break;
          case kI64:
            base::WriteUnalignedValue<int64_t>(element_addr, int64_t{});
            break;
          case kF32:
            base::WriteUnalignedValue<float>(element_addr, float{});
            break;
          case kF64:
            base::WriteUnalignedValue<double>(element_addr, double{});
            break;
          case kS128:
            base::WriteUnalignedValue<Simd128>(element_addr, Simd128{});
            break;
          case kRef:
          case kRefNull:
            StoreRefIntoMemory(
                Cast<HeapObject>(*array), element_addr, element_offset,
                wasm_runtime->GetNullValue(element_type), SKIP_WRITE_BARRIER);
            break;
          default:
            UNREACHABLE();
        }
        element_addr += element_size;
        element_offset += element_size;
      }
    }

    push<WasmRef>(sp, code, wasm_runtime, array);

    NextOp();
  }

  template <TrapReason OutOfBoundsError>
  INSTRUCTION_HANDLER_FUNC s2s_ArrayNewSegment(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    const uint32_t array_index = Read<int32_t>(code);
    // TODO(paolosev@microsoft.com): already validated?
    if (V8_UNLIKELY(!Smi::IsValid(array_index))) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    }

    const uint32_t data_index = Read<int32_t>(code);
    // TODO(paolosev@microsoft.com): already validated?
    if (V8_UNLIKELY(!Smi::IsValid(data_index))) {
      TRAP(OutOfBoundsError)
    }

    uint32_t length = pop<int32_t>(sp, code, wasm_runtime);
    uint32_t offset = pop<int32_t>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(!Smi::IsValid(offset))) {
      TRAP(OutOfBoundsError)
    }
    if (V8_UNLIKELY(length >= static_cast<uint32_t>(WasmArray::MaxLength(
                                  wasm_runtime->GetArrayType(array_index))))) {
      TRAP(TrapReason::kTrapArrayTooLarge)
    }

    WasmRef result = wasm_runtime->WasmArrayNewSegment(array_index, data_index,
                                                       offset, length);
    if (V8_UNLIKELY(result.is_null())) {
      wasm::TrapReason reason = WasmInterpreterThread::GetRuntimeLastWasmError(
          wasm_runtime->GetIsolate());
      INLINED_TRAP(reason)
    }
    push<WasmRef>(sp, code, wasm_runtime, result);

    NextOp();
  }
  // The instructions array.new_data and array.new_elem have the same
  // implementation after validation. The only difference is that
  // array.init_elem is used with arrays that contain elements of reference
  // types, and array.init_data with arrays that contain elements of numeric
  // types.
  static auto constexpr s2s_ArrayNewData =
      s2s_ArrayNewSegment<kTrapDataSegmentOutOfBounds>;
  static auto constexpr s2s_ArrayNewElem =
      s2s_ArrayNewSegment<kTrapElementSegmentOutOfBounds>;

  template <bool init_data>
  INSTRUCTION_HANDLER_FUNC s2s_ArrayInitSegment(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    const uint32_t array_index = Read<int32_t>(code);
    // TODO(paolosev@microsoft.com): already validated?
    if (V8_UNLIKELY(!Smi::IsValid(array_index))) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    }

    const uint32_t data_index = Read<int32_t>(code);
    // TODO(paolosev@microsoft.com): already validated?
    if (V8_UNLIKELY(!Smi::IsValid(data_index))) {
      TRAP(TrapReason::kTrapElementSegmentOutOfBounds)
    }

    uint32_t size = pop<int32_t>(sp, code, wasm_runtime);
    uint32_t src_offset = pop<int32_t>(sp, code, wasm_runtime);
    uint32_t dest_offset = pop<int32_t>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(!Smi::IsValid(size)) || !Smi::IsValid(dest_offset)) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    }
    if (V8_UNLIKELY(!Smi::IsValid(src_offset))) {
      TrapReason reason = init_data
                              ? TrapReason::kTrapDataSegmentOutOfBounds
                              : TrapReason::kTrapElementSegmentOutOfBounds;
      INLINED_TRAP(reason);
    }

    WasmRef array = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(array))) {
      TRAP(TrapReason::kTrapNullDereference)
    }

    bool ok = wasm_runtime->WasmArrayInitSegment(data_index, array, dest_offset,
                                                 src_offset, size);
    if (V8_UNLIKELY(!ok)) {
      TrapReason reason = WasmInterpreterThread::GetRuntimeLastWasmError(
          wasm_runtime->GetIsolate());
      INLINED_TRAP(reason)
    }

    NextOp();
  }
  // The instructions array.init_data and array.init_elem have the same
  // implementation after validation. The only difference is that
  // array.init_elem is used with arrays that contain elements of reference
  // types, and array.init_data with arrays that contain elements of numeric
  // types.
  static auto constexpr s2s_ArrayInitData = s2s_ArrayInitSegment<true>;
  static auto constexpr s2s_ArrayInitElem = s2s_ArrayInitSegment<false>;

  INSTRUCTION_HANDLER_FUNC s2s_ArrayLen(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    WasmRef array_obj = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(array_obj))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    DCHECK(IsWasmArray(*array_obj));

    Tagged<WasmArray> array = Cast<WasmArray>(*array_obj);
    push<int32_t>(sp, code, wasm_runtime, array->length());

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_ArrayCopy(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    const uint32_t dest_array_index = Read<int32_t>(code);
    const uint32_t src_array_index = Read<int32_t>(code);
    // TODO(paolosev@microsoft.com): already validated?
    if (V8_UNLIKELY(!Smi::IsValid(dest_array_index) ||
                    !Smi::IsValid(src_array_index))) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    }

    uint32_t size = pop<int32_t>(sp, code, wasm_runtime);
    uint32_t src_offset = pop<int32_t>(sp, code, wasm_runtime);
    WasmRef src_array = pop<WasmRef>(sp, code, wasm_runtime);
    uint32_t dest_offset = pop<int32_t>(sp, code, wasm_runtime);
    WasmRef dest_array = pop<WasmRef>(sp, code, wasm_runtime);

    if (V8_UNLIKELY(!Smi::IsValid(size) || !Smi::IsValid(src_offset) ||
                    !Smi::IsValid(dest_offset))) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    } else if (V8_UNLIKELY(wasm_runtime->IsRefNull(dest_array))) {
      TRAP(TrapReason::kTrapNullDereference)
    } else if (V8_UNLIKELY(dest_offset + size >
                           Cast<WasmArray>(*dest_array)->length())) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    } else if (V8_UNLIKELY(wasm_runtime->IsRefNull(src_array))) {
      TRAP(TrapReason::kTrapNullDereference)
    } else if (V8_UNLIKELY(src_offset + size >
                           Cast<WasmArray>(*src_array)->length())) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    }

    bool ok = true;
    if (size > 0) {
      ok = wasm_runtime->WasmArrayCopy(dest_array, dest_offset, src_array,
                                       src_offset, size);
    }

    if (V8_UNLIKELY(!ok)) {
      wasm::TrapReason reason = WasmInterpreterThread::GetRuntimeLastWasmError(
          wasm_runtime->GetIsolate());
      INLINED_TRAP(reason)
    }

    NextOp();
  }

  template <typename T, typename U = T>
  INSTRUCTION_HANDLER_FUNC s2s_ArrayGet(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    uint32_t index = pop<uint32_t>(sp, code, wasm_runtime);
    WasmRef array_obj = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(array_obj))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    DCHECK(IsWasmArray(*array_obj));

    Tagged<WasmArray> array = Cast<WasmArray>(*array_obj);
    if (V8_UNLIKELY(index >= array->length())) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    }

    Address element_addr = array->ElementAddress(index);
    push<T>(sp, code, wasm_runtime, base::ReadUnalignedValue<U>(element_addr));

    NextOp();
  }
  static auto constexpr s2s_I8SArrayGet = s2s_ArrayGet<int32_t, int8_t>;
  static auto constexpr s2s_I8UArrayGet = s2s_ArrayGet<uint32_t, uint8_t>;
  static auto constexpr s2s_I16SArrayGet = s2s_ArrayGet<int32_t, int16_t>;
  static auto constexpr s2s_I16UArrayGet = s2s_ArrayGet<uint32_t, uint16_t>;
  static auto constexpr s2s_I32ArrayGet = s2s_ArrayGet<int32_t>;
  static auto constexpr s2s_I64ArrayGet = s2s_ArrayGet<int64_t>;
  static auto constexpr s2s_F32ArrayGet = s2s_ArrayGet<float>;
  static auto constexpr s2s_F64ArrayGet = s2s_ArrayGet<double>;
  static auto constexpr s2s_S128ArrayGet = s2s_ArrayGet<Simd128>;

  INSTRUCTION_HANDLER_FUNC s2s_RefArrayGet(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    uint32_t index = pop<uint32_t>(sp, code, wasm_runtime);
    WasmRef array_obj = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(array_obj))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    DCHECK(IsWasmArray(*array_obj));

    Tagged<WasmArray> array = Cast<WasmArray>(*array_obj);
    if (V8_UNLIKELY(index >= array->length())) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    }

    push<WasmRef>(sp, code, wasm_runtime,
                  wasm_runtime->GetWasmArrayRefElement(array, index));

    NextOp();
  }

  template <typename T, typename U = T>
  INSTRUCTION_HANDLER_FUNC s2s_ArraySet(const uint8_t* code, uint32_t* sp,
                                        WasmInterpreterRuntime* wasm_runtime,
                                        int64_t r0, double fp0) {
    const T value = pop<T>(sp, code, wasm_runtime);
    const uint32_t index = pop<uint32_t>(sp, code, wasm_runtime);
    WasmRef array_obj = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(array_obj))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    DCHECK(IsWasmArray(*array_obj));

    Tagged<WasmArray> array = Cast<WasmArray>(*array_obj);
    if (V8_UNLIKELY(index >= array->length())) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    }

    Address element_addr = array->ElementAddress(index);
    base::WriteUnalignedValue<U>(element_addr, value);

    NextOp();
  }
  static auto constexpr s2s_I8ArraySet = s2s_ArraySet<int32_t, int8_t>;
  static auto constexpr s2s_I16ArraySet = s2s_ArraySet<int32_t, int16_t>;
  static auto constexpr s2s_I32ArraySet = s2s_ArraySet<int32_t>;
  static auto constexpr s2s_I64ArraySet = s2s_ArraySet<int64_t>;
  static auto constexpr s2s_F32ArraySet = s2s_ArraySet<float>;
  static auto constexpr s2s_F64ArraySet = s2s_ArraySet<double>;
  static auto constexpr s2s_S128ArraySet = s2s_ArraySet<Simd128>;

  INSTRUCTION_HANDLER_FUNC s2s_RefArraySet(const uint8_t* code, uint32_t* sp,
                                           WasmInterpreterRuntime* wasm_runtime,
                                           int64_t r0, double fp0) {
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
    const uint32_t index = pop<uint32_t>(sp, code, wasm_runtime);
    WasmRef array_obj = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(array_obj))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    DCHECK(IsWasmArray(*array_obj));

    Tagged<WasmArray> array = Cast<WasmArray>(*array_obj);
    if (V8_UNLIKELY(index >= array->length())) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    }

    Address element_addr = array->ElementAddress(index);
    uint32_t element_offset = array->element_offset(index);
    StoreRefIntoMemory(array, element_addr, element_offset, *ref,
                       UPDATE_WRITE_BARRIER);

    NextOp();
  }

  template <typename T, typename U = T>
  INSTRUCTION_HANDLER_FUNC s2s_ArrayFill(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    uint32_t size = pop<uint32_t>(sp, code, wasm_runtime);
    T value = pop<U>(sp, code, wasm_runtime);
    uint32_t offset = pop<uint32_t>(sp, code, wasm_runtime);

    WasmRef array_obj = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(array_obj))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    DCHECK(IsWasmArray(*array_obj));

    Tagged<WasmArray> array = Cast<WasmArray>(*array_obj);
    if (V8_UNLIKELY(static_cast<uint64_t>(offset) + size > array->length())) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    }

    Address element_addr = array->ElementAddress(offset);
    for (uint32_t i = 0; i < size; i++) {
      base::WriteUnalignedValue<T>(element_addr, value);
      element_addr += sizeof(T);
    }

    NextOp();
  }
  static auto constexpr s2s_I8ArrayFill = s2s_ArrayFill<int8_t, int32_t>;
  static auto constexpr s2s_I16ArrayFill = s2s_ArrayFill<int16_t, int32_t>;
  static auto constexpr s2s_I32ArrayFill = s2s_ArrayFill<int32_t>;
  static auto constexpr s2s_I64ArrayFill = s2s_ArrayFill<int64_t>;
  static auto constexpr s2s_F32ArrayFill = s2s_ArrayFill<float>;
  static auto constexpr s2s_F64ArrayFill = s2s_ArrayFill<double>;
  static auto constexpr s2s_S128ArrayFill = s2s_ArrayFill<Simd128>;

  INSTRUCTION_HANDLER_FUNC s2s_RefArrayFill(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    // DrumBrake currently only works with pointer compression.
    static_assert(COMPRESS_POINTERS_BOOL);

    uint32_t size = pop<uint32_t>(sp, code, wasm_runtime);
    WasmRef value = pop<WasmRef>(sp, code, wasm_runtime);
    Tagged<Object> tagged_value = *value;
    uint32_t offset = pop<uint32_t>(sp, code, wasm_runtime);

    WasmRef array_obj = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(array_obj))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    DCHECK(IsWasmArray(*array_obj));

    Tagged<WasmArray> array = Cast<WasmArray>(*array_obj);
    if (V8_UNLIKELY(static_cast<uint64_t>(offset) + size > array->length())) {
      TRAP(TrapReason::kTrapArrayOutOfBounds)
    }

    Address element_addr = array->ElementAddress(offset);
    uint32_t element_offset = array->element_offset(offset);
    for (uint32_t i = 0; i < size; i++) {
      StoreRefIntoMemory(array, element_addr, element_offset, tagged_value,
                         UPDATE_WRITE_BARRIER);
      element_addr += kTaggedSize;
      element_offset += kTaggedSize;
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_RefI31(const uint8_t* code, uint32_t* sp,
                                      WasmInterpreterRuntime* wasm_runtime,
                                      int64_t r0, double fp0) {
    uint32_t value = pop<int32_t>(sp, code, wasm_runtime);

    // Truncate high bit.
    Tagged<Smi> smi(Internals::IntToSmi(value & 0x7fffffff));
    push<WasmRef>(sp, code, wasm_runtime,
                  handle(smi, wasm_runtime->GetIsolate()));

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_I31GetS(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(ref))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    DCHECK(IsSmi(*ref));
    push<int32_t>(sp, code, wasm_runtime, i::Smi::ToInt(*ref));

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_I31GetU(const uint8_t* code, uint32_t* sp,
                                       WasmInterpreterRuntime* wasm_runtime,
                                       int64_t r0, double fp0) {
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(ref))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    DCHECK(IsSmi(*ref));
    push<uint32_t>(sp, code, wasm_runtime,
                   0x7fffffff & static_cast<uint32_t>(i::Smi::ToInt(*ref)));

    NextOp();
  }

  template <bool null_succeeds>
  INSTRUCTION_HANDLER_FUNC RefCast(const uint8_t* code, uint32_t* sp,
                                   WasmInterpreterRuntime* wasm_runtime,
                                   int64_t r0, double fp0) {
    HeapType target_type(
        ModuleTypeIndex({static_cast<uint32_t>(Read<int32_t>(code))}));

    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);

    const uint32_t ref_bitfield = Read<int32_t>(code);
    ValueType ref_type = ValueType::FromRawBitField(ref_bitfield);

    if (!DoRefCast(ref, ref_type, target_type, null_succeeds, wasm_runtime)) {
      TRAP(TrapReason::kTrapIllegalCast)
    }

    push<WasmRef>(sp, code, wasm_runtime, ref);

    NextOp();
  }
  static auto constexpr s2s_RefCast = RefCast<false>;
  static auto constexpr s2s_RefCastNull = RefCast<true>;

  template <bool null_succeeds>
  INSTRUCTION_HANDLER_FUNC RefTest(const uint8_t* code, uint32_t* sp,
                                   WasmInterpreterRuntime* wasm_runtime,
                                   int64_t r0, double fp0) {
    HeapType target_type(
        ModuleTypeIndex({static_cast<uint32_t>(Read<int32_t>(code))}));

    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);

    const uint32_t ref_bitfield = Read<int32_t>(code);
    ValueType ref_type = ValueType::FromRawBitField(ref_bitfield);

    bool cast_succeeds =
        DoRefCast(ref, ref_type, target_type, null_succeeds, wasm_runtime);
    push<int32_t>(sp, code, wasm_runtime, cast_succeeds ? 1 : 0);

    NextOp();
  }
  static auto constexpr s2s_RefTest = RefTest<false>;
  static auto constexpr s2s_RefTestNull = RefTest<true>;

  INSTRUCTION_HANDLER_FUNC s2s_AssertNullTypecheck(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);

    const uint32_t ref_bitfield = Read<int32_t>(code);
    ValueType ref_type = ValueType::FromRawBitField(ref_bitfield);
    if (!wasm_runtime->IsNullTypecheck(ref, ref_type)) {
      TRAP(TrapReason::kTrapIllegalCast)
    }
    push<WasmRef>(sp, code, wasm_runtime, ref);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_AssertNotNullTypecheck(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);

    const uint32_t ref_bitfield = Read<int32_t>(code);
    ValueType ref_type = ValueType::FromRawBitField(ref_bitfield);
    if (wasm_runtime->IsNullTypecheck(ref, ref_type)) {
      TRAP(TrapReason::kTrapIllegalCast)
    }
    push<WasmRef>(sp, code, wasm_runtime, ref);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_TrapIllegalCast(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0){TRAP(TrapReason::kTrapIllegalCast)}

  INSTRUCTION_HANDLER_FUNC
      s2s_RefTestSucceeds(const uint8_t* code, uint32_t* sp,
                          WasmInterpreterRuntime* wasm_runtime, int64_t r0,
                          double fp0) {
    pop<WasmRef>(sp, code, wasm_runtime);
    push<int32_t>(sp, code, wasm_runtime, 1);  // true

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC
  s2s_RefTestFails(const uint8_t* code, uint32_t* sp,
                   WasmInterpreterRuntime* wasm_runtime, int64_t r0,
                   double fp0) {
    pop<WasmRef>(sp, code, wasm_runtime);
    push<int32_t>(sp, code, wasm_runtime, 0);  // false

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_RefIsNonNull(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
    push<int32_t>(sp, code, wasm_runtime, wasm_runtime->IsRefNull(ref) ? 0 : 1);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_RefAsNonNull(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);
    if (V8_UNLIKELY(wasm_runtime->IsRefNull(ref))) {
      TRAP(TrapReason::kTrapNullDereference)
    }
    push<WasmRef>(sp, code, wasm_runtime, ref);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_AnyConvertExtern(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    WasmRef extern_ref = pop<WasmRef>(sp, code, wasm_runtime);
    // Pass 0 as canonical type index; see implementation of builtin
    // WasmAnyConvertExtern.
    WasmRef result = wasm_runtime->WasmJSToWasmObject(
        extern_ref, kWasmAnyRef, 0 /* canonical type index */);
    if (V8_UNLIKELY(result.is_null())) {
      wasm::TrapReason reason = WasmInterpreterThread::GetRuntimeLastWasmError(
          wasm_runtime->GetIsolate());
      INLINED_TRAP(reason)
    }
    push<WasmRef>(sp, code, wasm_runtime, result);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC s2s_ExternConvertAny(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    WasmRef ref = pop<WasmRef>(sp, code, wasm_runtime);

    if (wasm_runtime->IsNullTypecheck(ref, kWasmAnyRef)) {
      ref = handle(wasm_runtime->GetNullValue(kWasmExternRef),
                   wasm_runtime->GetIsolate());
    }
    push<WasmRef>(sp, code, wasm_runtime, ref);

    NextOp();
  }

#ifdef V8_ENABLE_DRUMBRAKE_TRACING

  INSTRUCTION_HANDLER_FUNC s2s_TraceInstruction(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t pc = Read<int32_t>(code);
    uint32_t opcode = Read<int32_t>(code);
    uint32_t reg_mode = Read<int32_t>(code);

    if (v8_flags.trace_drumbrake_execution) {
      wasm_runtime->Trace(
          "@%-3u:         %-24s: ", pc,
          wasm::WasmOpcodes::OpcodeName(static_cast<WasmOpcode>(opcode)));
      wasm_runtime->PrintStack(sp, static_cast<RegMode>(reg_mode), r0, fp0);
    }

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC trace_UpdateStack(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t stack_index = Read<int32_t>(code);
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    wasm_runtime->TraceUpdate(stack_index, slot_offset);

    NextOp();
  }

  template <typename T>
  INSTRUCTION_HANDLER_FUNC trace_PushConstSlot(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    slot_offset_t slot_offset = Read<slot_offset_t>(code);
    wasm_runtime->TracePush<T>(slot_offset);

    NextOp();
  }
  static auto constexpr trace_PushConstI32Slot = trace_PushConstSlot<int32_t>;
  static auto constexpr trace_PushConstI64Slot = trace_PushConstSlot<int64_t>;
  static auto constexpr trace_PushConstF32Slot = trace_PushConstSlot<float>;
  static auto constexpr trace_PushConstF64Slot = trace_PushConstSlot<double>;
  static auto constexpr trace_PushConstS128Slot = trace_PushConstSlot<Simd128>;
  static auto constexpr trace_PushConstRefSlot = trace_PushConstSlot<WasmRef>;

  INSTRUCTION_HANDLER_FUNC trace_PushCopySlot(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t stack_index = Read<int32_t>(code);

    wasm_runtime->TracePushCopy(stack_index);

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC trace_PopSlot(const uint8_t* code, uint32_t* sp,
                                         WasmInterpreterRuntime* wasm_runtime,
                                         int64_t r0, double fp0) {
    wasm_runtime->TracePop();

    NextOp();
  }

  INSTRUCTION_HANDLER_FUNC trace_SetSlotType(
      const uint8_t* code, uint32_t* sp, WasmInterpreterRuntime* wasm_runtime,
      int64_t r0, double fp0) {
    uint32_t stack_index = Read<int32_t>(code);
    uint32_t type = Read<int32_t>(code);
    wasm_runtime->TraceSetSlotType(stack_index, type);

    NextOp();
  }

#endif  // V8_ENABLE_DRUMBRAKE_TRACING
};      // class Handlers<Compressed>

#ifdef V8_ENABLE_DRUMBRAKE_TRACING

void WasmBytecodeGenerator::TracePushConstSlot(uint32_t slot_index) {
  if (v8_flags.trace_drumbrake_execution) {
    START_EMIT_INSTR_HANDLER() {
      switch (slots_[slot_index].kind()) {
        case kI32:
          EMIT_INSTR_HANDLER(trace_PushConstI32Slot);
          break;
        case kI64:
          EMIT_INSTR_HANDLER(trace_PushConstI64Slot);
          break;
        case kF32:
          EMIT_INSTR_HANDLER(trace_PushConstF32Slot);
          break;
        case kF64:
          EMIT_INSTR_HANDLER(trace_PushConstF64Slot);
          break;
        case kS128:
          EMIT_INSTR_HANDLER(trace_PushConstS128Slot);
          break;
        case kRef:
        case kRefNull:
          EMIT_INSTR_HANDLER(trace_PushConstRefSlot);
          break;
        default:
          UNREACHABLE();
      }
      EmitSlotOffset(slots_[slot_index].slot_offset * kSlotSize);
    }
    END_EMIT_INSTR_HANDLER()
  }
}

void WasmBytecodeGenerator::TracePushCopySlot(uint32_t from_stack_index) {
  if (v8_flags.trace_drumbrake_execution) {
    START_EMIT_INSTR_HANDLER_WITH_ID(trace_PushCopySlot) {
      EmitI32Const(from_stack_index);
    }
    END_EMIT_INSTR_HANDLER()
  }
}

void WasmBytecodeGenerator::TraceSetSlotType(uint32_t stack_index,
                                             ValueType type) {
  if (v8_flags.trace_drumbrake_execution) {
    START_EMIT_INSTR_HANDLER_WITH_ID(trace_SetSlotType) {
      EmitI32Const(stack_index);
      EmitRefValueType(type.raw_bit_field());
    }
    END_EMIT_INSTR_HANDLER()
  }
}

void ShadowStack::Print(WasmInterpreterRuntime* wasm_runtime,
                        const uint32_t* sp, size_t start_params,
                        size_t start_locals, size_t start_stack,
                        RegMode reg_mode, int64_t r0, double fp0) const {
  for (size_t i = 0; i < stack_.size(); i++) {
    char slot_kind = i < start_locals - start_params  ? 'p'
                     : i < start_stack - start_params ? 'l'
                                                      : 's';
    const uint8_t* addr =
        reinterpret_cast<const uint8_t*>(sp) + stack_[i].slot_offset_;
    stack_[i].Print(wasm_runtime, start_params + i, slot_kind, addr);
  }

  switch (reg_mode) {
    case RegMode::kI32Reg:
      ShadowStack::Slot::Print(wasm_runtime, kWasmI32,
                               start_params + stack_.size(), 'R',
                               reinterpret_cast<const uint8_t*>(&r0));
      break;
    case RegMode::kI64Reg:
      ShadowStack::Slot::Print(wasm_runtime, kWasmI64,
                               start_params + stack_.size(), 'R',
                               reinterpret_cast<const uint8_t*>(&r0));
      break;
    case RegMode::kF32Reg: {
      float f = static_cast<float>(fp0);
      ShadowStack::Slot::Print(wasm_runtime, kWasmF32,
                               start_params + stack_.size(), 'R',
                               reinterpret_cast<const uint8_t*>(&f));
    } break;
    case RegMode::kF64Reg:
      ShadowStack::Slot::Print(wasm_runtime, kWasmF64,
                               start_params + stack_.size(), 'R',
                               reinterpret_cast<const uint8_t*>(&fp0));
      break;
    default:
      break;
  }

  wasm_runtime->Trace("\n");
}

// static
void ShadowStack::Slot::Print(WasmInterpreterRuntime* wasm_runtime,
                              ValueType type, size_t index, char kind,
                              const uint8_t* addr) {
  switch (type.kind()) {
    case kI32:
      wasm_runtime->Trace(
          "%c%zu:i32:%d ", kind, index,
          base::ReadUnalignedValue<int32_t>(reinterpret_cast<Address>(addr)));
      break;
    case kI64:
      wasm_runtime->Trace(
          "%c%zu:i64:%" PRId64, kind, index,
          base::ReadUnalignedValue<int64_t>(reinterpret_cast<Address>(addr)));
      break;
    case kF32: {
      float f =
          base::ReadUnalignedValue<float>(reinterpret_cast<Address>(addr));
      wasm_runtime->Trace("%c%zu:f32:%f ", kind, index, static_cast<double>(f));
    } break;
    case kF64:
      wasm_runtime->Trace(
          "%c%zu:f64:%f ", kind, index,
          base::ReadUnalignedValue<double>(reinterpret_cast<Address>(addr)));
      break;
    case kS128: {
      // This defaults to tracing all S128 values as i32x4 values for now,
      // when there is more state to know what type of values are on the
      // stack, the right format should be printed here.
      int32x4 s;
      s.val[0] =
          base::ReadUnalignedValue<uint32_t>(reinterpret_cast<Address>(addr));
      s.val[1] = base::ReadUnalignedValue<uint32_t>(
          reinterpret_cast<Address>(addr + 4));
      s.val[2] = base::ReadUnalignedValue<uint32_t>(
          reinterpret_cast<Address>(addr + 8));
      s.val[3] = base::ReadUnalignedValue<uint32_t>(
          reinterpret_cast<Address>(addr + 12));
      wasm_runtime->Trace("%c%zu:s128:%08x,%08x,%08x,%08x ", kind, index,
                          s.val[0], s.val[1], s.val[2], s.val[3]);
      break;
    }
    case kRef:
    case kRefNull:
      DCHECK_EQ(sizeof(uint64_t), sizeof(WasmRef));
      // TODO(paolosev@microsoft.com): Extract actual ref value from the
      // thread's reference_stack_.
      wasm_runtime->Trace(
          "%c%zu:ref:%" PRIx64, kind, index,
          base::ReadUnalignedValue<uint64_t>(reinterpret_cast<Address>(addr)));
      break;
    default:
      UNREACHABLE();
  }
}

char const* kInstructionHandlerNames[kInstructionTableSize];

#endif  // V8_ENABLE_DRUMBRAKE_TRACING

PWasmOp* kInstructionTable[kInstructionTableSize] = {

// 1. Add "small" (compressed) instruction handlers.

#if !V8_DRUMBRAKE_BOUNDS_CHECKS
// For this case, this table will be initialized in
// InitInstructionTableOnce.
#define V(_) nullptr,
    FOREACH_LOAD_STORE_INSTR_HANDLER(V)
#undef V

#else  // !V8_DRUMBRAKE_BOUNDS_CHECKS
#define V(name) Handlers<true>::name,
    FOREACH_LOAD_STORE_INSTR_HANDLER(V)
        FOREACH_LOAD_STORE_DUPLICATED_INSTR_HANDLER(V)
#undef V

#endif  // !V8_DRUMBRAKE_BOUNDS_CHECKS

#define V(name) Handlers<true>::name,
        FOREACH_NO_BOUNDSCHECK_INSTR_HANDLER(V)
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
            FOREACH_TRACE_INSTR_HANDLER(V)
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
#undef V

// 2. Add "large" instruction handlers.

#if !V8_DRUMBRAKE_BOUNDS_CHECKS
// For this case, this table will be initialized in
// InitInstructionTableOnce.
#define V(_) nullptr,
                FOREACH_LOAD_STORE_INSTR_HANDLER(V)
#undef V

#else  // !V8_DRUMBRAKE_BOUNDS_CHECKS
#define V(name) Handlers<false>::name,
                FOREACH_LOAD_STORE_INSTR_HANDLER(V)
                    FOREACH_LOAD_STORE_DUPLICATED_INSTR_HANDLER(V)
#undef V

#endif  // !V8_DRUMBRAKE_BOUNDS_CHECKS

#define V(name) Handlers<false>::name,
                    FOREACH_NO_BOUNDSCHECK_INSTR_HANDLER(V)
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
                        FOREACH_TRACE_INSTR_HANDLER(V)
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
#undef V
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

const WasmEHData::TryBlock* WasmEHData::GetTryBlock(
    CodeOffset code_offset) const {
  const auto& catch_it = code_trycatch_map_.find(code_offset);
  if (catch_it == code_trycatch_map_.end()) return nullptr;
  BlockIndex try_block_index = catch_it->second;

  const auto& try_it = try_blocks_.find(try_block_index);
  DCHECK_NE(try_it, try_blocks_.end());
  const WasmEHData::TryBlock* try_block = &try_it->second;
  if (try_block->IsTryDelegate()) {
    try_block = GetDelegateTryBlock(try_block);
  }
  return try_block;
}

const WasmEHData::TryBlock* WasmEHData::GetParentTryBlock(
    const WasmEHData::TryBlock* try_block) const {
  const auto& try_it =
      try_blocks_.find(try_block->parent_or_matching_try_block);
  return try_it != try_blocks_.end() ? &try_it->second : nullptr;
}

const WasmEHData::TryBlock* WasmEHData::GetDelegateTryBlock(
    const WasmEHData::TryBlock* try_block) const {
  DCHECK_GE(try_block->delegate_try_index, 0);
  if (try_block->delegate_try_index == WasmEHData::kDelegateToCallerIndex) {
    return nullptr;
  }
  const auto& try_it = try_blocks_.find(try_block->delegate_try_index);
  DCHECK_NE(try_it, try_blocks_.end());
  return &try_it->second;
}

size_t WasmEHData::GetEndInstructionOffsetFor(
    WasmEHData::BlockIndex catch_block_index) const {
  int try_block_index = GetTryBranchOf(catch_block_index);
  DCHECK_GE(try_block_index, 0);

  const auto& it = try_blocks_.find(try_block_index);
  DCHECK_NE(it, try_blocks_.end());
  return it->second.end_instruction_code_offset;
}

WasmEHData::ExceptionPayloadSlotOffsets
WasmEHData::GetExceptionPayloadStartSlotOffsets(
    WasmEHData::BlockIndex catch_block_index) const {
  const auto& it = catch_blocks_.find(catch_block_index);
  DCHECK_NE(it, catch_blocks_.end());
  return {it->second.first_param_slot_offset,
          it->second.first_param_ref_stack_index};
}

WasmEHData::BlockIndex WasmEHData::GetTryBranchOf(
    WasmEHData::BlockIndex catch_block_index) const {
  const auto& it = catch_blocks_.find(catch_block_index);
  if (it == catch_blocks_.end()) return -1;
  return it->second.try_block_index;
}

void WasmEHDataGenerator::AddTryBlock(
    BlockIndex try_block_index, BlockIndex parent_or_matching_try_block_index,
    BlockIndex ancestor_try_block_index) {
  DCHECK_EQ(try_blocks_.find(try_block_index), try_blocks_.end());
  try_blocks_.insert(
      {try_block_index,
       TryBlock{parent_or_matching_try_block_index, ancestor_try_block_index}});
  current_try_block_index_ = try_block_index;
}

void WasmEHDataGenerator::AddCatchBlock(BlockIndex catch_block_index,
                                        int tag_index,
                                        uint32_t first_param_slot_offset,
                                        uint32_t first_param_ref_stack_index,
                                        CodeOffset code_offset) {
  DCHECK_EQ(catch_blocks_.find(catch_block_index), catch_blocks_.end());
  catch_blocks_.insert(
      {catch_block_index,
       CatchBlock{current_try_block_index_, first_param_slot_offset,
                  first_param_ref_stack_index}});

  auto it = try_blocks_.find(current_try_block_index_);
  DCHECK_NE(it, try_blocks_.end());
  it->second.catch_handlers.emplace_back(
      CatchHandler{catch_block_index, tag_index, code_offset});
}

void WasmEHDataGenerator::AddDelegatedBlock(
    BlockIndex delegate_try_block_index) {
  auto it = try_blocks_.find(current_try_block_index_);
  DCHECK_NE(it, try_blocks_.end());
  TryBlock& try_block = it->second;
  DCHECK(try_block.catch_handlers.empty());
  try_block.SetDelegated(delegate_try_block_index);
}

WasmEHData::BlockIndex WasmEHDataGenerator::EndTryCatchBlocks(
    WasmEHData::BlockIndex block_index, CodeOffset code_offset) {
  WasmEHData::BlockIndex try_block_index = GetTryBranchOf(block_index);
  if (try_block_index < 0) {
    // No catch/catch_all blocks.
    try_block_index = block_index;
  }

  const auto& try_it = try_blocks_.find(try_block_index);
  DCHECK_NE(try_it, try_blocks_.end());
  try_it->second.end_instruction_code_offset = code_offset;
  current_try_block_index_ = try_it->second.parent_or_matching_try_block;
  return try_block_index;
}

void WasmEHDataGenerator::RecordPotentialExceptionThrowingInstruction(
    WasmOpcode opcode, CodeOffset code_offset) {
  if (current_try_block_index_ < 0) {
    return;  // Not inside a try block.
  }

  BlockIndex try_block_index = current_try_block_index_;
  const auto& try_it = try_blocks_.find(current_try_block_index_);
  DCHECK_NE(try_it, try_blocks_.end());
  const TryBlock& try_block = try_it->second;

  bool inside_catch_handler = !try_block.catch_handlers.empty();
  if (inside_catch_handler) {
    // If we are throwing from inside a catch block, the exception should only
    // be caught by the catch handler of an ancestor try block.
    try_block_index = try_block.ancestor_try_index;
    if (try_block_index < 0) return;
  }

  code_trycatch_map_[code_offset] = try_block_index;
}

WasmBytecode::WasmBytecode(int func_index, const uint8_t* code_data,
                           size_t code_length, uint32_t stack_frame_size,
                           const FunctionSig* signature,
                           const InterpreterCode* interpreter_code,
                           size_t blocks_count, const uint8_t* const_slots_data,
                           size_t const_slots_length, uint32_t ref_slots_count,
                           const WasmEHData&& eh_data,
                           const std::map<CodeOffset, pc_t>&& code_pc_map)
    : code_(code_data, code_data + code_length),
      code_bytes_(code_.data()),
      signature_(signature),
      interpreter_code_(interpreter_code),
      const_slots_values_(const_slots_data,
                          const_slots_data + const_slots_length),
      func_index_(func_index),
      blocks_count_(static_cast<uint32_t>(blocks_count)),
      args_count_(static_cast<uint32_t>(signature_->parameter_count())),
      args_slots_size_(ArgsSizeInSlots(signature_)),
      return_count_(static_cast<uint32_t>(signature_->return_count())),
      rets_slots_size_(RetsSizeInSlots(signature_)),
      locals_count_(
          static_cast<uint32_t>(interpreter_code_->locals.num_locals)),
      locals_slots_size_(LocalsSizeInSlots(interpreter_code_)),
      total_frame_size_in_bytes_(stack_frame_size * kSlotSize +
                                 args_slots_size_ * kSlotSize +
                                 rets_slots_size_ * kSlotSize),
      ref_args_count_(RefArgsCount(signature_)),
      ref_rets_count_(RefRetsCount(signature_)),
      ref_locals_count_(RefLocalsCount(interpreter_code)),
      ref_slots_count_(ref_slots_count),
      eh_data_(eh_data),
      code_pc_map_(code_pc_map) {}

pc_t WasmBytecode::GetPcFromTrapCode(const uint8_t* current_code) const {
  DCHECK_GE(current_code, code_bytes_);
  size_t code_offset = current_code - code_bytes_;

  auto it = code_pc_map_.lower_bound(code_offset);
  if (it == code_pc_map_.begin()) return 0;
  it--;

  return it->second;
}

// static
size_t WasmBytecodeGenerator::total_bytecode_size_ = 0;
// static
size_t WasmBytecodeGenerator::emitted_short_slot_offset_count_ = 0;
// static
size_t WasmBytecodeGenerator::emitted_short_memory_offset_count_ = 0;

WasmBytecodeGenerator::WasmBytecodeGenerator(uint32_t function_index,
                                             InterpreterCode* wasm_code,
                                             const WasmModule* module)
    : const_slot_offset_(0),
      slot_offset_(0),
      ref_slots_count_(0),
      function_index_(function_index),
      wasm_code_(wasm_code),
      args_count_(0),
      args_slots_size_(0),
      return_count_(0),
      rets_slots_size_(0),
      locals_count_(0),
      current_block_index_(-1),
      is_instruction_reachable_(true),
      unreachable_block_count_(0),
#ifdef DEBUG
      was_current_instruction_reachable_(true),
#endif  // DEBUG
      module_(module),
      last_instr_offset_(kInvalidCodeOffset),
      handler_size_(InstrHandlerSize::Large),
      current_instr_encoding_failed_(false)
#ifdef DEBUG
      ,
      no_nested_emit_instr_handler_guard_(false)
#endif  // DEBUG
{
  DCHECK(v8_flags.wasm_jitless);

  // Multiple memories not supported.
  DCHECK_LE(module->memories.size(), 1);

  size_t wasm_code_size = wasm_code_->end - wasm_code_->start;
  code_.reserve(wasm_code_size * 6);
  slots_.reserve(wasm_code_size / 2);
  stack_.reserve(wasm_code_size / 4);
  blocks_.reserve(wasm_code_size / 8);

  const FunctionSig* sig = module_->functions[function_index].sig;
  args_count_ = static_cast<uint32_t>(sig->parameter_count());
  args_slots_size_ = WasmBytecode::ArgsSizeInSlots(sig);
  return_count_ = static_cast<uint32_t>(sig->return_count());
  rets_slots_size_ = WasmBytecode::RetsSizeInSlots(sig);
  locals_count_ = static_cast<uint32_t>(wasm_code->locals.num_locals);

  is_memory64_ = IsMemory64();
  if (is_memory64_) {
    int_mem_push_ = &WasmBytecodeGenerator::I64Push;
    int_mem_pop_ = &WasmBytecodeGenerator::I64Pop;
  } else {
    int_mem_push_ = &WasmBytecodeGenerator::I32Push;
    int_mem_pop_ = &WasmBytecodeGenerator::I32Pop;
  }
}

size_t WasmBytecodeGenerator::Simd128Hash::operator()(
    const Simd128& s128) const {
  static_assert(sizeof(size_t) == sizeof(uint64_t));
  const int64x2 s = s128.to_i64x2();
  return s.val[0] ^ s.val[1];
}

// Look if the slot that hold the value at {stack_index} is being shared with
// other slots. This can happen if there are multiple load.get operations that
// copy from the same local.
bool WasmBytecodeGenerator::HasSharedSlot(uint32_t stack_index) const {
  // Only consider stack entries added in the current block.
  // We don't need to consider ancestor blocks because if a block has a
  // non-empty signature we always pass arguments and results into separate
  // slots, emitting CopySlot operations.
  uint32_t start_slot_index = blocks_[current_block_index_].stack_size_;

  for (uint32_t i = start_slot_index; i < stack_.size(); i++) {
    if (stack_[i] == stack_[stack_index]) {
      return true;
    }
  }
  return false;
}

// Look if the slot that hold the value at {stack_index} is being shared with
// other slots. This can happen if there are multiple load.get operations that
// copy from the same local. In this case when we modify the value of the slot
// with a local.set or local.tee we need to first duplicate the slot to make
// sure that the old value is preserved in the other shared slots.
bool WasmBytecodeGenerator::FindSharedSlot(uint32_t stack_index,
                                           uint32_t* new_slot_index) {
  *new_slot_index = UINT_MAX;
  ValueType value_type = slots_[stack_[stack_index]].value_type;
  if (value_type.is_reference()) return false;

  // Only consider stack entries added in the current block.
  // We don't need to consider ancestor blocks because if a block has a
  // non-empty signature we always pass arguments and results into separate
  // slots, emitting CopySlot operations.
  uint32_t start_slot_index = blocks_[current_block_index_].stack_size_;

  for (uint32_t i = start_slot_index; i < stack_.size(); i++) {
    if (stack_[i] == stack_[stack_index]) {
      // Allocate new slot to preserve the old value of a shared slot.
      *new_slot_index = CreateSlot(value_type);
      break;
    }
  }

  if (*new_slot_index == UINT_MAX) return false;

  // If there was a collision and we allocated a new slot to preserve the old
  // value, we need to do two things to keep the state up to date:
  // 1. For each shared slot, we update the stack value to refer to the new
  // slot. This track the change at bytecode generation time.
  // 2. We return {true} to indicate that the slot was shared and the caller
  // should emit a 's2s_PreserveCopySlot...' instruction to copy the old slot
  // value into the new slot, at runtime.

  // This loop works because stack_index is always greater or equal to the index
  // of args/globals.
  DCHECK_GT(start_slot_index, stack_index);
  for (uint32_t i = start_slot_index; i < stack_.size(); i++) {
    if (stack_[i] == stack_[stack_index]) {
      // Copy value into the new slot.
      UpdateStack(i, *new_slot_index);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
      if (v8_flags.trace_drumbrake_execution &&
          v8_flags.trace_drumbrake_execution_verbose) {
        START_EMIT_INSTR_HANDLER_WITH_ID(trace_UpdateStack) {
          EmitStackIndex(i);
          EmitSlotOffset(slots_[*new_slot_index].slot_offset * kSlotSize);
          printf("Preserve UpdateStack: [%d] = %d\n", i,
                 slots_[*new_slot_index].slot_offset);
        }
        END_EMIT_INSTR_HANDLER()
      }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
    }
  }

  return true;
}

void WasmBytecodeGenerator::EmitCopySlot(ValueType value_type,
                                         uint32_t from_slot_index,
                                         uint32_t to_slot_index,
                                         bool copy_from_reg) {
  START_EMIT_INSTR_HANDLER() {
    const ValueKind kind = value_type.kind();
    switch (kind) {
      case kI32:
        if (copy_from_reg) {
          EMIT_INSTR_HANDLER(r2s_CopyR0ToSlot32);
        } else {
          EMIT_INSTR_HANDLER(s2s_CopySlot32);
        }
        break;
      case kI64:
        if (copy_from_reg) {
          EMIT_INSTR_HANDLER(r2s_CopyR0ToSlot64);
        } else {
          EMIT_INSTR_HANDLER(s2s_CopySlot64);
        }
        break;
      case kF32:
        if (copy_from_reg) {
          EMIT_INSTR_HANDLER(r2s_CopyFp0ToSlot32);
        } else {
          EMIT_INSTR_HANDLER(s2s_CopySlot32);
        }
        break;
      case kF64:
        if (copy_from_reg) {
          EMIT_INSTR_HANDLER(r2s_CopyFp0ToSlot64);
        } else {
          EMIT_INSTR_HANDLER(s2s_CopySlot64);
        }
        break;
      case kS128:
        DCHECK(!copy_from_reg);
        EMIT_INSTR_HANDLER(s2s_CopySlot128);
        break;
      case kRef:
      case kRefNull:
        DCHECK(!copy_from_reg);
        EMIT_INSTR_HANDLER(s2s_CopySlotRef);
        break;
      default:
        UNREACHABLE();
    }

    if (kind == kRefNull || kind == kRef) {
      DCHECK(!copy_from_reg);
      EmitRefStackIndex(slots_[from_slot_index].ref_stack_index);
      EmitRefStackIndex(slots_[to_slot_index].ref_stack_index);
    } else {
      if (!copy_from_reg) {
        EmitSlotOffset(slots_[from_slot_index].slot_offset);
      }
      EmitSlotOffset(slots_[to_slot_index].slot_offset);
    }
  }
  END_EMIT_INSTR_HANDLER()

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  if (v8_flags.trace_drumbrake_bytecode_generator &&
      v8_flags.trace_drumbrake_execution_verbose) {
    printf("emit CopySlot: %d(%d) -> %d(%d)\n", from_slot_index,
           slots_[from_slot_index].slot_offset, to_slot_index,
           slots_[to_slot_index].slot_offset);
  }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
}

// When a Wasm function starts the values for the function args and locals are
// already present in the Wasm stack. The stack entries for args and locals
// can be directly accessed with {local.get} and modified with {local.set} and
// {local.tee}, but they can never be popped, they are always present until
// the function returns.
// During the execution of the function other values are then pushed/popped
// into/from the stack, but these other entries are only accessible indirectly
// as operands/results of operations, not directly with local.get/set
// instructions.
//
// DrumBrake implements a "args/locals propagation" optimization that allows the
// stack slots for "local" stack entries to be shared with other stack entries
// (using the {stack_} and {slots_} arrays), in order to avoid emitting calls to
// 'local.get' instruction handlers.

// When an arg/local value is modified, and its slot is shared with other
// entries in the stack, we need to preserve the old value of the stack entry in
// a new slot.
void WasmBytecodeGenerator::CopyToSlot(ValueType value_type,
                                       uint32_t from_slot_index,
                                       uint32_t to_stack_index,
                                       bool copy_from_reg) {
  const ValueKind kind = value_type.kind();
  uint32_t to_slot_index = stack_[to_stack_index];
  DCHECK(copy_from_reg || CheckEqualKind(kind, slots_[from_slot_index].kind()));
  DCHECK(CheckEqualKind(slots_[to_slot_index].kind(), kind));

  uint32_t new_slot_index;
  // If the slot is shared {FindSharedSlot} creates a new slot and makes all the
  // 'non-locals' stack entries that shared the old slot point to this new slot.
  // We need to emit a {PreserveCopySlot} instruction to dynamically copy the
  // old value into the new slot.
  if (FindSharedSlot(to_stack_index, &new_slot_index)) {
    START_EMIT_INSTR_HANDLER() {
      switch (kind) {
        case kI32:
          if (copy_from_reg) {
            EMIT_INSTR_HANDLER(r2s_PreserveCopyR0ToSlot32);
          } else {
            EMIT_INSTR_HANDLER(s2s_PreserveCopySlot32);
          }
          break;
        case kI64:
          if (copy_from_reg) {
            EMIT_INSTR_HANDLER(r2s_PreserveCopyR0ToSlot64);
          } else {
            EMIT_INSTR_HANDLER(s2s_PreserveCopySlot64);
          }
          break;
        case kF32:
          if (copy_from_reg) {
            EMIT_INSTR_HANDLER(r2s_PreserveCopyFp0ToSlot32);
          } else {
            EMIT_INSTR_HANDLER(s2s_PreserveCopySlot32);
          }
          break;
        case kF64:
          if (copy_from_reg) {
            EMIT_INSTR_HANDLER(r2s_PreserveCopyFp0ToSlot64);
          } else {
            EMIT_INSTR_HANDLER(s2s_PreserveCopySlot64);
          }
          break;
        case kS128:
          DCHECK(!copy_from_reg);
          EMIT_INSTR_HANDLER(s2s_PreserveCopySlot128);
          break;
        case kRef:
        case kRefNull:
        default:
          UNREACHABLE();
      }

      if (kind == kRefNull || kind == kRef) {
        DCHECK(!copy_from_reg);
        EmitRefStackIndex(slots_[from_slot_index].ref_stack_index);
        EmitRefStackIndex(slots_[to_slot_index].ref_stack_index);
        EmitRefStackIndex(slots_[new_slot_index].ref_stack_index);
      } else {
        if (!copy_from_reg) {
          EmitSlotOffset(slots_[from_slot_index].slot_offset);
        }
        EmitSlotOffset(slots_[to_slot_index].slot_offset);
        EmitSlotOffset(slots_[new_slot_index].slot_offset);
      }

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
      if (v8_flags.trace_drumbrake_execution &&
          v8_flags.trace_drumbrake_execution_verbose) {
        printf("emit s2s_PreserveCopySlot: %d %d %d\n",
               slots_[from_slot_index].slot_offset,
               slots_[to_slot_index].slot_offset,
               slots_[new_slot_index].slot_offset);
      }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
    }
    END_EMIT_INSTR_HANDLER()
  } else {
    EmitCopySlot(value_type, from_slot_index, to_slot_index, copy_from_reg);
  }
}

// Used for 'local.tee' and 'local.set' instructions.
void WasmBytecodeGenerator::CopyToSlotAndPop(ValueType value_type,
                                             uint32_t to_stack_index,
                                             bool is_tee, bool copy_from_reg) {
  DCHECK(!stack_.empty());
  DCHECK_LT(to_stack_index, stack_.size() - (copy_from_reg ? 0 : 1));

  // LocalGet uses a "copy-on-write" mechanism: the arg/local value is not
  // copied and instead the stack entry references the same slot. When the
  // arg/local value is modified, we need to preserve the old value of the stack
  // entry in a new slot.
  CopyToSlot(value_type, stack_.back(), to_stack_index, copy_from_reg);

  if (!is_tee && !copy_from_reg) {
    PopSlot();

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      START_EMIT_INSTR_HANDLER_WITH_ID(trace_PopSlot) {}
      END_EMIT_INSTR_HANDLER()
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
  }
}

// This function is called when we enter a new 'block', 'loop' or 'if' block
// statement. Checks whether any of the 'non-locals' stack entries share a slot
// with an arg/local stack entry. In that case stop make sure the local stack
// entry will get its own slot. This is necessary because at runtime we could
// jump at the block after having modified the local value in some other code
// path.
// TODO(paolosev@microsoft.com) - Understand why this is not required only for
// 'loop' blocks.
void WasmBytecodeGenerator::PreserveArgsAndLocals() {
  uint32_t num_args_and_locals = args_count_ + locals_count_;

  // If there are only args/locals entries in the stack, nothing to do.
  if (num_args_and_locals >= stack_size()) return;

  for (uint32_t local_index = 0; local_index < num_args_and_locals;
       ++local_index) {
    uint32_t new_slot_index;
    if (FindSharedSlot(local_index, &new_slot_index)) {
      ValueType value_type = slots_[stack_[local_index]].value_type;
      EmitCopySlot(value_type, stack_[local_index], new_slot_index);
    }
  }
}

uint32_t WasmBytecodeGenerator::ReserveBlockSlots(
    uint8_t opcode, const WasmInstruction::Optional::Block& block_data,
    size_t* rets_slots_count, size_t* params_slots_count) {
  uint32_t first_slot_index = 0;
  *rets_slots_count = 0;
  *params_slots_count = 0;
  bool first_slot_found = false;
  const ValueType value_type = block_data.value_type();
  if (value_type == kWasmBottom) {
    const FunctionSig* sig = module_->signature(block_data.sig_index);
    *rets_slots_count = sig->return_count();
    for (uint32_t i = 0; i < *rets_slots_count; i++) {
      uint32_t slot_index = CreateSlot(sig->GetReturn(i));
      if (!first_slot_found) {
        first_slot_index = slot_index;
        first_slot_found = true;
      }
    }
    *params_slots_count = sig->parameter_count();
    for (uint32_t i = 0; i < *params_slots_count; i++) {
      uint32_t slot_index = CreateSlot(sig->GetParam(i));
      if (!first_slot_found) {
        first_slot_index = slot_index;
        first_slot_found = true;
      }
    }
  } else if (value_type != kWasmVoid) {
    *rets_slots_count = 1;
    first_slot_index = CreateSlot(value_type);
  }
  return first_slot_index;
}

void WasmBytecodeGenerator::StoreBlockParamsIntoSlots(
    uint32_t target_block_index, bool update_stack) {
  const WasmBytecodeGenerator::BlockData& target_block_data =
      blocks_[target_block_index];
  DCHECK_EQ(target_block_data.opcode_, kExprLoop);

  uint32_t params_count = ParamsCount(target_block_data);
  uint32_t rets_count = ReturnsCount(target_block_data);
  uint32_t first_param_slot_index =
      target_block_data.first_block_index_ + rets_count;
  for (uint32_t i = 0; i < params_count; i++) {
    uint32_t from_slot_index =
        stack_[stack_top_index() - (params_count - 1) + i];
    uint32_t to_slot_index = first_param_slot_index + i;
    if (from_slot_index != to_slot_index) {
      EmitCopySlot(GetParamType(target_block_data, i), from_slot_index,
                   to_slot_index);
      if (update_stack) {
        DCHECK_EQ(GetParamType(target_block_data, i),
                  slots_[first_param_slot_index + i].value_type);
        DCHECK_EQ(GetParamType(target_block_data, i),
                  slots_[first_param_slot_index + i].value_type);
        UpdateStack(stack_top_index() - (params_count - 1) + i,
                    first_param_slot_index + i);

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
        if (v8_flags.trace_drumbrake_execution) {
          START_EMIT_INSTR_HANDLER_WITH_ID(trace_UpdateStack) {
            EmitStackIndex(stack_top_index() - (params_count - 1) + i);
            EmitSlotOffset(slots_[first_param_slot_index + i].slot_offset *
                           kSlotSize);
          }
          END_EMIT_INSTR_HANDLER()
        }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
      }
    }
  }
}

void WasmBytecodeGenerator::StoreBlockParamsAndResultsIntoSlots(
    uint32_t target_block_index, WasmOpcode opcode) {
  bool is_branch = kExprBr == opcode || kExprBrIf == opcode ||
                   kExprBrTable == opcode || kExprBrOnNull == opcode ||
                   kExprBrOnNonNull == opcode || kExprBrOnCast == opcode;
  const WasmBytecodeGenerator::BlockData& target_block_data =
      blocks_[target_block_index];
  bool is_target_loop_block = target_block_data.opcode_ == kExprLoop;
  if (is_target_loop_block && is_branch) {
    StoreBlockParamsIntoSlots(target_block_index, false);
  }

  // Ignore params if this is the function main block.
  uint32_t params_count =
      target_block_index == 0 ? 0 : ParamsCount(target_block_data);
  uint32_t rets_count = ReturnsCount(target_block_data);

  // There could be valid code where there are not enough elements in the
  // stack if some code in unreachable (for example if a 'i32.const 0' is
  // followed by a 'br_if' the if branch is never reachable).
  uint32_t count = std::min(static_cast<uint32_t>(stack_.size()), rets_count);
  for (uint32_t i = 0; i < count; i++) {
    uint32_t from_slot_index = stack_[stack_top_index() - (count - 1) + i];
    uint32_t to_slot_index = target_block_data.first_block_index_ + i;
    if (from_slot_index != to_slot_index) {
      EmitCopySlot(GetReturnType(target_block_data, i), from_slot_index,
                   to_slot_index);
    }
  }

  bool is_else = (kExprElse == opcode);
  bool is_return = (kExprReturn == opcode);
  bool is_catch = (kExprCatch == opcode || kExprCatchAll == opcode);
  if (!is_branch && !is_return && !is_else && !is_catch) {
    uint32_t new_stack_height =
        target_block_data.stack_size_ - params_count + rets_count;
    DCHECK(new_stack_height <= stack_.size() ||
           !was_current_instruction_reachable_);
    stack_.resize(new_stack_height);

    for (uint32_t i = 0; i < rets_count; i++) {
      DCHECK_EQ(GetReturnType(target_block_data, i),
                slots_[target_block_data.first_block_index_ + i].value_type);
      DCHECK_EQ(GetReturnType(target_block_data, i),
                slots_[target_block_data.first_block_index_ + i].value_type);
      UpdateStack(target_block_data.stack_size_ - params_count + i,
                  target_block_data.first_block_index_ + i);

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
      if (v8_flags.trace_drumbrake_execution) {
        START_EMIT_INSTR_HANDLER_WITH_ID(trace_UpdateStack) {
          EmitStackIndex(target_block_data.stack_size_ - params_count + i);
          EmitSlotOffset(
              slots_[target_block_data.first_block_index_ + i].slot_offset *
              kSlotSize);
        }
        END_EMIT_INSTR_HANDLER()
      }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
    }
  }
}

void WasmBytecodeGenerator::RestoreIfElseParams(uint32_t if_block_index) {
  const WasmBytecodeGenerator::BlockData& if_block_data =
      blocks_[if_block_index];
  DCHECK_EQ(if_block_data.opcode_, kExprIf);

  stack_.resize(blocks_[if_block_index].stack_size_);
  uint32_t params_count = if_block_index == 0 ? 0 : ParamsCount(if_block_data);
  for (uint32_t i = 0; i < params_count; i++) {
    UpdateStack(if_block_data.stack_size_ - params_count + i,
                if_block_data.GetParam(i), GetParamType(if_block_data, i));
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
    if (v8_flags.trace_drumbrake_execution) {
      START_EMIT_INSTR_HANDLER_WITH_ID(trace_UpdateStack) {
        EmitStackIndex(if_block_data.stack_size_ - params_count + i);
        EmitSlotOffset(slots_[if_block_data.GetParam(i)].slot_offset *
                       kSlotSize);
      }
      END_EMIT_INSTR_HANDLER()
    }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
  }
}

uint32_t WasmBytecodeGenerator::ScanConstInstructions() const {
  Decoder decoder(wasm_code_->start, wasm_code_->end);
  uint32_t const_slots_size = 0;
  pc_t pc = wasm_code_->locals.encoded_size;
  pc_t limit = wasm_code_->end - wasm_code_->start;
  while (pc < limit) {
    uint32_t opcode = wasm_code_->start[pc];
    if (opcode == kExprI32Const || opcode == kExprF32Const) {
      const_slots_size += sizeof(uint32_t) / kSlotSize;
    } else if (opcode == kExprI64Const || opcode == kExprF64Const) {
      const_slots_size += sizeof(uint64_t) / kSlotSize;
    } else if (opcode == kSimdPrefix) {
      auto [opcode_index, opcode_len] =
          decoder.read_u32v<Decoder::FullValidationTag>(
              wasm_code_->start + pc + 1, "prefixed opcode index");
      opcode = (kSimdPrefix << 8) | opcode_index;
      if (opcode == kExprS128Const || opcode == kExprI8x16Shuffle) {
        const_slots_size += sizeof(Simd128) / kSlotSize;
      }
    }
    pc++;
  }
  return const_slots_size;
}

int32_t WasmBytecodeGenerator::EndBlock(WasmOpcode opcode) {
  WasmBytecodeGenerator::BlockData& block_data = blocks_[current_block_index_];
  bool is_try_catch =
      block_data.IsTry() || block_data.IsCatch() || block_data.IsCatchAll();

  StoreBlockParamsAndResultsIntoSlots(current_block_index_, opcode);

  block_data.end_code_offset_ = CurrentCodePos();
  if (opcode == kExprEnd && block_data.IsElse()) {
    DCHECK_GT(block_data.if_else_block_index_, 0);
    blocks_[block_data.if_else_block_index_].end_code_offset_ =
        CurrentCodePos();
  }

  if (!is_try_catch) {
    current_block_index_ = blocks_[current_block_index_].parent_block_index_;
  }

  if (is_try_catch && (opcode == kExprEnd || opcode == kExprDelegate)) {
    int32_t try_block_index =
        eh_data_.EndTryCatchBlocks(current_block_index_, CurrentCodePos());
    DCHECK_GE(try_block_index, 0);
    current_block_index_ = blocks_[try_block_index].parent_block_index_;
  }

  last_instr_offset_ = kInvalidCodeOffset;

  return current_block_index_;
}

void WasmBytecodeGenerator::Return() {
  if (current_block_index_ >= 0) {
    StoreBlockParamsAndResultsIntoSlots(0, kExprReturn);
  }

  START_EMIT_INSTR_HANDLER_WITH_ID(s2s_Return) {
    const WasmBytecodeGenerator::BlockData& target_block_data = blocks_[0];
    uint32_t final_stack_size =
        target_block_data.stack_size_ + ReturnsCount(target_block_data);
    EmitStackIndex(final_stack_size);
  }
  END_EMIT_INSTR_HANDLER()
}

WasmInstruction WasmBytecodeGenerator::DecodeInstruction(pc_t pc,
                                                         Decoder& decoder) {
  pc_t limit = wasm_code_->end - wasm_code_->start;
  if (pc >= limit) return WasmInstruction();

  int len = 1;
  uint8_t orig = wasm_code_->start[pc];
  WasmOpcode opcode = static_cast<WasmOpcode>(orig);
  if (WasmOpcodes::IsPrefixOpcode(opcode)) {
    uint32_t prefixed_opcode_length;
    std::tie(opcode, prefixed_opcode_length) =
        decoder.read_prefixed_opcode<Decoder::NoValidationTag>(
            wasm_code_->at(pc));
    // skip breakpoint by switching on original code.
    len = prefixed_opcode_length;
  }

  WasmInstruction::Optional optional;
  switch (orig) {
    case kExprUnreachable:
      break;
    case kExprNop:
      break;
    case kExprBlock:
    case kExprLoop:
    case kExprIf:
    case kExprTry: {
      BlockTypeImmediate imm(WasmEnabledFeatures::All(), &decoder,
                             wasm_code_->at(pc + 1), Decoder::kNoValidation);
      if (imm.sig_index.valid()) {
        // The block has at least one argument or at least two results, its
        // signature is identified by sig_index.
        optional.block.sig_index = imm.sig_index;
        optional.block.value_type_bitfield = kWasmBottom.raw_bit_field();
      } else if (imm.sig.return_count() + imm.sig.parameter_count() == 0) {
        // Void signature: no arguments and no results.
        optional.block.sig_index = ModuleTypeIndex::Invalid();
        optional.block.value_type_bitfield = kWasmVoid.raw_bit_field();
      } else {
        // No arguments and one result.
        optional.block.sig_index = ModuleTypeIndex::Invalid();
        std::optional<wasm::ValueType> wasm_return_type =
            GetWasmReturnTypeFromSignature(&imm.sig);
        DCHECK(wasm_return_type.has_value());
        optional.block.value_type_bitfield =
            wasm_return_type.value().raw_bit_field();
      }
      len = 1 + imm.length;
      break;
    }
    case kExprElse:
      break;
    case kExprCatch: {
      TagIndexImmediate imm(&decoder, wasm_code_->at(pc + 1),
                            Decoder::kNoValidation);
      optional.index = imm.index;
      len = 1 + imm.length;
      break;
    }
    case kExprCatchAll:
      break;
    case kExprEnd:
      break;
    case kExprThrow: {
      TagIndexImmediate imm(&decoder, wasm_code_->at(pc + 1),
                            Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.index = imm.index;
      break;
    }
    case kExprRethrow:
    case kExprBr:
    case kExprBrIf:
    case kExprBrOnNull:
    case kExprBrOnNonNull:
    case kExprDelegate: {
      BranchDepthImmediate imm(&decoder, wasm_code_->at(pc + 1),
                               Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.depth = imm.depth;
      break;
    }
    case kExprBrTable: {
      BranchTableImmediate imm(&decoder, wasm_code_->at(pc + 1),
                               Decoder::kNoValidation);
      BranchTableIterator<Decoder::NoValidationTag> iterator(&decoder, imm);
      optional.br_table.table_count = imm.table_count;
      optional.br_table.labels_index =
          static_cast<uint32_t>(br_table_labels_.size());
      for (uint32_t i = 0; i <= imm.table_count; i++) {
        DCHECK(iterator.has_next());
        br_table_labels_.emplace_back(iterator.next());
      }
      len = static_cast<int>(1 + iterator.pc() - imm.start);
      break;
    }
    case kExprReturn:
      break;
    case kExprCallFunction:
    case kExprReturnCall: {
      CallFunctionImmediate imm(&decoder, wasm_code_->at(pc + 1),
                                Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.index = imm.index;
      break;
    }
    case kExprCallIndirect:
    case kExprReturnCallIndirect: {
      CallIndirectImmediate imm(&decoder, wasm_code_->at(pc + 1),
                                Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.indirect_call.table_index = imm.table_imm.index;
      optional.indirect_call.sig_index = imm.sig_imm.index.index;
      break;
    }
    case kExprDrop:
      break;
    case kExprSelect:
      break;
    case kExprSelectWithType: {
      SelectTypeImmediate imm(WasmEnabledFeatures::All(), &decoder,
                              wasm_code_->at(pc + 1), Decoder::kNoValidation);
      len = 1 + imm.length;
      break;
    }
    case kExprLocalGet: {
      IndexImmediate imm(&decoder, wasm_code_->at(pc + 1), "local index",
                         Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.index = imm.index;
      break;
    }
    case kExprLocalSet: {
      IndexImmediate imm(&decoder, wasm_code_->at(pc + 1), "local index",
                         Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.index = imm.index;
      break;
    }
    case kExprLocalTee: {
      IndexImmediate imm(&decoder, wasm_code_->at(pc + 1), "local index",
                         Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.index = imm.index;
      break;
    }
    case kExprGlobalGet: {
      GlobalIndexImmediate imm(&decoder, wasm_code_->at(pc + 1),
                               Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.index = imm.index;
      break;
    }
    case kExprGlobalSet: {
      GlobalIndexImmediate imm(&decoder, wasm_code_->at(pc + 1),
                               Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.index = imm.index;
      break;
    }
    case kExprTableGet: {
      IndexImmediate imm(&decoder, wasm_code_->at(pc + 1), "table index",
                         Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.index = imm.index;
      break;
    }
    case kExprTableSet: {
      IndexImmediate imm(&decoder, wasm_code_->at(pc + 1), "table index",
                         Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.index = imm.index;
      break;
    }

#define LOAD_CASE(name, ctype, mtype, rep, type)                          \
  case kExpr##name: {                                                     \
    MemoryAccessImmediate imm(                                            \
        &decoder, wasm_code_->at(pc + 1), sizeof(ctype),                  \
        !module_->memories.empty() && module_->memories[0].is_memory64(), \
        Decoder::kNoValidation);                                          \
    len = 1 + imm.length;                                                 \
    optional.offset = imm.offset;                                         \
    break;                                                                \
  }
      LOAD_CASE(I32LoadMem8S, int32_t, int8_t, kWord8, I32);
      LOAD_CASE(I32LoadMem8U, int32_t, uint8_t, kWord8, I32);
      LOAD_CASE(I32LoadMem16S, int32_t, int16_t, kWord16, I32);
      LOAD_CASE(I32LoadMem16U, int32_t, uint16_t, kWord16, I32);
      LOAD_CASE(I64LoadMem8S, int64_t, int8_t, kWord8, I64);
      LOAD_CASE(I64LoadMem8U, int64_t, uint8_t, kWord16, I64);
      LOAD_CASE(I64LoadMem16S, int64_t, int16_t, kWord16, I64);
      LOAD_CASE(I64LoadMem16U, int64_t, uint16_t, kWord16, I64);
      LOAD_CASE(I64LoadMem32S, int64_t, int32_t, kWord32, I64);
      LOAD_CASE(I64LoadMem32U, int64_t, uint32_t, kWord32, I64);
      LOAD_CASE(I32LoadMem, int32_t, int32_t, kWord32, I32);
      LOAD_CASE(I64LoadMem, int64_t, int64_t, kWord64, I64);
      LOAD_CASE(F32LoadMem, Float32, uint32_t, kFloat32, F32);
      LOAD_CASE(F64LoadMem, Float64, uint64_t, kFloat64, F64);
#undef LOAD_CASE

#define STORE_CASE(name, ctype, mtype, rep, type)                         \
  case kExpr##name: {                                                     \
    MemoryAccessImmediate imm(                                            \
        &decoder, wasm_code_->at(pc + 1), sizeof(ctype),                  \
        !module_->memories.empty() && module_->memories[0].is_memory64(), \
        Decoder::kNoValidation);                                          \
    len = 1 + imm.length;                                                 \
    optional.offset = imm.offset;                                         \
    break;                                                                \
  }
      STORE_CASE(I32StoreMem8, int32_t, int8_t, kWord8, I32);
      STORE_CASE(I32StoreMem16, int32_t, int16_t, kWord16, I32);
      STORE_CASE(I64StoreMem8, int64_t, int8_t, kWord8, I64);
      STORE_CASE(I64StoreMem16, int64_t, int16_t, kWord16, I64);
      STORE_CASE(I64StoreMem32, int64_t, int32_t, kWord32, I64);
      STORE_CASE(I32StoreMem, int32_t, int32_t, kWord32, I32);
      STORE_CASE(I64StoreMem, int64_t, int64_t, kWord64, I64);
      STORE_CASE(F32StoreMem, Float32, uint32_t, kFloat32, F32);
      STORE_CASE(F64StoreMem, Float64, uint64_t, kFloat64, F64);
#undef STORE_CASE

    case kExprMemorySize: {
      MemoryIndexImmediate imm(&decoder, wasm_code_->at(pc + 1),
                               Decoder::kNoValidation);
      len = 1 + imm.length;
      break;
    }
    case kExprMemoryGrow: {
      MemoryIndexImmediate imm(&decoder, wasm_code_->at(pc + 1),
                               Decoder::kNoValidation);
      len = 1 + imm.length;
      break;
    }
    case kExprI32Const: {
      ImmI32Immediate imm(&decoder, wasm_code_->at(pc + 1),
                          Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.i32 = imm.value;
      break;
    }
    case kExprI64Const: {
      ImmI64Immediate imm(&decoder, wasm_code_->at(pc + 1),
                          Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.i64 = imm.value;
      break;
    }
    case kExprF32Const: {
      ImmF32Immediate imm(&decoder, wasm_code_->at(pc + 1),
                          Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.f32 = imm.value;
      break;
    }
    case kExprF64Const: {
      ImmF64Immediate imm(&decoder, wasm_code_->at(pc + 1),
                          Decoder::kNoValidation);
      len = 1 + imm.length;
      optional.f64 = imm.value;
      break;
    }

#define EXECUTE_BINOP(name, ctype, reg, op, type) \
  case kExpr##name:                               \
    break;

      FOREACH_COMPARISON_BINOP(EXECUTE_BINOP)
      FOREACH_ARITHMETIC_BINOP(EXECUTE_BINOP)
      FOREACH_TRAPPING_BINOP(EXECUTE_BINOP)
      FOREACH_MORE_BINOP(EXECUTE_BINOP)
#undef EXECUTE_BINOP

#define EXECUTE_UNOP(name, ctype, reg, op, type) \
  case kExpr##name:                              \
    break;

      FOREACH_SIMPLE_UNOP(EXECUTE_UNOP)
#undef EXECUTE_UNOP

#define EXECUTE_UNOP(name, from_ctype, from_type, from_reg, to_ctype, to_type, \
                     to_reg)                                                   \
  case kExpr##name:                                                            \
    break;

      FOREACH_ADDITIONAL_CONVERT_UNOP(EXECUTE_UNOP)
      FOREACH_CONVERT_UNOP(EXECUTE_UNOP)
      FOREACH_REINTERPRET_UNOP(EXECUTE_UNOP)
#undef EXECUTE_UNOP

#define EXECUTE_UNOP(name, from_ctype, from_type, to_ctype, to_type, op) \
  case kExpr##name:                                                      \
    break;

      FOREACH_BITS_UNOP(EXECUTE_UNOP)
#undef EXECUTE_UNOP

#define EXECUTE_UNOP(name, from_ctype, from_type, to_ctype, to_type) \
  case kExpr##name:                                                  \
    break;

      FOREACH_EXTENSION_UNOP(EXECUTE_UNOP)
#undef EXECUTE_UNOP

    case kExprRefNull: {
      HeapTypeImmediate imm(WasmEnabledFeatures::All(), &decoder,
                            wasm_code_->at(pc + 1), Decoder::kNoValidation);
      optional.ref_type = imm.type.representation();
      len = 1 + imm.length;
      break;
    }
    case kExprRefIsNull:
    case kExprRefEq:
    case kExprRefAsNonNull: {
      len = 1;
      break;
    }
    case kExprRefFunc: {
      IndexImmediate imm(&decoder, wasm_code_->at(pc + 1), "function index",
                         Decoder::kNoValidation);
      optional.index = imm.index;
      len = 1 + imm.length;
      break;
    }

    case kGCPrefix:
      DecodeGCOp(opcode, &optional, &decoder, wasm_code_, pc, &len);
      break;

    case kNumericPrefix:
      DecodeNumericOp(opcode, &optional, &decoder, wasm_code_, pc, &len);
      break;

    case kAtomicPrefix:
      DecodeAtomicOp(opcode, &optional, &decoder, wasm_code_, pc, &len);
      break;

    case kSimdPrefix: {
      bool is_valid_simd_op =
          DecodeSimdOp(opcode, &optional, &decoder, wasm_code_, pc, &len);
      if (V8_UNLIKELY(!is_valid_simd_op)) {
        UNREACHABLE();
      }
      break;
    }

    case kExprCallRef:
    case kExprReturnCallRef: {
      SigIndexImmediate imm(&decoder, wasm_code_->at(pc + 1),
                            Decoder::kNoValidation);
      optional.index = imm.index.index;
      len = 1 + imm.length;
      break;
    }

    default:
      // Not implemented yet
      UNREACHABLE();
  }

  return WasmInstruction{orig, opcode, len, static_cast<uint32_t>(pc),
                         optional};
}

void WasmBytecodeGenerator::DecodeGCOp(WasmOpcode opcode,
                                       WasmInstruction::Optional* optional,
                                       Decoder* decoder, InterpreterCode* code,
                                       pc_t pc, int* const len) {
  switch (opcode) {
    case kExprStructNew:
    case kExprStructNewDefault: {
      StructIndexImmediate imm(decoder, code->at(pc + *len),
                               Decoder::kNoValidation);
      optional->index = imm.index.index;
      *len += imm.length;
      break;
    }
    case kExprStructGet:
    case kExprStructGetS:
    case kExprStructGetU:
    case kExprStructSet: {
      FieldImmediate imm(decoder, code->at(pc + *len), Decoder::kNoValidation);
      optional->gc_field_immediate = {imm.struct_imm.index.index,
                                      imm.field_imm.index};
      *len += imm.length;
      break;
    }
    case kExprArrayNew:
    case kExprArrayNewDefault:
    case kExprArrayGet:
    case kExprArrayGetS:
    case kExprArrayGetU:
    case kExprArraySet:
    case kExprArrayFill: {
      ArrayIndexImmediate imm(decoder, code->at(pc + *len),
                              Decoder::kNoValidation);
      optional->index = imm.index.index;
      *len += imm.length;
      break;
    }

    case kExprArrayNewFixed: {
      ArrayIndexImmediate array_imm(decoder, code->at(pc + *len),
                                    Decoder::kNoValidation);
      optional->gc_array_new_fixed.array_index = array_imm.index.index;
      *len += array_imm.length;
      IndexImmediate data_imm(decoder, code->at(pc + *len), "array length",
                              Decoder::kNoValidation);
      optional->gc_array_new_fixed.length = data_imm.index;
      *len += data_imm.length;
      break;
    }

    case kExprArrayNewData:
    case kExprArrayNewElem:
    case kExprArrayInitData:
    case kExprArrayInitElem: {
      ArrayIndexImmediate array_imm(decoder, code->at(pc + *len),
                                    Decoder::kNoValidation);
      optional->gc_array_new_or_init_data.array_index = array_imm.index.index;
      *len += array_imm.length;
      IndexImmediate data_imm(decoder, code->at(pc + *len), "segment index",
                              Decoder::kNoValidation);
      optional->gc_array_new_or_init_data.data_index = data_imm.index;
      *len += data_imm.length;
      break;
    }

    case kExprArrayCopy: {
      ArrayIndexImmediate dest_array_imm(decoder, code->at(pc + *len),
                                         Decoder::kNoValidation);
      optional->gc_array_copy.dest_array_index = dest_array_imm.index.index;
      *len += dest_array_imm.length;
      ArrayIndexImmediate src_array_imm(decoder, code->at(pc + *len),
                                        Decoder::kNoValidation);
      optional->gc_array_copy.src_array_index = src_array_imm.index.index;
      *len += src_array_imm.length;
      break;
    }

    case kExprRefI31:
    case kExprI31GetS:
    case kExprI31GetU:
    case kExprAnyConvertExtern:
    case kExprExternConvertAny:
    case kExprArrayLen:
      break;

    case kExprRefCast:
    case kExprRefCastNull:
    case kExprRefTest:
    case kExprRefTestNull: {
      HeapTypeImmediate imm(WasmEnabledFeatures::All(), decoder,
                            code->at(pc + *len), Decoder::kNoValidation);
      optional->gc_heap_type_immediate.length = imm.length;
      optional->gc_heap_type_immediate.type_representation =
          imm.type.representation();
      *len += imm.length;
      break;
    }

    case kExprBrOnCast:
    case kExprBrOnCastFail: {
      BrOnCastImmediate flags_imm(decoder, code->at(pc + *len),
                                  Decoder::kNoValidation);
      *len += flags_imm.length;
      BranchDepthImmediate branch(decoder, code->at(pc + *len),
                                  Decoder::kNoValidation);
      *len += branch.length;
      HeapTypeImmediate source_imm(WasmEnabledFeatures::All(), decoder,
                                   code->at(pc + *len), Decoder::kNoValidation);
      *len += source_imm.length;
      HeapTypeImmediate target_imm(WasmEnabledFeatures::All(), decoder,
                                   code->at(pc + *len), Decoder::kNoValidation);
      *len += target_imm.length;
      optional->br_on_cast_data = BranchOnCastData{
          branch.depth, flags_imm.flags.src_is_null,
          flags_imm.flags.res_is_null, target_imm.type.representation()};
      break;
    }

    default:
      FATAL("Unknown or unimplemented opcode #%d:%s", code->start[pc],
            WasmOpcodes::OpcodeName(static_cast<WasmOpcode>(code->start[pc])));
      UNREACHABLE();
  }
}

void WasmBytecodeGenerator::DecodeNumericOp(WasmOpcode opcode,
                                            WasmInstruction::Optional* optional,
                                            Decoder* decoder,
                                            InterpreterCode* code, pc_t pc,
                                            int* const len) {
  switch (opcode) {
#define DECODE_UNOP(name, from_ctype, from_type, from_reg, to_ctype, to_type, \
                    to_reg)                                                   \
  case kExpr##name:                                                           \
    break;

    FOREACH_TRUNCSAT_UNOP(DECODE_UNOP)
#undef DECODE_UNOP

    case kExprMemoryInit: {
      MemoryInitImmediate imm(decoder, code->at(pc + *len),
                              Decoder::kNoValidation);
      DCHECK_LT(imm.data_segment.index, module_->num_declared_data_segments);
      optional->index = imm.data_segment.index;
      *len += imm.length;
      break;
    }
    case kExprDataDrop: {
      IndexImmediate imm(decoder, code->at(pc + *len), "data segment index",
                         Decoder::kNoValidation);
      DCHECK_LT(imm.index, module_->num_declared_data_segments);
      optional->index = imm.index;
      *len += imm.length;
      break;
    }
    case kExprMemoryCopy: {
      MemoryCopyImmediate imm(decoder, code->at(pc + *len),
                              Decoder::kNoValidation);
      *len += imm.length;
      break;
    }
    case kExprMemoryFill: {
      MemoryIndexImmediate imm(decoder, code->at(pc + *len),
                               Decoder::kNoValidation);
      *len += imm.length;
      break;
    }
    case kExprTableInit: {
      TableInitImmediate imm(decoder, code->at(pc + *len),
                             Decoder::kNoValidation);
      optional->table_init.table_index = imm.table.index;
      optional->table_init.element_segment_index = imm.element_segment.index;
      *len += imm.length;
      break;
    }
    case kExprElemDrop: {
      IndexImmediate imm(decoder, code->at(pc + *len), "element segment index",
                         Decoder::kNoValidation);
      optional->index = imm.index;
      *len += imm.length;
      break;
    }
    case kExprTableCopy: {
      TableCopyImmediate imm(decoder, code->at(pc + *len),
                             Decoder::kNoValidation);
      optional->table_copy.dst_table_index = imm.table_dst.index;
      optional->table_copy.src_table_index = imm.table_src.index;
      *len += imm.length;
      break;
    }
    case kExprTableGrow: {
      IndexImmediate imm(decoder, code->at(pc + *len), "table index",
                         Decoder::kNoValidation);
      optional->index = imm.index;
      *len += imm.length;
      break;
    }
    case kExprTableSize: {
      IndexImmediate imm(decoder, code->at(pc + *len), "table index",
                         Decoder::kNoValidation);
      optional->index = imm.index;
      *len += imm.length;
      break;
    }
    case kExprTableFill: {
      IndexImmediate imm(decoder, code->at(pc + *len), "table index",
                         Decoder::kNoValidation);
      optional->index = imm.index;
      *len += imm.length;
      break;
    }
    default:
      FATAL("Unknown or unimplemented opcode #%d:%s", code->start[pc],
            WasmOpcodes::OpcodeName(static_cast<WasmOpcode>(code->start[pc])));
      UNREACHABLE();
  }
}

void WasmBytecodeGenerator::DecodeAtomicOp(WasmOpcode opcode,
                                           WasmInstruction::Optional* optional,
                                           Decoder* decoder,
                                           InterpreterCode* code, pc_t pc,
                                           int* const len) {
  switch (opcode) {
    case kExprAtomicNotify:
    case kExprI32AtomicWait: {
      MachineType memtype = MachineType::Uint32();
      MemoryAccessImmediate imm(decoder, code->at(pc + *len),
                                ElementSizeLog2Of(memtype.representation()),
                                IsMemory64(), Decoder::kNoValidation);
      optional->offset = imm.offset;
      *len += imm.length;
      break;
    }
    case kExprI64AtomicWait: {
      MachineType memtype = MachineType::Uint64();
      MemoryAccessImmediate imm(decoder, code->at(pc + *len),
                                ElementSizeLog2Of(memtype.representation()),
                                IsMemory64(), Decoder::kNoValidation);
      optional->offset = imm.offset;
      *len += imm.length;
      break;
    }
    case kExprAtomicFence:
      *len += 1;
      break;

#define ATOMIC_BINOP(name, Type, ctype, type, op_ctype, op_type, operation) \
  case kExpr##name: {                                                       \
    MachineType memtype = MachineType::Type();                              \
    MemoryAccessImmediate imm(decoder, code->at(pc + *len),                 \
                              ElementSizeLog2Of(memtype.representation()),  \
                              IsMemory64(), Decoder::kNoValidation);        \
    optional->offset = imm.offset;                                          \
    *len += imm.length;                                                     \
    break;                                                                  \
  }
      FOREACH_ATOMIC_BINOP(ATOMIC_BINOP)
#undef ATOMIC_BINOP

#define ATOMIC_OP(name, Type, ctype, type, op_ctype, op_type)              \
  case kExpr##name: {                                                      \
    MachineType memtype = MachineType::Type();                             \
    MemoryAccessImmediate imm(decoder, code->at(pc + *len),                \
                              ElementSizeLog2Of(memtype.representation()), \
                              IsMemory64(), Decoder::kNoValidation);       \
    optional->offset = imm.offset;                                         \
    *len += imm.length;                                                    \
    break;                                                                 \
  }
      FOREACH_ATOMIC_COMPARE_EXCHANGE_OP(ATOMIC_OP)
      FOREACH_ATOMIC_LOAD_OP(ATOMIC_OP)
      FOREACH_ATOMIC_STORE_OP(ATOMIC_OP)
#undef ATOMIC_OP

    default:
      FATAL("Unknown or unimplemented opcode #%d:%s", code->start[pc],
            WasmOpcodes::OpcodeName(static_cast<WasmOpcode>(code->start[pc])));
      UNREACHABLE();
  }
}

const char* GetRegModeString(RegMode reg_mode) {
  switch (reg_mode) {
    case RegMode::kNoReg:
      return "NoReg";
    case RegMode::kAnyReg:
      return "AnyReg";
    case RegMode::kI32Reg:
      return "I32Reg";
    case RegMode::kI64Reg:
      return "I64Reg";
    case RegMode::kF32Reg:
      return "F32Reg";
    case RegMode::kF64Reg:
      return "F64Reg";
    default:
      UNREACHABLE();
  }
}

const char* GetOperatorModeString(OperatorMode mode) {
  switch (mode) {
    case kR2R:
      return "R2R";
    case kR2S:
      return "R2S";
    case kS2R:
      return "S2R";
    case kS2S:
      return "S2S";
    default:
      UNREACHABLE();
  }
}

#if !defined(V8_DRUMBRAKE_BOUNDS_CHECKS)
INSTRUCTION_HANDLER_FUNC
TrapMemOutOfBounds(const uint8_t* code, uint32_t* sp,
                   WasmInterpreterRuntime* wasm_runtime, int64_t r0,
                   double fp0) {
  TRAP(TrapReason::kTrapMemOutOfBounds)
}
#endif  // !defined(V8_DRUMBRAKE_BOUNDS_CHECKS)

// static
void WasmInterpreter::InitializeOncePerProcess() {
  WasmInterpreterThread::Initialize();
}

// static
void WasmInterpreter::GlobalTearDown() {
  // TODO(paolosev@microsoft.com): Support multithreading.

#ifdef DRUMBRAKE_ENABLE_PROFILING
  PrintAndClearProfilingData();
#endif  // DRUMBRAKE_ENABLE_PROFILING

  if (v8_flags.drumbrake_compact_bytecode) {
    WasmBytecodeGenerator::PrintBytecodeCompressionStats();
  }

  WasmInterpreterThread::Terminate();
}

// static
void WasmBytecodeGenerator::PrintBytecodeCompressionStats() {
  printf("Total bytecode size: %zu bytes.\n", total_bytecode_size_);
  size_t space_saved_in_bytes = 2 * emitted_short_slot_offset_count_ +
                                4 * emitted_short_memory_offset_count_;
  double saved_pct = (total_bytecode_size_ + space_saved_in_bytes == 0)
                         ? .0
                         : 100.0 * space_saved_in_bytes /
                               (total_bytecode_size_ + space_saved_in_bytes);
  printf("Bytes saved: %zu (%.1f%%).\n", space_saved_in_bytes, saved_pct);
}

void WasmBytecodeGenerator::InitSlotsForFunctionArgs(const FunctionSig* sig,
                                                     bool is_indirect_call) {
  size_t stack_index;
  if (is_indirect_call) {
    // Subtract one to discard the function index on the top of the stack.
    DCHECK_LE(sig->parameter_count(), stack_.size() - 1);
    stack_index = stack_.size() - sig->parameter_count() - 1;
  } else {
    DCHECK_LE(sig->parameter_count(), stack_.size());
    stack_index = stack_.size() - sig->parameter_count();
  }

  bool fast_path = sig->parameter_count() > 1 && sig->parameter_count() < 32 &&
                   !WasmBytecode::HasRefOrSimdArgs(sig);
  if (fast_path) {
    if (sig->parameter_count() == 2) {
      const ValueType type0 = sig->GetParam(0);
      const ValueKind kind0 = type0.kind();
      ValueType type1 = sig->GetParam(1);
      const ValueKind kind1 = type1.kind();
      uint32_t to = CreateSlot(type0);
      CreateSlot(type1);

      uint32_t copyslot32_two_args_func_id =
          ((kind0 == kI64 || kind0 == kF64) ? 0x01 : 0x00) |
          ((kind1 == kI64 || kind1 == kF64) ? 0x02 : 0x00);
      static const InstructionHandler kCopySlot32TwoArgFuncs[4] = {
          k_s2s_CopySlot_ll, k_s2s_CopySlot_lq, k_s2s_CopySlot_ql,
          k_s2s_CopySlot_qq};

      START_EMIT_INSTR_HANDLER() {
        EmitFnId(kCopySlot32TwoArgFuncs[copyslot32_two_args_func_id]);
        EmitSlotOffset(slots_[to].slot_offset);
        EmitSlotOffset(slots_[stack_[stack_index]].slot_offset);
        stack_index++;
        EmitSlotOffset(slots_[stack_[stack_index]].slot_offset);
        stack_index++;
      }
      END_EMIT_INSTR_HANDLER()
    } else {
      START_EMIT_INSTR_HANDLER_WITH_ID(s2s_CopySlotMulti) {
        EmitI32Const(static_cast<uint32_t>(sig->parameter_count()));

        uint32_t arg_size_mask = 0;
        for (size_t index = 0; index < sig->parameter_count(); index++) {
          const ValueType value_type = sig->GetParam(index);
          const ValueKind kind = value_type.kind();
          if (kind == kI64 || kind == kF64) {
            arg_size_mask |= (1 << index);
          }
        }
        EmitI32Const(arg_size_mask);

        uint32_t to = 0;
        for (size_t index = 0; index < sig->parameter_count(); index++) {
          const ValueType value_type = sig->GetParam(index);
          to = CreateSlot(value_type);
          if (index == 0) {
            EmitSlotOffset(slots_[to].slot_offset);
          }
          EmitSlotOffset(slots_[stack_[stack_index]].slot_offset);
          stack_index++;
        }
      }
      END_EMIT_INSTR_HANDLER()
    }
  } else {
    // Slow path.
    for (size_t index = 0; index < sig->parameter_count(); index++) {
      ValueType value_type = sig->GetParam(index);
      uint32_t to = CreateSlot(value_type);
      EmitCopySlot(value_type, stack_[stack_index], to);
      stack_index++;
    }
  }
}

// static
void WasmInterpreter::NotifyIsolateDisposal(Isolate* isolate) {
  WasmInterpreterThread::NotifyIsolateDisposal(isolate);
}

// Checks if {obj} is a subtype of type, thus checking will always
// succeed.
bool WasmBytecodeGenerator::TypeCheckAlwaysSucceeds(ValueType obj_type,
                                                    HeapType type) const {
  return IsSubtypeOf(obj_type, ValueType::RefNull(type), module_);
}

// Returns true if type checking will always fail, either because the types
// are unrelated or because the target_type is one of the null sentinels and
// conversion to null does not succeed.
bool WasmBytecodeGenerator::TypeCheckAlwaysFails(ValueType obj_type,
                                                 HeapType expected_type,
                                                 bool null_succeeds) const {
  bool types_unrelated =
      !IsSubtypeOf(ValueType::Ref(expected_type), obj_type, module_) &&
      !IsSubtypeOf(obj_type, ValueType::RefNull(expected_type), module_);
  // (Comment copied from function-body-decoder-impl.h).
  // For "unrelated" types the check can still succeed for the null value on
  // instructions treating null as a successful check.
  // TODO(12868): For string views, this implementation anticipates that
  // https://github.com/WebAssembly/stringref/issues/40 will be resolved
  // by making the views standalone types.
  return (types_unrelated &&
          (!null_succeeds || !obj_type.is_nullable() ||
           obj_type.is_string_view() || expected_type.is_string_view())) ||
         (!null_succeeds &&
          (expected_type.representation() == HeapType::kNone ||
           expected_type.representation() == HeapType::kNoFunc ||
           expected_type.representation() == HeapType::kNoExtern));
}

#ifdef DEBUG
// static
bool WasmBytecodeGenerator::HasSideEffects(WasmOpcode opcode) {
  switch (opcode) {
    case kExprBlock:
    case kExprLoop:
    case kExprTry:
    case kExprIf:
    case kExprElse:
    case kExprCatch:
    case kExprCatchAll:
    case kExprDelegate:
    case kExprEnd:
    case kExprBr:
    case kExprBrIf:
    case kExprBrOnNull:
    case kExprBrOnNonNull:
    case kExprBrOnCast:
    case kExprBrOnCastFail:
    case kExprBrTable:
    case kExprReturn:
    case kExprCallFunction:
    case kExprReturnCall:
    case kExprCallIndirect:
    case kExprReturnCallIndirect:
    case kExprCallRef:
    case kExprReturnCallRef:
    case kExprLocalSet:
    case kExprLocalTee:
    case kExprI32Const:
    case kExprI64Const:
    case kExprF32Const:
    case kExprF64Const:
    case kExprS128Const:
    case kExprI8x16Shuffle:
      return true;

    case kExprUnreachable:
    case kExprNop:
    case kExprThrow:
    case kExprRethrow:
    case kExprDrop:
    case kExprSelect:
    case kExprSelectWithType:
    case kExprLocalGet:
    case kExprGlobalGet:
    case kExprGlobalSet:
    case kExprTableGet:
    case kExprTableSet:
    case kExprI32LoadMem:
    case kExprI32LoadMem8S:
    case kExprI32LoadMem8U:
    case kExprI32LoadMem16S:
    case kExprI32LoadMem16U:
    case kExprI64LoadMem:
    case kExprI64LoadMem8S:
    case kExprI64LoadMem8U:
    case kExprI64LoadMem16S:
    case kExprI64LoadMem16U:
    case kExprI64LoadMem32S:
    case kExprI64LoadMem32U:  // 0x28 - 0x35
    case kExprI32StoreMem:
    case kExprI32StoreMem8:
    case kExprI32StoreMem16:
    case kExprI64StoreMem:
    case kExprI64StoreMem8:
    case kExprI64StoreMem16:
    case kExprI64StoreMem32:  // 0x36 - 0x3e
    case kExprMemoryGrow:
    case kExprMemorySize:
    case kExprI32Eqz:
    case kExprI32Eq:
    case kExprI32Ne:
    case kExprI32LtS:
    case kExprI32LtU:
    case kExprI32GtS:
    case kExprI32GtU:
    case kExprI32LeS:
    case kExprI32LeU:
    case kExprI32GeS:
    case kExprI32GeU:
    case kExprI32Clz:
    case kExprI32Ctz:
    case kExprI32Popcnt:  // 0x45 - 0x69
    case kExprI32Add:
    case kExprI32Sub:
    case kExprI32Mul:  // 0x6a - 0x6c
    case kExprI32DivS:
    case kExprI32DivU:
    case kExprI32RemS:
    case kExprI32RemU:
    case kExprI32And:
    case kExprI32Ior:
    case kExprI32Xor:
    case kExprI32Shl:
    case kExprI32ShrS:
    case kExprI32ShrU:
    case kExprI32Rol:
    case kExprI32Ror:  // 0x6d - 0x78
    case kExprI64Clz:
    case kExprI64Ctz:
    case kExprI64Popcnt:  // 0x79 - 0x7b
    case kExprI64Add:
    case kExprI64Sub:
    case kExprI64Mul:  // 0x7c - 0x7e
    case kExprI64DivS:
    case kExprI64DivU:
    case kExprI64RemS:
    case kExprI64RemU:
    case kExprI64And:
    case kExprI64Ior:
    case kExprI64Xor:
    case kExprI64Shl:
    case kExprI64ShrS:
    case kExprI64ShrU:
    case kExprI64Rol:
    case kExprI64Ror:  // 0x7f - 0x8a
    case kExprF32Abs:
    case kExprF32Neg:
    case kExprF32Ceil:
    case kExprF32Floor:
    case kExprF32Trunc:
    case kExprF32NearestInt:
    case kExprF32Sqrt:  // 0x8b - 0x91
    case kExprF32Add:
    case kExprF32Sub:
    case kExprF32Mul:
    case kExprF32Div:
    case kExprF32Min:
    case kExprF32Max:
    case kExprF32CopySign:  // 0x92 - 0x98
    case kExprF64Abs:
    case kExprF64Neg:
    case kExprF64Ceil:
    case kExprF64Floor:
    case kExprF64Trunc:
    case kExprF64NearestInt:
    case kExprF64Sqrt:  // 0x99 - 0x9f
    case kExprF64Add:
    case kExprF64Sub:
    case kExprF64Mul:
    case kExprF64Div:
    case kExprF64Min:
    case kExprF64Max:
    case kExprF64CopySign:  // 0xa0 - 0xa6
    case kExprI32ConvertI64:
    case kExprI32SConvertF32:
    case kExprI32UConvertF32:
    case kExprI32SConvertF64:
    case kExprI32UConvertF64:
    case kExprI64SConvertI32:
    case kExprI64UConvertI32:
    case kExprI64SConvertF32:
    case kExprI64UConvertF32:
    case kExprI64SConvertF64:
    case kExprI64UConvertF64:
    case kExprF32SConvertI32:
    case kExprF32UConvertI32:
    case kExprF32SConvertI64:
    case kExprF32UConvertI64:
    case kExprF32ConvertF64:
    case kExprF64SConvertI32:
    case kExprF64UConvertI32:
    case kExprF64SConvertI64:
    case kExprF64UConvertI64:
    case kExprF64ConvertF32:
    case kExprI32ReinterpretF32:
    case kExprI64ReinterpretF64:
    case kExprF32ReinterpretI32:
    case kExprF64ReinterpretI64:
    case kExprI32SExtendI8:
    case kExprI32SExtendI16:
    case kExprI64SExtendI8:
    case kExprI64SExtendI16:
    case kExprI64SExtendI32:  // 0xa7 - 0xc4
    case kExprRefNull:
    case kExprRefIsNull:
    case kExprRefFunc:
    case kExprRefEq:
    case kExprRefAsNonNull:  // 0xd0 - 0xd4
    // WasmGC
    case kGCPrefix:
    case kExprStructNew:
    case kExprStructNewDefault:
    case kExprStructGet:
    case kExprStructGetS:
    case kExprStructGetU:
    case kExprStructSet:
    case kExprArrayNew:
    case kExprArrayNewDefault:
    case kExprArrayGet:
    case kExprArrayGetS:
    case kExprArrayGetU:
    case kExprArraySet:
    case kExprArrayFill:
    case kExprRefI31:
    case kExprI31GetS:
    case kExprI31GetU:
    case kExprRefCast:
    case kExprRefCastNull:
    case kExprAnyConvertExtern:
    case kExprExternConvertAny:
    case kExprArrayLen:
    case kExprRefTest:
    case kExprRefTestNull:
    // Numeric
    case kNumericPrefix:
    case kExprI32SConvertSatF32:
    case kExprI32UConvertSatF32:
    case kExprI32SConvertSatF64:
    case kExprI32UConvertSatF64:
    case kExprI64SConvertSatF32:
    case kExprI64UConvertSatF32:
    case kExprI64SConvertSatF64:
    case kExprI64UConvertSatF64:
    case kExprMemoryInit:
    case kExprDataDrop:
    case kExprMemoryCopy:
    case kExprMemoryFill:
    case kExprTableInit:
    case kExprElemDrop:
    case kExprTableCopy:
    case kExprTableGrow:
    case kExprTableSize:
    case kExprTableFill:
    // Atomics
    case kAtomicPrefix:
    case kExprAtomicNotify:
    case kExprI32AtomicWait:
    case kExprI64AtomicWait:
    case kExprAtomicFence:  // 0xfe00 - 0xfe03
    case kExprI32AtomicLoad:
    case kExprI64AtomicLoad:
    case kExprI32AtomicStore:
    case kExprI64AtomicStore:
    case kExprI32AtomicAdd:
    case kExprI64AtomicAdd:
    case kExprI32AtomicSub:
    case kExprI64AtomicSub:
    case kExprI32AtomicAnd:
    case kExprI64AtomicAnd:
    case kExprI32AtomicOr:
    case kExprI64AtomicOr:
    case kExprI32AtomicXor:
    case kExprI64AtomicXor:
    case kExprI32AtomicExchange:
    case kExprI64AtomicExchange:
    case kExprI32AtomicCompareExchange:
    case kExprI64AtomicCompareExchange:  // 0xfe10 - 0xfe4e

    // SIMD
    case kSimdPrefix:
    case kExprS128LoadMem:
    case kExprS128Load8Splat:
    case kExprS128Load16Splat:
    case kExprS128Load32Splat:
    case kExprS128Load64Splat:
    case kExprS128StoreMem:
    case kExprI8x16Swizzle:
    case kExprI8x16Splat:
    case kExprI16x8Splat:
    case kExprI32x4Splat:
    case kExprI64x2Splat:
    case kExprF32x4Splat:
    case kExprF64x2Splat:
    case kExprI8x16ExtractLaneS:
    case kExprI8x16ExtractLaneU:
    case kExprI16x8ExtractLaneS:
    case kExprI16x8ExtractLaneU:
    case kExprI32x4ExtractLane:
    case kExprI64x2ExtractLane:
    case kExprF32x4ExtractLane:
    case kExprF64x2ExtractLane:
    case kExprI8x16ReplaceLane:
    case kExprI16x8ReplaceLane:
    case kExprI32x4ReplaceLane:
    case kExprI64x2ReplaceLane:
    case kExprF32x4ReplaceLane:
    case kExprF64x2ReplaceLane:
    case kExprI8x16Eq:
    case kExprI8x16Ne:
    case kExprI8x16LtS:
    case kExprI8x16LtU:
    case kExprI8x16GtS:
    case kExprI8x16GtU:
    case kExprI8x16LeS:
    case kExprI8x16LeU:
    case kExprI8x16GeS:
    case kExprI8x16GeU:
    case kExprI16x8Eq:
    case kExprI16x8Ne:
    case kExprI16x8LtS:
    case kExprI16x8LtU:
    case kExprI16x8GtS:
    case kExprI16x8GtU:
    case kExprI16x8LeS:
    case kExprI16x8LeU:
    case kExprI16x8GeS:
    case kExprI16x8GeU:
    case kExprI32x4Eq:
    case kExprI32x4Ne:
    case kExprI32x4LtS:
    case kExprI32x4LtU:
    case kExprI32x4GtS:
    case kExprI32x4GtU:
    case kExprI32x4LeS:
    case kExprI32x4LeU:
    case kExprI32x4GeS:
    case kExprI32x4GeU:
    case kExprI64x2Eq:
    case kExprI64x2Ne:
    case kExprI64x2LtS:
    case kExprI64x2GtS:
    case kExprI64x2LeS:
    case kExprI64x2GeS:
    case kExprF32x4Eq:
    case kExprF32x4Ne:
    case kExprF32x4Lt:
    case kExprF32x4Gt:
    case kExprF32x4Le:
    case kExprF32x4Ge:
    case kExprF64x2Eq:
    case kExprF64x2Ne:
    case kExprF64x2Lt:
    case kExprF64x2Gt:
    case kExprF64x2Le:
    case kExprF64x2Ge:
    case kExprS128Not:
    case kExprS128And:
    case kExprS128AndNot:
    case kExprS128Or:
    case kExprS128Xor:
    case kExprS128Select:
    case kExprV128AnyTrue:
    case kExprS128Load8Lane:           // 0xfd54
    case kExprS128Load16Lane:          // 0xfd55
    case kExprS128Load32Lane:          // 0xfd56
    case kExprS128Load64Lane:          // 0xfd57
    case kExprS128Store8Lane:          // 0xfd58
    case kExprS128Store16Lane:         // 0xfd59
    case kExprS128Store32Lane:         // 0xfd5a
    case kExprS128Store64Lane:         // 0xfd5b
    case kExprS128Load32Zero:          // 0xfd5c
    case kExprS128Load64Zero:          // 0xfd5d
    case kExprF32x4DemoteF64x2Zero:    // 0xfd5e
    case kExprI32x4Neg:                // 0xfda1
    case kExprI32x4AllTrue:            // 0xfda3
    case kExprI32x4BitMask:            // 0xfda4
    case kExprI32x4SConvertI16x8Low:   // 0xfda7
    case kExprI32x4Add:                // 0xfdae
    case kExprI32x4Sub:                // 0xfdb1
    case kExprI32x4Mul:                // 0xfdb5
    case kExprI32x4ExtMulLowI16x8S:    // 0xfdbc
    case kExprI32x4ExtMulHighI16x8S:   // 0xfdbd
    case kExprI32x4ExtMulLowI16x8U:    // 0xfdbe
    case kExprI32x4ExtMulHighI16x8U:   // 0xfdbf
    case kExprI64x2Neg:                // 0xfdc1
    case kExprI64x2AllTrue:            // 0xfdc3
    case kExprI64x2BitMask:            // 0xfdc4
    case kExprI64x2SConvertI32x4Low:   // 0xfdc7
    case kExprI64x2SConvertI32x4High:  // 0xfdc8
    case kExprI64x2UConvertI32x4Low:   // 0xfdc9
    case kExprI64x2UConvertI32x4High:  // 0xfdca
    case kExprI64x2Add:                // 0xfdce
    case kExprI64x2Sub:                // 0xfdd1
    case kExprI64x2Mul:                // 0xfdd5
    case kExprF32x4Neg:                // 0xfde1
    case kExprF32x4Sqrt:               // 0xfde3                     // 0xfde5
    case kExprF64x2ConvertLowI32x4S:   // 0xfde6
    case kExprF64x2ConvertLowI32x4U:   // 0xfde7

    // Relaxed SIMD
    case kExprI8x16RelaxedSwizzle:
    case kExprI32x4RelaxedTruncF32x4S:
    case kExprI32x4RelaxedTruncF32x4U:
    case kExprI32x4RelaxedTruncF64x2SZero:
    case kExprI32x4RelaxedTruncF64x2UZero:
    case kExprF32x4Qfma:
    case kExprF32x4Qfms:
    case kExprF64x2Qfma:
    case kExprF64x2Qfms:
    case kExprI8x16RelaxedLaneSelect:
    case kExprI16x8RelaxedLaneSelect:
    case kExprI32x4RelaxedLaneSelect:
    case kExprI64x2RelaxedLaneSelect:
    case kExprF32x4RelaxedMin:
    case kExprF32x4RelaxedMax:
    case kExprF64x2RelaxedMin:
    case kExprF64x2RelaxedMax:
    case kExprI16x8RelaxedQ15MulRS:
    case kExprI16x8DotI8x16I7x16S:
    case kExprI32x4DotI8x16I7x16AddS:

    // FP16 SIMD
    case kExprF16x8Splat:
    case kExprF16x8ExtractLane:
    case kExprF16x8ReplaceLane:
    case kExprF16x8Abs:
    case kExprF16x8Neg:
    case kExprF16x8Sqrt:
    case kExprF16x8Ceil:
    case kExprF16x8Floor:
    case kExprF16x8Trunc:
    case kExprF16x8NearestInt:
    case kExprF16x8Eq:
    case kExprF16x8Ne:
    case kExprF16x8Lt:
    case kExprF16x8Gt:
    case kExprF16x8Le:
    case kExprF16x8Ge:
    case kExprF16x8Add:
    case kExprF16x8Sub:
    case kExprF16x8Mul:
    case kExprF16x8Div:
    case kExprF16x8Min:
    case kExprF16x8Max:
    case kExprF16x8Pmin:
    case kExprF16x8Pmax:
    case kExprI16x8SConvertF16x8:
    case kExprI16x8UConvertF16x8:
    case kExprF16x8SConvertI16x8:
    case kExprF16x8UConvertI16x8:
    case kExprF16x8DemoteF32x4Zero:
    case kExprF16x8DemoteF64x2Zero:
    case kExprF32x4PromoteLowF16x8:
    case kExprF16x8Qfma:
    case kExprF16x8Qfms:  // 0xfd100 - 0xfd14f

      // Not handled by DrumBrake

    case kExprNopForTestingUnsupportedInLiftoff:
    case kExprTryTable:
    case kExprThrowRef:
    case kExprF64Acos:
    case kExprF64Asin:
    case kExprF64Atan:
    case kExprF64Atan2:
    case kExprF64Cos:
    case kExprF64Sin:
    case kExprF64Tan:
    case kExprF64Exp:
    case kExprF64Log:
    case kExprF64Pow:  // 0xdc - 0xe6
    case kExprI32AsmjsDivS:
    case kExprI32AsmjsDivU:
    case kExprI32AsmjsRemS:
    case kExprI32AsmjsRemU:
    case kExprI32AsmjsSConvertF32:
    case kExprI32AsmjsUConvertF32:
    case kExprI32AsmjsSConvertF64:
    case kExprI32AsmjsUConvertF64:  // 0xe7 - 0xfa
    case kExprRefCastNop:

    // StringRef
    case kExprStringNewUtf8:
    case kExprStringNewWtf16:
    case kExprStringConst:
    case kExprStringMeasureUtf8:
    case kExprStringMeasureWtf8:
    case kExprStringMeasureWtf16:
    case kExprStringEncodeUtf8:
    case kExprStringEncodeWtf16:
    case kExprStringConcat:
    case kExprStringEq:
    case kExprStringIsUSVSequence:
    case kExprStringNewLossyUtf8:
    case kExprStringNewWtf8:
    case kExprStringEncodeLossyUtf8:
    case kExprStringEncodeWtf8:
    case kExprStringNewUtf8Try:
    case kExprStringAsWtf8:
    case kExprStringViewWtf8Advance:
    case kExprStringViewWtf8EncodeUtf8:
    case kExprStringViewWtf8Slice:
    case kExprStringViewWtf8EncodeLossyUtf8:
    case kExprStringViewWtf8EncodeWtf8:  // 0xfb80 - 0xfb95
    case kExprStringAsWtf16:
    case kExprStringViewWtf16Length:
    case kExprStringViewWtf16GetCodeunit:
    case kExprStringViewWtf16Encode:
    case kExprStringViewWtf16Slice:
    case kExprStringAsIter:
    case kExprStringViewIterNext:
    case kExprStringViewIterAdvance:
    case kExprStringViewIterRewind:
    case kExprStringViewIterSlice:
    case kExprStringCompare:
    case kExprStringFromCodePoint:
    case kExprStringHash:
    case kExprStringNewUtf8Array:
    case kExprStringNewWtf16Array:
    case kExprStringEncodeUtf8Array:
    case kExprStringEncodeWtf16Array:
    case kExprStringNewLossyUtf8Array:
    case kExprStringNewWtf8Array:
    case kExprStringEncodeLossyUtf8Array:
    case kExprStringEncodeWtf8Array:
    case kExprStringNewUtf8ArrayTry:

    // FP16
    case kExprF32LoadMemF16:
    case kExprF32StoreMemF16:
    default:
      return false;
  }
}
#endif  // DEBUG

RegMode WasmBytecodeGenerator::EncodeInstruction(const WasmInstruction& instr,
                                                 RegMode curr_reg_mode,
                                                 RegMode next_reg_mode) {
  if (!v8_flags.drumbrake_compact_bytecode) {
    DCHECK_EQ(handler_size_, InstrHandlerSize::Large);
    return DoEncodeInstruction(instr, curr_reg_mode, next_reg_mode);
  }

  size_t current_instr_code_offset = code_.size();
  size_t current_slots_size = slots_.size();
  current_instr_encoding_failed_ = false;
  handler_size_ = InstrHandlerSize::Small;
  stack_.clear_history();

  RegMode reg_mode = DoEncodeInstruction(instr, curr_reg_mode, next_reg_mode);
  if (current_instr_encoding_failed_) {
    DCHECK(!HasSideEffects(instr.opcode));
    code_.resize(current_instr_code_offset);
    slots_.resize(current_slots_size);
    stack_.rollback();
    current_instr_encoding_failed_ = false;
    handler_size_ = InstrHandlerSize::Large;
    reg_mode = DoEncodeInstruction(instr, curr_reg_mode, next_reg_mode);
    DCHECK(!current_instr_encoding_failed_);
  }

  return reg_mode;
}

RegMode WasmBytecodeGenerator::DoEncodeInstruction(const WasmInstruction& instr,
                                                   RegMode curr_reg_mode,
                                                   RegMode next_reg_mode) {
  DCHECK(curr_reg_mode != RegMode::kAnyReg);

#ifdef DEBUG
  was_current_instruction_reachable_ = is_instruction_reachable_;
#endif  // DEBUG
  if (!is_instruction_reachable_) {
    if (instr.opcode == kExprBlock || instr.opcode == kExprLoop ||
        instr.opcode == kExprIf || instr.opcode == kExprTry) {
      unreachable_block_count_++;
    } else if (instr.opcode == kExprEnd || instr.opcode == kExprDelegate) {
      DCHECK_GT(unreachable_block_count_, 0);
      if (0 == --unreachable_block_count_) {
        is_instruction_reachable_ = true;
      }
    } else if (instr.opcode == kExprElse || instr.opcode == kExprCatch ||
               instr.opcode == kExprCatchAll) {
      if (1 == unreachable_block_count_) {
        is_instruction_reachable_ = true;
        unreachable_block_count_ = 0;
      }
    }
  }
  if (!is_instruction_reachable_) return RegMode::kNoReg;

  ValueKind top_stack_slot_type = GetTopStackType(curr_reg_mode);

  OperatorMode mode = kS2S;
  if (v8_flags.drumbrake_register_optimization) {
    switch (next_reg_mode) {
      case RegMode::kNoReg:
        if (curr_reg_mode != RegMode::kNoReg) {
          mode = kR2S;
        }
        break;
      case RegMode::kAnyReg:
      default:  // kI32Reg|kI64Reg|kF32Reg|kF64Reg
        if (curr_reg_mode == RegMode::kNoReg) {
          if (ToRegisterIsAllowed(instr)) {
            mode = kS2R;
          } else {
            mode = kS2S;
          }
        } else {
          if (ToRegisterIsAllowed(instr)) {
            mode = kR2R;
          } else {
            mode = kR2S;
          }
        }
        break;
    }
  }

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  if (v8_flags.trace_drumbrake_bytecode_generator) {
    printf("PRE   @%-3u:         %-24s: %3s %-7s -> %-7s\n", instr.pc,
           wasm::WasmOpcodes::OpcodeName(static_cast<WasmOpcode>(instr.opcode)),
           GetOperatorModeString(mode), GetRegModeString(curr_reg_mode),
           GetRegModeString(next_reg_mode));
  }

  if (v8_flags.trace_drumbrake_execution) {
    EMIT_INSTR_HANDLER(s2s_TraceInstruction);
    EmitI32Const(instr.pc);
    EmitI32Const(instr.opcode);
    EmitI32Const(static_cast<int>(curr_reg_mode));
  }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

  switch (instr.opcode) {
    case kExprUnreachable: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_Unreachable, instr.pc);
      SetUnreachableMode();
      break;
    }
    case kExprNop:
      break;
    case kExprBlock:
    case kExprLoop: {
      PreserveArgsAndLocals();
      BeginBlock(instr.opcode, instr.optional.block);
      break;
    }
    case kExprTry: {
      PreserveArgsAndLocals();
      int parent_or_matching_try_block_index = GetCurrentTryBlockIndex(true);
      int ancestor_try_block_index = GetCurrentTryBlockIndex(false);
      int try_block_index = BeginBlock(instr.opcode, instr.optional.block);
      eh_data_.AddTryBlock(try_block_index, parent_or_matching_try_block_index,
                           ancestor_try_block_index);
      break;
    }
    case kExprIf: {
      PreserveArgsAndLocals();
      if (mode == kR2S) {
        EMIT_INSTR_HANDLER(r2s_If);
      } else {
        DCHECK_EQ(mode, kS2S);
        EMIT_INSTR_HANDLER(s2s_If);
        I32Pop();  // cond
      }
      BeginBlock(instr.opcode, instr.optional.block);
      EmitIfElseBranchOffset();
      break;
    }
    case kExprElse: {
      DCHECK_GT(current_block_index_, 0);
      DCHECK(blocks_[current_block_index_].IsIf());
      BeginElseBlock(current_block_index_, false);
      EMIT_INSTR_HANDLER(s2s_Else);
      EmitIfElseBranchOffset();  // Jumps to the end of the 'else' block.
      break;
    }
    case kExprCatch:
    case kExprCatchAll: {
      DCHECK_GT(current_block_index_, 0);

      int try_block_index = eh_data_.GetCurrentTryBlockIndex();
      DCHECK_GT(try_block_index, 0);

      EndBlock(instr.opcode);  // End previous try or catch.

      stack_.resize(blocks_[try_block_index].stack_size_);
      int32_t catch_block_index =
          BeginBlock(instr.opcode, blocks_[try_block_index].signature_);

      EMIT_INSTR_HANDLER(s2s_Catch);
      EmitTryCatchBranchOffset();  // Jumps to the end of the try/catch blocks.

      uint32_t first_param_slot_index = UINT_MAX;
      uint32_t first_ref_param_slot_index = UINT_MAX;
      if (instr.opcode == kExprCatch) {
        // Exception arguments are pushed into the stack.
        const WasmTag& tag = module_->tags[instr.optional.index];
        const FunctionSig* sig = tag.sig;
        for (size_t i = 0; i < sig->parameter_count(); ++i) {
          const ValueType value_type = sig->GetParam(i);
          const ValueKind kind = value_type.kind();
          switch (kind) {
            case kI32:
            case kI64:
            case kF32:
            case kF64:
            case kS128:
            case kRef:
            case kRefNull: {
              uint32_t slot_index = CreateSlot(value_type);
              if (first_param_slot_index == UINT_MAX) {
                first_param_slot_index = slot_index;
              }
              if ((kind == kRefNull || kind == kRef) &&
                  first_ref_param_slot_index == UINT_MAX) {
                first_ref_param_slot_index = slot_index;
              }
              PushSlot(slot_index);
              slots_[slot_index].value_type = value_type;
              break;
            }
            default:
              UNREACHABLE();
          }
        }
      }

      blocks_[catch_block_index].first_block_index_ =
          blocks_[try_block_index].first_block_index_;

      if (instr.opcode == kExprCatch) {
        eh_data_.AddCatchBlock(
            current_block_index_, instr.optional.index,
            first_param_slot_index == UINT_MAX
                ? 0
                : slots_[first_param_slot_index].slot_offset,
            first_ref_param_slot_index == UINT_MAX
                ? 0
                : slots_[first_ref_param_slot_index].ref_stack_index,
            static_cast<int>(code_.size()));
      } else {  // kExprCatchAll
        eh_data_.AddCatchBlock(current_block_index_,
                               WasmEHData::kCatchAllTagIndex, 0, 0,
                               static_cast<int>(code_.size()));
      }

      break;
    }
    case kExprDelegate: {
      int32_t target_block_index = GetTargetBranch(instr.optional.depth + 1);
      DCHECK_LT(target_block_index, blocks_.size());
      int32_t delegated_try_block_index = WasmEHData::kDelegateToCallerIndex;
      if (target_block_index > 0) {
        const BlockData& target_block = blocks_[target_block_index];
        delegated_try_block_index = target_block.IsTry()
                                        ? target_block_index
                                        : target_block.parent_try_block_index_;
      }
      eh_data_.AddDelegatedBlock(delegated_try_block_index);
      EndBlock(kExprDelegate);
      break;
    }
    case kExprThrow: {
      EMIT_INSTR_HANDLER(s2s_Throw);
      EmitI32Const(instr.optional.index);

      // Exception arguments are popped from the stack (in reverse order!)
      const WasmTag& tag = module_->tags[instr.optional.index];
      const WasmTagSig* sig = tag.sig;
      DCHECK_GE(stack_.size(), sig->parameter_count());
      size_t stack_index = stack_.size() - sig->parameter_count();
      for (size_t index = 0; index < sig->parameter_count();
           index++, stack_index++) {
        ValueKind kind = sig->GetParam(index).kind();
        DCHECK(CheckEqualKind(kind, slots_[stack_[stack_index]].kind()));
        switch (kind) {
          case kI32:
          case kI64:
          case kF32:
          case kF64:
          case kS128: {
            EmitSlotOffset(slots_[stack_[stack_index]].slot_offset);
            break;
          }
          case kRef:
          case kRefNull: {
            uint32_t ref_index = slots_[stack_[stack_index]].ref_stack_index;
            Emit(&ref_index, sizeof(uint32_t));
            break;
          }
          default:
            UNREACHABLE();
        }
      }

      stack_.resize(stack_.size() - sig->parameter_count());
      eh_data_.RecordPotentialExceptionThrowingInstruction(instr.opcode,
                                                           CurrentCodePos());
      SetUnreachableMode();
      break;
    }
    case kExprRethrow: {
      EMIT_INSTR_HANDLER(s2s_Rethrow);
      int32_t target_branch_index = GetTargetBranch(instr.optional.depth);
      DCHECK(blocks_[target_branch_index].IsCatch() ||
             blocks_[target_branch_index].IsCatchAll());
      Emit(&target_branch_index, sizeof(int32_t));
      eh_data_.RecordPotentialExceptionThrowingInstruction(instr.opcode,
                                                           CurrentCodePos());
      SetUnreachableMode();
      break;
    }
    case kExprEnd: {
      // If there is an 'if...end' statement without an 'else' branch, create
      // a dummy else branch used to store results.
      if (blocks_[current_block_index_].IsIf()) {
        uint32_t if_block_index = current_block_index_;
        DCHECK(!blocks_[if_block_index].HasElseBranch());
        uint32_t params_count = ParamsCount(blocks_[if_block_index]);
        if (params_count > 0) {
          BeginElseBlock(if_block_index, true);
          EMIT_INSTR_HANDLER(s2s_Else);
          EmitIfElseBranchOffset();  // Jumps to the end of the 'else' block.
        }
      }

      if (EndBlock(kExprEnd) < 0) {
        Return();
      }
      break;
    }
    case kExprBr: {
      int32_t target_branch_index = GetTargetBranch(instr.optional.depth);
      StoreBlockParamsAndResultsIntoSlots(target_branch_index, kExprBr);

      EMIT_INSTR_HANDLER(s2s_Branch);
      EmitBranchOffset(instr.optional.depth);
      SetUnreachableMode();
      break;
    }
    case kExprBrIf: {
      int32_t target_branch_index = GetTargetBranch(instr.optional.depth);
      const WasmBytecodeGenerator::BlockData& target_block_data =
          blocks_[target_branch_index];
      if (HasVoidSignature(target_block_data)) {
        if (mode == kR2S) {
          EMIT_INSTR_HANDLER(r2s_BranchIf);
        } else {
          DCHECK_EQ(mode, kS2S);
          EMIT_INSTR_HANDLER(s2s_BranchIf);
          I32Pop();  // condition
        }
        // Emit code offset to branch to if the condition is true.
        EmitBranchOffset(instr.optional.depth);
      } else {
        if (mode == kR2S) {
          EMIT_INSTR_HANDLER(r2s_BranchIfWithParams);
        } else {
          DCHECK_EQ(mode, kS2S);
          EMIT_INSTR_HANDLER(s2s_BranchIfWithParams);
          I32Pop();  // condition
        }

        // Emit code offset to branch to if the condition is not true.
        const uint32_t if_false_code_offset = CurrentCodePos();
        Emit(&if_false_code_offset, sizeof(if_false_code_offset));

        StoreBlockParamsAndResultsIntoSlots(target_branch_index, kExprBrIf);

        EMIT_INSTR_HANDLER(s2s_Branch);
        EmitBranchOffset(instr.optional.depth);

        // Patch the 'if-false' offset with the correct jump offset.
        int32_t delta = CurrentCodePos() - if_false_code_offset;
        base::WriteUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(code_.data() + if_false_code_offset),
            delta);
      }
      break;
    }
    case kExprBrOnNull: {
      DCHECK_EQ(mode, kS2S);
      int32_t target_branch_index = GetTargetBranch(instr.optional.depth);
      const WasmBytecodeGenerator::BlockData& target_block_data =
          blocks_[target_branch_index];
      if (HasVoidSignature(target_block_data)) {
        EMIT_INSTR_HANDLER(s2s_BranchOnNull);
        ValueType value_type = RefPop();  // pop condition
        EmitRefValueType(value_type.raw_bit_field());
        // Remove nullability.
        if (value_type.kind() == kRefNull) {
          value_type = ValueType::Ref(value_type.heap_type());
        }
        RefPush(value_type);  // re-push condition value
        // Emit code offset to branch to if the condition is true.
        EmitBranchOffset(instr.optional.depth);
      } else {
        EMIT_INSTR_HANDLER(s2s_BranchOnNullWithParams);
        ValueType value_type = RefPop();  // pop condition
        EmitRefValueType(value_type.raw_bit_field());
        // Remove nullability.
        if (value_type.kind() == kRefNull) {
          value_type = ValueType::Ref(value_type.heap_type());
        }
        RefPush(value_type);  // re-push condition value

        // Emit code offset to branch to if the condition is not true.
        const uint32_t if_false_code_offset = CurrentCodePos();
        Emit(&if_false_code_offset, sizeof(if_false_code_offset));

        uint32_t stack_top = stack_.back();
        RefPop(false);  // Drop the null reference.

        StoreBlockParamsAndResultsIntoSlots(target_branch_index, kExprBrIf);

        EMIT_INSTR_HANDLER(s2s_Branch);
        EmitBranchOffset(instr.optional.depth);

        stack_.push_back(stack_top);  // re-push non-null ref on top of stack

        // Patch the 'if-false' offset with the correct jump offset.
        int32_t delta = CurrentCodePos() - if_false_code_offset;
        base::WriteUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(code_.data() + if_false_code_offset),
            delta);
      }
      break;
    }
    case kExprBrOnNonNull: {
      DCHECK_EQ(mode, kS2S);
      int32_t target_branch_index = GetTargetBranch(instr.optional.depth);
      const WasmBytecodeGenerator::BlockData& target_block_data =
          blocks_[target_branch_index];
      if (HasVoidSignature(target_block_data)) {
        EMIT_INSTR_HANDLER(s2s_BranchOnNonNull);
        ValueType value_type = RefPop();  // pop condition
        EmitRefValueType(value_type.raw_bit_field());
        RefPush(value_type);  // re-push condition value
        // Emit code offset to branch to if the condition is true.
        EmitBranchOffset(instr.optional.depth);

        RefPop(false);  // Drop the null reference.
      } else {
        EMIT_INSTR_HANDLER(s2s_BranchOnNonNullWithParams);
        ValueType value_type = RefPop();  // pop condition
        EmitRefValueType(value_type.raw_bit_field());
        RefPush(value_type);  // re-push condition value

        // Emit code offset to branch to if the condition is not true.
        const uint32_t if_false_code_offset = CurrentCodePos();
        Emit(&if_false_code_offset, sizeof(if_false_code_offset));

        StoreBlockParamsAndResultsIntoSlots(target_branch_index, kExprBrIf);

        EMIT_INSTR_HANDLER(s2s_Branch);
        EmitBranchOffset(instr.optional.depth);

        // Patch the 'if-false' offset with the correct jump offset.
        int32_t delta = CurrentCodePos() - if_false_code_offset;
        base::WriteUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(code_.data() + if_false_code_offset),
            delta);

        RefPop(false);  // Drop the null reference.
      }
      break;
    }
    case kExprBrOnCast: {
      const BranchOnCastData& br_on_cast_data = instr.optional.br_on_cast_data;
      const int32_t target_branch_index =
          GetTargetBranch(br_on_cast_data.label_depth);
      bool null_succeeds = br_on_cast_data.res_is_null;
      const ValueType target_type = ValueType::RefMaybeNull(
          ModuleTypeIndex({br_on_cast_data.target_type}),
          null_succeeds ? kNullable : kNonNullable);

      const ValueType obj_type = slots_[stack_.back()].value_type;
      DCHECK(obj_type.is_object_reference());

      // This logic ensures that code generation can assume that functions can
      // only be cast to function types, and data objects to data types.
      if (V8_UNLIKELY(
              TypeCheckAlwaysSucceeds(obj_type, target_type.heap_type()))) {
        StoreBlockParamsAndResultsIntoSlots(target_branch_index, kExprBrOnCast);
        // The branch will still not be taken on null if not {null_succeeds}.
        if (obj_type.is_nullable() && !null_succeeds) {
          EMIT_INSTR_HANDLER(s2s_BranchOnNull);
          RefPop();  // pop condition
          EmitRefValueType(obj_type.raw_bit_field());
          RefPush(target_type);  // re-push condition value with a new HeapType.
          EmitBranchOffset(br_on_cast_data.label_depth);
        } else {
          EMIT_INSTR_HANDLER(s2s_Branch);
          EmitBranchOffset(br_on_cast_data.label_depth);
        }
      } else if (V8_LIKELY(!TypeCheckAlwaysFails(
                     obj_type, target_type.heap_type(), null_succeeds))) {
        EMIT_INSTR_HANDLER(s2s_BranchOnCast);
        EmitI32Const(null_succeeds);
        HeapType br_on_cast_data_target_type(
            ModuleTypeIndex({br_on_cast_data.target_type}));
        EmitI32Const(br_on_cast_data_target_type.is_index()
                         ? br_on_cast_data_target_type.representation()
                         : target_type.heap_type().representation());
        ValueType value_type = RefPop();
        EmitRefValueType(value_type.raw_bit_field());
        RefPush(value_type);
        // Emit code offset to branch to if the condition is not true.
        const uint32_t no_branch_code_offset = CurrentCodePos();
        Emit(&no_branch_code_offset, sizeof(no_branch_code_offset));
        StoreBlockParamsAndResultsIntoSlots(target_branch_index, kExprBrOnCast);
        EMIT_INSTR_HANDLER(s2s_Branch);
        EmitBranchOffset(br_on_cast_data.label_depth);
        // Patch the 'if-false' offset with the correct jump offset.
        int32_t delta = CurrentCodePos() - no_branch_code_offset;
        base::WriteUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(code_.data() + no_branch_code_offset),
            delta);
      }
      break;
    }
    case kExprBrOnCastFail: {
      const BranchOnCastData& br_on_cast_data = instr.optional.br_on_cast_data;
      int32_t target_branch_index =
          GetTargetBranch(br_on_cast_data.label_depth);
      bool null_succeeds = br_on_cast_data.res_is_null;
      HeapType br_on_cast_data_target_type(
          ModuleTypeIndex({br_on_cast_data.target_type}));
      const ValueType target_type =
          ValueType::RefMaybeNull(br_on_cast_data_target_type,
                                  null_succeeds ? kNullable : kNonNullable);

      const ValueType obj_type = slots_[stack_.back()].value_type;
      DCHECK(obj_type.is_object_reference());

      // This logic ensures that code generation can assume that functions can
      // only be cast to function types, and data objects to data types.
      if (V8_UNLIKELY(TypeCheckAlwaysFails(obj_type, target_type.heap_type(),
                                           null_succeeds))) {
        StoreBlockParamsAndResultsIntoSlots(target_branch_index, kExprBrOnCast);
        EMIT_INSTR_HANDLER(s2s_Branch);
        EmitBranchOffset(br_on_cast_data.label_depth);
      } else if (V8_UNLIKELY(TypeCheckAlwaysSucceeds(
                     obj_type, target_type.heap_type()))) {
        // The branch can still be taken on null.
        if (obj_type.is_nullable() && !null_succeeds) {
          StoreBlockParamsAndResultsIntoSlots(target_branch_index,
                                              kExprBrOnCast);
          EMIT_INSTR_HANDLER(s2s_BranchOnNull);
          RefPop();  // pop condition
          EmitRefValueType(obj_type.raw_bit_field());
          RefPush(target_type);  // re-push condition value with a new HeapType.
          EmitBranchOffset(br_on_cast_data.label_depth);
        } else {
          // Fallthrough.
        }
      } else {
        EMIT_INSTR_HANDLER(s2s_BranchOnCastFail);
        EmitI32Const(null_succeeds);
        EmitI32Const(br_on_cast_data_target_type.is_index()
                         ? br_on_cast_data_target_type.representation()
                         : target_type.heap_type().representation());
        ValueType value_type = RefPop();
        EmitRefValueType(value_type.raw_bit_field());
        RefPush(value_type);
        // Emit code offset to branch to if the condition is not true.
        const uint32_t no_branch_code_offset = CurrentCodePos();
        Emit(&no_branch_code_offset, sizeof(no_branch_code_offset));
        StoreBlockParamsAndResultsIntoSlots(target_branch_index, kExprBrOnCast);
        EMIT_INSTR_HANDLER(s2s_Branch);
        EmitBranchOffset(br_on_cast_data.label_depth);
        // Patch the 'if-false' offset with the correct jump offset.
        int32_t delta = CurrentCodePos() - no_branch_code_offset;
        base::WriteUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(code_.data() + no_branch_code_offset),
            delta);
      }
      break;
    }
    case kExprBrTable: {
      if (mode == kR2S) {
        EMIT_INSTR_HANDLER(r2s_BrTable);
      } else {
        DCHECK_EQ(mode, kS2S);
        EMIT_INSTR_HANDLER(s2s_BrTable);
        I32Pop();  // branch label
      }

      // We emit the following bytecode for a br_table instruction:
      // s2s_BrTable handler id
      // (uint32) labels_count
      // (uint32) offset branch 0
      // (uint32) offset branch 1
      // ...
      // (uint32) offset branch labels_count - 1
      // (uint32) offset branch labels_count (default branch)
      // { Branch 0 slots }
      // { Branch 1 slots }
      // ...
      // { Branch labels_count slots }
      //
      // Where each {Branch i slots} contains the slots to execute a Branch
      // instruction:
      // { CopySlots for branch results, if present }
      // s2s_Branch handler id
      // (uint32) branch_offset (to be patched later)
      //
      const uint32_t labels_count = instr.optional.br_table.table_count;
      EmitI32Const(labels_count);
      uint32_t labels_offset_start = CurrentCodePos();
      for (uint32_t i = 0; i <= labels_count; i++) {
        // Here we don't know what will be the offset of this branch yet, so we
        // pass the current bytecode position as offset. This value will be
        // overwritten in the next loop.
        const uint32_t label_offset = CurrentCodePos();
        Emit(&label_offset, sizeof(label_offset));
      }
      for (uint32_t i = 0; i <= labels_count; i++) {
        uint32_t label =
            br_table_labels_[instr.optional.br_table.labels_index + i];
        int32_t target_branch_index = GetTargetBranch(label);
        uint32_t branch_code_start = CurrentCodePos();
        StoreBlockParamsAndResultsIntoSlots(target_branch_index, kExprBrTable);

        EMIT_INSTR_HANDLER(s2s_Branch);
        EmitBranchTableOffset(label, CurrentCodePos());

        // Patch the branch offset with the correct jump offset.
        uint32_t label_offset = labels_offset_start + i * sizeof(uint32_t);
        int32_t delta = branch_code_start - label_offset;
        base::WriteUnalignedValue<uint32_t>(
            reinterpret_cast<Address>(code_.data() + label_offset), delta);
      }
      SetUnreachableMode();
      break;
    }
    case kExprReturn: {
      Return();
      SetUnreachableMode();
      break;
    }
    case kExprCallFunction:
    case kExprReturnCall: {
      uint32_t function_index = instr.optional.index;
      const FunctionSig* sig = GetFunctionSignature(function_index);

      // Layout of a frame:
      // ------------------
      // stack slot #N-1 ‾\
      // ...              |
      // stack slot #0   _/
      // local #L-1      ‾\
      // ...              |
      // local #0        _/
      // const #C-1      ‾\
      // ...              |
      // const #0        _/
      // param #P-1      ‾\
      // ...              |
      // param #0        _/
      // return #R-1     ‾\
      // ...              |
      // return #0       _/
      // ------------------

      const bool is_imported = (module_->functions[function_index].imported);
      const bool is_tail_call = (instr.opcode == kExprReturnCall);
      uint32_t slot_offset = GetStackFrameSize() * kSlotSize;
      uint32_t ref_stack_fp_offset = ref_slots_count_;

      std::vector<uint32_t> rets_slots;
      rets_slots.resize(sig->return_count());
      for (size_t index = 0; index < sig->return_count(); index++) {
        rets_slots[index] = is_tail_call ? static_cast<uint32_t>(index)
                                         : CreateSlot(sig->GetReturn(index));
      }

      InitSlotsForFunctionArgs(sig, false);

      if (is_imported) {
        if (is_tail_call) {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_ReturnCallImportedFunction, instr.pc);
          EmitSlotOffset(WasmBytecode::RetsSizeInSlots(sig) * kSlotSize);
          EmitSlotOffset(WasmBytecode::ArgsSizeInSlots(sig) * kSlotSize);
          EmitI32Const(WasmBytecode::RefRetsCount(sig));
          EmitI32Const(WasmBytecode::RefArgsCount(sig));
        } else {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_CallImportedFunction, instr.pc);
        }
      } else {
        if (is_tail_call) {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_ReturnCall, instr.pc);
          EmitSlotOffset(WasmBytecode::RetsSizeInSlots(sig) * kSlotSize);
          EmitSlotOffset(WasmBytecode::ArgsSizeInSlots(sig) * kSlotSize);
          EmitI32Const(WasmBytecode::RefRetsCount(sig));
          EmitI32Const(WasmBytecode::RefArgsCount(sig));
        } else {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_CallFunction, instr.pc);
        }
      }
      EmitI32Const(function_index);
      EmitStackIndex(static_cast<uint32_t>(stack_.size()));
      EmitSlotOffset(slot_offset);
      EmitRefStackIndex(ref_stack_fp_offset);

      // Function arguments are popped from the stack.
      for (size_t index = sig->parameter_count(); index > 0; index--) {
        Pop(sig->GetParam(index - 1).kind(), false);
      }

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
      if (v8_flags.trace_drumbrake_execution) {
        EmitSlotOffset(rets_slots.empty()
                           ? 0
                           : slots_[rets_slots[0]].slot_offset * kSlotSize);
      }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

      if (!is_tail_call) {
        eh_data_.RecordPotentialExceptionThrowingInstruction(instr.opcode,
                                                             CurrentCodePos());
      }

      // Function results are pushed to the stack.
      for (size_t index = 0; index < sig->return_count(); index++) {
        const ValueType value_type = sig->GetReturn(index);
        const ValueKind kind = value_type.kind();
        switch (kind) {
          case kI32:
          case kI64:
          case kF32:
          case kF64:
          case kS128:
          case kRef:
          case kRefNull:
            PushSlot(rets_slots[index]);
            SetSlotType(stack_top_index(), value_type);
            break;
          default:
            UNREACHABLE();
        }
      }

      // If this is a tail call, the following instructions in this block are
      // unreachable.
      if (is_tail_call) {
        SetUnreachableMode();
      }

      return RegMode::kNoReg;
    }
    case kExprCallIndirect:
    case kExprReturnCallIndirect: {
      const FunctionSig* sig = module_->signature(
          ModuleTypeIndex({instr.optional.indirect_call.sig_index}));

      const bool is_tail_call = (instr.opcode == kExprReturnCallIndirect);
      uint32_t slot_offset = GetStackFrameSize() * kSlotSize;
      uint32_t ref_stack_fp_offset = ref_slots_count_;

      // Reserve space for return values.
      std::vector<uint32_t> rets_slots;
      rets_slots.resize(sig->return_count());
      for (size_t index = 0; index < sig->return_count(); index++) {
        rets_slots[index] = is_tail_call ? static_cast<uint32_t>(index)
                                         : CreateSlot(sig->GetReturn(index));
      }

      InitSlotsForFunctionArgs(sig, true);

      bool is_table64 =
          module_->tables[instr.optional.indirect_call.table_index]
              .is_table64();

      if (is_tail_call) {
        EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_ReturnCallIndirect,
                                         s2s_ReturnCallIndirect64, is_table64,
                                         instr.pc);
        EmitSlotOffset(WasmBytecode::RetsSizeInSlots(sig) * kSlotSize);
        EmitSlotOffset(WasmBytecode::ArgsSizeInSlots(sig) * kSlotSize);
        EmitI32Const(WasmBytecode::RefRetsCount(sig));
        EmitI32Const(WasmBytecode::RefArgsCount(sig));
      } else {
        EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_CallIndirect, s2s_CallIndirect64,
                                         is_table64, instr.pc);
      }

      // Pops the index of the function to call.
      is_table64 ? I64Pop() : I32Pop();

      EmitI32Const(instr.optional.indirect_call.table_index);
      EmitI32Const(instr.optional.indirect_call.sig_index);

      EmitStackIndex(stack_size());
      EmitSlotOffset(slot_offset);
      EmitRefStackIndex(ref_stack_fp_offset);

      // Function arguments are popped from the stack.
      for (size_t index = sig->parameter_count(); index > 0; index--) {
        Pop(sig->GetParam(index - 1).kind(), false);
      }

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
      if (v8_flags.trace_drumbrake_execution) {
        EmitSlotOffset(rets_slots.empty()
                           ? 0
                           : slots_[rets_slots[0]].slot_offset * kSlotSize);
      }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

      if (!is_tail_call) {
        eh_data_.RecordPotentialExceptionThrowingInstruction(instr.opcode,
                                                             CurrentCodePos());
      }

      // Function result is pushed to the stack.
      for (size_t index = 0; index < sig->return_count(); index++) {
        ValueType value_type = sig->GetReturn(index);
        switch (value_type.kind()) {
          case kI32:
          case kI64:
          case kF32:
          case kF64:
          case kS128:
          case kRef:
          case kRefNull:
            PushSlot(rets_slots[index]);
            SetSlotType(stack_top_index(), value_type);
            break;
          default:
            UNREACHABLE();
        }
      }

      // If this is a tail call, the following instructions in this block are
      // unreachable.
      if (is_tail_call) {
        SetUnreachableMode();
      }

      return RegMode::kNoReg;
    }

    case kExprCallRef:
    case kExprReturnCallRef: {
      const FunctionSig* sig =
          module_->signature(ModuleTypeIndex({instr.optional.index}));
      const bool is_tail_call = (instr.opcode == kExprReturnCallRef);
      uint32_t slot_offset = GetStackFrameSize() * kSlotSize;
      uint32_t ref_stack_fp_offset = ref_slots_count_;

      // Reserve space for return values.
      std::vector<uint32_t> rets_slots;
      rets_slots.resize(sig->return_count());
      for (size_t index = 0; index < sig->return_count(); index++) {
        rets_slots[index] = is_tail_call ? static_cast<uint32_t>(index)
                                         : CreateSlot(sig->GetReturn(index));
      }

      InitSlotsForFunctionArgs(sig, true);

      if (is_tail_call) {
        EMIT_INSTR_HANDLER_WITH_PC(s2s_ReturnCallRef, instr.pc);
        EmitSlotOffset(WasmBytecode::RetsSizeInSlots(sig) * kSlotSize);
        EmitSlotOffset(WasmBytecode::ArgsSizeInSlots(sig) * kSlotSize);
        EmitI32Const(WasmBytecode::RefRetsCount(sig));
        EmitI32Const(WasmBytecode::RefArgsCount(sig));
      } else {
        EMIT_INSTR_HANDLER_WITH_PC(s2s_CallRef, instr.pc);
      }

      // Pops the function to call.
      RefPop();

      EmitI32Const(instr.optional.index);  // Signature index.
      EmitStackIndex(stack_size());
      EmitSlotOffset(slot_offset);
      EmitRefStackIndex(ref_stack_fp_offset);

      // Function arguments are popped from the stack.
      for (size_t index = sig->parameter_count(); index > 0; index--) {
        Pop(sig->GetParam(index - 1).kind(), false);
      }

#ifdef V8_ENABLE_DRUMBRAKE_TRACING
      if (v8_flags.trace_drumbrake_execution) {
        EmitSlotOffset(rets_slots.empty()
                           ? 0
                           : slots_[rets_slots[0]].slot_offset * kSlotSize);
      }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

      if (!is_tail_call) {
        eh_data_.RecordPotentialExceptionThrowingInstruction(instr.opcode,
                                                             CurrentCodePos());
      }

      // Function result is pushed to the stack.
      for (size_t index = 0; index < sig->return_count(); index++) {
        const ValueType value_type = sig->GetReturn(index);
        const ValueKind kind = value_type.kind();
        switch (kind) {
          case kI32:
          case kI64:
          case kF32:
          case kF64:
          case kS128:
          case kRef:
          case kRefNull:
            PushSlot(rets_slots[index]);
            SetSlotType(stack_top_index(), value_type);
            break;
          default:
            UNREACHABLE();
        }
      }

      // If this is a tail call, the following instructions in this block are
      // unreachable.
      if (is_tail_call) {
        SetUnreachableMode();
      }

      return RegMode::kNoReg;
    }

    case kExprDrop: {
      switch (top_stack_slot_type) {
        case kI32:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_I32Drop);
              return RegMode::kNoReg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_I32Drop);
              I32Pop();
              return RegMode::kNoReg;
          }
          break;
        case kI64:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_I64Drop);
              return RegMode::kNoReg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_I64Drop);
              I64Pop();
              return RegMode::kNoReg;
          }
          break;
        case kF32:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_F32Drop);
              return RegMode::kNoReg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_F32Drop);
              F32Pop();
              return RegMode::kNoReg;
          }
          break;
        case kF64:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_F64Drop);
              return RegMode::kNoReg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_F64Drop);
              F64Pop();
              return RegMode::kNoReg;
          }
          break;
        case kS128:
          switch (mode) {
            case kR2R:
            case kR2S:
            case kS2R:
              UNREACHABLE();
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_S128Drop);
              S128Pop();
              return RegMode::kNoReg;
          }
          break;
        case kRef:
        case kRefNull:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_RefDrop);
              return RegMode::kNoReg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_RefDrop);
              RefPop();
              return RegMode::kNoReg;
          }
          break;
        default:
          UNREACHABLE();
      }
      break;
    }
    case kExprSelect:
    case kExprSelectWithType: {
      DCHECK_GE(stack_size(), 2);
      switch (slots_[stack_[stack_size() - 2]].kind()) {
        case kI32:
          switch (mode) {
            case kR2R:
              EMIT_INSTR_HANDLER(r2r_I32Select);
              I32Pop();  // val2
              I32Pop();  // val1
              return RegMode::kI32Reg;
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_I32Select);
              I32Pop();   // val2
              I32Pop();   // val1
              I32Push();  // result
              return RegMode::kNoReg;
            case kS2R:
              EMIT_INSTR_HANDLER(s2r_I32Select);
              I32Pop();  // condition
              I32Pop();  // val2
              I32Pop();  // val1
              return RegMode::kI32Reg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_I32Select);
              I32Pop();   // condition
              I32Pop();   // val2
              I32Pop();   // val1
              I32Push();  // result
              return RegMode::kNoReg;
          }
          break;
        case kI64:
          switch (mode) {
            case kR2R:
              EMIT_INSTR_HANDLER(r2r_I64Select);
              I64Pop();  // val2
              I64Pop();  // val1
              return RegMode::kI64Reg;
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_I64Select);
              I64Pop();   // val2
              I64Pop();   // val1
              I64Push();  // result
              return RegMode::kNoReg;
            case kS2R:
              EMIT_INSTR_HANDLER(s2r_I64Select);
              I32Pop();  // condition
              I64Pop();  // val2
              I64Pop();  // val1
              return RegMode::kI64Reg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_I64Select);
              I32Pop();  // condition
              I64Pop();
              I64Pop();
              I64Push();
              return RegMode::kNoReg;
          }
          break;
        case kF32:
          switch (mode) {
            case kR2R:
              EMIT_INSTR_HANDLER(r2r_F32Select);
              F32Pop();  // val2
              F32Pop();  // val1
              return RegMode::kF32Reg;
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_F32Select);
              F32Pop();   // val2
              F32Pop();   // val1
              F32Push();  // result
              return RegMode::kNoReg;
            case kS2R:
              EMIT_INSTR_HANDLER(s2r_F32Select);
              I32Pop();  // condition
              F32Pop();  // val2
              F32Pop();  // val1
              return RegMode::kF32Reg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_F32Select);
              I32Pop();  // condition
              F32Pop();
              F32Pop();
              F32Push();
              return RegMode::kNoReg;
          }
          break;
        case kF64:
          switch (mode) {
            case kR2R:
              EMIT_INSTR_HANDLER(r2r_F64Select);
              F64Pop();  // val2
              F64Pop();  // val1
              return RegMode::kF64Reg;
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_F64Select);
              F64Pop();   // val2
              F64Pop();   // val1
              F64Push();  // result
              return RegMode::kNoReg;
            case kS2R:
              EMIT_INSTR_HANDLER(s2r_F64Select);
              I32Pop();  // condition
              F64Pop();  // val2
              F64Pop();  // val1
              return RegMode::kF64Reg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_F64Select);
              I32Pop();  // condition
              F64Pop();
              F64Pop();
              F64Push();
              return RegMode::kNoReg;
          }
          break;
        case kS128:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_S128Select);
              S128Pop();
              S128Pop();
              S128Push();
              return RegMode::kNoReg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_S128Select);
              I32Pop();  // condition
              S128Pop();
              S128Pop();
              S128Push();
              return RegMode::kNoReg;
          }
          break;
        case kRef:
        case kRefNull:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S: {
              EMIT_INSTR_HANDLER(r2s_RefSelect);
              RefPop();                   // val2
              ValueType type = RefPop();  // val1
              RefPush(type);              // result
              return RegMode::kNoReg;
            }
            case kS2S: {
              EMIT_INSTR_HANDLER(s2s_RefSelect);
              I32Pop();  // condition
              RefPop();
              ValueType type = RefPop();
              RefPush(type);
              return RegMode::kNoReg;
            }
          }
          break;
        default:
          UNREACHABLE();
      }
      break;
    }

    case kExprLocalGet: {
      switch (slots_[stack_[instr.optional.index]].kind()) {
        case kI32:
        case kI64:
        case kF32:
        case kF64:
        case kS128:
        case kRef:
        case kRefNull:
          switch (mode) {
            case kR2R:
            case kR2S:
            case kS2R:
              UNREACHABLE();
            case kS2S:
              PushCopySlot(instr.optional.index);
              return RegMode::kNoReg;
          }
          break;
        default:
          UNREACHABLE();
      }
      break;
    }
    case kExprLocalSet: {
      DCHECK_LE(instr.optional.index, stack_size());
      // Validation ensures that the target slot type must be the same as the
      // stack top slot type.
      const ValueType value_type =
          slots_[stack_[instr.optional.index]].value_type;
      const ValueKind kind = value_type.kind();
      DCHECK(CheckEqualKind(kind, top_stack_slot_type));
      switch (kind) {
        case kI32:
        case kI64:
        case kF32:
        case kF64:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S:
              CopyToSlotAndPop(value_type, instr.optional.index, false, true);
              return RegMode::kNoReg;
            case kS2S:
              CopyToSlotAndPop(value_type, instr.optional.index, false, false);
              return RegMode::kNoReg;
          }
          break;
        case kS128:
          switch (mode) {
            case kR2R:
            case kR2S:
            case kS2R:
              UNREACHABLE();
            case kS2S:
              CopyToSlotAndPop(value_type, instr.optional.index, false, false);
              return RegMode::kNoReg;
          }
          break;
        case kRef:
        case kRefNull:
          switch (mode) {
            case kR2R:
            case kR2S:
            case kS2R:
              UNREACHABLE();
            case kS2S:
              CopyToSlotAndPop(slots_[stack_.back()].value_type,
                               instr.optional.index, false, false);
              return RegMode::kNoReg;
          }
          break;
        default:
          UNREACHABLE();
      }
      break;
    }
    case kExprLocalTee: {
      DCHECK_LE(instr.optional.index, stack_size());
      // Validation ensures that the target slot type must be the same as the
      // stack top slot type.
      const ValueType value_type =
          slots_[stack_[instr.optional.index]].value_type;
      const ValueKind kind = value_type.kind();
      DCHECK(CheckEqualKind(kind, top_stack_slot_type));
      switch (kind) {
        case kI32:
        case kI64:
        case kF32:
        case kF64:
          switch (mode) {
            case kR2R:
              CopyToSlotAndPop(value_type, instr.optional.index, true, true);
              return GetRegMode(value_type.kind());
            case kR2S:
              UNREACHABLE();
            case kS2R:
              UNREACHABLE();
            case kS2S:
              CopyToSlotAndPop(value_type, instr.optional.index, true, false);
              return RegMode::kNoReg;
          }
          break;
        case kS128:
          switch (mode) {
            case kR2R:
            case kR2S:
            case kS2R:
              UNREACHABLE();
            case kS2S:
              CopyToSlotAndPop(value_type, instr.optional.index, true, false);
              return RegMode::kNoReg;
          }
          break;
        case kRef:
        case kRefNull:
          switch (mode) {
            case kR2R:
            case kR2S:
            case kS2R:
              UNREACHABLE();
            case kS2S:
              CopyToSlotAndPop(slots_[stack_.back()].value_type,
                               instr.optional.index, true, false);
              return RegMode::kNoReg;
          }
          break;
        default:
          UNREACHABLE();
      }
      break;
    }
    case kExprGlobalGet: {
      switch (GetGlobalType(instr.optional.index)) {
        case kI32:
          switch (mode) {
            case kR2R:
            case kR2S:
              UNREACHABLE();
            case kS2R:
              EMIT_INSTR_HANDLER(s2r_I32GlobalGet);
              EmitGlobalIndex(instr.optional.index);
              return RegMode::kI32Reg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_I32GlobalGet);
              EmitGlobalIndex(instr.optional.index);
              I32Push();
              return RegMode::kNoReg;
          }
          break;
        case kI64:
          switch (mode) {
            case kR2R:
            case kR2S:
              UNREACHABLE();
            case kS2R:
              EMIT_INSTR_HANDLER(s2r_I64GlobalGet);
              EmitGlobalIndex(instr.optional.index);
              return RegMode::kI64Reg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_I64GlobalGet);
              EmitGlobalIndex(instr.optional.index);
              I64Push();
              return RegMode::kNoReg;
          }
          break;
        case kF32:
          switch (mode) {
            case kR2R:
            case kR2S:
              UNREACHABLE();
            case kS2R:
              EMIT_INSTR_HANDLER(s2r_F32GlobalGet);
              EmitGlobalIndex(instr.optional.index);
              return RegMode::kF32Reg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_F32GlobalGet);
              EmitGlobalIndex(instr.optional.index);
              F32Push();
              return RegMode::kNoReg;
          }
          break;
        case kF64:
          switch (mode) {
            case kR2R:
            case kR2S:
              UNREACHABLE();
            case kS2R:
              EMIT_INSTR_HANDLER(s2r_F64GlobalGet);
              EmitGlobalIndex(instr.optional.index);
              return RegMode::kF64Reg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_F64GlobalGet);
              EmitGlobalIndex(instr.optional.index);
              F64Push();
              return RegMode::kNoReg;
          }
          break;
        case kS128:
          switch (mode) {
            case kR2R:
            case kR2S:
            case kS2R:
              UNREACHABLE();
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_S128GlobalGet);
              EmitGlobalIndex(instr.optional.index);
              S128Push();
              return RegMode::kNoReg;
          }
          break;
        case kRef:
        case kRefNull:
          switch (mode) {
            case kR2R:
            case kR2S:
            case kS2R:
              UNREACHABLE();
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_RefGlobalGet);
              EmitGlobalIndex(instr.optional.index);
              RefPush(module_->globals[instr.optional.index].type);
              return RegMode::kNoReg;
          }
          break;
        default:
          UNREACHABLE();
      }
      break;
    }
    case kExprGlobalSet: {
      switch (top_stack_slot_type) {
        case kI32:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_I32GlobalSet);
              EmitGlobalIndex(instr.optional.index);
              return RegMode::kNoReg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_I32GlobalSet);
              EmitGlobalIndex(instr.optional.index);
              I32Pop();
              return RegMode::kNoReg;
          }
          break;
        case kI64:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_I64GlobalSet);
              EmitGlobalIndex(instr.optional.index);
              return RegMode::kNoReg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_I64GlobalSet);
              EmitGlobalIndex(instr.optional.index);
              I64Pop();
              return RegMode::kNoReg;
          }
          break;
        case kF32:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_F32GlobalSet);
              EmitGlobalIndex(instr.optional.index);
              return RegMode::kNoReg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_F32GlobalSet);
              EmitGlobalIndex(instr.optional.index);
              F32Pop();
              return RegMode::kNoReg;
          }
          break;
        case kF64:
          switch (mode) {
            case kR2R:
            case kS2R:
              UNREACHABLE();
            case kR2S:
              EMIT_INSTR_HANDLER(r2s_F64GlobalSet);
              EmitGlobalIndex(instr.optional.index);
              return RegMode::kNoReg;
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_F64GlobalSet);
              EmitGlobalIndex(instr.optional.index);
              F64Pop();
              return RegMode::kNoReg;
          }
          break;
        case kS128:
          switch (mode) {
            case kR2R:
            case kR2S:
            case kS2R:
              UNREACHABLE();
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_S128GlobalSet);
              EmitGlobalIndex(instr.optional.index);
              S128Pop();
              return RegMode::kNoReg;
          }
          break;
        case kRef:
        case kRefNull:
          switch (mode) {
            case kR2R:
            case kR2S:
            case kS2R:
              UNREACHABLE();
            case kS2S:
              EMIT_INSTR_HANDLER(s2s_RefGlobalSet);
              EmitGlobalIndex(instr.optional.index);
              RefPop();
              return RegMode::kNoReg;
          }
          break;
        default:
          UNREACHABLE();
      }
      break;
    }

    case kExprTableGet: {
      bool is_table64 = module_->tables[instr.optional.index].is_table64();
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_TableGet, s2s_Table64Get, is_table64,
                                       instr.pc);
      EmitI32Const(instr.optional.index);
      is_table64 ? I64Pop() : I32Pop();
      RefPush(module_->tables[instr.optional.index].type);
      break;
    }

    case kExprTableSet: {
      bool is_table64 = module_->tables[instr.optional.index].is_table64();
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_TableSet, s2s_Table64Set, is_table64,
                                       instr.pc);
      EmitI32Const(instr.optional.index);
      RefPop();
      is_table64 ? I64Pop() : I32Pop();
      break;
    }

#define LOAD_CASE(name, ctype, mtype, rep, type)                         \
  case kExpr##name: {                                                    \
    switch (mode) {                                                      \
      case kR2R:                                                         \
        EMIT_MEM64_INSTR_HANDLER_WITH_PC(r2r_##name, r2r_##name##_Idx64, \
                                         is_memory64_, instr.pc);        \
        EmitMemoryOffset(instr.optional.offset);                         \
        return RegMode::k##type##Reg;                                    \
      case kR2S:                                                         \
        EMIT_MEM64_INSTR_HANDLER_WITH_PC(r2s_##name, r2s_##name##_Idx64, \
                                         is_memory64_, instr.pc);        \
        EmitMemoryOffset(instr.optional.offset);                         \
        type##Push();                                                    \
        return RegMode::kNoReg;                                          \
      case kS2R:                                                         \
        EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2r_##name, s2r_##name##_Idx64, \
                                         is_memory64_, instr.pc);        \
        EmitMemoryOffset(instr.optional.offset);                         \
        MemIndexPop();                                                   \
        return RegMode::k##type##Reg;                                    \
      case kS2S:                                                         \
        EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_##name, s2s_##name##_Idx64, \
                                         is_memory64_, instr.pc);        \
        EmitMemoryOffset(instr.optional.offset);                         \
        MemIndexPop();                                                   \
        type##Push();                                                    \
        return RegMode::kNoReg;                                          \
    }                                                                    \
    break;                                                               \
  }
      LOAD_CASE(I32LoadMem8S, int32_t, int8_t, kWord8, I32);
      LOAD_CASE(I32LoadMem8U, int32_t, uint8_t, kWord8, I32);
      LOAD_CASE(I32LoadMem16S, int32_t, int16_t, kWord16, I32);
      LOAD_CASE(I32LoadMem16U, int32_t, uint16_t, kWord16, I32);
      LOAD_CASE(I64LoadMem8S, int64_t, int8_t, kWord8, I64);
      LOAD_CASE(I64LoadMem8U, int64_t, uint8_t, kWord16, I64);
      LOAD_CASE(I64LoadMem16S, int64_t, int16_t, kWord16, I64);
      LOAD_CASE(I64LoadMem16U, int64_t, uint16_t, kWord16, I64);
      LOAD_CASE(I64LoadMem32S, int64_t, int32_t, kWord32, I64);
      LOAD_CASE(I64LoadMem32U, int64_t, uint32_t, kWord32, I64);
      LOAD_CASE(I32LoadMem, int32_t, int32_t, kWord32, I32);
      LOAD_CASE(I64LoadMem, int64_t, int64_t, kWord64, I64);
      LOAD_CASE(F32LoadMem, Float32, uint32_t, kFloat32, F32);
      LOAD_CASE(F64LoadMem, Float64, uint64_t, kFloat64, F64);
#undef LOAD_CASE

#define STORE_CASE(name, ctype, mtype, rep, type)                        \
  case kExpr##name: {                                                    \
    switch (mode) {                                                      \
      case kR2R:                                                         \
      case kS2R:                                                         \
        UNREACHABLE();                                                   \
        break;                                                           \
      case kR2S:                                                         \
        EMIT_MEM64_INSTR_HANDLER_WITH_PC(r2s_##name, r2s_##name##_Idx64, \
                                         is_memory64_, instr.pc);        \
        EmitMemoryOffset(instr.optional.offset);                         \
        MemIndexPop();                                                   \
        return RegMode::kNoReg;                                          \
      case kS2S:                                                         \
        EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_##name, s2s_##name##_Idx64, \
                                         is_memory64_, instr.pc);        \
        type##Pop();                                                     \
        EmitMemoryOffset(instr.optional.offset);                         \
        MemIndexPop();                                                   \
        return RegMode::kNoReg;                                          \
    }                                                                    \
    break;                                                               \
  }
      STORE_CASE(I32StoreMem8, int32_t, int8_t, kWord8, I32);
      STORE_CASE(I32StoreMem16, int32_t, int16_t, kWord16, I32);
      STORE_CASE(I64StoreMem8, int64_t, int8_t, kWord8, I64);
      STORE_CASE(I64StoreMem16, int64_t, int16_t, kWord16, I64);
      STORE_CASE(I64StoreMem32, int64_t, int32_t, kWord32, I64);
      STORE_CASE(I32StoreMem, int32_t, int32_t, kWord32, I32);
      STORE_CASE(I64StoreMem, int64_t, int64_t, kWord64, I64);
      STORE_CASE(F32StoreMem, Float32, uint32_t, kFloat32, F32);
      STORE_CASE(F64StoreMem, Float64, uint64_t, kFloat64, F64);
#undef STORE_CASE

    case kExprMemoryGrow: {
      EMIT_MEM64_INSTR_HANDLER(s2s_MemoryGrow, s2s_Memory64Grow, is_memory64_);
      MemIndexPop();
      MemIndexPush();
      break;
    }
    case kExprMemorySize:
      EMIT_MEM64_INSTR_HANDLER(s2s_MemorySize, s2s_Memory64Size, is_memory64_);
      MemIndexPush();
      break;

    case kExprI32Const: {
      switch (mode) {
        case kR2R:
        case kR2S:
        case kS2R:
          UNREACHABLE();
        case kS2S:
          PushConstSlot<int32_t>(instr.optional.i32);
          return RegMode::kNoReg;
      }
      break;
    }
    case kExprI64Const: {
      switch (mode) {
        case kR2R:
        case kR2S:
        case kS2R:
          UNREACHABLE();
        case kS2S:
          PushConstSlot<int64_t>(instr.optional.i64);
          return RegMode::kNoReg;
      }
      break;
    }
    case kExprF32Const: {
      switch (mode) {
        case kR2R:
        case kR2S:
        case kS2R:
          UNREACHABLE();
        case kS2S:
          PushConstSlot<float>(instr.optional.f32);
          return RegMode::kNoReg;
      }
      break;
    }
    case kExprF64Const: {
      switch (mode) {
        case kR2R:
        case kR2S:
        case kS2R:
          UNREACHABLE();
        case kS2S:
          PushConstSlot<double>(instr.optional.f64);
          return RegMode::kNoReg;
      }
      break;
    }

#define EXECUTE_BINOP(name, ctype, reg, op, type) \
  case kExpr##name: {                             \
    switch (mode) {                               \
      case kR2R:                                  \
        EMIT_INSTR_HANDLER(r2r_##name);           \
        type##Pop();                              \
        return RegMode::kI32Reg;                  \
      case kR2S:                                  \
        EMIT_INSTR_HANDLER(r2s_##name);           \
        type##Pop();                              \
        I32Push();                                \
        return RegMode::kNoReg;                   \
      case kS2R:                                  \
        EMIT_INSTR_HANDLER(s2r_##name);           \
        type##Pop();                              \
        type##Pop();                              \
        return RegMode::kI32Reg;                  \
      case kS2S:                                  \
        EMIT_INSTR_HANDLER(s2s_##name);           \
        type##Pop();                              \
        type##Pop();                              \
        I32Push();                                \
        return RegMode::kNoReg;                   \
    }                                             \
    break;                                        \
  }
      FOREACH_COMPARISON_BINOP(EXECUTE_BINOP)
#undef EXECUTE_BINOP

#define EXECUTE_BINOP(name, ctype, reg, op, type) \
  case kExpr##name: {                             \
    switch (mode) {                               \
      case kR2R:                                  \
        EMIT_INSTR_HANDLER(r2r_##name);           \
        type##Pop();                              \
        return RegMode::k##type##Reg;             \
      case kR2S:                                  \
        EMIT_INSTR_HANDLER(r2s_##name);           \
        type##Pop();                              \
        type##Push();                             \
        return RegMode::kNoReg;                   \
      case kS2R:                                  \
        EMIT_INSTR_HANDLER(s2r_##name);           \
        type##Pop();                              \
        type##Pop();                              \
        return RegMode::k##type##Reg;             \
      case kS2S:                                  \
        EMIT_INSTR_HANDLER(s2s_##name);           \
        type##Pop();                              \
        type##Pop();                              \
        type##Push();                             \
        return RegMode::kNoReg;                   \
    }                                             \
    break;                                        \
  }
      FOREACH_ARITHMETIC_BINOP(EXECUTE_BINOP)
      FOREACH_MORE_BINOP(EXECUTE_BINOP)
#undef EXECUTE_BINOP

#define EXECUTE_BINOP(name, ctype, reg, op, type)         \
  case kExpr##name: {                                     \
    switch (mode) {                                       \
      case kR2R:                                          \
        EMIT_INSTR_HANDLER_WITH_PC(r2r_##name, instr.pc); \
        type##Pop();                                      \
        return RegMode::k##type##Reg;                     \
      case kR2S:                                          \
        EMIT_INSTR_HANDLER_WITH_PC(r2s_##name, instr.pc); \
        type##Pop();                                      \
        type##Push();                                     \
        return RegMode::kNoReg;                           \
      case kS2R:                                          \
        EMIT_INSTR_HANDLER_WITH_PC(s2r_##name, instr.pc); \
        type##Pop();                                      \
        type##Pop();                                      \
        return RegMode::k##type##Reg;                     \
      case kS2S:                                          \
        EMIT_INSTR_HANDLER_WITH_PC(s2s_##name, instr.pc); \
        type##Pop();                                      \
        type##Pop();                                      \
        type##Push();                                     \
        return RegMode::kNoReg;                           \
    }                                                     \
    break;                                                \
  }
      FOREACH_TRAPPING_BINOP(EXECUTE_BINOP)
#undef EXECUTE_BINOP

#define EXECUTE_UNOP(name, ctype, reg, op, type) \
  case kExpr##name: {                            \
    switch (mode) {                              \
      case kR2R:                                 \
        EMIT_INSTR_HANDLER(r2r_##name);          \
        return RegMode::k##type##Reg;            \
      case kR2S:                                 \
        EMIT_INSTR_HANDLER(r2s_##name);          \
        type##Push();                            \
        return RegMode::kNoReg;                  \
      case kS2R:                                 \
        EMIT_INSTR_HANDLER(s2r_##name);          \
        type##Pop();                             \
        return RegMode::k##type##Reg;            \
      case kS2S:                                 \
        EMIT_INSTR_HANDLER(s2s_##name);          \
        type##Pop();                             \
        type##Push();                            \
        return RegMode::kNoReg;                  \
    }                                            \
    break;                                       \
  }
      FOREACH_SIMPLE_UNOP(EXECUTE_UNOP)
#undef EXECUTE_UNOP

#define EXECUTE_UNOP(name, from_ctype, from_type, from_reg, to_ctype, to_type, \
                     to_reg)                                                   \
  case kExpr##name: {                                                          \
    switch (mode) {                                                            \
      case kR2R:                                                               \
        EMIT_INSTR_HANDLER(r2r_##name);                                        \
        return RegMode::k##to_type##Reg;                                       \
      case kR2S:                                                               \
        EMIT_INSTR_HANDLER(r2s_##name);                                        \
        to_type##Push();                                                       \
        return RegMode::kNoReg;                                                \
      case kS2R:                                                               \
        EMIT_INSTR_HANDLER(s2r_##name);                                        \
        from_type##Pop();                                                      \
        return RegMode::k##to_type##Reg;                                       \
      case kS2S:                                                               \
        EMIT_INSTR_HANDLER(s2s_##name);                                        \
        from_type##Pop();                                                      \
        to_type##Push();                                                       \
        return RegMode::kNoReg;                                                \
    }                                                                          \
    break;                                                                     \
  }
      FOREACH_ADDITIONAL_CONVERT_UNOP(EXECUTE_UNOP)
      FOREACH_OTHER_CONVERT_UNOP(EXECUTE_UNOP)
      FOREACH_REINTERPRET_UNOP(EXECUTE_UNOP)
      FOREACH_TRUNCSAT_UNOP(EXECUTE_UNOP)
#undef EXECUTE_UNOP

#define EXECUTE_UNOP(name, from_ctype, from_type, from_reg, to_ctype, to_type, \
                     to_reg)                                                   \
  case kExpr##name: {                                                          \
    switch (mode) {                                                            \
      case kR2R:                                                               \
        EMIT_INSTR_HANDLER_WITH_PC(r2r_##name, instr.pc);                      \
        return RegMode::k##to_type##Reg;                                       \
      case kR2S:                                                               \
        EMIT_INSTR_HANDLER_WITH_PC(r2s_##name, instr.pc);                      \
        to_type##Push();                                                       \
        return RegMode::kNoReg;                                                \
      case kS2R:                                                               \
        EMIT_INSTR_HANDLER_WITH_PC(s2r_##name, instr.pc);                      \
        from_type##Pop();                                                      \
        return RegMode::k##to_type##Reg;                                       \
      case kS2S:                                                               \
        EMIT_INSTR_HANDLER_WITH_PC(s2s_##name, instr.pc);                      \
        from_type##Pop();                                                      \
        to_type##Push();                                                       \
        return RegMode::kNoReg;                                                \
    }                                                                          \
    break;                                                                     \
  }
      FOREACH_I64_CONVERT_FROM_FLOAT_UNOP(EXECUTE_UNOP)
      FOREACH_I32_CONVERT_FROM_FLOAT_UNOP(EXECUTE_UNOP)
#undef EXECUTE_UNOP

#define EXECUTE_UNOP(name, from_ctype, from_type, to_ctype, to_type, op) \
  case kExpr##name: {                                                    \
    switch (mode) {                                                      \
      case kR2R:                                                         \
        EMIT_INSTR_HANDLER(r2r_##name);                                  \
        return RegMode::k##to_type##Reg;                                 \
      case kR2S:                                                         \
        EMIT_INSTR_HANDLER(r2s_##name);                                  \
        to_type##Push();                                                 \
        return RegMode::kNoReg;                                          \
      case kS2R:                                                         \
        EMIT_INSTR_HANDLER(s2r_##name);                                  \
        from_type##Pop();                                                \
        return RegMode::k##to_type##Reg;                                 \
      case kS2S:                                                         \
        EMIT_INSTR_HANDLER(s2s_##name);                                  \
        from_type##Pop();                                                \
        to_type##Push();                                                 \
        return RegMode::kNoReg;                                          \
    }                                                                    \
    break;                                                               \
  }
      FOREACH_BITS_UNOP(EXECUTE_UNOP)
#undef EXECUTE_UNOP

#define EXECUTE_UNOP(name, from_ctype, from_type, to_ctype, to_type) \
  case kExpr##name: {                                                \
    switch (mode) {                                                  \
      case kR2R:                                                     \
        EMIT_INSTR_HANDLER(r2r_##name);                              \
        return RegMode::k##to_type##Reg;                             \
      case kR2S:                                                     \
        EMIT_INSTR_HANDLER(r2s_##name);                              \
        to_type##Push();                                             \
        return RegMode::kNoReg;                                      \
      case kS2R:                                                     \
        EMIT_INSTR_HANDLER(s2r_##name);                              \
        from_type##Pop();                                            \
        return RegMode::k##to_type##Reg;                             \
      case kS2S:                                                     \
        EMIT_INSTR_HANDLER(s2s_##name);                              \
        from_type##Pop();                                            \
        to_type##Push();                                             \
        return RegMode::kNoReg;                                      \
    }                                                                \
    break;                                                           \
  }
      FOREACH_EXTENSION_UNOP(EXECUTE_UNOP)
#undef EXECUTE_UNOP

    case kExprRefNull: {
      EMIT_INSTR_HANDLER(s2s_RefNull);
      ValueType value_type =
          ValueType::RefNull(HeapType(instr.optional.ref_type));
      EmitRefValueType(value_type.raw_bit_field());
      RefPush(value_type);
      break;
    }

    case kExprRefIsNull:
      EMIT_INSTR_HANDLER(s2s_RefIsNull);
      RefPop();
      I32Push();
      break;

    case kExprRefFunc: {
      EMIT_INSTR_HANDLER(s2s_RefFunc);
      EmitI32Const(instr.optional.index);
      ValueType value_type =
          ValueType::Ref(module_->functions[instr.optional.index].sig_index);
      RefPush(value_type);
      break;
    }

    case kExprRefEq:
      EMIT_INSTR_HANDLER(s2s_RefEq);
      RefPop();
      RefPop();
      I32Push();
      break;

    case kExprRefAsNonNull: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_RefAsNonNull, instr.pc);
      ValueType value_type = RefPop();
      RefPush(value_type);
      break;
    }

    case kExprStructNew: {
      EMIT_INSTR_HANDLER(s2s_StructNew);
      EmitI32Const(instr.optional.index);
      // Pops args
      const StructType* struct_type = module_->struct_type(
          ModuleTypeIndex({instr.optional.gc_field_immediate.struct_index}));
      for (uint32_t i = struct_type->field_count(); i > 0;) {
        i--;
        ValueKind kind = struct_type->field(i).kind();
        Pop(kind);
      }

      RefPush(ValueType::Ref(ModuleTypeIndex({instr.optional.index})));
      break;
    }

    case kExprStructNewDefault: {
      EMIT_INSTR_HANDLER(s2s_StructNewDefault);
      EmitI32Const(instr.optional.index);
      RefPush(ValueType::Ref(ModuleTypeIndex({instr.optional.index})));
      break;
    }

    case kExprStructGet:
    case kExprStructGetS:
    case kExprStructGetU: {
      bool is_signed = (instr.opcode == wasm::kExprStructGetS);
      const StructType* struct_type = module_->struct_type(
          ModuleTypeIndex({instr.optional.gc_field_immediate.struct_index}));
      uint32_t field_index = instr.optional.gc_field_immediate.field_index;
      ValueType value_type = struct_type->field(field_index);
      ValueKind kind = value_type.kind();
      int offset = StructFieldOffset(struct_type, field_index);
      switch (kind) {
        case kI8:
          if (is_signed) {
            EMIT_INSTR_HANDLER_WITH_PC(s2s_I8SStructGet, instr.pc);
          } else {
            EMIT_INSTR_HANDLER_WITH_PC(s2s_I8UStructGet, instr.pc);
          }
          RefPop();
          EmitStructFieldOffset(offset);
          I32Push();
          break;
        case kI16:
          if (is_signed) {
            EMIT_INSTR_HANDLER_WITH_PC(s2s_I16SStructGet, instr.pc);
          } else {
            EMIT_INSTR_HANDLER_WITH_PC(s2s_I16UStructGet, instr.pc);
          }
          RefPop();
          EmitStructFieldOffset(offset);
          I32Push();
          break;
        case kI32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I32StructGet, instr.pc);
          RefPop();
          EmitStructFieldOffset(offset);
          I32Push();
          break;
        case kI64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I64StructGet, instr.pc);
          RefPop();
          EmitStructFieldOffset(offset);
          I64Push();
          break;
        case kF32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F32StructGet, instr.pc);
          RefPop();
          EmitStructFieldOffset(offset);
          F32Push();
          break;
        case kF64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F64StructGet, instr.pc);
          RefPop();
          EmitStructFieldOffset(offset);
          F64Push();
          break;
        case kS128:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_S128StructGet, instr.pc);
          RefPop();
          EmitStructFieldOffset(offset);
          S128Push();
          break;
        case kRef:
        case kRefNull:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_RefStructGet, instr.pc);
          RefPop();
          EmitStructFieldOffset(offset);
          RefPush(value_type);
          break;
        default:
          UNREACHABLE();
      }
      break;
    }

    case kExprStructSet: {
      const StructType* struct_type = module_->struct_type(
          ModuleTypeIndex({instr.optional.gc_field_immediate.struct_index}));
      uint32_t field_index = instr.optional.gc_field_immediate.field_index;
      int offset = StructFieldOffset(struct_type, field_index);
      ValueKind kind = struct_type->field(field_index).kind();
      switch (kind) {
        case kI8:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I8StructSet, instr.pc);
          EmitStructFieldOffset(offset);
          I32Pop();
          break;
        case kI16:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I16StructSet, instr.pc);
          EmitStructFieldOffset(offset);
          I32Pop();
          break;
        case kI32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I32StructSet, instr.pc);
          EmitStructFieldOffset(offset);
          I32Pop();
          break;
        case kI64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I64StructSet, instr.pc);
          EmitStructFieldOffset(offset);
          I64Pop();
          break;
        case kF32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F32StructSet, instr.pc);
          EmitStructFieldOffset(offset);
          F32Pop();
          break;
        case kF64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F64StructSet, instr.pc);
          EmitStructFieldOffset(offset);
          F64Pop();
          break;
        case kS128:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_S128StructSet, instr.pc);
          EmitStructFieldOffset(offset);
          S128Pop();
          break;
        case kRef:
        case kRefNull:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_RefStructSet, instr.pc);
          EmitStructFieldOffset(offset);
          RefPop();
          break;
        default:
          UNREACHABLE();
      }
      RefPop();  // The object to set the field to.
      break;
    }

    case kExprArrayNew: {
      uint32_t array_index = instr.optional.gc_array_new_fixed.array_index;
      const ArrayType* array_type =
          module_->array_type(ModuleTypeIndex({array_index}));
      ValueType element_type = array_type->element_type();
      ValueKind kind = element_type.kind();

      // Pop a single value to be used to initialize the array.
      switch (kind) {
        case kI8:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I8ArrayNew, instr.pc);
          EmitI32Const(array_index);
          I32Pop();  // Array length.
          I32Pop();  // Initialization value.
          break;
        case kI16:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I16ArrayNew, instr.pc);
          EmitI32Const(array_index);
          I32Pop();
          I32Pop();
          break;
        case kI32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I32ArrayNew, instr.pc);
          EmitI32Const(array_index);
          I32Pop();
          I32Pop();
          break;
        case kI64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I64ArrayNew, instr.pc);
          EmitI32Const(array_index);
          I32Pop();
          I64Pop();
          break;
        case kF32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F32ArrayNew, instr.pc);
          EmitI32Const(array_index);
          I32Pop();
          F32Pop();
          break;
        case kF64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F64ArrayNew, instr.pc);
          EmitI32Const(array_index);
          I32Pop();
          F64Pop();
          break;
        case kS128:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_S128ArrayNew, instr.pc);
          EmitI32Const(array_index);
          I32Pop();
          S128Pop();
          break;
        case kRef:
        case kRefNull:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_RefArrayNew, instr.pc);
          EmitI32Const(array_index);
          I32Pop();
          RefPop();
          break;
        default:
          UNREACHABLE();
      }
      // Push the new array.
      RefPush(ValueType::Ref(ModuleTypeIndex({array_index})));
      break;
    }

    case kExprArrayNewFixed: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_ArrayNewFixed, instr.pc);
      uint32_t length = instr.optional.gc_array_new_fixed.length;
      uint32_t array_index = instr.optional.gc_array_new_fixed.array_index;
      EmitI32Const(array_index);
      EmitI32Const(length);
      const ArrayType* array_type =
          module_->array_type(ModuleTypeIndex({array_index}));
      ValueType element_type = array_type->element_type();
      ValueKind kind = element_type.kind();
      // Pop values to initialize the array.
      for (uint32_t i = 0; i < length; i++) {
        switch (kind) {
          case kI8:
          case kI16:
          case kI32:
            I32Pop();
            break;
          case kI64:
            I64Pop();
            break;
          case kF32:
            F32Pop();
            break;
          case kF64:
            F64Pop();
            break;
          case kS128:
            S128Pop();
            break;
          case kRef:
          case kRefNull:
            RefPop();
            break;
          default:
            UNREACHABLE();
        }
      }
      // Push the new array.
      RefPush(ValueType::Ref(ModuleTypeIndex({array_index})));
      break;
    }

    case kExprArrayNewDefault: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_ArrayNewDefault, instr.pc);
      EmitI32Const(instr.optional.index);
      I32Pop();
      // Push the new array.
      RefPush(ValueType::Ref(ModuleTypeIndex({instr.optional.index})));
      break;
    }

    case kExprArrayNewData: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_ArrayNewData, instr.pc);
      uint32_t array_index =
          instr.optional.gc_array_new_or_init_data.array_index;
      EmitI32Const(array_index);
      uint32_t data_index = instr.optional.gc_array_new_or_init_data.data_index;
      EmitI32Const(data_index);
      I32Pop();
      I32Pop();
      // Push the new array.
      RefPush(ValueType::Ref(ModuleTypeIndex({array_index})));
      break;
    }

    case kExprArrayNewElem: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_ArrayNewElem, instr.pc);
      uint32_t array_index =
          instr.optional.gc_array_new_or_init_data.array_index;
      EmitI32Const(array_index);
      uint32_t data_index = instr.optional.gc_array_new_or_init_data.data_index;
      EmitI32Const(data_index);
      I32Pop();
      I32Pop();
      // Push the new array.
      RefPush(ValueType::Ref(ModuleTypeIndex({array_index})));
      break;
    }

    case kExprArrayInitData: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_ArrayInitData, instr.pc);
      uint32_t array_index =
          instr.optional.gc_array_new_or_init_data.array_index;
      EmitI32Const(array_index);
      uint32_t data_index = instr.optional.gc_array_new_or_init_data.data_index;
      EmitI32Const(data_index);
      I32Pop();  // size
      I32Pop();  // src offset
      I32Pop();  // dest offset
      RefPop();  // array to initialize
      break;
    }

    case kExprArrayInitElem: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_ArrayInitElem, instr.pc);
      uint32_t array_index =
          instr.optional.gc_array_new_or_init_data.array_index;
      EmitI32Const(array_index);
      uint32_t data_index = instr.optional.gc_array_new_or_init_data.data_index;
      EmitI32Const(data_index);
      I32Pop();  // size
      I32Pop();  // src offset
      I32Pop();  // dest offset
      RefPop();  // array to initialize
      break;
    }

    case kExprArrayLen: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_ArrayLen, instr.pc);
      RefPop();
      I32Push();
      break;
    }

    case kExprArrayCopy: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_ArrayCopy, instr.pc);
      EmitI32Const(instr.optional.gc_array_copy.dest_array_index);
      EmitI32Const(instr.optional.gc_array_copy.src_array_index);
      I32Pop();  // size
      I32Pop();  // src offset
      RefPop();  // src array
      I32Pop();  // dest offset
      RefPop();  // dest array
      break;
    }

    case kExprArrayGet:
    case kExprArrayGetS:
    case kExprArrayGetU: {
      bool is_signed = (instr.opcode == wasm::kExprArrayGetS);
      const ArrayType* array_type =
          module_->array_type(ModuleTypeIndex({instr.optional.index}));
      ValueType element_type = array_type->element_type();
      ValueKind kind = element_type.kind();
      switch (kind) {
        case kI8:
          if (is_signed) {
            EMIT_INSTR_HANDLER_WITH_PC(s2s_I8SArrayGet, instr.pc);
          } else {
            EMIT_INSTR_HANDLER_WITH_PC(s2s_I8UArrayGet, instr.pc);
          }
          I32Pop();
          RefPop();
          I32Push();
          break;
        case kI16:
          if (is_signed) {
            EMIT_INSTR_HANDLER_WITH_PC(s2s_I16SArrayGet, instr.pc);
          } else {
            EMIT_INSTR_HANDLER_WITH_PC(s2s_I16UArrayGet, instr.pc);
          }
          I32Pop();
          RefPop();
          I32Push();
          break;
        case kI32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I32ArrayGet, instr.pc);
          I32Pop();
          RefPop();
          I32Push();
          break;
        case kI64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I64ArrayGet, instr.pc);
          I32Pop();
          RefPop();
          I64Push();
          break;
        case kF32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F32ArrayGet, instr.pc);
          I32Pop();
          RefPop();
          F32Push();
          break;
        case kF64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F64ArrayGet, instr.pc);
          I32Pop();
          RefPop();
          F64Push();
          break;
        case kS128:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_S128ArrayGet, instr.pc);
          I32Pop();
          RefPop();
          S128Push();
          break;
        case kRef:
        case kRefNull:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_RefArrayGet, instr.pc);
          I32Pop();
          RefPop();
          RefPush(element_type);
          break;
        default:
          UNREACHABLE();
      }
      break;
    }

    case kExprArraySet: {
      const ArrayType* array_type =
          module_->array_type(ModuleTypeIndex({instr.optional.index}));
      ValueKind kind = array_type->element_type().kind();
      switch (kind) {
        case kI8:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I8ArraySet, instr.pc);
          I32Pop();
          I32Pop();
          RefPop();
          break;
        case kI16:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I16ArraySet, instr.pc);
          I32Pop();
          I32Pop();
          RefPop();
          break;
        case kI32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I32ArraySet, instr.pc);
          I32Pop();
          I32Pop();
          RefPop();
          break;
        case kI64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I64ArraySet, instr.pc);
          I64Pop();
          I32Pop();
          RefPop();
          break;
        case kF32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F32ArraySet, instr.pc);
          F32Pop();
          I32Pop();
          RefPop();
          break;
        case kF64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F64ArraySet, instr.pc);
          F64Pop();
          I32Pop();
          RefPop();
          break;
        case kS128:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_S128ArraySet, instr.pc);
          S128Pop();
          I32Pop();
          RefPop();
          break;
        case kRef:
        case kRefNull:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_RefArraySet, instr.pc);
          RefPop();
          I32Pop();
          RefPop();
          break;
        default:
          UNREACHABLE();
      }
      break;
    }

    case kExprArrayFill: {
      const ArrayType* array_type =
          module_->array_type(ModuleTypeIndex({instr.optional.index}));
      ValueKind kind = array_type->element_type().kind();
      switch (kind) {
        case kI8:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I8ArrayFill, instr.pc);
          I32Pop();  // The size of the filled slice.
          I32Pop();  // The value with which to fill the array.
          I32Pop();  // The offset at which to begin filling.
          RefPop();  // The array to fill.
          break;
        case kI16:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I16ArrayFill, instr.pc);
          I32Pop();
          I32Pop();
          I32Pop();
          RefPop();
          break;
        case kI32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I32ArrayFill, instr.pc);
          I32Pop();
          I32Pop();
          I32Pop();
          RefPop();
          break;
        case kI64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_I64ArrayFill, instr.pc);
          I32Pop();
          I64Pop();
          I32Pop();
          RefPop();
          break;
        case kF32:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F32ArrayFill, instr.pc);
          I32Pop();
          F32Pop();
          I32Pop();
          RefPop();
          break;
        case kF64:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_F64ArrayFill, instr.pc);
          I32Pop();
          F64Pop();
          I32Pop();
          RefPop();
          break;
        case kS128:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_S128ArrayFill, instr.pc);
          I32Pop();
          S128Pop();
          I32Pop();
          RefPop();
          break;
        case kRef:
        case kRefNull:
          EMIT_INSTR_HANDLER_WITH_PC(s2s_RefArrayFill, instr.pc);
          I32Pop();
          RefPop();
          I32Pop();
          RefPop();
          break;
        default:
          UNREACHABLE();
      }
      break;
    }

    case kExprRefI31: {
      EMIT_INSTR_HANDLER(s2s_RefI31);
      I32Pop();
      RefPush(ValueType::Ref(HeapType::kI31));
      break;
    }

    case kExprI31GetS: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_I31GetS, instr.pc);
      RefPop();
      I32Push();
      break;
    }

    case kExprI31GetU: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_I31GetU, instr.pc);
      RefPop();
      I32Push();
      break;
    }

    case kExprRefCast:
    case kExprRefCastNull: {
      bool null_succeeds = (instr.opcode == kExprRefCastNull);
      HeapType target_type = instr.optional.gc_heap_type_immediate.type();
      ValueType resulting_value_type = ValueType::RefMaybeNull(
          target_type, null_succeeds ? kNullable : kNonNullable);

      ValueType obj_type = slots_[stack_.back()].value_type;
      DCHECK(obj_type.is_object_reference());

      // This logic ensures that code generation can assume that functions
      // can only be cast to function types, and data objects to data types.
      if (V8_UNLIKELY(TypeCheckAlwaysSucceeds(obj_type, target_type))) {
        if (obj_type.is_nullable() && !null_succeeds) {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_AssertNotNullTypecheck, instr.pc);
          ValueType value_type = RefPop();
          EmitRefValueType(value_type.raw_bit_field());
          RefPush(resulting_value_type);
        } else {
          // Just forward the ref object.
        }
      } else if (V8_UNLIKELY(TypeCheckAlwaysFails(obj_type, target_type,
                                                  null_succeeds))) {
        // Unrelated types. The only way this will not trap is if the object
        // is null.
        if (obj_type.is_nullable() && null_succeeds) {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_AssertNullTypecheck, instr.pc);
          ValueType value_type = RefPop();
          EmitRefValueType(value_type.raw_bit_field());
          RefPush(resulting_value_type);
        } else {
          // In this case we just trap.
          EMIT_INSTR_HANDLER_WITH_PC(s2s_TrapIllegalCast, instr.pc);
        }
      } else {
        if (instr.opcode == kExprRefCast) {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_RefCast, instr.pc);
        } else {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_RefCastNull, instr.pc);
        }
        EmitI32Const(instr.optional.gc_heap_type_immediate.type_representation);
        ValueType value_type = RefPop();
        EmitRefValueType(value_type.raw_bit_field());
        RefPush(resulting_value_type);
      }
      break;
    }

    case kExprRefTest:
    case kExprRefTestNull: {
      bool null_succeeds = (instr.opcode == kExprRefTestNull);
      HeapType target_type = instr.optional.gc_heap_type_immediate.type();

      ValueType obj_type = slots_[stack_.back()].value_type;
      DCHECK(obj_type.is_object_reference());

      // This logic ensures that code generation can assume that functions
      // can only be cast to function types, and data objects to data types.
      if (V8_UNLIKELY(TypeCheckAlwaysSucceeds(obj_type, target_type))) {
        // Type checking can still fail for null.
        if (obj_type.is_nullable() && !null_succeeds) {
          EMIT_INSTR_HANDLER(s2s_RefIsNonNull);
          RefPop();
          I32Push();  // bool
        } else {
          EMIT_INSTR_HANDLER(s2s_RefTestSucceeds);
          RefPop();
          I32Push();  // bool=true
        }
      } else if (V8_UNLIKELY(TypeCheckAlwaysFails(obj_type, target_type,
                                                  null_succeeds))) {
        EMIT_INSTR_HANDLER(s2s_RefTestFails);
        RefPop();
        I32Push();  // bool=false
      } else {
        if (instr.opcode == kExprRefTest) {
          EMIT_INSTR_HANDLER(s2s_RefTest);
        } else {
          EMIT_INSTR_HANDLER(s2s_RefTestNull);
        }
        EmitI32Const(instr.optional.gc_heap_type_immediate.type_representation);
        ValueType value_type = RefPop();
        EmitRefValueType(value_type.raw_bit_field());
        I32Push();  // bool
      }
      break;
    }

    case kExprAnyConvertExtern: {
      EMIT_INSTR_HANDLER_WITH_PC(s2s_AnyConvertExtern, instr.pc);
      ValueType extern_val = RefPop();
      ValueType intern_type = ValueType::RefMaybeNull(
          HeapType::kAny, Nullability(extern_val.is_nullable()));
      RefPush(intern_type);
      break;
    }

    case kExprExternConvertAny: {
      EMIT_INSTR_HANDLER(s2s_ExternConvertAny);
      ValueType value_type = RefPop();
      ValueType extern_type = ValueType::RefMaybeNull(
          HeapType::kExtern, Nullability(value_type.is_nullable()));
      RefPush(extern_type);
      break;
    }

    case kExprMemoryInit:
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_MemoryInit, s2s_Memory64Init,
                                       is_memory64_, instr.pc);
      EmitI32Const(instr.optional.index);
      I32Pop();
      I32Pop();
      MemIndexPop();
      break;

    case kExprDataDrop:
      EMIT_INSTR_HANDLER(s2s_DataDrop);
      EmitI32Const(instr.optional.index);
      break;

    case kExprMemoryCopy:
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_MemoryCopy, s2s_Memory64Copy,
                                       is_memory64_, instr.pc);
      MemIndexPop();
      MemIndexPop();
      MemIndexPop();
      break;

    case kExprMemoryFill:
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_MemoryFill, s2s_Memory64Fill,
                                       is_memory64_, instr.pc);
      MemIndexPop();
      I32Pop();
      MemIndexPop();
      break;

    case kExprTableInit: {
      bool is_table64 = module_->tables[instr.optional.index].is_table64();
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_TableInit, s2s_Table64Init,
                                       is_table64, instr.pc);

      EmitI32Const(instr.optional.table_init.table_index);
      EmitI32Const(instr.optional.table_init.element_segment_index);
      I32Pop();
      I32Pop();
      is_table64 ? I64Pop() : I32Pop();
    } break;

    case kExprElemDrop:
      EMIT_INSTR_HANDLER(s2s_ElemDrop);
      EmitI32Const(instr.optional.index);
      break;

    case kExprTableCopy: {
      bool is_src_table64 =
          module_->tables[instr.optional.table_copy.src_table_index]
              .is_table64();
      bool is_dst_table64 =
          module_->tables[instr.optional.table_copy.dst_table_index]
              .is_table64();

      if (is_src_table64) {
        if (is_dst_table64) {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_Table64Copy_64_64_64, instr.pc);
        } else {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_Table64Copy_32_64_32, instr.pc);
        }
      } else {
        if (is_dst_table64) {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_Table64Copy_64_32_32, instr.pc);
        } else {
          EMIT_INSTR_HANDLER_WITH_PC(s2s_TableCopy, instr.pc);
        }
      }
      EmitI32Const(instr.optional.table_copy.dst_table_index);
      EmitI32Const(instr.optional.table_copy.src_table_index);
      is_dst_table64&& is_src_table64 ? I64Pop() : I32Pop();
      is_src_table64 ? I64Pop() : I32Pop();
      is_dst_table64 ? I64Pop() : I32Pop();
    } break;

    case kExprTableGrow: {
      bool is_table64 = module_->tables[instr.optional.index].is_table64();
      if (is_table64) {
        EMIT_INSTR_HANDLER(s2s_Table64Grow);
        EmitI32Const(instr.optional.index);
        I64Pop();
        RefPop();
        I64Push();
      } else {
        EMIT_INSTR_HANDLER(s2s_TableGrow);
        EmitI32Const(instr.optional.index);
        I32Pop();
        RefPop();
        I32Push();
      }
    } break;

    case kExprTableSize: {
      bool is_table64 = module_->tables[instr.optional.index].is_table64();
      if (is_table64) {
        EMIT_INSTR_HANDLER(s2s_Table64Size);
        EmitI32Const(instr.optional.index);
        I64Push();
      } else {
        EMIT_INSTR_HANDLER(s2s_TableSize);
        EmitI32Const(instr.optional.index);
        I32Push();
      }
    } break;

    case kExprTableFill: {
      bool is_table64 = module_->tables[instr.optional.index].is_table64();
      if (is_table64) {
        EMIT_INSTR_HANDLER_WITH_PC(s2s_Table64Fill, instr.pc);
        EmitI32Const(instr.optional.index);
        I64Pop();
        RefPop();
        I64Pop();
      } else {
        EMIT_INSTR_HANDLER_WITH_PC(s2s_TableFill, instr.pc);
        EmitI32Const(instr.optional.index);
        I32Pop();
        RefPop();
        I32Pop();
      }
    } break;

    case kExprAtomicNotify:
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_AtomicNotify, s2s_AtomicNotify_Idx64,
                                       is_memory64_, instr.pc);
      I32Pop();  // val
      EmitMemoryOffset(instr.optional.offset);
      MemIndexPop();  // memory index
      I32Push();
      break;

    case kExprI32AtomicWait:
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(
          s2s_I32AtomicWait, s2s_I32AtomicWait_Idx64, is_memory64_, instr.pc);
      I64Pop();  // timeout
      I32Pop();  // val
      EmitMemoryOffset(instr.optional.offset);
      MemIndexPop();  // memory index
      I32Push();
      break;

    case kExprI64AtomicWait:
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(
          s2s_I64AtomicWait, s2s_I64AtomicWait_Idx64, is_memory64_, instr.pc);
      I64Pop();  // timeout
      I64Pop();  // val
      EmitMemoryOffset(instr.optional.offset);
      MemIndexPop();  // memory index
      I32Push();
      break;

    case kExprAtomicFence:
      EMIT_INSTR_HANDLER(s2s_AtomicFence);
      break;

#define ATOMIC_BINOP(name, Type, ctype, type, op_ctype, op_type, operation) \
  case kExpr##name: {                                                       \
    EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_##name, s2s_##name##_Idx64,        \
                                     is_memory64_, instr.pc);               \
    op_type##Pop();                                                         \
    EmitMemoryOffset(instr.optional.offset);                                \
    MemIndexPop();                                                          \
    op_type##Push();                                                        \
    return RegMode::kNoReg;                                                 \
  }
      FOREACH_ATOMIC_BINOP(ATOMIC_BINOP)
#undef ATOMIC_BINOP

#define ATOMIC_COMPARE_EXCHANGE_OP(name, Type, ctype, type, op_ctype, op_type) \
  case kExpr##name: {                                                          \
    EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_##name, s2s_##name##_Idx64,           \
                                     is_memory64_, instr.pc);                  \
    op_type##Pop();                                                            \
    op_type##Pop();                                                            \
    EmitMemoryOffset(instr.optional.offset);                                   \
    MemIndexPop();                                                             \
    op_type##Push();                                                           \
    return RegMode::kNoReg;                                                    \
  }
      FOREACH_ATOMIC_COMPARE_EXCHANGE_OP(ATOMIC_COMPARE_EXCHANGE_OP)
#undef ATOMIC_COMPARE_EXCHANGE_OP

#define ATOMIC_LOAD_OP(name, Type, ctype, type, op_ctype, op_type)   \
  case kExpr##name: {                                                \
    EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_##name, s2s_##name##_Idx64, \
                                     is_memory64_, instr.pc);        \
    EmitMemoryOffset(instr.optional.offset);                         \
    MemIndexPop();                                                   \
    op_type##Push();                                                 \
    return RegMode::kNoReg;                                          \
  }
      FOREACH_ATOMIC_LOAD_OP(ATOMIC_LOAD_OP)
#undef ATOMIC_LOAD_OP

#define ATOMIC_STORE_OP(name, Type, ctype, type, op_ctype, op_type)  \
  case kExpr##name: {                                                \
    EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_##name, s2s_##name##_Idx64, \
                                     is_memory64_, instr.pc);        \
    op_type##Pop();                                                  \
    EmitMemoryOffset(instr.optional.offset);                         \
    MemIndexPop();                                                   \
    return RegMode::kNoReg;                                          \
  }
      FOREACH_ATOMIC_STORE_OP(ATOMIC_STORE_OP)
#undef ATOMIC_STORE_OP

#define SPLAT_CASE(format, stype, valType, op_type, num) \
  case kExpr##format##Splat: {                           \
    EMIT_INSTR_HANDLER(s2s_Simd##format##Splat);         \
    op_type##Pop();                                      \
    S128Push();                                          \
    return RegMode::kNoReg;                              \
  }
      SPLAT_CASE(F64x2, float64x2, double, F64, 2)
      SPLAT_CASE(F32x4, float32x4, float, F32, 4)
      SPLAT_CASE(I64x2, int64x2, int64_t, I64, 2)
      SPLAT_CASE(I32x4, int32x4, int32_t, I32, 4)
      SPLAT_CASE(I16x8, int16x8, int32_t, I32, 8)
      SPLAT_CASE(I8x16, int8x16, int32_t, I32, 16)
#undef SPLAT_CASE

#define EXTRACT_LANE_CASE(format, stype, op_type, name) \
  case kExpr##format##ExtractLane: {                    \
    EMIT_INSTR_HANDLER(s2s_Simd##format##ExtractLane);  \
    /* emit 8 bits ? */                                 \
    EmitI16Const(instr.optional.simd_lane);             \
    S128Pop();                                          \
    op_type##Push();                                    \
    return RegMode::kNoReg;                             \
  }
      EXTRACT_LANE_CASE(F64x2, float64x2, F64, f64x2)
      EXTRACT_LANE_CASE(F32x4, float32x4, F32, f32x4)
      EXTRACT_LANE_CASE(I64x2, int64x2, I64, i64x2)
      EXTRACT_LANE_CASE(I32x4, int32x4, I32, i32x4)
#undef EXTRACT_LANE_CASE

#define EXTRACT_LANE_EXTEND_CASE(format, stype, name, sign, extended_type) \
  case kExpr##format##ExtractLane##sign: {                                 \
    EMIT_INSTR_HANDLER(s2s_Simd##format##ExtractLane##sign);               \
    /* emit 8 bits ? */                                                    \
    EmitI16Const(instr.optional.simd_lane);                                \
    S128Pop();                                                             \
    I32Push();                                                             \
    return RegMode::kNoReg;                                                \
  }
      EXTRACT_LANE_EXTEND_CASE(I16x8, int16x8, i16x8, S, int32_t)
      EXTRACT_LANE_EXTEND_CASE(I16x8, int16x8, i16x8, U, uint32_t)
      EXTRACT_LANE_EXTEND_CASE(I8x16, int8x16, i8x16, S, int32_t)
      EXTRACT_LANE_EXTEND_CASE(I8x16, int8x16, i8x16, U, uint32_t)
#undef EXTRACT_LANE_EXTEND_CASE

#define BINOP_CASE(op, name, stype, count, expr) \
  case kExpr##op: {                              \
    EMIT_INSTR_HANDLER(s2s_Simd##op);            \
    S128Pop();                                   \
    S128Pop();                                   \
    S128Push();                                  \
    return RegMode::kNoReg;                      \
  }
      BINOP_CASE(F64x2Add, f64x2, float64x2, 2, a + b)
      BINOP_CASE(F64x2Sub, f64x2, float64x2, 2, a - b)
      BINOP_CASE(F64x2Mul, f64x2, float64x2, 2, a * b)
      BINOP_CASE(F64x2Div, f64x2, float64x2, 2, base::Divide(a, b))
      BINOP_CASE(F64x2Min, f64x2, float64x2, 2, JSMin(a, b))
      BINOP_CASE(F64x2Max, f64x2, float64x2, 2, JSMax(a, b))
      BINOP_CASE(F64x2Pmin, f64x2, float64x2, 2, std::min(a, b))
      BINOP_CASE(F64x2Pmax, f64x2, float64x2, 2, std::max(a, b))
      BINOP_CASE(F32x4RelaxedMin, f32x4, float32x4, 4, std::min(a, b))
      BINOP_CASE(F32x4RelaxedMax, f32x4, float32x4, 4, std::max(a, b))
      BINOP_CASE(F64x2RelaxedMin, f64x2, float64x2, 2, std::min(a, b))
      BINOP_CASE(F64x2RelaxedMax, f64x2, float64x2, 2, std::max(a, b))
      BINOP_CASE(F32x4Add, f32x4, float32x4, 4, a + b)
      BINOP_CASE(F32x4Sub, f32x4, float32x4, 4, a - b)
      BINOP_CASE(F32x4Mul, f32x4, float32x4, 4, a * b)
      BINOP_CASE(F32x4Div, f32x4, float32x4, 4, a / b)
      BINOP_CASE(F32x4Min, f32x4, float32x4, 4, JSMin(a, b))
      BINOP_CASE(F32x4Max, f32x4, float32x4, 4, JSMax(a, b))
      BINOP_CASE(F32x4Pmin, f32x4, float32x4, 4, std::min(a, b))
      BINOP_CASE(F32x4Pmax, f32x4, float32x4, 4, std::max(a, b))
      BINOP_CASE(I64x2Add, i64x2, int64x2, 2, base::AddWithWraparound(a, b))
      BINOP_CASE(I64x2Sub, i64x2, int64x2, 2, base::SubWithWraparound(a, b))
      BINOP_CASE(I64x2Mul, i64x2, int64x2, 2, base::MulWithWraparound(a, b))
      BINOP_CASE(I32x4Add, i32x4, int32x4, 4, base::AddWithWraparound(a, b))
      BINOP_CASE(I32x4Sub, i32x4, int32x4, 4, base::SubWithWraparound(a, b))
      BINOP_CASE(I32x4Mul, i32x4, int32x4, 4, base::MulWithWraparound(a, b))
      BINOP_CASE(I32x4MinS, i32x4, int32x4, 4, a < b ? a : b)
      BINOP_CASE(I32x4MinU, i32x4, int32x4, 4,
                 static_cast<uint32_t>(a) < static_cast<uint32_t>(b) ? a : b)
      BINOP_CASE(I32x4MaxS, i32x4, int32x4, 4, a > b ? a : b)
      BINOP_CASE(I32x4MaxU, i32x4, int32x4, 4,
                 static_cast<uint32_t>(a) > static_cast<uint32_t>(b) ? a : b)
      BINOP_CASE(S128And, i32x4, int32x4, 4, a & b)
      BINOP_CASE(S128Or, i32x4, int32x4, 4, a | b)
      BINOP_CASE(S128Xor, i32x4, int32x4, 4, a ^ b)
      BINOP_CASE(S128AndNot, i32x4, int32x4, 4, a & ~b)
      BINOP_CASE(I16x8Add, i16x8, int16x8, 8, base::AddWithWraparound(a, b))
      BINOP_CASE(I16x8Sub, i16x8, int16x8, 8, base::SubWithWraparound(a, b))
      BINOP_CASE(I16x8Mul, i16x8, int16x8, 8, base::MulWithWraparound(a, b))
      BINOP_CASE(I16x8MinS, i16x8, int16x8, 8, a < b ? a : b)
      BINOP_CASE(I16x8MinU, i16x8, int16x8, 8,
                 static_cast<uint16_t>(a) < static_cast<uint16_t>(b) ? a : b)
      BINOP_CASE(I16x8MaxS, i16x8, int16x8, 8, a > b ? a : b)
      BINOP_CASE(I16x8MaxU, i16x8, int16x8, 8,
                 static_cast<uint16_t>(a) > static_cast<uint16_t>(b) ? a : b)
      BINOP_CASE(I16x8AddSatS, i16x8, int16x8, 8, SaturateAdd<int16_t>(a, b))
      BINOP_CASE(I16x8AddSatU, i16x8, int16x8, 8, SaturateAdd<uint16_t>(a, b))
      BINOP_CASE(I16x8SubSatS, i16x8, int16x8, 8, SaturateSub<int16_t>(a, b))
      BINOP_CASE(I16x8SubSatU, i16x8, int16x8, 8, SaturateSub<uint16_t>(a, b))
      BINOP_CASE(I16x8RoundingAverageU, i16x8, int16x8, 8,
                 RoundingAverageUnsigned<uint16_t>(a, b))
      BINOP_CASE(I16x8Q15MulRSatS, i16x8, int16x8, 8,
                 SaturateRoundingQMul<int16_t>(a, b))
      BINOP_CASE(I16x8RelaxedQ15MulRS, i16x8, int16x8, 8,
                 SaturateRoundingQMul<int16_t>(a, b))
      BINOP_CASE(I8x16Add, i8x16, int8x16, 16, base::AddWithWraparound(a, b))
      BINOP_CASE(I8x16Sub, i8x16, int8x16, 16, base::SubWithWraparound(a, b))
      BINOP_CASE(I8x16MinS, i8x16, int8x16, 16, a < b ? a : b)
      BINOP_CASE(I8x16MinU, i8x16, int8x16, 16,
                 static_cast<uint8_t>(a) < static_cast<uint8_t>(b) ? a : b)
      BINOP_CASE(I8x16MaxS, i8x16, int8x16, 16, a > b ? a : b)
      BINOP_CASE(I8x16MaxU, i8x16, int8x16, 16,
                 static_cast<uint8_t>(a) > static_cast<uint8_t>(b) ? a : b)
      BINOP_CASE(I8x16AddSatS, i8x16, int8x16, 16, SaturateAdd<int8_t>(a, b))
      BINOP_CASE(I8x16AddSatU, i8x16, int8x16, 16, SaturateAdd<uint8_t>(a, b))
      BINOP_CASE(I8x16SubSatS, i8x16, int8x16, 16, SaturateSub<int8_t>(a, b))
      BINOP_CASE(I8x16SubSatU, i8x16, int8x16, 16, SaturateSub<uint8_t>(a, b))
      BINOP_CASE(I8x16RoundingAverageU, i8x16, int8x16, 16,
                 RoundingAverageUnsigned<uint8_t>(a, b))
#undef BINOP_CASE

#define UNOP_CASE(op, name, stype, count, expr) \
  case kExpr##op: {                             \
    EMIT_INSTR_HANDLER(s2s_Simd##op);           \
    S128Pop();                                  \
    S128Push();                                 \
    return RegMode::kNoReg;                     \
  }
      UNOP_CASE(F64x2Abs, f64x2, float64x2, 2, std::abs(a))
      UNOP_CASE(F64x2Neg, f64x2, float64x2, 2, -a)
      UNOP_CASE(F64x2Sqrt, f64x2, float64x2, 2, std::sqrt(a))
      UNOP_CASE(F64x2Ceil, f64x2, float64x2, 2,
                (AixFpOpWorkaround<double, &ceil>(a)))
      UNOP_CASE(F64x2Floor, f64x2, float64x2, 2,
                (AixFpOpWorkaround<double, &floor>(a)))
      UNOP_CASE(F64x2Trunc, f64x2, float64x2, 2,
                (AixFpOpWorkaround<double, &trunc>(a)))
      UNOP_CASE(F64x2NearestInt, f64x2, float64x2, 2,
                (AixFpOpWorkaround<double, &nearbyint>(a)))
      UNOP_CASE(F32x4Abs, f32x4, float32x4, 4, std::abs(a))
      UNOP_CASE(F32x4Neg, f32x4, float32x4, 4, -a)
      UNOP_CASE(F32x4Sqrt, f32x4, float32x4, 4, std::sqrt(a))
      UNOP_CASE(F32x4Ceil, f32x4, float32x4, 4,
                (AixFpOpWorkaround<float, &ceilf>(a)))
      UNOP_CASE(F32x4Floor, f32x4, float32x4, 4,
                (AixFpOpWorkaround<float, &floorf>(a)))
      UNOP_CASE(F32x4Trunc, f32x4, float32x4, 4,
                (AixFpOpWorkaround<float, &truncf>(a)))
      UNOP_CASE(F32x4NearestInt, f32x4, float32x4, 4,
                (AixFpOpWorkaround<float, &nearbyintf>(a)))
      UNOP_CASE(I64x2Neg, i64x2, int64x2, 2, base::NegateWithWraparound(a))
      UNOP_CASE(I32x4Neg, i32x4, int32x4, 4, base::NegateWithWraparound(a))
      // Use llabs which will work correctly on both 64-bit and 32-bit.
      UNOP_CASE(I64x2Abs, i64x2, int64x2, 2, std::llabs(a))
      UNOP_CASE(I32x4Abs, i32x4, int32x4, 4, std::abs(a))
      UNOP_CASE(S128Not, i32x4, int32x4, 4, ~a)
      UNOP_CASE(I16x8Neg, i16x8, int16x8, 8, base::NegateWithWraparound(a))
      UNOP_CASE(I16x8Abs, i16x8, int16x8, 8, std::abs(a))
      UNOP_CASE(I8x16Neg, i8x16, int8x16, 16, base::NegateWithWraparound(a))
      UNOP_CASE(I8x16Abs, i8x16, int8x16, 16, std::abs(a))
      UNOP_CASE(I8x16Popcnt, i8x16, int8x16, 16,
                base::bits::CountPopulation<uint8_t>(a))
#undef UNOP_CASE

#define BITMASK_CASE(op, name, stype, count) \
  case kExpr##op: {                          \
    EMIT_INSTR_HANDLER(s2s_Simd##op);        \
    S128Pop();                               \
    I32Push();                               \
    return RegMode::kNoReg;                  \
  }
      BITMASK_CASE(I8x16BitMask, i8x16, int8x16, 16)
      BITMASK_CASE(I16x8BitMask, i16x8, int16x8, 8)
      BITMASK_CASE(I32x4BitMask, i32x4, int32x4, 4)
      BITMASK_CASE(I64x2BitMask, i64x2, int64x2, 2)
#undef BITMASK_CASE

#define CMPOP_CASE(op, name, stype, out_stype, count, expr) \
  case kExpr##op: {                                         \
    EMIT_INSTR_HANDLER(s2s_Simd##op);                       \
    S128Pop();                                              \
    S128Pop();                                              \
    S128Push();                                             \
    return RegMode::kNoReg;                                 \
  }
      CMPOP_CASE(F64x2Eq, f64x2, float64x2, int64x2, 2, a == b)
      CMPOP_CASE(F64x2Ne, f64x2, float64x2, int64x2, 2, a != b)
      CMPOP_CASE(F64x2Gt, f64x2, float64x2, int64x2, 2, a > b)
      CMPOP_CASE(F64x2Ge, f64x2, float64x2, int64x2, 2, a >= b)
      CMPOP_CASE(F64x2Lt, f64x2, float64x2, int64x2, 2, a < b)
      CMPOP_CASE(F64x2Le, f64x2, float64x2, int64x2, 2, a <= b)
      CMPOP_CASE(F32x4Eq, f32x4, float32x4, int32x4, 4, a == b)
      CMPOP_CASE(F32x4Ne, f32x4, float32x4, int32x4, 4, a != b)
      CMPOP_CASE(F32x4Gt, f32x4, float32x4, int32x4, 4, a > b)
      CMPOP_CASE(F32x4Ge, f32x4, float32x4, int32x4, 4, a >= b)
      CMPOP_CASE(F32x4Lt, f32x4, float32x4, int32x4, 4, a < b)
      CMPOP_CASE(F32x4Le, f32x4, float32x4, int32x4, 4, a <= b)
      CMPOP_CASE(I64x2Eq, i64x2, int64x2, int64x2, 2, a == b)
      CMPOP_CASE(I64x2Ne, i64x2, int64x2, int64x2, 2, a != b)
      CMPOP_CASE(I64x2LtS, i64x2, int64x2, int64x2, 2, a < b)
      CMPOP_CASE(I64x2GtS, i64x2, int64x2, int64x2, 2, a > b)
      CMPOP_CASE(I64x2LeS, i64x2, int64x2, int64x2, 2, a <= b)
      CMPOP_CASE(I64x2GeS, i64x2, int64x2, int64x2, 2, a >= b)
      CMPOP_CASE(I32x4Eq, i32x4, int32x4, int32x4, 4, a == b)
      CMPOP_CASE(I32x4Ne, i32x4, int32x4, int32x4, 4, a != b)
      CMPOP_CASE(I32x4GtS, i32x4, int32x4, int32x4, 4, a > b)
      CMPOP_CASE(I32x4GeS, i32x4, int32x4, int32x4, 4, a >= b)
      CMPOP_CASE(I32x4LtS, i32x4, int32x4, int32x4, 4, a < b)
      CMPOP_CASE(I32x4LeS, i32x4, int32x4, int32x4, 4, a <= b)
      CMPOP_CASE(I32x4GtU, i32x4, int32x4, int32x4, 4,
                 static_cast<uint32_t>(a) > static_cast<uint32_t>(b))
      CMPOP_CASE(I32x4GeU, i32x4, int32x4, int32x4, 4,
                 static_cast<uint32_t>(a) >= static_cast<uint32_t>(b))
      CMPOP_CASE(I32x4LtU, i32x4, int32x4, int32x4, 4,
                 static_cast<uint32_t>(a) < static_cast<uint32_t>(b))
      CMPOP_CASE(I32x4LeU, i32x4, int32x4, int32x4, 4,
                 static_cast<uint32_t>(a) <= static_cast<uint32_t>(b))
      CMPOP_CASE(I16x8Eq, i16x8, int16x8, int16x8, 8, a == b)
      CMPOP_CASE(I16x8Ne, i16x8, int16x8, int16x8, 8, a != b)
      CMPOP_CASE(I16x8GtS, i16x8, int16x8, int16x8, 8, a > b)
      CMPOP_CASE(I16x8GeS, i16x8, int16x8, int16x8, 8, a >= b)
      CMPOP_CASE(I16x8LtS, i16x8, int16x8, int16x8, 8, a < b)
      CMPOP_CASE(I16x8LeS, i16x8, int16x8, int16x8, 8, a <= b)
      CMPOP_CASE(I16x8GtU, i16x8, int16x8, int16x8, 8,
                 static_cast<uint16_t>(a) > static_cast<uint16_t>(b))
      CMPOP_CASE(I16x8GeU, i16x8, int16x8, int16x8, 8,
                 static_cast<uint16_t>(a) >= static_cast<uint16_t>(b))
      CMPOP_CASE(I16x8LtU, i16x8, int16x8, int16x8, 8,
                 static_cast<uint16_t>(a) < static_cast<uint16_t>(b))
      CMPOP_CASE(I16x8LeU, i16x8, int16x8, int16x8, 8,
                 static_cast<uint16_t>(a) <= static_cast<uint16_t>(b))
      CMPOP_CASE(I8x16Eq, i8x16, int8x16, int8x16, 16, a == b)
      CMPOP_CASE(I8x16Ne, i8x16, int8x16, int8x16, 16, a != b)
      CMPOP_CASE(I8x16GtS, i8x16, int8x16, int8x16, 16, a > b)
      CMPOP_CASE(I8x16GeS, i8x16, int8x16, int8x16, 16, a >= b)
      CMPOP_CASE(I8x16LtS, i8x16, int8x16, int8x16, 16, a < b)
      CMPOP_CASE(I8x16LeS, i8x16, int8x16, int8x16, 16, a <= b)
      CMPOP_CASE(I8x16GtU, i8x16, int8x16, int8x16, 16,
                 static_cast<uint8_t>(a) > static_cast<uint8_t>(b))
      CMPOP_CASE(I8x16GeU, i8x16, int8x16, int8x16, 16,
                 static_cast<uint8_t>(a) >= static_cast<uint8_t>(b))
      CMPOP_CASE(I8x16LtU, i8x16, int8x16, int8x16, 16,
                 static_cast<uint8_t>(a) < static_cast<uint8_t>(b))
      CMPOP_CASE(I8x16LeU, i8x16, int8x16, int8x16, 16,
                 static_cast<uint8_t>(a) <= static_cast<uint8_t>(b))
#undef CMPOP_CASE

#define REPLACE_LANE_CASE(format, name, stype, ctype, op_type) \
  case kExpr##format##ReplaceLane: {                           \
    EMIT_INSTR_HANDLER(s2s_Simd##format##ReplaceLane);         \
    /* emit 8 bits ? */                                        \
    EmitI16Const(instr.optional.simd_lane);                    \
    op_type##Pop();                                            \
    S128Pop();                                                 \
    S128Push();                                                \
    return RegMode::kNoReg;                                    \
  }
      REPLACE_LANE_CASE(F64x2, f64x2, float64x2, double, F64)
      REPLACE_LANE_CASE(F32x4, f32x4, float32x4, float, F32)
      REPLACE_LANE_CASE(I64x2, i64x2, int64x2, int64_t, I64)
      REPLACE_LANE_CASE(I32x4, i32x4, int32x4, int32_t, I32)
      REPLACE_LANE_CASE(I16x8, i16x8, int16x8, int32_t, I32)
      REPLACE_LANE_CASE(I8x16, i8x16, int8x16, int32_t, I32)
#undef REPLACE_LANE_CASE

    case kExprS128LoadMem: {
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_SimdS128LoadMem,
                                       s2s_SimdS128LoadMem_Idx64, is_memory64_,
                                       instr.pc);
      EmitMemoryOffset(instr.optional.offset);
      MemIndexPop();
      S128Push();
      return RegMode::kNoReg;
    }

    case kExprS128StoreMem: {
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_SimdS128StoreMem,
                                       s2s_SimdS128StoreMem_Idx64, is_memory64_,
                                       instr.pc);
      S128Pop();
      EmitMemoryOffset(instr.optional.offset);
      MemIndexPop();
      return RegMode::kNoReg;
    }

#define SHIFT_CASE(op, name, stype, count, expr) \
  case kExpr##op: {                              \
    EMIT_INSTR_HANDLER(s2s_Simd##op);            \
    I32Pop();                                    \
    S128Pop();                                   \
    S128Push();                                  \
    return RegMode::kNoReg;                      \
  }
      SHIFT_CASE(I64x2Shl, i64x2, int64x2, 2,
                 static_cast<uint64_t>(a) << (shift % 64))
      SHIFT_CASE(I64x2ShrS, i64x2, int64x2, 2, a >> (shift % 64))
      SHIFT_CASE(I64x2ShrU, i64x2, int64x2, 2,
                 static_cast<uint64_t>(a) >> (shift % 64))
      SHIFT_CASE(I32x4Shl, i32x4, int32x4, 4,
                 static_cast<uint32_t>(a) << (shift % 32))
      SHIFT_CASE(I32x4ShrS, i32x4, int32x4, 4, a >> (shift % 32))
      SHIFT_CASE(I32x4ShrU, i32x4, int32x4, 4,
                 static_cast<uint32_t>(a) >> (shift % 32))
      SHIFT_CASE(I16x8Shl, i16x8, int16x8, 8,
                 static_cast<uint16_t>(a) << (shift % 16))
      SHIFT_CASE(I16x8ShrS, i16x8, int16x8, 8, a >> (shift % 16))
      SHIFT_CASE(I16x8ShrU, i16x8, int16x8, 8,
                 static_cast<uint16_t>(a) >> (shift % 16))
      SHIFT_CASE(I8x16Shl, i8x16, int8x16, 16,
                 static_cast<uint8_t>(a) << (shift % 8))
      SHIFT_CASE(I8x16ShrS, i8x16, int8x16, 16, a >> (shift % 8))
      SHIFT_CASE(I8x16ShrU, i8x16, int8x16, 16,
                 static_cast<uint8_t>(a) >> (shift % 8))
#undef SHIFT_CASE

#define EXT_MUL_CASE(op)              \
  case kExpr##op: {                   \
    EMIT_INSTR_HANDLER(s2s_Simd##op); \
    S128Pop();                        \
    S128Pop();                        \
    S128Push();                       \
    return RegMode::kNoReg;           \
  }
      EXT_MUL_CASE(I16x8ExtMulLowI8x16S)
      EXT_MUL_CASE(I16x8ExtMulHighI8x16S)
      EXT_MUL_CASE(I16x8ExtMulLowI8x16U)
      EXT_MUL_CASE(I16x8ExtMulHighI8x16U)
      EXT_MUL_CASE(I32x4ExtMulLowI16x8S)
      EXT_MUL_CASE(I32x4ExtMulHighI16x8S)
      EXT_MUL_CASE(I32x4ExtMulLowI16x8U)
      EXT_MUL_CASE(I32x4ExtMulHighI16x8U)
      EXT_MUL_CASE(I64x2ExtMulLowI32x4S)
      EXT_MUL_CASE(I64x2ExtMulHighI32x4S)
      EXT_MUL_CASE(I64x2ExtMulLowI32x4U)
      EXT_MUL_CASE(I64x2ExtMulHighI32x4U)
#undef EXT_MUL_CASE

#define CONVERT_CASE(op, src_type, name, dst_type, count, start_index, ctype, \
                     expr)                                                    \
  case kExpr##op: {                                                           \
    EMIT_INSTR_HANDLER(s2s_Simd##op);                                         \
    S128Pop();                                                                \
    S128Push();                                                               \
    return RegMode::kNoReg;                                                   \
  }
      CONVERT_CASE(F32x4SConvertI32x4, int32x4, i32x4, float32x4, 4, 0, int32_t,
                   static_cast<float>(a))
      CONVERT_CASE(F32x4UConvertI32x4, int32x4, i32x4, float32x4, 4, 0,
                   uint32_t, static_cast<float>(a))
      CONVERT_CASE(I32x4SConvertF32x4, float32x4, f32x4, int32x4, 4, 0, float,
                   base::saturated_cast<int32_t>(a))
      CONVERT_CASE(I32x4UConvertF32x4, float32x4, f32x4, int32x4, 4, 0, float,
                   base::saturated_cast<uint32_t>(a))
      CONVERT_CASE(I32x4RelaxedTruncF32x4S, float32x4, f32x4, int32x4, 4, 0,
                   float, base::saturated_cast<int32_t>(a))
      CONVERT_CASE(I32x4RelaxedTruncF32x4U, float32x4, f32x4, int32x4, 4, 0,
                   float, base::saturated_cast<uint32_t>(a))
      CONVERT_CASE(I64x2SConvertI32x4Low, int32x4, i32x4, int64x2, 2, 0,
                   int32_t, a)
      CONVERT_CASE(I64x2SConvertI32x4High, int32x4, i32x4, int64x2, 2, 2,
                   int32_t, a)
      CONVERT_CASE(I64x2UConvertI32x4Low, int32x4, i32x4, int64x2, 2, 0,
                   uint32_t, a)
      CONVERT_CASE(I64x2UConvertI32x4High, int32x4, i32x4, int64x2, 2, 2,
                   uint32_t, a)
      CONVERT_CASE(I32x4SConvertI16x8High, int16x8, i16x8, int32x4, 4, 4,
                   int16_t, a)
      CONVERT_CASE(I32x4UConvertI16x8High, int16x8, i16x8, int32x4, 4, 4,
                   uint16_t, a)
      CONVERT_CASE(I32x4SConvertI16x8Low, int16x8, i16x8, int32x4, 4, 0,
                   int16_t, a)
      CONVERT_CASE(I32x4UConvertI16x8Low, int16x8, i16x8, int32x4, 4, 0,
                   uint16_t, a)
      CONVERT_CASE(I16x8SConvertI8x16High, int8x16, i8x16, int16x8, 8, 8,
                   int8_t, a)
      CONVERT_CASE(I16x8UConvertI8x16High, int8x16, i8x16, int16x8, 8, 8,
                   uint8_t, a)
      CONVERT_CASE(I16x8SConvertI8x16Low, int8x16, i8x16, int16x8, 8, 0, int8_t,
                   a)
      CONVERT_CASE(I16x8UConvertI8x16Low, int8x16, i8x16, int16x8, 8, 0,
                   uint8_t, a)
      CONVERT_CASE(F64x2ConvertLowI32x4S, int32x4, i32x4, float64x2, 2, 0,
                   int32_t, static_cast<double>(a))
      CONVERT_CASE(F64x2ConvertLowI32x4U, int32x4, i32x4, float64x2, 2, 0,
                   uint32_t, static_cast<double>(a))
      CONVERT_CASE(I32x4TruncSatF64x2SZero, float64x2, f64x2, int32x4, 2, 0,
                   double, base::saturated_cast<int32_t>(a))
      CONVERT_CASE(I32x4TruncSatF64x2UZero, float64x2, f64x2, int32x4, 2, 0,
                   double, base::saturated_cast<uint32_t>(a))
      CONVERT_CASE(I32x4RelaxedTruncF64x2SZero, float64x2, f64x2, int32x4, 2, 0,
                   double, base::saturated_cast<int32_t>(a))
      CONVERT_CASE(I32x4RelaxedTruncF64x2UZero, float64x2, f64x2, int32x4, 2, 0,
                   double, base::saturated_cast<uint32_t>(a))
      CONVERT_CASE(F32x4DemoteF64x2Zero, float64x2, f64x2, float32x4, 2, 0,
                   float, DoubleToFloat32(a))
      CONVERT_CASE(F64x2PromoteLowF32x4, float32x4, f32x4, float64x2, 2, 0,
                   float, static_cast<double>(a))
#undef CONVERT_CASE

#define PACK_CASE(op, src_type, name, dst_type, count, dst_ctype) \
  case kExpr##op: {                                               \
    EMIT_INSTR_HANDLER(s2s_Simd##op);                             \
    S128Pop();                                                    \
    S128Pop();                                                    \
    S128Push();                                                   \
    return RegMode::kNoReg;                                       \
  }
      PACK_CASE(I16x8SConvertI32x4, int32x4, i32x4, int16x8, 8, int16_t)
      PACK_CASE(I16x8UConvertI32x4, int32x4, i32x4, int16x8, 8, uint16_t)
      PACK_CASE(I8x16SConvertI16x8, int16x8, i16x8, int8x16, 16, int8_t)
      PACK_CASE(I8x16UConvertI16x8, int16x8, i16x8, int8x16, 16, uint8_t)
#undef PACK_CASE

#define SELECT_CASE(op)               \
  case kExpr##op: {                   \
    EMIT_INSTR_HANDLER(s2s_Simd##op); \
    S128Pop();                        \
    S128Pop();                        \
    S128Pop();                        \
    S128Push();                       \
    return RegMode::kNoReg;           \
  }
      SELECT_CASE(I8x16RelaxedLaneSelect)
      SELECT_CASE(I16x8RelaxedLaneSelect)
      SELECT_CASE(I32x4RelaxedLaneSelect)
      SELECT_CASE(I64x2RelaxedLaneSelect)
      SELECT_CASE(S128Select)
#undef SELECT_CASE

    case kExprI32x4DotI16x8S: {
      EMIT_INSTR_HANDLER(s2s_SimdI32x4DotI16x8S);
      S128Pop();
      S128Pop();
      S128Push();
      return RegMode::kNoReg;
    }

    case kExprS128Const: {
      PushConstSlot<Simd128>(
          simd_immediates_[instr.optional.simd_immediate_index]);
      return RegMode::kNoReg;
    }

    case kExprI16x8DotI8x16I7x16S: {
      EMIT_INSTR_HANDLER(s2s_SimdI16x8DotI8x16I7x16S);
      S128Pop();
      S128Pop();
      S128Push();
      return RegMode::kNoReg;
    }

    case kExprI32x4DotI8x16I7x16AddS: {
      EMIT_INSTR_HANDLER(s2s_SimdI32x4DotI8x16I7x16AddS);
      S128Pop();
      S128Pop();
      S128Pop();
      S128Push();
      return RegMode::kNoReg;
    }

    case kExprI8x16RelaxedSwizzle: {
      EMIT_INSTR_HANDLER(s2s_SimdI8x16RelaxedSwizzle);
      S128Pop();
      S128Pop();
      S128Push();
      return RegMode::kNoReg;
    }

    case kExprI8x16Swizzle: {
      EMIT_INSTR_HANDLER(s2s_SimdI8x16Swizzle);
      S128Pop();
      S128Pop();
      S128Push();
      return RegMode::kNoReg;
    }

    case kExprI8x16Shuffle: {
      uint32_t slot_index = CreateConstSlot(
          simd_immediates_[instr.optional.simd_immediate_index]);
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
      TracePushConstSlot(slot_index);
#endif  // V8_ENABLE_DRUMBRAKE_TRACING
      EMIT_INSTR_HANDLER(s2s_SimdI8x16Shuffle);
      PushSlot(slot_index);
      S128Pop();
      S128Pop();
      S128Pop();
      S128Push();
      return RegMode::kNoReg;
    }

    case kExprV128AnyTrue: {
      EMIT_INSTR_HANDLER(s2s_SimdV128AnyTrue);
      S128Pop();
      I32Push();
      return RegMode::kNoReg;
    }

#define REDUCTION_CASE(op, name, stype, count, operation) \
  case kExpr##op: {                                       \
    EMIT_INSTR_HANDLER(s2s_Simd##op);                     \
    S128Pop();                                            \
    I32Push();                                            \
    return RegMode::kNoReg;                               \
  }
      REDUCTION_CASE(I64x2AllTrue, i64x2, int64x2, 2, &)
      REDUCTION_CASE(I32x4AllTrue, i32x4, int32x4, 4, &)
      REDUCTION_CASE(I16x8AllTrue, i16x8, int16x8, 8, &)
      REDUCTION_CASE(I8x16AllTrue, i8x16, int8x16, 16, &)
#undef REDUCTION_CASE

#define QFM_CASE(op, name, stype, count, operation) \
  case kExpr##op: {                                 \
    EMIT_INSTR_HANDLER(s2s_Simd##op);               \
    S128Pop();                                      \
    S128Pop();                                      \
    S128Pop();                                      \
    S128Push();                                     \
    return RegMode::kNoReg;                         \
  }
      QFM_CASE(F32x4Qfma, f32x4, float32x4, 4, +)
      QFM_CASE(F32x4Qfms, f32x4, float32x4, 4, -)
      QFM_CASE(F64x2Qfma, f64x2, float64x2, 2, +)
      QFM_CASE(F64x2Qfms, f64x2, float64x2, 2, -)
#undef QFM_CASE

#define LOAD_SPLAT_CASE(op)                                                  \
  case kExprS128##op: {                                                      \
    EMIT_MEM64_INSTR_HANDLER_WITH_PC(                                        \
        s2s_SimdS128##op, s2s_SimdS128##op##_Idx64, is_memory64_, instr.pc); \
    EmitMemoryOffset(instr.optional.offset);                                 \
    MemIndexPop();                                                           \
    S128Push();                                                              \
    return RegMode::kNoReg;                                                  \
  }
      LOAD_SPLAT_CASE(Load8Splat)
      LOAD_SPLAT_CASE(Load16Splat)
      LOAD_SPLAT_CASE(Load32Splat)
      LOAD_SPLAT_CASE(Load64Splat)
#undef LOAD_SPLAT_CASE

#define LOAD_EXTEND_CASE(op)                                                 \
  case kExprS128##op: {                                                      \
    EMIT_MEM64_INSTR_HANDLER_WITH_PC(                                        \
        s2s_SimdS128##op, s2s_SimdS128##op##_Idx64, is_memory64_, instr.pc); \
    EmitMemoryOffset(instr.optional.offset);                                 \
    MemIndexPop();                                                           \
    S128Push();                                                              \
    return RegMode::kNoReg;                                                  \
  }
      LOAD_EXTEND_CASE(Load8x8S)
      LOAD_EXTEND_CASE(Load8x8U)
      LOAD_EXTEND_CASE(Load16x4S)
      LOAD_EXTEND_CASE(Load16x4U)
      LOAD_EXTEND_CASE(Load32x2S)
      LOAD_EXTEND_CASE(Load32x2U)
#undef LOAD_EXTEND_CASE

#define LOAD_ZERO_EXTEND_CASE(op, load_type)                                 \
  case kExprS128##op: {                                                      \
    EMIT_MEM64_INSTR_HANDLER_WITH_PC(                                        \
        s2s_SimdS128##op, s2s_SimdS128##op##_Idx64, is_memory64_, instr.pc); \
    EmitMemoryOffset(instr.optional.offset);                                 \
    MemIndexPop();                                                           \
    S128Push();                                                              \
    return RegMode::kNoReg;                                                  \
  }
      LOAD_ZERO_EXTEND_CASE(Load32Zero, I32)
      LOAD_ZERO_EXTEND_CASE(Load64Zero, I64)
#undef LOAD_ZERO_EXTEND_CASE

#define LOAD_LANE_CASE(op)                                                   \
  case kExprS128##op: {                                                      \
    EMIT_MEM64_INSTR_HANDLER_WITH_PC(                                        \
        s2s_SimdS128##op, s2s_SimdS128##op##_Idx64, is_memory64_, instr.pc); \
    S128Pop();                                                               \
    EmitMemoryOffset(instr.optional.simd_loadstore_lane.offset);             \
    MemIndexPop();                                                           \
    /* emit 8 bits ? */                                                      \
    EmitI16Const(instr.optional.simd_loadstore_lane.lane);                   \
    S128Push();                                                              \
    return RegMode::kNoReg;                                                  \
  }
      LOAD_LANE_CASE(Load8Lane)
      LOAD_LANE_CASE(Load16Lane)
      LOAD_LANE_CASE(Load32Lane)
      LOAD_LANE_CASE(Load64Lane)
#undef LOAD_LANE_CASE

#define STORE_LANE_CASE(op)                                                  \
  case kExprS128##op: {                                                      \
    EMIT_MEM64_INSTR_HANDLER_WITH_PC(                                        \
        s2s_SimdS128##op, s2s_SimdS128##op##_Idx64, is_memory64_, instr.pc); \
    S128Pop();                                                               \
    EmitMemoryOffset(instr.optional.simd_loadstore_lane.offset);             \
    MemIndexPop();                                                           \
    /* emit 8 bits ? */                                                      \
    EmitI16Const(instr.optional.simd_loadstore_lane.lane);                   \
    return RegMode::kNoReg;                                                  \
  }
      STORE_LANE_CASE(Store8Lane)
      STORE_LANE_CASE(Store16Lane)
      STORE_LANE_CASE(Store32Lane)
      STORE_LANE_CASE(Store64Lane)
#undef STORE_LANE_CASE

#define EXT_ADD_PAIRWISE_CASE(op)     \
  case kExpr##op: {                   \
    EMIT_INSTR_HANDLER(s2s_Simd##op); \
    S128Pop();                        \
    S128Push();                       \
    return RegMode::kNoReg;           \
  }
      EXT_ADD_PAIRWISE_CASE(I32x4ExtAddPairwiseI16x8S)
      EXT_ADD_PAIRWISE_CASE(I32x4ExtAddPairwiseI16x8U)
      EXT_ADD_PAIRWISE_CASE(I16x8ExtAddPairwiseI8x16S)
      EXT_ADD_PAIRWISE_CASE(I16x8ExtAddPairwiseI8x16U)
#undef EXT_ADD_PAIRWISE_CASE

    default:
      FATAL("Unknown or unimplemented opcode #%d:%s",
            wasm_code_->start[instr.pc],
            WasmOpcodes::OpcodeName(
                static_cast<WasmOpcode>(wasm_code_->start[instr.pc])));
      UNREACHABLE();
  }

  return RegMode::kNoReg;
}

bool WasmBytecodeGenerator::EncodeSuperInstruction(
    RegMode& reg_mode, const WasmInstruction& curr_instr,
    const WasmInstruction& next_instr) {
  if (!v8_flags.drumbrake_compact_bytecode) {
    DCHECK_EQ(handler_size_, InstrHandlerSize::Large);
    return DoEncodeSuperInstruction(reg_mode, curr_instr, next_instr);
  }

  size_t current_instr_code_offset = code_.size();
  size_t current_slots_size = slots_.size();
  current_instr_encoding_failed_ = false;
  handler_size_ = InstrHandlerSize::Small;
  stack_.clear_history();

  bool result = DoEncodeSuperInstruction(reg_mode, curr_instr, next_instr);
  if (result && current_instr_encoding_failed_) {
    code_.resize(current_instr_code_offset);
    slots_.resize(current_slots_size);
    stack_.rollback();
    current_instr_encoding_failed_ = false;
    handler_size_ = InstrHandlerSize::Large;
    result = DoEncodeSuperInstruction(reg_mode, curr_instr, next_instr);
    DCHECK(!current_instr_encoding_failed_);
  }

  return result;
}

bool WasmBytecodeGenerator::DoEncodeSuperInstruction(
    RegMode& reg_mode, const WasmInstruction& curr_instr,
    const WasmInstruction& next_instr) {
  if (curr_instr.orig >= kExprI32LoadMem &&
      curr_instr.orig <= kExprI64LoadMem32U &&
      next_instr.orig == kExprLocalSet) {
    // Do not optimize if we are updating a shared slot.
    uint32_t to_stack_index = next_instr.optional.index;
    if (HasSharedSlot(to_stack_index)) return false;

    switch (curr_instr.orig) {
// The implementation of r2s_LoadMem_LocalSet is identical to the
// implementation of r2s_LoadMem, so we can reuse the same builtin.
#define LOAD_CASE(name, ctype, mtype, rep, type)                           \
  case kExpr##name: {                                                      \
    if (reg_mode == RegMode::kNoReg) {                                     \
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_##name##_LocalSet,              \
                                       s2s_##name##_LocalSet_Idx64,        \
                                       is_memory64_, curr_instr.pc);       \
      EmitMemoryOffset(curr_instr.optional.offset);                        \
      MemIndexPop();                                                       \
      EmitSlotOffset(slots_[stack_[to_stack_index]].slot_offset);          \
      reg_mode = RegMode::kNoReg;                                          \
    } else {                                                               \
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(r2s_##name, r2s_##name##_Idx64,     \
                                       is_memory64_, curr_instr.pc);       \
      EmitMemoryOffset(static_cast<uint64_t>(curr_instr.optional.offset)); \
      EmitSlotOffset(slots_[stack_[to_stack_index]].slot_offset);          \
      reg_mode = RegMode::kNoReg;                                          \
    }                                                                      \
    return true;                                                           \
  }
      LOAD_CASE(I32LoadMem8S, int32_t, int8_t, kWord8, I32);
      LOAD_CASE(I32LoadMem8U, int32_t, uint8_t, kWord8, I32);
      LOAD_CASE(I32LoadMem16S, int32_t, int16_t, kWord16, I32);
      LOAD_CASE(I32LoadMem16U, int32_t, uint16_t, kWord16, I32);
      LOAD_CASE(I64LoadMem8S, int64_t, int8_t, kWord8, I64);
      LOAD_CASE(I64LoadMem8U, int64_t, uint8_t, kWord16, I64);
      LOAD_CASE(I64LoadMem16S, int64_t, int16_t, kWord16, I64);
      LOAD_CASE(I64LoadMem16U, int64_t, uint16_t, kWord16, I64);
      LOAD_CASE(I64LoadMem32S, int64_t, int32_t, kWord32, I64);
      LOAD_CASE(I64LoadMem32U, int64_t, uint32_t, kWord32, I64);
      LOAD_CASE(I32LoadMem, int32_t, int32_t, kWord32, I32);
      LOAD_CASE(I64LoadMem, int64_t, int64_t, kWord64, I64);
      LOAD_CASE(F32LoadMem, Float32, uint32_t, kFloat32, F32);
      LOAD_CASE(F64LoadMem, Float64, uint64_t, kFloat64, F64);
#undef LOAD_CASE

      default:
        return false;
    }
  } else if (curr_instr.orig == kExprI32LoadMem &&
             next_instr.orig == kExprI32StoreMem) {
    if (reg_mode == RegMode::kNoReg) {
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_I32LoadStoreMem,
                                       s2s_I32LoadStoreMem_Idx64, is_memory64_,
                                       curr_instr.pc);
      EmitMemoryOffset(curr_instr.optional.offset);  // load_offset
      MemIndexPop();                                 // load_index
    } else {
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(r2s_I32LoadStoreMem,
                                       r2s_I32LoadStoreMem_Idx64, is_memory64_,
                                       curr_instr.pc);
      EmitMemoryOffset(curr_instr.optional.offset);  // load_offset
    }
    EmitMemoryOffset(next_instr.optional.offset);  // store_offset
    MemIndexPop();                                 // store_index
    reg_mode = RegMode::kNoReg;
    return true;
  } else if (curr_instr.orig == kExprI64LoadMem &&
             next_instr.orig == kExprI64StoreMem) {
    if (reg_mode == RegMode::kNoReg) {
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_I64LoadStoreMem,
                                       s2s_I64LoadStoreMem_Idx64, is_memory64_,
                                       curr_instr.pc);
      EmitMemoryOffset(curr_instr.optional.offset);
      MemIndexPop();
    } else {
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(r2s_I64LoadStoreMem,
                                       r2s_I64LoadStoreMem_Idx64, is_memory64_,
                                       curr_instr.pc);
      EmitMemoryOffset(curr_instr.optional.offset);
    }
    EmitMemoryOffset(next_instr.optional.offset);
    MemIndexPop();
    reg_mode = RegMode::kNoReg;
    return true;
  } else if (curr_instr.orig == kExprF32LoadMem &&
             next_instr.orig == kExprF32StoreMem) {
    if (reg_mode == RegMode::kNoReg) {
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_F32LoadStoreMem,
                                       s2s_F32LoadStoreMem_Idx64, is_memory64_,
                                       curr_instr.pc);
      EmitMemoryOffset(curr_instr.optional.offset);
      MemIndexPop();
    } else {
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(r2s_F32LoadStoreMem,
                                       r2s_F32LoadStoreMem_Idx64, is_memory64_,
                                       curr_instr.pc);
      EmitMemoryOffset(curr_instr.optional.offset);
    }
    EmitMemoryOffset(next_instr.optional.offset);
    MemIndexPop();
    reg_mode = RegMode::kNoReg;
    return true;
  } else if (curr_instr.orig == kExprF64LoadMem &&
             next_instr.orig == kExprF64StoreMem) {
    if (reg_mode == RegMode::kNoReg) {
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_F64LoadStoreMem,
                                       s2s_F64LoadStoreMem_Idx64, is_memory64_,
                                       curr_instr.pc);
      EmitMemoryOffset(curr_instr.optional.offset);
      MemIndexPop();
    } else {
      EMIT_MEM64_INSTR_HANDLER_WITH_PC(r2s_F64LoadStoreMem,
                                       r2s_F64LoadStoreMem_Idx64, is_memory64_,
                                       curr_instr.pc);
      EmitMemoryOffset(curr_instr.optional.offset);
    }
    EmitMemoryOffset(next_instr.optional.offset);
    MemIndexPop();
    reg_mode = RegMode::kNoReg;
    return true;
  } else if (curr_instr.orig >= kExprI32Const &&
             curr_instr.orig <= kExprF32Const &&
             next_instr.orig == kExprLocalSet) {
    uint32_t to_stack_index = next_instr.optional.index;
    switch (curr_instr.orig) {
      case kExprI32Const: {
        uint32_t from_slot_index =
            CreateConstSlot<int32_t>(curr_instr.optional.i32);
        CopyToSlot(kWasmI32, from_slot_index, to_stack_index, false);
        reg_mode = RegMode::kNoReg;
        return true;
      }
      case kExprI64Const: {
        uint32_t from_slot_index =
            CreateConstSlot<int64_t>(curr_instr.optional.i64);
        CopyToSlot(kWasmI64, from_slot_index, to_stack_index, false);
        reg_mode = RegMode::kNoReg;
        return true;
      }
      case kExprF32Const: {
        uint32_t from_slot_index =
            CreateConstSlot<float>(curr_instr.optional.f32);
        CopyToSlot(kWasmF32, from_slot_index, to_stack_index, false);
        reg_mode = RegMode::kNoReg;
        return true;
      }
      case kExprF64Const: {
        uint32_t from_slot_index =
            CreateConstSlot<double>(curr_instr.optional.f64);
        CopyToSlot(kWasmF64, from_slot_index, to_stack_index, false);
        reg_mode = RegMode::kNoReg;
        return true;
      }
      default:
        return false;
    }
  } else if (curr_instr.orig == kExprLocalGet &&
             next_instr.orig >= kExprI32StoreMem &&
             next_instr.orig <= kExprI64StoreMem32) {
    switch (next_instr.orig) {
// The implementation of r2s_LocalGet_StoreMem is identical to the
// implementation of r2s_StoreMem, so we can reuse the same builtin.
#define STORE_CASE(name, ctype, mtype, rep, type)                          \
  case kExpr##name: {                                                      \
    EMIT_MEM64_INSTR_HANDLER_WITH_PC(s2s_##name, s2s_##name##_Idx64,       \
                                     is_memory64_, curr_instr.pc);         \
    EmitSlotOffset(slots_[stack_[curr_instr.optional.index]].slot_offset); \
    EmitMemoryOffset(next_instr.optional.offset);                          \
    MemIndexPop();                                                         \
    reg_mode = RegMode::kNoReg;                                            \
    return true;                                                           \
  }
      STORE_CASE(I32StoreMem8, int32_t, int8_t, kWord8, I32);
      STORE_CASE(I32StoreMem16, int32_t, int16_t, kWord16, I32);
      STORE_CASE(I64StoreMem8, int64_t, int8_t, kWord8, I64);
      STORE_CASE(I64StoreMem16, int64_t, int16_t, kWord16, I64);
      STORE_CASE(I64StoreMem32, int64_t, int32_t, kWord32, I64);
      STORE_CASE(I32StoreMem, int32_t, int32_t, kWord32, I32);
      STORE_CASE(I64StoreMem, int64_t, int64_t, kWord64, I64);
      STORE_CASE(F32StoreMem, Float32, uint32_t, kFloat32, F32);
      STORE_CASE(F64StoreMem, Float64, uint64_t, kFloat64, F64);
#undef STORE_CASE

      default:
        return false;
    }
  }

  return false;
}

std::unique_ptr<WasmBytecode> WasmBytecodeGenerator::GenerateBytecode() {
#ifdef V8_ENABLE_DRUMBRAKE_TRACING
  if (v8_flags.trace_drumbrake_bytecode_generator) {
    printf("\nGenerate bytecode for function: %d\n", function_index_);
  }
#endif  // V8_ENABLE_DRUMBRAKE_TRACING

  uint32_t const_slots = ScanConstInstructions();
  const_slots_values_.resize(const_slots * kSlotSize);

  pc_t pc = wasm_code_->locals.encoded_size;
  RegMode reg_mode = RegMode::kNoReg;

  Decoder decoder(wasm_code_->start, wasm_code_->end);

  current_block_index_ = -1;

  // Init stack_ with return values, args and local types.

  for (uint32_t index = 0; index < return_count_; index++) {
    CreateSlot(wasm_code_->function->sig->GetReturn(index));
  }

  for (uint32_t index = 0; index < args_count_; index++) {
    _PushSlot(wasm_code_->function->sig->GetParam(index));
  }

  // Reserve space for const slots
  slot_offset_ += const_slots;

  for (uint32_t index = 0; index < wasm_code_->locals.num_locals; index++) {
    _PushSlot(wasm_code_->locals.local_types[index]);
  }

  current_block_index_ =
      BeginBlock(kExprBlock, {wasm_code_->function->sig_index, kBottom});

  WasmInstruction curr_instr;
  WasmInstruction next_instr;

  pc_t limit = wasm_code_->end - wasm_code_->start;
  while (pc < limit) {
    DCHECK_NOT_NULL(wasm_code_->start);

    if (!curr_instr) {
      curr_instr = DecodeInstruction(pc, decoder);
      if (curr_instr) pc += curr_instr.length;
    }
    if (!curr_instr) break;
    DCHECK(!next_instr);
    next_instr = DecodeInstruction(pc, decoder);
    if (next_instr) pc += next_instr.length;

    if (next_instr) {
      if (v8_flags.drumbrake_super_instructions && is_instruction_reachable_ &&
          EncodeSuperInstruction(reg_mode, curr_instr, next_instr)) {
        curr_instr = {};
        next_instr = {};
      } else {
        reg_mode =
            EncodeInstruction(curr_instr, reg_mode, next_instr.InputRegMode());
        curr_instr = next_instr;
        next_instr = {};
      }
    } else {
      reg_mode = EncodeInstruction(curr_instr, reg_mode, RegMode::kNoReg);
      curr_instr = {};
    }

    if (pc == limit && curr_instr) {
      reg_mode = EncodeInstruction(curr_instr, reg_mode, RegMode::kNoReg);
    }
  }

  PatchLoopBeginInstructions();
  PatchBranchOffsets();

  total_bytecode_size_ += code_.size();

  return std::make_unique<WasmBytecode>(
      function_index_, code_.data(), code_.size(), slot_offset_,
      module_->functions[function_index_].sig, wasm_code_, blocks_.size(),
      const_slots_values_.data(), const_slots_values_.size(), ref_slots_count_,
      std::move(eh_data_), std::move(code_pc_map_));
}

int32_t WasmBytecodeGenerator::BeginBlock(
    WasmOpcode opcode, const WasmInstruction::Optional::Block signature) {
  int32_t block_index = static_cast<int32_t>(blocks_.size());
  uint32_t stack_size = this->stack_size();

  uint32_t first_block_index = 0;
  size_t rets_slots_count = 0;
  size_t params_slots_count = 0;
  if (block_index > 0 && (opcode != kExprElse && opcode != kExprCatch &&
                          opcode != kExprCatchAll)) {
    first_block_index = ReserveBlockSlots(opcode, signature, &rets_slots_count,
                                          &params_slots_count);
  }

  uint32_t parent_block_index = current_block_index_;
  if (opcode == kExprCatch || opcode == kExprCatchAll) {
    parent_block_index =
        blocks_[eh_data_.GetCurrentTryBlockIndex()].parent_block_index_;
  }

  blocks_.emplace_back(opcode, CurrentCodePos(), parent_block_index, stack_size,
                       signature, first_block_index, rets_slots_count,
                       params_slots_count, eh_data_.GetCurrentTryBlockIndex());
  current_block_index_ = block_index;

  if (opcode == kExprIf && params_slots_count > 0) {
    DCHECK_GE(stack_size, params_slots_count);
    blocks_.back().SaveParams(&stack_[stack_size - params_slots_count],
                              params_slots_count);
  }

  if (opcode == kExprLoop) {
    StoreBlockParamsIntoSlots(current_block_index_, true);
    loop_begin_code_offsets_.push_back(CurrentCodePos());
    blocks_[current_block_index_].begin_code_offset_ = CurrentCodePos();
    last_instr_offset_ = kInvalidCodeOffset;

    START_EMIT_INSTR_HANDLER_WITH_ID(s2s_OnLoopBegin) {}
    END_EMIT_INSTR_HANDLER()
  }
  return current_block_index_;
}

int WasmBytecodeGenerator::GetCurrentTryBlockIndex(
    bool return_matching_try_for_catch_blocks) const {
  DCHECK_GE(current_block_index_, 0);
  int index = current_block_index_;
  while (index >= 0) {
    const auto& block = blocks_[index];
    if (block.IsTry()) return index;
    if (return_matching_try_for_catch_blocks &&
        (block.IsCatch() || block.IsCatchAll())) {
      return block.parent_try_block_index_;
    }
    index = blocks_[index].parent_block_index_;
  }
  return -1;
}

void WasmBytecodeGenerator::PatchLoopBeginInstructions() {
  if (ref_slots_count_ == 0) {
    for (size_t i = 0; i < loop_begin_code_offsets_.size(); i++) {
      base::WriteUnalignedValue<InstructionHandler>(
          reinterpret_cast<Address>(code_.data() + loop_begin_code_offsets_[i]),
          k_s2s_OnLoopBeginNoRefSlots);
    }
  }
}

void WasmBytecodeGenerator::PatchBranchOffsets() {
  static const uint32_t kElseBlockStartOffset =
      sizeof(InstructionHandler) + sizeof(uint32_t);

  for (int block_index = 0; block_index < static_cast<int>(blocks_.size());
       block_index++) {
    const BlockData block_data = blocks_[block_index];
    for (size_t i = 0; i < block_data.branch_code_offsets_.size(); i++) {
      uint32_t current_code_offset = block_data.branch_code_offsets_[i];
      uint32_t target_offset = block_data.end_code_offset_;
      if (block_data.IsLoop()) {
        target_offset = block_data.begin_code_offset_;
      } else if (block_data.IsIf() && block_data.if_else_block_index_ >= 0 &&
                 current_code_offset == block_data.begin_code_offset_) {
        // Jumps to the 'else' branch.
        target_offset =
            blocks_[block_data.if_else_block_index_].begin_code_offset_ +
            kElseBlockStartOffset;
      } else if ((block_data.IsCatch() || block_data.IsCatchAll()) &&
                 current_code_offset == block_data.begin_code_offset_ +
                                            sizeof(InstructionHandler)) {
        // Jumps to the end of a sequence of 'try'/'catch' branches.
        target_offset = static_cast<uint32_t>(
            eh_data_.GetEndInstructionOffsetFor(block_index));
      }

      int32_t delta = target_offset - current_code_offset;
      base::WriteUnalignedValue<uint32_t>(
          reinterpret_cast<Address>(code_.data() + current_code_offset), delta);
    }
  }
}

bool WasmBytecodeGenerator::TryCompactInstructionHandler(
    InstructionHandler func_id) {
  if (last_instr_offset_ == kInvalidCodeOffset) return false;
  InstructionHandler* prev_instr_addr =
      reinterpret_cast<InstructionHandler*>(code_.data() + last_instr_offset_);
  InstructionHandler prev_instr_handler = *prev_instr_addr;
  if (func_id == k_s2s_CopySlot32 && prev_instr_handler == k_s2s_CopySlot32) {
    // Tranforms:
    //  [CopySlot32: InstrId][from: slot_offset_t][to: slot_offset_t]
    // into:
    //  [CopySlot32x2: InstrId][from0: slot_offset_t][to0: slot_offset_t][from1:
    //  slot_offset_t][to1: slot_offset_t]
    DCHECK(handler_size_ == InstrHandlerSize::Large ||
           v8_flags.drumbrake_compact_bytecode);
    base::WriteUnalignedValue<InstructionHandler>(
        reinterpret_cast<Address>(prev_instr_addr), k_s2s_CopySlot32x2);
    return true;
  } else if (func_id == k_s2s_CopySlot64 &&
             prev_instr_handler == k_s2s_CopySlot64) {
    DCHECK(handler_size_ == InstrHandlerSize::Large ||
           v8_flags.drumbrake_compact_bytecode);
    base::WriteUnalignedValue<InstructionHandler>(
        reinterpret_cast<Address>(prev_instr_addr), k_s2s_CopySlot64x2);
    return true;
  }
  return false;
}

ClearThreadInWasmScope::ClearThreadInWasmScope(Isolate* isolate)
    : isolate_(isolate) {
  DCHECK_IMPLIES(trap_handler::IsTrapHandlerEnabled(),
                 trap_handler::IsThreadInWasm());
  trap_handler::ClearThreadInWasm();
}

ClearThreadInWasmScope ::~ClearThreadInWasmScope() {
  DCHECK_IMPLIES(trap_handler::IsTrapHandlerEnabled(),
                 !trap_handler::IsThreadInWasm());
  if (!isolate_->has_exception()) {
    trap_handler::SetThreadInWasm();
  }
  // Otherwise we only want to set the flag if the exception is caught in
  // wasm. This is handled by the unwinder.
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
