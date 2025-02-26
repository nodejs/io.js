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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "hwy/aligned_allocator.h"
#include "hwy/base.h"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/dot/dot_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/contrib/dot/dot-inl.h"
#include "hwy/tests/test_util-inl.h"
// clang-format on

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

template <typename T1, typename T2>
HWY_NOINLINE T1 SimpleDot(const T1* pa, const T2* pb, size_t num) {
  float sum = 0.0f;
  for (size_t i = 0; i < num; ++i) {
    sum += ConvertScalarTo<float>(pa[i]) * ConvertScalarTo<float>(pb[i]);
  }
  return ConvertScalarTo<T1>(sum);
}

HWY_MAYBE_UNUSED HWY_NOINLINE float SimpleDot(const float* pa,
                                              const hwy::bfloat16_t* pb,
                                              size_t num) {
  float sum = 0.0f;
  for (size_t i = 0; i < num; ++i) {
    sum += pa[i] * F32FromBF16(pb[i]);
  }
  return sum;
}

// Overload is required because the generic template hits an internal compiler
// error on aarch64 clang.
HWY_MAYBE_UNUSED HWY_NOINLINE float SimpleDot(const bfloat16_t* pa,
                                              const bfloat16_t* pb,
                                              size_t num) {
  float sum = 0.0f;
  for (size_t i = 0; i < num; ++i) {
    sum += F32FromBF16(pa[i]) * F32FromBF16(pb[i]);
  }
  return sum;
}

class TestDot {
  // Computes/verifies one dot product.
  template <int kAssumptions, class D>
  void Test(D d, size_t num, size_t misalign_a, size_t misalign_b,
            RandomState& rng) {
    using T = TFromD<D>;
    const size_t N = Lanes(d);
    const auto random_t = [&rng]() {
      const int32_t bits = static_cast<int32_t>(Random32(&rng)) & 1023;
      return static_cast<float>(bits - 512) * (1.0f / 64);
    };

    const size_t padded =
        (kAssumptions & Dot::kPaddedToVector) ? RoundUpTo(num, N) : num;
    AlignedFreeUniquePtr<T[]> pa = AllocateAligned<T>(misalign_a + padded);
    AlignedFreeUniquePtr<T[]> pb = AllocateAligned<T>(misalign_b + padded);
    HWY_ASSERT(pa && pb);
    T* a = pa.get() + misalign_a;
    T* b = pb.get() + misalign_b;
    size_t i = 0;
    for (; i < num; ++i) {
      a[i] = ConvertScalarTo<T>(random_t());
      b[i] = ConvertScalarTo<T>(random_t());
    }
    // Fill padding with NaN - the values are not used, but avoids MSAN errors.
    for (; i < padded; ++i) {
      ScalableTag<float> df1;
      a[i] = ConvertScalarTo<T>(GetLane(NaN(df1)));
      b[i] = ConvertScalarTo<T>(GetLane(NaN(df1)));
    }

    const double expected = SimpleDot(a, b, num);
    const double magnitude = expected > 0.0 ? expected : -expected;
    const double actual =
        ConvertScalarTo<double>(Dot::Compute<kAssumptions>(d, a, b, num));
    const double max = static_cast<double>(8 * 8 * num);
    HWY_ASSERT(-max <= actual && actual <= max);
    const double tolerance =
        96.0 * ConvertScalarTo<double>(Epsilon<T>()) * HWY_MAX(magnitude, 1.0);
    HWY_ASSERT(expected - tolerance <= actual &&
               actual <= expected + tolerance);
  }

  // Runs tests with various alignments.
  template <int kAssumptions, class D>
  void ForeachMisalign(D d, size_t num, RandomState& rng) {
    const size_t N = Lanes(d);
    const size_t misalignments[3] = {0, N / 4, 3 * N / 5};
    for (size_t ma : misalignments) {
      for (size_t mb : misalignments) {
        Test<kAssumptions>(d, num, ma, mb, rng);
      }
    }
  }

  // Runs tests with various lengths compatible with the given assumptions.
  template <int kAssumptions, class D>
  void ForeachCount(D d, RandomState& rng) {
    const size_t N = Lanes(d);
    const size_t counts[] = {1,
                             3,
                             7,
                             16,
                             HWY_MAX(N / 2, 1),
                             HWY_MAX(2 * N / 3, 1),
                             N,
                             N + 1,
                             4 * N / 3,
                             3 * N,
                             8 * N,
                             8 * N + 2};
    for (size_t num : counts) {
      if ((kAssumptions & Dot::kAtLeastOneVector) && num < N) continue;
      if ((kAssumptions & Dot::kMultipleOfVector) && (num % N) != 0) continue;
      ForeachMisalign<kAssumptions>(d, num, rng);
    }
  }

 public:
  // Must be inlined on aarch64 for bf16, else clang crashes.
  template <class T, class D>
  HWY_INLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    // All 8 combinations of the three length-related flags:
    ForeachCount<0>(d, rng);
    ForeachCount<Dot::kAtLeastOneVector>(d, rng);
    ForeachCount<Dot::kMultipleOfVector>(d, rng);
    ForeachCount<Dot::kMultipleOfVector | Dot::kAtLeastOneVector>(d, rng);
    ForeachCount<Dot::kPaddedToVector>(d, rng);
    ForeachCount<Dot::kPaddedToVector | Dot::kAtLeastOneVector>(d, rng);
    ForeachCount<Dot::kPaddedToVector | Dot::kMultipleOfVector>(d, rng);
    ForeachCount<Dot::kPaddedToVector | Dot::kMultipleOfVector |
                 Dot::kAtLeastOneVector>(d, rng);
  }
};

