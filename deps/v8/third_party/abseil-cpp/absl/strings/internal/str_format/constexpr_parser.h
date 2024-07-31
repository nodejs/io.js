// Copyright 2022 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_STRINGS_INTERNAL_STR_FORMAT_CONSTEXPR_PARSER_H_
#define ABSL_STRINGS_INTERNAL_STR_FORMAT_CONSTEXPR_PARSER_H_

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "absl/base/config.h"
#include "absl/base/const_init.h"
#include "absl/base/optimization.h"
#include "absl/strings/internal/str_format/extension.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace str_format_internal {

// The analyzed properties of a single specified conversion.
struct UnboundConversion {
  // This is a user defined default constructor on purpose to skip the
  // initialization of parts of the object that are not necessary.
  UnboundConversion() {}  // NOLINT

  // This constructor is provided for the static checker. We don't want to do
  // the unnecessary initialization in the normal case.
  explicit constexpr UnboundConversion(absl::ConstInitType)
      : arg_position{}, width{}, precision{} {}

  class InputValue {
   public:
    constexpr void set_value(int value) {
      assert(value >= 0);
      value_ = value;
    }
    constexpr int value() const { return value_; }

    // Marks the value as "from arg". aka the '*' format.
    // Requires `value >= 1`.
    // When set, is_from_arg() return true and get_from_arg() returns the
    // original value.
    // `value()`'s return value is unspecified in this state.
    constexpr void set_from_arg(int value) {
      assert(value > 0);
      value_ = -value - 1;
    }
    constexpr bool is_from_arg() const { return value_ < -1; }
    constexpr int get_from_arg() const {
      assert(is_from_arg());
      return -value_ - 1;
    }

   private:
    int value_ = -1;
  };

  // No need to initialize. It will always be set in the parser.
  int arg_position;

  InputValue width;
  InputValue precision;

  Flags flags = Flags::kBasic;
  LengthMod length_mod = LengthMod::none;
  FormatConversionChar conv = FormatConversionCharInternal::kNone;
};

// Helper tag class for the table below.
// It allows fast `char -> ConversionChar/LengthMod/Flags` checking and
// conversions.
class ConvTag {
 public:
  constexpr ConvTag(FormatConversionChar conversion_char)  // NOLINT
      : tag_(static_cast<uint8_t>(conversion_char)) {}
  constexpr ConvTag(LengthMod length_mod)  // NOLINT
      : tag_(0x80 | static_cast<uint8_t>(length_mod)) {}
  constexpr ConvTag(Flags flags)  // NOLINT
      : tag_(0xc0 | static_cast<uint8_t>(flags)) {}
  constexpr ConvTag() : tag_(0xFF) {}

  constexpr bool is_conv() const { return (tag_ & 0x80) == 0; }
  constexpr bool is_length() const { return (tag_ & 0xC0) == 0x80; }
  constexpr bool is_flags() const { return (tag_ & 0xE0) == 0xC0; }

  constexpr FormatConversionChar as_conv() const {
    assert(is_conv());
    assert(!is_length());
    assert(!is_flags());
    return static_cast<FormatConversionChar>(tag_);
  }
  constexpr LengthMod as_length() const {
    assert(!is_conv());
    assert(is_length());
    assert(!is_flags());
    return static_cast<LengthMod>(tag_ & 0x3F);
  }
  constexpr Flags as_flags() const {
    assert(!is_conv());
    assert(!is_length());
    assert(is_flags());
    return static_cast<Flags>(tag_ & 0x1F);
  }

 private:
  uint8_t tag_;
};

struct ConvTagHolder {
  using CC = FormatConversionCharInternal;
  using LM = LengthMod;

  // Abbreviations to fit in the table below.
  static constexpr auto kFSign = Flags::kSignCol;
  static constexpr auto kFAlt = Flags::kAlt;
  static constexpr auto kFPos = Flags::kShowPos;
  static constexpr auto kFLeft = Flags::kLeft;
  static constexpr auto kFZero = Flags::kZero;

