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
#ifndef NGHTTP2_BUF_H
#define NGHTTP2_BUF_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <nghttp2/nghttp2.h>

#include "nghttp2_int.h"
#include "nghttp2_mem.h"

typedef struct {
  /* This points to the beginning of the buffer. The effective range
     of buffer is [begin, end). */
  uint8_t *begin;
  /* This points to the memory one byte beyond the end of the
     buffer. */
  uint8_t *end;
  /* The position indicator for effective start of the buffer. pos <=
     last must be hold. */
  uint8_t *pos;
  /* The position indicator for effective one beyond of the end of the
     buffer. last <= end must be hold. */
  uint8_t *last;
  /* Mark arbitrary position in buffer [begin, end) */
  uint8_t *mark;
} nghttp2_buf;

#define nghttp2_buf_len(BUF) ((size_t)((BUF)->last - (BUF)->pos))
#define nghttp2_buf_avail(BUF) ((size_t)((BUF)->end - (BUF)->last))
#define nghttp2_buf_mark_avail(BUF) ((size_t)((BUF)->mark - (BUF)->last))
#define nghttp2_buf_cap(BUF) ((size_t)((BUF)->end - (BUF)->begin))

#define nghttp2_buf_pos_offset(BUF) ((size_t)((BUF)->pos - (BUF)->begin))
#define nghttp2_buf_last_offset(BUF) ((size_t)((BUF)->last - (BUF)->begin))

#define nghttp2_buf_shift_right(BUF, AMT)                                      \
  do {                                                                         \
    (BUF)->pos += AMT;                                                         \
    (BUF)->last += AMT;                                                        \
  } while (0)

#define nghttp2_buf_shift_left(BUF, AMT)                                       \
  do {                                                                         \
    (BUF)->pos -= AMT;                                                         \
    (BUF)->last -= AMT;                                                        \
  } while (0)

/*
 * Initializes the |buf|. No memory is allocated in this function. Use
 * nghttp2_buf_reserve() to allocate memory.
 */
void nghttp2_buf_init(nghttp2_buf *buf);

/*
 * Initializes the |buf| and allocates at least |initial| bytes of
 * memory.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 */
int nghttp2_buf_init2(nghttp2_buf *buf, size_t initial, nghttp2_mem *mem);

/*
 * Frees buffer in |buf|.
 */
void nghttp2_buf_free(nghttp2_buf *buf, nghttp2_mem *mem);

/*
 * Extends buffer so that nghttp2_buf_cap() returns at least
 * |new_cap|. If extensions took place, buffer pointers in |buf| will
 * change.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 */
int nghttp2_buf_reserve(nghttp2_buf *buf, size_t new_cap, nghttp2_mem *mem);

/*
 * Resets pos, last, mark member of |buf| to buf->begin.
 */
void nghttp2_buf_reset(nghttp2_buf *buf);

/*
 * Initializes |buf| using supplied buffer |begin| of length
 * |len|. Semantically, the application should not call *_reserve() or
 * nghttp2_free() functions for |buf|.
 */
void nghttp2_buf_wrap_init(nghttp2_buf *buf, uint8_t *begin, size_t len);

struct nghttp2_buf_chain;

typedef struct nghttp2_buf_chain nghttp2_buf_chain;

/* Chains 2 buffers */
struct nghttp2_buf_chain {
  /* Points to the subsequent buffer. NULL if there is no such
     buffer. */
  nghttp2_buf_chain *next;
  nghttp2_buf buf;
};

typedef struct {
  /* Points to the first buffer */
  nghttp2_buf_chain *head;
  /* Buffer pointer where write occurs. */
  nghttp2_buf_chain *cur;
  /* Memory allocator */
  nghttp2_mem *mem;
  /* The buffer capacity of each buf.  This field may be 0 if
     nghttp2_bufs is initialized by nghttp2_bufs_wrap_init* family
     functions. */
  size_t chunk_length;
  /* The maximum number of nghttp2_buf_chain */
  size_t max_chunk;
  /* The number of nghttp2_buf_chain allocated */
  size_t chunk_used;
  /* The number of nghttp2_buf_chain to keep on reset */
  size_t chunk_keep;
  /* pos offset from begin in each buffers. On initialization and
     reset, buf->pos and buf->last are positioned at buf->begin +
     offset. */
  size_t offset;
} nghttp2_bufs;

