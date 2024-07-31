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
#ifndef NGTCP2_ACKTR_H
#define NGTCP2_ACKTR_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_mem.h"
#include "ngtcp2_ringbuf.h"
#include "ngtcp2_ksl.h"
#include "ngtcp2_pkt.h"
#include "ngtcp2_objalloc.h"

/* NGTCP2_ACKTR_MAX_ENT is the maximum number of ngtcp2_acktr_entry
   which ngtcp2_acktr stores. */
#define NGTCP2_ACKTR_MAX_ENT 1024

typedef struct ngtcp2_log ngtcp2_log;

/*
 * ngtcp2_acktr_entry is a range of packets which need to be acked.
 */
typedef struct ngtcp2_acktr_entry {
  union {
    struct {
      /* pkt_num is the largest packet number to acknowledge in this
         range. */
      int64_t pkt_num;
      /* len is the consecutive packets started from pkt_num which
         includes pkt_num itself counting in decreasing order.  So pkt_num
         = 987 and len = 2, this entry includes packet 987 and 986. */
      size_t len;
      /* tstamp is the timestamp when a packet denoted by pkt_num is
         received. */
      ngtcp2_tstamp tstamp;
    };

    ngtcp2_opl_entry oplent;
  };
} ngtcp2_acktr_entry;

ngtcp2_objalloc_decl(acktr_entry, ngtcp2_acktr_entry, oplent);

/*
 * ngtcp2_acktr_entry_objalloc_new allocates memory for ent, and
 * initializes it with the given parameters.  The pointer to the
 * allocated object is stored to |*ent|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_acktr_entry_objalloc_new(ngtcp2_acktr_entry **ent, int64_t pkt_num,
                                    ngtcp2_tstamp tstamp,
                                    ngtcp2_objalloc *objalloc);

/*
 * ngtcp2_acktr_entry_objalloc_del deallocates memory allocated for
 * |ent|.
 */
void ngtcp2_acktr_entry_objalloc_del(ngtcp2_acktr_entry *ent,
                                     ngtcp2_objalloc *objalloc);

typedef struct ngtcp2_acktr_ack_entry {
  /* largest_ack is the largest packet number in outgoing ACK frame */
  int64_t largest_ack;
  /* pkt_num is the packet number that ACK frame is included. */
  int64_t pkt_num;
} ngtcp2_acktr_ack_entry;

/* NGTCP2_ACKTR_FLAG_NONE indicates that no flag set. */
#define NGTCP2_ACKTR_FLAG_NONE 0x00u
/* NGTCP2_ACKTR_FLAG_IMMEDIATE_ACK indicates that immediate
   acknowledgement is required. */
#define NGTCP2_ACKTR_FLAG_IMMEDIATE_ACK 0x01u
/* NGTCP2_ACKTR_FLAG_ACTIVE_ACK indicates that there are pending
   protected packet to be acknowledged. */
#define NGTCP2_ACKTR_FLAG_ACTIVE_ACK 0x02u
/* NGTCP2_ACKTR_FLAG_CANCEL_TIMER is set when ACK delay timer is
   expired and canceled. */
#define NGTCP2_ACKTR_FLAG_CANCEL_TIMER 0x0100u

/*
 * ngtcp2_acktr tracks received packets which we have to send ack.
 */
typedef struct ngtcp2_acktr {
  ngtcp2_objalloc objalloc;
  ngtcp2_ringbuf acks;
  /* ents includes ngtcp2_acktr_entry sorted by decreasing order of
     packet number. */
  ngtcp2_ksl ents;
  ngtcp2_log *log;
  const ngtcp2_mem *mem;
  /* flags is bitwise OR of zero, or more of NGTCP2_ACKTR_FLAG_*. */
  uint16_t flags;
  /* first_unacked_ts is timestamp when ngtcp2_acktr_entry is added
     first time after the last outgoing ACK frame. */
  ngtcp2_tstamp first_unacked_ts;
  /* rx_npkt is the number of ACK eliciting packets received without
     sending ACK. */
  size_t rx_npkt;
} ngtcp2_acktr;

/*
 * ngtcp2_acktr_init initializes |acktr|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_acktr_init(ngtcp2_acktr *acktr, ngtcp2_log *log,
                      const ngtcp2_mem *mem);

/*
 * ngtcp2_acktr_free frees resources allocated for |acktr|.  It frees
 * any ngtcp2_acktr_entry added to |acktr|.
 */
void ngtcp2_acktr_free(ngtcp2_acktr *acktr);

/*
 * ngtcp2_acktr_add adds packet number |pkt_num| to |acktr|.
 * |active_ack| is nonzero if |pkt_num| is retransmittable packet.
 *
 * This function assumes that |acktr| does not contain |pkt_num|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     OUt of memory.
 */
int ngtcp2_acktr_add(ngtcp2_acktr *acktr, int64_t pkt_num, int active_ack,
                     ngtcp2_tstamp ts);

/*
 * ngtcp2_acktr_forget removes all entries which have the packet
 * number that is equal to or less than ent->pkt_num.  This function
 * assumes that |acktr| includes |ent|.
 */
void ngtcp2_acktr_forget(ngtcp2_acktr *acktr, ngtcp2_acktr_entry *ent);

/*
 * ngtcp2_acktr_get returns the pointer to pointer to the entry which
 * has the largest packet number to be acked.  If there is no entry,
 * returned value satisfies ngtcp2_ksl_it_end(&it) != 0.
 */
ngtcp2_ksl_it ngtcp2_acktr_get(ngtcp2_acktr *acktr);

/*
 * ngtcp2_acktr_empty returns nonzero if it has no packet to
 * acknowledge.
 */
int ngtcp2_acktr_empty(ngtcp2_acktr *acktr);

/*
 * ngtcp2_acktr_add_ack records outgoing ACK frame whose largest
 * acknowledged packet number is |largest_ack|.  |pkt_num| is the
 * packet number of a packet in which ACK frame is included.  This
 * function returns a pointer to the object it adds.
 */
ngtcp2_acktr_ack_entry *
ngtcp2_acktr_add_ack(ngtcp2_acktr *acktr, int64_t pkt_num, int64_t largest_ack);

/*
 * ngtcp2_acktr_recv_ack processes the incoming ACK frame |fr|.
 * |pkt_num| is a packet number which includes |fr|.  If we receive
 * ACK which acknowledges the ACKs added by ngtcp2_acktr_add_ack,
 * ngtcp2_acktr_entry which the outgoing ACK acknowledges is removed.
 */
void ngtcp2_acktr_recv_ack(ngtcp2_acktr *acktr, const ngtcp2_ack *fr);

/*
 * ngtcp2_acktr_commit_ack tells |acktr| that ACK frame is generated.
 */
void ngtcp2_acktr_commit_ack(ngtcp2_acktr *acktr);

/*
 * ngtcp2_acktr_require_active_ack returns nonzero if ACK frame should
 * be generated actively.
 */
int ngtcp2_acktr_require_active_ack(ngtcp2_acktr *acktr,
                                    ngtcp2_duration max_ack_delay,
                                    ngtcp2_tstamp ts);

/*
 * ngtcp2_acktr_immediate_ack tells |acktr| that immediate
 * acknowledgement is required.
 */
void ngtcp2_acktr_immediate_ack(ngtcp2_acktr *acktr);

#endif /* NGTCP2_ACKTR_H */
