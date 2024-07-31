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

#include "absl/flags/commandlineflag.h"

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "absl/flags/config.h"
#include "absl/flags/flag.h"
#include "absl/flags/internal/private_handle_accessor.h"
#include "absl/flags/reflection.h"
#include "absl/flags/usage_config.h"
#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

ABSL_FLAG(int, int_flag, 201, "int_flag help");
ABSL_FLAG(std::string, string_flag, "dflt",
          absl::StrCat("string_flag", " help"));
ABSL_RETIRED_FLAG(bool, bool_retired_flag, false, "bool_retired_flag help");

// These are only used to test default values.
ABSL_FLAG(int, int_flag2, 201, "");
ABSL_FLAG(std::string, string_flag2, "dflt", "");

namespace {

namespace flags = absl::flags_internal;

class CommandLineFlagTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    // Install a function to normalize filenames before this test is run.
    absl::FlagsUsageConfig default_config;
    default_config.normalize_filename = &CommandLineFlagTest::NormalizeFileName;
    absl::SetFlagsUsageConfig(default_config);
  }

  void SetUp() override {
#if ABSL_FLAGS_STRIP_NAMES
    GTEST_SKIP() << "This test requires flag names to be present";
#endif
    flag_saver_ = absl::make_unique<absl::FlagSaver>();
  }
  void TearDown() override { flag_saver_.reset(); }

 private:
  static std::string NormalizeFileName(absl::string_view fname) {
#ifdef _WIN32
    std::string normalized(fname);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    fname = normalized;
#endif
    return std::string(fname);
  }

  std::unique_ptr<absl::FlagSaver> flag_saver_;
};

TEST_F(CommandLineFlagTest, TestAttributesAccessMethods) {
  auto* flag_01 = absl::FindCommandLineFlag("int_flag");

  ASSERT_TRUE(flag_01);
  EXPECT_EQ(flag_01->Name(), "int_flag");
  EXPECT_EQ(flag_01->Help(), "int_flag help");
  EXPECT_TRUE(!flag_01->IsRetired());
  EXPECT_TRUE(flag_01->IsOfType<int>());
  EXPECT_TRUE(!flag_01->IsOfType<bool>());
  EXPECT_TRUE(!flag_01->IsOfType<std::string>());
  EXPECT_TRUE(absl::EndsWith(flag_01->Filename(),
                             "absl/flags/commandlineflag_test.cc"))
      << flag_01->Filename();

  auto* flag_02 = absl::FindCommandLineFlag("string_flag");

  ASSERT_TRUE(flag_02);
  EXPECT_EQ(flag_02->Name(), "string_flag");
  EXPECT_EQ(flag_02->Help(), "string_flag help");
  EXPECT_TRUE(!flag_02->IsRetired());
  EXPECT_TRUE(flag_02->IsOfType<std::string>());
  EXPECT_TRUE(!flag_02->IsOfType<bool>());
  EXPECT_TRUE(!flag_02->IsOfType<int>());
  EXPECT_TRUE(absl::EndsWith(flag_02->Filename(),
                             "absl/flags/commandlineflag_test.cc"))
      << flag_02->Filename();
}

// --------------------------------------------------------------------

TEST_F(CommandLineFlagTest, TestValueAccessMethods) {
  absl::SetFlag(&FLAGS_int_flag2, 301);
  auto* flag_01 = absl::FindCommandLineFlag("int_flag2");

  ASSERT_TRUE(flag_01);
  EXPECT_EQ(flag_01->CurrentValue(), "301");
  EXPECT_EQ(flag_01->DefaultValue(), "201");

  absl::SetFlag(&FLAGS_string_flag2, "new_str_value");
  auto* flag_02 = absl::FindCommandLineFlag("string_flag2");

  ASSERT_TRUE(flag_02);
  EXPECT_EQ(flag_02->CurrentValue(), "new_str_value");
  EXPECT_EQ(flag_02->DefaultValue(), "dflt");
}

// --------------------------------------------------------------------

