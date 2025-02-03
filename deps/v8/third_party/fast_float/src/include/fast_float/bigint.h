#ifndef FASTFLOAT_BIGINT_H
#define FASTFLOAT_BIGINT_H

#include <algorithm>
#include <cstdint>
#include <climits>
#include <cstring>

#include "float_common.h"

namespace fast_float {

// the limb width: we want efficient multiplication of double the bits in
// limb, or for 64-bit limbs, at least 64-bit multiplication where we can
// extract the high and low parts efficiently. this is every 64-bit
// architecture except for sparc, which emulates 128-bit multiplication.
// we might have platforms where `CHAR_BIT` is not 8, so let's avoid
// doing `8 * sizeof(limb)`.
#if defined(FASTFLOAT_64BIT) && !defined(__sparc)
#define FASTFLOAT_64BIT_LIMB 1
typedef uint64_t limb;
constexpr size_t limb_bits = 64;
#else
#define FASTFLOAT_32BIT_LIMB
typedef uint32_t limb;
constexpr size_t limb_bits = 32;
#endif

typedef span<limb> limb_span;

// number of bits in a bigint. this needs to be at least the number
// of bits required to store the largest bigint, which is
// `log2(10**(digits + max_exp))`, or `log2(10**(767 + 342))`, or
// ~3600 bits, so we round to 4000.
constexpr size_t bigint_bits = 4000;
constexpr size_t bigint_limbs = bigint_bits / limb_bits;

// vector-like type that is allocated on the stack. the entire
// buffer is pre-allocated, and only the length changes.
template <uint16_t size> struct stackvec {
  limb data[size];
  // we never need more than 150 limbs
  uint16_t length{0};

  stackvec() = default;
  stackvec(const stackvec &) = delete;
  stackvec &operator=(const stackvec &) = delete;
  stackvec(stackvec &&) = delete;
  stackvec &operator=(stackvec &&other) = delete;

  // create stack vector from existing limb span.
  FASTFLOAT_CONSTEXPR20 stackvec(limb_span s) {
    FASTFLOAT_ASSERT(try_extend(s));
  }

  FASTFLOAT_CONSTEXPR14 limb &operator[](size_t index) noexcept {
    FASTFLOAT_DEBUG_ASSERT(index < length);
    return data[index];
  }
  FASTFLOAT_CONSTEXPR14 const limb &operator[](size_t index) const noexcept {
    FASTFLOAT_DEBUG_ASSERT(index < length);
    return data[index];
  }
  // index from the end of the container
  FASTFLOAT_CONSTEXPR14 const limb &rindex(size_t index) const noexcept {
    FASTFLOAT_DEBUG_ASSERT(index < length);
    size_t rindex = length - index - 1;
    return data[rindex];
  }

  // set the length, without bounds checking.
  FASTFLOAT_CONSTEXPR14 void set_len(size_t len) noexcept {
    length = uint16_t(len);
  }
  constexpr size_t len() const noexcept { return length; }
  constexpr bool is_empty() const noexcept { return length == 0; }
  constexpr size_t capacity() const noexcept { return size; }
  // append item to vector, without bounds checking
  FASTFLOAT_CONSTEXPR14 void push_unchecked(limb value) noexcept {
    data[length] = value;
    length++;
  }
  // append item to vector, returning if item was added
  FASTFLOAT_CONSTEXPR14 bool try_push(limb value) noexcept {
    if (len() < capacity()) {
      push_unchecked(value);
      return true;
    } else {
      return false;
    }
  }
  // add items to the vector, from a span, without bounds checking
  FASTFLOAT_CONSTEXPR20 void extend_unchecked(limb_span s) noexcept {
    limb *ptr = data + length;
    std::copy_n(s.ptr, s.len(), ptr);
    set_len(len() + s.len());
  }
  // try to add items to the vector, returning if items were added
  FASTFLOAT_CONSTEXPR20 bool try_extend(limb_span s) noexcept {
    if (len() + s.len() <= capacity()) {
      extend_unchecked(s);
      return true;
    } else {
      return false;
    }
  }
  // resize the vector, without bounds checking
  // if the new size is longer than the vector, assign value to each
  // appended item.
  FASTFLOAT_CONSTEXPR20
  void resize_unchecked(size_t new_len, limb value) noexcept {
    if (new_len > len()) {
      size_t count = new_len - len();
      limb *first = data + len();
      limb *last = first + count;
      ::std::fill(first, last, value);
      set_len(new_len);
    } else {
      set_len(new_len);
    }
  }
  // try to resize the vector, returning if the vector was resized.
  FASTFLOAT_CONSTEXPR20 bool try_resize(size_t new_len, limb value) noexcept {
    if (new_len > capacity()) {
      return false;
    } else {
      resize_unchecked(new_len, value);
      return true;
    }
  }
  // check if any limbs are non-zero after the given index.
  // this needs to be done in reverse order, since the index
  // is relative to the most significant limbs.
  FASTFLOAT_CONSTEXPR14 bool nonzero(size_t index) const noexcept {
    while (index < len()) {
      if (rindex(index) != 0) {
        return true;
      }
      index++;
    }
    return false;
  }
  // normalize the big integer, so most-significant zero limbs are removed.
  FASTFLOAT_CONSTEXPR14 void normalize() noexcept {
    while (len() > 0 && rindex(0) == 0) {
      length--;
    }
  }
};

fastfloat_really_inline FASTFLOAT_CONSTEXPR14 uint64_t
empty_hi64(bool &truncated) noexcept {
  truncated = false;
  return 0;
}

fastfloat_really_inline FASTFLOAT_CONSTEXPR20 uint64_t
uint64_hi64(uint64_t r0, bool &truncated) noexcept {
  truncated = false;
  int shl = leading_zeroes(r0);
  return r0 << shl;
}

fastfloat_really_inline FASTFLOAT_CONSTEXPR20 uint64_t
uint64_hi64(uint64_t r0, uint64_t r1, bool &truncated) noexcept {
  int shl = leading_zeroes(r0);
  if (shl == 0) {
    truncated = r1 != 0;
    return r0;
  } else {
    int shr = 64 - shl;
    truncated = (r1 << shl) != 0;
    return (r0 << shl) | (r1 >> shr);
  }
}

fastfloat_really_inline FASTFLOAT_CONSTEXPR20 uint64_t
uint32_hi64(uint32_t r0, bool &truncated) noexcept {
  return uint64_hi64(r0, truncated);
}

fastfloat_really_inline FASTFLOAT_CONSTEXPR20 uint64_t
uint32_hi64(uint32_t r0, uint32_t r1, bool &truncated) noexcept {
  uint64_t x0 = r0;
  uint64_t x1 = r1;
  return uint64_hi64((x0 << 32) | x1, truncated);
}

fastfloat_really_inline FASTFLOAT_CONSTEXPR20 uint64_t
uint32_hi64(uint32_t r0, uint32_t r1, uint32_t r2, bool &truncated) noexcept {
  uint64_t x0 = r0;
  uint64_t x1 = r1;
  uint64_t x2 = r2;
  return uint64_hi64(x0, (x1 << 32) | x2, truncated);
}

// add two small integers, checking for overflow.
// we want an efficient operation. for msvc, where
// we don't have built-in intrinsics, this is still
// pretty fast.
fastfloat_really_inline FASTFLOAT_CONSTEXPR20 limb
scalar_add(limb x, limb y, bool &overflow) noexcept {
  limb z;
// gcc and clang
#if defined(__has_builtin)
#if __has_builtin(__builtin_add_overflow)
  if (!cpp20_and_in_constexpr()) {
    overflow = __builtin_add_overflow(x, y, &z);
    return z;
  }
#endif
#endif

  // generic, this still optimizes correctly on MSVC.
  z = x + y;
  overflow = z < x;
  return z;
}

// multiply two small integers, getting both the high and low bits.
fastfloat_really_inline FASTFLOAT_CONSTEXPR20 limb
scalar_mul(limb x, limb y, limb &carry) noexcept {
#ifdef FASTFLOAT_64BIT_LIMB
#if defined(__SIZEOF_INT128__)
  // GCC and clang both define it as an extension.
  __uint128_t z = __uint128_t(x) * __uint128_t(y) + __uint128_t(carry);
  carry = limb(z >> limb_bits);
  return limb(z);
#else
  // fallback, no native 128-bit integer multiplication with carry.
  // on msvc, this optimizes identically, somehow.
  value128 z = full_multiplication(x, y);
  bool overflow;
  z.low = scalar_add(z.low, carry, overflow);
  z.high += uint64_t(overflow); // cannot overflow
  carry = z.high;
  return z.low;
#endif
#else
  uint64_t z = uint64_t(x) * uint64_t(y) + uint64_t(carry);
  carry = limb(z >> limb_bits);
  return limb(z);
#endif
}

// add scalar value to bigint starting from offset.
// used in grade school multiplication
template <uint16_t size>
inline FASTFLOAT_CONSTEXPR20 bool small_add_from(stackvec<size> &vec, limb y,
                                                 size_t start) noexcept {
  size_t index = start;
  limb carry = y;
  bool overflow;
  while (carry != 0 && index < vec.len()) {
    vec[index] = scalar_add(vec[index], carry, overflow);
    carry = limb(overflow);
    index += 1;
  }
  if (carry != 0) {
    FASTFLOAT_TRY(vec.try_push(carry));
  }
  return true;
}

// add scalar value to bigint.
template <uint16_t size>
fastfloat_really_inline FASTFLOAT_CONSTEXPR20 bool
small_add(stackvec<size> &vec, limb y) noexcept {
  return small_add_from(vec, y, 0);
}

// multiply bigint by scalar value.
template <uint16_t size>
inline FASTFLOAT_CONSTEXPR20 bool small_mul(stackvec<size> &vec,
                                            limb y) noexcept {
  limb carry = 0;
  for (size_t index = 0; index < vec.len(); index++) {
    vec[index] = scalar_mul(vec[index], y, carry);
  }
  if (carry != 0) {
    FASTFLOAT_TRY(vec.try_push(carry));
  }
  return true;
}

// add bigint to bigint starting from index.
// used in grade school multiplication
template <uint16_t size>
FASTFLOAT_CONSTEXPR20 bool large_add_from(stackvec<size> &x, limb_span y,
                                          size_t start) noexcept {
  // the effective x buffer is from `xstart..x.len()`, so exit early
  // if we can't get that current range.
  if (x.len() < start || y.len() > x.len() - start) {
    FASTFLOAT_TRY(x.try_resize(y.len() + start, 0));
  }

  bool carry = false;
  for (size_t index = 0; index < y.len(); index++) {
    limb xi = x[index + start];
    limb yi = y[index];
    bool c1 = false;
    bool c2 = false;
    xi = scalar_add(xi, yi, c1);
    if (carry) {
      xi = scalar_add(xi, 1, c2);
    }
    x[index + start] = xi;
    carry = c1 | c2;
  }

  // handle overflow
  if (carry) {
    FASTFLOAT_TRY(small_add_from(x, 1, y.len() + start));
  }
  return true;
}

// add bigint to bigint.
template <uint16_t size>
fastfloat_really_inline FASTFLOAT_CONSTEXPR20 bool
large_add_from(stackvec<size> &x, limb_span y) noexcept {
  return large_add_from(x, y, 0);
}

// grade-school multiplication algorithm
template <uint16_t size>
FASTFLOAT_CONSTEXPR20 bool long_mul(stackvec<size> &x, limb_span y) noexcept {
  limb_span xs = limb_span(x.data, x.len());
  stackvec<size> z(xs);
  limb_span zs = limb_span(z.data, z.len());

  if (y.len() != 0) {
    limb y0 = y[0];
    FASTFLOAT_TRY(small_mul(x, y0));
    for (size_t index = 1; index < y.len(); index++) {
      limb yi = y[index];
      stackvec<size> zi;
      if (yi != 0) {
        // re-use the same buffer throughout
        zi.set_len(0);
        FASTFLOAT_TRY(zi.try_extend(zs));
        FASTFLOAT_TRY(small_mul(zi, yi));
        limb_span zis = limb_span(zi.data, zi.len());
        FASTFLOAT_TRY(large_add_from(x, zis, index));
      }
    }
  }

  x.normalize();
  return true;
}

// grade-school multiplication algorithm
template <uint16_t size>
FASTFLOAT_CONSTEXPR20 bool large_mul(stackvec<size> &x, limb_span y) noexcept {
  if (y.len() == 1) {
    FASTFLOAT_TRY(small_mul(x, y[0]));
  } else {
    FASTFLOAT_TRY(long_mul(x, y));
  }
  return true;
}

template <typename = void> struct pow5_tables {
  static constexpr uint32_t large_step = 135;
  static constexpr uint64_t small_power_of_5[] = {
      1UL,
      5UL,
      25UL,
      125UL,
      625UL,
      3125UL,
      15625UL,
      78125UL,
      390625UL,
      1953125UL,
      9765625UL,
      48828125UL,
      244140625UL,
      1220703125UL,
      6103515625UL,
      30517578125UL,
      152587890625UL,
      762939453125UL,
      3814697265625UL,
      19073486328125UL,
      95367431640625UL,
      476837158203125UL,
      2384185791015625UL,
      11920928955078125UL,
      59604644775390625UL,
      298023223876953125UL,
      1490116119384765625UL,
      7450580596923828125UL,
  };
#ifdef FASTFLOAT_64BIT_LIMB
  constexpr static limb large_power_of_5[] = {
      1414648277510068013UL, 9180637584431281687UL, 4539964771860779200UL,
      10482974169319127550UL, 198276706040285095UL};
#else
  constexpr static limb large_power_of_5[] = {
      4279965485U, 329373468U,  4020270615U, 2137533757U, 4287402176U,
      1057042919U, 1071430142U, 2440757623U, 381945767U,  46164893U};
#endif
};

template <typename T> constexpr uint32_t pow5_tables<T>::large_step;

template <typename T> constexpr uint64_t pow5_tables<T>::small_power_of_5[];

template <typename T> constexpr limb pow5_tables<T>::large_power_of_5[];

// big integer type. implements a small subset of big integer
// arithmetic, using simple algorithms since asymptotically
// faster algorithms are slower for a small number of limbs.
// all operations assume the big-integer is normalized.
struct bigint : pow5_tables<> {
  // storage of the limbs, in little-endian order.
  stackvec<bigint_limbs> vec;

