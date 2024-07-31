//
// Copyright 2019 The Abseil Authors.
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

#include "absl/flags/parse.h"

#include <stdlib.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/internal/scoped_set_env.h"
#include "absl/flags/config.h"
#include "absl/flags/flag.h"
#include "absl/flags/internal/parse.h"
#include "absl/flags/internal/usage.h"
#include "absl/flags/reflection.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"

#ifdef _WIN32
#include <windows.h>
#endif

// Define 125 similar flags to test kMaxHints for flag suggestions.
#define FLAG_MULT(x) F3(x)
#define TEST_FLAG_HEADER FLAG_HEADER_

#define F(name) ABSL_FLAG(int, name, 0, "");

#define F1(name) \
  F(name##1);    \
  F(name##2);    \
  F(name##3);    \
  F(name##4);    \
  F(name##5);
/**/
#define F2(name) \
  F1(name##1);   \
  F1(name##2);   \
  F1(name##3);   \
  F1(name##4);   \
  F1(name##5);
/**/
#define F3(name) \
  F2(name##1);   \
  F2(name##2);   \
  F2(name##3);   \
  F2(name##4);   \
  F2(name##5);
/**/

FLAG_MULT(TEST_FLAG_HEADER)

namespace {

using absl::base_internal::ScopedSetEnv;

struct UDT {
  UDT() = default;
  UDT(const UDT&) = default;
  UDT& operator=(const UDT&) = default;
  UDT(int v) : value(v) {}  // NOLINT

  int value;
};

bool AbslParseFlag(absl::string_view in, UDT* udt, std::string* err) {
  if (in == "A") {
    udt->value = 1;
    return true;
  }
  if (in == "AAA") {
    udt->value = 10;
    return true;
  }

  *err = "Use values A, AAA instead";
  return false;
}
std::string AbslUnparseFlag(const UDT& udt) {
  return udt.value == 1 ? "A" : "AAA";
}

std::string GetTestTmpDirEnvVar(const char* const env_var_name) {
#ifdef _WIN32
  char buf[MAX_PATH];
  auto get_res = GetEnvironmentVariableA(env_var_name, buf, sizeof(buf));
  if (get_res >= sizeof(buf) || get_res == 0) {
    return "";
  }

  return std::string(buf, get_res);
#else
  const char* val = ::getenv(env_var_name);
  if (val == nullptr) {
    return "";
  }

  return val;
#endif
}

const std::string& GetTestTempDir() {
  static std::string* temp_dir_name = []() -> std::string* {
    std::string* res = new std::string(GetTestTmpDirEnvVar("TEST_TMPDIR"));

    if (res->empty()) {
      *res = GetTestTmpDirEnvVar("TMPDIR");
    }

    if (res->empty()) {
#ifdef _WIN32
      char temp_path_buffer[MAX_PATH];

      auto len = GetTempPathA(MAX_PATH, temp_path_buffer);
      if (len < MAX_PATH && len != 0) {
        std::string temp_dir_name = temp_path_buffer;
        if (!absl::EndsWith(temp_dir_name, "\\")) {
          temp_dir_name.push_back('\\');
        }
        absl::StrAppend(&temp_dir_name, "parse_test.", GetCurrentProcessId());
        if (CreateDirectoryA(temp_dir_name.c_str(), nullptr)) {
          *res = temp_dir_name;
        }
      }
#else
      char temp_dir_template[] = "/tmp/parse_test.XXXXXX";
      if (auto* unique_name = ::mkdtemp(temp_dir_template)) {
        *res = unique_name;
      }
#endif
    }

    if (res->empty()) {
      LOG(FATAL) << "Failed to make temporary directory for data files";
    }

#ifdef _WIN32
    *res += "\\";
#else
    *res += "/";
#endif

    return res;
  }();

  return *temp_dir_name;
}

struct FlagfileData {
  const absl::string_view file_name;
  const absl::Span<const char* const> file_lines;
};

// clang-format off
constexpr const char* const ff1_data[] = {
    "# comment    ",
    "  # comment  ",
    "",
    "     ",
    "--int_flag=-1",
    "  --string_flag=q2w2  ",
    "  ##   ",
    "  --double_flag=0.1",
    "--bool_flag=Y  "
};

constexpr const char* const ff2_data[] = {
    "# Setting legacy flag",
    "--legacy_int=1111",
    "--legacy_bool",
    "--nobool_flag",
    "--legacy_str=aqsw",
    "--int_flag=100",
    "   ## ============="
};
// clang-format on

// Builds flagfile flag in the flagfile_flag buffer and returns it. This
// function also creates a temporary flagfile based on FlagfileData input.
// We create a flagfile in a temporary directory with the name specified in
// FlagfileData and populate it with lines specified in FlagfileData. If $0 is
// referenced in any of the lines in FlagfileData they are replaced with
// temporary directory location. This way we can test inclusion of one flagfile
// from another flagfile.
const char* GetFlagfileFlag(const std::vector<FlagfileData>& ffd,
                            std::string& flagfile_flag) {
  flagfile_flag = "--flagfile=";
  absl::string_view separator;
  for (const auto& flagfile_data : ffd) {
    std::string flagfile_name =
        absl::StrCat(GetTestTempDir(), flagfile_data.file_name);

    std::ofstream flagfile_out(flagfile_name);
    for (auto line : flagfile_data.file_lines) {
      flagfile_out << absl::Substitute(line, GetTestTempDir()) << "\n";
    }

    absl::StrAppend(&flagfile_flag, separator, flagfile_name);
    separator = ",";
  }

  return flagfile_flag.c_str();
}

}  // namespace

ABSL_FLAG(int, int_flag, 1, "");
ABSL_FLAG(double, double_flag, 1.1, "");
ABSL_FLAG(std::string, string_flag, "a", "");
ABSL_FLAG(bool, bool_flag, false, "");
ABSL_FLAG(UDT, udt_flag, -1, "");
ABSL_RETIRED_FLAG(int, legacy_int, 1, "");
ABSL_RETIRED_FLAG(bool, legacy_bool, false, "");
ABSL_RETIRED_FLAG(std::string, legacy_str, "l", "");

namespace {

namespace flags = absl::flags_internal;
using testing::AllOf;
using testing::ElementsAreArray;
using testing::HasSubstr;

class ParseTest : public testing::Test {
 public:
  ~ParseTest() override { flags::SetFlagsHelpMode(flags::HelpMode::kNone); }

  void SetUp() override {
#if ABSL_FLAGS_STRIP_NAMES
    GTEST_SKIP() << "This test requires flag names to be present";
#endif
  }

 private:
  absl::FlagSaver flag_saver_;
};

// --------------------------------------------------------------------

template <int N>
flags::HelpMode InvokeParseAbslOnlyImpl(const char* (&in_argv)[N]) {
  std::vector<char*> positional_args;
  std::vector<absl::UnrecognizedFlag> unrecognized_flags;

  return flags::ParseAbseilFlagsOnlyImpl(N, const_cast<char**>(in_argv),
                                         positional_args, unrecognized_flags,
                                         flags::UsageFlagsAction::kHandleUsage);
}

// --------------------------------------------------------------------

template <int N>
void InvokeParseAbslOnly(const char* (&in_argv)[N]) {
  std::vector<char*> positional_args;
  std::vector<absl::UnrecognizedFlag> unrecognized_flags;

  absl::ParseAbseilFlagsOnly(2, const_cast<char**>(in_argv), positional_args,
                             unrecognized_flags);
}

// --------------------------------------------------------------------

template <int N>
std::vector<char*> InvokeParseCommandLineImpl(const char* (&in_argv)[N]) {
  return flags::ParseCommandLineImpl(
      N, const_cast<char**>(in_argv), flags::UsageFlagsAction::kHandleUsage,
      flags::OnUndefinedFlag::kAbortIfUndefined, std::cerr);
}

// --------------------------------------------------------------------

template <int N>
std::vector<char*> InvokeParse(const char* (&in_argv)[N]) {
  return absl::ParseCommandLine(N, const_cast<char**>(in_argv));
}

// --------------------------------------------------------------------

template <int N>
void TestParse(const char* (&in_argv)[N], int int_flag_value,
               double double_flag_val, absl::string_view string_flag_val,
               bool bool_flag_val, int exp_position_args = 0) {
  auto out_args = InvokeParse(in_argv);

  EXPECT_EQ(out_args.size(), 1 + exp_position_args);
  EXPECT_STREQ(out_args[0], "testbin");

  EXPECT_EQ(absl::GetFlag(FLAGS_int_flag), int_flag_value);
  EXPECT_NEAR(absl::GetFlag(FLAGS_double_flag), double_flag_val, 0.0001);
  EXPECT_EQ(absl::GetFlag(FLAGS_string_flag), string_flag_val);
  EXPECT_EQ(absl::GetFlag(FLAGS_bool_flag), bool_flag_val);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestEmptyArgv) {
  const char* in_argv[] = {"testbin"};

  auto out_args = InvokeParse(in_argv);

  EXPECT_EQ(out_args.size(), 1);
  EXPECT_STREQ(out_args[0], "testbin");
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestValidIntArg) {
  const char* in_args1[] = {
      "testbin",
      "--int_flag=10",
  };
  TestParse(in_args1, 10, 1.1, "a", false);

  const char* in_args2[] = {
      "testbin",
      "-int_flag=020",
  };
  TestParse(in_args2, 20, 1.1, "a", false);

  const char* in_args3[] = {
      "testbin",
      "--int_flag",
      "-30",
  };
  TestParse(in_args3, -30, 1.1, "a", false);

  const char* in_args4[] = {
      "testbin",
      "-int_flag",
      "0x21",
  };
  TestParse(in_args4, 33, 1.1, "a", false);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestValidDoubleArg) {
  const char* in_args1[] = {
      "testbin",
      "--double_flag=2.3",
  };
  TestParse(in_args1, 1, 2.3, "a", false);

  const char* in_args2[] = {
      "testbin",
      "--double_flag=0x1.2",
  };
  TestParse(in_args2, 1, 1.125, "a", false);

  const char* in_args3[] = {
      "testbin",
      "--double_flag",
      "99.7",
  };
  TestParse(in_args3, 1, 99.7, "a", false);

  const char* in_args4[] = {
      "testbin",
      "--double_flag",
      "0x20.1",
  };
  TestParse(in_args4, 1, 32.0625, "a", false);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestValidStringArg) {
  const char* in_args1[] = {
      "testbin",
      "--string_flag=aqswde",
  };
  TestParse(in_args1, 1, 1.1, "aqswde", false);

  const char* in_args2[] = {
      "testbin",
      "-string_flag=a=b=c",
  };
  TestParse(in_args2, 1, 1.1, "a=b=c", false);

  const char* in_args3[] = {
      "testbin",
      "--string_flag",
      "zaxscd",
  };
  TestParse(in_args3, 1, 1.1, "zaxscd", false);

  const char* in_args4[] = {
      "testbin",
      "-string_flag",
      "--int_flag",
  };
  TestParse(in_args4, 1, 1.1, "--int_flag", false);

  const char* in_args5[] = {
      "testbin",
      "--string_flag",
      "--no_a_flag=11",
  };
  TestParse(in_args5, 1, 1.1, "--no_a_flag=11", false);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestValidBoolArg) {
  const char* in_args1[] = {
      "testbin",
      "--bool_flag",
  };
  TestParse(in_args1, 1, 1.1, "a", true);

  const char* in_args2[] = {
      "testbin",
      "--nobool_flag",
  };
  TestParse(in_args2, 1, 1.1, "a", false);

  const char* in_args3[] = {
      "testbin",
      "--bool_flag=true",
  };
  TestParse(in_args3, 1, 1.1, "a", true);

  const char* in_args4[] = {
      "testbin",
      "-bool_flag=false",
  };
  TestParse(in_args4, 1, 1.1, "a", false);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestValidUDTArg) {
  const char* in_args1[] = {
      "testbin",
      "--udt_flag=A",
  };
  InvokeParse(in_args1);

  EXPECT_EQ(absl::GetFlag(FLAGS_udt_flag).value, 1);

  const char* in_args2[] = {"testbin", "--udt_flag", "AAA"};
  InvokeParse(in_args2);

  EXPECT_EQ(absl::GetFlag(FLAGS_udt_flag).value, 10);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestValidMultipleArg) {
  const char* in_args1[] = {
      "testbin",           "--bool_flag",       "--int_flag=2",
      "--double_flag=0.1", "--string_flag=asd",
  };
  TestParse(in_args1, 2, 0.1, "asd", true);

  const char* in_args2[] = {
      "testbin", "--string_flag=", "--nobool_flag", "--int_flag",
      "-011",    "--double_flag",  "-1e-2",
  };
  TestParse(in_args2, -11, -0.01, "", false);

  const char* in_args3[] = {
      "testbin",          "--int_flag",         "-0", "--string_flag", "\"\"",
      "--bool_flag=true", "--double_flag=1e18",
  };
  TestParse(in_args3, 0, 1e18, "\"\"", true);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestPositionalArgs) {
  const char* in_args1[] = {
      "testbin",
      "p1",
      "p2",
  };
  TestParse(in_args1, 1, 1.1, "a", false, 2);

  auto out_args1 = InvokeParse(in_args1);

  EXPECT_STREQ(out_args1[1], "p1");
  EXPECT_STREQ(out_args1[2], "p2");

  const char* in_args2[] = {
      "testbin",
      "--int_flag=2",
      "p1",
  };
  TestParse(in_args2, 2, 1.1, "a", false, 1);

  auto out_args2 = InvokeParse(in_args2);

  EXPECT_STREQ(out_args2[1], "p1");

  const char* in_args3[] = {"testbin", "p1",          "--int_flag=3",
                            "p2",      "--bool_flag", "true"};
  TestParse(in_args3, 3, 1.1, "a", true, 3);

  auto out_args3 = InvokeParse(in_args3);

  EXPECT_STREQ(out_args3[1], "p1");
  EXPECT_STREQ(out_args3[2], "p2");
  EXPECT_STREQ(out_args3[3], "true");

  const char* in_args4[] = {
      "testbin",
      "--",
      "p1",
      "p2",
  };
  TestParse(in_args4, 3, 1.1, "a", true, 2);

  auto out_args4 = InvokeParse(in_args4);

  EXPECT_STREQ(out_args4[1], "p1");
  EXPECT_STREQ(out_args4[2], "p2");

  const char* in_args5[] = {
      "testbin", "p1", "--int_flag=4", "--", "--bool_flag", "false", "p2",
  };
  TestParse(in_args5, 4, 1.1, "a", true, 4);

  auto out_args5 = InvokeParse(in_args5);

  EXPECT_STREQ(out_args5[1], "p1");
  EXPECT_STREQ(out_args5[2], "--bool_flag");
  EXPECT_STREQ(out_args5[3], "false");
  EXPECT_STREQ(out_args5[4], "p2");
}

// --------------------------------------------------------------------

using ParseDeathTest = ParseTest;

TEST_F(ParseDeathTest, TestUndefinedArg) {
  const char* in_args1[] = {
      "testbin",
      "--undefined_flag",
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args1),
                            "Unknown command line flag 'undefined_flag'");

  const char* in_args2[] = {
      "testbin",
      "--noprefixed_flag",
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args2),
                            "Unknown command line flag 'noprefixed_flag'");

  const char* in_args3[] = {
      "testbin",
      "--Int_flag=1",
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args3),
                            "Unknown command line flag 'Int_flag'");
}

// --------------------------------------------------------------------

TEST_F(ParseDeathTest, TestInvalidBoolFlagFormat) {
  const char* in_args1[] = {
      "testbin",
      "--bool_flag=",
  };
  EXPECT_DEATH_IF_SUPPORTED(
      InvokeParse(in_args1),
      "Missing the value after assignment for the boolean flag 'bool_flag'");

  const char* in_args2[] = {
      "testbin",
      "--nobool_flag=true",
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args2),
               "Negative form with assignment is not valid for the boolean "
               "flag 'bool_flag'");
}

// --------------------------------------------------------------------

TEST_F(ParseDeathTest, TestInvalidNonBoolFlagFormat) {
  const char* in_args1[] = {
      "testbin",
      "--nostring_flag",
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args1),
               "Negative form is not valid for the flag 'string_flag'");

  const char* in_args2[] = {
      "testbin",
      "--int_flag",
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args2),
               "Missing the value for the flag 'int_flag'");
}

// --------------------------------------------------------------------

TEST_F(ParseDeathTest, TestInvalidUDTFlagFormat) {
  const char* in_args1[] = {
      "testbin",
      "--udt_flag=1",
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args1),
               "Illegal value '1' specified for flag 'udt_flag'; Use values A, "
               "AAA instead");

  const char* in_args2[] = {
      "testbin",
      "--udt_flag",
      "AA",
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args2),
               "Illegal value 'AA' specified for flag 'udt_flag'; Use values "
               "A, AAA instead");
}

// --------------------------------------------------------------------

TEST_F(ParseDeathTest, TestFlagSuggestions) {
  const char* in_args1[] = {
      "testbin",
      "--legacy_boo",
  };
  EXPECT_DEATH_IF_SUPPORTED(
      InvokeParse(in_args1),
      "Unknown command line flag 'legacy_boo'. Did you mean: legacy_bool ?");

  const char* in_args2[] = {"testbin", "--foo", "--undefok=foo1"};
  EXPECT_DEATH_IF_SUPPORTED(
      InvokeParse(in_args2),
      "Unknown command line flag 'foo'. Did you mean: foo1 \\(undefok\\)?");

  const char* in_args3[] = {
      "testbin",
      "--nolegacy_ino",
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args3),
                            "Unknown command line flag 'nolegacy_ino'. Did "
                            "you mean: nolegacy_bool, legacy_int ?");
}

// --------------------------------------------------------------------

TEST_F(ParseTest, GetHints) {
  EXPECT_THAT(absl::flags_internal::GetMisspellingHints("legacy_boo"),
              testing::ContainerEq(std::vector<std::string>{"legacy_bool"}));
  EXPECT_THAT(absl::flags_internal::GetMisspellingHints("nolegacy_itn"),
              testing::ContainerEq(std::vector<std::string>{"legacy_int"}));
  EXPECT_THAT(absl::flags_internal::GetMisspellingHints("nolegacy_int1"),
              testing::ContainerEq(std::vector<std::string>{"legacy_int"}));
  EXPECT_THAT(absl::flags_internal::GetMisspellingHints("nolegacy_int"),
              testing::ContainerEq(std::vector<std::string>{"legacy_int"}));
  EXPECT_THAT(absl::flags_internal::GetMisspellingHints("nolegacy_ino"),
              testing::ContainerEq(
                  std::vector<std::string>{"nolegacy_bool", "legacy_int"}));
  EXPECT_THAT(
      absl::flags_internal::GetMisspellingHints("FLAG_HEADER_000").size(), 100);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestLegacyFlags) {
  const char* in_args1[] = {
      "testbin",
      "--legacy_int=11",
  };
  TestParse(in_args1, 1, 1.1, "a", false);

  const char* in_args2[] = {
      "testbin",
      "--legacy_bool",
  };
  TestParse(in_args2, 1, 1.1, "a", false);

  const char* in_args3[] = {
      "testbin",       "--legacy_int", "22",           "--int_flag=2",
      "--legacy_bool", "true",         "--legacy_str", "--string_flag=qwe",
  };
  TestParse(in_args3, 2, 1.1, "a", false, 1);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestSimpleValidFlagfile) {
  std::string flagfile_flag;

  const char* in_args1[] = {
      "testbin",
      GetFlagfileFlag({{"parse_test.ff1", absl::MakeConstSpan(ff1_data)}},
                      flagfile_flag),
  };
  TestParse(in_args1, -1, 0.1, "q2w2  ", true);

  const char* in_args2[] = {
      "testbin",
      GetFlagfileFlag({{"parse_test.ff2", absl::MakeConstSpan(ff2_data)}},
                      flagfile_flag),
  };
  TestParse(in_args2, 100, 0.1, "q2w2  ", false);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestValidMultiFlagfile) {
  std::string flagfile_flag;

  const char* in_args1[] = {
      "testbin",
      GetFlagfileFlag({{"parse_test.ff2", absl::MakeConstSpan(ff2_data)},
                       {"parse_test.ff1", absl::MakeConstSpan(ff1_data)}},
                      flagfile_flag),
  };
  TestParse(in_args1, -1, 0.1, "q2w2  ", true);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestFlagfileMixedWithRegularFlags) {
  std::string flagfile_flag;

  const char* in_args1[] = {
      "testbin", "--int_flag=3",
      GetFlagfileFlag({{"parse_test.ff1", absl::MakeConstSpan(ff1_data)}},
                      flagfile_flag),
      "-double_flag=0.2"};
  TestParse(in_args1, -1, 0.2, "q2w2  ", true);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestFlagfileInFlagfile) {
  std::string flagfile_flag;

  constexpr const char* const ff3_data[] = {
      "--flagfile=$0/parse_test.ff1",
      "--flagfile=$0/parse_test.ff2",
  };

  GetFlagfileFlag({{"parse_test.ff2", absl::MakeConstSpan(ff2_data)},
                   {"parse_test.ff1", absl::MakeConstSpan(ff1_data)}},
                      flagfile_flag);

  const char* in_args1[] = {
      "testbin",
      GetFlagfileFlag({{"parse_test.ff3", absl::MakeConstSpan(ff3_data)}},
                      flagfile_flag),
  };
  TestParse(in_args1, 100, 0.1, "q2w2  ", false);
}

// --------------------------------------------------------------------

TEST_F(ParseDeathTest, TestInvalidFlagfiles) {
  std::string flagfile_flag;

  constexpr const char* const ff4_data[] = {
    "--unknown_flag=10"
  };

  const char* in_args1[] = {
      "testbin",
      GetFlagfileFlag({{"parse_test.ff4",
                        absl::MakeConstSpan(ff4_data)}}, flagfile_flag),
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args1),
               "Unknown command line flag 'unknown_flag'");

  constexpr const char* const ff5_data[] = {
    "--int_flag 10",
  };

  const char* in_args2[] = {
      "testbin",
      GetFlagfileFlag({{"parse_test.ff5",
                        absl::MakeConstSpan(ff5_data)}}, flagfile_flag),
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args2),
               "Unknown command line flag 'int_flag 10'");

  constexpr const char* const ff6_data[] = {
      "--int_flag=10", "--", "arg1", "arg2", "arg3",
  };

  const char* in_args3[] = {
      "testbin",
      GetFlagfileFlag({{"parse_test.ff6", absl::MakeConstSpan(ff6_data)}},
                      flagfile_flag),
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args3),
               "Flagfile can't contain position arguments or --");

  const char* in_args4[] = {
      "testbin",
      "--flagfile=invalid_flag_file",
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args4),
                            "Can't open flagfile invalid_flag_file");

  constexpr const char* const ff7_data[] = {
      "--int_flag=10",
      "*bin*",
      "--str_flag=aqsw",
  };

  const char* in_args5[] = {
      "testbin",
      GetFlagfileFlag({{"parse_test.ff7", absl::MakeConstSpan(ff7_data)}},
                      flagfile_flag),
  };
  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args5),
               "Unexpected line in the flagfile .*: \\*bin\\*");
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestReadingRequiredFlagsFromEnv) {
  const char* in_args1[] = {"testbin",
                            "--fromenv=int_flag,bool_flag,string_flag"};

  ScopedSetEnv set_int_flag("FLAGS_int_flag", "33");
  ScopedSetEnv set_bool_flag("FLAGS_bool_flag", "True");
  ScopedSetEnv set_string_flag("FLAGS_string_flag", "AQ12");

  TestParse(in_args1, 33, 1.1, "AQ12", true);
}

// --------------------------------------------------------------------

TEST_F(ParseDeathTest, TestReadingUnsetRequiredFlagsFromEnv) {
  const char* in_args1[] = {"testbin", "--fromenv=int_flag"};

  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args1),
               "FLAGS_int_flag not found in environment");
}

