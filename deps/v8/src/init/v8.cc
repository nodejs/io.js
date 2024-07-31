// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/init/v8.h"

#include <fstream>

#include "include/cppgc/platform.h"
#include "src/api/api.h"
#include "src/base/atomicops.h"
#include "src/base/once.h"
#include "src/base/platform/platform.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/interface-descriptors.h"
#include "src/common/code-memory-access.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/frames.h"
#include "src/execution/isolate.h"
#include "src/execution/simulator.h"
#include "src/init/bootstrapper.h"
#include "src/libsampler/sampler.h"
#include "src/objects/elements.h"
#include "src/objects/objects-inl.h"
#include "src/profiler/heap-profiler.h"
#include "src/sandbox/sandbox.h"
#include "src/snapshot/snapshot.h"
#if defined(V8_USE_PERFETTO)
#include "src/tracing/code-data-source.h"
#endif  // defined(V8_USE_PERFETTO)
#include "src/tracing/tracing-category-observer.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-engine.h"
#endif  // V8_ENABLE_WEBASSEMBLY

#if defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
#include "src/diagnostics/etw-jit-win.h"
#endif

namespace v8 {
namespace internal {

// static
v8::Platform* V8::platform_ = nullptr;
const OOMDetails V8::kNoOOMDetails{false, nullptr};
const OOMDetails V8::kHeapOOM{true, nullptr};

namespace {
enum class V8StartupState {
  kIdle,
  kPlatformInitializing,
  kPlatformInitialized,
  kV8Initializing,
  kV8Initialized,
  kV8Disposing,
  kV8Disposed,
  kPlatformDisposing,
  kPlatformDisposed
};

std::atomic<V8StartupState> v8_startup_state_(V8StartupState::kIdle);

void AdvanceStartupState(V8StartupState expected_next_state) {
  V8StartupState current_state = v8_startup_state_;
  CHECK_NE(current_state, V8StartupState::kPlatformDisposed);
  V8StartupState next_state =
      static_cast<V8StartupState>(static_cast<int>(current_state) + 1);
  if (next_state != expected_next_state) {
    // Ensure the following order:
    // v8::V8::InitializePlatform(platform);
    // v8::V8::Initialize();
    // v8::Isolate* isolate = v8::Isolate::New(...);
    // ...
    // isolate->Dispose();
    // v8::V8::Dispose();
    // v8::V8::DisposePlatform();
    FATAL("Wrong initialization order: from %d to %d, expected to %d!",
          static_cast<int>(current_state), static_cast<int>(next_state),
          static_cast<int>(expected_next_state));
  }
  if (!v8_startup_state_.compare_exchange_strong(current_state, next_state)) {
    FATAL(
        "Multiple threads are initializating V8 in the wrong order: expected "
        "%d got %d!",
        static_cast<int>(current_state),
        static_cast<int>(v8_startup_state_.load()));
  }
}

}  // namespace

#ifdef V8_USE_EXTERNAL_STARTUP_DATA
V8_DECLARE_ONCE(init_snapshot_once);
#endif

// static
void V8::InitializePlatform(v8::Platform* platform) {
  AdvanceStartupState(V8StartupState::kPlatformInitializing);
  CHECK(!platform_);
  CHECK_NOT_NULL(platform);
  platform_ = platform;
  v8::base::SetPrintStackTrace(platform_->GetStackTracePrinter());
  v8::tracing::TracingCategoryObserver::SetUp();
#if defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
  if (v8_flags.enable_etw_stack_walking) {
    // TODO(sartang@microsoft.com): Move to platform specific diagnostics object
    v8::internal::ETWJITInterface::Register();
  }
#endif

  // Initialization needs to happen on platform-level, as this sets up some
  // cppgc internals that are needed to allow gracefully failing during cppgc
  // platform setup.
  CppHeap::InitializeOncePerProcess();

  AdvanceStartupState(V8StartupState::kPlatformInitialized);
}

// static
void V8::InitializePlatformForTesting(v8::Platform* platform) {
  if (v8_startup_state_ != V8StartupState::kIdle) {
    FATAL(
        "The platform was initialized before. Note that running multiple tests "
        "in the same process is not supported.");
  }
  V8::InitializePlatform(platform);
}

#define DISABLE_FLAG(flag)                                                    \
  if (v8_flags.flag) {                                                        \
    PrintF(stderr,                                                            \
           "Warning: disabling flag --" #flag " due to conflicting flags\n"); \
    v8_flags.flag = false;                                                    \
  }

void V8::Initialize() {
  AdvanceStartupState(V8StartupState::kV8Initializing);
  CHECK(platform_);

  // Update logging information before enforcing flag implications.
  FlagValue<bool>* log_all_flags[] = {
      &v8_flags.log_all,
      &v8_flags.log_code,
      &v8_flags.log_code_disassemble,
      &v8_flags.log_deopt,
      &v8_flags.log_feedback_vector,
      &v8_flags.log_function_events,
      &v8_flags.log_ic,
      &v8_flags.log_maps,
      &v8_flags.log_source_code,
      &v8_flags.log_source_position,
      &v8_flags.log_timer_events,
      &v8_flags.prof,
      &v8_flags.prof_cpp,
  };
  if (v8_flags.log_all) {
    // Enable all logging flags
    for (auto* flag : log_all_flags) {
      *flag = true;
    }
    v8_flags.log = true;
  } else if (!v8_flags.log) {
    // Enable --log if any log flag is set.
    for (const auto* flag : log_all_flags) {
      if (!*flag) continue;
      v8_flags.log = true;
      break;
    }
    // Profiling flags depend on logging.
    v8_flags.log = v8_flags.log || v8_flags.perf_prof ||
                   v8_flags.perf_basic_prof || v8_flags.ll_prof ||
                   v8_flags.prof || v8_flags.prof_cpp || v8_flags.gdbjit;
  }

  FlagList::EnforceFlagImplications();

  if (v8_flags.predictable && v8_flags.random_seed == 0) {
    // Avoid random seeds in predictable mode.
    v8_flags.random_seed = 12347;
  }

  if (v8_flags.stress_compaction) {
    v8_flags.force_marking_deque_overflows = true;
    v8_flags.gc_global = true;
    v8_flags.max_semi_space_size = 1;
  }

  if (v8_flags.trace_turbo) {
    // Create an empty file shared by the process (e.g. the wasm engine).
    std::ofstream(Isolate::GetTurboCfgFileName(nullptr).c_str(),
                  std::ios_base::trunc);
  }

  // Do not expose wasm in jitless mode.
  //
  // Even in interpreter-only mode, wasm currently still creates executable
  // memory at runtime. Unexpose wasm until this changes.
  // The correctness fuzzers are a special case: many of their test cases are
  // built by fetching a random property from the the global object, and thus
  // the global object layout must not change between configs. That is why we
  // continue exposing wasm on correctness fuzzers even in jitless mode.
  // TODO(jgruber): Remove this once / if wasm can run without executable
  // memory.
#if V8_ENABLE_WEBASSEMBLY
  if (v8_flags.jitless && !v8_flags.correctness_fuzzer_suppressions) {
    DISABLE_FLAG(expose_wasm);
  }
#endif

  // When fuzzing and concurrent compilation is enabled, disable Turbofan
  // tracing flags since reading/printing heap state is not thread-safe and
  // leads to false positives on TSAN bots.
  // TODO(chromium:1205289): Teach relevant fuzzers to not pass TF tracing
  // flags instead, and remove this section.
  if (v8_flags.fuzzing && v8_flags.concurrent_recompilation) {
    DISABLE_FLAG(trace_turbo);
    DISABLE_FLAG(trace_turbo_graph);
    DISABLE_FLAG(trace_turbo_scheduled);
    DISABLE_FLAG(trace_turbo_reduction);
#ifdef V8_ENABLE_SLOW_TRACING
    // If expensive tracing is disabled via a build flag, the following flags
    // cannot be disabled (because they are already).
    DISABLE_FLAG(trace_turbo_trimming);
    DISABLE_FLAG(trace_turbo_jt);
    DISABLE_FLAG(trace_turbo_ceq);
    DISABLE_FLAG(trace_turbo_loop);
    DISABLE_FLAG(trace_turbo_alloc);
    DISABLE_FLAG(trace_all_uses);
    DISABLE_FLAG(trace_representation);
#endif
    DISABLE_FLAG(trace_turbo_stack_accesses);
  }

  // The --jitless and --interpreted-frames-native-stack flags are incompatible
  // since the latter requires code generation while the former prohibits code
  // generation.
  CHECK(!v8_flags.interpreted_frames_native_stack || !v8_flags.jitless);

  base::AbortMode abort_mode = base::AbortMode::kDefault;

  if (v8_flags.soft_abort) {
    abort_mode = base::AbortMode::kSoft;
  } else if (v8_flags.hard_abort) {
    abort_mode = base::AbortMode::kHard;
  }

  base::OS::Initialize(abort_mode, v8_flags.gc_fake_mmap);

  if (v8_flags.random_seed) {
    GetPlatformPageAllocator()->SetRandomMmapSeed(v8_flags.random_seed);
    GetPlatformVirtualAddressSpace()->SetRandomSeed(v8_flags.random_seed);
  }

  if (v8_flags.print_flag_values) FlagList::PrintValues();

  // Initialize the default FlagList::Hash.
  FlagList::Hash();

  // Before initializing internals, freeze the flags such that further changes
  // are not allowed. Global initialization of the Isolate or the WasmEngine
  // already reads flags, so they should not be changed afterwards.
  if (v8_flags.freeze_flags_after_init) FlagList::FreezeFlags();

#if defined(V8_ENABLE_SANDBOX)
  // If enabled, the sandbox must be initialized first.
  GetProcessWideSandbox()->Initialize(GetPlatformVirtualAddressSpace());
  CHECK_EQ(kSandboxSize, GetProcessWideSandbox()->size());

  GetProcessWideCodePointerTable()->Initialize();
#endif

#if defined(V8_USE_PERFETTO)
  if (perfetto::Tracing::IsInitialized()) {
    TrackEvent::Register();
    if (v8_flags.perfetto_code_logger) {
      v8::internal::CodeDataSource::Register();
    }
  }
#endif
  IsolateAllocator::InitializeOncePerProcess();
  Isolate::InitializeOncePerProcess();

#if defined(USE_SIMULATOR)
  Simulator::InitializeOncePerProcess();
#endif
  CpuFeatures::Probe(false);
  ElementsAccessor::InitializeOncePerProcess();
  Bootstrapper::InitializeOncePerProcess();
  CallDescriptors::InitializeOncePerProcess();

  // Fetch the ThreadIsolatedAllocator once since we need to keep the pointer in
  // protected memory.
  ThreadIsolation::Initialize(
      GetCurrentPlatform()->GetThreadIsolatedAllocator());

#if V8_ENABLE_WEBASSEMBLY
  wasm::WasmEngine::InitializeOncePerProcess();
#endif  // V8_ENABLE_WEBASSEMBLY

  ExternalReferenceTable::InitializeOncePerProcess();

  AdvanceStartupState(V8StartupState::kV8Initialized);
}

#undef DISABLE_FLAG

void V8::Dispose() {
  AdvanceStartupState(V8StartupState::kV8Disposing);
  CHECK(platform_);
#if V8_ENABLE_WEBASSEMBLY
  wasm::WasmEngine::GlobalTearDown();
#endif  // V8_ENABLE_WEBASSEMBLY
#if defined(USE_SIMULATOR)
  Simulator::GlobalTearDown();
#endif
  CallDescriptors::TearDown();
  ElementsAccessor::TearDown();
  RegisteredExtension::UnregisterAll();
  FlagList::ReleaseDynamicAllocations();
  AdvanceStartupState(V8StartupState::kV8Disposed);
}

void V8::DisposePlatform() {
  AdvanceStartupState(V8StartupState::kPlatformDisposing);
  CHECK(platform_);
#if defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
  if (v8_flags.enable_etw_stack_walking) {
    v8::internal::ETWJITInterface::Unregister();
  }
#endif
  v8::tracing::TracingCategoryObserver::TearDown();
  v8::base::SetPrintStackTrace(nullptr);

#ifdef V8_ENABLE_SANDBOX
  // TODO(chromium:1218005) alternatively, this could move to its own
  // public TearDownSandbox function.
  GetProcessWideSandbox()->TearDown();
#endif  // V8_ENABLE_SANDBOX

  platform_ = nullptr;

#if DEBUG
  internal::ThreadIsolation::CheckTrackedMemoryEmpty();
#endif

  AdvanceStartupState(V8StartupState::kPlatformDisposed);
}

v8::Platform* V8::GetCurrentPlatform() {
  v8::Platform* platform = reinterpret_cast<v8::Platform*>(
      base::Relaxed_Load(reinterpret_cast<base::AtomicWord*>(&platform_)));
  DCHECK(platform);
  return platform;
}

void V8::SetPlatformForTesting(v8::Platform* platform) {
  base::Relaxed_Store(reinterpret_cast<base::AtomicWord*>(&platform_),
                      reinterpret_cast<base::AtomicWord>(platform));
}

void V8::SetSnapshotBlob(StartupData* snapshot_blob) {
#ifdef V8_USE_EXTERNAL_STARTUP_DATA
  base::CallOnce(&init_snapshot_once, &SetSnapshotFromFile, snapshot_blob);
#else
  UNREACHABLE();
#endif
}
}  // namespace internal

// static
double Platform::SystemClockTimeMillis() {
  return base::OS::TimeCurrentMillis();
}

// static
void ThreadIsolatedAllocator::SetDefaultPermissionsForSignalHandler() {
#if V8_HAS_PKU_JIT_WRITE_PROTECT
  internal::RwxMemoryWriteScope::SetDefaultPermissionsForSignalHandler();
#endif
}

}  // namespace v8
