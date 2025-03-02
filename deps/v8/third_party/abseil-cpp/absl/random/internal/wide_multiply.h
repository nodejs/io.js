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

#ifndef ABSL_RANDOM_INTERNAL_WIDE_MULTIPLY_H_
#define ABSL_RANDOM_INTERNAL_WIDE_MULTIPLY_H_

#include <cstdint>
#include <limits>
#include <type_traits>

#if (defined(_WIN32) || defined(_WIN64)) && defined(_M_IA64)
#include <intrin.h>  // NOLINT(build/include_order)
#pragma intrinsic(_umul128)
#define ABSL_INTERNAL_USE_UMUL128 1
#endif

#include "absl/base/config.h"
#include "absl/numeric/bits.h"
#include "absl/numeric/int128.h"
#include "absl/random/internal/traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// wide_multiply<T> multiplies two N-bit values to a 2N-bit result.
template <typename UIntType>
struct wide_multiply {
  static constexpr size_t kN = std::numeric_limits<UIntType>::digits;
  using input_type = UIntType;
  using result_type = typename random_internal::unsigned_bits<kN * 2>::type;

  static result_type multiply(input_type a, input_type b) {
    return static_cast<result_type>(a) * b;
  }

  static input_type hi(result_type r) {
    return static_cast<input_type>(r >> kN);
  }
  static input_type lo(result_type r) { return static_cast<input_type>(r); }

  static_assert(std::is_unsigned<UIntType>::value,
                "Class-template wide_multiply<> argument must be unsigned.");
};

// MultiplyU128ToU256 multiplies two 128-bit values to a 256-bit value.
inline U256 MultiplyU128ToU256(uint128 a, uint128 b) {
  const uint128 a00 = static_cast<uint64_t>(a);
  const uint128 a64 = a >> 64;
  const uint128 b00 = static_cast<uint64_t>(b);
  const uint128 b64 = b >> 64;

  const uint128 c00 = a00 * b00;
  const uint128 c64a = a00 * b64;
  const uint128 c64b = a64 * b00;
  const uint128 c128 = a64 * b64;

  const uint64_t carry =
      static_cast<uint64_t>(((c00 >> 64) + static_cast<uint64_t>(c64a) +
                             static_cast<uint64_t>(c64b)) >>
                            64);

  return {c128 + (c64a >> 64) + (c64b >> 64) + carry,
          c00 + (c64a << 64) + (c64b << 64)};
}

template <>
struct wide_multiply<uint128> {
  using input_type = uint128;
  using result_type = U256;

  static result_type multiply(input_type a, input_type b) {
    return MultiplyU128ToU256(a, b);
  }

  static input_type hi(result_type r) { return r.hi; }
  static input_type lo(result_type r) { return r.lo; }
};

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_WIDE_MULTIPLY_H_
