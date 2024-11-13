// Copyright 2024 The Abseil Authors.
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
// File: status_matchers.h
// -----------------------------------------------------------------------------
//
// Testing utilities for working with `absl::Status` and `absl::StatusOr`.
//
// Defines the following utilities:
//
//   ===============
//   `IsOkAndHolds(m)`
//   ===============
//
//   This gMock matcher matches a StatusOr<T> value whose status is OK
//   and whose inner value matches matcher m.  Example:
//
//   ```
//   using ::testing::MatchesRegex;
//   using ::absl_testing::IsOkAndHolds;
//   ...
//   absl::StatusOr<string> maybe_name = ...;
//   EXPECT_THAT(maybe_name, IsOkAndHolds(MatchesRegex("John .*")));
//   ```
//
//   ===============================
//   `StatusIs(status_code_matcher)`
//   ===============================
//
//   This is a shorthand for
//     `StatusIs(status_code_matcher, ::testing::_)`
//   In other words, it's like the two-argument `StatusIs()`, except that it
//   ignores error message.
//
//   ===============
//   `IsOk()`
//   ===============
//
//   Matches an `absl::Status` or `absl::StatusOr<T>` value whose status value
//   is `absl::StatusCode::kOk.`
//
//   Equivalent to 'StatusIs(absl::StatusCode::kOk)'.
//   Example:
//   ```
//   using ::absl_testing::IsOk;
//   ...
//   absl::StatusOr<string> maybe_name = ...;
//   EXPECT_THAT(maybe_name, IsOk());
//   Status s = ...;
//   EXPECT_THAT(s, IsOk());
//   ```

#ifndef ABSL_STATUS_STATUS_MATCHERS_H_
#define ABSL_STATUS_STATUS_MATCHERS_H_

#include <ostream>  // NOLINT
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"  // gmock_for_status_matchers.h
#include "absl/base/config.h"
#include "absl/status/internal/status_matchers.h"

namespace absl_testing {
ABSL_NAMESPACE_BEGIN

// Returns a gMock matcher that matches a StatusOr<> whose status is
// OK and whose value matches the inner matcher.
template <typename InnerMatcherT>
status_internal::IsOkAndHoldsMatcher<typename std::decay<InnerMatcherT>::type>
IsOkAndHolds(InnerMatcherT&& inner_matcher) {
  return status_internal::IsOkAndHoldsMatcher<
      typename std::decay<InnerMatcherT>::type>(
      std::forward<InnerMatcherT>(inner_matcher));
}

// Returns a gMock matcher that matches a Status or StatusOr<> whose status code
// matches code_matcher and whose error message matches message_matcher.
// Typically, code_matcher will be an absl::StatusCode, e.g.
//
// StatusIs(absl::StatusCode::kInvalidArgument, "...")
template <typename StatusCodeMatcherT, typename StatusMessageMatcherT>
status_internal::StatusIsMatcher StatusIs(
    StatusCodeMatcherT&& code_matcher,
    StatusMessageMatcherT&& message_matcher) {
  return status_internal::StatusIsMatcher(
      std::forward<StatusCodeMatcherT>(code_matcher),
      std::forward<StatusMessageMatcherT>(message_matcher));
}

// Returns a gMock matcher that matches a Status or StatusOr<> and whose status
// code matches code_matcher.  See above for details.
template <typename StatusCodeMatcherT>
status_internal::StatusIsMatcher StatusIs(StatusCodeMatcherT&& code_matcher) {
  return StatusIs(std::forward<StatusCodeMatcherT>(code_matcher), ::testing::_);
}

// Returns a gMock matcher that matches a Status or StatusOr<> which is OK.
inline status_internal::IsOkMatcher IsOk() {
  return status_internal::IsOkMatcher();
}

ABSL_NAMESPACE_END
}  // namespace absl_testing

#endif  // ABSL_STATUS_STATUS_MATCHERS_H_
