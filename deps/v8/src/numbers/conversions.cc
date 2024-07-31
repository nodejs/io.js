// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/numbers/conversions.h"

#include <limits.h>
#include <stdarg.h>

#include <cmath>

#include "src/base/numbers/dtoa.h"
#include "src/base/numbers/strtod.h"
#include "src/base/small-vector.h"
#include "src/bigint/bigint.h"
#include "src/common/assert-scope.h"
#include "src/handles/handles.h"
#include "src/heap/factory.h"
#include "src/objects/bigint.h"
#include "src/objects/objects-inl.h"
#include "src/objects/string-inl.h"
#include "src/strings/char-predicates-inl.h"
#include "src/utils/allocation.h"

#if defined(_STLP_VENDOR_CSTD)
// STLPort doesn't import fpclassify into the std namespace.
#define FPCLASSIFY_NAMESPACE
#else
#define FPCLASSIFY_NAMESPACE std
#endif

namespace v8 {
namespace internal {

// Helper class for building result strings in a character buffer. The
// purpose of the class is to use safe operations that checks the
// buffer bounds on all operations in debug mode.
// This simple base class does not allow formatted output.
class SimpleStringBuilder {
 public:
  // Create a string builder with a buffer of the given size. The
  // buffer is allocated through NewArray<char> and must be
  // deallocated by the caller of Finalize().
  explicit SimpleStringBuilder(int size) {
    buffer_ = base::Vector<char>::New(size);
    position_ = 0;
  }

  SimpleStringBuilder(char* buffer, int size)
      : buffer_(buffer, size), position_(0) {}

  ~SimpleStringBuilder() {
    if (!is_finalized()) Finalize();
  }

  // Get the current position in the builder.
  int position() const {
    DCHECK(!is_finalized());
    return position_;
  }

  // Add a single character to the builder. It is not allowed to add
  // 0-characters; use the Finalize() method to terminate the string
  // instead.
  void AddCharacter(char c) {
    DCHECK_NE(c, '\0');
    DCHECK(!is_finalized() && position_ < buffer_.length());
    buffer_[position_++] = c;
  }

  // Add an entire string to the builder. Uses strlen() internally to
  // compute the length of the input string.
  void AddString(const char* s) {
    size_t len = strlen(s);
    DCHECK_GE(kMaxInt, len);
    AddSubstring(s, static_cast<int>(len));
  }

  // Add the first 'n' characters of the given 0-terminated string 's' to the
  // builder. The input string must have enough characters.
  void AddSubstring(const char* s, int n) {
    DCHECK(!is_finalized() && position_ + n <= buffer_.length());
    DCHECK_LE(n, strlen(s));
    std::memcpy(&buffer_[position_], s, n * kCharSize);
    position_ += n;
  }

  // Add character padding to the builder. If count is non-positive,
  // nothing is added to the builder.
  void AddPadding(char c, int count) {
    for (int i = 0; i < count; i++) {
      AddCharacter(c);
    }
  }

  // Add the decimal representation of the value.
  void AddDecimalInteger(int value) {
    uint32_t number = static_cast<uint32_t>(value);
    if (value < 0) {
      AddCharacter('-');
      number = static_cast<uint32_t>(-value);
    }
    int digits = 1;
    for (uint32_t factor = 10; digits < 10; digits++, factor *= 10) {
      if (factor > number) break;
    }
    position_ += digits;
    for (int i = 1; i <= digits; i++) {
      buffer_[position_ - i] = '0' + static_cast<char>(number % 10);
      number /= 10;
    }
  }

  // Finalize the string by 0-terminating it and returning the buffer.
  char* Finalize() {
    DCHECK(!is_finalized() && position_ <= buffer_.length());
    // If there is no space for null termination, overwrite last character.
    if (position_ == buffer_.length()) {
      position_--;
      // Print ellipsis.
      for (int i = 3; i > 0 && position_ > i; --i) buffer_[position_ - i] = '.';
    }
    buffer_[position_] = '\0';
    // Make sure nobody managed to add a 0-character to the
    // buffer while building the string.
    DCHECK(strlen(buffer_.begin()) == static_cast<size_t>(position_));
    position_ = -1;
    DCHECK(is_finalized());
    return buffer_.begin();
  }

 protected:
  base::Vector<char> buffer_;
  int position_;

  bool is_finalized() const { return position_ < 0; }

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SimpleStringBuilder);
};

inline double JunkStringValue() {
  return base::bit_cast<double, uint64_t>(kQuietNaNMask);
}

inline double SignedZero(bool negative) {
  return negative ? base::uint64_to_double(base::Double::kSignMask) : 0.0;
}

inline bool isDigit(int x, int radix) {
  return (x >= '0' && x <= '9' && x < '0' + radix) ||
         (radix > 10 && x >= 'a' && x < 'a' + radix - 10) ||
         (radix > 10 && x >= 'A' && x < 'A' + radix - 10);
}

inline bool isBinaryDigit(int x) { return x == '0' || x == '1'; }

template <class Iterator, class EndMark>
bool SubStringEquals(Iterator* current, EndMark end, const char* substring) {
  DCHECK(**current == *substring);
  for (substring++; *substring != '\0'; substring++) {
    ++*current;
    if (*current == end || **current != *substring) return false;
  }
  ++*current;
  return true;
}

// Returns true if a nonspace character has been found and false if the
// end was been reached before finding a nonspace character.
template <class Iterator, class EndMark>
inline bool AdvanceToNonspace(Iterator* current, EndMark end) {
  while (*current != end) {
    if (!IsWhiteSpaceOrLineTerminator(**current)) return true;
    ++*current;
  }
  return false;
}

// Parsing integers with radix 2, 4, 8, 16, 32. Assumes current != end.
template <int radix_log_2, class Iterator, class EndMark>
double InternalStringToIntDouble(Iterator current, EndMark end, bool negative,
                                 bool allow_trailing_junk) {
  DCHECK(current != end);

  // Skip leading 0s.
  while (*current == '0') {
    ++current;
    if (current == end) return SignedZero(negative);
  }

  int64_t number = 0;
  int exponent = 0;
  const int radix = (1 << radix_log_2);

  int lim_0 = '0' + (radix < 10 ? radix : 10);
  int lim_a = 'a' + (radix - 10);
  int lim_A = 'A' + (radix - 10);

  do {
    int digit;
    if (*current >= '0' && *current < lim_0) {
      digit = static_cast<char>(*current) - '0';
    } else if (*current >= 'a' && *current < lim_a) {
      digit = static_cast<char>(*current) - 'a' + 10;
    } else if (*current >= 'A' && *current < lim_A) {
      digit = static_cast<char>(*current) - 'A' + 10;
    } else {
      if (allow_trailing_junk || !AdvanceToNonspace(&current, end)) {
        break;
      } else {
        return JunkStringValue();
      }
    }

    number = number * radix + digit;
    int overflow = static_cast<int>(number >> 53);
    if (overflow != 0) {
      // Overflow occurred. Need to determine which direction to round the
      // result.
      int overflow_bits_count = 1;
      while (overflow > 1) {
        overflow_bits_count++;
        overflow >>= 1;
      }

      int dropped_bits_mask = ((1 << overflow_bits_count) - 1);
      int dropped_bits = static_cast<int>(number) & dropped_bits_mask;
      number >>= overflow_bits_count;
      exponent = overflow_bits_count;

      bool zero_tail = true;
      while (true) {
        ++current;
        if (current == end || !isDigit(*current, radix)) break;
        zero_tail = zero_tail && *current == '0';
        exponent += radix_log_2;
      }

      if (!allow_trailing_junk && AdvanceToNonspace(&current, end)) {
        return JunkStringValue();
      }

      int middle_value = (1 << (overflow_bits_count - 1));
      if (dropped_bits > middle_value) {
        number++;  // Rounding up.
      } else if (dropped_bits == middle_value) {
        // Rounding to even to consistency with decimals: half-way case rounds
        // up if significant part is odd and down otherwise.
        if ((number & 1) != 0 || !zero_tail) {
          number++;  // Rounding up.
        }
      }

      // Rounding up may cause overflow.
      if ((number & (static_cast<int64_t>(1) << 53)) != 0) {
        exponent++;
        number >>= 1;
      }
      break;
    }
    ++current;
  } while (current != end);

  DCHECK(number < ((int64_t)1 << 53));
  DCHECK(static_cast<int64_t>(static_cast<double>(number)) == number);

  if (exponent == 0) {
    if (negative) {
      if (number == 0) return -0.0;
      number = -number;
    }
    return static_cast<double>(number);
  }

  DCHECK_NE(number, 0);
  return std::ldexp(static_cast<double>(negative ? -number : number), exponent);
}

namespace {

// Subclasses of StringToIntHelper get access to internal state:
enum class State { kRunning, kError, kJunk, kEmpty, kZero, kDone };

enum class Sign { kNegative, kPositive, kNone };

}  // namespace

// ES6 18.2.5 parseInt(string, radix) (with NumberParseIntHelper subclass);
// and BigInt parsing cases from https://tc39.github.io/proposal-bigint/
// (with StringToBigIntHelper subclass).
class StringToIntHelper {
 public:
  StringToIntHelper(Handle<String> subject, int radix)
      : subject_(subject), radix_(radix) {
    DCHECK(subject->IsFlat());
  }

