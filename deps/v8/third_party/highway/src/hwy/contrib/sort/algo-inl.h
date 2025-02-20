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

// Normal include guard for target-independent parts
#ifndef HIGHWAY_HWY_CONTRIB_SORT_ALGO_INL_H_
#define HIGHWAY_HWY_CONTRIB_SORT_ALGO_INL_H_

#include <stddef.h>
#include <stdint.h>

#include <algorithm>   // std::sort, std::min, std::max
#include <functional>  // std::less, std::greater
#include <vector>

#include "hwy/contrib/sort/vqsort.h"
#include "hwy/highway.h"
#include "hwy/print.h"

// Third-party algorithms
#define HAVE_AVX2SORT 0
#define HAVE_IPS4O 0
// When enabling, consider changing max_threads (required for Table 1a)
#define HAVE_PARALLEL_IPS4O (HAVE_IPS4O && 1)
#define HAVE_PDQSORT 0
#define HAVE_SORT512 0
#define HAVE_VXSORT 0
#if HWY_ARCH_X86
#define HAVE_INTEL 0
#else
#define HAVE_INTEL 0
#endif

#if HAVE_PARALLEL_IPS4O
#include <thread>  // NOLINT
#endif

#if HAVE_AVX2SORT
HWY_PUSH_ATTRIBUTES("avx2,avx")
#include "avx2sort.h"  //NOLINT
HWY_POP_ATTRIBUTES
#endif
#if HAVE_IPS4O || HAVE_PARALLEL_IPS4O
#include "third_party/ips4o/include/ips4o.hpp"
#include "third_party/ips4o/include/ips4o/thread_pool.hpp"
#endif
#if HAVE_PDQSORT
#include "third_party/boost/allowed/sort/sort.hpp"
#endif
#if HAVE_SORT512
#include "sort512.h"  //NOLINT
#endif

// vxsort is difficult to compile for multiple targets because it also uses
// .cpp files, and we'd also have to #undef its include guards. Instead, compile
// only for AVX2 or AVX3 depending on this macro.
#define VXSORT_AVX3 1
#if HAVE_VXSORT
// inlined from vxsort_targets_enable_avx512 (must close before end of header)
#ifdef __GNUC__
#ifdef __clang__
#if VXSORT_AVX3
#pragma clang attribute push(__attribute__((target("avx512f,avx512dq"))), \
                             apply_to = any(function))
#else
#pragma clang attribute push(__attribute__((target("avx2"))), \
                             apply_to = any(function))
#endif  // VXSORT_AVX3

#else
#pragma GCC push_options
#if VXSORT_AVX3
#pragma GCC target("avx512f,avx512dq")
#else
#pragma GCC target("avx2")
#endif  // VXSORT_AVX3
#endif
#endif

#if VXSORT_AVX3
#include "vxsort/machine_traits.avx512.h"
#else
#include "vxsort/machine_traits.avx2.h"
#endif  // VXSORT_AVX3
#include "vxsort/vxsort.h"
#ifdef __GNUC__
#ifdef __clang__
#pragma clang attribute pop
#else
#pragma GCC pop_options
#endif
#endif
#endif  // HAVE_VXSORT

namespace hwy {

enum class Dist { kUniform8, kUniform16, kUniform32 };

static inline std::vector<Dist> AllDist() {
  // Also include lower-entropy distributions to test MaybePartitionTwoValue.
  return {Dist::kUniform8, /*Dist::kUniform16,*/ Dist::kUniform32};
}

static inline const char* DistName(Dist dist) {
  switch (dist) {
    case Dist::kUniform8:
      return "uniform8";
    case Dist::kUniform16:
      return "uniform16";
    case Dist::kUniform32:
      return "uniform32";
  }
  return "unreachable";
}

template <typename T>
class InputStats {
 public:
  void Notify(T value) {
    min_ = std::min(min_, value);
    max_ = std::max(max_, value);
    // Converting to integer would truncate floats, multiplying to save digits
    // risks overflow especially when casting, so instead take the sum of the
    // bit representations as the checksum.
    uint64_t bits = 0;
    static_assert(sizeof(T) <= 8, "Expected a built-in type");
    CopyBytes<sizeof(T)>(&value, &bits);  // not same size
    sum_ += bits;
    count_ += 1;
  }

