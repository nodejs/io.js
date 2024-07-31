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

#include "absl/synchronization/internal/waiter_base.h"

#include "absl/base/config.h"
#include "absl/base/internal/thread_identity.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr int WaiterBase::kIdlePeriods;
#endif

void WaiterBase::MaybeBecomeIdle() {
  base_internal::ThreadIdentity *identity =
      base_internal::CurrentThreadIdentityIfPresent();
  assert(identity != nullptr);
  const bool is_idle = identity->is_idle.load(std::memory_order_relaxed);
  const int ticker = identity->ticker.load(std::memory_order_relaxed);
  const int wait_start = identity->wait_start.load(std::memory_order_relaxed);
  if (!is_idle && ticker - wait_start > kIdlePeriods) {
    identity->is_idle.store(true, std::memory_order_relaxed);
  }
}

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl
