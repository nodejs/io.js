/*
 * ngtcp2
 *
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
#ifndef NGTCP2_OPL_H
#define NGTCP2_OPL_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

typedef struct ngtcp2_opl_entry ngtcp2_opl_entry;

struct ngtcp2_opl_entry {
  ngtcp2_opl_entry *next;
};

/*
 * ngtcp2_opl is an object memory pool.
 */
typedef struct ngtcp2_opl {
  ngtcp2_opl_entry *head;
} ngtcp2_opl;

/*
 * ngtcp2_opl_init initializes |opl|.
 */
void ngtcp2_opl_init(ngtcp2_opl *opl);

/*
 * ngtcp2_opl_push inserts |ent| to |opl| head.
 */
void ngtcp2_opl_push(ngtcp2_opl *opl, ngtcp2_opl_entry *ent);

/*
 * ngtcp2_opl_pop removes the first ngtcp2_opl_entry from |opl| and
 * returns it.  If |opl| does not have any entry, it returns NULL.
 */
ngtcp2_opl_entry *ngtcp2_opl_pop(ngtcp2_opl *opl);

void ngtcp2_opl_clear(ngtcp2_opl *opl);

#endif /* !defined(NGTCP2_OPL_H) */
