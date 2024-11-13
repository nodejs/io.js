/* MIT License
 *
 * Copyright (c) 2023 Brad House
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ares_private.h"
#include "ares_buf.h"
#include <limits.h>
#ifdef HAVE_STDINT_H
#  include <stdint.h>
#endif

struct ares_buf {
  const unsigned char *data;          /*!< pointer to start of data buffer */
  size_t               data_len;      /*!< total size of data in buffer */

  unsigned char       *alloc_buf;     /*!< Pointer to allocated data buffer,
                                       *   not used for const buffers */
  size_t               alloc_buf_len; /*!< Size of allocated data buffer */

  size_t               offset;        /*!< Current working offset in buffer */
  size_t               tag_offset;    /*!< Tagged offset in buffer. Uses
                                       *   SIZE_MAX if not set. */
};

ares_buf_t *ares_buf_create(void)
{
  ares_buf_t *buf = ares_malloc_zero(sizeof(*buf));
  if (buf == NULL) {
    return NULL;
  }

  buf->tag_offset = SIZE_MAX;
  return buf;
}

ares_buf_t *ares_buf_create_const(const unsigned char *data, size_t data_len)
{
  ares_buf_t *buf;

  if (data == NULL || data_len == 0) {
    return NULL;
  }

  buf = ares_buf_create();
  if (buf == NULL) {
    return NULL;
  }

  buf->data     = data;
  buf->data_len = data_len;

  return buf;
}

void ares_buf_destroy(ares_buf_t *buf)
{
  if (buf == NULL) {
    return;
  }
  ares_free(buf->alloc_buf);
  ares_free(buf);
}

