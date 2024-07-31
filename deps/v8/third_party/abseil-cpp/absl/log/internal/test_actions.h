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
// File: log/internal/test_actions.h
// -----------------------------------------------------------------------------
//
// This file declares Googletest's actions used in the Abseil Logging library
// unit tests.

#ifndef ABSL_LOG_INTERNAL_TEST_ACTIONS_H_
#define ABSL_LOG_INTERNAL_TEST_ACTIONS_H_

#include <iostream>
#include <ostream>
#include <string>

#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/log/log_entry.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace log_internal {

// These actions are used by the child process in a death test.
//
// Expectations set in the child cannot cause test failure in the parent
// directly.  Instead, the child can use these actions with
// `EXPECT_CALL`/`WillOnce` and `ON_CALL`/`WillByDefault` (for unexpected calls)
// to write messages to stderr that the parent can match against.
struct WriteToStderr final {
  explicit WriteToStderr(absl::string_view m) : message(m) {}
  std::string message;

  template <typename... Args>
  void operator()(const Args&...) const {
    std::cerr << message << std::endl;
  }
};

struct WriteToStderrWithFilename final {
  explicit WriteToStderrWithFilename(absl::string_view m) : message(m) {}

  std::string message;

  void operator()(const absl::LogEntry& entry) const;
};

struct WriteEntryToStderr final {
  explicit WriteEntryToStderr(absl::string_view m) : message(m) {}

  std::string message = "";

  void operator()(const absl::LogEntry& entry) const;
  void operator()(absl::LogSeverity, absl::string_view,
                  absl::string_view) const;
};

// See the documentation for `DeathTestValidateExpectations` above.
// `DeathTestExpectedLogging` should be used once in a given death test, and the
// applicable severity level is the one that should be passed to
// `DeathTestValidateExpectations`.
inline WriteEntryToStderr DeathTestExpectedLogging() {
  return WriteEntryToStderr{"Mock received expected entry:"};
}

// `DeathTestUnexpectedLogging` should be used zero or more times to mark
// messages that should not hit the logs as the process dies.
inline WriteEntryToStderr DeathTestUnexpectedLogging() {
  return WriteEntryToStderr{"Mock received unexpected entry:"};
}

}  // namespace log_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOG_INTERNAL_TEST_ACTIONS_H_
