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

#include <assert.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <thread>  // NOLINT
#include <type_traits>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/strings/ascii.h"
#include "absl/strings/internal/str_format/arg.h"
#include "absl/strings/internal/str_format/bind.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

#if defined(ABSL_HAVE_STD_STRING_VIEW)
#include <string_view>
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace str_format_internal {
namespace {

struct NativePrintfTraits {
  bool hex_float_has_glibc_rounding;
  bool hex_float_prefers_denormal_repr;
  bool hex_float_uses_minimal_precision_when_not_specified;
  bool hex_float_optimizes_leading_digit_bit_count;
};

template <typename T, size_t N>
size_t ArraySize(T (&)[N]) {
  return N;
}

template <typename T>
struct AlwaysFalse : std::false_type {};

template <typename T>
std::string LengthModFor() {
  static_assert(AlwaysFalse<T>::value, "Unsupported type");
  return "";
}
template <>
std::string LengthModFor<char>() {
  return "hh";
}
template <>
std::string LengthModFor<signed char>() {
  return "hh";
}
template <>
std::string LengthModFor<unsigned char>() {
  return "hh";
}
template <>
std::string LengthModFor<short>() {  // NOLINT
  return "h";
}
template <>
std::string LengthModFor<unsigned short>() {  // NOLINT
  return "h";
}
template <>
std::string LengthModFor<int>() {
  return "";
}
template <>
std::string LengthModFor<unsigned>() {
  return "";
}
template <>
std::string LengthModFor<long>() {  // NOLINT
  return "l";
}
template <>
std::string LengthModFor<unsigned long>() {  // NOLINT
  return "l";
}
template <>
std::string LengthModFor<long long>() {  // NOLINT
  return "ll";
}
template <>
std::string LengthModFor<unsigned long long>() {  // NOLINT
  return "ll";
}

// An integral type of the same rank and signedness as `wchar_t`, that isn't
// `wchar_t`.
using IntegralTypeForWCharT =
    std::conditional_t<std::is_signed<wchar_t>::value,
                       // Some STLs are broken and return `wchar_t` from
                       // `std::make_[un]signed_t<wchar_t>` when the signedness
                       // matches. Work around by round-tripping through the
                       // opposite signedness.
                       std::make_signed_t<std::make_unsigned_t<wchar_t>>,
                       std::make_unsigned_t<std::make_signed_t<wchar_t>>>;

// Given an integral type `T`, returns a type of the same rank and signedness
// that is guaranteed to not be `wchar_t`.
template <typename T>
using MatchingIntegralType = std::conditional_t<std::is_same<T, wchar_t>::value,
                                                IntegralTypeForWCharT, T>;

std::string EscCharImpl(int v) {
  char buf[64];
  int n = absl::ascii_isprint(static_cast<unsigned char>(v))
              ? snprintf(buf, sizeof(buf), "'%c'", v)
              : snprintf(buf, sizeof(buf), "'\\x%.*x'", CHAR_BIT / 4,
                         static_cast<unsigned>(
                             static_cast<std::make_unsigned_t<char>>(v)));
  assert(n > 0 && static_cast<size_t>(n) < sizeof(buf));
  return std::string(buf, static_cast<size_t>(n));
}

std::string Esc(char v) { return EscCharImpl(v); }
std::string Esc(signed char v) { return EscCharImpl(v); }
std::string Esc(unsigned char v) { return EscCharImpl(v); }

std::string Esc(wchar_t v) {
  char buf[64];
  int n = std::iswprint(static_cast<wint_t>(v))
              ? snprintf(buf, sizeof(buf), "L'%lc'", static_cast<wint_t>(v))
              : snprintf(buf, sizeof(buf), "L'\\x%.*llx'",
                         static_cast<int>(sizeof(wchar_t) * CHAR_BIT / 4),
                         static_cast<unsigned long long>(
                             static_cast<std::make_unsigned_t<wchar_t>>(v)));
  assert(n > 0 && static_cast<size_t>(n) < sizeof(buf));
  return std::string(buf, static_cast<size_t>(n));
}

template <typename T>
std::string Esc(const T &v) {
  std::ostringstream oss;
  oss << v;
  return oss.str();
}

void StrAppendV(std::string *dst, const char *format, va_list ap) {
  // First try with a small fixed size buffer
  static const int kSpaceLength = 1024;
  char space[kSpaceLength];

  // It's possible for methods that use a va_list to invalidate
  // the data in it upon use.  The fix is to make a copy
  // of the structure before using it and use that copy instead.
  va_list backup_ap;
  va_copy(backup_ap, ap);
  int result = vsnprintf(space, kSpaceLength, format, backup_ap);
  va_end(backup_ap);
  if (result < kSpaceLength) {
    if (result >= 0) {
      // Normal case -- everything fit.
      dst->append(space, static_cast<size_t>(result));
      return;
    }
    if (result < 0) {
      // Just an error.
      return;
    }
  }

  // Increase the buffer size to the size requested by vsnprintf,
  // plus one for the closing \0.
  size_t length = static_cast<size_t>(result) + 1;
  char *buf = new char[length];

  // Restore the va_list before we use it again
  va_copy(backup_ap, ap);
  result = vsnprintf(buf, length, format, backup_ap);
  va_end(backup_ap);

  if (result >= 0 && static_cast<size_t>(result) < length) {
    // It fit
    dst->append(buf, static_cast<size_t>(result));
  }
  delete[] buf;
}

void StrAppend(std::string *, const char *, ...) ABSL_PRINTF_ATTRIBUTE(2, 3);
void StrAppend(std::string *out, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  StrAppendV(out, format, ap);
  va_end(ap);
}

std::string StrPrint(const char *, ...) ABSL_PRINTF_ATTRIBUTE(1, 2);
std::string StrPrint(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  std::string result;
  StrAppendV(&result, format, ap);
  va_end(ap);
  return result;
}

NativePrintfTraits VerifyNativeImplementationImpl() {
  NativePrintfTraits result;

  // >>> hex_float_has_glibc_rounding. To have glibc's rounding behavior we need
  // to meet three requirements:
  //
  //   - The threshold for rounding up is 8 (for e.g. MSVC uses 9).
  //   - If the digits lower than than the 8 are non-zero then we round up.
  //   - If the digits lower than the 8 are all zero then we round toward even.
  //
  // The numbers below represent all the cases covering {below,at,above} the
  // threshold (8) with both {zero,non-zero} lower bits and both {even,odd}
  // preceding digits.
  const double d0079 = 65657.0;  // 0x1.0079p+16
  const double d0179 = 65913.0;  // 0x1.0179p+16
  const double d0080 = 65664.0;  // 0x1.0080p+16
  const double d0180 = 65920.0;  // 0x1.0180p+16
  const double d0081 = 65665.0;  // 0x1.0081p+16
  const double d0181 = 65921.0;  // 0x1.0181p+16
  result.hex_float_has_glibc_rounding =
      StartsWith(StrPrint("%.2a", d0079), "0x1.00") &&
      StartsWith(StrPrint("%.2a", d0179), "0x1.01") &&
      StartsWith(StrPrint("%.2a", d0080), "0x1.00") &&
      StartsWith(StrPrint("%.2a", d0180), "0x1.02") &&
      StartsWith(StrPrint("%.2a", d0081), "0x1.01") &&
      StartsWith(StrPrint("%.2a", d0181), "0x1.02");

  // >>> hex_float_prefers_denormal_repr. Formatting `denormal` on glibc yields
  // "0x0.0000000000001p-1022", whereas on std libs that don't use denormal
  // representation it would either be 0x1p-1074 or 0x1.0000000000000-1074.
  const double denormal = std::numeric_limits<double>::denorm_min();
  result.hex_float_prefers_denormal_repr =
      StartsWith(StrPrint("%a", denormal), "0x0.0000000000001");

  // >>> hex_float_uses_minimal_precision_when_not_specified. Some (non-glibc)
  // libs will format the following as "0x1.0079000000000p+16".
  result.hex_float_uses_minimal_precision_when_not_specified =
      (StrPrint("%a", d0079) == "0x1.0079p+16");

  // >>> hex_float_optimizes_leading_digit_bit_count. The number 1.5, when
  // formatted by glibc should yield "0x1.8p+0" for `double` and "0xcp-3" for
  // `long double`, i.e., number of bits in the leading digit is adapted to the
  // number of bits in the mantissa.
  const double d_15 = 1.5;
  const long double ld_15 = 1.5;
  result.hex_float_optimizes_leading_digit_bit_count =
      StartsWith(StrPrint("%a", d_15), "0x1.8") &&
      StartsWith(StrPrint("%La", ld_15), "0xc");

  return result;
}

const NativePrintfTraits &VerifyNativeImplementation() {
  static NativePrintfTraits native_traits = VerifyNativeImplementationImpl();
  return native_traits;
}

class FormatConvertTest : public ::testing::Test { };

template <typename T>
void TestStringConvert(const T& str) {
  const FormatArgImpl args[] = {FormatArgImpl(str)};
  struct Expectation {
    const char *out;
    const char *fmt;
  };
  const Expectation kExpect[] = {
    {"hello",  "%1$s"      },
    {"",       "%1$.s"     },
    {"",       "%1$.0s"    },
    {"h",      "%1$.1s"    },
    {"he",     "%1$.2s"    },
    {"hello",  "%1$.10s"   },
    {" hello", "%1$6s"     },
    {"   he",  "%1$5.2s"   },
    {"he   ",  "%1$-5.2s"  },
    {"hello ", "%1$-6.10s" },
  };
  for (const Expectation &e : kExpect) {
    UntypedFormatSpecImpl format(e.fmt);
    EXPECT_EQ(e.out, FormatPack(format, absl::MakeSpan(args)));
  }
}

TEST_F(FormatConvertTest, BasicString) {
  TestStringConvert("hello");  // As char array.
  TestStringConvert(L"hello");
  TestStringConvert(static_cast<const char*>("hello"));
  TestStringConvert(static_cast<const wchar_t*>(L"hello"));
  TestStringConvert(std::string("hello"));
  TestStringConvert(std::wstring(L"hello"));
  TestStringConvert(string_view("hello"));
#if defined(ABSL_HAVE_STD_STRING_VIEW)
  TestStringConvert(std::string_view("hello"));
  TestStringConvert(std::wstring_view(L"hello"));
#endif  // ABSL_HAVE_STD_STRING_VIEW
}

TEST_F(FormatConvertTest, NullString) {
  const char* p = nullptr;
  UntypedFormatSpecImpl format("%s");
  EXPECT_EQ("", FormatPack(format, {FormatArgImpl(p)}));

  const wchar_t* wp = nullptr;
  UntypedFormatSpecImpl wformat("%ls");
  EXPECT_EQ("", FormatPack(wformat, {FormatArgImpl(wp)}));
}

TEST_F(FormatConvertTest, StringPrecision) {
  // We cap at the precision.
  char c = 'a';
  const char* p = &c;
  UntypedFormatSpecImpl format("%.1s");
  EXPECT_EQ("a", FormatPack(format, {FormatArgImpl(p)}));

  wchar_t wc = L'a';
  const wchar_t* wp = &wc;
  UntypedFormatSpecImpl wformat("%.1ls");
  EXPECT_EQ("a", FormatPack(wformat, {FormatArgImpl(wp)}));

  // We cap at the NUL-terminator.
  p = "ABC";
  UntypedFormatSpecImpl format2("%.10s");
  EXPECT_EQ("ABC", FormatPack(format2, {FormatArgImpl(p)}));

  wp = L"ABC";
  UntypedFormatSpecImpl wformat2("%.10ls");
  EXPECT_EQ("ABC", FormatPack(wformat2, {FormatArgImpl(wp)}));
}

// Pointer formatting is implementation defined. This checks that the argument
// can be matched to `ptr`.
MATCHER_P(MatchesPointerString, ptr, "") {
  if (ptr == nullptr && arg == "(nil)") {
    return true;
  }
  void* parsed = nullptr;
  if (sscanf(arg.c_str(), "%p", &parsed) != 1) {
    LOG(FATAL) << "Could not parse " << arg;
  }
  return ptr == parsed;
}

TEST_F(FormatConvertTest, Pointer) {
  static int x = 0;
  const int *xp = &x;
  char c = 'h';
  char *mcp = &c;
  const char *cp = "hi";
  const char *cnil = nullptr;
  wchar_t wc = L'h';
  wchar_t *mwcp = &wc;
  const wchar_t *wcp = L"hi";
  const wchar_t *wcnil = nullptr;
  const int *inil = nullptr;
  using VoidF = void (*)();
  VoidF fp = [] {}, fnil = nullptr;
  volatile char vc;
  volatile char *vcp = &vc;
  volatile char *vcnil = nullptr;
  volatile wchar_t vwc;
  volatile wchar_t *vwcp = &vwc;
  volatile wchar_t *vwcnil = nullptr;
  const FormatArgImpl args_array[] = {
      FormatArgImpl(xp),    FormatArgImpl(cp),     FormatArgImpl(wcp),
      FormatArgImpl(inil),  FormatArgImpl(cnil),   FormatArgImpl(wcnil),
      FormatArgImpl(mcp),   FormatArgImpl(mwcp),   FormatArgImpl(fp),
      FormatArgImpl(fnil),  FormatArgImpl(vcp),    FormatArgImpl(vwcp),
      FormatArgImpl(vcnil), FormatArgImpl(vwcnil),
  };
  auto args = absl::MakeConstSpan(args_array);

  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%20p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%.1p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%.20p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%30.20p"), args),
              MatchesPointerString(&x));

  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%-p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%-20p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%-.1p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%.20p"), args),
              MatchesPointerString(&x));
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%-30.20p"), args),
              MatchesPointerString(&x));

  // const int*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%1$p"), args),
              MatchesPointerString(xp));
  // const char*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%2$p"), args),
              MatchesPointerString(cp));
  // const wchar_t*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%3$p"), args),
              MatchesPointerString(wcp));
  // null const int*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%4$p"), args),
              MatchesPointerString(nullptr));
  // null const char*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%5$p"), args),
              MatchesPointerString(nullptr));
  // null const wchar_t*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%6$p"), args),
              MatchesPointerString(nullptr));
  // nonconst char*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%7$p"), args),
              MatchesPointerString(mcp));
  // nonconst wchar_t*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%8$p"), args),
              MatchesPointerString(mwcp));
  // function pointer
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%9$p"), args),
              MatchesPointerString(reinterpret_cast<const void *>(fp)));
  // null function pointer
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%10$p"), args),
              MatchesPointerString(nullptr));
  // volatile char*
  EXPECT_THAT(
      FormatPack(UntypedFormatSpecImpl("%11$p"), args),
      MatchesPointerString(reinterpret_cast<volatile const void *>(vcp)));
  // volatile wchar_t*
  EXPECT_THAT(
      FormatPack(UntypedFormatSpecImpl("%12$p"), args),
      MatchesPointerString(reinterpret_cast<volatile const void *>(vwcp)));
  // null volatile char*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%13$p"), args),
              MatchesPointerString(nullptr));
  // null volatile wchar_t*
  EXPECT_THAT(FormatPack(UntypedFormatSpecImpl("%14$p"), args),
              MatchesPointerString(nullptr));
}