  FASTFLOAT_CONSTEXPR20 bigint() : vec() {}
  bigint(const bigint &) = delete;
  bigint &operator=(const bigint &) = delete;
  bigint(bigint &&) = delete;
  bigint &operator=(bigint &&other) = delete;

  FASTFLOAT_CONSTEXPR20 bigint(uint64_t value) : vec() {
#ifdef FASTFLOAT_64BIT_LIMB
    vec.push_unchecked(value);
#else
    vec.push_unchecked(uint32_t(value));
    vec.push_unchecked(uint32_t(value >> 32));
#endif
    vec.normalize();
  }

  // get the high 64 bits from the vector, and if bits were truncated.
  // this is to get the significant digits for the float.
  FASTFLOAT_CONSTEXPR20 uint64_t hi64(bool &truncated) const noexcept {
#ifdef FASTFLOAT_64BIT_LIMB
    if (vec.len() == 0) {
      return empty_hi64(truncated);
    } else if (vec.len() == 1) {
      return uint64_hi64(vec.rindex(0), truncated);
    } else {
      uint64_t result = uint64_hi64(vec.rindex(0), vec.rindex(1), truncated);
      truncated |= vec.nonzero(2);
      return result;
    }
#else
    if (vec.len() == 0) {
      return empty_hi64(truncated);
    } else if (vec.len() == 1) {
      return uint32_hi64(vec.rindex(0), truncated);
    } else if (vec.len() == 2) {
      return uint32_hi64(vec.rindex(0), vec.rindex(1), truncated);
    } else {
      uint64_t result =
          uint32_hi64(vec.rindex(0), vec.rindex(1), vec.rindex(2), truncated);
      truncated |= vec.nonzero(3);
      return result;
    }
#endif
  }