  static constexpr ConvTag value[256] = {
      {},     {},    {},    {},    {},    {},     {},    {},     // 00-07
      {},     {},    {},    {},    {},    {},     {},    {},     // 08-0f
      {},     {},    {},    {},    {},    {},     {},    {},     // 10-17
      {},     {},    {},    {},    {},    {},     {},    {},     // 18-1f
      kFSign, {},    {},    kFAlt, {},    {},     {},    {},     //  !"#$%&'
      {},     {},    {},    kFPos, {},    kFLeft, {},    {},     // ()*+,-./
      kFZero, {},    {},    {},    {},    {},     {},    {},     // 01234567
      {},     {},    {},    {},    {},    {},     {},    {},     // 89:;<=>?
      {},     CC::A, {},    {},    {},    CC::E,  CC::F, CC::G,  // @ABCDEFG
      {},     {},    {},    {},    LM::L, {},     {},    {},     // HIJKLMNO
      {},     {},    {},    {},    {},    {},     {},    {},     // PQRSTUVW
      CC::X,  {},    {},    {},    {},    {},     {},    {},     // XYZ[\]^_
      {},     CC::a, {},    CC::c, CC::d, CC::e,  CC::f, CC::g,  // `abcdefg
      LM::h,  CC::i, LM::j, {},    LM::l, {},     CC::n, CC::o,  // hijklmno
      CC::p,  LM::q, {},    CC::s, LM::t, CC::u,  CC::v, {},     // pqrstuvw
      CC::x,  {},    LM::z, {},    {},    {},     {},    {},     // xyz{|}!
      {},     {},    {},    {},    {},    {},     {},    {},     // 80-87
      {},     {},    {},    {},    {},    {},     {},    {},     // 88-8f
      {},     {},    {},    {},    {},    {},     {},    {},     // 90-97
      {},     {},    {},    {},    {},    {},     {},    {},     // 98-9f
      {},     {},    {},    {},    {},    {},     {},    {},     // a0-a7
      {},     {},    {},    {},    {},    {},     {},    {},     // a8-af
      {},     {},    {},    {},    {},    {},     {},    {},     // b0-b7
      {},     {},    {},    {},    {},    {},     {},    {},     // b8-bf
      {},     {},    {},    {},    {},    {},     {},    {},     // c0-c7
      {},     {},    {},    {},    {},    {},     {},    {},     // c8-cf
      {},     {},    {},    {},    {},    {},     {},    {},     // d0-d7
      {},     {},    {},    {},    {},    {},     {},    {},     // d8-df
      {},     {},    {},    {},    {},    {},     {},    {},     // e0-e7
      {},     {},    {},    {},    {},    {},     {},    {},     // e8-ef
      {},     {},    {},    {},    {},    {},     {},    {},     // f0-f7
      {},     {},    {},    {},    {},    {},     {},    {},     // f8-ff
  };
};

// Keep a single table for all the conversion chars and length modifiers.
constexpr ConvTag GetTagForChar(char c) {
  return ConvTagHolder::value[static_cast<unsigned char>(c)];
}

constexpr bool CheckFastPathSetting(const UnboundConversion& conv) {
  bool width_precision_needed =
      conv.width.value() >= 0 || conv.precision.value() >= 0;
  if (width_precision_needed && conv.flags == Flags::kBasic) {
#if defined(__clang__)
    // Some compilers complain about this in constexpr even when not executed,
    // so only enable the error dump in clang.
    fprintf(stderr,
            "basic=%d left=%d show_pos=%d sign_col=%d alt=%d zero=%d "
            "width=%d precision=%d\n",
            conv.flags == Flags::kBasic ? 1 : 0,
            FlagsContains(conv.flags, Flags::kLeft) ? 1 : 0,
            FlagsContains(conv.flags, Flags::kShowPos) ? 1 : 0,
            FlagsContains(conv.flags, Flags::kSignCol) ? 1 : 0,
            FlagsContains(conv.flags, Flags::kAlt) ? 1 : 0,
            FlagsContains(conv.flags, Flags::kZero) ? 1 : 0, conv.width.value(),
            conv.precision.value());
#endif  // defined(__clang__)
    return false;
  }
  return true;
}

constexpr int ParseDigits(char& c, const char*& pos, const char* const end) {
  int digits = c - '0';
  // We do not want to overflow `digits` so we consume at most digits10
  // digits. If there are more digits the parsing will fail later on when the
  // digit doesn't match the expected characters.
  int num_digits = std::numeric_limits<int>::digits10;
  for (;;) {
    if (ABSL_PREDICT_FALSE(pos == end)) break;
    c = *pos++;
    if ('0' > c || c > '9') break;
    --num_digits;
    if (ABSL_PREDICT_FALSE(!num_digits)) break;
    digits = 10 * digits + c - '0';
  }
  return digits;
}

