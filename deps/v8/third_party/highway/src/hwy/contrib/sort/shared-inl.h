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

// Definitions shared between vqsort-inl and sorting_networks-inl.

// Normal include guard for target-independent parts
#ifndef HIGHWAY_HWY_CONTRIB_SORT_SHARED_INL_H_
#define HIGHWAY_HWY_CONTRIB_SORT_SHARED_INL_H_

#include "hwy/base.h"

namespace hwy {

// Internal constants - these are to avoid magic numbers/literals and cannot be
// changed without also changing the associated code.
struct SortConstants {
  // SortingNetwork reshapes its input into a matrix. This is the maximum number
  // of *lanes* per vector. Must be at least 8 because SortSamples assumes the
  // sorting network can handle 128 bytes with 8 rows, so 16 bytes per vector,
  // which means 8 lanes for 16-bit types.
#if HWY_COMPILER_MSVC || HWY_IS_DEBUG_BUILD
  static constexpr size_t kMaxCols = 8;  // avoid build timeout/stack overflow
#else
  static constexpr size_t kMaxCols = 16;  // enough for u32 in 512-bit vector
#endif

  // 16 rows is a compromise between using the 32 AVX-512/SVE/RVV registers,
  // fitting within 16 AVX2 registers with only a few spills, keeping BaseCase
  // code size reasonable, and minimizing the extra logN factor for larger
  // networks (for which only loose upper bounds on size are known).
  static constexpr size_t kMaxRows = 16;

  // Template argument ensures there is no actual division instruction.
  template <size_t kLPK>
  static constexpr HWY_INLINE size_t BaseCaseNumLanes(size_t N) {
    // We use 8, 8x2, 8x4, and 16x{4..} networks, in units of keys. For N/kLPK
    // < 4, we cannot use the 16-row networks.
    return (((N / kLPK) >= 4) ? kMaxRows : 8) * HWY_MIN(N, kMaxCols);
  }

  // Unrolling is important (pipelining and amortizing branch mispredictions);
  // 2x is sufficient to reach full memory bandwidth on SKX in Partition, but
  // somewhat slower for sorting than 4x.
  //
  // To change, must also update left + 3 * N etc. in the loop.
  static constexpr size_t kPartitionUnroll = 4;

  // Chunk := group of keys loaded for sampling a pivot. Matches the typical
  // cache line size of 64 bytes to get maximum benefit per L2 miss. Sort()
  // ensures vectors are no larger than that, so this can be independent of the
  // vector size and thus constexpr.
  static constexpr HWY_INLINE size_t LanesPerChunk(size_t sizeof_t) {
    return 64 / sizeof_t;
  }

  template <typename T>
  static constexpr HWY_INLINE size_t SampleLanes() {
    return 2 * LanesPerChunk(sizeof(T));  // Stored samples
  }

  static constexpr HWY_INLINE size_t PartitionBufNum(size_t N) {
    // The main loop reads kPartitionUnroll vectors, and first loads from
    // both left and right beforehand, so it requires 2 * kPartitionUnroll
    // vectors. To handle amounts between that and BaseCaseNumLanes(), we
    // partition up 3 * kPartitionUnroll + 1 vectors into a two-part buffer.
    return 2 * (3 * kPartitionUnroll + 1) * N;
  }

  // Max across the three buffer usages.
  template <typename T, size_t kLPK>
  static constexpr HWY_INLINE size_t BufNum(size_t N) {
    // BaseCase may write one padding vector, and SortSamples uses the space
    // after samples as the buffer.
    return HWY_MAX(SampleLanes<T>() + BaseCaseNumLanes<kLPK>(N) + N,
                   PartitionBufNum(N));
  }

  // Translates vector_size to lanes and returns size in bytes.
  template <typename T, size_t kLPK>
  static constexpr HWY_INLINE size_t BufBytes(size_t vector_size) {
    return BufNum<T, kLPK>(vector_size / sizeof(T)) * sizeof(T);
  }

  // Returns max for any type.
  template <size_t kLPK>
  static constexpr HWY_INLINE size_t MaxBufBytes(size_t vector_size) {
    // If 2 lanes per key, it's a 128-bit key with u64 lanes.
    return kLPK == 2 ? BufBytes<uint64_t, 2>(vector_size)
                     : HWY_MAX((BufBytes<uint16_t, 1>(vector_size)),
                               HWY_MAX((BufBytes<uint32_t, 1>(vector_size)),
                                       (BufBytes<uint64_t, 1>(vector_size))));
  }
};

static_assert(SortConstants::MaxBufBytes<1>(64) <= 1664, "Unexpectedly high");
static_assert(SortConstants::MaxBufBytes<2>(64) <= 1664, "Unexpectedly high");

}  // namespace hwy

#endif  // HIGHWAY_HWY_CONTRIB_SORT_SHARED_INL_H_

// Per-target
// clang-format off
#if defined(HIGHWAY_HWY_CONTRIB_SORT_SHARED_TOGGLE) == defined(HWY_TARGET_TOGGLE) // NOLINT
// clang-format on
#ifdef HIGHWAY_HWY_CONTRIB_SORT_SHARED_TOGGLE
#undef HIGHWAY_HWY_CONTRIB_SORT_SHARED_TOGGLE
#else
#define HIGHWAY_HWY_CONTRIB_SORT_SHARED_TOGGLE
#endif

#include "hwy/highway.h"

// vqsort isn't available on HWY_SCALAR, and builds time out on MSVC opt and
// Armv7 debug, and Armv8 GCC 11 asan hits an internal compiler error likely
// due to https://gcc.gnu.org/bugzilla/show_bug.cgi?id=97696. Armv8 Clang
// hwasan/msan/tsan/asan also fail to build SVE (b/335157772).
#undef VQSORT_ENABLED
#if (HWY_TARGET == HWY_SCALAR) ||                                   \
    (HWY_COMPILER_MSVC && !HWY_IS_DEBUG_BUILD) ||                   \
    (HWY_ARCH_ARM_V7 && HWY_IS_DEBUG_BUILD) ||                      \
    (HWY_ARCH_ARM_A64 && HWY_COMPILER_GCC_ACTUAL && HWY_IS_ASAN)
#define VQSORT_ENABLED 0
#else
#define VQSORT_ENABLED 1
#endif

namespace hwy {
namespace HWY_NAMESPACE {

// Default tag / vector width selector.
#if HWY_TARGET == HWY_RVV
// Use LMUL = 1/2; for SEW=64 this ends up emulated via VSETVLI.
template <typename T>
using SortTag = ScalableTag<T, -1>;
#else
template <typename T>
using SortTag = ScalableTag<T>;
#endif

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy

#endif  // HIGHWAY_HWY_CONTRIB_SORT_SHARED_TOGGLE
