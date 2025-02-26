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

#include "hwy/contrib/sort/vqsort.h"  // VQSort

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/sort/vqsort_f16a.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep

// After foreach_target
#include "hwy/contrib/sort/vqsort-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

void SortF16Asc(float16_t* HWY_RESTRICT keys, const size_t num) {
#if HWY_HAVE_FLOAT16
  return VQSortStatic(keys, num, SortAscending());
#else
  (void)keys;
  (void)num;
  HWY_ASSERT(0);
#endif
}

void PartialSortF16Asc(float16_t* HWY_RESTRICT keys, const size_t num,
                       const size_t k) {
#if HWY_HAVE_FLOAT16
  return VQPartialSortStatic(keys, num, k, SortAscending());
#else
  (void)keys;
  (void)num;
  (void)k;
  HWY_ASSERT(0);
#endif
}

void SelectF16Asc(float16_t* HWY_RESTRICT keys, const size_t num,
                  const size_t k) {
#if HWY_HAVE_FLOAT16
  return VQSelectStatic(keys, num, k, SortAscending());
#else
  (void)keys;
  (void)num;
  (void)k;
  HWY_ASSERT(0);
#endif
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_EXPORT(SortF16Asc);
HWY_EXPORT(PartialSortF16Asc);
HWY_EXPORT(SelectF16Asc);
}  // namespace

void VQSort(float16_t* HWY_RESTRICT keys, const size_t n, SortAscending) {
  HWY_DYNAMIC_DISPATCH(SortF16Asc)(keys, n);
}

void VQPartialSort(float16_t* HWY_RESTRICT keys, const size_t n, const size_t k,
                   SortAscending) {
  HWY_DYNAMIC_DISPATCH(PartialSortF16Asc)(keys, n, k);
}

void VQSelect(float16_t* HWY_RESTRICT keys, const size_t n, const size_t k,
              SortAscending) {
  HWY_DYNAMIC_DISPATCH(SelectF16Asc)(keys, n, k);
}

}  // namespace hwy
#endif  // HWY_ONCE
