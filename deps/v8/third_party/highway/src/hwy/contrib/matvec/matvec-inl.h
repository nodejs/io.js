// Copyright 2023 Google LLC
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

// Include guard (still compiled once per target)
#if defined(HIGHWAY_HWY_CONTRIB_MATVEC_MATVEC_INL_H_) == \
    defined(HWY_TARGET_TOGGLE)
#ifdef HIGHWAY_HWY_CONTRIB_MATVEC_MATVEC_INL_H_
#undef HIGHWAY_HWY_CONTRIB_MATVEC_MATVEC_INL_H_
#else
#define HIGHWAY_HWY_CONTRIB_MATVEC_MATVEC_INL_H_
#endif

#include "hwy/cache_control.h"
#include "hwy/contrib/thread_pool/thread_pool.h"
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

template <typename TA, typename TB>
TA AddScalar(TA a, TB b) {
  return ConvertScalarTo<TA>(ConvertScalarTo<float>(a) +
                             ConvertScalarTo<float>(b));
}

template <size_t kOuter, size_t kInner, typename T, bool kAdd>
HWY_NOINLINE void MatVecAddImpl(const T* HWY_RESTRICT mat,
                                const T* HWY_RESTRICT vec,
                                const T* HWY_RESTRICT add, T* HWY_RESTRICT out,
                                hwy::ThreadPool& pool) {
  (void)add;

  // Process multiple rows at a time so that we write multiples of a cache line
  // to avoid false sharing (>= 64). 128 is better than 256. 512 has too little
  // parallelization potential.
  constexpr size_t kChunkSize = 64 / sizeof(T);
  const uint64_t num_chunks = static_cast<uint64_t>(kOuter / kChunkSize);

  const ScalableTag<T> d;
  const size_t N = Lanes(d);
  // Required for Stream loop, otherwise we might have partial vectors.
  HWY_DASSERT(kChunkSize >= N);
  pool.Run(0, num_chunks,
           [&](const uint64_t chunk, size_t /*thread*/) HWY_ATTR {
             // MSVC workaround: duplicate to ensure constexpr.
             constexpr size_t kChunkSize = 64 / sizeof(T);
             // Software write-combining to avoid cache pollution from out.
             // Although `out` may be used later, keeping it out of the cache
             // now and avoiding RFOs is a consistent 5% overall win.
             HWY_ALIGN T buf[kChunkSize];

             // Only handle entire chunks here because the Stream is not masked.
             // Remaining rows are handled after the pool.Run.
             const size_t begin = static_cast<size_t>(chunk * kChunkSize);
             for (size_t idx_row = 0; idx_row < kChunkSize; ++idx_row) {
               auto sum0 = Zero(d);
               auto sum1 = Zero(d);
               // 4x unrolling barely helps SKX but likely helps Arm V2.
               auto sum2 = Zero(d);
               auto sum3 = Zero(d);

               const T* HWY_RESTRICT row = &mat[(begin + idx_row) * kInner];
               size_t i = 0;
               // No clear win from prefetching from the next 1..3 rows.
               // clflush &row[i] is slow, clflushopt less so but not helping.
               HWY_UNROLL(1)
               for (; i + 4 * N <= kInner; i += 4 * N) {
                 const auto a0 = LoadU(d, row + i + 0 * N);
                 const auto v0 = LoadU(d, vec + i + 0 * N);
                 sum0 = MulAdd(a0, v0, sum0);

                 const auto a1 = LoadU(d, row + i + 1 * N);
                 const auto v1 = LoadU(d, vec + i + 1 * N);
                 sum1 = MulAdd(a1, v1, sum1);

                 const auto a2 = LoadU(d, row + i + 2 * N);
                 const auto v2 = LoadU(d, vec + i + 2 * N);
                 sum2 = MulAdd(a2, v2, sum2);

                 const auto a3 = LoadU(d, row + i + 3 * N);
                 const auto v3 = LoadU(d, vec + i + 3 * N);
                 sum3 = MulAdd(a3, v3, sum3);
               }
               // Last entire vectors
               for (; i + N <= kInner; i += N) {
                 const auto a0 = LoadU(d, row + i);
                 const auto v0 = LoadU(d, vec + i);
                 sum0 = MulAdd(a0, v0, sum0);
               }
               const size_t remainder = kInner - i;
               if (remainder != 0) {
                 const auto a0 = LoadN(d, row + i, remainder);
                 const auto v0 = LoadN(d, vec + i, remainder);
                 sum1 = MulAdd(a0, v0, sum1);
               }
               // Reduction tree: sum of all accumulators, then their lanes
               sum2 = Add(sum2, sum3);
               sum0 = Add(sum0, sum1);
               sum0 = Add(sum0, sum2);
               buf[idx_row] = ReduceSum(d, sum0);
               HWY_IF_CONSTEXPR(kAdd) {
                 buf[idx_row] = AddScalar(buf[idx_row], add[begin + idx_row]);
               }
             }  // idx_row
             HWY_UNROLL(4)  // 1..4 iterations
             for (size_t i = 0; i != kChunkSize; i += N) {
               Stream(Load(d, buf + i), d, out + begin + i);
             }
           });
  hwy::FlushStream();

  // Handle remainder rows which are not a multiple of the chunk size.
  for (size_t r = num_chunks * kChunkSize; r < kOuter; ++r) {
    auto sum0 = Zero(d);

    const T* HWY_RESTRICT row = &mat[r * kInner];
    size_t i = 0;
    HWY_UNROLL(1)
    for (; i + N <= kInner; i += N) {
      const auto a0 = LoadU(d, row + i);
      const auto v0 = LoadU(d, vec + i);
      sum0 = MulAdd(a0, v0, sum0);
    }
    const size_t remainder = kInner - i;
    if (remainder != 0) {
      const auto a0 = LoadN(d, row + i, remainder);
      const auto v0 = LoadN(d, vec + i, remainder);
      sum0 = MulAdd(a0, v0, sum0);
    }
    out[r] = ReduceSum(d, sum0);
    HWY_IF_CONSTEXPR(kAdd) { out[r] = AddScalar(out[r], add[r]); }
  }  // r
}

