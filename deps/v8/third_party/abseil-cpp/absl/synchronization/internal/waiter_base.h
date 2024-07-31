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

#ifndef ABSL_SYNCHRONIZATION_INTERNAL_WAITER_BASE_H_
#define ABSL_SYNCHRONIZATION_INTERNAL_WAITER_BASE_H_

#include "absl/base/config.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/synchronization/internal/kernel_timeout.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

// `Waiter` is a platform specific semaphore implementation that `PerThreadSem`
// waits on to implement blocking in `absl::Mutex`.  Implementations should
// inherit from `WaiterCrtp` and must implement `Wait()`, `Post()`, and `Poke()`
// as described in `WaiterBase`.  `waiter.h` selects the implementation and uses
// static-dispatch for performance.
class WaiterBase {
 public:
  WaiterBase() = default;

  // Not copyable or movable
  WaiterBase(const WaiterBase&) = delete;
  WaiterBase& operator=(const WaiterBase&) = delete;

  // Blocks the calling thread until a matching call to `Post()` or
  // `t` has passed. Returns `true` if woken (`Post()` called),
  // `false` on timeout.
  //
  // bool Wait(KernelTimeout t);

  // Restart the caller of `Wait()` as with a normal semaphore.
  //
  // void Post();

  // If anyone is waiting, wake them up temporarily and cause them to
  // call `MaybeBecomeIdle()`. They will then return to waiting for a
  // `Post()` or timeout.
  //
  // void Poke();

  // Returns the name of this implementation. Used only for debugging.
  //
  // static constexpr char kName[];

  // How many periods to remain idle before releasing resources
#ifndef ABSL_HAVE_THREAD_SANITIZER
  static constexpr int kIdlePeriods = 60;
#else
  // Memory consumption under ThreadSanitizer is a serious concern,
  // so we release resources sooner. The value of 1 leads to 1 to 2 second
  // delay before marking a thread as idle.
  static constexpr int kIdlePeriods = 1;
#endif

 protected:
  static void MaybeBecomeIdle();
};

template <typename T>
class WaiterCrtp : public WaiterBase {
 public:
  // Returns the Waiter associated with the identity.
  static T* GetWaiter(base_internal::ThreadIdentity* identity) {
    static_assert(
        sizeof(T) <= sizeof(base_internal::ThreadIdentity::WaiterState),
        "Insufficient space for Waiter");
    return reinterpret_cast<T*>(identity->waiter_state.data);
  }
};

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_SYNCHRONIZATION_INTERNAL_WAITER_BASE_H_