/*
 * This is the same as calling nghttp2_bufs_init2 with the given
 * arguments and offset = 0.
 */
int nghttp2_bufs_init(nghttp2_bufs *bufs, size_t chunk_length, size_t max_chunk,
                      nghttp2_mem *mem);

/*
 * This is the same as calling nghttp2_bufs_init3 with the given
 * arguments and chunk_keep = max_chunk.
 */
int nghttp2_bufs_init2(nghttp2_bufs *bufs, size_t chunk_length,
                       size_t max_chunk, size_t offset, nghttp2_mem *mem);

/*
 * Initializes |bufs|. Each buffer size is given in the
 * |chunk_length|.  The maximum number of buffers is given in the
 * |max_chunk|.  On reset, first |chunk_keep| buffers are kept and
 * remaining buffers are deleted.  Each buffer will have bufs->pos and
 * bufs->last shifted to left by |offset| bytes on creation and reset.
 *
 * This function allocates first buffer.  bufs->head and bufs->cur
 * will point to the first buffer after this call.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_INVALID_ARGUMENT
 *     chunk_keep is 0; or max_chunk < chunk_keep; or offset is too
 *     long.
 */
int nghttp2_bufs_init3(nghttp2_bufs *bufs, size_t chunk_length,
                       size_t max_chunk, size_t chunk_keep, size_t offset,
                       nghttp2_mem *mem);

/*
 * Frees any related resources to the |bufs|.
 */
void nghttp2_bufs_free(nghttp2_bufs *bufs);

/*
 * Initializes |bufs| using supplied buffer |begin| of length |len|.
 * The first buffer bufs->head uses buffer |begin|.  The buffer size
 * is fixed and no extra chunk buffer is allocated.  In other
 * words, max_chunk = chunk_keep = 1.  To free the resource allocated
 * for |bufs|, use nghttp2_bufs_wrap_free().
 *
 * Don't use the function which performs allocation, such as
 * nghttp2_bufs_realloc().
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 */
int nghttp2_bufs_wrap_init(nghttp2_bufs *bufs, uint8_t *begin, size_t len,
                           nghttp2_mem *mem);

/*
 * Initializes |bufs| using supplied |veclen| size of buf vector
 * |vec|.  The number of buffers is fixed and no extra chunk buffer is
 * allocated.  In other words, max_chunk = chunk_keep = |in_len|.  To
 * free the resource allocated for |bufs|, use
 * nghttp2_bufs_wrap_free().
 *
 * Don't use the function which performs allocation, such as
 * nghttp2_bufs_realloc().
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 */
int nghttp2_bufs_wrap_init2(nghttp2_bufs *bufs, const nghttp2_vec *vec,
                            size_t veclen, nghttp2_mem *mem);

/*
 * Frees any related resource to the |bufs|.  This function does not
 * free supplied buffer provided in nghttp2_bufs_wrap_init().
 */
void nghttp2_bufs_wrap_free(nghttp2_bufs *bufs);

/*
 * Reallocates internal buffer using |chunk_length|.  The max_chunk,
 * chunk_keep and offset do not change.  After successful allocation
 * of new buffer, previous buffers are deallocated without copying
 * anything into new buffers.  chunk_used is reset to 1.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_INVALID_ARGUMENT
 *     chunk_length < offset
 */
int nghttp2_bufs_realloc(nghttp2_bufs *bufs, size_t chunk_length);

/*
 * Appends the |data| of length |len| to the |bufs|. The write starts
 * at bufs->cur->buf.last. A new buffers will be allocated to store
 * all data.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_BUFFER_ERROR
 *     Out of buffer space.
 */
int nghttp2_bufs_add(nghttp2_bufs *bufs, const void *data, size_t len);

/*
 * Appends a single byte |b| to the |bufs|. The write starts at
 * bufs->cur->buf.last. A new buffers will be allocated to store all
 * data.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_BUFFER_ERROR
 *     Out of buffer space.
 */
int nghttp2_bufs_addb(nghttp2_bufs *bufs, uint8_t b);

