// Copyright 2023 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: prefetch.h
// -----------------------------------------------------------------------------
//
// This header file defines prefetch functions to prefetch memory contents
// into the first level cache (L1) for the current CPU. The prefetch logic
// offered in this header is limited to prefetching first level cachelines
// only, and is aimed at relatively 'simple' prefetching logic.
//
#ifndef ABSL_BASE_PREFETCH_H_
#define ABSL_BASE_PREFETCH_H_

#include "absl/base/attributes.h"
#include "absl/base/config.h"

#if defined(ABSL_INTERNAL_HAVE_SSE)
#include <xmmintrin.h>
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#if defined(ABSL_INTERNAL_HAVE_SSE)
#pragma intrinsic(_mm_prefetch)
#endif
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN

// Moves data into the L1 cache before it is read, or "prefetches" it.
//
// The value of `addr` is the address of the memory to prefetch. If
// the target and compiler support it, data prefetch instructions are
// generated. If the prefetch is done some time before the memory is
// read, it may be in the cache by the time the read occurs.
//
// This method prefetches data with the highest degree of temporal locality;
// data is prefetched where possible into all levels of the cache.
//
// Incorrect or gratuitous use of this function can degrade performance.
// Use this function only when representative benchmarks show an improvement.
//
// Example:
//
//  // Computes incremental checksum for `data`.
//  int ComputeChecksum(int sum, absl::string_view data);
//
//  // Computes cumulative checksum for all values in `data`
//  int ComputeChecksum(absl::Span<const std::string> data) {
//    int sum = 0;
//    auto it = data.begin();
//    auto pit = data.begin();
//    auto end = data.end();
//    for (int dist = 8; dist > 0 && pit != data.end(); --dist, ++pit) {
//      absl::PrefetchToLocalCache(pit->data());
//    }
//    for (; pit != end; ++pit, ++it) {
//      sum = ComputeChecksum(sum, *it);
//      absl::PrefetchToLocalCache(pit->data());
//    }
//    for (; it != end; ++it) {
//      sum = ComputeChecksum(sum, *it);
//    }
//    return sum;
//  }
//
void PrefetchToLocalCache(const void* addr);

// Moves data into the L1 cache before it is read, or "prefetches" it.
//
// This function is identical to `PrefetchToLocalCache()` except that it has
// non-temporal locality: the fetched data should not be left in any of the
// cache tiers. This is useful for cases where the data is used only once /
// short term, for example, invoking a destructor on an object.
//
// Incorrect or gratuitous use of this function can degrade performance.
// Use this function only when representative benchmarks show an improvement.
//
// Example:
//
//  template <typename Iterator>
//  void DestroyPointers(Iterator begin, Iterator end) {
//    size_t distance = std::min(8U, bars.size());
//
//    int dist = 8;
//    auto prefetch_it = begin;
//    while (prefetch_it != end && --dist;) {
//      absl::PrefetchToLocalCacheNta(*prefetch_it++);
//    }
//    while (prefetch_it != end) {
//      delete *begin++;
//      absl::PrefetchToLocalCacheNta(*prefetch_it++);
//    }
//    while (begin != end) {
//      delete *begin++;
//    }
//  }
//
void PrefetchToLocalCacheNta(const void* addr);

// Moves data into the L1 cache with the intent to modify it.
//
// This function is similar to `PrefetchToLocalCache()` except that it
// prefetches cachelines with an 'intent to modify' This typically includes
// invalidating cache entries for this address in all other cache tiers, and an
// exclusive access intent.
//
// Incorrect or gratuitous use of this function can degrade performance. As this
// function can invalidate cached cachelines on other caches and computer cores,
// incorrect usage of this function can have an even greater negative impact
// than incorrect regular prefetches.
// Use this function only when representative benchmarks show an improvement.
//
// Example:
//
//  void* Arena::Allocate(size_t size) {
//    void* ptr = AllocateBlock(size);
//    absl::PrefetchToLocalCacheForWrite(ptr);
//    return ptr;
//  }
//
void PrefetchToLocalCacheForWrite(const void* addr);

#if ABSL_HAVE_BUILTIN(__builtin_prefetch) || defined(__GNUC__)

#define ABSL_HAVE_PREFETCH 1

// See __builtin_prefetch:
// https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html.
//
ABSL_ATTRIBUTE_ALWAYS_INLINE inline void PrefetchToLocalCache(
    const void* addr) {
  __builtin_prefetch(addr, 0, 3);
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void PrefetchToLocalCacheNta(
    const void* addr) {
  __builtin_prefetch(addr, 0, 0);
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void PrefetchToLocalCacheForWrite(
    const void* addr) {
  // [x86] gcc/clang don't generate PREFETCHW for __builtin_prefetch(.., 1)
  // unless -march=broadwell or newer; this is not generally the default, so we
  // manually emit prefetchw. PREFETCHW is recognized as a no-op on older Intel
  // processors and has been present on AMD processors since the K6-2.
#if defined(__x86_64__) && !defined(__PRFCHW__)
  asm("prefetchw %0" : : "m"(*reinterpret_cast<const char*>(addr)));
#else
  __builtin_prefetch(addr, 1, 3);
#endif
}

#elif defined(ABSL_INTERNAL_HAVE_SSE)

#define ABSL_HAVE_PREFETCH 1

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void PrefetchToLocalCache(
    const void* addr) {
  _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0);
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void PrefetchToLocalCacheNta(
    const void* addr) {
  _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_NTA);
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void PrefetchToLocalCacheForWrite(
    const void* addr) {
#if defined(_MM_HINT_ET0)
  _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_ET0);
#elif !defined(_MSC_VER) && defined(__x86_64__)
  // _MM_HINT_ET0 is not universally supported. As we commented further
  // up, PREFETCHW is recognized as a no-op on older Intel processors
  // and has been present on AMD processors since the K6-2. We have this
  // disabled for MSVC compilers as this miscompiles on older MSVC compilers.
  asm("prefetchw %0" : : "m"(*reinterpret_cast<const char*>(addr)));
#endif
}

#else

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void PrefetchToLocalCache(
    const void* addr) {}
ABSL_ATTRIBUTE_ALWAYS_INLINE inline void PrefetchToLocalCacheNta(
    const void* addr) {}
ABSL_ATTRIBUTE_ALWAYS_INLINE inline void PrefetchToLocalCacheForWrite(
    const void* addr) {}

#endif

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_PREFETCH_H_
