#include "nbytes.h"
#include <cmath>
#include <cstddef>
#include <string.h>

namespace nbytes {

// ============================================================================
// Byte Swapping

namespace {
// These are defined by <sys/byteorder.h> or <netinet/in.h> on some systems.
// To avoid warnings, undefine them before redefining them.
#ifdef BSWAP_2
#undef BSWAP_2
#endif
#ifdef BSWAP_4
#undef BSWAP_4
#endif
#ifdef BSWAP_8
#undef BSWAP_8
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#define BSWAP_2(x) _byteswap_ushort(x)
#define BSWAP_4(x) _byteswap_ulong(x)
#define BSWAP_8(x) _byteswap_uint64(x)
#else
#define BSWAP_2(x) ((x) << 8) | ((x) >> 8)
#define BSWAP_4(x)                                                       \
  (((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | (((x) >> 8) & 0xFF00) | \
      (((x) >> 24) & 0xFF)
#define BSWAP_8(x)                            \
  (((x) & 0xFF00000000000000ull) >> 56) |     \
      (((x) & 0x00FF000000000000ull) >> 40) | \
      (((x) & 0x0000FF0000000000ull) >> 24) | \
      (((x) & 0x000000FF00000000ull) >> 8) |  \
      (((x) & 0x00000000FF000000ull) << 8) |  \
      (((x) & 0x0000000000FF0000ull) << 24) | \
      (((x) & 0x000000000000FF00ull) << 40) | \
      (((x) & 0x00000000000000FFull) << 56)
#endif
}  // namespace

bool SwapBytes16(char *data, size_t nbytes) {
  if (nbytes % sizeof(uint16_t) != 0) return false;

#if defined(_MSC_VER)
  if (AlignUp(data, sizeof(uint16_t)) == data) {
    // MSVC has no strict aliasing, and is able to highly optimize this case.
    uint16_t *data16 = reinterpret_cast<uint16_t *>(data);
    size_t len16 = nbytes / sizeof(uint16_t);
    for (size_t i = 0; i < len16; i++) {
      data16[i] = BSWAP_2(data16[i]);
    }
    return true;
  }
#endif

  uint16_t temp;
  for (size_t i = 0; i < nbytes; i += sizeof(temp)) {
    memcpy(&temp, &data[i], sizeof(temp));
    temp = BSWAP_2(temp);
    memcpy(&data[i], &temp, sizeof(temp));
  }

  return true;
}

bool SwapBytes32(char *data, size_t nbytes) {
  if (nbytes % sizeof(uint32_t) != 0) return false;

#if defined(_MSC_VER)
  // MSVC has no strict aliasing, and is able to highly optimize this case.
  if (AlignUp(data, sizeof(uint32_t)) == data) {
    uint32_t *data32 = reinterpret_cast<uint32_t *>(data);
    size_t len32 = nbytes / sizeof(uint32_t);
    for (size_t i = 0; i < len32; i++) {
      data32[i] = BSWAP_4(data32[i]);
    }
    return true;
  }
#endif

  uint32_t temp;
  for (size_t i = 0; i < nbytes; i += sizeof(temp)) {
    memcpy(&temp, &data[i], sizeof(temp));
    temp = BSWAP_4(temp);
    memcpy(&data[i], &temp, sizeof(temp));
  }

  return true;
}

bool SwapBytes64(char *data, size_t nbytes) {
  if (nbytes % sizeof(uint64_t) != 0) return false;

#if defined(_MSC_VER)
  if (AlignUp(data, sizeof(uint64_t)) == data) {
    // MSVC has no strict aliasing, and is able to highly optimize this case.
    uint64_t *data64 = reinterpret_cast<uint64_t *>(data);
    size_t len64 = nbytes / sizeof(uint64_t);
    for (size_t i = 0; i < len64; i++) {
      data64[i] = BSWAP_8(data64[i]);
    }
    return true;
  }
#endif

  uint64_t temp;
  for (size_t i = 0; i < nbytes; i += sizeof(temp)) {
    memcpy(&temp, &data[i], sizeof(temp));
    temp = BSWAP_8(temp);
    memcpy(&data[i], &temp, sizeof(temp));
  }

  return true;
}

// ============================================================================
// Base64 (legacy)

// supports regular and URL-safe base64
const int8_t unbase64_table[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -1, -1, -2, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, 62, -1, 62, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1,
    63, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1};

// ============================================================================
// Hex

const int8_t unhex_table[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,
    9,  -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1};

size_t HexEncode(const char *src, size_t slen, char *dst, size_t dlen) {
  // We know how much we'll write, just make sure that there's space.
  NBYTES_ASSERT_TRUE(dlen >= MultiplyWithOverflowCheck<size_t>(slen, 2u) &&
                     "not enough space provided for hex encode");

  dlen = slen * 2;
  for (size_t i = 0, k = 0; k < dlen; i += 1, k += 2) {
    static const char hex[] = "0123456789abcdef";
    uint8_t val = static_cast<uint8_t>(src[i]);
    dst[k + 0] = hex[val >> 4];
    dst[k + 1] = hex[val & 15];
  }

  return dlen;
}

std::string HexEncode(const char *src, size_t slen) {
  size_t dlen = slen * 2;
  std::string dst(dlen, '\0');
  HexEncode(src, slen, dst.data(), dlen);
  return dst;
}

// ============================================================================

void ForceAsciiSlow(const char *src, char *dst, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    dst[i] = src[i] & 0x7f;
  }
}

void ForceAscii(const char *src, char *dst, size_t len) {
  if (len < 16) {
    ForceAsciiSlow(src, dst, len);
    return;
  }

  const unsigned bytes_per_word = sizeof(uintptr_t);
  const unsigned align_mask = bytes_per_word - 1;
  const unsigned src_unalign = reinterpret_cast<uintptr_t>(src) & align_mask;
  const unsigned dst_unalign = reinterpret_cast<uintptr_t>(dst) & align_mask;

  if (src_unalign > 0) {
    if (src_unalign == dst_unalign) {
      const unsigned unalign = bytes_per_word - src_unalign;
      ForceAsciiSlow(src, dst, unalign);
      src += unalign;
      dst += unalign;
      len -= src_unalign;
    } else {
      ForceAsciiSlow(src, dst, len);
      return;
    }
  }

#if defined(_WIN64) || defined(_LP64)
  const uintptr_t mask = ~0x8080808080808080ll;
#else
  const uintptr_t mask = ~0x80808080l;
#endif

  const uintptr_t *srcw = reinterpret_cast<const uintptr_t *>(src);
  uintptr_t *dstw = reinterpret_cast<uintptr_t *>(dst);

  for (size_t i = 0, n = len / bytes_per_word; i < n; ++i) {
    dstw[i] = srcw[i] & mask;
  }

  const unsigned remainder = len & align_mask;
  if (remainder > 0) {
    const size_t offset = len - remainder;
    ForceAsciiSlow(src + offset, dst + offset, remainder);
  }
}

}  // namespace nbytes
