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

#ifndef ABSL_STRINGS_CHARCONV_H_
#define ABSL_STRINGS_CHARCONV_H_

#include <system_error>  // NOLINT(build/c++11)

#include "absl/base/config.h"
#include "absl/base/nullability.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// Workalike compatibility version of std::chars_format from C++17.
//
// This is an bitfield enumerator which can be passed to absl::from_chars to
// configure the string-to-float conversion.
enum class chars_format {
  scientific = 1,
  fixed = 2,
  hex = 4,
  general = fixed | scientific,
};

// The return result of a string-to-number conversion.
//
// `ec` will be set to `invalid_argument` if a well-formed number was not found
// at the start of the input range, `result_out_of_range` if a well-formed
// number was found, but it was out of the representable range of the requested
// type, or to std::errc() otherwise.
//
// If a well-formed number was found, `ptr` is set to one past the sequence of
// characters that were successfully parsed.  If none was found, `ptr` is set
// to the `first` argument to from_chars.
struct from_chars_result {
  absl::Nonnull<const char*> ptr;
  std::errc ec;
};

// Workalike compatibility version of std::from_chars from C++17.  Currently
// this only supports the `double` and `float` types.
//
// This interface incorporates the proposed resolutions for library issues
// DR 3080 and DR 3081.  If these are adopted with different wording,
// Abseil's behavior will change to match the standard.  (The behavior most
// likely to change is for DR 3081, which says what `value` will be set to in
// the case of overflow and underflow.  Code that wants to avoid possible
// breaking changes in this area should not depend on `value` when the returned
// from_chars_result indicates a range error.)
//
// Searches the range [first, last) for the longest matching pattern beginning
// at `first` that represents a floating point number.  If one is found, store
// the result in `value`.
//
// The matching pattern format is almost the same as that of strtod(), except
// that (1) C locale is not respected, (2) an initial '+' character in the
// input range will never be matched, and (3) leading whitespaces are not
// ignored.
//
// If `fmt` is set, it must be one of the enumerator values of the chars_format.
// (This is despite the fact that chars_format is a bitmask type.)  If set to
// `scientific`, a matching number must contain an exponent.  If set to `fixed`,
// then an exponent will never match.  (For example, the string "1e5" will be
// parsed as "1".)  If set to `hex`, then a hexadecimal float is parsed in the
// format that strtod() accepts, except that a "0x" prefix is NOT matched.
// (In particular, in `hex` mode, the input "0xff" results in the largest
// matching pattern "0".)
absl::from_chars_result from_chars(absl::Nonnull<const char*> first,
                                   absl::Nonnull<const char*> last,
                                   double& value,  // NOLINT
                                   chars_format fmt = chars_format::general);

absl::from_chars_result from_chars(absl::Nonnull<const char*> first,
                                   absl::Nonnull<const char*> last,
                                   float& value,  // NOLINT
                                   chars_format fmt = chars_format::general);

// std::chars_format is specified as a bitmask type, which means the following
// operations must be provided:
inline constexpr chars_format operator&(chars_format lhs, chars_format rhs) {
  return static_cast<chars_format>(static_cast<int>(lhs) &
                                   static_cast<int>(rhs));
}
inline constexpr chars_format operator|(chars_format lhs, chars_format rhs) {
  return static_cast<chars_format>(static_cast<int>(lhs) |
                                   static_cast<int>(rhs));
}
inline constexpr chars_format operator^(chars_format lhs, chars_format rhs) {
  return static_cast<chars_format>(static_cast<int>(lhs) ^
                                   static_cast<int>(rhs));
}
inline constexpr chars_format operator~(chars_format arg) {
  return static_cast<chars_format>(~static_cast<int>(arg));
}
inline chars_format& operator&=(chars_format& lhs, chars_format rhs) {
  lhs = lhs & rhs;
  return lhs;
}
inline chars_format& operator|=(chars_format& lhs, chars_format rhs) {
  lhs = lhs | rhs;
  return lhs;
}
inline chars_format& operator^=(chars_format& lhs, chars_format rhs) {
  lhs = lhs ^ rhs;
  return lhs;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_CHARCONV_H_
