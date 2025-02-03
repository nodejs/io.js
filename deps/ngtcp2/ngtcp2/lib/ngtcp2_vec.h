/*
 * ngtcp2
 *
 * Copyright (c) 2018 ngtcp2 contributors
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
#ifndef NGTCP2_VEC_H
#define NGTCP2_VEC_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_mem.h"

/*
 * ngtcp2_vec_lit is a convenient macro to fill the object pointed by
 * |DEST| with the literal string |LIT|.
 */
#define ngtcp2_vec_lit(DEST, LIT)                                              \
  ((DEST)->base = (uint8_t *)(LIT), (DEST)->len = sizeof(LIT) - 1, (DEST))

/*
 * ngtcp2_vec_init initializes |vec| with the given parameters.  It
 * returns |vec|.
 */
ngtcp2_vec *ngtcp2_vec_init(ngtcp2_vec *vec, const uint8_t *base, size_t len);

/*
 * ngtcp2_vec_new allocates and initializes |*pvec| with given |data|
 * of length |datalen|.  This function allocates memory for |*pvec|
 * and the given data with a single allocation, and the contents
 * pointed by |data| is copied into the allocated memory space.  To
 * free the allocated memory, call ngtcp2_vec_del.
 */
int ngtcp2_vec_new(ngtcp2_vec **pvec, const uint8_t *data, size_t datalen,
                   const ngtcp2_mem *mem);

/*
 * ngtcp2_vec_del frees the memory allocated by |vec| which is
 * allocated and initialized by ngtcp2_vec_new.
 */
void ngtcp2_vec_del(ngtcp2_vec *vec, const ngtcp2_mem *mem);

/*
 * ngtcp2_vec_len returns the sum of length in |vec| of |n| elements.
 */
uint64_t ngtcp2_vec_len(const ngtcp2_vec *vec, size_t n);

/*
 * ngtcp2_vec_len_varint is similar to ngtcp2_vec_len, but it returns
 * -1 if the sum of the length exceeds NGTCP2_MAX_VARINT.
 */
int64_t ngtcp2_vec_len_varint(const ngtcp2_vec *vec, size_t n);

/*
 * ngtcp2_vec_split splits |src| at the data position where
 * ngtcp2_vec_len(|src|) does not exceed |left| bytes.  The removed
 * vectors are moved to |dst|.  The existing elements in |dst| are
 * moved forward to make up a space.  The |maxcnt| is the maximum
 * number of elements which |dst| array can contain.  The caller must
 * set |*psrccnt| to the number of elements of |src|.  Similarly, the
 * caller must set |*pdstcnt| to the number of elements of |dst|.  The
 * split does not necessarily occur at the boundary of ngtcp2_vec
 * object.  After split has done, this function updates |*psrccnt| and
 * |*pdstcnt|.  This function returns the number of bytes moved from
 * |src| to |dst|.  If split cannot be made because doing so exceeds
 * |maxcnt|, this function returns -1.
 */
ngtcp2_ssize ngtcp2_vec_split(ngtcp2_vec *dst, size_t *pdstcnt, ngtcp2_vec *src,
                              size_t *psrccnt, size_t left, size_t maxcnt);

/*
 * ngtcp2_vec_merge merges |src| into |dst| by moving at most |left|
 * bytes from |src|.  The |maxcnt| is the maximum number of elements
 * which |dst| array can contain.  The caller must set |*pdstcnt| to
 * the number of elements of |dst|.  Similarly, the caller must set
 * |*psrccnt| to the number of elements of |src|.  After merge has
 * done, this function updates |*psrccnt| and |*pdstcnt|.  This
 * function returns the number of bytes moved from |src| to |dst|.
 */
size_t ngtcp2_vec_merge(ngtcp2_vec *dst, size_t *pdstcnt, ngtcp2_vec *src,
                        size_t *psrccnt, size_t left, size_t maxcnt);

/*
 * ngtcp2_vec_copy_at_most copies |src| of length |srccnt| to |dst| of
 * length |dstcnt|.  The total number of bytes which the copied
 * ngtcp2_vec refers to is at most |left|.  The empty elements in
 * |src| are ignored.  This function returns the number of elements
 * copied.
 */
size_t ngtcp2_vec_copy_at_most(ngtcp2_vec *dst, size_t dstcnt,
                               const ngtcp2_vec *src, size_t srccnt,
                               size_t left);

/*
 * ngtcp2_vec_copy copies |src| of length |cnt| to |dst|.  |dst| must
 * have sufficient capacity.
 */
void ngtcp2_vec_copy(ngtcp2_vec *dst, const ngtcp2_vec *src, size_t cnt);

#endif /* !defined(NGTCP2_VEC_H) */
