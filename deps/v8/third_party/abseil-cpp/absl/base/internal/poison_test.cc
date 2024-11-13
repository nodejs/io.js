// Copyright 2024 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/base/internal/poison.h"

#include <iostream>

#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {
namespace {

TEST(PoisonTest, CrashesOnDereference) {
#ifdef __ANDROID__
  GTEST_SKIP() << "On Android, poisoned pointer dereference times out instead "
                  "of crashing.";
#endif
  int* poisoned_ptr = static_cast<int*>(get_poisoned_pointer());
  EXPECT_DEATH_IF_SUPPORTED(std::cout << *poisoned_ptr, "");
  EXPECT_DEATH_IF_SUPPORTED(std::cout << *(poisoned_ptr - 10), "");
  EXPECT_DEATH_IF_SUPPORTED(std::cout << *(poisoned_ptr + 10), "");
}

}  // namespace
}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
