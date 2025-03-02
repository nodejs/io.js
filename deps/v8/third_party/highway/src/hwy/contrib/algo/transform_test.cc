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

#include <string.h>  // memcpy

#include <vector>

#include "hwy/aligned_allocator.h"
#include "hwy/base.h"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/algo/transform_test.cc"  //NOLINT
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/contrib/algo/transform-inl.h"
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

constexpr double kAlpha = 1.5;  // arbitrary scalar

// Returns random floating-point number in [-8, 8) to ensure computations do
// not exceed float32 precision.
template <typename T>
T Random(RandomState& rng) {
  const int32_t bits = static_cast<int32_t>(Random32(&rng)) & 1023;
  const double val = (bits - 512) / 64.0;
  // Clamp negative to zero for unsigned types.
  return ConvertScalarTo<T>(
      HWY_MAX(ConvertScalarTo<double>(hwy::LowestValue<T>()), val));
}

// SCAL, AXPY names are from BLAS.
template <typename T>
HWY_NOINLINE void SimpleSCAL(const T* x, T* out, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    out[i] = ConvertScalarTo<T>(ConvertScalarTo<T>(kAlpha) * x[i]);
  }
}

template <typename T>
HWY_NOINLINE void SimpleAXPY(const T* x, const T* y, T* out, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    out[i] = ConvertScalarTo<T>(
        ConvertScalarTo<T>(ConvertScalarTo<T>(kAlpha) * x[i]) + y[i]);
  }
}

template <typename T>
HWY_NOINLINE void SimpleFMA4(const T* x, const T* y, const T* z, T* out,
                             size_t count) {
  for (size_t i = 0; i < count; ++i) {
    out[i] = ConvertScalarTo<T>(x[i] * y[i] + z[i]);
  }
}

// In C++14, we can instead define these as generic lambdas next to where they
// are invoked.
#if !HWY_GENERIC_LAMBDA

// Generator that returns even numbers by doubling the output indices.
struct Gen2 {
  template <class D, class VU>
  Vec<D> operator()(D d, VU vidx) const {
    return BitCast(d, Add(vidx, vidx));
  }
};

struct SCAL {
  template <class D, class V>
  Vec<D> operator()(D d, V v) const {
    using T = TFromD<D>;
    return Mul(Set(d, ConvertScalarTo<T>(kAlpha)), v);
  }
};

struct AXPY {
  template <class D, class V>
  Vec<D> operator()(D d, V v, V v1) const {
    using T = TFromD<D>;
    return MulAdd(Set(d, ConvertScalarTo<T>(kAlpha)), v, v1);
  }
};

struct FMA4 {
  template <class D, class V>
  Vec<D> operator()(D /*d*/, V v, V v1, V v2) const {
    return MulAdd(v, v1, v2);
  }
};

#endif  // !HWY_GENERIC_LAMBDA

// Invokes Test (e.g. TestTransform1) with all arg combinations. T comes from
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

// Fills an array with random values, placing a given sentinel value both before
// (when misalignment space is available) and after. Requires an allocation of
// at least count + misalign + 1 elements.
template <typename T>
T* FillRandom(AlignedFreeUniquePtr<T[]>& pa, size_t count, size_t misalign,
              T sentinel, RandomState& rng) {
  for (size_t i = 0; i < misalign; ++i) {
    pa[i] = sentinel;
  }

  T* a = pa.get() + misalign;
  for (size_t i = 0; i < count; ++i) {
    a[i] = Random<T>(rng);
  }
  a[count] = sentinel;
  return a;
}

