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

// clang-format off
#if defined(HIGHWAY_HWY_CONTRIB_DOT_DOT_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
// clang-format on
#ifdef HIGHWAY_HWY_CONTRIB_DOT_DOT_INL_H_
#undef HIGHWAY_HWY_CONTRIB_DOT_DOT_INL_H_
#else
#define HIGHWAY_HWY_CONTRIB_DOT_DOT_INL_H_
#endif

#include <stddef.h>

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

struct Dot {
  // Specify zero or more of these, ORed together, as the kAssumptions template
  // argument to Compute. Each one may improve performance or reduce code size,
  // at the cost of additional requirements on the arguments.
  enum Assumptions {
    // num_elements is at least N, which may be up to HWY_MAX_BYTES / sizeof(T).
    kAtLeastOneVector = 1,
    // num_elements is divisible by N (a power of two, so this can be used if
    // the problem size is known to be a power of two >= HWY_MAX_BYTES /
    // sizeof(T)).
    kMultipleOfVector = 2,
    // RoundUpTo(num_elements, N) elements are accessible; their value does not
    // matter (will be treated as if they were zero).
    kPaddedToVector = 4,
  };

  // Returns sum{pa[i] * pb[i]} for floating-point inputs, including float16_t
  // and double if HWY_HAVE_FLOAT16/64. Aligning the
  // pointers to a multiple of N elements is helpful but not required.
  template <int kAssumptions, class D, typename T = TFromD<D>>
  static HWY_INLINE T Compute(const D d, const T* const HWY_RESTRICT pa,
                              const T* const HWY_RESTRICT pb,
                              const size_t num_elements) {
    static_assert(IsFloat<T>(), "MulAdd requires float type");
    using V = decltype(Zero(d));

    const size_t N = Lanes(d);
    size_t i = 0;

    constexpr bool kIsAtLeastOneVector =
        (kAssumptions & kAtLeastOneVector) != 0;
    constexpr bool kIsMultipleOfVector =
        (kAssumptions & kMultipleOfVector) != 0;
    constexpr bool kIsPaddedToVector = (kAssumptions & kPaddedToVector) != 0;

    // Won't be able to do a full vector load without padding => scalar loop.
    if (!kIsAtLeastOneVector && !kIsMultipleOfVector && !kIsPaddedToVector &&
        HWY_UNLIKELY(num_elements < N)) {
      // Only 2x unroll to avoid excessive code size.
      T sum0 = ConvertScalarTo<T>(0);
      T sum1 = ConvertScalarTo<T>(0);
      for (; i + 2 <= num_elements; i += 2) {
        // For reasons unknown, fp16 += does not compile on clang (Arm).
        sum0 = ConvertScalarTo<T>(sum0 + pa[i + 0] * pb[i + 0]);
        sum1 = ConvertScalarTo<T>(sum1 + pa[i + 1] * pb[i + 1]);
      }
      if (i < num_elements) {
        sum1 = ConvertScalarTo<T>(sum1 + pa[i] * pb[i]);
      }
      return ConvertScalarTo<T>(sum0 + sum1);
    }

    // Compiler doesn't make independent sum* accumulators, so unroll manually.
    // 2 FMA ports * 4 cycle latency = up to 8 in-flight, but that is excessive
    // for unaligned inputs (each unaligned pointer halves the throughput
    // because it occupies both L1 load ports for a cycle). We cannot have
    // arrays of vectors on RVV/SVE, so always unroll 4x.
    V sum0 = Zero(d);
    V sum1 = Zero(d);
    V sum2 = Zero(d);
    V sum3 = Zero(d);

    // Main loop: unrolled
    for (; i + 4 * N <= num_elements; /* i += 4 * N */) {  // incr in loop
      const auto a0 = LoadU(d, pa + i);
      const auto b0 = LoadU(d, pb + i);
      i += N;
      sum0 = MulAdd(a0, b0, sum0);
      const auto a1 = LoadU(d, pa + i);
      const auto b1 = LoadU(d, pb + i);
      i += N;
      sum1 = MulAdd(a1, b1, sum1);
      const auto a2 = LoadU(d, pa + i);
      const auto b2 = LoadU(d, pb + i);
      i += N;
      sum2 = MulAdd(a2, b2, sum2);
      const auto a3 = LoadU(d, pa + i);
      const auto b3 = LoadU(d, pb + i);
      i += N;
      sum3 = MulAdd(a3, b3, sum3);
    }

    // Up to 3 iterations of whole vectors
    for (; i + N <= num_elements; i += N) {
      const auto a = LoadU(d, pa + i);
      const auto b = LoadU(d, pb + i);
      sum0 = MulAdd(a, b, sum0);
    }

    if (!kIsMultipleOfVector) {
      const size_t remaining = num_elements - i;
      if (remaining != 0) {
        if (kIsPaddedToVector) {
          const auto mask = FirstN(d, remaining);
          const auto a = LoadU(d, pa + i);
          const auto b = LoadU(d, pb + i);
          sum1 = MulAdd(IfThenElseZero(mask, a), IfThenElseZero(mask, b), sum1);
        } else {
          // Unaligned load such that the last element is in the highest lane -
          // ensures we do not touch any elements outside the valid range.
          // If we get here, then num_elements >= N.
          HWY_DASSERT(i >= N);
          i += remaining - N;
          const auto skip = FirstN(d, N - remaining);
          const auto a = LoadU(d, pa + i);  // always unaligned
          const auto b = LoadU(d, pb + i);
          sum1 = MulAdd(IfThenZeroElse(skip, a), IfThenZeroElse(skip, b), sum1);
        }
      }
    }  // kMultipleOfVector

    // Reduction tree: sum of all accumulators by pairs, then across lanes.
    sum0 = Add(sum0, sum1);
    sum2 = Add(sum2, sum3);
    sum0 = Add(sum0, sum2);
    return ReduceSum(d, sum0);
  }

