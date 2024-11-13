// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BIGINT_BIGINT_H_
#define V8_BIGINT_BIGINT_H_

#include <stdint.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace v8 {
namespace bigint {

// To play nice with embedders' macros, we define our own DCHECK here.
// It's only used in this file, and undef'ed at the end.
#ifdef DEBUG
#define BIGINT_H_DCHECK(cond)                         \
  if (!(cond)) {                                      \
    std::cerr << __FILE__ << ":" << __LINE__ << ": "; \
    std::cerr << "Assertion failed: " #cond "\n";     \
    abort();                                          \
  }

extern bool kAdvancedAlgorithmsEnabledInLibrary;
#else
#define BIGINT_H_DCHECK(cond) (void(0))
#endif

// The type of a digit: a register-width unsigned integer.
using digit_t = uintptr_t;
using signed_digit_t = intptr_t;
#if UINTPTR_MAX == 0xFFFFFFFF
// 32-bit platform.
using twodigit_t = uint64_t;
#define HAVE_TWODIGIT_T 1
static constexpr int kLog2DigitBits = 5;
#elif UINTPTR_MAX == 0xFFFFFFFFFFFFFFFF
// 64-bit platform.
static constexpr int kLog2DigitBits = 6;
#if defined(__SIZEOF_INT128__)
using twodigit_t = __uint128_t;
#define HAVE_TWODIGIT_T 1
#endif  // defined(__SIZEOF_INT128__)
#else
#error Unsupported platform.
#endif
static constexpr int kDigitBits = 1 << kLog2DigitBits;
static_assert(kDigitBits == 8 * sizeof(digit_t), "inconsistent type sizes");

// Describes an array of digits, also known as a BigInt. Unsigned.
// Does not own the memory it points at, and only gives read-only access to it.
// Digits are stored in little-endian order.
class Digits {
 public:
  // This is the constructor intended for public consumption.
  Digits(const digit_t* mem, int len)
      // The const_cast here is ugly, but we need the digits field to be mutable
      // for the RWDigits subclass. We pinky swear to not mutate the memory with
      // this class.
      : Digits(const_cast<digit_t*>(mem), len) {}

  Digits(digit_t* mem, int len) : digits_(mem), len_(len) {
    // Require 4-byte alignment (even on 64-bit platforms).
    // TODO(jkummerow): See if we can tighten BigInt alignment in V8 to
    // system pointer size, and raise this requirement to that.
    BIGINT_H_DCHECK((reinterpret_cast<uintptr_t>(mem) & 3) == 0);
  }

  // Provides a "slice" view into another Digits object.
  Digits(Digits src, int offset, int len)
      : digits_(src.digits_ + offset),
        len_(std::max(0, std::min(src.len_ - offset, len))) {
    BIGINT_H_DCHECK(offset >= 0);
  }

  Digits() : Digits(static_cast<digit_t*>(nullptr), 0) {}

  // Alternative way to get a "slice" view into another Digits object.
  Digits operator+(int i) {
    BIGINT_H_DCHECK(i >= 0 && i <= len_);
    return Digits(digits_ + i, len_ - i);
  }

  // Provides access to individual digits.
  digit_t operator[](int i) {
    BIGINT_H_DCHECK(i >= 0 && i < len_);
    return read_4byte_aligned(i);
  }
  // Convenience accessor for the most significant digit.
  digit_t msd() {
    BIGINT_H_DCHECK(len_ > 0);
    return read_4byte_aligned(len_ - 1);
  }
  // Checks "pointer equality" (does not compare digits contents).
  bool operator==(const Digits& other) const {
    return digits_ == other.digits_ && len_ == other.len_;
  }

  // Decrements {len_} until there are no leading zero digits left.
  void Normalize() {
    while (len_ > 0 && msd() == 0) len_--;
  }
  // Unconditionally drops exactly one leading zero digit.
  void TrimOne() {
    BIGINT_H_DCHECK(len_ > 0 && msd() == 0);
    len_--;
  }

  int len() { return len_; }
  const digit_t* digits() const { return digits_; }