static ares_bool_t ares_buf_is_const(const ares_buf_t *buf)
{
  if (buf == NULL) {
    return ARES_FALSE; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (buf->data != NULL && buf->alloc_buf == NULL) {
    return ARES_TRUE;
  }

  return ARES_FALSE;
}

void ares_buf_reclaim(ares_buf_t *buf)
{
  size_t prefix_size;
  size_t data_size;

  if (buf == NULL) {
    return;
  }

  if (ares_buf_is_const(buf)) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* Silence coverity.  All lengths are zero so would bail out later but
   * coverity doesn't know this */
  if (buf->alloc_buf == NULL) {
    return;
  }

  if (buf->tag_offset != SIZE_MAX && buf->tag_offset < buf->offset) {
    prefix_size = buf->tag_offset;
  } else {
    prefix_size = buf->offset;
  }

  if (prefix_size == 0) {
    return;
  }

  data_size = buf->data_len - prefix_size;

  memmove(buf->alloc_buf, buf->alloc_buf + prefix_size, data_size);
  buf->data      = buf->alloc_buf;
  buf->data_len  = data_size;
  buf->offset   -= prefix_size;
  if (buf->tag_offset != SIZE_MAX) {
    buf->tag_offset -= prefix_size;
  }
}

static ares_status_t ares_buf_ensure_space(ares_buf_t *buf, size_t needed_size)
{
  size_t         remaining_size;
  size_t         alloc_size;
  unsigned char *ptr;

  if (buf == NULL) {
    return ARES_EFORMERR;
  }

  if (ares_buf_is_const(buf)) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* When calling ares_buf_finish_str() we end up adding a null terminator,
   * so we want to ensure the size is always sufficient for this as we don't
   * want an ARES_ENOMEM at that point */
  needed_size++;

  /* No need to do an expensive move operation, we have enough to just append */
  remaining_size = buf->alloc_buf_len - buf->data_len;
  if (remaining_size >= needed_size) {
    return ARES_SUCCESS;
  }

  /* See if just moving consumed data frees up enough space */
  ares_buf_reclaim(buf);

  remaining_size = buf->alloc_buf_len - buf->data_len;
  if (remaining_size >= needed_size) {
    return ARES_SUCCESS;
  }

  alloc_size = buf->alloc_buf_len;

  /* Not yet started */
  if (alloc_size == 0) {
    alloc_size = 16; /* Always shifts 1, so ends up being 32 minimum */
  }

  /* Increase allocation by powers of 2 */
  do {
    alloc_size     <<= 1;
    remaining_size   = alloc_size - buf->data_len;
  } while (remaining_size < needed_size);

  ptr = ares_realloc(buf->alloc_buf, alloc_size);
  if (ptr == NULL) {
    return ARES_ENOMEM;
  }

  buf->alloc_buf     = ptr;
  buf->alloc_buf_len = alloc_size;
  buf->data          = ptr;

  return ARES_SUCCESS;
}

ares_status_t ares_buf_set_length(ares_buf_t *buf, size_t len)
{
  if (buf == NULL || ares_buf_is_const(buf)) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (len >= buf->alloc_buf_len - buf->offset) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  buf->data_len = len + buf->offset;
  return ARES_SUCCESS;
}

ares_status_t ares_buf_append(ares_buf_t *buf, const unsigned char *data,
                              size_t data_len)
{
  ares_status_t status;

  if (data == NULL && data_len != 0) {
    return ARES_EFORMERR;
  }

  if (data_len == 0) {
    return ARES_SUCCESS;
  }

  status = ares_buf_ensure_space(buf, data_len);
  if (status != ARES_SUCCESS) {
    return status;
  }

  memcpy(buf->alloc_buf + buf->data_len, data, data_len);
  buf->data_len += data_len;
  return ARES_SUCCESS;
}

ares_status_t ares_buf_append_byte(ares_buf_t *buf, unsigned char b)
{
  return ares_buf_append(buf, &b, 1);
}

ares_status_t ares_buf_append_be16(ares_buf_t *buf, unsigned short u16)
{
  ares_status_t status;

  status = ares_buf_append_byte(buf, (unsigned char)((u16 >> 8) & 0xff));
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares_buf_append_byte(buf, (unsigned char)(u16 & 0xff));
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  return ARES_SUCCESS;
}

ares_status_t ares_buf_append_be32(ares_buf_t *buf, unsigned int u32)
{
  ares_status_t status;

  status = ares_buf_append_byte(buf, ((unsigned char)(u32 >> 24) & 0xff));
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares_buf_append_byte(buf, ((unsigned char)(u32 >> 16) & 0xff));
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares_buf_append_byte(buf, ((unsigned char)(u32 >> 8) & 0xff));
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares_buf_append_byte(buf, ((unsigned char)u32 & 0xff));
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  return ARES_SUCCESS;
}

unsigned char *ares_buf_append_start(ares_buf_t *buf, size_t *len)
{
  ares_status_t status;

  if (len == NULL || *len == 0) {
    return NULL;
  }

  status = ares_buf_ensure_space(buf, *len);
  if (status != ARES_SUCCESS) {
    return NULL;
  }

  /* -1 for possible null terminator for ares_buf_finish_str() */
  *len = buf->alloc_buf_len - buf->data_len - 1;
  return buf->alloc_buf + buf->data_len;
}

void ares_buf_append_finish(ares_buf_t *buf, size_t len)
{
  if (buf == NULL) {
    return;
  }

  buf->data_len += len;
}

unsigned char *ares_buf_finish_bin(ares_buf_t *buf, size_t *len)
{
  unsigned char *ptr = NULL;
  if (buf == NULL || len == NULL || ares_buf_is_const(buf)) {
    return NULL;
  }

  ares_buf_reclaim(buf);

  /* We don't want to return NULL except on failure, may be zero-length */
  if (buf->alloc_buf == NULL && ares_buf_ensure_space(buf, 1) != ARES_SUCCESS) {
    return NULL; /* LCOV_EXCL_LINE: OutOfMemory */
  }
  ptr  = buf->alloc_buf;
  *len = buf->data_len;
  ares_free(buf);
  return ptr;
}

char *ares_buf_finish_str(ares_buf_t *buf, size_t *len)
{
  char  *ptr;
  size_t mylen;

  ptr = (char *)ares_buf_finish_bin(buf, &mylen);
  if (ptr == NULL) {
    return NULL;
  }

  if (len != NULL) {
    *len = mylen;
  }

  /* NOTE: ensured via ares_buf_ensure_space() that there is always at least
   *       1 extra byte available for this specific use-case */
  ptr[mylen] = 0;

  return ptr;
}

void ares_buf_tag(ares_buf_t *buf)
{
  if (buf == NULL) {
    return;
  }

  buf->tag_offset = buf->offset;
}

ares_status_t ares_buf_tag_rollback(ares_buf_t *buf)
{
  if (buf == NULL || buf->tag_offset == SIZE_MAX) {
    return ARES_EFORMERR;
  }

  buf->offset     = buf->tag_offset;
  buf->tag_offset = SIZE_MAX;
  return ARES_SUCCESS;
}

ares_status_t ares_buf_tag_clear(ares_buf_t *buf)
{
  if (buf == NULL || buf->tag_offset == SIZE_MAX) {
    return ARES_EFORMERR;
  }

  buf->tag_offset = SIZE_MAX;
  return ARES_SUCCESS;
}

const unsigned char *ares_buf_tag_fetch(const ares_buf_t *buf, size_t *len)
{
  if (buf == NULL || buf->tag_offset == SIZE_MAX || len == NULL) {
    return NULL;
  }

  *len = buf->offset - buf->tag_offset;
  return buf->data + buf->tag_offset;
}

size_t ares_buf_tag_length(const ares_buf_t *buf)
{
  if (buf == NULL || buf->tag_offset == SIZE_MAX) {
    return 0;
  }
  return buf->offset - buf->tag_offset;
}

ares_status_t ares_buf_tag_fetch_bytes(const ares_buf_t *buf,
                                       unsigned char *bytes, size_t *len)
{
  size_t               ptr_len = 0;
  const unsigned char *ptr     = ares_buf_tag_fetch(buf, &ptr_len);

  if (ptr == NULL || bytes == NULL || len == NULL) {
    return ARES_EFORMERR;
  }

  if (*len < ptr_len) {
    return ARES_EFORMERR;
  }

  *len = ptr_len;

  if (ptr_len > 0) {
    memcpy(bytes, ptr, ptr_len);
  }
  return ARES_SUCCESS;
}

ares_status_t ares_buf_tag_fetch_constbuf(const ares_buf_t *buf,
                                          ares_buf_t      **newbuf)
{
  size_t               ptr_len = 0;
  const unsigned char *ptr     = ares_buf_tag_fetch(buf, &ptr_len);

  if (ptr == NULL || newbuf == NULL) {
    return ARES_EFORMERR;
  }

  *newbuf = ares_buf_create_const(ptr, ptr_len);
  if (*newbuf == NULL) {
    return ARES_ENOMEM;
  }
  return ARES_SUCCESS;
}

ares_status_t ares_buf_tag_fetch_string(const ares_buf_t *buf, char *str,
                                        size_t len)
{
  size_t        out_len;
  ares_status_t status;
  size_t        i;

  if (str == NULL || len == 0) {
    return ARES_EFORMERR;
  }

  /* Space for NULL terminator */
  out_len = len - 1;

  status = ares_buf_tag_fetch_bytes(buf, (unsigned char *)str, &out_len);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* NULL terminate */
  str[out_len] = 0;

  /* Validate string is printable */
  for (i = 0; i < out_len; i++) {
    if (!ares_isprint(str[i])) {
      return ARES_EBADSTR;
    }
  }

  return ARES_SUCCESS;
}

ares_status_t ares_buf_tag_fetch_strdup(const ares_buf_t *buf, char **str)
{
  size_t               ptr_len = 0;
  const unsigned char *ptr     = ares_buf_tag_fetch(buf, &ptr_len);

  if (ptr == NULL || str == NULL) {
    return ARES_EFORMERR;
  }

  if (!ares_str_isprint((const char *)ptr, ptr_len)) {
    return ARES_EBADSTR;
  }

  *str = ares_malloc(ptr_len + 1);
  if (*str == NULL) {
    return ARES_ENOMEM;
  }

  if (ptr_len > 0) {
    memcpy(*str, ptr, ptr_len);
  }
  (*str)[ptr_len] = 0;
  return ARES_SUCCESS;
}

static const unsigned char *ares_buf_fetch(const ares_buf_t *buf, size_t *len)
{
  if (len != NULL) {
    *len = 0;
  }

  if (buf == NULL || len == NULL || buf->data == NULL) {
    return NULL;
  }

  *len = buf->data_len - buf->offset;
  if (*len == 0) {
    return NULL;
  }

  return buf->data + buf->offset;
}

ares_status_t ares_buf_consume(ares_buf_t *buf, size_t len)
{
  size_t remaining_len = ares_buf_len(buf);

  if (remaining_len < len) {
    return ARES_EBADRESP;
  }

  buf->offset += len;
  return ARES_SUCCESS;
}

ares_status_t ares_buf_fetch_be16(ares_buf_t *buf, unsigned short *u16)
{
  size_t               remaining_len;
  const unsigned char *ptr = ares_buf_fetch(buf, &remaining_len);
  unsigned int         u32;

  if (buf == NULL || u16 == NULL || remaining_len < sizeof(*u16)) {
    return ARES_EBADRESP;
  }

  /* Do math in an unsigned int in order to prevent warnings due to automatic
   * conversion by the compiler from short to int during shifts */
  u32  = ((unsigned int)(ptr[0]) << 8 | (unsigned int)ptr[1]);
  *u16 = (unsigned short)(u32 & 0xFFFF);

  return ares_buf_consume(buf, sizeof(*u16));
}

ares_status_t ares_buf_fetch_be32(ares_buf_t *buf, unsigned int *u32)
{
  size_t               remaining_len;
  const unsigned char *ptr = ares_buf_fetch(buf, &remaining_len);

  if (buf == NULL || u32 == NULL || remaining_len < sizeof(*u32)) {
    return ARES_EBADRESP;
  }

  *u32 = ((unsigned int)(ptr[0]) << 24 | (unsigned int)(ptr[1]) << 16 |
          (unsigned int)(ptr[2]) << 8 | (unsigned int)(ptr[3]));

  return ares_buf_consume(buf, sizeof(*u32));
}

ares_status_t ares_buf_fetch_bytes(ares_buf_t *buf, unsigned char *bytes,
                                   size_t len)
{
  size_t               remaining_len;
  const unsigned char *ptr = ares_buf_fetch(buf, &remaining_len);

  if (buf == NULL || bytes == NULL || len == 0 || remaining_len < len) {
    return ARES_EBADRESP;
  }

  memcpy(bytes, ptr, len);
  return ares_buf_consume(buf, len);
}

ares_status_t ares_buf_fetch_bytes_dup(ares_buf_t *buf, size_t len,
                                       ares_bool_t     null_term,
                                       unsigned char **bytes)
{
  size_t               remaining_len;
  const unsigned char *ptr = ares_buf_fetch(buf, &remaining_len);

  if (buf == NULL || bytes == NULL || len == 0 || remaining_len < len) {
    return ARES_EBADRESP;
  }

  *bytes = ares_malloc(null_term ? len + 1 : len);
  if (*bytes == NULL) {
    return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  memcpy(*bytes, ptr, len);
  if (null_term) {
    (*bytes)[len] = 0;
  }
  return ares_buf_consume(buf, len);
}

ares_status_t ares_buf_fetch_str_dup(ares_buf_t *buf, size_t len, char **str)
{
  size_t               remaining_len;
  size_t               i;
  const unsigned char *ptr = ares_buf_fetch(buf, &remaining_len);

  if (buf == NULL || str == NULL || len == 0 || remaining_len < len) {
    return ARES_EBADRESP;
  }

  /* Validate string is printable */
  for (i = 0; i < len; i++) {
    if (!ares_isprint(ptr[i])) {
      return ARES_EBADSTR;
    }
  }

  *str = ares_malloc(len + 1);
  if (*str == NULL) {
    return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  memcpy(*str, ptr, len);
  (*str)[len] = 0;

  return ares_buf_consume(buf, len);
}

ares_status_t ares_buf_fetch_bytes_into_buf(ares_buf_t *buf, ares_buf_t *dest,
                                            size_t len)
{
  size_t               remaining_len;
  const unsigned char *ptr = ares_buf_fetch(buf, &remaining_len);
  ares_status_t        status;

  if (buf == NULL || dest == NULL || len == 0 || remaining_len < len) {
    return ARES_EBADRESP;
  }

  status = ares_buf_append(dest, ptr, len);
  if (status != ARES_SUCCESS) {
    return status;
  }

  return ares_buf_consume(buf, len);
}

static ares_bool_t ares_is_whitespace(unsigned char c,
                                      ares_bool_t   include_linefeed)
{
  switch (c) {
    case '\r':
    case '\t':
    case ' ':
    case '\v':
    case '\f':
      return ARES_TRUE;
    case '\n':
      return include_linefeed;
    default:
      break;
  }
  return ARES_FALSE;
}

size_t ares_buf_consume_whitespace(ares_buf_t *buf,
                                   ares_bool_t include_linefeed)
{
  size_t               remaining_len = 0;
  const unsigned char *ptr           = ares_buf_fetch(buf, &remaining_len);
  size_t               i;

  if (ptr == NULL) {
    return 0;
  }

  for (i = 0; i < remaining_len; i++) {
    if (!ares_is_whitespace(ptr[i], include_linefeed)) {
      break;
    }
  }

  if (i > 0) {
    ares_buf_consume(buf, i);
  }
  return i;
}

size_t ares_buf_consume_nonwhitespace(ares_buf_t *buf)
{
  size_t               remaining_len = 0;
  const unsigned char *ptr           = ares_buf_fetch(buf, &remaining_len);
  size_t               i;

  if (ptr == NULL) {
    return 0;
  }

  for (i = 0; i < remaining_len; i++) {
    if (ares_is_whitespace(ptr[i], ARES_TRUE)) {
      break;
    }
  }

  if (i > 0) {
    ares_buf_consume(buf, i);
  }
  return i;
}

size_t ares_buf_consume_line(ares_buf_t *buf, ares_bool_t include_linefeed)
{
  size_t               remaining_len = 0;
  const unsigned char *ptr           = ares_buf_fetch(buf, &remaining_len);
  size_t               i;

  if (ptr == NULL) {
    return 0;
  }

  for (i = 0; i < remaining_len; i++) {
    if (ptr[i] == '\n') {
      goto done;
    }
  }

done:
  if (include_linefeed && i < remaining_len && ptr[i] == '\n') {
    i++;
  }

  if (i > 0) {
    ares_buf_consume(buf, i);
  }
  return i;
}

size_t ares_buf_consume_until_charset(ares_buf_t          *buf,
                                      const unsigned char *charset, size_t len,
                                      ares_bool_t require_charset)
{
  size_t               remaining_len = 0;
  const unsigned char *ptr           = ares_buf_fetch(buf, &remaining_len);
  size_t               pos;
  ares_bool_t          found = ARES_FALSE;

  if (ptr == NULL || charset == NULL || len == 0) {
    return 0;
  }

  /* Optimize for single character searches */
  if (len == 1) {
    const unsigned char *p = memchr(ptr, charset[0], remaining_len);
    if (p != NULL) {
      found = ARES_TRUE;
      pos   = (size_t)(p - ptr);
    } else {
      pos = remaining_len;
    }
    goto done;
  }

  for (pos = 0; pos < remaining_len; pos++) {
    size_t j;
    for (j = 0; j < len; j++) {
      if (ptr[pos] == charset[j]) {
        found = ARES_TRUE;
        goto done;
      }
    }
  }

done:
  if (require_charset && !found) {
    return SIZE_MAX;
  }

  if (pos > 0) {
    ares_buf_consume(buf, pos);
  }
  return pos;
}

size_t ares_buf_consume_until_seq(ares_buf_t *buf, const unsigned char *seq,
                                  size_t len, ares_bool_t require_seq)
{
  size_t               remaining_len = 0;
  const unsigned char *ptr           = ares_buf_fetch(buf, &remaining_len);
  const unsigned char *p;
  size_t               consume_len = 0;

  if (ptr == NULL || seq == NULL || len == 0) {
    return 0;
  }

  p = ares_memmem(ptr, remaining_len, seq, len);
  if (require_seq && p == NULL) {
    return SIZE_MAX;
  }

  if (p != NULL) {
    consume_len = (size_t)(p - ptr);
  } else {
    consume_len = remaining_len;
  }

  if (consume_len > 0) {
    ares_buf_consume(buf, consume_len);
  }

  return consume_len;
}

size_t ares_buf_consume_charset(ares_buf_t *buf, const unsigned char *charset,
                                size_t len)
{
  size_t               remaining_len = 0;
  const unsigned char *ptr           = ares_buf_fetch(buf, &remaining_len);
  size_t               i;

  if (ptr == NULL || charset == NULL || len == 0) {
    return 0;
  }

  for (i = 0; i < remaining_len; i++) {
    size_t j;
    for (j = 0; j < len; j++) {
      if (ptr[i] == charset[j]) {
        break;
      }
    }
    /* Not found */
    if (j == len) {
      break;
    }
  }

  if (i > 0) {
    ares_buf_consume(buf, i);
  }
  return i;
}

static void ares_buf_destroy_cb(void *arg)
{
  ares_buf_t **buf = arg;
  ares_buf_destroy(*buf);
}

static ares_bool_t ares_buf_split_isduplicate(ares_array_t        *arr,
                                              const unsigned char *val,
                                              size_t               len,
                                              ares_buf_split_t     flags)
{
  size_t i;
  size_t num = ares_array_len(arr);

  for (i = 0; i < num; i++) {
    ares_buf_t         **bufptr = ares_array_at(arr, i);
    const ares_buf_t    *buf    = *bufptr;
    size_t               plen   = 0;
    const unsigned char *ptr    = ares_buf_peek(buf, &plen);

    /* Can't be duplicate if lengths mismatch */
    if (plen != len) {
      continue;
    }

    if (flags & ARES_BUF_SPLIT_CASE_INSENSITIVE) {
      if (ares_memeq_ci(ptr, val, len)) {
        return ARES_TRUE;
      }
    } else {
      if (ares_memeq(ptr, val, len)) {
        return ARES_TRUE;
      }
    }
  }

  return ARES_FALSE;
}

ares_status_t ares_buf_split(ares_buf_t *buf, const unsigned char *delims,
                             size_t delims_len, ares_buf_split_t flags,
                             size_t max_sections, ares_array_t **arr)
{
  ares_status_t status = ARES_SUCCESS;
  ares_bool_t   first  = ARES_TRUE;

  if (buf == NULL || delims == NULL || delims_len == 0 || arr == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  *arr = ares_array_create(sizeof(ares_buf_t *), ares_buf_destroy_cb);
  if (*arr == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  while (ares_buf_len(buf)) {
    size_t               len = 0;
    const unsigned char *ptr;

    if (first) {
      /* No delimiter yet, just tag the start */
      ares_buf_tag(buf);
    } else {
      if (flags & ARES_BUF_SPLIT_KEEP_DELIMS) {
        /* tag then eat delimiter so its first byte in buffer */
        ares_buf_tag(buf);
        ares_buf_consume(buf, 1);
      } else {
        /* throw away delimiter */
        ares_buf_consume(buf, 1);
        ares_buf_tag(buf);
      }
    }

    if (max_sections && ares_array_len(*arr) >= max_sections - 1) {
      ares_buf_consume(buf, ares_buf_len(buf));
    } else {
      ares_buf_consume_until_charset(buf, delims, delims_len, ARES_FALSE);
    }

    ptr = ares_buf_tag_fetch(buf, &len);

    /* Shouldn't be possible */
    if (ptr == NULL) {
      status = ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
      goto done;
    }

    if (flags & ARES_BUF_SPLIT_LTRIM) {
      size_t i;
      for (i = 0; i < len; i++) {
        if (!ares_is_whitespace(ptr[i], ARES_TRUE)) {
          break;
        }
      }
      ptr += i;
      len -= i;
    }

    if (flags & ARES_BUF_SPLIT_RTRIM) {
      while (len > 0 && ares_is_whitespace(ptr[len - 1], ARES_TRUE)) {
        len--;
      }
    }

    if (len != 0 || flags & ARES_BUF_SPLIT_ALLOW_BLANK) {
      ares_buf_t *data;

      if (!(flags & ARES_BUF_SPLIT_NO_DUPLICATES) ||
          !ares_buf_split_isduplicate(*arr, ptr, len, flags)) {
        /* Since we don't allow const buffers of 0 length, and user wants
         * 0-length buffers, swap what we do here */
        if (len) {
          data = ares_buf_create_const(ptr, len);
        } else {
          data = ares_buf_create();
        }

        if (data == NULL) {
          status = ARES_ENOMEM;
          goto done;
        }

        status = ares_array_insertdata_last(*arr, &data);
        if (status != ARES_SUCCESS) {
          ares_buf_destroy(data);
          goto done;
        }
      }
    }

    first = ARES_FALSE;
  }

done:
  if (status != ARES_SUCCESS) {
    ares_array_destroy(*arr);
    *arr = NULL;
  }

  return status;
}

static void ares_free_split_array(void *arg)
{
  void **ptr = arg;
  ares_free(*ptr);
}

ares_status_t ares_buf_split_str_array(ares_buf_t          *buf,
                                       const unsigned char *delims,
                                       size_t               delims_len,
                                       ares_buf_split_t     flags,
                                       size_t max_sections, ares_array_t **arr)
{
  ares_status_t status;
  ares_array_t *split = NULL;
  size_t        i;
  size_t        len;

  if (arr == NULL) {
    return ARES_EFORMERR;
  }

  *arr = NULL;

  status = ares_buf_split(buf, delims, delims_len, flags, max_sections, &split);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  *arr = ares_array_create(sizeof(char *), ares_free_split_array);
  if (*arr == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  len = ares_array_len(split);
  for (i = 0; i < len; i++) {
    ares_buf_t **bufptr = ares_array_at(split, i);
    ares_buf_t  *lbuf   = *bufptr;
    char        *str    = NULL;

    status = ares_buf_fetch_str_dup(lbuf, ares_buf_len(lbuf), &str);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    status = ares_array_insertdata_last(*arr, &str);
    if (status != ARES_SUCCESS) {
      ares_free(str);
      goto done;
    }
  }

done:
  ares_array_destroy(split);
  if (status != ARES_SUCCESS) {
    ares_array_destroy(*arr);
    *arr = NULL;
  }
  return status;
}

ares_status_t ares_buf_split_str(ares_buf_t *buf, const unsigned char *delims,
                                 size_t delims_len, ares_buf_split_t flags,
                                 size_t max_sections, char ***strs,
                                 size_t *nstrs)
{
  ares_status_t status;
  ares_array_t *arr = NULL;

  if (strs == NULL || nstrs == NULL) {
    return ARES_EFORMERR;
  }

  *strs  = NULL;
  *nstrs = 0;

  status = ares_buf_split_str_array(buf, delims, delims_len, flags,
                                    max_sections, &arr);

  if (status != ARES_SUCCESS) {
    goto done;
  }

done:
  if (status == ARES_SUCCESS) {
    *strs = ares_array_finish(arr, nstrs);
  } else {
    ares_array_destroy(arr);
  }
  return status;
}

ares_bool_t ares_buf_begins_with(const ares_buf_t    *buf,
                                 const unsigned char *data, size_t data_len)
{
  size_t               remaining_len = 0;
  const unsigned char *ptr           = ares_buf_fetch(buf, &remaining_len);

  if (ptr == NULL || data == NULL || data_len == 0) {
    return ARES_FALSE;
  }

  if (data_len > remaining_len) {
    return ARES_FALSE;
  }

  if (memcmp(ptr, data, data_len) != 0) {
    return ARES_FALSE;
  }

  return ARES_TRUE;
}

size_t ares_buf_len(const ares_buf_t *buf)
{
  if (buf == NULL) {
    return 0;
  }

  return buf->data_len - buf->offset;
}

const unsigned char *ares_buf_peek(const ares_buf_t *buf, size_t *len)
{
  return ares_buf_fetch(buf, len);
}

ares_status_t ares_buf_peek_byte(const ares_buf_t *buf, unsigned char *b)
{
  size_t               remaining_len = 0;
  const unsigned char *ptr           = ares_buf_fetch(buf, &remaining_len);

  if (buf == NULL || b == NULL) {
    return ARES_EFORMERR;
  }

  if (remaining_len == 0) {
    return ARES_EBADRESP;
  }
  *b = ptr[0];
  return ARES_SUCCESS;
}

size_t ares_buf_get_position(const ares_buf_t *buf)
{
  if (buf == NULL) {
    return 0;
  }
  return buf->offset;
}

ares_status_t ares_buf_set_position(ares_buf_t *buf, size_t idx)
{
  if (buf == NULL) {
    return ARES_EFORMERR;
  }

  if (idx > buf->data_len) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  buf->offset = idx;
  return ARES_SUCCESS;
}

static ares_status_t
  ares_buf_parse_dns_binstr_int(ares_buf_t *buf, size_t remaining_len,
                                unsigned char **bin, size_t *bin_len,
                                ares_bool_t validate_printable)
{
  unsigned char len;
  ares_status_t status = ARES_EBADRESP;
  ares_buf_t   *binbuf = NULL;

  if (buf == NULL) {
    return ARES_EFORMERR;
  }

  if (remaining_len == 0) {
    return ARES_EBADRESP;
  }

  binbuf = ares_buf_create();
  if (binbuf == NULL) {
    return ARES_ENOMEM;
  }

  status = ares_buf_fetch_bytes(buf, &len, 1);
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  remaining_len--;

  if (len > remaining_len) {
    status = ARES_EBADRESP;
    goto done;
  }

  if (len) {
    /* When used by the _str() parser, it really needs to be validated to
     * be a valid printable ascii string.  Do that here */
    if (validate_printable && ares_buf_len(buf) >= len) {
      size_t      mylen;
      const char *data = (const char *)ares_buf_peek(buf, &mylen);
      if (!ares_str_isprint(data, len)) {
        status = ARES_EBADSTR;
        goto done;
      }
    }

    if (bin != NULL) {
      status = ares_buf_fetch_bytes_into_buf(buf, binbuf, len);
    } else {
      status = ares_buf_consume(buf, len);
    }
  }

done:
  if (status != ARES_SUCCESS) {
    ares_buf_destroy(binbuf);
  } else {
    if (bin != NULL) {
      size_t mylen = 0;
      /* NOTE: we use ares_buf_finish_str() here as we guarantee NULL
       *       Termination even though we are technically returning binary data.
       */
      *bin     = (unsigned char *)ares_buf_finish_str(binbuf, &mylen);
      *bin_len = mylen;
    }
  }

  return status;
}

ares_status_t ares_buf_parse_dns_binstr(ares_buf_t *buf, size_t remaining_len,
                                        unsigned char **bin, size_t *bin_len)
{
  return ares_buf_parse_dns_binstr_int(buf, remaining_len, bin, bin_len,
                                       ARES_FALSE);
}

ares_status_t ares_buf_parse_dns_str(ares_buf_t *buf, size_t remaining_len,
                                     char **str)
{
  size_t len;

  return ares_buf_parse_dns_binstr_int(buf, remaining_len,
                                       (unsigned char **)str, &len, ARES_TRUE);
}

ares_status_t ares_buf_append_num_dec(ares_buf_t *buf, size_t num, size_t len)
{
  size_t i;
  size_t mod;

  if (len == 0) {
    len = ares_count_digits(num);
  }

  mod = ares_pow(10, len);

  for (i = len; i > 0; i--) {
    size_t        digit = (num % mod);
    ares_status_t status;

    mod /= 10;

    /* Silence coverity.  Shouldn't be possible since we calculate it above */
    if (mod == 0) {
      return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
    }

    digit  /= mod;
    status  = ares_buf_append_byte(buf, '0' + (unsigned char)(digit & 0xFF));
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }
  return ARES_SUCCESS;
}

ares_status_t ares_buf_append_num_hex(ares_buf_t *buf, size_t num, size_t len)
{
  size_t                     i;
  static const unsigned char hexbytes[] = "0123456789ABCDEF";

  if (len == 0) {
    len = ares_count_hexdigits(num);
  }

  for (i = len; i > 0; i--) {
    ares_status_t status;
    status = ares_buf_append_byte(buf, hexbytes[(num >> ((i - 1) * 4)) & 0xF]);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }
  return ARES_SUCCESS;
}

ares_status_t ares_buf_append_str(ares_buf_t *buf, const char *str)
{
  return ares_buf_append(buf, (const unsigned char *)str, ares_strlen(str));
}

static ares_status_t ares_buf_hexdump_line(ares_buf_t *buf, size_t idx,
                                           const unsigned char *data,
                                           size_t               len)
{
  size_t        i;
  ares_status_t status;

  /* Address */
  status = ares_buf_append_num_hex(buf, idx, 6);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* | */
  status = ares_buf_append_str(buf, " | ");
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  for (i = 0; i < 16; i++) {
    if (i >= len) {
      status = ares_buf_append_str(buf, "  ");
    } else {
      status = ares_buf_append_num_hex(buf, data[i], 2);
    }
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    status = ares_buf_append_byte(buf, ' ');
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  /* | */
  status = ares_buf_append_str(buf, " | ");
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  for (i = 0; i < 16; i++) {
    if (i >= len) {
      break;
    }
    status = ares_buf_append_byte(buf, ares_isprint(data[i]) ? data[i] : '.');
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  return ares_buf_append_byte(buf, '\n');
}

ares_status_t ares_buf_hexdump(ares_buf_t *buf, const unsigned char *data,
                               size_t len)
{
  size_t i;

  /* Each line is 16 bytes */
  for (i = 0; i < len; i += 16) {
    ares_status_t status;
    status = ares_buf_hexdump_line(buf, i, data + i, len - i);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  return ARES_SUCCESS;
}

ares_status_t ares_buf_load_file(const char *filename, ares_buf_t *buf)
{
  FILE          *fp        = NULL;
  unsigned char *ptr       = NULL;
  size_t         len       = 0;
  size_t         ptr_len   = 0;
  long           ftell_len = 0;
  ares_status_t  status;

  if (filename == NULL || buf == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  fp = fopen(filename, "rb");
  if (fp == NULL) {
    int error = errno;
    switch (error) {
      case ENOENT:
      case ESRCH:
        status = ARES_ENOTFOUND;
        goto done;
      default:
        DEBUGF(fprintf(stderr, "fopen() failed with error: %d %s\n", error,
                       strerror(error)));
        DEBUGF(fprintf(stderr, "Error opening file: %s\n", filename));
        status = ARES_EFILE;
        goto done;
    }
  }

  /* Get length portably, fstat() is POSIX, not C */
  if (fseek(fp, 0, SEEK_END) != 0) {
    status = ARES_EFILE; /* LCOV_EXCL_LINE: DefensiveCoding */
    goto done;           /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  ftell_len = ftell(fp);
  if (ftell_len < 0) {
    status = ARES_EFILE; /* LCOV_EXCL_LINE: DefensiveCoding */
    goto done;           /* LCOV_EXCL_LINE: DefensiveCoding */
  }
  len = (size_t)ftell_len;

  if (fseek(fp, 0, SEEK_SET) != 0) {
    status = ARES_EFILE; /* LCOV_EXCL_LINE: DefensiveCoding */
    goto done;           /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (len == 0) {
    status = ARES_SUCCESS; /* LCOV_EXCL_LINE: DefensiveCoding */
    goto done;             /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* Read entire data into buffer */
  ptr_len = len;
  ptr     = ares_buf_append_start(buf, &ptr_len);
  if (ptr == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  ptr_len = fread(ptr, 1, len, fp);
  if (ptr_len != len) {
    status = ARES_EFILE; /* LCOV_EXCL_LINE: DefensiveCoding */
    goto done;           /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  ares_buf_append_finish(buf, len);
  status = ARES_SUCCESS;

done:
  if (fp != NULL) {
    fclose(fp);
  }
  return status;
}
