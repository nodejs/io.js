/*
 * nghttp3
 *
 * Copyright (c) 2022 nghttp3 contributors
 * Copyright (c) 2022 ngtcp2 contributors
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
#ifndef NGHTTP3_OPL_H
#define NGHTTP3_OPL_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <nghttp3/nghttp3.h>

typedef struct nghttp3_opl_entry nghttp3_opl_entry;

struct nghttp3_opl_entry {
  nghttp3_opl_entry *next;
};

/*
 * nghttp3_opl is an object memory pool.
 */
typedef struct nghttp3_opl {
  nghttp3_opl_entry *head;
} nghttp3_opl;

/*
 * nghttp3_opl_init initializes |opl|.
 */
void nghttp3_opl_init(nghttp3_opl *opl);

/*
 * nghttp3_opl_push inserts |ent| to |opl| head.
 */
void nghttp3_opl_push(nghttp3_opl *opl, nghttp3_opl_entry *ent);

/*
 * nghttp3_opl_pop removes the first nghttp3_opl_entry from |opl| and
 * returns it.  If |opl| does not have any entry, it returns NULL.
 */
nghttp3_opl_entry *nghttp3_opl_pop(nghttp3_opl *opl);

void nghttp3_opl_clear(nghttp3_opl *opl);

#endif /* !defined(NGHTTP3_OPL_H) */
