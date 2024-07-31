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

#include "absl/flags/internal/usage.h"

#include <stdint.h>

#include <sstream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/config.h"
#include "absl/flags/flag.h"
#include "absl/flags/internal/parse.h"
#include "absl/flags/internal/program_name.h"
#include "absl/flags/reflection.h"
#include "absl/flags/usage.h"
#include "absl/flags/usage_config.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

ABSL_FLAG(int, usage_reporting_test_flag_01, 101,
          "usage_reporting_test_flag_01 help message");
ABSL_FLAG(bool, usage_reporting_test_flag_02, false,
          "usage_reporting_test_flag_02 help message");
ABSL_FLAG(double, usage_reporting_test_flag_03, 1.03,
          "usage_reporting_test_flag_03 help message");
ABSL_FLAG(int64_t, usage_reporting_test_flag_04, 1000000000000004L,
          "usage_reporting_test_flag_04 help message");
ABSL_FLAG(std::string, usage_reporting_test_flag_07, "\r\n\f\v\a\b\t ",
          "usage_reporting_test_flag_07 help \r\n\f\v\a\b\t ");

static const char kTestUsageMessage[] = "Custom usage message";

struct UDT {
  UDT() = default;
  UDT(const UDT&) = default;
  UDT& operator=(const UDT&) = default;
};
static bool AbslParseFlag(absl::string_view, UDT*, std::string*) {
  return true;
}
static std::string AbslUnparseFlag(const UDT&) { return "UDT{}"; }

ABSL_FLAG(UDT, usage_reporting_test_flag_05, {},
          "usage_reporting_test_flag_05 help message");

ABSL_FLAG(
    std::string, usage_reporting_test_flag_06, {},
    "usage_reporting_test_flag_06 help message.\n"
    "\n"
    "Some more help.\n"
    "Even more long long long long long long long long long long long long "
    "help message.");

namespace {

namespace flags = absl::flags_internal;

static std::string NormalizeFileName(absl::string_view fname) {
#ifdef _WIN32
  std::string normalized(fname);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  fname = normalized;
#endif

  auto absl_pos = fname.rfind("absl/");
  if (absl_pos != absl::string_view::npos) {
    fname = fname.substr(absl_pos);
  }
  return std::string(fname);
}

class UsageReportingTest : public testing::Test {
 protected:
  UsageReportingTest() {
    // Install default config for the use on this unit test.
    // Binary may install a custom config before tests are run.
    absl::FlagsUsageConfig default_config;
    default_config.normalize_filename = &NormalizeFileName;
    absl::SetFlagsUsageConfig(default_config);
  }
  ~UsageReportingTest() override {
    flags::SetFlagsHelpMode(flags::HelpMode::kNone);
    flags::SetFlagsHelpMatchSubstr("");
    flags::SetFlagsHelpFormat(flags::HelpFormat::kHumanReadable);
  }
  void SetUp() override {
#if ABSL_FLAGS_STRIP_NAMES
    GTEST_SKIP() << "This test requires flag names to be present";
#endif
  }

