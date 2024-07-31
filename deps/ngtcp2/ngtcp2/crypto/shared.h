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
#ifndef NGTCP2_SHARED_H
#define NGTCP2_SHARED_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2_crypto.h>

/**
 * @macro
 *
 * :macro:`NGTCP2_INITIAL_SALT_V1` is a salt value which is used to
 * derive initial secret.  It is used for QUIC v1.
 */
#define NGTCP2_INITIAL_SALT_V1                                                 \
  "\x38\x76\x2c\xf7\xf5\x59\x34\xb3\x4d\x17\x9a\xe6\xa4\xc8\x0c\xad\xcc\xbb"   \
  "\x7f\x0a"

/**
 * @macro
 *
 * :macro:`NGTCP2_INITIAL_SALT_V2` is a salt value which is used to
 * derive initial secret.  It is used for QUIC v2.
 */
#define NGTCP2_INITIAL_SALT_V2                                                 \
  "\x0d\xed\xe3\xde\xf7\x00\xa6\xdb\x81\x93\x81\xbe\x6e\x26\x9d\xcb\xf9\xbd"   \
  "\x2e\xd9"

/* Maximum key usage (encryption) limits */
#define NGTCP2_CRYPTO_MAX_ENCRYPTION_AES_GCM (1ULL << 23)
#define NGTCP2_CRYPTO_MAX_ENCRYPTION_CHACHA20_POLY1305 (1ULL << 62)
#define NGTCP2_CRYPTO_MAX_ENCRYPTION_AES_CCM (2965820ULL)

/* Maximum authentication failure (decryption) limits during the
   lifetime of a connection. */
#define NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_AES_GCM (1ULL << 52)
#define NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_CHACHA20_POLY1305 (1ULL << 36)
#define NGTCP2_CRYPTO_MAX_DECRYPTION_FAILURE_AES_CCM (2965820ULL)

/**
 * @macro
 *
 * :macro:`NGTCP2_CRYPTO_INITIAL_SECRETLEN` is the length of secret
 * for Initial packets.
 */
#define NGTCP2_CRYPTO_INITIAL_SECRETLEN 32

/**
 * @macro
 *
 * :macro:`NGTCP2_CRYPTO_INITIAL_KEYLEN` is the length of key for
 * Initial packets.
 */
#define NGTCP2_CRYPTO_INITIAL_KEYLEN 16

/**
 * @macro
 *
 * :macro:`NGTCP2_CRYPTO_INITIAL_IVLEN` is the length of IV for
 * Initial packets.
 */
#define NGTCP2_CRYPTO_INITIAL_IVLEN 12

/**
 * @function
 *
 * `ngtcp2_crypto_ctx_initial` initializes |ctx| for Initial packet
 * encryption and decryption.
 */
ngtcp2_crypto_ctx *ngtcp2_crypto_ctx_initial(ngtcp2_crypto_ctx *ctx);

/**
 * @function
 *
 * `ngtcp2_crypto_aead_init` initializes |aead| with the provided
 * |aead_native_handle| which is an underlying AEAD object.
 *
 * If libngtcp2_crypto_quictls is linked, |aead_native_handle| must be
 * a pointer to EVP_CIPHER.
 *
 * If libngtcp2_crypto_gnutls is linked, |aead_native_handle| must be
 * gnutls_cipher_algorithm_t casted to ``void *``.
 *
 * If libngtcp2_crypto_boringssl is linked, |aead_native_handle| must
 * be a pointer to EVP_AEAD.
 */
ngtcp2_crypto_aead *ngtcp2_crypto_aead_init(ngtcp2_crypto_aead *aead,
                                            void *aead_native_handle);

/**
 * @function
 *
 * `ngtcp2_crypto_aead_retry` initializes |aead| with the AEAD cipher
 * AEAD_AES_128_GCM for Retry packet integrity protection.
 */
ngtcp2_crypto_aead *ngtcp2_crypto_aead_retry(ngtcp2_crypto_aead *aead);