template <bool is_positional>
constexpr const char* ConsumeConversion(const char* pos, const char* const end,
                                        UnboundConversion* conv,
                                        int* next_arg) {
  const char* const original_pos = pos;
  char c = 0;
  // Read the next char into `c` and update `pos`. Returns false if there are
  // no more chars to read.
#define ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR()          \
  do {                                                  \
    if (ABSL_PREDICT_FALSE(pos == end)) return nullptr; \
    c = *pos++;                                         \
  } while (0)

  if (is_positional) {
    ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR();
    if (ABSL_PREDICT_FALSE(c < '1' || c > '9')) return nullptr;
    conv->arg_position = ParseDigits(c, pos, end);
    assert(conv->arg_position > 0);
    if (ABSL_PREDICT_FALSE(c != '$')) return nullptr;
  }

  ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR();

  // We should start with the basic flag on.
  assert(conv->flags == Flags::kBasic);

  // Any non alpha character makes this conversion not basic.
  // This includes flags (-+ #0), width (1-9, *) or precision (.).
  // All conversion characters and length modifiers are alpha characters.
  if (c < 'A') {
    while (c <= '0') {
      auto tag = GetTagForChar(c);
      if (tag.is_flags()) {
        conv->flags = conv->flags | tag.as_flags();
        ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR();
      } else {
        break;
      }
    }

    if (c <= '9') {
      if (c >= '0') {
        int maybe_width = ParseDigits(c, pos, end);
        if (!is_positional && c == '$') {
          if (ABSL_PREDICT_FALSE(*next_arg != 0)) return nullptr;
          // Positional conversion.
          *next_arg = -1;
          return ConsumeConversion<true>(original_pos, end, conv, next_arg);
        }
        conv->flags = conv->flags | Flags::kNonBasic;
        conv->width.set_value(maybe_width);
      } else if (c == '*') {
        conv->flags = conv->flags | Flags::kNonBasic;
        ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR();
        if (is_positional) {
          if (ABSL_PREDICT_FALSE(c < '1' || c > '9')) return nullptr;
          conv->width.set_from_arg(ParseDigits(c, pos, end));
          if (ABSL_PREDICT_FALSE(c != '$')) return nullptr;
          ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR();
        } else {
          conv->width.set_from_arg(++*next_arg);
        }
      }
    }

    if (c == '.') {
      conv->flags = conv->flags | Flags::kNonBasic;
      ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR();
      if ('0' <= c && c <= '9') {
        conv->precision.set_value(ParseDigits(c, pos, end));
      } else if (c == '*') {
        ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR();
        if (is_positional) {
          if (ABSL_PREDICT_FALSE(c < '1' || c > '9')) return nullptr;
          conv->precision.set_from_arg(ParseDigits(c, pos, end));
          if (c != '$') return nullptr;
          ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR();
        } else {
          conv->precision.set_from_arg(++*next_arg);
        }
      } else {
        conv->precision.set_value(0);
      }
    }
  }

  auto tag = GetTagForChar(c);

  if (ABSL_PREDICT_FALSE(c == 'v' && conv->flags != Flags::kBasic)) {
    return nullptr;
  }

  if (ABSL_PREDICT_FALSE(!tag.is_conv())) {
    if (ABSL_PREDICT_FALSE(!tag.is_length())) return nullptr;

    // It is a length modifier.
    LengthMod length_mod = tag.as_length();
    ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR();
    if (c == 'h' && length_mod == LengthMod::h) {
      conv->length_mod = LengthMod::hh;
      ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR();
    } else if (c == 'l' && length_mod == LengthMod::l) {
      conv->length_mod = LengthMod::ll;
      ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR();
    } else {
      conv->length_mod = length_mod;
    }
    tag = GetTagForChar(c);

    if (ABSL_PREDICT_FALSE(c == 'v')) return nullptr;
    if (ABSL_PREDICT_FALSE(!tag.is_conv())) return nullptr;

    // `wchar_t` args are marked non-basic so `Bind()` will copy the length mod.
    if (conv->length_mod == LengthMod::l && c == 'c') {
      conv->flags = conv->flags | Flags::kNonBasic;
    }
  }
#undef ABSL_FORMAT_PARSER_INTERNAL_GET_CHAR

  assert(CheckFastPathSetting(*conv));
  (void)(&CheckFastPathSetting);

  conv->conv = tag.as_conv();
  if (!is_positional) conv->arg_position = ++*next_arg;
  return pos;
}

// Consume conversion spec prefix (not including '%') of [p, end) if valid.
// Examples of valid specs would be e.g.: "s", "d", "-12.6f".
// If valid, it returns the first character following the conversion spec,
// and the spec part is broken down and returned in 'conv'.
// If invalid, returns nullptr.
constexpr const char* ConsumeUnboundConversion(const char* p, const char* end,
                                               UnboundConversion* conv,
                                               int* next_arg) {
  if (*next_arg < 0) return ConsumeConversion<true>(p, end, conv, next_arg);
  return ConsumeConversion<false>(p, end, conv, next_arg);
}

}  // namespace str_format_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_STR_FORMAT_CONSTEXPR_PARSER_H_
