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
#include <string.h>

#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_picotls.h>

#include <picotls.h>
#include <picotls/openssl.h>

#include "shared.h"

ngtcp2_crypto_aead *ngtcp2_crypto_aead_aes_128_gcm(ngtcp2_crypto_aead *aead) {
  return ngtcp2_crypto_aead_init(aead, (void *)&ptls_openssl_aes128gcm);
}

ngtcp2_crypto_md *ngtcp2_crypto_md_sha256(ngtcp2_crypto_md *md) {
  md->native_handle = (void *)&ptls_openssl_sha256;
  return md;
}

ngtcp2_crypto_ctx *ngtcp2_crypto_ctx_initial(ngtcp2_crypto_ctx *ctx) {
  ngtcp2_crypto_aead_init(&ctx->aead, (void *)&ptls_openssl_aes128gcm);
  ctx->md.native_handle = (void *)&ptls_openssl_sha256;
  ctx->hp.native_handle = (void *)&ptls_openssl_aes128ctr;
  ctx->max_encryption = 0;
  ctx->max_decryption_failure = 0;
  return ctx;
}

ngtcp2_crypto_aead *ngtcp2_crypto_aead_init(ngtcp2_crypto_aead *aead,
                                            void *aead_native_handle) {
  ptls_aead_algorithm_t *alg = aead_native_handle;

  aead->native_handle = aead_native_handle;
  aead->max_overhead = alg->tag_size;
  return aead;
}

ngtcp2_crypto_aead *ngtcp2_crypto_aead_retry(ngtcp2_crypto_aead *aead) {
  return ngtcp2_crypto_aead_init(aead, (void *)&ptls_openssl_aes128gcm);
}

static uint64_t
crypto_cipher_suite_get_aead_max_encryption(ptls_cipher_suite_t *cs) {
  if (cs->aead == &ptls_openssl_aes128gcm ||
      cs->aead == &ptls_openssl_aes256gcm) {
    return NGTCP2_CRYPTO_MAX_ENCRYPTION_AES_GCM;
  }

#ifdef PTLS_OPENSSL_HAVE_CHACHA20_POLY1305
  if (cs->aead == &ptls_openssl_chacha20poly1305) {
    return NGTCP2_CRYPTO_MAX_ENCRYPTION_CHACHA20_POLY1305;
  }
#endif /* defined(PTLS_OPENSSL_HAVE_CHACHA20_POLY1305) */

  return 0;
}

static uint64_t
crypto_cipher_suite_get_aead_max_decryption_failure(ptls_cipher_suite_t *cs) {
  if (cs->aead == &ptls_openssl_aes128gcm ||
      cs->aead == &ptls_openssl_aes256gcm) {
    return NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_AES_GCM;
  }

#ifdef PTLS_OPENSSL_HAVE_CHACHA20_POLY1305
  if (cs->aead == &ptls_openssl_chacha20poly1305) {
    return NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_CHACHA20_POLY1305;
  }
#endif /* defined(PTLS_OPENSSL_HAVE_CHACHA20_POLY1305) */

  return 0;
}

static const ptls_cipher_algorithm_t *
crypto_cipher_suite_get_hp(ptls_cipher_suite_t *cs) {
  if (cs->aead == &ptls_openssl_aes128gcm) {
    return &ptls_openssl_aes128ctr;
  }

  if (cs->aead == &ptls_openssl_aes256gcm) {
    return &ptls_openssl_aes256ctr;
  }

#ifdef PTLS_OPENSSL_HAVE_CHACHA20_POLY1305
  if (cs->aead == &ptls_openssl_chacha20poly1305) {
    return &ptls_openssl_chacha20;
  }
#endif /* defined(PTLS_OPENSSL_HAVE_CHACHA20_POLY1305) */

  return NULL;
}

static int supported_cipher_suite(ptls_cipher_suite_t *cs) {
  return cs->aead == &ptls_openssl_aes128gcm ||
         cs->aead == &ptls_openssl_aes256gcm
#ifdef PTLS_OPENSSL_HAVE_CHACHA20_POLY1305
         || cs->aead == &ptls_openssl_chacha20poly1305
#endif /* defined(PTLS_OPENSSL_HAVE_CHACHA20_POLY1305) */
      ;
}

