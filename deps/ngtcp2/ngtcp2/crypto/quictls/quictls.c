/*
 * ngtcp2
 *
 * Copyright (c) 2019 ngtcp2 contributors
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
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <assert.h>

#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_quictls.h>

#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#  include <openssl/core_names.h>
#endif /* OPENSSL_VERSION_NUMBER >= 0x30000000L */

#include "shared.h"

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
static int crypto_initialized;
static EVP_CIPHER *crypto_aes_128_gcm;
static EVP_CIPHER *crypto_aes_256_gcm;
static EVP_CIPHER *crypto_chacha20_poly1305;
static EVP_CIPHER *crypto_aes_128_ccm;
static EVP_CIPHER *crypto_aes_128_ctr;
static EVP_CIPHER *crypto_aes_256_ctr;
static EVP_CIPHER *crypto_chacha20;
static EVP_MD *crypto_sha256;
static EVP_MD *crypto_sha384;
static EVP_KDF *crypto_hkdf;

int ngtcp2_crypto_quictls_init(void) {
  crypto_aes_128_gcm = EVP_CIPHER_fetch(NULL, "AES-128-GCM", NULL);
  if (crypto_aes_128_gcm == NULL) {
    return -1;
  }

  crypto_aes_256_gcm = EVP_CIPHER_fetch(NULL, "AES-256-GCM", NULL);
  if (crypto_aes_256_gcm == NULL) {
    return -1;
  }

  crypto_chacha20_poly1305 = EVP_CIPHER_fetch(NULL, "ChaCha20-Poly1305", NULL);
  if (crypto_chacha20_poly1305 == NULL) {
    return -1;
  }

  crypto_aes_128_ccm = EVP_CIPHER_fetch(NULL, "AES-128-CCM", NULL);
  if (crypto_aes_128_ccm == NULL) {
    return -1;
  }

  crypto_aes_128_ctr = EVP_CIPHER_fetch(NULL, "AES-128-CTR", NULL);
  if (crypto_aes_128_ctr == NULL) {
    return -1;
  }

  crypto_aes_256_ctr = EVP_CIPHER_fetch(NULL, "AES-256-CTR", NULL);
  if (crypto_aes_256_ctr == NULL) {
    return -1;
  }

  crypto_chacha20 = EVP_CIPHER_fetch(NULL, "ChaCha20", NULL);
  if (crypto_chacha20 == NULL) {
    return -1;
  }

  crypto_sha256 = EVP_MD_fetch(NULL, "sha256", NULL);
  if (crypto_sha256 == NULL) {
    return -1;
  }

  crypto_sha384 = EVP_MD_fetch(NULL, "sha384", NULL);
  if (crypto_sha384 == NULL) {
    return -1;
  }

  crypto_hkdf = EVP_KDF_fetch(NULL, "hkdf", NULL);
  if (crypto_hkdf == NULL) {
    return -1;
  }

  crypto_initialized = 1;

  return 0;
}

static const EVP_CIPHER *crypto_aead_aes_128_gcm(void) {
  if (crypto_aes_128_gcm) {
    return crypto_aes_128_gcm;
  }

  return EVP_aes_128_gcm();
}

static const EVP_CIPHER *crypto_aead_aes_256_gcm(void) {
  if (crypto_aes_256_gcm) {
    return crypto_aes_256_gcm;
  }

  return EVP_aes_256_gcm();
}

static const EVP_CIPHER *crypto_aead_chacha20_poly1305(void) {
  if (crypto_chacha20_poly1305) {
    return crypto_chacha20_poly1305;
  }

  return EVP_chacha20_poly1305();
}

static const EVP_CIPHER *crypto_aead_aes_128_ccm(void) {
  if (crypto_aes_128_ccm) {
    return crypto_aes_128_ccm;
  }

  return EVP_aes_128_ccm();
}

static const EVP_CIPHER *crypto_cipher_aes_128_ctr(void) {
  if (crypto_aes_128_ctr) {
    return crypto_aes_128_ctr;
  }

  return EVP_aes_128_ctr();
}

static const EVP_CIPHER *crypto_cipher_aes_256_ctr(void) {
  if (crypto_aes_256_ctr) {
    return crypto_aes_256_ctr;
  }

  return EVP_aes_256_ctr();
}

static const EVP_CIPHER *crypto_cipher_chacha20(void) {
  if (crypto_chacha20) {
    return crypto_chacha20;
  }

  return EVP_chacha20();
}