// Multiplies mat with vec, adds add and puts the result in out.
//
// mat is a (kOuter, kInner)-shaped array, where element [i,j] is located at
// index i * kInner + j.
//
// vec is a (kInner,)-shaped array.
//
// add is a (kOuter,)-shaped array.
//
// out is a (kOuter,)-shaped array that will set to mat @ vec + add.
template <size_t kOuter, size_t kInner, typename T>
HWY_NOINLINE void MatVecAdd(const T* HWY_RESTRICT mat,
                            const T* HWY_RESTRICT vec,
                            const T* HWY_RESTRICT add, T* HWY_RESTRICT out,
                            hwy::ThreadPool& pool) {
  MatVecAddImpl<kOuter, kInner, T, true>(mat, vec, add, out, pool);
}

// Multiplies mat with vec and puts the result in out.
//
// mat is a (kOuter, kInner)-shaped array, where element [i,j] is located at
// index i * kInner + j.
//
// vec is a (kInner,)-shaped array.
//
// out is a (kOuter,)-shaped array that will set to mat @ vec.
template <size_t kOuter, size_t kInner, typename T>
HWY_NOINLINE void MatVec(const T* HWY_RESTRICT mat, const T* HWY_RESTRICT vec,
                         T* HWY_RESTRICT out, hwy::ThreadPool& pool) {
  MatVecAddImpl<kOuter, kInner, T, false>(mat, vec, /*add=*/nullptr, out, pool);
}

// This target lacks too many ops required in our implementation, use
// HWY_EMU128 instead.
#if HWY_TARGET != HWY_SCALAR

