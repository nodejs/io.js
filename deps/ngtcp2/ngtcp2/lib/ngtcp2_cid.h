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
#ifndef NGTCP2_CID_H
#define NGTCP2_CID_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_pq.h"
#include "ngtcp2_path.h"

/* NGTCP2_SCID_FLAG_NONE indicates that no flag is set. */
#define NGTCP2_SCID_FLAG_NONE 0x00u
/* NGTCP2_SCID_FLAG_USED indicates that a local endpoint observed that
   a remote endpoint uses this particular Connection ID. */
#define NGTCP2_SCID_FLAG_USED 0x01u
/* NGTCP2_SCID_FLAG_RETIRED indicates that this particular Connection
   ID is retired. */
#define NGTCP2_SCID_FLAG_RETIRED 0x02u

typedef struct ngtcp2_scid {
  ngtcp2_pq_entry pe;
  /* seq is the sequence number associated to the Connection ID. */
  uint64_t seq;
  /* cid is a connection ID */
  ngtcp2_cid cid;
  /* retired_ts is the timestamp when a remote endpoint tells that
     this Connection ID is retired. */
  ngtcp2_tstamp retired_ts;
  /* flags is the bitwise OR of zero or more of NGTCP2_SCID_FLAG_*. */
  uint8_t flags;
} ngtcp2_scid;

/* NGTCP2_DCID_FLAG_NONE indicates that no flag is set. */
#define NGTCP2_DCID_FLAG_NONE 0x00u
/* NGTCP2_DCID_FLAG_PATH_VALIDATED indicates that an associated path
   has been validated. */
#define NGTCP2_DCID_FLAG_PATH_VALIDATED 0x01u
/* NGTCP2_DCID_FLAG_TOKEN_PRESENT indicates that a stateless reset
   token is set in token field. */
#define NGTCP2_DCID_FLAG_TOKEN_PRESENT 0x02u

typedef struct ngtcp2_dcid {
  /* seq is the sequence number associated to the Connection ID. */
  uint64_t seq;
  /* cid is a Connection ID */
  ngtcp2_cid cid;
  /* path is a path which cid is bound to.  The addresses are zero
     length if cid has not been bound to a particular path yet. */
  ngtcp2_path_storage ps;
  /* retired_ts is the timestamp when this Connection ID is
     retired. */
  ngtcp2_tstamp retired_ts;
  /* bound_ts is the timestamp when this Connection ID is bound to a
     particular path.  It is only assigned when a Connection ID is
     used just for sending PATH_RESPONSE, and is not zero-length. */
  ngtcp2_tstamp bound_ts;
  /* bytes_sent is the number of bytes sent to an associated path. */
  uint64_t bytes_sent;
  /* bytes_recv is the number of bytes received from an associated
     path. */
  uint64_t bytes_recv;
  /* max_udp_payload_size is the maximum size of UDP datagram payload
     that is allowed to be sent to this path. */
  size_t max_udp_payload_size;
  /* flags is bitwise OR of zero or more of NGTCP2_DCID_FLAG_*. */
  uint8_t flags;
  /* token is a stateless reset token received along with this
     Connection ID.  The stateless reset token is tied to the
     connection, not to the particular Connection ID. */
  uint8_t token[NGTCP2_STATELESS_RESET_TOKENLEN];
} ngtcp2_dcid;

/* ngtcp2_cid_zero makes |cid| zero-length. */
void ngtcp2_cid_zero(ngtcp2_cid *cid);

/*
 * ngtcp2_cid_less returns nonzero if |lhs| is lexicographical smaller
 * than |rhs|.
 */
int ngtcp2_cid_less(const ngtcp2_cid *lhs, const ngtcp2_cid *rhs);

/*
 * ngtcp2_cid_empty returns nonzero if |cid| includes empty Connection
 * ID.
 */
int ngtcp2_cid_empty(const ngtcp2_cid *cid);

/*
 * ngtcp2_scid_init initializes |scid| with the given parameters.
 */
void ngtcp2_scid_init(ngtcp2_scid *scid, uint64_t seq, const ngtcp2_cid *cid);

/*
 * ngtcp2_scid_copy copies |src| into |dest|.
 */
void ngtcp2_scid_copy(ngtcp2_scid *dest, const ngtcp2_scid *src);

/*
 * ngtcp2_dcid_init initializes |dcid| with the given parameters.  If
 * |token| is NULL, the function fills dcid->token with 0.  |token|
 * must be NGTCP2_STATELESS_RESET_TOKENLEN bytes long.
 */
void ngtcp2_dcid_init(ngtcp2_dcid *dcid, uint64_t seq, const ngtcp2_cid *cid,
                      const uint8_t *token);

/*
 * ngtcp2_dcid_set_token sets |token| to |dcid|.  |token| must not be
 * NULL, and must be NGTCP2_STATELESS_RESET_TOKENLEN bytes long.
 */
void ngtcp2_dcid_set_token(ngtcp2_dcid *dcid, const uint8_t *token);

/*
 * ngtcp2_dcid_set_path sets |path| to |dcid|.  It sets
 * max_udp_payload_size to the minimum UDP datagram payload size
 * supported by the IP protocol version.
 */
void ngtcp2_dcid_set_path(ngtcp2_dcid *dcid, const ngtcp2_path *path);

/*
 * ngtcp2_dcid_copy copies |src| into |dest|.
 */
void ngtcp2_dcid_copy(ngtcp2_dcid *dest, const ngtcp2_dcid *src);

/*
 * ngtcp2_dcid_copy_cid_token behaves like ngtcp2_dcid_copy, but it
 * only copies cid, seq, and token.
 */
void ngtcp2_dcid_copy_cid_token(ngtcp2_dcid *dest, const ngtcp2_dcid *src);

/*
 * ngtcp2_dcid_verify_uniqueness verifies uniqueness of (|seq|, |cid|,
 * |token|) tuple against |dcid|.
 */
int ngtcp2_dcid_verify_uniqueness(const ngtcp2_dcid *dcid, uint64_t seq,
                                  const ngtcp2_cid *cid, const uint8_t *token);

/*
 * ngtcp2_dcid_verify_stateless_reset_token verifies stateless reset
 * token |token| against the one included in |dcid|.  Tokens are
 * compared in constant time.  This function returns 0 if the
 * verification succeeds, or one of the following negative error
 * codes:
 *
 * NGTCP2_ERR_INVALID_ARGUMENT
 *     Tokens do not match; or |dcid| does not contain a token.
 */
int ngtcp2_dcid_verify_stateless_reset_token(const ngtcp2_dcid *dcid,
                                             const uint8_t *token);

#endif /* !defined(NGTCP2_CID_H) */