struct Cardinal {
  enum Pos { k1 = 1, k2 = 2, k3 = 3 };
  enum Neg { kM1 = -1, kM2 = -2, kM3 = -3 };
};

TEST_F(FormatConvertTest, Enum) {
  const Cardinal::Pos k3 = Cardinal::k3;
  const Cardinal::Neg km3 = Cardinal::kM3;
  const FormatArgImpl args[] = {FormatArgImpl(k3), FormatArgImpl(km3)};
  UntypedFormatSpecImpl format("%1$d");
  UntypedFormatSpecImpl format2("%2$d");
  EXPECT_EQ("3", FormatPack(format, absl::MakeSpan(args)));
  EXPECT_EQ("-3", FormatPack(format2, absl::MakeSpan(args)));
}

template <typename T>
class TypedFormatConvertTest : public FormatConvertTest { };

TYPED_TEST_SUITE_P(TypedFormatConvertTest);

std::vector<std::string> AllFlagCombinations() {
  const char kFlags[] = {'-', '#', '0', '+', ' '};
  std::vector<std::string> result;
  for (size_t fsi = 0; fsi < (1ull << ArraySize(kFlags)); ++fsi) {
    std::string flag_set;
    for (size_t fi = 0; fi < ArraySize(kFlags); ++fi)
      if (fsi & (1ull << fi))
        flag_set += kFlags[fi];
    result.push_back(flag_set);
  }
  return result;
}

