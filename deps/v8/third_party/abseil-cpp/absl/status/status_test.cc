// Copyright 2019 The Abseil Authors.
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

#include "absl/status/status.h"

#include <errno.h>

#include <array>
#include <cstddef>
#include <sstream>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace {

using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Optional;
using ::testing::UnorderedElementsAreArray;

TEST(StatusCode, InsertionOperator) {
  const absl::StatusCode code = absl::StatusCode::kUnknown;
  std::ostringstream oss;
  oss << code;
  EXPECT_EQ(oss.str(), absl::StatusCodeToString(code));
}

// This structure holds the details for testing a single error code,
// its creator, and its classifier.
struct ErrorTest {
  absl::StatusCode code;
  using Creator = absl::Status (*)(
      absl::string_view
  );
  using Classifier = bool (*)(const absl::Status&);
  Creator creator;
  Classifier classifier;
};

constexpr ErrorTest kErrorTests[]{
    {absl::StatusCode::kCancelled, absl::CancelledError, absl::IsCancelled},
    {absl::StatusCode::kUnknown, absl::UnknownError, absl::IsUnknown},
    {absl::StatusCode::kInvalidArgument, absl::InvalidArgumentError,
     absl::IsInvalidArgument},
    {absl::StatusCode::kDeadlineExceeded, absl::DeadlineExceededError,
     absl::IsDeadlineExceeded},
    {absl::StatusCode::kNotFound, absl::NotFoundError, absl::IsNotFound},
    {absl::StatusCode::kAlreadyExists, absl::AlreadyExistsError,
     absl::IsAlreadyExists},
    {absl::StatusCode::kPermissionDenied, absl::PermissionDeniedError,
     absl::IsPermissionDenied},
    {absl::StatusCode::kResourceExhausted, absl::ResourceExhaustedError,
     absl::IsResourceExhausted},
    {absl::StatusCode::kFailedPrecondition, absl::FailedPreconditionError,
     absl::IsFailedPrecondition},
    {absl::StatusCode::kAborted, absl::AbortedError, absl::IsAborted},
    {absl::StatusCode::kOutOfRange, absl::OutOfRangeError, absl::IsOutOfRange},
    {absl::StatusCode::kUnimplemented, absl::UnimplementedError,
     absl::IsUnimplemented},
    {absl::StatusCode::kInternal, absl::InternalError, absl::IsInternal},
    {absl::StatusCode::kUnavailable, absl::UnavailableError,
     absl::IsUnavailable},
    {absl::StatusCode::kDataLoss, absl::DataLossError, absl::IsDataLoss},
    {absl::StatusCode::kUnauthenticated, absl::UnauthenticatedError,
     absl::IsUnauthenticated},
};

TEST(Status, CreateAndClassify) {
  for (const auto& test : kErrorTests) {
    SCOPED_TRACE(absl::StatusCodeToString(test.code));

    // Ensure that the creator does, in fact, create status objects with the
    // expected error code and message.
    std::string message =
        absl::StrCat("error code ", test.code, " test message");
    absl::Status status = test.creator(
        message
    );
    EXPECT_EQ(test.code, status.code());
    EXPECT_EQ(message, status.message());

    // Ensure that the classifier returns true for a status produced by the
    // creator.
    EXPECT_TRUE(test.classifier(status));

    // Ensure that the classifier returns false for status with a different
    // code.
    for (const auto& other : kErrorTests) {
      if (other.code != test.code) {
        EXPECT_FALSE(test.classifier(absl::Status(other.code, "")))
            << " other.code = " << other.code;
      }
    }
  }
}

TEST(Status, DefaultConstructor) {
  absl::Status status;
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(absl::StatusCode::kOk, status.code());
  EXPECT_EQ("", status.message());
}

TEST(Status, OkStatus) {
  absl::Status status = absl::OkStatus();
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(absl::StatusCode::kOk, status.code());
  EXPECT_EQ("", status.message());
}

TEST(Status, ConstructorWithCodeMessage) {
  {
    absl::Status status(absl::StatusCode::kCancelled, "");
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(absl::StatusCode::kCancelled, status.code());
    EXPECT_EQ("", status.message());
  }
  {
    absl::Status status(absl::StatusCode::kInternal, "message");
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(absl::StatusCode::kInternal, status.code());
    EXPECT_EQ("message", status.message());
  }
}