 private:
  absl::FlagSaver flag_saver_;
};

// --------------------------------------------------------------------

using UsageReportingDeathTest = UsageReportingTest;

TEST_F(UsageReportingDeathTest, TestSetProgramUsageMessage) {
#if !defined(GTEST_HAS_ABSL) || !GTEST_HAS_ABSL
  // Check for kTestUsageMessage set in main() below.
  EXPECT_EQ(absl::ProgramUsageMessage(), kTestUsageMessage);
#else
  // Check for part of the usage message set by GoogleTest.
  EXPECT_THAT(absl::ProgramUsageMessage(),
              ::testing::HasSubstr(
                  "This program contains tests written using Google Test"));
#endif

  EXPECT_DEATH_IF_SUPPORTED(
      absl::SetProgramUsageMessage("custom usage message"),
      ::testing::HasSubstr("SetProgramUsageMessage() called twice"));
}

// --------------------------------------------------------------------

TEST_F(UsageReportingTest, TestFlagHelpHRF_on_flag_01) {
  const auto* flag = absl::FindCommandLineFlag("usage_reporting_test_flag_01");
  std::stringstream test_buf;

  flags::FlagHelp(test_buf, *flag, flags::HelpFormat::kHumanReadable);
  EXPECT_EQ(
      test_buf.str(),
      R"(    --usage_reporting_test_flag_01 (usage_reporting_test_flag_01 help message);
      default: 101;
)");
}

TEST_F(UsageReportingTest, TestFlagHelpHRF_on_flag_02) {
  const auto* flag = absl::FindCommandLineFlag("usage_reporting_test_flag_02");
  std::stringstream test_buf;

  flags::FlagHelp(test_buf, *flag, flags::HelpFormat::kHumanReadable);
  EXPECT_EQ(
      test_buf.str(),
      R"(    --usage_reporting_test_flag_02 (usage_reporting_test_flag_02 help message);
      default: false;
)");
}

TEST_F(UsageReportingTest, TestFlagHelpHRF_on_flag_03) {
  const auto* flag = absl::FindCommandLineFlag("usage_reporting_test_flag_03");
  std::stringstream test_buf;

  flags::FlagHelp(test_buf, *flag, flags::HelpFormat::kHumanReadable);
  EXPECT_EQ(
      test_buf.str(),
      R"(    --usage_reporting_test_flag_03 (usage_reporting_test_flag_03 help message);
      default: 1.03;
)");
}

TEST_F(UsageReportingTest, TestFlagHelpHRF_on_flag_04) {
  const auto* flag = absl::FindCommandLineFlag("usage_reporting_test_flag_04");
  std::stringstream test_buf;

  flags::FlagHelp(test_buf, *flag, flags::HelpFormat::kHumanReadable);
  EXPECT_EQ(
      test_buf.str(),
      R"(    --usage_reporting_test_flag_04 (usage_reporting_test_flag_04 help message);
      default: 1000000000000004;
)");
}

TEST_F(UsageReportingTest, TestFlagHelpHRF_on_flag_05) {
  const auto* flag = absl::FindCommandLineFlag("usage_reporting_test_flag_05");
  std::stringstream test_buf;

  flags::FlagHelp(test_buf, *flag, flags::HelpFormat::kHumanReadable);
  EXPECT_EQ(
      test_buf.str(),
      R"(    --usage_reporting_test_flag_05 (usage_reporting_test_flag_05 help message);
      default: UDT{};
)");
}

// --------------------------------------------------------------------

TEST_F(UsageReportingTest, TestFlagsHelpHRF) {
  std::string usage_test_flags_out =
      R"(usage_test: Custom usage message

  Flags from absl/flags/internal/usage_test.cc:
    --usage_reporting_test_flag_01 (usage_reporting_test_flag_01 help message);
      default: 101;
    --usage_reporting_test_flag_02 (usage_reporting_test_flag_02 help message);
      default: false;
    --usage_reporting_test_flag_03 (usage_reporting_test_flag_03 help message);
      default: 1.03;
    --usage_reporting_test_flag_04 (usage_reporting_test_flag_04 help message);
      default: 1000000000000004;
    --usage_reporting_test_flag_05 (usage_reporting_test_flag_05 help message);
      default: UDT{};
    --usage_reporting_test_flag_06 (usage_reporting_test_flag_06 help message.

      Some more help.
      Even more long long long long long long long long long long long long help
      message.); default: "";)"

      "\n    --usage_reporting_test_flag_07 (usage_reporting_test_flag_07 "
      "help\n\n      \f\v\a\b ); default: \"\r\n\f\v\a\b\t \";\n"