 protected:
  friend class ShiftedDigits;
  digit_t* digits_;
  int len_;

 private:
  // We require externally-provided digits arrays to be 4-byte aligned, but
  // not necessarily 8-byte aligned; so on 64-bit platforms we use memcpy
  // to allow unaligned reads.
  digit_t read_4byte_aligned(int i) {
    if (sizeof(digit_t) == 4) {
      return digits_[i];
    } else {
      digit_t result;
      memcpy(&result, static_cast<const void*>(digits_ + i), sizeof(result));
      return result;
    }
  }
};

// Writable version of a Digits array.
// Does not own the memory it points at.
class RWDigits : public Digits {
 public:
  RWDigits(digit_t* mem, int len) : Digits(mem, len) {}
  RWDigits(RWDigits src, int offset, int len) : Digits(src, offset, len) {}
  RWDigits operator+(int i) {
    BIGINT_H_DCHECK(i >= 0 && i <= len_);
    return RWDigits(digits_ + i, len_ - i);
  }

#if UINTPTR_MAX == 0xFFFFFFFF
  digit_t& operator[](int i) {
    BIGINT_H_DCHECK(i >= 0 && i < len_);
    return digits_[i];
  }
#else
  // 64-bit platform. We only require digits arrays to be 4-byte aligned,
  // so we use a wrapper class to allow regular array syntax while
  // performing unaligned memory accesses under the hood.
  class WritableDigitReference {
   public:
    // Support "X[i] = x" notation.
    void operator=(digit_t digit) { memcpy(ptr_, &digit, sizeof(digit)); }
    // Support "X[i] = Y[j]" notation.
    WritableDigitReference& operator=(const WritableDigitReference& src) {
      memcpy(ptr_, src.ptr_, sizeof(digit_t));
      return *this;
    }
    // Support "x = X[i]" notation.
    operator digit_t() {
      digit_t result;
      memcpy(&result, ptr_, sizeof(result));
      return result;
    }

   private:
    // This class is not for public consumption.
    friend class RWDigits;
    // Primary constructor.
    explicit WritableDigitReference(digit_t* ptr)
        : ptr_(reinterpret_cast<uint32_t*>(ptr)) {}
    // Required for returning WDR instances from "operator[]" below.
    WritableDigitReference(const WritableDigitReference& src) = default;

    uint32_t* ptr_;
  };

  WritableDigitReference operator[](int i) {
    BIGINT_H_DCHECK(i >= 0 && i < len_);
    return WritableDigitReference(digits_ + i);
  }
#endif

  digit_t* digits() { return digits_; }
  void set_len(int len) { len_ = len; }

  void Clear() { memset(digits_, 0, len_ * sizeof(digit_t)); }
};

class Platform {
 public:
  virtual ~Platform() = default;

