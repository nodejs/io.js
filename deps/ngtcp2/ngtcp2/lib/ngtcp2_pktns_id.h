/*
 * ngtcp2
 *
 * Copyright (c) 2023 ngtcp2 contributors
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
#ifndef NGTCP2_PKTNS_ID_H
#define NGTCP2_PKTNS_ID_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

/**
 * @enum
 *
 * :type:`ngtcp2_pktns_id` defines packet number space identifier.
 */
typedef enum ngtcp2_pktns_id {
  /**
   * :enum:`NGTCP2_PKTNS_ID_INITIAL` is the Initial packet number
   * space.
   */
  NGTCP2_PKTNS_ID_INITIAL,
  /**
   * :enum:`NGTCP2_PKTNS_ID_HANDSHAKE` is the Handshake packet number
   * space.
   */
  NGTCP2_PKTNS_ID_HANDSHAKE,
  /**
   * :enum:`NGTCP2_PKTNS_ID_APPLICATION` is the Application data
   * packet number space.
   */
  NGTCP2_PKTNS_ID_APPLICATION,
  /**
   * :enum:`NGTCP2_PKTNS_ID_MAX` is defined to get the number of
   * packet number spaces.
   */
  NGTCP2_PKTNS_ID_MAX
} ngtcp2_pktns_id;

#endif /* NGTCP2_PKTNS_ID_H */