TEST(Status, StatusMessageCStringTest) {
  {
    absl::Status status = absl::OkStatus();
    EXPECT_EQ(status.message(), "");
    EXPECT_STREQ(absl::StatusMessageAsCStr(status), "");
    EXPECT_EQ(status.message(), absl::StatusMessageAsCStr(status));
    EXPECT_NE(absl::StatusMessageAsCStr(status), nullptr);
  }
  {
    absl::Status status;
    EXPECT_EQ(status.message(), "");
    EXPECT_NE(absl::StatusMessageAsCStr(status), nullptr);
    EXPECT_STREQ(absl::StatusMessageAsCStr(status), "");
  }
  {
    absl::Status status(absl::StatusCode::kInternal, "message");
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(absl::StatusCode::kInternal, status.code());
    EXPECT_EQ("message", status.message());
    EXPECT_STREQ("message", absl::StatusMessageAsCStr(status));
  }
}

TEST(Status, ConstructOutOfRangeCode) {
  const int kRawCode = 9999;
  absl::Status status(static_cast<absl::StatusCode>(kRawCode), "");
  EXPECT_EQ(absl::StatusCode::kUnknown, status.code());
  EXPECT_EQ(kRawCode, status.raw_code());
}

constexpr char kUrl1[] = "url.payload.1";
constexpr char kUrl2[] = "url.payload.2";
constexpr char kUrl3[] = "url.payload.3";
constexpr char kUrl4[] = "url.payload.xx";

constexpr char kPayload1[] = "aaaaa";
constexpr char kPayload2[] = "bbbbb";
constexpr char kPayload3[] = "ccccc";

using PayloadsVec = std::vector<std::pair<std::string, absl::Cord>>;

TEST(Status, TestGetSetPayload) {
  absl::Status ok_status = absl::OkStatus();
  ok_status.SetPayload(kUrl1, absl::Cord(kPayload1));
  ok_status.SetPayload(kUrl2, absl::Cord(kPayload2));

  EXPECT_FALSE(ok_status.GetPayload(kUrl1));
  EXPECT_FALSE(ok_status.GetPayload(kUrl2));

  absl::Status bad_status(absl::StatusCode::kInternal, "fail");
  bad_status.SetPayload(kUrl1, absl::Cord(kPayload1));
  bad_status.SetPayload(kUrl2, absl::Cord(kPayload2));

  EXPECT_THAT(bad_status.GetPayload(kUrl1), Optional(Eq(kPayload1)));
  EXPECT_THAT(bad_status.GetPayload(kUrl2), Optional(Eq(kPayload2)));

  EXPECT_FALSE(bad_status.GetPayload(kUrl3));

  bad_status.SetPayload(kUrl1, absl::Cord(kPayload3));
  EXPECT_THAT(bad_status.GetPayload(kUrl1), Optional(Eq(kPayload3)));

  // Testing dynamically generated type_url
  bad_status.SetPayload(absl::StrCat(kUrl1, ".1"), absl::Cord(kPayload1));
  EXPECT_THAT(bad_status.GetPayload(absl::StrCat(kUrl1, ".1")),
              Optional(Eq(kPayload1)));
}

TEST(Status, TestErasePayload) {
  absl::Status bad_status(absl::StatusCode::kInternal, "fail");
  bad_status.SetPayload(kUrl1, absl::Cord(kPayload1));
  bad_status.SetPayload(kUrl2, absl::Cord(kPayload2));
  bad_status.SetPayload(kUrl3, absl::Cord(kPayload3));

  EXPECT_FALSE(bad_status.ErasePayload(kUrl4));

  EXPECT_TRUE(bad_status.GetPayload(kUrl2));
  EXPECT_TRUE(bad_status.ErasePayload(kUrl2));
  EXPECT_FALSE(bad_status.GetPayload(kUrl2));
  EXPECT_FALSE(bad_status.ErasePayload(kUrl2));

  EXPECT_TRUE(bad_status.ErasePayload(kUrl1));
  EXPECT_TRUE(bad_status.ErasePayload(kUrl3));

  bad_status.SetPayload(kUrl1, absl::Cord(kPayload1));
  EXPECT_TRUE(bad_status.ErasePayload(kUrl1));
}

TEST(Status, TestComparePayloads) {
  absl::Status bad_status1(absl::StatusCode::kInternal, "fail");
  bad_status1.SetPayload(kUrl1, absl::Cord(kPayload1));
  bad_status1.SetPayload(kUrl2, absl::Cord(kPayload2));
  bad_status1.SetPayload(kUrl3, absl::Cord(kPayload3));

  absl::Status bad_status2(absl::StatusCode::kInternal, "fail");
  bad_status2.SetPayload(kUrl2, absl::Cord(kPayload2));
  bad_status2.SetPayload(kUrl3, absl::Cord(kPayload3));
  bad_status2.SetPayload(kUrl1, absl::Cord(kPayload1));

  EXPECT_EQ(bad_status1, bad_status2);
}