TEST_F(CommandLineFlagTest, TestParseFromCurrentValue) {
  std::string err;

  auto* flag_01 = absl::FindCommandLineFlag("int_flag");
  EXPECT_FALSE(
      flags::PrivateHandleAccessor::IsSpecifiedOnCommandLine(*flag_01));

  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "11", flags::SET_FLAGS_VALUE, flags::kProgrammaticChange, err));
  EXPECT_EQ(absl::GetFlag(FLAGS_int_flag), 11);
  EXPECT_FALSE(
      flags::PrivateHandleAccessor::IsSpecifiedOnCommandLine(*flag_01));

  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "-123", flags::SET_FLAGS_VALUE, flags::kProgrammaticChange,
      err));
  EXPECT_EQ(absl::GetFlag(FLAGS_int_flag), -123);
  EXPECT_FALSE(
      flags::PrivateHandleAccessor::IsSpecifiedOnCommandLine(*flag_01));

  EXPECT_TRUE(!flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "xyz", flags::SET_FLAGS_VALUE, flags::kProgrammaticChange,
      err));
  EXPECT_EQ(absl::GetFlag(FLAGS_int_flag), -123);
  EXPECT_EQ(err, "Illegal value 'xyz' specified for flag 'int_flag'");
  EXPECT_FALSE(
      flags::PrivateHandleAccessor::IsSpecifiedOnCommandLine(*flag_01));

  EXPECT_TRUE(!flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "A1", flags::SET_FLAGS_VALUE, flags::kProgrammaticChange, err));
  EXPECT_EQ(absl::GetFlag(FLAGS_int_flag), -123);
  EXPECT_EQ(err, "Illegal value 'A1' specified for flag 'int_flag'");
  EXPECT_FALSE(
      flags::PrivateHandleAccessor::IsSpecifiedOnCommandLine(*flag_01));

  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "0x10", flags::SET_FLAGS_VALUE, flags::kProgrammaticChange,
      err));
  EXPECT_EQ(absl::GetFlag(FLAGS_int_flag), 16);
  EXPECT_FALSE(
      flags::PrivateHandleAccessor::IsSpecifiedOnCommandLine(*flag_01));

  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "011", flags::SET_FLAGS_VALUE, flags::kCommandLine, err));
  EXPECT_EQ(absl::GetFlag(FLAGS_int_flag), 11);
  EXPECT_TRUE(flags::PrivateHandleAccessor::IsSpecifiedOnCommandLine(*flag_01));

  EXPECT_TRUE(!flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "", flags::SET_FLAGS_VALUE, flags::kProgrammaticChange, err));
  EXPECT_EQ(err, "Illegal value '' specified for flag 'int_flag'");

  auto* flag_02 = absl::FindCommandLineFlag("string_flag");
  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_02, "xyz", flags::SET_FLAGS_VALUE, flags::kProgrammaticChange,
      err));
  EXPECT_EQ(absl::GetFlag(FLAGS_string_flag), "xyz");

  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_02, "", flags::SET_FLAGS_VALUE, flags::kProgrammaticChange, err));
  EXPECT_EQ(absl::GetFlag(FLAGS_string_flag), "");
}

// --------------------------------------------------------------------

TEST_F(CommandLineFlagTest, TestParseFromDefaultValue) {
  std::string err;

  auto* flag_01 = absl::FindCommandLineFlag("int_flag");

  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "111", flags::SET_FLAGS_DEFAULT, flags::kProgrammaticChange,
      err));
  EXPECT_EQ(flag_01->DefaultValue(), "111");

  auto* flag_02 = absl::FindCommandLineFlag("string_flag");

  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_02, "abc", flags::SET_FLAGS_DEFAULT, flags::kProgrammaticChange,
      err));
  EXPECT_EQ(flag_02->DefaultValue(), "abc");
}

// --------------------------------------------------------------------

TEST_F(CommandLineFlagTest, TestParseFromIfDefault) {
  std::string err;

  auto* flag_01 = absl::FindCommandLineFlag("int_flag");

  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "22", flags::SET_FLAG_IF_DEFAULT, flags::kProgrammaticChange,
      err))
      << err;
  EXPECT_EQ(absl::GetFlag(FLAGS_int_flag), 22);

  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "33", flags::SET_FLAG_IF_DEFAULT, flags::kProgrammaticChange,
      err));
  EXPECT_EQ(absl::GetFlag(FLAGS_int_flag), 22);
  // EXPECT_EQ(err, "ERROR: int_flag is already set to 22");

  // Reset back to default value
  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "201", flags::SET_FLAGS_VALUE, flags::kProgrammaticChange,
      err));

  EXPECT_TRUE(flags::PrivateHandleAccessor::ParseFrom(
      *flag_01, "33", flags::SET_FLAG_IF_DEFAULT, flags::kProgrammaticChange,
      err));
  EXPECT_EQ(absl::GetFlag(FLAGS_int_flag), 201);
  // EXPECT_EQ(err, "ERROR: int_flag is already set to 201");
}

}  // namespace
