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

#ifndef ABSL_STRINGS_CORDZ_TEST_HELPERS_H_
#define ABSL_STRINGS_CORDZ_TEST_HELPERS_H_

#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/strings/cord.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cordz_info.h"
#include "absl/strings/internal/cordz_sample_token.h"
#include "absl/strings/internal/cordz_statistics.h"
#include "absl/strings/internal/cordz_update_tracker.h"
#include "absl/strings/str_cat.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// Returns the CordzInfo for the cord, or nullptr if the cord is not sampled.
inline absl::Nullable<const cord_internal::CordzInfo*> GetCordzInfoForTesting(
    const Cord& cord) {
  if (!cord.contents_.is_tree()) return nullptr;
  return cord.contents_.cordz_info();
}

// Returns true if the provided cordz_info is in the list of sampled cords.
inline bool CordzInfoIsListed(
    absl::Nonnull<const cord_internal::CordzInfo*> cordz_info,
    cord_internal::CordzSampleToken token = {}) {
  for (const cord_internal::CordzInfo& info : token) {
    if (cordz_info == &info) return true;
  }
  return false;
}

// Matcher on Cord that verifies all of:
// - the cord is sampled
// - the CordzInfo of the cord is listed / discoverable.
// - the reported CordzStatistics match the cord's actual properties
// - the cord has an (initial) UpdateTracker count of 1 for `method`
MATCHER_P(HasValidCordzInfoOf, method, "CordzInfo matches cord") {
  const cord_internal::CordzInfo* cord_info = GetCordzInfoForTesting(arg);
  if (cord_info == nullptr) {
    *result_listener << "cord is not sampled";
    return false;
  }
  if (!CordzInfoIsListed(cord_info)) {
    *result_listener << "cord is sampled, but not listed";
    return false;
  }
  cord_internal::CordzStatistics stat = cord_info->GetCordzStatistics();
  if (stat.size != arg.size()) {
    *result_listener << "cordz size " << stat.size
                     << " does not match cord size " << arg.size();
    return false;
  }
  if (stat.update_tracker.Value(method) != 1) {
    *result_listener << "Expected method count 1 for " << method << ", found "
                     << stat.update_tracker.Value(method);
    return false;
  }
  return true;
}

// Matcher on Cord that verifies that the cord is sampled and that the CordzInfo
// update tracker has 'method' with a call count of 'n'
MATCHER_P2(CordzMethodCountEq, method, n,
           absl::StrCat("CordzInfo method count equals ", n)) {
  const cord_internal::CordzInfo* cord_info = GetCordzInfoForTesting(arg);
  if (cord_info == nullptr) {
    *result_listener << "cord is not sampled";
    return false;
  }
  cord_internal::CordzStatistics stat = cord_info->GetCordzStatistics();
  if (stat.update_tracker.Value(method) != n) {
    *result_listener << "Expected method count " << n << " for " << method
                     << ", found " << stat.update_tracker.Value(method);
    return false;
  }
  return true;
}

// Cordz will only update with a new rate once the previously scheduled event
// has fired. When we disable Cordz, a long delay takes place where we won't
// consider profiling new Cords. CordzSampleIntervalHelper will burn through
// that interval and allow for testing that assumes that the average sampling
// interval is a particular value.
class CordzSamplingIntervalHelper {
 public:
  explicit CordzSamplingIntervalHelper(int32_t interval)
      : orig_mean_interval_(absl::cord_internal::get_cordz_mean_interval()) {
    absl::cord_internal::set_cordz_mean_interval(interval);
    absl::cord_internal::cordz_set_next_sample_for_testing(interval);
  }

  ~CordzSamplingIntervalHelper() {
    absl::cord_internal::set_cordz_mean_interval(orig_mean_interval_);
    absl::cord_internal::cordz_set_next_sample_for_testing(orig_mean_interval_);
  }

 private:
  int32_t orig_mean_interval_;
};

// Wrapper struct managing a small CordRep `rep`
struct TestCordRep {
  absl::Nonnull<cord_internal::CordRepFlat*> rep;

  TestCordRep() {
    rep = cord_internal::CordRepFlat::New(100);
    rep->length = 100;
    memset(rep->Data(), 1, 100);
  }
  ~TestCordRep() { cord_internal::CordRep::Unref(rep); }
};

// Wrapper struct managing a small CordRep `rep`, and
// an InlineData `data` initialized with that CordRep.
struct TestCordData {
  TestCordRep rep;
  cord_internal::InlineData data{rep.rep};
};

// Creates a Cord that is not sampled
template <typename... Args>
Cord UnsampledCord(Args... args) {
  CordzSamplingIntervalHelper never(9999);
  Cord cord(std::forward<Args>(args)...);
  ABSL_ASSERT(GetCordzInfoForTesting(cord) == nullptr);
  return cord;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_CORDZ_TEST_HELPERS_H_
