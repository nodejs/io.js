/*
 * rapidhash - Very fast, high quality, platform-independent hashing algorithm.
 * Copyright (C) 2024 Nicolas De Carli
 *
 * Based on 'wyhash', by Wang Yi <godspeed_china@yeah.net>
 *
 * BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at:
 *   - rapidhash source repository: https://github.com/Nicoshev/rapidhash
 */

#ifndef _THIRD_PARTY_RAPIDHASH_H
#define _THIRD_PARTY_RAPIDHASH_H 1

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <tuple>
#include <utility>

#include "include/v8config.h"
#include "src/base/logging.h"

// Chromium has some local modifications to upstream rapidhash,
// mostly around the concept of HashReaders (including slightly
// more comments for ease of understanding). Generally, rapidhash
// hashes bytes without really caring what these bytes are,
// but often in Chromium, we want to hash _strings_, and strings
// can have multiple representations. In particular, the WTF
// StringImpl class (and by extension, String and AtomicString)
// can have three different states:
//
//   1. Latin1 (or ASCII) code points, in 8 bits per character (LChar).
//   2. The same code points, in 16 bits per character (UChar).
//   3. Strings actually including non-Latin1 code points, in 16 bits
//      per character (UChar, UTF-16 encoding).
//
// The problem is that we'd like to hash case #1 and #2 to actually
// return the same hash. There are several ways we could deal with this:
//
//   a) Just ignore the problem and hash everything as raw bytes;
//      case #2 is fairly rare, and some algorithms (e.g. opportunistic
//      deduplication) could live with some false negatives.
//   b) Expand all strings to UTF-16 before hashing. This was the
//      strategy for the old StringHasher (using SuperFastHash),
//      as it naturally worked in 16-bit increments and it is probably
//      the simplest. However, this both halves the throughput of the
//      hasher and incurs conversion costs.
//   c) Detect #2 and convert those cases to #1 (UTF-16 to Latin1
//      conversion), and apart from that, juts hash bytes.
//
// b) is used in e.g. CaseFoldingHash, but c) is the one we use the most
// often. Most strings that we want to hash are 8-bit (e.g. HTML tags), so
// that makes the common case fast. And for UChar strings, it is fairly fast
// to make a pre-pass over the string to check for the existence of any
// non-Latin1 characters. Of course, #1 and #3 can just be hashed as raw
// bytes, as strings from those groups can never be equal anyway.
//
// To support these 8-to-16 and 16-to-8 conversions, and also things like
// lowercasing on the fly, we have modified rapidhash to be templatized
// on a HashReader, supplying bytes to the hash function. For the default
// case of just hashing raw bytes, this is simply reading. But for other
// conversions, the reader is doing slightly more work, such as packing
// or unpacking. See e.g. ConvertTo8BitHashReader in string_hasher.h.
//
// Note that this reader could be made constexpr if we needed to evaluate
// hashes at compile-time.
struct PlainHashReader {
  // If this is different from 1 (only 1, 2 and 4 are really reasonable
  // options), it means the reader consumes its input at a faster rate than
  // would be normally expected. For instance, if it is 2, it means that when
  // the reader produces 64 bits, it needs to move its input 128 bits
  // ahead. This is used when e.g. a reader converts from UTF-16 to ASCII,
  // by removing zeros. The input length must divide the compression factor.
  static constexpr unsigned kCompressionFactor = 1;

  // This is the opposite of kExpansionFactor. It does not make sense to have
  // both kCompressionFactor and kExpansionFactor different from 1.
  // The output length must divide the expansion factor.
  static constexpr unsigned kExpansionFactor = 1;

  // Read 64 little-endian bits from the input, taking into account
  // the expansion/compression if relevant. Unaligned reads must be
  // supported.
  static inline uint64_t Read64(const uint8_t* p) {
    uint64_t v;
#ifdef V8_TARGET_BIG_ENDIAN
    uint8_t* dst = reinterpret_cast<uint8_t*>(&v);
    for (size_t i = 0; i < sizeof(v); i++) {
      dst[i] = p[sizeof(v) - i - 1];
    }
#else
    memcpy(&v, p, 8);
#endif
    return v;
  }

  // Similarly, read 32 little-endian (unaligned) bits. Note that
  // this must return uint64_t, _not_ uint32_t, as the hasher assumes
  // it can freely shift such numbers up and down.
  static inline uint64_t Read32(const uint8_t* p) {
    uint32_t v;
#ifdef V8_TARGET_BIG_ENDIAN
    uint8_t* dst = reinterpret_cast<uint8_t*>(&v);
    for (size_t i = 0; i < sizeof(v); i++) {
      dst[i] = p[sizeof(v) - i - 1];
    }
#else
    memcpy(&v, p, 4);
#endif
    return v;
  }