static const EVP_MD *crypto_md_sha256(void) {
  if (crypto_sha256) {
    return crypto_sha256;
  }

  return EVP_sha256();
}

static const EVP_MD *crypto_md_sha384(void) {
  if (crypto_sha384) {
    return crypto_sha384;
  }

  return EVP_sha384();
}

static EVP_KDF *crypto_kdf_hkdf(void) {
  if (crypto_hkdf) {
    return crypto_hkdf;
  }

  return EVP_KDF_fetch(NULL, "hkdf", NULL);
}
#else /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
#  define crypto_aead_aes_128_gcm EVP_aes_128_gcm
#  define crypto_aead_aes_256_gcm EVP_aes_256_gcm
#  define crypto_aead_chacha20_poly1305 EVP_chacha20_poly1305
#  define crypto_aead_aes_128_ccm EVP_aes_128_ccm
#  define crypto_cipher_aes_128_ctr EVP_aes_128_ctr
#  define crypto_cipher_aes_256_ctr EVP_aes_256_ctr
#  define crypto_cipher_chacha20 EVP_chacha20
#  define crypto_md_sha256 EVP_sha256
#  define crypto_md_sha384 EVP_sha384

int ngtcp2_crypto_quictls_init(void) { return 0; }
#endif /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */

static size_t crypto_aead_max_overhead(const EVP_CIPHER *aead) {
  switch (EVP_CIPHER_nid(aead)) {
  case NID_aes_128_gcm:
  case NID_aes_256_gcm:
    return EVP_GCM_TLS_TAG_LEN;
  case NID_chacha20_poly1305:
    return EVP_CHACHAPOLY_TLS_TAG_LEN;
  case NID_aes_128_ccm:
    return EVP_CCM_TLS_TAG_LEN;
  default:
    assert(0);
    abort(); /* if NDEBUG is set */
  }
}

ngtcp2_crypto_aead *ngtcp2_crypto_aead_aes_128_gcm(ngtcp2_crypto_aead *aead) {
  return ngtcp2_crypto_aead_init(aead, (void *)crypto_aead_aes_128_gcm());
}

ngtcp2_crypto_md *ngtcp2_crypto_md_sha256(ngtcp2_crypto_md *md) {
  md->native_handle = (void *)crypto_md_sha256();
  return md;
}

ngtcp2_crypto_ctx *ngtcp2_crypto_ctx_initial(ngtcp2_crypto_ctx *ctx) {
  ngtcp2_crypto_aead_init(&ctx->aead, (void *)crypto_aead_aes_128_gcm());
  ctx->md.native_handle = (void *)crypto_md_sha256();
  ctx->hp.native_handle = (void *)crypto_cipher_aes_128_ctr();
  ctx->max_encryption = 0;
  ctx->max_decryption_failure = 0;
  return ctx;
}

ngtcp2_crypto_aead *ngtcp2_crypto_aead_init(ngtcp2_crypto_aead *aead,
                                            void *aead_native_handle) {
  aead->native_handle = aead_native_handle;
  aead->max_overhead = crypto_aead_max_overhead(aead_native_handle);
  return aead;
}

ngtcp2_crypto_aead *ngtcp2_crypto_aead_retry(ngtcp2_crypto_aead *aead) {
  return ngtcp2_crypto_aead_init(aead, (void *)crypto_aead_aes_128_gcm());
}

static const EVP_CIPHER *crypto_cipher_id_get_aead(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_3_CK_AES_128_GCM_SHA256:
    return crypto_aead_aes_128_gcm();
  case TLS1_3_CK_AES_256_GCM_SHA384:
    return crypto_aead_aes_256_gcm();
  case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
    return crypto_aead_chacha20_poly1305();
  case TLS1_3_CK_AES_128_CCM_SHA256:
    return crypto_aead_aes_128_ccm();
  default:
    return NULL;
  }
}

static uint64_t crypto_cipher_id_get_aead_max_encryption(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_3_CK_AES_128_GCM_SHA256:
  case TLS1_3_CK_AES_256_GCM_SHA384:
    return NGTCP2_CRYPTO_MAX_ENCRYPTION_AES_GCM;
  case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
    return NGTCP2_CRYPTO_MAX_ENCRYPTION_CHACHA20_POLY1305;
  case TLS1_3_CK_AES_128_CCM_SHA256:
    return NGTCP2_CRYPTO_MAX_ENCRYPTION_AES_CCM;
  default:
    return 0;
  }
}

