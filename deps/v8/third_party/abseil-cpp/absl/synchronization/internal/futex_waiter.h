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

#ifndef ABSL_SYNCHRONIZATION_INTERNAL_FUTEX_WAITER_H_
#define ABSL_SYNCHRONIZATION_INTERNAL_FUTEX_WAITER_H_

#include <atomic>
#include <cstdint>

#include "absl/base/config.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/synchronization/internal/futex.h"
#include "absl/synchronization/internal/waiter_base.h"

#ifdef ABSL_INTERNAL_HAVE_FUTEX

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

#define ABSL_INTERNAL_HAVE_FUTEX_WAITER 1

class FutexWaiter : public WaiterCrtp<FutexWaiter> {
 public:
  FutexWaiter() : futex_(0) {}

  bool Wait(KernelTimeout t);
  void Post();
  void Poke();

  static constexpr char kName[] = "FutexWaiter";

 private:
  // Atomically check that `*v == val`, and if it is, then sleep until the
  // timeout `t` has been reached, or until woken by `Wake()`.
  static int WaitUntil(std::atomic<int32_t>* v, int32_t val,
                       KernelTimeout t);

  // Futexes are defined by specification to be 32-bits.
  // Thus std::atomic<int32_t> must be just an int32_t with lockfree methods.
  std::atomic<int32_t> futex_;
  static_assert(sizeof(int32_t) == sizeof(futex_), "Wrong size for futex");
};

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_INTERNAL_HAVE_FUTEX

#endif  // ABSL_SYNCHRONIZATION_INTERNAL_FUTEX_WAITER_H_
