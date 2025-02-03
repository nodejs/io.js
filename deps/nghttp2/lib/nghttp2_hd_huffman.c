/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2013 Tatsuhiro Tsujikawa
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
#include "nghttp2_hd_huffman.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "nghttp2_hd.h"
#include "nghttp2_net.h"

size_t nghttp2_hd_huff_encode_count(const uint8_t *src, size_t len) {
  size_t i;
  size_t nbits = 0;

  for (i = 0; i < len; ++i) {
    nbits += huff_sym_table[src[i]].nbits;
  }
  /* pad the prefix of EOS (256) */
  return (nbits + 7) / 8;
}

int nghttp2_hd_huff_encode(nghttp2_bufs *bufs, const uint8_t *src,
                           size_t srclen) {
  const nghttp2_huff_sym *sym;
  const uint8_t *end = src + srclen;
  uint64_t code = 0;
  uint32_t x;
  size_t nbits = 0;
  size_t avail;
  int rv;

  avail = nghttp2_bufs_cur_avail(bufs);

  for (; src != end;) {
    sym = &huff_sym_table[*src++];
    code |= (uint64_t)sym->code << (32 - nbits);
    nbits += sym->nbits;
    if (nbits < 32) {
      continue;
    }
    if (avail >= 4) {
      x = htonl((uint32_t)(code >> 32));
      memcpy(bufs->cur->buf.last, &x, 4);
      bufs->cur->buf.last += 4;
      avail -= 4;
      code <<= 32;
      nbits -= 32;
      continue;
    }

    for (; nbits >= 8;) {
      rv = nghttp2_bufs_addb(bufs, (uint8_t)(code >> 56));
      if (rv != 0) {
        return rv;
      }
      code <<= 8;
      nbits -= 8;
    }

    avail = nghttp2_bufs_cur_avail(bufs);
  }

  for (; nbits >= 8;) {
    rv = nghttp2_bufs_addb(bufs, (uint8_t)(code >> 56));
    if (rv != 0) {
      return rv;
    }
    code <<= 8;
    nbits -= 8;
  }

  if (nbits) {
    rv = nghttp2_bufs_addb(
      bufs, (uint8_t)((uint8_t)(code >> 56) | ((1 << (8 - nbits)) - 1)));
    if (rv != 0) {
      return rv;
    }
  }

  return 0;
}

void nghttp2_hd_huff_decode_context_init(nghttp2_hd_huff_decode_context *ctx) {
  ctx->fstate = NGHTTP2_HUFF_ACCEPTED;
}

nghttp2_ssize nghttp2_hd_huff_decode(nghttp2_hd_huff_decode_context *ctx,
                                     nghttp2_buf *buf, const uint8_t *src,
                                     size_t srclen, int final) {
  const uint8_t *end = src + srclen;
  nghttp2_huff_decode node = {ctx->fstate, 0};
  const nghttp2_huff_decode *t = &node;
  uint8_t c;

  /* We use the decoding algorithm described in
      - http://graphics.ics.uci.edu/pub/Prefix.pdf [!!! NO LONGER VALID !!!]
      - https://ics.uci.edu/~dan/pubs/Prefix.pdf
      - https://github.com/nghttp2/nghttp2/files/15141264/Prefix.pdf */
  for (; src != end;) {
    c = *src++;
    t = &huff_decode_table[t->fstate & 0x1ff][c >> 4];
    if (t->fstate & NGHTTP2_HUFF_SYM) {
      *buf->last++ = t->sym;
    }

    t = &huff_decode_table[t->fstate & 0x1ff][c & 0xf];
    if (t->fstate & NGHTTP2_HUFF_SYM) {
      *buf->last++ = t->sym;
    }
  }

  ctx->fstate = t->fstate;

  if (final && !(ctx->fstate & NGHTTP2_HUFF_ACCEPTED)) {
    return NGHTTP2_ERR_HEADER_COMP;
  }

  return (nghttp2_ssize)srclen;
}

int nghttp2_hd_huff_decode_failure_state(nghttp2_hd_huff_decode_context *ctx) {
  return ctx->fstate == 0x100;
}
