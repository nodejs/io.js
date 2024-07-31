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

#include "absl/strings/internal/cordz_update_scope.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/strings/cordz_test_helpers.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/internal/cordz_info.h"
#include "absl/strings/internal/cordz_update_tracker.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

namespace {

// Used test values
auto constexpr kTrackCordMethod = CordzUpdateTracker::kConstructorString;

TEST(CordzUpdateScopeTest, ScopeNullptr) {
  CordzUpdateScope scope(nullptr, kTrackCordMethod);
}

TEST(CordzUpdateScopeTest, ScopeSampledCord) {
  TestCordData cord;
  CordzInfo::TrackCord(cord.data, kTrackCordMethod);
  CordzUpdateScope scope(cord.data.cordz_info(), kTrackCordMethod);
  cord.data.cordz_info()->SetCordRep(nullptr);
}

}  // namespace
ABSL_NAMESPACE_END
}  // namespace cord_internal

}  // namespace absl
