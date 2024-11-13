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
//
// -----------------------------------------------------------------------------
// File: status_matchers_test.cc
// -----------------------------------------------------------------------------
#include "absl/status/status_matchers.h"

#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Gt;

TEST(StatusMatcherTest, StatusIsOk) { EXPECT_THAT(absl::OkStatus(), IsOk()); }

TEST(StatusMatcherTest, StatusOrIsOk) {
  absl::StatusOr<int> ok_int = {0};
  EXPECT_THAT(ok_int, IsOk());
}

TEST(StatusMatcherTest, StatusIsNotOk) {
  absl::Status error = absl::UnknownError("Smigla");
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(error, IsOk()), "Smigla");
}

TEST(StatusMatcherTest, StatusOrIsNotOk) {
  absl::StatusOr<int> error = absl::UnknownError("Smigla");
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(error, IsOk()), "Smigla");
}

TEST(StatusMatcherTest, IsOkAndHolds) {
  absl::StatusOr<int> ok_int = {4};
  absl::StatusOr<absl::string_view> ok_str = {"text"};
  EXPECT_THAT(ok_int, IsOkAndHolds(4));
  EXPECT_THAT(ok_int, IsOkAndHolds(Gt(0)));
  EXPECT_THAT(ok_str, IsOkAndHolds("text"));
}

TEST(StatusMatcherTest, IsOkAndHoldsFailure) {
  absl::StatusOr<int> ok_int = {502};
  absl::StatusOr<int> error = absl::UnknownError("Smigla");
  absl::StatusOr<absl::string_view> ok_str = {"actual"};
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(ok_int, IsOkAndHolds(0)), "502");
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(error, IsOkAndHolds(0)), "Smigla");
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(ok_str, IsOkAndHolds("expected")),
                          "actual");
}

TEST(StatusMatcherTest, StatusIs) {
  absl::Status unknown = absl::UnknownError("unbekannt");
  absl::Status invalid = absl::InvalidArgumentError("ungueltig");
  EXPECT_THAT(absl::OkStatus(), StatusIs(absl::StatusCode::kOk));
  EXPECT_THAT(absl::OkStatus(), StatusIs(0));
  EXPECT_THAT(unknown, StatusIs(absl::StatusCode::kUnknown));
  EXPECT_THAT(unknown, StatusIs(2));
  EXPECT_THAT(unknown, StatusIs(absl::StatusCode::kUnknown, "unbekannt"));
  EXPECT_THAT(invalid, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(invalid, StatusIs(3));
  EXPECT_THAT(invalid,
              StatusIs(absl::StatusCode::kInvalidArgument, "ungueltig"));
}

TEST(StatusMatcherTest, StatusOrIs) {
  absl::StatusOr<int> ok = {42};
  absl::StatusOr<int> unknown = absl::UnknownError("unbekannt");
  absl::StatusOr<absl::string_view> invalid =
      absl::InvalidArgumentError("ungueltig");
  EXPECT_THAT(ok, StatusIs(absl::StatusCode::kOk));
  EXPECT_THAT(ok, StatusIs(0));
  EXPECT_THAT(unknown, StatusIs(absl::StatusCode::kUnknown));
  EXPECT_THAT(unknown, StatusIs(2));
  EXPECT_THAT(unknown, StatusIs(absl::StatusCode::kUnknown, "unbekannt"));
  EXPECT_THAT(invalid, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(invalid, StatusIs(3));
  EXPECT_THAT(invalid,
              StatusIs(absl::StatusCode::kInvalidArgument, "ungueltig"));
}

TEST(StatusMatcherTest, StatusIsFailure) {
  absl::Status unknown = absl::UnknownError("unbekannt");
  absl::Status invalid = absl::InvalidArgumentError("ungueltig");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(absl::OkStatus(),
                  StatusIs(absl::StatusCode::kInvalidArgument)),
      "OK");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(unknown, StatusIs(absl::StatusCode::kCancelled)), "UNKNOWN");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(unknown, StatusIs(absl::StatusCode::kUnknown, "inconnu")),
      "unbekannt");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(invalid, StatusIs(absl::StatusCode::kOutOfRange)), "INVALID");
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(invalid,
                  StatusIs(absl::StatusCode::kInvalidArgument, "invalide")),
      "ungueltig");
}

}  // namespace