  // Used for the NumberParseInt operation
  StringToIntHelper(const uint8_t* subject, int radix, int length)
      : raw_one_byte_subject_(subject), radix_(radix), length_(length) {}

  StringToIntHelper(const base::uc16* subject, int radix, int length)
      : raw_two_byte_subject_(subject), radix_(radix), length_(length) {}

  // Used for the StringToBigInt operation.
  explicit StringToIntHelper(Handle<String> subject) : subject_(subject) {
    DCHECK(subject->IsFlat());
  }

  // Used for parsing BigInt literals, where the input is a Zone-allocated
  // buffer of one-byte digits, along with an optional radix prefix.
  StringToIntHelper(const uint8_t* subject, int length)
      : raw_one_byte_subject_(subject), length_(length) {}
  virtual ~StringToIntHelper() = default;

 protected:
  // Subclasses must implement these:
  virtual void ParseOneByte(const uint8_t* start) = 0;
  virtual void ParseTwoByte(const base::uc16* start) = 0;

  // Subclasses must call this to do all the work.
  void ParseInt();

  // Subclass constructors should call these for configuration before calling
  // ParseInt().
  void set_allow_binary_and_octal_prefixes() {
    allow_binary_and_octal_prefixes_ = true;
  }
  void set_disallow_trailing_junk() { allow_trailing_junk_ = false; }
  bool allow_trailing_junk() { return allow_trailing_junk_; }

  bool IsOneByte() const {
    if (raw_two_byte_subject_ != nullptr) return false;
    return raw_one_byte_subject_ != nullptr ||
           String::IsOneByteRepresentationUnderneath(*subject_);
  }

  base::Vector<const uint8_t> GetOneByteVector(
      const DisallowGarbageCollection& no_gc) {
    if (raw_one_byte_subject_ != nullptr) {
      return base::Vector<const uint8_t>(raw_one_byte_subject_, length_);
    }
    return subject_->GetFlatContent(no_gc).ToOneByteVector();
  }

  base::Vector<const base::uc16> GetTwoByteVector(
      const DisallowGarbageCollection& no_gc) {
    if (raw_two_byte_subject_ != nullptr) {
      return base::Vector<const base::uc16>(raw_two_byte_subject_, length_);
    }
    return subject_->GetFlatContent(no_gc).ToUC16Vector();
  }

  int radix() { return radix_; }
  int cursor() { return cursor_; }
  int length() { return length_; }
  bool negative() { return sign_ == Sign::kNegative; }
  Sign sign() { return sign_; }
  State state() { return state_; }
  void set_state(State state) { state_ = state; }

 private:
  template <class Char>
  void DetectRadixInternal(Char current, int length);

