// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <limits>

#include "src/bigint/bigint-internal.h"
#include "src/bigint/digit-arithmetic.h"
#include "src/bigint/div-helpers.h"
#include "src/bigint/util.h"
#include "src/bigint/vector-arithmetic.h"

namespace v8 {
namespace bigint {

namespace {

// Lookup table for the maximum number of bits required per character of a
// base-N string representation of a number. To increase accuracy, the array
// value is the actual value multiplied by 32. To generate this table:
// for (var i = 0; i <= 36; i++) { print(Math.ceil(Math.log2(i) * 32) + ","); }
constexpr uint8_t kMaxBitsPerChar[] = {
    0,   0,   32,  51,  64,  75,  83,  90,  96,  // 0..8
    102, 107, 111, 115, 119, 122, 126, 128,      // 9..16
    131, 134, 136, 139, 141, 143, 145, 147,      // 17..24
    149, 151, 153, 154, 156, 158, 159, 160,      // 25..32
    162, 163, 165, 166,                          // 33..36
};

static const int kBitsPerCharTableShift = 5;
static const size_t kBitsPerCharTableMultiplier = 1u << kBitsPerCharTableShift;

static const char kConversionChars[] = "0123456789abcdefghijklmnopqrstuvwxyz";

// Raises {base} to the power of {exponent}. Does not check for overflow.
digit_t digit_pow(digit_t base, digit_t exponent) {
  digit_t result = 1ull;
  while (exponent > 0) {
    if (exponent & 1) {
      result *= base;
    }
    exponent >>= 1;
    base *= base;
  }
  return result;
}

// Compile-time version of the above.
constexpr digit_t digit_pow_rec(digit_t base, digit_t exponent) {
  return exponent == 1 ? base : base * digit_pow_rec(base, exponent - 1);
}

// A variant of ToStringFormatter::BasecaseLast, specialized for a radix
// known at compile-time.
template <int radix>
char* BasecaseFixedLast(digit_t chunk, char* out) {
  while (chunk != 0) {
    DCHECK(*(out - 1) == kStringZapValue);
    if (radix <= 10) {
      *(--out) = '0' + (chunk % radix);
    } else {
      *(--out) = kConversionChars[chunk % radix];
    }
    chunk /= radix;
  }
  return out;
}

// By making {radix} a compile-time constant and computing {chunk_divisor}
// as another compile-time constant from it, we allow the compiler to emit
// an optimized instruction sequence based on multiplications with "magic"
// numbers (modular multiplicative inverses) instead of actual divisions.
// The price we pay is having to work on half digits; the technique doesn't
// work with twodigit_t-by-digit_t divisions.
// Includes an equivalent of ToStringFormatter::BasecaseMiddle, accordingly
// specialized for a radix known at compile time.
template <digit_t radix>
char* DivideByMagic(RWDigits rest, Digits input, char* output) {
  constexpr uint8_t max_bits_per_char = kMaxBitsPerChar[radix];
  constexpr int chunk_chars =
      kHalfDigitBits * kBitsPerCharTableMultiplier / max_bits_per_char;
  constexpr digit_t chunk_divisor = digit_pow_rec(radix, chunk_chars);
  digit_t remainder = 0;
  for (int i = input.len() - 1; i >= 0; i--) {
    digit_t d = input[i];
    digit_t upper = (remainder << kHalfDigitBits) | (d >> kHalfDigitBits);
    digit_t u_result = upper / chunk_divisor;
    remainder = upper % chunk_divisor;
    digit_t lower = (remainder << kHalfDigitBits) | (d & kHalfDigitMask);
    digit_t l_result = lower / chunk_divisor;
    remainder = lower % chunk_divisor;
    rest[i] = (u_result << kHalfDigitBits) | l_result;
  }
  // {remainder} is now the current chunk to be written out.
  for (int i = 0; i < chunk_chars; i++) {
    DCHECK(*(output - 1) == kStringZapValue);
    if (radix <= 10) {
      *(--output) = '0' + (remainder % radix);
    } else {
      *(--output) = kConversionChars[remainder % radix];
    }
    remainder /= radix;
  }
  DCHECK(remainder == 0);
  return output;
}

class RecursionLevel;

// The classic algorithm must check for interrupt requests if no faster
// algorithm is available.
#if V8_ADVANCED_BIGINT_ALGORITHMS
#define MAYBE_INTERRUPT(code) ((void)0)
#else
#define MAYBE_INTERRUPT(code) code
#endif

class ToStringFormatter {
 public:
  ToStringFormatter(Digits X, int radix, bool sign, char* out,
                    int chars_available, ProcessorImpl* processor)
      : digits_(X),
        radix_(radix),
        sign_(sign),
        out_start_(out),
        out_end_(out + chars_available),
        out_(out_end_),
        processor_(processor) {
    digits_.Normalize();
    DCHECK(chars_available >= ToStringResultLength(digits_, radix_, sign_));
  }