// --------------------------------------------------------------------

TEST_F(ParseDeathTest, TestRecursiveFlagsFromEnv) {
  const char* in_args1[] = {"testbin", "--fromenv=tryfromenv"};

  ScopedSetEnv set_tryfromenv("FLAGS_tryfromenv", "int_flag");

  EXPECT_DEATH_IF_SUPPORTED(InvokeParse(in_args1),
                            "Infinite recursion on flag tryfromenv");
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestReadingOptionalFlagsFromEnv) {
  const char* in_args1[] = {
      "testbin", "--tryfromenv=int_flag,bool_flag,string_flag,other_flag"};

  ScopedSetEnv set_int_flag("FLAGS_int_flag", "17");
  ScopedSetEnv set_bool_flag("FLAGS_bool_flag", "Y");

  TestParse(in_args1, 17, 1.1, "a", true);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestReadingFlagsFromEnvMoxedWithRegularFlags) {
  const char* in_args1[] = {
      "testbin",
      "--bool_flag=T",
      "--tryfromenv=int_flag,bool_flag",
      "--int_flag=-21",
  };

  ScopedSetEnv set_int_flag("FLAGS_int_flag", "-15");
  ScopedSetEnv set_bool_flag("FLAGS_bool_flag", "F");

  TestParse(in_args1, -21, 1.1, "a", false);
}

