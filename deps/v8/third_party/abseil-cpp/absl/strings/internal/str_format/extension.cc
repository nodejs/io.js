//
// Copyright 2017 The Abseil Authors.
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

#include "absl/strings/internal/str_format/extension.h"

#include <errno.h>
#include <algorithm>
#include <string>

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace str_format_internal {

std::string FlagsToString(Flags v) {
  std::string s;
  s.append(FlagsContains(v, Flags::kLeft) ? "-" : "");
  s.append(FlagsContains(v, Flags::kShowPos) ? "+" : "");
  s.append(FlagsContains(v, Flags::kSignCol) ? " " : "");
  s.append(FlagsContains(v, Flags::kAlt) ? "#" : "");
  s.append(FlagsContains(v, Flags::kZero) ? "0" : "");
  return s;
}

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL

#define ABSL_INTERNAL_X_VAL(id) \
  constexpr absl::FormatConversionChar FormatConversionCharInternal::id;
ABSL_INTERNAL_CONVERSION_CHARS_EXPAND_(ABSL_INTERNAL_X_VAL, )
#undef ABSL_INTERNAL_X_VAL
// NOLINTNEXTLINE(readability-redundant-declaration)
constexpr absl::FormatConversionChar FormatConversionCharInternal::kNone;

#define ABSL_INTERNAL_CHAR_SET_CASE(c) \
  constexpr FormatConversionCharSet FormatConversionCharSetInternal::c;
ABSL_INTERNAL_CONVERSION_CHARS_EXPAND_(ABSL_INTERNAL_CHAR_SET_CASE, )
#undef ABSL_INTERNAL_CHAR_SET_CASE

constexpr FormatConversionCharSet FormatConversionCharSetInternal::kStar;
constexpr FormatConversionCharSet FormatConversionCharSetInternal::kIntegral;
constexpr FormatConversionCharSet FormatConversionCharSetInternal::kFloating;
constexpr FormatConversionCharSet FormatConversionCharSetInternal::kNumeric;
constexpr FormatConversionCharSet FormatConversionCharSetInternal::kPointer;

#endif  // ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL

bool FormatSinkImpl::PutPaddedString(string_view value, int width,
                                     int precision, bool left) {
  size_t space_remaining = 0;
  if (width >= 0)
    space_remaining = static_cast<size_t>(width);
  size_t n = value.size();
  if (precision >= 0) n = std::min(n, static_cast<size_t>(precision));
  string_view shown(value.data(), n);
  space_remaining = Excess(shown.size(), space_remaining);
  if (!left) Append(space_remaining, ' ');
  Append(shown);
  if (left) Append(space_remaining, ' ');
  return true;
}

}  // namespace str_format_internal
ABSL_NAMESPACE_END
}  // namespace absl