  Handle<String> subject_;
  const uint8_t* raw_one_byte_subject_ = nullptr;
  const base::uc16* raw_two_byte_subject_ = nullptr;
  int radix_ = 0;
  int cursor_ = 0;
  int length_ = 0;
  Sign sign_ = Sign::kNone;
  bool leading_zero_ = false;
  bool allow_binary_and_octal_prefixes_ = false;
  bool allow_trailing_junk_ = true;
  State state_ = State::kRunning;
};

void StringToIntHelper::ParseInt() {
  DisallowGarbageCollection no_gc;
  if (IsOneByte()) {
    base::Vector<const uint8_t> vector = GetOneByteVector(no_gc);
    DetectRadixInternal(vector.begin(), vector.length());
    if (state_ != State::kRunning) return;
    ParseOneByte(vector.begin());
  } else {
    base::Vector<const base::uc16> vector = GetTwoByteVector(no_gc);
    DetectRadixInternal(vector.begin(), vector.length());
    if (state_ != State::kRunning) return;
    ParseTwoByte(vector.begin());
  }
}

template <class Char>
void StringToIntHelper::DetectRadixInternal(Char current, int length) {
  Char start = current;
  length_ = length;
  Char end = start + length;

  if (!AdvanceToNonspace(&current, end)) {
    return set_state(State::kEmpty);
  }

  if (*current == '+') {
    // Ignore leading sign; skip following spaces.
    ++current;
    if (current == end) {
      return set_state(State::kJunk);
    }
    sign_ = Sign::kPositive;
  } else if (*current == '-') {
    ++current;
    if (current == end) {
      return set_state(State::kJunk);
    }
    sign_ = Sign::kNegative;
  }

  if (radix_ == 0) {
    // Radix detection.
    radix_ = 10;
    if (*current == '0') {
      ++current;
      if (current == end) return set_state(State::kZero);
      if (*current == 'x' || *current == 'X') {
        radix_ = 16;
        ++current;
        if (current == end) return set_state(State::kJunk);
      } else if (allow_binary_and_octal_prefixes_ &&
                 (*current == 'o' || *current == 'O')) {
        radix_ = 8;
        ++current;
        if (current == end) return set_state(State::kJunk);
      } else if (allow_binary_and_octal_prefixes_ &&
                 (*current == 'b' || *current == 'B')) {
        radix_ = 2;
        ++current;
        if (current == end) return set_state(State::kJunk);
      } else {
        leading_zero_ = true;
      }
    }
  } else if (radix_ == 16) {
    if (*current == '0') {
      // Allow "0x" prefix.
      ++current;
      if (current == end) return set_state(State::kZero);
      if (*current == 'x' || *current == 'X') {
        ++current;
        if (current == end) return set_state(State::kJunk);
      } else {
        leading_zero_ = true;
      }
    }
  }
  // Skip leading zeros.
  while (*current == '0') {
    leading_zero_ = true;
    ++current;
    if (current == end) return set_state(State::kZero);
  }

  if (!leading_zero_ && !isDigit(*current, radix_)) {
    return set_state(State::kJunk);
  }

  DCHECK(radix_ >= 2 && radix_ <= 36);
  static_assert(String::kMaxLength <= INT_MAX);
  cursor_ = static_cast<int>(current - start);
}

class NumberParseIntHelper : public StringToIntHelper {
 public:
  NumberParseIntHelper(Handle<String> string, int radix)
      : StringToIntHelper(string, radix) {}

  NumberParseIntHelper(const uint8_t* string, int radix, int length)
      : StringToIntHelper(string, radix, length) {}

  NumberParseIntHelper(const base::uc16* string, int radix, int length)
      : StringToIntHelper(string, radix, length) {}

  template <class Char>
  void ParseInternal(Char start) {
    Char current = start + cursor();
    Char end = start + length();

    if (radix() == 10) return HandleBaseTenCase(current, end);
    if (base::bits::IsPowerOfTwo(radix())) {
      result_ = HandlePowerOfTwoCase(current, end);
      set_state(State::kDone);
      return;
    }
    return HandleGenericCase(current, end);
  }
  void ParseOneByte(const uint8_t* start) final { return ParseInternal(start); }
  void ParseTwoByte(const base::uc16* start) final {
    return ParseInternal(start);
  }

  double GetResult() {
    ParseInt();
    switch (state()) {
      case State::kJunk:
      case State::kEmpty:
        return JunkStringValue();
      case State::kZero:
        return SignedZero(negative());
      case State::kDone:
        return negative() ? -result_ : result_;
      case State::kError:
      case State::kRunning:
        break;
    }
    UNREACHABLE();
  }

 private:
  template <class Char>
  void HandleGenericCase(Char current, Char end);

  template <class Char>
  double HandlePowerOfTwoCase(Char current, Char end) {
    const bool allow_trailing_junk = true;
    // GetResult() will take care of the sign bit, so ignore it for now.
    const bool negative = false;
    switch (radix()) {
      case 2:
        return InternalStringToIntDouble<1>(current, end, negative,
                                            allow_trailing_junk);
      case 4:
        return InternalStringToIntDouble<2>(current, end, negative,
                                            allow_trailing_junk);
      case 8:
        return InternalStringToIntDouble<3>(current, end, negative,
                                            allow_trailing_junk);

      case 16:
        return InternalStringToIntDouble<4>(current, end, negative,
                                            allow_trailing_junk);

      case 32:
        return InternalStringToIntDouble<5>(current, end, negative,
                                            allow_trailing_junk);
      default:
        UNREACHABLE();
    }
  }

  template <class Char>
  void HandleBaseTenCase(Char current, Char end) {
    // Parsing with strtod.
    const int kMaxSignificantDigits = 309;  // Doubles are less than 1.8e308.
    // The buffer may contain up to kMaxSignificantDigits + 1 digits and a zero
    // end.
    const int kBufferSize = kMaxSignificantDigits + 2;
    char buffer[kBufferSize];
    int buffer_pos = 0;
    while (*current >= '0' && *current <= '9') {
      if (buffer_pos <= kMaxSignificantDigits) {
        // If the number has more than kMaxSignificantDigits it will be parsed
        // as infinity.
        DCHECK_LT(buffer_pos, kBufferSize);
        buffer[buffer_pos++] = static_cast<char>(*current);
      }
      ++current;
      if (current == end) break;
    }

    SLOW_DCHECK(buffer_pos < kBufferSize);
    buffer[buffer_pos] = '\0';
    base::Vector<const char> buffer_vector(buffer, buffer_pos);
    result_ = Strtod(buffer_vector, 0);
    set_state(State::kDone);
  }