  // compare two big integers, returning the large value.
  // assumes both are normalized. if the return value is
  // negative, other is larger, if the return value is
  // positive, this is larger, otherwise they are equal.
  // the limbs are stored in little-endian order, so we
  // must compare the limbs in ever order.
  FASTFLOAT_CONSTEXPR20 int compare(const bigint &other) const noexcept {
    if (vec.len() > other.vec.len()) {
      return 1;
    } else if (vec.len() < other.vec.len()) {
      return -1;
    } else {
      for (size_t index = vec.len(); index > 0; index--) {
        limb xi = vec[index - 1];
        limb yi = other.vec[index - 1];
        if (xi > yi) {
          return 1;
        } else if (xi < yi) {
          return -1;
        }
      }
      return 0;
    }
  }

  // shift left each limb n bits, carrying over to the new limb
  // returns true if we were able to shift all the digits.
  FASTFLOAT_CONSTEXPR20 bool shl_bits(size_t n) noexcept {
    // Internally, for each item, we shift left by n, and add the previous
    // right shifted limb-bits.
    // For example, we transform (for u8) shifted left 2, to:
    //      b10100100 b01000010
    //      b10 b10010001 b00001000
    FASTFLOAT_DEBUG_ASSERT(n != 0);
    FASTFLOAT_DEBUG_ASSERT(n < sizeof(limb) * 8);

    size_t shl = n;
    size_t shr = limb_bits - shl;
    limb prev = 0;
    for (size_t index = 0; index < vec.len(); index++) {
      limb xi = vec[index];
      vec[index] = (xi << shl) | (prev >> shr);
      prev = xi;
    }

    limb carry = prev >> shr;
    if (carry != 0) {
      return vec.try_push(carry);
    }
    return true;
  }