// Output-only, no loads
struct TestGenerate {
  template <class D>
  void operator()(D d, size_t count, size_t misalign_a, size_t /*misalign_b*/,
                  RandomState& /*rng*/) {
    using T = TFromD<D>;
    AlignedFreeUniquePtr<T[]> pa = AllocateAligned<T>(misalign_a + count + 1);
    AlignedFreeUniquePtr<T[]> expected = AllocateAligned<T>(HWY_MAX(1, count));
    HWY_ASSERT(pa && expected);

    T* actual = pa.get() + misalign_a;

    for (size_t i = 0; i < count; ++i) {
      expected[i] = ConvertScalarTo<T>(2 * i);
    }

    // TODO(janwas): can we update the apply_to in HWY_PUSH_ATTRIBUTES so that
    // the attribute also applies to lambdas? If so, remove HWY_ATTR.
#if HWY_GENERIC_LAMBDA
    const auto gen2 = [](const auto d, const auto vidx)
                          HWY_ATTR { return BitCast(d, Add(vidx, vidx)); };
#else
    const Gen2 gen2;
#endif
    actual[count] = ConvertScalarTo<T>(0);  // sentinel
    Generate(d, actual, count, gen2);
    HWY_ASSERT_EQ(ConvertScalarTo<T>(0), actual[count]);  // no write past end

    const auto info = hwy::detail::MakeTypeInfo<T>();
    const char* target_name = hwy::TargetName(HWY_TARGET);
    hwy::detail::AssertArrayEqual(info, expected.get(), actual, count,
                                  target_name, __FILE__, __LINE__);
  }
};

// Input-only, no stores
struct TestForeach {
  template <class D>
  void operator()(D d, size_t count, size_t misalign_a, size_t misalign_b,
                  RandomState& /*rng*/) {
    if (misalign_b != 0) return;
    using T = TFromD<D>;
    AlignedFreeUniquePtr<T[]> pa = AllocateAligned<T>(misalign_a + count + 1);
    HWY_ASSERT(pa);

    T* actual = pa.get() + misalign_a;
    T max = hwy::LowestValue<T>();
    for (size_t i = 0; i < count; ++i) {
      actual[i] = hwy::ConvertScalarTo<T>(i <= count / 2 ? 2 * i : i);
      max = HWY_MAX(max, actual[i]);
    }

    // Place sentinel values in the misalignment area and at the input's end.
    for (size_t i = 0; i < misalign_a; ++i) {
      pa[i] = ConvertScalarTo<T>(2 * count);
    }
    actual[count] = ConvertScalarTo<T>(2 * count);

    const Vec<D> vmin = Set(d, hwy::LowestValue<T>());
    // TODO(janwas): can we update the apply_to in HWY_PUSH_ATTRIBUTES so that
    // the attribute also applies to lambdas? If so, remove HWY_ATTR.
    Vec<D> vmax = vmin;
    const auto func = [&vmax](const D, const Vec<D> v)
                          HWY_ATTR { vmax = Max(vmax, v); };
    Foreach(d, actual, count, vmin, func);

    const char* target_name = hwy::TargetName(HWY_TARGET);
    AssertEqual(max, ReduceMax(d, vmax), target_name, __FILE__, __LINE__);
  }
};

// Zero extra input arrays
struct TestTransform {
  template <class D>
  void operator()(D d, size_t count, size_t misalign_a, size_t misalign_b,
                  RandomState& rng) {
    if (misalign_b != 0) return;
    using T = TFromD<D>;
    // Prevents error if size to allocate is zero.
    AlignedFreeUniquePtr<T[]> pa =
        AllocateAligned<T>(HWY_MAX(1, misalign_a + count + 1));
    AlignedFreeUniquePtr<T[]> expected = AllocateAligned<T>(HWY_MAX(1, count));
    HWY_ASSERT(pa && expected);

    const T sentinel = ConvertScalarTo<T>(-42);
    T* a = FillRandom(pa, count, misalign_a, sentinel, rng);
    SimpleSCAL(a, expected.get(), count);

    // TODO(janwas): can we update the apply_to in HWY_PUSH_ATTRIBUTES so that
    // the attribute also applies to lambdas? If so, remove HWY_ATTR.
#if HWY_GENERIC_LAMBDA
    const auto scal = [](const auto d, const auto v) HWY_ATTR {
      return Mul(Set(d, ConvertScalarTo<T>(kAlpha)), v);
    };
#else
    const SCAL scal;
#endif
    Transform(d, a, count, scal);

    const auto info = hwy::detail::MakeTypeInfo<T>();
    const char* target_name = hwy::TargetName(HWY_TARGET);
    hwy::detail::AssertArrayEqual(info, expected.get(), a, count, target_name,
                                  __FILE__, __LINE__);

    // Ensure no out-of-bound writes.
    for (size_t i = 0; i < misalign_a; ++i) {
      HWY_ASSERT_EQ(sentinel, pa[i]);
    }
    HWY_ASSERT_EQ(sentinel, a[count]);
  }
};

