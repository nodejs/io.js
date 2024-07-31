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

// This test serves primarily as a compilation test for base/raw_logging.h.
// Raw logging testing is covered by logging_unittest.cc, which is not as
// portable as this test.

#include "absl/base/internal/raw_logging.h"

#include <tuple>

#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"

namespace {

TEST(RawLoggingCompilationTest, Log) {
  ABSL_RAW_LOG(INFO, "RAW INFO: %d", 1);
  ABSL_RAW_LOG(INFO, "RAW INFO: %d %d", 1, 2);
  ABSL_RAW_LOG(INFO, "RAW INFO: %d %d %d", 1, 2, 3);
  ABSL_RAW_LOG(INFO, "RAW INFO: %d %d %d %d", 1, 2, 3, 4);
  ABSL_RAW_LOG(INFO, "RAW INFO: %d %d %d %d %d", 1, 2, 3, 4, 5);
  ABSL_RAW_LOG(WARNING, "RAW WARNING: %d", 1);
  ABSL_RAW_LOG(ERROR, "RAW ERROR: %d", 1);
}

TEST(RawLoggingCompilationTest, PassingCheck) {
  ABSL_RAW_CHECK(true, "RAW CHECK");
}

// Not all platforms support output from raw log, so we don't verify any
// particular output for RAW check failures (expecting the empty string
// accomplishes this).  This test is primarily a compilation test, but we
// are verifying process death when EXPECT_DEATH works for a platform.
const char kExpectedDeathOutput[] = "";

TEST(RawLoggingDeathTest, FailingCheck) {
  EXPECT_DEATH_IF_SUPPORTED(ABSL_RAW_CHECK(1 == 0, "explanation"),
                            kExpectedDeathOutput);
}

TEST(RawLoggingDeathTest, LogFatal) {
  EXPECT_DEATH_IF_SUPPORTED(ABSL_RAW_LOG(FATAL, "my dog has fleas"),
                            kExpectedDeathOutput);
}

TEST(InternalLog, CompilationTest) {
  ABSL_INTERNAL_LOG(INFO, "Internal Log");
  std::string log_msg = "Internal Log";
  ABSL_INTERNAL_LOG(INFO, log_msg);

  ABSL_INTERNAL_LOG(INFO, log_msg + " 2");

  float d = 1.1f;
  ABSL_INTERNAL_LOG(INFO, absl::StrCat("Internal log ", 3, " + ", d));
}

TEST(InternalLogDeathTest, FailingCheck) {
  EXPECT_DEATH_IF_SUPPORTED(ABSL_INTERNAL_CHECK(1 == 0, "explanation"),
                            kExpectedDeathOutput);
}

TEST(InternalLogDeathTest, LogFatal) {
  EXPECT_DEATH_IF_SUPPORTED(ABSL_INTERNAL_LOG(FATAL, "my dog has fleas"),
                            kExpectedDeathOutput);
}

}  // namespace
