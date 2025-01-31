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
#ifndef NGHTTP3_QPACK_HUFFMAN_H
#define NGHTTP3_QPACK_HUFFMAN_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

typedef struct nghttp3_qpack_huffman_sym {
  /* The number of bits in this code */
  uint32_t nbits;
  /* Huffman code aligned to LSB */
  uint32_t code;
} nghttp3_qpack_huffman_sym;

extern const nghttp3_qpack_huffman_sym huffman_sym_table[];

size_t nghttp3_qpack_huffman_encode_count(const uint8_t *src, size_t len);

uint8_t *nghttp3_qpack_huffman_encode(uint8_t *dest, const uint8_t *src,
                                      size_t srclen);

typedef enum nghttp3_qpack_huffman_decode_flag {
  /* FSA accepts this state as the end of huffman encoding
     sequence. */
  NGHTTP3_QPACK_HUFFMAN_ACCEPTED = 1 << 14,
  /* This state emits symbol */
  NGHTTP3_QPACK_HUFFMAN_SYM = 1 << 15,
} nghttp3_qpack_huffman_decode_flag;

typedef struct nghttp3_qpack_huffman_decode_node {
  /* fstate is the current huffman decoding state, which is actually
     the node ID of internal huffman tree with
     nghttp3_qpack_huffman_decode_flag OR-ed.  We have 257 leaf nodes,
     but they are identical to root node other than emitting a symbol,
     so we have 256 internal nodes [1..256], inclusive.  The node ID
     256 is a special node and it is a terminal state that means
     decoding failed. */
  uint16_t fstate;
  /* symbol if NGHTTP3_QPACK_HUFFMAN_SYM flag set */
  uint8_t sym;
} nghttp3_qpack_huffman_decode_node;

typedef struct nghttp3_qpack_huffman_decode_context {
  /* fstate is the current huffman decoding state. */
  uint16_t fstate;
} nghttp3_qpack_huffman_decode_context;

extern const nghttp3_qpack_huffman_decode_node qpack_huffman_decode_table[][16];

void nghttp3_qpack_huffman_decode_context_init(
  nghttp3_qpack_huffman_decode_context *ctx);

/*
 * nghttp3_qpack_huffman_decode decodes huffman encoded byte string
 * stored in |src| of length |srclen|.  |ctx| is a decoding context.
 * |ctx| remembers the decoding state, and caller can call this
 * function multiple times to feed each chunk of huffman encoded
 * substring.  |fin| must be nonzero if |src| contains the last chunk
 * of huffman string.  The decoded string is written to the buffer
 * pointed by |dest|.  This function assumes that the buffer pointed
 * by |dest| contains enough memory to store decoded byte string.
 *
 * This function returns the number of bytes written to |dest|, or one
 * of the following negative error codes:
 *
 * NGHTTP3_ERR_QPACK_FATAL
 *     Could not decode huffman string.
 */
nghttp3_ssize
nghttp3_qpack_huffman_decode(nghttp3_qpack_huffman_decode_context *ctx,
                             uint8_t *dest, const uint8_t *src, size_t srclen,
                             int fin);

/*
 * nghttp3_qpack_huffman_decode_failure_state returns nonzero if |ctx|
 * indicates that huffman decoding context is in failure state.
 */
int nghttp3_qpack_huffman_decode_failure_state(
  nghttp3_qpack_huffman_decode_context *ctx);

#endif /* !defined(NGHTTP3_QPACK_HUFFMAN_H) */
