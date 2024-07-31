// Copyright 2022 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_CRC_INTERNAL_CRC32_X86_ARM_COMBINED_SIMD_H_
#define ABSL_CRC_INTERNAL_CRC32_X86_ARM_COMBINED_SIMD_H_

#include <cstdint>

#include "absl/base/config.h"

// -------------------------------------------------------------------------
// Many x86 and ARM machines have CRC acceleration hardware.
// We can do a faster version of Extend() on such machines.
// We define a translation layer for both x86 and ARM for the ease of use and
// most performance gains.

// This implementation requires 64-bit CRC instructions (part of SSE 4.2) and
// PCLMULQDQ instructions. 32-bit builds with SSE 4.2 do exist, so the
// __x86_64__ condition is necessary.
#if defined(__x86_64__) && defined(__SSE4_2__) && defined(__PCLMUL__)

#include <x86intrin.h>
#define ABSL_CRC_INTERNAL_HAVE_X86_SIMD

#elif defined(_MSC_VER) && !defined(__clang__) && defined(__AVX__) && \
    defined(_M_AMD64)

// MSVC AVX (/arch:AVX) implies SSE 4.2 and PCLMULQDQ.
#include <intrin.h>
#define ABSL_CRC_INTERNAL_HAVE_X86_SIMD

#elif defined(__aarch64__) && defined(__LITTLE_ENDIAN__) &&                 \
    defined(__ARM_FEATURE_CRC32) && defined(ABSL_INTERNAL_HAVE_ARM_NEON) && \
    defined(__ARM_FEATURE_CRYPTO)

#include <arm_acle.h>
#include <arm_neon.h>
#define ABSL_CRC_INTERNAL_HAVE_ARM_SIMD

#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

#if defined(ABSL_CRC_INTERNAL_HAVE_ARM_SIMD) || \
    defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD)

#if defined(ABSL_CRC_INTERNAL_HAVE_ARM_SIMD)
using V128 = uint64x2_t;
#else
// Note: Do not use __m128i_u, it is not portable.
// Use V128_LoadU() perform an unaligned load from __m128i*.
using V128 = __m128i;
#endif

// Starting with the initial value in |crc|, accumulates a CRC32 value for
// unsigned integers of different sizes.
uint32_t CRC32_u8(uint32_t crc, uint8_t v);

uint32_t CRC32_u16(uint32_t crc, uint16_t v);

uint32_t CRC32_u32(uint32_t crc, uint32_t v);

uint32_t CRC32_u64(uint32_t crc, uint64_t v);

// Loads 128 bits of integer data. |src| must be 16-byte aligned.
V128 V128_Load(const V128* src);

// Load 128 bits of integer data. |src| does not need to be aligned.
V128 V128_LoadU(const V128* src);

// Store 128 bits of integer data. |src| must be 16-byte aligned.
void V128_Store(V128* dst, V128 data);

// Polynomially multiplies the high 64 bits of |l| and |r|.
V128 V128_PMulHi(const V128 l, const V128 r);

// Polynomially multiplies the low 64 bits of |l| and |r|.
V128 V128_PMulLow(const V128 l, const V128 r);

// Polynomially multiplies the low 64 bits of |r| and high 64 bits of |l|.
V128 V128_PMul01(const V128 l, const V128 r);

// Polynomially multiplies the low 64 bits of |l| and high 64 bits of |r|.
V128 V128_PMul10(const V128 l, const V128 r);

// Produces a XOR operation of |l| and |r|.
V128 V128_Xor(const V128 l, const V128 r);

// Produces an AND operation of |l| and |r|.
V128 V128_And(const V128 l, const V128 r);

// Sets two 64 bit integers to one 128 bit vector. The order is reverse.
// dst[63:0] := |r|
// dst[127:64] := |l|
V128 V128_From2x64(const uint64_t l, const uint64_t r);

// Shift |l| right by |imm| bytes while shifting in zeros.
template <int imm>
V128 V128_ShiftRight(const V128 l);

// Extracts a 32-bit integer from |l|, selected with |imm|.
template <int imm>
int V128_Extract32(const V128 l);

// Extracts a 64-bit integer from |l|, selected with |imm|.
template <int imm>
uint64_t V128_Extract64(const V128 l);

// Extracts the low 64 bits from V128.
int64_t V128_Low64(const V128 l);

// Left-shifts packed 64-bit integers in l by r.
V128 V128_ShiftLeft64(const V128 l, const V128 r);

#endif

#if defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD)

inline uint32_t CRC32_u8(uint32_t crc, uint8_t v) {
  return _mm_crc32_u8(crc, v);
}

inline uint32_t CRC32_u16(uint32_t crc, uint16_t v) {
  return _mm_crc32_u16(crc, v);
}

inline uint32_t CRC32_u32(uint32_t crc, uint32_t v) {
  return _mm_crc32_u32(crc, v);
}

inline uint32_t CRC32_u64(uint32_t crc, uint64_t v) {
  return static_cast<uint32_t>(_mm_crc32_u64(crc, v));
}

inline V128 V128_Load(const V128* src) { return _mm_load_si128(src); }

inline V128 V128_LoadU(const V128* src) { return _mm_loadu_si128(src); }

inline void V128_Store(V128* dst, V128 data) { _mm_store_si128(dst, data); }