  bool operator==(const InputStats& other) const {
    char type_name[100];
    detail::TypeName(hwy::detail::MakeTypeInfo<T>(), 1, type_name);

    if (count_ != other.count_) {
      HWY_ABORT("Sort %s: count %d vs %d\n", type_name,
                static_cast<int>(count_), static_cast<int>(other.count_));
    }

    if (min_ != other.min_ || max_ != other.max_) {
      HWY_ABORT("Sort %s: minmax %f/%f vs %f/%f\n", type_name,
                static_cast<double>(min_), static_cast<double>(max_),
                static_cast<double>(other.min_),
                static_cast<double>(other.max_));
    }

    // Sum helps detect duplicated/lost values
    if (sum_ != other.sum_) {
      HWY_ABORT("Sort %s: Sum mismatch %g %g; min %g max %g\n", type_name,
                static_cast<double>(sum_), static_cast<double>(other.sum_),
                static_cast<double>(min_), static_cast<double>(max_));
    }

    return true;
  }

 private:
  T min_ = hwy::HighestValue<T>();
  T max_ = hwy::LowestValue<T>();
  uint64_t sum_ = 0;
  size_t count_ = 0;
};

enum class Algo {
#if HAVE_INTEL
  kIntel,
#endif
#if HAVE_AVX2SORT
  kSEA,
#endif
#if HAVE_IPS4O
  kIPS4O,
#endif
#if HAVE_PARALLEL_IPS4O
  kParallelIPS4O,
#endif
#if HAVE_PDQSORT
  kPDQ,
#endif
#if HAVE_SORT512
  kSort512,
#endif
#if HAVE_VXSORT
  kVXSort,
#endif
  kStdSort,
  kStdSelect,
  kStdPartialSort,
  kVQSort,
  kVQPartialSort,
  kVQSelect,
  kHeapSort,
  kHeapPartialSort,
  kHeapSelect,
};

static inline bool IsVQ(Algo algo) {
  switch (algo) {
    case Algo::kVQSort:
    case Algo::kVQPartialSort:
    case Algo::kVQSelect:
      return true;
    default:
      return false;
  }
}

static inline bool IsSelect(Algo algo) {
  switch (algo) {
    case Algo::kStdSelect:
    case Algo::kVQSelect:
    case Algo::kHeapSelect:
      return true;
    default:
      return false;
  }
}

static inline bool IsPartialSort(Algo algo) {
  switch (algo) {
    case Algo::kStdPartialSort:
    case Algo::kVQPartialSort:
    case Algo::kHeapPartialSort:
      return true;
    default:
      return false;
  }
}

static inline Algo ReferenceAlgoFor(Algo algo) {
  if (IsPartialSort(algo)) return Algo::kStdPartialSort;
#if HAVE_PDQSORT
  return Algo::kPDQ;
#else
  return Algo::kStdSort;
#endif
}

static inline const char* AlgoName(Algo algo) {
  switch (algo) {
#if HAVE_INTEL
    case Algo::kIntel:
      return "intel";
#endif
#if HAVE_AVX2SORT
    case Algo::kSEA:
      return "sea";
#endif
#if HAVE_IPS4O
    case Algo::kIPS4O:
      return "ips4o";
#endif
#if HAVE_PARALLEL_IPS4O
    case Algo::kParallelIPS4O:
      return "par_ips4o";
#endif
#if HAVE_PDQSORT
    case Algo::kPDQ:
      return "pdq";
#endif
#if HAVE_SORT512
    case Algo::kSort512:
      return "sort512";
#endif
#if HAVE_VXSORT
    case Algo::kVXSort:
      return "vxsort";
#endif
    case Algo::kStdSort:
      return "std";
    case Algo::kStdPartialSort:
      return "std_partial";
    case Algo::kStdSelect:
      return "std_select";
    case Algo::kVQSort:
      return "vq";
    case Algo::kVQPartialSort:
      return "vq_partial";
    case Algo::kVQSelect:
      return "vq_select";
    case Algo::kHeapSort:
      return "heap";
    case Algo::kHeapPartialSort:
      return "heap_partial";
    case Algo::kHeapSelect:
      return "heap_select";
  }
  return "unreachable";
}

}  // namespace hwy
#endif  // HIGHWAY_HWY_CONTRIB_SORT_ALGO_INL_H_

