// Copyright 2021 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HWY_TESTS_TEST_UTIL_H_
#define HWY_TESTS_TEST_UTIL_H_

// Target-independent helper functions for use by *_test.cc.

#include <string.h>

#include <cmath>  // std::isnan
#include <string>

#include "hwy/base.h"
#include "hwy/nanobenchmark.h"
#include "hwy/print.h"

namespace hwy {

// The maximum vector size used in tests when defining test data. DEPRECATED.
HWY_MAYBE_UNUSED constexpr size_t kTestMaxVectorSize = 64;

// 64-bit random generator (Xorshift128+). Much smaller state than std::mt19937,
// which triggers a compiler bug.
class RandomState {
 public:
  explicit RandomState(
      const uint64_t seed = uint64_t{0x123456789} *
                            static_cast<uint64_t>(hwy::Unpredictable1())) {
    s0_ = SplitMix64(seed + 0x9E3779B97F4A7C15ull);
    s1_ = SplitMix64(s0_);
  }

  HWY_INLINE uint64_t operator()() {
    uint64_t s1 = s0_;
    const uint64_t s0 = s1_;
    const uint64_t bits = s1 + s0;
    s0_ = s0;
    s1 ^= s1 << 23;
    s1 ^= s0 ^ (s1 >> 18) ^ (s0 >> 5);
    s1_ = s1;
    return bits;
  }

 private:
  static uint64_t SplitMix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