TEST(Status, TestComparePayloadsAfterErase) {
  absl::Status payload_status(absl::StatusCode::kInternal, "");
  payload_status.SetPayload(kUrl1, absl::Cord(kPayload1));
  payload_status.SetPayload(kUrl2, absl::Cord(kPayload2));

  absl::Status empty_status(absl::StatusCode::kInternal, "");

  // Different payloads, not equal
  EXPECT_NE(payload_status, empty_status);
  EXPECT_TRUE(payload_status.ErasePayload(kUrl1));

  // Still Different payloads, still not equal.
  EXPECT_NE(payload_status, empty_status);
  EXPECT_TRUE(payload_status.ErasePayload(kUrl2));

  // Both empty payloads, should be equal
  EXPECT_EQ(payload_status, empty_status);
}

PayloadsVec AllVisitedPayloads(const absl::Status& s) {
  PayloadsVec result;

  s.ForEachPayload([&](absl::string_view type_url, const absl::Cord& payload) {
    result.push_back(std::make_pair(std::string(type_url), payload));
  });

  return result;
}

TEST(Status, TestForEachPayload) {
  absl::Status bad_status(absl::StatusCode::kInternal, "fail");
  bad_status.SetPayload(kUrl1, absl::Cord(kPayload1));
  bad_status.SetPayload(kUrl2, absl::Cord(kPayload2));
  bad_status.SetPayload(kUrl3, absl::Cord(kPayload3));

  int count = 0;

  bad_status.ForEachPayload(
      [&count](absl::string_view, const absl::Cord&) { ++count; });

  EXPECT_EQ(count, 3);

  PayloadsVec expected_payloads = {{kUrl1, absl::Cord(kPayload1)},
                                   {kUrl2, absl::Cord(kPayload2)},
                                   {kUrl3, absl::Cord(kPayload3)}};

  // Test that we visit all the payloads in the status.
  PayloadsVec visited_payloads = AllVisitedPayloads(bad_status);
  EXPECT_THAT(visited_payloads, UnorderedElementsAreArray(expected_payloads));

  // Test that visitation order is not consistent between run.
  std::vector<absl::Status> scratch;
  while (true) {
    scratch.emplace_back(absl::StatusCode::kInternal, "fail");

    scratch.back().SetPayload(kUrl1, absl::Cord(kPayload1));
    scratch.back().SetPayload(kUrl2, absl::Cord(kPayload2));
    scratch.back().SetPayload(kUrl3, absl::Cord(kPayload3));

    if (AllVisitedPayloads(scratch.back()) != visited_payloads) {
      break;
    }
  }
}

TEST(Status, ToString) {
  absl::Status status(absl::StatusCode::kInternal, "fail");
  EXPECT_EQ("INTERNAL: fail", status.ToString());
  status.SetPayload("foo", absl::Cord("bar"));
  EXPECT_EQ("INTERNAL: fail [foo='bar']", status.ToString());
  status.SetPayload("bar", absl::Cord("\377"));
  EXPECT_THAT(status.ToString(),
              AllOf(HasSubstr("INTERNAL: fail"), HasSubstr("[foo='bar']"),
                    HasSubstr("[bar='\\xff']")));
}

TEST(Status, ToStringMode) {
  absl::Status status(absl::StatusCode::kInternal, "fail");
  status.SetPayload("foo", absl::Cord("bar"));
  status.SetPayload("bar", absl::Cord("\377"));

  EXPECT_EQ("INTERNAL: fail",
            status.ToString(absl::StatusToStringMode::kWithNoExtraData));

  EXPECT_THAT(status.ToString(absl::StatusToStringMode::kWithPayload),
              AllOf(HasSubstr("INTERNAL: fail"), HasSubstr("[foo='bar']"),
                    HasSubstr("[bar='\\xff']")));

  EXPECT_THAT(status.ToString(absl::StatusToStringMode::kWithEverything),
              AllOf(HasSubstr("INTERNAL: fail"), HasSubstr("[foo='bar']"),
                    HasSubstr("[bar='\\xff']")));

  EXPECT_THAT(status.ToString(~absl::StatusToStringMode::kWithPayload),
              AllOf(HasSubstr("INTERNAL: fail"), Not(HasSubstr("[foo='bar']")),
                    Not(HasSubstr("[bar='\\xff']"))));
}

