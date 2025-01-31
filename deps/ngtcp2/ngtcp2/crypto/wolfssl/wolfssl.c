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
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* defined(HAVE_CONFIG_H) */

#include <assert.h>

#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_wolfssl.h>

#include <wolfssl/ssl.h>
#include <wolfssl/quic.h>

#include "shared.h"

#define PRINTF_DEBUG 0
#if PRINTF_DEBUG
#  define DEBUG_MSG(...) fprintf(stderr, __VA_ARGS__)
#else /* !PRINTF_DEBUG */
#  define DEBUG_MSG(...) (void)0
#endif /* !PRINTF_DEBUG */

ngtcp2_crypto_aead *ngtcp2_crypto_aead_aes_128_gcm(ngtcp2_crypto_aead *aead) {
  return ngtcp2_crypto_aead_init(aead, (void *)wolfSSL_EVP_aes_128_gcm());
}

ngtcp2_crypto_md *ngtcp2_crypto_md_sha256(ngtcp2_crypto_md *md) {
  md->native_handle = (void *)wolfSSL_EVP_sha256();
  return md;
}

ngtcp2_crypto_ctx *ngtcp2_crypto_ctx_initial(ngtcp2_crypto_ctx *ctx) {
  ngtcp2_crypto_aead_init(&ctx->aead, (void *)wolfSSL_EVP_aes_128_gcm());
  ctx->md.native_handle = (void *)wolfSSL_EVP_sha256();
  ctx->hp.native_handle = (void *)wolfSSL_EVP_aes_128_ctr();
  ctx->max_encryption = 0;
  ctx->max_decryption_failure = 0;
  return ctx;
}

ngtcp2_crypto_aead *ngtcp2_crypto_aead_init(ngtcp2_crypto_aead *aead,
                                            void *aead_native_handle) {
  aead->native_handle = aead_native_handle;
  aead->max_overhead = wolfSSL_quic_get_aead_tag_len(
    (const WOLFSSL_EVP_CIPHER *)(aead_native_handle));
  return aead;
}

ngtcp2_crypto_aead *ngtcp2_crypto_aead_retry(ngtcp2_crypto_aead *aead) {
  return ngtcp2_crypto_aead_init(aead, (void *)wolfSSL_EVP_aes_128_gcm());
}

static uint64_t
crypto_aead_get_aead_max_encryption(const WOLFSSL_EVP_CIPHER *aead) {
  if (wolfSSL_quic_aead_is_gcm(aead)) {
    return NGTCP2_CRYPTO_MAX_ENCRYPTION_AES_GCM;
  }
  if (wolfSSL_quic_aead_is_chacha20(aead)) {
    return NGTCP2_CRYPTO_MAX_ENCRYPTION_CHACHA20_POLY1305;
  }
  if (wolfSSL_quic_aead_is_ccm(aead)) {
    return NGTCP2_CRYPTO_MAX_ENCRYPTION_AES_CCM;
  }
  return 0;
}

static uint64_t
crypto_aead_get_aead_max_decryption_failure(const WOLFSSL_EVP_CIPHER *aead) {
  if (wolfSSL_quic_aead_is_gcm(aead)) {
    return NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_AES_GCM;
  }
  if (wolfSSL_quic_aead_is_chacha20(aead)) {
    return NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_CHACHA20_POLY1305;
  }
  if (wolfSSL_quic_aead_is_ccm(aead)) {
    return NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_AES_CCM;
  }
  return 0;
}

static int supported_aead(const WOLFSSL_EVP_CIPHER *aead) {
  return wolfSSL_quic_aead_is_gcm(aead) ||
         wolfSSL_quic_aead_is_chacha20(aead) || wolfSSL_quic_aead_is_ccm(aead);
}