// --------------------------------------------------------------------

TEST_F(ParseDeathTest, TestSimpleHelpFlagHandling) {
  const char* in_args1[] = {
      "testbin",
      "--help",
  };

  EXPECT_EQ(InvokeParseAbslOnlyImpl(in_args1), flags::HelpMode::kImportant);
  EXPECT_EXIT(InvokeParse(in_args1), testing::ExitedWithCode(1), "");

  const char* in_args2[] = {
      "testbin",
      "--help",
      "--int_flag=3",
  };

  EXPECT_EQ(InvokeParseAbslOnlyImpl(in_args2), flags::HelpMode::kImportant);
  EXPECT_EQ(absl::GetFlag(FLAGS_int_flag), 3);

  const char* in_args3[] = {"testbin", "--help", "some_positional_arg"};

  EXPECT_EQ(InvokeParseAbslOnlyImpl(in_args3), flags::HelpMode::kImportant);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestSubstringHelpFlagHandling) {
  const char* in_args1[] = {
      "testbin",
      "--help=abcd",
  };

  EXPECT_EQ(InvokeParseAbslOnlyImpl(in_args1), flags::HelpMode::kMatch);
  EXPECT_EQ(flags::GetFlagsHelpMatchSubstr(), "abcd");
}

// --------------------------------------------------------------------