TEST(Status, OstreamOperator) {
  absl::Status status(absl::StatusCode::kInternal, "fail");
  { std::stringstream stream;
    stream << status;
    EXPECT_EQ("INTERNAL: fail", stream.str());
  }
  status.SetPayload("foo", absl::Cord("bar"));
  { std::stringstream stream;
    stream << status;
    EXPECT_EQ("INTERNAL: fail [foo='bar']", stream.str());
  }
  status.SetPayload("bar", absl::Cord("\377"));
  { std::stringstream stream;
    stream << status;
    EXPECT_THAT(stream.str(),
                AllOf(HasSubstr("INTERNAL: fail"), HasSubstr("[foo='bar']"),
                      HasSubstr("[bar='\\xff']")));
  }
}

TEST(Status, AbslStringify) {
  absl::Status status(absl::StatusCode::kInternal, "fail");
  EXPECT_EQ("INTERNAL: fail", absl::StrCat(status));
  EXPECT_EQ("INTERNAL: fail", absl::StrFormat("%v", status));
  status.SetPayload("foo", absl::Cord("bar"));
  EXPECT_EQ("INTERNAL: fail [foo='bar']", absl::StrCat(status));
  status.SetPayload("bar", absl::Cord("\377"));
  EXPECT_THAT(absl::StrCat(status),
              AllOf(HasSubstr("INTERNAL: fail"), HasSubstr("[foo='bar']"),
                    HasSubstr("[bar='\\xff']")));
}

TEST(Status, OstreamEqStringify) {
  absl::Status status(absl::StatusCode::kUnknown, "fail");
  status.SetPayload("foo", absl::Cord("bar"));
  std::stringstream stream;
  stream << status;
  EXPECT_EQ(stream.str(), absl::StrCat(status));
}

absl::Status EraseAndReturn(const absl::Status& base) {
  absl::Status copy = base;
  EXPECT_TRUE(copy.ErasePayload(kUrl1));
  return copy;
}

TEST(Status, CopyOnWriteForErasePayload) {
  {
    absl::Status base(absl::StatusCode::kInvalidArgument, "fail");
    base.SetPayload(kUrl1, absl::Cord(kPayload1));
    EXPECT_TRUE(base.GetPayload(kUrl1).has_value());
    absl::Status copy = EraseAndReturn(base);
    EXPECT_TRUE(base.GetPayload(kUrl1).has_value());
    EXPECT_FALSE(copy.GetPayload(kUrl1).has_value());
  }
  {
    absl::Status base(absl::StatusCode::kInvalidArgument, "fail");
    base.SetPayload(kUrl1, absl::Cord(kPayload1));
    absl::Status copy = base;

    EXPECT_TRUE(base.GetPayload(kUrl1).has_value());
    EXPECT_TRUE(copy.GetPayload(kUrl1).has_value());

    EXPECT_TRUE(base.ErasePayload(kUrl1));

    EXPECT_FALSE(base.GetPayload(kUrl1).has_value());
    EXPECT_TRUE(copy.GetPayload(kUrl1).has_value());
  }
}

TEST(Status, CopyConstructor) {
  {
    absl::Status status;
    absl::Status copy(status);
    EXPECT_EQ(copy, status);
  }
  {
    absl::Status status(absl::StatusCode::kInvalidArgument, "message");
    absl::Status copy(status);
    EXPECT_EQ(copy, status);
  }
  {
    absl::Status status(absl::StatusCode::kInvalidArgument, "message");
    status.SetPayload(kUrl1, absl::Cord(kPayload1));
    absl::Status copy(status);
    EXPECT_EQ(copy, status);
  }
}

TEST(Status, CopyAssignment) {
  absl::Status assignee;
  {
    absl::Status status;
    assignee = status;
    EXPECT_EQ(assignee, status);
  }
  {
    absl::Status status(absl::StatusCode::kInvalidArgument, "message");
    assignee = status;
    EXPECT_EQ(assignee, status);
  }
  {
    absl::Status status(absl::StatusCode::kInvalidArgument, "message");
    status.SetPayload(kUrl1, absl::Cord(kPayload1));
    assignee = status;
    EXPECT_EQ(assignee, status);
  }
}

TEST(Status, CopyAssignmentIsNotRef) {
  const absl::Status status_orig(absl::StatusCode::kInvalidArgument, "message");
  absl::Status status_copy = status_orig;
  EXPECT_EQ(status_orig, status_copy);
  status_copy.SetPayload(kUrl1, absl::Cord(kPayload1));
  EXPECT_NE(status_orig, status_copy);
}

