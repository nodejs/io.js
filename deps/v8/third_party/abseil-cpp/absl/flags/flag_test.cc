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

#include "absl/flags/flag.h"

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <string>
#include <thread>  // NOLINT
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/flags/config.h"
#include "absl/flags/declare.h"
#include "absl/flags/internal/flag.h"
#include "absl/flags/marshalling.h"
#include "absl/flags/parse.h"
#include "absl/flags/reflection.h"
#include "absl/flags/usage_config.h"
#include "absl/numeric/int128.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"

ABSL_DECLARE_FLAG(int64_t, mistyped_int_flag);
ABSL_DECLARE_FLAG(std::vector<std::string>, mistyped_string_flag);

namespace {

namespace flags = absl::flags_internal;

std::string TestHelpMsg() { return "dynamic help"; }
#if defined(_MSC_VER) && !defined(__clang__)
std::string TestLiteralHelpMsg() { return "literal help"; }
#endif
template <typename T>
void TestMakeDflt(void* dst) {
  new (dst) T{};
}
void TestCallback() {}

struct UDT {
  UDT() = default;
  UDT(const UDT&) = default;
  UDT& operator=(const UDT&) = default;
};
bool AbslParseFlag(absl::string_view, UDT*, std::string*) { return true; }
std::string AbslUnparseFlag(const UDT&) { return ""; }

class FlagTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    // Install a function to normalize filenames before this test is run.
    absl::FlagsUsageConfig default_config;
    default_config.normalize_filename = &FlagTest::NormalizeFileName;
    absl::SetFlagsUsageConfig(default_config);
  }

 private:
  static std::string NormalizeFileName(absl::string_view fname) {
#ifdef _WIN32
    std::string normalized(fname);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    fname = normalized;
#endif
    return std::string(fname);
  }
  absl::FlagSaver flag_saver_;
};

struct S1 {
  S1() = default;
  S1(const S1&) = default;
  int32_t f1;
  int64_t f2;
};

struct S2 {
  S2() = default;
  S2(const S2&) = default;
  int64_t f1;
  double f2;
};

TEST_F(FlagTest, Traits) {
  EXPECT_EQ(flags::StorageKind<int>(),
            flags::FlagValueStorageKind::kValueAndInitBit);
  EXPECT_EQ(flags::StorageKind<bool>(),
            flags::FlagValueStorageKind::kValueAndInitBit);
  EXPECT_EQ(flags::StorageKind<double>(),
            flags::FlagValueStorageKind::kOneWordAtomic);
  EXPECT_EQ(flags::StorageKind<int64_t>(),
            flags::FlagValueStorageKind::kOneWordAtomic);

  EXPECT_EQ(flags::StorageKind<S1>(),
            flags::FlagValueStorageKind::kSequenceLocked);
  EXPECT_EQ(flags::StorageKind<S2>(),
            flags::FlagValueStorageKind::kSequenceLocked);
// Make sure absl::Duration uses the sequence-locked code path. MSVC 2015
// doesn't consider absl::Duration to be trivially-copyable so we just
// restrict this to clang as it seems to be a well-behaved compiler.
#ifdef __clang__
  EXPECT_EQ(flags::StorageKind<absl::Duration>(),
            flags::FlagValueStorageKind::kSequenceLocked);
#endif

  EXPECT_EQ(flags::StorageKind<std::string>(),
            flags::FlagValueStorageKind::kHeapAllocated);
  EXPECT_EQ(flags::StorageKind<std::vector<std::string>>(),
            flags::FlagValueStorageKind::kHeapAllocated);

  EXPECT_EQ(flags::StorageKind<absl::int128>(),
            flags::FlagValueStorageKind::kSequenceLocked);
  EXPECT_EQ(flags::StorageKind<absl::uint128>(),
            flags::FlagValueStorageKind::kSequenceLocked);
}

// --------------------------------------------------------------------

constexpr flags::FlagHelpArg help_arg{flags::FlagHelpMsg("literal help"),
                                      flags::FlagHelpKind::kLiteral};

using String = std::string;
using int128 = absl::int128;
using uint128 = absl::uint128;

#define DEFINE_CONSTRUCTED_FLAG(T, dflt, dflt_kind)                        \
  constexpr flags::FlagDefaultArg f1default##T{                            \
      flags::FlagDefaultSrc{dflt}, flags::FlagDefaultKind::dflt_kind};     \
  constexpr absl::Flag<T> f1##T{"f1", "file", help_arg, f1default##T};     \
  ABSL_CONST_INIT absl::Flag<T> f2##T {                                    \
    "f2", "file",                                                          \
        {flags::FlagHelpMsg(&TestHelpMsg), flags::FlagHelpKind::kGenFunc}, \
        flags::FlagDefaultArg {                                            \
      flags::FlagDefaultSrc(&TestMakeDflt<T>),                             \
          flags::FlagDefaultKind::kGenFunc                                 \
    }                                                                      \
  }

DEFINE_CONSTRUCTED_FLAG(bool, true, kOneWord);
DEFINE_CONSTRUCTED_FLAG(int16_t, 1, kOneWord);
DEFINE_CONSTRUCTED_FLAG(uint16_t, 2, kOneWord);
DEFINE_CONSTRUCTED_FLAG(int32_t, 3, kOneWord);
DEFINE_CONSTRUCTED_FLAG(uint32_t, 4, kOneWord);
DEFINE_CONSTRUCTED_FLAG(int64_t, 5, kOneWord);
DEFINE_CONSTRUCTED_FLAG(uint64_t, 6, kOneWord);
DEFINE_CONSTRUCTED_FLAG(float, 7.8, kOneWord);
DEFINE_CONSTRUCTED_FLAG(double, 9.10, kOneWord);
DEFINE_CONSTRUCTED_FLAG(String, &TestMakeDflt<String>, kGenFunc);
DEFINE_CONSTRUCTED_FLAG(UDT, &TestMakeDflt<UDT>, kGenFunc);
DEFINE_CONSTRUCTED_FLAG(int128, 13, kGenFunc);
DEFINE_CONSTRUCTED_FLAG(uint128, 14, kGenFunc);