      R"(
Try --helpfull to get a list of all flags or --help=substring shows help for
flags which include specified substring in either in the name, or description or
path.
)";

  std::stringstream test_buf_01;
  flags::FlagsHelp(test_buf_01, "usage_test.cc",
                   flags::HelpFormat::kHumanReadable, kTestUsageMessage);
  EXPECT_EQ(test_buf_01.str(), usage_test_flags_out);

  std::stringstream test_buf_02;
  flags::FlagsHelp(test_buf_02, "flags/internal/usage_test.cc",
                   flags::HelpFormat::kHumanReadable, kTestUsageMessage);
  EXPECT_EQ(test_buf_02.str(), usage_test_flags_out);

  std::stringstream test_buf_03;
  flags::FlagsHelp(test_buf_03, "usage_test", flags::HelpFormat::kHumanReadable,
                   kTestUsageMessage);
  EXPECT_EQ(test_buf_03.str(), usage_test_flags_out);

  std::stringstream test_buf_04;
  flags::FlagsHelp(test_buf_04, "flags/invalid_file_name.cc",
                   flags::HelpFormat::kHumanReadable, kTestUsageMessage);
  EXPECT_EQ(test_buf_04.str(),
            R"(usage_test: Custom usage message

No flags matched.

Try --helpfull to get a list of all flags or --help=substring shows help for
flags which include specified substring in either in the name, or description or
path.
)");

  std::stringstream test_buf_05;
  flags::FlagsHelp(test_buf_05, "", flags::HelpFormat::kHumanReadable,
                   kTestUsageMessage);
  std::string test_out = test_buf_05.str();
  absl::string_view test_out_str(test_out);
  EXPECT_TRUE(
      absl::StartsWith(test_out_str, "usage_test: Custom usage message"));
  EXPECT_TRUE(absl::StrContains(
      test_out_str, "Flags from absl/flags/internal/usage_test.cc:"));
  EXPECT_TRUE(
      absl::StrContains(test_out_str, "-usage_reporting_test_flag_01 "));
}

// --------------------------------------------------------------------

TEST_F(UsageReportingTest, TestNoUsageFlags) {
  std::stringstream test_buf;
  EXPECT_EQ(flags::HandleUsageFlags(test_buf, kTestUsageMessage),
            flags::HelpMode::kNone);
}

// --------------------------------------------------------------------

TEST_F(UsageReportingTest, TestUsageFlag_helpshort) {
  flags::SetFlagsHelpMode(flags::HelpMode::kShort);

  std::stringstream test_buf;
  EXPECT_EQ(flags::HandleUsageFlags(test_buf, kTestUsageMessage),
            flags::HelpMode::kShort);
  EXPECT_EQ(
      test_buf.str(),
      R"(usage_test: Custom usage message

  Flags from absl/flags/internal/usage_test.cc:
    --usage_reporting_test_flag_01 (usage_reporting_test_flag_01 help message);
      default: 101;
    --usage_reporting_test_flag_02 (usage_reporting_test_flag_02 help message);
      default: false;
    --usage_reporting_test_flag_03 (usage_reporting_test_flag_03 help message);
      default: 1.03;
    --usage_reporting_test_flag_04 (usage_reporting_test_flag_04 help message);
      default: 1000000000000004;
    --usage_reporting_test_flag_05 (usage_reporting_test_flag_05 help message);
      default: UDT{};
    --usage_reporting_test_flag_06 (usage_reporting_test_flag_06 help message.

      Some more help.
      Even more long long long long long long long long long long long long help
      message.); default: "";)"

      "\n    --usage_reporting_test_flag_07 (usage_reporting_test_flag_07 "
      "help\n\n      \f\v\a\b ); default: \"\r\n\f\v\a\b\t \";\n"

      R"(
Try --helpfull to get a list of all flags or --help=substring shows help for
flags which include specified substring in either in the name, or description or
path.
)");
}

// --------------------------------------------------------------------

TEST_F(UsageReportingTest, TestUsageFlag_help_simple) {
  flags::SetFlagsHelpMode(flags::HelpMode::kImportant);

  std::stringstream test_buf;
  EXPECT_EQ(flags::HandleUsageFlags(test_buf, kTestUsageMessage),
            flags::HelpMode::kImportant);
  EXPECT_EQ(
      test_buf.str(),
      R"(usage_test: Custom usage message

  Flags from absl/flags/internal/usage_test.cc:
    --usage_reporting_test_flag_01 (usage_reporting_test_flag_01 help message);
      default: 101;
    --usage_reporting_test_flag_02 (usage_reporting_test_flag_02 help message);
      default: false;
    --usage_reporting_test_flag_03 (usage_reporting_test_flag_03 help message);
      default: 1.03;
    --usage_reporting_test_flag_04 (usage_reporting_test_flag_04 help message);
      default: 1000000000000004;
    --usage_reporting_test_flag_05 (usage_reporting_test_flag_05 help message);
      default: UDT{};
    --usage_reporting_test_flag_06 (usage_reporting_test_flag_06 help message.

      Some more help.
      Even more long long long long long long long long long long long long help
      message.); default: "";)"

      "\n    --usage_reporting_test_flag_07 (usage_reporting_test_flag_07 "
      "help\n\n      \f\v\a\b ); default: \"\r\n\f\v\a\b\t \";\n"

      R"(
Try --helpfull to get a list of all flags or --help=substring shows help for
flags which include specified substring in either in the name, or description or
path.
)");
}

