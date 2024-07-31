/*
 * ngtcp2
 *
 * Copyright (c) 2017 ngtcp2 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "ngtcp2_str.h"

#include <string.h>

#include "ngtcp2_macro.h"

void *ngtcp2_cpymem(void *dest, const void *src, size_t n) {
  memcpy(dest, src, n);
  return (uint8_t *)dest + n;
}

uint8_t *ngtcp2_setmem(uint8_t *dest, uint8_t b, size_t n) {
  memset(dest, b, n);
  return dest + n;
}

const void *ngtcp2_get_bytes(void *dest, const void *src, size_t n) {
  memcpy(dest, src, n);
  return (uint8_t *)src + n;
}

#define LOWER_XDIGITS "0123456789abcdef"

uint8_t *ngtcp2_encode_hex(uint8_t *dest, const uint8_t *data, size_t len) {
  size_t i;
  uint8_t *p = dest;

  for (i = 0; i < len; ++i) {
    *p++ = (uint8_t)LOWER_XDIGITS[data[i] >> 4];
    *p++ = (uint8_t)LOWER_XDIGITS[data[i] & 0xf];
  }

  *p = '\0';

  return dest;
}

char *ngtcp2_encode_printable_ascii(char *dest, const uint8_t *data,
                                    size_t len) {
  size_t i;
  char *p = dest;
  uint8_t c;

  for (i = 0; i < len; ++i) {
    c = data[i];
    if (0x20 <= c && c <= 0x7e) {
      *p++ = (char)c;
    } else {
      *p++ = '.';
    }
  }

  *p = '\0';

  return dest;
}

/*
 * write_uint writes |n| to the buffer pointed by |p| in decimal
 * representation.  It returns |p| plus the number of bytes written.
 * The function assumes that the buffer has enough capacity to contain
 * a string.
 */
static uint8_t *write_uint(uint8_t *p, uint64_t n) {
  size_t nlen = 0;
  uint64_t t;
  uint8_t *res;

  if (n == 0) {
    *p++ = '0';
    return p;
  }
  for (t = n; t; t /= 10, ++nlen)
    ;
  p += nlen;
  res = p;
  for (; n; n /= 10) {
    *--p = (uint8_t)((n % 10) + '0');
  }
  return res;
}

uint8_t *ngtcp2_encode_ipv4(uint8_t *dest, const uint8_t *addr) {
  size_t i;
  uint8_t *p = dest;

  p = write_uint(p, addr[0]);

  for (i = 1; i < 4; ++i) {
    *p++ = '.';
    p = write_uint(p, addr[i]);
  }

  *p = '\0';

  return dest;
}

/*
 * write_hex_zsup writes the content of buffer pointed by |data| of
 * length |len| to |dest| in hex string.  Any leading zeros are
 * suppressed.  It returns |dest| plus the number of bytes written.
 */
static uint8_t *write_hex_zsup(uint8_t *dest, const uint8_t *data, size_t len) {
  size_t i;
  uint8_t *p = dest;
  uint8_t d;

  for (i = 0; i < len; ++i) {
    d = data[i];
    if (d >> 4) {
      break;
    }

    d &= 0xf;

    if (d) {
      *p++ = (uint8_t)LOWER_XDIGITS[d];
      ++i;
      break;
    }
  }

  if (p == dest && i == len) {
    *p++ = '0';
    return p;
  }

  for (; i < len; ++i) {
    d = data[i];
    *p++ = (uint8_t)LOWER_XDIGITS[d >> 4];
    *p++ = (uint8_t)LOWER_XDIGITS[d & 0xf];
  }

  return p;
}

uint8_t *ngtcp2_encode_ipv6(uint8_t *dest, const uint8_t *addr) {
  uint16_t blks[8];
  size_t i;
  size_t zlen, zoff;
  size_t max_zlen = 0, max_zoff = 8;
  uint8_t *p = dest;

  for (i = 0; i < 16; i += sizeof(uint16_t)) {
    /* Copy in network byte order. */
    memcpy(&blks[i / sizeof(uint16_t)], addr + i, sizeof(uint16_t));
  }

  for (i = 0; i < 8;) {
    if (blks[i]) {
      ++i;
      continue;
    }

    zlen = 1;
    zoff = i;

    ++i;
    for (; i < 8 && blks[i] == 0; ++i, ++zlen)
      ;
    if (zlen > max_zlen) {
      max_zlen = zlen;
      max_zoff = zoff;
    }
  }

  /* Do not suppress a single '0' block */
  if (max_zlen == 1) {
    max_zoff = 8;
  }

  if (max_zoff != 0) {
    p = write_hex_zsup(p, (const uint8_t *)blks, sizeof(uint16_t));

    for (i = 1; i < max_zoff; ++i) {
      *p++ = ':';
      p = write_hex_zsup(p, (const uint8_t *)(blks + i), sizeof(uint16_t));
    }
  }

  if (max_zoff != 8) {
    *p++ = ':';

    if (max_zoff + max_zlen == 8) {
      *p++ = ':';
    } else {
      for (i = max_zoff + max_zlen; i < 8; ++i) {
        *p++ = ':';
        p = write_hex_zsup(p, (const uint8_t *)(blks + i), sizeof(uint16_t));
      }
    }
  }

  *p = '\0';

  return dest;
}

int ngtcp2_cmemeq(const uint8_t *a, const uint8_t *b, size_t n) {
  size_t i;
  int rv = 0;

  for (i = 0; i < n; ++i) {
    rv |= a[i] ^ b[i];
  }

  return rv == 0;
}