TYPED_TEST_P(TypedFormatConvertTest, AllIntsWithFlags) {
  typedef TypeParam T;
  typedef typename std::make_unsigned<T>::type UnsignedT;
  using remove_volatile_t = typename std::remove_volatile<T>::type;
  const T kMin = std::numeric_limits<remove_volatile_t>::min();
  const T kMax = std::numeric_limits<remove_volatile_t>::max();
  const T kVals[] = {
      remove_volatile_t(1),
      remove_volatile_t(2),
      remove_volatile_t(3),
      remove_volatile_t(123),
      remove_volatile_t(-1),
      remove_volatile_t(-2),
      remove_volatile_t(-3),
      remove_volatile_t(-123),
      remove_volatile_t(0),
      kMax - remove_volatile_t(1),
      kMax,
      kMin + remove_volatile_t(1),
      kMin,
  };
  const char kConvChars[] = {'d', 'i', 'u', 'o', 'x', 'X'};
  const std::string kWid[] = {"", "4", "10"};
  const std::string kPrec[] = {"", ".", ".0", ".4", ".10"};

  const std::vector<std::string> flag_sets = AllFlagCombinations();

  for (size_t vi = 0; vi < ArraySize(kVals); ++vi) {
    const T val = kVals[vi];
    SCOPED_TRACE(Esc(val));
    const FormatArgImpl args[] = {FormatArgImpl(val)};
    for (size_t ci = 0; ci < ArraySize(kConvChars); ++ci) {
      const char conv_char = kConvChars[ci];
      for (size_t fsi = 0; fsi < flag_sets.size(); ++fsi) {
        const std::string &flag_set = flag_sets[fsi];
        for (size_t wi = 0; wi < ArraySize(kWid); ++wi) {
          const std::string &wid = kWid[wi];
          for (size_t pi = 0; pi < ArraySize(kPrec); ++pi) {
            const std::string &prec = kPrec[pi];

            const bool is_signed_conv = (conv_char == 'd' || conv_char == 'i');
            const bool is_unsigned_to_signed =
                !std::is_signed<T>::value && is_signed_conv;
            // Don't consider sign-related flags '+' and ' ' when doing
            // unsigned to signed conversions.
            if (is_unsigned_to_signed &&
                flag_set.find_first_of("+ ") != std::string::npos) {
              continue;
            }

            std::string new_fmt("%");
            new_fmt += flag_set;
            new_fmt += wid;
            new_fmt += prec;
            // old and new always agree up to here.
            std::string old_fmt = new_fmt;
            new_fmt += conv_char;
            std::string old_result;
            if (is_unsigned_to_signed) {
              // don't expect agreement on unsigned formatted as signed,
              // as printf can't do that conversion properly. For those
              // cases, we do expect agreement with printf with a "%u"
              // and the unsigned equivalent of 'val'.
              UnsignedT uval =
                  static_cast<std::remove_volatile_t<UnsignedT>>(val);
              old_fmt += LengthModFor<
                  MatchingIntegralType<std::remove_cv_t<decltype(uval)>>>();
              old_fmt += "u";
              old_result = StrPrint(old_fmt.c_str(), uval);
            } else {
              old_fmt += LengthModFor<
                  MatchingIntegralType<std::remove_cv_t<decltype(val)>>>();
              old_fmt += conv_char;
              old_result = StrPrint(old_fmt.c_str(), val);
            }

            SCOPED_TRACE(std::string() + " old_fmt: \"" + old_fmt +
                         "\"'"
                         " new_fmt: \"" +
                         new_fmt + "\"");
            UntypedFormatSpecImpl format(new_fmt);
            EXPECT_EQ(old_result, FormatPack(format, absl::MakeSpan(args)));
          }
        }
      }
    }
  }
}

template <typename T>
absl::optional<std::string> StrPrintChar(T c) {
  return StrPrint("%c", static_cast<int>(c));
}
template <>
absl::optional<std::string> StrPrintChar(wchar_t c) {
  // musl libc has a bug where ("%lc", 0) writes no characters, and Android
  // doesn't support forcing UTF-8 via setlocale(). Hardcode the expected
  // answers for ASCII inputs to maximize test coverage on these platforms.
  if (static_cast<std::make_unsigned_t<wchar_t>>(c) < 0x80) {
    return std::string(1, static_cast<char>(c));
  }

  // Force a UTF-8 locale to match the expected `StrFormat()` behavior.
  // It's important to copy the string returned by `old_locale` here, because
  // its contents are not guaranteed to be valid after the next `setlocale()`
  // call.
  std::string old_locale = setlocale(LC_CTYPE, nullptr);
  if (!setlocale(LC_CTYPE, "en_US.UTF-8")) {
    return absl::nullopt;
  }
  const std::string output = StrPrint("%lc", static_cast<wint_t>(c));
  setlocale(LC_CTYPE, old_locale.c_str());
  return output;
}

template <typename T>
typename std::remove_volatile<T>::type GetMaxForConversion() {
  return static_cast<typename std::remove_volatile<T>::type>(
      std::numeric_limits<int>::max());
}

template <>
wchar_t GetMaxForConversion<wchar_t>() {
  // Don't return values that aren't legal Unicode. For wchar_t conversions in a
  // UTF-8 locale, conversion behavior for such values is unspecified, and we
  // don't care about matching it.
  return (sizeof(wchar_t) * CHAR_BIT <= 16) ? wchar_t{0xffff}
                                            : static_cast<wchar_t>(0x10ffff);
}

TYPED_TEST_P(TypedFormatConvertTest, Char) {
  // Pass a bunch of values of type TypeParam to both FormatPack and libc's
  // vsnprintf("%c", ...) (wrapped in StrPrint) to make sure we get the same
  // value.
  typedef TypeParam T;
  using remove_volatile_t = typename std::remove_volatile<T>::type;
  std::vector<remove_volatile_t> vals = {
      remove_volatile_t(1),  remove_volatile_t(2),  remove_volatile_t(10),   //
      remove_volatile_t(-1), remove_volatile_t(-2), remove_volatile_t(-10),  //
      remove_volatile_t(0),
  };

  // We'd like to test values near std::numeric_limits::min() and
  // std::numeric_limits::max(), too, but vsnprintf("%c", ...) can't handle
  // anything larger than an int. Add in the most extreme values we can without
  // exceeding that range.
  // Special case: Formatting a wchar_t should behave like vsnprintf("%lc").
  // Technically vsnprintf can accept a wint_t in this case, but since we must
  // pass a wchar_t to FormatPack, the largest type we can use here is wchar_t.
  using ArgType =
      std::conditional_t<std::is_same<T, wchar_t>::value, wchar_t, int>;
  static const T kMin =
      static_cast<remove_volatile_t>(std::numeric_limits<ArgType>::min());
  static const T kMax = GetMaxForConversion<T>();
  vals.insert(vals.end(), {static_cast<remove_volatile_t>(kMin + 1), kMin,
                           static_cast<remove_volatile_t>(kMax - 1), kMax});

  static const auto kMaxWCharT =
      static_cast<remove_volatile_t>(GetMaxForConversion<wchar_t>());
  for (const T c : vals) {
    SCOPED_TRACE(Esc(c));
    const FormatArgImpl args[] = {FormatArgImpl(c)};
    UntypedFormatSpecImpl format("%c");
    absl::optional<std::string> result = StrPrintChar(c);
    if (result.has_value()) {
      EXPECT_EQ(result.value(), FormatPack(format, absl::MakeSpan(args)));
    }

    // Also test that if the format specifier is "%lc", the argument is treated
    // as if it's a `wchar_t`.
    const T wc =
        std::max(remove_volatile_t{0},
                 std::min(static_cast<remove_volatile_t>(c), kMaxWCharT));
    SCOPED_TRACE(Esc(wc));
    const FormatArgImpl wide_args[] = {FormatArgImpl(wc)};
    UntypedFormatSpecImpl wide_format("%lc");
    result = StrPrintChar(static_cast<wchar_t>(wc));
    if (result.has_value()) {
      EXPECT_EQ(result.value(),
                FormatPack(wide_format, absl::MakeSpan(wide_args)));
    }
  }
}

