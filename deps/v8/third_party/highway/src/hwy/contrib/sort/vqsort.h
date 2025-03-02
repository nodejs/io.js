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

// Interface to vectorized quicksort with dynamic dispatch. For static dispatch
// without any DLLEXPORT, avoid including this header and instead define
// VQSORT_ONLY_STATIC, then call VQSortStatic* in vqsort-inl.h.
//
// Blog post: https://tinyurl.com/vqsort-blog
// Paper with measurements: https://arxiv.org/abs/2205.05982
//
// To ensure the overhead of using wide vectors (e.g. AVX2 or AVX-512) is
// worthwhile, we recommend using this code for sorting arrays whose size is at
// least 100 KiB. See the README for details.

#ifndef HIGHWAY_HWY_CONTRIB_SORT_VQSORT_H_
#define HIGHWAY_HWY_CONTRIB_SORT_VQSORT_H_

// IWYU pragma: begin_exports
#include <stddef.h>

#include "hwy/base.h"
#include "hwy/contrib/sort/order.h"  // SortAscending
// IWYU pragma: end_exports

namespace hwy {

// Vectorized Quicksort: sorts keys[0, n). Does not preserve the ordering of
// equivalent keys (defined as: neither greater nor less than another).
// Dispatches to the best available instruction set. Does not allocate memory.
// Uses about 1.2 KiB stack plus an internal 3-word TLS cache for random state.
HWY_CONTRIB_DLLEXPORT void VQSort(uint16_t* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(uint16_t* HWY_RESTRICT keys, size_t n,
                                  SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSort(uint32_t* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(uint32_t* HWY_RESTRICT keys, size_t n,
                                  SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSort(uint64_t* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(uint64_t* HWY_RESTRICT keys, size_t n,
                                  SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSort(int16_t* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(int16_t* HWY_RESTRICT keys, size_t n,
                                  SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSort(int32_t* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(int32_t* HWY_RESTRICT keys, size_t n,
                                  SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSort(int64_t* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(int64_t* HWY_RESTRICT keys, size_t n,
                                  SortDescending);

// These two must only be called if hwy::HaveFloat16() is true.
HWY_CONTRIB_DLLEXPORT void VQSort(float16_t* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(float16_t* HWY_RESTRICT keys, size_t n,
                                  SortDescending);

HWY_CONTRIB_DLLEXPORT void VQSort(float* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(float* HWY_RESTRICT keys, size_t n,
                                  SortDescending);

// These two must only be called if hwy::HaveFloat64() is true.
HWY_CONTRIB_DLLEXPORT void VQSort(double* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(double* HWY_RESTRICT keys, size_t n,
                                  SortDescending);

HWY_CONTRIB_DLLEXPORT void VQSort(K32V32* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(K32V32* HWY_RESTRICT keys, size_t n,
                                  SortDescending);

// 128-bit types: `n` is still in units of the 128-bit keys.
HWY_CONTRIB_DLLEXPORT void VQSort(uint128_t* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(uint128_t* HWY_RESTRICT keys, size_t n,
                                  SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSort(K64V64* HWY_RESTRICT keys, size_t n,
                                  SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSort(K64V64* HWY_RESTRICT keys, size_t n,
                                  SortDescending);

// Vectorized partial Quicksort:
// Rearranges elements such that the range [0, k) contains the sorted first k
// elements in the range [0, n). Does not preserve the ordering of equivalent
// keys (defined as: neither greater nor less than another).
// Dispatches to the best available instruction set. Does not allocate memory.
// Uses about 1.2 KiB stack plus an internal 3-word TLS cache for random state.
HWY_CONTRIB_DLLEXPORT void VQPartialSort(uint16_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(uint16_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(uint32_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(uint32_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(uint64_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(uint64_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(int16_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(int16_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(int32_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(int32_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(int64_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(int64_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);

// These two must only be called if hwy::HaveFloat16() is true.
HWY_CONTRIB_DLLEXPORT void VQPartialSort(float16_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(float16_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);

HWY_CONTRIB_DLLEXPORT void VQPartialSort(float* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(float* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);

// These two must only be called if hwy::HaveFloat64() is true.
HWY_CONTRIB_DLLEXPORT void VQPartialSort(double* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(double* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);

HWY_CONTRIB_DLLEXPORT void VQPartialSort(K32V32* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(K32V32* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);

// 128-bit types: `n` and `k` are still in units of the 128-bit keys.
HWY_CONTRIB_DLLEXPORT void VQPartialSort(uint128_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(uint128_t* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(K64V64* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQPartialSort(K64V64* HWY_RESTRICT keys, size_t n,
                                         size_t k, SortDescending);

// Vectorized Quickselect:
// rearranges elements in [0, n) such that:
// The element pointed at by kth is changed to whatever element would occur in
// that position if [0, n) were sorted. All of the elements before this new kth
// element are less than or equal to the elements after the new kth element.
HWY_CONTRIB_DLLEXPORT void VQSelect(uint16_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(uint16_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSelect(uint32_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(uint32_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSelect(uint64_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(uint64_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSelect(int16_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(int16_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSelect(int32_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(int32_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSelect(int64_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(int64_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);

// These two must only be called if hwy::HaveFloat16() is true.
HWY_CONTRIB_DLLEXPORT void VQSelect(float16_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(float16_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);

HWY_CONTRIB_DLLEXPORT void VQSelect(float* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(float* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);

// These two must only be called if hwy::HaveFloat64() is true.
HWY_CONTRIB_DLLEXPORT void VQSelect(double* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(double* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);

HWY_CONTRIB_DLLEXPORT void VQSelect(K32V32* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(K32V32* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);

// 128-bit types: `n` and `k` are still in units of the 128-bit keys.
HWY_CONTRIB_DLLEXPORT void VQSelect(uint128_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(uint128_t* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);
HWY_CONTRIB_DLLEXPORT void VQSelect(K64V64* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortAscending);
HWY_CONTRIB_DLLEXPORT void VQSelect(K64V64* HWY_RESTRICT keys, size_t n,
                                    size_t k, SortDescending);

// User-level caching is no longer required, so this class is no longer
// beneficial. We recommend using the simpler VQSort() interface instead, and
// retain this class only for compatibility. It now just calls VQSort.
class HWY_CONTRIB_DLLEXPORT Sorter {
 public:
  Sorter();
  ~Sorter() { Delete(); }

  // Move-only
  Sorter(const Sorter&) = delete;
  Sorter& operator=(const Sorter&) = delete;
  Sorter(Sorter&& /*other*/) {}
  Sorter& operator=(Sorter&& /*other*/) { return *this; }

  void operator()(uint16_t* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(uint16_t* HWY_RESTRICT keys, size_t n, SortDescending) const;
  void operator()(uint32_t* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(uint32_t* HWY_RESTRICT keys, size_t n, SortDescending) const;
  void operator()(uint64_t* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(uint64_t* HWY_RESTRICT keys, size_t n, SortDescending) const;

  void operator()(int16_t* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(int16_t* HWY_RESTRICT keys, size_t n, SortDescending) const;
  void operator()(int32_t* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(int32_t* HWY_RESTRICT keys, size_t n, SortDescending) const;
  void operator()(int64_t* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(int64_t* HWY_RESTRICT keys, size_t n, SortDescending) const;

  // These two must only be called if hwy::HaveFloat16() is true.
  void operator()(float16_t* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(float16_t* HWY_RESTRICT keys, size_t n, SortDescending) const;

  void operator()(float* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(float* HWY_RESTRICT keys, size_t n, SortDescending) const;

  // These two must only be called if hwy::HaveFloat64() is true.
  void operator()(double* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(double* HWY_RESTRICT keys, size_t n, SortDescending) const;

  void operator()(uint128_t* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(uint128_t* HWY_RESTRICT keys, size_t n, SortDescending) const;

  void operator()(K64V64* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(K64V64* HWY_RESTRICT keys, size_t n, SortDescending) const;

  void operator()(K32V32* HWY_RESTRICT keys, size_t n, SortAscending) const;
  void operator()(K32V32* HWY_RESTRICT keys, size_t n, SortDescending) const;

  // Unused
  static void Fill24Bytes(const void*, size_t, void*);
  static bool HaveFloat64();  // Can also use hwy::HaveFloat64 directly.

 private:
  void Delete();

  template <typename T>
  T* Get() const {
    return unused_;
  }

#if HWY_COMPILER_CLANG
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4700, ignored "-Wunused-private-field")
#endif
  void* unused_ = nullptr;
#if HWY_COMPILER_CLANG
  HWY_DIAGNOSTICS(pop)
#endif
};

// Used by vqsort-inl.h unless VQSORT_ONLY_STATIC.
HWY_CONTRIB_DLLEXPORT bool Fill16BytesSecure(void* bytes);

// Unused, only provided for binary compatibility.
HWY_CONTRIB_DLLEXPORT uint64_t* GetGeneratorState();

}  // namespace hwy

#endif  // HIGHWAY_HWY_CONTRIB_SORT_VQSORT_H_