static uint64_t
crypto_cipher_id_get_aead_max_decryption_failure(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_3_CK_AES_128_GCM_SHA256:
  case TLS1_3_CK_AES_256_GCM_SHA384:
    return NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_AES_GCM;
  case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
    return NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_CHACHA20_POLY1305;
  case TLS1_3_CK_AES_128_CCM_SHA256:
    return NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_AES_CCM;
  default:
    return 0;
  }
}

static const EVP_CIPHER *crypto_cipher_id_get_hp(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_3_CK_AES_128_GCM_SHA256:
  case TLS1_3_CK_AES_128_CCM_SHA256:
    return crypto_cipher_aes_128_ctr();
  case TLS1_3_CK_AES_256_GCM_SHA384:
    return crypto_cipher_aes_256_ctr();
  case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
    return crypto_cipher_chacha20();
  default:
    return NULL;
  }
}

static const EVP_MD *crypto_cipher_id_get_md(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_3_CK_AES_128_GCM_SHA256:
  case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
  case TLS1_3_CK_AES_128_CCM_SHA256:
    return crypto_md_sha256();
  case TLS1_3_CK_AES_256_GCM_SHA384:
    return crypto_md_sha384();
  default:
    return NULL;
  }
}

static int supported_cipher_id(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_3_CK_AES_128_GCM_SHA256:
  case TLS1_3_CK_AES_256_GCM_SHA384:
  case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
  case TLS1_3_CK_AES_128_CCM_SHA256:
    return 1;
  default:
    return 0;
  }
}

static ngtcp2_crypto_ctx *crypto_ctx_cipher_id(ngtcp2_crypto_ctx *ctx,
                                               uint32_t cipher_id) {
  ngtcp2_crypto_aead_init(&ctx->aead,
                          (void *)crypto_cipher_id_get_aead(cipher_id));
  ctx->md.native_handle = (void *)crypto_cipher_id_get_md(cipher_id);
  ctx->hp.native_handle = (void *)crypto_cipher_id_get_hp(cipher_id);
  ctx->max_encryption = crypto_cipher_id_get_aead_max_encryption(cipher_id);
  ctx->max_decryption_failure =
    crypto_cipher_id_get_aead_max_decryption_failure(cipher_id);

  return ctx;
}

ngtcp2_crypto_ctx *ngtcp2_crypto_ctx_tls(ngtcp2_crypto_ctx *ctx,
                                         void *tls_native_handle) {
  SSL *ssl = tls_native_handle;
  const SSL_CIPHER *cipher = SSL_get_current_cipher(ssl);
  uint32_t cipher_id;

  if (cipher == NULL) {
    return NULL;
  }

  cipher_id = (uint32_t)SSL_CIPHER_get_id(cipher);

  if (!supported_cipher_id(cipher_id)) {
    return NULL;
  }

  return crypto_ctx_cipher_id(ctx, cipher_id);
}

ngtcp2_crypto_ctx *ngtcp2_crypto_ctx_tls_early(ngtcp2_crypto_ctx *ctx,
                                               void *tls_native_handle) {
  return ngtcp2_crypto_ctx_tls(ctx, tls_native_handle);
}

static size_t crypto_md_hashlen(const EVP_MD *md) {
  return (size_t)EVP_MD_size(md);
}

size_t ngtcp2_crypto_md_hashlen(const ngtcp2_crypto_md *md) {
  return crypto_md_hashlen(md->native_handle);
}

static size_t crypto_aead_keylen(const EVP_CIPHER *aead) {
  return (size_t)EVP_CIPHER_key_length(aead);
}

size_t ngtcp2_crypto_aead_keylen(const ngtcp2_crypto_aead *aead) {
  return crypto_aead_keylen(aead->native_handle);
}

static size_t crypto_aead_noncelen(const EVP_CIPHER *aead) {
  return (size_t)EVP_CIPHER_iv_length(aead);
}

size_t ngtcp2_crypto_aead_noncelen(const ngtcp2_crypto_aead *aead) {
  return crypto_aead_noncelen(aead->native_handle);
}