// Per-target
// clang-format off
#if defined(HIGHWAY_HWY_CONTRIB_SORT_ALGO_TOGGLE) == defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef HIGHWAY_HWY_CONTRIB_SORT_ALGO_TOGGLE
#undef HIGHWAY_HWY_CONTRIB_SORT_ALGO_TOGGLE
#else
#define HIGHWAY_HWY_CONTRIB_SORT_ALGO_TOGGLE
#endif
// clang-format on

#include "hwy/aligned_allocator.h"
#include "hwy/contrib/sort/traits-inl.h"
#include "hwy/contrib/sort/traits128-inl.h"
#include "hwy/contrib/sort/vqsort-inl.h"  // HeapSort

HWY_BEFORE_NAMESPACE();

// Requires target pragma set by HWY_BEFORE_NAMESPACE
#if HAVE_INTEL && HWY_TARGET <= HWY_AVX3
// #include "avx512-16bit-qsort.hpp"  // requires AVX512-VBMI2
#include "avx512-32bit-qsort.hpp"
#include "avx512-64bit-qsort.hpp"
#endif

namespace hwy {
namespace HWY_NAMESPACE {

#if HAVE_INTEL || HAVE_VXSORT  // only supports ascending order
template <typename T>
using OtherOrder = detail::OrderAscending<T>;
#else
template <typename T>
using OtherOrder = detail::OrderDescending<T>;
#endif

class Xorshift128Plus {
  static HWY_INLINE uint64_t SplitMix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

 public:
  // Generates two vectors of 64-bit seeds via SplitMix64 and stores into
  // `seeds`. Generating these afresh in each ChoosePivot is too expensive.
  template <class DU64>
  static void GenerateSeeds(DU64 du64, TFromD<DU64>* HWY_RESTRICT seeds) {
    seeds[0] = SplitMix64(0x9E3779B97F4A7C15ull);
    for (size_t i = 1; i < 2 * Lanes(du64); ++i) {
      seeds[i] = SplitMix64(seeds[i - 1]);
    }
  }