REGISTER_TYPED_TEST_SUITE_P(TypedFormatConvertTest, AllIntsWithFlags, Char);

typedef ::testing::Types<int, unsigned, volatile int, short,   // NOLINT
                         unsigned short, long, unsigned long,  // NOLINT
                         long long, unsigned long long,        // NOLINT
                         signed char, unsigned char, char, wchar_t>
    AllIntTypes;
INSTANTIATE_TYPED_TEST_SUITE_P(TypedFormatConvertTestWithAllIntTypes,
                               TypedFormatConvertTest, AllIntTypes);
TEST_F(FormatConvertTest, VectorBool) {
  // Make sure vector<bool>'s values behave as bools.
  std::vector<bool> v = {true, false};
  const std::vector<bool> cv = {true, false};
  EXPECT_EQ("1,0,1,0",
            FormatPack(UntypedFormatSpecImpl("%d,%d,%d,%d"),
                       absl::Span<const FormatArgImpl>(
                           {FormatArgImpl(v[0]), FormatArgImpl(v[1]),
                            FormatArgImpl(cv[0]), FormatArgImpl(cv[1])})));
}

TEST_F(FormatConvertTest, UnicodeWideString) {
  // StrFormat() should be able to convert wide strings containing Unicode
  // characters (to UTF-8).
  const FormatArgImpl args[] = {FormatArgImpl(L"\u47e3 \U00011112")};
  // `u8""` forces UTF-8 encoding; MSVC will default to e.g. CP1252 (and warn)
  // without it. However, the resulting character type differs between pre-C++20
  // (`char`) and C++20 (`char8_t`). So deduce the right character type for all
  // C++ versions, init it with UTF-8, then `memcpy()` to get the result as a
  // `char*`.
  using ConstChar8T = std::remove_reference_t<decltype(*u8"a")>;
  ConstChar8T kOutputUtf8[] = u8"\u47e3 \U00011112";
  char output[sizeof kOutputUtf8];
  std::memcpy(output, kOutputUtf8, sizeof kOutputUtf8);
  EXPECT_EQ(output,
            FormatPack(UntypedFormatSpecImpl("%ls"), absl::MakeSpan(args)));
}

TEST_F(FormatConvertTest, Int128) {
  absl::int128 positive = static_cast<absl::int128>(0x1234567890abcdef) * 1979;
  absl::int128 negative = -positive;
  absl::int128 max = absl::Int128Max(), min = absl::Int128Min();
  const FormatArgImpl args[] = {FormatArgImpl(positive),
                                FormatArgImpl(negative), FormatArgImpl(max),
                                FormatArgImpl(min)};

  struct Case {
    const char* format;
    const char* expected;
  } cases[] = {
      {"%1$d", "2595989796776606496405"},
      {"%1$30d", "        2595989796776606496405"},
      {"%1$-30d", "2595989796776606496405        "},
      {"%1$u", "2595989796776606496405"},
      {"%1$x", "8cba9876066020f695"},
      {"%2$d", "-2595989796776606496405"},
      {"%2$30d", "       -2595989796776606496405"},
      {"%2$-30d", "-2595989796776606496405       "},
      {"%2$u", "340282366920938460867384810655161715051"},
      {"%2$x", "ffffffffffffff73456789f99fdf096b"},
      {"%3$d", "170141183460469231731687303715884105727"},
      {"%3$u", "170141183460469231731687303715884105727"},
      {"%3$x", "7fffffffffffffffffffffffffffffff"},
      {"%4$d", "-170141183460469231731687303715884105728"},
      {"%4$x", "80000000000000000000000000000000"},
  };

  for (auto c : cases) {
    UntypedFormatSpecImpl format(c.format);
    EXPECT_EQ(c.expected, FormatPack(format, absl::MakeSpan(args)));
  }
}

TEST_F(FormatConvertTest, Uint128) {
  absl::uint128 v = static_cast<absl::uint128>(0x1234567890abcdef) * 1979;
  absl::uint128 max = absl::Uint128Max();
  const FormatArgImpl args[] = {FormatArgImpl(v), FormatArgImpl(max)};

  struct Case {
    const char* format;
    const char* expected;
  } cases[] = {
      {"%1$d", "2595989796776606496405"},
      {"%1$30d", "        2595989796776606496405"},
      {"%1$-30d", "2595989796776606496405        "},
      {"%1$u", "2595989796776606496405"},
      {"%1$x", "8cba9876066020f695"},
      {"%2$d", "340282366920938463463374607431768211455"},
      {"%2$u", "340282366920938463463374607431768211455"},
      {"%2$x", "ffffffffffffffffffffffffffffffff"},
  };

  for (auto c : cases) {
    UntypedFormatSpecImpl format(c.format);
    EXPECT_EQ(c.expected, FormatPack(format, absl::MakeSpan(args)));
  }
}

template <typename Floating>
void TestWithMultipleFormatsHelper(const std::vector<Floating> &floats,
                                   const std::set<Floating> &skip_verify) {
  const NativePrintfTraits &native_traits = VerifyNativeImplementation();
  // Reserve the space to ensure we don't allocate memory in the output itself.
  std::string str_format_result;
  str_format_result.reserve(1 << 20);
  std::string string_printf_result;
  string_printf_result.reserve(1 << 20);

  const char *const kFormats[] = {
      "%",  "%.3", "%8.5", "%500",   "%.5000", "%.60", "%.30",   "%03",
      "%+", "% ",  "%-10", "%#15.3", "%#.0",   "%.0",  "%1$*2$", "%1$.*2$"};

  for (const char *fmt : kFormats) {
    for (char f : {'f', 'F',  //
                   'g', 'G',  //
                   'a', 'A',  //
                   'e', 'E'}) {
      std::string fmt_str = std::string(fmt) + f;

      if (fmt == absl::string_view("%.5000") && f != 'f' && f != 'F' &&
          f != 'a' && f != 'A') {
        // This particular test takes way too long with snprintf.
        // Disable for the case we are not implementing natively.
        continue;
      }

      if ((f == 'a' || f == 'A') &&
          !native_traits.hex_float_has_glibc_rounding) {
        continue;
      }

      for (Floating d : floats) {
        if (!native_traits.hex_float_prefers_denormal_repr &&
            (f == 'a' || f == 'A') && std::fpclassify(d) == FP_SUBNORMAL) {
          continue;
        }
        int i = -10;
        FormatArgImpl args[2] = {FormatArgImpl(d), FormatArgImpl(i)};
        UntypedFormatSpecImpl format(fmt_str);

        string_printf_result.clear();
        StrAppend(&string_printf_result, fmt_str.c_str(), d, i);
        str_format_result.clear();

        {
          AppendPack(&str_format_result, format, absl::MakeSpan(args));
        }

#ifdef _MSC_VER
        // MSVC has a different rounding policy than us so we can't test our
        // implementation against the native one there.
        continue;
#elif defined(__APPLE__)
        // Apple formats NaN differently (+nan) vs. (nan)
        if (std::isnan(d)) continue;
#endif
        if (string_printf_result != str_format_result &&
            skip_verify.find(d) == skip_verify.end()) {
          // We use ASSERT_EQ here because failures are usually correlated and a
          // bug would print way too many failed expectations causing the test
          // to time out.
          ASSERT_EQ(string_printf_result, str_format_result)
              << fmt_str << " " << StrPrint("%.18g", d) << " "
              << StrPrint("%a", d) << " " << StrPrint("%.50f", d);
        }
      }
    }
  }
}

