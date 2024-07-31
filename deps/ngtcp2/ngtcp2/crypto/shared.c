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
#include "shared.h"

#ifdef WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <netinet/in.h>
#endif

#include <string.h>
#include <assert.h>

#include "ngtcp2_macro.h"
#include "ngtcp2_net.h"

ngtcp2_crypto_md *ngtcp2_crypto_md_init(ngtcp2_crypto_md *md,
                                        void *md_native_handle) {
  md->native_handle = md_native_handle;
  return md;
}

int ngtcp2_crypto_hkdf_expand_label(uint8_t *dest, size_t destlen,
                                    const ngtcp2_crypto_md *md,
                                    const uint8_t *secret, size_t secretlen,
                                    const uint8_t *label, size_t labellen) {
  static const uint8_t LABEL[] = "tls13 ";
  uint8_t info[256];
  uint8_t *p = info;

  *p++ = (uint8_t)(destlen / 256);
  *p++ = (uint8_t)(destlen % 256);
  *p++ = (uint8_t)(sizeof(LABEL) - 1 + labellen);
  memcpy(p, LABEL, sizeof(LABEL) - 1);
  p += sizeof(LABEL) - 1;
  memcpy(p, label, labellen);
  p += labellen;
  *p++ = 0;

  return ngtcp2_crypto_hkdf_expand(dest, destlen, md, secret, secretlen, info,
                                   (size_t)(p - info));
}

int ngtcp2_crypto_derive_initial_secrets(uint8_t *rx_secret, uint8_t *tx_secret,
                                         uint8_t *initial_secret,
                                         uint32_t version,
                                         const ngtcp2_cid *client_dcid,
                                         ngtcp2_crypto_side side) {
  static const uint8_t CLABEL[] = "client in";
  static const uint8_t SLABEL[] = "server in";
  uint8_t initial_secret_buf[NGTCP2_CRYPTO_INITIAL_SECRETLEN];
  uint8_t *client_secret;
  uint8_t *server_secret;
  ngtcp2_crypto_ctx ctx;
  const uint8_t *salt;
  size_t saltlen;

  if (!initial_secret) {
    initial_secret = initial_secret_buf;
  }

  ngtcp2_crypto_ctx_initial(&ctx);

  switch (version) {
  case NGTCP2_PROTO_VER_V1:
  default:
    salt = (const uint8_t *)NGTCP2_INITIAL_SALT_V1;
    saltlen = sizeof(NGTCP2_INITIAL_SALT_V1) - 1;
    break;
  case NGTCP2_PROTO_VER_V2:
    salt = (const uint8_t *)NGTCP2_INITIAL_SALT_V2;
    saltlen = sizeof(NGTCP2_INITIAL_SALT_V2) - 1;
    break;
  }

  if (ngtcp2_crypto_hkdf_extract(initial_secret, &ctx.md, client_dcid->data,
                                 client_dcid->datalen, salt, saltlen) != 0) {
    return -1;
  }

  if (side == NGTCP2_CRYPTO_SIDE_SERVER) {
    client_secret = rx_secret;
    server_secret = tx_secret;
  } else {
    client_secret = tx_secret;
    server_secret = rx_secret;
  }

  if (ngtcp2_crypto_hkdf_expand_label(
          client_secret, NGTCP2_CRYPTO_INITIAL_SECRETLEN, &ctx.md,
          initial_secret, NGTCP2_CRYPTO_INITIAL_SECRETLEN, CLABEL,
          sizeof(CLABEL) - 1) != 0 ||
      ngtcp2_crypto_hkdf_expand_label(
          server_secret, NGTCP2_CRYPTO_INITIAL_SECRETLEN, &ctx.md,
          initial_secret, NGTCP2_CRYPTO_INITIAL_SECRETLEN, SLABEL,
          sizeof(SLABEL) - 1) != 0) {
    return -1;
  }

  return 0;
}

size_t ngtcp2_crypto_packet_protection_ivlen(const ngtcp2_crypto_aead *aead) {
  size_t noncelen = ngtcp2_crypto_aead_noncelen(aead);
  return ngtcp2_max(8, noncelen);
}

