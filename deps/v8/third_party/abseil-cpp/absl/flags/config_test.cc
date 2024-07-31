//  Copyright 2019 The Abseil Authors.
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

#include "absl/flags/config.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "gtest/gtest.h"

#ifndef ABSL_FLAGS_STRIP_NAMES
#error ABSL_FLAGS_STRIP_NAMES is not defined
#endif

#ifndef ABSL_FLAGS_STRIP_HELP
#error ABSL_FLAGS_STRIP_HELP is not defined
#endif

namespace {

// Test that ABSL_FLAGS_STRIP_NAMES and ABSL_FLAGS_STRIP_HELP are configured how
// we expect them to be configured by default. If you override this
// configuration, this test will fail, but the code should still be safe to use.
TEST(FlagsConfigTest, Test) {
#if defined(__ANDROID__)
  EXPECT_EQ(ABSL_FLAGS_STRIP_NAMES, 1);
  EXPECT_EQ(ABSL_FLAGS_STRIP_HELP, 1);
#elif defined(__myriad2__)
  EXPECT_EQ(ABSL_FLAGS_STRIP_NAMES, 0);
  EXPECT_EQ(ABSL_FLAGS_STRIP_HELP, 0);
#elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  EXPECT_EQ(ABSL_FLAGS_STRIP_NAMES, 1);
  EXPECT_EQ(ABSL_FLAGS_STRIP_HELP, 1);
#elif defined(TARGET_OS_EMBEDDED) && TARGET_OS_EMBEDDED
  EXPECT_EQ(ABSL_FLAGS_STRIP_NAMES, 1);
  EXPECT_EQ(ABSL_FLAGS_STRIP_HELP, 1);
#elif defined(__APPLE__)
  EXPECT_EQ(ABSL_FLAGS_STRIP_NAMES, 0);
  EXPECT_EQ(ABSL_FLAGS_STRIP_HELP, 0);
#elif defined(_WIN32)
  EXPECT_EQ(ABSL_FLAGS_STRIP_NAMES, 0);
  EXPECT_EQ(ABSL_FLAGS_STRIP_HELP, 0);
#elif defined(__linux__)
  EXPECT_EQ(ABSL_FLAGS_STRIP_NAMES, 0);
  EXPECT_EQ(ABSL_FLAGS_STRIP_HELP, 0);
#endif
}

}  // namespace
