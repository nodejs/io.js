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
#ifndef NGTCP2_PMTUD_H
#define NGTCP2_PMTUD_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

typedef struct ngtcp2_pmtud {
  const ngtcp2_mem *mem;
  /* mtu_idx is the index of UDP payload size candidates to try
     out. */
  size_t mtu_idx;
  /* num_pkts_sent is the number of mtu_idx sized UDP datagram payload
     sent */
  size_t num_pkts_sent;
  /* expiry is the expired, if it is reached, send out the next UDP
     datagram.  UINT64_MAX means no expiry, or expiration is canceled.
     In either case, new probe packet should be sent unless we have
     done all attempts. */
  ngtcp2_tstamp expiry;
  /* tx_pkt_num is the smallest outgoing packet number where the
     current discovery is performed.  In other words, acknowledging
     packet whose packet number lower than that does not indicate the
     success of Path MTU Discovery. */
  int64_t tx_pkt_num;
  /* max_udp_payload_size is the maximum UDP payload size which is
     known to work. */
  size_t max_udp_payload_size;
  /* hard_max_udp_payload_size is the maximum UDP payload size that is
     going to be probed. */
  size_t hard_max_udp_payload_size;
  /* min_fail_udp_payload_size is the minimum UDP payload size that is
     known to fail. */
  size_t min_fail_udp_payload_size;
  /* probes is the array of UDP datagram payload size to probe. */
  const uint16_t *probes;
  /* probeslen is the number of probes pointed by probes. */
  size_t probeslen;
} ngtcp2_pmtud;

/*
 * ngtcp2_pmtud_new creates new ngtcp2_pmtud object, and assigns its
 * pointer to |*ppmtud|.  |max_udp_payload_size| is the maximum UDP
 * payload size that is known to work for the current path.
 * |tx_pkt_num| should be the next packet number to send, which is
 * used to differentiate the PMTUD probe packet sent by the previous
 * PMTUD.  PMTUD might finish immediately if |max_udp_payload_size| is
 * larger than or equal to all UDP payload probe candidates.
 * Therefore, call ngtcp2_pmtud_finished to check this situation.
 *
 * The array pointed by |pmtud_probes| of length |pmtud_probeslen|
 * specifies UDP datagram payload size to probe.  If |pmtud_probeslen|
 * is zero, the default probes are used.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * NGTCP2_ERR_NOMEM
 *     Out of memory.
 */
int ngtcp2_pmtud_new(ngtcp2_pmtud **ppmtud, size_t max_udp_payload_size,
                     size_t hard_max_udp_payload_size, int64_t tx_pkt_num,
                     const uint16_t *pmtud_probes, size_t pmtud_probeslen,
                     const ngtcp2_mem *mem);

/*
 * ngtcp2_pmtud_del deletes |pmtud|.
 */
void ngtcp2_pmtud_del(ngtcp2_pmtud *pmtud);

/*
 * ngtcp2_pmtud_probelen returns the length of UDP payload size for a
 * PMTUD probe packet.
 */
size_t ngtcp2_pmtud_probelen(ngtcp2_pmtud *pmtud);

/*
 * ngtcp2_pmtud_probe_sent should be invoked when a PMTUD probe packet is
 * sent.
 */
void ngtcp2_pmtud_probe_sent(ngtcp2_pmtud *pmtud, ngtcp2_duration pto,
                             ngtcp2_tstamp ts);

/*
 * ngtcp2_pmtud_require_probe returns nonzero if a PMTUD probe packet
 * should be sent.
 */
int ngtcp2_pmtud_require_probe(ngtcp2_pmtud *pmtud);

/*
 * ngtcp2_pmtud_probe_success should be invoked when a PMTUD probe
 * UDP datagram sized |payloadlen| is acknowledged.
 */
void ngtcp2_pmtud_probe_success(ngtcp2_pmtud *pmtud, size_t payloadlen);

/*
 * ngtcp2_pmtud_handle_expiry handles expiry.
 */
void ngtcp2_pmtud_handle_expiry(ngtcp2_pmtud *pmtud, ngtcp2_tstamp ts);

/*
 * ngtcp2_pmtud_finished returns nonzero if PMTUD has finished.
 */
int ngtcp2_pmtud_finished(ngtcp2_pmtud *pmtud);

#endif /* !defined(NGTCP2_PMTUD_H) */
