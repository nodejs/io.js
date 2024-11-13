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

#ifndef ABSL_STRINGS_INTERNAL_CHARCONV_BIGINT_H_
#define ABSL_STRINGS_INTERNAL_CHARCONV_BIGINT_H_

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

#include "absl/base/config.h"
#include "absl/strings/ascii.h"
#include "absl/strings/internal/charconv_parse.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

// The largest power that 5 that can be raised to, and still fit in a uint32_t.
constexpr int kMaxSmallPowerOfFive = 13;
// The largest power that 10 that can be raised to, and still fit in a uint32_t.
constexpr int kMaxSmallPowerOfTen = 9;

ABSL_DLL extern const uint32_t
    kFiveToNth[kMaxSmallPowerOfFive + 1];
ABSL_DLL extern const uint32_t kTenToNth[kMaxSmallPowerOfTen + 1];

// Large, fixed-width unsigned integer.
//
// Exact rounding for decimal-to-binary floating point conversion requires very
// large integer math, but a design goal of absl::from_chars is to avoid
// allocating memory.  The integer precision needed for decimal-to-binary
// conversions is large but bounded, so a huge fixed-width integer class
// suffices.
//
// This is an intentionally limited big integer class.  Only needed operations
// are implemented.  All storage lives in an array data member, and all
// arithmetic is done in-place, to avoid requiring separate storage for operand
// and result.
//
// This is an internal class.  Some methods live in the .cc file, and are
// instantiated only for the values of max_words we need.
template <int max_words>
class BigUnsigned {
 public:
  static_assert(max_words == 4 || max_words == 84,
                "unsupported max_words value");

  BigUnsigned() : size_(0), words_{} {}
  explicit constexpr BigUnsigned(uint64_t v)
      : size_((v >> 32) ? 2 : v ? 1 : 0),
        words_{static_cast<uint32_t>(v & 0xffffffffu),
               static_cast<uint32_t>(v >> 32)} {}

  // Constructs a BigUnsigned from the given string_view containing a decimal
  // value.  If the input string is not a decimal integer, constructs a 0
  // instead.
  explicit BigUnsigned(absl::string_view sv) : size_(0), words_{} {
    // Check for valid input, returning a 0 otherwise.  This is reasonable
    // behavior only because this constructor is for unit tests.
    if (std::find_if_not(sv.begin(), sv.end(), ascii_isdigit) != sv.end() ||
        sv.empty()) {
      return;
    }
    int exponent_adjust =
        ReadDigits(sv.data(), sv.data() + sv.size(), Digits10() + 1);
    if (exponent_adjust > 0) {
      MultiplyByTenToTheNth(exponent_adjust);
    }
  }

  // Loads the mantissa value of a previously-parsed float.
  //
  // Returns the associated decimal exponent.  The value of the parsed float is
  // exactly *this * 10**exponent.
  int ReadFloatMantissa(const ParsedFloat& fp, int significant_digits);

  // Returns the number of decimal digits of precision this type provides.  All
  // numbers with this many decimal digits or fewer are representable by this
  // type.
  //
  // Analogous to std::numeric_limits<BigUnsigned>::digits10.
  static constexpr int Digits10() {
    // 9975007/1035508 is very slightly less than log10(2**32).
    return static_cast<uint64_t>(max_words) * 9975007 / 1035508;
  }

