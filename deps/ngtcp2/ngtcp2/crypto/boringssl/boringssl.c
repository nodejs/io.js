/*
 * ngtcp2
 *
 * Copyright (c) 2020 ngtcp2 contributors
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
#include <string.h>

#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_boringssl.h>

#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/hkdf.h>
#include <openssl/aes.h>
#include <openssl/chacha.h>
#include <openssl/rand.h>

#include "shared.h"

typedef enum ngtcp2_crypto_boringssl_cipher_type {
  NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_AES_128,
  NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_AES_256,
  NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_CHACHA20,
} ngtcp2_crypto_boringssl_cipher_type;

typedef struct ngtcp2_crypto_boringssl_cipher {
  ngtcp2_crypto_boringssl_cipher_type type;
} ngtcp2_crypto_boringssl_cipher;

static ngtcp2_crypto_boringssl_cipher crypto_cipher_aes_128 = {
  NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_AES_128,
};

static ngtcp2_crypto_boringssl_cipher crypto_cipher_aes_256 = {
  NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_AES_256,
};

static ngtcp2_crypto_boringssl_cipher crypto_cipher_chacha20 = {
  NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_CHACHA20,
};

ngtcp2_crypto_aead *ngtcp2_crypto_aead_aes_128_gcm(ngtcp2_crypto_aead *aead) {
  return ngtcp2_crypto_aead_init(aead, (void *)EVP_aead_aes_128_gcm());
}

ngtcp2_crypto_md *ngtcp2_crypto_md_sha256(ngtcp2_crypto_md *md) {
  md->native_handle = (void *)EVP_sha256();
  return md;
}

ngtcp2_crypto_ctx *ngtcp2_crypto_ctx_initial(ngtcp2_crypto_ctx *ctx) {
  ngtcp2_crypto_aead_init(&ctx->aead, (void *)EVP_aead_aes_128_gcm());
  ctx->md.native_handle = (void *)EVP_sha256();
  ctx->hp.native_handle = (void *)&crypto_cipher_aes_128;
  ctx->max_encryption = 0;
  ctx->max_decryption_failure = 0;
  return ctx;
}

ngtcp2_crypto_aead *ngtcp2_crypto_aead_init(ngtcp2_crypto_aead *aead,
                                            void *aead_native_handle) {
  aead->native_handle = aead_native_handle;
  aead->max_overhead = EVP_AEAD_max_overhead(aead->native_handle);
  return aead;
}

ngtcp2_crypto_aead *ngtcp2_crypto_aead_retry(ngtcp2_crypto_aead *aead) {
  return ngtcp2_crypto_aead_init(aead, (void *)EVP_aead_aes_128_gcm());
}

static const EVP_AEAD *crypto_cipher_id_get_aead(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_CK_AES_128_GCM_SHA256:
    return EVP_aead_aes_128_gcm();
  case TLS1_CK_AES_256_GCM_SHA384:
    return EVP_aead_aes_256_gcm();
  case TLS1_CK_CHACHA20_POLY1305_SHA256:
    return EVP_aead_chacha20_poly1305();
  default:
    return NULL;
  }
}

static uint64_t crypto_cipher_id_get_aead_max_encryption(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_CK_AES_128_GCM_SHA256:
  case TLS1_CK_AES_256_GCM_SHA384:
    return NGTCP2_CRYPTO_MAX_ENCRYPTION_AES_GCM;
  case TLS1_CK_CHACHA20_POLY1305_SHA256:
    return NGTCP2_CRYPTO_MAX_ENCRYPTION_CHACHA20_POLY1305;
  default:
    return 0;
  }
}

static uint64_t
crypto_cipher_id_get_aead_max_decryption_failure(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_CK_AES_128_GCM_SHA256:
  case TLS1_CK_AES_256_GCM_SHA384:
    return NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_AES_GCM;
  case TLS1_CK_CHACHA20_POLY1305_SHA256:
    return NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_CHACHA20_POLY1305;
  default:
    return 0;
  }
}

static const ngtcp2_crypto_boringssl_cipher *
crypto_cipher_id_get_hp(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_CK_AES_128_GCM_SHA256:
    return &crypto_cipher_aes_128;
  case TLS1_CK_AES_256_GCM_SHA384:
    return &crypto_cipher_aes_256;
  case TLS1_CK_CHACHA20_POLY1305_SHA256:
    return &crypto_cipher_chacha20;
  default:
    return NULL;
  }
}

static const EVP_MD *crypto_cipher_id_get_md(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_CK_AES_128_GCM_SHA256:
  case TLS1_CK_CHACHA20_POLY1305_SHA256:
    return EVP_sha256();
  case TLS1_CK_AES_256_GCM_SHA384:
    return EVP_sha384();
  default:
    return NULL;
  }
}

static int supported_cipher_id(uint32_t cipher_id) {
  switch (cipher_id) {
  case TLS1_CK_AES_128_GCM_SHA256:
  case TLS1_CK_AES_256_GCM_SHA384:
  case TLS1_CK_CHACHA20_POLY1305_SHA256:
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

  cipher_id = SSL_CIPHER_get_id(cipher);

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

static size_t crypto_aead_keylen(const EVP_AEAD *aead) {
  return (size_t)EVP_AEAD_key_length(aead);
}

size_t ngtcp2_crypto_aead_keylen(const ngtcp2_crypto_aead *aead) {
  return crypto_aead_keylen(aead->native_handle);
}

static size_t crypto_aead_noncelen(const EVP_AEAD *aead) {
  return (size_t)EVP_AEAD_nonce_length(aead);
}

size_t ngtcp2_crypto_aead_noncelen(const ngtcp2_crypto_aead *aead) {
  return crypto_aead_noncelen(aead->native_handle);
}

int ngtcp2_crypto_aead_ctx_encrypt_init(ngtcp2_crypto_aead_ctx *aead_ctx,
                                        const ngtcp2_crypto_aead *aead,
                                        const uint8_t *key, size_t noncelen) {
  const EVP_AEAD *cipher = aead->native_handle;
  size_t keylen = crypto_aead_keylen(cipher);
  EVP_AEAD_CTX *actx;

  (void)noncelen;

  actx = EVP_AEAD_CTX_new(cipher, key, keylen, EVP_AEAD_DEFAULT_TAG_LENGTH);
  if (actx == NULL) {
    return -1;
  }

  aead_ctx->native_handle = actx;

  return 0;
}

int ngtcp2_crypto_aead_ctx_decrypt_init(ngtcp2_crypto_aead_ctx *aead_ctx,
                                        const ngtcp2_crypto_aead *aead,
                                        const uint8_t *key, size_t noncelen) {
  return ngtcp2_crypto_aead_ctx_encrypt_init(aead_ctx, aead, key, noncelen);
}

void ngtcp2_crypto_aead_ctx_free(ngtcp2_crypto_aead_ctx *aead_ctx) {
  if (aead_ctx->native_handle) {
    EVP_AEAD_CTX_free(aead_ctx->native_handle);
  }
}

typedef struct ngtcp2_crypto_boringssl_cipher_ctx {
  ngtcp2_crypto_boringssl_cipher_type type;
  union {
    /* aes_key is an encryption key when type is either
       NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_AES_128 or
       NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_AES_256. */
    AES_KEY aes_key;
    /* key contains an encryption key when type ==
       NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_CHACHA20. */
    uint8_t key[32];
  };
} ngtcp2_crypto_boringssl_cipher_ctx;