// Specialization for bf16 matrix, which halves memory bandwidth requirements.
template <size_t kOuter, size_t kInner, bool kAdd>
HWY_NOINLINE void MatVecAddImpl(const hwy::bfloat16_t* HWY_RESTRICT mat,
                                const float* HWY_RESTRICT vec,
                                const float* HWY_RESTRICT add,
                                float* HWY_RESTRICT out,
                                hwy::ThreadPool& pool) {
  // Process multiple rows at a time so that we write multiples of a cache line
  // to avoid false sharing (>= 64). 128 is better than 256. 512 has too little
  // parallelization potential.
  constexpr size_t kChunkSize = 64 / sizeof(float);
  const uint64_t num_chunks = static_cast<uint64_t>(kOuter / kChunkSize);

  const ScalableTag<float> d;
  const Repartition<hwy::bfloat16_t, decltype(d)> d16;
  // In the remainder loop, we only process a single f32 vector, so load half
  // vectors of bf16 to avoid overrun.
  const Half<decltype(d16)> d16h;
  using V = Vec<decltype(d)>;
  using V16 = Vec<decltype(d16)>;
  using V16H = Vec<decltype(d16h)>;
  const size_t N = Lanes(d);
  // Required for Stream loop, otherwise we might have partial vectors.
  HWY_DASSERT(kChunkSize >= N);
  pool.Run(0, num_chunks,
           [&](const uint64_t chunk, size_t /*thread*/) HWY_ATTR {
             // MSVC workaround: duplicate to ensure constexpr.
             constexpr size_t kChunkSize = 64 / sizeof(float);
             // Software write-combining to avoid cache pollution from out.
             // Although `out` may be used later, keeping it out of the cache
             // now and avoiding RFOs is a consistent 5% overall win.
             HWY_ALIGN float buf[kChunkSize];

             // Only handle entire chunks here because the Stream is not masked.
             // Remaining rows are handled after the pool.Run.
             const size_t begin = static_cast<size_t>(chunk * kChunkSize);
             for (size_t idx_row = 0; idx_row < kChunkSize; ++idx_row) {
               auto sum0 = Zero(d);
               auto sum1 = Zero(d);
               // 4x unrolling barely helps SKX but likely helps Arm V2.
               auto sum2 = Zero(d);
               auto sum3 = Zero(d);

               const hwy::bfloat16_t* HWY_RESTRICT row =
                   &mat[(begin + idx_row) * kInner];
               size_t i = 0;
               // No clear win from prefetching from the next 1..3 rows.
               // clflush &row[i] is slow, clflushopt less so but not helping.
               HWY_UNROLL(1)
               for (; i + 4 * N <= kInner; i += 4 * N) {
                 const V16 b0 = LoadU(d16, row + i + 0 * N);
                 const V a0 = PromoteLowerTo(d, b0);
                 const V a1 = PromoteUpperTo(d, b0);

                 const V16 b1 = LoadU(d16, row + i + 2 * N);
                 const V a2 = PromoteLowerTo(d, b1);
                 const V a3 = PromoteUpperTo(d, b1);

                 const V v0 = LoadU(d, vec + i + 0 * N);
                 sum0 = MulAdd(a0, v0, sum0);

                 const V v1 = LoadU(d, vec + i + 1 * N);
                 sum1 = MulAdd(a1, v1, sum1);

                 const V v2 = LoadU(d, vec + i + 2 * N);
                 sum2 = MulAdd(a2, v2, sum2);

                 const V v3 = LoadU(d, vec + i + 3 * N);
                 sum3 = MulAdd(a3, v3, sum3);
               }
               // Last entire vectors
               for (; i + N <= kInner; i += N) {
                 const V16H b0 = LoadU(d16h, row + i);
                 const V a0 = PromoteTo(d, b0);
                 const V v0 = LoadU(d, vec + i);
                 sum0 = MulAdd(a0, v0, sum0);
               }
               const size_t remainder = kInner - i;
               if (remainder != 0) {
                 const V16H b0 = LoadN(d16h, row + i, remainder);
                 const V a0 = PromoteTo(d, b0);
                 const V v0 = LoadN(d, vec + i, remainder);
                 sum1 = MulAdd(a0, v0, sum1);
               }
               // Reduction tree: sum of all accumulators, then their lanes
               sum2 = Add(sum2, sum3);
               sum0 = Add(sum0, sum1);
               sum0 = Add(sum0, sum2);
               buf[idx_row] = ReduceSum(d, sum0);
               HWY_IF_CONSTEXPR(kAdd) {
                 buf[idx_row] = AddScalar(buf[idx_row], add[begin + idx_row]);
               }
             }  // idx_row
             HWY_UNROLL(4)  // 1..4 iterations
             for (size_t i = 0; i != kChunkSize; i += N) {
               Stream(Load(d, buf + i), d, out + begin + i);
             }
           });
  hwy::FlushStream();

  // Handle remainder rows which are not a multiple of the chunk size.
  for (size_t r = num_chunks * kChunkSize; r < kOuter; ++r) {
    auto sum0 = Zero(d);

    const hwy::bfloat16_t* HWY_RESTRICT row = &mat[r * kInner];
    size_t i = 0;
    HWY_UNROLL(1)
    for (; i + N <= kInner; i += N) {
      const V16H b0 = LoadU(d16h, row + i);
      const V a0 = PromoteTo(d, b0);
      const V v0 = LoadU(d, vec + i);
      sum0 = MulAdd(a0, v0, sum0);
    }
    const size_t remainder = kInner - i;
    if (remainder != 0) {
      const V16H b0 = LoadN(d16h, row + i, remainder);
      const V a0 = PromoteTo(d, b0);
      const V v0 = LoadN(d, vec + i, remainder);
      sum0 = MulAdd(a0, v0, sum0);
    }
    out[r] = ReduceSum(d, sum0);
    HWY_IF_CONSTEXPR(kAdd) { out[r] = AddScalar(out[r], add[r]); }
  }  // r
}