TEST_F(ParseDeathTest, TestVersionHandling) {
  const char* in_args1[] = {
      "testbin",
      "--version",
  };

  EXPECT_EQ(InvokeParseAbslOnlyImpl(in_args1), flags::HelpMode::kVersion);
}

// --------------------------------------------------------------------

TEST_F(ParseTest, TestCheckArgsHandling) {
  const char* in_args1[] = {"testbin", "--only_check_args", "--int_flag=211"};

  EXPECT_EQ(InvokeParseAbslOnlyImpl(in_args1), flags::HelpMode::kOnlyCheckArgs);
  EXPECT_EXIT(InvokeParseAbslOnly(in_args1), testing::ExitedWithCode(0), "");
  EXPECT_EXIT(InvokeParse(in_args1), testing::ExitedWithCode(0), "");

  const char* in_args2[] = {"testbin", "--only_check_args", "--unknown_flag=a"};

  EXPECT_EQ(InvokeParseAbslOnlyImpl(in_args2), flags::HelpMode::kOnlyCheckArgs);
  EXPECT_EXIT(InvokeParseAbslOnly(in_args2), testing::ExitedWithCode(0), "");
  EXPECT_EXIT(InvokeParse(in_args2), testing::ExitedWithCode(1), "");
}

// --------------------------------------------------------------------

