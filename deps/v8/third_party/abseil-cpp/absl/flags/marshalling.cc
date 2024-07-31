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

#include "absl/flags/marshalling.h"

#include <stddef.h>

#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/base/macros.h"
#include "absl/numeric/int128.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace flags_internal {

// --------------------------------------------------------------------
// AbslParseFlag specializations for boolean type.

bool AbslParseFlag(absl::string_view text, bool* dst, std::string*) {
  const char* kTrue[] = {"1", "t", "true", "y", "yes"};
  const char* kFalse[] = {"0", "f", "false", "n", "no"};
  static_assert(sizeof(kTrue) == sizeof(kFalse), "true_false_equal");

  text = absl::StripAsciiWhitespace(text);

  for (size_t i = 0; i < ABSL_ARRAYSIZE(kTrue); ++i) {
    if (absl::EqualsIgnoreCase(text, kTrue[i])) {
      *dst = true;
      return true;
    } else if (absl::EqualsIgnoreCase(text, kFalse[i])) {
      *dst = false;
      return true;
    }
  }
  return false;  // didn't match a legal input
}

// --------------------------------------------------------------------
// AbslParseFlag for integral types.

// Return the base to use for parsing text as an integer.  Leading 0x
// puts us in base 16.  But leading 0 does not put us in base 8. It
// caused too many bugs when we had that behavior.
static int NumericBase(absl::string_view text) {
  if (text.empty()) return 0;
  size_t num_start = (text[0] == '-' || text[0] == '+') ? 1 : 0;
  const bool hex = (text.size() >= num_start + 2 && text[num_start] == '0' &&
                    (text[num_start + 1] == 'x' || text[num_start + 1] == 'X'));
  return hex ? 16 : 10;
}

template <typename IntType>
inline bool ParseFlagImpl(absl::string_view text, IntType& dst) {
  text = absl::StripAsciiWhitespace(text);

  return absl::numbers_internal::safe_strtoi_base(text, &dst,
                                                  NumericBase(text));
}

bool AbslParseFlag(absl::string_view text, short* dst, std::string*) {
  int val;
  if (!ParseFlagImpl(text, val)) return false;
  if (static_cast<short>(val) != val)  // worked, but number out of range
    return false;
  *dst = static_cast<short>(val);
  return true;
}

bool AbslParseFlag(absl::string_view text, unsigned short* dst, std::string*) {
  unsigned int val;
  if (!ParseFlagImpl(text, val)) return false;
  if (static_cast<unsigned short>(val) !=
      val)  // worked, but number out of range
    return false;
  *dst = static_cast<unsigned short>(val);
  return true;
}

bool AbslParseFlag(absl::string_view text, int* dst, std::string*) {
  return ParseFlagImpl(text, *dst);
}

bool AbslParseFlag(absl::string_view text, unsigned int* dst, std::string*) {
  return ParseFlagImpl(text, *dst);
}

bool AbslParseFlag(absl::string_view text, long* dst, std::string*) {
  return ParseFlagImpl(text, *dst);
}

bool AbslParseFlag(absl::string_view text, unsigned long* dst, std::string*) {
  return ParseFlagImpl(text, *dst);
}

bool AbslParseFlag(absl::string_view text, long long* dst, std::string*) {
  return ParseFlagImpl(text, *dst);
}

bool AbslParseFlag(absl::string_view text, unsigned long long* dst,
                   std::string*) {
  return ParseFlagImpl(text, *dst);
}

bool AbslParseFlag(absl::string_view text, absl::int128* dst, std::string*) {
  text = absl::StripAsciiWhitespace(text);

  // check hex
  int base = NumericBase(text);
  if (!absl::numbers_internal::safe_strto128_base(text, dst, base)) {
    return false;
  }

  return base == 16 ? absl::SimpleHexAtoi(text, dst)
                    : absl::SimpleAtoi(text, dst);
}

bool AbslParseFlag(absl::string_view text, absl::uint128* dst, std::string*) {
  text = absl::StripAsciiWhitespace(text);

  // check hex
  int base = NumericBase(text);
  if (!absl::numbers_internal::safe_strtou128_base(text, dst, base)) {
    return false;
  }

  return base == 16 ? absl::SimpleHexAtoi(text, dst)
                    : absl::SimpleAtoi(text, dst);
}

// --------------------------------------------------------------------
// AbslParseFlag for floating point types.

bool AbslParseFlag(absl::string_view text, float* dst, std::string*) {
  return absl::SimpleAtof(text, dst);
}