// --------------------------------------------------------------------

TEST_F(UsageReportingTest, TestUsageFlag_help_one_flag) {
  flags::SetFlagsHelpMode(flags::HelpMode::kMatch);
  flags::SetFlagsHelpMatchSubstr("usage_reporting_test_flag_06");

  std::stringstream test_buf;
  EXPECT_EQ(flags::HandleUsageFlags(test_buf, kTestUsageMessage),
            flags::HelpMode::kMatch);
  EXPECT_EQ(test_buf.str(),
            R"(usage_test: Custom usage message

  Flags from absl/flags/internal/usage_test.cc:
    --usage_reporting_test_flag_06 (usage_reporting_test_flag_06 help message.

      Some more help.
      Even more long long long long long long long long long long long long help
      message.); default: "";

Try --helpfull to get a list of all flags or --help=substring shows help for
flags which include specified substring in either in the name, or description or
path.
)");
}

// --------------------------------------------------------------------

TEST_F(UsageReportingTest, TestUsageFlag_help_multiple_flag) {
  flags::SetFlagsHelpMode(flags::HelpMode::kMatch);
  flags::SetFlagsHelpMatchSubstr("test_flag");

  std::stringstream test_buf;
  EXPECT_EQ(flags::HandleUsageFlags(test_buf, kTestUsageMessage),
            flags::HelpMode::kMatch);
  EXPECT_EQ(
      test_buf.str(),
      R"(usage_test: Custom usage message

  Flags from absl/flags/internal/usage_test.cc:
    --usage_reporting_test_flag_01 (usage_reporting_test_flag_01 help message);
      default: 101;
    --usage_reporting_test_flag_02 (usage_reporting_test_flag_02 help message);
      default: false;
    --usage_reporting_test_flag_03 (usage_reporting_test_flag_03 help message);
      default: 1.03;
    --usage_reporting_test_flag_04 (usage_reporting_test_flag_04 help message);
      default: 1000000000000004;
    --usage_reporting_test_flag_05 (usage_reporting_test_flag_05 help message);
      default: UDT{};
    --usage_reporting_test_flag_06 (usage_reporting_test_flag_06 help message.

      Some more help.
      Even more long long long long long long long long long long long long help
      message.); default: "";)"

      "\n    --usage_reporting_test_flag_07 (usage_reporting_test_flag_07 "
      "help\n\n      \f\v\a\b ); default: \"\r\n\f\v\a\b\t \";\n"

      R"(
Try --helpfull to get a list of all flags or --help=substring shows help for
flags which include specified substring in either in the name, or description or
path.
)");
}

// --------------------------------------------------------------------

TEST_F(UsageReportingTest, TestUsageFlag_helppackage) {
  flags::SetFlagsHelpMode(flags::HelpMode::kPackage);

  std::stringstream test_buf;
  EXPECT_EQ(flags::HandleUsageFlags(test_buf, kTestUsageMessage),
            flags::HelpMode::kPackage);
  EXPECT_EQ(
      test_buf.str(),
      R"(usage_test: Custom usage message

  Flags from absl/flags/internal/usage_test.cc:
    --usage_reporting_test_flag_01 (usage_reporting_test_flag_01 help message);
      default: 101;
    --usage_reporting_test_flag_02 (usage_reporting_test_flag_02 help message);
      default: false;
    --usage_reporting_test_flag_03 (usage_reporting_test_flag_03 help message);
      default: 1.03;
    --usage_reporting_test_flag_04 (usage_reporting_test_flag_04 help message);
      default: 1000000000000004;
    --usage_reporting_test_flag_05 (usage_reporting_test_flag_05 help message);
      default: UDT{};
    --usage_reporting_test_flag_06 (usage_reporting_test_flag_06 help message.

      Some more help.
      Even more long long long long long long long long long long long long help
      message.); default: "";)"

      "\n    --usage_reporting_test_flag_07 (usage_reporting_test_flag_07 "
      "help\n\n      \f\v\a\b ); default: \"\r\n\f\v\a\b\t \";\n"

      R"(
Try --helpfull to get a list of all flags or --help=substring shows help for
flags which include specified substring in either in the name, or description or
path.
)");
}

