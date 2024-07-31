//
//  Copyright 2019 The Abseil Authors.
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

#include "absl/flags/reflection.h"

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/config.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

ABSL_FLAG(int, int_flag, 1, "int_flag help");
ABSL_FLAG(std::string, string_flag, "dflt", "string_flag help");
ABSL_RETIRED_FLAG(bool, bool_retired_flag, false, "bool_retired_flag help");

namespace {

class ReflectionTest : public testing::Test {
 protected:
  void SetUp() override {
#if ABSL_FLAGS_STRIP_NAMES
    GTEST_SKIP() << "This test requires flag names to be present";
#endif
    flag_saver_ = absl::make_unique<absl::FlagSaver>();
  }
  void TearDown() override { flag_saver_.reset(); }

 private:
  std::unique_ptr<absl::FlagSaver> flag_saver_;
};

// --------------------------------------------------------------------

TEST_F(ReflectionTest, TestFindCommandLineFlag) {
  auto* handle = absl::FindCommandLineFlag("some_flag");
  EXPECT_EQ(handle, nullptr);

  handle = absl::FindCommandLineFlag("int_flag");
  EXPECT_NE(handle, nullptr);

  handle = absl::FindCommandLineFlag("string_flag");
  EXPECT_NE(handle, nullptr);

  handle = absl::FindCommandLineFlag("bool_retired_flag");
  EXPECT_NE(handle, nullptr);
}

// --------------------------------------------------------------------

TEST_F(ReflectionTest, TestGetAllFlags) {
  auto all_flags = absl::GetAllFlags();
  EXPECT_NE(all_flags.find("int_flag"), all_flags.end());
  EXPECT_EQ(all_flags.find("bool_retired_flag"), all_flags.end());
  EXPECT_EQ(all_flags.find("some_undefined_flag"), all_flags.end());

  std::vector<absl::string_view> flag_names_first_attempt;
  auto all_flags_1 = absl::GetAllFlags();
  for (auto f : all_flags_1) {
    flag_names_first_attempt.push_back(f.first);
  }

  std::vector<absl::string_view> flag_names_second_attempt;
  auto all_flags_2 = absl::GetAllFlags();
  for (auto f : all_flags_2) {
    flag_names_second_attempt.push_back(f.first);
  }

  EXPECT_THAT(flag_names_first_attempt,
              ::testing::UnorderedElementsAreArray(flag_names_second_attempt));
}

// --------------------------------------------------------------------

struct CustomUDT {
  CustomUDT() : a(1), b(1) {}
  CustomUDT(int a_, int b_) : a(a_), b(b_) {}

  friend bool operator==(const CustomUDT& f1, const CustomUDT& f2) {
    return f1.a == f2.a && f1.b == f2.b;
  }

  int a;
  int b;
};
bool AbslParseFlag(absl::string_view in, CustomUDT* f, std::string*) {
  std::vector<absl::string_view> parts =
      absl::StrSplit(in, ':', absl::SkipWhitespace());

  if (parts.size() != 2) return false;

  if (!absl::SimpleAtoi(parts[0], &f->a)) return false;

  if (!absl::SimpleAtoi(parts[1], &f->b)) return false;

  return true;
}
std::string AbslUnparseFlag(const CustomUDT& f) {
  return absl::StrCat(f.a, ":", f.b);
}

}  // namespace

// --------------------------------------------------------------------

ABSL_FLAG(bool, test_flag_01, true, "");
ABSL_FLAG(int, test_flag_02, 1234, "");
ABSL_FLAG(int16_t, test_flag_03, -34, "");
ABSL_FLAG(uint16_t, test_flag_04, 189, "");
ABSL_FLAG(int32_t, test_flag_05, 10765, "");
ABSL_FLAG(uint32_t, test_flag_06, 40000, "");
ABSL_FLAG(int64_t, test_flag_07, -1234567, "");
ABSL_FLAG(uint64_t, test_flag_08, 9876543, "");
ABSL_FLAG(double, test_flag_09, -9.876e-50, "");
ABSL_FLAG(float, test_flag_10, 1.234e12f, "");
ABSL_FLAG(std::string, test_flag_11, "", "");
ABSL_FLAG(absl::Duration, test_flag_12, absl::Minutes(10), "");
static int counter = 0;
ABSL_FLAG(int, test_flag_13, 200, "").OnUpdate([]() { counter++; });
ABSL_FLAG(CustomUDT, test_flag_14, {}, "");