TEST_F(ParseTest, WasPresentOnCommandLine) {
  const char* in_args1[] = {
      "testbin",        "arg1", "--bool_flag",
      "--int_flag=211", "arg2", "--double_flag=1.1",
      "--string_flag",  "asd",  "--",
      "--some_flag",    "arg4",
  };

  InvokeParse(in_args1);

  EXPECT_TRUE(flags::WasPresentOnCommandLine("bool_flag"));
  EXPECT_TRUE(flags::WasPresentOnCommandLine("int_flag"));
  EXPECT_TRUE(flags::WasPresentOnCommandLine("double_flag"));
  EXPECT_TRUE(flags::WasPresentOnCommandLine("string_flag"));
  EXPECT_FALSE(flags::WasPresentOnCommandLine("some_flag"));
  EXPECT_FALSE(flags::WasPresentOnCommandLine("another_flag"));
}

// --------------------------------------------------------------------

TEST_F(ParseTest, ParseAbseilFlagsOnlySuccess) {
  const char* in_args[] = {
      "testbin",
      "arg1",
      "--bool_flag",
      "--int_flag=211",
      "arg2",
      "--double_flag=1.1",
      "--undef_flag1",
      "--undef_flag2=123",
      "--string_flag",
      "asd",
      "--",
      "--some_flag",
      "arg4",
  };

  std::vector<char*> positional_args;
  std::vector<absl::UnrecognizedFlag> unrecognized_flags;

  absl::ParseAbseilFlagsOnly(13, const_cast<char**>(in_args), positional_args,
                             unrecognized_flags);
  EXPECT_THAT(positional_args,
              ElementsAreArray(
                  {absl::string_view("testbin"), absl::string_view("arg1"),
                   absl::string_view("arg2"), absl::string_view("--some_flag"),
                   absl::string_view("arg4")}));
  EXPECT_THAT(unrecognized_flags,
              ElementsAreArray(
                  {absl::UnrecognizedFlag(absl::UnrecognizedFlag::kFromArgv,
                                          "undef_flag1"),
                   absl::UnrecognizedFlag(absl::UnrecognizedFlag::kFromArgv,
                                          "undef_flag2")}));
}