TEST_F(FormatConvertTest, Float) {
  std::vector<float> floats = {0.0f,
                               -0.0f,
                               .9999999f,
                               9999999.f,
                               std::numeric_limits<float>::max(),
                               -std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::min(),
                               -std::numeric_limits<float>::min(),
                               std::numeric_limits<float>::lowest(),
                               -std::numeric_limits<float>::lowest(),
                               std::numeric_limits<float>::epsilon(),
                               std::numeric_limits<float>::epsilon() + 1.0f,
                               std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity(),
                               std::nanf("")};

  // Some regression tests.
  floats.push_back(0.999999989f);

  if (std::numeric_limits<float>::has_denorm != std::denorm_absent) {
    floats.push_back(std::numeric_limits<float>::denorm_min());
    floats.push_back(-std::numeric_limits<float>::denorm_min());
  }

  for (float base :
       {1.f, 12.f, 123.f, 1234.f, 12345.f, 123456.f, 1234567.f, 12345678.f,
        123456789.f, 1234567890.f, 12345678901.f, 12345678.f, 12345678.f}) {
    for (int exp = -123; exp <= 123; ++exp) {
      for (int sign : {1, -1}) {
        floats.push_back(sign * std::ldexp(base, exp));
      }
    }
  }

  for (int exp = -300; exp <= 300; ++exp) {
    const float all_ones_mantissa = 0xffffff;
    floats.push_back(std::ldexp(all_ones_mantissa, exp));
  }

  // Remove duplicates to speed up the logic below.
  std::sort(floats.begin(), floats.end(), [](const float a, const float b) {
    if (std::isnan(a)) return false;
    if (std::isnan(b)) return true;
    return a < b;
  });
  floats.erase(std::unique(floats.begin(), floats.end()), floats.end());

  TestWithMultipleFormatsHelper(floats, {});
}

TEST_F(FormatConvertTest, Double) {
  // For values that we know won't match the standard library implementation we
  // skip verification, but still run the algorithm to catch asserts/sanitizer
  // bugs.
  std::set<double> skip_verify;
  std::vector<double> doubles = {0.0,
                                 -0.0,
                                 .99999999999999,
                                 99999999999999.,
                                 std::numeric_limits<double>::max(),
                                 -std::numeric_limits<double>::max(),
                                 std::numeric_limits<double>::min(),
                                 -std::numeric_limits<double>::min(),
                                 std::numeric_limits<double>::lowest(),
                                 -std::numeric_limits<double>::lowest(),
                                 std::numeric_limits<double>::epsilon(),
                                 std::numeric_limits<double>::epsilon() + 1,
                                 std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity(),
                                 std::nan("")};

  // Some regression tests.
  doubles.push_back(0.99999999999999989);

  if (std::numeric_limits<double>::has_denorm != std::denorm_absent) {
    doubles.push_back(std::numeric_limits<double>::denorm_min());
    doubles.push_back(-std::numeric_limits<double>::denorm_min());
  }

  for (double base :
       {1., 12., 123., 1234., 12345., 123456., 1234567., 12345678., 123456789.,
        1234567890., 12345678901., 123456789012., 1234567890123.}) {
    for (int exp = -123; exp <= 123; ++exp) {
      for (int sign : {1, -1}) {
        doubles.push_back(sign * std::ldexp(base, exp));
      }
    }
  }

  // Workaround libc bug.
  // https://sourceware.org/bugzilla/show_bug.cgi?id=22142
  const bool gcc_bug_22142 =
      StrPrint("%f", std::numeric_limits<double>::max()) !=
      "1797693134862315708145274237317043567980705675258449965989174768031"
      "5726078002853876058955863276687817154045895351438246423432132688946"
      "4182768467546703537516986049910576551282076245490090389328944075868"
      "5084551339423045832369032229481658085593321233482747978262041447231"
      "68738177180919299881250404026184124858368.000000";

  for (int exp = -300; exp <= 300; ++exp) {
    const double all_ones_mantissa = 0x1fffffffffffff;
    doubles.push_back(std::ldexp(all_ones_mantissa, exp));
    if (gcc_bug_22142) {
      skip_verify.insert(doubles.back());
    }
  }

  if (gcc_bug_22142) {
    using L = std::numeric_limits<double>;
    skip_verify.insert(L::max());
    skip_verify.insert(L::min());  // NOLINT
    skip_verify.insert(L::denorm_min());
    skip_verify.insert(-L::max());
    skip_verify.insert(-L::min());  // NOLINT
    skip_verify.insert(-L::denorm_min());
  }

  // Remove duplicates to speed up the logic below.
  std::sort(doubles.begin(), doubles.end(), [](const double a, const double b) {
    if (std::isnan(a)) return false;
    if (std::isnan(b)) return true;
    return a < b;
  });
  doubles.erase(std::unique(doubles.begin(), doubles.end()), doubles.end());

  TestWithMultipleFormatsHelper(doubles, skip_verify);
}

TEST_F(FormatConvertTest, DoubleRound) {
  std::string s;
  const auto format = [&](const char *fmt, double d) -> std::string & {
    s.clear();
    FormatArgImpl args[1] = {FormatArgImpl(d)};
    AppendPack(&s, UntypedFormatSpecImpl(fmt), absl::MakeSpan(args));
#if !defined(_MSC_VER)
    // MSVC has a different rounding policy than us so we can't test our
    // implementation against the native one there.
    EXPECT_EQ(StrPrint(fmt, d), s);
#endif  // _MSC_VER

    return s;
  };
  // All of these values have to be exactly represented.
  // Otherwise we might not be testing what we think we are testing.

  // These values can fit in a 64bit "fast" representation.
  const double exact_value = 0.00000000000005684341886080801486968994140625;
  assert(exact_value == std::pow(2, -44));
  // Round up at a 5xx.
  EXPECT_EQ(format("%.13f", exact_value), "0.0000000000001");
  // Round up at a >5
  EXPECT_EQ(format("%.14f", exact_value), "0.00000000000006");
  // Round down at a <5
  EXPECT_EQ(format("%.16f", exact_value), "0.0000000000000568");
  // Nine handling
  EXPECT_EQ(format("%.35f", exact_value),
            "0.00000000000005684341886080801486969");
  EXPECT_EQ(format("%.36f", exact_value),
            "0.000000000000056843418860808014869690");
  // Round down the last nine.
  EXPECT_EQ(format("%.37f", exact_value),
            "0.0000000000000568434188608080148696899");
  EXPECT_EQ(format("%.10f", 0.000003814697265625), "0.0000038147");
  // Round up the last nine
  EXPECT_EQ(format("%.11f", 0.000003814697265625), "0.00000381470");
  EXPECT_EQ(format("%.12f", 0.000003814697265625), "0.000003814697");

  // Round to even (down)
  EXPECT_EQ(format("%.43f", exact_value),
            "0.0000000000000568434188608080148696899414062");
  // Exact
  EXPECT_EQ(format("%.44f", exact_value),
            "0.00000000000005684341886080801486968994140625");
  // Round to even (up), let make the last digits 75 instead of 25
  EXPECT_EQ(format("%.43f", exact_value + std::pow(2, -43)),
            "0.0000000000001705302565824240446090698242188");
  // Exact, just to check.
  EXPECT_EQ(format("%.44f", exact_value + std::pow(2, -43)),
            "0.00000000000017053025658242404460906982421875");

  // This value has to be small enough that it won't fit in the uint128
  // representation for printing.
  const double small_exact_value =
      0.000000000000000000000000000000000000752316384526264005099991383822237233803945956334136013765601092018187046051025390625;  // NOLINT
  assert(small_exact_value == std::pow(2, -120));
  // Round up at a 5xx.
  EXPECT_EQ(format("%.37f", small_exact_value),
            "0.0000000000000000000000000000000000008");
  // Round down at a <5
  EXPECT_EQ(format("%.38f", small_exact_value),
            "0.00000000000000000000000000000000000075");
  // Round up at a >5
  EXPECT_EQ(format("%.41f", small_exact_value),
            "0.00000000000000000000000000000000000075232");
  // Nine handling
  EXPECT_EQ(format("%.55f", small_exact_value),
            "0.0000000000000000000000000000000000007523163845262640051");
  EXPECT_EQ(format("%.56f", small_exact_value),
            "0.00000000000000000000000000000000000075231638452626400510");
  EXPECT_EQ(format("%.57f", small_exact_value),
            "0.000000000000000000000000000000000000752316384526264005100");
  EXPECT_EQ(format("%.58f", small_exact_value),
            "0.0000000000000000000000000000000000007523163845262640051000");
  // Round down the last nine
  EXPECT_EQ(format("%.59f", small_exact_value),
            "0.00000000000000000000000000000000000075231638452626400509999");
  // Round up the last nine
  EXPECT_EQ(format("%.79f", small_exact_value),
            "0.000000000000000000000000000000000000"
            "7523163845262640050999913838222372338039460");

  // Round to even (down)
  EXPECT_EQ(format("%.119f", small_exact_value),
            "0.000000000000000000000000000000000000"
            "75231638452626400509999138382223723380"
            "394595633413601376560109201818704605102539062");
  // Exact
  EXPECT_EQ(format("%.120f", small_exact_value),
            "0.000000000000000000000000000000000000"
            "75231638452626400509999138382223723380"
            "3945956334136013765601092018187046051025390625");
  // Round to even (up), let make the last digits 75 instead of 25
  EXPECT_EQ(format("%.119f", small_exact_value + std::pow(2, -119)),
            "0.000000000000000000000000000000000002"
            "25694915357879201529997415146671170141"
            "183786900240804129680327605456113815307617188");
  // Exact, just to check.
  EXPECT_EQ(format("%.120f", small_exact_value + std::pow(2, -119)),
            "0.000000000000000000000000000000000002"
            "25694915357879201529997415146671170141"
            "1837869002408041296803276054561138153076171875");
}