  // If you want the ability to interrupt long-running operations, implement
  // a Platform subclass that overrides this method. It will be queried
  // every now and then by long-running operations.
  virtual bool InterruptRequested() { return false; }
};

// These are the operations that this library supports.
// The signatures follow the convention:
//
//   void Operation(RWDigits results, Digits inputs);
//
// You must preallocate the result; use the respective {OperationResultLength}
// function to determine its minimum required length. The actual result may
// be smaller, so you should call result.Normalize() on the result.
//
// The operations are divided into two groups: "fast" (O(n) with small
// coefficient) operations are exposed directly as free functions, "slow"
// operations are methods on a {Processor} object, which provides
// support for interrupting execution via the {Platform}'s {InterruptRequested}
// mechanism when it takes too long. These functions return a {Status} value.

// Returns r such that r < 0 if A < B; r > 0 if A > B; r == 0 if A == B.
// Defined here to be inlineable, which helps ia32 a lot (64-bit platforms
// don't care).
inline int Compare(Digits A, Digits B) {
  A.Normalize();
  B.Normalize();
  int diff = A.len() - B.len();
  if (diff != 0) return diff;
  int i = A.len() - 1;
  while (i >= 0 && A[i] == B[i]) i--;
  if (i < 0) return 0;
  return A[i] > B[i] ? 1 : -1;
}

// Z := X + Y
void Add(RWDigits Z, Digits X, Digits Y);
// Addition of signed integers. Returns true if the result is negative.
bool AddSigned(RWDigits Z, Digits X, bool x_negative, Digits Y,
               bool y_negative);
// Z := X + 1
void AddOne(RWDigits Z, Digits X);

// Z := X - Y. Requires X >= Y.
void Subtract(RWDigits Z, Digits X, Digits Y);
// Subtraction of signed integers. Returns true if the result is negative.
bool SubtractSigned(RWDigits Z, Digits X, bool x_negative, Digits Y,
                    bool y_negative);
// Z := X - 1
void SubtractOne(RWDigits Z, Digits X);

// The bitwise operations assume that negative BigInts are represented as
// sign+magnitude. Their behavior depends on the sign of the inputs: negative
// inputs perform an implicit conversion to two's complement representation.
// Z := X & Y
void BitwiseAnd_PosPos(RWDigits Z, Digits X, Digits Y);
// Call this for a BigInt x = (magnitude=X, negative=true).
void BitwiseAnd_NegNeg(RWDigits Z, Digits X, Digits Y);
// Positive X, negative Y. Callers must swap arguments as needed.
void BitwiseAnd_PosNeg(RWDigits Z, Digits X, Digits Y);
void BitwiseOr_PosPos(RWDigits Z, Digits X, Digits Y);
void BitwiseOr_NegNeg(RWDigits Z, Digits X, Digits Y);
void BitwiseOr_PosNeg(RWDigits Z, Digits X, Digits Y);
void BitwiseXor_PosPos(RWDigits Z, Digits X, Digits Y);
void BitwiseXor_NegNeg(RWDigits Z, Digits X, Digits Y);
void BitwiseXor_PosNeg(RWDigits Z, Digits X, Digits Y);
void LeftShift(RWDigits Z, Digits X, digit_t shift);
// RightShiftState is provided by RightShift_ResultLength and used by the actual
// RightShift to avoid some recomputation.
struct RightShiftState {
  bool must_round_down = false;
};
void RightShift(RWDigits Z, Digits X, digit_t shift,
                const RightShiftState& state);

// Z := (least significant n bits of X, interpreted as a signed n-bit integer).
// Returns true if the result is negative; Z will hold the absolute value.
bool AsIntN(RWDigits Z, Digits X, bool x_negative, int n);
// Z := (least significant n bits of X).
void AsUintN_Pos(RWDigits Z, Digits X, int n);
// Same, but X is the absolute value of a negative BigInt.
void AsUintN_Neg(RWDigits Z, Digits X, int n);

enum class Status { kOk, kInterrupted };

class FromStringAccumulator;

class Processor {
 public:
  // Takes ownership of {platform}.
  static Processor* New(Platform* platform);

  // Use this for any std::unique_ptr holding an instance of {Processor}.
  class Destroyer {
   public:
    void operator()(Processor* proc) { proc->Destroy(); }
  };
  // When not using std::unique_ptr, call this to delete the instance.
  void Destroy();

  // Z := X * Y
  Status Multiply(RWDigits Z, Digits X, Digits Y);
  // Q := A / B
  Status Divide(RWDigits Q, Digits A, Digits B);
  // R := A % B
  Status Modulo(RWDigits R, Digits A, Digits B);

  // {out_length} initially contains the allocated capacity of {out}, and
  // upon return will be set to the actual length of the result string.
  Status ToString(char* out, int* out_length, Digits X, int radix, bool sign);

  // Z := the contents of {accumulator}.
  // Assume that this leaves {accumulator} in unusable state.
  Status FromString(RWDigits Z, FromStringAccumulator* accumulator);