bool AbslParseFlag(absl::string_view text, double* dst, std::string*) {
  return absl::SimpleAtod(text, dst);
}

// --------------------------------------------------------------------
// AbslParseFlag for strings.

bool AbslParseFlag(absl::string_view text, std::string* dst, std::string*) {
  dst->assign(text.data(), text.size());
  return true;
}

// --------------------------------------------------------------------
// AbslParseFlag for vector of strings.

bool AbslParseFlag(absl::string_view text, std::vector<std::string>* dst,
                   std::string*) {
  // An empty flag value corresponds to an empty vector, not a vector
  // with a single, empty std::string.
  if (text.empty()) {
    dst->clear();
    return true;
  }
  *dst = absl::StrSplit(text, ',', absl::AllowEmpty());
  return true;
}

// --------------------------------------------------------------------
// AbslUnparseFlag specializations for various builtin flag types.

std::string Unparse(bool v) { return v ? "true" : "false"; }
std::string Unparse(short v) { return absl::StrCat(v); }
std::string Unparse(unsigned short v) { return absl::StrCat(v); }
std::string Unparse(int v) { return absl::StrCat(v); }
std::string Unparse(unsigned int v) { return absl::StrCat(v); }
std::string Unparse(long v) { return absl::StrCat(v); }
std::string Unparse(unsigned long v) { return absl::StrCat(v); }
std::string Unparse(long long v) { return absl::StrCat(v); }
std::string Unparse(unsigned long long v) { return absl::StrCat(v); }
std::string Unparse(absl::int128 v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string Unparse(absl::uint128 v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

template <typename T>
std::string UnparseFloatingPointVal(T v) {
  // digits10 is guaranteed to roundtrip correctly in string -> value -> string
  // conversions, but may not be enough to represent all the values correctly.
  std::string digit10_str =
      absl::StrFormat("%.*g", std::numeric_limits<T>::digits10, v);
  if (std::isnan(v) || std::isinf(v)) return digit10_str;

  T roundtrip_val = 0;
  std::string err;
  if (absl::ParseFlag(digit10_str, &roundtrip_val, &err) &&
      roundtrip_val == v) {
    return digit10_str;
  }

  // max_digits10 is the number of base-10 digits that are necessary to uniquely
  // represent all distinct values.
  return absl::StrFormat("%.*g", std::numeric_limits<T>::max_digits10, v);
}
std::string Unparse(float v) { return UnparseFloatingPointVal(v); }
std::string Unparse(double v) { return UnparseFloatingPointVal(v); }
std::string AbslUnparseFlag(absl::string_view v) { return std::string(v); }
std::string AbslUnparseFlag(const std::vector<std::string>& v) {
  return absl::StrJoin(v, ",");
}

}  // namespace flags_internal

bool AbslParseFlag(absl::string_view text, absl::LogSeverity* dst,
                   std::string* err) {
  text = absl::StripAsciiWhitespace(text);
  if (text.empty()) {
    *err = "no value provided";
    return false;
  }
  if (absl::EqualsIgnoreCase(text, "dfatal")) {
    *dst = absl::kLogDebugFatal;
    return true;
  }
  if (absl::EqualsIgnoreCase(text, "klogdebugfatal")) {
    *dst = absl::kLogDebugFatal;
    return true;
  }
  if (text.front() == 'k' || text.front() == 'K') text.remove_prefix(1);
  if (absl::EqualsIgnoreCase(text, "info")) {
    *dst = absl::LogSeverity::kInfo;
    return true;
  }
  if (absl::EqualsIgnoreCase(text, "warning")) {
    *dst = absl::LogSeverity::kWarning;
    return true;
  }
  if (absl::EqualsIgnoreCase(text, "error")) {
    *dst = absl::LogSeverity::kError;
    return true;
  }
  if (absl::EqualsIgnoreCase(text, "fatal")) {
    *dst = absl::LogSeverity::kFatal;
    return true;
  }
  std::underlying_type<absl::LogSeverity>::type numeric_value;
  if (absl::ParseFlag(text, &numeric_value, err)) {
    *dst = static_cast<absl::LogSeverity>(numeric_value);
    return true;
  }
  *err =
      "only integers, absl::LogSeverity enumerators, and DFATAL are accepted";
  return false;
}

std::string AbslUnparseFlag(absl::LogSeverity v) {
  if (v == absl::NormalizeLogSeverity(v)) return absl::LogSeverityName(v);
  return absl::UnparseFlag(static_cast<int>(v));
}

ABSL_NAMESPACE_END
}  // namespace absl