TEST_F(FormatConvertTest, DoubleRoundA) {
  const NativePrintfTraits &native_traits = VerifyNativeImplementation();
  std::string s;
  const auto format = [&](const char *fmt, double d) -> std::string & {
    s.clear();
    FormatArgImpl args[1] = {FormatArgImpl(d)};
    AppendPack(&s, UntypedFormatSpecImpl(fmt), absl::MakeSpan(args));
    if (native_traits.hex_float_has_glibc_rounding) {
      EXPECT_EQ(StrPrint(fmt, d), s);
    }
    return s;
  };

  // 0x1.00018000p+100
  const double on_boundary_odd = 1267679614447900152596896153600.0;
  EXPECT_EQ(format("%.0a", on_boundary_odd), "0x1p+100");
  EXPECT_EQ(format("%.1a", on_boundary_odd), "0x1.0p+100");
  EXPECT_EQ(format("%.2a", on_boundary_odd), "0x1.00p+100");
  EXPECT_EQ(format("%.3a", on_boundary_odd), "0x1.000p+100");
  EXPECT_EQ(format("%.4a", on_boundary_odd), "0x1.0002p+100");  // round
  EXPECT_EQ(format("%.5a", on_boundary_odd), "0x1.00018p+100");
  EXPECT_EQ(format("%.6a", on_boundary_odd), "0x1.000180p+100");

  // 0x1.00028000p-2
  const double on_boundary_even = 0.250009536743164062500;
  EXPECT_EQ(format("%.0a", on_boundary_even), "0x1p-2");
  EXPECT_EQ(format("%.1a", on_boundary_even), "0x1.0p-2");
  EXPECT_EQ(format("%.2a", on_boundary_even), "0x1.00p-2");
  EXPECT_EQ(format("%.3a", on_boundary_even), "0x1.000p-2");
  EXPECT_EQ(format("%.4a", on_boundary_even), "0x1.0002p-2");  // no round
  EXPECT_EQ(format("%.5a", on_boundary_even), "0x1.00028p-2");
  EXPECT_EQ(format("%.6a", on_boundary_even), "0x1.000280p-2");

  // 0x1.00018001p+1
  const double slightly_over = 2.00004577683284878730773925781250;
  EXPECT_EQ(format("%.0a", slightly_over), "0x1p+1");
  EXPECT_EQ(format("%.1a", slightly_over), "0x1.0p+1");
  EXPECT_EQ(format("%.2a", slightly_over), "0x1.00p+1");
  EXPECT_EQ(format("%.3a", slightly_over), "0x1.000p+1");
  EXPECT_EQ(format("%.4a", slightly_over), "0x1.0002p+1");
  EXPECT_EQ(format("%.5a", slightly_over), "0x1.00018p+1");
  EXPECT_EQ(format("%.6a", slightly_over), "0x1.000180p+1");

  // 0x1.00017fffp+0
  const double slightly_under = 1.000022887950763106346130371093750;
  EXPECT_EQ(format("%.0a", slightly_under), "0x1p+0");
  EXPECT_EQ(format("%.1a", slightly_under), "0x1.0p+0");
  EXPECT_EQ(format("%.2a", slightly_under), "0x1.00p+0");
  EXPECT_EQ(format("%.3a", slightly_under), "0x1.000p+0");
  EXPECT_EQ(format("%.4a", slightly_under), "0x1.0001p+0");
  EXPECT_EQ(format("%.5a", slightly_under), "0x1.00018p+0");
  EXPECT_EQ(format("%.6a", slightly_under), "0x1.000180p+0");
  EXPECT_EQ(format("%.7a", slightly_under), "0x1.0001800p+0");

  // 0x1.1b3829ac28058p+3
  const double hex_value = 8.85060580848964661981881363317370414733886718750;
  EXPECT_EQ(format("%.0a", hex_value), "0x1p+3");
  EXPECT_EQ(format("%.1a", hex_value), "0x1.2p+3");
  EXPECT_EQ(format("%.2a", hex_value), "0x1.1bp+3");
  EXPECT_EQ(format("%.3a", hex_value), "0x1.1b4p+3");
  EXPECT_EQ(format("%.4a", hex_value), "0x1.1b38p+3");
  EXPECT_EQ(format("%.5a", hex_value), "0x1.1b383p+3");
  EXPECT_EQ(format("%.6a", hex_value), "0x1.1b382ap+3");
  EXPECT_EQ(format("%.7a", hex_value), "0x1.1b3829bp+3");
  EXPECT_EQ(format("%.8a", hex_value), "0x1.1b3829acp+3");
  EXPECT_EQ(format("%.9a", hex_value), "0x1.1b3829ac3p+3");
  EXPECT_EQ(format("%.10a", hex_value), "0x1.1b3829ac28p+3");
  EXPECT_EQ(format("%.11a", hex_value), "0x1.1b3829ac280p+3");
  EXPECT_EQ(format("%.12a", hex_value), "0x1.1b3829ac2806p+3");
  EXPECT_EQ(format("%.13a", hex_value), "0x1.1b3829ac28058p+3");
  EXPECT_EQ(format("%.14a", hex_value), "0x1.1b3829ac280580p+3");
  EXPECT_EQ(format("%.15a", hex_value), "0x1.1b3829ac2805800p+3");
  EXPECT_EQ(format("%.16a", hex_value), "0x1.1b3829ac28058000p+3");
  EXPECT_EQ(format("%.17a", hex_value), "0x1.1b3829ac280580000p+3");
  EXPECT_EQ(format("%.18a", hex_value), "0x1.1b3829ac2805800000p+3");
  EXPECT_EQ(format("%.19a", hex_value), "0x1.1b3829ac28058000000p+3");
  EXPECT_EQ(format("%.20a", hex_value), "0x1.1b3829ac280580000000p+3");
  EXPECT_EQ(format("%.21a", hex_value), "0x1.1b3829ac2805800000000p+3");

  // 0x1.0818283848586p+3
  const double hex_value2 = 8.2529488658208371987257123691961169242858886718750;
  EXPECT_EQ(format("%.0a", hex_value2), "0x1p+3");
  EXPECT_EQ(format("%.1a", hex_value2), "0x1.1p+3");
  EXPECT_EQ(format("%.2a", hex_value2), "0x1.08p+3");
  EXPECT_EQ(format("%.3a", hex_value2), "0x1.082p+3");
  EXPECT_EQ(format("%.4a", hex_value2), "0x1.0818p+3");
  EXPECT_EQ(format("%.5a", hex_value2), "0x1.08183p+3");
  EXPECT_EQ(format("%.6a", hex_value2), "0x1.081828p+3");
  EXPECT_EQ(format("%.7a", hex_value2), "0x1.0818284p+3");
  EXPECT_EQ(format("%.8a", hex_value2), "0x1.08182838p+3");
  EXPECT_EQ(format("%.9a", hex_value2), "0x1.081828385p+3");
  EXPECT_EQ(format("%.10a", hex_value2), "0x1.0818283848p+3");
  EXPECT_EQ(format("%.11a", hex_value2), "0x1.08182838486p+3");
  EXPECT_EQ(format("%.12a", hex_value2), "0x1.081828384858p+3");
  EXPECT_EQ(format("%.13a", hex_value2), "0x1.0818283848586p+3");
  EXPECT_EQ(format("%.14a", hex_value2), "0x1.08182838485860p+3");
  EXPECT_EQ(format("%.15a", hex_value2), "0x1.081828384858600p+3");
  EXPECT_EQ(format("%.16a", hex_value2), "0x1.0818283848586000p+3");
  EXPECT_EQ(format("%.17a", hex_value2), "0x1.08182838485860000p+3");
  EXPECT_EQ(format("%.18a", hex_value2), "0x1.081828384858600000p+3");
  EXPECT_EQ(format("%.19a", hex_value2), "0x1.0818283848586000000p+3");
  EXPECT_EQ(format("%.20a", hex_value2), "0x1.08182838485860000000p+3");
  EXPECT_EQ(format("%.21a", hex_value2), "0x1.081828384858600000000p+3");
}