  // Need to pass in the state because vector cannot be class members.
  template <class VU64>
  static VU64 RandomBits(VU64& state0, VU64& state1) {
    VU64 s1 = state0;
    VU64 s0 = state1;
    const VU64 bits = Add(s1, s0);
    state0 = s0;
    s1 = Xor(s1, ShiftLeft<23>(s1));
    state1 = Xor(s1, Xor(s0, Xor(ShiftRight<18>(s1), ShiftRight<5>(s0))));
    return bits;
  }
};

template <class D, class VU64, HWY_IF_NOT_FLOAT_D(D)>
Vec<D> RandomValues(D d, VU64& s0, VU64& s1, const VU64 mask) {
  const VU64 bits = Xorshift128Plus::RandomBits(s0, s1);
  return BitCast(d, And(bits, mask));
}

// It is important to avoid denormals, which are flushed to zero by SIMD but not
// scalar sorts, and NaN, which may be ordered differently in scalar vs. SIMD.
template <class DF, class VU64, HWY_IF_FLOAT_D(DF)>
Vec<DF> RandomValues(DF df, VU64& s0, VU64& s1, const VU64 mask) {
  using TF = TFromD<DF>;
  const RebindToUnsigned<decltype(df)> du;
  using VU = Vec<decltype(du)>;

  const VU64 bits64 = And(Xorshift128Plus::RandomBits(s0, s1), mask);

#if HWY_TARGET == HWY_SCALAR  // Cannot repartition u64 to smaller types
  using TU = MakeUnsigned<TF>;
  const VU bits = Set(du, static_cast<TU>(GetLane(bits64) & LimitsMax<TU>()));
#else
  const VU bits = BitCast(du, bits64);
#endif
  // Avoid NaN/denormal by only generating values in [1, 2), i.e. random
  // mantissas with the exponent taken from the representation of 1.0.
  const VU k1 = BitCast(du, Set(df, TF{1.0}));
  const VU mantissa_mask = Set(du, MantissaMask<TF>());
  const VU representation = OrAnd(k1, bits, mantissa_mask);
  return BitCast(df, representation);
}

template <class DU64>
Vec<DU64> MaskForDist(DU64 du64, const Dist dist, size_t sizeof_t) {
  switch (sizeof_t) {
    case 2:
      return Set(du64, (dist == Dist::kUniform8) ? 0x00FF00FF00FF00FFull
                                                 : 0xFFFFFFFFFFFFFFFFull);
    case 4:
      return Set(du64, (dist == Dist::kUniform8)    ? 0x000000FF000000FFull
                       : (dist == Dist::kUniform16) ? 0x0000FFFF0000FFFFull
                                                    : 0xFFFFFFFFFFFFFFFFull);
    case 8:
      return Set(du64, (dist == Dist::kUniform8)    ? 0x00000000000000FFull
                       : (dist == Dist::kUniform16) ? 0x000000000000FFFFull
                                                    : 0x00000000FFFFFFFFull);
    default:
      HWY_ABORT("Logic error");
      return Zero(du64);
  }
}

template <typename T>
InputStats<T> GenerateInput(const Dist dist, T* v, size_t num_lanes) {
  SortTag<uint64_t> du64;
  using VU64 = Vec<decltype(du64)>;
  const size_t N64 = Lanes(du64);
  auto seeds = hwy::AllocateAligned<uint64_t>(2 * N64);
  Xorshift128Plus::GenerateSeeds(du64, seeds.get());
  VU64 s0 = Load(du64, seeds.get());
  VU64 s1 = Load(du64, seeds.get() + N64);

#if HWY_TARGET == HWY_SCALAR
  const Sisd<T> d;
#else
  const Repartition<T, decltype(du64)> d;
#endif
  using V = Vec<decltype(d)>;
  const size_t N = Lanes(d);
  const VU64 mask = MaskForDist(du64, dist, sizeof(T));
  auto buf = hwy::AllocateAligned<T>(N);

  size_t i = 0;
  for (; i + N <= num_lanes; i += N) {
    const V values = RandomValues(d, s0, s1, mask);
    StoreU(values, d, v + i);
  }
  if (i < num_lanes) {
    const V values = RandomValues(d, s0, s1, mask);
    StoreU(values, d, buf.get());
    CopyBytes(buf.get(), v + i, (num_lanes - i) * sizeof(T));
  }

  InputStats<T> input_stats;
  for (size_t i = 0; i < num_lanes; ++i) {
    input_stats.Notify(v[i]);
  }
  return input_stats;
}

struct SharedState {
#if HAVE_PARALLEL_IPS4O
  const unsigned max_threads = hwy::LimitsMax<unsigned>();  // 16 for Table 1a
  ips4o::StdThreadPool pool{static_cast<int>(
      HWY_MIN(max_threads, std::thread::hardware_concurrency() / 2))};
#endif
};

// Adapters from Run's num_keys to vqsort-inl.h num_lanes.
template <typename KeyType, class Order>
void CallHeapSort(KeyType* keys, const size_t num_keys, Order) {
  const detail::MakeTraits<KeyType, Order> st;
  using LaneType = typename decltype(st)::LaneType;
  return detail::HeapSort(st, reinterpret_cast<LaneType*>(keys),
                          num_keys * st.LanesPerKey());
}
template <typename KeyType, class Order>
void CallHeapPartialSort(KeyType* keys, const size_t num_keys,
                         const size_t k_keys, Order) {
  const detail::MakeTraits<KeyType, Order> st;
  using LaneType = typename decltype(st)::LaneType;
  detail::HeapPartialSort(st, reinterpret_cast<LaneType*>(keys),
                          num_keys * st.LanesPerKey(),
                          k_keys * st.LanesPerKey());
}
template <typename KeyType, class Order>
void CallHeapSelect(KeyType* keys, const size_t num_keys, const size_t k_keys,
                    Order) {
  const detail::MakeTraits<KeyType, Order> st;
  using LaneType = typename decltype(st)::LaneType;
  detail::HeapSelect(st, reinterpret_cast<LaneType*>(keys),
                     num_keys * st.LanesPerKey(), k_keys * st.LanesPerKey());
}

template <typename KeyType, class Order>
void Run(Algo algo, KeyType* inout, size_t num_keys, SharedState& shared,
         size_t /*thread*/, size_t k_keys, Order) {
  const std::less<KeyType> less;
  const std::greater<KeyType> greater;

  constexpr bool kAscending = Order::IsAscending();

#if !HAVE_PARALLEL_IPS4O
  (void)shared;
#endif

  switch (algo) {
#if HAVE_INTEL && HWY_TARGET <= HWY_AVX3
    case Algo::kIntel:
      return avx512_qsort<KeyType>(inout, static_cast<int64_t>(num_keys));
#endif

#if HAVE_AVX2SORT
    case Algo::kSEA:
      return avx2::quicksort(inout, static_cast<int>(num_keys));
#endif

#if HAVE_IPS4O
    case Algo::kIPS4O:
      if (kAscending) {
        return ips4o::sort(inout, inout + num_keys, less);
      } else {
        return ips4o::sort(inout, inout + num_keys, greater);
      }
#endif

#if HAVE_PARALLEL_IPS4O
    case Algo::kParallelIPS4O:
      if (kAscending) {
        return ips4o::parallel::sort(inout, inout + num_keys, less,
                                     shared.pool);
      } else {
        return ips4o::parallel::sort(inout, inout + num_keys, greater,
                                     shared.pool);
      }
#endif

#if HAVE_SORT512
    case Algo::kSort512:
      HWY_ABORT("not supported");
      //    return Sort512::Sort(inout, num_keys);
#endif

#if HAVE_PDQSORT
    case Algo::kPDQ:
      if (kAscending) {
        return boost::sort::pdqsort_branchless(inout, inout + num_keys, less);
      } else {
        return boost::sort::pdqsort_branchless(inout, inout + num_keys,
                                               greater);
      }
#endif

#if HAVE_VXSORT
    case Algo::kVXSort: {
#if (VXSORT_AVX3 && HWY_TARGET != HWY_AVX3) || \
    (!VXSORT_AVX3 && HWY_TARGET != HWY_AVX2)
      fprintf(stderr, "Do not call for target %s\n",
              hwy::TargetName(HWY_TARGET));
      return;
#else
#if VXSORT_AVX3
      vxsort::vxsort<KeyType, vxsort::AVX512> vx;
#else
      vxsort::vxsort<KeyType, vxsort::AVX2> vx;
#endif
      if (kAscending) {
        return vx.sort(inout, inout + num_keys - 1);
      } else {
        fprintf(stderr, "Skipping VX - does not support descending order\n");
        return;
      }
#endif  // enabled for this target
    }
#endif  // HAVE_VXSORT

    case Algo::kStdSort:
      if (kAscending) {
        return std::sort(inout, inout + num_keys, less);
      } else {
        return std::sort(inout, inout + num_keys, greater);
      }
    case Algo::kStdPartialSort:
      if (kAscending) {
        return std::partial_sort(inout, inout + k_keys, inout + num_keys, less);
      } else {
        return std::partial_sort(inout, inout + k_keys, inout + num_keys,
                                 greater);
      }
    case Algo::kStdSelect:
      if (kAscending) {
        return std::nth_element(inout, inout + k_keys, inout + num_keys, less);
      } else {
        return std::nth_element(inout, inout + k_keys, inout + num_keys,
                                greater);
      }

    case Algo::kVQSort:
      return VQSort(inout, num_keys, Order());
    case Algo::kVQPartialSort:
      return VQPartialSort(inout, num_keys, k_keys, Order());
    case Algo::kVQSelect:
      return VQSelect(inout, num_keys, k_keys, Order());

    case Algo::kHeapSort:
      return CallHeapSort(inout, num_keys, Order());
    case Algo::kHeapPartialSort:
      return CallHeapPartialSort(inout, num_keys, k_keys, Order());
    case Algo::kHeapSelect:
      return CallHeapSelect(inout, num_keys, k_keys, Order());

    default:
      HWY_ABORT("Not implemented");
  }
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_CONTRIB_SORT_ALGO_TOGGLE
