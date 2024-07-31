// Copyright 2020 The Abseil Authors.
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

#include "absl/base/internal/strerror.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/match.h"

namespace {
using ::testing::AnyOf;
using ::testing::Eq;

TEST(StrErrorTest, ValidErrorCode) {
  errno = ERANGE;
  EXPECT_THAT(absl::base_internal::StrError(EDOM), Eq(strerror(EDOM)));
  EXPECT_THAT(errno, Eq(ERANGE));
}

TEST(StrErrorTest, InvalidErrorCode) {
  errno = ERANGE;
  EXPECT_THAT(absl::base_internal::StrError(-1),
              AnyOf(Eq("No error information"), Eq("Unknown error -1")));
  EXPECT_THAT(errno, Eq(ERANGE));
}

TEST(StrErrorTest, MultipleThreads) {
  // In this test, we will start up 2 threads and have each one call
  // StrError 1000 times, each time with a different errnum.  We
  // expect that StrError(errnum) will return a string equal to the
  // one returned by strerror(errnum), if the code is known.  Since
  // strerror is known to be thread-hostile, collect all the expected
  // strings up front.
  const int kNumCodes = 1000;
  std::vector<std::string> expected_strings(kNumCodes);
  for (int i = 0; i < kNumCodes; ++i) {
    expected_strings[i] = strerror(i);
  }

  std::atomic_int counter(0);
  auto thread_fun = [&]() {
    for (int i = 0; i < kNumCodes; ++i) {
      ++counter;
      errno = ERANGE;
      const std::string value = absl::base_internal::StrError(i);
      // EXPECT_* could change errno. Stash it first.
      int check_err = errno;
      EXPECT_THAT(check_err, Eq(ERANGE));
      // Only the GNU implementation is guaranteed to provide the
      // string "Unknown error nnn". POSIX doesn't say anything.
      if (!absl::StartsWith(value, "Unknown error ")) {
        EXPECT_THAT(value, Eq(expected_strings[i]));
      }
    }
  };

  const int kNumThreads = 100;
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread(thread_fun));
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_THAT(counter, Eq(kNumThreads * kNumCodes));
}

}  // namespace
