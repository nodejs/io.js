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

#ifndef ABSL_STRINGS_INTERNAL_STR_FORMAT_PARSER_H_
#define ABSL_STRINGS_INTERNAL_STR_FORMAT_PARSER_H_

#include <stddef.h>
#include <stdlib.h>

#include <cassert>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/strings/internal/str_format/checker.h"
#include "absl/strings/internal/str_format/constexpr_parser.h"
#include "absl/strings/internal/str_format/extension.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace str_format_internal {

std::string LengthModToString(LengthMod v);

const char* ConsumeUnboundConversionNoInline(const char* p, const char* end,
                                             UnboundConversion* conv,
                                             int* next_arg);

// Parse the format string provided in 'src' and pass the identified items into
// 'consumer'.
// Text runs will be passed by calling
//   Consumer::Append(string_view);
// ConversionItems will be passed by calling
//   Consumer::ConvertOne(UnboundConversion, string_view);
// In the case of ConvertOne, the string_view that is passed is the
// portion of the format string corresponding to the conversion, not including
// the leading %. On success, it returns true. On failure, it stops and returns
// false.
template <typename Consumer>
bool ParseFormatString(string_view src, Consumer consumer) {
  int next_arg = 0;
  const char* p = src.data();
  const char* const end = p + src.size();
  while (p != end) {
    const char* percent =
        static_cast<const char*>(memchr(p, '%', static_cast<size_t>(end - p)));
    if (!percent) {
      // We found the last substring.
      return consumer.Append(string_view(p, static_cast<size_t>(end - p)));
    }
    // We found a percent, so push the text run then process the percent.
    if (ABSL_PREDICT_FALSE(!consumer.Append(
            string_view(p, static_cast<size_t>(percent - p))))) {
      return false;
    }
    if (ABSL_PREDICT_FALSE(percent + 1 >= end)) return false;

    auto tag = GetTagForChar(percent[1]);
    if (tag.is_conv()) {
      if (ABSL_PREDICT_FALSE(next_arg < 0)) {
        // This indicates an error in the format string.
        // The only way to get `next_arg < 0` here is to have a positional
        // argument first which sets next_arg to -1 and then a non-positional
        // argument.
        return false;
      }
      p = percent + 2;

      // Keep this case separate from the one below.
      // ConvertOne is more efficient when the compiler can see that the `basic`
      // flag is set.
      UnboundConversion conv;
      conv.conv = tag.as_conv();
      conv.arg_position = ++next_arg;
      if (ABSL_PREDICT_FALSE(
              !consumer.ConvertOne(conv, string_view(percent + 1, 1)))) {
        return false;
      }
    } else if (percent[1] != '%') {
      UnboundConversion conv;
      p = ConsumeUnboundConversionNoInline(percent + 1, end, &conv, &next_arg);
      if (ABSL_PREDICT_FALSE(p == nullptr)) return false;
      if (ABSL_PREDICT_FALSE(!consumer.ConvertOne(
              conv, string_view(percent + 1,
                                static_cast<size_t>(p - (percent + 1)))))) {
        return false;
      }
    } else {
      if (ABSL_PREDICT_FALSE(!consumer.Append("%"))) return false;
      p = percent + 2;
      continue;
    }
  }
  return true;
}

// Always returns true, or fails to compile in a constexpr context if s does not
// point to a constexpr char array.
constexpr bool EnsureConstexpr(string_view s) {
  return s.empty() || s[0] == s[0];
}

class ParsedFormatBase {
 public:
  explicit ParsedFormatBase(
      string_view format, bool allow_ignored,
      std::initializer_list<FormatConversionCharSet> convs);

  ParsedFormatBase(const ParsedFormatBase& other) { *this = other; }

  ParsedFormatBase(ParsedFormatBase&& other) { *this = std::move(other); }

  ParsedFormatBase& operator=(const ParsedFormatBase& other) {
    if (this == &other) return *this;
    has_error_ = other.has_error_;
    items_ = other.items_;
    size_t text_size = items_.empty() ? 0 : items_.back().text_end;
    data_.reset(new char[text_size]);
    memcpy(data_.get(), other.data_.get(), text_size);
    return *this;
  }

  ParsedFormatBase& operator=(ParsedFormatBase&& other) {
    if (this == &other) return *this;
    has_error_ = other.has_error_;
    data_ = std::move(other.data_);
    items_ = std::move(other.items_);
    // Reset the vector to make sure the invariants hold.
    other.items_.clear();
    return *this;
  }

