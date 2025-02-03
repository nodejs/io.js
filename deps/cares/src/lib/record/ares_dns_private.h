/* MIT License
 *
 * Copyright (c) 2023 Brad House
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
#ifndef __ARES_DNS_PRIVATE_H
#define __ARES_DNS_PRIVATE_H

ares_status_t        ares_dns_record_duplicate_ex(ares_dns_record_t      **dest,
                                                  const ares_dns_record_t *src);
ares_bool_t          ares_dns_rec_allow_name_comp(ares_dns_rec_type_t type);
ares_bool_t          ares_dns_opcode_isvalid(ares_dns_opcode_t opcode);
ares_bool_t          ares_dns_rcode_isvalid(ares_dns_rcode_t rcode);
ares_bool_t          ares_dns_flags_arevalid(unsigned short flags);
ares_bool_t          ares_dns_rec_type_isvalid(ares_dns_rec_type_t type,
                                               ares_bool_t         is_query);
ares_bool_t          ares_dns_class_isvalid(ares_dns_class_t    qclass,
                                            ares_dns_rec_type_t type,
                                            ares_bool_t         is_query);
ares_bool_t          ares_dns_section_isvalid(ares_dns_section_t sect);
ares_status_t        ares_dns_rr_set_str_own(ares_dns_rr_t    *dns_rr,
                                             ares_dns_rr_key_t key, char *val);
ares_status_t        ares_dns_rr_set_bin_own(ares_dns_rr_t    *dns_rr,
                                             ares_dns_rr_key_t key, unsigned char *val,
                                             size_t len);
ares_status_t        ares_dns_rr_set_abin_own(ares_dns_rr_t          *dns_rr,
                                              ares_dns_rr_key_t       key,
                                              ares_dns_multistring_t *strs);
ares_status_t        ares_dns_rr_set_opt_own(ares_dns_rr_t    *dns_rr,
                                             ares_dns_rr_key_t key, unsigned short opt,
                                             unsigned char *val, size_t val_len);
ares_status_t        ares_dns_record_rr_prealloc(ares_dns_record_t *dnsrec,
                                                 ares_dns_section_t sect, size_t cnt);
ares_dns_rr_t       *ares_dns_get_opt_rr(ares_dns_record_t *rec);
const ares_dns_rr_t *ares_dns_get_opt_rr_const(const ares_dns_record_t *rec);
void                 ares_dns_record_ttl_decrement(ares_dns_record_t *dnsrec,
                                                   unsigned int       ttl_decrement);

/* Same as ares_dns_write() but appends to an existing buffer object */
ares_status_t        ares_dns_write_buf(const ares_dns_record_t *dnsrec,
                                        ares_buf_t              *buf);

/* Same as ares_dns_write_buf(), but prepends a 16bit length */
ares_status_t        ares_dns_write_buf_tcp(const ares_dns_record_t *dnsrec,
                                            ares_buf_t              *buf);

/*! Create a DNS record object for a query. The arguments are the same as
 *  those for ares_create_query().
 *
 *  \param[out] dnsrec       DNS record object to create.
 *  \param[in]  name         NUL-terminated name for the query.
 *  \param[in]  dnsclass     Class for the query.
 *  \param[in]  type         Type for the query.
 *  \param[in]  id           Identifier for the query.
 *  \param[in]  flags        Flags for the query.
 *  \param[in]  max_udp_size Maximum size of a UDP packet for EDNS.
 *  \return ARES_SUCCESS on success, otherwise an error code.
 */
ares_status_t
  ares_dns_record_create_query(ares_dns_record_t **dnsrec, const char *name,
                               ares_dns_class_t    dnsclass,
                               ares_dns_rec_type_t type, unsigned short id,
                               ares_dns_flags_t flags, size_t max_udp_size);

/*! Convert the RCODE and ANCOUNT from a DNS query reply into a status code.
 *
 *  \param[in] rcode   The RCODE from the reply.
 *  \param[in] ancount The ANCOUNT from the reply.
 *  \return An appropriate status code.
 */
ares_status_t ares_dns_query_reply_tostatus(ares_dns_rcode_t rcode,
                                            size_t           ancount);

struct ares_dns_qd {
  char               *name;
  ares_dns_rec_type_t qtype;
  ares_dns_class_t    qclass;
};

typedef struct {
  struct in_addr addr;
} ares_dns_a_t;

typedef struct {
  char *nsdname;
} ares_dns_ns_t;

typedef struct {
  char *cname;
} ares_dns_cname_t;

typedef struct {
  char        *mname;
  char        *rname;
  unsigned int serial;
  unsigned int refresh;
  unsigned int retry;
  unsigned int expire;
  unsigned int minimum;
} ares_dns_soa_t;

