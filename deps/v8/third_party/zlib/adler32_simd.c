/* adler32_simd.c
 *
 * Copyright 2017 The Chromium Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the Chromium source repository LICENSE file.
 *
 * Per http://en.wikipedia.org/wiki/Adler-32 the adler32 A value (aka s1) is
 * the sum of N input data bytes D1 ... DN,
 *
 *   A = A0 + D1 + D2 + ... + DN
 *
 * where A0 is the initial value.
 *
 * SSE2 _mm_sad_epu8() can be used for byte sums (see http://bit.ly/2wpUOeD,
 * for example) and accumulating the byte sums can use SSE shuffle-adds (see
 * the "Integer" section of http://bit.ly/2erPT8t for details). Arm NEON has
 * similar instructions.
 *
 * The adler32 B value (aka s2) sums the A values from each step:
 *
 *   B0 + (A0 + D1) + (A0 + D1 + D2) + ... + (A0 + D1 + D2 + ... + DN) or
 *
 *       B0 + N.A0 + N.D1 + (N-1).D2 + (N-2).D3 + ... + (N-(N-1)).DN
 *
 * B0 being the initial value. For 32 bytes (ideal for garden-variety SIMD):
 *
 *   B = B0 + 32.A0 + [D1 D2 D3 ... D32] x [32 31 30 ... 1].
 *
 * Adjacent blocks of 32 input bytes can be iterated with the expressions to
 * compute the adler32 s1 s2 of M >> 32 input bytes [1].
 *
 * As M grows, the s1 s2 sums grow. If left unchecked, they would eventually
 * overflow the precision of their integer representation (bad). However, s1
 * and s2 also need to be computed modulo the adler BASE value (reduced). If
 * at most NMAX bytes are processed before a reduce, s1 s2 _cannot_ overflow
 * a uint32_t type (the NMAX constraint) [2].
 *
 * [1] the iterative equations for s2 contain constant factors; these can be
 * hoisted from the n-blocks do loop of the SIMD code.
 *
 * [2] zlib adler32_z() uses this fact to implement NMAX-block-based updates
 * of the adler s1 s2 of uint32_t type (see adler32.c).
 */

#include "adler32_simd.h"

/* Definitions from adler32.c: largest prime smaller than 65536 */
#define BASE 65521U
/* NMAX is the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */
#define NMAX 5552

#if defined(ADLER32_SIMD_SSSE3)

#include <tmmintrin.h>