  template <typename Consumer>
  bool ProcessFormat(Consumer consumer) const {
    const char* const base = data_.get();
    string_view text(base, 0);
    for (const auto& item : items_) {
      const char* const end = text.data() + text.size();
      text =
          string_view(end, static_cast<size_t>((base + item.text_end) - end));
      if (item.is_conversion) {
        if (!consumer.ConvertOne(item.conv, text)) return false;
      } else {
        if (!consumer.Append(text)) return false;
      }
    }
    return !has_error_;
  }

  bool has_error() const { return has_error_; }

 private:
  // Returns whether the conversions match and if !allow_ignored it verifies
  // that all conversions are used by the format.
  bool MatchesConversions(
      bool allow_ignored,
      std::initializer_list<FormatConversionCharSet> convs) const;

  struct ParsedFormatConsumer;

  struct ConversionItem {
    bool is_conversion;
    // Points to the past-the-end location of this element in the data_ array.
    size_t text_end;
    UnboundConversion conv;
  };

  bool has_error_;
  std::unique_ptr<char[]> data_;
  std::vector<ConversionItem> items_;
};


// A value type representing a preparsed format.  These can be created, copied
// around, and reused to speed up formatting loops.
// The user must specify through the template arguments the conversion
// characters used in the format. This will be checked at compile time.
//
// This class uses Conv enum values to specify each argument.
// This allows for more flexibility as you can specify multiple possible
// conversion characters for each argument.
// ParsedFormat<char...> is a simplified alias for when the user only
// needs to specify a single conversion character for each argument.
//
// Example:
//   // Extended format supports multiple characters per argument:
//   using MyFormat = ExtendedParsedFormat<Conv::d | Conv::x>;
//   MyFormat GetFormat(bool use_hex) {
//     if (use_hex) return MyFormat("foo %x bar");
//     return MyFormat("foo %d bar");
//   }
//   // 'format' can be used with any value that supports 'd' and 'x',
//   // like `int`.
//   auto format = GetFormat(use_hex);
//   value = StringF(format, i);
//
// This class also supports runtime format checking with the ::New() and
// ::NewAllowIgnored() factory functions.
// This is the only API that allows the user to pass a runtime specified format
// string. These factory functions will return NULL if the format does not match
// the conversions requested by the user.
template <FormatConversionCharSet... C>
class ExtendedParsedFormat : public str_format_internal::ParsedFormatBase {
 public:
  explicit ExtendedParsedFormat(string_view format)
#ifdef ABSL_INTERNAL_ENABLE_FORMAT_CHECKER
      __attribute__((
          enable_if(str_format_internal::EnsureConstexpr(format),
                    "Format string is not constexpr."),
          enable_if(str_format_internal::ValidFormatImpl<C...>(format),
                    "Format specified does not match the template arguments.")))
#endif  // ABSL_INTERNAL_ENABLE_FORMAT_CHECKER
      : ExtendedParsedFormat(format, false) {
  }

  // ExtendedParsedFormat factory function.
  // The user still has to specify the conversion characters, but they will not
  // be checked at compile time. Instead, it will be checked at runtime.
  // This delays the checking to runtime, but allows the user to pass
  // dynamically sourced formats.
  // It returns NULL if the format does not match the conversion characters.
  // The user is responsible for checking the return value before using it.
  //
  // The 'New' variant will check that all the specified arguments are being
  // consumed by the format and return NULL if any argument is being ignored.
  // The 'NewAllowIgnored' variant will not verify this and will allow formats
  // that ignore arguments.
  static std::unique_ptr<ExtendedParsedFormat> New(string_view format) {
    return New(format, false);
  }
  static std::unique_ptr<ExtendedParsedFormat> NewAllowIgnored(
      string_view format) {
    return New(format, true);
  }

 private:
  static std::unique_ptr<ExtendedParsedFormat> New(string_view format,
                                                   bool allow_ignored) {
    std::unique_ptr<ExtendedParsedFormat> conv(
        new ExtendedParsedFormat(format, allow_ignored));
    if (conv->has_error()) return nullptr;
    return conv;
  }

  ExtendedParsedFormat(string_view s, bool allow_ignored)
      : ParsedFormatBase(s, allow_ignored, {C...}) {}
};
}  // namespace str_format_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_STR_FORMAT_PARSER_H_
