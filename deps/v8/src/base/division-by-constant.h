// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_DIVISION_BY_CONSTANT_H_
#define V8_BASE_DIVISION_BY_CONSTANT_H_

#include <stdint.h>

#include <concepts>
#include <tuple>
#include <type_traits>

#include "src/base/base-export.h"
#include "src/base/export-template.h"

namespace v8 {
namespace base {

// ----------------------------------------------------------------------------

// The magic numbers for division via multiplication, see Warren's "Hacker's
// Delight", chapter 10.
template <class T>
struct EXPORT_TEMPLATE_DECLARE(V8_BASE_EXPORT) MagicNumbersForDivision {
  static_assert(std::is_integral_v<T>);
  MagicNumbersForDivision(T m, unsigned s, bool a)
      : multiplier(m), shift(s), add(a) {}
  bool operator==(const MagicNumbersForDivision& rhs) const {
    return multiplier == rhs.multiplier && shift == rhs.shift && add == rhs.add;
  }

  T multiplier;
  unsigned shift;
  bool add;
};

// Calculate the multiplier and shift for signed division via multiplication.
// The divisor must not be -1, 0 or 1 when interpreted as a signed value.
template <typename T>
  requires std::unsigned_integral<T>
EXPORT_TEMPLATE_DECLARE(V8_BASE_EXPORT)
    MagicNumbersForDivision<T> SignedDivisionByConstant(T d);

template <typename T>
  requires std::signed_integral<T>
MagicNumbersForDivision<T> SignedDivisionByConstant(T d) {
  using Unsigned = std::make_unsigned_t<T>;
  MagicNumbersForDivision<Unsigned> magic =
      SignedDivisionByConstant(static_cast<Unsigned>(d));
  return {static_cast<T>(magic.multiplier), magic.shift, magic.add};
}

// Calculate the multiplier and shift for unsigned division via multiplication,
// see Warren's "Hacker's Delight", chapter 10. The divisor must not be 0 and
// leading_zeros can be used to speed up the calculation if the given number of
// upper bits of the dividend value are known to be zero.
template <class T>
EXPORT_TEMPLATE_DECLARE(V8_BASE_EXPORT)
MagicNumbersForDivision<T> UnsignedDivisionByConstant(
    T d, unsigned leading_zeros = 0);

// Explicit instantiation declarations.
extern template struct EXPORT_TEMPLATE_DECLARE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint32_t>;
extern template struct EXPORT_TEMPLATE_DECLARE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint64_t>;

extern template EXPORT_TEMPLATE_DECLARE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint32_t> SignedDivisionByConstant(uint32_t d);
extern template EXPORT_TEMPLATE_DECLARE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint64_t> SignedDivisionByConstant(uint64_t d);

extern template EXPORT_TEMPLATE_DECLARE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint32_t> UnsignedDivisionByConstant(
        uint32_t d, unsigned leading_zeros);
extern template EXPORT_TEMPLATE_DECLARE(V8_BASE_EXPORT)
    MagicNumbersForDivision<uint64_t> UnsignedDivisionByConstant(
        uint64_t d, unsigned leading_zeros);

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_DIVISION_BY_CONSTANT_H_