  double result_ = 0;
};

template <class Char>
void NumberParseIntHelper::HandleGenericCase(Char current, Char end) {
  // The following code causes accumulating rounding error for numbers greater
  // than ~2^56. It's explicitly allowed in the spec: "if R is not 2, 4, 8, 10,
  // 16, or 32, then mathInt may be an implementation-dependent approximation to
  // the mathematical integer value" (15.1.2.2).

  int lim_0 = '0' + (radix() < 10 ? radix() : 10);
  int lim_a = 'a' + (radix() - 10);
  int lim_A = 'A' + (radix() - 10);

  // NOTE: The code for computing the value may seem a bit complex at
  // first glance. It is structured to use 32-bit multiply-and-add
  // loops as long as possible to avoid losing precision.

  bool done = false;
  do {
    // Parse the longest part of the string starting at {current}
    // possible while keeping the multiplier, and thus the part
    // itself, within 32 bits.
    uint32_t part = 0, multiplier = 1;
    while (true) {
      uint32_t d;
      if (*current >= '0' && *current < lim_0) {
        d = *current - '0';
      } else if (*current >= 'a' && *current < lim_a) {
        d = *current - 'a' + 10;
      } else if (*current >= 'A' && *current < lim_A) {
        d = *current - 'A' + 10;
      } else {
        done = true;
        break;
      }

      // Update the value of the part as long as the multiplier fits
      // in 32 bits. When we can't guarantee that the next iteration
      // will not overflow the multiplier, we stop parsing the part
      // by leaving the loop.
      const uint32_t kMaximumMultiplier = 0xFFFFFFFFU / 36;
      uint32_t m = multiplier * static_cast<uint32_t>(radix());
      if (m > kMaximumMultiplier) break;
      part = part * radix() + d;
      multiplier = m;
      DCHECK(multiplier > part);

      ++current;
      if (current == end) {
        done = true;
        break;
      }
    }
    result_ = result_ * multiplier + part;
  } while (!done);

  if (!allow_trailing_junk() && AdvanceToNonspace(&current, end)) {
    return set_state(State::kJunk);
  }
  return set_state(State::kDone);
}

// Converts a string to a double value. Assumes the Iterator supports
// the following operations:
// 1. current == end (other ops are not allowed), current != end.
// 2. *current - gets the current character in the sequence.
// 3. ++current (advances the position).
template <class Iterator, class EndMark>
double InternalStringToDouble(Iterator current, EndMark end, int flags,
                              double empty_string_val) {
  // To make sure that iterator dereferencing is valid the following
  // convention is used:
  // 1. Each '++current' statement is followed by check for equality to 'end'.
  // 2. If AdvanceToNonspace returned false then current == end.
  // 3. If 'current' becomes be equal to 'end' the function returns or goes to
  // 'parsing_done'.
  // 4. 'current' is not dereferenced after the 'parsing_done' label.
  // 5. Code before 'parsing_done' may rely on 'current != end'.
  if (!AdvanceToNonspace(&current, end)) {
    return empty_string_val;
  }

  const bool allow_trailing_junk = (flags & ALLOW_TRAILING_JUNK) != 0;

  // Maximum number of significant digits in decimal representation.
  // The longest possible double in decimal representation is
  // (2^53 - 1) * 2 ^ -1074 that is (2 ^ 53 - 1) * 5 ^ 1074 / 10 ^ 1074
  // (768 digits). If we parse a number whose first digits are equal to a
  // mean of 2 adjacent doubles (that could have up to 769 digits) the result
  // must be rounded to the bigger one unless the tail consists of zeros, so
  // we don't need to preserve all the digits.
  const int kMaxSignificantDigits = 772;

  // The longest form of simplified number is: "-<significant digits>'.1eXXX\0".
  const int kBufferSize = kMaxSignificantDigits + 10;
  char buffer[kBufferSize];
  int buffer_pos = 0;

  // Exponent will be adjusted if insignificant digits of the integer part
  // or insignificant leading zeros of the fractional part are dropped.
  int exponent = 0;
  int significant_digits = 0;
  int insignificant_digits = 0;
  bool nonzero_digit_dropped = false;

  enum class Sign { kNone, kNegative, kPositive };

  Sign sign = Sign::kNone;

  if (*current == '+') {
    // Ignore leading sign.
    ++current;
    if (current == end) return JunkStringValue();
    sign = Sign::kPositive;
  } else if (*current == '-') {
    ++current;
    if (current == end) return JunkStringValue();
    sign = Sign::kNegative;
  }

  static const char kInfinityString[] = "Infinity";
  if (*current == kInfinityString[0]) {
    if (!SubStringEquals(&current, end, kInfinityString)) {
      return JunkStringValue();
    }

    if (!allow_trailing_junk && AdvanceToNonspace(&current, end)) {
      return JunkStringValue();
    }

    DCHECK_EQ(buffer_pos, 0);
    return (sign == Sign::kNegative) ? -V8_INFINITY : V8_INFINITY;
  }

  bool leading_zero = false;
  if (*current == '0') {
    ++current;
    if (current == end) return SignedZero(sign == Sign::kNegative);

    leading_zero = true;

    // It could be hexadecimal value.
    if ((flags & ALLOW_HEX) && (*current == 'x' || *current == 'X')) {
      ++current;
      if (current == end || !isDigit(*current, 16) || sign != Sign::kNone) {
        return JunkStringValue();  // "0x".
      }

      return InternalStringToIntDouble<4>(current, end, false,
                                          allow_trailing_junk);

      // It could be an explicit octal value.
    } else if ((flags & ALLOW_OCTAL) && (*current == 'o' || *current == 'O')) {
      ++current;
      if (current == end || !isDigit(*current, 8) || sign != Sign::kNone) {
        return JunkStringValue();  // "0o".
      }

      return InternalStringToIntDouble<3>(current, end, false,
                                          allow_trailing_junk);

      // It could be a binary value.
    } else if ((flags & ALLOW_BINARY) && (*current == 'b' || *current == 'B')) {
      ++current;
      if (current == end || !isBinaryDigit(*current) || sign != Sign::kNone) {
        return JunkStringValue();  // "0b".
      }

      return InternalStringToIntDouble<1>(current, end, false,
                                          allow_trailing_junk);
    }

    // Ignore leading zeros in the integer part.
    while (*current == '0') {
      ++current;
      if (current == end) return SignedZero(sign == Sign::kNegative);
    }
  }

  bool octal = leading_zero && (flags & ALLOW_IMPLICIT_OCTAL) != 0;

  // Copy significant digits of the integer part (if any) to the buffer.
  while (*current >= '0' && *current <= '9') {
    if (significant_digits < kMaxSignificantDigits) {
      DCHECK_LT(buffer_pos, kBufferSize);
      buffer[buffer_pos++] = static_cast<char>(*current);
      significant_digits++;
      // Will later check if it's an octal in the buffer.
    } else {
      insignificant_digits++;  // Move the digit into the exponential part.
      nonzero_digit_dropped = nonzero_digit_dropped || *current != '0';
    }
    octal = octal && *current < '8';
    ++current;
    if (current == end) goto parsing_done;
  }

  if (significant_digits == 0) {
    octal = false;
  }

  if (*current == '.') {
    if (octal && !allow_trailing_junk) return JunkStringValue();
    if (octal) goto parsing_done;

    ++current;
    if (current == end) {
      if (significant_digits == 0 && !leading_zero) {
        return JunkStringValue();
      } else {
        goto parsing_done;
      }
    }

    if (significant_digits == 0) {
      // octal = false;
      // Integer part consists of 0 or is absent. Significant digits start after
      // leading zeros (if any).
      while (*current == '0') {
        ++current;
        if (current == end) return SignedZero(sign == Sign::kNegative);
        exponent--;  // Move this 0 into the exponent.
      }
    }

    // There is a fractional part.  We don't emit a '.', but adjust the exponent
    // instead.
    while (*current >= '0' && *current <= '9') {
      if (significant_digits < kMaxSignificantDigits) {
        DCHECK_LT(buffer_pos, kBufferSize);
        buffer[buffer_pos++] = static_cast<char>(*current);
        significant_digits++;
        exponent--;
      } else {
        // Ignore insignificant digits in the fractional part.
        nonzero_digit_dropped = nonzero_digit_dropped || *current != '0';
      }
      ++current;
      if (current == end) goto parsing_done;
    }
  }

  if (!leading_zero && exponent == 0 && significant_digits == 0) {
    // If leading_zeros is true then the string contains zeros.
    // If exponent < 0 then string was [+-]\.0*...
    // If significant_digits != 0 the string is not equal to 0.
    // Otherwise there are no digits in the string.
    return JunkStringValue();
  }

  // Parse exponential part.
  if (*current == 'e' || *current == 'E') {
    if (octal) return JunkStringValue();
    ++current;
    if (current == end) {
      if (allow_trailing_junk) {
        goto parsing_done;
      } else {
        return JunkStringValue();
      }
    }
    char exponent_sign = '+';
    if (*current == '+' || *current == '-') {
      exponent_sign = static_cast<char>(*current);
      ++current;
      if (current == end) {
        if (allow_trailing_junk) {
          goto parsing_done;
        } else {
          return JunkStringValue();
        }
      }
    }

    if (current == end || *current < '0' || *current > '9') {
      if (allow_trailing_junk) {
        goto parsing_done;
      } else {
        return JunkStringValue();
      }
    }

    const int max_exponent = INT_MAX / 2;
    DCHECK(-max_exponent / 2 <= exponent && exponent <= max_exponent / 2);
    int num = 0;
    do {
      // Check overflow.
      int digit = *current - '0';
      if (num >= max_exponent / 10 &&
          !(num == max_exponent / 10 && digit <= max_exponent % 10)) {
        num = max_exponent;
      } else {
        num = num * 10 + digit;
      }
      ++current;
    } while (current != end && *current >= '0' && *current <= '9');

    exponent += (exponent_sign == '-' ? -num : num);
  }

  if (!allow_trailing_junk && AdvanceToNonspace(&current, end)) {
    return JunkStringValue();
  }

parsing_done:
  exponent += insignificant_digits;

  if (octal) {
    return InternalStringToIntDouble<3>(buffer, buffer + buffer_pos,
                                        sign == Sign::kNegative,
                                        allow_trailing_junk);
  }

  if (nonzero_digit_dropped) {
    buffer[buffer_pos++] = '1';
    exponent--;
  }

  SLOW_DCHECK(buffer_pos < kBufferSize);
  buffer[buffer_pos] = '\0';

  double converted =
      Strtod(base::Vector<const char>(buffer, buffer_pos), exponent);
  return (sign == Sign::kNegative) ? -converted : converted;
}

double StringToDouble(const char* str, int flags, double empty_string_val) {
  // We use {base::OneByteVector} instead of {base::CStrVector} to avoid
  // instantiating the InternalStringToDouble() template for {const char*} as
  // well.
  return StringToDouble(base::OneByteVector(str), flags, empty_string_val);
}

double StringToDouble(base::Vector<const uint8_t> str, int flags,
                      double empty_string_val) {
  return InternalStringToDouble(str.begin(), str.end(), flags,
                                empty_string_val);
}

double StringToDouble(base::Vector<const base::uc16> str, int flags,
                      double empty_string_val) {
  const base::uc16* end = str.begin() + str.length();
  return InternalStringToDouble(str.begin(), end, flags, empty_string_val);
}

double StringToInt(Isolate* isolate, Handle<String> string, int radix) {
  NumberParseIntHelper helper(string, radix);
  return helper.GetResult();
}

template <typename IsolateT>
class StringToBigIntHelper : public StringToIntHelper {
 public:
  enum class Behavior { kStringToBigInt, kLiteral };

