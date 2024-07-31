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

#include <memory>

#include "gtest/gtest.h"
#include "absl/debugging/leak_check.h"
#include "absl/log/log.h"

namespace {

TEST(LeakCheckTest, LeakMemory) {
  // This test is expected to cause lsan failures on program exit. Therefore the
  // test will be run only by leak_check_test.sh, which will verify a
  // failed exit code.

  char* foo = strdup("lsan should complain about this leaked string");
  LOG(INFO) << "Should detect leaked string " << foo;
}

TEST(LeakCheckTest, LeakMemoryAfterDisablerScope) {
  // This test is expected to cause lsan failures on program exit. Therefore the
  // test will be run only by external_leak_check_test.sh, which will verify a
  // failed exit code.
  { absl::LeakCheckDisabler disabler; }
  char* foo = strdup("lsan should also complain about this leaked string");
  LOG(INFO) << "Re-enabled leak detection.Should detect leaked string " << foo;
}

}  // namespace