  void Start();
  int Finish();

  void Classic() {
    if (digits_.len() == 0) {
      *(--out_) = '0';
      return;
    }
    if (digits_.len() == 1) {
      out_ = BasecaseLast(digits_[0], out_);
      return;
    }
    // {rest} holds the part of the BigInt that we haven't looked at yet.
    // Not to be confused with "remainder"!
    ScratchDigits rest(digits_.len());
    // In the first round, divide the input, allocating a new BigInt for
    // the result == rest; from then on divide the rest in-place.
    Digits dividend = digits_;
    do {
      if (radix_ == 10) {
        // Faster but costs binary size, so we optimize the most common case.
        out_ = DivideByMagic<10>(rest, dividend, out_);
        MAYBE_INTERRUPT(processor_->AddWorkEstimate(rest.len() * 2));
      } else {
        digit_t chunk;
        processor_->DivideSingle(rest, &chunk, dividend, chunk_divisor_);
        out_ = BasecaseMiddle(chunk, out_);
        // Assume that a division is about ten times as expensive as a
        // multiplication.
        MAYBE_INTERRUPT(processor_->AddWorkEstimate(rest.len() * 10));
      }
      MAYBE_INTERRUPT(if (processor_->should_terminate()) return );
      rest.Normalize();
      dividend = rest;
    } while (rest.len() > 1);
    out_ = BasecaseLast(rest[0], out_);
  }

  void BasePowerOfTwo();

  void Fast();
  char* FillWithZeros(RecursionLevel* level, char* prev_cursor, char* out,
                      bool is_last_on_level);
  char* ProcessLevel(RecursionLevel* level, Digits chunk, char* out,
                     bool is_last_on_level);

 private:
  // When processing the last (most significant) digit, don't write leading
  // zeros.
  char* BasecaseLast(digit_t digit, char* out) {
    if (radix_ == 10) return BasecaseFixedLast<10>(digit, out);
    do {
      DCHECK(*(out - 1) == kStringZapValue);
      *(--out) = kConversionChars[digit % radix_];
      digit /= radix_;
    } while (digit > 0);
    return out;
  }

  // When processing a middle (non-most significant) digit, always write the
  // same number of characters (as many '0' as necessary).
  char* BasecaseMiddle(digit_t digit, char* out) {
    for (int i = 0; i < chunk_chars_; i++) {
      DCHECK(*(out - 1) == kStringZapValue);
      *(--out) = kConversionChars[digit % radix_];
      digit /= radix_;
    }
    DCHECK(digit == 0);
    return out;
  }