// One extra input array
struct TestTransform1 {
  template <class D>
  void operator()(D d, size_t count, size_t misalign_a, size_t misalign_b,
                  RandomState& rng) {
    using T = TFromD<D>;
    // Prevents error if size to allocate is zero.
    AlignedFreeUniquePtr<T[]> pa =
        AllocateAligned<T>(HWY_MAX(1, misalign_a + count + 1));
    AlignedFreeUniquePtr<T[]> pb =
        AllocateAligned<T>(HWY_MAX(1, misalign_b + count));
    AlignedFreeUniquePtr<T[]> expected = AllocateAligned<T>(HWY_MAX(1, count));
    HWY_ASSERT(pa && pb && expected);

    const T sentinel = ConvertScalarTo<T>(-42);
    T* a = FillRandom(pa, count, misalign_a, sentinel, rng);
    T* b = pb.get() + misalign_b;
    for (size_t i = 0; i < count; ++i) {
      b[i] = Random<T>(rng);
    }

    SimpleAXPY(a, b, expected.get(), count);

#if HWY_GENERIC_LAMBDA
    const auto axpy = [](const auto d, const auto v, const auto v1) HWY_ATTR {
      return MulAdd(Set(d, ConvertScalarTo<T>(kAlpha)), v, v1);
    };
#else
    const AXPY axpy;
#endif
    Transform1(d, a, count, b, axpy);

    AssertArraySimilar(expected.get(), a, count, hwy::TargetName(HWY_TARGET),
                       __FILE__, __LINE__);
    // Ensure no out-of-bound writes.
    for (size_t i = 0; i < misalign_a; ++i) {
      HWY_ASSERT_EQ(sentinel, pa[i]);
    }
    HWY_ASSERT_EQ(sentinel, a[count]);
  }
};

// Two extra input arrays
struct TestTransform2 {
  template <class D>
  void operator()(D d, size_t count, size_t misalign_a, size_t misalign_b,
                  RandomState& rng) {
    using T = TFromD<D>;
    // Prevents error if size to allocate is zero.
    AlignedFreeUniquePtr<T[]> pa =
        AllocateAligned<T>(HWY_MAX(1, misalign_a + count + 1));
    AlignedFreeUniquePtr<T[]> pb =
        AllocateAligned<T>(HWY_MAX(1, misalign_b + count));
    AlignedFreeUniquePtr<T[]> pc =
        AllocateAligned<T>(HWY_MAX(1, misalign_a + count));
    AlignedFreeUniquePtr<T[]> expected = AllocateAligned<T>(HWY_MAX(1, count));
    HWY_ASSERT(pa && pb && pc && expected);

    const T sentinel = ConvertScalarTo<T>(-42);
    T* a = FillRandom(pa, count, misalign_a, sentinel, rng);
    T* b = pb.get() + misalign_b;
    T* c = pc.get() + misalign_a;
    for (size_t i = 0; i < count; ++i) {
      b[i] = Random<T>(rng);
      c[i] = Random<T>(rng);
    }

    SimpleFMA4(a, b, c, expected.get(), count);

#if HWY_GENERIC_LAMBDA
    const auto fma4 = [](auto /*d*/, auto v, auto v1, auto v2)
                          HWY_ATTR { return MulAdd(v, v1, v2); };
#else
    const FMA4 fma4;
#endif
    Transform2(d, a, count, b, c, fma4);

    AssertArraySimilar(expected.get(), a, count, hwy::TargetName(HWY_TARGET),
                       __FILE__, __LINE__);
    // Ensure no out-of-bound writes.
    for (size_t i = 0; i < misalign_a; ++i) {
      HWY_ASSERT_EQ(sentinel, pa[i]);
    }
    HWY_ASSERT_EQ(sentinel, a[count]);
  }
};

