// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This file is a Linux-specific part of spinlock_wait.cc

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <climits>
#include <cstdint>
#include <ctime>

#include "absl/base/attributes.h"
#include "absl/base/internal/errno_saver.h"

// The SpinLock lockword is `std::atomic<uint32_t>`. Here we assert that
// `std::atomic<uint32_t>` is bitwise equivalent of the `int` expected
// by SYS_futex. We also assume that reads/writes done to the lockword
// by SYS_futex have rational semantics with regard to the
// std::atomic<> API. C++ provides no guarantees of these assumptions,
// but they are believed to hold in practice.
static_assert(sizeof(std::atomic<uint32_t>) == sizeof(int),
              "SpinLock lockword has the wrong size for a futex");

// Some Android headers are missing these definitions even though they
// support these futex operations.
#ifdef __BIONIC__
#ifndef SYS_futex
#define SYS_futex __NR_futex
#endif
#ifndef FUTEX_PRIVATE_FLAG
#define FUTEX_PRIVATE_FLAG 128
#endif
#endif

#if defined(__NR_futex_time64) && !defined(SYS_futex_time64)
#define SYS_futex_time64 __NR_futex_time64
#endif

#if defined(SYS_futex_time64) && !defined(SYS_futex)
#define SYS_futex SYS_futex_time64
#endif

extern "C" {

ABSL_ATTRIBUTE_WEAK void ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockDelay)(
    std::atomic<uint32_t> *w, uint32_t value, int,
    absl::base_internal::SchedulingMode) {
  absl::base_internal::ErrnoSaver errno_saver;
  syscall(SYS_futex, w, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, value, nullptr);
}

ABSL_ATTRIBUTE_WEAK void ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockWake)(
    std::atomic<uint32_t> *w, bool all) {
  syscall(SYS_futex, w, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, all ? INT_MAX : 1, 0);
}

}  // extern "C"
