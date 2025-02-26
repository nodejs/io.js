// Copyright 2021 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HWY_TESTS_HWY_GTEST_H_
#define HWY_TESTS_HWY_GTEST_H_

// Adapter/replacement for GUnit to run tests for all targets.

#include "hwy/base.h"

// Allow opting out of GUnit.
#ifndef HWY_TEST_STANDALONE
// GUnit and its dependencies no longer support MSVC.
#if HWY_COMPILER_MSVC
#define HWY_TEST_STANDALONE 1
#else
#define HWY_TEST_STANDALONE 0
#endif  // HWY_COMPILER_MSVC
#endif  // HWY_TEST_STANDALONE

#include <stdint.h>
#if HWY_TEST_STANDALONE
#include <stdlib.h>
#include <string.h>
#endif

#include <string>
#include <tuple>

#if !HWY_TEST_STANDALONE
#include "gtest/gtest.h"  // IWYU pragma: export
#endif
#include "hwy/detect_targets.h"
#include "hwy/targets.h"

namespace hwy {

#if !HWY_TEST_STANDALONE

// googletest before 1.10 didn't define INSTANTIATE_TEST_SUITE_P() but instead
// used INSTANTIATE_TEST_CASE_P which is now deprecated.
#ifdef INSTANTIATE_TEST_SUITE_P
#define HWY_GTEST_INSTANTIATE_TEST_SUITE_P INSTANTIATE_TEST_SUITE_P
#else
#define HWY_GTEST_INSTANTIATE_TEST_SUITE_P INSTANTIATE_TEST_CASE_P
#endif

// Helper class to run parametric tests using the hwy target as parameter. To
// use this define the following in your test:
//   class MyTestSuite : public TestWithParamTarget {
//    ...
//   };
//   HWY_TARGET_INSTANTIATE_TEST_SUITE_P(MyTestSuite);
//   TEST_P(MyTestSuite, MyTest) { ... }
class TestWithParamTarget : public testing::TestWithParam<int64_t> {
 protected:
  void SetUp() override { SetSupportedTargetsForTest(GetParam()); }

