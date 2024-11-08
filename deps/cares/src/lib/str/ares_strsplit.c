/* MIT License
 *
 * Copyright (c) 2018 John Schember
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

void ares_strsplit_free(char **elms, size_t num_elm)
{
  ares_free_array(elms, num_elm, ares_free);
}

char **ares_strsplit_duplicate(char **elms, size_t num_elm)
{
  size_t i;
  char **out;

  if (elms == NULL || num_elm == 0) {
    return NULL; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  out = ares_malloc_zero(sizeof(*elms) * num_elm);
  if (out == NULL) {
    return NULL; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  for (i = 0; i < num_elm; i++) {
    out[i] = ares_strdup(elms[i]);
    if (out[i] == NULL) {
      ares_strsplit_free(out, num_elm); /* LCOV_EXCL_LINE: OutOfMemory */
      return NULL;                      /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  return out;
}

char **ares_strsplit(const char *in, const char *delms, size_t *num_elm)
{
  ares_status_t status;
  ares_buf_t   *buf = NULL;
  char        **out = NULL;

  if (in == NULL || delms == NULL || num_elm == NULL) {
    return NULL; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  *num_elm = 0;

  buf = ares_buf_create_const((const unsigned char *)in, ares_strlen(in));
  if (buf == NULL) {
    return NULL;
  }

  status = ares_buf_split_str(
    buf, (const unsigned char *)delms, ares_strlen(delms),
    ARES_BUF_SPLIT_NO_DUPLICATES | ARES_BUF_SPLIT_CASE_INSENSITIVE, 0, &out,
    num_elm);
  if (status != ARES_SUCCESS) {
    goto done;
  }

done:
  ares_buf_destroy(buf);
  if (status != ARES_SUCCESS) {
    out = NULL;
  }

  return out;
}