ngtcp2_crypto_ctx *ngtcp2_crypto_ctx_tls(ngtcp2_crypto_ctx *ctx,
                                         void *tls_native_handle) {
  WOLFSSL *ssl = tls_native_handle;
  const WOLFSSL_EVP_CIPHER *aead = wolfSSL_quic_get_aead(ssl);

  if (aead == NULL) {
    return NULL;
  }

  if (!supported_aead(aead)) {
    return NULL;
  }

  ngtcp2_crypto_aead_init(&ctx->aead, (void *)aead);
  ctx->md.native_handle = (void *)wolfSSL_quic_get_md(ssl);
  ctx->hp.native_handle = (void *)wolfSSL_quic_get_hp(ssl);
  ctx->max_encryption = crypto_aead_get_aead_max_encryption(aead);
  ctx->max_decryption_failure =
    crypto_aead_get_aead_max_decryption_failure(aead);
  return ctx;
}

ngtcp2_crypto_ctx *ngtcp2_crypto_ctx_tls_early(ngtcp2_crypto_ctx *ctx,
                                               void *tls_native_handle) {
  return ngtcp2_crypto_ctx_tls(ctx, tls_native_handle);
}

static size_t crypto_md_hashlen(const WOLFSSL_EVP_MD *md) {
  return (size_t)wolfSSL_EVP_MD_size(md);
}

size_t ngtcp2_crypto_md_hashlen(const ngtcp2_crypto_md *md) {
  return crypto_md_hashlen(md->native_handle);
}

static size_t crypto_aead_keylen(const WOLFSSL_EVP_CIPHER *aead) {
  return (size_t)wolfSSL_EVP_Cipher_key_length(aead);
}

size_t ngtcp2_crypto_aead_keylen(const ngtcp2_crypto_aead *aead) {
  return crypto_aead_keylen(aead->native_handle);
}

static size_t crypto_aead_noncelen(const WOLFSSL_EVP_CIPHER *aead) {
  return (size_t)wolfSSL_EVP_CIPHER_iv_length(aead);
}

size_t ngtcp2_crypto_aead_noncelen(const ngtcp2_crypto_aead *aead) {
  return crypto_aead_noncelen(aead->native_handle);
}

int ngtcp2_crypto_aead_ctx_encrypt_init(ngtcp2_crypto_aead_ctx *aead_ctx,
                                        const ngtcp2_crypto_aead *aead,
                                        const uint8_t *key, size_t noncelen) {
  const WOLFSSL_EVP_CIPHER *cipher = aead->native_handle;
  WOLFSSL_EVP_CIPHER_CTX *actx;
  static const uint8_t iv[AES_BLOCK_SIZE] = {0};

  (void)noncelen;
  actx = wolfSSL_quic_crypt_new(cipher, key, iv, /* encrypt */ 1);
  if (actx == NULL) {
    return -1;
  }

  aead_ctx->native_handle = actx;
  return 0;
}

int ngtcp2_crypto_aead_ctx_decrypt_init(ngtcp2_crypto_aead_ctx *aead_ctx,
                                        const ngtcp2_crypto_aead *aead,
                                        const uint8_t *key, size_t noncelen) {
  const WOLFSSL_EVP_CIPHER *cipher = aead->native_handle;
  WOLFSSL_EVP_CIPHER_CTX *actx;
  static const uint8_t iv[AES_BLOCK_SIZE] = {0};

  (void)noncelen;
  actx = wolfSSL_quic_crypt_new(cipher, key, iv, /* encrypt */ 0);
  if (actx == NULL) {
    return -1;
  }

  aead_ctx->native_handle = actx;
  return 0;
}

void ngtcp2_crypto_aead_ctx_free(ngtcp2_crypto_aead_ctx *aead_ctx) {
  if (aead_ctx->native_handle) {
    wolfSSL_EVP_CIPHER_CTX_free(aead_ctx->native_handle);
  }
}

int ngtcp2_crypto_cipher_ctx_encrypt_init(ngtcp2_crypto_cipher_ctx *cipher_ctx,
                                          const ngtcp2_crypto_cipher *cipher,
                                          const uint8_t *key) {
  WOLFSSL_EVP_CIPHER_CTX *actx;

  actx =
    wolfSSL_quic_crypt_new(cipher->native_handle, key, NULL, /* encrypt */ 1);
  if (actx == NULL) {
    return -1;
  }

  cipher_ctx->native_handle = actx;
  return 0;
}

void ngtcp2_crypto_cipher_ctx_free(ngtcp2_crypto_cipher_ctx *cipher_ctx) {
  if (cipher_ctx->native_handle) {
    wolfSSL_EVP_CIPHER_CTX_free(cipher_ctx->native_handle);
  }
}

