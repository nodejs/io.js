/*
 * ngtcp2
 *
 * Copyright (c) 2023 ngtcp2 contributors
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
#include "ngtcp2_frame_chain.h"

#include <string.h>
#include <assert.h>

ngtcp2_objalloc_def(frame_chain, ngtcp2_frame_chain, oplent);

int ngtcp2_frame_chain_new(ngtcp2_frame_chain **pfrc, const ngtcp2_mem *mem) {
  *pfrc = ngtcp2_mem_malloc(mem, sizeof(ngtcp2_frame_chain));
  if (*pfrc == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  ngtcp2_frame_chain_init(*pfrc);

  return 0;
}

int ngtcp2_frame_chain_objalloc_new(ngtcp2_frame_chain **pfrc,
                                    ngtcp2_objalloc *objalloc) {
  *pfrc = ngtcp2_objalloc_frame_chain_get(objalloc);
  if (*pfrc == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  ngtcp2_frame_chain_init(*pfrc);

  return 0;
}

int ngtcp2_frame_chain_extralen_new(ngtcp2_frame_chain **pfrc, size_t extralen,
                                    const ngtcp2_mem *mem) {
  *pfrc = ngtcp2_mem_malloc(mem, sizeof(ngtcp2_frame_chain) + extralen);
  if (*pfrc == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  ngtcp2_frame_chain_init(*pfrc);

  return 0;
}

int ngtcp2_frame_chain_stream_datacnt_objalloc_new(ngtcp2_frame_chain **pfrc,
                                                   size_t datacnt,
                                                   ngtcp2_objalloc *objalloc,
                                                   const ngtcp2_mem *mem) {
  size_t need, avail = sizeof(ngtcp2_frame) - sizeof(ngtcp2_stream);

  if (datacnt > 1) {
    need = sizeof(ngtcp2_vec) * (datacnt - 1);

    if (need > avail) {
      return ngtcp2_frame_chain_extralen_new(pfrc, need - avail, mem);
    }
  }

  return ngtcp2_frame_chain_objalloc_new(pfrc, objalloc);
}

int ngtcp2_frame_chain_new_token_objalloc_new(ngtcp2_frame_chain **pfrc,
                                              const uint8_t *token,
                                              size_t tokenlen,
                                              ngtcp2_objalloc *objalloc,
                                              const ngtcp2_mem *mem) {
  size_t avail = sizeof(ngtcp2_frame) - sizeof(ngtcp2_new_token);
  int rv;
  uint8_t *p;
  ngtcp2_frame *fr;

  if (tokenlen > avail) {
    rv = ngtcp2_frame_chain_extralen_new(pfrc, tokenlen - avail, mem);
  } else {
    rv = ngtcp2_frame_chain_objalloc_new(pfrc, objalloc);
  }
  if (rv != 0) {
    return rv;
  }

  fr = &(*pfrc)->fr;
  fr->type = NGTCP2_FRAME_NEW_TOKEN;

  p = (uint8_t *)fr + sizeof(ngtcp2_new_token);
  memcpy(p, token, tokenlen);

  fr->new_token.token = p;
  fr->new_token.tokenlen = tokenlen;

  return 0;
}

void ngtcp2_frame_chain_del(ngtcp2_frame_chain *frc, const ngtcp2_mem *mem) {
  ngtcp2_frame_chain_binder *binder;

  if (frc == NULL) {
    return;
  }

  binder = frc->binder;
  if (binder && --binder->refcount == 0) {
    ngtcp2_mem_free(mem, binder);
  }

  ngtcp2_mem_free(mem, frc);
}

void ngtcp2_frame_chain_objalloc_del(ngtcp2_frame_chain *frc,
                                     ngtcp2_objalloc *objalloc,
                                     const ngtcp2_mem *mem) {
  ngtcp2_frame_chain_binder *binder;

  if (frc == NULL) {
    return;
  }

  switch (frc->fr.type) {
  case NGTCP2_FRAME_CRYPTO:
  case NGTCP2_FRAME_STREAM:
    if (frc->fr.stream.datacnt &&
        sizeof(ngtcp2_vec) * (frc->fr.stream.datacnt - 1) >
            sizeof(ngtcp2_frame) - sizeof(ngtcp2_stream)) {
      ngtcp2_frame_chain_del(frc, mem);

      return;
    }

    break;
  case NGTCP2_FRAME_NEW_TOKEN:
    if (frc->fr.new_token.tokenlen >
        sizeof(ngtcp2_frame) - sizeof(ngtcp2_new_token)) {
      ngtcp2_frame_chain_del(frc, mem);

      return;
    }

    break;
  }

  binder = frc->binder;
  if (binder && --binder->refcount == 0) {
    ngtcp2_mem_free(mem, binder);
  }

  frc->binder = NULL;

  ngtcp2_objalloc_frame_chain_release(objalloc, frc);
}

void ngtcp2_frame_chain_init(ngtcp2_frame_chain *frc) {
  frc->next = NULL;
  frc->binder = NULL;
}

void ngtcp2_frame_chain_list_objalloc_del(ngtcp2_frame_chain *frc,
                                          ngtcp2_objalloc *objalloc,
                                          const ngtcp2_mem *mem) {
  ngtcp2_frame_chain *next;

  for (; frc; frc = next) {
    next = frc->next;

    ngtcp2_frame_chain_objalloc_del(frc, objalloc, mem);
  }
}

int ngtcp2_frame_chain_binder_new(ngtcp2_frame_chain_binder **pbinder,
                                  const ngtcp2_mem *mem) {
  *pbinder = ngtcp2_mem_calloc(mem, 1, sizeof(ngtcp2_frame_chain_binder));
  if (*pbinder == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  return 0;
}

int ngtcp2_bind_frame_chains(ngtcp2_frame_chain *a, ngtcp2_frame_chain *b,
                             const ngtcp2_mem *mem) {
  ngtcp2_frame_chain_binder *binder;
  int rv;

  assert(b->binder == NULL);

  if (a->binder == NULL) {
    rv = ngtcp2_frame_chain_binder_new(&binder, mem);
    if (rv != 0) {
      return rv;
    }

    a->binder = binder;
    ++a->binder->refcount;
  }

  b->binder = a->binder;
  ++b->binder->refcount;

  return 0;
}