template <typename T>
bool TestConstructionFor(const absl::Flag<T>& f1, absl::Flag<T>& f2) {
  EXPECT_EQ(absl::GetFlagReflectionHandle(f1).Name(), "f1");
  EXPECT_EQ(absl::GetFlagReflectionHandle(f1).Help(), "literal help");
  EXPECT_EQ(absl::GetFlagReflectionHandle(f1).Filename(), "file");

  flags::FlagRegistrar<T, false>(ABSL_FLAG_IMPL_FLAG_PTR(f2), nullptr)
      .OnUpdate(TestCallback);

  EXPECT_EQ(absl::GetFlagReflectionHandle(f2).Name(), "f2");
  EXPECT_EQ(absl::GetFlagReflectionHandle(f2).Help(), "dynamic help");
  EXPECT_EQ(absl::GetFlagReflectionHandle(f2).Filename(), "file");

  return true;
}

#define TEST_CONSTRUCTED_FLAG(T) TestConstructionFor(f1##T, f2##T);

TEST_F(FlagTest, TestConstruction) {
  TEST_CONSTRUCTED_FLAG(bool);
  TEST_CONSTRUCTED_FLAG(int16_t);
  TEST_CONSTRUCTED_FLAG(uint16_t);
  TEST_CONSTRUCTED_FLAG(int32_t);
  TEST_CONSTRUCTED_FLAG(uint32_t);
  TEST_CONSTRUCTED_FLAG(int64_t);
  TEST_CONSTRUCTED_FLAG(uint64_t);
  TEST_CONSTRUCTED_FLAG(float);
  TEST_CONSTRUCTED_FLAG(double);
  TEST_CONSTRUCTED_FLAG(String);
  TEST_CONSTRUCTED_FLAG(UDT);
  TEST_CONSTRUCTED_FLAG(int128);
  TEST_CONSTRUCTED_FLAG(uint128);
}

// --------------------------------------------------------------------

}  // namespace

ABSL_DECLARE_FLAG(bool, test_flag_01);
ABSL_DECLARE_FLAG(int, test_flag_02);
ABSL_DECLARE_FLAG(int16_t, test_flag_03);
ABSL_DECLARE_FLAG(uint16_t, test_flag_04);
ABSL_DECLARE_FLAG(int32_t, test_flag_05);
ABSL_DECLARE_FLAG(uint32_t, test_flag_06);
ABSL_DECLARE_FLAG(int64_t, test_flag_07);
ABSL_DECLARE_FLAG(uint64_t, test_flag_08);
ABSL_DECLARE_FLAG(double, test_flag_09);
ABSL_DECLARE_FLAG(float, test_flag_10);
ABSL_DECLARE_FLAG(std::string, test_flag_11);
ABSL_DECLARE_FLAG(absl::Duration, test_flag_12);
ABSL_DECLARE_FLAG(absl::int128, test_flag_13);
ABSL_DECLARE_FLAG(absl::uint128, test_flag_14);

namespace {

TEST_F(FlagTest, TestFlagDeclaration) {
#if ABSL_FLAGS_STRIP_NAMES
  GTEST_SKIP() << "This test requires flag names to be present";
#endif
  // test that we can access flag objects.
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_01).Name(),
            "test_flag_01");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_02).Name(),
            "test_flag_02");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_03).Name(),
            "test_flag_03");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_04).Name(),
            "test_flag_04");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_05).Name(),
            "test_flag_05");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_06).Name(),
            "test_flag_06");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_07).Name(),
            "test_flag_07");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_08).Name(),
            "test_flag_08");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_09).Name(),
            "test_flag_09");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_10).Name(),
            "test_flag_10");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_11).Name(),
            "test_flag_11");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_12).Name(),
            "test_flag_12");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_13).Name(),
            "test_flag_13");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_14).Name(),
            "test_flag_14");
}

}  // namespace

#if ABSL_FLAGS_STRIP_NAMES
// The intent of this helper struct and an expression below is to make sure that
// in the configuration where ABSL_FLAGS_STRIP_NAMES=1 registrar construction
// (in cases of no Tail calls like OnUpdate) is constexpr and thus can and
// should be completely optimized away, thus avoiding the cost/overhead of
// static initializers.
struct VerifyConsteval {
  friend consteval flags::FlagRegistrarEmpty operator+(
      flags::FlagRegistrarEmpty, VerifyConsteval) {
    return {};
  }
};

ABSL_FLAG(int, test_registrar_const_init, 0, "") + VerifyConsteval();
#endif

// --------------------------------------------------------------------

ABSL_FLAG(bool, test_flag_01, true, "test flag 01");
ABSL_FLAG(int, test_flag_02, 1234, "test flag 02");
ABSL_FLAG(int16_t, test_flag_03, -34, "test flag 03");
ABSL_FLAG(uint16_t, test_flag_04, 189, "test flag 04");
ABSL_FLAG(int32_t, test_flag_05, 10765, "test flag 05");
ABSL_FLAG(uint32_t, test_flag_06, 40000, "test flag 06");
ABSL_FLAG(int64_t, test_flag_07, -1234567, "test flag 07");
ABSL_FLAG(uint64_t, test_flag_08, 9876543, "test flag 08");
ABSL_FLAG(double, test_flag_09, -9.876e-50, "test flag 09");
ABSL_FLAG(float, test_flag_10, 1.234e12f, "test flag 10");
ABSL_FLAG(std::string, test_flag_11, "", "test flag 11");
ABSL_FLAG(absl::Duration, test_flag_12, absl::Minutes(10), "test flag 12");
ABSL_FLAG(absl::int128, test_flag_13, absl::MakeInt128(-1, 0), "test flag 13");
ABSL_FLAG(absl::uint128, test_flag_14, absl::MakeUint128(0, 0xFFFAAABBBCCCDDD),
          "test flag 14");

