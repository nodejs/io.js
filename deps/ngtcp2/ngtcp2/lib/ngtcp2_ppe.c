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
#include "ngtcp2_ppe.h"

#include <string.h>
#include <assert.h>

#include "ngtcp2_str.h"
#include "ngtcp2_conv.h"

void ngtcp2_ppe_init(ngtcp2_ppe *ppe, uint8_t *out, size_t outlen,
                     ngtcp2_crypto_cc *cc) {
  ngtcp2_buf_init(&ppe->buf, out, outlen);

  ppe->hdlen = 0;
  ppe->len_offset = 0;
  ppe->pkt_num_offset = 0;
  ppe->pkt_numlen = 0;
  ppe->pkt_num = 0;
  ppe->cc = cc;
}

int ngtcp2_ppe_encode_hd(ngtcp2_ppe *ppe, const ngtcp2_pkt_hd *hd) {
  ngtcp2_ssize rv;
  ngtcp2_buf *buf = &ppe->buf;
  ngtcp2_crypto_cc *cc = ppe->cc;

  if (ngtcp2_buf_left(buf) < cc->aead.max_overhead) {
    return NGTCP2_ERR_NOBUF;
  }

  if (hd->flags & NGTCP2_PKT_FLAG_LONG_FORM) {
    ppe->len_offset = 1 + 4 + 1 + hd->dcid.datalen + 1 + hd->scid.datalen;
    if (hd->type == NGTCP2_PKT_INITIAL) {
      ppe->len_offset += ngtcp2_put_uvarintlen(hd->tokenlen) + hd->tokenlen;
    }
    ppe->pkt_num_offset = ppe->len_offset + NGTCP2_PKT_LENGTHLEN;
    rv = ngtcp2_pkt_encode_hd_long(
        buf->last, ngtcp2_buf_left(buf) - cc->aead.max_overhead, hd);
  } else {
    ppe->pkt_num_offset = 1 + hd->dcid.datalen;
    rv = ngtcp2_pkt_encode_hd_short(
        buf->last, ngtcp2_buf_left(buf) - cc->aead.max_overhead, hd);
  }
  if (rv < 0) {
    return (int)rv;
  }

  buf->last += rv;

  ppe->pkt_numlen = hd->pkt_numlen;
  ppe->hdlen = (size_t)rv;

  ppe->pkt_num = hd->pkt_num;

  return 0;
}

int ngtcp2_ppe_encode_frame(ngtcp2_ppe *ppe, ngtcp2_frame *fr) {
  ngtcp2_ssize rv;
  ngtcp2_buf *buf = &ppe->buf;
  ngtcp2_crypto_cc *cc = ppe->cc;

  if (ngtcp2_buf_left(buf) < cc->aead.max_overhead) {
    return NGTCP2_ERR_NOBUF;
  }

  rv = ngtcp2_pkt_encode_frame(
      buf->last, ngtcp2_buf_left(buf) - cc->aead.max_overhead, fr);
  if (rv < 0) {
    return (int)rv;
  }

  buf->last += rv;

  return 0;
}

/*
 * ppe_sample_offset returns the offset to sample for packet number
 * encryption.
 */
static size_t ppe_sample_offset(ngtcp2_ppe *ppe) {
  return ppe->pkt_num_offset + 4;
}