  // Used for StringToBigInt operation (BigInt constructor and == operator).
  StringToBigIntHelper(IsolateT* isolate, Handle<String> string)
      : StringToIntHelper(string),
        isolate_(isolate),
        behavior_(Behavior::kStringToBigInt) {
    set_allow_binary_and_octal_prefixes();
    set_disallow_trailing_junk();
  }

  // Used for parsing BigInt literals, where the input is a buffer of
  // one-byte ASCII digits, along with an optional radix prefix.
  StringToBigIntHelper(IsolateT* isolate, const uint8_t* string, int length)
      : StringToIntHelper(string, length),
        isolate_(isolate),
        behavior_(Behavior::kLiteral) {
    set_allow_binary_and_octal_prefixes();
  }

  void ParseOneByte(const uint8_t* start) final { return ParseInternal(start); }
  void ParseTwoByte(const base::uc16* start) final {
    return ParseInternal(start);
  }

  MaybeHandle<BigInt> GetResult() {
    ParseInt();
    if (behavior_ == Behavior::kStringToBigInt && sign() != Sign::kNone &&
        radix() != 10) {
      return MaybeHandle<BigInt>();
    }
    if (state() == State::kEmpty) {
      if (behavior_ == Behavior::kStringToBigInt) {
        set_state(State::kZero);
      } else {
        UNREACHABLE();
      }
    }
    switch (this->state()) {
      case State::kJunk:
      case State::kError:
        return MaybeHandle<BigInt>();
      case State::kZero:
        return BigInt::Zero(isolate(), allocation_type());
      case State::kDone:
        return BigInt::Allocate(isolate(), &accumulator_, negative(),
                                allocation_type());
      case State::kEmpty:
      case State::kRunning:
        break;
    }
    UNREACHABLE();
  }

  // Used for converting BigInt literals. The scanner has already checked
  // that the literal is valid and not too big, so this always succeeds.
  std::unique_ptr<char[]> DecimalString(bigint::Processor* processor) {
    DCHECK_EQ(behavior_, Behavior::kLiteral);
    ParseInt();
    if (state() == State::kZero) {
      // Input may have been "0x0" or similar.
      return std::unique_ptr<char[]>(new char[2]{'0', '\0'});
    }
    DCHECK_EQ(state(), State::kDone);
    int num_digits = accumulator_.ResultLength();
    base::SmallVector<bigint::digit_t, 8> digit_storage(num_digits);
    bigint::RWDigits digits(digit_storage.data(), num_digits);
    processor->FromString(digits, &accumulator_);
    int num_chars = bigint::ToStringResultLength(digits, 10, false);
    std::unique_ptr<char[]> out(new char[num_chars + 1]);
    processor->ToString(out.get(), &num_chars, digits, 10, false);
    out[num_chars] = '\0';
    return out;
  }
  IsolateT* isolate() { return isolate_; }

 private:
  template <class Char>
  void ParseInternal(Char start) {
    using Result = bigint::FromStringAccumulator::Result;
    Char current = start + cursor();
    Char end = start + length();
    current = accumulator_.Parse(current, end, radix());

    Result result = accumulator_.result();
    if (result == Result::kMaxSizeExceeded) {
      return set_state(State::kError);
    }
    if (!allow_trailing_junk() && AdvanceToNonspace(&current, end)) {
      return set_state(State::kJunk);
    }
    return set_state(State::kDone);
  }

  AllocationType allocation_type() {
    // For literals, we pretenure the allocated BigInt, since it's about
    // to be stored in the interpreter's constants array.
    return behavior_ == Behavior::kLiteral ? AllocationType::kOld
                                           : AllocationType::kYoung;
  }

