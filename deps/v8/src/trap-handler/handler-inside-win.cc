// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PLEASE READ BEFORE CHANGING THIS FILE!
//
// This file implements the out of bounds trap handler for
// WebAssembly. Exception handlers are notoriously difficult to get
// right, and getting it wrong can lead to security
// vulnerabilities. In order to minimize this risk, here are some
// rules to follow.
//
// 1. Do not introduce any new external dependencies. This file needs
//    to be self contained so it is easy to audit everything that a
//    trap handler might do.
//
// 2. Any changes must be reviewed by someone from the crash reporting
//    or security team. See OWNERS for suggested reviewers.
//
// For more information, see https://goo.gl/yMeyUY.
//
// This file contains most of the code that actually runs in an exception
// handler context. Some additional code is used both inside and outside the
// trap handler. This code can be found in handler-shared.cc.

#include "src/trap-handler/handler-inside-win.h"

#include <windows.h>

#include "src/trap-handler/trap-handler-internal.h"
#include "src/trap-handler/trap-handler.h"

#ifdef V8_TRAP_HANDLER_VIA_SIMULATOR
#include "src/trap-handler/trap-handler-simulator.h"
#endif

namespace v8 {
namespace internal {
namespace trap_handler {

#if V8_TRAP_HANDLER_SUPPORTED

// The below struct needed to access the offset in the Thread Environment Block
// to see if the thread local storage for the thread has been allocated yet.
//
// The ThreadLocalStorage pointer is located 12 pointers into the TEB (i.e. at
// offset 0x58 for 64-bit platforms, and 0x2c for 32-bit platforms). This is
// true for x64, x86, ARM, and ARM64 platforms (see the header files in the SDK
// named ksamd64.inc, ks386.inc, ksarm.h, and ksarm64.h respectively).
//
// These offsets are baked into compiled binaries, so can never be changed for
// backwards compatibility reasons.
struct TEB {
  PVOID reserved[11];
  PVOID thread_local_storage_pointer;
};

#ifdef V8_TRAP_HANDLER_VIA_SIMULATOR
// This is the address where we continue on a failed "ProbeMemory". It's defined
// in "handler-outside-simulator.cc".
extern char probe_memory_continuation[] asm(
    "v8_simulator_probe_memory_continuation");
#endif  // V8_TRAP_HANDLER_VIA_SIMULATOR

bool TryHandleWasmTrap(EXCEPTION_POINTERS* exception) {
  // VectoredExceptionHandlers need extreme caution. Do as little as possible
  // to determine if the exception should be handled or not. Exceptions can be
  // thrown very early in a threads life, before the thread has even completed
  // initializing. As a demonstrative example, there was a bug (#8966) where an
  // exception would be raised before the thread local copy of the
  // "__declspec(thread)" variables had been allocated, the handler tried to
  // access the thread-local "g_thread_in_wasm_code", which would then raise
  // another exception, and an infinite loop ensued.

  // First ensure this is an exception type of interest
  if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
    return false;
  }

  // See if thread-local storage for __declspec(thread) variables has been
  // allocated yet. This pointer is initially null in the TEB until the
  // loader has completed allocating the memory for thread_local variables
  // and copy constructed their initial values. (Note: Any functions that
  // need to run to initialize values may not have run yet, but that is not
  // the case for any thread_locals used here).
  TEB* pteb = reinterpret_cast<TEB*>(NtCurrentTeb());
  if (!pteb->thread_local_storage_pointer) return false;

  // Now safe to run more advanced logic, which may access thread_locals
  // Ensure the faulting thread was actually running Wasm code.
  if (!IsThreadInWasm()) return false;

  // Clear g_thread_in_wasm_code, primarily to protect against nested faults.
  // The only path that resets the flag to true is if we find a landing pad (in
  // which case this function returns true). Otherwise we leave the flag unset
  // since we do not return to wasm code.
  g_thread_in_wasm_code = false;

  const EXCEPTION_RECORD* record = exception->ExceptionRecord;

  uintptr_t fault_addr = reinterpret_cast<uintptr_t>(record->ExceptionAddress);

#ifdef V8_TRAP_HANDLER_VIA_SIMULATOR
  // Only handle signals triggered by the load in {ProbeMemory}.
  if (fault_addr != reinterpret_cast<uintptr_t>(&ProbeMemory)) return false;

  // The simulated ip will be in the second parameter register (%rdx).
  uintptr_t simulated_ip = exception->ContextRecord->Rdx;
  if (!IsFaultAddressCovered(simulated_ip)) return false;

  exception->ContextRecord->Rax = gLandingPad;
  // The fault_address that is set in non-simulator builds here is set in the
  // simulator directly.
  // Continue at the memory probing continuation.
  exception->ContextRecord->Rip =
      reinterpret_cast<uintptr_t>(&probe_memory_continuation);
#else
  if (!IsFaultAddressCovered(fault_addr)) return false;

  TH_DCHECK(gLandingPad != 0);
  // Tell the caller to return to the landing pad.
  exception->ContextRecord->Rip = gLandingPad;
  exception->ContextRecord->R10 = fault_addr;
#endif
  // We will return to wasm code, so restore the g_thread_in_wasm_code flag.
  g_thread_in_wasm_code = true;
  return true;
}

LONG HandleWasmTrap(EXCEPTION_POINTERS* exception) {
  if (TryHandleWasmTrap(exception)) {
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

#endif

}  // namespace trap_handler
}  // namespace internal
}  // namespace v8
