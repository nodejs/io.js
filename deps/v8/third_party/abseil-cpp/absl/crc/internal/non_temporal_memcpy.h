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

#ifndef ABSL_CRC_INTERNAL_NON_TEMPORAL_MEMCPY_H_
#define ABSL_CRC_INTERNAL_NON_TEMPORAL_MEMCPY_H_

#ifdef _MSC_VER
#include <intrin.h>
#endif

#if defined(__SSE__) || defined(__AVX__)
// Pulls in both SSE and AVX intrinsics.
#include <immintrin.h>
#endif

#ifdef __aarch64__
#include "absl/crc/internal/non_temporal_arm_intrinsics.h"
#endif

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

// This non-temporal memcpy does regular load and non-temporal store memory
// copy. It is compatible to both 16-byte aligned and unaligned addresses. If
// data at the destination is not immediately accessed, using non-temporal
// memcpy can save 1 DRAM load of the destination cacheline.
constexpr size_t kCacheLineSize = ABSL_CACHELINE_SIZE;

// If the objects overlap, the behavior is undefined. Uses regular memcpy
// instead of non-temporal memcpy if the required CPU intrinsics are unavailable
// at compile time.
inline void *non_temporal_store_memcpy(void *__restrict dst,
                                       const void *__restrict src, size_t len) {
#if defined(__SSE3__) || defined(__aarch64__) || \
    (defined(_MSC_VER) && defined(__AVX__))
  // This implementation requires SSE3.
  // MSVC cannot target SSE3 directly, but when MSVC targets AVX,
  // SSE3 support is implied.
  uint8_t *d = reinterpret_cast<uint8_t *>(dst);
  const uint8_t *s = reinterpret_cast<const uint8_t *>(src);

  // memcpy() the misaligned header. At the end of this if block, <d> is
  // aligned to a 64-byte cacheline boundary or <len> == 0.
  if (reinterpret_cast<uintptr_t>(d) & (kCacheLineSize - 1)) {
    uintptr_t bytes_before_alignment_boundary =
        kCacheLineSize -
        (reinterpret_cast<uintptr_t>(d) & (kCacheLineSize - 1));
    size_t header_len = (std::min)(bytes_before_alignment_boundary, len);
    assert(bytes_before_alignment_boundary < kCacheLineSize);
    memcpy(d, s, header_len);
    d += header_len;
    s += header_len;
    len -= header_len;
  }

  if (len >= kCacheLineSize) {
    _mm_sfence();
    __m128i *dst_cacheline = reinterpret_cast<__m128i *>(d);
    const __m128i *src_cacheline = reinterpret_cast<const __m128i *>(s);
    constexpr int kOpsPerCacheLine = kCacheLineSize / sizeof(__m128i);
    size_t loops = len / kCacheLineSize;

    while (len >= kCacheLineSize) {
      __m128i temp1, temp2, temp3, temp4;
      temp1 = _mm_lddqu_si128(src_cacheline + 0);
      temp2 = _mm_lddqu_si128(src_cacheline + 1);
      temp3 = _mm_lddqu_si128(src_cacheline + 2);
      temp4 = _mm_lddqu_si128(src_cacheline + 3);
      _mm_stream_si128(dst_cacheline + 0, temp1);
      _mm_stream_si128(dst_cacheline + 1, temp2);
      _mm_stream_si128(dst_cacheline + 2, temp3);
      _mm_stream_si128(dst_cacheline + 3, temp4);
      src_cacheline += kOpsPerCacheLine;
      dst_cacheline += kOpsPerCacheLine;
      len -= kCacheLineSize;
    }
    d += loops * kCacheLineSize;
    s += loops * kCacheLineSize;
    _mm_sfence();
  }

  // memcpy the tail.
  if (len) {
    memcpy(d, s, len);
  }
  return dst;
#else
  // Fallback to regular memcpy.
  return memcpy(dst, src, len);
#endif  // __SSE3__ || __aarch64__ || (_MSC_VER && __AVX__)
}

// If the objects overlap, the behavior is undefined. Uses regular memcpy
// instead of non-temporal memcpy if the required CPU intrinsics are unavailable
// at compile time.
#if ABSL_HAVE_CPP_ATTRIBUTE(gnu::target) && \
    (defined(__x86_64__) || defined(__i386__))
[[gnu::target("avx")]]
#endif
inline void *non_temporal_store_memcpy_avx(void *__restrict dst,
                                           const void *__restrict src,
                                           size_t len) {
  // This function requires AVX. For clang and gcc we compile it with AVX even
  // if the translation unit isn't built with AVX support. This works because we
  // only select this implementation at runtime if the CPU supports AVX.
#if defined(__SSE3__) || (defined(_MSC_VER) && defined(__AVX__))
  uint8_t *d = reinterpret_cast<uint8_t *>(dst);
  const uint8_t *s = reinterpret_cast<const uint8_t *>(src);

  // memcpy() the misaligned header. At the end of this if block, <d> is
  // aligned to a 64-byte cacheline boundary or <len> == 0.
  if (reinterpret_cast<uintptr_t>(d) & (kCacheLineSize - 1)) {
    uintptr_t bytes_before_alignment_boundary =
        kCacheLineSize -
        (reinterpret_cast<uintptr_t>(d) & (kCacheLineSize - 1));
    size_t header_len = (std::min)(bytes_before_alignment_boundary, len);
    assert(bytes_before_alignment_boundary < kCacheLineSize);
    memcpy(d, s, header_len);
    d += header_len;
    s += header_len;
    len -= header_len;
  }

  if (len >= kCacheLineSize) {
    _mm_sfence();
    __m256i *dst_cacheline = reinterpret_cast<__m256i *>(d);
    const __m256i *src_cacheline = reinterpret_cast<const __m256i *>(s);
    constexpr int kOpsPerCacheLine = kCacheLineSize / sizeof(__m256i);
    size_t loops = len / kCacheLineSize;

    while (len >= kCacheLineSize) {
      __m256i temp1, temp2;
      temp1 = _mm256_lddqu_si256(src_cacheline + 0);
      temp2 = _mm256_lddqu_si256(src_cacheline + 1);
      _mm256_stream_si256(dst_cacheline + 0, temp1);
      _mm256_stream_si256(dst_cacheline + 1, temp2);
      src_cacheline += kOpsPerCacheLine;
      dst_cacheline += kOpsPerCacheLine;
      len -= kCacheLineSize;
    }
    d += loops * kCacheLineSize;
    s += loops * kCacheLineSize;
    _mm_sfence();
  }

  // memcpy the tail.
  if (len) {
    memcpy(d, s, len);
  }
  return dst;
#else
  return memcpy(dst, src, len);
#endif  // __SSE3__ || (_MSC_VER && __AVX__)
}

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CRC_INTERNAL_NON_TEMPORAL_MEMCPY_H_