namespace {

TEST_F(FlagTest, TestFlagDefinition) {
#if ABSL_FLAGS_STRIP_NAMES
  GTEST_SKIP() << "This test requires flag names to be present";
#endif
  absl::string_view expected_file_name = "absl/flags/flag_test.cc";

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_01).Name(),
            "test_flag_01");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_01).Help(),
            "test flag 01");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_01).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_01).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_02).Name(),
            "test_flag_02");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_02).Help(),
            "test flag 02");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_02).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_02).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_03).Name(),
            "test_flag_03");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_03).Help(),
            "test flag 03");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_03).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_03).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_04).Name(),
            "test_flag_04");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_04).Help(),
            "test flag 04");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_04).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_04).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_05).Name(),
            "test_flag_05");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_05).Help(),
            "test flag 05");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_05).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_05).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_06).Name(),
            "test_flag_06");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_06).Help(),
            "test flag 06");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_06).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_06).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_07).Name(),
            "test_flag_07");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_07).Help(),
            "test flag 07");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_07).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_07).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_08).Name(),
            "test_flag_08");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_08).Help(),
            "test flag 08");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_08).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_08).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_09).Name(),
            "test_flag_09");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_09).Help(),
            "test flag 09");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_09).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_09).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_10).Name(),
            "test_flag_10");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_10).Help(),
            "test flag 10");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_10).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_10).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_11).Name(),
            "test_flag_11");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_11).Help(),
            "test flag 11");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_11).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_11).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_12).Name(),
            "test_flag_12");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_12).Help(),
            "test flag 12");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_12).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_12).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_13).Name(),
            "test_flag_13");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_13).Help(),
            "test flag 13");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_13).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_13).Filename();

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_14).Name(),
            "test_flag_14");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_14).Help(),
            "test flag 14");
  EXPECT_TRUE(absl::EndsWith(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_14).Filename(),
      expected_file_name))
      << absl::GetFlagReflectionHandle(FLAGS_test_flag_14).Filename();
}

// --------------------------------------------------------------------

TEST_F(FlagTest, TestDefault) {
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_01).DefaultValue(),
            "true");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_02).DefaultValue(),
            "1234");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_03).DefaultValue(),
            "-34");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_04).DefaultValue(),
            "189");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_05).DefaultValue(),
            "10765");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_06).DefaultValue(),
            "40000");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_07).DefaultValue(),
            "-1234567");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_08).DefaultValue(),
            "9876543");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_09).DefaultValue(),
            "-9.876e-50");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_10).DefaultValue(),
            "1.234e+12");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_11).DefaultValue(),
            "");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_12).DefaultValue(),
            "10m");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_13).DefaultValue(),
            "-18446744073709551616");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_14).DefaultValue(),
            "1152827684197027293");

  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_01).CurrentValue(),
            "true");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_02).CurrentValue(),
            "1234");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_03).CurrentValue(),
            "-34");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_04).CurrentValue(),
            "189");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_05).CurrentValue(),
            "10765");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_06).CurrentValue(),
            "40000");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_07).CurrentValue(),
            "-1234567");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_08).CurrentValue(),
            "9876543");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_09).CurrentValue(),
            "-9.876e-50");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_10).CurrentValue(),
            "1.234e+12");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_11).CurrentValue(),
            "");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_12).CurrentValue(),
            "10m");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_13).CurrentValue(),
            "-18446744073709551616");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_14).CurrentValue(),
            "1152827684197027293");

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
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_13), absl::MakeInt128(-1, 0));
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_14),
            absl::MakeUint128(0, 0xFFFAAABBBCCCDDD));
}

// --------------------------------------------------------------------

struct NonTriviallyCopyableAggregate {
  NonTriviallyCopyableAggregate() = default;
  // NOLINTNEXTLINE
  NonTriviallyCopyableAggregate(const NonTriviallyCopyableAggregate& rhs)
      : value(rhs.value) {}
  // NOLINTNEXTLINE
  NonTriviallyCopyableAggregate& operator=(
      const NonTriviallyCopyableAggregate& rhs) {
    value = rhs.value;
    return *this;
  }

  int value;
};
bool AbslParseFlag(absl::string_view src, NonTriviallyCopyableAggregate* f,
                   std::string* e) {
  return absl::ParseFlag(src, &f->value, e);
}
std::string AbslUnparseFlag(const NonTriviallyCopyableAggregate& ntc) {
  return absl::StrCat(ntc.value);
}

bool operator==(const NonTriviallyCopyableAggregate& ntc1,
                const NonTriviallyCopyableAggregate& ntc2) {
  return ntc1.value == ntc2.value;
}

}  // namespace

ABSL_FLAG(bool, test_flag_eb_01, {}, "");
ABSL_FLAG(int32_t, test_flag_eb_02, {}, "");
ABSL_FLAG(int64_t, test_flag_eb_03, {}, "");
ABSL_FLAG(double, test_flag_eb_04, {}, "");
ABSL_FLAG(std::string, test_flag_eb_05, {}, "");
ABSL_FLAG(NonTriviallyCopyableAggregate, test_flag_eb_06, {}, "");

namespace {

TEST_F(FlagTest, TestEmptyBracesDefault) {
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_eb_01).DefaultValue(),
            "false");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_eb_02).DefaultValue(),
            "0");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_eb_03).DefaultValue(),
            "0");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_eb_04).DefaultValue(),
            "0");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_eb_05).DefaultValue(),
            "");
  EXPECT_EQ(absl::GetFlagReflectionHandle(FLAGS_test_flag_eb_06).DefaultValue(),
            "0");

  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_eb_01), false);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_eb_02), 0);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_eb_03), 0);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_eb_04), 0.0);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_eb_05), "");
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_eb_06),
            NonTriviallyCopyableAggregate{});
}

// --------------------------------------------------------------------