  // Read 1, 2 or 3 bytes from the input, and distribute them somewhat
  // reasonably across the resulting 64-bit number. Note that this is
  // done in a branch-free fashion, so that it always reads three bytes
  // but never reads past the end.
  //
  // This is only used in the case where we hash a string of exactly
  // 1, 2 or 3 bytes; if the hash is e.g. 7 bytes, two overlapping 32-bit
  // reads will be done instead. Note that if you have kCompressionFactor == 2,
  // this means that this will only ever be called with k = 2.
  static inline uint64_t ReadSmall(const uint8_t* p, size_t k) {
    return (uint64_t{p[0]} << 56) | (uint64_t{p[k >> 1]} << 32) | p[k - 1];
  }
};

/*
 *  Likely and unlikely macros.
 */
#define _likely_(x) __builtin_expect(x, 1)
#define _unlikely_(x) __builtin_expect(x, 0)

/*
 *  Default seed.
 */
static constexpr uint64_t RAPID_SEED = 0xbdd89aa982704029ull;

// Default secret parameters. If we wanted to, we could generate our own
// versions of these at renderer startup in order to perturb the hash
// and make it more DoS-resistant (similar to what base/hash.h does),
// but generating new ones takes a little bit of time (~200 µs on a desktop
// machine as of 2024), and good-quality random numbers may not be copious
// from within the sandbox. The secret concept is inherited from wyhash,
// described by the wyhash author here:
//
//   https://github.com/wangyi-fudan/wyhash/issues/139
//
// The rules are:
//
//   1. Each byte must be “balanced”, i.e., have exactly 4 bits set.
//      (This is trivially done by just precompting a LUT with the
//      possible bytes and picking from those.)
//
//   2. Each 64-bit group should have a Hamming distance of 32 to
//      all the others (i.e., popcount(secret[i] ^ secret[j]) == 32).
//      This is just done by rejection sampling.
//
//   3. Each 64-bit group should be prime. It's not obvious that this
//      is really needed for the hash, as opposed to wyrand which also
//      uses the same secret, but according to the author, it is
//      “a feeling to be perfect”. This naturally means the last byte
//      cannot be divisible by 2, but apart from that, is easiest
//      checked by testing a few small factors and then the Miller-Rabin
//      test, which rejects nearly all bad candidates in the first iteration
//      and is fast as long as we have 64x64 -> 128 bit muls and modulos.
//
// For now, we just use the rapidhash-supplied standard.
static constexpr uint64_t rapid_secret[3] = {
    0x2d358dccaa6c78a5ull, 0x8bb84b93962eacc9ull, 0x4b33a62ed433d4a3ull};

/*
 *  64*64 -> 128bit multiply function.
 *
 *  @param A  Address of 64-bit number.
 *  @param B  Address of 64-bit number.
 *
 *  Calculates 128-bit C = *A * *B.
 */
inline std::pair<uint64_t, uint64_t> rapid_mul128(uint64_t A, uint64_t B) {
#if defined(__SIZEOF_INT128__)
  __uint128_t r = A;
  r *= B;
  return {static_cast<uint64_t>(r), static_cast<uint64_t>(r >> 64)};
#else
  // High and low 32 bits of A and B.
  uint64_t a_high = A >> 32, b_high = B >> 32, a_low = (uint32_t)A,
           b_low = (uint32_t)B;

  // Intermediate products.
  uint64_t result_high = a_high * b_high;
  uint64_t result_m0 = a_high * b_low;
  uint64_t result_m1 = b_high * a_low;
  uint64_t result_low = a_low * b_low;

  // Final sum. The lower 64-bit addition can overflow twice,
  // so accumulate carry as we go.
  uint64_t high = result_high + (result_m0 >> 32) + (result_m1 >> 32);
  uint64_t t = result_low + (result_m0 << 32);
  high += (t < result_low);  // Carry.
  uint64_t low = t + (result_m1 << 32);
  high += (low < t);  // Carry.

  return {low, high};
#endif
}

/*
 *  Multiply and xor mix function.
 *
 *  @param A  64-bit number.
 *  @param B  64-bit number.
 *
 *  Calculates 128-bit C = A * B.
 *  Returns 64-bit xor between high and low 64 bits of C.
 */
inline uint64_t rapid_mix(uint64_t A, uint64_t B) {
  std::tie(A, B) = rapid_mul128(A, B);
  return A ^ B;
}

