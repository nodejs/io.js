//
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

#include "absl/log/structured.h"

#include <ios>
#include <sstream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/internal/test_matchers.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"

namespace {
using ::absl::log_internal::MatchesOstream;
using ::absl::log_internal::TextMessage;
using ::testing::ElementsAre;
using ::testing::Eq;

auto *test_env ABSL_ATTRIBUTE_UNUSED = ::testing::AddGlobalTestEnvironment(
    new absl::log_internal::LogTestEnvironment);

// Abseil Logging library uses these by default, so we set them on the
// `std::ostream` we compare against too.
std::ios &LoggingDefaults(std::ios &str) {
  str.setf(std::ios_base::showbase | std::ios_base::boolalpha |
           std::ios_base::internal);
  return str;
}

TEST(StreamingFormatTest, LogAsLiteral) {
  std::ostringstream stream;
  const std::string not_a_literal("hello world");
  stream << LoggingDefaults << absl::LogAsLiteral(not_a_literal);

  absl::ScopedMockLog sink;

  EXPECT_CALL(sink, Send(AllOf(TextMessage(MatchesOstream(stream)),
                               TextMessage(Eq("hello world")),
                               ENCODED_MESSAGE(HasValues(ElementsAre(
                                   ValueWithLiteral(Eq("hello world"))))))));

  sink.StartCapturingLogs();
  LOG(INFO) << absl::LogAsLiteral(not_a_literal);
}

}  // namespace
