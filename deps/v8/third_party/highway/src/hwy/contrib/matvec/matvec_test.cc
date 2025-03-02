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

#include "hwy/base.h"

// Reduce targets to avoid timeout under emulation.
#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS \
  (HWY_SVE2_128 | HWY_SVE2 | HWY_SVE_256 | HWY_NEON_WITHOUT_AES)
#endif

#include <stddef.h>
#include <stdint.h>

#include "hwy/aligned_allocator.h"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/matvec/matvec_test.cc"  // NOLINT
#include "hwy/foreach_target.h"  // IWYU pragma: keep
// Must come after foreach_target.h
#include "hwy/contrib/algo/transform-inl.h"
#include "hwy/contrib/matvec/matvec-inl.h"
#include "hwy/highway.h"
#include "hwy/contrib/thread_pool/thread_pool.h"
#include "hwy/contrib/thread_pool/topology.h"
#include "hwy/tests/test_util-inl.h"
// clang-format on

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

template <typename MatT, typename T>
HWY_NOINLINE void SimpleMatVecAdd(const MatT* HWY_RESTRICT mat,
                                  const T* HWY_RESTRICT vec,
                                  const T* HWY_RESTRICT add, size_t rows,
                                  size_t cols, T* HWY_RESTRICT out,
                                  ThreadPool& pool) {
  if (add) {
    pool.Run(0, rows, [=](uint64_t r, size_t /*thread*/) {
      T dot = ConvertScalarTo<T>(0);
      for (size_t c = 0; c < cols; c++) {
        // For reasons unknown, fp16 += does not compile on clang (Arm).
        dot = ConvertScalarTo<T>(dot + mat[r * cols + c] * vec[c]);
      }
      out[r] = dot + add[r];
    });
  } else {
    pool.Run(0, rows, [=](uint64_t r, size_t /*thread*/) {
      T dot = ConvertScalarTo<T>(0);
      for (size_t c = 0; c < cols; c++) {
        // For reasons unknown, fp16 += does not compile on clang (Arm).
        dot = ConvertScalarTo<T>(dot + mat[r * cols + c] * vec[c]);
      }
      out[r] = dot;
    });
  }
}

HWY_MAYBE_UNUSED HWY_NOINLINE void SimpleMatVecAdd(
    const hwy::bfloat16_t* HWY_RESTRICT mat, const float* HWY_RESTRICT vec,
    const float* add, size_t rows, size_t cols, float* HWY_RESTRICT out,
    ThreadPool& pool) {
  if (add) {
    pool.Run(0, rows, [=](uint64_t r, size_t /*thread*/) {
      float dot = 0.0f;
      for (size_t c = 0; c < cols; c++) {
        dot += F32FromBF16(mat[r * cols + c]) * vec[c];
      }
      out[r] = dot + add[r];
    });
  } else {
    pool.Run(0, rows, [=](uint64_t r, size_t /*thread*/) {
      float dot = 0.0f;
      for (size_t c = 0; c < cols; c++) {
        dot += F32FromBF16(mat[r * cols + c]) * vec[c];
      }
      out[r] = dot;
    });
  }
}

HWY_MAYBE_UNUSED HWY_NOINLINE void SimpleMatVecAdd(
    const hwy::bfloat16_t* HWY_RESTRICT mat,
    const hwy::bfloat16_t* HWY_RESTRICT vec,
    const hwy::bfloat16_t* HWY_RESTRICT add, size_t rows, size_t cols,
    float* HWY_RESTRICT out, ThreadPool& pool) {
  if (add) {
    pool.Run(0, rows, [=](uint64_t r, size_t /*thread*/) {
      float dot = 0.0f;
      for (size_t c = 0; c < cols; c++) {
        dot += F32FromBF16(mat[r * cols + c]) * F32FromBF16(vec[c]);
      }
      out[r] = dot + F32FromBF16(add[r]);
    });
  } else {
    pool.Run(0, rows, [=](uint64_t r, size_t /*thread*/) {
      float dot = 0.0f;
      for (size_t c = 0; c < cols; c++) {
        dot += F32FromBF16(mat[r * cols + c]) * F32FromBF16(vec[c]);
      }
      out[r] = dot;
    });
  }
}