ngtcp2_ssize ngtcp2_ppe_final(ngtcp2_ppe *ppe, const uint8_t **ppkt) {
  ngtcp2_buf *buf = &ppe->buf;
  ngtcp2_crypto_cc *cc = ppe->cc;
  uint8_t *payload = buf->begin + ppe->hdlen;
  size_t payloadlen = ngtcp2_buf_len(buf) - ppe->hdlen;
  uint8_t mask[NGTCP2_HP_SAMPLELEN];
  uint8_t *p;
  size_t i;
  int rv;

  assert(cc->encrypt);
  assert(cc->hp_mask);

  if (ppe->len_offset) {
    ngtcp2_put_uvarint30(
        buf->begin + ppe->len_offset,
        (uint16_t)(payloadlen + ppe->pkt_numlen + cc->aead.max_overhead));
  }

  ngtcp2_crypto_create_nonce(ppe->nonce, cc->ckm->iv.base, cc->ckm->iv.len,
                             ppe->pkt_num);

  rv = cc->encrypt(payload, &cc->aead, &cc->ckm->aead_ctx, payload, payloadlen,
                   ppe->nonce, cc->ckm->iv.len, buf->begin, ppe->hdlen);
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  buf->last = payload + payloadlen + cc->aead.max_overhead;

  /* TODO Check that we have enough space to get sample */
  assert(ppe_sample_offset(ppe) + NGTCP2_HP_SAMPLELEN <= ngtcp2_buf_len(buf));

  rv = cc->hp_mask(mask, &cc->hp, &cc->hp_ctx,
                   buf->begin + ppe_sample_offset(ppe));
  if (rv != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  p = buf->begin;
  if (*p & NGTCP2_HEADER_FORM_BIT) {
    *p = (uint8_t)(*p ^ (mask[0] & 0x0f));
  } else {
    *p = (uint8_t)(*p ^ (mask[0] & 0x1f));
  }

  p = buf->begin + ppe->pkt_num_offset;
  for (i = 0; i < ppe->pkt_numlen; ++i) {
    *(p + i) ^= mask[i + 1];
  }

  if (ppkt != NULL) {
    *ppkt = buf->begin;
  }

  return (ngtcp2_ssize)ngtcp2_buf_len(buf);
}

size_t ngtcp2_ppe_left(ngtcp2_ppe *ppe) {
  ngtcp2_crypto_cc *cc = ppe->cc;

  if (ngtcp2_buf_left(&ppe->buf) < cc->aead.max_overhead) {
    return 0;
  }

  return ngtcp2_buf_left(&ppe->buf) - cc->aead.max_overhead;
}

size_t ngtcp2_ppe_pktlen(ngtcp2_ppe *ppe) {
  ngtcp2_crypto_cc *cc = ppe->cc;

  return ngtcp2_buf_len(&ppe->buf) + cc->aead.max_overhead;
}

size_t ngtcp2_ppe_padding(ngtcp2_ppe *ppe) {
  ngtcp2_crypto_cc *cc = ppe->cc;
  ngtcp2_buf *buf = &ppe->buf;
  size_t len;

  assert(ngtcp2_buf_left(buf) >= cc->aead.max_overhead);

  len = ngtcp2_buf_left(buf) - cc->aead.max_overhead;
  memset(buf->last, 0, len);
  buf->last += len;

  return len;
}

size_t ngtcp2_ppe_padding_hp_sample(ngtcp2_ppe *ppe) {
  ngtcp2_crypto_cc *cc = ppe->cc;
  ngtcp2_buf *buf = &ppe->buf;
  size_t max_samplelen;
  size_t len = 0;

  assert(cc->aead.max_overhead);

  max_samplelen =
      ngtcp2_buf_len(buf) + cc->aead.max_overhead - ppe_sample_offset(ppe);
  if (max_samplelen < NGTCP2_HP_SAMPLELEN) {
    len = NGTCP2_HP_SAMPLELEN - max_samplelen;
    assert(ngtcp2_ppe_left(ppe) >= len);
    memset(buf->last, 0, len);
    buf->last += len;
  }

  return len;
}

size_t ngtcp2_ppe_padding_size(ngtcp2_ppe *ppe, size_t n) {
  ngtcp2_crypto_cc *cc = ppe->cc;
  ngtcp2_buf *buf = &ppe->buf;
  size_t pktlen = ngtcp2_buf_len(buf) + cc->aead.max_overhead;
  size_t len;

  if (pktlen >= n) {
    return 0;
  }

  len = n - pktlen;
  buf->last = ngtcp2_setmem(buf->last, 0, len);

  return len;
}

int ngtcp2_ppe_ensure_hp_sample(ngtcp2_ppe *ppe) {
  ngtcp2_buf *buf = &ppe->buf;
  return ngtcp2_buf_left(buf) >= (4 - ppe->pkt_numlen) + NGTCP2_HP_SAMPLELEN;
}