TEST_F(FlagTest, TestGetSet) {
  absl::SetFlag(&FLAGS_test_flag_01, false);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_01), false);

  absl::SetFlag(&FLAGS_test_flag_02, 321);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_02), 321);

  absl::SetFlag(&FLAGS_test_flag_03, 67);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_03), 67);

  absl::SetFlag(&FLAGS_test_flag_04, 1);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_04), 1);

  absl::SetFlag(&FLAGS_test_flag_05, -908);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_05), -908);

  absl::SetFlag(&FLAGS_test_flag_06, 4001);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_06), 4001);

  absl::SetFlag(&FLAGS_test_flag_07, -23456);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_07), -23456);

  absl::SetFlag(&FLAGS_test_flag_08, 975310);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_08), 975310);

  absl::SetFlag(&FLAGS_test_flag_09, 1.00001);
  EXPECT_NEAR(absl::GetFlag(FLAGS_test_flag_09), 1.00001, 1e-10);

  absl::SetFlag(&FLAGS_test_flag_10, -3.54f);
  EXPECT_NEAR(absl::GetFlag(FLAGS_test_flag_10), -3.54f, 1e-6f);

  absl::SetFlag(&FLAGS_test_flag_11, "asdf");
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_11), "asdf");

  absl::SetFlag(&FLAGS_test_flag_12, absl::Seconds(110));
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_12), absl::Seconds(110));

  absl::SetFlag(&FLAGS_test_flag_13, absl::MakeInt128(-1, 0));
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_13), absl::MakeInt128(-1, 0));

  absl::SetFlag(&FLAGS_test_flag_14, absl::MakeUint128(0, 0xFFFAAABBBCCCDDD));
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_14),
            absl::MakeUint128(0, 0xFFFAAABBBCCCDDD));
}

// --------------------------------------------------------------------

TEST_F(FlagTest, TestGetViaReflection) {
#if ABSL_FLAGS_STRIP_NAMES
  GTEST_SKIP() << "This test requires flag names to be present";
#endif
  auto* handle = absl::FindCommandLineFlag("test_flag_01");
  EXPECT_EQ(*handle->TryGet<bool>(), true);
  handle = absl::FindCommandLineFlag("test_flag_02");
  EXPECT_EQ(*handle->TryGet<int>(), 1234);
  handle = absl::FindCommandLineFlag("test_flag_03");
  EXPECT_EQ(*handle->TryGet<int16_t>(), -34);
  handle = absl::FindCommandLineFlag("test_flag_04");
  EXPECT_EQ(*handle->TryGet<uint16_t>(), 189);
  handle = absl::FindCommandLineFlag("test_flag_05");
  EXPECT_EQ(*handle->TryGet<int32_t>(), 10765);
  handle = absl::FindCommandLineFlag("test_flag_06");
  EXPECT_EQ(*handle->TryGet<uint32_t>(), 40000);
  handle = absl::FindCommandLineFlag("test_flag_07");
  EXPECT_EQ(*handle->TryGet<int64_t>(), -1234567);
  handle = absl::FindCommandLineFlag("test_flag_08");
  EXPECT_EQ(*handle->TryGet<uint64_t>(), 9876543);
  handle = absl::FindCommandLineFlag("test_flag_09");
  EXPECT_NEAR(*handle->TryGet<double>(), -9.876e-50, 1e-55);
  handle = absl::FindCommandLineFlag("test_flag_10");
  EXPECT_NEAR(*handle->TryGet<float>(), 1.234e12f, 1e5f);
  handle = absl::FindCommandLineFlag("test_flag_11");
  EXPECT_EQ(*handle->TryGet<std::string>(), "");
  handle = absl::FindCommandLineFlag("test_flag_12");
  EXPECT_EQ(*handle->TryGet<absl::Duration>(), absl::Minutes(10));
  handle = absl::FindCommandLineFlag("test_flag_13");
  EXPECT_EQ(*handle->TryGet<absl::int128>(), absl::MakeInt128(-1, 0));
  handle = absl::FindCommandLineFlag("test_flag_14");
  EXPECT_EQ(*handle->TryGet<absl::uint128>(),
            absl::MakeUint128(0, 0xFFFAAABBBCCCDDD));
}

// --------------------------------------------------------------------

TEST_F(FlagTest, ConcurrentSetAndGet) {
#if ABSL_FLAGS_STRIP_NAMES
  GTEST_SKIP() << "This test requires flag names to be present";
#endif
  static constexpr int kNumThreads = 8;
  // Two arbitrary durations. One thread will concurrently flip the flag
  // between these two values, while the other threads read it and verify
  // that no other value is seen.
  static const absl::Duration kValidDurations[] = {
      absl::Seconds(int64_t{0x6cebf47a9b68c802}) + absl::Nanoseconds(229702057),
      absl::Seconds(int64_t{0x23fec0307e4e9d3}) + absl::Nanoseconds(44555374)};
  absl::SetFlag(&FLAGS_test_flag_12, kValidDurations[0]);

  std::atomic<bool> stop{false};
  std::vector<std::thread> threads;
  auto* handle = absl::FindCommandLineFlag("test_flag_12");
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&]() {
      while (!stop.load(std::memory_order_relaxed)) {
        // Try loading the flag both directly and via a reflection
        // handle.
        absl::Duration v = absl::GetFlag(FLAGS_test_flag_12);
        EXPECT_TRUE(v == kValidDurations[0] || v == kValidDurations[1]);
        v = *handle->TryGet<absl::Duration>();
        EXPECT_TRUE(v == kValidDurations[0] || v == kValidDurations[1]);
      }
    });
  }
  absl::Time end_time = absl::Now() + absl::Seconds(1);
  int i = 0;
  while (absl::Now() < end_time) {
    absl::SetFlag(&FLAGS_test_flag_12,
                  kValidDurations[i++ % ABSL_ARRAYSIZE(kValidDurations)]);
  }
  stop.store(true, std::memory_order_relaxed);
  for (auto& t : threads) t.join();
}

// --------------------------------------------------------------------

int GetDflt1() { return 1; }

}  // namespace

ABSL_FLAG(int, test_int_flag_with_non_const_default, GetDflt1(),
          "test int flag non const default");
ABSL_FLAG(std::string, test_string_flag_with_non_const_default,
          absl::StrCat("AAA", "BBB"), "test string flag non const default");