int ngtcp2_crypto_hkdf_extract(uint8_t *dest, const ngtcp2_crypto_md *md,
                               const uint8_t *secret, size_t secretlen,
                               const uint8_t *salt, size_t saltlen) {
  if (wolfSSL_quic_hkdf_extract(dest, md->native_handle, secret, secretlen,
                                salt, saltlen) != WOLFSSL_SUCCESS) {
    DEBUG_MSG("WOLFSSL: wolfSSL_quic_hkdf_extract FAILED\n");
    return -1;
  }
  return 0;
}

int ngtcp2_crypto_hkdf_expand(uint8_t *dest, size_t destlen,
                              const ngtcp2_crypto_md *md, const uint8_t *secret,
                              size_t secretlen, const uint8_t *info,
                              size_t infolen) {
  if (wolfSSL_quic_hkdf_expand(dest, destlen, md->native_handle, secret,
                               secretlen, info, infolen) != WOLFSSL_SUCCESS) {
    DEBUG_MSG("WOLFSSL: wolfSSL_quic_hkdf_expand FAILED\n");
    return -1;
  }
  return 0;
}

int ngtcp2_crypto_hkdf(uint8_t *dest, size_t destlen,
                       const ngtcp2_crypto_md *md, const uint8_t *secret,
                       size_t secretlen, const uint8_t *salt, size_t saltlen,
                       const uint8_t *info, size_t infolen) {
  if (wolfSSL_quic_hkdf(dest, destlen, md->native_handle, secret, secretlen,
                        salt, saltlen, info, infolen) != WOLFSSL_SUCCESS) {
    DEBUG_MSG("WOLFSSL: wolfSSL_quic_hkdf FAILED\n");
    return -1;
  }
  return 0;
}

int ngtcp2_crypto_encrypt(uint8_t *dest, const ngtcp2_crypto_aead *aead,
                          const ngtcp2_crypto_aead_ctx *aead_ctx,
                          const uint8_t *plaintext, size_t plaintextlen,
                          const uint8_t *nonce, size_t noncelen,
                          const uint8_t *aad, size_t aadlen) {
  (void)aead;
  (void)noncelen;
  if (wolfSSL_quic_aead_encrypt(dest, aead_ctx->native_handle, plaintext,
                                plaintextlen, nonce, aad,
                                aadlen) != WOLFSSL_SUCCESS) {
    DEBUG_MSG("WOLFSSL: encrypt FAILED\n");
    return -1;
  }
  return 0;
}

int ngtcp2_crypto_decrypt(uint8_t *dest, const ngtcp2_crypto_aead *aead,
                          const ngtcp2_crypto_aead_ctx *aead_ctx,
                          const uint8_t *ciphertext, size_t ciphertextlen,
                          const uint8_t *nonce, size_t noncelen,
                          const uint8_t *aad, size_t aadlen) {
  (void)aead;
  (void)noncelen;
  if (wolfSSL_quic_aead_decrypt(dest, aead_ctx->native_handle, ciphertext,
                                ciphertextlen, nonce, aad,
                                aadlen) != WOLFSSL_SUCCESS) {
    DEBUG_MSG("WOLFSSL: decrypt FAILED\n");
    return -1;
  }
  return 0;
}