int ngtcp2_crypto_aead_ctx_encrypt_init(ngtcp2_crypto_aead_ctx *aead_ctx,
                                        const ngtcp2_crypto_aead *aead,
                                        const uint8_t *key, size_t noncelen) {
  const EVP_CIPHER *cipher = aead->native_handle;
  int cipher_nid = EVP_CIPHER_nid(cipher);
  EVP_CIPHER_CTX *actx;
  size_t taglen = crypto_aead_max_overhead(cipher);
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  OSSL_PARAM params[3];
#endif /* OPENSSL_VERSION_NUMBER >= 0x30000000L */

  actx = EVP_CIPHER_CTX_new();
  if (actx == NULL) {
    return -1;
  }

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  params[0] = OSSL_PARAM_construct_size_t(OSSL_CIPHER_PARAM_IVLEN, &noncelen);

  if (cipher_nid == NID_aes_128_ccm) {
    params[1] = OSSL_PARAM_construct_octet_string(OSSL_CIPHER_PARAM_AEAD_TAG,
                                                  NULL, taglen);
    params[2] = OSSL_PARAM_construct_end();
  } else {
    params[1] = OSSL_PARAM_construct_end();
  }
#endif /* OPENSSL_VERSION_NUMBER >= 0x30000000L */

  if (!EVP_EncryptInit_ex(actx, cipher, NULL, NULL, NULL) ||
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
      !EVP_CIPHER_CTX_set_params(actx, params) ||
#else  /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
      !EVP_CIPHER_CTX_ctrl(actx, EVP_CTRL_AEAD_SET_IVLEN, (int)noncelen,
                           NULL) ||
      (cipher_nid == NID_aes_128_ccm &&
       !EVP_CIPHER_CTX_ctrl(actx, EVP_CTRL_AEAD_SET_TAG, (int)taglen, NULL)) ||
#endif /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
      !EVP_EncryptInit_ex(actx, NULL, NULL, key, NULL)) {
    EVP_CIPHER_CTX_free(actx);
    return -1;
  }

  aead_ctx->native_handle = actx;

  return 0;
}

int ngtcp2_crypto_aead_ctx_decrypt_init(ngtcp2_crypto_aead_ctx *aead_ctx,
                                        const ngtcp2_crypto_aead *aead,
                                        const uint8_t *key, size_t noncelen) {
  const EVP_CIPHER *cipher = aead->native_handle;
  int cipher_nid = EVP_CIPHER_nid(cipher);
  EVP_CIPHER_CTX *actx;
  size_t taglen = crypto_aead_max_overhead(cipher);
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  OSSL_PARAM params[3];
#endif /* OPENSSL_VERSION_NUMBER >= 0x30000000L */

  actx = EVP_CIPHER_CTX_new();
  if (actx == NULL) {
    return -1;
  }

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  params[0] = OSSL_PARAM_construct_size_t(OSSL_CIPHER_PARAM_IVLEN, &noncelen);

  if (cipher_nid == NID_aes_128_ccm) {
    params[1] = OSSL_PARAM_construct_octet_string(OSSL_CIPHER_PARAM_AEAD_TAG,
                                                  NULL, taglen);
    params[2] = OSSL_PARAM_construct_end();
  } else {
    params[1] = OSSL_PARAM_construct_end();
  }
#endif /* OPENSSL_VERSION_NUMBER >= 0x30000000L */

  if (!EVP_DecryptInit_ex(actx, cipher, NULL, NULL, NULL) ||
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
      !EVP_CIPHER_CTX_set_params(actx, params) ||
#else  /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
      !EVP_CIPHER_CTX_ctrl(actx, EVP_CTRL_AEAD_SET_IVLEN, (int)noncelen,
                           NULL) ||
      (cipher_nid == NID_aes_128_ccm &&
       !EVP_CIPHER_CTX_ctrl(actx, EVP_CTRL_AEAD_SET_TAG, (int)taglen, NULL)) ||
#endif /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
      !EVP_DecryptInit_ex(actx, NULL, NULL, key, NULL)) {
    EVP_CIPHER_CTX_free(actx);
    return -1;
  }

  aead_ctx->native_handle = actx;

  return 0;
}

void ngtcp2_crypto_aead_ctx_free(ngtcp2_crypto_aead_ctx *aead_ctx) {
  if (aead_ctx->native_handle) {
    EVP_CIPHER_CTX_free(aead_ctx->native_handle);
  }
}

int ngtcp2_crypto_cipher_ctx_encrypt_init(ngtcp2_crypto_cipher_ctx *cipher_ctx,
                                          const ngtcp2_crypto_cipher *cipher,
                                          const uint8_t *key) {
  EVP_CIPHER_CTX *actx;

  actx = EVP_CIPHER_CTX_new();
  if (actx == NULL) {
    return -1;
  }

  if (!EVP_EncryptInit_ex(actx, cipher->native_handle, NULL, key, NULL)) {
    EVP_CIPHER_CTX_free(actx);
    return -1;
  }

  cipher_ctx->native_handle = actx;

  return 0;
}

