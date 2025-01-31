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
#ifndef NGTCP2_CRYPTO_H
#define NGTCP2_CRYPTO_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <ngtcp2/ngtcp2.h>

#include "ngtcp2_mem.h"

/* NGTCP2_INITIAL_AEAD_OVERHEAD is an overhead of AEAD used by Initial
   packets.  Because QUIC uses AEAD_AES_128_GCM, the overhead is 16
   bytes. */
#define NGTCP2_INITIAL_AEAD_OVERHEAD 16

/* NGTCP2_MAX_AEAD_OVERHEAD is the maximum AEAD overhead. */
#define NGTCP2_MAX_AEAD_OVERHEAD 16

/* NGTCP2_CRYPTO_KM_FLAG_NONE indicates that no flag is set. */
#define NGTCP2_CRYPTO_KM_FLAG_NONE 0x00u
/* NGTCP2_CRYPTO_KM_FLAG_KEY_PHASE_ONE is set if key phase bit is
   set. */
#define NGTCP2_CRYPTO_KM_FLAG_KEY_PHASE_ONE 0x01u

typedef struct ngtcp2_crypto_km {
  ngtcp2_vec secret;
  ngtcp2_crypto_aead_ctx aead_ctx;
  ngtcp2_vec iv;
  /* pkt_num is a packet number of a packet which uses this keying
     material.  For encryption key, it is the lowest packet number of
     a packet.  For decryption key, it is the lowest packet number of
     a packet which can be decrypted with this keying material. */
  int64_t pkt_num;
  /* use_count is the number of encryption applied with this key. */
  uint64_t use_count;
  /* flags is the bitwise OR of zero or more of
     NGTCP2_CRYPTO_KM_FLAG_*. */
  uint8_t flags;
} ngtcp2_crypto_km;

/*
 * ngtcp2_crypto_km_new creates new ngtcp2_crypto_km object, and
 * assigns its pointer to |*pckm|.  The |secret| of length
 * |secretlen|, |aead_ctx|, and the |iv| of length |ivlen| are copied
 * to |*pckm|.  If |secretlen| == 0, the function assumes no secret is
 * given which is acceptable.  The sole reason to store secret is
 * update keys.  Only 1RTT key can be updated.
 */
int ngtcp2_crypto_km_new(ngtcp2_crypto_km **pckm, const uint8_t *secret,
                         size_t secretlen,
                         const ngtcp2_crypto_aead_ctx *aead_ctx,
                         const uint8_t *iv, size_t ivlen,
                         const ngtcp2_mem *mem);

/*
 * ngtcp2_crypto_km_nocopy_new is similar to ngtcp2_crypto_km_new, but
 * it does not copy secret, aead context, and IV.
 */
int ngtcp2_crypto_km_nocopy_new(ngtcp2_crypto_km **pckm, size_t secretlen,
                                size_t ivlen, const ngtcp2_mem *mem);

void ngtcp2_crypto_km_del(ngtcp2_crypto_km *ckm, const ngtcp2_mem *mem);

typedef struct ngtcp2_crypto_cc {
  ngtcp2_crypto_aead aead;
  ngtcp2_crypto_cipher hp;
  ngtcp2_crypto_km *ckm;
  ngtcp2_crypto_cipher_ctx hp_ctx;
  ngtcp2_encrypt encrypt;
  ngtcp2_decrypt decrypt;
  ngtcp2_hp_mask hp_mask;
} ngtcp2_crypto_cc;

void ngtcp2_crypto_create_nonce(uint8_t *dest, const uint8_t *iv, size_t ivlen,
                                int64_t pkt_num);

#endif /* !defined(NGTCP2_CRYPTO_H) */