int ngtcp2_crypto_hp_mask(uint8_t *dest, const ngtcp2_crypto_cipher *hp,
                          const ngtcp2_crypto_cipher_ctx *hp_ctx,
                          const uint8_t *sample) {
  static const uint8_t PLAINTEXT[] = "\x00\x00\x00\x00\x00";
  WOLFSSL_EVP_CIPHER_CTX *actx = hp_ctx->native_handle;
  int len;

  (void)hp;

  if (wolfSSL_EVP_EncryptInit_ex(actx, NULL, NULL, NULL, sample) !=
        WOLFSSL_SUCCESS ||
      wolfSSL_EVP_CipherUpdate(actx, dest, &len, PLAINTEXT,
                               sizeof(PLAINTEXT) - 1) != WOLFSSL_SUCCESS ||
      wolfSSL_EVP_EncryptFinal_ex(actx, dest + sizeof(PLAINTEXT) - 1, &len) !=
        WOLFSSL_SUCCESS) {
    DEBUG_MSG("WOLFSSL: hp_mask FAILED\n");
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_read_write_crypto_data(
  ngtcp2_conn *conn, ngtcp2_encryption_level encryption_level,
  const uint8_t *data, size_t datalen) {
  WOLFSSL *ssl = ngtcp2_conn_get_tls_native_handle(conn);
  WOLFSSL_ENCRYPTION_LEVEL level =
    ngtcp2_crypto_wolfssl_from_ngtcp2_encryption_level(encryption_level);
  int rv;
  int err;

  DEBUG_MSG("WOLFSSL: read/write crypto data, level=%d len=%lu\n", level,
            datalen);
  if (datalen > 0) {
    rv = wolfSSL_provide_quic_data(ssl, level, data, datalen);
    if (rv != WOLFSSL_SUCCESS) {
      DEBUG_MSG("WOLFSSL: read/write crypto data FAILED, rv=%d\n", rv);
      return -1;
    }
  }

  if (!ngtcp2_conn_get_handshake_completed(conn)) {
    rv = wolfSSL_quic_do_handshake(ssl);
    if (rv <= 0) {
      err = wolfSSL_get_error(ssl, rv);
      DEBUG_MSG("WOLFSSL: do_handshake, rv=%d, err=%d\n", rv, err);
      switch (err) {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_WRITE:
        return 0;
      case SSL_ERROR_SSL:
        return -1;
      default:
        return -1;
      }
    }

    DEBUG_MSG("WOLFSSL: handshake done\n");
    ngtcp2_conn_tls_handshake_completed(conn);
  }

  rv = wolfSSL_process_quic_post_handshake(ssl);
  DEBUG_MSG("WOLFSSL: process post handshake, rv=%d\n", rv);
  if (rv != 1) {
    err = wolfSSL_get_error(ssl, rv);
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
  WOLFSSL *ssl = tls;
  const uint8_t *tp;
  size_t tplen;
  int rv;

  wolfSSL_get_peer_quic_transport_params(ssl, &tp, &tplen);
  DEBUG_MSG("WOLFSSL: get peer transport params, len=%lu\n", tplen);

  rv = ngtcp2_conn_decode_and_set_remote_transport_params(conn, tp, tplen);
  if (rv != 0) {
    DEBUG_MSG("WOLFSSL: decode peer transport params failed, rv=%d\n", rv);
    ngtcp2_conn_set_tls_error(conn, rv);
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_set_local_transport_params(void *tls, const uint8_t *buf,
                                             size_t len) {
  WOLFSSL *ssl = tls;
  DEBUG_MSG("WOLFSSL: set local peer transport params, len=%lu\n", len);
  if (wolfSSL_set_quic_transport_params(ssl, buf, len) != WOLFSSL_SUCCESS) {
    return -1;
  }

  return 0;
}

ngtcp2_encryption_level ngtcp2_crypto_wolfssl_from_wolfssl_encryption_level(
  WOLFSSL_ENCRYPTION_LEVEL wolfssl_level) {
  switch (wolfssl_level) {
  case wolfssl_encryption_initial:
    return NGTCP2_ENCRYPTION_LEVEL_INITIAL;
  case wolfssl_encryption_early_data:
    return NGTCP2_ENCRYPTION_LEVEL_0RTT;
  case wolfssl_encryption_handshake:
    return NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE;
  case wolfssl_encryption_application:
    return NGTCP2_ENCRYPTION_LEVEL_1RTT;
  default:
    assert(0);
    abort(); /* if NDEBUG is set */
  }
}

WOLFSSL_ENCRYPTION_LEVEL
ngtcp2_crypto_wolfssl_from_ngtcp2_encryption_level(
  ngtcp2_encryption_level encryption_level) {
  switch (encryption_level) {
  case NGTCP2_ENCRYPTION_LEVEL_INITIAL:
    return wolfssl_encryption_initial;
  case NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE:
    return wolfssl_encryption_handshake;
  case NGTCP2_ENCRYPTION_LEVEL_1RTT:
    return wolfssl_encryption_application;
  case NGTCP2_ENCRYPTION_LEVEL_0RTT:
    return wolfssl_encryption_early_data;
  default:
    assert(0);
    abort(); /* if NDEBUG is set */
  }
}

int ngtcp2_crypto_get_path_challenge_data_cb(ngtcp2_conn *conn, uint8_t *data,
                                             void *user_data) {
  (void)conn;
  (void)user_data;

  DEBUG_MSG("WOLFSSL: get path challenge data\n");
  if (wolfSSL_RAND_bytes(data, NGTCP2_PATH_CHALLENGE_DATALEN) != 1) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  return 0;
}

int ngtcp2_crypto_random(uint8_t *data, size_t datalen) {
  DEBUG_MSG("WOLFSSL: get random\n");
  if (wolfSSL_RAND_bytes(data, (int)datalen) != 1) {
    return -1;
  }
  return 0;
}

static int set_encryption_secrets(WOLFSSL *ssl,
                                  WOLFSSL_ENCRYPTION_LEVEL wolfssl_level,
                                  const uint8_t *rx_secret,
                                  const uint8_t *tx_secret, size_t secretlen) {
  ngtcp2_crypto_conn_ref *conn_ref = SSL_get_app_data(ssl);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  ngtcp2_encryption_level level =
    ngtcp2_crypto_wolfssl_from_wolfssl_encryption_level(wolfssl_level);

  DEBUG_MSG("WOLFSSL: set encryption secrets, level=%d, rxlen=%lu, txlen=%lu\n",
            wolfssl_level, rx_secret ? secretlen : 0,
            tx_secret ? secretlen : 0);
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

static int add_handshake_data(WOLFSSL *ssl,
                              WOLFSSL_ENCRYPTION_LEVEL wolfssl_level,
                              const uint8_t *data, size_t datalen) {
  ngtcp2_crypto_conn_ref *conn_ref = SSL_get_app_data(ssl);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  ngtcp2_encryption_level level =
    ngtcp2_crypto_wolfssl_from_wolfssl_encryption_level(wolfssl_level);
  int rv;

  DEBUG_MSG("WOLFSSL: add handshake data, level=%d len=%lu\n", wolfssl_level,
            datalen);
  rv = ngtcp2_conn_submit_crypto_data(conn, level, data, datalen);
  if (rv != 0) {
    ngtcp2_conn_set_tls_error(conn, rv);
    return 0;
  }

  return 1;
}

static int flush_flight(WOLFSSL *ssl) {
  (void)ssl;
  return 1;
}

static int send_alert(WOLFSSL *ssl, enum wolfssl_encryption_level_t level,
                      uint8_t alert) {
  ngtcp2_crypto_conn_ref *conn_ref = SSL_get_app_data(ssl);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  (void)level;

  DEBUG_MSG("WOLFSSL: send alert, level=%d alert=%d\n", level, alert);
  ngtcp2_conn_set_tls_alert(conn, alert);

  return 1;
}

static WOLFSSL_QUIC_METHOD quic_method = {
  set_encryption_secrets,
  add_handshake_data,
  flush_flight,
  send_alert,
};

static void crypto_wolfssl_configure_context(WOLFSSL_CTX *ssl_ctx) {
  wolfSSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION);
  wolfSSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);
  wolfSSL_CTX_set_quic_method(ssl_ctx, &quic_method);
}

int ngtcp2_crypto_wolfssl_configure_server_context(WOLFSSL_CTX *ssl_ctx) {
  crypto_wolfssl_configure_context(ssl_ctx);
#if PRINTF_DEBUG
  wolfSSL_Debugging_ON();
#endif /* PRINTF_DEBUG */
  return 0;
}

int ngtcp2_crypto_wolfssl_configure_client_context(WOLFSSL_CTX *ssl_ctx) {
  crypto_wolfssl_configure_context(ssl_ctx);
  wolfSSL_CTX_UseSessionTicket(ssl_ctx);
#if PRINTF_DEBUG
  wolfSSL_Debugging_ON();
#endif /* PRINTF_DEBUG */
  return 0;
}