 protected:
  // Use {Destroy} or {Destroyer} instead of the destructor directly.
  ~Processor() = default;
};

inline int AddResultLength(int x_length, int y_length) {
  return std::max(x_length, y_length) + 1;
}
inline int AddSignedResultLength(int x_length, int y_length, bool same_sign) {
  return same_sign ? AddResultLength(x_length, y_length)
                   : std::max(x_length, y_length);
}
inline int SubtractResultLength(int x_length, int y_length) { return x_length; }
inline int SubtractSignedResultLength(int x_length, int y_length,
                                      bool same_sign) {
  return same_sign ? std::max(x_length, y_length)
                   : AddResultLength(x_length, y_length);
}
inline int MultiplyResultLength(Digits X, Digits Y) {
  return X.len() + Y.len();
}
constexpr int kBarrettThreshold = 13310;
inline int DivideResultLength(Digits A, Digits B) {
#if V8_ADVANCED_BIGINT_ALGORITHMS
  BIGINT_H_DCHECK(kAdvancedAlgorithmsEnabledInLibrary);
  // The Barrett division algorithm needs one extra digit for temporary use.
  int kBarrettExtraScratch = B.len() >= kBarrettThreshold ? 1 : 0;
#else
  // If this fails, set -DV8_ADVANCED_BIGINT_ALGORITHMS in any compilation unit
  // that #includes this header.
  BIGINT_H_DCHECK(!kAdvancedAlgorithmsEnabledInLibrary);
  constexpr int kBarrettExtraScratch = 0;
#endif
  return A.len() - B.len() + 1 + kBarrettExtraScratch;
}
inline int ModuloResultLength(Digits B) { return B.len(); }

int ToStringResultLength(Digits X, int radix, bool sign);
// In DEBUG builds, the result of {ToString} will be initialized to this value.
constexpr char kStringZapValue = '?';

int RightShift_ResultLength(Digits X, bool x_sign, digit_t shift,
                            RightShiftState* state);

// Returns -1 if this "asIntN" operation would be a no-op.
int AsIntNResultLength(Digits X, bool x_negative, int n);
// Returns -1 if this "asUintN" operation would be a no-op.
int AsUintN_Pos_ResultLength(Digits X, int n);
inline int AsUintN_Neg_ResultLength(int n) {
  return ((n - 1) / kDigitBits) + 1;
}

// Support for parsing BigInts from Strings, using an Accumulator object
// for intermediate state.

class ProcessorImpl;

#if !defined(DEBUG) && (defined(__GNUC__) || defined(__clang__))
// Clang supports this since 3.9, GCC since 4.x.
#define ALWAYS_INLINE inline __attribute__((always_inline))
#elif !defined(DEBUG) && defined(_MSC_VER)
#define ALWAYS_INLINE __forceinline
#else
#define ALWAYS_INLINE inline
#endif

static constexpr int kStackParts = 8;

// A container object for all metadata required for parsing a BigInt from
// a string.
// Aggressively optimized not to waste instructions for small cases, while
// also scaling transparently to huge cases.
// Defined here in the header so that it can be inlined.
class FromStringAccumulator {
 public:
  enum class Result { kOk, kMaxSizeExceeded };

  // Step 1: Create a FromStringAccumulator instance. For best performance,
  // stack allocation is recommended.
  // {max_digits} is only used for refusing to grow beyond a given size
  // (see "Step 2" below). It does not cause pre-allocation, so feel free to
  // specify a large maximum.
  // TODO(jkummerow): The limit applies to the number of intermediate chunks,
  // whereas the final result will be slightly smaller (depending on {radix}).
  // So for sufficiently large N, setting max_digits=N here will not actually
  // allow parsing BigInts with N digits. We can fix that if/when anyone cares.
  explicit FromStringAccumulator(int max_digits)
      : max_digits_(std::max(max_digits, kStackParts)) {}

  // Step 2: Call this method to read all characters.
  // {CharIt} should be a forward iterator and
  // std::iterator_traits<CharIt>::value_type shall be a character type, such as
  // uint8_t or uint16_t. {end} should be one past the last character (i.e.
  // {start == end} would indicate an empty string). Returns the current
  // position when an invalid character is encountered.
  template <class CharIt>
  ALWAYS_INLINE CharIt Parse(CharIt start, CharIt end, digit_t radix);

