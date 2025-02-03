// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include "src/flags/flags-impl.h"
#include "src/flags/flags.h"
#include "src/init/v8.h"
#include "test/unittests/fuzztest.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8::internal {

class FlagDefinitionsTest : public ::testing::Test {
 public:
  void SetUp() override { FlagList::EnforceFlagImplications(); }
};

void TestDefault() {
  CHECK(v8_flags.testing_bool_flag);
  CHECK_EQ(13, v8_flags.testing_int_flag);
  CHECK_EQ(2.5, v8_flags.testing_float_flag);
  CHECK_EQ(0, strcmp(v8_flags.testing_string_flag, "Hello, world!"));
}

// This test must be executed first!
TEST_F(FlagDefinitionsTest, Default) { TestDefault(); }

TEST_F(FlagDefinitionsTest, Flags1) { FlagList::PrintHelp(); }

TEST_F(FlagDefinitionsTest, Flags2) {
  int argc = 8;
  const char* argv[] = {"Test2",
                        "-notesting-bool-flag",
                        "--notesting-maybe-bool-flag",
                        "notaflag",
                        "--testing_int_flag=77",
                        "-testing_float_flag=.25",
                        "--testing_string_flag",
                        "no way!"};
  CHECK_EQ(0, FlagList::SetFlagsFromCommandLine(&argc, const_cast<char**>(argv),
                                                false));
  CHECK_EQ(8, argc);
  CHECK(!v8_flags.testing_bool_flag);
  CHECK(v8_flags.testing_maybe_bool_flag.value().has_value());
  CHECK(!v8_flags.testing_maybe_bool_flag.value().value());
  CHECK_EQ(77, v8_flags.testing_int_flag);
  CHECK_EQ(.25, v8_flags.testing_float_flag);
  CHECK_EQ(0, strcmp(v8_flags.testing_string_flag, "no way!"));
}

TEST_F(FlagDefinitionsTest, Flags2b) {
  const char* str =
      " -notesting-bool-flag notaflag   --testing_int_flag=77 "
      "-notesting-maybe-bool-flag   "
      "-testing_float_flag=.25  "
      "--testing_string_flag   no_way!  ";
  CHECK_EQ(0, FlagList::SetFlagsFromString(str, strlen(str)));
  CHECK(!v8_flags.testing_bool_flag);
  CHECK(v8_flags.testing_maybe_bool_flag.value().has_value());
  CHECK(!v8_flags.testing_maybe_bool_flag.value().value());
  CHECK_EQ(77, v8_flags.testing_int_flag);
  CHECK_EQ(.25, v8_flags.testing_float_flag);
  CHECK_EQ(0, strcmp(v8_flags.testing_string_flag, "no_way!"));
}

TEST_F(FlagDefinitionsTest, Flags3) {
  int argc = 9;
  const char* argv[] = {"Test3",
                        "--testing_bool_flag",
                        "--testing-maybe-bool-flag",
                        "notaflag",
                        "--testing_int_flag",
                        "-666",
                        "--testing_float_flag",
                        "-12E10",
                        "-testing-string-flag=foo-bar"};
  CHECK_EQ(0, FlagList::SetFlagsFromCommandLine(&argc, const_cast<char**>(argv),
                                                true));
  CHECK_EQ(2, argc);
  CHECK(v8_flags.testing_bool_flag);
  CHECK(v8_flags.testing_maybe_bool_flag.value().has_value());
  CHECK(v8_flags.testing_maybe_bool_flag.value().value());
  CHECK_EQ(-666, v8_flags.testing_int_flag);
  CHECK_EQ(-12E10, v8_flags.testing_float_flag);
  CHECK_EQ(0, strcmp(v8_flags.testing_string_flag, "foo-bar"));
}