namespace {

TEST_F(FlagTest, TestNonConstexprDefault) {
  EXPECT_EQ(absl::GetFlag(FLAGS_test_int_flag_with_non_const_default), 1);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_string_flag_with_non_const_default),
            "AAABBB");
}

// --------------------------------------------------------------------

}  // namespace

ABSL_FLAG(bool, test_flag_with_non_const_help, true,
          absl::StrCat("test ", "flag ", "non const help"));

namespace {

#if !ABSL_FLAGS_STRIP_HELP
TEST_F(FlagTest, TestNonConstexprHelp) {
  EXPECT_EQ(
      absl::GetFlagReflectionHandle(FLAGS_test_flag_with_non_const_help).Help(),
      "test flag non const help");
}
#endif  //! ABSL_FLAGS_STRIP_HELP

// --------------------------------------------------------------------

int cb_test_value = -1;
void TestFlagCB();

}  // namespace

ABSL_FLAG(int, test_flag_with_cb, 100, "").OnUpdate(TestFlagCB);

ABSL_FLAG(int, test_flag_with_lambda_cb, 200, "").OnUpdate([]() {
  cb_test_value = absl::GetFlag(FLAGS_test_flag_with_lambda_cb) +
                  absl::GetFlag(FLAGS_test_flag_with_cb);
});

namespace {

void TestFlagCB() { cb_test_value = absl::GetFlag(FLAGS_test_flag_with_cb); }

// Tests side-effects of callback invocation.
TEST_F(FlagTest, CallbackInvocation) {
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_with_cb), 100);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_with_lambda_cb), 200);
  EXPECT_EQ(cb_test_value, 300);

  absl::SetFlag(&FLAGS_test_flag_with_cb, 1);
  EXPECT_EQ(cb_test_value, 1);

  absl::SetFlag(&FLAGS_test_flag_with_lambda_cb, 3);
  EXPECT_EQ(cb_test_value, 4);
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

ABSL_FLAG(CustomUDT, test_flag_custom_udt, CustomUDT(), "test flag custom UDT");

namespace {

TEST_F(FlagTest, TestCustomUDT) {
  EXPECT_EQ(flags::StorageKind<CustomUDT>(),
            flags::FlagValueStorageKind::kOneWordAtomic);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_custom_udt), CustomUDT(1, 1));
  absl::SetFlag(&FLAGS_test_flag_custom_udt, CustomUDT(2, 3));
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_custom_udt), CustomUDT(2, 3));
}

// MSVC produces link error on the type mismatch.
// Linux does not have build errors and validations work as expected.
#if !defined(_WIN32) && GTEST_HAS_DEATH_TEST
using FlagDeathTest = FlagTest;

TEST_F(FlagDeathTest, TestTypeMismatchValidations) {
#if ABSL_FLAGS_STRIP_NAMES
  GTEST_SKIP() << "This test requires flag names to be present";
#endif
#if !defined(NDEBUG)
  EXPECT_DEATH_IF_SUPPORTED(
      static_cast<void>(absl::GetFlag(FLAGS_mistyped_int_flag)),
      "Flag 'mistyped_int_flag' is defined as one type and declared "
      "as another");
  EXPECT_DEATH_IF_SUPPORTED(
      static_cast<void>(absl::GetFlag(FLAGS_mistyped_string_flag)),
      "Flag 'mistyped_string_flag' is defined as one type and "
      "declared as another");
#endif

  EXPECT_DEATH_IF_SUPPORTED(
      absl::SetFlag(&FLAGS_mistyped_int_flag, 1),
      "Flag 'mistyped_int_flag' is defined as one type and declared "
      "as another");
  EXPECT_DEATH_IF_SUPPORTED(
      absl::SetFlag(&FLAGS_mistyped_string_flag, std::vector<std::string>{}),
      "Flag 'mistyped_string_flag' is defined as one type and declared as "
      "another");
}

#endif

// --------------------------------------------------------------------

// A contrived type that offers implicit and explicit conversion from specific
// source types.
struct ConversionTestVal {
  ConversionTestVal() = default;
  explicit ConversionTestVal(int a_in) : a(a_in) {}

  enum class ViaImplicitConv { kTen = 10, kEleven };
  // NOLINTNEXTLINE
  ConversionTestVal(ViaImplicitConv from) : a(static_cast<int>(from)) {}

  int a;
};

bool AbslParseFlag(absl::string_view in, ConversionTestVal* val_out,
                   std::string*) {
  if (!absl::SimpleAtoi(in, &val_out->a)) {
    return false;
  }
  return true;
}
std::string AbslUnparseFlag(const ConversionTestVal& val) {
  return absl::StrCat(val.a);
}

}  // namespace

// Flag default values can be specified with a value that converts to the flag
// value type implicitly.
ABSL_FLAG(ConversionTestVal, test_flag_implicit_conv,
          ConversionTestVal::ViaImplicitConv::kTen,
          "test flag init via implicit conversion");

namespace {

TEST_F(FlagTest, CanSetViaImplicitConversion) {
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_implicit_conv).a, 10);
  absl::SetFlag(&FLAGS_test_flag_implicit_conv,
                ConversionTestVal::ViaImplicitConv::kEleven);
  EXPECT_EQ(absl::GetFlag(FLAGS_test_flag_implicit_conv).a, 11);
}

// --------------------------------------------------------------------

struct NonDfltConstructible {
 public:
  // This constructor tests that we can initialize the flag with int value
  NonDfltConstructible(int i) : value(i) {}  // NOLINT

  // This constructor tests that we can't initialize the flag with char value
  // but can with explicitly constructed NonDfltConstructible.
  explicit NonDfltConstructible(char c) : value(100 + static_cast<int>(c)) {}

  int value;
};

bool AbslParseFlag(absl::string_view in, NonDfltConstructible* ndc_out,
                   std::string*) {
  return absl::SimpleAtoi(in, &ndc_out->value);
}
std::string AbslUnparseFlag(const NonDfltConstructible& ndc) {
  return absl::StrCat(ndc.value);
}

}  // namespace

