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

#include "absl/random/internal/traits.h"

#include <cstdint>
#include <type_traits>

#include "gtest/gtest.h"

namespace {

using absl::random_internal::is_widening_convertible;

// CheckWideningConvertsToSelf<T1, T2, ...>()
//
// For each type T, checks:
// - T IS widening-convertible to itself.
//
template <typename T>
void CheckWideningConvertsToSelf() {
  static_assert(is_widening_convertible<T, T>::value,
                "Type is not convertible to self!");
}

template <typename T, typename Next, typename... Args>
void CheckWideningConvertsToSelf() {
  CheckWideningConvertsToSelf<T>();
  CheckWideningConvertsToSelf<Next, Args...>();
}

// CheckNotWideningConvertibleWithSigned<T1, T2, ...>()
//
// For each unsigned-type T, checks that:
// - T is NOT widening-convertible to Signed(T)
// - Signed(T) is NOT widening-convertible to T
//
template <typename T>
void CheckNotWideningConvertibleWithSigned() {
  using signed_t = typename std::make_signed<T>::type;

  static_assert(!is_widening_convertible<T, signed_t>::value,
                "Unsigned type is convertible to same-sized signed-type!");
  static_assert(!is_widening_convertible<signed_t, T>::value,
                "Signed type is convertible to same-sized unsigned-type!");
}

template <typename T, typename Next, typename... Args>
void CheckNotWideningConvertibleWithSigned() {
  CheckNotWideningConvertibleWithSigned<T>();
  CheckWideningConvertsToSelf<Next, Args...>();
}

// CheckWideningConvertsToLargerType<T1, T2, ...>()
//
// For each successive unsigned-types {Ti, Ti+1}, checks that:
// - Ti IS widening-convertible to Ti+1
// - Ti IS widening-convertible to Signed(Ti+1)
// - Signed(Ti) is NOT widening-convertible to Ti
// - Signed(Ti) IS widening-convertible to Ti+1
template <typename T, typename Higher>
void CheckWideningConvertsToLargerTypes() {
  using signed_t = typename std::make_signed<T>::type;
  using higher_t = Higher;
  using signed_higher_t = typename std::make_signed<Higher>::type;

  static_assert(is_widening_convertible<T, higher_t>::value,
                "Type not embeddable into larger type!");
  static_assert(is_widening_convertible<T, signed_higher_t>::value,
                "Type not embeddable into larger signed type!");
  static_assert(!is_widening_convertible<signed_t, higher_t>::value,
                "Signed type is embeddable into larger unsigned type!");
  static_assert(is_widening_convertible<signed_t, signed_higher_t>::value,
                "Signed type not embeddable into larger signed type!");
}

template <typename T, typename Higher, typename Next, typename... Args>
void CheckWideningConvertsToLargerTypes() {
  CheckWideningConvertsToLargerTypes<T, Higher>();
  CheckWideningConvertsToLargerTypes<Higher, Next, Args...>();
}

// CheckWideningConvertsTo<T, U, [expect]>
//
// Checks that T DOES widening-convert to U.
// If "expect" is false, then asserts that T does NOT widening-convert to U.
template <typename T, typename U, bool expect = true>
void CheckWideningConvertsTo() {
  static_assert(is_widening_convertible<T, U>::value == expect,
                "Unexpected result for is_widening_convertible<T, U>!");
}

TEST(TraitsTest, IsWideningConvertibleTest) {
  constexpr bool kInvalid = false;

  CheckWideningConvertsToSelf<
      uint8_t, uint16_t, uint32_t, uint64_t,
      int8_t,  int16_t,  int32_t,  int64_t,
      float,   double>();
  CheckNotWideningConvertibleWithSigned<
      uint8_t, uint16_t, uint32_t, uint64_t>();
  CheckWideningConvertsToLargerTypes<
      uint8_t, uint16_t, uint32_t, uint64_t>();

  CheckWideningConvertsTo<float, double>();
  CheckWideningConvertsTo<uint16_t, float>();
  CheckWideningConvertsTo<uint32_t, double>();
  CheckWideningConvertsTo<uint64_t, double, kInvalid>();
  CheckWideningConvertsTo<double, float, kInvalid>();

  CheckWideningConvertsTo<bool, int>();
  CheckWideningConvertsTo<bool, float>();
}

}  // namespace