TEST_F(FlagDefinitionsTest, Flags3b) {
  const char* str =
      "--testing_bool_flag --testing-maybe-bool-flag notaflag "
      "--testing_int_flag -666 "
      "--testing_float_flag -12E10 "
      "-testing-string-flag=foo-bar";
  CHECK_EQ(0, FlagList::SetFlagsFromString(str, strlen(str)));
  CHECK(v8_flags.testing_bool_flag);
  CHECK(v8_flags.testing_maybe_bool_flag.value().has_value());
  CHECK(v8_flags.testing_maybe_bool_flag.value().value());
  CHECK_EQ(-666, v8_flags.testing_int_flag);
  CHECK_EQ(-12E10, v8_flags.testing_float_flag);
  CHECK_EQ(0, strcmp(v8_flags.testing_string_flag, "foo-bar"));
}

TEST_F(FlagDefinitionsTest, Flags4) {
  int argc = 3;
  const char* argv[] = {"Test4", "--testing_bool_flag", "--foo"};
  CHECK_EQ(0, FlagList::SetFlagsFromCommandLine(&argc, const_cast<char**>(argv),
                                                true));
  CHECK_EQ(2, argc);
  CHECK(!v8_flags.testing_maybe_bool_flag.value().has_value());
}

TEST_F(FlagDefinitionsTest, Flags4b) {
  const char* str = "--testing_bool_flag --foo";
  CHECK_EQ(2, FlagList::SetFlagsFromString(str, strlen(str)));
  CHECK(!v8_flags.testing_maybe_bool_flag.value().has_value());
}

TEST_F(FlagDefinitionsTest, Flags5) {
  int argc = 2;
  const char* argv[] = {"Test5", "--testing_int_flag=\"foobar\""};
  CHECK_EQ(1, FlagList::SetFlagsFromCommandLine(&argc, const_cast<char**>(argv),
                                                true));
  CHECK_EQ(2, argc);
}

TEST_F(FlagDefinitionsTest, Flags5b) {
  const char* str = "                     --testing_int_flag=\"foobar\"";
  CHECK_EQ(1, FlagList::SetFlagsFromString(str, strlen(str)));
}

TEST_F(FlagDefinitionsTest, Flags6) {
  int argc = 4;
  const char* argv[] = {"Test5", "--testing-int-flag", "0",
                        "--testing_float_flag"};
  CHECK_EQ(3, FlagList::SetFlagsFromCommandLine(&argc, const_cast<char**>(argv),
                                                true));
  CHECK_EQ(2, argc);
}

TEST_F(FlagDefinitionsTest, Flags6b) {
  const char* str = "       --testing-int-flag 0      --testing_float_flag    ";
  CHECK_EQ(3, FlagList::SetFlagsFromString(str, strlen(str)));
}

TEST_F(FlagDefinitionsTest, FlagsRemoveIncomplete) {
  // Test that processed command line arguments are removed, even
  // if the list of arguments ends unexpectedly.
  int argc = 3;
  const char* argv[] = {"", "--testing-bool-flag", "--expose-gc-as"};
  CHECK_EQ(2, FlagList::SetFlagsFromCommandLine(&argc, const_cast<char**>(argv),
                                                true));
  CHECK(argv[1]);
  CHECK_EQ(2, argc);
}

TEST_F(FlagDefinitionsTest, FlagsJitlessImplications) {
  if (v8_flags.jitless) {
    // Double-check implications work as expected. Our implication system is
    // fairly primitive and can break easily depending on the implication
    // definition order in flag-definitions.h.
    CHECK(!v8_flags.turbofan);
    CHECK(!v8_flags.maglev);
    CHECK(!v8_flags.sparkplug);
#if V8_ENABLE_WEBASSEMBLY
    CHECK(!v8_flags.validate_asm);
    CHECK(!v8_flags.asm_wasm_lazy_compilation);
    CHECK(!v8_flags.wasm_lazy_compilation);
#endif  // V8_ENABLE_WEBASSEMBLY
  }
}

TEST_F(FlagDefinitionsTest, FlagsDisableOptimizingCompilersImplications) {
  if (v8_flags.disable_optimizing_compilers) {
    // Double-check implications work as expected. Our implication system is
    // fairly primitive and can break easily depending on the implication
    // definition order in flag-definitions.h.
    CHECK(!v8_flags.turbofan);
    CHECK(!v8_flags.turboshaft);
    CHECK(!v8_flags.maglev);
#ifdef V8_ENABLE_WEBASSEMBLY
    CHECK(!v8_flags.wasm_tier_up);
    CHECK(!v8_flags.wasm_dynamic_tiering);
    CHECK(!v8_flags.validate_asm);
#endif  // V8_ENABLE_WEBASSEMBLY
  }
}

