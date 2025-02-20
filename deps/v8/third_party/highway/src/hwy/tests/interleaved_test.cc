// Copyright 2019 Google LLC
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

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/interleaved_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestLoadStoreInterleaved2 {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);

    RandomState rng;

    constexpr size_t kVectors = 2;
    // Data to be interleaved
    auto in = AllocateAligned<T>(kVectors * N);
    // Ensure unaligned; kVectors plus one zero vector.
    auto actual_aligned = AllocateAligned<T>((kVectors + 1) * N + 1);
    HWY_ASSERT(in && actual_aligned);

    for (size_t i = 0; i < kVectors * N; ++i) {
      in[i] = ConvertScalarTo<T>(Random32(&rng) & 0x7F);
    }
    const Vec<D> in0 = Load(d, &in[0 * N]);
    const Vec<D> in1 = Load(d, &in[1 * N]);

    T* actual = actual_aligned.get() + 1;
    StoreInterleaved2(in0, in1, d, actual);
    StoreU(Zero(d), d, actual + kVectors * N);

    Vec<D> out0, out1;
    LoadInterleaved2(d, actual, out0, out1);
    HWY_ASSERT_VEC_EQ(d, in0, out0);
    HWY_ASSERT_VEC_EQ(d, in1, out1);
    HWY_ASSERT_VEC_EQ(d, Zero(d), LoadU(d, actual + kVectors * N));
  }
};

HWY_NOINLINE void TestAllLoadStoreInterleaved2() {
  ForAllTypes(ForMaxPow2<TestLoadStoreInterleaved2>());
  // Temporarily disable this test for special floats on arm7.
#ifndef HWY_ARCH_ARM_V7
  ForSpecialTypes(ForMaxPow2<TestLoadStoreInterleaved2>());
#endif
}

// Workaround for build timeout on GCC 12 aarch64, see #776.
#undef HWY_BROKEN_LOAD34
#if HWY_ARCH_ARM_A64 && HWY_COMPILER_GCC_ACTUAL && \
    HWY_COMPILER_GCC_ACTUAL < 1300
#define HWY_BROKEN_LOAD34 1
#else
#define HWY_BROKEN_LOAD34 0
#endif

struct TestLoadStoreInterleaved3 {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_BROKEN_LOAD34
    (void)d;
#else   // !HWY_BROKEN_LOAD34
    const size_t N = Lanes(d);

    RandomState rng;

    constexpr size_t kVectors = 3;
    // Data to be interleaved
    auto in = AllocateAligned<T>(kVectors * N);
    // Ensure unaligned; kVectors plus one zero vector.
    auto actual_aligned = AllocateAligned<T>((kVectors + 1) * N + 1);
    HWY_ASSERT(in && actual_aligned);

    for (size_t i = 0; i < kVectors * N; ++i) {
      in[i] = ConvertScalarTo<T>(Random32(&rng) & 0x7F);
    }
    const Vec<D> in0 = Load(d, &in[0 * N]);
    const Vec<D> in1 = Load(d, &in[1 * N]);
    const Vec<D> in2 = Load(d, &in[2 * N]);

    T* actual = actual_aligned.get() + 1;
    StoreInterleaved3(in0, in1, in2, d, actual);
    StoreU(Zero(d), d, actual + kVectors * N);

    Vec<D> out0, out1, out2;
    LoadInterleaved3(d, actual, out0, out1, out2);
    HWY_ASSERT_VEC_EQ(d, in0, out0);
    HWY_ASSERT_VEC_EQ(d, in1, out1);
    HWY_ASSERT_VEC_EQ(d, in2, out2);
    HWY_ASSERT_VEC_EQ(d, Zero(d), LoadU(d, actual + kVectors * N));
#endif  // HWY_BROKEN_LOAD34
  }
};

HWY_NOINLINE void TestAllLoadStoreInterleaved3() {
  ForAllTypes(ForMaxPow2<TestLoadStoreInterleaved3>());
  // Temporarily disable this test for special floats on arm7.
#ifndef HWY_ARCH_ARM_V7
  ForSpecialTypes(ForMaxPow2<TestLoadStoreInterleaved3>());
#endif
}

struct TestLoadStoreInterleaved4 {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_BROKEN_LOAD34
    (void)d;
#else   // !HWY_BROKEN_LOAD34
    const size_t N = Lanes(d);

    RandomState rng;

    constexpr size_t kVectors = 4;
    // Data to be interleaved
    auto in = AllocateAligned<T>(kVectors * N);
    // Ensure unaligned; kVectors plus one zero vector.
    auto actual_aligned = AllocateAligned<T>((kVectors + 1) * N + 1);
    HWY_ASSERT(in && actual_aligned);

    for (size_t i = 0; i < kVectors * N; ++i) {
      in[i] = ConvertScalarTo<T>(Random32(&rng) & 0x7F);
    }
    const Vec<D> in0 = Load(d, &in[0 * N]);
    const Vec<D> in1 = Load(d, &in[1 * N]);
    const Vec<D> in2 = Load(d, &in[2 * N]);
    const Vec<D> in3 = Load(d, &in[3 * N]);

    T* actual = actual_aligned.get() + 1;
    StoreInterleaved4(in0, in1, in2, in3, d, actual);
    StoreU(Zero(d), d, actual + kVectors * N);

    Vec<D> out0, out1, out2, out3;
    LoadInterleaved4(d, actual, out0, out1, out2, out3);
    HWY_ASSERT_VEC_EQ(d, in0, out0);
    HWY_ASSERT_VEC_EQ(d, in1, out1);
    HWY_ASSERT_VEC_EQ(d, in2, out2);
    HWY_ASSERT_VEC_EQ(d, in3, out3);
    HWY_ASSERT_VEC_EQ(d, Zero(d), LoadU(d, actual + kVectors * N));
#endif  // HWY_BROKEN_LOAD34
  }
};

HWY_NOINLINE void TestAllLoadStoreInterleaved4() {
  ForAllTypes(ForMaxPow2<TestLoadStoreInterleaved4>());
  // Temporarily disable this test for special floats on arm7.
#ifndef HWY_ARCH_ARM_V7
  ForSpecialTypes(ForMaxPow2<TestLoadStoreInterleaved4>());
#endif
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyInterleavedTest);
HWY_EXPORT_AND_TEST_P(HwyInterleavedTest, TestAllLoadStoreInterleaved2);
HWY_EXPORT_AND_TEST_P(HwyInterleavedTest, TestAllLoadStoreInterleaved3);
HWY_EXPORT_AND_TEST_P(HwyInterleavedTest, TestAllLoadStoreInterleaved4);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
