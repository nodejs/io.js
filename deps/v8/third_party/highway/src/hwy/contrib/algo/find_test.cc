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

#include <stdio.h>

#include <algorithm>  // std::find_if
#include <vector>

#include "hwy/aligned_allocator.h"
#include "hwy/base.h"
#include "hwy/print.h"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/algo/find_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/contrib/algo/find-inl.h"
#include "hwy/tests/test_util-inl.h"
// clang-format on

// If your project requires C++14 or later, you can ignore this and pass lambdas
// directly to FindIf, without requiring an lvalue as we do here for C++11.
#if __cplusplus < 201402L
#define HWY_GENERIC_LAMBDA 0
#else
#define HWY_GENERIC_LAMBDA 1
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

// Returns random number in [-8, 8] - we use knowledge of the range to Find()
// values we know are not present.
template <typename T>
T Random(RandomState& rng) {
  const int32_t bits = static_cast<int32_t>(Random32(&rng)) & 1023;
  double val = (bits - 512) / 64.0;
  // Clamp negative to zero for unsigned types.
  if (!hwy::IsSigned<T>() && val < 0.0) {
    val = -val;
  }
  return ConvertScalarTo<T>(val);
}

// In C++14, we can instead define these as generic lambdas next to where they
// are invoked.
#if !HWY_GENERIC_LAMBDA

class GreaterThan {
 public:
  GreaterThan(int val) : val_(val) {}
  template <class D, class V>
  Mask<D> operator()(D d, V v) const {
    return Gt(v, Set(d, ConvertScalarTo<TFromD<D>>(val_)));
  }

 private:
  int val_;
};

#endif  // !HWY_GENERIC_LAMBDA

// Invokes Test (e.g. TestFind) with all arg combinations.
template <class Test>
struct ForeachCountAndMisalign {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) const {
    RandomState rng;
    const size_t N = Lanes(d);
    const size_t misalignments[3] = {0, N / 4, 3 * N / 5};

    // Find() checks 8 vectors at a time, so we want to cover a fairly large
    // range without oversampling (checking every possible count).
    std::vector<size_t> counts(AdjustedReps(512));
    for (size_t& count : counts) {
      count = static_cast<size_t>(rng()) % (16 * N + 1);
    }
    counts[0] = 0;  // ensure we test count=0.

    for (size_t count : counts) {
      for (size_t m : misalignments) {
        Test()(d, count, m, rng);
      }
    }
  }
};

struct TestFind {
  template <class D>
  void operator()(D d, size_t count, size_t misalign, RandomState& rng) {
    using T = TFromD<D>;
    // Must allocate at least one even if count is zero.
    AlignedFreeUniquePtr<T[]> storage =
        AllocateAligned<T>(HWY_MAX(1, misalign + count));
    HWY_ASSERT(storage);
    T* in = storage.get() + misalign;
    for (size_t i = 0; i < count; ++i) {
      in[i] = Random<T>(rng);
    }

    // For each position, search for that element (which we know is there)
    for (size_t pos = 0; pos < count; ++pos) {
      const size_t actual = Find(d, in[pos], in, count);

      // We may have found an earlier occurrence of the same value; ensure the
      // value is the same, and that it is the first.
      if (!IsEqual(in[pos], in[actual])) {
        fprintf(stderr, "%s count %d, found %.15f at %d but wanted %.15f\n",
                hwy::TypeName(T(), Lanes(d)).c_str(), static_cast<int>(count),
                ConvertScalarTo<double>(in[actual]), static_cast<int>(actual),
                ConvertScalarTo<double>(in[pos]));
        HWY_ASSERT(false);
      }
      for (size_t i = 0; i < actual; ++i) {
        if (IsEqual(in[i], in[pos])) {
          fprintf(stderr, "%s count %d, found %f at %d but Find returned %d\n",
                  hwy::TypeName(T(), Lanes(d)).c_str(), static_cast<int>(count),
                  ConvertScalarTo<double>(in[i]), static_cast<int>(i),
                  static_cast<int>(actual));
          HWY_ASSERT(false);
        }
      }
    }

    // Also search for values we know not to be present (out of range)
    HWY_ASSERT_EQ(count, Find(d, ConvertScalarTo<T>(9), in, count));
    HWY_ASSERT_EQ(count, Find(d, ConvertScalarTo<T>(-9), in, count));
  }
};

void TestAllFind() {
  ForAllTypes(ForPartialVectors<ForeachCountAndMisalign<TestFind>>());
}

struct TestFindIf {
  template <class D>
  void operator()(D d, size_t count, size_t misalign, RandomState& rng) {
    using T = TFromD<D>;
    using TI = MakeSigned<T>;
    // Must allocate at least one even if count is zero.
    AlignedFreeUniquePtr<T[]> storage =
        AllocateAligned<T>(HWY_MAX(1, misalign + count));
    HWY_ASSERT(storage);
    T* in = storage.get() + misalign;
    for (size_t i = 0; i < count; ++i) {
      in[i] = Random<T>(rng);
      HWY_ASSERT(ConvertScalarTo<TI>(in[i]) <= 8);
      HWY_ASSERT(!hwy::IsSigned<T>() || ConvertScalarTo<TI>(in[i]) >= -8);
    }

    bool found_any = false;
    bool not_found_any = false;

    // unsigned T would be promoted to signed and compare greater than any
    // negative val, whereas Set() would just cast to an unsigned value and the
    // comparison remains unsigned, so avoid negative numbers there.
    const int min_val = IsSigned<T>() ? -9 : 0;
    // Includes out-of-range value 9 to test the not-found path.
    for (int val = min_val; val <= 9; ++val) {
#if HWY_GENERIC_LAMBDA
      const auto greater = [val](const auto d, const auto v) HWY_ATTR {
        return Gt(v, Set(d, ConvertScalarTo<T>(val)));
      };
#else
      const GreaterThan greater(val);
#endif
      const size_t actual = FindIf(d, in, count, greater);
      found_any |= actual < count;
      not_found_any |= actual == count;

      const auto pos = std::find_if(
          in, in + count, [val](T x) { return x > ConvertScalarTo<T>(val); });
      // Convert returned iterator to index.
      const size_t expected = static_cast<size_t>(pos - in);
      if (expected != actual) {
        fprintf(stderr, "%s count %d val %d, expected %d actual %d\n",
                hwy::TypeName(T(), Lanes(d)).c_str(), static_cast<int>(count),
                val, static_cast<int>(expected), static_cast<int>(actual));
        hwy::detail::PrintArray(hwy::detail::MakeTypeInfo<T>(), "in", in, count,
                                0, count);
        HWY_ASSERT(false);
      }
    }

    // We will always not-find something due to val=9.
    HWY_ASSERT(not_found_any);
    // We'll find something unless the input is empty or {0} - because 0 > i
    // is false for all i=[0,9].
    if (count != 0 && in[0] != ConvertScalarTo<T>(0)) {
      HWY_ASSERT(found_any);
    }
  }
};

void TestAllFindIf() {
  ForAllTypes(ForPartialVectors<ForeachCountAndMisalign<TestFindIf>>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(FindTest);
HWY_EXPORT_AND_TEST_P(FindTest, TestAllFind);
HWY_EXPORT_AND_TEST_P(FindTest, TestAllFindIf);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