/**
 * @enum
 *
 * :type:`ngtcp2_crypto_side` indicates which side the application
 * implements; client or server.
 */
typedef enum ngtcp2_crypto_side {
  /**
   * :enum:`NGTCP2_CRYPTO_SIDE_CLIENT` indicates that the application
   * is client.
   */
  NGTCP2_CRYPTO_SIDE_CLIENT,
  /**
   * :enum:`NGTCP2_CRYPTO_SIDE_SERVER` indicates that the application
   * is server.
   */
  NGTCP2_CRYPTO_SIDE_SERVER
} ngtcp2_crypto_side;

/**
 * @function
 *
 * `ngtcp2_crypto_derive_initial_secrets` derives initial secrets.
 * |rx_secret| and |tx_secret| must point to the buffer of at least 32
 * bytes capacity.  rx for read and tx for write.  This function
 * writes rx and tx secrets into |rx_secret| and |tx_secret|
 * respectively.  The length of secret is 32 bytes long.
 * |client_dcid| is the destination connection ID in first Initial
 * packet of client.  If |initial_secret| is not NULL, the initial
 * secret is written to it.  It must point to the buffer which has at
 * least 32 bytes capacity.  The initial secret is 32 bytes long.
 * |side| specifies the side of application.
 *
 * This function returns 0 if it succeeds, or -1.
 */
int ngtcp2_crypto_derive_initial_secrets(uint8_t *rx_secret, uint8_t *tx_secret,
                                         uint8_t *initial_secret,
                                         uint32_t version,
                                         const ngtcp2_cid *client_dcid,
                                         ngtcp2_crypto_side side);

/**
 * @function
 *
 * `ngtcp2_crypto_derive_packet_protection_key` derives packet
 * protection key.  This function writes packet protection key into
 * the buffer pointed by |key|.  The length of derived key is
 * `ngtcp2_crypto_aead_keylen(aead) <ngtcp2_crypto_aead_keylen>`
 * bytes.  |key| must have enough capacity to store the key.  This
 * function writes packet protection IV into |iv|.  The length of
 * derived IV is `ngtcp2_crypto_packet_protection_ivlen(aead)
 * <ngtcp2_crypto_packet_protection_ivlen>` bytes.  |iv| must have
 * enough capacity to store the IV.
 *
 * If |hp| is not NULL, this function also derives packet header
 * protection key and writes the key into the buffer pointed by |hp|.
 * The length of derived key is `ngtcp2_crypto_aead_keylen(aead)
 * <ngtcp2_crypto_aead_keylen>` bytes.  |hp|, if not NULL, must have
 * enough capacity to store the key.
 *
 * This function returns 0 if it succeeds, or -1.
 */
int ngtcp2_crypto_derive_packet_protection_key(uint8_t *key, uint8_t *iv,
                                               uint8_t *hp, uint32_t version,
                                               const ngtcp2_crypto_aead *aead,
                                               const ngtcp2_crypto_md *md,
                                               const uint8_t *secret,
                                               size_t secretlen);

/**
 * @function
 *
 * `ngtcp2_crypto_update_traffic_secret` derives the next generation
 * of the traffic secret.  |secret| specifies the current secret and
 * its length is given in |secretlen|.  The length of new key is the
 * same as the current key.  This function writes new key into the
 * buffer pointed by |dest|.  |dest| must have the enough capacity to
 * store the new key.
 *
 * This function returns 0 if it succeeds, or -1.
 */
int ngtcp2_crypto_update_traffic_secret(uint8_t *dest, uint32_t version,
                                        const ngtcp2_crypto_md *md,
                                        const uint8_t *secret,
                                        size_t secretlen);

/**
 * @function
 *
 * `ngtcp2_crypto_set_local_transport_params` sets QUIC transport
 * parameter, which is encoded in wire format and stored in the buffer
 * pointed by |buf| of length |len|, to the native handle |tls|.
 *
 * |tls| points to a implementation dependent TLS session object.  If
 * libngtcp2_crypto_quictls is linked, |tls| must be a pointer to SSL
 * object.
 *
 * This function returns 0 if it succeeds, or -1.
 */