template <size_t kOuter, size_t kInner>
HWY_NOINLINE void MatVecAdd(const hwy::bfloat16_t* HWY_RESTRICT mat,
                            const float* HWY_RESTRICT vec,
                            const float* HWY_RESTRICT add,
                            float* HWY_RESTRICT out, hwy::ThreadPool& pool) {
  MatVecAddImpl<kOuter, kInner, true>(mat, vec, add, out, pool);
}

template <size_t kOuter, size_t kInner>
HWY_NOINLINE void MatVec(const hwy::bfloat16_t* HWY_RESTRICT mat,
                         const float* HWY_RESTRICT vec, float* HWY_RESTRICT out,
                         hwy::ThreadPool& pool) {
  MatVecAddImpl<kOuter, kInner, false>(mat, vec, /*add=*/nullptr, out, pool);
}

// Both mat and vec are bf16.
template <size_t kOuter, size_t kInner, bool kAdd>
HWY_NOINLINE void MatVecAddImpl(const hwy::bfloat16_t* HWY_RESTRICT mat,
                                const hwy::bfloat16_t* HWY_RESTRICT vec,
                                const hwy::bfloat16_t* HWY_RESTRICT add,
                                float* HWY_RESTRICT out,
                                hwy::ThreadPool& pool) {
  // Process multiple rows at a time so that we write multiples of a cache line
  // to avoid false sharing (>= 64). 128 is better than 256. 512 has too little
  // parallelization potential.
  constexpr size_t kChunkSize = 64 / sizeof(bfloat16_t);
  const uint64_t num_chunks = static_cast<uint64_t>(kOuter / kChunkSize);

  const ScalableTag<float> df;
  const Repartition<hwy::bfloat16_t, decltype(df)> d16;
  using V16 = Vec<decltype(d16)>;
  const size_t N = Lanes(d16);
  // Required for Stream loop, otherwise we might have partial vectors.
  HWY_DASSERT(kChunkSize >= N);
  pool.Run(0, num_chunks,
           [&](const uint64_t chunk, size_t /*thread*/) HWY_ATTR {
             // MSVC workaround: duplicate to ensure constexpr.
             constexpr size_t kChunkSize = 64 / sizeof(bfloat16_t);
             // Software write-combining to avoid cache pollution from out.
             // Although `out` may be used later, keeping it out of the cache
             // now and avoiding RFOs is a consistent 5% overall win.
             HWY_ALIGN float buf[kChunkSize];

             // Only handle entire chunks here because the Stream is not masked.
             // Remaining rows are handled after the pool.Run.
             const size_t begin = static_cast<size_t>(chunk * kChunkSize);
             for (size_t idx_row = 0; idx_row < kChunkSize; ++idx_row) {
               auto sum0 = Zero(df);
               auto sum1 = Zero(df);
               auto sum2 = Zero(df);
               auto sum3 = Zero(df);

               const hwy::bfloat16_t* HWY_RESTRICT row =
                   &mat[(begin + idx_row) * kInner];
               size_t i = 0;
               // No clear win from prefetching from the next 1..3 rows.
               // clflush &row[i] is slow, clflushopt less so but not helping.
               HWY_UNROLL(1)
               for (; i + 2 * N <= kInner; i += 2 * N) {
                 const V16 b0 = LoadU(d16, row + i + 0 * N);
                 const V16 b1 = LoadU(d16, row + i + 1 * N);
                 const V16 v0 = LoadU(d16, vec + i + 0 * N);
                 const V16 v1 = LoadU(d16, vec + i + 1 * N);
                 sum0 = ReorderWidenMulAccumulate(df, b0, v0, sum0, sum1);
                 sum2 = ReorderWidenMulAccumulate(df, b1, v1, sum2, sum3);
               }
               // Last entire vector
               for (; i + N <= kInner; i += N) {
                 const V16 b0 = LoadU(d16, row + i);
                 const V16 v0 = LoadU(d16, vec + i);
                 sum0 = ReorderWidenMulAccumulate(df, b0, v0, sum0, sum1);
               }
               const size_t remainder = kInner - i;
               if (remainder != 0) {
                 const V16 b0 = LoadN(d16, row + i, remainder);
                 const V16 v0 = LoadN(d16, vec + i, remainder);
                 sum2 = ReorderWidenMulAccumulate(df, b0, v0, sum2, sum3);
               }
               // Reduction tree: sum of all accumulators, then their lanes
               sum0 = Add(sum0, sum1);
               sum2 = Add(sum2, sum3);
               sum0 = Add(sum0, sum2);
               buf[idx_row] = ReduceSum(df, sum0);
               HWY_IF_CONSTEXPR(kAdd) {
                 buf[idx_row] = AddScalar(buf[idx_row], add[begin + idx_row]);
               }
             }  // idx_row
             HWY_UNROLL(4)  // 1..4 iterations
             for (size_t i = 0; i != kChunkSize; i += N / 2) {
               Stream(Load(df, buf + i), df, out + begin + i);
             }
           });
  hwy::FlushStream();

  // Handle remainder rows which are not a multiple of the chunk size.
  for (size_t r = num_chunks * kChunkSize; r < kOuter; ++r) {
    auto sum0 = Zero(df);
    auto sum1 = Zero(df);

    const hwy::bfloat16_t* HWY_RESTRICT row = &mat[r * kInner];
    size_t i = 0;
    HWY_UNROLL(1)
    for (; i + N <= kInner; i += N) {
      const V16 b0 = LoadU(d16, row + i);
      const V16 v0 = LoadU(d16, vec + i);
      sum0 = ReorderWidenMulAccumulate(df, b0, v0, sum0, sum1);
    }
    const size_t remainder = kInner - i;
    if (remainder != 0) {
      const V16 b0 = LoadN(d16, row + i, remainder);
      const V16 v0 = LoadN(d16, vec + i, remainder);
      sum0 = ReorderWidenMulAccumulate(df, b0, v0, sum0, sum1);
    }
    out[r] = ReduceSum(df, Add(sum0, sum1));
    HWY_IF_CONSTEXPR(kAdd) { out[r] = AddScalar(out[r], add[r]); }
  }  // r
}

template <size_t kOuter, size_t kInner>
HWY_NOINLINE void MatVecAdd(const hwy::bfloat16_t* HWY_RESTRICT mat,
                            const hwy::bfloat16_t* HWY_RESTRICT vec,
                            const hwy::bfloat16_t* HWY_RESTRICT add,
                            float* HWY_RESTRICT out, hwy::ThreadPool& pool) {
  MatVecAddImpl<kOuter, kInner, true>(mat, vec, add, out, pool);
}

template <size_t kOuter, size_t kInner>
HWY_NOINLINE void MatVec(const hwy::bfloat16_t* HWY_RESTRICT mat,
                         const hwy::bfloat16_t* HWY_RESTRICT vec,
                         float* HWY_RESTRICT out, hwy::ThreadPool& pool) {
  MatVecAddImpl<kOuter, kInner, false>(mat, vec, /*add=*/nullptr, out, pool);
}

#endif  // HWY_TARGET != HWY_SCALAR

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_CONTRIB_MATVEC_MATVEC_INL_H_