class TestDotF32BF16 {
  // Computes/verifies one dot product.
  template <int kAssumptions, class D>
  void Test(D d, size_t num, size_t misalign_a, size_t misalign_b,
            RandomState& rng) {
    using T = TFromD<D>;
    using T2 = hwy::bfloat16_t;
    const size_t N = Lanes(d);
    const auto random_t = [&rng]() {
      const int32_t bits = static_cast<int32_t>(Random32(&rng)) & 1023;
      return static_cast<float>(bits - 512) * (1.0f / 64);
    };

    const size_t padded =
        (kAssumptions & Dot::kPaddedToVector) ? RoundUpTo(num, N) : num;
    AlignedFreeUniquePtr<T[]> pa = AllocateAligned<T>(misalign_a + padded);
    AlignedFreeUniquePtr<T2[]> pb = AllocateAligned<T2>(misalign_b + padded);
    HWY_ASSERT(pa && pb);
    T* a = pa.get() + misalign_a;
    T2* b = pb.get() + misalign_b;
    size_t i = 0;
    for (; i < num; ++i) {
      a[i] = ConvertScalarTo<T>(random_t());
      b[i] = ConvertScalarTo<T2>(random_t());
    }
    // Fill padding with NaN - the values are not used, but avoids MSAN errors.
    for (; i < padded; ++i) {
      ScalableTag<float> df1;
      a[i] = ConvertScalarTo<T>(GetLane(NaN(df1)));
      b[i] = ConvertScalarTo<T2>(GetLane(NaN(df1)));
    }

    const double expected = SimpleDot(a, b, num);
    const double magnitude = expected > 0.0 ? expected : -expected;
    const double actual =
        ConvertScalarTo<double>(Dot::Compute<kAssumptions>(d, a, b, num));
    const double max = static_cast<double>(8 * 8 * num);
    HWY_ASSERT(-max <= actual && actual <= max);
    const double tolerance =
        64.0 * ConvertScalarTo<double>(Epsilon<T2>()) * HWY_MAX(magnitude, 1.0);
    HWY_ASSERT(expected - tolerance <= actual &&
               actual <= expected + tolerance);
  }

  // Runs tests with various alignments.
  template <int kAssumptions, class D>
  void ForeachMisalign(D d, size_t num, RandomState& rng) {
    const size_t N = Lanes(d);
    const size_t misalignments[3] = {0, N / 4, 3 * N / 5};
    for (size_t ma : misalignments) {
      for (size_t mb : misalignments) {
        Test<kAssumptions>(d, num, ma, mb, rng);
      }
    }
  }

  // Runs tests with various lengths compatible with the given assumptions.
  template <int kAssumptions, class D>
  void ForeachCount(D d, RandomState& rng) {
    const size_t N = Lanes(d);
    const size_t counts[] = {1,
                             3,
                             7,
                             16,
                             HWY_MAX(N / 2, 1),
                             HWY_MAX(2 * N / 3, 1),
                             N,
                             N + 1,
                             4 * N / 3,
                             3 * N,
                             8 * N,
                             8 * N + 2};
    for (size_t num : counts) {
      if ((kAssumptions & Dot::kAtLeastOneVector) && num < N) continue;
      if ((kAssumptions & Dot::kMultipleOfVector) && (num % N) != 0) continue;
      ForeachMisalign<kAssumptions>(d, num, rng);
    }
  }

 public:
  // Must be inlined on aarch64 for bf16, else clang crashes.
  template <class T, class D>
  HWY_INLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    // All 8 combinations of the three length-related flags:
    ForeachCount<0>(d, rng);
    ForeachCount<Dot::kAtLeastOneVector>(d, rng);
    ForeachCount<Dot::kMultipleOfVector>(d, rng);
    ForeachCount<Dot::kMultipleOfVector | Dot::kAtLeastOneVector>(d, rng);
    ForeachCount<Dot::kPaddedToVector>(d, rng);
    ForeachCount<Dot::kPaddedToVector | Dot::kAtLeastOneVector>(d, rng);
    ForeachCount<Dot::kPaddedToVector | Dot::kMultipleOfVector>(d, rng);
    ForeachCount<Dot::kPaddedToVector | Dot::kMultipleOfVector |
                 Dot::kAtLeastOneVector>(d, rng);
  }
};

// All floating-point types, both arguments same.
void TestAllDot() { ForFloatTypes(ForPartialVectors<TestDot>()); }

// Mixed f32 and bf16.
void TestAllDotF32BF16() {
  ForPartialVectors<TestDotF32BF16> test;
  test(float());
}

// Both bf16.
void TestAllDotBF16() { ForShrinkableVectors<TestDot>()(bfloat16_t()); }

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(DotTest);
HWY_EXPORT_AND_TEST_P(DotTest, TestAllDot);
HWY_EXPORT_AND_TEST_P(DotTest, TestAllDotF32BF16);
HWY_EXPORT_AND_TEST_P(DotTest, TestAllDotBF16);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