// --------------------------------------------------------------------

TEST_F(ParseDeathTest, ParseAbseilFlagsOnlyFailure) {
  const char* in_args[] = {
      "testbin",
      "--int_flag=21.1",
  };

  EXPECT_DEATH_IF_SUPPORTED(
      InvokeParseAbslOnly(in_args),
      "Illegal value '21.1' specified for flag 'int_flag'");
}

// --------------------------------------------------------------------

TEST_F(ParseTest, UndefOkFlagsAreIgnored) {
  const char* in_args[] = {
      "testbin",           "--undef_flag1",
      "--undef_flag2=123", "--undefok=undef_flag2",
      "--undef_flag3",     "value",
  };

  std::vector<char*> positional_args;
  std::vector<absl::UnrecognizedFlag> unrecognized_flags;

  absl::ParseAbseilFlagsOnly(6, const_cast<char**>(in_args), positional_args,
                             unrecognized_flags);
  EXPECT_THAT(positional_args, ElementsAreArray({absl::string_view("testbin"),
                                                 absl::string_view("value")}));
  EXPECT_THAT(unrecognized_flags,
              ElementsAreArray(
                  {absl::UnrecognizedFlag(absl::UnrecognizedFlag::kFromArgv,
                                          "undef_flag1"),
                   absl::UnrecognizedFlag(absl::UnrecognizedFlag::kFromArgv,
                                          "undef_flag3")}));
}

