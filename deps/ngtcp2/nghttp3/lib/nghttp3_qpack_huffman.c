/*
 * nghttp3
 *
 * Copyright (c) 2019 nghttp3 contributors
 * Copyright (c) 2013 nghttp2 contributors
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
#include "nghttp3_qpack_huffman.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "nghttp3_conv.h"

size_t nghttp3_qpack_huffman_encode_count(const uint8_t *src, size_t len) {
  size_t i;
  size_t nbits = 0;

  for (i = 0; i < len; ++i) {
    nbits += huffman_sym_table[src[i]].nbits;
  }
  /* pad the prefix of EOS (256) */
  return (nbits + 7) / 8;
}

uint8_t *nghttp3_qpack_huffman_encode(uint8_t *dest, const uint8_t *src,
                                      size_t srclen) {
  const nghttp3_qpack_huffman_sym *sym;
  const uint8_t *end = src + srclen;
  uint64_t code = 0;
  size_t nbits = 0;
  uint32_t x;

  for (; src != end;) {
    sym = &huffman_sym_table[*src++];
    code |= (uint64_t)sym->code << (32 - nbits);
    nbits += sym->nbits;
    if (nbits < 32) {
      continue;
    }
    x = htonl((uint32_t)(code >> 32));
    memcpy(dest, &x, 4);
    dest += 4;
    code <<= 32;
    nbits -= 32;
  }

  for (; nbits >= 8;) {
    *dest++ = (uint8_t)(code >> 56);
    code <<= 8;
    nbits -= 8;
  }

  if (nbits) {
    *dest++ = (uint8_t)((uint8_t)(code >> 56) | ((1 << (8 - nbits)) - 1));
  }

  return dest;
}

void nghttp3_qpack_huffman_decode_context_init(
  nghttp3_qpack_huffman_decode_context *ctx) {
  ctx->fstate = NGHTTP3_QPACK_HUFFMAN_ACCEPTED;
}

nghttp3_ssize
nghttp3_qpack_huffman_decode(nghttp3_qpack_huffman_decode_context *ctx,
                             uint8_t *dest, const uint8_t *src, size_t srclen,
                             int fin) {
  uint8_t *p = dest;
  const uint8_t *end = src + srclen;
  nghttp3_qpack_huffman_decode_node node = {
    .fstate = ctx->fstate,
  };
  const nghttp3_qpack_huffman_decode_node *t = &node;
  uint8_t c;

  /* We use the decoding algorithm described in
      - http://graphics.ics.uci.edu/pub/Prefix.pdf [!!! NO LONGER VALID !!!]
      - https://ics.uci.edu/~dan/pubs/Prefix.pdf
      - https://github.com/nghttp2/nghttp2/files/15141264/Prefix.pdf */
  for (; src != end;) {
    c = *src++;
    t = &qpack_huffman_decode_table[t->fstate & 0x1ff][c >> 4];
    if (t->fstate & NGHTTP3_QPACK_HUFFMAN_SYM) {
      *p++ = t->sym;
    }

    t = &qpack_huffman_decode_table[t->fstate & 0x1ff][c & 0xf];
    if (t->fstate & NGHTTP3_QPACK_HUFFMAN_SYM) {
      *p++ = t->sym;
    }
  }

  ctx->fstate = t->fstate;

  if (fin && !(ctx->fstate & NGHTTP3_QPACK_HUFFMAN_ACCEPTED)) {
    return NGHTTP3_ERR_QPACK_FATAL;
  }

  return p - dest;
}

int nghttp3_qpack_huffman_decode_failure_state(
  nghttp3_qpack_huffman_decode_context *ctx) {
  return ctx->fstate == 0x100;
}