uint32_t ZLIB_INTERNAL adler32_simd_(  /* SSSE3 */
    uint32_t adler,
    const unsigned char *buf,
    z_size_t len)
{
    /*
     * Split Adler-32 into component sums.
     */
    uint32_t s1 = adler & 0xffff;
    uint32_t s2 = adler >> 16;

    /*
     * Process the data in blocks.
     */
    const unsigned BLOCK_SIZE = 1 << 5;

    z_size_t blocks = len / BLOCK_SIZE;
    len -= blocks * BLOCK_SIZE;

    while (blocks)
    {
        unsigned n = NMAX / BLOCK_SIZE;  /* The NMAX constraint. */
        if (n > blocks)
            n = (unsigned) blocks;
        blocks -= n;

        const __m128i tap1 =
            _mm_setr_epi8(32,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17);
        const __m128i tap2 =
            _mm_setr_epi8(16,15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1);
        const __m128i zero =
            _mm_setr_epi8( 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        const __m128i ones =
            _mm_set_epi16( 1, 1, 1, 1, 1, 1, 1, 1);

        /*
         * Process n blocks of data. At most NMAX data bytes can be
         * processed before s2 must be reduced modulo BASE.
         */
        __m128i v_ps = _mm_set_epi32(0, 0, 0, s1 * n);
        __m128i v_s2 = _mm_set_epi32(0, 0, 0, s2);
        __m128i v_s1 = _mm_set_epi32(0, 0, 0, 0);

        do {
            /*
             * Load 32 input bytes.
             */
            const __m128i bytes1 = _mm_loadu_si128((__m128i*)(buf));
            const __m128i bytes2 = _mm_loadu_si128((__m128i*)(buf + 16));

            /*
             * Add previous block byte sum to v_ps.
             */
            v_ps = _mm_add_epi32(v_ps, v_s1);

            /*
             * Horizontally add the bytes for s1, multiply-adds the
             * bytes by [ 32, 31, 30, ... ] for s2.
             */
            v_s1 = _mm_add_epi32(v_s1, _mm_sad_epu8(bytes1, zero));
            const __m128i mad1 = _mm_maddubs_epi16(bytes1, tap1);
            v_s2 = _mm_add_epi32(v_s2, _mm_madd_epi16(mad1, ones));

            v_s1 = _mm_add_epi32(v_s1, _mm_sad_epu8(bytes2, zero));
            const __m128i mad2 = _mm_maddubs_epi16(bytes2, tap2);
            v_s2 = _mm_add_epi32(v_s2, _mm_madd_epi16(mad2, ones));

            buf += BLOCK_SIZE;

        } while (--n);

        v_s2 = _mm_add_epi32(v_s2, _mm_slli_epi32(v_ps, 5));

        /*
         * Sum epi32 ints v_s1(s2) and accumulate in s1(s2).
         */

#define S23O1 _MM_SHUFFLE(2,3,0,1)  /* A B C D -> B A D C */
#define S1O32 _MM_SHUFFLE(1,0,3,2)  /* A B C D -> C D A B */

        v_s1 = _mm_add_epi32(v_s1, _mm_shuffle_epi32(v_s1, S23O1));
        v_s1 = _mm_add_epi32(v_s1, _mm_shuffle_epi32(v_s1, S1O32));

        s1 += _mm_cvtsi128_si32(v_s1);

        v_s2 = _mm_add_epi32(v_s2, _mm_shuffle_epi32(v_s2, S23O1));
        v_s2 = _mm_add_epi32(v_s2, _mm_shuffle_epi32(v_s2, S1O32));

        s2 = _mm_cvtsi128_si32(v_s2);

#undef S23O1
#undef S1O32

        /*
         * Reduce.
         */
        s1 %= BASE;
        s2 %= BASE;
    }

    /*
     * Handle leftover data.
     */
    if (len) {
        if (len >= 16) {
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);

            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);

            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);

            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);

            len -= 16;
        }

        while (len--) {
            s2 += (s1 += *buf++);
        }

        if (s1 >= BASE)
            s1 -= BASE;
        s2 %= BASE;
    }

    /*
     * Return the recombined sums.
     */
    return s1 | (s2 << 16);
}

#elif defined(ADLER32_SIMD_NEON)

#include <arm_neon.h>

