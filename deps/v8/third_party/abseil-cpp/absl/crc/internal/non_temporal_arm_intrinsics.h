// Copyright 2022 The Abseil Authors
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

#ifndef ABSL_CRC_INTERNAL_NON_TEMPORAL_ARM_INTRINSICS_H_
#define ABSL_CRC_INTERNAL_NON_TEMPORAL_ARM_INTRINSICS_H_

#include "absl/base/config.h"

#ifdef __aarch64__
#include <arm_neon.h>

typedef int64x2_t __m128i; /* 128-bit vector containing integers */
#define vreinterpretq_m128i_s32(x) vreinterpretq_s64_s32(x)
#define vreinterpretq_s64_m128i(x) (x)

// Guarantees that every preceding store is globally visible before any
// subsequent store.
// https://msdn.microsoft.com/en-us/library/5h2w73d1%28v=vs.90%29.aspx
static inline __attribute__((always_inline)) void _mm_sfence(void) {
  __sync_synchronize();
}

// Load 128-bits of integer data from unaligned memory into dst. This intrinsic
// may perform better than _mm_loadu_si128 when the data crosses a cache line
// boundary.
//
//   dst[127:0] := MEM[mem_addr+127:mem_addr]
//
// https://software.intel.com/sites/landingpage/IntrinsicsGuide/#text=_mm_lddqu_si128
#define _mm_lddqu_si128 _mm_loadu_si128

// Loads 128-bit value. :
// https://msdn.microsoft.com/zh-cn/library/f4k12ae8(v=vs.90).aspx
static inline __attribute__((always_inline)) __m128i _mm_loadu_si128(
    const __m128i *p) {
  return vreinterpretq_m128i_s32(vld1q_s32((const int32_t *)p));
}

// Stores the data in a to the address p without polluting the caches.  If the
// cache line containing address p is already in the cache, the cache will be
// updated.
// https://msdn.microsoft.com/en-us/library/ba08y07y%28v=vs.90%29.aspx
static inline __attribute__((always_inline)) void _mm_stream_si128(__m128i *p,
                                                                   __m128i a) {
#if ABSL_HAVE_BUILTIN(__builtin_nontemporal_store)
  __builtin_nontemporal_store(a, p);
#else
  vst1q_s64((int64_t *)p, vreinterpretq_s64_m128i(a));
#endif
}

// Sets the 16 signed 8-bit integer values.
// https://msdn.microsoft.com/en-us/library/x0cx8zd3(v=vs.90).aspx
static inline __attribute__((always_inline)) __m128i _mm_set_epi8(
    signed char b15, signed char b14, signed char b13, signed char b12,
    signed char b11, signed char b10, signed char b9, signed char b8,
    signed char b7, signed char b6, signed char b5, signed char b4,
    signed char b3, signed char b2, signed char b1, signed char b0) {
  int8_t __attribute__((aligned(16)))
  data[16] = {(int8_t)b0,  (int8_t)b1,  (int8_t)b2,  (int8_t)b3,
              (int8_t)b4,  (int8_t)b5,  (int8_t)b6,  (int8_t)b7,
              (int8_t)b8,  (int8_t)b9,  (int8_t)b10, (int8_t)b11,
              (int8_t)b12, (int8_t)b13, (int8_t)b14, (int8_t)b15};
  return (__m128i)vld1q_s8(data);
}
#endif  // __aarch64__

#endif  // ABSL_CRC_INTERNAL_NON_TEMPORAL_ARM_INTRINSICS_H_
