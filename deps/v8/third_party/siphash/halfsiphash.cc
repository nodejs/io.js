/*
 * SipHash reference C implementation
 *
 * Copyright (c) 2016 Jean-Philippe Aumasson <jeanphilippe.aumasson@gmail.com>
 *
 * To the extent possible under law, the author(s) have dedicated all
 * copyright and related and neighboring rights to this software to the public
 * domain worldwide. This software is distributed without any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

/*
 * Originally taken from https://github.com/veorq/SipHash/
 * Altered to match V8's use case.
 */

#include "third_party/siphash/halfsiphash.h"

#define ROTL(x, b) (uint32_t)(((x) << (b)) | ((x) >> (32 - (b))))

#define SIPROUND       \
  do {                 \
    v0 += v1;          \
    v1 = ROTL(v1, 5);  \
    v1 ^= v0;          \
    v0 = ROTL(v0, 16); \
    v2 += v3;          \
    v3 = ROTL(v3, 8);  \
    v3 ^= v2;          \
    v0 += v3;          \
    v3 = ROTL(v3, 7);  \
    v3 ^= v0;          \
    v2 += v1;          \
    v1 = ROTL(v1, 13); \
    v1 ^= v2;          \
    v2 = ROTL(v2, 16); \
  } while (0)

// Simplified half-siphash-2-4 implementation for 4 byte input.
uint32_t halfsiphash(const uint32_t value, const uint64_t seed) {
  uint32_t v0 = 0;
  uint32_t v1 = 0;
  uint32_t v2 = 0x6c796765;
  uint32_t v3 = 0x74656462;
  uint32_t k[2];
  memcpy(k, &seed, sizeof(seed));
  uint32_t b = 4 << 24;
  v3 ^= k[1];
  v2 ^= k[0];
  v1 ^= k[1];
  v0 ^= k[0];

  v3 ^= value;

  // 2 c-rounds
  SIPROUND;
  SIPROUND;

  v0 ^= value;
  v3 ^= b;

  // 2 c-rounds
  SIPROUND;
  SIPROUND;

  v0 ^= b;
  v2 ^= 0xff;

  // 4 d-rounds
  SIPROUND;
  SIPROUND;
  SIPROUND;
  SIPROUND;

  b = v1 ^ v3;
  return b;
}
