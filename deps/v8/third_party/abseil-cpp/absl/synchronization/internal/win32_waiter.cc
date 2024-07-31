// Copyright 2023 The Abseil Authors.
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

#include "absl/synchronization/internal/win32_waiter.h"

#ifdef ABSL_INTERNAL_HAVE_WIN32_WAITER

#include <windows.h>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/base/optimization.h"
#include "absl/synchronization/internal/kernel_timeout.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr char Win32Waiter::kName[];
#endif

class Win32Waiter::WinHelper {
 public:
  static SRWLOCK *GetLock(Win32Waiter *w) {
    return reinterpret_cast<SRWLOCK *>(&w->mu_storage_);
  }

  static CONDITION_VARIABLE *GetCond(Win32Waiter *w) {
    return reinterpret_cast<CONDITION_VARIABLE *>(&w->cv_storage_);
  }

  static_assert(sizeof(SRWLOCK) == sizeof(void *),
                "`mu_storage_` does not have the same size as SRWLOCK");
  static_assert(alignof(SRWLOCK) == alignof(void *),
                "`mu_storage_` does not have the same alignment as SRWLOCK");

  static_assert(sizeof(CONDITION_VARIABLE) == sizeof(void *),
                "`ABSL_CONDITION_VARIABLE_STORAGE` does not have the same size "
                "as `CONDITION_VARIABLE`");
  static_assert(
      alignof(CONDITION_VARIABLE) == alignof(void *),
      "`cv_storage_` does not have the same alignment as `CONDITION_VARIABLE`");

  // The SRWLOCK and CONDITION_VARIABLE types must be trivially constructible
  // and destructible because we never call their constructors or destructors.
  static_assert(std::is_trivially_constructible<SRWLOCK>::value,
                "The `SRWLOCK` type must be trivially constructible");
  static_assert(
      std::is_trivially_constructible<CONDITION_VARIABLE>::value,
      "The `CONDITION_VARIABLE` type must be trivially constructible");
  static_assert(std::is_trivially_destructible<SRWLOCK>::value,
                "The `SRWLOCK` type must be trivially destructible");
  static_assert(std::is_trivially_destructible<CONDITION_VARIABLE>::value,
                "The `CONDITION_VARIABLE` type must be trivially destructible");
};

class LockHolder {
 public:
  explicit LockHolder(SRWLOCK* mu) : mu_(mu) {
    AcquireSRWLockExclusive(mu_);
  }

  LockHolder(const LockHolder&) = delete;
  LockHolder& operator=(const LockHolder&) = delete;

  ~LockHolder() {
    ReleaseSRWLockExclusive(mu_);
  }

 private:
  SRWLOCK* mu_;
};

Win32Waiter::Win32Waiter() {
  auto *mu = ::new (static_cast<void *>(&mu_storage_)) SRWLOCK;
  auto *cv = ::new (static_cast<void *>(&cv_storage_)) CONDITION_VARIABLE;
  InitializeSRWLock(mu);
  InitializeConditionVariable(cv);
  waiter_count_ = 0;
  wakeup_count_ = 0;
}

bool Win32Waiter::Wait(KernelTimeout t) {
  SRWLOCK *mu = WinHelper::GetLock(this);
  CONDITION_VARIABLE *cv = WinHelper::GetCond(this);

  LockHolder h(mu);
  ++waiter_count_;

  // Loop until we find a wakeup to consume or timeout.
  // Note that, since the thread ticker is just reset, we don't need to check
  // whether the thread is idle on the very first pass of the loop.
  bool first_pass = true;
  while (wakeup_count_ == 0) {
    if (!first_pass) MaybeBecomeIdle();
    // No wakeups available, time to wait.
    if (!SleepConditionVariableSRW(cv, mu, t.InMillisecondsFromNow(), 0)) {
      // GetLastError() returns a Win32 DWORD, but we assign to
      // unsigned long to simplify the ABSL_RAW_LOG case below.  The uniform
      // initialization guarantees this is not a narrowing conversion.
      const unsigned long err{GetLastError()};  // NOLINT(runtime/int)
      if (err == ERROR_TIMEOUT) {
        --waiter_count_;
        return false;
      } else {
        ABSL_RAW_LOG(FATAL, "SleepConditionVariableSRW failed: %lu", err);
      }
    }
    first_pass = false;
  }
  // Consume a wakeup and we're done.
  --wakeup_count_;
  --waiter_count_;
  return true;
}

void Win32Waiter::Post() {
  LockHolder h(WinHelper::GetLock(this));
  ++wakeup_count_;
  InternalCondVarPoke();
}

void Win32Waiter::Poke() {
  LockHolder h(WinHelper::GetLock(this));
  InternalCondVarPoke();
}

void Win32Waiter::InternalCondVarPoke() {
  if (waiter_count_ != 0) {
    WakeConditionVariable(WinHelper::GetCond(this));
  }
}

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_INTERNAL_HAVE_WIN32_WAITER