  // f32 * bf16
  template <int kAssumptions, class DF, HWY_IF_F32_D(DF)>
  static HWY_INLINE float Compute(const DF df,
                                  const float* const HWY_RESTRICT pa,
                                  const hwy::bfloat16_t* const HWY_RESTRICT pb,
                                  const size_t num_elements) {
#if HWY_TARGET == HWY_SCALAR
    const Rebind<hwy::bfloat16_t, DF> dbf;
#else
    const Repartition<hwy::bfloat16_t, DF> dbf;
    using VBF = decltype(Zero(dbf));
#endif
    const Half<decltype(dbf)> dbfh;
    using VF = decltype(Zero(df));

    const size_t NF = Lanes(df);

    constexpr bool kIsAtLeastOneVector =
        (kAssumptions & kAtLeastOneVector) != 0;
    constexpr bool kIsMultipleOfVector =
        (kAssumptions & kMultipleOfVector) != 0;
    constexpr bool kIsPaddedToVector = (kAssumptions & kPaddedToVector) != 0;

    // Won't be able to do a full vector load without padding => scalar loop.
    if (!kIsAtLeastOneVector && !kIsMultipleOfVector && !kIsPaddedToVector &&
        HWY_UNLIKELY(num_elements < NF)) {
      // Only 2x unroll to avoid excessive code size.
      float sum0 = 0.0f;
      float sum1 = 0.0f;
      size_t i = 0;
      for (; i + 2 <= num_elements; i += 2) {
        sum0 += pa[i + 0] * ConvertScalarTo<float>(pb[i + 0]);
        sum1 += pa[i + 1] * ConvertScalarTo<float>(pb[i + 1]);
      }
      for (; i < num_elements; ++i) {
        sum1 += pa[i] * ConvertScalarTo<float>(pb[i]);
      }
      return sum0 + sum1;
    }

    // Compiler doesn't make independent sum* accumulators, so unroll manually.
    // 2 FMA ports * 4 cycle latency = up to 8 in-flight, but that is excessive
    // for unaligned inputs (each unaligned pointer halves the throughput
    // because it occupies both L1 load ports for a cycle). We cannot have
    // arrays of vectors on RVV/SVE, so always unroll 4x.
    VF sum0 = Zero(df);
    VF sum1 = Zero(df);
    VF sum2 = Zero(df);
    VF sum3 = Zero(df);

    size_t i = 0;

#if HWY_TARGET != HWY_SCALAR  // PromoteUpperTo supported
    // Main loop: unrolled
    for (; i + 4 * NF <= num_elements; /* i += 4 * N */) {  // incr in loop
      const VF a0 = LoadU(df, pa + i);
      const VBF b0 = LoadU(dbf, pb + i);
      i += NF;
      sum0 = MulAdd(a0, PromoteLowerTo(df, b0), sum0);
      const VF a1 = LoadU(df, pa + i);
      i += NF;
      sum1 = MulAdd(a1, PromoteUpperTo(df, b0), sum1);
      const VF a2 = LoadU(df, pa + i);
      const VBF b2 = LoadU(dbf, pb + i);
      i += NF;
      sum2 = MulAdd(a2, PromoteLowerTo(df, b2), sum2);
      const VF a3 = LoadU(df, pa + i);
      i += NF;
      sum3 = MulAdd(a3, PromoteUpperTo(df, b2), sum3);
    }
#endif  // HWY_TARGET == HWY_SCALAR

    // Up to 3 iterations of whole vectors
    for (; i + NF <= num_elements; i += NF) {
      const VF a = LoadU(df, pa + i);
      const VF b = PromoteTo(df, LoadU(dbfh, pb + i));
      sum0 = MulAdd(a, b, sum0);
    }

    if (!kIsMultipleOfVector) {
      const size_t remaining = num_elements - i;
      if (remaining != 0) {
        if (kIsPaddedToVector) {
          const auto mask = FirstN(df, remaining);
          const VF a = LoadU(df, pa + i);
          const VF b = PromoteTo(df, LoadU(dbfh, pb + i));
          sum1 = MulAdd(IfThenElseZero(mask, a), IfThenElseZero(mask, b), sum1);
        } else {
          // Unaligned load such that the last element is in the highest lane -
          // ensures we do not touch any elements outside the valid range.
          // If we get here, then num_elements >= N.
          HWY_DASSERT(i >= NF);
          i += remaining - NF;
          const auto skip = FirstN(df, NF - remaining);
          const VF a = LoadU(df, pa + i);  // always unaligned
          const VF b = PromoteTo(df, LoadU(dbfh, pb + i));
          sum1 = MulAdd(IfThenZeroElse(skip, a), IfThenZeroElse(skip, b), sum1);
        }
      }
    }  // kMultipleOfVector

    // Reduction tree: sum of all accumulators by pairs, then across lanes.
    sum0 = Add(sum0, sum1);
    sum2 = Add(sum2, sum3);
    sum0 = Add(sum0, sum2);
    return ReduceSum(df, sum0);
  }