ngtcp2_crypto_ctx *ngtcp2_crypto_ctx_tls(ngtcp2_crypto_ctx *ctx,
                                         void *tls_native_handle) {
  ngtcp2_crypto_picotls_ctx *cptls = tls_native_handle;
  ptls_cipher_suite_t *cs = ptls_get_cipher(cptls->ptls);

  if (cs == NULL) {
    return NULL;
  }

  if (!supported_cipher_suite(cs)) {
    return NULL;
  }

  ngtcp2_crypto_aead_init(&ctx->aead, (void *)cs->aead);
  ctx->md.native_handle = (void *)cs->hash;
  ctx->hp.native_handle = (void *)crypto_cipher_suite_get_hp(cs);
  ctx->max_encryption = crypto_cipher_suite_get_aead_max_encryption(cs);
  ctx->max_decryption_failure =
      crypto_cipher_suite_get_aead_max_decryption_failure(cs);
  return ctx;
}

ngtcp2_crypto_ctx *ngtcp2_crypto_ctx_tls_early(ngtcp2_crypto_ctx *ctx,
                                               void *tls_native_handle) {
  return ngtcp2_crypto_ctx_tls(ctx, tls_native_handle);
}

static size_t crypto_md_hashlen(const ptls_hash_algorithm_t *md) {
  return md->digest_size;
}

size_t ngtcp2_crypto_md_hashlen(const ngtcp2_crypto_md *md) {
  return crypto_md_hashlen(md->native_handle);
}

static size_t crypto_aead_keylen(const ptls_aead_algorithm_t *aead) {
  return aead->key_size;
}

size_t ngtcp2_crypto_aead_keylen(const ngtcp2_crypto_aead *aead) {
  return crypto_aead_keylen(aead->native_handle);
}

static size_t crypto_aead_noncelen(const ptls_aead_algorithm_t *aead) {
  return aead->iv_size;
}

size_t ngtcp2_crypto_aead_noncelen(const ngtcp2_crypto_aead *aead) {
  return crypto_aead_noncelen(aead->native_handle);
}

int ngtcp2_crypto_aead_ctx_encrypt_init(ngtcp2_crypto_aead_ctx *aead_ctx,
                                        const ngtcp2_crypto_aead *aead,
                                        const uint8_t *key, size_t noncelen) {
  const ptls_aead_algorithm_t *cipher = aead->native_handle;
  size_t keylen = crypto_aead_keylen(cipher);
  ptls_aead_context_t *actx;
  static const uint8_t iv[PTLS_MAX_IV_SIZE] = {0};

  (void)noncelen;
  (void)keylen;

  actx = ptls_aead_new_direct(cipher, /* is_enc = */ 1, key, iv);
  if (actx == NULL) {
    return -1;
  }

  aead_ctx->native_handle = actx;

  return 0;
}

int ngtcp2_crypto_aead_ctx_decrypt_init(ngtcp2_crypto_aead_ctx *aead_ctx,
                                        const ngtcp2_crypto_aead *aead,
                                        const uint8_t *key, size_t noncelen) {
  const ptls_aead_algorithm_t *cipher = aead->native_handle;
  size_t keylen = crypto_aead_keylen(cipher);
  ptls_aead_context_t *actx;
  const uint8_t iv[PTLS_MAX_IV_SIZE] = {0};

  (void)noncelen;
  (void)keylen;

  actx = ptls_aead_new_direct(cipher, /* is_enc = */ 0, key, iv);
  if (actx == NULL) {
    return -1;
  }

  aead_ctx->native_handle = actx;

  return 0;
}

void ngtcp2_crypto_aead_ctx_free(ngtcp2_crypto_aead_ctx *aead_ctx) {
  if (aead_ctx->native_handle) {
    ptls_aead_free(aead_ctx->native_handle);
  }
}