struct GenerateMod {
  template <class D, HWY_IF_NOT_BF16_D(D), HWY_IF_LANES_GT_D(D, 1)>
  Vec<D> operator()(D d, Vec<RebindToUnsigned<D>> indices) const {
    const RebindToUnsigned<D> du;
    return Reverse2(d, ConvertTo(d, And(indices, Set(du, 0xF))));
  }

  template <class D, HWY_IF_NOT_BF16_D(D), HWY_IF_LANES_LE_D(D, 1)>
  Vec<D> operator()(D d, Vec<RebindToUnsigned<D>> indices) const {
    const RebindToUnsigned<D> du;
    return ConvertTo(d, And(indices, Set(du, 0xF)));
  }

  // Requires >= 4 bf16 lanes for float32 Reverse2.
  template <class D, HWY_IF_BF16_D(D), HWY_IF_LANES_GT_D(D, 2)>
  Vec<D> operator()(D d, Vec<RebindToUnsigned<D>> indices) const {
    const RebindToUnsigned<D> du;
    const RebindToSigned<D> di;
    const RepartitionToWide<decltype(di)> dw;
    const RebindToFloat<decltype(dw)> df;
    indices = And(indices, Set(du, 0xF));
    const Vec<decltype(df)> i0 = ConvertTo(df, PromoteLowerTo(dw, indices));
    const Vec<decltype(df)> i1 = ConvertTo(df, PromoteUpperTo(dw, indices));
    return OrderedDemote2To(d, Reverse2(df, i0), Reverse2(df, i1));
  }

  // For one or two lanes, we don't have OrderedDemote2To nor Reverse2.
  template <class D, HWY_IF_BF16_D(D), HWY_IF_LANES_LE_D(D, 2)>
  Vec<D> operator()(D d, Vec<RebindToUnsigned<D>> indices) const {
    const Rebind<float, D> df;
    return DemoteTo(d, Set(df, GetLane(indices)));
  }
};