  // Returns sum{pa[i] * pb[i]} for bfloat16 inputs. Aligning the pointers to a
  // multiple of N elements is helpful but not required.
  template <int kAssumptions, class D, HWY_IF_BF16_D(D)>
  static HWY_INLINE float Compute(const D d,
                                  const bfloat16_t* const HWY_RESTRICT pa,
                                  const bfloat16_t* const HWY_RESTRICT pb,
                                  const size_t num_elements) {
    const RebindToUnsigned<D> du16;
    const Repartition<float, D> df32;

    using V = decltype(Zero(df32));
    const size_t N = Lanes(d);
    size_t i = 0;

    constexpr bool kIsAtLeastOneVector =
        (kAssumptions & kAtLeastOneVector) != 0;
    constexpr bool kIsMultipleOfVector =
        (kAssumptions & kMultipleOfVector) != 0;
    constexpr bool kIsPaddedToVector = (kAssumptions & kPaddedToVector) != 0;

    // Won't be able to do a full vector load without padding => scalar loop.
    if (!kIsAtLeastOneVector && !kIsMultipleOfVector && !kIsPaddedToVector &&
        HWY_UNLIKELY(num_elements < N)) {
      float sum0 = 0.0f;  // Only 2x unroll to avoid excessive code size for..
      float sum1 = 0.0f;  // this unlikely(?) case.
      for (; i + 2 <= num_elements; i += 2) {
        sum0 += F32FromBF16(pa[i + 0]) * F32FromBF16(pb[i + 0]);
        sum1 += F32FromBF16(pa[i + 1]) * F32FromBF16(pb[i + 1]);
      }
      if (i < num_elements) {
        sum1 += F32FromBF16(pa[i]) * F32FromBF16(pb[i]);
      }
      return sum0 + sum1;
    }

    // See comment in the other Compute() overload. Unroll 2x, but we need
    // twice as many sums for ReorderWidenMulAccumulate.
    V sum0 = Zero(df32);
    V sum1 = Zero(df32);
    V sum2 = Zero(df32);
    V sum3 = Zero(df32);

    // Main loop: unrolled
    for (; i + 2 * N <= num_elements; /* i += 2 * N */) {  // incr in loop
      const auto a0 = LoadU(d, pa + i);
      const auto b0 = LoadU(d, pb + i);
      i += N;
      sum0 = ReorderWidenMulAccumulate(df32, a0, b0, sum0, sum1);
      const auto a1 = LoadU(d, pa + i);
      const auto b1 = LoadU(d, pb + i);
      i += N;
      sum2 = ReorderWidenMulAccumulate(df32, a1, b1, sum2, sum3);
    }

    // Possibly one more iteration of whole vectors
    if (i + N <= num_elements) {
      const auto a0 = LoadU(d, pa + i);
      const auto b0 = LoadU(d, pb + i);
      i += N;
      sum0 = ReorderWidenMulAccumulate(df32, a0, b0, sum0, sum1);
    }

    if (!kIsMultipleOfVector) {
      const size_t remaining = num_elements - i;
      if (remaining != 0) {
        if (kIsPaddedToVector) {
          const auto mask = FirstN(du16, remaining);
          const auto va = LoadU(d, pa + i);
          const auto vb = LoadU(d, pb + i);
          const auto a16 = BitCast(d, IfThenElseZero(mask, BitCast(du16, va)));
          const auto b16 = BitCast(d, IfThenElseZero(mask, BitCast(du16, vb)));
          sum2 = ReorderWidenMulAccumulate(df32, a16, b16, sum2, sum3);

        } else {
          // Unaligned load such that the last element is in the highest lane -
          // ensures we do not touch any elements outside the valid range.
          // If we get here, then num_elements >= N.
          HWY_DASSERT(i >= N);
          i += remaining - N;
          const auto skip = FirstN(du16, N - remaining);
          const auto va = LoadU(d, pa + i);  // always unaligned
          const auto vb = LoadU(d, pb + i);
          const auto a16 = BitCast(d, IfThenZeroElse(skip, BitCast(du16, va)));
          const auto b16 = BitCast(d, IfThenZeroElse(skip, BitCast(du16, vb)));
          sum2 = ReorderWidenMulAccumulate(df32, a16, b16, sum2, sum3);
        }
      }
    }  // kMultipleOfVector

    // Reduction tree: sum of all accumulators by pairs, then across lanes.
    sum0 = Add(sum0, sum1);
    sum2 = Add(sum2, sum3);
    sum0 = Add(sum0, sum2);
    return ReduceSum(df32, sum0);
  }
};

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_CONTRIB_DOT_DOT_INL_H_
