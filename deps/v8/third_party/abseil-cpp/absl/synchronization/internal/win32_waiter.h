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
//

#ifndef ABSL_SYNCHRONIZATION_INTERNAL_WIN32_WAITER_H_
#define ABSL_SYNCHRONIZATION_INTERNAL_WIN32_WAITER_H_

#ifdef _WIN32
#include <sdkddkver.h>
#endif

#if defined(_WIN32) && !defined(__MINGW32__) && \
    _WIN32_WINNT >= _WIN32_WINNT_VISTA

#include "absl/base/config.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/synchronization/internal/waiter_base.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

#define ABSL_INTERNAL_HAVE_WIN32_WAITER 1

class Win32Waiter : public WaiterCrtp<Win32Waiter> {
 public:
  Win32Waiter();

  bool Wait(KernelTimeout t);
  void Post();
  void Poke();

  static constexpr char kName[] = "Win32Waiter";

 private:
  // WinHelper - Used to define utilities for accessing the lock and
  // condition variable storage once the types are complete.
  class WinHelper;

  // REQUIRES: WinHelper::GetLock(this) must be held.
  void InternalCondVarPoke();

  // We can't include Windows.h in our headers, so we use aligned character
  // buffers to define the storage of SRWLOCK and CONDITION_VARIABLE.
  // SRW locks and condition variables do not need to be explicitly destroyed.
  // https://docs.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializesrwlock
  // https://stackoverflow.com/questions/28975958/why-does-windows-have-no-deleteconditionvariable-function-to-go-together-with
  alignas(void*) unsigned char mu_storage_[sizeof(void*)];
  alignas(void*) unsigned char cv_storage_[sizeof(void*)];
  int waiter_count_;
  int wakeup_count_;
};

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // defined(_WIN32) && !defined(__MINGW32__) &&
        // _WIN32_WINNT >= _WIN32_WINNT_VISTA

#endif  // ABSL_SYNCHRONIZATION_INTERNAL_WIN32_WAITER_H_
