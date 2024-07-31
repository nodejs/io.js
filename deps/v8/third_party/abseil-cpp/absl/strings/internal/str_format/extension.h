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
//
#ifndef ABSL_STRINGS_INTERNAL_STR_FORMAT_EXTENSION_H_
#define ABSL_STRINGS_INTERNAL_STR_FORMAT_EXTENSION_H_


#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>

#include "absl/base/config.h"
#include "absl/strings/internal/str_format/output.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

enum class FormatConversionChar : uint8_t;
enum class FormatConversionCharSet : uint64_t;
enum class LengthMod : std::uint8_t { h, hh, l, ll, L, j, z, t, q, none };

namespace str_format_internal {

class FormatRawSinkImpl {
 public:
  // Implicitly convert from any type that provides the hook function as
  // described above.
  template <typename T, decltype(str_format_internal::InvokeFlush(
                            std::declval<T*>(), string_view()))* = nullptr>
  FormatRawSinkImpl(T* raw)  // NOLINT
      : sink_(raw), write_(&FormatRawSinkImpl::Flush<T>) {}

  void Write(string_view s) { write_(sink_, s); }

  template <typename T>
  static FormatRawSinkImpl Extract(T s) {
    return s.sink_;
  }

 private:
  template <typename T>
  static void Flush(void* r, string_view s) {
    str_format_internal::InvokeFlush(static_cast<T*>(r), s);
  }

  void* sink_;
  void (*write_)(void*, string_view);
};

// An abstraction to which conversions write their string data.
class FormatSinkImpl {
 public:
  explicit FormatSinkImpl(FormatRawSinkImpl raw) : raw_(raw) {}

  ~FormatSinkImpl() { Flush(); }

  void Flush() {
    raw_.Write(string_view(buf_, static_cast<size_t>(pos_ - buf_)));
    pos_ = buf_;
  }

  void Append(size_t n, char c) {
    if (n == 0) return;
    size_ += n;
    auto raw_append = [&](size_t count) {
      memset(pos_, c, count);
      pos_ += count;
    };
    while (n > Avail()) {
      n -= Avail();
      if (Avail() > 0) {
        raw_append(Avail());
      }
      Flush();
    }
    raw_append(n);
  }

  void Append(string_view v) {
    size_t n = v.size();
    if (n == 0) return;
    size_ += n;
    if (n >= Avail()) {
      Flush();
      raw_.Write(v);
      return;
    }
    memcpy(pos_, v.data(), n);
    pos_ += n;
  }

  size_t size() const { return size_; }

  // Put 'v' to 'sink' with specified width, precision, and left flag.
  bool PutPaddedString(string_view v, int width, int precision, bool left);

  template <typename T>
  T Wrap() {
    return T(this);
  }

  template <typename T>
  static FormatSinkImpl* Extract(T* s) {
    return s->sink_;
  }

 private:
  size_t Avail() const {
    return static_cast<size_t>(buf_ + sizeof(buf_) - pos_);
  }