void ngtcp2_crypto_cipher_ctx_free(ngtcp2_crypto_cipher_ctx *cipher_ctx) {
  if (cipher_ctx->native_handle) {
    EVP_CIPHER_CTX_free(cipher_ctx->native_handle);
  }
}

int ngtcp2_crypto_hkdf_extract(uint8_t *dest, const ngtcp2_crypto_md *md,
                               const uint8_t *secret, size_t secretlen,
                               const uint8_t *salt, size_t saltlen) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  const EVP_MD *prf = md->native_handle;
  EVP_KDF *kdf = crypto_kdf_hkdf();
  EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
  int mode = EVP_KDF_HKDF_MODE_EXTRACT_ONLY;
  OSSL_PARAM params[] = {
    OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode),
    OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,
                                     (char *)EVP_MD_get0_name(prf), 0),
    OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (void *)secret,
                                      secretlen),
    OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, (void *)salt,
                                      saltlen),
    OSSL_PARAM_construct_end(),
  };
  int rv = 0;

  if (!crypto_initialized) {
    EVP_KDF_free(kdf);
  }

  if (EVP_KDF_derive(kctx, dest, (size_t)EVP_MD_size(prf), params) <= 0) {
    rv = -1;
  }

  EVP_KDF_CTX_free(kctx);

  return rv;
#else  /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
  const EVP_MD *prf = md->native_handle;
  int rv = 0;
  EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
  size_t destlen = (size_t)EVP_MD_size(prf);

  if (pctx == NULL) {
    return -1;
  }

  if (EVP_PKEY_derive_init(pctx) != 1 ||
      EVP_PKEY_CTX_hkdf_mode(pctx, EVP_PKEY_HKDEF_MODE_EXTRACT_ONLY) != 1 ||
      EVP_PKEY_CTX_set_hkdf_md(pctx, prf) != 1 ||
      EVP_PKEY_CTX_set1_hkdf_salt(pctx, salt, (int)saltlen) != 1 ||
      EVP_PKEY_CTX_set1_hkdf_key(pctx, secret, (int)secretlen) != 1 ||
      EVP_PKEY_derive(pctx, dest, &destlen) != 1) {
    rv = -1;
  }

  EVP_PKEY_CTX_free(pctx);

  return rv;
#endif /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
}

int ngtcp2_crypto_hkdf_expand(uint8_t *dest, size_t destlen,
                              const ngtcp2_crypto_md *md, const uint8_t *secret,
                              size_t secretlen, const uint8_t *info,
                              size_t infolen) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  const EVP_MD *prf = md->native_handle;
  EVP_KDF *kdf = crypto_kdf_hkdf();
  EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
  int mode = EVP_KDF_HKDF_MODE_EXPAND_ONLY;
  OSSL_PARAM params[] = {
    OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode),
    OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,
                                     (char *)EVP_MD_get0_name(prf), 0),
    OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (void *)secret,
                                      secretlen),
    OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, (void *)info,
                                      infolen),
    OSSL_PARAM_construct_end(),
  };
  int rv = 0;

  if (!crypto_initialized) {
    EVP_KDF_free(kdf);
  }

  if (EVP_KDF_derive(kctx, dest, destlen, params) <= 0) {
    rv = -1;
  }

  EVP_KDF_CTX_free(kctx);

  return rv;
#else  /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
  const EVP_MD *prf = md->native_handle;
  int rv = 0;
  EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
  if (pctx == NULL) {
    return -1;
  }

  if (EVP_PKEY_derive_init(pctx) != 1 ||
      EVP_PKEY_CTX_hkdf_mode(pctx, EVP_PKEY_HKDEF_MODE_EXPAND_ONLY) != 1 ||
      EVP_PKEY_CTX_set_hkdf_md(pctx, prf) != 1 ||
      EVP_PKEY_CTX_set1_hkdf_salt(pctx, (const unsigned char *)"", 0) != 1 ||
      EVP_PKEY_CTX_set1_hkdf_key(pctx, secret, (int)secretlen) != 1 ||
      EVP_PKEY_CTX_add1_hkdf_info(pctx, info, (int)infolen) != 1 ||
      EVP_PKEY_derive(pctx, dest, &destlen) != 1) {
    rv = -1;
  }

  EVP_PKEY_CTX_free(pctx);

  return rv;
#endif /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
}