  // move the limbs left by `n` limbs.
  FASTFLOAT_CONSTEXPR20 bool shl_limbs(size_t n) noexcept {
    FASTFLOAT_DEBUG_ASSERT(n != 0);
    if (n + vec.len() > vec.capacity()) {
      return false;
    } else if (!vec.is_empty()) {
      // move limbs
      limb *dst = vec.data + n;
      const limb *src = vec.data;
      std::copy_backward(src, src + vec.len(), dst + vec.len());
      // fill in empty limbs
      limb *first = vec.data;
      limb *last = first + n;
      ::std::fill(first, last, 0);
      vec.set_len(n + vec.len());
      return true;
    } else {
      return true;
    }
  }

  // move the limbs left by `n` bits.
  FASTFLOAT_CONSTEXPR20 bool shl(size_t n) noexcept {
    size_t rem = n % limb_bits;
    size_t div = n / limb_bits;
    if (rem != 0) {
      FASTFLOAT_TRY(shl_bits(rem));
    }
    if (div != 0) {
      FASTFLOAT_TRY(shl_limbs(div));
    }
    return true;
  }

  // get the number of leading zeros in the bigint.
  FASTFLOAT_CONSTEXPR20 int ctlz() const noexcept {
    if (vec.is_empty()) {
      return 0;
    } else {
#ifdef FASTFLOAT_64BIT_LIMB
      return leading_zeroes(vec.rindex(0));
#else
      // no use defining a specialized leading_zeroes for a 32-bit type.
      uint64_t r0 = vec.rindex(0);
      return leading_zeroes(r0 << 32);
#endif
    }
  }