/*
 *  rapidhash main function.
 *
 *  @param key     Buffer to be hashed.
 *  @param len     Number of input bytes coming from the reader.
 *  @param seed    64-bit seed used to alter the hash result predictably.
 *  @param secret  Triplet of 64-bit secrets used to alter hash result
 *                 predictably.
 *
 *  Returns a 64-bit hash.
 *
 *  The data flow is separated so that we never mix input data with pointers;
 *
 *  a, b, seed, secret[0], secret[1], secret[2], see1 and see2 are affected
 *  by the input data.
 *
 *  p is only ever indexed by len, delta (comes from len only), i (comes from
 *  len only) or integral constants. len is const, so no data can flow into it.
 *
 *  No other reads from memory take place. No writes to memory take place.
 */
template <class Reader>
V8_INLINE uint64_t rapidhash_internal(const uint8_t* p, const size_t len,
                                      uint64_t seed, const uint64_t secret[3]) {
  // For brevity.
  constexpr unsigned x = Reader::kCompressionFactor;
  constexpr unsigned y = Reader::kExpansionFactor;
  DCHECK_EQ(len % y, 0u);

  seed ^= rapid_mix(seed ^ secret[0], secret[1]) ^ len;
  uint64_t a, b;
  if (_likely_(len <= 16)) {
    if (_likely_(len >= 4)) {
      // Read the first and last 32 bits (they may overlap).
      const uint8_t* plast = p + (len - 4) * x / y;
      a = (Reader::Read32(p) << 32) | Reader::Read32(plast);

      // This is equivalent to: delta = (len >= 8) ? 4 : 0;
      const uint64_t delta = ((len & 24) >> (len >> 3)) * x / y;
      b = ((Reader::Read32(p + delta) << 32) | Reader::Read32(plast - delta));
    } else if (_likely_(len > 0)) {
      // 1, 2 or 3 bytes.
      a = Reader::ReadSmall(p, len);
      b = 0;
    } else {
      a = b = 0;
    }
  } else {
    size_t i = len;
    if (_unlikely_(i > 48)) {
      uint64_t see1 = seed, see2 = seed;
      do {
        seed = rapid_mix(Reader::Read64(p) ^ secret[0],
                         Reader::Read64(p + 8 * x / y) ^ seed);
        see1 = rapid_mix(Reader::Read64(p + 16 * x / y) ^ secret[1],
                         Reader::Read64(p + 24 * x / y) ^ see1);
        see2 = rapid_mix(Reader::Read64(p + 32 * x / y) ^ secret[2],
                         Reader::Read64(p + 40 * x / y) ^ see2);
        p += 48 * x / y;
        i -= 48;
      } while (_likely_(i >= 48));
      seed ^= see1 ^ see2;
    }
    if (i > 16) {
      seed = rapid_mix(Reader::Read64(p) ^ secret[2],
                       Reader::Read64(p + 8 * x / y) ^ seed ^ secret[1]);
      if (i > 32) {
        seed = rapid_mix(Reader::Read64(p + 16 * x / y) ^ secret[2],
                         Reader::Read64(p + 24 * x / y) ^ seed);
      }
    }

    // Read the last 2x64 bits. Note that due to the division by y,
    // this must be a signed quantity (or the division would become
    // unsigned, possibly moving the sign bit down into the address).
    // Similarly for x.
    a = Reader::Read64(p + (static_cast<ptrdiff_t>(i) - 16) *
                               static_cast<int>(x) / static_cast<int>(y));
    b = Reader::Read64(p + (static_cast<ptrdiff_t>(i) - 8) *
                               static_cast<int>(x) / static_cast<int>(y));
  }
  a ^= secret[1];
  b ^= seed;
  std::tie(a, b) = rapid_mul128(a, b);
  return rapid_mix(a ^ secret[0] ^ len, b ^ secret[1]);
}

/*
 *  rapidhash default seeded hash function.
 *
 *  @param key     Buffer to be hashed.
 *  @param len     Number of input bytes coming from the reader.
 *  @param seed    64-bit seed used to alter the hash result predictably.
 *
 *  Calls rapidhash_internal using provided parameters and default secrets.
 *
 *  Returns a 64-bit hash.
 */
template <class Reader = PlainHashReader>
V8_INLINE static uint64_t rapidhash(const uint8_t* key, size_t len,
                                    uint64_t seed = RAPID_SEED) {
  return rapidhash_internal<Reader>(key, len, seed, rapid_secret);
}

#undef _likely_
#undef _unlikely_

#endif  // _THIRD_PARTY_RAPIDHASH_H