TEST_F(FlagDefinitionsTest, FreezeFlags) {
  // Before freezing, we can arbitrarily change values.
  CHECK_EQ(13, v8_flags.testing_int_flag);  // Initial (default) value.
  v8_flags.testing_int_flag = 27;
  CHECK_EQ(27, v8_flags.testing_int_flag);

  // Get a direct pointer to the flag storage.
  static_assert(sizeof(v8_flags.testing_int_flag) == sizeof(int));
  int* direct_testing_int_ptr =
      reinterpret_cast<int*>(&v8_flags.testing_int_flag);
  CHECK_EQ(27, *direct_testing_int_ptr);
  *direct_testing_int_ptr = 42;
  CHECK_EQ(42, v8_flags.testing_int_flag);

  // Now freeze flags. Accesses via the API and via the direct pointer should
  // both crash.
  FlagList::FreezeFlags();
  // Accessing via the API fails with a CHECK.
  ASSERT_DEATH_IF_SUPPORTED(v8_flags.testing_int_flag = 41,
                            "Check failed: !IsFrozen\\(\\)");
  // Writing to the memory directly results in a segfault.
  ASSERT_DEATH_IF_SUPPORTED(*direct_testing_int_ptr = 41, "");
  // We can still read the old value.
  CHECK_EQ(42, v8_flags.testing_int_flag);
  CHECK_EQ(42, *direct_testing_int_ptr);
}

// Stress implications after setting a flag. We only set one flag, as multiple
// might just lead to known flag contradictions.
void StressFlagImplications(const std::string& s1) {
  int result = FlagList::SetFlagsFromString(s1.c_str(), s1.length());
  // Only process implications if a flag was set successfully (which happens
  // only in a small portion of fuzz runs).
  if (result == 0) FlagList::EnforceFlagImplications();
  // Ensure a clean state in each iteration.
  for (Flag& flag : Flags()) {
    if (!flag.IsReadOnly()) flag.Reset();
  }
}

V8_FUZZ_TEST(FlagDefinitionsFuzzTest, StressFlagImplications)
    .WithDomains(fuzztest::InRegexp("^--(\\w|\\-){1,50}(=\\w{1,5})?$"));

struct FlagAndName {
  FlagValue<bool>* value;
  const char* name;
  const char* test_name;
};

class ExperimentalFlagImplicationTest
    : public ::testing::TestWithParam<FlagAndName> {};

// Check that no experimental feature is enabled; this is executed for different
// {FlagAndName} combinations.
TEST_P(ExperimentalFlagImplicationTest, TestExperimentalNotEnabled) {
  FlagList::EnforceFlagImplications();
  // --experimental should be disabled by default. Note that unittests do not
  // get executed in variants.
  CHECK(!v8_flags.experimental);
  auto [flag_value, flag_name, test_name] = GetParam();
  CHECK_EQ(flag_value == nullptr, flag_name == nullptr);

  if (flag_name) {
    int argc = 2;
    const char* argv[] = {"", flag_name};
    CHECK_EQ(0, FlagList::SetFlagsFromCommandLine(
                    &argc, const_cast<char**>(argv), false));
    CHECK(*flag_value);
  }

  // Always enforce implications before checking if --experimental is set.
  FlagList::EnforceFlagImplications();

  if (v8_flags.experimental) {
    if (flag_value == nullptr) {
      FATAL("--experimental is enabled by default");
    } else {
      FATAL("--experimental is implied by %s", flag_name);
    }
  }
}

std::string FlagNameToTestName(::testing::TestParamInfo<FlagAndName> info) {
  return info.param.test_name;
}