ABSL_FLAG(NonDfltConstructible, ndc_flag1, NonDfltConstructible('1'),
          "Flag with non default constructible type");
ABSL_FLAG(NonDfltConstructible, ndc_flag2, 0,
          "Flag with non default constructible type");

namespace {

TEST_F(FlagTest, TestNonDefaultConstructibleType) {
  EXPECT_EQ(absl::GetFlag(FLAGS_ndc_flag1).value, '1' + 100);
  EXPECT_EQ(absl::GetFlag(FLAGS_ndc_flag2).value, 0);

  absl::SetFlag(&FLAGS_ndc_flag1, NonDfltConstructible('A'));
  absl::SetFlag(&FLAGS_ndc_flag2, 25);

  EXPECT_EQ(absl::GetFlag(FLAGS_ndc_flag1).value, 'A' + 100);
  EXPECT_EQ(absl::GetFlag(FLAGS_ndc_flag2).value, 25);
}

}  // namespace

// --------------------------------------------------------------------

ABSL_RETIRED_FLAG(bool, old_bool_flag, true, "old descr");
ABSL_RETIRED_FLAG(int, old_int_flag, (int)std::sqrt(10), "old descr");
ABSL_RETIRED_FLAG(std::string, old_str_flag, "", absl::StrCat("old ", "descr"));

namespace {

bool initialization_order_fiasco_test ABSL_ATTRIBUTE_UNUSED = [] {
  // Iterate over all the flags during static initialization.
  // This should not trigger ASan's initialization-order-fiasco.
  auto* handle1 = absl::FindCommandLineFlag("flag_on_separate_file");
  auto* handle2 = absl::FindCommandLineFlag("retired_flag_on_separate_file");
  if (handle1 != nullptr && handle2 != nullptr) {
    return handle1->Name() == handle2->Name();
  }
  return true;
}();

TEST_F(FlagTest, TestRetiredFlagRegistration) {
  auto* handle = absl::FindCommandLineFlag("old_bool_flag");
  EXPECT_TRUE(handle->IsOfType<bool>());
  EXPECT_TRUE(handle->IsRetired());
  handle = absl::FindCommandLineFlag("old_int_flag");
  EXPECT_TRUE(handle->IsOfType<int>());
  EXPECT_TRUE(handle->IsRetired());
  handle = absl::FindCommandLineFlag("old_str_flag");
  EXPECT_TRUE(handle->IsOfType<std::string>());
  EXPECT_TRUE(handle->IsRetired());
}

}  // namespace

// --------------------------------------------------------------------

namespace {

// User-defined type with small alignment, but size exceeding 16.
struct SmallAlignUDT {
  SmallAlignUDT() : c('A'), s(12) {}
  char c;
  int16_t s;
  char bytes[14];
};

bool AbslParseFlag(absl::string_view, SmallAlignUDT*, std::string*) {
  return true;
}
std::string AbslUnparseFlag(const SmallAlignUDT&) { return ""; }

}  // namespace

ABSL_FLAG(SmallAlignUDT, test_flag_sa_udt, {}, "help");

namespace {

TEST_F(FlagTest, TestSmallAlignUDT) {
  EXPECT_EQ(flags::StorageKind<SmallAlignUDT>(),
            flags::FlagValueStorageKind::kSequenceLocked);
  SmallAlignUDT value = absl::GetFlag(FLAGS_test_flag_sa_udt);
  EXPECT_EQ(value.c, 'A');
  EXPECT_EQ(value.s, 12);

  value.c = 'B';
  value.s = 45;
  absl::SetFlag(&FLAGS_test_flag_sa_udt, value);
  value = absl::GetFlag(FLAGS_test_flag_sa_udt);
  EXPECT_EQ(value.c, 'B');
  EXPECT_EQ(value.s, 45);
}
}  // namespace

// --------------------------------------------------------------------

namespace {

// User-defined not trivially copyable type.
template <int id>
struct NonTriviallyCopyableUDT {
  NonTriviallyCopyableUDT() : c('A') { s_num_instance++; }
  NonTriviallyCopyableUDT(const NonTriviallyCopyableUDT& rhs) : c(rhs.c) {
    s_num_instance++;
  }
  NonTriviallyCopyableUDT& operator=(const NonTriviallyCopyableUDT& rhs) {
    c = rhs.c;
    return *this;
  }
  ~NonTriviallyCopyableUDT() { s_num_instance--; }

  static uint64_t s_num_instance;
  char c;
};

template <int id>
uint64_t NonTriviallyCopyableUDT<id>::s_num_instance = 0;

template <int id>
bool AbslParseFlag(absl::string_view txt, NonTriviallyCopyableUDT<id>* f,
                   std::string*) {
  f->c = txt.empty() ? '\0' : txt[0];
  return true;
}
template <int id>
std::string AbslUnparseFlag(const NonTriviallyCopyableUDT<id>&) {
  return "";
}

template <int id, typename F>
void TestExpectedLeaks(
    F&& f, uint64_t num_leaks,
    absl::optional<uint64_t> num_new_instances = absl::nullopt) {
  if (!num_new_instances.has_value()) num_new_instances = num_leaks;

  auto num_leaked_before = flags::NumLeakedFlagValues();
  auto num_instances_before = NonTriviallyCopyableUDT<id>::s_num_instance;
  f();
  EXPECT_EQ(num_leaked_before + num_leaks, flags::NumLeakedFlagValues());
  EXPECT_EQ(num_instances_before + num_new_instances.value(),
            NonTriviallyCopyableUDT<id>::s_num_instance);
}
}  // namespace

ABSL_FLAG(NonTriviallyCopyableUDT<1>, test_flag_ntc_udt1, {}, "help");
ABSL_FLAG(NonTriviallyCopyableUDT<2>, test_flag_ntc_udt2, {}, "help");
ABSL_FLAG(NonTriviallyCopyableUDT<3>, test_flag_ntc_udt3, {}, "help");
ABSL_FLAG(NonTriviallyCopyableUDT<4>, test_flag_ntc_udt4, {}, "help");
ABSL_FLAG(NonTriviallyCopyableUDT<5>, test_flag_ntc_udt5, {}, "help");