uint32_t ZLIB_INTERNAL adler32_simd_(  /* NEON */
    uint32_t adler,
    const unsigned char *buf,
    z_size_t len)
{
    /*
     * Split Adler-32 into component sums.
     */
    uint32_t s1 = adler & 0xffff;
    uint32_t s2 = adler >> 16;

    /*
     * Serially compute s1 & s2, until the data is 16-byte aligned.
     */
    if ((uintptr_t)buf & 15) {
        while ((uintptr_t)buf & 15) {
            s2 += (s1 += *buf++);
            --len;
        }

        if (s1 >= BASE)
            s1 -= BASE;
        s2 %= BASE;
    }

    /*
     * Process the data in blocks.
     */
    const unsigned BLOCK_SIZE = 1 << 5;

    z_size_t blocks = len / BLOCK_SIZE;
    len -= blocks * BLOCK_SIZE;

    while (blocks)
    {
        unsigned n = NMAX / BLOCK_SIZE;  /* The NMAX constraint. */
        if (n > blocks)
            n = (unsigned) blocks;
        blocks -= n;

        /*
         * Process n blocks of data. At most NMAX data bytes can be
         * processed before s2 must be reduced modulo BASE.
         */
        uint32x4_t v_s2 = (uint32x4_t) { 0, 0, 0, s1 * n };
        uint32x4_t v_s1 = (uint32x4_t) { 0, 0, 0, 0 };

        uint16x8_t v_column_sum_1 = vdupq_n_u16(0);
        uint16x8_t v_column_sum_2 = vdupq_n_u16(0);
        uint16x8_t v_column_sum_3 = vdupq_n_u16(0);
        uint16x8_t v_column_sum_4 = vdupq_n_u16(0);

        do {
            /*
             * Load 32 input bytes.
             */
            const uint8x16_t bytes1 = vld1q_u8((uint8_t*)(buf));
            const uint8x16_t bytes2 = vld1q_u8((uint8_t*)(buf + 16));

            /*
             * Add previous block byte sum to v_s2.
             */
            v_s2 = vaddq_u32(v_s2, v_s1);

            /*
             * Horizontally add the bytes for s1.
             */
            v_s1 = vpadalq_u16(v_s1, vpadalq_u8(vpaddlq_u8(bytes1), bytes2));

            /*
             * Vertically add the bytes for s2.
             */
            v_column_sum_1 = vaddw_u8(v_column_sum_1, vget_low_u8 (bytes1));
            v_column_sum_2 = vaddw_u8(v_column_sum_2, vget_high_u8(bytes1));
            v_column_sum_3 = vaddw_u8(v_column_sum_3, vget_low_u8 (bytes2));
            v_column_sum_4 = vaddw_u8(v_column_sum_4, vget_high_u8(bytes2));

            buf += BLOCK_SIZE;

        } while (--n);

        v_s2 = vshlq_n_u32(v_s2, 5);

        /*
         * Multiply-add bytes by [ 32, 31, 30, ... ] for s2.
         */
        v_s2 = vmlal_u16(v_s2, vget_low_u16 (v_column_sum_1),
            (uint16x4_t) { 32, 31, 30, 29 });
        v_s2 = vmlal_u16(v_s2, vget_high_u16(v_column_sum_1),
            (uint16x4_t) { 28, 27, 26, 25 });
        v_s2 = vmlal_u16(v_s2, vget_low_u16 (v_column_sum_2),
            (uint16x4_t) { 24, 23, 22, 21 });
        v_s2 = vmlal_u16(v_s2, vget_high_u16(v_column_sum_2),
            (uint16x4_t) { 20, 19, 18, 17 });
        v_s2 = vmlal_u16(v_s2, vget_low_u16 (v_column_sum_3),
            (uint16x4_t) { 16, 15, 14, 13 });
        v_s2 = vmlal_u16(v_s2, vget_high_u16(v_column_sum_3),
            (uint16x4_t) { 12, 11, 10,  9 });
        v_s2 = vmlal_u16(v_s2, vget_low_u16 (v_column_sum_4),
            (uint16x4_t) {  8,  7,  6,  5 });
        v_s2 = vmlal_u16(v_s2, vget_high_u16(v_column_sum_4),
            (uint16x4_t) {  4,  3,  2,  1 });

        /*
         * Sum epi32 ints v_s1(s2) and accumulate in s1(s2).
         */
        uint32x2_t sum1 = vpadd_u32(vget_low_u32(v_s1), vget_high_u32(v_s1));
        uint32x2_t sum2 = vpadd_u32(vget_low_u32(v_s2), vget_high_u32(v_s2));
        uint32x2_t s1s2 = vpadd_u32(sum1, sum2);

        s1 += vget_lane_u32(s1s2, 0);
        s2 += vget_lane_u32(s1s2, 1);

        /*
         * Reduce.
         */
        s1 %= BASE;
        s2 %= BASE;
    }

    /*
     * Handle leftover data.
     */
    if (len) {
        if (len >= 16) {
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);

            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);

            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);

            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);
            s2 += (s1 += *buf++);

            len -= 16;
        }

        while (len--) {
            s2 += (s1 += *buf++);
        }

        if (s1 >= BASE)
            s1 -= BASE;
        s2 %= BASE;
    }

    /*
     * Return the recombined sums.
     */
    return s1 | (s2 << 16);
}

#elif defined(ADLER32_SIMD_RVV)
#include <riscv_vector.h>

/*
 * Patch by Simon Hosie, from:
 *    https://github.com/cloudflare/zlib/pull/55
 */

