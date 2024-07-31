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
#ifndef NGTCP2_FRAME_CHAIN_H
#define NGTCP2_FRAME_CHAIN_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_pkt.h"
#include "ngtcp2_objalloc.h"

/* NGTCP2_FRAME_CHAIN_BINDER_FLAG_NONE indicates that no flag is
   set. */
#define NGTCP2_FRAME_CHAIN_BINDER_FLAG_NONE 0x00u
/* NGTCP2_FRAME_CHAIN_BINDER_FLAG_ACK indicates that an information
   which a frame carries has been acknowledged. */
#define NGTCP2_FRAME_CHAIN_BINDER_FLAG_ACK 0x01u

/*
 * ngtcp2_frame_chain_binder binds 2 or more of ngtcp2_frame_chain to
 * share the acknowledgement state.  In general, all
 * ngtcp2_frame_chains bound to the same binder must have the same
 * information.
 */
typedef struct ngtcp2_frame_chain_binder {
  size_t refcount;
  /* flags is bitwise OR of zero or more of
     NGTCP2_FRAME_CHAIN_BINDER_FLAG_*. */
  uint32_t flags;
} ngtcp2_frame_chain_binder;

int ngtcp2_frame_chain_binder_new(ngtcp2_frame_chain_binder **pbinder,
                                  const ngtcp2_mem *mem);

typedef struct ngtcp2_frame_chain ngtcp2_frame_chain;

/*
 * ngtcp2_frame_chain chains frames in a single packet.
 */
struct ngtcp2_frame_chain {
  union {
    struct {
      ngtcp2_frame_chain *next;
      ngtcp2_frame_chain_binder *binder;
      ngtcp2_frame fr;
    };

    ngtcp2_opl_entry oplent;
  };
};

ngtcp2_objalloc_decl(frame_chain, ngtcp2_frame_chain, oplent);

/*
 * ngtcp2_bind_frame_chains binds two frame chains |a| and |b| using
 * new or existing ngtcp2_frame_chain_binder.  |a| might have non-NULL
 * a->binder.  |b| must not have non-NULL b->binder.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory
 */
int ngtcp2_bind_frame_chains(ngtcp2_frame_chain *a, ngtcp2_frame_chain *b,
                             const ngtcp2_mem *mem);

/* NGTCP2_MAX_STREAM_DATACNT is the maximum number of ngtcp2_vec that
   a ngtcp2_stream can include. */
#define NGTCP2_MAX_STREAM_DATACNT 256

/*
 * ngtcp2_frame_chain_new allocates ngtcp2_frame_chain object and
 * assigns its pointer to |*pfrc|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_frame_chain_new(ngtcp2_frame_chain **pfrc, const ngtcp2_mem *mem);

/*
 * ngtcp2_frame_chain_objalloc_new behaves like
 * ngtcp2_frame_chain_new, but it uses |objalloc| to allocate the object.
 */
int ngtcp2_frame_chain_objalloc_new(ngtcp2_frame_chain **pfrc,
                                    ngtcp2_objalloc *objalloc);

/*
 * ngtcp2_frame_chain_extralen_new works like ngtcp2_frame_chain_new,
 * but it allocates extra memory |extralen| in order to extend
 * ngtcp2_frame.
 */
int ngtcp2_frame_chain_extralen_new(ngtcp2_frame_chain **pfrc, size_t extralen,
                                    const ngtcp2_mem *mem);

/*
 * ngtcp2_frame_chain_stream_datacnt_objalloc_new works like
 * ngtcp2_frame_chain_new, but it allocates enough data to store
 * additional |datacnt| - 1 ngtcp2_vec object after ngtcp2_stream
 * object.  If no additional space is required,
 * ngtcp2_frame_chain_objalloc_new is called internally.
 */
int ngtcp2_frame_chain_stream_datacnt_objalloc_new(ngtcp2_frame_chain **pfrc,
                                                   size_t datacnt,
                                                   ngtcp2_objalloc *objalloc,
                                                   const ngtcp2_mem *mem);

int ngtcp2_frame_chain_new_token_objalloc_new(ngtcp2_frame_chain **pfrc,
                                              const uint8_t *token,
                                              size_t tokenlen,
                                              ngtcp2_objalloc *objalloc,
                                              const ngtcp2_mem *mem);

/*
 * ngtcp2_frame_chain_del deallocates |frc|.  It also deallocates the
 * memory pointed by |frc|.
 */
void ngtcp2_frame_chain_del(ngtcp2_frame_chain *frc, const ngtcp2_mem *mem);

/*
 * ngtcp2_frame_chain_objalloc_del adds |frc| to |objalloc| for reuse.
 * It might just delete |frc| depending on the frame type and the size
 * of |frc|.
 */
void ngtcp2_frame_chain_objalloc_del(ngtcp2_frame_chain *frc,
                                     ngtcp2_objalloc *objalloc,
                                     const ngtcp2_mem *mem);

/*
 * ngtcp2_frame_chain_init initializes |frc|.
 */
void ngtcp2_frame_chain_init(ngtcp2_frame_chain *frc);

/*
 * ngtcp2_frame_chain_list_objalloc_del adds all ngtcp2_frame_chain
 * linked from |frc| to |objalloc| for reuse.  Depending on the frame type
 * and its size, ngtcp2_frame_chain might be deleted instead.
 */
void ngtcp2_frame_chain_list_objalloc_del(ngtcp2_frame_chain *frc,
                                          ngtcp2_objalloc *objalloc,
                                          const ngtcp2_mem *mem);

#endif /* NGTCP2_FRAME_CHAIN_H */
