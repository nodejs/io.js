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
#ifndef NGTCP2_CRYPTO_PICOTLS_H
#define NGTCP2_CRYPTO_PICOTLS_H

#include <ngtcp2/ngtcp2.h>

#include <picotls.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct
 *
 * :type:`ngtcp2_crypto_picotls_ctx` contains per-connection state of
 * Picotls objects and must be an object to bet set to
 * `ngtcp2_conn_set_tls_native_handle`.
 */
typedef struct ngtcp2_crypto_picotls_ctx {
  /**
   * :member:`ptls` is a pointer to ptls_t object.
   */
  ptls_t *ptls;
  /**
   * :member:`handshake_properties` is a set of configurations used
   * during this particular TLS handshake.
   */
  ptls_handshake_properties_t handshake_properties;
} ngtcp2_crypto_picotls_ctx;

/**
 * @function
 *
 * `ngtcp2_crypto_picotls_ctx_init` initializes the object pointed by
 * |cptls|.  |cptls| must not be NULL.
 */
NGTCP2_EXTERN void
ngtcp2_crypto_picotls_ctx_init(ngtcp2_crypto_picotls_ctx *cptls);

/**
 * @function
 *
 * `ngtcp2_crypto_picotls_from_epoch` translates |epoch| to
 * :type:`ngtcp2_encryption_level`.  This function is only available
 * for Picotls backend.
 */
NGTCP2_EXTERN ngtcp2_encryption_level
ngtcp2_crypto_picotls_from_epoch(size_t epoch);

/**
 * @function
 *
 * `ngtcp2_crypto_picotls_from_ngtcp2_encryption_level` translates
 * |encryption_level| to epoch.  This function is only available for
 * Picotls backend.
 */
NGTCP2_EXTERN size_t ngtcp2_crypto_picotls_from_ngtcp2_encryption_level(
    ngtcp2_encryption_level encryption_level);

/**
 * @function
 *
 * `ngtcp2_crypto_picotls_configure_server_context` configures |ctx|
 * for server side QUIC connection.  It performs the following
 * modifications:
 *
 * - Set max_early_data_size to UINT32_MAX.
 * - Set omit_end_of_early_data to 1.
 * - Set update_traffic_key callback.
 *
 * Application must set a pointer to :type:`ngtcp2_crypto_conn_ref` to
 * ptls_t object by assigning the pointer using ptls_get_data_ptr, and
 * :type:`ngtcp2_crypto_conn_ref` object must have
 * :member:`ngtcp2_crypto_conn_ref.get_conn` field assigned to get
 * :type:`ngtcp2_conn`.
 *
 * It returns 0 if it succeeds, or -1.
 */
NGTCP2_EXTERN int
ngtcp2_crypto_picotls_configure_server_context(ptls_context_t *ctx);

/**
 * @function
 *
 * `ngtcp2_crypto_picotls_configure_client_context` configures |ctx|
 * for client side QUIC connection.  It performs the following
 * modifications:
 *
 * - Set omit_end_of_early_data to 1.
 * - Set update_traffic_key callback.
 *
 * Application must set a pointer to :type:`ngtcp2_crypto_conn_ref` to
 * ptls_t object by assigning the pointer using ptls_get_data_ptr, and
 * :type:`ngtcp2_crypto_conn_ref` object must have
 * :member:`ngtcp2_crypto_conn_ref.get_conn` field assigned to get
 * :type:`ngtcp2_conn`.
 *
 * It returns 0 if it succeeds, or -1.
 */
NGTCP2_EXTERN int
ngtcp2_crypto_picotls_configure_client_context(ptls_context_t *ctx);

/**
 * @function
 *
 * `ngtcp2_crypto_picotls_configure_server_session` configures |cptls|
 * for server side QUIC connection.  It performs the following
 * modifications:
 *
 * - Set handshake_properties.collect_extension to
 *   `ngtcp2_crypto_picotls_collect_extension`.
 * - Set handshake_properties.collected_extensions to
 *   `ngtcp2_crypto_picotls_collected_extensions`.
 *
 * The callbacks set by this function only handle QUIC Transport
 * Parameters TLS extension.  If an application needs to handle the
 * other TLS extensions, set its own callbacks and call
 * `ngtcp2_crypto_picotls_collect_extension` and
 * `ngtcp2_crypto_picotls_collected_extensions` form them.
 *
 * During the QUIC handshake, the first element of
 * handshake_properties.additional_extensions is assigned to send QUIC
 * Transport Parameter TLS extension.  Therefore, an application must
 * allocate at least 2 elements for
 * handshake_properties.additional_extensions.
 *
 * Call `ngtcp2_crypto_picotls_deconfigure_session` to free up the
 * resources.
 *
 * Application must set a pointer to :type:`ngtcp2_crypto_conn_ref` to
 * ptls_t object by assigning the pointer using ptls_get_data_ptr, and
 * :type:`ngtcp2_crypto_conn_ref` object must have
 * :member:`ngtcp2_crypto_conn_ref.get_conn` field assigned to get
 * :type:`ngtcp2_conn`.
 *
 * It returns 0 if it succeeds, or -1.
 */
