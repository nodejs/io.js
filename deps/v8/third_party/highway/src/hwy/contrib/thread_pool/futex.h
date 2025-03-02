// Copyright 2024 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HIGHWAY_HWY_CONTRIB_THREAD_POOL_FUTEX_H_
#define HIGHWAY_HWY_CONTRIB_THREAD_POOL_FUTEX_H_

// Keyed event (futex): kernel queue of blocked threads, identified by the
// address of an atomic u32 called `current` within the same process (do NOT
// use with shared-memory mappings).
//
// Futex equivalents: https://outerproduct.net/futex-dictionary.html; we
// support Linux/Emscripten/Apple/Windows and C++20 std::atomic::wait, plus a
// usleep fallback.

#include <atomic>
#include <climits>  // INT_MAX

#include "hwy/base.h"

#if HWY_ARCH_WASM
#include <emscripten/threading.h>
#include <math.h>  // INFINITY

#elif HWY_OS_LINUX
#include <errno.h>        // IWYU pragma: keep
#include <linux/futex.h>  // FUTEX_*
#include <pthread.h>
#include <sys/syscall.h>  // SYS_*
#include <unistd.h>
// Android may not declare these:
#ifndef SYS_futex
#ifdef SYS_futex_time64  // 32-bit with 64-bit time_t
#define SYS_futex SYS_futex_time64
#else
#define SYS_futex __NR_futex
#endif  // SYS_futex_time64
#endif  // SYS_futex
#ifndef FUTEX_WAIT_PRIVATE
#define FUTEX_WAIT_PRIVATE (FUTEX_WAIT | 128)
#endif
#ifndef FUTEX_WAKE_PRIVATE
#define FUTEX_WAKE_PRIVATE (FUTEX_WAKE | 128)
#endif

#elif HWY_OS_APPLE && !defined(HWY_DISABLE_FUTEX)
// These are private APIs, so add an opt-out.
extern "C" {
int __ulock_wait(uint32_t op, void* address, uint64_t val, uint32_t max_us);
int __ulock_wake(uint32_t op, void* address, uint64_t zero);
}  // extern "C"
#define UL_COMPARE_AND_WAIT 1
#define ULF_WAKE_ALL 0x00000100

#elif HWY_OS_WIN && !defined(HWY_DISABLE_FUTEX)
// WakeByAddressAll requires Windows 8, so add an opt-out.
#include <windows.h>
#pragma comment(lib, "synchronization.lib")

#elif HWY_CXX_LANG < 202002L  // NOT C++20, which has native support
#define HWY_FUTEX_SLEEP
#include <chrono>  // NOLINT (sleep_for)
#endif

namespace hwy {

// Waits until `current != prev` and returns the new value. May return
// immediately if `current` already changed, or after blocking and waking.
static inline uint32_t BlockUntilDifferent(
    const uint32_t prev, const std::atomic<uint32_t>& current) {
  const auto acq = std::memory_order_acquire;

#if HWY_ARCH_WASM
  // It is always safe to cast to void.
  volatile void* address =
      const_cast<volatile void*>(static_cast<const volatile void*>(&current));
  const double max_ms = INFINITY;
  for (;;) {
    const uint32_t next = current.load(acq);
    if (next != prev) return next;
    const int ret = emscripten_futex_wait(address, prev, max_ms);
    HWY_DASSERT(ret >= 0);
    (void)ret;
  }

#elif HWY_OS_LINUX
  // Safe to cast because std::atomic is a standard layout type.
  const uint32_t* address = reinterpret_cast<const uint32_t*>(&current);
  // _PRIVATE requires this only be used in the same process, and avoids
  // virtual->physical lookups and atomic reference counting.
  const int op = FUTEX_WAIT_PRIVATE;
  for (;;) {
    const uint32_t next = current.load(acq);
    if (next != prev) return next;
    // timeout=null may prevent interrupts via signal. No lvalue because
    // the timespec type is only standardized since C++17 or C11.
    const auto ret = syscall(SYS_futex, address, op, prev, nullptr, nullptr, 0);
    if (ret == -1) {
      HWY_DASSERT(errno == EAGAIN);  // otherwise an actual error
    }
  }

#elif HWY_OS_WIN && !defined(HWY_DISABLE_FUTEX)
  // It is always safe to cast to void.
  volatile void* address =
      const_cast<volatile void*>(static_cast<const volatile void*>(&current));
  // API is not const-correct, but only loads from the pointer.
  PVOID pprev = const_cast<void*>(static_cast<const void*>(&prev));
  const DWORD max_ms = INFINITE;
  for (;;) {
    const uint32_t next = current.load(acq);
    if (next != prev) return next;
    const BOOL ok = WaitOnAddress(address, pprev, sizeof(prev), max_ms);
    HWY_DASSERT(ok);
    (void)ok;
  }

#elif HWY_OS_APPLE && !defined(HWY_DISABLE_FUTEX)
  // It is always safe to cast to void.
  void* address = const_cast<void*>(static_cast<const void*>(&current));
  for (;;) {
    const uint32_t next = current.load(acq);
    if (next != prev) return next;
    __ulock_wait(UL_COMPARE_AND_WAIT, address, prev, 0);
  }

#elif defined(HWY_FUTEX_SLEEP)
  for (;;) {
    const uint32_t next = current.load(acq);
    if (next != prev) return next;
    std::this_thread::sleep_for(std::chrono::microseconds(2));
  }

#elif HWY_CXX_LANG >= 202002L
  current.wait(prev, acq);  // No spurious wakeup.
  const uint32_t next = current.load(acq);
  HWY_DASSERT(next != prev);
  return next;

#else
#error "Logic error, should have reached HWY_FUTEX_SLEEP"
#endif  // HWY_OS_*
}  // BlockUntilDifferent

// Wakes all threads, if any, that are waiting because they called
// `BlockUntilDifferent` with the same `current`.
static inline void WakeAll(std::atomic<uint32_t>& current) {
#if HWY_ARCH_WASM
  // It is always safe to cast to void.
  volatile void* address = static_cast<volatile void*>(&current);
  const int max_to_wake = INT_MAX;  // actually signed
  const int ret = emscripten_futex_wake(address, max_to_wake);
  HWY_DASSERT(ret >= 0);
  (void)ret;

#elif HWY_OS_LINUX
  // Safe to cast because std::atomic is a standard layout type.
  uint32_t* address = reinterpret_cast<uint32_t*>(&current);
  const int max_to_wake = INT_MAX;  // actually signed
  const auto ret = syscall(SYS_futex, address, FUTEX_WAKE_PRIVATE, max_to_wake,
                           nullptr, nullptr, 0);
  HWY_DASSERT(ret >= 0);  // number woken
  (void)ret;

#elif HWY_OS_WIN && !defined(HWY_DISABLE_FUTEX)
  // It is always safe to cast to void.
  void* address = static_cast<void*>(&current);
  WakeByAddressAll(address);

#elif HWY_OS_APPLE && !defined(HWY_DISABLE_FUTEX)
  // It is always safe to cast to void.
  void* address = static_cast<void*>(&current);
  __ulock_wake(UL_COMPARE_AND_WAIT | ULF_WAKE_ALL, address, 0);

#elif defined(HWY_FUTEX_SLEEP)
  // Sleep loop does not require wakeup.
  (void)current;
#elif HWY_CXX_LANG >= 202002L
  current.notify_all();

#else
#error "Logic error, should have reached HWY_FUTEX_SLEEP"
#endif
}  // WakeAll

}  // namespace hwy

#endif  // HIGHWAY_HWY_CONTRIB_THREAD_POOL_FUTEX_H_