int ngtcp2_crypto_set_local_transport_params(void *tls, const uint8_t *buf,
                                             size_t len);

/**
 * @function
 *
 * `ngtcp2_crypto_set_remote_transport_params` retrieves a remote QUIC
 * transport parameters from |tls| and sets it to |conn| using
 * `ngtcp2_conn_set_remote_transport_params`.
 *
 * |tls| points to a implementation dependent TLS session object.  If
 * libngtcp2_crypto_quictls is linked, |tls| must be a pointer to SSL
 * object.
 *
 * This function returns 0 if it succeeds, or -1.
 */
int ngtcp2_crypto_set_remote_transport_params(ngtcp2_conn *conn, void *tls);

/**
 * @function
 *
 * `ngtcp2_crypto_derive_and_install_initial_key` derives initial
 * keying materials and installs keys to |conn|.
 *
 * If |rx_secret| is not NULL, the secret for decryption is written to
 * the buffer pointed by |rx_secret|.  The length of secret is 32
 * bytes, and |rx_secret| must point to the buffer which has enough
 * capacity.
 *
 * If |tx_secret| is not NULL, the secret for encryption is written to
 * the buffer pointed by |tx_secret|.  The length of secret is 32
 * bytes, and |tx_secret| must point to the buffer which has enough
 * capacity.
 *
 * If |initial_secret| is not NULL, the initial secret is written to
 * the buffer pointed by |initial_secret|.  The length of secret is 32
 * bytes, and |initial_secret| must point to the buffer which has
 * enough capacity.
 *
 * |client_dcid| is the destination connection ID in first Initial
 * packet of client.
 *
 * If |rx_key| is not NULL, the derived packet protection key for
 * decryption is written to the buffer pointed by |rx_key|.  If
 * |rx_iv| is not NULL, the derived packet protection IV for
 * decryption is written to the buffer pointed by |rx_iv|.  If |rx_hp|
 * is not NULL, the derived header protection key for decryption is
 * written to the buffer pointed by |rx_hp|.
 *
 * If |tx_key| is not NULL, the derived packet protection key for
 * encryption is written to the buffer pointed by |tx_key|.  If
 * |tx_iv| is not NULL, the derived packet protection IV for
 * encryption is written to the buffer pointed by |tx_iv|.  If |tx_hp|
 * is not NULL, the derived header protection key for encryption is
 * written to the buffer pointed by |tx_hp|.
 *
 * The length of packet protection key and header protection key is 16
 * bytes long.  The length of packet protection IV is 12 bytes long.
 *
 * This function calls `ngtcp2_conn_set_initial_crypto_ctx` to set
 * initial AEAD and message digest algorithm.  After the successful
 * call of this function, application can use
 * `ngtcp2_conn_get_initial_crypto_ctx` to get the object.
 *
 * This function returns 0 if it succeeds, or -1.
 */
int ngtcp2_crypto_derive_and_install_initial_key(
    ngtcp2_conn *conn, uint8_t *rx_secret, uint8_t *tx_secret,
    uint8_t *initial_secret, uint8_t *rx_key, uint8_t *rx_iv, uint8_t *rx_hp,
    uint8_t *tx_key, uint8_t *tx_iv, uint8_t *tx_hp, uint32_t version,
    const ngtcp2_cid *client_dcid);

