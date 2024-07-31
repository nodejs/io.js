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

#include "absl/strings/internal/str_format/parser.h"

#include <assert.h>
#include <string.h>
#include <wchar.h>
#include <cctype>
#include <cstdint>

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <ostream>
#include <string>
#include <unordered_set>

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace str_format_internal {

// Define the array for non-constexpr uses.
constexpr ConvTag ConvTagHolder::value[256];

ABSL_ATTRIBUTE_NOINLINE const char* ConsumeUnboundConversionNoInline(
    const char* p, const char* end, UnboundConversion* conv, int* next_arg) {
  return ConsumeUnboundConversion(p, end, conv, next_arg);
}

std::string LengthModToString(LengthMod v) {
  switch (v) {
    case LengthMod::h:
      return "h";
    case LengthMod::hh:
      return "hh";
    case LengthMod::l:
      return "l";
    case LengthMod::ll:
      return "ll";
    case LengthMod::L:
      return "L";
    case LengthMod::j:
      return "j";
    case LengthMod::z:
      return "z";
    case LengthMod::t:
      return "t";
    case LengthMod::q:
      return "q";
    case LengthMod::none:
      return "";
  }
  return "";
}

struct ParsedFormatBase::ParsedFormatConsumer {
  explicit ParsedFormatConsumer(ParsedFormatBase *parsedformat)
      : parsed(parsedformat), data_pos(parsedformat->data_.get()) {}

  bool Append(string_view s) {
    if (s.empty()) return true;

    size_t text_end = AppendText(s);

    if (!parsed->items_.empty() && !parsed->items_.back().is_conversion) {
      // Let's extend the existing text run.
      parsed->items_.back().text_end = text_end;
    } else {
      // Let's make a new text run.
      parsed->items_.push_back({false, text_end, {}});
    }
    return true;
  }

  bool ConvertOne(const UnboundConversion &conv, string_view s) {
    size_t text_end = AppendText(s);
    parsed->items_.push_back({true, text_end, conv});
    return true;
  }

  size_t AppendText(string_view s) {
    memcpy(data_pos, s.data(), s.size());
    data_pos += s.size();
    return static_cast<size_t>(data_pos - parsed->data_.get());
  }

  ParsedFormatBase *parsed;
  char* data_pos;
};

ParsedFormatBase::ParsedFormatBase(
    string_view format, bool allow_ignored,
    std::initializer_list<FormatConversionCharSet> convs)
    : data_(format.empty() ? nullptr : new char[format.size()]) {
  has_error_ = !ParseFormatString(format, ParsedFormatConsumer(this)) ||
               !MatchesConversions(allow_ignored, convs);
}

bool ParsedFormatBase::MatchesConversions(
    bool allow_ignored,
    std::initializer_list<FormatConversionCharSet> convs) const {
  std::unordered_set<int> used;
  auto add_if_valid_conv = [&](int pos, char c) {
    if (static_cast<size_t>(pos) > convs.size() ||
        !Contains(convs.begin()[pos - 1], c))
      return false;
    used.insert(pos);
    return true;
  };
  for (const ConversionItem &item : items_) {
    if (!item.is_conversion) continue;
    auto &conv = item.conv;
    if (conv.precision.is_from_arg() &&
        !add_if_valid_conv(conv.precision.get_from_arg(), '*'))
      return false;
    if (conv.width.is_from_arg() &&
        !add_if_valid_conv(conv.width.get_from_arg(), '*'))
      return false;
    if (!add_if_valid_conv(conv.arg_position,
                           FormatConversionCharToChar(conv.conv)))
      return false;
  }
  return used.size() == convs.size() || allow_ignored;
}

}  // namespace str_format_internal
ABSL_NAMESPACE_END
}  // namespace absl