  Digits digits_;
  int radix_;
  int max_bits_per_char_ = 0;
  int chunk_chars_ = 0;
  bool sign_;
  char* out_start_;
  char* out_end_;
  char* out_;
  digit_t chunk_divisor_ = 0;
  ProcessorImpl* processor_;
};

#undef MAYBE_INTERRUPT

// Prepares data for {Classic}. Not needed for {BasePowerOfTwo}.
void ToStringFormatter::Start() {
  max_bits_per_char_ = kMaxBitsPerChar[radix_];
  chunk_chars_ = kDigitBits * kBitsPerCharTableMultiplier / max_bits_per_char_;
  chunk_divisor_ = digit_pow(radix_, chunk_chars_);
  // By construction of chunk_chars_, there can't have been overflow.
  DCHECK(chunk_divisor_ != 0);
}

int ToStringFormatter::Finish() {
  DCHECK(out_ >= out_start_);
  DCHECK(out_ < out_end_);  // At least one character was written.
  while (out_ < out_end_ && *out_ == '0') out_++;
  if (sign_) *(--out_) = '-';
  int excess = 0;
  if (out_ > out_start_) {
    size_t actual_length = out_end_ - out_;
    excess = static_cast<int>(out_ - out_start_);
    std::memmove(out_start_, out_, actual_length);
  }
  return excess;
}

void ToStringFormatter::BasePowerOfTwo() {
  const int bits_per_char = CountTrailingZeros(radix_);
  const int char_mask = radix_ - 1;
  digit_t digit = 0;
  // Keeps track of how many unprocessed bits there are in {digit}.
  int available_bits = 0;
  for (int i = 0; i < digits_.len() - 1; i++) {
    digit_t new_digit = digits_[i];
    // Take any leftover bits from the last iteration into account.
    int current = (digit | (new_digit << available_bits)) & char_mask;
    *(--out_) = kConversionChars[current];
    int consumed_bits = bits_per_char - available_bits;
    digit = new_digit >> consumed_bits;
    available_bits = kDigitBits - consumed_bits;
    while (available_bits >= bits_per_char) {
      *(--out_) = kConversionChars[digit & char_mask];
      digit >>= bits_per_char;
      available_bits -= bits_per_char;
    }
  }
  // Take any leftover bits from the last iteration into account.
  digit_t msd = digits_.msd();
  int current = (digit | (msd << available_bits)) & char_mask;
  *(--out_) = kConversionChars[current];
  digit = msd >> (bits_per_char - available_bits);
  while (digit != 0) {
    *(--out_) = kConversionChars[digit & char_mask];
    digit >>= bits_per_char;
  }
}

#if V8_ADVANCED_BIGINT_ALGORITHMS

// "Fast" divide-and-conquer conversion to string. The basic idea is to
// recursively cut the BigInt in half (using a division with remainder,
// the divisor being ~half as large (in bits) as the current dividend).
//
// As preparation, we build up a linked list of metadata for each recursion
// level. We do this bottom-up, i.e. start with the level that will produce
// two halves that are register-sized and bail out to the base case.
// Each higher level (executed earlier, prepared later) uses a divisor that is
// the square of the previously-created "next" level's divisor. Preparation
// terminates when the current divisor is at least half as large as the bigint.
// We also precompute each level's divisor's inverse, so we can use
// Barrett division later.
//
// Example: say we want to format 1234567890123, and we can fit two decimal
// digits into a register for the base case.
//
//              1234567890123
//                    ↓
//               %100000000 (a)              // RecursionLevel 2,
//             /            \                // is_toplevel_ == true.
//         12345            67890123
//           ↓                  ↓
//    (e) %10000             %10000 (b)      // RecursionLevel 1
//        /    \            /      \
//       1     2345      6789      0123
//       ↓   (f) ↓         ↓ (d)     ↓
// (g) %100    %100      %100      %100 (c)  // RecursionLevel 0
//     / \     /   \     /   \     /   \
//    00 01   23   45   67   89   01   23
//        ↓    ↓    ↓    ↓    ↓    ↓    ↓    // Base case.
//       "1" "23" "45" "67" "89" "01" "23"
//
// We start building RecursionLevels in order 0 -> 1 -> 2, performing the
// squarings 100² = 10000 and 10000² = 100000000 each only once. Execution
// then happens in order (a) through (g); lower-level divisors are used
// repeatedly. We build the string from right to left.
// Note that we can skip the division at (g) and fall through directly.
// Also, note that there are two chunks with value 1: one of them must produce
// a leading "0" in its string representation, the other must not.
//
// In this example, {base_divisor} is 100 and {base_char_count} is 2.

// TODO(jkummerow): Investigate whether it is beneficial to build one or two
// fewer RecursionLevels, and use the topmost level for more than one division.

class RecursionLevel {
 public:
  static RecursionLevel* CreateLevels(digit_t base_divisor, int base_char_count,
                                      int target_bit_length,
                                      ProcessorImpl* processor);
  ~RecursionLevel() { delete next_; }

