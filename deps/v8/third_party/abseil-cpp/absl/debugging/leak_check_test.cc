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

#include <string>

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/debugging/leak_check.h"
#include "absl/log/log.h"

namespace {

TEST(LeakCheckTest, IgnoreLeakSuppressesLeakedMemoryErrors) {
  if (!absl::LeakCheckerIsActive()) {
    GTEST_SKIP() << "LeakChecker is not active";
  }
  auto foo = absl::IgnoreLeak(new std::string("some ignored leaked string"));
  LOG(INFO) << "Ignoring leaked string " << foo;
}

TEST(LeakCheckTest, LeakCheckDisablerIgnoresLeak) {
  if (!absl::LeakCheckerIsActive()) {
    GTEST_SKIP() << "LeakChecker is not active";
  }
  absl::LeakCheckDisabler disabler;
  auto foo = new std::string("some string leaked while checks are disabled");
  LOG(INFO) << "Ignoring leaked string " << foo;
}

}  // namespace
