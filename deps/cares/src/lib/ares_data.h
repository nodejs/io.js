/* MIT License
 *
 * Copyright (c) 2009 Daniel Stenberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef __ARES_DATA_H
#define __ARES_DATA_H

typedef enum {
  ARES_DATATYPE_UNKNOWN = 1, /* unknown data type     - introduced in 1.7.0 */
  ARES_DATATYPE_SRV_REPLY,   /* struct ares_srv_reply - introduced in 1.7.0 */
  ARES_DATATYPE_TXT_REPLY,   /* struct ares_txt_reply - introduced in 1.7.0 */
  ARES_DATATYPE_TXT_EXT,     /* struct ares_txt_ext   - introduced in 1.11.0 */
  ARES_DATATYPE_ADDR_NODE,   /* struct ares_addr_node - introduced in 1.7.1 */
  ARES_DATATYPE_MX_REPLY,    /* struct ares_mx_reply   - introduced in 1.7.2 */
  ARES_DATATYPE_NAPTR_REPLY, /* struct ares_naptr_reply - introduced in 1.7.6 */
  ARES_DATATYPE_SOA_REPLY,   /* struct ares_soa_reply - introduced in 1.9.0 */
  ARES_DATATYPE_URI_REPLY,   /* struct ares_uri_reply */
#if 0
  ARES_DATATYPE_ADDR6TTL,     /* struct ares_addrttl   */
  ARES_DATATYPE_ADDRTTL,      /* struct ares_addr6ttl  */
  ARES_DATATYPE_HOSTENT,      /* struct hostent        */
  ARES_DATATYPE_OPTIONS,      /* struct ares_options   */
#endif
  ARES_DATATYPE_ADDR_PORT_NODE, /* struct ares_addr_port_node - introduced
                                   in 1.11.0 */
  ARES_DATATYPE_CAA_REPLY, /* struct ares_caa_reply   - introduced in 1.17 */
  ARES_DATATYPE_LAST       /* not used              - introduced in 1.7.0 */
} ares_datatype;

#define ARES_DATATYPE_MARK 0xbead

/*
 * ares_data struct definition is internal to c-ares and shall not
 * be exposed by the public API in order to allow future changes
 * and extensions to it without breaking ABI.  This will be used
 * internally by c-ares as the container of multiple types of data
 * dynamically allocated for which a reference will be returned
 * to the calling application.
 *
 * c-ares API functions returning a pointer to c-ares internally
 * allocated data will actually be returning an interior pointer
 * into this ares_data struct.
 *
 * All this is 'invisible' to the calling application, the only
 * requirement is that this kind of data must be free'ed by the
 * calling application using ares_free_data() with the pointer
 * it has received from a previous c-ares function call.
 */

struct ares_data {
  ares_datatype type; /* Actual data type identifier. */
  unsigned int  mark; /* Private ares_data signature. */

  union {
    struct ares_txt_reply      txt_reply;
    struct ares_txt_ext        txt_ext;
    struct ares_srv_reply      srv_reply;
    struct ares_addr_node      addr_node;
    struct ares_addr_port_node addr_port_node;
    struct ares_mx_reply       mx_reply;
    struct ares_naptr_reply    naptr_reply;
    struct ares_soa_reply      soa_reply;
    struct ares_caa_reply      caa_reply;
    struct ares_uri_reply      uri_reply;
  } data;
};

void *ares_malloc_data(ares_datatype type);


#endif /* __ARES_DATA_H */
