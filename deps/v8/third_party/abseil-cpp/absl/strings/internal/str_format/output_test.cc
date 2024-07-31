// Copyright 2017 The Abseil Authors.
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

#include "absl/strings/internal/str_format/output.h"

#include <sstream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/cord.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

TEST(InvokeFlush, String) {
  std::string str = "ABC";
  str_format_internal::InvokeFlush(&str, "DEF");
  EXPECT_EQ(str, "ABCDEF");
}

TEST(InvokeFlush, Stream) {
  std::stringstream str;
  str << "ABC";
  str_format_internal::InvokeFlush(&str, "DEF");
  EXPECT_EQ(str.str(), "ABCDEF");
}

TEST(InvokeFlush, Cord) {
  absl::Cord str("ABC");
  str_format_internal::InvokeFlush(&str, "DEF");
  EXPECT_EQ(str, "ABCDEF");
}

TEST(BufferRawSink, Limits) {
  char buf[16];
  {
    std::fill(std::begin(buf), std::end(buf), 'x');
    str_format_internal::BufferRawSink bufsink(buf, sizeof(buf) - 1);
    str_format_internal::InvokeFlush(&bufsink, "Hello World237");
    EXPECT_EQ(std::string(buf, sizeof(buf)), "Hello World237xx");
  }
  {
    std::fill(std::begin(buf), std::end(buf), 'x');
    str_format_internal::BufferRawSink bufsink(buf, sizeof(buf) - 1);
    str_format_internal::InvokeFlush(&bufsink, "Hello World237237");
    EXPECT_EQ(std::string(buf, sizeof(buf)), "Hello World2372x");
  }
  {
    std::fill(std::begin(buf), std::end(buf), 'x');
    str_format_internal::BufferRawSink bufsink(buf, sizeof(buf) - 1);
    str_format_internal::InvokeFlush(&bufsink, "Hello World");
    str_format_internal::InvokeFlush(&bufsink, "237");
    EXPECT_EQ(std::string(buf, sizeof(buf)), "Hello World237xx");
  }
  {
    std::fill(std::begin(buf), std::end(buf), 'x');
    str_format_internal::BufferRawSink bufsink(buf, sizeof(buf) - 1);
    str_format_internal::InvokeFlush(&bufsink, "Hello World");
    str_format_internal::InvokeFlush(&bufsink, "237237");
    EXPECT_EQ(std::string(buf, sizeof(buf)), "Hello World2372x");
  }
}

}  // namespace
ABSL_NAMESPACE_END
}  // namespace absl
