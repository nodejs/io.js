/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2022 nghttp3 contributors
 * Copyright (c) 2022 nghttp2 contributors
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
#ifndef NGHTTP2_EXTPRI_H
#define NGHTTP2_EXTPRI_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <nghttp2/nghttp2.h>

/*
 * NGHTTP2_EXTPRI_INC_MASK is a bit mask to retrieve incremental bit
 * from a value produced by nghttp2_extpri_to_uint8.
 */
#define NGHTTP2_EXTPRI_INC_MASK (1 << 7)

/*
 * nghttp2_extpri_to_uint8 encodes |pri| into uint8_t variable.
 */
uint8_t nghttp2_extpri_to_uint8(const nghttp2_extpri *extpri);

/*
 * nghttp2_extpri_from_uint8 decodes |u8extpri|, which is produced by
 * nghttp2_extpri_to_uint8, intto |extpri|.
 */
void nghttp2_extpri_from_uint8(nghttp2_extpri *extpri, uint8_t u8extpri);

/*
 * nghttp2_extpri_uint8_urgency extracts urgency from |PRI| which is
 * supposed to be constructed by nghttp2_extpri_to_uint8.
 */
#define nghttp2_extpri_uint8_urgency(PRI)                                      \
  ((uint32_t)((PRI) & ~NGHTTP2_EXTPRI_INC_MASK))

/*
 * nghttp2_extpri_uint8_inc extracts inc from |PRI| which is supposed to
 * be constructed by nghttp2_extpri_to_uint8.
 */
#define nghttp2_extpri_uint8_inc(PRI) (((PRI) & NGHTTP2_EXTPRI_INC_MASK) != 0)

#endif /* NGHTTP2_EXTPRI_H */