// --------------------------------------------------------------------

TEST_F(UsageReportingTest, TestUsageFlag_version) {
  flags::SetFlagsHelpMode(flags::HelpMode::kVersion);

  std::stringstream test_buf;
  EXPECT_EQ(flags::HandleUsageFlags(test_buf, kTestUsageMessage),
            flags::HelpMode::kVersion);
#ifndef NDEBUG
  EXPECT_EQ(test_buf.str(), "usage_test\nDebug build (NDEBUG not #defined)\n");
#else
  EXPECT_EQ(test_buf.str(), "usage_test\n");
#endif
}

// --------------------------------------------------------------------

TEST_F(UsageReportingTest, TestUsageFlag_only_check_args) {
  flags::SetFlagsHelpMode(flags::HelpMode::kOnlyCheckArgs);

  std::stringstream test_buf;
  EXPECT_EQ(flags::HandleUsageFlags(test_buf, kTestUsageMessage),
            flags::HelpMode::kOnlyCheckArgs);
  EXPECT_EQ(test_buf.str(), "");
}

// --------------------------------------------------------------------

TEST_F(UsageReportingTest, TestUsageFlag_helpon) {
  flags::SetFlagsHelpMode(flags::HelpMode::kMatch);
  flags::SetFlagsHelpMatchSubstr("/bla-bla.");

  std::stringstream test_buf_01;
  EXPECT_EQ(flags::HandleUsageFlags(test_buf_01, kTestUsageMessage),
            flags::HelpMode::kMatch);
  EXPECT_EQ(test_buf_01.str(),
            R"(usage_test: Custom usage message

No flags matched.

Try --helpfull to get a list of all flags or --help=substring shows help for
flags which include specified substring in either in the name, or description or
path.
)");

  flags::SetFlagsHelpMatchSubstr("/usage_test.");

  std::stringstream test_buf_02;
  EXPECT_EQ(flags::HandleUsageFlags(test_buf_02, kTestUsageMessage),
            flags::HelpMode::kMatch);
  EXPECT_EQ(
      test_buf_02.str(),
      R"(usage_test: Custom usage message

  Flags from absl/flags/internal/usage_test.cc:
    --usage_reporting_test_flag_01 (usage_reporting_test_flag_01 help message);
      default: 101;
    --usage_reporting_test_flag_02 (usage_reporting_test_flag_02 help message);
      default: false;
    --usage_reporting_test_flag_03 (usage_reporting_test_flag_03 help message);
      default: 1.03;
    --usage_reporting_test_flag_04 (usage_reporting_test_flag_04 help message);
      default: 1000000000000004;
    --usage_reporting_test_flag_05 (usage_reporting_test_flag_05 help message);
      default: UDT{};
    --usage_reporting_test_flag_06 (usage_reporting_test_flag_06 help message.

      Some more help.
      Even more long long long long long long long long long long long long help
      message.); default: "";)"

      "\n    --usage_reporting_test_flag_07 (usage_reporting_test_flag_07 "
      "help\n\n      \f\v\a\b ); default: \"\r\n\f\v\a\b\t \";\n"

      R"(
Try --helpfull to get a list of all flags or --help=substring shows help for
flags which include specified substring in either in the name, or description or
path.
)");
}

// --------------------------------------------------------------------

}  // namespace

int main(int argc, char* argv[]) {
  (void)absl::GetFlag(FLAGS_undefok);  // Force linking of parse.cc
  flags::SetProgramInvocationName("usage_test");
#if !defined(GTEST_HAS_ABSL) || !GTEST_HAS_ABSL
  // GoogleTest calls absl::SetProgramUsageMessage() already.
  absl::SetProgramUsageMessage(kTestUsageMessage);
#endif
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
