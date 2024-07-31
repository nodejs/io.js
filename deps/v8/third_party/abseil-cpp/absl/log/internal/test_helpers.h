// Copyright 2022 The Abseil Authors.
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
// -----------------------------------------------------------------------------
// File: log/internal/test_helpers.h
// -----------------------------------------------------------------------------
//
// This file declares testing helpers for the logging library.

#ifndef ABSL_LOG_INTERNAL_TEST_HELPERS_H_
#define ABSL_LOG_INTERNAL_TEST_HELPERS_H_

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/log/globals.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

// `ABSL_MIN_LOG_LEVEL` can't be used directly since it is not always defined.
constexpr auto kAbslMinLogLevel =
#ifdef ABSL_MIN_LOG_LEVEL
    static_cast<absl::LogSeverityAtLeast>(ABSL_MIN_LOG_LEVEL);
#else
    absl::LogSeverityAtLeast::kInfo;
#endif

// Returns false if the specified severity level is disabled by
// `ABSL_MIN_LOG_LEVEL` or `absl::MinLogLevel()`.
bool LoggingEnabledAt(absl::LogSeverity severity);

// -----------------------------------------------------------------------------
// Googletest Death Test Predicates
// -----------------------------------------------------------------------------

#if GTEST_HAS_DEATH_TEST

bool DiedOfFatal(int exit_status);
bool DiedOfQFatal(int exit_status);

#endif

// -----------------------------------------------------------------------------
// Helper for Log initialization in test
// -----------------------------------------------------------------------------

class LogTestEnvironment : public ::testing::Environment {
 public:
  ~LogTestEnvironment() override = default;

  void SetUp() override;
};

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_TEST_HELPERS_H_
