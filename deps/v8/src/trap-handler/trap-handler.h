// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TRAP_HANDLER_TRAP_HANDLER_H_
#define V8_TRAP_HANDLER_TRAP_HANDLER_H_

#include <stdint.h>
#include <stdlib.h>

#include <atomic>

#include "include/v8config.h"
#include "src/base/immediate-crash.h"

namespace v8 {
namespace internal {
namespace trap_handler {

// X64 on Linux, Windows, MacOS, FreeBSD.
#if V8_HOST_ARCH_X64 && V8_TARGET_ARCH_X64 &&                        \
    ((V8_OS_LINUX && !V8_OS_ANDROID) || V8_OS_WIN || V8_OS_DARWIN || \
     V8_OS_FREEBSD)
#define V8_TRAP_HANDLER_SUPPORTED true
// Arm64 (non-simulator) on Mac and Linux.
#elif V8_TARGET_ARCH_ARM64 && V8_HOST_ARCH_ARM64 && \
    (V8_OS_DARWIN || (V8_OS_LINUX && !V8_OS_ANDROID))
#define V8_TRAP_HANDLER_SUPPORTED true
// Arm64 simulator on x64 on Linux, Mac, or Windows.
//
// The simulator case uses some inline assembly code, which cannot be
// compiled with MSVC, so don't enable the trap handler in that case.
// (MSVC #defines _MSC_VER, but so does Clang when targeting Windows, hence
// the check for __clang__.)
#elif V8_TARGET_ARCH_ARM64 && V8_HOST_ARCH_X64 && \
    (V8_OS_LINUX || V8_OS_DARWIN || V8_OS_WIN) && \
    (!defined(_MSC_VER) || defined(__clang__))
#define V8_TRAP_HANDLER_VIA_SIMULATOR
#define V8_TRAP_HANDLER_SUPPORTED true
// Loong64 (non-simulator) on Linux.
#elif V8_TARGET_ARCH_LOONG64 && V8_HOST_ARCH_LOONG64 && V8_OS_LINUX
#define V8_TRAP_HANDLER_SUPPORTED true
// Loong64 simulator on x64 on Linux
#elif V8_TARGET_ARCH_LOONG64 && V8_HOST_ARCH_X64 && V8_OS_LINUX
#define V8_TRAP_HANDLER_VIA_SIMULATOR
#define V8_TRAP_HANDLER_SUPPORTED true
// RISCV64 (non-simulator) on Linux.
#elif V8_TARGET_ARCH_RISCV64 && V8_HOST_ARCH_RISCV64 && V8_OS_LINUX
#define V8_TRAP_HANDLER_SUPPORTED true
// RISCV64 simulator on x64 on Linux
#elif V8_TARGET_ARCH_RISCV64 && V8_HOST_ARCH_X64 && V8_OS_LINUX
#define V8_TRAP_HANDLER_VIA_SIMULATOR
#define V8_TRAP_HANDLER_SUPPORTED true
// Everything else is unsupported.
#else
#define V8_TRAP_HANDLER_SUPPORTED false
#endif

#if V8_OS_ANDROID && V8_TRAP_HANDLER_SUPPORTED
// It would require some careful security review before the trap handler
// can be enabled on Android.  Android may do unexpected things with signal
// handling and crash reporting that could open up security holes in V8's
// trap handling.
#error "The V8 trap handler should not be enabled on Android"
#endif

// Setup for shared library export.
#if defined(BUILDING_V8_SHARED_PRIVATE) && defined(V8_OS_WIN)
#define TH_EXPORT_PRIVATE __declspec(dllexport)
#elif defined(BUILDING_V8_SHARED_PRIVATE)
#define TH_EXPORT_PRIVATE __attribute__((visibility("default")))
#elif defined(USING_V8_SHARED_PRIVATE) && defined(V8_OS_WIN)
#define TH_EXPORT_PRIVATE __declspec(dllimport)
#else
#define TH_EXPORT_PRIVATE
#endif

#define TH_CHECK(condition) \
  if (!(condition)) IMMEDIATE_CRASH();
#ifdef DEBUG
#define TH_DCHECK(condition) TH_CHECK(condition)
#else
#define TH_DCHECK(condition) void(0)
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TH_DISABLE_ASAN __attribute__((no_sanitize_address))
#else
#define TH_DISABLE_ASAN
#endif
#else
#define TH_DISABLE_ASAN
#endif

struct ProtectedInstructionData {
  // The offset of this instruction from the start of its code object.
  // Wasm code never grows larger than 2GB, so uint32_t is sufficient.
  uint32_t instr_offset;
};

const int kInvalidIndex = -1;

/// Adds the handler data to the place where the trap handler will find it.
///
/// This returns a number that can be used to identify the handler data to
/// ReleaseHandlerData, or -1 on failure.
int TH_EXPORT_PRIVATE RegisterHandlerData(
    uintptr_t base, size_t size, size_t num_protected_instructions,
    const ProtectedInstructionData* protected_instructions);

/// Removes the data from the master list and frees any memory, if necessary.
/// TODO(mtrofin): We can switch to using size_t for index and not need
/// kInvalidIndex.
void TH_EXPORT_PRIVATE ReleaseHandlerData(int index);

/// Sets the base and size of the V8 sandbox region. If set, these will be used
/// by the trap handler: only faulting accesses to memory inside the V8 sandbox
/// should be handled by the trap handler since all Wasm memory objects are
/// located inside the sandbox.
void TH_EXPORT_PRIVATE SetV8SandboxBaseAndSize(uintptr_t base, size_t size);

// Initially false, set to true if when trap handlers are enabled. Never goes
// back to false then.
TH_EXPORT_PRIVATE extern bool g_is_trap_handler_enabled;

// Initially true, set to false when either {IsTrapHandlerEnabled} or
// {EnableTrapHandler} is called to prevent calling {EnableTrapHandler}
// repeatedly, or after {IsTrapHandlerEnabled}. Needs to be atomic because
// {IsTrapHandlerEnabled} can be called from any thread. Updated using relaxed
// semantics, since it's not used for synchronization.
TH_EXPORT_PRIVATE extern std::atomic<bool> g_can_enable_trap_handler;

// Enables trap handling for WebAssembly bounds checks.
//
// use_v8_handler indicates that V8 should install its own handler
// rather than relying on the embedder to do it.
TH_EXPORT_PRIVATE bool EnableTrapHandler(bool use_v8_handler);

// Set the address that the trap handler should continue execution from when it
// gets a fault at a recognised address.
TH_EXPORT_PRIVATE void SetLandingPad(uintptr_t landing_pad);

inline bool IsTrapHandlerEnabled() {
  TH_DCHECK(!g_is_trap_handler_enabled || V8_TRAP_HANDLER_SUPPORTED);
  // Disallow enabling the trap handler after retrieving the current value.
  // Re-enabling them late can produce issues because code or objects might have
  // been generated under the assumption that trap handlers are disabled.
  // Note: We test before setting to avoid contention by an unconditional write.
  if (g_can_enable_trap_handler.load(std::memory_order_relaxed)) {
    g_can_enable_trap_handler.store(false, std::memory_order_relaxed);
  }
  return g_is_trap_handler_enabled;
}

#if defined(V8_OS_AIX)
// `thread_local` does not link on AIX:
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=100641
extern __thread int g_thread_in_wasm_code;
#else
extern thread_local int g_thread_in_wasm_code;
#endif

// Return the address of the thread-local {g_thread_in_wasm_code} variable. This
// pointer can be accessed and modified as long as the thread calling this
// function exists. Only use if from the same thread do avoid race conditions.
V8_NOINLINE TH_EXPORT_PRIVATE int* GetThreadInWasmThreadLocalAddress();

// On Windows, asan installs its own exception handler which maps shadow
// memory. Since our exception handler may be executed before the asan exception
// handler, we have to make sure that asan shadow memory is not accessed here.
TH_DISABLE_ASAN inline bool IsThreadInWasm() { return g_thread_in_wasm_code; }

inline void SetThreadInWasm() {
  if (IsTrapHandlerEnabled()) {
    TH_DCHECK(!IsThreadInWasm());
    g_thread_in_wasm_code = true;
  }
}

inline void ClearThreadInWasm() {
  if (IsTrapHandlerEnabled()) {
    TH_DCHECK(IsThreadInWasm());
    g_thread_in_wasm_code = false;
  }
}

bool RegisterDefaultTrapHandler();
TH_EXPORT_PRIVATE void RemoveTrapHandler();

TH_EXPORT_PRIVATE size_t GetRecoveredTrapCount();

}  // namespace trap_handler
}  // namespace internal
}  // namespace v8

#endif  // V8_TRAP_HANDLER_TRAP_HANDLER_H_