typedef struct {
  char *dname;
} ares_dns_ptr_t;

typedef struct {
  char *cpu;
  char *os;
} ares_dns_hinfo_t;

typedef struct {
  unsigned short preference;
  char          *exchange;
} ares_dns_mx_t;

typedef struct {
  ares_dns_multistring_t *strs;
} ares_dns_txt_t;

typedef struct {
  unsigned short type_covered;
  unsigned char  algorithm;
  unsigned char  labels;
  unsigned int   original_ttl;
  unsigned int   expiration;
  unsigned int   inception;
  unsigned short key_tag;
  char          *signers_name;
  unsigned char *signature;
  size_t         signature_len;
} ares_dns_sig_t;

typedef struct {
  struct ares_in6_addr addr;
} ares_dns_aaaa_t;

typedef struct {
  unsigned short priority;
  unsigned short weight;
  unsigned short port;
  char          *target;
} ares_dns_srv_t;

typedef struct {
  unsigned short order;
  unsigned short preference;
  char          *flags;
  char          *services;
  char          *regexp;
  char          *replacement;
} ares_dns_naptr_t;

typedef struct {
  unsigned short opt;
  unsigned char *val;
  size_t         val_len;
} ares_dns_optval_t;

typedef struct {
  unsigned short udp_size; /*!< taken from class */
  unsigned char  version;  /*!< taken from bits 8-16 of ttl */
  unsigned short flags;    /*!< Flags, remaining 16 bits, though only
                            *   1 currently defined */
  ares_array_t  *options;  /*!< Type is ares_dns_optval_t */
} ares_dns_opt_t;

typedef struct {
  unsigned char  cert_usage;
  unsigned char  selector;
  unsigned char  match;
  unsigned char *data;
  size_t         data_len;
} ares_dns_tlsa_t;

typedef struct {
  unsigned short priority;
  char          *target;
  ares_array_t  *params; /*!< Type is ares_dns_optval_t */
} ares_dns_svcb_t;

typedef struct {
  unsigned short priority;
  unsigned short weight;
  char          *target;
} ares_dns_uri_t;

typedef struct {
  unsigned char  critical;
  char          *tag;
  unsigned char *value;
  size_t         value_len;
} ares_dns_caa_t;

/*! Raw, unparsed RR data */
typedef struct {
  unsigned short type;   /*!< Not ares_rec_type_t because it likely isn't one
                          *   of those values since it wasn't parsed */
  unsigned char *data;   /*!< Raw RR data */
  size_t         length; /*!< Length of raw RR data */
} ares_dns_raw_rr_t;

/*! DNS RR data structure */
struct ares_dns_rr {
  ares_dns_record_t  *parent;
  char               *name;
  ares_dns_rec_type_t type;
  ares_dns_class_t    rclass;
  unsigned int        ttl;

  union {
    ares_dns_a_t      a;
    ares_dns_ns_t     ns;
    ares_dns_cname_t  cname;
    ares_dns_soa_t    soa;
    ares_dns_ptr_t    ptr;
    ares_dns_hinfo_t  hinfo;
    ares_dns_mx_t     mx;
    ares_dns_txt_t    txt;
    ares_dns_sig_t    sig;
    ares_dns_aaaa_t   aaaa;
    ares_dns_srv_t    srv;
    ares_dns_naptr_t  naptr;
    ares_dns_opt_t    opt;
    ares_dns_tlsa_t   tlsa;
    ares_dns_svcb_t   svcb;
    ares_dns_svcb_t   https; /*!< https is a type of svcb, so this is right */
    ares_dns_uri_t    uri;
    ares_dns_caa_t    caa;
    ares_dns_raw_rr_t raw_rr;
  } r;
};

/*! DNS data structure */
struct ares_dns_record {
  unsigned short    id;            /*!< DNS query id */
  unsigned short    flags;         /*!< One or more ares_dns_flags_t */
  ares_dns_opcode_t opcode;        /*!< DNS Opcode */
  ares_dns_rcode_t  rcode;         /*!< DNS RCODE */
  unsigned short    raw_rcode;     /*!< Raw rcode, used to ultimately form real
                                    *   rcode after reading OPT record if it
                                    *   exists */
  unsigned int      ttl_decrement; /*!< Special case to apply to writing out
                                    *   this record, where it will decrement
                                    *   the ttl of any resource records by
                                    *   this amount.  Used for cache */

  ares_array_t     *qd;            /*!< Type is ares_dns_qd_t */
  ares_array_t     *an;            /*!< Type is ares_dns_rr_t */
  ares_array_t     *ns;            /*!< Type is ares_dns_rr_t */
  ares_array_t     *ar;            /*!< Type is ares_dns_rr_t */
};

#endif
