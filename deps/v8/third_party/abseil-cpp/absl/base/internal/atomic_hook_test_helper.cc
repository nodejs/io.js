// Copyright 2017 The Abseil Authors.
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

#include "absl/base/internal/atomic_hook_test_helper.h"

#include "absl/base/attributes.h"
#include "absl/base/internal/atomic_hook.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace atomic_hook_internal {

ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES absl::base_internal::AtomicHook<VoidF>
    func(DefaultFunc);
ABSL_CONST_INIT int default_func_calls = 0;
void DefaultFunc() { default_func_calls++; }
void RegisterFunc(VoidF f) { func.Store(f); }

}  // namespace atomic_hook_internal
ABSL_NAMESPACE_END
}  // namespace absl