uint32_t ZLIB_INTERNAL adler32_simd_(  /* RVV */
    uint32_t adler,
    const unsigned char *buf,
    unsigned long len)
{
  size_t vl = __riscv_vsetvlmax_e8m2();
  const vuint16m4_t zero16 = __riscv_vmv_v_x_u16m4(0, vl);
  vuint16m4_t a_sum = zero16;
  vuint32m8_t b_sum = __riscv_vmv_v_x_u32m8(0, vl);

  /* Deal with the part which is not a multiple of vl first; because it's
   * easier to zero-stuff the beginning of the checksum than it is to tweak the
   * multipliers and sums for odd lengths afterwards.
   */
  size_t head = len & (vl - 1);
  if (head > 0) {
    vuint8m2_t zero8 = __riscv_vmv_v_x_u8m2(0, vl);
    vuint8m2_t in = __riscv_vle8_v_u8m2(buf, vl);
    in = __riscv_vslideup(zero8, in, vl - head, vl);
    vuint16m4_t in16 = __riscv_vwcvtu_x(in, vl);
    a_sum = in16;
    buf += head;
  }

  /* We have a 32-bit accumulator, and in each iteration we add 22-times a
   * 16-bit value, plus another 16-bit value.  We periodically subtract up to
   * 65535 times BASE to avoid overflow.  b_overflow estimates how often we
   * need to do this subtraction.
   */
  const int b_overflow = BASE / 23;
  int fixup = b_overflow;
  ssize_t iters = (len - head) / vl;
  while (iters > 0) {
    const vuint16m4_t a_overflow = __riscv_vrsub(a_sum, BASE, vl);
    int batch = iters < 22 ? iters : 22;
    iters -= batch;
    b_sum = __riscv_vwmaccu(b_sum, batch, a_sum, vl);
    vuint16m4_t a_batch = zero16, b_batch = zero16;

    /* Do a short batch, where neither a_sum nor b_sum can overflow a 16-bit
     * register.  Then add them back into the main accumulators.
     */
    while (batch-- > 0) {
      vuint8m2_t in8 = __riscv_vle8_v_u8m2(buf, vl);
      buf += vl;
      b_batch = __riscv_vadd(b_batch, a_batch, vl);
      a_batch = __riscv_vwaddu_wv(a_batch, in8, vl);
    }
    vbool4_t ov = __riscv_vmsgeu(a_batch, a_overflow, vl);
    a_sum = __riscv_vadd(a_sum, a_batch, vl);
    a_sum = __riscv_vadd_mu(ov, a_sum, a_sum, 65536 - BASE, vl);
    b_sum = __riscv_vwaddu_wv(b_sum, b_batch, vl);
    if (--fixup <= 0) {
      b_sum = __riscv_vnmsac(b_sum, BASE, __riscv_vsrl(b_sum, 16, vl), vl);
      fixup = b_overflow;
    }
  }
  /* Adjust per-lane sums to have appropriate offsets from the end of the
   * buffer.
   */
  const vuint16m4_t off = __riscv_vrsub(__riscv_vid_v_u16m4(vl), vl, vl);
  vuint16m4_t bsum16 = __riscv_vncvt_x(__riscv_vremu(b_sum, BASE, vl), vl);
  b_sum = __riscv_vadd(__riscv_vwmulu(a_sum, off, vl),
                       __riscv_vwmulu(bsum16, vl, vl), vl);
  bsum16 = __riscv_vncvt_x(__riscv_vremu(b_sum, BASE, vl), vl);

  /* And finally, do a horizontal sum across the registers for the final
   * result.
   */
  uint32_t a = adler & 0xffff;
  uint32_t b = ((adler >> 16) + a * (len % BASE)) % BASE;
  vuint32m1_t sca = __riscv_vmv_v_x_u32m1(a, 1);
  vuint32m1_t scb = __riscv_vmv_v_x_u32m1(b, 1);
  sca = __riscv_vwredsumu(a_sum, sca, vl);
  scb = __riscv_vwredsumu(bsum16, scb, vl);
  a = __riscv_vmv_x(sca);
  b = __riscv_vmv_x(scb);
  a %= BASE;
  b %= BASE;
  return (b << 16) | a;
}

#endif  /* ADLER32_SIMD_SSSE3 */