  IsolateT* isolate_;
  bigint::FromStringAccumulator accumulator_{BigInt::kMaxLength};
  Behavior behavior_;
};

MaybeHandle<BigInt> StringToBigInt(Isolate* isolate, Handle<String> string) {
  string = String::Flatten(isolate, string);
  StringToBigIntHelper<Isolate> helper(isolate, string);
  return helper.GetResult();
}

template <typename IsolateT>
MaybeHandle<BigInt> BigIntLiteral(IsolateT* isolate, const char* string) {
  StringToBigIntHelper<IsolateT> helper(
      isolate, reinterpret_cast<const uint8_t*>(string),
      static_cast<int>(strlen(string)));
  return helper.GetResult();
}
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    MaybeHandle<BigInt> BigIntLiteral(Isolate* isolate, const char* string);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    MaybeHandle<BigInt> BigIntLiteral(LocalIsolate* isolate,
                                      const char* string);

std::unique_ptr<char[]> BigIntLiteralToDecimal(
    LocalIsolate* isolate, base::Vector<const uint8_t> literal) {
  StringToBigIntHelper<LocalIsolate> helper(nullptr, literal.begin(),
                                            literal.length());
  return helper.DecimalString(isolate->bigint_processor());
}

const char* DoubleToCString(double v, base::Vector<char> buffer) {
  switch (FPCLASSIFY_NAMESPACE::fpclassify(v)) {
    case FP_NAN:
      return "NaN";
    case FP_INFINITE:
      return (v < 0.0 ? "-Infinity" : "Infinity");
    case FP_ZERO:
      return "0";
    default: {
      if (IsInt32Double(v)) {
        // This will trigger if v is -0 and -0.0 is stringified to "0".
        // (see ES section 7.1.12.1 #sec-tostring-applied-to-the-number-type)
        return IntToCString(FastD2I(v), buffer);
      }
      SimpleStringBuilder builder(buffer.begin(), buffer.length());
      int decimal_point;
      int sign;
      const int kV8DtoaBufferCapacity = base::kBase10MaximalLength + 1;
      char decimal_rep[kV8DtoaBufferCapacity];
      int length;

      base::DoubleToAscii(
          v, base::DTOA_SHORTEST, 0,
          base::Vector<char>(decimal_rep, kV8DtoaBufferCapacity), &sign,
          &length, &decimal_point);

      if (sign) builder.AddCharacter('-');

      if (length <= decimal_point && decimal_point <= 21) {
        // ECMA-262 section 9.8.1 step 6.
        builder.AddString(decimal_rep);
        builder.AddPadding('0', decimal_point - length);

      } else if (0 < decimal_point && decimal_point <= 21) {
        // ECMA-262 section 9.8.1 step 7.
        builder.AddSubstring(decimal_rep, decimal_point);
        builder.AddCharacter('.');
        builder.AddString(decimal_rep + decimal_point);

      } else if (decimal_point <= 0 && decimal_point > -6) {
        // ECMA-262 section 9.8.1 step 8.
        builder.AddString("0.");
        builder.AddPadding('0', -decimal_point);
        builder.AddString(decimal_rep);

      } else {
        // ECMA-262 section 9.8.1 step 9 and 10 combined.
        builder.AddCharacter(decimal_rep[0]);
        if (length != 1) {
          builder.AddCharacter('.');
          builder.AddString(decimal_rep + 1);
        }
        builder.AddCharacter('e');
        builder.AddCharacter((decimal_point >= 0) ? '+' : '-');
        int exponent = decimal_point - 1;
        if (exponent < 0) exponent = -exponent;
        builder.AddDecimalInteger(exponent);
      }
      return builder.Finalize();
    }
  }
}

const char* IntToCString(int n, base::Vector<char> buffer) {
  bool negative = true;
  if (n >= 0) {
    n = -n;
    negative = false;
  }
  // Build the string backwards from the least significant digit.
  int i = buffer.length();
  buffer[--i] = '\0';
  do {
    // We ensured n <= 0, so the subtraction does the right addition.
    buffer[--i] = '0' - (n % 10);
    n /= 10;
  } while (n);
  if (negative) buffer[--i] = '-';
  return buffer.begin() + i;
}

char* DoubleToFixedCString(double value, int f) {
  const int kMaxDigitsBeforePoint = 21;
  const double kFirstNonFixed = 1e21;
  DCHECK_GE(f, 0);
  DCHECK_LE(f, kMaxFractionDigits);

  bool negative = false;
  double abs_value = value;
  if (value < 0) {
    abs_value = -value;
    negative = true;
  }

  // If abs_value has more than kMaxDigitsBeforePoint digits before the point
  // use the non-fixed conversion routine.
  if (abs_value >= kFirstNonFixed) {
    char arr[kMaxFractionDigits];
    base::Vector<char> buffer(arr, arraysize(arr));
    return StrDup(DoubleToCString(value, buffer));
  }

  // Find a sufficiently precise decimal representation of n.
  int decimal_point;
  int sign;
  // Add space for the '\0' byte.
  const int kDecimalRepCapacity =
      kMaxDigitsBeforePoint + kMaxFractionDigits + 1;
  char decimal_rep[kDecimalRepCapacity];
  int decimal_rep_length;
  base::DoubleToAscii(value, base::DTOA_FIXED, f,
                      base::Vector<char>(decimal_rep, kDecimalRepCapacity),
                      &sign, &decimal_rep_length, &decimal_point);

  // Create a representation that is padded with zeros if needed.
  int zero_prefix_length = 0;
  int zero_postfix_length = 0;

  if (decimal_point <= 0) {
    zero_prefix_length = -decimal_point + 1;
    decimal_point = 1;
  }

  if (zero_prefix_length + decimal_rep_length < decimal_point + f) {
    zero_postfix_length =
        decimal_point + f - decimal_rep_length - zero_prefix_length;
  }

  unsigned rep_length =
      zero_prefix_length + decimal_rep_length + zero_postfix_length;
  SimpleStringBuilder rep_builder(rep_length + 1);
  rep_builder.AddPadding('0', zero_prefix_length);
  rep_builder.AddString(decimal_rep);
  rep_builder.AddPadding('0', zero_postfix_length);
  char* rep = rep_builder.Finalize();

  // Create the result string by appending a minus and putting in a
  // decimal point if needed.
  unsigned result_size = decimal_point + f + 2;
  SimpleStringBuilder builder(result_size + 1);
  if (negative) builder.AddCharacter('-');
  builder.AddSubstring(rep, decimal_point);
  if (f > 0) {
    builder.AddCharacter('.');
    builder.AddSubstring(rep + decimal_point, f);
  }
  DeleteArray(rep);
  return builder.Finalize();
}

static char* CreateExponentialRepresentation(char* decimal_rep, int exponent,
                                             bool negative,
                                             int significant_digits) {
  bool negative_exponent = false;
  if (exponent < 0) {
    negative_exponent = true;
    exponent = -exponent;
  }

  // Leave room in the result for appending a minus, for a period, the
  // letter 'e', a minus or a plus depending on the exponent, and a
  // three digit exponent.
  unsigned result_size = significant_digits + 7;
  SimpleStringBuilder builder(result_size + 1);

  if (negative) builder.AddCharacter('-');
  builder.AddCharacter(decimal_rep[0]);
  if (significant_digits != 1) {
    builder.AddCharacter('.');
    builder.AddString(decimal_rep + 1);
    size_t rep_length = strlen(decimal_rep);
    DCHECK_GE(significant_digits, rep_length);
    builder.AddPadding('0', significant_digits - static_cast<int>(rep_length));
  }

  builder.AddCharacter('e');
  builder.AddCharacter(negative_exponent ? '-' : '+');
  builder.AddDecimalInteger(exponent);
  return builder.Finalize();
}

char* DoubleToExponentialCString(double value, int f) {
  // f might be -1 to signal that f was undefined in JavaScript.
  DCHECK(f >= -1 && f <= kMaxFractionDigits);

  bool negative = false;
  if (value < 0) {
    value = -value;
    negative = true;
  }

  // Find a sufficiently precise decimal representation of n.
  int decimal_point;
  int sign;
  // f corresponds to the digits after the point. There is always one digit
  // before the point. The number of requested_digits equals hence f + 1.
  // And we have to add one character for the null-terminator.
  const int kV8DtoaBufferCapacity = kMaxFractionDigits + 1 + 1;
  // Make sure that the buffer is big enough, even if we fall back to the
  // shortest representation (which happens when f equals -1).
  DCHECK_LE(base::kBase10MaximalLength, kMaxFractionDigits + 1);
  char decimal_rep[kV8DtoaBufferCapacity];
  int decimal_rep_length;

  if (f == -1) {
    base::DoubleToAscii(value, base::DTOA_SHORTEST, 0,
                        base::Vector<char>(decimal_rep, kV8DtoaBufferCapacity),
                        &sign, &decimal_rep_length, &decimal_point);
    f = decimal_rep_length - 1;
  } else {
    base::DoubleToAscii(value, base::DTOA_PRECISION, f + 1,
                        base::Vector<char>(decimal_rep, kV8DtoaBufferCapacity),
                        &sign, &decimal_rep_length, &decimal_point);
  }
  DCHECK_GT(decimal_rep_length, 0);
  DCHECK(decimal_rep_length <= f + 1);

  int exponent = decimal_point - 1;
  char* result =
      CreateExponentialRepresentation(decimal_rep, exponent, negative, f + 1);

  return result;
}

char* DoubleToPrecisionCString(double value, int p) {
  const int kMinimalDigits = 1;
  DCHECK(p >= kMinimalDigits && p <= kMaxFractionDigits);
  USE(kMinimalDigits);

  bool negative = false;
  if (value < 0) {
    value = -value;
    negative = true;
  }

  // Find a sufficiently precise decimal representation of n.
  int decimal_point;
  int sign;
  // Add one for the terminating null character.
  const int kV8DtoaBufferCapacity = kMaxFractionDigits + 1;
  char decimal_rep[kV8DtoaBufferCapacity];
  int decimal_rep_length;

  base::DoubleToAscii(value, base::DTOA_PRECISION, p,
                      base::Vector<char>(decimal_rep, kV8DtoaBufferCapacity),
                      &sign, &decimal_rep_length, &decimal_point);
  DCHECK(decimal_rep_length <= p);

  int exponent = decimal_point - 1;

  char* result = nullptr;

  if (exponent < -6 || exponent >= p) {
    result =
        CreateExponentialRepresentation(decimal_rep, exponent, negative, p);
  } else {
    // Use fixed notation.
    //
    // Leave room in the result for appending a minus, a period and in
    // the case where decimal_point is not positive for a zero in
    // front of the period.
    unsigned result_size =
        (decimal_point <= 0) ? -decimal_point + p + 3 : p + 2;
    SimpleStringBuilder builder(result_size + 1);
    if (negative) builder.AddCharacter('-');
    if (decimal_point <= 0) {
      builder.AddString("0.");
      builder.AddPadding('0', -decimal_point);
      builder.AddString(decimal_rep);
      builder.AddPadding('0', p - decimal_rep_length);
    } else {
      const int m = std::min(decimal_rep_length, decimal_point);
      builder.AddSubstring(decimal_rep, m);
      builder.AddPadding('0', decimal_point - decimal_rep_length);
      if (decimal_point < p) {
        builder.AddCharacter('.');
        const int extra = negative ? 2 : 1;
        if (decimal_rep_length > decimal_point) {
          const size_t len = strlen(decimal_rep + decimal_point);
          DCHECK_GE(kMaxInt, len);
          const int n =
              std::min(static_cast<int>(len), p - (builder.position() - extra));
          builder.AddSubstring(decimal_rep + decimal_point, n);
        }
        builder.AddPadding('0', extra + (p - builder.position()));
      }
    }
    result = builder.Finalize();
  }

  return result;
}

char* DoubleToRadixCString(double value, int radix) {
  DCHECK(radix >= 2 && radix <= 36);
  DCHECK(std::isfinite(value));
  DCHECK_NE(0.0, value);
  // Character array used for conversion.
  static const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";

  // Temporary buffer for the result. We start with the decimal point in the
  // middle and write to the left for the integer part and to the right for the
  // fractional part. 1024 characters for the exponent and 52 for the mantissa
  // either way, with additional space for sign, decimal point and string
  // termination should be sufficient.
  static const int kBufferSize = 2200;
  char buffer[kBufferSize];
  int integer_cursor = kBufferSize / 2;
  int fraction_cursor = integer_cursor;

  bool negative = value < 0;
  if (negative) value = -value;

  // Split the value into an integer part and a fractional part.
  double integer = std::floor(value);
  double fraction = value - integer;
  // We only compute fractional digits up to the input double's precision.
  double delta = 0.5 * (base::Double(value).NextDouble() - value);
  delta = std::max(base::Double(0.0).NextDouble(), delta);
  DCHECK_GT(delta, 0.0);
  if (fraction >= delta) {
    // Insert decimal point.
    buffer[fraction_cursor++] = '.';
    do {
      // Shift up by one digit.
      fraction *= radix;
      delta *= radix;
      // Write digit.
      int digit = static_cast<int>(fraction);
      buffer[fraction_cursor++] = chars[digit];
      // Calculate remainder.
      fraction -= digit;
      // Round to even.
      if (fraction > 0.5 || (fraction == 0.5 && (digit & 1))) {
        if (fraction + delta > 1) {
          // We need to back trace already written digits in case of carry-over.
          while (true) {
            fraction_cursor--;
            if (fraction_cursor == kBufferSize / 2) {
              CHECK_EQ('.', buffer[fraction_cursor]);
              // Carry over to the integer part.
              integer += 1;
              break;
            }
            char c = buffer[fraction_cursor];
            // Reconstruct digit.
            digit = c > '9' ? (c - 'a' + 10) : (c - '0');
            if (digit + 1 < radix) {
              buffer[fraction_cursor++] = chars[digit + 1];
              break;
            }
          }
          break;
        }
      }
    } while (fraction >= delta);
  }

  // Compute integer digits. Fill unrepresented digits with zero.
  while (base::Double(integer / radix).Exponent() > 0) {
    integer /= radix;
    buffer[--integer_cursor] = '0';
  }
  do {
    double remainder = Modulo(integer, radix);
    buffer[--integer_cursor] = chars[static_cast<int>(remainder)];
    integer = (integer - remainder) / radix;
  } while (integer > 0);

  // Add sign and terminate string.
  if (negative) buffer[--integer_cursor] = '-';
  buffer[fraction_cursor++] = '\0';
  DCHECK_LT(fraction_cursor, kBufferSize);
  DCHECK_LE(0, integer_cursor);
  // Allocate new string as return value.
  char* result = NewArray<char>(fraction_cursor - integer_cursor);
  memcpy(result, buffer + integer_cursor, fraction_cursor - integer_cursor);
  return result;
}

// ES6 18.2.4 parseFloat(string)
double StringToDouble(Isolate* isolate, Handle<String> string, int flags,
                      double empty_string_val) {
  Handle<String> flattened = String::Flatten(isolate, string);
  return FlatStringToDouble(*flattened, flags, empty_string_val);
}

double FlatStringToDouble(Tagged<String> string, int flags,
                          double empty_string_val) {
  DisallowGarbageCollection no_gc;
  DCHECK(string->IsFlat());
  String::FlatContent flat = string->GetFlatContent(no_gc);
  DCHECK(flat.IsFlat());
  if (flat.IsOneByte()) {
    return StringToDouble(flat.ToOneByteVector(), flags, empty_string_val);
  } else {
    return StringToDouble(flat.ToUC16Vector(), flags, empty_string_val);
  }
}

base::Optional<double> TryStringToDouble(LocalIsolate* isolate,
                                         Handle<String> object,
                                         int max_length_for_conversion) {
  DisallowGarbageCollection no_gc;
  int length = object->length();
  if (length > max_length_for_conversion) {
    return base::nullopt;
  }

  const int flags = ALLOW_HEX | ALLOW_OCTAL | ALLOW_BINARY;
  auto buffer = std::make_unique<base::uc16[]>(max_length_for_conversion);
  SharedStringAccessGuardIfNeeded access_guard(isolate);
  String::WriteToFlat(*object, buffer.get(), 0, length, access_guard);
  base::Vector<const base::uc16> v(buffer.get(), length);
  return StringToDouble(v, flags);
}

base::Optional<double> TryStringToInt(LocalIsolate* isolate,
                                      Handle<String> object, int radix) {
  DisallowGarbageCollection no_gc;
  const int kMaxLengthForConversion = 20;
  int length = object->length();
  if (length > kMaxLengthForConversion) {
    return base::nullopt;
  }

  if (String::IsOneByteRepresentationUnderneath(*object)) {
    uint8_t buffer[kMaxLengthForConversion];
    SharedStringAccessGuardIfNeeded access_guard(isolate);
    String::WriteToFlat(*object, buffer, 0, length, access_guard);
    NumberParseIntHelper helper(buffer, radix, length);
    return helper.GetResult();
  } else {
    base::uc16 buffer[kMaxLengthForConversion];
    SharedStringAccessGuardIfNeeded access_guard(isolate);
    String::WriteToFlat(*object, buffer, 0, length, access_guard);
    NumberParseIntHelper helper(buffer, radix, length);
    return helper.GetResult();
  }
}

bool IsSpecialIndex(Tagged<String> string) {
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(string));
  SharedStringAccessGuardIfNeeded access_guard =
      SharedStringAccessGuardIfNeeded::NotNeeded();
  return IsSpecialIndex(string, access_guard);
}