NGTCP2_EXTERN int ngtcp2_crypto_picotls_configure_server_session(
    ngtcp2_crypto_picotls_ctx *cptls);

/**
 * @function
 *
 * `ngtcp2_crypto_picotls_configure_client_session` configures |cptls|
 * for client side QUIC connection.  It performs the following
 * modifications:
 *
 * - Set handshake_properties.max_early_data_size to a pointer to
 *   uint32_t, which is allocated dynamically by this function.
 * - Set handshake_properties.collect_extension to
 *   `ngtcp2_crypto_picotls_collect_extension`.
 * - Set handshake_properties.collected_extensions to
 *   `ngtcp2_crypto_picotls_collected_extensions`.
 * - Set handshake_properties.additional_extensions[0].data to the
 *   dynamically allocated buffer which contains QUIC Transport
 *   Parameters TLS extension.  An application must allocate at least
 *   2 elements for handshake_properties.additional_extensions.
 *
 * The callbacks set by this function only handle QUIC Transport
 * Parameters TLS extension.  If an application needs to handle the
 * other TLS extensions, set its own callbacks and call
 * `ngtcp2_crypto_picotls_collect_extension` and
 * `ngtcp2_crypto_picotls_collected_extensions` form them.
 *
 * Call `ngtcp2_crypto_picotls_deconfigure_session` to free up the
 * resources.
 *
 * Application must set a pointer to :type:`ngtcp2_crypto_conn_ref` to
 * ptls_t object by assigning the pointer using ptls_get_data_ptr, and
 * :type:`ngtcp2_crypto_conn_ref` object must have
 * :member:`ngtcp2_crypto_conn_ref.get_conn` field assigned to get
 * :type:`ngtcp2_conn`.
 *
 * It returns 0 if it succeeds, or -1.
 */
NGTCP2_EXTERN int
ngtcp2_crypto_picotls_configure_client_session(ngtcp2_crypto_picotls_ctx *cptls,
                                               ngtcp2_conn *conn);

/**
 * @function
 *
 * `ngtcp2_crypto_picotls_deconfigure_session` frees the resources
 * allocated for |cptls| during QUIC connection.  It frees the
 * following data using :manpage:`free(3)`:
 *
 * - handshake_properties.max_early_data_size
 * - handshake_properties.additional_extensions[0].data.base
 *
 * If |cptls| is NULL, this function does nothing.
 */
NGTCP2_EXTERN void
ngtcp2_crypto_picotls_deconfigure_session(ngtcp2_crypto_picotls_ctx *cptls);

/**
 * @function
 *
 * `ngtcp2_crypto_picotls_collect_extension` is a callback function
 * which only returns nonzero if |type| ==
 * :macro:`NGTCP2_TLSEXT_QUIC_TRANSPORT_PARAMETERS_V1`.
 */
NGTCP2_EXTERN int ngtcp2_crypto_picotls_collect_extension(
    ptls_t *ptls, struct st_ptls_handshake_properties_t *properties,
    uint16_t type);

/**
 * @function
 *
 * `ngtcp2_crypto_picotls_collected_extensions` is a callback function
 * which only handles the extension of type
 * :macro:`NGTCP2_TLSEXT_QUIC_TRANSPORT_PARAMETERS_V1`.  The other
 * extensions are ignored.
 */
NGTCP2_EXTERN int ngtcp2_crypto_picotls_collected_extensions(
    ptls_t *ptls, struct st_ptls_handshake_properties_t *properties,
    ptls_raw_extension_t *extensions);

#ifdef __cplusplus
}
#endif

#endif /* NGTCP2_CRYPTO_PICOTLS_H */
