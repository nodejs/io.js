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

#include "absl/strings/internal/str_format/bind.h"

#include <string.h>
#include <limits>

#include "gtest/gtest.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace str_format_internal {
namespace {

class FormatBindTest : public ::testing::Test {
 public:
  bool Extract(const char *s, UnboundConversion *props, int *next) const {
    return ConsumeUnboundConversion(s, s + strlen(s), props, next) ==
           s + strlen(s);
  }
};

TEST_F(FormatBindTest, BindSingle) {
  struct Expectation {
    int line;
    const char *fmt;
    int ok_phases;
    const FormatArgImpl *arg;
    int width;
    int precision;
    int next_arg;
  };
  const int no = -1;
  const int ia[] = { 10, 20, 30, 40};
  const FormatArgImpl args[] = {FormatArgImpl(ia[0]), FormatArgImpl(ia[1]),
                                FormatArgImpl(ia[2]), FormatArgImpl(ia[3])};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  const Expectation kExpect[] = {
    {__LINE__, "d",          2, &args[0], no, no, 2},
    {__LINE__, "4d",         2, &args[0],  4, no, 2},
    {__LINE__, ".5d",        2, &args[0], no,  5, 2},
    {__LINE__, "4.5d",       2, &args[0],  4,  5, 2},
    {__LINE__, "*d",         2, &args[1], 10, no, 3},
    {__LINE__, ".*d",        2, &args[1], no, 10, 3},
    {__LINE__, "*.*d",       2, &args[2], 10, 20, 4},
    {__LINE__, "1$d",        2, &args[0], no, no, 0},
    {__LINE__, "2$d",        2, &args[1], no, no, 0},
    {__LINE__, "3$d",        2, &args[2], no, no, 0},
    {__LINE__, "4$d",        2, &args[3], no, no, 0},
    {__LINE__, "2$*1$d",     2, &args[1], 10, no, 0},
    {__LINE__, "2$*2$d",     2, &args[1], 20, no, 0},
    {__LINE__, "2$*3$d",     2, &args[1], 30, no, 0},
    {__LINE__, "2$.*1$d",    2, &args[1], no, 10, 0},
    {__LINE__, "2$.*2$d",    2, &args[1], no, 20, 0},
    {__LINE__, "2$.*3$d",    2, &args[1], no, 30, 0},
    {__LINE__, "2$*3$.*1$d", 2, &args[1], 30, 10, 0},
    {__LINE__, "2$*2$.*2$d", 2, &args[1], 20, 20, 0},
    {__LINE__, "2$*1$.*3$d", 2, &args[1], 10, 30, 0},
    {__LINE__, "2$*3$.*1$d", 2, &args[1], 30, 10, 0},
    {__LINE__, "1$*d",       0},  // indexed, then positional
    {__LINE__, "*2$d",       0},  // positional, then indexed
    {__LINE__, "6$d",        1},  // arg position out of bounds
    {__LINE__, "1$6$d",      0},  // width position incorrectly specified
    {__LINE__, "1$.6$d",     0},  // precision position incorrectly specified
    {__LINE__, "1$*6$d",     1},  // width position out of bounds
    {__LINE__, "1$.*6$d",    1},  // precision position out of bounds
  };
#pragma GCC diagnostic pop
  for (const Expectation &e : kExpect) {
    SCOPED_TRACE(e.line);
    SCOPED_TRACE(e.fmt);
    UnboundConversion props;
    BoundConversion bound;
    int ok_phases = 0;
    int next = 0;
    if (Extract(e.fmt, &props, &next)) {
      ++ok_phases;
      if (BindWithPack(&props, args, &bound)) {
        ++ok_phases;
      }
    }
    EXPECT_EQ(e.ok_phases, ok_phases);
    if (e.ok_phases < 2) continue;
    if (e.arg != nullptr) {
      EXPECT_EQ(e.arg, bound.arg());
    }
    EXPECT_EQ(e.width, bound.width());
    EXPECT_EQ(e.precision, bound.precision());
  }
}

TEST_F(FormatBindTest, WidthUnderflowRegression) {
  UnboundConversion props;
  BoundConversion bound;
  int next = 0;
  const int args_i[] = {std::numeric_limits<int>::min(), 17};
  const FormatArgImpl args[] = {FormatArgImpl(args_i[0]),
                                FormatArgImpl(args_i[1])};
  ASSERT_TRUE(Extract("*d", &props, &next));
  ASSERT_TRUE(BindWithPack(&props, args, &bound));

  EXPECT_EQ(bound.width(), std::numeric_limits<int>::max());
  EXPECT_EQ(bound.arg(), args + 1);
}

TEST_F(FormatBindTest, FormatPack) {
  struct Expectation {
    int line;
    const char *fmt;
    const char *summary;
  };
  const int ia[] = { 10, 20, 30, 40, -10 };
  const FormatArgImpl args[] = {FormatArgImpl(ia[0]), FormatArgImpl(ia[1]),
                                FormatArgImpl(ia[2]), FormatArgImpl(ia[3]),
                                FormatArgImpl(ia[4])};
  const Expectation kExpect[] = {
      {__LINE__, "a%4db%dc", "a{10:4d}b{20:d}c"},
      {__LINE__, "a%.4db%dc", "a{10:.4d}b{20:d}c"},
      {__LINE__, "a%4.5db%dc", "a{10:4.5d}b{20:d}c"},
      {__LINE__, "a%db%4.5dc", "a{10:d}b{20:4.5d}c"},
      {__LINE__, "a%db%*.*dc", "a{10:d}b{40:20.30d}c"},
      {__LINE__, "a%.*fb", "a{20:.10f}b"},
      {__LINE__, "a%1$db%2$*3$.*4$dc", "a{10:d}b{20:30.40d}c"},
      {__LINE__, "a%4$db%3$*2$.*1$dc", "a{40:d}b{30:20.10d}c"},
      {__LINE__, "a%04ldb", "a{10:04d}b"},
      {__LINE__, "a%-#04lldb", "a{10:-#04d}b"},
      {__LINE__, "a%1$*5$db", "a{10:-10d}b"},
      {__LINE__, "a%1$.*5$db", "a{10:d}b"},
  };
  for (const Expectation &e : kExpect) {
    absl::string_view fmt = e.fmt;
    SCOPED_TRACE(e.line);
    SCOPED_TRACE(e.fmt);
    UntypedFormatSpecImpl format(fmt);
    EXPECT_EQ(e.summary,
              str_format_internal::Summarize(format, absl::MakeSpan(args)))
        << "line:" << e.line;
  }
}

}  // namespace
}  // namespace str_format_internal
ABSL_NAMESPACE_END
}  // namespace absl
