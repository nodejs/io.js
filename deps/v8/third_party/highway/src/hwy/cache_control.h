// Copyright 2020 Google LLC
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

#ifndef HIGHWAY_HWY_CACHE_CONTROL_H_
#define HIGHWAY_HWY_CACHE_CONTROL_H_

#include "hwy/base.h"

// Requires SSE2; fails to compile on 32-bit Clang 7 (see
// https://github.com/gperftools/gperftools/issues/946).
#if !defined(__SSE2__) || (HWY_COMPILER_CLANG && HWY_ARCH_X86_32)
#undef HWY_DISABLE_CACHE_CONTROL
#define HWY_DISABLE_CACHE_CONTROL
#endif

#ifndef HWY_DISABLE_CACHE_CONTROL
// intrin.h is sufficient on MSVC and already included by base.h.
#if HWY_ARCH_X86 && !HWY_COMPILER_MSVC
#include <emmintrin.h>  // SSE2
#include <xmmintrin.h>  // _mm_prefetch
#elif HWY_ARCH_ARM_A64
#include <arm_acle.h>
#endif
#endif  // HWY_DISABLE_CACHE_CONTROL

namespace hwy {

// Even if N*sizeof(T) is smaller, Stream may write a multiple of this size.
#define HWY_STREAM_MULTIPLE 16

// The following functions may also require an attribute.
#if HWY_ARCH_X86 && !defined(HWY_DISABLE_CACHE_CONTROL) && !HWY_COMPILER_MSVC
#define HWY_ATTR_CACHE __attribute__((target("sse2")))
#else
#define HWY_ATTR_CACHE
#endif

// Windows.h #defines this, which causes infinite recursion. Temporarily
// undefine to avoid conflict with our function.
// TODO(janwas): remove when this function is removed.
#pragma push_macro("LoadFence")
#undef LoadFence

// Delays subsequent loads until prior loads are visible. Beware of potentially
// differing behavior across architectures and vendors: on Intel but not
// AMD CPUs, also serves as a full fence (waits for all prior instructions to
// complete).
HWY_INLINE HWY_ATTR_CACHE void LoadFence() {
#if HWY_ARCH_X86 && !defined(HWY_DISABLE_CACHE_CONTROL)
  _mm_lfence();
#endif
}

// TODO(janwas): remove when this function is removed. (See above.)
#pragma pop_macro("LoadFence")

// Ensures values written by previous `Stream` calls are visible on the current
// core. This is NOT sufficient for synchronizing across cores; when `Stream`
// outputs are to be consumed by other core(s), the producer must publish
// availability (e.g. via mutex or atomic_flag) after `FlushStream`.
HWY_INLINE HWY_ATTR_CACHE void FlushStream() {
#if HWY_ARCH_X86 && !defined(HWY_DISABLE_CACHE_CONTROL)
  _mm_sfence();
#endif
}

// Optionally begins loading the cache line containing "p" to reduce latency of
// subsequent actual loads.
template <typename T>
HWY_INLINE HWY_ATTR_CACHE void Prefetch(const T* p) {
  (void)p;
#ifndef HWY_DISABLE_CACHE_CONTROL
#if HWY_ARCH_X86
  _mm_prefetch(reinterpret_cast<const char*>(p), _MM_HINT_T0);
#elif HWY_COMPILER_GCC  // includes clang
  // Hint=0 (NTA) behavior differs, but skipping outer caches is probably not
  // desirable, so use the default 3 (keep in caches).
  __builtin_prefetch(p, /*write=*/0, /*hint=*/3);
#endif
#endif  //  HWY_DISABLE_CACHE_CONTROL
}

// Invalidates and flushes the cache line containing "p", if possible.
HWY_INLINE HWY_ATTR_CACHE void FlushCacheline(const void* p) {
#if HWY_ARCH_X86 && !defined(HWY_DISABLE_CACHE_CONTROL)
  _mm_clflush(p);
#else
  (void)p;
#endif
}

// Hints that we are inside a spin loop and potentially reduces power
// consumption and coherency traffic. For example, x86 avoids multiple
// outstanding load requests, which reduces the memory order violation penalty
// when exiting the loop.
HWY_INLINE HWY_ATTR_CACHE void Pause() {
#ifndef HWY_DISABLE_CACHE_CONTROL
#if HWY_ARCH_X86
  _mm_pause();
#elif HWY_ARCH_ARM_A64 && HWY_COMPILER_CLANG
  // This is documented in ACLE and the YIELD instruction is also available in
  // Armv7, but the intrinsic is broken for Armv7 clang, hence A64 only.
  __yield();
#elif HWY_ARCH_ARM && HWY_COMPILER_GCC  // includes clang
  __asm__ volatile("yield" ::: "memory");
#elif HWY_ARCH_PPC && HWY_COMPILER_GCC  // includes clang
  __asm__ volatile("or 27,27,27" ::: "memory");
#endif
#endif  // HWY_DISABLE_CACHE_CONTROL
}

}  // namespace hwy

#endif  // HIGHWAY_HWY_CACHE_CONTROL_H_