int ngtcp2_crypto_cipher_ctx_encrypt_init(ngtcp2_crypto_cipher_ctx *cipher_ctx,
                                          const ngtcp2_crypto_cipher *cipher,
                                          const uint8_t *key) {
  ngtcp2_crypto_boringssl_cipher *hp_cipher = cipher->native_handle;
  ngtcp2_crypto_boringssl_cipher_ctx *ctx;
  int rv;
  (void)rv;

  ctx = malloc(sizeof(*ctx));
  if (ctx == NULL) {
    return -1;
  }

  ctx->type = hp_cipher->type;
  cipher_ctx->native_handle = ctx;

  switch (hp_cipher->type) {
  case NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_AES_128:
    rv = AES_set_encrypt_key(key, 128, &ctx->aes_key);
    assert(0 == rv);
    return 0;
  case NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_AES_256:
    rv = AES_set_encrypt_key(key, 256, &ctx->aes_key);
    assert(0 == rv);
    return 0;
  case NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_CHACHA20:
    memcpy(ctx->key, key, sizeof(ctx->key));
    return 0;
  default:
    assert(0);
    abort();
  };
}

void ngtcp2_crypto_cipher_ctx_free(ngtcp2_crypto_cipher_ctx *cipher_ctx) {
  if (!cipher_ctx->native_handle) {
    return;
  }

  free(cipher_ctx->native_handle);
}