int ngtcp2_crypto_hkdf(uint8_t *dest, size_t destlen,
                       const ngtcp2_crypto_md *md, const uint8_t *secret,
                       size_t secretlen, const uint8_t *salt, size_t saltlen,
                       const uint8_t *info, size_t infolen) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  const EVP_MD *prf = md->native_handle;
  EVP_KDF *kdf = crypto_kdf_hkdf();
  EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
  OSSL_PARAM params[] = {
    OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,
                                     (char *)EVP_MD_get0_name(prf), 0),
    OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (void *)secret,
                                      secretlen),
    OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, (void *)salt,
                                      saltlen),
    OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, (void *)info,
                                      infolen),
    OSSL_PARAM_construct_end(),
  };
  int rv = 0;

  if (!crypto_initialized) {
    EVP_KDF_free(kdf);
  }

  if (EVP_KDF_derive(kctx, dest, destlen, params) <= 0) {
    rv = -1;
  }

  EVP_KDF_CTX_free(kctx);

  return rv;
#else  /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
  const EVP_MD *prf = md->native_handle;
  int rv = 0;
  EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL);
  if (pctx == NULL) {
    return -1;
  }

  if (EVP_PKEY_derive_init(pctx) != 1 ||
      EVP_PKEY_CTX_hkdf_mode(pctx, EVP_PKEY_HKDEF_MODE_EXTRACT_AND_EXPAND) !=
        1 ||
      EVP_PKEY_CTX_set_hkdf_md(pctx, prf) != 1 ||
      EVP_PKEY_CTX_set1_hkdf_salt(pctx, salt, (int)saltlen) != 1 ||
      EVP_PKEY_CTX_set1_hkdf_key(pctx, secret, (int)secretlen) != 1 ||
      EVP_PKEY_CTX_add1_hkdf_info(pctx, info, (int)infolen) != 1 ||
      EVP_PKEY_derive(pctx, dest, &destlen) != 1) {
    rv = -1;
  }

  EVP_PKEY_CTX_free(pctx);

  return rv;
#endif /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
}