TEST_F(FormatConvertTest, LongDoubleRoundA) {
  if (std::numeric_limits<long double>::digits % 4 != 0) {
    // This test doesn't really make sense to run on platforms where a long
    // double has a different mantissa size (mod 4) than Prod, since then the
    // leading digit will be formatted differently.
    return;
  }
  const NativePrintfTraits &native_traits = VerifyNativeImplementation();
  std::string s;
  const auto format = [&](const char *fmt, long double d) -> std::string & {
    s.clear();
    FormatArgImpl args[1] = {FormatArgImpl(d)};
    AppendPack(&s, UntypedFormatSpecImpl(fmt), absl::MakeSpan(args));
    if (native_traits.hex_float_has_glibc_rounding &&
        native_traits.hex_float_optimizes_leading_digit_bit_count) {
      EXPECT_EQ(StrPrint(fmt, d), s);
    }
    return s;
  };

  // 0x8.8p+4
  const long double on_boundary_even = 136.0;
  EXPECT_EQ(format("%.0La", on_boundary_even), "0x8p+4");
  EXPECT_EQ(format("%.1La", on_boundary_even), "0x8.8p+4");
  EXPECT_EQ(format("%.2La", on_boundary_even), "0x8.80p+4");
  EXPECT_EQ(format("%.3La", on_boundary_even), "0x8.800p+4");
  EXPECT_EQ(format("%.4La", on_boundary_even), "0x8.8000p+4");
  EXPECT_EQ(format("%.5La", on_boundary_even), "0x8.80000p+4");
  EXPECT_EQ(format("%.6La", on_boundary_even), "0x8.800000p+4");

  // 0x9.8p+4
  const long double on_boundary_odd = 152.0;
  EXPECT_EQ(format("%.0La", on_boundary_odd), "0xap+4");
  EXPECT_EQ(format("%.1La", on_boundary_odd), "0x9.8p+4");
  EXPECT_EQ(format("%.2La", on_boundary_odd), "0x9.80p+4");
  EXPECT_EQ(format("%.3La", on_boundary_odd), "0x9.800p+4");
  EXPECT_EQ(format("%.4La", on_boundary_odd), "0x9.8000p+4");
  EXPECT_EQ(format("%.5La", on_boundary_odd), "0x9.80000p+4");
  EXPECT_EQ(format("%.6La", on_boundary_odd), "0x9.800000p+4");

  // 0x8.80001p+24
  const long double slightly_over = 142606352.0;
  EXPECT_EQ(format("%.0La", slightly_over), "0x9p+24");
  EXPECT_EQ(format("%.1La", slightly_over), "0x8.8p+24");
  EXPECT_EQ(format("%.2La", slightly_over), "0x8.80p+24");
  EXPECT_EQ(format("%.3La", slightly_over), "0x8.800p+24");
  EXPECT_EQ(format("%.4La", slightly_over), "0x8.8000p+24");
  EXPECT_EQ(format("%.5La", slightly_over), "0x8.80001p+24");
  EXPECT_EQ(format("%.6La", slightly_over), "0x8.800010p+24");

  // 0x8.7ffffp+24
  const long double slightly_under = 142606320.0;
  EXPECT_EQ(format("%.0La", slightly_under), "0x8p+24");
  EXPECT_EQ(format("%.1La", slightly_under), "0x8.8p+24");
  EXPECT_EQ(format("%.2La", slightly_under), "0x8.80p+24");
  EXPECT_EQ(format("%.3La", slightly_under), "0x8.800p+24");
  EXPECT_EQ(format("%.4La", slightly_under), "0x8.8000p+24");
  EXPECT_EQ(format("%.5La", slightly_under), "0x8.7ffffp+24");
  EXPECT_EQ(format("%.6La", slightly_under), "0x8.7ffff0p+24");
  EXPECT_EQ(format("%.7La", slightly_under), "0x8.7ffff00p+24");

  // 0xc.0828384858688000p+128
  const long double eights = 4094231060438608800781871108094404067328.0;
  EXPECT_EQ(format("%.0La", eights), "0xcp+128");
  EXPECT_EQ(format("%.1La", eights), "0xc.1p+128");
  EXPECT_EQ(format("%.2La", eights), "0xc.08p+128");
  EXPECT_EQ(format("%.3La", eights), "0xc.083p+128");
  EXPECT_EQ(format("%.4La", eights), "0xc.0828p+128");
  EXPECT_EQ(format("%.5La", eights), "0xc.08284p+128");
  EXPECT_EQ(format("%.6La", eights), "0xc.082838p+128");
  EXPECT_EQ(format("%.7La", eights), "0xc.0828385p+128");
  EXPECT_EQ(format("%.8La", eights), "0xc.08283848p+128");
  EXPECT_EQ(format("%.9La", eights), "0xc.082838486p+128");
  EXPECT_EQ(format("%.10La", eights), "0xc.0828384858p+128");
  EXPECT_EQ(format("%.11La", eights), "0xc.08283848587p+128");
  EXPECT_EQ(format("%.12La", eights), "0xc.082838485868p+128");
  EXPECT_EQ(format("%.13La", eights), "0xc.0828384858688p+128");
  EXPECT_EQ(format("%.14La", eights), "0xc.08283848586880p+128");
  EXPECT_EQ(format("%.15La", eights), "0xc.082838485868800p+128");
  EXPECT_EQ(format("%.16La", eights), "0xc.0828384858688000p+128");
}

// We don't actually store the results. This is just to exercise the rest of the
// machinery.
struct NullSink {
  friend void AbslFormatFlush(NullSink *, string_view) {}
};

template <typename... T>
bool FormatWithNullSink(absl::string_view fmt, const T &... a) {
  NullSink sink;
  FormatArgImpl args[] = {FormatArgImpl(a)...};
  return FormatUntyped(&sink, UntypedFormatSpecImpl(fmt), absl::MakeSpan(args));
}

