// Copyright 2021 The Abseil Authors
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

#include "absl/strings/internal/cordz_update_tracker.h"

#include <array>
#include <thread>  // NOLINT

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/synchronization/notification.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

using ::testing::AnyOf;
using ::testing::Eq;

using Method = CordzUpdateTracker::MethodIdentifier;
using Methods = std::array<Method, Method::kNumMethods>;

// Returns an array of all methods defined in `MethodIdentifier`
Methods AllMethods() {
  return Methods{Method::kUnknown,
                 Method::kAppendCord,
                 Method::kAppendCordBuffer,
                 Method::kAppendExternalMemory,
                 Method::kAppendString,
                 Method::kAssignCord,
                 Method::kAssignString,
                 Method::kClear,
                 Method::kConstructorCord,
                 Method::kConstructorString,
                 Method::kCordReader,
                 Method::kFlatten,
                 Method::kGetAppendBuffer,
                 Method::kGetAppendRegion,
                 Method::kMakeCordFromExternal,
                 Method::kMoveAppendCord,
                 Method::kMoveAssignCord,
                 Method::kMovePrependCord,
                 Method::kPrependCord,
                 Method::kPrependCordBuffer,
                 Method::kPrependString,
                 Method::kRemovePrefix,
                 Method::kRemoveSuffix,
                 Method::kSetExpectedChecksum,
                 Method::kSubCord};
}

TEST(CordzUpdateTracker, IsConstExprAndInitializesToZero) {
  constexpr CordzUpdateTracker tracker;
  for (Method method : AllMethods()) {
    ASSERT_THAT(tracker.Value(method), Eq(0));
  }
}

TEST(CordzUpdateTracker, LossyAdd) {
  int64_t n = 1;
  CordzUpdateTracker tracker;
  for (Method method : AllMethods()) {
    tracker.LossyAdd(method, n);
    EXPECT_THAT(tracker.Value(method), Eq(n));
    n += 2;
  }
}

TEST(CordzUpdateTracker, CopyConstructor) {
  int64_t n = 1;
  CordzUpdateTracker src;
  for (Method method : AllMethods()) {
    src.LossyAdd(method, n);
    n += 2;
  }

  n = 1;
  CordzUpdateTracker tracker(src);
  for (Method method : AllMethods()) {
    EXPECT_THAT(tracker.Value(method), Eq(n));
    n += 2;
  }
}

TEST(CordzUpdateTracker, OperatorAssign) {
  int64_t n = 1;
  CordzUpdateTracker src;
  CordzUpdateTracker tracker;
  for (Method method : AllMethods()) {
    src.LossyAdd(method, n);
    n += 2;
  }

  n = 1;
  tracker = src;
  for (Method method : AllMethods()) {
    EXPECT_THAT(tracker.Value(method), Eq(n));
    n += 2;
  }
}

TEST(CordzUpdateTracker, ThreadSanitizedValueCheck) {
  absl::Notification done;
  CordzUpdateTracker tracker;

  std::thread reader([&done, &tracker] {
    while (!done.HasBeenNotified()) {
      int n = 1;
      for (Method method : AllMethods()) {
        EXPECT_THAT(tracker.Value(method), AnyOf(Eq(n), Eq(0)));
        n += 2;
      }
    }
    int n = 1;
    for (Method method : AllMethods()) {
      EXPECT_THAT(tracker.Value(method), Eq(n));
      n += 2;
    }
  });

  int64_t n = 1;
  for (Method method : AllMethods()) {
    tracker.LossyAdd(method, n);
    n += 2;
  }
  done.Notify();
  reader.join();
}

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