int ngtcp2_crypto_encrypt(uint8_t *dest, const ngtcp2_crypto_aead *aead,
                          const ngtcp2_crypto_aead_ctx *aead_ctx,
                          const uint8_t *plaintext, size_t plaintextlen,
                          const uint8_t *nonce, size_t noncelen,
                          const uint8_t *aad, size_t aadlen) {
  const EVP_CIPHER *cipher = aead->native_handle;
  size_t taglen = crypto_aead_max_overhead(cipher);
  int cipher_nid = EVP_CIPHER_nid(cipher);
  EVP_CIPHER_CTX *actx = aead_ctx->native_handle;
  int len;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  OSSL_PARAM params[] = {
    OSSL_PARAM_construct_octet_string(OSSL_CIPHER_PARAM_AEAD_TAG,
                                      dest + plaintextlen, taglen),
    OSSL_PARAM_construct_end(),
  };
#endif /* OPENSSL_VERSION_NUMBER >= 0x30000000L */

  (void)noncelen;

  if (!EVP_EncryptInit_ex(actx, NULL, NULL, NULL, nonce) ||
      (cipher_nid == NID_aes_128_ccm &&
       !EVP_EncryptUpdate(actx, NULL, &len, NULL, (int)plaintextlen)) ||
      !EVP_EncryptUpdate(actx, NULL, &len, aad, (int)aadlen) ||
      !EVP_EncryptUpdate(actx, dest, &len, plaintext, (int)plaintextlen) ||
      !EVP_EncryptFinal_ex(actx, dest + len, &len) ||
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
      !EVP_CIPHER_CTX_get_params(actx, params)
#else  /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
      !EVP_CIPHER_CTX_ctrl(actx, EVP_CTRL_AEAD_GET_TAG, (int)taglen,
                           dest + plaintextlen)
#endif /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
  ) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_decrypt(uint8_t *dest, const ngtcp2_crypto_aead *aead,
                          const ngtcp2_crypto_aead_ctx *aead_ctx,
                          const uint8_t *ciphertext, size_t ciphertextlen,
                          const uint8_t *nonce, size_t noncelen,
                          const uint8_t *aad, size_t aadlen) {
  const EVP_CIPHER *cipher = aead->native_handle;
  size_t taglen = crypto_aead_max_overhead(cipher);
  int cipher_nid = EVP_CIPHER_nid(cipher);
  EVP_CIPHER_CTX *actx = aead_ctx->native_handle;
  int len;
  const uint8_t *tag;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  OSSL_PARAM params[2];
#endif /* OPENSSL_VERSION_NUMBER >= 0x30000000L */

  (void)noncelen;

  if (taglen > ciphertextlen) {
    return -1;
  }

  ciphertextlen -= taglen;
  tag = ciphertext + ciphertextlen;

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  params[0] = OSSL_PARAM_construct_octet_string(OSSL_CIPHER_PARAM_AEAD_TAG,
                                                (void *)tag, taglen);
  params[1] = OSSL_PARAM_construct_end();
#endif /* OPENSSL_VERSION_NUMBER >= 0x30000000L */

  if (!EVP_DecryptInit_ex(actx, NULL, NULL, NULL, nonce) ||
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
      !EVP_CIPHER_CTX_set_params(actx, params) ||
#else  /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
      !EVP_CIPHER_CTX_ctrl(actx, EVP_CTRL_AEAD_SET_TAG, (int)taglen,
                           (uint8_t *)tag) ||
#endif /* !(OPENSSL_VERSION_NUMBER >= 0x30000000L) */
      (cipher_nid == NID_aes_128_ccm &&
       !EVP_DecryptUpdate(actx, NULL, &len, NULL, (int)ciphertextlen)) ||
      !EVP_DecryptUpdate(actx, NULL, &len, aad, (int)aadlen) ||
      !EVP_DecryptUpdate(actx, dest, &len, ciphertext, (int)ciphertextlen) ||
      (cipher_nid != NID_aes_128_ccm &&
       !EVP_DecryptFinal_ex(actx, dest + ciphertextlen, &len))) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_hp_mask(uint8_t *dest, const ngtcp2_crypto_cipher *hp,
                          const ngtcp2_crypto_cipher_ctx *hp_ctx,
                          const uint8_t *sample) {
  static const uint8_t PLAINTEXT[] = "\x00\x00\x00\x00\x00";
  EVP_CIPHER_CTX *actx = hp_ctx->native_handle;
  int len;

  (void)hp;

  if (!EVP_EncryptInit_ex(actx, NULL, NULL, NULL, sample) ||
      !EVP_EncryptUpdate(actx, dest, &len, PLAINTEXT, sizeof(PLAINTEXT) - 1) ||
      !EVP_EncryptFinal_ex(actx, dest + sizeof(PLAINTEXT) - 1, &len)) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_read_write_crypto_data(
  ngtcp2_conn *conn, ngtcp2_encryption_level encryption_level,
  const uint8_t *data, size_t datalen) {
  SSL *ssl = ngtcp2_conn_get_tls_native_handle(conn);
  int rv;
  int err;

  if (SSL_provide_quic_data(
        ssl,
        ngtcp2_crypto_quictls_from_ngtcp2_encryption_level(encryption_level),
        data, datalen) != 1) {
    return -1;
  }

  if (!ngtcp2_conn_get_handshake_completed(conn)) {
    rv = SSL_do_handshake(ssl);
    if (rv <= 0) {
      err = SSL_get_error(ssl, rv);
      switch (err) {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_WRITE:
        return 0;
      case SSL_ERROR_WANT_CLIENT_HELLO_CB:
        return NGTCP2_CRYPTO_QUICTLS_ERR_TLS_WANT_CLIENT_HELLO_CB;
      case SSL_ERROR_WANT_X509_LOOKUP:
        return NGTCP2_CRYPTO_QUICTLS_ERR_TLS_WANT_X509_LOOKUP;
      case SSL_ERROR_SSL:
        return -1;
      default:
        return -1;
      }
    }

    ngtcp2_conn_tls_handshake_completed(conn);
  }

  rv = SSL_process_quic_post_handshake(ssl);
  if (rv != 1) {
    err = SSL_get_error(ssl, rv);
    switch (err) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      return 0;
    case SSL_ERROR_SSL:
    case SSL_ERROR_ZERO_RETURN:
      return -1;
    default:
      return -1;
    }
  }

  return 0;
}

int ngtcp2_crypto_set_remote_transport_params(ngtcp2_conn *conn, void *tls) {
  SSL *ssl = tls;
  const uint8_t *tp;
  size_t tplen;
  int rv;

  SSL_get_peer_quic_transport_params(ssl, &tp, &tplen);

  rv = ngtcp2_conn_decode_and_set_remote_transport_params(conn, tp, tplen);
  if (rv != 0) {
    ngtcp2_conn_set_tls_error(conn, rv);
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_set_local_transport_params(void *tls, const uint8_t *buf,
                                             size_t len) {
  if (SSL_set_quic_transport_params(tls, buf, len) != 1) {
    return -1;
  }

  return 0;
}

ngtcp2_encryption_level ngtcp2_crypto_quictls_from_ossl_encryption_level(
  OSSL_ENCRYPTION_LEVEL ossl_level) {
  switch (ossl_level) {
  case ssl_encryption_initial:
    return NGTCP2_ENCRYPTION_LEVEL_INITIAL;
  case ssl_encryption_early_data:
    return NGTCP2_ENCRYPTION_LEVEL_0RTT;
  case ssl_encryption_handshake:
    return NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE;
  case ssl_encryption_application:
    return NGTCP2_ENCRYPTION_LEVEL_1RTT;
  default:
    assert(0);
    abort(); /* if NDEBUG is set */
  }
}

OSSL_ENCRYPTION_LEVEL
ngtcp2_crypto_quictls_from_ngtcp2_encryption_level(
  ngtcp2_encryption_level encryption_level) {
  switch (encryption_level) {
  case NGTCP2_ENCRYPTION_LEVEL_INITIAL:
    return ssl_encryption_initial;
  case NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE:
    return ssl_encryption_handshake;
  case NGTCP2_ENCRYPTION_LEVEL_1RTT:
    return ssl_encryption_application;
  case NGTCP2_ENCRYPTION_LEVEL_0RTT:
    return ssl_encryption_early_data;
  default:
    assert(0);
    abort(); /* if NDEBUG is set */
  }
}

int ngtcp2_crypto_get_path_challenge_data_cb(ngtcp2_conn *conn, uint8_t *data,
                                             void *user_data) {
  (void)conn;
  (void)user_data;

  if (RAND_bytes(data, NGTCP2_PATH_CHALLENGE_DATALEN) != 1) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

int ngtcp2_crypto_random(uint8_t *data, size_t datalen) {
  if (RAND_bytes(data, (int)datalen) != 1) {
    return -1;
  }

  return 0;
}

static int set_encryption_secrets(SSL *ssl, OSSL_ENCRYPTION_LEVEL ossl_level,
                                  const uint8_t *rx_secret,
                                  const uint8_t *tx_secret, size_t secretlen) {
  ngtcp2_crypto_conn_ref *conn_ref = SSL_get_app_data(ssl);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  ngtcp2_encryption_level level =
    ngtcp2_crypto_quictls_from_ossl_encryption_level(ossl_level);

  if (rx_secret &&
      ngtcp2_crypto_derive_and_install_rx_key(conn, NULL, NULL, NULL, level,
                                              rx_secret, secretlen) != 0) {
    return 0;
  }

  if (tx_secret &&
      ngtcp2_crypto_derive_and_install_tx_key(conn, NULL, NULL, NULL, level,
                                              tx_secret, secretlen) != 0) {
    return 0;
  }

  return 1;
}

static int add_handshake_data(SSL *ssl, OSSL_ENCRYPTION_LEVEL ossl_level,
                              const uint8_t *data, size_t datalen) {
  ngtcp2_crypto_conn_ref *conn_ref = SSL_get_app_data(ssl);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  ngtcp2_encryption_level level =
    ngtcp2_crypto_quictls_from_ossl_encryption_level(ossl_level);
  int rv;

  rv = ngtcp2_conn_submit_crypto_data(conn, level, data, datalen);
  if (rv != 0) {
    ngtcp2_conn_set_tls_error(conn, rv);
    return 0;
  }

  return 1;
}

static int flush_flight(SSL *ssl) {
  (void)ssl;
  return 1;
}

static int send_alert(SSL *ssl, OSSL_ENCRYPTION_LEVEL level, uint8_t alert) {
  ngtcp2_crypto_conn_ref *conn_ref = SSL_get_app_data(ssl);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  (void)level;

  ngtcp2_conn_set_tls_alert(conn, alert);

  return 1;
}

static SSL_QUIC_METHOD quic_method = {
  set_encryption_secrets,
  add_handshake_data,
  flush_flight,
  send_alert,
#ifdef LIBRESSL_VERSION_NUMBER
  NULL,
  NULL,
#endif /* defined(LIBRESSL_VERSION_NUMBER) */
};

static void crypto_quictls_configure_context(SSL_CTX *ssl_ctx) {
  SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_quic_method(ssl_ctx, &quic_method);
}

int ngtcp2_crypto_quictls_configure_server_context(SSL_CTX *ssl_ctx) {
  crypto_quictls_configure_context(ssl_ctx);

  return 0;
}

int ngtcp2_crypto_quictls_configure_client_context(SSL_CTX *ssl_ctx) {
  crypto_quictls_configure_context(ssl_ctx);

  return 0;
}
