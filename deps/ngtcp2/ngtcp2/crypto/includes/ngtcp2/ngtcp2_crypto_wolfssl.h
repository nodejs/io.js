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
#ifndef NGTCP2_CRYPTO_WOLFSSL_H
#define NGTCP2_CRYPTO_WOLFSSL_H

#include <ngtcp2/ngtcp2.h>

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <wolfssl/quic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @function
 *
 * `ngtcp2_crypto_wolfssl_from_wolfssl_encryption_level` translates
 * |wolfssl_level| to :type:`ngtcp2_encryption_level`.  This function
 * is only available for wolfSSL backend.
 */
NGTCP2_EXTERN ngtcp2_encryption_level
ngtcp2_crypto_wolfssl_from_wolfssl_encryption_level(
    WOLFSSL_ENCRYPTION_LEVEL wolfssl_level);

/**
 * @function
 *
 * `ngtcp2_crypto_wolfssl_from_ngtcp2_encryption_level` translates
 * |encryption_level| to WOLFSSL_ENCRYPTION_LEVEL.  This function is
 * only available for wolfSSL backend.
 */
NGTCP2_EXTERN WOLFSSL_ENCRYPTION_LEVEL
ngtcp2_crypto_wolfssl_from_ngtcp2_encryption_level(
    ngtcp2_encryption_level encryption_level);

/**
 * @function
 *
 * `ngtcp2_crypto_wolfssl_configure_server_context` configures
 * |ssl_ctx| for server side QUIC connection.  It performs the
 * following modifications:
 *
 * - Set minimum and maximum TLS version to TLSv1.3.
 * - Set WOLFSSL_QUIC_METHOD by calling wolfSSL_CTX_set_quic_method.
 *
 * Application must set a pointer to :type:`ngtcp2_crypto_conn_ref` to
 * WOLFSSL object by calling wolfSSL_set_app_data, and
 * :type:`ngtcp2_crypto_conn_ref` object must have
 * :member:`ngtcp2_crypto_conn_ref.get_conn` field assigned to get
 * :type:`ngtcp2_conn`.
 *
 * It returns 0 if it succeeds, or -1.
 */
NGTCP2_EXTERN int
ngtcp2_crypto_wolfssl_configure_server_context(WOLFSSL_CTX *ssl_ctx);

/**
 * @function
 *
 * `ngtcp2_crypto_wolfssl_configure_client_context` configures
 * |ssl_ctx| for client side QUIC connection.  It performs the
 * following modifications:
 *
 * - Set minimum and maximum TLS version to TLSv1.3.
 * - Set WOLFSSL_QUIC_METHOD by calling wolfSSL_CTX_set_quic_method.
 *
 * Application must set a pointer to :type:`ngtcp2_crypto_conn_ref` to
 * SSL object by calling wolfSSL_set_app_data, and
 * :type:`ngtcp2_crypto_conn_ref` object must have
 * :member:`ngtcp2_crypto_conn_ref.get_conn` field assigned to get
 * :type:`ngtcp2_conn`.
 *
 * It returns 0 if it succeeds, or -1.
 */
NGTCP2_EXTERN int
ngtcp2_crypto_wolfssl_configure_client_context(WOLFSSL_CTX *ssl_ctx);

#ifdef __cplusplus
}
#endif

#endif /* NGTCP2_CRYPTO_WOLFSSL_H */