namespace {

TEST_F(FlagTest, TestNonTriviallyCopyableGetSetSet) {
  EXPECT_EQ(flags::StorageKind<NonTriviallyCopyableUDT<1>>(),
            flags::FlagValueStorageKind::kHeapAllocated);

  TestExpectedLeaks<1>(
      [&] {
        NonTriviallyCopyableUDT<1> value =
            absl::GetFlag(FLAGS_test_flag_ntc_udt1);
        EXPECT_EQ(value.c, 'A');
      },
      0);

  TestExpectedLeaks<1>(
      [&] {
        NonTriviallyCopyableUDT<1> value;
        value.c = 'B';
        absl::SetFlag(&FLAGS_test_flag_ntc_udt1, value);
        EXPECT_EQ(value.c, 'B');
      },
      1);

  TestExpectedLeaks<1>(
      [&] {
        NonTriviallyCopyableUDT<1> value;
        value.c = 'C';
        absl::SetFlag(&FLAGS_test_flag_ntc_udt1, value);
      },
      0);
}

TEST_F(FlagTest, TestNonTriviallyCopyableParseSet) {
  TestExpectedLeaks<2>(
      [&] {
        const char* in_argv[] = {"testbin", "--test_flag_ntc_udt2=A"};
        absl::ParseCommandLine(2, const_cast<char**>(in_argv));
      },
      0);

  TestExpectedLeaks<2>(
      [&] {
        NonTriviallyCopyableUDT<2> value;
        value.c = 'B';
        absl::SetFlag(&FLAGS_test_flag_ntc_udt2, value);
        EXPECT_EQ(value.c, 'B');
      },
      0);
}

TEST_F(FlagTest, TestNonTriviallyCopyableSet) {
  TestExpectedLeaks<3>(
      [&] {
        NonTriviallyCopyableUDT<3> value;
        value.c = 'B';
        absl::SetFlag(&FLAGS_test_flag_ntc_udt3, value);
        EXPECT_EQ(value.c, 'B');
      },
      0);
}

// One new instance created during initialization and stored in the flag.
auto premain_utd4_get =
    (TestExpectedLeaks<4>([] { (void)absl::GetFlag(FLAGS_test_flag_ntc_udt4); },
                          0, 1),
     false);

TEST_F(FlagTest, TestNonTriviallyCopyableGetBeforeMainParseGet) {
  TestExpectedLeaks<4>(
      [&] {
        const char* in_argv[] = {"testbin", "--test_flag_ntc_udt4=C"};
        absl::ParseCommandLine(2, const_cast<char**>(in_argv));
      },
      1);

  TestExpectedLeaks<4>(
      [&] {
        NonTriviallyCopyableUDT<4> value =
            absl::GetFlag(FLAGS_test_flag_ntc_udt4);
        EXPECT_EQ(value.c, 'C');
      },
      0);
}

// One new instance created during initialization, which is reused since it was
// never read.
auto premain_utd5_set = (TestExpectedLeaks<5>(
                             [] {
                               NonTriviallyCopyableUDT<5> value;
                               value.c = 'B';
                               absl::SetFlag(&FLAGS_test_flag_ntc_udt5, value);
                             },
                             0, 1),
                         false);

TEST_F(FlagTest, TestNonTriviallyCopyableSetParseGet) {
  TestExpectedLeaks<5>(
      [&] {
        const char* in_argv[] = {"testbin", "--test_flag_ntc_udt5=C"};
        absl::ParseCommandLine(2, const_cast<char**>(in_argv));
      },
      0);

  TestExpectedLeaks<5>(
      [&] {
        NonTriviallyCopyableUDT<5> value =
            absl::GetFlag(FLAGS_test_flag_ntc_udt5);
        EXPECT_EQ(value.c, 'C');
      },
      0);
}

}  // namespace

// --------------------------------------------------------------------

namespace {

enum TestE { A = 1, B = 2, C = 3 };

struct EnumWrapper {
  EnumWrapper() : e(A) {}

  TestE e;
};

bool AbslParseFlag(absl::string_view, EnumWrapper*, std::string*) {
  return true;
}
std::string AbslUnparseFlag(const EnumWrapper&) { return ""; }

}  // namespace

ABSL_FLAG(EnumWrapper, test_enum_wrapper_flag, {}, "help");

TEST_F(FlagTest, TesTypeWrappingEnum) {
  EnumWrapper value = absl::GetFlag(FLAGS_test_enum_wrapper_flag);
  EXPECT_EQ(value.e, A);

  value.e = B;
  absl::SetFlag(&FLAGS_test_enum_wrapper_flag, value);
  value = absl::GetFlag(FLAGS_test_enum_wrapper_flag);
  EXPECT_EQ(value.e, B);
}

// This is a compile test to ensure macros are expanded within ABSL_FLAG and
// ABSL_DECLARE_FLAG.
#define FLAG_NAME_MACRO(name) prefix_##name
ABSL_DECLARE_FLAG(int, FLAG_NAME_MACRO(test_macro_named_flag));
ABSL_FLAG(int, FLAG_NAME_MACRO(test_macro_named_flag), 0,
          "Testing macro expansion within ABSL_FLAG");

TEST_F(FlagTest, MacroWithinAbslFlag) {
  EXPECT_EQ(absl::GetFlag(FLAGS_prefix_test_macro_named_flag), 0);
  absl::SetFlag(&FLAGS_prefix_test_macro_named_flag, 1);
  EXPECT_EQ(absl::GetFlag(FLAGS_prefix_test_macro_named_flag), 1);
}

// --------------------------------------------------------------------

ABSL_FLAG(absl::optional<bool>, optional_bool, absl::nullopt, "help");
ABSL_FLAG(absl::optional<int>, optional_int, {}, "help");
ABSL_FLAG(absl::optional<double>, optional_double, 9.3, "help");
ABSL_FLAG(absl::optional<std::string>, optional_string, absl::nullopt, "help");
ABSL_FLAG(absl::optional<absl::Duration>, optional_duration, absl::nullopt,
          "help");
