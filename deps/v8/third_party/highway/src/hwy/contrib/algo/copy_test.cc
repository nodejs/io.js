// Copyright 2022 Google LLC
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

#include <stddef.h>

#include "hwy/aligned_allocator.h"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/algo/copy_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/contrib/algo/copy-inl.h"
#include "hwy/tests/test_util-inl.h"
// clang-format on

// If your project requires C++14 or later, you can ignore this and pass lambdas
// directly to Transform, without requiring an lvalue as we do here for C++11.
#if __cplusplus < 201402L
#define HWY_GENERIC_LAMBDA 0
#else
#define HWY_GENERIC_LAMBDA 1
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

// Returns random integer in [0, 128), which fits in any lane type.
template <typename T>
T Random7Bit(RandomState& rng) {
  return ConvertScalarTo<T>(Random32(&rng) & 127);
}

// In C++14, we can instead define these as generic lambdas next to where they
// are invoked.
#if !HWY_GENERIC_LAMBDA

struct IsOdd {
  template <class D, class V>
  Mask<D> operator()(D d, V v) const {
    return TestBit(v, Set(d, TFromD<D>{1}));
  }
};

#endif  // !HWY_GENERIC_LAMBDA

// Invokes Test (e.g. TestCopyIf) with all arg combinations. T comes from
// ForFloatTypes.
template <class Test>
struct ForeachCountAndMisalign {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) const {
    RandomState rng;
    const size_t N = Lanes(d);
    const size_t misalignments[3] = {0, N / 4, 3 * N / 5};

    for (size_t count = 0; count < 2 * N; ++count) {
      for (size_t ma : misalignments) {
        for (size_t mb : misalignments) {
          Test()(d, count, ma, mb, rng);
        }
      }
    }
  }
};

struct TestFill {
  template <class D>
  void operator()(D d, size_t count, size_t misalign_a, size_t misalign_b,
                  RandomState& rng) {
    using T = TFromD<D>;
    // HWY_MAX prevents error when misalign == count == 0.
    AlignedFreeUniquePtr<T[]> pa =
        AllocateAligned<T>(HWY_MAX(1, misalign_a + count));
    AlignedFreeUniquePtr<T[]> pb = AllocateAligned<T>(misalign_b + count + 1);
    HWY_ASSERT(pa && pb);
    T* expected = pa.get() + misalign_a;
    const T value = Random7Bit<T>(rng);
    for (size_t i = 0; i < count; ++i) {
      expected[i] = value;
    }
    T* actual = pb.get() + misalign_b;

    actual[count] = ConvertScalarTo<T>(0);  // sentinel
    Fill(d, value, count, actual);
    HWY_ASSERT_EQ(ConvertScalarTo<T>(0), actual[count]);  // no write past end

    const auto info = hwy::detail::MakeTypeInfo<T>();
    const char* target_name = hwy::TargetName(HWY_TARGET);
    hwy::detail::AssertArrayEqual(info, expected, actual, count, target_name,
                                  __FILE__, __LINE__);
  }
};

void TestAllFill() {
  ForAllTypes(ForPartialVectors<ForeachCountAndMisalign<TestFill>>());
}

struct TestCopy {
  template <class D>
  void operator()(D d, size_t count, size_t misalign_a, size_t misalign_b,
                  RandomState& rng) {
    using T = TFromD<D>;
    // Prevents error if size to allocate is zero.
    AlignedFreeUniquePtr<T[]> pa =
        AllocateAligned<T>(HWY_MAX(1, misalign_a + count));
    AlignedFreeUniquePtr<T[]> pb =
        AllocateAligned<T>(HWY_MAX(1, misalign_b + count));
    HWY_ASSERT(pa && pb);
    T* a = pa.get() + misalign_a;
    for (size_t i = 0; i < count; ++i) {
      a[i] = Random7Bit<T>(rng);
    }
    T* b = pb.get() + misalign_b;

    Copy(d, a, count, b);

    const auto info = hwy::detail::MakeTypeInfo<T>();
    const char* target_name = hwy::TargetName(HWY_TARGET);
    hwy::detail::AssertArrayEqual(info, a, b, count, target_name, __FILE__,
                                  __LINE__);
  }
};

void TestAllCopy() {
  ForAllTypes(ForPartialVectors<ForeachCountAndMisalign<TestCopy>>());
}

struct TestCopyIf {
  template <class D>
  void operator()(D d, size_t count, size_t misalign_a, size_t misalign_b,
                  RandomState& rng) {
    using T = TFromD<D>;
    const size_t padding = Lanes(ScalableTag<T>());

    // Prevents error if size to allocate is zero.
    AlignedFreeUniquePtr<T[]> pa =
        AllocateAligned<T>(HWY_MAX(1, misalign_a + count));
    AlignedFreeUniquePtr<T[]> pb =
        AllocateAligned<T>(HWY_MAX(1, misalign_b + count + padding));
    AlignedFreeUniquePtr<T[]> expected = AllocateAligned<T>(HWY_MAX(1, count));
    HWY_ASSERT(pa && pb && expected);

    T* a = pa.get() + misalign_a;
    for (size_t i = 0; i < count; ++i) {
      a[i] = Random7Bit<T>(rng);
    }
    T* b = pb.get() + misalign_b;

    size_t num_odd = 0;
    for (size_t i = 0; i < count; ++i) {
      if (a[i] & 1) {
        expected[num_odd++] = a[i];
      }
    }

#if HWY_GENERIC_LAMBDA
    const auto is_odd = [](const auto d, const auto v) HWY_ATTR {
      return TestBit(v, Set(d, TFromD<decltype(d)>{1}));
    };
#else
    const IsOdd is_odd;
#endif
    T* end = CopyIf(d, a, count, b, is_odd);
    const size_t num_written = static_cast<size_t>(end - b);
    HWY_ASSERT_EQ(num_odd, num_written);

    const auto info = hwy::detail::MakeTypeInfo<T>();
    const char* target_name = hwy::TargetName(HWY_TARGET);
    hwy::detail::AssertArrayEqual(info, expected.get(), b, num_odd, target_name,
                                  __FILE__, __LINE__);
  }
};

void TestAllCopyIf() {
  ForUI163264(ForPartialVectors<ForeachCountAndMisalign<TestCopyIf>>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(CopyTest);
HWY_EXPORT_AND_TEST_P(CopyTest, TestAllFill);
HWY_EXPORT_AND_TEST_P(CopyTest, TestAllCopy);
HWY_EXPORT_AND_TEST_P(CopyTest, TestAllCopyIf);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
