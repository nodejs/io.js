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

#include "absl/flags/usage_config.h"

#include <string>

#include "gtest/gtest.h"
#include "absl/flags/internal/path_util.h"
#include "absl/flags/internal/program_name.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace {

class FlagsUsageConfigTest : public testing::Test {
 protected:
  void SetUp() override {
    // Install Default config for the use on this unit test.
    // Binary may install a custom config before tests are run.
    absl::FlagsUsageConfig default_config;
    absl::SetFlagsUsageConfig(default_config);
  }
};

namespace flags = absl::flags_internal;

bool TstContainsHelpshortFlags(absl::string_view f) {
  return absl::StartsWith(flags::Basename(f), "progname.");
}

bool TstContainsHelppackageFlags(absl::string_view f) {
  return absl::EndsWith(flags::Package(f), "aaa/");
}

bool TstContainsHelpFlags(absl::string_view f) {
  return absl::EndsWith(flags::Package(f), "zzz/");
}

std::string TstVersionString() { return "program 1.0.0"; }

std::string TstNormalizeFilename(absl::string_view filename) {
  return std::string(filename.substr(2));
}

void TstReportUsageMessage(absl::string_view msg) {}

// --------------------------------------------------------------------

TEST_F(FlagsUsageConfigTest, TestGetSetFlagsUsageConfig) {
  EXPECT_TRUE(flags::GetUsageConfig().contains_helpshort_flags);
  EXPECT_TRUE(flags::GetUsageConfig().contains_help_flags);
  EXPECT_TRUE(flags::GetUsageConfig().contains_helppackage_flags);
  EXPECT_TRUE(flags::GetUsageConfig().version_string);
  EXPECT_TRUE(flags::GetUsageConfig().normalize_filename);

  absl::FlagsUsageConfig empty_config;
  empty_config.contains_helpshort_flags = &TstContainsHelpshortFlags;
  empty_config.contains_help_flags = &TstContainsHelpFlags;
  empty_config.contains_helppackage_flags = &TstContainsHelppackageFlags;
  empty_config.version_string = &TstVersionString;
  empty_config.normalize_filename = &TstNormalizeFilename;
  absl::SetFlagsUsageConfig(empty_config);

  EXPECT_TRUE(flags::GetUsageConfig().contains_helpshort_flags);
  EXPECT_TRUE(flags::GetUsageConfig().contains_help_flags);
  EXPECT_TRUE(flags::GetUsageConfig().contains_helppackage_flags);
  EXPECT_TRUE(flags::GetUsageConfig().version_string);
  EXPECT_TRUE(flags::GetUsageConfig().normalize_filename);
}

// --------------------------------------------------------------------

TEST_F(FlagsUsageConfigTest, TestContainsHelpshortFlags) {
#if defined(_WIN32)
  flags::SetProgramInvocationName("usage_config_test.exe");
#else
  flags::SetProgramInvocationName("usage_config_test");
#endif

  auto config = flags::GetUsageConfig();
  EXPECT_TRUE(config.contains_helpshort_flags("adir/cd/usage_config_test.cc"));
  EXPECT_TRUE(
      config.contains_helpshort_flags("aaaa/usage_config_test-main.cc"));
  EXPECT_TRUE(config.contains_helpshort_flags("abc/usage_config_test_main.cc"));
  EXPECT_FALSE(config.contains_helpshort_flags("usage_config_main.cc"));

  absl::FlagsUsageConfig empty_config;
  empty_config.contains_helpshort_flags = &TstContainsHelpshortFlags;
  absl::SetFlagsUsageConfig(empty_config);

  EXPECT_TRUE(
      flags::GetUsageConfig().contains_helpshort_flags("aaa/progname.cpp"));
  EXPECT_FALSE(
      flags::GetUsageConfig().contains_helpshort_flags("aaa/progmane.cpp"));
}

// --------------------------------------------------------------------