int ngtcp2_crypto_derive_packet_protection_key(
    uint8_t *key, uint8_t *iv, uint8_t *hp_key, uint32_t version,
    const ngtcp2_crypto_aead *aead, const ngtcp2_crypto_md *md,
    const uint8_t *secret, size_t secretlen) {
  static const uint8_t KEY_LABEL_V1[] = "quic key";
  static const uint8_t IV_LABEL_V1[] = "quic iv";
  static const uint8_t HP_KEY_LABEL_V1[] = "quic hp";
  static const uint8_t KEY_LABEL_V2[] = "quicv2 key";
  static const uint8_t IV_LABEL_V2[] = "quicv2 iv";
  static const uint8_t HP_KEY_LABEL_V2[] = "quicv2 hp";
  size_t keylen = ngtcp2_crypto_aead_keylen(aead);
  size_t ivlen = ngtcp2_crypto_packet_protection_ivlen(aead);
  const uint8_t *key_label;
  size_t key_labellen;
  const uint8_t *iv_label;
  size_t iv_labellen;
  const uint8_t *hp_key_label;
  size_t hp_key_labellen;

  switch (version) {
  case NGTCP2_PROTO_VER_V2:
    key_label = KEY_LABEL_V2;
    key_labellen = sizeof(KEY_LABEL_V2) - 1;
    iv_label = IV_LABEL_V2;
    iv_labellen = sizeof(IV_LABEL_V2) - 1;
    hp_key_label = HP_KEY_LABEL_V2;
    hp_key_labellen = sizeof(HP_KEY_LABEL_V2) - 1;
    break;
  default:
    key_label = KEY_LABEL_V1;
    key_labellen = sizeof(KEY_LABEL_V1) - 1;
    iv_label = IV_LABEL_V1;
    iv_labellen = sizeof(IV_LABEL_V1) - 1;
    hp_key_label = HP_KEY_LABEL_V1;
    hp_key_labellen = sizeof(HP_KEY_LABEL_V1) - 1;
  }

  if (ngtcp2_crypto_hkdf_expand_label(key, keylen, md, secret, secretlen,
                                      key_label, key_labellen) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_hkdf_expand_label(iv, ivlen, md, secret, secretlen,
                                      iv_label, iv_labellen) != 0) {
    return -1;
  }

  if (hp_key != NULL &&
      ngtcp2_crypto_hkdf_expand_label(hp_key, keylen, md, secret, secretlen,
                                      hp_key_label, hp_key_labellen) != 0) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_update_traffic_secret(uint8_t *dest, uint32_t version,
                                        const ngtcp2_crypto_md *md,
                                        const uint8_t *secret,
                                        size_t secretlen) {
  static const uint8_t LABEL[] = "quic ku";
  static const uint8_t LABEL_V2[] = "quicv2 ku";
  const uint8_t *label;
  size_t labellen;

  switch (version) {
  case NGTCP2_PROTO_VER_V2:
    label = LABEL_V2;
    labellen = sizeof(LABEL_V2) - 1;
    break;
  default:
    label = LABEL;
    labellen = sizeof(LABEL) - 1;
  }

  if (ngtcp2_crypto_hkdf_expand_label(dest, secretlen, md, secret, secretlen,
                                      label, labellen) != 0) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_derive_and_install_rx_key(ngtcp2_conn *conn, uint8_t *key,
                                            uint8_t *iv, uint8_t *hp_key,
                                            ngtcp2_encryption_level level,
                                            const uint8_t *secret,
                                            size_t secretlen) {
  const ngtcp2_crypto_ctx *ctx;
  const ngtcp2_crypto_aead *aead;
  const ngtcp2_crypto_md *md;
  const ngtcp2_crypto_cipher *hp;
  ngtcp2_crypto_aead_ctx aead_ctx = {0};
  ngtcp2_crypto_cipher_ctx hp_ctx = {0};
  void *tls = ngtcp2_conn_get_tls_native_handle(conn);
  uint8_t keybuf[64], ivbuf[64], hp_keybuf[64];
  size_t ivlen;
  int rv;
  ngtcp2_crypto_ctx cctx;
  uint32_t version;

  if (level == NGTCP2_ENCRYPTION_LEVEL_0RTT && !ngtcp2_conn_is_server(conn)) {
    return 0;
  }

  if (!key) {
    key = keybuf;
  }
  if (!iv) {
    iv = ivbuf;
  }
  if (!hp_key) {
    hp_key = hp_keybuf;
  }

  switch (level) {
  case NGTCP2_ENCRYPTION_LEVEL_0RTT:
    if (ngtcp2_crypto_ctx_tls_early(&cctx, tls) == NULL) {
      return -1;
    }

    ngtcp2_conn_set_0rtt_crypto_ctx(conn, &cctx);
    ctx = ngtcp2_conn_get_0rtt_crypto_ctx(conn);
    version = ngtcp2_conn_get_client_chosen_version(conn);
    break;
  case NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE:
    if (ngtcp2_conn_is_server(conn) &&
        !ngtcp2_conn_get_negotiated_version(conn)) {
      rv = ngtcp2_crypto_set_remote_transport_params(conn, tls);
      if (rv != 0) {
        return -1;
      }
    }
    /* fall through */
  case NGTCP2_ENCRYPTION_LEVEL_1RTT:
    ctx = ngtcp2_conn_get_crypto_ctx(conn);
    version = ngtcp2_conn_get_negotiated_version(conn);

    if (!ctx->aead.native_handle) {
      if (ngtcp2_crypto_ctx_tls(&cctx, tls) == NULL) {
        return -1;
      }

      ngtcp2_conn_set_crypto_ctx(conn, &cctx);
      ctx = ngtcp2_conn_get_crypto_ctx(conn);
    }
    break;
  default:
    return -1;
  }

  aead = &ctx->aead;
  md = &ctx->md;
  hp = &ctx->hp;
  ivlen = ngtcp2_crypto_packet_protection_ivlen(aead);

  if (ngtcp2_crypto_derive_packet_protection_key(key, iv, hp_key, version, aead,
                                                 md, secret, secretlen) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_aead_ctx_decrypt_init(&aead_ctx, aead, key, ivlen) != 0) {
    goto fail;
  }

  if (ngtcp2_crypto_cipher_ctx_encrypt_init(&hp_ctx, hp, hp_key) != 0) {
    goto fail;
  }

  switch (level) {
  case NGTCP2_ENCRYPTION_LEVEL_0RTT:
    rv = ngtcp2_conn_install_0rtt_key(conn, &aead_ctx, iv, ivlen, &hp_ctx);
    if (rv != 0) {
      goto fail;
    }
    break;
  case NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE:
    rv = ngtcp2_conn_install_rx_handshake_key(conn, &aead_ctx, iv, ivlen,
                                              &hp_ctx);
    if (rv != 0) {
      goto fail;
    }
    break;
  case NGTCP2_ENCRYPTION_LEVEL_1RTT:
    if (!ngtcp2_conn_is_server(conn)) {
      rv = ngtcp2_crypto_set_remote_transport_params(conn, tls);
      if (rv != 0) {
        goto fail;
      }
    }

    rv = ngtcp2_conn_install_rx_key(conn, secret, secretlen, &aead_ctx, iv,
                                    ivlen, &hp_ctx);
    if (rv != 0) {
      goto fail;
    }

    break;
  default:
    goto fail;
  }

  return 0;

fail:
  ngtcp2_crypto_cipher_ctx_free(&hp_ctx);
  ngtcp2_crypto_aead_ctx_free(&aead_ctx);

  return -1;
}

/*
 * crypto_set_local_transport_params gets local QUIC transport
 * parameters from |conn| and sets it to |tls|.
 *
 * This function returns 0 if it succeeds, or -1.
 */
static int crypto_set_local_transport_params(ngtcp2_conn *conn, void *tls) {
  ngtcp2_ssize nwrite;
  uint8_t buf[256];

  nwrite = ngtcp2_conn_encode_local_transport_params(conn, buf, sizeof(buf));
  if (nwrite < 0) {
    return -1;
  }

  if (ngtcp2_crypto_set_local_transport_params(tls, buf, (size_t)nwrite) != 0) {
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_derive_and_install_tx_key(ngtcp2_conn *conn, uint8_t *key,
                                            uint8_t *iv, uint8_t *hp_key,
                                            ngtcp2_encryption_level level,
                                            const uint8_t *secret,
                                            size_t secretlen) {
  const ngtcp2_crypto_ctx *ctx;
  const ngtcp2_crypto_aead *aead;
  const ngtcp2_crypto_md *md;
  const ngtcp2_crypto_cipher *hp;
  ngtcp2_crypto_aead_ctx aead_ctx = {0};
  ngtcp2_crypto_cipher_ctx hp_ctx = {0};
  void *tls = ngtcp2_conn_get_tls_native_handle(conn);
  uint8_t keybuf[64], ivbuf[64], hp_keybuf[64];
  size_t ivlen;
  int rv;
  ngtcp2_crypto_ctx cctx;
  uint32_t version;

  if (level == NGTCP2_ENCRYPTION_LEVEL_0RTT && ngtcp2_conn_is_server(conn)) {
    return 0;
  }

  if (!key) {
    key = keybuf;
  }
  if (!iv) {
    iv = ivbuf;
  }
  if (!hp_key) {
    hp_key = hp_keybuf;
  }

  switch (level) {
  case NGTCP2_ENCRYPTION_LEVEL_0RTT:
    if (ngtcp2_crypto_ctx_tls_early(&cctx, tls) == NULL) {
      return -1;
    }

    ngtcp2_conn_set_0rtt_crypto_ctx(conn, &cctx);
    ctx = ngtcp2_conn_get_0rtt_crypto_ctx(conn);
    version = ngtcp2_conn_get_client_chosen_version(conn);
    break;
  case NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE:
    if (ngtcp2_conn_is_server(conn) &&
        !ngtcp2_conn_get_negotiated_version(conn)) {
      rv = ngtcp2_crypto_set_remote_transport_params(conn, tls);
      if (rv != 0) {
        return -1;
      }
    }
    /* fall through */
  case NGTCP2_ENCRYPTION_LEVEL_1RTT:
    ctx = ngtcp2_conn_get_crypto_ctx(conn);
    version = ngtcp2_conn_get_negotiated_version(conn);

    if (!ctx->aead.native_handle) {
      if (ngtcp2_crypto_ctx_tls(&cctx, tls) == NULL) {
        return -1;
      }

      ngtcp2_conn_set_crypto_ctx(conn, &cctx);
      ctx = ngtcp2_conn_get_crypto_ctx(conn);
    }
    break;
  default:
    return -1;
  }

  aead = &ctx->aead;
  md = &ctx->md;
  hp = &ctx->hp;
  ivlen = ngtcp2_crypto_packet_protection_ivlen(aead);

  if (ngtcp2_crypto_derive_packet_protection_key(key, iv, hp_key, version, aead,
                                                 md, secret, secretlen) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_aead_ctx_encrypt_init(&aead_ctx, aead, key, ivlen) != 0) {
    goto fail;
  }

  if (ngtcp2_crypto_cipher_ctx_encrypt_init(&hp_ctx, hp, hp_key) != 0) {
    goto fail;
  }

  switch (level) {
  case NGTCP2_ENCRYPTION_LEVEL_0RTT:
    rv = ngtcp2_conn_install_0rtt_key(conn, &aead_ctx, iv, ivlen, &hp_ctx);
    if (rv != 0) {
      goto fail;
    }
    break;
  case NGTCP2_ENCRYPTION_LEVEL_HANDSHAKE:
    rv = ngtcp2_conn_install_tx_handshake_key(conn, &aead_ctx, iv, ivlen,
                                              &hp_ctx);
    if (rv != 0) {
      goto fail;
    }

    if (ngtcp2_conn_is_server(conn) &&
        crypto_set_local_transport_params(conn, tls) != 0) {
      goto fail;
    }

    break;
  case NGTCP2_ENCRYPTION_LEVEL_1RTT:
    rv = ngtcp2_conn_install_tx_key(conn, secret, secretlen, &aead_ctx, iv,
                                    ivlen, &hp_ctx);
    if (rv != 0) {
      goto fail;
    }

    break;
  default:
    goto fail;
  }

  return 0;

fail:
  ngtcp2_crypto_cipher_ctx_free(&hp_ctx);
  ngtcp2_crypto_aead_ctx_free(&aead_ctx);

  return -1;
}

int ngtcp2_crypto_derive_and_install_initial_key(
    ngtcp2_conn *conn, uint8_t *rx_secret, uint8_t *tx_secret,
    uint8_t *initial_secret, uint8_t *rx_key, uint8_t *rx_iv,
    uint8_t *rx_hp_key, uint8_t *tx_key, uint8_t *tx_iv, uint8_t *tx_hp_key,
    uint32_t version, const ngtcp2_cid *client_dcid) {
  uint8_t rx_secretbuf[NGTCP2_CRYPTO_INITIAL_SECRETLEN];
  uint8_t tx_secretbuf[NGTCP2_CRYPTO_INITIAL_SECRETLEN];
  uint8_t initial_secretbuf[NGTCP2_CRYPTO_INITIAL_SECRETLEN];
  uint8_t rx_keybuf[NGTCP2_CRYPTO_INITIAL_KEYLEN];
  uint8_t rx_ivbuf[NGTCP2_CRYPTO_INITIAL_IVLEN];
  uint8_t rx_hp_keybuf[NGTCP2_CRYPTO_INITIAL_KEYLEN];
  uint8_t tx_keybuf[NGTCP2_CRYPTO_INITIAL_KEYLEN];
  uint8_t tx_ivbuf[NGTCP2_CRYPTO_INITIAL_IVLEN];
  uint8_t tx_hp_keybuf[NGTCP2_CRYPTO_INITIAL_KEYLEN];
  ngtcp2_crypto_ctx ctx;
  ngtcp2_crypto_aead retry_aead;
  ngtcp2_crypto_aead_ctx rx_aead_ctx = {0};
  ngtcp2_crypto_cipher_ctx rx_hp_ctx = {0};
  ngtcp2_crypto_aead_ctx tx_aead_ctx = {0};
  ngtcp2_crypto_cipher_ctx tx_hp_ctx = {0};
  ngtcp2_crypto_aead_ctx retry_aead_ctx = {0};
  int rv;
  int server = ngtcp2_conn_is_server(conn);
  const uint8_t *retry_key;
  size_t retry_noncelen;

  ngtcp2_crypto_ctx_initial(&ctx);

  if (!rx_secret) {
    rx_secret = rx_secretbuf;
  }
  if (!tx_secret) {
    tx_secret = tx_secretbuf;
  }
  if (!initial_secret) {
    initial_secret = initial_secretbuf;
  }

  if (!rx_key) {
    rx_key = rx_keybuf;
  }
  if (!rx_iv) {
    rx_iv = rx_ivbuf;
  }
  if (!rx_hp_key) {
    rx_hp_key = rx_hp_keybuf;
  }
  if (!tx_key) {
    tx_key = tx_keybuf;
  }
  if (!tx_iv) {
    tx_iv = tx_ivbuf;
  }
  if (!tx_hp_key) {
    tx_hp_key = tx_hp_keybuf;
  }

  ngtcp2_conn_set_initial_crypto_ctx(conn, &ctx);

  if (ngtcp2_crypto_derive_initial_secrets(
          rx_secret, tx_secret, initial_secret, version, client_dcid,
          server ? NGTCP2_CRYPTO_SIDE_SERVER : NGTCP2_CRYPTO_SIDE_CLIENT) !=
      0) {
    return -1;
  }

  if (ngtcp2_crypto_derive_packet_protection_key(
          rx_key, rx_iv, rx_hp_key, version, &ctx.aead, &ctx.md, rx_secret,
          NGTCP2_CRYPTO_INITIAL_SECRETLEN) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_derive_packet_protection_key(
          tx_key, tx_iv, tx_hp_key, version, &ctx.aead, &ctx.md, tx_secret,
          NGTCP2_CRYPTO_INITIAL_SECRETLEN) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_aead_ctx_decrypt_init(&rx_aead_ctx, &ctx.aead, rx_key,
                                          NGTCP2_CRYPTO_INITIAL_IVLEN) != 0) {
    goto fail;
  }

  if (ngtcp2_crypto_cipher_ctx_encrypt_init(&rx_hp_ctx, &ctx.hp, rx_hp_key) !=
      0) {
    goto fail;
  }

  if (ngtcp2_crypto_aead_ctx_encrypt_init(&tx_aead_ctx, &ctx.aead, tx_key,
                                          NGTCP2_CRYPTO_INITIAL_IVLEN) != 0) {
    goto fail;
  }

  if (ngtcp2_crypto_cipher_ctx_encrypt_init(&tx_hp_ctx, &ctx.hp, tx_hp_key) !=
      0) {
    goto fail;
  }

  if (!server && !ngtcp2_conn_after_retry(conn)) {
    ngtcp2_crypto_aead_retry(&retry_aead);

    switch (version) {
    case NGTCP2_PROTO_VER_V1:
    default:
      retry_key = (const uint8_t *)NGTCP2_RETRY_KEY_V1;
      retry_noncelen = sizeof(NGTCP2_RETRY_NONCE_V1) - 1;
      break;
    case NGTCP2_PROTO_VER_V2:
      retry_key = (const uint8_t *)NGTCP2_RETRY_KEY_V2;
      retry_noncelen = sizeof(NGTCP2_RETRY_NONCE_V2) - 1;
      break;
    }

    if (ngtcp2_crypto_aead_ctx_encrypt_init(&retry_aead_ctx, &retry_aead,
                                            retry_key, retry_noncelen) != 0) {
      goto fail;
    }
  }

  rv = ngtcp2_conn_install_initial_key(conn, &rx_aead_ctx, rx_iv, &rx_hp_ctx,
                                       &tx_aead_ctx, tx_iv, &tx_hp_ctx,
                                       NGTCP2_CRYPTO_INITIAL_IVLEN);
  if (rv != 0) {
    goto fail;
  }

  if (retry_aead_ctx.native_handle) {
    ngtcp2_conn_set_retry_aead(conn, &retry_aead, &retry_aead_ctx);
  }

  return 0;

fail:
  ngtcp2_crypto_aead_ctx_free(&retry_aead_ctx);
  ngtcp2_crypto_cipher_ctx_free(&tx_hp_ctx);
  ngtcp2_crypto_aead_ctx_free(&tx_aead_ctx);
  ngtcp2_crypto_cipher_ctx_free(&rx_hp_ctx);
  ngtcp2_crypto_aead_ctx_free(&rx_aead_ctx);

  return -1;
}

int ngtcp2_crypto_derive_and_install_vneg_initial_key(
    ngtcp2_conn *conn, uint8_t *rx_secret, uint8_t *tx_secret,
    uint8_t *initial_secret, uint8_t *rx_key, uint8_t *rx_iv,
    uint8_t *rx_hp_key, uint8_t *tx_key, uint8_t *tx_iv, uint8_t *tx_hp_key,
    uint32_t version, const ngtcp2_cid *client_dcid) {
  uint8_t rx_secretbuf[NGTCP2_CRYPTO_INITIAL_SECRETLEN];
  uint8_t tx_secretbuf[NGTCP2_CRYPTO_INITIAL_SECRETLEN];
  uint8_t initial_secretbuf[NGTCP2_CRYPTO_INITIAL_SECRETLEN];
  uint8_t rx_keybuf[NGTCP2_CRYPTO_INITIAL_KEYLEN];
  uint8_t rx_ivbuf[NGTCP2_CRYPTO_INITIAL_IVLEN];
  uint8_t rx_hp_keybuf[NGTCP2_CRYPTO_INITIAL_KEYLEN];
  uint8_t tx_keybuf[NGTCP2_CRYPTO_INITIAL_KEYLEN];
  uint8_t tx_ivbuf[NGTCP2_CRYPTO_INITIAL_IVLEN];
  uint8_t tx_hp_keybuf[NGTCP2_CRYPTO_INITIAL_KEYLEN];
  const ngtcp2_crypto_ctx *ctx = ngtcp2_conn_get_initial_crypto_ctx(conn);
  ngtcp2_crypto_aead_ctx rx_aead_ctx = {0};
  ngtcp2_crypto_cipher_ctx rx_hp_ctx = {0};
  ngtcp2_crypto_aead_ctx tx_aead_ctx = {0};
  ngtcp2_crypto_cipher_ctx tx_hp_ctx = {0};
  int rv;
  int server = ngtcp2_conn_is_server(conn);

  if (!rx_secret) {
    rx_secret = rx_secretbuf;
  }
  if (!tx_secret) {
    tx_secret = tx_secretbuf;
  }
  if (!initial_secret) {
    initial_secret = initial_secretbuf;
  }

  if (!rx_key) {
    rx_key = rx_keybuf;
  }
  if (!rx_iv) {
    rx_iv = rx_ivbuf;
  }
  if (!rx_hp_key) {
    rx_hp_key = rx_hp_keybuf;
  }
  if (!tx_key) {
    tx_key = tx_keybuf;
  }
  if (!tx_iv) {
    tx_iv = tx_ivbuf;
  }
  if (!tx_hp_key) {
    tx_hp_key = tx_hp_keybuf;
  }

  if (ngtcp2_crypto_derive_initial_secrets(
          rx_secret, tx_secret, initial_secret, version, client_dcid,
          server ? NGTCP2_CRYPTO_SIDE_SERVER : NGTCP2_CRYPTO_SIDE_CLIENT) !=
      0) {
    return -1;
  }

  if (ngtcp2_crypto_derive_packet_protection_key(
          rx_key, rx_iv, rx_hp_key, version, &ctx->aead, &ctx->md, rx_secret,
          NGTCP2_CRYPTO_INITIAL_SECRETLEN) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_derive_packet_protection_key(
          tx_key, tx_iv, tx_hp_key, version, &ctx->aead, &ctx->md, tx_secret,
          NGTCP2_CRYPTO_INITIAL_SECRETLEN) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_aead_ctx_decrypt_init(&rx_aead_ctx, &ctx->aead, rx_key,
                                          NGTCP2_CRYPTO_INITIAL_IVLEN) != 0) {
    goto fail;
  }

  if (ngtcp2_crypto_cipher_ctx_encrypt_init(&rx_hp_ctx, &ctx->hp, rx_hp_key) !=
      0) {
    goto fail;
  }

  if (ngtcp2_crypto_aead_ctx_encrypt_init(&tx_aead_ctx, &ctx->aead, tx_key,
                                          NGTCP2_CRYPTO_INITIAL_IVLEN) != 0) {
    goto fail;
  }

  if (ngtcp2_crypto_cipher_ctx_encrypt_init(&tx_hp_ctx, &ctx->hp, tx_hp_key) !=
      0) {
    goto fail;
  }

  rv = ngtcp2_conn_install_vneg_initial_key(
      conn, version, &rx_aead_ctx, rx_iv, &rx_hp_ctx, &tx_aead_ctx, tx_iv,
      &tx_hp_ctx, NGTCP2_CRYPTO_INITIAL_IVLEN);
  if (rv != 0) {
    goto fail;
  }

  return 0;

fail:
  ngtcp2_crypto_cipher_ctx_free(&tx_hp_ctx);
  ngtcp2_crypto_aead_ctx_free(&tx_aead_ctx);
  ngtcp2_crypto_cipher_ctx_free(&rx_hp_ctx);
  ngtcp2_crypto_aead_ctx_free(&rx_aead_ctx);

  return -1;
}

int ngtcp2_crypto_update_key(
    ngtcp2_conn *conn, uint8_t *rx_secret, uint8_t *tx_secret,
    ngtcp2_crypto_aead_ctx *rx_aead_ctx, uint8_t *rx_key, uint8_t *rx_iv,
    ngtcp2_crypto_aead_ctx *tx_aead_ctx, uint8_t *tx_key, uint8_t *tx_iv,
    const uint8_t *current_rx_secret, const uint8_t *current_tx_secret,
    size_t secretlen) {
  const ngtcp2_crypto_ctx *ctx = ngtcp2_conn_get_crypto_ctx(conn);
  const ngtcp2_crypto_aead *aead = &ctx->aead;
  const ngtcp2_crypto_md *md = &ctx->md;
  size_t ivlen = ngtcp2_crypto_packet_protection_ivlen(aead);
  uint32_t version = ngtcp2_conn_get_negotiated_version(conn);

  if (ngtcp2_crypto_update_traffic_secret(rx_secret, version, md,
                                          current_rx_secret, secretlen) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_derive_packet_protection_key(
          rx_key, rx_iv, NULL, version, aead, md, rx_secret, secretlen) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_update_traffic_secret(tx_secret, version, md,
                                          current_tx_secret, secretlen) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_derive_packet_protection_key(
          tx_key, tx_iv, NULL, version, aead, md, tx_secret, secretlen) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_aead_ctx_decrypt_init(rx_aead_ctx, aead, rx_key, ivlen) !=
      0) {
    return -1;
  }

  if (ngtcp2_crypto_aead_ctx_encrypt_init(tx_aead_ctx, aead, tx_key, ivlen) !=
      0) {
    ngtcp2_crypto_aead_ctx_free(rx_aead_ctx);
    rx_aead_ctx->native_handle = NULL;
    return -1;
  }

  return 0;
}

int ngtcp2_crypto_encrypt_cb(uint8_t *dest, const ngtcp2_crypto_aead *aead,
                             const ngtcp2_crypto_aead_ctx *aead_ctx,
                             const uint8_t *plaintext, size_t plaintextlen,
                             const uint8_t *nonce, size_t noncelen,
                             const uint8_t *aad, size_t aadlen) {
  if (ngtcp2_crypto_encrypt(dest, aead, aead_ctx, plaintext, plaintextlen,
                            nonce, noncelen, aad, aadlen) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  return 0;
}

int ngtcp2_crypto_decrypt_cb(uint8_t *dest, const ngtcp2_crypto_aead *aead,
                             const ngtcp2_crypto_aead_ctx *aead_ctx,
                             const uint8_t *ciphertext, size_t ciphertextlen,
                             const uint8_t *nonce, size_t noncelen,
                             const uint8_t *aad, size_t aadlen) {
  if (ngtcp2_crypto_decrypt(dest, aead, aead_ctx, ciphertext, ciphertextlen,
                            nonce, noncelen, aad, aadlen) != 0) {
    return NGTCP2_ERR_DECRYPT;
  }
  return 0;
}

int ngtcp2_crypto_hp_mask_cb(uint8_t *dest, const ngtcp2_crypto_cipher *hp,
                             const ngtcp2_crypto_cipher_ctx *hp_ctx,
                             const uint8_t *sample) {
  if (ngtcp2_crypto_hp_mask(dest, hp, hp_ctx, sample) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  return 0;
}

int ngtcp2_crypto_update_key_cb(
    ngtcp2_conn *conn, uint8_t *rx_secret, uint8_t *tx_secret,
    ngtcp2_crypto_aead_ctx *rx_aead_ctx, uint8_t *rx_iv,
    ngtcp2_crypto_aead_ctx *tx_aead_ctx, uint8_t *tx_iv,
    const uint8_t *current_rx_secret, const uint8_t *current_tx_secret,
    size_t secretlen, void *user_data) {
  uint8_t rx_key[64];
  uint8_t tx_key[64];
  (void)conn;
  (void)user_data;

  if (ngtcp2_crypto_update_key(conn, rx_secret, tx_secret, rx_aead_ctx, rx_key,
                               rx_iv, tx_aead_ctx, tx_key, tx_iv,
                               current_rx_secret, current_tx_secret,
                               secretlen) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  return 0;
}

int ngtcp2_crypto_generate_stateless_reset_token(uint8_t *token,
                                                 const uint8_t *secret,
                                                 size_t secretlen,
                                                 const ngtcp2_cid *cid) {
  static const uint8_t info[] = "stateless_reset";
  ngtcp2_crypto_md md;

  if (ngtcp2_crypto_hkdf(token, NGTCP2_STATELESS_RESET_TOKENLEN,
                         ngtcp2_crypto_md_sha256(&md), secret, secretlen,
                         cid->data, cid->datalen, info,
                         sizeof(info) - 1) != 0) {
    return -1;
  }

  return 0;
}

static int crypto_derive_token_key(uint8_t *key, size_t keylen, uint8_t *iv,
                                   size_t ivlen, const ngtcp2_crypto_md *md,
                                   const uint8_t *secret, size_t secretlen,
                                   const uint8_t *salt, size_t saltlen,
                                   const uint8_t *info_prefix,
                                   size_t info_prefixlen) {
  static const uint8_t key_info_suffix[] = " key";
  static const uint8_t iv_info_suffix[] = " iv";
  uint8_t intsecret[32];
  uint8_t info[32];
  uint8_t *p;

  assert(ngtcp2_crypto_md_hashlen(md) == sizeof(intsecret));
  assert(info_prefixlen + sizeof(key_info_suffix) - 1 <= sizeof(info));
  assert(info_prefixlen + sizeof(iv_info_suffix) - 1 <= sizeof(info));

  if (ngtcp2_crypto_hkdf_extract(intsecret, md, secret, secretlen, salt,
                                 saltlen) != 0) {
    return -1;
  }

  memcpy(info, info_prefix, info_prefixlen);
  p = info + info_prefixlen;

  memcpy(p, key_info_suffix, sizeof(key_info_suffix) - 1);
  p += sizeof(key_info_suffix) - 1;

  if (ngtcp2_crypto_hkdf_expand(key, keylen, md, intsecret, sizeof(intsecret),
                                info, (size_t)(p - info)) != 0) {
    return -1;
  }

  p = info + info_prefixlen;

  memcpy(p, iv_info_suffix, sizeof(iv_info_suffix) - 1);
  p += sizeof(iv_info_suffix) - 1;

  if (ngtcp2_crypto_hkdf_expand(iv, ivlen, md, intsecret, sizeof(intsecret),
                                info, (size_t)(p - info)) != 0) {
    return -1;
  }

  return 0;
}

static size_t crypto_generate_retry_token_aad(uint8_t *dest, uint32_t version,
                                              const ngtcp2_sockaddr *sa,
                                              ngtcp2_socklen salen,
                                              const ngtcp2_cid *retry_scid) {
  uint8_t *p = dest;

  version = ngtcp2_htonl(version);
  memcpy(p, &version, sizeof(version));
  p += sizeof(version);
  memcpy(p, sa, (size_t)salen);
  p += salen;
  memcpy(p, retry_scid->data, retry_scid->datalen);
  p += retry_scid->datalen;

  return (size_t)(p - dest);
}

static const uint8_t retry_token_info_prefix[] = "retry_token";

ngtcp2_ssize ngtcp2_crypto_generate_retry_token(
    uint8_t *token, const uint8_t *secret, size_t secretlen, uint32_t version,
    const ngtcp2_sockaddr *remote_addr, ngtcp2_socklen remote_addrlen,
    const ngtcp2_cid *retry_scid, const ngtcp2_cid *odcid, ngtcp2_tstamp ts) {
  uint8_t plaintext[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN];
  uint8_t rand_data[NGTCP2_CRYPTO_TOKEN_RAND_DATALEN];
  uint8_t key[32];
  uint8_t iv[32];
  size_t keylen;
  size_t ivlen;
  ngtcp2_crypto_aead aead;
  ngtcp2_crypto_md md;
  ngtcp2_crypto_aead_ctx aead_ctx;
  size_t plaintextlen;
  uint8_t
      aad[sizeof(version) + sizeof(ngtcp2_sockaddr_union) + NGTCP2_MAX_CIDLEN];
  size_t aadlen;
  uint8_t *p = plaintext;
  ngtcp2_tstamp ts_be = ngtcp2_htonl64(ts);
  int rv;

  assert((size_t)remote_addrlen <= sizeof(ngtcp2_sockaddr_union));

  memset(plaintext, 0, sizeof(plaintext));

  *p++ = (uint8_t)odcid->datalen;
  memcpy(p, odcid->data, odcid->datalen);
  p += NGTCP2_MAX_CIDLEN;
  memcpy(p, &ts_be, sizeof(ts_be));
  p += sizeof(ts_be);

  plaintextlen = (size_t)(p - plaintext);

  if (ngtcp2_crypto_random(rand_data, sizeof(rand_data)) != 0) {
    return -1;
  }

  ngtcp2_crypto_aead_aes_128_gcm(&aead);
  ngtcp2_crypto_md_sha256(&md);

  keylen = ngtcp2_crypto_aead_keylen(&aead);
  ivlen = ngtcp2_crypto_aead_noncelen(&aead);

  assert(sizeof(key) >= keylen);
  assert(sizeof(iv) >= ivlen);

  if (crypto_derive_token_key(key, keylen, iv, ivlen, &md, secret, secretlen,
                              rand_data, sizeof(rand_data),
                              retry_token_info_prefix,
                              sizeof(retry_token_info_prefix) - 1) != 0) {
    return -1;
  }

  aadlen = crypto_generate_retry_token_aad(aad, version, remote_addr,
                                           remote_addrlen, retry_scid);

  p = token;
  *p++ = NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY;

  if (ngtcp2_crypto_aead_ctx_encrypt_init(&aead_ctx, &aead, key, ivlen) != 0) {
    return -1;
  }

  rv = ngtcp2_crypto_encrypt(p, &aead, &aead_ctx, plaintext, plaintextlen, iv,
                             ivlen, aad, aadlen);

  ngtcp2_crypto_aead_ctx_free(&aead_ctx);

  if (rv != 0) {
    return -1;
  }

  p += plaintextlen + aead.max_overhead;
  memcpy(p, rand_data, sizeof(rand_data));
  p += sizeof(rand_data);

  return p - token;
}

int ngtcp2_crypto_verify_retry_token(
    ngtcp2_cid *odcid, const uint8_t *token, size_t tokenlen,
    const uint8_t *secret, size_t secretlen, uint32_t version,
    const ngtcp2_sockaddr *remote_addr, ngtcp2_socklen remote_addrlen,
    const ngtcp2_cid *dcid, ngtcp2_duration timeout, ngtcp2_tstamp ts) {
  uint8_t
      plaintext[/* cid len = */ 1 + NGTCP2_MAX_CIDLEN + sizeof(ngtcp2_tstamp)];
  uint8_t key[32];
  uint8_t iv[32];
  size_t keylen;
  size_t ivlen;
  ngtcp2_crypto_aead_ctx aead_ctx;
  ngtcp2_crypto_aead aead;
  ngtcp2_crypto_md md;
  uint8_t
      aad[sizeof(version) + sizeof(ngtcp2_sockaddr_union) + NGTCP2_MAX_CIDLEN];
  size_t aadlen;
  const uint8_t *rand_data;
  const uint8_t *ciphertext;
  size_t ciphertextlen;
  size_t cil;
  int rv;
  ngtcp2_tstamp gen_ts;

  assert((size_t)remote_addrlen <= sizeof(ngtcp2_sockaddr_union));

  if (tokenlen != NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN ||
      token[0] != NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY) {
    return -1;
  }

  rand_data = token + tokenlen - NGTCP2_CRYPTO_TOKEN_RAND_DATALEN;
  ciphertext = token + 1;
  ciphertextlen = tokenlen - 1 - NGTCP2_CRYPTO_TOKEN_RAND_DATALEN;

  ngtcp2_crypto_aead_aes_128_gcm(&aead);
  ngtcp2_crypto_md_sha256(&md);

  keylen = ngtcp2_crypto_aead_keylen(&aead);
  ivlen = ngtcp2_crypto_aead_noncelen(&aead);

  if (crypto_derive_token_key(key, keylen, iv, ivlen, &md, secret, secretlen,
                              rand_data, NGTCP2_CRYPTO_TOKEN_RAND_DATALEN,
                              retry_token_info_prefix,
                              sizeof(retry_token_info_prefix) - 1) != 0) {
    return -1;
  }

  aadlen = crypto_generate_retry_token_aad(aad, version, remote_addr,
                                           remote_addrlen, dcid);

  if (ngtcp2_crypto_aead_ctx_decrypt_init(&aead_ctx, &aead, key, ivlen) != 0) {
    return -1;
  }

  rv = ngtcp2_crypto_decrypt(plaintext, &aead, &aead_ctx, ciphertext,
                             ciphertextlen, iv, ivlen, aad, aadlen);

  ngtcp2_crypto_aead_ctx_free(&aead_ctx);

  if (rv != 0) {
    return -1;
  }

  cil = plaintext[0];

  if (cil != 0 && (cil < NGTCP2_MIN_CIDLEN || cil > NGTCP2_MAX_CIDLEN)) {
    return -1;
  }

  memcpy(&gen_ts, plaintext + /* cid len = */ 1 + NGTCP2_MAX_CIDLEN,
         sizeof(gen_ts));

  gen_ts = ngtcp2_ntohl64(gen_ts);
  if (gen_ts + timeout <= ts) {
    return -1;
  }

  ngtcp2_cid_init(odcid, plaintext + /* cid len = */ 1, cil);

  return 0;
}

static size_t crypto_generate_regular_token_aad(uint8_t *dest,
                                                const ngtcp2_sockaddr *sa) {
  const uint8_t *addr;
  size_t addrlen;

  switch (sa->sa_family) {
  case NGTCP2_AF_INET:
    addr = (const uint8_t *)&((const ngtcp2_sockaddr_in *)(void *)sa)->sin_addr;
    addrlen = sizeof(((const ngtcp2_sockaddr_in *)(void *)sa)->sin_addr);
    break;
  case NGTCP2_AF_INET6:
    addr =
        (const uint8_t *)&((const ngtcp2_sockaddr_in6 *)(void *)sa)->sin6_addr;
    addrlen = sizeof(((const ngtcp2_sockaddr_in6 *)(void *)sa)->sin6_addr);
    break;
  default:
    assert(0);
    abort();
  }

  memcpy(dest, addr, addrlen);

  return addrlen;
}

static const uint8_t regular_token_info_prefix[] = "regular_token";

ngtcp2_ssize ngtcp2_crypto_generate_regular_token(
    uint8_t *token, const uint8_t *secret, size_t secretlen,
    const ngtcp2_sockaddr *remote_addr, ngtcp2_socklen remote_addrlen,
    ngtcp2_tstamp ts) {
  uint8_t plaintext[sizeof(ngtcp2_tstamp)];
  uint8_t rand_data[NGTCP2_CRYPTO_TOKEN_RAND_DATALEN];
  uint8_t key[32];
  uint8_t iv[32];
  size_t keylen;
  size_t ivlen;
  ngtcp2_crypto_aead aead;
  ngtcp2_crypto_md md;
  ngtcp2_crypto_aead_ctx aead_ctx;
  size_t plaintextlen;
  uint8_t aad[sizeof(ngtcp2_sockaddr_in6)];
  size_t aadlen;
  uint8_t *p = plaintext;
  ngtcp2_tstamp ts_be = ngtcp2_htonl64(ts);
  int rv;
  (void)remote_addrlen;

  memcpy(p, &ts_be, sizeof(ts_be));
  p += sizeof(ts_be);

  plaintextlen = (size_t)(p - plaintext);

  if (ngtcp2_crypto_random(rand_data, sizeof(rand_data)) != 0) {
    return -1;
  }

  ngtcp2_crypto_aead_aes_128_gcm(&aead);
  ngtcp2_crypto_md_sha256(&md);

  keylen = ngtcp2_crypto_aead_keylen(&aead);
  ivlen = ngtcp2_crypto_aead_noncelen(&aead);

  assert(sizeof(key) >= keylen);
  assert(sizeof(iv) >= ivlen);

  if (crypto_derive_token_key(key, keylen, iv, ivlen, &md, secret, secretlen,
                              rand_data, sizeof(rand_data),
                              regular_token_info_prefix,
                              sizeof(regular_token_info_prefix) - 1) != 0) {
    return -1;
  }

  aadlen = crypto_generate_regular_token_aad(aad, remote_addr);

  p = token;
  *p++ = NGTCP2_CRYPTO_TOKEN_MAGIC_REGULAR;

  if (ngtcp2_crypto_aead_ctx_encrypt_init(&aead_ctx, &aead, key, ivlen) != 0) {
    return -1;
  }

  rv = ngtcp2_crypto_encrypt(p, &aead, &aead_ctx, plaintext, plaintextlen, iv,
                             ivlen, aad, aadlen);

  ngtcp2_crypto_aead_ctx_free(&aead_ctx);

  if (rv != 0) {
    return -1;
  }

  p += plaintextlen + aead.max_overhead;
  memcpy(p, rand_data, sizeof(rand_data));
  p += sizeof(rand_data);

  return p - token;
}

int ngtcp2_crypto_verify_regular_token(const uint8_t *token, size_t tokenlen,
                                       const uint8_t *secret, size_t secretlen,
                                       const ngtcp2_sockaddr *remote_addr,
                                       ngtcp2_socklen remote_addrlen,
                                       ngtcp2_duration timeout,
                                       ngtcp2_tstamp ts) {
  uint8_t plaintext[sizeof(ngtcp2_tstamp)];
  uint8_t key[32];
  uint8_t iv[32];
  size_t keylen;
  size_t ivlen;
  ngtcp2_crypto_aead_ctx aead_ctx;
  ngtcp2_crypto_aead aead;
  ngtcp2_crypto_md md;
  uint8_t aad[sizeof(ngtcp2_sockaddr_in6)];
  size_t aadlen;
  const uint8_t *rand_data;
  const uint8_t *ciphertext;
  size_t ciphertextlen;
  int rv;
  ngtcp2_tstamp gen_ts;
  (void)remote_addrlen;

  if (tokenlen != NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN ||
      token[0] != NGTCP2_CRYPTO_TOKEN_MAGIC_REGULAR) {
    return -1;
  }

  rand_data = token + tokenlen - NGTCP2_CRYPTO_TOKEN_RAND_DATALEN;
  ciphertext = token + 1;
  ciphertextlen = tokenlen - 1 - NGTCP2_CRYPTO_TOKEN_RAND_DATALEN;

  ngtcp2_crypto_aead_aes_128_gcm(&aead);
  ngtcp2_crypto_md_sha256(&md);

  keylen = ngtcp2_crypto_aead_keylen(&aead);
  ivlen = ngtcp2_crypto_aead_noncelen(&aead);

  if (crypto_derive_token_key(key, keylen, iv, ivlen, &md, secret, secretlen,
                              rand_data, NGTCP2_CRYPTO_TOKEN_RAND_DATALEN,
                              regular_token_info_prefix,
                              sizeof(regular_token_info_prefix) - 1) != 0) {
    return -1;
  }

  aadlen = crypto_generate_regular_token_aad(aad, remote_addr);

  if (ngtcp2_crypto_aead_ctx_decrypt_init(&aead_ctx, &aead, key, ivlen) != 0) {
    return -1;
  }

  rv = ngtcp2_crypto_decrypt(plaintext, &aead, &aead_ctx, ciphertext,
                             ciphertextlen, iv, ivlen, aad, aadlen);

  ngtcp2_crypto_aead_ctx_free(&aead_ctx);

  if (rv != 0) {
    return -1;
  }

  memcpy(&gen_ts, plaintext, sizeof(gen_ts));

  gen_ts = ngtcp2_ntohl64(gen_ts);
  if (gen_ts + timeout <= ts) {
    return -1;
  }

  return 0;
}

ngtcp2_ssize ngtcp2_crypto_write_connection_close(
    uint8_t *dest, size_t destlen, uint32_t version, const ngtcp2_cid *dcid,
    const ngtcp2_cid *scid, uint64_t error_code, const uint8_t *reason,
    size_t reasonlen) {
  uint8_t rx_secret[NGTCP2_CRYPTO_INITIAL_SECRETLEN];
  uint8_t tx_secret[NGTCP2_CRYPTO_INITIAL_SECRETLEN];
  uint8_t initial_secret[NGTCP2_CRYPTO_INITIAL_SECRETLEN];
  uint8_t tx_key[NGTCP2_CRYPTO_INITIAL_KEYLEN];
  uint8_t tx_iv[NGTCP2_CRYPTO_INITIAL_IVLEN];
  uint8_t tx_hp_key[NGTCP2_CRYPTO_INITIAL_KEYLEN];
  ngtcp2_crypto_ctx ctx;
  ngtcp2_ssize spktlen;
  ngtcp2_crypto_aead_ctx aead_ctx = {0};
  ngtcp2_crypto_cipher_ctx hp_ctx = {0};

  ngtcp2_crypto_ctx_initial(&ctx);

  if (ngtcp2_crypto_derive_initial_secrets(rx_secret, tx_secret, initial_secret,
                                           version, scid,
                                           NGTCP2_CRYPTO_SIDE_SERVER) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_derive_packet_protection_key(
          tx_key, tx_iv, tx_hp_key, version, &ctx.aead, &ctx.md, tx_secret,
          NGTCP2_CRYPTO_INITIAL_SECRETLEN) != 0) {
    return -1;
  }

  if (ngtcp2_crypto_aead_ctx_encrypt_init(&aead_ctx, &ctx.aead, tx_key,
                                          NGTCP2_CRYPTO_INITIAL_IVLEN) != 0) {
    spktlen = -1;
    goto end;
  }

  if (ngtcp2_crypto_cipher_ctx_encrypt_init(&hp_ctx, &ctx.hp, tx_hp_key) != 0) {
    spktlen = -1;
    goto end;
  }

  spktlen = ngtcp2_pkt_write_connection_close(
      dest, destlen, version, dcid, scid, error_code, reason, reasonlen,
      ngtcp2_crypto_encrypt_cb, &ctx.aead, &aead_ctx, tx_iv,
      ngtcp2_crypto_hp_mask_cb, &ctx.hp, &hp_ctx);
  if (spktlen < 0) {
    spktlen = -1;
  }

end:
  ngtcp2_crypto_cipher_ctx_free(&hp_ctx);
  ngtcp2_crypto_aead_ctx_free(&aead_ctx);

  return spktlen;
}

ngtcp2_ssize ngtcp2_crypto_write_retry(uint8_t *dest, size_t destlen,
                                       uint32_t version, const ngtcp2_cid *dcid,
                                       const ngtcp2_cid *scid,
                                       const ngtcp2_cid *odcid,
                                       const uint8_t *token, size_t tokenlen) {
  ngtcp2_crypto_aead aead;
  ngtcp2_ssize spktlen;
  ngtcp2_crypto_aead_ctx aead_ctx = {0};
  const uint8_t *key;
  size_t noncelen;

  ngtcp2_crypto_aead_retry(&aead);

  switch (version) {
  case NGTCP2_PROTO_VER_V1:
  default:
    key = (const uint8_t *)NGTCP2_RETRY_KEY_V1;
    noncelen = sizeof(NGTCP2_RETRY_NONCE_V1) - 1;
    break;
  case NGTCP2_PROTO_VER_V2:
    key = (const uint8_t *)NGTCP2_RETRY_KEY_V2;
    noncelen = sizeof(NGTCP2_RETRY_NONCE_V2) - 1;
    break;
  }

  if (ngtcp2_crypto_aead_ctx_encrypt_init(&aead_ctx, &aead, key, noncelen) !=
      0) {
    return -1;
  }

  spktlen = ngtcp2_pkt_write_retry(dest, destlen, version, dcid, scid, odcid,
                                   token, tokenlen, ngtcp2_crypto_encrypt_cb,
                                   &aead, &aead_ctx);
  if (spktlen < 0) {
    spktlen = -1;
  }

  ngtcp2_crypto_aead_ctx_free(&aead_ctx);

  return spktlen;
}

int ngtcp2_crypto_client_initial_cb(ngtcp2_conn *conn, void *user_data) {
  const ngtcp2_cid *dcid = ngtcp2_conn_get_dcid(conn);
  void *tls = ngtcp2_conn_get_tls_native_handle(conn);
  (void)user_data;

  if (ngtcp2_crypto_derive_and_install_initial_key(
          conn, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
          ngtcp2_conn_get_client_chosen_version(conn), dcid) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  if (crypto_set_local_transport_params(conn, tls) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  if (ngtcp2_crypto_read_write_crypto_data(
          conn, NGTCP2_ENCRYPTION_LEVEL_INITIAL, NULL, 0) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

int ngtcp2_crypto_recv_retry_cb(ngtcp2_conn *conn, const ngtcp2_pkt_hd *hd,
                                void *user_data) {
  (void)user_data;

  if (ngtcp2_crypto_derive_and_install_initial_key(
          conn, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
          ngtcp2_conn_get_client_chosen_version(conn), &hd->scid) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

int ngtcp2_crypto_recv_client_initial_cb(ngtcp2_conn *conn,
                                         const ngtcp2_cid *dcid,
                                         void *user_data) {
  (void)user_data;

  if (ngtcp2_crypto_derive_and_install_initial_key(
          conn, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
          ngtcp2_conn_get_client_chosen_version(conn), dcid) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

int ngtcp2_crypto_version_negotiation_cb(ngtcp2_conn *conn, uint32_t version,
                                         const ngtcp2_cid *client_dcid,
                                         void *user_data) {
  (void)user_data;

  if (ngtcp2_crypto_derive_and_install_vneg_initial_key(
          conn, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, version,
          client_dcid) != 0) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }

  return 0;
}

void ngtcp2_crypto_delete_crypto_aead_ctx_cb(ngtcp2_conn *conn,
                                             ngtcp2_crypto_aead_ctx *aead_ctx,
                                             void *user_data) {
  (void)conn;
  (void)user_data;

  ngtcp2_crypto_aead_ctx_free(aead_ctx);
}

void ngtcp2_crypto_delete_crypto_cipher_ctx_cb(
    ngtcp2_conn *conn, ngtcp2_crypto_cipher_ctx *cipher_ctx, void *user_data) {
  (void)conn;
  (void)user_data;

  ngtcp2_crypto_cipher_ctx_free(cipher_ctx);
}

int ngtcp2_crypto_recv_crypto_data_cb(ngtcp2_conn *conn,
                                      ngtcp2_encryption_level encryption_level,
                                      uint64_t offset, const uint8_t *data,
                                      size_t datalen, void *user_data) {
  int rv;
  (void)offset;
  (void)user_data;

  if (ngtcp2_crypto_read_write_crypto_data(conn, encryption_level, data,
                                           datalen) != 0) {
    rv = ngtcp2_conn_get_tls_error(conn);
    if (rv) {
      return rv;
    }
    return NGTCP2_ERR_CRYPTO;
  }

  return 0;
}
