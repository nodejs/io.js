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
#include "ngtcp2_crypto.h"

#include <string.h>
#include <assert.h>

#include "ngtcp2_net.h"

int ngtcp2_crypto_km_new(ngtcp2_crypto_km **pckm, const uint8_t *secret,
                         size_t secretlen,
                         const ngtcp2_crypto_aead_ctx *aead_ctx,
                         const uint8_t *iv, size_t ivlen,
                         const ngtcp2_mem *mem) {
  int rv = ngtcp2_crypto_km_nocopy_new(pckm, secretlen, ivlen, mem);
  if (rv != 0) {
    return rv;
  }

  if (secretlen) {
    memcpy((*pckm)->secret.base, secret, secretlen);
  }

  if (aead_ctx) {
    (*pckm)->aead_ctx = *aead_ctx;
  }

  memcpy((*pckm)->iv.base, iv, ivlen);

  return 0;
}

int ngtcp2_crypto_km_nocopy_new(ngtcp2_crypto_km **pckm, size_t secretlen,
                                size_t ivlen, const ngtcp2_mem *mem) {
  size_t len;
  uint8_t *p;

  len = sizeof(ngtcp2_crypto_km) + secretlen + ivlen;

  *pckm = ngtcp2_mem_malloc(mem, len);
  if (*pckm == NULL) {
    return NGTCP2_ERR_NOMEM;
  }

  p = (uint8_t *)(*pckm) + sizeof(ngtcp2_crypto_km);
  (*pckm)->secret.base = p;
  (*pckm)->secret.len = secretlen;
  p += secretlen;
  (*pckm)->iv.base = p;
  (*pckm)->iv.len = ivlen;
  (*pckm)->aead_ctx.native_handle = NULL;
  (*pckm)->pkt_num = -1;
  (*pckm)->use_count = 0;
  (*pckm)->flags = NGTCP2_CRYPTO_KM_FLAG_NONE;

  return 0;
}

void ngtcp2_crypto_km_del(ngtcp2_crypto_km *ckm, const ngtcp2_mem *mem) {
  if (ckm == NULL) {
    return;
  }

  if (ckm->secret.len) {
#ifdef WIN32
    SecureZeroMemory(ckm->secret.base, ckm->secret.len);
#elif defined(HAVE_EXPLICIT_BZERO)
    explicit_bzero(ckm->secret.base, ckm->secret.len);
#elif defined(HAVE_MEMSET_S)
    memset_s(ckm->secret.base, ckm->secret.len, 0, ckm->secret.len);
#endif /* defined(HAVE_MEMSET_S) */
  }

  ngtcp2_mem_free(mem, ckm);
}

void ngtcp2_crypto_create_nonce(uint8_t *dest, const uint8_t *iv, size_t ivlen,
                                int64_t pkt_num) {
  uint64_t n;

  assert(ivlen >= sizeof(n));

  memcpy(dest, iv, ivlen);

  dest += ivlen - sizeof(n);

  memcpy(&n, dest, sizeof(n));
  n ^= ngtcp2_htonl64((uint64_t)pkt_num);
  memcpy(dest, &n, sizeof(n));
}
