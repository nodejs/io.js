// Copyright 2018 The Abseil Authors.
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

#ifndef ABSL_STRINGS_INTERNAL_CHARCONV_PARSE_H_
#define ABSL_STRINGS_INTERNAL_CHARCONV_PARSE_H_

#include <cstdint>

#include "absl/base/config.h"
#include "absl/strings/charconv.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

// Enum indicating whether a parsed float is a number or special value.
enum class FloatType { kNumber, kInfinity, kNan };

// The decomposed parts of a parsed `float` or `double`.
struct ParsedFloat {
  // Representation of the parsed mantissa, with the decimal point adjusted to
  // make it an integer.
  //
  // During decimal scanning, this contains 19 significant digits worth of
  // mantissa value.  If digits beyond this point are found, they
  // are truncated, and if any of these dropped digits are nonzero, then
  // `mantissa` is inexact, and the full mantissa is stored in [subrange_begin,
  // subrange_end).
  //
  // During hexadecimal scanning, this contains 15 significant hex digits worth
  // of mantissa value.  Digits beyond this point are sticky -- they are
  // truncated, but if any dropped digits are nonzero, the low bit of mantissa
  // will be set.  (This allows for precise rounding, and avoids the need
  // to store the full mantissa in [subrange_begin, subrange_end).)
  uint64_t mantissa = 0;

  // Floating point expontent.  This reflects any decimal point adjustments and
  // any truncated digits from the mantissa.  The absolute value of the parsed
  // number is represented by mantissa * (base ** exponent), where base==10 for
  // decimal floats, and base==2 for hexadecimal floats.
  int exponent = 0;

  // The literal exponent value scanned from the input, or 0 if none was
  // present.  This does not reflect any adjustments applied to mantissa.
  int literal_exponent = 0;

  // The type of number scanned.
  FloatType type = FloatType::kNumber;

  // When non-null, [subrange_begin, subrange_end) marks a range of characters
  // that require further processing.  The meaning is dependent on float type.
  // If type == kNumber and this is set, this is a "wide input": the input
  // mantissa contained more than 19 digits.  The range contains the full
  // mantissa.  It plus `literal_exponent` need to be examined to find the best
  // floating point match.
  // If type == kNan and this is set, the range marks the contents of a
  // matched parenthesized character region after the NaN.
  const char* subrange_begin = nullptr;
  const char* subrange_end = nullptr;

  // One-past-the-end of the successfully parsed region, or nullptr if no
  // matching pattern was found.
  const char* end = nullptr;
};

// Read the floating point number in the provided range, and populate
// ParsedFloat accordingly.
//
// format_flags is a bitmask value specifying what patterns this API will match.
// `scientific` and `fixed`  are honored per std::from_chars rules
// ([utility.from.chars], C++17): if exactly one of these bits is set, then an
// exponent is required, or dislallowed, respectively.
//
// Template parameter `base` must be either 10 or 16.  For base 16, a "0x" is
// *not* consumed.  The `hex` bit from format_flags is ignored by ParseFloat.
template <int base>
ParsedFloat ParseFloat(const char* begin, const char* end,
                       absl::chars_format format_flags);

extern template ParsedFloat ParseFloat<10>(const char* begin, const char* end,
                                           absl::chars_format format_flags);
extern template ParsedFloat ParseFloat<16>(const char* begin, const char* end,
                                           absl::chars_format format_flags);

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl
#endif  // ABSL_STRINGS_INTERNAL_CHARCONV_PARSE_H_