bool IsSpecialIndex(Tagged<String> string,
                    SharedStringAccessGuardIfNeeded& access_guard) {
  // Max length of canonical double: -X.XXXXXXXXXXXXXXXXX-eXXX
  const int kBufferSize = 24;
  const int length = string->length();
  if (length == 0 || length > kBufferSize) return false;
  uint16_t buffer[kBufferSize];
  String::WriteToFlat(string, buffer, 0, length, access_guard);
  // If the first char is not a digit or a '-' or we can't match 'NaN' or
  // '(-)Infinity', bailout immediately.
  int offset = 0;
  if (!IsDecimalDigit(buffer[0])) {
    if (buffer[0] == '-') {
      if (length == 1) return false;  // Just '-' is bad.
      if (!IsDecimalDigit(buffer[1])) {
        if (buffer[1] == 'I' && length == 9) {
          // Allow matching of '-Infinity' below.
        } else {
          return false;
        }
      }
      offset++;
    } else if (buffer[0] == 'I' && length == 8) {
      // Allow matching of 'Infinity' below.
    } else if (buffer[0] == 'N' && length == 3) {
      // Match NaN.
      return buffer[1] == 'a' && buffer[2] == 'N';
    } else {
      return false;
    }
  }
  // Expected fast path: key is an integer.
  static const int kRepresentableIntegerLength = 15;  // (-)XXXXXXXXXXXXXXX
  if (length - offset <= kRepresentableIntegerLength) {
    const int initial_offset = offset;
    bool matches = true;
    for (; offset < length; offset++) {
      matches &= IsDecimalDigit(buffer[offset]);
    }
    if (matches) {
      // Match 0 and -0.
      if (buffer[initial_offset] == '0') return initial_offset == length - 1;
      return true;
    }
  }
  // Slow path: test DoubleToString(StringToDouble(string)) == string.
  base::Vector<const uint16_t> vector(buffer, length);
  double d = StringToDouble(vector, NO_CONVERSION_FLAGS);
  if (std::isnan(d)) return false;
  // Compute reverse string.
  char reverse_buffer[kBufferSize + 1];  // Result will be /0 terminated.
  base::Vector<char> reverse_vector(reverse_buffer, arraysize(reverse_buffer));
  const char* reverse_string = DoubleToCString(d, reverse_vector);
  for (int i = 0; i < length; ++i) {
    if (static_cast<uint16_t>(reverse_string[i]) != buffer[i]) return false;
  }
  return true;
}

float DoubleToFloat32_NoInline(double x) { return DoubleToFloat32(x); }

int32_t DoubleToInt32_NoInline(double x) { return DoubleToInt32(x); }

}  // namespace internal
}  // namespace v8

#undef FPCLASSIFY_NAMESPACE