  FormatRawSinkImpl raw_;
  size_t size_ = 0;
  char* pos_ = buf_;
  char buf_[1024];
};

enum class Flags : uint8_t {
  kBasic = 0,
  kLeft = 1 << 0,
  kShowPos = 1 << 1,
  kSignCol = 1 << 2,
  kAlt = 1 << 3,
  kZero = 1 << 4,
  // This is not a real flag. It just exists to turn off kBasic when no other
  // flags are set. This is for when width/precision are specified, or a length
  // modifier affects the behavior ("%lc").
  kNonBasic = 1 << 5,
};

constexpr Flags operator|(Flags a, Flags b) {
  return static_cast<Flags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr bool FlagsContains(Flags haystack, Flags needle) {
  return (static_cast<uint8_t>(haystack) & static_cast<uint8_t>(needle)) ==
         static_cast<uint8_t>(needle);
}

std::string FlagsToString(Flags v);

inline std::ostream& operator<<(std::ostream& os, Flags v) {
  return os << FlagsToString(v);
}

// clang-format off
#define ABSL_INTERNAL_CONVERSION_CHARS_EXPAND_(X_VAL, X_SEP) \
  /* text */ \
  X_VAL(c) X_SEP X_VAL(s) X_SEP \
  /* ints */ \
  X_VAL(d) X_SEP X_VAL(i) X_SEP X_VAL(o) X_SEP \
  X_VAL(u) X_SEP X_VAL(x) X_SEP X_VAL(X) X_SEP \
  /* floats */ \
  X_VAL(f) X_SEP X_VAL(F) X_SEP X_VAL(e) X_SEP X_VAL(E) X_SEP \
  X_VAL(g) X_SEP X_VAL(G) X_SEP X_VAL(a) X_SEP X_VAL(A) X_SEP \
  /* misc */ \
  X_VAL(n) X_SEP X_VAL(p) X_SEP X_VAL(v)
// clang-format on

// This type should not be referenced, it exists only to provide labels
// internally that match the values declared in FormatConversionChar in
// str_format.h. This is meant to allow internal libraries to use the same
// declared interface type as the public interface
// (absl::StrFormatConversionChar) while keeping the definition in a public
// header.
// Internal libraries should use the form
// `FormatConversionCharInternal::c`, `FormatConversionCharInternal::kNone` for
// comparisons.  Use in switch statements is not recommended due to a bug in how
// gcc 4.9 -Wswitch handles declared but undefined enums.
struct FormatConversionCharInternal {
  FormatConversionCharInternal() = delete;

 private:
  // clang-format off
  enum class Enum : uint8_t {
    c, s,                    // text
    d, i, o, u, x, X,        // int
    f, F, e, E, g, G, a, A,  // float
    n, p, v,                    // misc
    kNone
  };
  // clang-format on
 public:
#define ABSL_INTERNAL_X_VAL(id)              \
  static constexpr FormatConversionChar id = \
      static_cast<FormatConversionChar>(Enum::id);
  ABSL_INTERNAL_CONVERSION_CHARS_EXPAND_(ABSL_INTERNAL_X_VAL, )
#undef ABSL_INTERNAL_X_VAL
  static constexpr FormatConversionChar kNone =
      static_cast<FormatConversionChar>(Enum::kNone);
};
// clang-format on

inline FormatConversionChar FormatConversionCharFromChar(char c) {
  switch (c) {
#define ABSL_INTERNAL_X_VAL(id) \
  case #id[0]:                  \
    return FormatConversionCharInternal::id;
    ABSL_INTERNAL_CONVERSION_CHARS_EXPAND_(ABSL_INTERNAL_X_VAL, )
#undef ABSL_INTERNAL_X_VAL
  }
  return FormatConversionCharInternal::kNone;
}

inline bool FormatConversionCharIsUpper(FormatConversionChar c) {
  if (c == FormatConversionCharInternal::X ||
      c == FormatConversionCharInternal::F ||
      c == FormatConversionCharInternal::E ||
      c == FormatConversionCharInternal::G ||
      c == FormatConversionCharInternal::A) {
    return true;
  } else {
    return false;
  }
}

inline bool FormatConversionCharIsFloat(FormatConversionChar c) {
  if (c == FormatConversionCharInternal::a ||
      c == FormatConversionCharInternal::e ||
      c == FormatConversionCharInternal::f ||
      c == FormatConversionCharInternal::g ||
      c == FormatConversionCharInternal::A ||
      c == FormatConversionCharInternal::E ||
      c == FormatConversionCharInternal::F ||
      c == FormatConversionCharInternal::G) {
    return true;
  } else {
    return false;
  }
}

inline char FormatConversionCharToChar(FormatConversionChar c) {
  if (c == FormatConversionCharInternal::kNone) {
    return '\0';

#define ABSL_INTERNAL_X_VAL(e)                       \
  } else if (c == FormatConversionCharInternal::e) { \
    return #e[0];
#define ABSL_INTERNAL_X_SEP
  ABSL_INTERNAL_CONVERSION_CHARS_EXPAND_(ABSL_INTERNAL_X_VAL,
                                         ABSL_INTERNAL_X_SEP)
  } else {
    return '\0';
  }

#undef ABSL_INTERNAL_X_VAL
#undef ABSL_INTERNAL_X_SEP
}

// The associated char.
inline std::ostream& operator<<(std::ostream& os, FormatConversionChar v) {
  char c = FormatConversionCharToChar(v);
  if (!c) c = '?';
  return os << c;
}

struct FormatConversionSpecImplFriend;

class FormatConversionSpecImpl {
 public:
  // Width and precision are not specified, no flags are set.
  bool is_basic() const { return flags_ == Flags::kBasic; }
  bool has_left_flag() const { return FlagsContains(flags_, Flags::kLeft); }
  bool has_show_pos_flag() const {
    return FlagsContains(flags_, Flags::kShowPos);
  }
  bool has_sign_col_flag() const {
    return FlagsContains(flags_, Flags::kSignCol);
  }
  bool has_alt_flag() const { return FlagsContains(flags_, Flags::kAlt); }
  bool has_zero_flag() const { return FlagsContains(flags_, Flags::kZero); }

  LengthMod length_mod() const { return length_mod_; }

  FormatConversionChar conversion_char() const {
    // Keep this field first in the struct . It generates better code when
    // accessing it when ConversionSpec is passed by value in registers.
    static_assert(offsetof(FormatConversionSpecImpl, conv_) == 0, "");
    return conv_;
  }

  void set_conversion_char(FormatConversionChar c) { conv_ = c; }

  // Returns the specified width. If width is unspecfied, it returns a negative
  // value.
  int width() const { return width_; }
  // Returns the specified precision. If precision is unspecfied, it returns a
  // negative value.
  int precision() const { return precision_; }

  template <typename T>
  T Wrap() {
    return T(*this);
  }

 private:
  friend struct str_format_internal::FormatConversionSpecImplFriend;
  FormatConversionChar conv_ = FormatConversionCharInternal::kNone;
  Flags flags_;
  LengthMod length_mod_ = LengthMod::none;
  int width_;
  int precision_;
};

struct FormatConversionSpecImplFriend final {
  static void SetFlags(Flags f, FormatConversionSpecImpl* conv) {
    conv->flags_ = f;
  }
  static void SetLengthMod(LengthMod l, FormatConversionSpecImpl* conv) {
    conv->length_mod_ = l;
  }
  static void SetConversionChar(FormatConversionChar c,
                                FormatConversionSpecImpl* conv) {
    conv->conv_ = c;
  }
  static void SetWidth(int w, FormatConversionSpecImpl* conv) {
    conv->width_ = w;
  }
  static void SetPrecision(int p, FormatConversionSpecImpl* conv) {
    conv->precision_ = p;
  }
  static std::string FlagsToString(const FormatConversionSpecImpl& spec) {
    return str_format_internal::FlagsToString(spec.flags_);
  }
};

// Type safe OR operator.
// We need this for two reasons:
//  1. operator| on enums makes them decay to integers and the result is an
//     integer. We need the result to stay as an enum.
//  2. We use "enum class" which would not work even if we accepted the decay.
constexpr FormatConversionCharSet FormatConversionCharSetUnion(
    FormatConversionCharSet a) {
  return a;
}

template <typename... CharSet>
constexpr FormatConversionCharSet FormatConversionCharSetUnion(
    FormatConversionCharSet a, CharSet... rest) {
  return static_cast<FormatConversionCharSet>(
      static_cast<uint64_t>(a) |
      static_cast<uint64_t>(FormatConversionCharSetUnion(rest...)));
}

constexpr uint64_t FormatConversionCharToConvInt(FormatConversionChar c) {
  return uint64_t{1} << (1 + static_cast<uint8_t>(c));
}

constexpr uint64_t FormatConversionCharToConvInt(char conv) {
  return
#define ABSL_INTERNAL_CHAR_SET_CASE(c)                                 \
  conv == #c[0]                                                        \
      ? FormatConversionCharToConvInt(FormatConversionCharInternal::c) \
      :
      ABSL_INTERNAL_CONVERSION_CHARS_EXPAND_(ABSL_INTERNAL_CHAR_SET_CASE, )
#undef ABSL_INTERNAL_CHAR_SET_CASE
                  conv == '*'
          ? 1
          : 0;
}

constexpr FormatConversionCharSet FormatConversionCharToConvValue(char conv) {
  return static_cast<FormatConversionCharSet>(
      FormatConversionCharToConvInt(conv));
}

struct FormatConversionCharSetInternal {
#define ABSL_INTERNAL_CHAR_SET_CASE(c)         \
  static constexpr FormatConversionCharSet c = \
      FormatConversionCharToConvValue(#c[0]);
  ABSL_INTERNAL_CONVERSION_CHARS_EXPAND_(ABSL_INTERNAL_CHAR_SET_CASE, )
#undef ABSL_INTERNAL_CHAR_SET_CASE

  // Used for width/precision '*' specification.
  static constexpr FormatConversionCharSet kStar =
      FormatConversionCharToConvValue('*');

  static constexpr FormatConversionCharSet kIntegral =
      FormatConversionCharSetUnion(d, i, u, o, x, X);
  static constexpr FormatConversionCharSet kFloating =
      FormatConversionCharSetUnion(a, e, f, g, A, E, F, G);
  static constexpr FormatConversionCharSet kNumeric =
      FormatConversionCharSetUnion(kIntegral, kFloating);
  static constexpr FormatConversionCharSet kPointer = p;
};

// Type safe OR operator.
// We need this for two reasons:
//  1. operator| on enums makes them decay to integers and the result is an
//     integer. We need the result to stay as an enum.
//  2. We use "enum class" which would not work even if we accepted the decay.
constexpr FormatConversionCharSet operator|(FormatConversionCharSet a,
                                            FormatConversionCharSet b) {
  return FormatConversionCharSetUnion(a, b);
}

// Overloaded conversion functions to support absl::ParsedFormat.
// Get a conversion with a single character in it.
constexpr FormatConversionCharSet ToFormatConversionCharSet(char c) {
  return static_cast<FormatConversionCharSet>(
      FormatConversionCharToConvValue(c));
}

// Get a conversion with a single character in it.
constexpr FormatConversionCharSet ToFormatConversionCharSet(
    FormatConversionCharSet c) {
  return c;
}

template <typename T>
void ToFormatConversionCharSet(T) = delete;

// Checks whether `c` exists in `set`.
constexpr bool Contains(FormatConversionCharSet set, char c) {
  return (static_cast<uint64_t>(set) &
          static_cast<uint64_t>(FormatConversionCharToConvValue(c))) != 0;
}

// Checks whether all the characters in `c` are contained in `set`
constexpr bool Contains(FormatConversionCharSet set,
                        FormatConversionCharSet c) {
  return (static_cast<uint64_t>(set) & static_cast<uint64_t>(c)) ==
         static_cast<uint64_t>(c);
}

// Checks whether all the characters in `c` are contained in `set`
constexpr bool Contains(FormatConversionCharSet set, FormatConversionChar c) {
  return (static_cast<uint64_t>(set) & FormatConversionCharToConvInt(c)) != 0;
}

// Return capacity - used, clipped to a minimum of 0.
inline size_t Excess(size_t used, size_t capacity) {
  return used < capacity ? capacity - used : 0;
}

}  // namespace str_format_internal

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_STR_FORMAT_EXTENSION_H_
