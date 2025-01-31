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
#include "ngtcp2_idtr.h"

#include <assert.h>

void ngtcp2_idtr_init(ngtcp2_idtr *idtr, const ngtcp2_mem *mem) {
  ngtcp2_gaptr_init(&idtr->gap, mem);
}

void ngtcp2_idtr_free(ngtcp2_idtr *idtr) {
  if (idtr == NULL) {
    return;
  }

  ngtcp2_gaptr_free(&idtr->gap);
}

/*
 * id_from_stream_id translates |stream_id| to an internal ID.
 */
static uint64_t id_from_stream_id(int64_t stream_id) {
  return (uint64_t)(stream_id >> 2);
}

int ngtcp2_idtr_open(ngtcp2_idtr *idtr, int64_t stream_id) {
  uint64_t q;

  q = id_from_stream_id(stream_id);

  if (ngtcp2_gaptr_is_pushed(&idtr->gap, q, 1)) {
    return NGTCP2_ERR_STREAM_IN_USE;
  }

  return ngtcp2_gaptr_push(&idtr->gap, q, 1);
}

int ngtcp2_idtr_is_open(const ngtcp2_idtr *idtr, int64_t stream_id) {
  uint64_t q;

  q = id_from_stream_id(stream_id);

  return ngtcp2_gaptr_is_pushed(&idtr->gap, q, 1);
}