inline V128 V128_PMulHi(const V128 l, const V128 r) {
  return _mm_clmulepi64_si128(l, r, 0x11);
}

inline V128 V128_PMulLow(const V128 l, const V128 r) {
  return _mm_clmulepi64_si128(l, r, 0x00);
}

inline V128 V128_PMul01(const V128 l, const V128 r) {
  return _mm_clmulepi64_si128(l, r, 0x01);
}

inline V128 V128_PMul10(const V128 l, const V128 r) {
  return _mm_clmulepi64_si128(l, r, 0x10);
}

inline V128 V128_Xor(const V128 l, const V128 r) { return _mm_xor_si128(l, r); }

inline V128 V128_And(const V128 l, const V128 r) { return _mm_and_si128(l, r); }

inline V128 V128_From2x64(const uint64_t l, const uint64_t r) {
  return _mm_set_epi64x(static_cast<int64_t>(l), static_cast<int64_t>(r));
}

template <int imm>
inline V128 V128_ShiftRight(const V128 l) {
  return _mm_srli_si128(l, imm);
}

template <int imm>
inline int V128_Extract32(const V128 l) {
  return _mm_extract_epi32(l, imm);
}

template <int imm>
inline uint64_t V128_Extract64(const V128 l) {
  return static_cast<uint64_t>(_mm_extract_epi64(l, imm));
}

inline int64_t V128_Low64(const V128 l) { return _mm_cvtsi128_si64(l); }

inline V128 V128_ShiftLeft64(const V128 l, const V128 r) {
  return _mm_sll_epi64(l, r);
}

#elif defined(ABSL_CRC_INTERNAL_HAVE_ARM_SIMD)

inline uint32_t CRC32_u8(uint32_t crc, uint8_t v) { return __crc32cb(crc, v); }

inline uint32_t CRC32_u16(uint32_t crc, uint16_t v) {
  return __crc32ch(crc, v);
}

inline uint32_t CRC32_u32(uint32_t crc, uint32_t v) {
  return __crc32cw(crc, v);
}

inline uint32_t CRC32_u64(uint32_t crc, uint64_t v) {
  return __crc32cd(crc, v);
}

inline V128 V128_Load(const V128* src) {
  return vld1q_u64(reinterpret_cast<const uint64_t*>(src));
}

inline V128 V128_LoadU(const V128* src) {
  return vld1q_u64(reinterpret_cast<const uint64_t*>(src));
}

inline void V128_Store(V128* dst, V128 data) {
  vst1q_u64(reinterpret_cast<uint64_t*>(dst), data);
}

// Using inline assembly as clang does not generate the pmull2 instruction and
// performance drops by 15-20%.
// TODO(b/193678732): Investigate why there is a slight performance hit when
// using intrinsics instead of inline assembly.
inline V128 V128_PMulHi(const V128 l, const V128 r) {
  uint64x2_t res;
  __asm__ __volatile__("pmull2 %0.1q, %1.2d, %2.2d \n\t"
                       : "=w"(res)
                       : "w"(l), "w"(r));
  return res;
}

// TODO(b/193678732): Investigate why the compiler decides to move the constant
// loop multiplicands from GPR to Neon registers every loop iteration.
inline V128 V128_PMulLow(const V128 l, const V128 r) {
  uint64x2_t res;
  __asm__ __volatile__("pmull %0.1q, %1.1d, %2.1d \n\t"
                       : "=w"(res)
                       : "w"(l), "w"(r));
  return res;
}

inline V128 V128_PMul01(const V128 l, const V128 r) {
  return reinterpret_cast<V128>(vmull_p64(
      reinterpret_cast<poly64_t>(vget_high_p64(vreinterpretq_p64_u64(l))),
      reinterpret_cast<poly64_t>(vget_low_p64(vreinterpretq_p64_u64(r)))));
}

inline V128 V128_PMul10(const V128 l, const V128 r) {
  return reinterpret_cast<V128>(vmull_p64(
      reinterpret_cast<poly64_t>(vget_low_p64(vreinterpretq_p64_u64(l))),
      reinterpret_cast<poly64_t>(vget_high_p64(vreinterpretq_p64_u64(r)))));
}

inline V128 V128_Xor(const V128 l, const V128 r) { return veorq_u64(l, r); }

inline V128 V128_And(const V128 l, const V128 r) { return vandq_u64(l, r); }

inline V128 V128_From2x64(const uint64_t l, const uint64_t r) {
  return vcombine_u64(vcreate_u64(r), vcreate_u64(l));
}

template <int imm>
inline V128 V128_ShiftRight(const V128 l) {
  return vreinterpretq_u64_s8(
      vextq_s8(vreinterpretq_s8_u64(l), vdupq_n_s8(0), imm));
}

template <int imm>
inline int V128_Extract32(const V128 l) {
  return vgetq_lane_s32(vreinterpretq_s32_u64(l), imm);
}

template <int imm>
inline uint64_t V128_Extract64(const V128 l) {
  return vgetq_lane_u64(l, imm);
}

inline int64_t V128_Low64(const V128 l) {
  return vgetq_lane_s64(vreinterpretq_s64_u64(l), 0);
}

inline V128 V128_ShiftLeft64(const V128 l, const V128 r) {
  return vshlq_u64(l, vreinterpretq_s64_u64(r));
}

#endif

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CRC_INTERNAL_CRC32_X86_ARM_COMBINED_SIMD_H_