// --------------------------------------------------------------------

TEST_F(ParseTest, AllUndefOkFlagsAreIgnored) {
  const char* in_args[] = {
      "testbin",
      "--undef_flag1",
      "--undef_flag2=123",
      "--undefok=undef_flag2,undef_flag1,undef_flag3",
      "--undef_flag3",
      "value",
      "--",
      "--undef_flag4",
  };

  std::vector<char*> positional_args;
  std::vector<absl::UnrecognizedFlag> unrecognized_flags;

  absl::ParseAbseilFlagsOnly(8, const_cast<char**>(in_args), positional_args,
                             unrecognized_flags);
  EXPECT_THAT(positional_args,
              ElementsAreArray({absl::string_view("testbin"),
                                absl::string_view("value"),
                                absl::string_view("--undef_flag4")}));
  EXPECT_THAT(unrecognized_flags, testing::IsEmpty());
}

// --------------------------------------------------------------------

TEST_F(ParseDeathTest, ExitOnUnrecognizedFlagPrintsHelp) {
  const char* in_args[] = {
      "testbin",
      "--undef_flag1",
      "--help=int_flag",
  };

  EXPECT_EXIT(InvokeParseCommandLineImpl(in_args), testing::ExitedWithCode(1),
              AllOf(HasSubstr("Unknown command line flag 'undef_flag1'"),
                    HasSubstr("Try --helpfull to get a list of all flags")));
}

// --------------------------------------------------------------------

}  // namespace