/**
 * @function
 *
 * `ngtcp2_crypto_derive_and_install_vneg_initial_key` derives initial
 * keying materials and installs keys to |conn|.  This function is
 * dedicated to install keys for |version| which is negotiated, or
 * being negotiated.
 *
 * If |rx_secret| is not NULL, the secret for decryption is written to
 * the buffer pointed by |rx_secret|.  The length of secret is 32
 * bytes, and |rx_secret| must point to the buffer which has enough
 * capacity.
 *
 * If |tx_secret| is not NULL, the secret for encryption is written to
 * the buffer pointed by |tx_secret|.  The length of secret is 32
 * bytes, and |tx_secret| must point to the buffer which has enough
 * capacity.
 *
 * If |initial_secret| is not NULL, the initial secret is written to
 * the buffer pointed by |initial_secret|.  The length of secret is 32
 * bytes, and |initial_secret| must point to the buffer which has
 * enough capacity.
 *
 * |client_dcid| is the destination connection ID in first Initial
 * packet of client.
 *
 * If |rx_key| is not NULL, the derived packet protection key for
 * decryption is written to the buffer pointed by |rx_key|.  If
 * |rx_iv| is not NULL, the derived packet protection IV for
 * decryption is written to the buffer pointed by |rx_iv|.  If |rx_hp|
 * is not NULL, the derived header protection key for decryption is
 * written to the buffer pointed by |rx_hp|.
 *
 * If |tx_key| is not NULL, the derived packet protection key for
 * encryption is written to the buffer pointed by |tx_key|.  If
 * |tx_iv| is not NULL, the derived packet protection IV for
 * encryption is written to the buffer pointed by |tx_iv|.  If |tx_hp|
 * is not NULL, the derived header protection key for encryption is
 * written to the buffer pointed by |tx_hp|.
 *
 * The length of packet protection key and header protection key is 16
 * bytes long.  The length of packet protection IV is 12 bytes long.
 *
 * This function returns 0 if it succeeds, or -1.
 */
int ngtcp2_crypto_derive_and_install_vneg_initial_key(
    ngtcp2_conn *conn, uint8_t *rx_secret, uint8_t *tx_secret,
    uint8_t *initial_secret, uint8_t *rx_key, uint8_t *rx_iv, uint8_t *rx_hp,
    uint8_t *tx_key, uint8_t *tx_iv, uint8_t *tx_hp, uint32_t version,
    const ngtcp2_cid *client_dcid);

/**
 * @function
 *
 * `ngtcp2_crypto_cipher_ctx_encrypt_init` initializes |cipher_ctx|
 * with new cipher context object for encryption which is constructed
 * to use |key| as encryption key.  |cipher| specifies cipher to use.
 *
 * This function returns 0 if it succeeds, or -1.
 */
int ngtcp2_crypto_cipher_ctx_encrypt_init(ngtcp2_crypto_cipher_ctx *cipher_ctx,
                                          const ngtcp2_crypto_cipher *cipher,
                                          const uint8_t *key);

/**
 * @function
 *
 * `ngtcp2_crypto_cipher_ctx_free` frees up resources used by
 * |cipher_ctx|.  This function does not free the memory pointed by
 * |cipher_ctx| itself.
 */
void ngtcp2_crypto_cipher_ctx_free(ngtcp2_crypto_cipher_ctx *cipher_ctx);

/*
 * `ngtcp2_crypto_md_sha256` initializes |md| with SHA256 message
 * digest algorithm and returns |md|.
 */
ngtcp2_crypto_md *ngtcp2_crypto_md_sha256(ngtcp2_crypto_md *md);

ngtcp2_crypto_aead *ngtcp2_crypto_aead_aes_128_gcm(ngtcp2_crypto_aead *aead);

/*
 * `ngtcp2_crypto_random` writes cryptographically-secure random
 * |datalen| bytes into the buffer pointed by |data|.
 *
 * This function returns 0 if it succeeds, or -1.
 */
int ngtcp2_crypto_random(uint8_t *data, size_t datalen);

/**
 * @function
 *
 * `ngtcp2_crypto_hkdf_expand_label` performs HKDF expand label.  The
 * result is |destlen| bytes long, and is stored to the buffer pointed
 * by |dest|.
 *
 * This function returns 0 if it succeeds, or -1.
 */
int ngtcp2_crypto_hkdf_expand_label(uint8_t *dest, size_t destlen,
                                    const ngtcp2_crypto_md *md,
                                    const uint8_t *secret, size_t secretlen,
                                    const uint8_t *label, size_t labellen);

#endif /* NGTCP2_SHARED_H */