  // Shifts left by the given number of bits.
  void ShiftLeft(int count) {
    if (count > 0) {
      const int word_shift = count / 32;
      if (word_shift >= max_words) {
        SetToZero();
        return;
      }
      size_ = (std::min)(size_ + word_shift, max_words);
      count %= 32;
      if (count == 0) {
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=warray-bounds
// shows a lot of bogus -Warray-bounds warnings under GCC.
// This is not the only one in Abseil.
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(14, 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
        std::copy_backward(words_, words_ + size_ - word_shift, words_ + size_);
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(14, 0)
#pragma GCC diagnostic pop
#endif
      } else {
        for (int i = (std::min)(size_, max_words - 1); i > word_shift; --i) {
          words_[i] = (words_[i - word_shift] << count) |
                      (words_[i - word_shift - 1] >> (32 - count));
        }
        words_[word_shift] = words_[0] << count;
        // Grow size_ if necessary.
        if (size_ < max_words && words_[size_]) {
          ++size_;
        }
      }
      std::fill_n(words_, word_shift, 0u);
    }
  }


  // Multiplies by v in-place.
  void MultiplyBy(uint32_t v) {
    if (size_ == 0 || v == 1) {
      return;
    }
    if (v == 0) {
      SetToZero();
      return;
    }
    const uint64_t factor = v;
    uint64_t window = 0;
    for (int i = 0; i < size_; ++i) {
      window += factor * words_[i];
      words_[i] = window & 0xffffffff;
      window >>= 32;
    }
    // If carry bits remain and there's space for them, grow size_.
    if (window && size_ < max_words) {
      words_[size_] = window & 0xffffffff;
      ++size_;
    }
  }

  void MultiplyBy(uint64_t v) {
    uint32_t words[2];
    words[0] = static_cast<uint32_t>(v);
    words[1] = static_cast<uint32_t>(v >> 32);
    if (words[1] == 0) {
      MultiplyBy(words[0]);
    } else {
      MultiplyBy(2, words);
    }
  }

  // Multiplies in place by 5 to the power of n.  n must be non-negative.
  void MultiplyByFiveToTheNth(int n) {
    while (n >= kMaxSmallPowerOfFive) {
      MultiplyBy(kFiveToNth[kMaxSmallPowerOfFive]);
      n -= kMaxSmallPowerOfFive;
    }
    if (n > 0) {
      MultiplyBy(kFiveToNth[n]);
    }
  }

  // Multiplies in place by 10 to the power of n.  n must be non-negative.
  void MultiplyByTenToTheNth(int n) {
    if (n > kMaxSmallPowerOfTen) {
      // For large n, raise to a power of 5, then shift left by the same amount.
      // (10**n == 5**n * 2**n.)  This requires fewer multiplications overall.
      MultiplyByFiveToTheNth(n);
      ShiftLeft(n);
    } else if (n > 0) {
      // We can do this more quickly for very small N by using a single
      // multiplication.
      MultiplyBy(kTenToNth[n]);
    }
  }

  // Returns the value of 5**n, for non-negative n.  This implementation uses
  // a lookup table, and is faster then seeding a BigUnsigned with 1 and calling
  // MultiplyByFiveToTheNth().
  static BigUnsigned FiveToTheNth(int n);

  // Multiplies by another BigUnsigned, in-place.
  template <int M>
  void MultiplyBy(const BigUnsigned<M>& other) {
    MultiplyBy(other.size(), other.words());
  }

  void SetToZero() {
    std::fill_n(words_, size_, 0u);
    size_ = 0;
  }

  // Returns the value of the nth word of this BigUnsigned.  This is
  // range-checked, and returns 0 on out-of-bounds accesses.
  uint32_t GetWord(int index) const {
    if (index < 0 || index >= size_) {
      return 0;
    }
    return words_[index];
  }

  // Returns this integer as a decimal string.  This is not used in the decimal-
  // to-binary conversion; it is intended to aid in testing.
  std::string ToString() const;

  int size() const { return size_; }
  const uint32_t* words() const { return words_; }

 private:
  // Reads the number between [begin, end), possibly containing a decimal point,
  // into this BigUnsigned.
  //
  // Callers are required to ensure [begin, end) contains a valid number, with
  // one or more decimal digits and at most one decimal point.  This routine
  // will behave unpredictably if these preconditions are not met.
  //
  // Only the first `significant_digits` digits are read.  Digits beyond this
  // limit are "sticky": If the final significant digit is 0 or 5, and if any
  // dropped digit is nonzero, then that final significant digit is adjusted up
  // to 1 or 6.  This adjustment allows for precise rounding.
  //
  // Returns `exponent_adjustment`, a power-of-ten exponent adjustment to
  // account for the decimal point and for dropped significant digits.  After
  // this function returns,
  //   actual_value_of_parsed_string ~= *this * 10**exponent_adjustment.
  int ReadDigits(const char* begin, const char* end, int significant_digits);

  // Performs a step of big integer multiplication.  This computes the full
  // (64-bit-wide) values that should be added at the given index (step), and
  // adds to that location in-place.
  //
  // Because our math all occurs in place, we must multiply starting from the
  // highest word working downward.  (This is a bit more expensive due to the
  // extra carries involved.)
  //
  // This must be called in steps, for each word to be calculated, starting from
  // the high end and working down to 0.  The first value of `step` should be
  //   `std::min(original_size + other.size_ - 2, max_words - 1)`.
  // The reason for this expression is that multiplying the i'th word from one
  // multiplicand and the j'th word of another multiplicand creates a
  // two-word-wide value to be stored at the (i+j)'th element.  The highest
  // word indices we will access are `original_size - 1` from this object, and
  // `other.size_ - 1` from our operand.  Therefore,
  // `original_size + other.size_ - 2` is the first step we should calculate,
  // but limited on an upper bound by max_words.

  // Working from high-to-low ensures that we do not overwrite the portions of
  // the initial value of *this which are still needed for later steps.
  //
  // Once called with step == 0, *this contains the result of the
  // multiplication.
  //
  // `original_size` is the size_ of *this before the first call to
  // MultiplyStep().  `other_words` and `other_size` are the contents of our
  // operand.  `step` is the step to perform, as described above.
  void MultiplyStep(int original_size, const uint32_t* other_words,
                    int other_size, int step);

  void MultiplyBy(int other_size, const uint32_t* other_words) {
    const int original_size = size_;
    const int first_step =
        (std::min)(original_size + other_size - 2, max_words - 1);
    for (int step = first_step; step >= 0; --step) {
      MultiplyStep(original_size, other_words, other_size, step);
    }
  }

  // Adds a 32-bit value to the index'th word, with carry.
  void AddWithCarry(int index, uint32_t value) {
    if (value) {
      while (index < max_words && value > 0) {
        words_[index] += value;
        // carry if we overflowed in this word:
        if (value > words_[index]) {
          value = 1;
          ++index;
        } else {
          value = 0;
        }
      }
      size_ = (std::min)(max_words, (std::max)(index + 1, size_));
    }
  }

  void AddWithCarry(int index, uint64_t value) {
    if (value && index < max_words) {
      uint32_t high = value >> 32;
      uint32_t low = value & 0xffffffff;
      words_[index] += low;
      if (words_[index] < low) {
        ++high;
        if (high == 0) {
          // Carry from the low word caused our high word to overflow.
          // Short circuit here to do the right thing.
          AddWithCarry(index + 2, static_cast<uint32_t>(1));
          return;
        }
      }
      if (high > 0) {
        AddWithCarry(index + 1, high);
      } else {
        // Normally 32-bit AddWithCarry() sets size_, but since we don't call
        // it when `high` is 0, do it ourselves here.
        size_ = (std::min)(max_words, (std::max)(index + 1, size_));
      }
    }
  }

  // Divide this in place by a constant divisor.  Returns the remainder of the
  // division.
  template <uint32_t divisor>
  uint32_t DivMod() {
    uint64_t accumulator = 0;
    for (int i = size_ - 1; i >= 0; --i) {
      accumulator <<= 32;
      accumulator += words_[i];
      // accumulator / divisor will never overflow an int32_t in this loop
      words_[i] = static_cast<uint32_t>(accumulator / divisor);
      accumulator = accumulator % divisor;
    }
    while (size_ > 0 && words_[size_ - 1] == 0) {
      --size_;
    }
    return static_cast<uint32_t>(accumulator);
  }

  // The number of elements in words_ that may carry significant values.
  // All elements beyond this point are 0.
  //
  // When size_ is 0, this BigUnsigned stores the value 0.
  // When size_ is nonzero, is *not* guaranteed that words_[size_ - 1] is
  // nonzero.  This can occur due to overflow truncation.
  // In particular, x.size_ != y.size_ does *not* imply x != y.
  int size_;
  uint32_t words_[max_words];
};

// Compares two big integer instances.
//
// Returns -1 if lhs < rhs, 0 if lhs == rhs, and 1 if lhs > rhs.
template <int N, int M>
int Compare(const BigUnsigned<N>& lhs, const BigUnsigned<M>& rhs) {
  int limit = (std::max)(lhs.size(), rhs.size());
  for (int i = limit - 1; i >= 0; --i) {
    const uint32_t lhs_word = lhs.GetWord(i);
    const uint32_t rhs_word = rhs.GetWord(i);
    if (lhs_word < rhs_word) {
      return -1;
    } else if (lhs_word > rhs_word) {
      return 1;
    }
  }
  return 0;
}

template <int N, int M>
bool operator==(const BigUnsigned<N>& lhs, const BigUnsigned<M>& rhs) {
  int limit = (std::max)(lhs.size(), rhs.size());
  for (int i = 0; i < limit; ++i) {
    if (lhs.GetWord(i) != rhs.GetWord(i)) {
      return false;
    }
  }
  return true;
}

template <int N, int M>
bool operator!=(const BigUnsigned<N>& lhs, const BigUnsigned<M>& rhs) {
  return !(lhs == rhs);
}

template <int N, int M>
bool operator<(const BigUnsigned<N>& lhs, const BigUnsigned<M>& rhs) {
  return Compare(lhs, rhs) == -1;
}

template <int N, int M>
bool operator>(const BigUnsigned<N>& lhs, const BigUnsigned<M>& rhs) {
  return rhs < lhs;
}
template <int N, int M>
bool operator<=(const BigUnsigned<N>& lhs, const BigUnsigned<M>& rhs) {
  return !(rhs < lhs);
}
template <int N, int M>
bool operator>=(const BigUnsigned<N>& lhs, const BigUnsigned<M>& rhs) {
  return !(lhs < rhs);
}

// Output operator for BigUnsigned, for testing purposes only.
template <int N>
std::ostream& operator<<(std::ostream& os, const BigUnsigned<N>& num) {
  return os << num.ToString();
}

// Explicit instantiation declarations for the sizes of BigUnsigned that we
// are using.
//
// For now, the choices of 4 and 84 are arbitrary; 4 is a small value that is
// still bigger than an int128, and 84 is a large value we will want to use
// in the from_chars implementation.
//
// Comments justifying the use of 84 belong in the from_chars implementation,
// and will be added in a follow-up CL.
extern template class BigUnsigned<4>;
extern template class BigUnsigned<84>;

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CHARCONV_BIGINT_H_