ABSL_FLAG(absl::optional<absl::optional<int>>, optional_optional_int,
          absl::nullopt, "help");
#if defined(ABSL_HAVE_STD_OPTIONAL) && !defined(ABSL_USES_STD_OPTIONAL)
ABSL_FLAG(std::optional<int64_t>, std_optional_int64, std::nullopt, "help");
#endif

namespace {

TEST_F(FlagTest, TestOptionalBool) {
  EXPECT_FALSE(absl::GetFlag(FLAGS_optional_bool).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_bool), absl::nullopt);

  absl::SetFlag(&FLAGS_optional_bool, false);
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_bool).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_bool), false);

  absl::SetFlag(&FLAGS_optional_bool, true);
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_bool).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_bool), true);

  absl::SetFlag(&FLAGS_optional_bool, absl::nullopt);
  EXPECT_FALSE(absl::GetFlag(FLAGS_optional_bool).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_bool), absl::nullopt);
}

// --------------------------------------------------------------------

TEST_F(FlagTest, TestOptionalInt) {
  EXPECT_FALSE(absl::GetFlag(FLAGS_optional_int).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_int), absl::nullopt);

  absl::SetFlag(&FLAGS_optional_int, 0);
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_int).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_int), 0);

  absl::SetFlag(&FLAGS_optional_int, 10);
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_int).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_int), 10);

  absl::SetFlag(&FLAGS_optional_int, absl::nullopt);
  EXPECT_FALSE(absl::GetFlag(FLAGS_optional_int).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_int), absl::nullopt);
}

// --------------------------------------------------------------------

TEST_F(FlagTest, TestOptionalDouble) {
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_double).has_value());
  EXPECT_DOUBLE_EQ(*absl::GetFlag(FLAGS_optional_double), 9.3);

  absl::SetFlag(&FLAGS_optional_double, 0.0);
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_double).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_double), 0.0);

  absl::SetFlag(&FLAGS_optional_double, 1.234);
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_double).has_value());
  EXPECT_DOUBLE_EQ(*absl::GetFlag(FLAGS_optional_double), 1.234);

  absl::SetFlag(&FLAGS_optional_double, absl::nullopt);
  EXPECT_FALSE(absl::GetFlag(FLAGS_optional_double).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_double), absl::nullopt);
}

// --------------------------------------------------------------------

TEST_F(FlagTest, TestOptionalString) {
  EXPECT_FALSE(absl::GetFlag(FLAGS_optional_string).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_string), absl::nullopt);

  // Setting optional string to "" leads to undefined behavior.

  absl::SetFlag(&FLAGS_optional_string, " ");
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_string).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_string), " ");

  absl::SetFlag(&FLAGS_optional_string, "QWERTY");
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_string).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_string), "QWERTY");

  absl::SetFlag(&FLAGS_optional_string, absl::nullopt);
  EXPECT_FALSE(absl::GetFlag(FLAGS_optional_string).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_string), absl::nullopt);
}

// --------------------------------------------------------------------

TEST_F(FlagTest, TestOptionalDuration) {
  EXPECT_FALSE(absl::GetFlag(FLAGS_optional_duration).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_duration), absl::nullopt);

  absl::SetFlag(&FLAGS_optional_duration, absl::ZeroDuration());
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_duration).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_duration), absl::Seconds(0));

  absl::SetFlag(&FLAGS_optional_duration, absl::Hours(3));
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_duration).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_duration), absl::Hours(3));

  absl::SetFlag(&FLAGS_optional_duration, absl::nullopt);
  EXPECT_FALSE(absl::GetFlag(FLAGS_optional_duration).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_duration), absl::nullopt);
}

// --------------------------------------------------------------------

TEST_F(FlagTest, TestOptionalOptional) {
  EXPECT_FALSE(absl::GetFlag(FLAGS_optional_optional_int).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_optional_int), absl::nullopt);

  absl::optional<int> nullint{absl::nullopt};

  absl::SetFlag(&FLAGS_optional_optional_int, nullint);
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_optional_int).has_value());
  EXPECT_NE(absl::GetFlag(FLAGS_optional_optional_int), nullint);
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_optional_int),
            absl::optional<absl::optional<int>>{nullint});

  absl::SetFlag(&FLAGS_optional_optional_int, 0);
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_optional_int).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_optional_int), 0);

  absl::SetFlag(&FLAGS_optional_optional_int, absl::optional<int>{0});
  EXPECT_TRUE(absl::GetFlag(FLAGS_optional_optional_int).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_optional_int), 0);
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_optional_int), absl::optional<int>{0});

  absl::SetFlag(&FLAGS_optional_optional_int, absl::nullopt);
  EXPECT_FALSE(absl::GetFlag(FLAGS_optional_optional_int).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_optional_optional_int), absl::nullopt);
}

// --------------------------------------------------------------------

#if defined(ABSL_HAVE_STD_OPTIONAL) && !defined(ABSL_USES_STD_OPTIONAL)

TEST_F(FlagTest, TestStdOptional) {
  EXPECT_FALSE(absl::GetFlag(FLAGS_std_optional_int64).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_std_optional_int64), std::nullopt);

  absl::SetFlag(&FLAGS_std_optional_int64, 0);
  EXPECT_TRUE(absl::GetFlag(FLAGS_std_optional_int64).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_std_optional_int64), 0);

  absl::SetFlag(&FLAGS_std_optional_int64, 0xFFFFFFFFFF16);
  EXPECT_TRUE(absl::GetFlag(FLAGS_std_optional_int64).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_std_optional_int64), 0xFFFFFFFFFF16);

  absl::SetFlag(&FLAGS_std_optional_int64, std::nullopt);
  EXPECT_FALSE(absl::GetFlag(FLAGS_std_optional_int64).has_value());
  EXPECT_EQ(absl::GetFlag(FLAGS_std_optional_int64), std::nullopt);
}

// --------------------------------------------------------------------

#endif

}  // namespace