// MatT is usually the same as T, but can also be bfloat16_t when T = float.
template <typename MatT, typename VecT>
class TestMatVecAdd {
  template <size_t kRows, size_t kCols, class D, typename T = TFromD<D>>
  HWY_NOINLINE void Test(D d, ThreadPool& pool) {
// This target lacks too many ops required in our implementation, use
// HWY_EMU128 instead.
#if HWY_TARGET != HWY_SCALAR
    const Repartition<MatT, D> dm;
    const Repartition<VecT, D> dv;
    const size_t misalign = 3 * Lanes(d) / 5;
    // Fill matrix and vector with small integer values
    const size_t area = kRows * kCols;
    AlignedFreeUniquePtr<MatT[]> storage_m =
        AllocateAligned<MatT>(misalign + area);
    AlignedFreeUniquePtr<VecT[]> storage_v =
        AllocateAligned<VecT>(misalign + kCols);
    AlignedFreeUniquePtr<VecT[]> storage_a =
        AllocateAligned<VecT>(misalign + kRows);
    HWY_ASSERT(storage_m && storage_v && storage_a);
    MatT* pm = storage_m.get() + misalign;
    VecT* pv = storage_v.get() + misalign;
    VecT* av = storage_a.get() + misalign;
    Generate(dm, pm, area, GenerateMod());
    Generate(dv, pv, kCols, GenerateMod());
    Generate(dv, av, kRows, GenerateMod());

    AlignedFreeUniquePtr<T[]> expected_without_add = AllocateAligned<T>(kRows);
    HWY_ASSERT(expected_without_add);
    SimpleMatVecAdd(pm, pv, static_cast<VecT*>(nullptr), kRows, kCols,
                    expected_without_add.get(), pool);

    AlignedFreeUniquePtr<T[]> actual_without_add = AllocateAligned<T>(kRows);
    HWY_ASSERT(actual_without_add);
    MatVec<kRows, kCols>(pm, pv, actual_without_add.get(), pool);

    const auto assert_close = [&](const AlignedFreeUniquePtr<T[]>& expected,
                                  const AlignedFreeUniquePtr<T[]>& actual,
                                  bool with_add) {
      for (size_t i = 0; i < kRows; ++i) {
        const double exp = ConvertScalarTo<double>(expected[i]);
        const double act = ConvertScalarTo<double>(actual[i]);
        const double tolerance =
            exp * 20 * 1.0 /
            (1ULL << HWY_MIN(MantissaBits<MatT>(), MantissaBits<VecT>()));
        if (!(exp - tolerance <= act && act <= exp + tolerance)) {
          fprintf(stderr,
                  "%s/%s %zu x %zu, %s: mismatch at %zu %f %f; tol %f\n",
                  TypeName(MatT(), 1).c_str(), TypeName(VecT(), 1).c_str(),
                  kRows, kCols, (with_add ? "with add" : "without add"), i, exp,
                  act, tolerance);
          HWY_ASSERT(0);
        }
      }
    };

    assert_close(expected_without_add, actual_without_add, /*with_add=*/false);

    AlignedFreeUniquePtr<T[]> expected_with_add = AllocateAligned<T>(kRows);
    SimpleMatVecAdd(pm, pv, av, kRows, kCols, expected_with_add.get(), pool);

    AlignedFreeUniquePtr<T[]> actual_with_add = AllocateAligned<T>(kRows);
    MatVecAdd<kRows, kCols>(pm, pv, av, actual_with_add.get(), pool);

    assert_close(expected_with_add, actual_with_add, /*with_add=*/true);

#else
    (void)d;
    (void)pool;
#endif  // HWY_TARGET != HWY_SCALAR
  }

  template <class D>
  HWY_NOINLINE void CreatePoolAndTest(D d, size_t num_threads) {
    // Threads might not work on WASM; run only on main thread.
    if (HaveThreadingSupport()) num_threads = 0;

    ThreadPool pool(HWY_MIN(num_threads, ThreadPool::MaxThreads()));

    Test<AdjustedReps(192), AdjustedReps(256)>(d, pool);
// Fewer tests due to compiler OOM
#if !HWY_ARCH_RISCV
    Test<40, AdjustedReps(512)>(d, pool);
    Test<AdjustedReps(1024), 50>(d, pool);

    // Too large for low-precision vectors/accumulators.
    if (sizeof(TFromD<D>) != 2 && sizeof(VecT) != 2) {
      Test<AdjustedReps(1536), AdjustedReps(1536)>(d, pool);
    }
#endif  // !HWY_ARCH_RISCV
  }

 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    CreatePoolAndTest(d, 13);
// Fewer tests due to compiler OOM
#if !HWY_ARCH_RISCV
    CreatePoolAndTest(d, 16);
#endif
  }
};

void TestAllMatVecAdd() {
#if HWY_HAVE_FLOAT16
  ForPartialVectors<TestMatVecAdd<float16_t, float16_t>>()(float16_t());
#endif
  ForPartialVectors<TestMatVecAdd<float, float>>()(float());
#if HWY_HAVE_FLOAT64
  ForPartialVectors<TestMatVecAdd<double, double>>()(double());
#endif
}

void TestAllMatVecBF16() {
  ForGEVectors<32, TestMatVecAdd<bfloat16_t, float>>()(float());
}

void TestAllMatVecBF16Both() {
  ForGEVectors<32, TestMatVecAdd<bfloat16_t, bfloat16_t>>()(float());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(MatVecTest);
HWY_EXPORT_AND_TEST_P(MatVecTest, TestAllMatVecAdd);
HWY_EXPORT_AND_TEST_P(MatVecTest, TestAllMatVecBF16);
HWY_EXPORT_AND_TEST_P(MatVecTest, TestAllMatVecBF16Both);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