int ngtcp2_crypto_cipher_ctx_encrypt_init(ngtcp2_crypto_cipher_ctx *cipher_ctx,
                                          const ngtcp2_crypto_cipher *cipher,
                                          const uint8_t *key) {
  ptls_cipher_context_t *actx;

  actx = ptls_cipher_new(cipher->native_handle, /* is_enc = */ 1, key);
  if (actx == NULL) {
    return -1;
  }

  cipher_ctx->native_handle = actx;

  return 0;
}

void ngtcp2_crypto_cipher_ctx_free(ngtcp2_crypto_cipher_ctx *cipher_ctx) {
  if (cipher_ctx->native_handle) {
    ptls_cipher_free(cipher_ctx->native_handle);
  }
}

int ngtcp2_crypto_hkdf_extract(uint8_t *dest, const ngtcp2_crypto_md *md,
                               const uint8_t *secret, size_t secretlen,
                               const uint8_t *salt, size_t saltlen) {
  ptls_iovec_t saltv, ikm;

  saltv = ptls_iovec_init(salt, saltlen);
  ikm = ptls_iovec_init(secret, secretlen);

  if (ptls_hkdf_extract(md->native_handle, dest, saltv, ikm) != 0) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_hkdf_expand(uint8_t *dest, size_t destlen,
                              const ngtcp2_crypto_md *md, const uint8_t *secret,
                              size_t secretlen, const uint8_t *info,
                              size_t infolen) {
  ptls_iovec_t prk, infov;

  prk = ptls_iovec_init(secret, secretlen);
  infov = ptls_iovec_init(info, infolen);

  if (ptls_hkdf_expand(md->native_handle, dest, destlen, prk, infov) != 0) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_hkdf(uint8_t *dest, size_t destlen,
                       const ngtcp2_crypto_md *md, const uint8_t *secret,
                       size_t secretlen, const uint8_t *salt, size_t saltlen,
                       const uint8_t *info, size_t infolen) {
  ptls_iovec_t saltv, ikm, prk, infov;
  uint8_t prkbuf[PTLS_MAX_DIGEST_SIZE];
  ptls_hash_algorithm_t *algo = md->native_handle;

  saltv = ptls_iovec_init(salt, saltlen);
  ikm = ptls_iovec_init(secret, secretlen);

  if (ptls_hkdf_extract(algo, prkbuf, saltv, ikm) != 0) {
    return -1;
  }

  prk = ptls_iovec_init(prkbuf, algo->digest_size);
  infov = ptls_iovec_init(info, infolen);

  if (ptls_hkdf_expand(algo, dest, destlen, prk, infov) != 0) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_encrypt(uint8_t *dest, const ngtcp2_crypto_aead *aead,
                          const ngtcp2_crypto_aead_ctx *aead_ctx,
                          const uint8_t *plaintext, size_t plaintextlen,
                          const uint8_t *nonce, size_t noncelen,
                          const uint8_t *aad, size_t aadlen) {
  ptls_aead_context_t *actx = aead_ctx->native_handle;

  (void)aead;

  ptls_aead_xor_iv(actx, nonce, noncelen);

  ptls_aead_encrypt(actx, dest, plaintext, plaintextlen, 0, aad, aadlen);

  /* zero-out static iv once again */
  ptls_aead_xor_iv(actx, nonce, noncelen);

  return 0;
}

int ngtcp2_crypto_decrypt(uint8_t *dest, const ngtcp2_crypto_aead *aead,
                          const ngtcp2_crypto_aead_ctx *aead_ctx,
                          const uint8_t *ciphertext, size_t ciphertextlen,
                          const uint8_t *nonce, size_t noncelen,
                          const uint8_t *aad, size_t aadlen) {
  ptls_aead_context_t *actx = aead_ctx->native_handle;

  (void)aead;

  ptls_aead_xor_iv(actx, nonce, noncelen);

  if (ptls_aead_decrypt(actx, dest, ciphertext, ciphertextlen, 0, aad,
                        aadlen) == SIZE_MAX) {
    return -1;
  }

  /* zero-out static iv once again */
  ptls_aead_xor_iv(actx, nonce, noncelen);

  return 0;
}

int ngtcp2_crypto_hp_mask(uint8_t *dest, const ngtcp2_crypto_cipher *hp,
                          const ngtcp2_crypto_cipher_ctx *hp_ctx,
                          const uint8_t *sample) {
  ptls_cipher_context_t *actx = hp_ctx->native_handle;
  static const uint8_t PLAINTEXT[] = "\x00\x00\x00\x00\x00";

  (void)hp;

  ptls_cipher_init(actx, sample);
  ptls_cipher_encrypt(actx, dest, PLAINTEXT, sizeof(PLAINTEXT) - 1);

  return 0;
}

int ngtcp2_crypto_read_write_crypto_data(
    ngtcp2_conn *conn, ngtcp2_encryption_level encryption_level,
    const uint8_t *data, size_t datalen) {
  ngtcp2_crypto_picotls_ctx *cptls = ngtcp2_conn_get_tls_native_handle(conn);
  ptls_buffer_t sendbuf;
  size_t epoch_offsets[5] = {0};
  size_t epoch =
      ngtcp2_crypto_picotls_from_ngtcp2_encryption_level(encryption_level);
  size_t epoch_datalen;
  size_t i;
  int rv;

  ptls_buffer_init(&sendbuf, (void *)"", 0);

  assert(epoch == ptls_get_read_epoch(cptls->ptls));

  rv = ptls_handle_message(cptls->ptls, &sendbuf, epoch_offsets, epoch, data,
                           datalen, &cptls->handshake_properties);
  if (rv != 0 && rv != PTLS_ERROR_IN_PROGRESS) {
    if (PTLS_ERROR_GET_CLASS(rv) == PTLS_ERROR_CLASS_SELF_ALERT) {
      ngtcp2_conn_set_tls_alert(conn, (uint8_t)PTLS_ERROR_TO_ALERT(rv));
    }

    rv = -1;
    goto fin;
  }

  if (!ngtcp2_conn_is_server(conn) &&
      cptls->handshake_properties.client.early_data_acceptance ==
          PTLS_EARLY_DATA_REJECTED) {
    rv = ngtcp2_conn_tls_early_data_rejected(conn);
    if (rv != 0) {
      rv = -1;
      goto fin;
    }
  }

  for (i = 0; i < 4; ++i) {
    epoch_datalen = epoch_offsets[i + 1] - epoch_offsets[i];
    if (epoch_datalen == 0) {
      continue;
    }

    assert(i != 1);

    if (ngtcp2_conn_submit_crypto_data(
            conn, ngtcp2_crypto_picotls_from_epoch(i),
            sendbuf.base + epoch_offsets[i], epoch_datalen) != 0) {
      rv = -1;
      goto fin;
    }
  }

  if (rv == 0) {
    ngtcp2_conn_tls_handshake_completed(conn);
  }

  rv = 0;

fin:
  ptls_buffer_dispose(&sendbuf);

  return rv;
}

int ngtcp2_crypto_set_remote_transport_params(ngtcp2_conn *conn, void *tls) {
  (void)conn;
  (void)tls;

  /* The remote transport parameters will be set via picotls
     collected_extensions callback */

  return 0;
}

int ngtcp2_crypto_set_local_transport_params(void *tls, const uint8_t *buf,
                                             size_t len) {
  (void)tls;
  (void)buf;
  (void)len;

  /* The local transport parameters will be set in an external
     call. */

  return 0;
}

ngtcp2_encryption_level ngtcp2_crypto_picotls_from_epoch(size_t epoch) {
  switch (epoch) {
  case 0:
    return NGTCP2_ENCRYPTION_LEVEL_INITIAL;
  case 1:
    return NGTCP2_ENCRYPTION_LEVEL_0RTT;
  case 2:
    return NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE;
  case 3:
    return NGTCP2_ENCRYPTION_LEVEL_1RTT;
  default:
    assert(0);
    abort();
  }
}

size_t ngtcp2_crypto_picotls_from_ngtcp2_encryption_level(
    ngtcp2_encryption_level encryption_level) {
  switch (encryption_level) {
  case NGTCP2_ENCRYPTION_LEVEL_INITIAL:
    return 0;
  case NGTCP2_ENCRYPTION_LEVEL_0RTT:
    return 1;
  case NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE:
    return 2;
  case NGTCP2_ENCRYPTION_LEVEL_1RTT:
    return 3;
  default:
    assert(0);
    abort();
  }
}

int ngtcp2_crypto_get_path_challenge_data_cb(ngtcp2_conn *conn, uint8_t *data,
                                             void *user_data) {
  (void)conn;
  (void)user_data;

  ptls_openssl_random_bytes(data, NGTCP2_PATH_CHALLENGE_DATALEN);

  return 0;
}

int ngtcp2_crypto_random(uint8_t *data, size_t datalen) {
  ptls_openssl_random_bytes(data, datalen);

  return 0;
}

void ngtcp2_crypto_picotls_ctx_init(ngtcp2_crypto_picotls_ctx *cptls) {
  cptls->ptls = NULL;
  memset(&cptls->handshake_properties, 0, sizeof(cptls->handshake_properties));
}

static int set_additional_extensions(ptls_handshake_properties_t *hsprops,
                                     ngtcp2_conn *conn) {
  const size_t buflen = 256;
  uint8_t *buf;
  ngtcp2_ssize nwrite;
  ptls_raw_extension_t *exts = hsprops->additional_extensions;

  assert(exts);

  buf = malloc(buflen);
  if (buf == NULL) {
    return -1;
  }

  nwrite = ngtcp2_conn_encode_local_transport_params(conn, buf, buflen);
  if (nwrite < 0) {
    goto fail;
  }

  exts[0].type = NGTCP2_TLSEXT_QUIC_TRANSPORT_PARAMETERS_V1;
  exts[0].data.base = buf;
  exts[0].data.len = (size_t)nwrite;

  return 0;

fail:
  free(buf);

  return -1;
}

int ngtcp2_crypto_picotls_collect_extension(
    ptls_t *ptls, struct st_ptls_handshake_properties_t *properties,
    uint16_t type) {
  (void)ptls;
  (void)properties;

  return type == NGTCP2_TLSEXT_QUIC_TRANSPORT_PARAMETERS_V1;
}

int ngtcp2_crypto_picotls_collected_extensions(
    ptls_t *ptls, struct st_ptls_handshake_properties_t *properties,
    ptls_raw_extension_t *extensions) {
  ngtcp2_crypto_conn_ref *conn_ref;
  ngtcp2_conn *conn;
  int rv;

  (void)properties;

  for (; extensions->type != UINT16_MAX; ++extensions) {
    if (extensions->type != NGTCP2_TLSEXT_QUIC_TRANSPORT_PARAMETERS_V1) {
      continue;
    }

    conn_ref = *ptls_get_data_ptr(ptls);
    conn = conn_ref->get_conn(conn_ref);

    rv = ngtcp2_conn_decode_and_set_remote_transport_params(
        conn, extensions->data.base, extensions->data.len);
    if (rv != 0) {
      ngtcp2_conn_set_tls_error(conn, rv);
      return -1;
    }

    return 0;
  }

  return 0;
}

static int update_traffic_key_server_cb(ptls_update_traffic_key_t *self,
                                        ptls_t *ptls, int is_enc, size_t epoch,
                                        const void *secret) {
  ngtcp2_crypto_conn_ref *conn_ref = *ptls_get_data_ptr(ptls);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  ngtcp2_encryption_level level = ngtcp2_crypto_picotls_from_epoch(epoch);
  ptls_cipher_suite_t *cipher = ptls_get_cipher(ptls);
  size_t secretlen = cipher->hash->digest_size;
  ngtcp2_crypto_picotls_ctx *cptls;

  (void)self;

  if (is_enc) {
    if (ngtcp2_crypto_derive_and_install_tx_key(conn, NULL, NULL, NULL, level,
                                                secret, secretlen) != 0) {
      return -1;
    }

    if (level == NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE) {
      /* libngtcp2 allows an application to change QUIC transport
       * parameters before installing Handshake tx key.  We need to
       * wait for the key to get the correct local transport
       * parameters from ngtcp2_conn.
       */
      cptls = ngtcp2_conn_get_tls_native_handle(conn);

      if (set_additional_extensions(&cptls->handshake_properties, conn) != 0) {
        return -1;
      }
    }

    return 0;
  }

  if (ngtcp2_crypto_derive_and_install_rx_key(conn, NULL, NULL, NULL, level,
                                              secret, secretlen) != 0) {
    return -1;
  }

  return 0;
}

static ptls_update_traffic_key_t update_traffic_key_server = {
    update_traffic_key_server_cb,
};

static int update_traffic_key_cb(ptls_update_traffic_key_t *self, ptls_t *ptls,
                                 int is_enc, size_t epoch, const void *secret) {
  ngtcp2_crypto_conn_ref *conn_ref = *ptls_get_data_ptr(ptls);
  ngtcp2_conn *conn = conn_ref->get_conn(conn_ref);
  ngtcp2_encryption_level level = ngtcp2_crypto_picotls_from_epoch(epoch);
  ptls_cipher_suite_t *cipher = ptls_get_cipher(ptls);
  size_t secretlen = cipher->hash->digest_size;

  (void)self;

  if (is_enc) {
    if (ngtcp2_crypto_derive_and_install_tx_key(conn, NULL, NULL, NULL, level,
                                                secret, secretlen) != 0) {
      return -1;
    }

    return 0;
  }

  if (ngtcp2_crypto_derive_and_install_rx_key(conn, NULL, NULL, NULL, level,
                                              secret, secretlen) != 0) {
    return -1;
  }

  return 0;
}

static ptls_update_traffic_key_t update_traffic_key = {update_traffic_key_cb};

int ngtcp2_crypto_picotls_configure_server_context(ptls_context_t *ctx) {
  ctx->max_early_data_size = UINT32_MAX;
  ctx->omit_end_of_early_data = 1;
  ctx->update_traffic_key = &update_traffic_key_server;

  return 0;
}

int ngtcp2_crypto_picotls_configure_client_context(ptls_context_t *ctx) {
  ctx->omit_end_of_early_data = 1;
  ctx->update_traffic_key = &update_traffic_key;

  return 0;
}

int ngtcp2_crypto_picotls_configure_server_session(
    ngtcp2_crypto_picotls_ctx *cptls) {
  ptls_handshake_properties_t *hsprops = &cptls->handshake_properties;

  hsprops->collect_extension = ngtcp2_crypto_picotls_collect_extension;
  hsprops->collected_extensions = ngtcp2_crypto_picotls_collected_extensions;

  return 0;
}

int ngtcp2_crypto_picotls_configure_client_session(
    ngtcp2_crypto_picotls_ctx *cptls, ngtcp2_conn *conn) {
  ptls_handshake_properties_t *hsprops = &cptls->handshake_properties;

  hsprops->client.max_early_data_size = calloc(1, sizeof(size_t));
  if (hsprops->client.max_early_data_size == NULL) {
    return -1;
  }

  if (set_additional_extensions(hsprops, conn) != 0) {
    free(hsprops->client.max_early_data_size);
    hsprops->client.max_early_data_size = NULL;
    return -1;
  }

  hsprops->collect_extension = ngtcp2_crypto_picotls_collect_extension;
  hsprops->collected_extensions = ngtcp2_crypto_picotls_collected_extensions;

  return 0;
}

void ngtcp2_crypto_picotls_deconfigure_session(
    ngtcp2_crypto_picotls_ctx *cptls) {
  ptls_handshake_properties_t *hsprops;
  ptls_raw_extension_t *exts;

  if (cptls == NULL) {
    return;
  }

  hsprops = &cptls->handshake_properties;

  free(hsprops->client.max_early_data_size);

  exts = hsprops->additional_extensions;
  if (exts) {
    free(hsprops->additional_extensions[0].data.base);
  }
}
