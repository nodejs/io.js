// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/division-by-constant.h"

#include <stdint.h>

#include <type_traits>

#include "src/base/logging.h"
#include "src/base/macros.h"

namespace v8 {
namespace base {

template <typename T>
  requires std::unsigned_integral<T>
MagicNumbersForDivision<T> SignedDivisionByConstant(T d) {
  DCHECK(d != static_cast<T>(-1) && d != 0 && d != 1);
  const unsigned bits = static_cast<unsigned>(sizeof(T)) * 8;
  const T min = (static_cast<T>(1) << (bits - 1));
  const bool neg = (min & d) != 0;
  const T ad = neg ? (0 - d) : d;
  const T t = min + (d >> (bits - 1));
  const T anc = t - 1 - t % ad;  // Absolute value of nc
  unsigned p = bits - 1;         // Init. p.
  T q1 = min / anc;              // Init. q1 = 2**p/|nc|.
  T r1 = min - q1 * anc;         // Init. r1 = rem(2**p, |nc|).
  T q2 = min / ad;               // Init. q2 = 2**p/|d|.
  T r2 = min - q2 * ad;          // Init. r2 = rem(2**p, |d|).
  T delta;
  do {
    p = p + 1;
    q1 = 2 * q1;      // Update q1 = 2**p/|nc|.
    r1 = 2 * r1;      // Update r1 = rem(2**p, |nc|).
    if (r1 >= anc) {  // Must be an unsigned comparison here.
      q1 = q1 + 1;
      r1 = r1 - anc;
    }
    q2 = 2 * q2;     // Update q2 = 2**p/|d|.
    r2 = 2 * r2;     // Update r2 = rem(2**p, |d|).
    if (r2 >= ad) {  // Must be an unsigned comparison here.
      q2 = q2 + 1;
      r2 = r2 - ad;
    }
    delta = ad - r2;
  } while (q1 < delta || (q1 == delta && r1 == 0));
  T mul = q2 + 1;
  return MagicNumbersForDivision<T>(neg ? (0 - mul) : mul, p - bits, false);
}

template <class T>
MagicNumbersForDivision<T> UnsignedDivisionByConstant(T d,
                                                      unsigned leading_zeros) {
  static_assert(std::is_unsigned_v<T>);
  DCHECK_NE(d, 0);
  const unsigned bits = static_cast<unsigned>(sizeof(T)) * 8;
  const T ones = ~static_cast<T>(0) >> leading_zeros;
  const T min = static_cast<T>(1) << (bits - 1);
  const T max = ~static_cast<T>(0) >> 1;
  const T nc = ones - (ones - d) % d;
  bool a = false;         // Init. "add" indicator.
  unsigned p = bits - 1;  // Init. p.
  T q1 = min / nc;        // Init. q1 = 2**p/nc
  T r1 = min - q1 * nc;   // Init. r1 = rem(2**p,nc)
  T q2 = max / d;         // Init. q2 = (2**p - 1)/d.
  T r2 = max - q2 * d;    // Init. r2 = rem(2**p - 1, d).
  T delta;
  do {
    p = p + 1;
    if (r1 >= nc - r1) {
      q1 = 2 * q1 + 1;
      r1 = 2 * r1 - nc;
    } else {
      q1 = 2 * q1;
      r1 = 2 * r1;
    }
    if (r2 + 1 >= d - r2) {
      if (q2 >= max) a = true;
      q2 = 2 * q2 + 1;
      r2 = 2 * r2 + 1 - d;
    } else {
      if (q2 >= min) a = true;
      q2 = 2 * q2;
      r2 = 2 * r2 + 1;
    }
    delta = d - 1 - r2;
  } while (p < bits * 2 && (q1 < delta || (q1 == delta && r1 == 0)));
  return MagicNumbersForDivision<T>(q2 + 1, p - bits, a);
}


// -----------------------------------------------------------------------------
// Instantiations.

template struct EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint32_t>;
template struct EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint64_t>;

template EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint32_t> SignedDivisionByConstant(uint32_t d);
template EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint64_t> SignedDivisionByConstant(uint64_t d);

template EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint32_t> UnsignedDivisionByConstant(
        uint32_t d, unsigned leading_zeros);
template EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint64_t> UnsignedDivisionByConstant(
        uint64_t d, unsigned leading_zeros);

}  // namespace base
}  // namespace v8
