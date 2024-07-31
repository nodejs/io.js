// Copyright 2018 The Abseil Authors.
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

#include "absl/base/internal/errno_saver.h"

#include <cerrno>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/internal/strerror.h"

namespace {
using ::testing::Eq;

struct ErrnoPrinter {
  int no;
};
std::ostream &operator<<(std::ostream &os, ErrnoPrinter ep) {
  return os << absl::base_internal::StrError(ep.no) << " [" << ep.no << "]";
}
bool operator==(ErrnoPrinter one, ErrnoPrinter two) { return one.no == two.no; }

TEST(ErrnoSaverTest, Works) {
  errno = EDOM;
  {
    absl::base_internal::ErrnoSaver errno_saver;
    EXPECT_THAT(ErrnoPrinter{errno}, Eq(ErrnoPrinter{EDOM}));
    errno = ERANGE;
    EXPECT_THAT(ErrnoPrinter{errno}, Eq(ErrnoPrinter{ERANGE}));
    EXPECT_THAT(ErrnoPrinter{errno_saver()}, Eq(ErrnoPrinter{EDOM}));
  }
  EXPECT_THAT(ErrnoPrinter{errno}, Eq(ErrnoPrinter{EDOM}));
}
}  // namespace