TEST(Status, MoveConstructor) {
  {
    absl::Status status;
    absl::Status copy(absl::Status{});
    EXPECT_EQ(copy, status);
  }
  {
    absl::Status status(absl::StatusCode::kInvalidArgument, "message");
    absl::Status copy(
        absl::Status(absl::StatusCode::kInvalidArgument, "message"));
    EXPECT_EQ(copy, status);
  }
  {
    absl::Status status(absl::StatusCode::kInvalidArgument, "message");
    status.SetPayload(kUrl1, absl::Cord(kPayload1));
    absl::Status copy1(status);
    absl::Status copy2(std::move(status));
    EXPECT_EQ(copy1, copy2);
  }
}

TEST(Status, MoveAssignment) {
  absl::Status assignee;
  {
    absl::Status status;
    assignee = absl::Status();
    EXPECT_EQ(assignee, status);
  }
  {
    absl::Status status(absl::StatusCode::kInvalidArgument, "message");
    assignee = absl::Status(absl::StatusCode::kInvalidArgument, "message");
    EXPECT_EQ(assignee, status);
  }
  {
    absl::Status status(absl::StatusCode::kInvalidArgument, "message");
    status.SetPayload(kUrl1, absl::Cord(kPayload1));
    absl::Status copy(status);
    assignee = std::move(status);
    EXPECT_EQ(assignee, copy);
  }
  {
    absl::Status status(absl::StatusCode::kInvalidArgument, "message");
    absl::Status copy(status);
    assignee = static_cast<absl::Status&&>(status);
    EXPECT_EQ(assignee, copy);
  }
}

TEST(Status, Update) {
  absl::Status s;
  s.Update(absl::OkStatus());
  EXPECT_TRUE(s.ok());
  const absl::Status a(absl::StatusCode::kCancelled, "message");
  s.Update(a);
  EXPECT_EQ(s, a);
  const absl::Status b(absl::StatusCode::kInternal, "other message");
  s.Update(b);
  EXPECT_EQ(s, a);
  s.Update(absl::OkStatus());
  EXPECT_EQ(s, a);
  EXPECT_FALSE(s.ok());
}

TEST(Status, Equality) {
  absl::Status ok;
  absl::Status no_payload = absl::CancelledError("no payload");
  absl::Status one_payload = absl::InvalidArgumentError("one payload");
  one_payload.SetPayload(kUrl1, absl::Cord(kPayload1));
  absl::Status two_payloads = one_payload;
  two_payloads.SetPayload(kUrl2, absl::Cord(kPayload2));
  const std::array<absl::Status, 4> status_arr = {ok, no_payload, one_payload,
                                                  two_payloads};
  for (int i = 0; i < status_arr.size(); i++) {
    for (int j = 0; j < status_arr.size(); j++) {
      if (i == j) {
        EXPECT_TRUE(status_arr[i] == status_arr[j]);
        EXPECT_FALSE(status_arr[i] != status_arr[j]);
      } else {
        EXPECT_TRUE(status_arr[i] != status_arr[j]);
        EXPECT_FALSE(status_arr[i] == status_arr[j]);
      }
    }
  }
}

TEST(Status, Swap) {
  auto test_swap = [](const absl::Status& s1, const absl::Status& s2) {
    absl::Status copy1 = s1, copy2 = s2;
    swap(copy1, copy2);
    EXPECT_EQ(copy1, s2);
    EXPECT_EQ(copy2, s1);
  };
  const absl::Status ok;
  const absl::Status no_payload(absl::StatusCode::kAlreadyExists, "no payload");
  absl::Status with_payload(absl::StatusCode::kInternal, "with payload");
  with_payload.SetPayload(kUrl1, absl::Cord(kPayload1));
  test_swap(ok, no_payload);
  test_swap(no_payload, ok);
  test_swap(ok, with_payload);
  test_swap(with_payload, ok);
  test_swap(no_payload, with_payload);
  test_swap(with_payload, no_payload);
}

TEST(StatusErrno, ErrnoToStatusCode) {
  EXPECT_EQ(absl::ErrnoToStatusCode(0), absl::StatusCode::kOk);

  // Spot-check a few errno values.
  EXPECT_EQ(absl::ErrnoToStatusCode(EINVAL),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(absl::ErrnoToStatusCode(ENOENT), absl::StatusCode::kNotFound);

  // We'll pick a very large number so it hopefully doesn't collide to errno.
  EXPECT_EQ(absl::ErrnoToStatusCode(19980927), absl::StatusCode::kUnknown);
}

TEST(StatusErrno, ErrnoToStatus) {
  absl::Status status = absl::ErrnoToStatus(ENOENT, "Cannot open 'path'");
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
  EXPECT_EQ(status.message(), "Cannot open 'path': No such file or directory");
}

}  // namespace