  void TearDown() override {
    // Check that the parametric test calls SupportedTargets() when the source
    // was compiled with more than one target. In the single-target case only
    // static dispatch will be used anyway.
#if (HWY_TARGETS & (HWY_TARGETS - 1)) != 0
    EXPECT_TRUE(GetChosenTarget().IsInitialized())
        << "This hwy target parametric test doesn't use dynamic-dispatch and "
           "doesn't need to be parametric.";
#endif
    SetSupportedTargetsForTest(0);
  }
};

// Function to convert the test parameter of a TestWithParamTarget for
// displaying it in the gtest test name.
static inline std::string TestParamTargetName(
    const testing::TestParamInfo<int64_t>& info) {
  return TargetName(info.param);
}

#define HWY_TARGET_INSTANTIATE_TEST_SUITE_P(suite)              \
  HWY_GTEST_INSTANTIATE_TEST_SUITE_P(                           \
      suite##Group, suite,                                      \
      testing::ValuesIn(::hwy::SupportedAndGeneratedTargets()), \
      ::hwy::TestParamTargetName)

// Helper class similar to TestWithParamTarget to run parametric tests that
// depend on the target and another parametric test. If you need to use multiple
// extra parameters use a std::tuple<> of them and ::testing::Generate(...) as
// the generator. To use this class define the following in your test:
//   class MyTestSuite : public TestWithParamTargetT<int> {
//    ...
//   };
//   HWY_TARGET_INSTANTIATE_TEST_SUITE_P_T(MyTestSuite, ::testing::Range(0, 9));
//   TEST_P(MyTestSuite, MyTest) { ... GetParam() .... }
template <typename T>
class TestWithParamTargetAndT
    : public ::testing::TestWithParam<std::tuple<int64_t, T>> {
 public:
  // Expose the parametric type here so it can be used by the
  // HWY_TARGET_INSTANTIATE_TEST_SUITE_P_T macro.
  using HwyParamType = T;

 protected:
  void SetUp() override {
    SetSupportedTargetsForTest(std::get<0>(
        ::testing::TestWithParam<std::tuple<int64_t, T>>::GetParam()));
  }

  void TearDown() override {
    // Check that the parametric test calls SupportedTargets() when the source
    // was compiled with more than one target. In the single-target case only
    // static dispatch will be used anyway.
#if (HWY_TARGETS & (HWY_TARGETS - 1)) != 0
    EXPECT_TRUE(GetChosenTarget().IsInitialized())
        << "This hwy target parametric test doesn't use dynamic-dispatch and "
           "doesn't need to be parametric.";
#endif
    SetSupportedTargetsForTest(0);
  }

  T GetParam() {
    return std::get<1>(
        ::testing::TestWithParam<std::tuple<int64_t, T>>::GetParam());
  }
};

template <typename T>
std::string TestParamTargetNameAndT(
    const testing::TestParamInfo<std::tuple<int64_t, T>>& info) {
  return std::string(TargetName(std::get<0>(info.param))) + "_" +
         ::testing::PrintToString(std::get<1>(info.param));
}

#define HWY_TARGET_INSTANTIATE_TEST_SUITE_P_T(suite, generator)     \
  HWY_GTEST_INSTANTIATE_TEST_SUITE_P(                               \
      suite##Group, suite,                                          \
      ::testing::Combine(                                           \
          testing::ValuesIn(::hwy::SupportedAndGeneratedTargets()), \
          generator),                                               \
      ::hwy::TestParamTargetNameAndT<suite::HwyParamType>)

// Helper macro to export a function and define a test that tests it. This is
// equivalent to do a HWY_EXPORT of a void(void) function and run it in a test:
//   class MyTestSuite : public TestWithParamTarget {
//    ...
//   };
//   HWY_TARGET_INSTANTIATE_TEST_SUITE_P(MyTestSuite);
//   HWY_EXPORT_AND_TEST_P(MyTestSuite, MyTest);
#define HWY_EXPORT_AND_TEST_P(suite, func_name)                   \
  HWY_EXPORT(func_name);                                          \
  TEST_P(suite, func_name) { HWY_DYNAMIC_DISPATCH(func_name)(); } \
  static_assert(true, "For requiring trailing semicolon")

#define HWY_EXPORT_AND_TEST_P_T(suite, func_name)                           \
  HWY_EXPORT(func_name);                                                    \
  TEST_P(suite, func_name) { HWY_DYNAMIC_DISPATCH(func_name)(GetParam()); } \
  static_assert(true, "For requiring trailing semicolon")

#define HWY_BEFORE_TEST(suite)                      \
  class suite : public hwy::TestWithParamTarget {}; \
  HWY_TARGET_INSTANTIATE_TEST_SUITE_P(suite);       \
  static_assert(true, "For requiring trailing semicolon")

#define HWY_AFTER_TEST() static_assert(true, "For requiring trailing semicolon")

#define HWY_TEST_MAIN() static_assert(true, "For requiring trailing semicolon")

#else  // HWY_TEST_STANDALONE

namespace {

class GTestFilterPattern {
 private:
  struct FilterPatternComponent {
    bool has_match_any_string_wildcard;
    size_t min_num_of_leading_chars_to_match;
    const char* subpattern_start;
    size_t subpattern_to_match_len;
  };

 public:
  GTestFilterPattern() = default;
  GTestFilterPattern(const GTestFilterPattern&) = default;
  GTestFilterPattern(GTestFilterPattern&&) = default;
  GTestFilterPattern& operator=(const GTestFilterPattern&) = default;
  GTestFilterPattern& operator=(GTestFilterPattern&&) = default;
  GTestFilterPattern(const char* gtest_filter_pattern,
                     size_t gtest_filter_pattern_len);

 public:
  bool Matches(const char* test_name,
               size_t remaining_to_match_len) const noexcept;