  uint64_t s0_;
  uint64_t s1_;
};

static HWY_INLINE uint32_t Random32(RandomState* rng) {
  return static_cast<uint32_t>((*rng)());
}

static HWY_INLINE uint64_t Random64(RandomState* rng) { return (*rng)(); }

template <class T, HWY_IF_FLOAT_OR_SPECIAL(T)>
static HWY_INLINE T RandomFiniteValue(RandomState* rng) {
  const uint64_t rand_bits = Random64(rng);

  using TU = MakeUnsigned<T>;
  constexpr TU kExponentMask = ExponentMask<T>();
  constexpr TU kSignMantMask = static_cast<TU>(~kExponentMask);
  constexpr TU kMaxExpField = static_cast<TU>(MaxExponentField<T>());
  constexpr int kNumOfMantBits = MantissaBits<T>();

  const TU orig_exp_field_val =
      static_cast<TU>((rand_bits >> kNumOfMantBits) & kMaxExpField);

  const TU sign_mant_bits = static_cast<TU>(rand_bits & kSignMantMask);
  const TU exp_bits =
      static_cast<TU>(HWY_MIN(HWY_MAX(orig_exp_field_val, 1), kMaxExpField - 1)
                      << kNumOfMantBits);

  return BitCastScalar<T>(static_cast<TU>(sign_mant_bits | exp_bits));
}

template <class T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
static HWY_INLINE T RandomFiniteValue(RandomState* rng) {
  using TU = MakeUnsigned<T>;
  return static_cast<T>(Random64(rng) & LimitsMax<TU>());
}

HWY_TEST_DLLEXPORT bool BytesEqual(const void* p1, const void* p2, size_t size,
                                   size_t* pos = nullptr);

void AssertStringEqual(const char* expected, const char* actual,
                       const char* target_name, const char* filename, int line);

namespace detail {

template <typename T, typename TU = MakeUnsigned<T>>
TU ComputeUlpDelta(const T expected, const T actual) {
  // Handle -0 == 0 and infinities.
  if (expected == actual) return 0;

  // Consider "equal" if both are NaN, so we can verify an expected NaN.
  // Needs a special case because there are many possible NaN representations.
  if (std::isnan(expected) && std::isnan(actual)) return 0;

  // Compute the difference in units of last place. We do not need to check for
  // differing signs; they will result in large differences, which is fine.
  TU ux, uy;
  CopySameSize(&expected, &ux);
  CopySameSize(&actual, &uy);

  // Avoid unsigned->signed cast: 2's complement is only guaranteed by C++20.
  const TU ulp = HWY_MAX(ux, uy) - HWY_MIN(ux, uy);
  return ulp;
}

HWY_TEST_DLLEXPORT bool IsEqual(const TypeInfo& info, const void* expected_ptr,
                                const void* actual_ptr);

HWY_TEST_DLLEXPORT HWY_NORETURN void PrintMismatchAndAbort(
    const TypeInfo& info, const void* expected_ptr, const void* actual_ptr,
    const char* target_name, const char* filename, int line, size_t lane = 0,
    size_t num_lanes = 1);

HWY_TEST_DLLEXPORT void AssertArrayEqual(const TypeInfo& info,
                                         const void* expected_void,
                                         const void* actual_void, size_t N,
                                         const char* target_name,
                                         const char* filename, int line);

}  // namespace detail

// Returns a name for the vector/part/scalar. The type prefix is u/i/f for
// unsigned/signed/floating point, followed by the number of bits per lane;
// then 'x' followed by the number of lanes. Example: u8x16. This is useful for
// understanding which instantiation of a generic test failed.
template <typename T>
std::string TypeName(T /*unused*/, size_t N) {
  char string100[100];
  detail::TypeName(detail::MakeTypeInfo<T>(), N, string100);
  return string100;
}

// Type large enough to hold either value, to which we cast for comparison.
template <typename T1, typename T2>
using LargestType = If<IsFloat<T1>() || IsFloat<T2>(),
                       FloatFromSize<HWY_MAX(sizeof(T1), sizeof(T2))>,
                       If<IsSigned<T1>() || IsSigned<T2>(),
                          SignedFromSize<HWY_MAX(sizeof(T1), sizeof(T2))>,
                          UnsignedFromSize<HWY_MAX(sizeof(T1), sizeof(T2))>>>;

// TTo is the lane type of the actual value and T is an often but not
// necessarily larger type of the expected value. Especially for 8-bit lanes
// initialized via Iota, the actual value often wraps around. To ensure it still
// compares equal to the expected value, wrap integers.
// 1) < 64-bit integer: mask
template <typename TTo, typename T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(TTo),
          HWY_IF_T_SIZE_LE(TTo, 4)>
T WrapTo(T value) {
  return static_cast<T>(static_cast<uint64_t>(value) &
                        ((uint64_t{1} << (sizeof(TTo) * 8)) - 1));
}
// 2) 64-bit integer: no mask (shift would overflow)
template <typename TTo, typename T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(TTo),
          HWY_IF_T_SIZE_GT(TTo, 4)>
T WrapTo(T value) {
  return value;
}
// 3) float or special: do nothing because their value range is sufficient for
// Iota for any vector length.
template <typename TTo, typename T, HWY_IF_FLOAT_OR_SPECIAL(TTo)>
T WrapTo(T value) {
  return value;
}

// Compare non-vector, non-string T, after promoting to the largest type.
template <typename TExpected, typename TActual>
HWY_INLINE bool IsEqual(const TExpected texpected, const TActual actual) {
  const TActual expected = ConvertScalarTo<TActual>(WrapTo<TActual>(texpected));
  const auto info = detail::MakeTypeInfo<TActual>();
  return detail::IsEqual(info, &expected, &actual);
}

template <typename TExpected, typename TActual>
HWY_INLINE void AssertEqual(const TExpected texpected, const TActual actual,
                            const char* target_name, const char* filename,
                            int line, size_t lane = 0) {
  const TActual expected = ConvertScalarTo<TActual>(WrapTo<TActual>(texpected));
  const auto info = detail::MakeTypeInfo<TActual>();
  if (!detail::IsEqual(info, &expected, &actual)) {
    detail::PrintMismatchAndAbort(info, &expected, &actual, target_name,
                                  filename, line, lane);
  }
}

template <typename T>
HWY_INLINE void AssertArrayEqual(const T* expected, const T* actual,
                                 size_t count, const char* target_name,
                                 const char* filename, int line) {
  const auto info = hwy::detail::MakeTypeInfo<T>();
  detail::AssertArrayEqual(info, expected, actual, count, target_name, filename,
                           line);
}

// Compare with tolerance due to FMA and f16 precision.
template <typename T>
HWY_INLINE void AssertArraySimilar(const T* expected, const T* actual,
                                   size_t count, const char* target_name,
                                   const char* filename, int line) {
  const double tolerance =
      (hwy::IsSame<RemoveCvRef<T>, float16_t>() ? 128.0 : 1.0) /
      (uint64_t{1} << MantissaBits<T>());
  for (size_t i = 0; i < count; ++i) {
    const double exp = ConvertScalarTo<double>(expected[i]);
    const double act = ConvertScalarTo<double>(actual[i]);
    const double l1 = ScalarAbs(act - exp);
    // Cannot divide, so check absolute error.
    if (exp == 0.0) {
      if (l1 > tolerance) {
        HWY_ABORT("%s %s:%d %s mismatch %zu of %zu: %E %E l1 %E tol %E\n",
                  target_name, filename, line, TypeName(T(), 1).c_str(), i,
                  count, exp, act, l1, tolerance);
      }
    } else {  // relative
      const double rel = l1 / exp;
      if (rel > tolerance) {
        HWY_ABORT("%s %s:%d %s mismatch %zu of %zu: %E %E rel %E tol %E\n",
                  target_name, filename, line, TypeName(T(), 1).c_str(), i,
                  count, exp, act, rel, tolerance);
      }
    }
  }
}

}  // namespace hwy

#endif  // HWY_TESTS_TEST_UTIL_H_