TEST_F(FormatConvertTest, ExtremeWidthPrecision) {
  for (const char *fmt : {"f"}) {
    for (double d : {1e-100, 1.0, 1e100}) {
      constexpr int max = std::numeric_limits<int>::max();
      EXPECT_TRUE(FormatWithNullSink(std::string("%.*") + fmt, max, d));
      EXPECT_TRUE(FormatWithNullSink(std::string("%1.*") + fmt, max, d));
      EXPECT_TRUE(FormatWithNullSink(std::string("%*") + fmt, max, d));
      EXPECT_TRUE(FormatWithNullSink(std::string("%*.*") + fmt, max, max, d));
    }
  }
}

TEST_F(FormatConvertTest, LongDouble) {
  const NativePrintfTraits &native_traits = VerifyNativeImplementation();
  const char *const kFormats[] = {"%",    "%.3", "%8.5", "%9",  "%.5000",
                                  "%.60", "%+",  "% ",   "%-10"};

  std::vector<long double> doubles = {
      0.0,
      -0.0,
      std::numeric_limits<long double>::max(),
      -std::numeric_limits<long double>::max(),
      std::numeric_limits<long double>::min(),
      -std::numeric_limits<long double>::min(),
      std::numeric_limits<long double>::infinity(),
      -std::numeric_limits<long double>::infinity()};

  for (long double base : {1.L, 12.L, 123.L, 1234.L, 12345.L, 123456.L,
                           1234567.L, 12345678.L, 123456789.L, 1234567890.L,
                           12345678901.L, 123456789012.L, 1234567890123.L,
                           // This value is not representable in double, but it
                           // is in long double that uses the extended format.
                           // This is to verify that we are not truncating the
                           // value mistakenly through a double.
                           10000000000000000.25L}) {
    for (int exp : {-1000, -500, 0, 500, 1000}) {
      for (int sign : {1, -1}) {
        doubles.push_back(sign * std::ldexp(base, exp));
        doubles.push_back(sign / std::ldexp(base, exp));
      }
    }
  }

  // Regression tests
  //
  // Using a string literal because not all platforms support hex literals or it
  // might be out of range.
  doubles.push_back(std::strtold("-0xf.ffffffb5feafffbp-16324L", nullptr));

  for (const char *fmt : kFormats) {
    for (char f : {'f', 'F',  //
                   'g', 'G',  //
                   'a', 'A',  //
                   'e', 'E'}) {
      std::string fmt_str = std::string(fmt) + 'L' + f;

      if (fmt == absl::string_view("%.5000") && f != 'f' && f != 'F' &&
          f != 'a' && f != 'A') {
        // This particular test takes way too long with snprintf.
        // Disable for the case we are not implementing natively.
        continue;
      }

      if (f == 'a' || f == 'A') {
        if (!native_traits.hex_float_has_glibc_rounding ||
            !native_traits.hex_float_optimizes_leading_digit_bit_count) {
          continue;
        }
      }

      for (auto d : doubles) {
        FormatArgImpl arg(d);
        UntypedFormatSpecImpl format(fmt_str);
        std::string result = FormatPack(format, {&arg, 1});

#ifdef _MSC_VER
        // MSVC has a different rounding policy than us so we can't test our
        // implementation against the native one there.
        continue;
#endif  // _MSC_VER

        // We use ASSERT_EQ here because failures are usually correlated and a
        // bug would print way too many failed expectations causing the test to
        // time out.
        ASSERT_EQ(StrPrint(fmt_str.c_str(), d), result)
            << fmt_str << " " << StrPrint("%.18Lg", d) << " "
            << StrPrint("%La", d) << " " << StrPrint("%.1080Lf", d);
      }
    }
  }
}

TEST_F(FormatConvertTest, IntAsDouble) {
  const NativePrintfTraits &native_traits = VerifyNativeImplementation();
  const int kMin = std::numeric_limits<int>::min();
  const int kMax = std::numeric_limits<int>::max();
  const int ia[] = {
    1, 2, 3, 123,
    -1, -2, -3, -123,
    0, kMax - 1, kMax, kMin + 1, kMin };
  for (const int fx : ia) {
    SCOPED_TRACE(fx);
    const FormatArgImpl args[] = {FormatArgImpl(fx)};
    struct Expectation {
      int line;
      std::string out;
      const char *fmt;
    };
    const double dx = static_cast<double>(fx);
    std::vector<Expectation> expect = {
        {__LINE__, StrPrint("%f", dx), "%f"},
        {__LINE__, StrPrint("%12f", dx), "%12f"},
        {__LINE__, StrPrint("%.12f", dx), "%.12f"},
        {__LINE__, StrPrint("%.12a", dx), "%.12a"},
    };
    if (native_traits.hex_float_uses_minimal_precision_when_not_specified) {
      Expectation ex = {__LINE__, StrPrint("%12a", dx), "%12a"};
      expect.push_back(ex);
    }
    for (const Expectation &e : expect) {
      SCOPED_TRACE(e.line);
      SCOPED_TRACE(e.fmt);
      UntypedFormatSpecImpl format(e.fmt);
      EXPECT_EQ(e.out, FormatPack(format, absl::MakeSpan(args)));
    }
  }
}

template <typename T>
bool FormatFails(const char* test_format, T value) {
  std::string format_string = std::string("<<") + test_format + ">>";
  UntypedFormatSpecImpl format(format_string);

  int one = 1;
  const FormatArgImpl args[] = {FormatArgImpl(value), FormatArgImpl(one)};
  EXPECT_EQ(FormatPack(format, absl::MakeSpan(args)), "")
      << "format=" << test_format << " value=" << value;
  return FormatPack(format, absl::MakeSpan(args)).empty();
}

TEST_F(FormatConvertTest, ExpectedFailures) {
  // Int input
  EXPECT_TRUE(FormatFails("%p", 1));
  EXPECT_TRUE(FormatFails("%s", 1));
  EXPECT_TRUE(FormatFails("%n", 1));

  // Double input
  EXPECT_TRUE(FormatFails("%p", 1.));
  EXPECT_TRUE(FormatFails("%s", 1.));
  EXPECT_TRUE(FormatFails("%n", 1.));
  EXPECT_TRUE(FormatFails("%c", 1.));
  EXPECT_TRUE(FormatFails("%d", 1.));
  EXPECT_TRUE(FormatFails("%x", 1.));
  EXPECT_TRUE(FormatFails("%*d", 1.));

  // String input
  EXPECT_TRUE(FormatFails("%n", ""));
  EXPECT_TRUE(FormatFails("%c", ""));
  EXPECT_TRUE(FormatFails("%d", ""));
  EXPECT_TRUE(FormatFails("%x", ""));
  EXPECT_TRUE(FormatFails("%f", ""));
  EXPECT_TRUE(FormatFails("%*d", ""));
}

// Sanity check to make sure that we are testing what we think we're testing on
// e.g. the x86_64+glibc platform.
TEST_F(FormatConvertTest, GlibcHasCorrectTraits) {
#if defined(__GLIBC__) && defined(__x86_64__)
  constexpr bool kIsSupportedGlibc = true;
#else
  constexpr bool kIsSupportedGlibc = false;
#endif

  if (!kIsSupportedGlibc) {
    GTEST_SKIP() << "Test does not support this platform";
  }

  const NativePrintfTraits &native_traits = VerifyNativeImplementation();
  // If one of the following tests break then it is either because the above PP
  // macro guards failed to exclude a new platform (likely) or because something
  // has changed in the implementation of glibc sprintf float formatting
  // behavior.  If the latter, then the code that computes these flags needs to
  // be revisited and/or possibly the StrFormat implementation.
  EXPECT_TRUE(native_traits.hex_float_has_glibc_rounding);
  EXPECT_TRUE(native_traits.hex_float_prefers_denormal_repr);
  EXPECT_TRUE(
      native_traits.hex_float_uses_minimal_precision_when_not_specified);
  EXPECT_TRUE(native_traits.hex_float_optimizes_leading_digit_bit_count);
}

}  // namespace
}  // namespace str_format_internal
ABSL_NAMESPACE_END
}  // namespace absl