  // Step 3: Check if a result is available, and determine its required
  // allocation size (guaranteed to be <= max_digits passed to the constructor).
  Result result() { return result_; }
  int ResultLength() {
    return std::max(stack_parts_used_, static_cast<int>(heap_parts_.size()));
  }

  // Step 4: Use BigIntProcessor::FromString() to retrieve the result into an
  // {RWDigits} struct allocated for the size returned by step 3.

 private:
  friend class ProcessorImpl;

  template <class CharIt>
  ALWAYS_INLINE CharIt ParsePowerTwo(CharIt start, CharIt end, digit_t radix);

  ALWAYS_INLINE bool AddPart(digit_t multiplier, digit_t part, bool is_last);
  ALWAYS_INLINE bool AddPart(digit_t part);

  digit_t stack_parts_[kStackParts];
  std::vector<digit_t> heap_parts_;
  digit_t max_multiplier_{0};
  digit_t last_multiplier_;
  const int max_digits_;
  Result result_{Result::kOk};
  int stack_parts_used_{0};
  bool inline_everything_{false};
  uint8_t radix_{0};
};

// The rest of this file is the inlineable implementation of
// FromStringAccumulator methods.

#if defined(__GNUC__) || defined(__clang__)
// Clang supports this since 3.9, GCC since 5.x.
#define HAVE_BUILTIN_MUL_OVERFLOW 1
#else
#define HAVE_BUILTIN_MUL_OVERFLOW 0
#endif

// Numerical value of the first 127 ASCII characters, using 255 as sentinel
// for "invalid".
static constexpr uint8_t kCharValue[] = {
    255, 255, 255, 255, 255, 255, 255, 255,  // 0..7
    255, 255, 255, 255, 255, 255, 255, 255,  // 8..15
    255, 255, 255, 255, 255, 255, 255, 255,  // 16..23
    255, 255, 255, 255, 255, 255, 255, 255,  // 24..31
    255, 255, 255, 255, 255, 255, 255, 255,  // 32..39
    255, 255, 255, 255, 255, 255, 255, 255,  // 40..47
    0,   1,   2,   3,   4,   5,   6,   7,    // 48..55    '0' == 48
    8,   9,   255, 255, 255, 255, 255, 255,  // 56..63    '9' == 57
    255, 10,  11,  12,  13,  14,  15,  16,   // 64..71    'A' == 65
    17,  18,  19,  20,  21,  22,  23,  24,   // 72..79
    25,  26,  27,  28,  29,  30,  31,  32,   // 80..87
    33,  34,  35,  255, 255, 255, 255, 255,  // 88..95    'Z' == 90
    255, 10,  11,  12,  13,  14,  15,  16,   // 96..103   'a' == 97
    17,  18,  19,  20,  21,  22,  23,  24,   // 104..111
    25,  26,  27,  28,  29,  30,  31,  32,   // 112..119
    33,  34,  35,  255, 255, 255, 255, 255,  // 120..127  'z' == 122
};

// A space- and time-efficient way to map {2,4,8,16,32} to {1,2,3,4,5}.
static constexpr uint8_t kCharBits[] = {1, 2, 3, 0, 4, 0, 0, 0, 5};

template <class CharIt>
CharIt FromStringAccumulator::ParsePowerTwo(CharIt current, CharIt end,
                                            digit_t radix) {
  radix_ = static_cast<uint8_t>(radix);
  const int char_bits = kCharBits[radix >> 2];
  int bits_left;
  bool done = false;
  do {
    digit_t part = 0;
    bits_left = kDigitBits;
    while (true) {
      digit_t d;  // Numeric value of the current character {c}.
      uint32_t c = *current;
      if (c > 127 || (d = bigint::kCharValue[c]) >= radix) {
        done = true;
        break;
      }

      if (bits_left < char_bits) break;
      bits_left -= char_bits;
      part = (part << char_bits) | d;

      ++current;
      if (current == end) {
        done = true;
        break;
      }
    }
    if (!AddPart(part)) return current;
  } while (!done);
  // We use the unused {last_multiplier_} field to
  // communicate how many bits are unused in the last part.
  last_multiplier_ = bits_left;
  return current;
}

template <class CharIt>
CharIt FromStringAccumulator::Parse(CharIt start, CharIt end, digit_t radix) {
  BIGINT_H_DCHECK(2 <= radix && radix <= 36);
  CharIt current = start;
#if !HAVE_BUILTIN_MUL_OVERFLOW
  const digit_t kMaxMultiplier = (~digit_t{0}) / radix;
#endif
#if HAVE_TWODIGIT_T  // The inlined path requires twodigit_t availability.
  // The max supported radix is 36, and Math.log2(36) == 5.169..., so we
  // need at most 5.17 bits per char.
  static constexpr int kInlineThreshold = kStackParts * kDigitBits * 100 / 517;
  inline_everything_ = (end - start) <= kInlineThreshold;
#endif
  if (!inline_everything_ && (radix & (radix - 1)) == 0) {
    return ParsePowerTwo(start, end, radix);
  }
  bool done = false;
  do {
    digit_t multiplier = 1;
    digit_t part = 0;
    while (true) {
      digit_t d;  // Numeric value of the current character {c}.
      uint32_t c = *current;
      if (c > 127 || (d = bigint::kCharValue[c]) >= radix) {
        done = true;
        break;
      }

#if HAVE_BUILTIN_MUL_OVERFLOW
      digit_t new_multiplier;
      if (__builtin_mul_overflow(multiplier, radix, &new_multiplier)) break;
      multiplier = new_multiplier;
#else
      if (multiplier > kMaxMultiplier) break;
      multiplier *= radix;
#endif
      part = part * radix + d;

      ++current;
      if (current == end) {
        done = true;
        break;
      }
    }
    if (!AddPart(multiplier, part, done)) return current;
  } while (!done);
  return current;
}

bool FromStringAccumulator::AddPart(digit_t multiplier, digit_t part,
                                    bool is_last) {
#if HAVE_TWODIGIT_T
  if (inline_everything_) {
    // Inlined version of {MultiplySingle}.
    digit_t carry = part;
    digit_t high = 0;
    for (int i = 0; i < stack_parts_used_; i++) {
      twodigit_t result = twodigit_t{stack_parts_[i]} * multiplier;
      digit_t new_high = result >> bigint::kDigitBits;
      digit_t low = static_cast<digit_t>(result);
      result = twodigit_t{low} + high + carry;
      carry = result >> bigint::kDigitBits;
      stack_parts_[i] = static_cast<digit_t>(result);
      high = new_high;
    }
    stack_parts_[stack_parts_used_++] = carry + high;
    return true;
  }
#else
  BIGINT_H_DCHECK(!inline_everything_);
#endif
  if (is_last) {
    last_multiplier_ = multiplier;
  } else {
    BIGINT_H_DCHECK(max_multiplier_ == 0 || max_multiplier_ == multiplier);
    max_multiplier_ = multiplier;
  }
  return AddPart(part);
}

bool FromStringAccumulator::AddPart(digit_t part) {
  if (stack_parts_used_ < kStackParts) {
    stack_parts_[stack_parts_used_++] = part;
    return true;
  }
  if (heap_parts_.size() == 0) {
    // Initialize heap storage. Copy the stack part to make things easier later.
    heap_parts_.reserve(kStackParts * 2);
    for (int i = 0; i < kStackParts; i++) {
      heap_parts_.push_back(stack_parts_[i]);
    }
  }
  if (static_cast<int>(heap_parts_.size()) >= max_digits_) {
    result_ = Result::kMaxSizeExceeded;
    return false;
  }
  heap_parts_.push_back(part);
  return true;
}

}  // namespace bigint
}  // namespace v8

#undef BIGINT_H_DCHECK
#undef ALWAYS_INLINE
#undef HAVE_BUILTIN_MUL_OVERFLOW

#endif  // V8_BIGINT_BIGINT_H_