 private:
  std::vector<FilterPatternComponent> pattern_components_;
  size_t min_test_name_len_;
};

GTestFilterPattern::GTestFilterPattern(const char* gtest_filter_pattern_str,
                                       size_t gtest_filter_pattern_len) {
  size_t min_test_name_len = 0;
  const char* const gtest_filter_pattern_str_end =
      gtest_filter_pattern_str + gtest_filter_pattern_len;

  for (const char* subpattern_start = gtest_filter_pattern_str;
       subpattern_start != gtest_filter_pattern_str_end;) {
    size_t min_num_of_leading_chars_to_match = 0;
    bool has_match_any_string_wildcard = false;

    // Advance subpattern_start past any '*' or '?' characters
    for (char first_non_wildcard_ch;
         subpattern_start != gtest_filter_pattern_str_end &&
         ((first_non_wildcard_ch = (*subpattern_start)) == '*' ||
          first_non_wildcard_ch == '?');
         ++subpattern_start) {
      if (first_non_wildcard_ch == '*') {
        has_match_any_string_wildcard = true;
      } else {
        ++min_num_of_leading_chars_to_match;
      }
    }

    // If subpattern_start != gtest_filter_pattern_str_end is true,
    // subpattern_start points to a non-wildcard character

    const char* subpattern_end;
    if ((subpattern_end = subpattern_start) != gtest_filter_pattern_str_end) {
      // Find the next '*' character past subpattern_start if there are any
      // '*' characters past subpattern_start in the subpattern
      while ((++subpattern_end) != gtest_filter_pattern_str_end &&
             (*subpattern_end) != '*') {
      }

      // Decrement subpattern_end while subpattern_end != subpattern_start + 1
      // is true and subpattern_end - 1 points to a '?' character
      for (;
           subpattern_end != subpattern_start + 1 && subpattern_end[-1] == '?';
           --subpattern_end) {
      }

      // subpattern_end - 1 now points to a non-wildcard character
    }

    // Add the current filter pattern component to pattern_components_
    const FilterPatternComponent curr_filter_component{
        has_match_any_string_wildcard, min_num_of_leading_chars_to_match,
        subpattern_start,
        static_cast<size_t>(subpattern_end - subpattern_start)};
    pattern_components_.push_back(curr_filter_component);

    // Advance to the next subpattern by setting subpattern_start to
    // subpattern_end
    subpattern_start = subpattern_end;
  }

  min_test_name_len_ = min_test_name_len;
}

bool GTestFilterPattern::Matches(const char* test_name,
                                 size_t remaining_to_match_len) const noexcept {
  if (remaining_to_match_len < min_test_name_len_) {
    return false;
  }

  const size_t num_of_pattern_components = pattern_components_.size();
  for (size_t i = num_of_pattern_components; i != 0; i--) {
    const FilterPatternComponent& curr_pattern_component =
        pattern_components_[i - 1];
    const size_t subpattern_to_match_len =
        curr_pattern_component.subpattern_to_match_len;
    const size_t min_num_to_match =
        curr_pattern_component.min_num_of_leading_chars_to_match +
        subpattern_to_match_len;

    if (remaining_to_match_len < min_num_to_match) {
      return false;
    }

    if (subpattern_to_match_len != 0) {
      const bool is_restartable_subpattern_match =
          i != num_of_pattern_components &&
          pattern_components_[i].has_match_any_string_wildcard;

      const char* subpattern_start = curr_pattern_component.subpattern_start;

      bool restart_match;
      do {
        restart_match = false;
        const size_t test_name_match_substr_offset =
            remaining_to_match_len - subpattern_to_match_len;

        bool matches_subpattern =
            test_name[test_name_match_substr_offset] == subpattern_start[0] &&
            test_name[test_name_match_substr_offset + subpattern_to_match_len -
                      1] == subpattern_start[subpattern_to_match_len - 1];
        if (matches_subpattern) {
          for (size_t i = 1; i != subpattern_to_match_len - 1; i++) {
            char c1 = test_name[test_name_match_substr_offset + i];
            char c2 = subpattern_start[i];

            if (c1 != c2 && c2 != '?') {
              matches_subpattern = false;
              break;
            }
          }
        }

        if (!matches_subpattern) {
          restart_match = is_restartable_subpattern_match &&
                          (--remaining_to_match_len) >= min_num_to_match;
          if (!restart_match) {
            return false;
          }
        }
      } while (restart_match);
    }

    remaining_to_match_len -= min_num_to_match;
  }

  return true;
}

std::vector<GTestFilterPattern>& PositiveGTestFilterPatterns() {
  static std::vector<GTestFilterPattern> positive_test_filter_patterns;
  return positive_test_filter_patterns;
}

std::vector<GTestFilterPattern>& NegativeGTestFilterPatterns() {
  static std::vector<GTestFilterPattern> negative_test_filter_patterns;
  return negative_test_filter_patterns;
}

// ShouldOnlyListHighwayTestNames() returns true if the names of the unit tests
// should be outputted without executing the unit tests.
//
// Otherwise, if the unit tests should be executed,
// ShouldOnlyListHighwayTestNames() returns true.
bool& ShouldOnlyListHighwayTestNames() {
  static bool should_only_list_test_names = false;
  return should_only_list_test_names;
}

// ParseGTestFilterPatterns parses the filter patterns passed into the
// --gtest_filter= command line argument (or set by the GTEST_FILTER environment
// variable if there is no --gtest_filter= command line argument present)
static void ParseGTestFilterPatterns(const char* gtest_filter_str) {
  std::vector<GTestFilterPattern>* ptr_to_positive_test_filter_patterns =
      &PositiveGTestFilterPatterns();
  std::vector<GTestFilterPattern>* ptr_to_negative_test_filter_patterns =
      &NegativeGTestFilterPatterns();

  std::vector<GTestFilterPattern>* ptr_to_vector_to_append_pattern_to =
      ptr_to_positive_test_filter_patterns;

  char first_filter_pattern_ch;
  bool colon_delimiter_encountered = false;
  while ((first_filter_pattern_ch = (*gtest_filter_str)) != '\0') {
    if (first_filter_pattern_ch == ':') {
      colon_delimiter_encountered = true;
      ++gtest_filter_str;
      continue;
    }

    if (first_filter_pattern_ch == '-' &&
        ptr_to_vector_to_append_pattern_to ==
            ptr_to_positive_test_filter_patterns) {
      ptr_to_vector_to_append_pattern_to = ptr_to_negative_test_filter_patterns;
      if (!colon_delimiter_encountered &&
          ptr_to_positive_test_filter_patterns->empty()) {
        ptr_to_positive_test_filter_patterns->emplace_back("*", 1);
      }
      ++gtest_filter_str;
      continue;
    }

    size_t filter_pattern_len = 1;

    // Find the next filter pattern delimiter character or null terminator
    for (char filter_pattern_end_ch;
         (filter_pattern_end_ch = gtest_filter_str[filter_pattern_len]) !=
             '\0' &&
         filter_pattern_end_ch != ':' &&
         (filter_pattern_end_ch != '-' ||
          ptr_to_vector_to_append_pattern_to ==
              ptr_to_negative_test_filter_patterns);
         ++filter_pattern_len) {
    }

    // Add the current filter pattern to *ptr_to_vector_to_append_pattern_to
    ptr_to_vector_to_append_pattern_to->emplace_back(gtest_filter_str,
                                                     filter_pattern_len);

    // Advance gtest_filter_str by filter_pattern_len chars
    gtest_filter_str += filter_pattern_len;
  }
}

// TestNameMatchesGTestFilter(test_name) returns true if any of the following
// are true:
// - test_name matches the filter passed in by the last --gtest_filter= command
//   line argument if any --gtest_filter= arguments are present on the command
//   line
// - test_name matches the filter set by the GTEST_FILTER environment variable
//   if no --gtest_filter= commands were passed into the command line and
//   the GTEST_FILTER environment variable is set
// - there were no --gtest_filter= arguments on the command line and the
//   GTEST_FILTER environment variable is not set
//
// Otherwise, TestNameMatchesGTestFilter(test_name) returns false
static HWY_INLINE HWY_MAYBE_UNUSED bool TestNameMatchesGTestFilter(
    const char* test_name, size_t test_name_len) {
  for (const GTestFilterPattern& negative_pattern :
       NegativeGTestFilterPatterns()) {
    if (negative_pattern.Matches(test_name, test_name_len)) {
      return false;
    }
  }
  for (const GTestFilterPattern& positive_pattern :
       PositiveGTestFilterPatterns()) {
    if (positive_pattern.Matches(test_name, test_name_len)) {
      return true;
    }
  }
  return false;
}
static HWY_INLINE HWY_MAYBE_UNUSED bool TestNameMatchesGTestFilter(
    const char* test_name) {
  return TestNameMatchesGTestFilter(test_name, strlen(test_name));
}
static HWY_INLINE HWY_MAYBE_UNUSED bool TestNameMatchesGTestFilter(
    const std::string& test_name) {
  return TestNameMatchesGTestFilter(test_name.data(), test_name.length());
}

// InitTestProgramOptions processes the GTEST_FILTER environment variable, the
// --gtest_filter= command line argument, and the --gtest_list_tests command
// line argument
static HWY_MAYBE_UNUSED void InitTestProgramOptions(
    const int argc, const char* const* const argv) {
  // Suppress warning that is normally emitted by MSVC by the getenv call below
  HWY_DIAGNOSTICS(push)
#if HWY_COMPILER_MSVC || HWY_COMPILER_CLANGCL
  HWY_DIAGNOSTICS_OFF(disable : 4996, ignored "-Wdeprecated-declarations")
#endif
  const char* gtest_filter = getenv("GTEST_FILTER");
  HWY_DIAGNOSTICS(pop)

  if (!gtest_filter) {
    gtest_filter = "*";
  }
  for (int i = 1; i < argc; i++) {
    const char* const curr_arg = argv[i];
    if (!curr_arg) {
      break;
    }
    if (curr_arg[0] == '-' && curr_arg[1] == '-' && curr_arg[2] == 'g' &&
        curr_arg[3] == 't' && curr_arg[4] == 'e' && curr_arg[5] == 's' &&
        curr_arg[6] == 't' && curr_arg[7] == '_') { /* --gtest_ */
      switch (curr_arg[8]) {
        case 'f':
          if (curr_arg[9] == 'i' && curr_arg[10] == 'l' &&
              curr_arg[11] == 't' && curr_arg[12] == 'e' &&
              curr_arg[13] == 'r' && curr_arg[14] == '=') {
            // If the --gtest_filter= command line option is specified, only
            // execute the tests that match the specified filter
            gtest_filter = curr_arg + 15;
          }
          break;
        case 'l':
          if (curr_arg[9] == 'i' && curr_arg[10] == 's' &&
              curr_arg[11] == 't' && curr_arg[12] == '_' &&
              curr_arg[13] == 't' && curr_arg[14] == 'e' &&
              curr_arg[15] == 's' && curr_arg[16] == 't' &&
              curr_arg[17] == 's' && curr_arg[18] == '\0') {
            // If the --gtest_list_tests command line option is specified,
            // output the name of the unit tests but do not execute the unit
            // tests
            ShouldOnlyListHighwayTestNames() = true;
            break;
          }
        default:
          break;
      }
    }
  }

  // Initialize PositiveGTestFilterPatterns() and NegativeGTestFilterPatterns()
  // from gtest_filter
  ParseGTestFilterPatterns(gtest_filter);
}

}  // namespace

// Cannot be a function, otherwise the HWY_EXPORT table defined here will not
// be visible to HWY_DYNAMIC_DISPATCH.
#define HWY_EXPORT_AND_TEST_P(suite, func_name)                         \
  full_test_name = #suite;                                              \
  full_test_name += "Group/";                                           \
  full_test_name += #suite;                                             \
  full_test_name += '.';                                                \
  full_test_name_suite_prefix_len = full_test_name.length();            \
  full_test_name += #func_name;                                         \
  full_test_name += '/';                                                \
  full_test_name_prefix_len = full_test_name.length();                  \
  HWY_EXPORT(func_name);                                                \
  hwy::SetSupportedTargetsForTest(0);                                   \
  for (int64_t target : hwy::SupportedAndGeneratedTargets()) {          \
    hwy::SetSupportedTargetsForTest(target);                            \
    full_test_name.resize(full_test_name_prefix_len);                   \
    full_test_name += hwy::TargetName(target);                          \
    if (hwy::TestNameMatchesGTestFilter(full_test_name)) {              \
      if (hwy::ShouldOnlyListHighwayTestNames()) {                      \
        const char* full_test_name_c_str = full_test_name.c_str();      \
        if (need_to_output_suite_name) {                                \
          need_to_output_suite_name = false;                            \
          printf("%sGroup/%s.\n", #suite, #suite);                      \
        }                                                               \
        printf("  %s\n",                                                \
               full_test_name_c_str + full_test_name_suite_prefix_len); \
      } else {                                                          \
        fprintf(stderr, "=== %s for %s:\n", #func_name,                 \
                hwy::TargetName(target));                               \
        HWY_DYNAMIC_DISPATCH(func_name)();                              \
      }                                                                 \
    }                                                                   \
  }                                                                     \
  /* Disable the mask after the test. */                                \
  hwy::SetSupportedTargetsForTest(0);                                   \
  static_assert(true, "For requiring trailing semicolon")

// HWY_BEFORE_TEST may reside inside a namespace, but HWY_AFTER_TEST will define
// a main() at namespace scope that wants to call into that namespace, so stash
// the function address in a singleton defined in namespace hwy.
using VoidFunc = void (*)(void);

VoidFunc& GetRunAll() {
  static VoidFunc func;
  return func;
}

struct RegisterRunAll {
  RegisterRunAll(VoidFunc func) { hwy::GetRunAll() = func; }
};

#define HWY_BEFORE_TEST(suite)                                 \
  void RunAll();                                               \
  static hwy::RegisterRunAll HWY_CONCAT(reg_, suite)(&RunAll); \
  void RunAll() {                                              \
    std::string full_test_name;                                \
    size_t full_test_name_suite_prefix_len;                    \
    size_t full_test_name_prefix_len;                          \
    bool need_to_output_suite_name = true;                     \
    static_assert(true, "For requiring trailing semicolon")

// Must be followed by semicolon, then a closing brace for ONE namespace.
#define HWY_AFTER_TEST()                          \
  } /* RunAll*/                                   \
  } /* namespace */                               \
  int main(int argc, char** argv) {               \
    hwy::InitTestProgramOptions(argc, argv);      \
    hwy::GetRunAll()();                           \
    if (!hwy::ShouldOnlyListHighwayTestNames()) { \
      fprintf(stderr, "Success.\n");              \
    }                                             \
    return 0

// -------------------- Non-SIMD test cases:

struct FuncAndName {
  VoidFunc func;
  const char* name;
  const char* suite_name;
  const char* full_name;
};

// Singleton of registered tests to be run by HWY_TEST_MAIN
std::vector<FuncAndName>& GetFuncAndNames() {
  static std::vector<FuncAndName> vec;
  return vec;
}

// For use by TEST; adds to the list.
struct RegisterTest {
  RegisterTest(VoidFunc func, const char* name, const char* suite_name,
               const char* full_name) {
    hwy::GetFuncAndNames().push_back({func, name, suite_name, full_name});
  }
};

// Registers a function to be called by `HWY_TEST_MAIN`.
#define TEST(suite, func)                                                    \
  void func();                                                               \
  static hwy::RegisterTest HWY_CONCAT(                                       \
      reg_, func)({&func, #func, #suite, #suite "Group/" #suite "." #func}); \
  void func()

// Expands to a main() that calls all TEST. Must reside at namespace scope.
#define HWY_TEST_MAIN()                                                    \
  int main(int argc, char** argv) {                                        \
    hwy::InitTestProgramOptions(argc, argv);                               \
    const char* suite_name_of_prev_test = nullptr;                         \
    for (const auto& func_and_name : hwy::GetFuncAndNames()) {             \
      if (hwy::TestNameMatchesGTestFilter(func_and_name.full_name)) {      \
        if (hwy::ShouldOnlyListHighwayTestNames()) {                       \
          const char* const suite_name_of_curr_test =                      \
              func_and_name.suite_name;                                    \
          if (suite_name_of_curr_test != suite_name_of_prev_test &&        \
              (!suite_name_of_prev_test ||                                 \
               strcmp(suite_name_of_prev_test, suite_name_of_curr_test) == \
                   0)) {                                                   \
            suite_name_of_prev_test = suite_name_of_curr_test;             \
            printf("%sGroup/%s.\n", suite_name_of_curr_test,               \
                   suite_name_of_curr_test);                               \
          }                                                                \
          printf("  %s\n", func_and_name.name);                            \
        } else {                                                           \
          fprintf(stderr, "=== %s:\n", func_and_name.name);                \
          func_and_name.func();                                            \
        }                                                                  \
      }                                                                    \
    }                                                                      \
    if (!hwy::ShouldOnlyListHighwayTestNames()) {                          \
      fprintf(stderr, "Success.\n");                                       \
    }                                                                      \
    return 0;                                                              \
  }                                                                        \
  static_assert(true, "For requiring trailing semicolon")

#endif  // HWY_TEST_STANDALONE

}  // namespace hwy

#endif  // HWY_TESTS_HWY_GTEST_H_