/*
 * Behaves like nghttp2_bufs_addb(), but this does not update
 * buf->last pointer.
 */
int nghttp2_bufs_addb_hold(nghttp2_bufs *bufs, uint8_t b);

#define nghttp2_bufs_fast_addb(BUFS, B)                                        \
  do {                                                                         \
    *(BUFS)->cur->buf.last++ = B;                                              \
  } while (0)

#define nghttp2_bufs_fast_addb_hold(BUFS, B)                                   \
  do {                                                                         \
    *(BUFS)->cur->buf.last = B;                                                \
  } while (0)

/*
 * Performs bitwise-OR of |b| at bufs->cur->buf.last. A new buffers
 * will be allocated if necessary.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory.
 * NGHTTP2_ERR_BUFFER_ERROR
 *     Out of buffer space.
 */
int nghttp2_bufs_orb(nghttp2_bufs *bufs, uint8_t b);

/*
 * Behaves like nghttp2_bufs_orb(), but does not update buf->last
 * pointer.
 */
int nghttp2_bufs_orb_hold(nghttp2_bufs *bufs, uint8_t b);

#define nghttp2_bufs_fast_orb(BUFS, B)                                         \
  do {                                                                         \
    uint8_t **p = &(BUFS)->cur->buf.last;                                      \
    **p = (uint8_t)(**p | (B));                                                \
    ++(*p);                                                                    \
  } while (0)

#define nghttp2_bufs_fast_orb_hold(BUFS, B)                                    \
  do {                                                                         \
    uint8_t *p = (BUFS)->cur->buf.last;                                        \
    *p = (uint8_t)(*p | (B));                                                  \
  } while (0)

/*
 * Copies all data stored in |bufs| to the contiguous buffer.  This
 * function allocates the contiguous memory to store all data in
 * |bufs| and assigns it to |*out|.
 *
 * The contents of |bufs| is left unchanged.
 *
 * This function returns the length of copied data and assigns the
 * pointer to copied data to |*out| if it succeeds, or one of the
 * following negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 */
nghttp2_ssize nghttp2_bufs_remove(nghttp2_bufs *bufs, uint8_t **out);

/*
 * Copies all data stored in |bufs| to |out|.  This function assumes
 * that the buffer space pointed by |out| has at least
 * nghttp2_bufs(bufs) bytes.
 *
 * The contents of |bufs| is left unchanged.
 *
 * This function returns the length of copied data.
 */
size_t nghttp2_bufs_remove_copy(nghttp2_bufs *bufs, uint8_t *out);

/*
 * Resets |bufs| and makes the buffers empty.
 */
void nghttp2_bufs_reset(nghttp2_bufs *bufs);

/*
 * Moves bufs->cur to bufs->cur->next.  If resulting bufs->cur is
 * NULL, this function allocates new buffers and bufs->cur points to
 * it.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGHTTP2_ERR_NOMEM
 *     Out of memory
 * NGHTTP2_ERR_BUFFER_ERROR
 *     Out of buffer space.
 */
int nghttp2_bufs_advance(nghttp2_bufs *bufs);

/* Sets bufs->cur to bufs->head */
#define nghttp2_bufs_rewind(BUFS)                                              \
  do {                                                                         \
    (BUFS)->cur = (BUFS)->head;                                                \
  } while (0)

/*
 * Move bufs->cur, from the current position, using next member, to
 * the last buf which has nghttp2_buf_len(buf) > 0 without seeing buf
 * which satisfies nghttp2_buf_len(buf) == 0.  If
 * nghttp2_buf_len(&bufs->cur->buf) == 0 or bufs->cur->next is NULL,
 * bufs->cur is unchanged.
 */
void nghttp2_bufs_seek_last_present(nghttp2_bufs *bufs);

/*
 * Returns nonzero if bufs->cur->next is not empty.
 */
int nghttp2_bufs_next_present(nghttp2_bufs *bufs);

#define nghttp2_bufs_cur_avail(BUFS) nghttp2_buf_avail(&(BUFS)->cur->buf)

/*
 * Returns the total buffer length of |bufs|.
 */
size_t nghttp2_bufs_len(nghttp2_bufs *bufs);

#endif /* NGHTTP2_BUF_H */