namespace {

TEST_F(ReflectionTest, TestFlagSaverInScope) {
  {
    absl::FlagSaver s;
    counter = 0;
    absl::SetFlag(&FLAGS_test_flag_01, false);
    absl::SetFlag(&FLAGS_test_flag_02, -1021);
    absl::SetFlag(&FLAGS_test_flag_03, 6009);
    absl::SetFlag(&FLAGS_test_flag_04, 44);
    absl::SetFlag(&FLAGS_test_flag_05, +800);
    absl::SetFlag(&FLAGS_test_flag_06, -40978756);
    absl::SetFlag(&FLAGS_test_flag_07, 23405);
    absl::SetFlag(&FLAGS_test_flag_08, 975310);
    absl::SetFlag(&FLAGS_test_flag_09, 1.00001);
    absl::SetFlag(&FLAGS_test_flag_10, -3.54f);
    absl::SetFlag(&FLAGS_test_flag_11, "asdf");
    absl::SetFlag(&FLAGS_test_flag_12, absl::Hours(20));
    absl::SetFlag(&FLAGS_test_flag_13, 4);
    absl::SetFlag(&FLAGS_test_flag_14, CustomUDT{-1, -2});
  }

  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_01), true);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_02), 1234);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_03), -34);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_04), 189);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_05), 10765);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_06), 40000);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_07), -1234567);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_08), 9876543);
  EXPECT_NEAR(absl::GetFlag(FLAGS_test_flag_09), -9.876e-50, 1e-55);
  EXPECT_NEAR(absl::GetFlag(FLAGS_test_flag_10), 1.234e12f, 1e5f);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_11), "");
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_12), absl::Minutes(10));
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_13), 200);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_14), CustomUDT{});
  EXPECT_EQ(counter, 2);
}

// --------------------------------------------------------------------

TEST_F(ReflectionTest, TestFlagSaverVsUpdateViaReflection) {
  {
    absl::FlagSaver s;
    counter = 0;
    std::string error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_01")->ParseFrom("false", &error))
        << error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_02")->ParseFrom("-4536", &error))
        << error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_03")->ParseFrom("111", &error))
        << error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_04")->ParseFrom("909", &error))
        << error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_05")->ParseFrom("-2004", &error))
        << error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_06")->ParseFrom("1000023", &error))
        << error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_07")->ParseFrom("69305", &error))
        << error;
    EXPECT_TRUE(absl::FindCommandLineFlag("test_flag_08")
                    ->ParseFrom("1000000001", &error))
        << error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_09")->ParseFrom("2.09021", &error))
        << error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_10")->ParseFrom("-33.1", &error))
        << error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_11")->ParseFrom("ADD_FOO", &error))
        << error;
    EXPECT_TRUE(absl::FindCommandLineFlag("test_flag_12")
                    ->ParseFrom("3h11m16s", &error))
        << error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_13")->ParseFrom("0", &error))
        << error;
    EXPECT_TRUE(
        absl::FindCommandLineFlag("test_flag_14")->ParseFrom("10:1", &error))
        << error;
  }

  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_01), true);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_02), 1234);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_03), -34);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_04), 189);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_05), 10765);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_06), 40000);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_07), -1234567);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_08), 9876543);
  EXPECT_NEAR(absl::GetFlag(FLAGS_test_flag_09), -9.876e-50, 1e-55);
  EXPECT_NEAR(absl::GetFlag(FLAGS_test_flag_10), 1.234e12f, 1e5f);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_11), "");
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_12), absl::Minutes(10));
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_13), 200);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_14), CustomUDT{});
  EXPECT_EQ(counter, 2);
}

// --------------------------------------------------------------------

TEST_F(ReflectionTest, TestMultipleFlagSaversInEnclosedScopes) {
  {
    absl::FlagSaver s;
    absl::SetFlag(&FLAGS_test_flag_08, 10);
    EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_08), 10);
    {
      absl::FlagSaver s;
      absl::SetFlag(&FLAGS_test_flag_08, 20);
      EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_08), 20);
      {
        absl::FlagSaver s;
        absl::SetFlag(&FLAGS_test_flag_08, -200);
        EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_08), -200);
      }
      EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_08), 20);
    }
    EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_08), 10);
  }
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_08), 9876543);
}

}  // namespace
