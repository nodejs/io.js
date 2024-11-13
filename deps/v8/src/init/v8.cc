// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/init/v8.h"

#include <fstream>

#include "include/cppgc/platform.h"
#include "include/v8-sandbox.h"
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
#include "src/flags/flags.h"
#include "src/init/bootstrapper.h"
#include "src/libsampler/sampler.h"
#include "src/objects/elements.h"
#include "src/objects/objects-inl.h"
#include "src/profiler/heap-profiler.h"
#include "src/sandbox/hardware-support.h"
#include "src/sandbox/sandbox.h"
#include "src/sandbox/testing.h"
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

  FlagList::EnforceFlagImplications();

  // Initialize the default FlagList::Hash.
  FlagList::Hash();

  // Before initializing internals, freeze the flags such that further changes
  // are not allowed. Global initialization of the Isolate or the WasmEngine
  // already reads flags, so they should not be changed afterwards.
  if (v8_flags.freeze_flags_after_init) FlagList::FreezeFlags();

  if (v8_flags.trace_turbo) {
    // Create an empty file shared by the process (e.g. the wasm engine).
    std::ofstream(Isolate::GetTurboCfgFileName(nullptr).c_str(),
                  std::ios_base::trunc);
  }

  // The --jitless and --interpreted-frames-native-stack flags are incompatible
  // since the latter requires code generation while the former prohibits code
  // generation.
  CHECK(!v8_flags.interpreted_frames_native_stack || !v8_flags.jitless);

  base::AbortMode abort_mode = base::AbortMode::kDefault;

  if (v8_flags.sandbox_fuzzing || v8_flags.hole_fuzzing) {
    // In this mode, controlled crashes are harmless. Furthermore, DCHECK
    // failures should be ignored (and execution should continue past them) as
    // they may otherwise hide issues.
    abort_mode = base::AbortMode::kExitWithFailureAndIgnoreDcheckFailures;
  } else if (v8_flags.sandbox_testing) {
    // Similar to the above case, but here we want to exit with a status
    // indicating success (e.g. zero on unix). This is useful for example for
    // sandbox regression tests, which should "pass" if they crash in a
    // controlled fashion (e.g. in a SBXCHECK).
    abort_mode = base::AbortMode::kExitWithSuccessAndIgnoreDcheckFailures;
  } else if (v8_flags.hard_abort) {
    abort_mode = base::AbortMode::kImmediateCrash;
  }

  base::OS::Initialize(abort_mode, v8_flags.gc_fake_mmap);

  if (v8_flags.random_seed) {
    GetPlatformPageAllocator()->SetRandomMmapSeed(v8_flags.random_seed);
    GetPlatformVirtualAddressSpace()->SetRandomSeed(v8_flags.random_seed);
  }

  if (v8_flags.print_flag_values) FlagList::PrintValues();

  // Fetch the ThreadIsolatedAllocator once since we need to keep the pointer in
  // protected memory.
  ThreadIsolation::Initialize(
      GetCurrentPlatform()->GetThreadIsolatedAllocator());

#ifdef V8_ENABLE_SANDBOX
  // If enabled, the sandbox must be initialized first.
  GetProcessWideSandbox()->Initialize(GetPlatformVirtualAddressSpace());
  CHECK_EQ(kSandboxSize, GetProcessWideSandbox()->size());

  GetProcessWideCodePointerTable()->Initialize();
  JSDispatchTable::Initialize();

  // Enable sandbox testing mode if requested.
  //
  // This will install the sandbox crash filter to ignore all crashes that do
  // not represent sandbox violations.
  //
  // Note: this should happen before the Wasm trap handler is installed, so that
  // the wasm trap handler is invoked first (and can handle Wasm OOB accesses),
  // then forwards all "real" crashes to the sandbox crash filter.
  if (v8_flags.sandbox_testing || v8_flags.sandbox_fuzzing) {
    SandboxTesting::Mode mode = v8_flags.sandbox_testing
                                    ? SandboxTesting::Mode::kForTesting
                                    : SandboxTesting::Mode::kForFuzzing;
    SandboxTesting::Enable(mode);
  }
#endif  // V8_ENABLE_SANDBOX

#if defined(V8_USE_PERFETTO)
  if (perfetto::Tracing::IsInitialized()) {
    TrackEvent::Register();
    if (v8_flags.perfetto_code_logger) {
      v8::internal::CodeDataSource::Register();
    }
  }
#endif
  IsolateGroup::InitializeOncePerProcess();
  Isolate::InitializeOncePerProcess();

#if defined(USE_SIMULATOR)
  Simulator::InitializeOncePerProcess();
#endif
  CpuFeatures::Probe(false);
  ElementsAccessor::InitializeOncePerProcess();
  Bootstrapper::InitializeOncePerProcess();
  CallDescriptors::InitializeOncePerProcess();

#if V8_ENABLE_WEBASSEMBLY
  wasm::WasmEngine::InitializeOncePerProcess();
#endif  // V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  ExternalReferenceTable::InitializeOncePerIsolateGroup(
      IsolateGroup::current()->external_ref_table());
#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES

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
  // TODO(sroettger): this could move to a more generic
  // SecurityHardwareSupport::SetDefaultPermissionsForSignalHandler.
  internal::SandboxHardwareSupport::SetDefaultPermissionsForSignalHandler();
}

// static
void SandboxHardwareSupport::InitializeBeforeThreadCreation() {
  internal::SandboxHardwareSupport::InitializeBeforeThreadCreation();
}

}  // namespace v8
