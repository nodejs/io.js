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
#ifndef NGTCP2_BUF_H
#define NGTCP2_BUF_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

typedef struct ngtcp2_buf {
  /* begin points to the beginning of the buffer. */
  uint8_t *begin;
  /* end points to the one beyond of the last byte of the buffer */
  uint8_t *end;
  /* pos points to the start of data.  Typically, this points to the
     point that next data should be read.  Initially, it points to
     |begin|. */
  uint8_t *pos;
  /* last points to the one beyond of the last data of the buffer.
     Typically, new data is written at this point.  Initially, it
     points to |begin|. */
  uint8_t *last;
} ngtcp2_buf;

/*
 * ngtcp2_buf_init initializes |buf| with the given buffer.
 */
void ngtcp2_buf_init(ngtcp2_buf *buf, uint8_t *begin, size_t len);

/*
 * ngtcp2_buf_reset resets pos and last fields to match begin field to
 * make ngtcp2_buf_len(buf) return 0.
 */
void ngtcp2_buf_reset(ngtcp2_buf *buf);

/*
 * ngtcp2_buf_left returns the number of additional bytes which can be
 * written to the underlying buffer.  In other words, it returns
 * buf->end - buf->last.
 */
#define ngtcp2_buf_left(BUF) (size_t)((BUF)->end - (BUF)->last)

/*
 * ngtcp2_buf_len returns the number of bytes left to read.  In other
 * words, it returns buf->last - buf->pos.
 */
#define ngtcp2_buf_len(BUF) (size_t)((BUF)->last - (BUF)->pos)

/*
 * ngtcp2_buf_cap returns the capacity of the buffer.  In other words,
 * it returns buf->end - buf->begin.
 */
size_t ngtcp2_buf_cap(const ngtcp2_buf *buf);

/*
 * ngtcp2_buf_chain is a linked list of ngtcp2_buf.
 */
typedef struct ngtcp2_buf_chain ngtcp2_buf_chain;

struct ngtcp2_buf_chain {
  ngtcp2_buf_chain *next;
  ngtcp2_buf buf;
};

/*
 * ngtcp2_buf_chain_new creates new ngtcp2_buf_chain and initializes
 * the internal buffer with |len| bytes space.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
int ngtcp2_buf_chain_new(ngtcp2_buf_chain **pbufchain, size_t len,
                         const ngtcp2_mem *mem);

/*
 * ngtcp2_buf_chain_del deletes the resource allocated by |bufchain|.
 * It also deletes the memory pointed by |bufchain|.
 */
void ngtcp2_buf_chain_del(ngtcp2_buf_chain *bufchain, const ngtcp2_mem *mem);

#endif /* NGTCP2_BUF_H */