int ngtcp2_crypto_hkdf_extract(uint8_t *dest, const ngtcp2_crypto_md *md,
                               const uint8_t *secret, size_t secretlen,
                               const uint8_t *salt, size_t saltlen) {
  const EVP_MD *prf = md->native_handle;
  size_t destlen = (size_t)EVP_MD_size(prf);

  if (HKDF_extract(dest, &destlen, prf, secret, secretlen, salt, saltlen) !=
      1) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_hkdf_expand(uint8_t *dest, size_t destlen,
                              const ngtcp2_crypto_md *md, const uint8_t *secret,
                              size_t secretlen, const uint8_t *info,
                              size_t infolen) {
  const EVP_MD *prf = md->native_handle;

  if (HKDF_expand(dest, destlen, prf, secret, secretlen, info, infolen) != 1) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_hkdf(uint8_t *dest, size_t destlen,
                       const ngtcp2_crypto_md *md, const uint8_t *secret,
                       size_t secretlen, const uint8_t *salt, size_t saltlen,
                       const uint8_t *info, size_t infolen) {
  const EVP_MD *prf = md->native_handle;

  if (HKDF(dest, destlen, prf, secret, secretlen, salt, saltlen, info,
           infolen) != 1) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_encrypt(uint8_t *dest, const ngtcp2_crypto_aead *aead,
                          const ngtcp2_crypto_aead_ctx *aead_ctx,
                          const uint8_t *plaintext, size_t plaintextlen,
                          const uint8_t *nonce, size_t noncelen,
                          const uint8_t *aad, size_t aadlen) {
  const EVP_AEAD *cipher = aead->native_handle;
  EVP_AEAD_CTX *actx = aead_ctx->native_handle;
  size_t max_outlen = plaintextlen + EVP_AEAD_max_overhead(cipher);
  size_t outlen;

  if (EVP_AEAD_CTX_seal(actx, dest, &outlen, max_outlen, nonce, noncelen,
                        plaintext, plaintextlen, aad, aadlen) != 1) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_decrypt(uint8_t *dest, const ngtcp2_crypto_aead *aead,
                          const ngtcp2_crypto_aead_ctx *aead_ctx,
                          const uint8_t *ciphertext, size_t ciphertextlen,
                          const uint8_t *nonce, size_t noncelen,
                          const uint8_t *aad, size_t aadlen) {
  const EVP_AEAD *cipher = aead->native_handle;
  EVP_AEAD_CTX *actx = aead_ctx->native_handle;
  size_t max_overhead = EVP_AEAD_max_overhead(cipher);
  size_t max_outlen;
  size_t outlen;

  if (ciphertextlen < max_overhead) {
    return -1;
  }

  max_outlen = ciphertextlen - max_overhead;

  if (EVP_AEAD_CTX_open(actx, dest, &outlen, max_outlen, nonce, noncelen,
                        ciphertext, ciphertextlen, aad, aadlen) != 1) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_hp_mask(uint8_t *dest, const ngtcp2_crypto_cipher *hp,
                          const ngtcp2_crypto_cipher_ctx *hp_ctx,
                          const uint8_t *sample) {
  static const uint8_t PLAINTEXT[] = "\x00\x00\x00\x00\x00";
  ngtcp2_crypto_boringssl_cipher_ctx *ctx = hp_ctx->native_handle;
  uint32_t counter;

  (void)hp;

  switch (ctx->type) {
  case NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_AES_128:
  case NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_AES_256:
    AES_ecb_encrypt(sample, dest, &ctx->aes_key, 1);
    return 0;
  case NGTCP2_CRYPTO_BORINGSSL_CIPHER_TYPE_CHACHA20:
#ifdef WORDS_BIGENDIAN
    counter = (uint32_t)sample[0] + (uint32_t)(sample[1] << 8) +
              (uint32_t)(sample[2] << 16) + (uint32_t)(sample[3] << 24);
#else  /* !defined(WORDS_BIGENDIAN) */
    memcpy(&counter, sample, sizeof(counter));
#endif /* !defined(WORDS_BIGENDIAN) */
    CRYPTO_chacha_20(dest, PLAINTEXT, sizeof(PLAINTEXT) - 1, ctx->key,
                     sample + sizeof(counter), counter);
    return 0;
  default:
    assert(0);
    abort();
  }
}

int ngtcp2_crypto_read_write_crypto_data(
  ngtcp2_conn *conn, ngtcp2_encryption_level encryption_level,
  const uint8_t *data, size_t datalen) {
  SSL *ssl = ngtcp2_conn_get_tls_native_handle(conn);
  int rv;
  int err;

  if (SSL_provide_quic_data(
        ssl,
        ngtcp2_crypto_boringssl_from_ngtcp2_encryption_level(encryption_level),
        data, datalen) != 1) {
    return -1;
  }

  if (!ngtcp2_conn_get_handshake_completed(conn)) {
  retry:
    rv = SSL_do_handshake(ssl);
    if (rv <= 0) {
      err = SSL_get_error(ssl, rv);
      switch (err) {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_WRITE:
        return 0;
      case SSL_ERROR_SSL:
        return -1;
      case SSL_ERROR_EARLY_DATA_REJECTED:
        assert(!ngtcp2_conn_is_server(conn));

        SSL_reset_early_data_reject(ssl);

        rv = ngtcp2_conn_tls_early_data_rejected(conn);
        if (rv != 0) {
          return -1;
        }

        goto retry;
      default:
        return -1;
      }
    }

    if (SSL_in_early_data(ssl)) {
      return 0;
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

ngtcp2_encryption_level ngtcp2_crypto_boringssl_from_ssl_encryption_level(
  enum ssl_encryption_level_t ssl_level) {
  switch (ssl_level) {
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
    abort();
  }
}

enum ssl_encryption_level_t
ngtcp2_crypto_boringssl_from_ngtcp2_encryption_level(
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
    abort();
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
  if (RAND_bytes(data, datalen) != 1) {
    return -1;
  }

  return 0;
}

static int set_read_secret(SSL *ssl, enum ssl_encryption_level_t bssl_level,
                           const SSL_CIPHER *cipher, const uint8_t *secret,
                           size_t secretlen) {
  ngtcp2_crypto_conn_ref *conn_ref = SSL_get_app_data(ssl);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  ngtcp2_encryption_level level =
    ngtcp2_crypto_boringssl_from_ssl_encryption_level(bssl_level);
  (void)cipher;

  if (ngtcp2_crypto_derive_and_install_rx_key(conn, NULL, NULL, NULL, level,
                                              secret, secretlen) != 0) {
    return 0;
  }

  return 1;
}

static int set_write_secret(SSL *ssl, enum ssl_encryption_level_t bssl_level,
                            const SSL_CIPHER *cipher, const uint8_t *secret,
                            size_t secretlen) {
  ngtcp2_crypto_conn_ref *conn_ref = SSL_get_app_data(ssl);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  ngtcp2_encryption_level level =
    ngtcp2_crypto_boringssl_from_ssl_encryption_level(bssl_level);
  (void)cipher;

  if (ngtcp2_crypto_derive_and_install_tx_key(conn, NULL, NULL, NULL, level,
                                              secret, secretlen) != 0) {
    return 0;
  }

  return 1;
}

static int add_handshake_data(SSL *ssl, enum ssl_encryption_level_t bssl_level,
                              const uint8_t *data, size_t datalen) {
  ngtcp2_crypto_conn_ref *conn_ref = SSL_get_app_data(ssl);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  ngtcp2_encryption_level level =
    ngtcp2_crypto_boringssl_from_ssl_encryption_level(bssl_level);
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

static int send_alert(SSL *ssl, enum ssl_encryption_level_t bssl_level,
                      uint8_t alert) {
  ngtcp2_crypto_conn_ref *conn_ref = SSL_get_app_data(ssl);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  (void)bssl_level;

  ngtcp2_conn_set_tls_alert(conn, alert);

  return 1;
}

static SSL_QUIC_METHOD quic_method = {
  set_read_secret, set_write_secret, add_handshake_data,
  flush_flight,    send_alert,
};

static void crypto_boringssl_configure_context(SSL_CTX *ssl_ctx) {
  SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_quic_method(ssl_ctx, &quic_method);
}

int ngtcp2_crypto_boringssl_configure_server_context(SSL_CTX *ssl_ctx) {
  crypto_boringssl_configure_context(ssl_ctx);

  return 0;
}

int ngtcp2_crypto_boringssl_configure_client_context(SSL_CTX *ssl_ctx) {
  crypto_boringssl_configure_context(ssl_ctx);

  return 0;
}