TEST_F(FlagsUsageConfigTest, TestContainsHelpFlags) {
  flags::SetProgramInvocationName("usage_config_test");

  auto config = flags::GetUsageConfig();
  EXPECT_TRUE(config.contains_help_flags("zzz/usage_config_test.cc"));
  EXPECT_TRUE(
      config.contains_help_flags("bdir/a/zzz/usage_config_test-main.cc"));
  EXPECT_TRUE(
      config.contains_help_flags("//aqse/zzz/usage_config_test_main.cc"));
  EXPECT_FALSE(config.contains_help_flags("zzz/aa/usage_config_main.cc"));

  absl::FlagsUsageConfig empty_config;
  empty_config.contains_help_flags = &TstContainsHelpFlags;
  absl::SetFlagsUsageConfig(empty_config);

  EXPECT_TRUE(flags::GetUsageConfig().contains_help_flags("zzz/main-body.c"));
  EXPECT_FALSE(
      flags::GetUsageConfig().contains_help_flags("zzz/dir/main-body.c"));
}

// --------------------------------------------------------------------

TEST_F(FlagsUsageConfigTest, TestContainsHelppackageFlags) {
  flags::SetProgramInvocationName("usage_config_test");

  auto config = flags::GetUsageConfig();
  EXPECT_TRUE(config.contains_helppackage_flags("aaa/usage_config_test.cc"));
  EXPECT_TRUE(
      config.contains_helppackage_flags("bbdir/aaa/usage_config_test-main.cc"));
  EXPECT_TRUE(config.contains_helppackage_flags(
      "//aqswde/aaa/usage_config_test_main.cc"));
  EXPECT_FALSE(config.contains_helppackage_flags("aadir/usage_config_main.cc"));

  absl::FlagsUsageConfig empty_config;
  empty_config.contains_helppackage_flags = &TstContainsHelppackageFlags;
  absl::SetFlagsUsageConfig(empty_config);

  EXPECT_TRUE(
      flags::GetUsageConfig().contains_helppackage_flags("aaa/main-body.c"));
  EXPECT_FALSE(
      flags::GetUsageConfig().contains_helppackage_flags("aadir/main-body.c"));
}

// --------------------------------------------------------------------

TEST_F(FlagsUsageConfigTest, TestVersionString) {
  flags::SetProgramInvocationName("usage_config_test");

#ifdef NDEBUG
  std::string expected_output = "usage_config_test\n";
#else
  std::string expected_output =
      "usage_config_test\nDebug build (NDEBUG not #defined)\n";
#endif

  EXPECT_EQ(flags::GetUsageConfig().version_string(), expected_output);

  absl::FlagsUsageConfig empty_config;
  empty_config.version_string = &TstVersionString;
  absl::SetFlagsUsageConfig(empty_config);

  EXPECT_EQ(flags::GetUsageConfig().version_string(), "program 1.0.0");
}

// --------------------------------------------------------------------

TEST_F(FlagsUsageConfigTest, TestNormalizeFilename) {
  // This tests the default implementation.
  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("a/a.cc"), "a/a.cc");
  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("/a/a.cc"), "a/a.cc");
  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("///a/a.cc"), "a/a.cc");
  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("/"), "");

  // This tests that the custom implementation is called.
  absl::FlagsUsageConfig empty_config;
  empty_config.normalize_filename = &TstNormalizeFilename;
  absl::SetFlagsUsageConfig(empty_config);

  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("a/a.cc"), "a.cc");
  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("aaa/a.cc"), "a/a.cc");

  // This tests that the default implementation is called.
  empty_config.normalize_filename = nullptr;
  absl::SetFlagsUsageConfig(empty_config);

  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("a/a.cc"), "a/a.cc");
  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("/a/a.cc"), "a/a.cc");
  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("///a/a.cc"), "a/a.cc");
  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("\\a\\a.cc"), "a\\a.cc");
  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("//"), "");
  EXPECT_EQ(flags::GetUsageConfig().normalize_filename("\\\\"), "");
}

}  // namespace