  void ComputeInverse(ProcessorImpl* proc, int dividend_length = 0);
  Digits GetInverse(int dividend_length);

 private:
  friend class ToStringFormatter;
  RecursionLevel(digit_t base_divisor, int base_char_count)
      : char_count_(base_char_count), divisor_(1) {
    divisor_[0] = base_divisor;
  }
  explicit RecursionLevel(RecursionLevel* next)
      : char_count_(next->char_count_ * 2),
        next_(next),
        divisor_(next->divisor_.len() * 2) {
    next->is_toplevel_ = false;
  }

  void LeftShiftDivisor() {
    leading_zero_shift_ = CountLeadingZeros(divisor_.msd());
    LeftShift(divisor_, divisor_, leading_zero_shift_);
  }

  int leading_zero_shift_{0};
  // The number of characters generated by *each half* of this level.
  int char_count_;
  bool is_toplevel_{true};
  RecursionLevel* next_{nullptr};
  ScratchDigits divisor_;
  std::unique_ptr<Storage> inverse_storage_;
  Digits inverse_;
};

// static
RecursionLevel* RecursionLevel::CreateLevels(digit_t base_divisor,
                                             int base_char_count,
                                             int target_bit_length,
                                             ProcessorImpl* processor) {
  RecursionLevel* level = new RecursionLevel(base_divisor, base_char_count);
  // We can stop creating levels when the next level's divisor, which is the
  // square of the current level's divisor, would be strictly bigger (in terms
  // of its numeric value) than the input we're formatting. Since computing that
  // next divisor is expensive, we want to predict the necessity based on bit
  // lengths. Bit lengths are an imperfect predictor of numeric value, so we
  // have to be careful:
  // - since we can't estimate which one of two numbers of equal bit length
  //   is bigger, we have to aim for a strictly bigger bit length.
  // - when squaring, the bit length sometimes doubles (e.g. 0b11² == 0b1001),
  //   but usually we "lose" a bit (e.g. 0b10² == 0b100).
  while (BitLength(level->divisor_) * 2 - 1 <= target_bit_length) {
    RecursionLevel* prev = level;
    level = new RecursionLevel(prev);
    processor->Multiply(level->divisor_, prev->divisor_, prev->divisor_);
    if (processor->should_terminate()) {
      delete level;
      return nullptr;
    }
    level->divisor_.Normalize();
    // Left-shifting the divisor must only happen after it's been used to
    // compute the next divisor.
    prev->LeftShiftDivisor();
    prev->ComputeInverse(processor);
  }
  level->LeftShiftDivisor();
  // Not calling info->ComputeInverse here so that it can take the input's
  // length into account to save some effort on inverse generation.
  return level;
}

// The top level might get by with a smaller inverse than we could maximally
// compute, so the caller should provide the dividend length.
void RecursionLevel::ComputeInverse(ProcessorImpl* processor,
                                    int dividend_length) {
  int inverse_len = divisor_.len();
  if (dividend_length != 0) {
    inverse_len = dividend_length - divisor_.len();
    DCHECK(inverse_len <= divisor_.len());
  }
  int scratch_len = InvertScratchSpace(inverse_len);
  ScratchDigits scratch(scratch_len);
  Storage* inv_storage = new Storage(inverse_len + 1);
  inverse_storage_.reset(inv_storage);
  RWDigits inverse_initializer(inv_storage->get(), inverse_len + 1);
  Digits input(divisor_, divisor_.len() - inverse_len, inverse_len);
  processor->Invert(inverse_initializer, input, scratch);
  inverse_initializer.TrimOne();
  inverse_ = inverse_initializer;
}

Digits RecursionLevel::GetInverse(int dividend_length) {
  DCHECK(inverse_.len() != 0);
  int inverse_len = dividend_length - divisor_.len();
  DCHECK(inverse_len <= inverse_.len());
  return inverse_ + (inverse_.len() - inverse_len);
}

void ToStringFormatter::Fast() {
  std::unique_ptr<RecursionLevel> recursion_levels(RecursionLevel::CreateLevels(
      chunk_divisor_, chunk_chars_, BitLength(digits_), processor_));
  if (processor_->should_terminate()) return;
  out_ = ProcessLevel(recursion_levels.get(), digits_, out_, true);
}

// Writes '0' characters right-to-left, starting at {out}-1, until the distance
// from {right_boundary} to {out} equals the number of characters that {level}
// is supposed to produce.
char* ToStringFormatter::FillWithZeros(RecursionLevel* level,
                                       char* right_boundary, char* out,
                                       bool is_last_on_level) {
  // Fill up with zeros up to the character count expected to be generated
  // on this level; unless this is the left edge of the result.
  if (is_last_on_level) return out;
  int chunk_chars = level == nullptr ? chunk_chars_ : level->char_count_ * 2;
  char* end = right_boundary - chunk_chars;
  DCHECK(out >= end);
  while (out > end) {
    *(--out) = '0';
  }
  return out;
}

char* ToStringFormatter::ProcessLevel(RecursionLevel* level, Digits chunk,
                                      char* out, bool is_last_on_level) {
  // Step 0: if only one digit is left, bail out to the base case.
  Digits normalized = chunk;
  normalized.Normalize();
  if (normalized.len() <= 1) {
    char* right_boundary = out;
    if (normalized.len() == 1) {
      out = BasecaseLast(normalized[0], out);
    }
    return FillWithZeros(level, right_boundary, out, is_last_on_level);
  }

  // Step 1: If the chunk is guaranteed to remain smaller than the divisor
  // even after left-shifting, fall through to the next level immediately.
  if (normalized.len() < level->divisor_.len()) {
    char* right_boundary = out;
    out = ProcessLevel(level->next_, chunk, out, is_last_on_level);
    return FillWithZeros(level, right_boundary, out, is_last_on_level);
  }
  // Step 2: Prepare the chunk.
  bool allow_inplace_modification = chunk.digits() != digits_.digits();
  Digits original_chunk = chunk;
  ShiftedDigits chunk_shifted(chunk, level->leading_zero_shift_,
                              allow_inplace_modification);
  chunk = chunk_shifted;
  chunk.Normalize();
  // Check (now precisely) if the chunk is smaller than the divisor.
  int comparison = Compare(chunk, level->divisor_);
  if (comparison <= 0) {
    char* right_boundary = out;
    if (comparison < 0) {
      // If the chunk is strictly smaller than the divisor, we can process
      // it directly on the next level as the right half, and know that the
      // left half is all '0'.
      // In case we shifted {chunk} in-place, we must undo that
      // before the call...
      chunk_shifted.Reset();
      // ...and otherwise undo the {chunk = chunk_shifted} assignment above.
      chunk = original_chunk;
      out = ProcessLevel(level->next_, chunk, out, is_last_on_level);
    } else {
      DCHECK(comparison == 0);
      // If the chunk is equal to the divisor, we know that the right half
      // is all '0', and the left half is '...0001'.
      // Handling this case specially is an optimization; we could also
      // fall through to the generic "chunk > divisor" path below.
      out = FillWithZeros(level->next_, right_boundary, out, false);
      *(--out) = '1';
    }
    // In both cases, make sure the left half is fully written.
    return FillWithZeros(level, right_boundary, out, is_last_on_level);
  }
  // Step 3: Allocate space for the results.
  // Allocate one extra digit so the next level can left-shift in-place.
  ScratchDigits right(level->divisor_.len() + 1);
  // Allocate one extra digit because DivideBarrett requires it.
  ScratchDigits left(chunk.len() - level->divisor_.len() + 1);

  // Step 4: Divide to split {chunk} into {left} and {right}.
  int inverse_len = chunk.len() - level->divisor_.len();
  if (inverse_len == 0) {
    processor_->DivideSchoolbook(left, right, chunk, level->divisor_);
  } else if (level->divisor_.len() == 1) {
    processor_->DivideSingle(left, right.digits(), chunk, level->divisor_[0]);
    for (int i = 1; i < right.len(); i++) right[i] = 0;
  } else {
    ScratchDigits scratch(DivideBarrettScratchSpace(chunk.len()));
    // The top level only computes its inverse when {chunk.len()} is
    // available. Other levels have precomputed theirs.
    if (level->is_toplevel_) {
      level->ComputeInverse(processor_, chunk.len());
      if (processor_->should_terminate()) return out;
    }
    Digits inverse = level->GetInverse(chunk.len());
    processor_->DivideBarrett(left, right, chunk, level->divisor_, inverse,
                              scratch);
    if (processor_->should_terminate()) return out;
  }
  RightShift(right, right, level->leading_zero_shift_);
#if DEBUG
  Digits left_test = left;
  left_test.Normalize();
  DCHECK(left_test.len() <= level->divisor_.len());
#endif

  // Step 5: Recurse.
  char* end_of_right_part = ProcessLevel(level->next_, right, out, false);
  if (processor_->should_terminate()) return out;
  // The recursive calls are required and hence designed to write exactly as
  // many characters as their level is responsible for.
  DCHECK(end_of_right_part == out - level->char_count_);
  USE(end_of_right_part);
  // We intentionally don't use {end_of_right_part} here to be prepared for
  // potential future multi-threaded execution.
  return ProcessLevel(level->next_, left, out - level->char_count_,
                      is_last_on_level);
}

#endif  // V8_ADVANCED_BIGINT_ALGORITHMS

}  // namespace

void ProcessorImpl::ToString(char* out, int* out_length, Digits X, int radix,
                             bool sign) {
  const bool use_fast_algorithm = X.len() >= kToStringFastThreshold;
  ToStringImpl(out, out_length, X, radix, sign, use_fast_algorithm);
}

// Factored out so that tests can call it.
void ProcessorImpl::ToStringImpl(char* out, int* out_length, Digits X,
                                 int radix, bool sign, bool fast) {
#if DEBUG
  for (int i = 0; i < *out_length; i++) out[i] = kStringZapValue;
#endif
  ToStringFormatter formatter(X, radix, sign, out, *out_length, this);
  if (IsPowerOfTwo(radix)) {
    formatter.BasePowerOfTwo();
#if V8_ADVANCED_BIGINT_ALGORITHMS
  } else if (fast) {
    formatter.Start();
    formatter.Fast();
    if (should_terminate()) return;
#else
    USE(fast);
#endif  // V8_ADVANCED_BIGINT_ALGORITHMS
  } else {
    formatter.Start();
    formatter.Classic();
  }
  int excess = formatter.Finish();
  *out_length -= excess;
  memset(out + *out_length, 0, excess);
}

Status Processor::ToString(char* out, int* out_length, Digits X, int radix,
                           bool sign) {
  ProcessorImpl* impl = static_cast<ProcessorImpl*>(this);
  impl->ToString(out, out_length, X, radix, sign);
  return impl->get_and_clear_status();
}

int ToStringResultLength(Digits X, int radix, bool sign) {
  const int bit_length = BitLength(X);
  int result;
  if (IsPowerOfTwo(radix)) {
    const int bits_per_char = CountTrailingZeros(radix);
    result = DIV_CEIL(bit_length, bits_per_char) + sign;
  } else {
    // Maximum number of bits we can represent with one character.
    const uint8_t max_bits_per_char = kMaxBitsPerChar[radix];
    // For estimating the result length, we have to be pessimistic and work with
    // the minimum number of bits one character can represent.
    const uint8_t min_bits_per_char = max_bits_per_char - 1;
    // Perform the following computation with uint64_t to avoid overflows.
    uint64_t chars_required = bit_length;
    chars_required *= kBitsPerCharTableMultiplier;
    chars_required = DIV_CEIL(chars_required, min_bits_per_char);
    DCHECK(chars_required <
           static_cast<uint64_t>(std::numeric_limits<int>::max()));
    result = static_cast<int>(chars_required);
  }
  result += sign;
  return result;
}

}  // namespace bigint
}  // namespace v8