template <typename T>
class IfEq {
 public:
  IfEq(T val) : val_(val) {}

  template <class D, class V>
  Mask<D> operator()(D d, V v) const {
    return Eq(v, Set(d, val_));
  }

 private:
  T val_;
};

struct TestReplace {
  template <class D>
  void operator()(D d, size_t count, size_t misalign_a, size_t misalign_b,
                  RandomState& rng) {
    if (misalign_b != 0) return;
    if (count == 0) return;
    using T = TFromD<D>;
    AlignedFreeUniquePtr<T[]> pa = AllocateAligned<T>(misalign_a + count + 1);
    AlignedFreeUniquePtr<T[]> pb = AllocateAligned<T>(count);
    AlignedFreeUniquePtr<T[]> expected = AllocateAligned<T>(count);
    HWY_ASSERT(pa && pb && expected);

    const T sentinel = ConvertScalarTo<T>(-42);
    T* a = FillRandom(pa, count, misalign_a, sentinel, rng);

    std::vector<size_t> positions(AdjustedReps(count));
    for (size_t& pos : positions) {
      pos = static_cast<size_t>(rng()) % count;
    }

    for (size_t pos = 0; pos < count; ++pos) {
      const T old_t = a[pos];
      const T new_t = Random<T>(rng);
      for (size_t i = 0; i < count; ++i) {
        expected[i] = IsEqual(a[i], old_t) ? new_t : a[i];
      }

      // Copy so ReplaceIf gets the same input (and thus also outputs expected)
      memcpy(pb.get(), a, count * sizeof(T));

      Replace(d, a, count, new_t, old_t);
      HWY_ASSERT_ARRAY_EQ(expected.get(), a, count);
      // Ensure no out-of-bound writes.
      for (size_t i = 0; i < misalign_a; ++i) {
        HWY_ASSERT_EQ(sentinel, pa[i]);
      }
      HWY_ASSERT_EQ(sentinel, a[count]);

      ReplaceIf(d, pb.get(), count, new_t, IfEq<T>(old_t));
      HWY_ASSERT_ARRAY_EQ(expected.get(), pb.get(), count);
      // Ensure no out-of-bound writes.
      for (size_t i = 0; i < misalign_a; ++i) {
        HWY_ASSERT_EQ(sentinel, pa[i]);
      }
      HWY_ASSERT_EQ(sentinel, a[count]);
    }
  }
};

void TestAllGenerate() {
  // The test BitCast-s the indices, which does not work for floats.
  ForIntegerTypes(ForPartialVectors<ForeachCountAndMisalign<TestGenerate>>());
}

void TestAllForeach() {
  ForAllTypes(ForPartialVectors<ForeachCountAndMisalign<TestForeach>>());
}

void TestAllTransform() {
  ForFloatTypes(ForPartialVectors<ForeachCountAndMisalign<TestTransform>>());
}

void TestAllTransform1() {
  ForFloatTypes(ForPartialVectors<ForeachCountAndMisalign<TestTransform1>>());
}

void TestAllTransform2() {
  ForFloatTypes(ForPartialVectors<ForeachCountAndMisalign<TestTransform2>>());
}

void TestAllReplace() {
  ForFloatTypes(ForPartialVectors<ForeachCountAndMisalign<TestReplace>>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(TransformTest);
HWY_EXPORT_AND_TEST_P(TransformTest, TestAllGenerate);
HWY_EXPORT_AND_TEST_P(TransformTest, TestAllForeach);
HWY_EXPORT_AND_TEST_P(TransformTest, TestAllTransform);
HWY_EXPORT_AND_TEST_P(TransformTest, TestAllTransform1);
HWY_EXPORT_AND_TEST_P(TransformTest, TestAllTransform2);
HWY_EXPORT_AND_TEST_P(TransformTest, TestAllReplace);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
