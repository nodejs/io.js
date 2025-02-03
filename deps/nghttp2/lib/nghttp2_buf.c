/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2014 Tatsuhiro Tsujikawa
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
#include "nghttp2_buf.h"

#include <stdio.h>

#include "nghttp2_helper.h"
#include "nghttp2_debug.h"

void nghttp2_buf_init(nghttp2_buf *buf) {
  buf->begin = NULL;
  buf->end = NULL;
  buf->pos = NULL;
  buf->last = NULL;
  buf->mark = NULL;
}

int nghttp2_buf_init2(nghttp2_buf *buf, size_t initial, nghttp2_mem *mem) {
  nghttp2_buf_init(buf);
  return nghttp2_buf_reserve(buf, initial, mem);
}

void nghttp2_buf_free(nghttp2_buf *buf, nghttp2_mem *mem) {
  if (buf == NULL) {
    return;
  }

  nghttp2_mem_free(mem, buf->begin);
  buf->begin = NULL;
}

int nghttp2_buf_reserve(nghttp2_buf *buf, size_t new_cap, nghttp2_mem *mem) {
  uint8_t *ptr;
  size_t cap;

  cap = nghttp2_buf_cap(buf);

  if (cap >= new_cap) {
    return 0;
  }

  new_cap = nghttp2_max_size(new_cap, cap * 2);

  ptr = nghttp2_mem_realloc(mem, buf->begin, new_cap);
  if (ptr == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  buf->pos = ptr + (buf->pos - buf->begin);
  buf->last = ptr + (buf->last - buf->begin);
  buf->mark = ptr + (buf->mark - buf->begin);
  buf->begin = ptr;
  buf->end = ptr + new_cap;

  return 0;
}

void nghttp2_buf_reset(nghttp2_buf *buf) {
  buf->pos = buf->last = buf->mark = buf->begin;
}

void nghttp2_buf_wrap_init(nghttp2_buf *buf, uint8_t *begin, size_t len) {
  buf->begin = buf->pos = buf->last = buf->mark = buf->end = begin;
  if (len) {
    buf->end += len;
  }
}

static int buf_chain_new(nghttp2_buf_chain **chain, size_t chunk_length,
                         nghttp2_mem *mem) {
  int rv;

  *chain = nghttp2_mem_malloc(mem, sizeof(nghttp2_buf_chain));
  if (*chain == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  (*chain)->next = NULL;

  rv = nghttp2_buf_init2(&(*chain)->buf, chunk_length, mem);
  if (rv != 0) {
    nghttp2_mem_free(mem, *chain);
    return NGHTTP2_ERR_NOMEM;
  }

  return 0;
}

static void buf_chain_del(nghttp2_buf_chain *chain, nghttp2_mem *mem) {
  nghttp2_buf_free(&chain->buf, mem);
  nghttp2_mem_free(mem, chain);
}

int nghttp2_bufs_init(nghttp2_bufs *bufs, size_t chunk_length, size_t max_chunk,
                      nghttp2_mem *mem) {
  return nghttp2_bufs_init2(bufs, chunk_length, max_chunk, 0, mem);
}

int nghttp2_bufs_init2(nghttp2_bufs *bufs, size_t chunk_length,
                       size_t max_chunk, size_t offset, nghttp2_mem *mem) {
  return nghttp2_bufs_init3(bufs, chunk_length, max_chunk, max_chunk, offset,
                            mem);
}

int nghttp2_bufs_init3(nghttp2_bufs *bufs, size_t chunk_length,
                       size_t max_chunk, size_t chunk_keep, size_t offset,
                       nghttp2_mem *mem) {
  int rv;
  nghttp2_buf_chain *chain;

  if (chunk_keep == 0 || max_chunk < chunk_keep || chunk_length < offset) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  rv = buf_chain_new(&chain, chunk_length, mem);
  if (rv != 0) {
    return rv;
  }

  bufs->mem = mem;
  bufs->offset = offset;

  bufs->head = chain;
  bufs->cur = bufs->head;

  nghttp2_buf_shift_right(&bufs->cur->buf, offset);

  bufs->chunk_length = chunk_length;
  bufs->chunk_used = 1;
  bufs->max_chunk = max_chunk;
  bufs->chunk_keep = chunk_keep;

  return 0;
}

int nghttp2_bufs_realloc(nghttp2_bufs *bufs, size_t chunk_length) {
  int rv;
  nghttp2_buf_chain *chain;

  if (chunk_length < bufs->offset) {
    return NGHTTP2_ERR_INVALID_ARGUMENT;
  }

  rv = buf_chain_new(&chain, chunk_length, bufs->mem);
  if (rv != 0) {
    return rv;
  }

  nghttp2_bufs_free(bufs);

  bufs->head = chain;
  bufs->cur = bufs->head;

  nghttp2_buf_shift_right(&bufs->cur->buf, bufs->offset);

  bufs->chunk_length = chunk_length;
  bufs->chunk_used = 1;

  return 0;
}

void nghttp2_bufs_free(nghttp2_bufs *bufs) {
  nghttp2_buf_chain *chain, *next_chain;

  if (bufs == NULL) {
    return;
  }

  for (chain = bufs->head; chain;) {
    next_chain = chain->next;

    buf_chain_del(chain, bufs->mem);

    chain = next_chain;
  }

  bufs->head = NULL;
}

int nghttp2_bufs_wrap_init(nghttp2_bufs *bufs, uint8_t *begin, size_t len,
                           nghttp2_mem *mem) {
  nghttp2_buf_chain *chain;

  chain = nghttp2_mem_malloc(mem, sizeof(nghttp2_buf_chain));
  if (chain == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  chain->next = NULL;

  nghttp2_buf_wrap_init(&chain->buf, begin, len);

  bufs->mem = mem;
  bufs->offset = 0;

  bufs->head = chain;
  bufs->cur = bufs->head;

  bufs->chunk_length = len;
  bufs->chunk_used = 1;
  bufs->max_chunk = 1;
  bufs->chunk_keep = 1;

  return 0;
}

int nghttp2_bufs_wrap_init2(nghttp2_bufs *bufs, const nghttp2_vec *vec,
                            size_t veclen, nghttp2_mem *mem) {
  size_t i = 0;
  nghttp2_buf_chain *cur_chain;
  nghttp2_buf_chain *head_chain;
  nghttp2_buf_chain **dst_chain = &head_chain;

  if (veclen == 0) {
    return nghttp2_bufs_wrap_init(bufs, NULL, 0, mem);
  }

  head_chain = nghttp2_mem_malloc(mem, sizeof(nghttp2_buf_chain) * veclen);
  if (head_chain == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  for (i = 0; i < veclen; ++i) {
    cur_chain = &head_chain[i];
    cur_chain->next = NULL;
    nghttp2_buf_wrap_init(&cur_chain->buf, vec[i].base, vec[i].len);

    *dst_chain = cur_chain;
    dst_chain = &cur_chain->next;
  }

  bufs->mem = mem;
  bufs->offset = 0;

  bufs->head = head_chain;
  bufs->cur = bufs->head;

  /* We don't use chunk_length since no allocation is expected. */
  bufs->chunk_length = 0;
  bufs->chunk_used = veclen;
  bufs->max_chunk = veclen;
  bufs->chunk_keep = veclen;

  return 0;
}

void nghttp2_bufs_wrap_free(nghttp2_bufs *bufs) {
  if (bufs == NULL) {
    return;
  }

  if (bufs->head) {
    nghttp2_mem_free(bufs->mem, bufs->head);
  }
}

void nghttp2_bufs_seek_last_present(nghttp2_bufs *bufs) {
  nghttp2_buf_chain *ci;

  for (ci = bufs->cur; ci; ci = ci->next) {
    if (nghttp2_buf_len(&ci->buf) == 0) {
      return;
    } else {
      bufs->cur = ci;
    }
  }
}

size_t nghttp2_bufs_len(nghttp2_bufs *bufs) {
  nghttp2_buf_chain *ci;
  size_t len;

  len = 0;
  for (ci = bufs->head; ci; ci = ci->next) {
    len += nghttp2_buf_len(&ci->buf);
  }

  return len;
}

static int bufs_alloc_chain(nghttp2_bufs *bufs) {
  int rv;
  nghttp2_buf_chain *chain;

  if (bufs->cur->next) {
    bufs->cur = bufs->cur->next;

    return 0;
  }

  if (bufs->max_chunk == bufs->chunk_used) {
    return NGHTTP2_ERR_BUFFER_ERROR;
  }

  rv = buf_chain_new(&chain, bufs->chunk_length, bufs->mem);
  if (rv != 0) {
    return rv;
  }

  DEBUGF("new buffer %zu bytes allocated for bufs %p, used %zu\n",
         bufs->chunk_length, bufs, bufs->chunk_used);

  ++bufs->chunk_used;

  bufs->cur->next = chain;
  bufs->cur = chain;

  nghttp2_buf_shift_right(&bufs->cur->buf, bufs->offset);

  return 0;
}

int nghttp2_bufs_add(nghttp2_bufs *bufs, const void *data, size_t len) {
  int rv;
  size_t nwrite;
  nghttp2_buf *buf;
  const uint8_t *p;

  p = data;

  while (len) {
    buf = &bufs->cur->buf;

    nwrite = nghttp2_min_size(nghttp2_buf_avail(buf), len);
    if (nwrite == 0) {
      rv = bufs_alloc_chain(bufs);
      if (rv != 0) {
        return rv;
      }
      continue;
    }

    buf->last = nghttp2_cpymem(buf->last, p, nwrite);
    p += nwrite;
    len -= nwrite;
  }

  return 0;
}

static int bufs_ensure_addb(nghttp2_bufs *bufs) {
  int rv;
  nghttp2_buf *buf;

  buf = &bufs->cur->buf;

  if (nghttp2_buf_avail(buf) > 0) {
    return 0;
  }

  rv = bufs_alloc_chain(bufs);
  if (rv != 0) {
    return rv;
  }

  return 0;
}

int nghttp2_bufs_addb(nghttp2_bufs *bufs, uint8_t b) {
  int rv;

  rv = bufs_ensure_addb(bufs);
  if (rv != 0) {
    return rv;
  }

  *bufs->cur->buf.last++ = b;

  return 0;
}

int nghttp2_bufs_addb_hold(nghttp2_bufs *bufs, uint8_t b) {
  int rv;

  rv = bufs_ensure_addb(bufs);
  if (rv != 0) {
    return rv;
  }

  *bufs->cur->buf.last = b;

  return 0;
}

int nghttp2_bufs_orb(nghttp2_bufs *bufs, uint8_t b) {
  int rv;

  rv = bufs_ensure_addb(bufs);
  if (rv != 0) {
    return rv;
  }

  *bufs->cur->buf.last++ |= b;

  return 0;
}

int nghttp2_bufs_orb_hold(nghttp2_bufs *bufs, uint8_t b) {
  int rv;

  rv = bufs_ensure_addb(bufs);
  if (rv != 0) {
    return rv;
  }

  *bufs->cur->buf.last |= b;

  return 0;
}

nghttp2_ssize nghttp2_bufs_remove(nghttp2_bufs *bufs, uint8_t **out) {
  size_t len;
  nghttp2_buf_chain *chain;
  nghttp2_buf *buf;
  uint8_t *res;
  nghttp2_buf resbuf;

  len = 0;

  for (chain = bufs->head; chain; chain = chain->next) {
    len += nghttp2_buf_len(&chain->buf);
  }

  if (len == 0) {
    res = NULL;
    return 0;
  }

  res = nghttp2_mem_malloc(bufs->mem, len);
  if (res == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  nghttp2_buf_wrap_init(&resbuf, res, len);

  for (chain = bufs->head; chain; chain = chain->next) {
    buf = &chain->buf;
    resbuf.last = nghttp2_cpymem(resbuf.last, buf->pos, nghttp2_buf_len(buf));
  }

  *out = res;

  return (nghttp2_ssize)len;
}

size_t nghttp2_bufs_remove_copy(nghttp2_bufs *bufs, uint8_t *out) {
  size_t len;
  nghttp2_buf_chain *chain;
  nghttp2_buf *buf;
  nghttp2_buf resbuf;

  len = nghttp2_bufs_len(bufs);

  nghttp2_buf_wrap_init(&resbuf, out, len);

  for (chain = bufs->head; chain; chain = chain->next) {
    buf = &chain->buf;
    resbuf.last = nghttp2_cpymem(resbuf.last, buf->pos, nghttp2_buf_len(buf));
  }

  return len;
}

void nghttp2_bufs_reset(nghttp2_bufs *bufs) {
  nghttp2_buf_chain *chain, *ci;
  size_t k;

  k = bufs->chunk_keep;

  for (ci = bufs->head; ci; ci = ci->next) {
    nghttp2_buf_reset(&ci->buf);
    nghttp2_buf_shift_right(&ci->buf, bufs->offset);

    if (--k == 0) {
      break;
    }
  }

  if (ci) {
    chain = ci->next;
    ci->next = NULL;

    for (ci = chain; ci;) {
      chain = ci->next;

      buf_chain_del(ci, bufs->mem);

      ci = chain;
    }

    bufs->chunk_used = bufs->chunk_keep;
  }

  bufs->cur = bufs->head;
}

int nghttp2_bufs_advance(nghttp2_bufs *bufs) { return bufs_alloc_chain(bufs); }

int nghttp2_bufs_next_present(nghttp2_bufs *bufs) {
  nghttp2_buf_chain *chain;

  chain = bufs->cur->next;

  return chain && nghttp2_buf_len(&chain->buf);
}