  // get the number of bits in the bigint.
  FASTFLOAT_CONSTEXPR20 int bit_length() const noexcept {
    int lz = ctlz();
    return int(limb_bits * vec.len()) - lz;
  }

  FASTFLOAT_CONSTEXPR20 bool mul(limb y) noexcept { return small_mul(vec, y); }

  FASTFLOAT_CONSTEXPR20 bool add(limb y) noexcept { return small_add(vec, y); }

  // multiply as if by 2 raised to a power.
  FASTFLOAT_CONSTEXPR20 bool pow2(uint32_t exp) noexcept { return shl(exp); }

  // multiply as if by 5 raised to a power.
  FASTFLOAT_CONSTEXPR20 bool pow5(uint32_t exp) noexcept {
    // multiply by a power of 5
    size_t large_length = sizeof(large_power_of_5) / sizeof(limb);
    limb_span large = limb_span(large_power_of_5, large_length);
    while (exp >= large_step) {
      FASTFLOAT_TRY(large_mul(vec, large));
      exp -= large_step;
    }
#ifdef FASTFLOAT_64BIT_LIMB
    uint32_t small_step = 27;
    limb max_native = 7450580596923828125UL;
#else
    uint32_t small_step = 13;
    limb max_native = 1220703125U;
#endif
    while (exp >= small_step) {
      FASTFLOAT_TRY(small_mul(vec, max_native));
      exp -= small_step;
    }
    if (exp != 0) {
      // Work around clang bug https://godbolt.org/z/zedh7rrhc
      // This is similar to https://github.com/llvm/llvm-project/issues/47746,
      // except the workaround described there don't work here
      FASTFLOAT_TRY(small_mul(
          vec, limb(((void)small_power_of_5[0], small_power_of_5[exp]))));
    }

    return true;
  }

  // multiply as if by 10 raised to a power.
  FASTFLOAT_CONSTEXPR20 bool pow10(uint32_t exp) noexcept {
    FASTFLOAT_TRY(pow5(exp));
    return pow2(exp);
  }
};

} // namespace fast_float

#endif