// MVSC does not like an "#if" inside of a macro, hence define this list outside
// of INSTANTIATE_TEST_SUITE_P.
auto GetFlagImplicationTestVariants() {
  return ::testing::Values(
      FlagAndName{nullptr, nullptr, "Default"},
      FlagAndName{&v8_flags.future, "--future", "Future"},
#if V8_ENABLE_WEBASSEMBLY
      FlagAndName{&v8_flags.wasm_staging, "--wasm-staging", "WasmStaging"},
#endif  // V8_ENABLE_WEBASSEMBLY
      FlagAndName{&v8_flags.harmony, "--harmony", "Harmony"});
}

INSTANTIATE_TEST_SUITE_P(ExperimentalFlagImplication,
                         ExperimentalFlagImplicationTest,
                         GetFlagImplicationTestVariants(), FlagNameToTestName);

TEST(FlagContradictionsTest, ResolvesContradictions) {
#ifdef V8_ENABLE_MAGLEV
  int argc = 4;
  const char* argv[] = {"Test", "--fuzzing", "--stress-maglev", "--jitless"};
  FlagList::SetFlagsFromCommandLine(&argc, const_cast<char**>(argv), false);
  CHECK(v8_flags.fuzzing);
  CHECK(v8_flags.jitless);
  CHECK(v8_flags.stress_maglev);
  FlagList::ResolveContradictionsWhenFuzzing();
  FlagList::EnforceFlagImplications();
  CHECK(v8_flags.fuzzing);
  CHECK(!v8_flags.jitless);
  CHECK(v8_flags.stress_maglev);
#endif
}

const char* smallerValues[] = {"", "--a", "--a-b-c", "--a_b_c"};
const char* largerValues[] = {"--a-c-b", "--a_c_b",   "--a_b_d",
                              "--a-b-d", "--a_b_c_d", "--a-b-c-d"};

TEST(FlagHelpersTest, CompareDifferentFlags) {
  TRACED_FOREACH(const char*, smaller, smallerValues) {
    TRACED_FOREACH(const char*, larger, largerValues) {
      CHECK_EQ(-1, FlagHelpers::FlagNamesCmp(smaller, larger));
      CHECK_EQ(1, FlagHelpers::FlagNamesCmp(larger, smaller));
    }
  }
}

void CheckEqualFlags(const char* f1, const char* f2) {
  CHECK(FlagHelpers::EqualNames(f1, f2));
  CHECK(FlagHelpers::EqualNames(f2, f1));
}

TEST(FlagHelpersTest, CompareSameFlags) {
  CheckEqualFlags("", "");
  CheckEqualFlags("--a", "--a");
  CheckEqualFlags("--a-b-c", "--a_b_c");
  CheckEqualFlags("--a-b-c", "--a-b-c");
}

void CheckFlagInvariants(const std::string& s1, const std::string& s2) {
  const char* f1 = s1.c_str();
  const char* f2 = s2.c_str();
  CHECK_EQ(-FlagHelpers::FlagNamesCmp(f1, f2),
           FlagHelpers::FlagNamesCmp(f2, f1));
  CHECK(FlagHelpers::EqualNames(f1, f1));
  CHECK(FlagHelpers::EqualNames(f2, f2));
}

V8_FUZZ_TEST(FlagHelpersFuzzTest, CheckFlagInvariants)
    .WithDomains(fuzztest::AsciiString(), fuzztest::AsciiString());

TEST(FlagInternalsTest, LookupFlagByName) {
  CHECK_EQ(0, strcmp("trace_opt", FindFlagByName("trace_opt")->name()));
  CHECK_EQ(0, strcmp("trace_opt", FindFlagByName("trace-opt")->name()));
  CHECK_EQ(nullptr, FindFlagByName("trace?opt"));
}

TEST(FlagInternalsTest, LookupAllFlagsByName) {
  for (const Flag& flag : Flags()) {
    CHECK_EQ(&flag, FindFlagByName(flag.name()));
  }
}

TEST(FlagInternalsTest, LookupAllImplicationFlagsByName) {
  for (const Flag& flag : Flags()) {
    CHECK_EQ(&flag, FindImplicationFlagByName(flag.name()));
    auto name_with_suffix = std::string(flag.name()) + " < 3";
    CHECK_EQ(&flag, FindImplicationFlagByName(name_with_suffix.c_str()));
  }
}

}  // namespace v8::internal
