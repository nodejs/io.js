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
#ifndef NGTCP2_PPE_H
#define NGTCP2_PPE_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_pkt.h"
#include "ngtcp2_buf.h"
#include "ngtcp2_crypto.h"

/*
 * ngtcp2_ppe is the Protected Packet Encoder.
 */
typedef struct ngtcp2_ppe {
  ngtcp2_buf buf;
  ngtcp2_crypto_cc *cc;
  /* hdlen is the number of bytes for packet header written in buf. */
  size_t hdlen;
  /* len_offset is the offset to Length field. */
  size_t len_offset;
  /* pkt_num_offset is the offset to packet number field. */
  size_t pkt_num_offset;
  /* pkt_numlen is the number of bytes used to encode a packet
     number */
  size_t pkt_numlen;
  /* pkt_num is the packet number written in buf. */
  int64_t pkt_num;
  /* nonce is the buffer to store nonce.  It should be equal or longer
     than then length of IV. */
  uint8_t nonce[32];
} ngtcp2_ppe;

/*
 * ngtcp2_ppe_init initializes |ppe| with the given buffer.
 */
void ngtcp2_ppe_init(ngtcp2_ppe *ppe, uint8_t *out, size_t outlen,
                     ngtcp2_crypto_cc *cc);

/*
 * ngtcp2_ppe_encode_hd encodes |hd|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     The buffer is too small.
 */
int ngtcp2_ppe_encode_hd(ngtcp2_ppe *ppe, const ngtcp2_pkt_hd *hd);

/*
 * ngtcp2_ppe_encode_frame encodes |fr|.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOBUF
 *     The buffer is too small.
 */
int ngtcp2_ppe_encode_frame(ngtcp2_ppe *ppe, ngtcp2_frame *fr);

/*
 * ngtcp2_ppe_final encrypts QUIC packet payload.  If |**ppkt| is not
 * NULL, the pointer to the packet is assigned to it.
 *
 * This function returns the length of QUIC packet, including header,
 * and payload if it succeeds, or one of the following negative error
 * codes:
 *
 * NGTCP2_ERR_CALLBACK_FAILURE
 *     User-defined callback function failed.
 */
ngtcp2_ssize ngtcp2_ppe_final(ngtcp2_ppe *ppe, const uint8_t **ppkt);

/*
 * ngtcp2_ppe_left returns the number of bytes left to write
 * additional frames.  This does not count AEAD overhead.
 */
size_t ngtcp2_ppe_left(ngtcp2_ppe *ppe);

/*
 * ngtcp2_ppe_pktlen returns the provisional packet length.  It
 * includes AEAD overhead.
 */
size_t ngtcp2_ppe_pktlen(ngtcp2_ppe *ppe);

/**
 * @function
 *
 * `ngtcp2_ppe_padding` encodes PADDING frames to the end of the
 * buffer.  This function returns the number of bytes padded.
 */
size_t ngtcp2_ppe_padding(ngtcp2_ppe *ppe);

/*
 * ngtcp2_ppe_padding_hp_sample adds PADDING frame if the current
 * payload does not have enough space for header protection sample.
 * This function should be called just before calling
 * ngtcp2_ppe_final().
 *
 * This function returns the number of bytes added as padding.
 */
size_t ngtcp2_ppe_padding_hp_sample(ngtcp2_ppe *ppe);

/*
 * ngtcp2_ppe_padding_size adds PADDING frame so that the size of QUIC
 * packet is at least |n| bytes long.  If it is unable to add PADDING
 * in that way, this function still adds PADDING frame as much as
 * possible.  This function should be called just before calling
 * ngtcp2_ppe_final().  For Short packet, this function should be
 * called instead of ngtcp2_ppe_padding_hp_sample.
 *
 * This function returns the number of bytes added as padding.
 */
size_t ngtcp2_ppe_padding_size(ngtcp2_ppe *ppe, size_t n);

/*
 * ngtcp2_ppe_ensure_hp_sample returns nonzero if the buffer has
 * enough space for header protection sample.  This should be called
 * right after packet header is written.
 */
int ngtcp2_ppe_ensure_hp_sample(ngtcp2_ppe *ppe);

#endif /* NGTCP2_PPE_H */
