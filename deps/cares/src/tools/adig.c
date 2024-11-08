/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (c) The c-ares project and its contributors
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
#include "ares_setup.h"

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif
#ifdef HAVE_NETDB_H
#  include <netdb.h>
#endif

#include "ares_nameser.h"

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#include "ares.h"
#include "ares_array.h"
#include "ares_buf.h"
#include "ares_dns.h"
#include "ares_getopt.h"
#include "ares_mem.h"
#include "ares_str.h"

#include "limits.h"

#ifndef PATH_MAX
#  define PATH_MAX 1024
#endif

typedef struct {
  unsigned short port;
  size_t         tries;
  size_t         ndots;
  ares_bool_t    tcp;
  ares_bool_t    ignore_tc;
  char          *search;
  ares_bool_t    do_search;
  ares_bool_t    aa_flag;
  ares_bool_t    ad_flag;
  ares_bool_t    cd_flag;
  ares_bool_t    rd_flag;
  /* ares_bool_t do_flag; */
  ares_bool_t    edns;
  size_t         udp_size;
  ares_bool_t    primary;
  ares_bool_t    aliases;
  ares_bool_t    stayopen;
  ares_bool_t    dns0x20;
  ares_bool_t    display_class;
  ares_bool_t    display_ttl;
  ares_bool_t    display_command;
  ares_bool_t    display_stats;
  ares_bool_t    display_query;
  ares_bool_t    display_question;
  ares_bool_t    display_answer;
  ares_bool_t    display_authority;
  ares_bool_t    display_additional;
  ares_bool_t    display_comments;
} dns_options_t;

typedef struct {
  dns_options_t       opts;
  ares_bool_t         is_help;
  ares_bool_t         no_rcfile;
  struct ares_options options;
  int                 optmask;
  ares_dns_class_t    qclass;
  ares_dns_rec_type_t qtype;
  char               *name;
  char               *servers;
  char                error[256];
} adig_config_t;

static adig_config_t global_config;


static const char   *helpstr[] = {
  "usage: adig [@server] [-c class] [-p port#] [-q name] [-t type] [-x addr]",
  "       [name] [type] [class] [queryopt...]",
  "",
  "@server: server ip address.  May specify multiple in comma delimited "
    "format.",
  "         may be specified in URI format",
  "name:    name of the resource record that is to be looked up",
  "type:    what type of query is required.  e.g. - A, AAAA, MX, TXT, etc.  If",
  "         no specified, A will be used.",
  "class:   Sets the query class, defaults to IN.  May also be HS or CH.",
  "",
  "FLAGS",
  "-c class: Sets the query class, defaults to IN.  May also be HS or CH.",
  "-h:       Prints this help.",
  "-p port:  Sends query to a port other than 53.  Often recommended to set",
  "          the port using @server instead.",
  "-q name:  Specifies the domain name to query. Useful to distinguish name",
  "          from other arguments",
  "-r:       Skip adigrc processing",
  "-s:       Server (alias for @server syntax), compatibility with old cmdline",
  "-t type:  Indicates resource record type to query. Useful to distinguish",
  "          type from other arguments",
  "-x addr:  Simplified reverse lookups.  Sets the type to PTR and forms a",
  "          valid in-arpa query string",
  "",
  "QUERY OPTIONS",
  "+[no]aaonly:      Sets the aa flag in the query. Default is off.",
  "+[no]aaflag:      Alias for +[no]aaonly",
  "+[no]additional:  Toggles printing the additional section. On by default.",
  "+[no]adflag:      Sets the ad (authentic data) bit in the query. Default is",
  "                  off.",
  "+[no]aliases:     Whether or not to honor the HOSTALIASES file. Default is",
  "                  on.",
  "+[no]all:         Toggles all of +[no]cmd, +[no]stats, +[no]question,",
  "                  +[no]answer, +[no]authority, +[no]additional, "
    "+[no]comments",
  "+[no]answer:      Toggles printing the answer. On by default.",
  "+[no]authority:   Toggles printing the authority. On by default.",
  "+bufsize=#:       UDP EDNS 0 packet size allowed. Defaults to 1232.",
  "+[no]cdflag:      Sets the CD (checking disabled) bit in the query. Default",
  "                  is off.",
  "+[no]class:       Display the class when printing the record. On by "
    "default.",
  "+[no]cmd:         Toggles printing the command requested. On by default.",
  "+[no]comments:    Toggles printing the comments. On by default",
  "+[no]defname:     Alias for +[no]search",
  "+domain=somename: Sets the search list to a single domain.",
  "+[no]dns0x20:     Whether or not to use DNS 0x20 case randomization when",
  "                  sending queries.  Default is off.",
  "+[no]edns[=#]:    Enable or disable EDNS.  Only allows a value of 0 if",
  "                  specified. Default is to enable EDNS.",
  "+[no]ignore:      Ignore truncation on UDP, by default retried on TCP.",
  "+[no]keepopen:    Whether or not the server connection should be "
    "persistent.",
  "                  Default is off.",
  "+ndots=#:         Sets the number of dots that must appear before being",
  "                  considered absolute. Defaults to 1.",
  "+[no]primary:     Whether or not to only use a single server if more than "
    "one",
  "                  server is available.  Defaults to using all servers.",
  "+[no]qr:          Toggles printing the request query. Off by default.",
  "+[no]question:    Toggles printing the question. On by default.",
  "+[no]recurse:     Toggles the RD (Recursion Desired) bit. On by default.",
  "+retry=#:         Same as +tries but does not include the initial attempt.",
  "+[no]search:      To use or not use the search list. Search list is not "
    "used",
  "                  by default.",
  "+[no]stats:       Toggles printing the statistics. On by default.",
  "+[no]tcp:         Whether to use TCP when querying name servers. Default is",
  "                  UDP.",
  "+tries=#:         Number of query tries. Defaults to 3.",
  "+[no]ttlid:       Display the TTL when printing the record. On by default.",
  "+[no]vc:          Alias for +[no]tcp",
  "",
  NULL
};

static void free_config(void)
{
  free(global_config.servers);
  free(global_config.name);
  free(global_config.opts.search);
  memset(&global_config, 0, sizeof(global_config));
}

static void print_help(void)
{
  size_t i;
  printf("adig version %s\n\n", ares_version(NULL));
  for (i = 0; helpstr[i] != NULL; i++) {
    printf("%s\n", helpstr[i]);
  }
}

static void print_flags(ares_dns_flags_t flags)
{
  if (flags & ARES_FLAG_QR) {
    printf(" qr");
  }
  if (flags & ARES_FLAG_AA) {
    printf(" aa");
  }
  if (flags & ARES_FLAG_TC) {
    printf(" tc");
  }
  if (flags & ARES_FLAG_RD) {
    printf(" rd");
  }
  if (flags & ARES_FLAG_RA) {
    printf(" ra");
  }
  if (flags & ARES_FLAG_AD) {
    printf(" ad");
  }
  if (flags & ARES_FLAG_CD) {
    printf(" cd");
  }
}

static void print_header(const ares_dns_record_t *dnsrec)
{
  printf(";; ->>HEADER<<- opcode: %s, status: %s, id: %u\n",
         ares_dns_opcode_tostr(ares_dns_record_get_opcode(dnsrec)),
         ares_dns_rcode_tostr(ares_dns_record_get_rcode(dnsrec)),
         ares_dns_record_get_id(dnsrec));
  printf(";; flags:");
  print_flags(ares_dns_record_get_flags(dnsrec));
  printf("; QUERY: %u, ANSWER: %u, AUTHORITY: %u, ADDITIONAL: %u\n\n",
         (unsigned int)ares_dns_record_query_cnt(dnsrec),
         (unsigned int)ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ANSWER),
         (unsigned int)ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_AUTHORITY),
         (unsigned int)ares_dns_record_rr_cnt(dnsrec, ARES_SECTION_ADDITIONAL));
}

static void print_question(const ares_dns_record_t *dnsrec)
{
  size_t i;

  if (global_config.opts.display_comments) {
    printf(";; QUESTION SECTION:\n");
  }

  for (i = 0; i < ares_dns_record_query_cnt(dnsrec); i++) {
    const char         *name;
    ares_dns_rec_type_t qtype;
    ares_dns_class_t    qclass;
    size_t              len;
    if (ares_dns_record_query_get(dnsrec, i, &name, &qtype, &qclass) !=
        ARES_SUCCESS) {
      return;
    }
    if (name == NULL) {
      return;
    }
    len = strlen(name);
    printf(";%s.\t", name);
    if (len + 1 < 24) {
      printf("\t");
    }
    if (len + 1 < 16) {
      printf("\t");
    }

    if (global_config.opts.display_class) {
      printf("%s\t", ares_dns_class_tostr(qclass));
    }

    printf("%s\n", ares_dns_rec_type_tostr(qtype));
  }

  if (global_config.opts.display_comments) {
    printf("\n");
  }
}

static void print_opt_none(const unsigned char *val, size_t val_len)
{
  (void)val;
  if (val_len != 0) {
    printf("INVALID!");
  }
}

static void print_opt_addr_list(const unsigned char *val, size_t val_len)
{
  size_t i;
  if (val_len % 4 != 0) {
    printf("INVALID!");
    return;
  }
  for (i = 0; i < val_len; i += 4) {
    char buf[256] = "";
    ares_inet_ntop(AF_INET, val + i, buf, sizeof(buf));
    if (i != 0) {
      printf(",");
    }
    printf("%s", buf);
  }
}

static void print_opt_addr6_list(const unsigned char *val, size_t val_len)
{
  size_t i;
  if (val_len % 16 != 0) {
    printf("INVALID!");
    return;
  }
  for (i = 0; i < val_len; i += 16) {
    char buf[256] = "";

    ares_inet_ntop(AF_INET6, val + i, buf, sizeof(buf));
    if (i != 0) {
      printf(",");
    }
    printf("%s", buf);
  }
}

static void print_opt_u8_list(const unsigned char *val, size_t val_len)
{
  size_t i;

  for (i = 0; i < val_len; i++) {
    if (i != 0) {
      printf(",");
    }
    printf("%u", (unsigned int)val[i]);
  }
}

static void print_opt_u16_list(const unsigned char *val, size_t val_len)
{
  size_t i;
  if (val_len < 2 || val_len % 2 != 0) {
    printf("INVALID!");
    return;
  }
  for (i = 0; i < val_len; i += 2) {
    unsigned short u16 = 0;
    unsigned short c;
    /* Jumping over backwards to try to avoid odd compiler warnings */
    c    = (unsigned short)val[i];
    u16 |= (unsigned short)((c << 8) & 0xFFFF);
    c    = (unsigned short)val[i + 1];
    u16 |= c;
    if (i != 0) {
      printf(",");
    }
    printf("%u", (unsigned int)u16);
  }
}

static void print_opt_u32_list(const unsigned char *val, size_t val_len)
{
  size_t i;
  if (val_len < 4 || val_len % 4 != 0) {
    printf("INVALID!");
    return;
  }
  for (i = 0; i < val_len; i += 4) {
    unsigned int u32 = 0;

    u32 |= (unsigned int)(val[i] << 24);
    u32 |= (unsigned int)(val[i + 1] << 16);
    u32 |= (unsigned int)(val[i + 2] << 8);
    u32 |= (unsigned int)(val[i + 3]);
    if (i != 0) {
      printf(",");
    }
    printf("%u", u32);
  }
}

static void print_opt_str_list(const unsigned char *val, size_t val_len)
{
  size_t cnt = 0;

  printf("\"");
  while (val_len) {
    long           read_len = 0;
    unsigned char *str      = NULL;
    ares_status_t  status;

    if (cnt) {
      printf(",");
    }

    status = (ares_status_t)ares_expand_string(val, val, (int)val_len, &str,
                                               &read_len);
    if (status != ARES_SUCCESS) {
      printf("INVALID");
      break;
    }
    printf("%s", str);
    ares_free_string(str);
    val_len -= (size_t)read_len;
    val     += read_len;
    cnt++;
  }
  printf("\"");
}

static void print_opt_name(const unsigned char *val, size_t val_len)
{
  char *str      = NULL;
  long  read_len = 0;

  if (ares_expand_name(val, val, (int)val_len, &str, &read_len) !=
      ARES_SUCCESS) {
    printf("INVALID!");
    return;
  }

  printf("%s.", str);
  ares_free_string(str);
}

static void print_opt_bin(const unsigned char *val, size_t val_len)
{
  size_t i;

  for (i = 0; i < val_len; i++) {
    printf("%02x", (unsigned int)val[i]);
  }
}

static ares_bool_t adig_isprint(int ch)
{
  if (ch >= 0x20 && ch <= 0x7E) {
    return ARES_TRUE;
  }
  return ARES_FALSE;
}

static void print_opt_binp(const unsigned char *val, size_t val_len)
{
  size_t i;
  printf("\"");
  for (i = 0; i < val_len; i++) {
    if (adig_isprint(val[i])) {
      printf("%c", val[i]);
    } else {
      printf("\\%03d", val[i]);
    }
  }
  printf("\"");
}

static void print_opts(const ares_dns_rr_t *rr, ares_dns_rr_key_t key)
{
  size_t i;

  for (i = 0; i < ares_dns_rr_get_opt_cnt(rr, key); i++) {
    size_t               val_len = 0;
    const unsigned char *val     = NULL;
    unsigned short       opt;
    const char          *name;

    if (i != 0) {
      printf(" ");
    }

    opt  = ares_dns_rr_get_opt(rr, key, i, &val, &val_len);
    name = ares_dns_opt_get_name(key, opt);
    if (name == NULL) {
      printf("key%u", (unsigned int)opt);
    } else {
      printf("%s", name);
    }
    if (val_len == 0) {
      return;
    }

    printf("=");

    switch (ares_dns_opt_get_datatype(key, opt)) {
      case ARES_OPT_DATATYPE_NONE:
        print_opt_none(val, val_len);
        break;
      case ARES_OPT_DATATYPE_U8_LIST:
        print_opt_u8_list(val, val_len);
        break;
      case ARES_OPT_DATATYPE_INADDR4_LIST:
        print_opt_addr_list(val, val_len);
        break;
      case ARES_OPT_DATATYPE_INADDR6_LIST:
        print_opt_addr6_list(val, val_len);
        break;
      case ARES_OPT_DATATYPE_U16:
      case ARES_OPT_DATATYPE_U16_LIST:
        print_opt_u16_list(val, val_len);
        break;
      case ARES_OPT_DATATYPE_U32:
      case ARES_OPT_DATATYPE_U32_LIST:
        print_opt_u32_list(val, val_len);
        break;
      case ARES_OPT_DATATYPE_STR_LIST:
        print_opt_str_list(val, val_len);
        break;
      case ARES_OPT_DATATYPE_BIN:
        print_opt_bin(val, val_len);
        break;
      case ARES_OPT_DATATYPE_NAME:
        print_opt_name(val, val_len);
        break;
    }
  }
}

static void print_addr(const ares_dns_rr_t *rr, ares_dns_rr_key_t key)
{
  const struct in_addr *addr     = ares_dns_rr_get_addr(rr, key);
  char                  buf[256] = "";

  ares_inet_ntop(AF_INET, addr, buf, sizeof(buf));
  printf("%s", buf);
}

static void print_addr6(const ares_dns_rr_t *rr, ares_dns_rr_key_t key)
{
  const struct ares_in6_addr *addr     = ares_dns_rr_get_addr6(rr, key);
  char                        buf[256] = "";

  ares_inet_ntop(AF_INET6, addr, buf, sizeof(buf));
  printf("%s", buf);
}

static void print_u8(const ares_dns_rr_t *rr, ares_dns_rr_key_t key)
{
  unsigned char u8 = ares_dns_rr_get_u8(rr, key);
  printf("%u", (unsigned int)u8);
}

static void print_u16(const ares_dns_rr_t *rr, ares_dns_rr_key_t key)
{
  unsigned short u16 = ares_dns_rr_get_u16(rr, key);
  printf("%u", (unsigned int)u16);
}

static void print_u32(const ares_dns_rr_t *rr, ares_dns_rr_key_t key)
{
  unsigned int u32 = ares_dns_rr_get_u32(rr, key);
  printf("%u", u32);
}

static void print_name(const ares_dns_rr_t *rr, ares_dns_rr_key_t key)
{
  const char *str = ares_dns_rr_get_str(rr, key);
  printf("%s.", str);
}

static void print_str(const ares_dns_rr_t *rr, ares_dns_rr_key_t key)
{
  const char *str = ares_dns_rr_get_str(rr, key);
  printf("\"%s\"", str);
}

static void print_bin(const ares_dns_rr_t *rr, ares_dns_rr_key_t key)
{
  size_t               len  = 0;
  const unsigned char *binp = ares_dns_rr_get_bin(rr, key, &len);
  print_opt_bin(binp, len);
}

static void print_binp(const ares_dns_rr_t *rr, ares_dns_rr_key_t key)
{
  size_t               len;
  const unsigned char *binp = ares_dns_rr_get_bin(rr, key, &len);

  print_opt_binp(binp, len);
}

static void print_abinp(const ares_dns_rr_t *rr, ares_dns_rr_key_t key)
{
  size_t i;
  size_t cnt = ares_dns_rr_get_abin_cnt(rr, key);

  for (i = 0; i < cnt; i++) {
    size_t               len;
    const unsigned char *binp = ares_dns_rr_get_abin(rr, key, i, &len);
    if (i != 0) {
      printf(" ");
    }
    print_opt_binp(binp, len);
  }
}

static void print_rr(const ares_dns_rr_t *rr)
{
  const char              *name     = ares_dns_rr_get_name(rr);
  size_t                   len      = 0;
  size_t                   keys_cnt = 0;
  ares_dns_rec_type_t      rtype    = ares_dns_rr_get_type(rr);
  const ares_dns_rr_key_t *keys     = ares_dns_rr_get_keys(rtype, &keys_cnt);
  size_t                   i;

  if (name == NULL) {
    return;
  }

  len = strlen(name);

  printf("%s.\t", name);
  if (len < 24) {
    printf("\t");
  }

  if (global_config.opts.display_ttl) {
    printf("%u\t", ares_dns_rr_get_ttl(rr));
  }

  if (global_config.opts.display_class) {
    printf("%s\t", ares_dns_class_tostr(ares_dns_rr_get_class(rr)));
  }

  printf("%s\t", ares_dns_rec_type_tostr(rtype));

  /* Output params here */
  for (i = 0; i < keys_cnt; i++) {
    ares_dns_datatype_t datatype = ares_dns_rr_key_datatype(keys[i]);
    if (i != 0) {
      printf(" ");
    }

    switch (datatype) {
      case ARES_DATATYPE_INADDR:
        print_addr(rr, keys[i]);
        break;
      case ARES_DATATYPE_INADDR6:
        print_addr6(rr, keys[i]);
        break;
      case ARES_DATATYPE_U8:
        print_u8(rr, keys[i]);
        break;
      case ARES_DATATYPE_U16:
        print_u16(rr, keys[i]);
        break;
      case ARES_DATATYPE_U32:
        print_u32(rr, keys[i]);
        break;
      case ARES_DATATYPE_NAME:
        print_name(rr, keys[i]);
        break;
      case ARES_DATATYPE_STR:
        print_str(rr, keys[i]);
        break;
      case ARES_DATATYPE_BIN:
        print_bin(rr, keys[i]);
        break;
      case ARES_DATATYPE_BINP:
        print_binp(rr, keys[i]);
        break;
      case ARES_DATATYPE_ABINP:
        print_abinp(rr, keys[i]);
        break;
      case ARES_DATATYPE_OPT:
        print_opts(rr, keys[i]);
        break;
    }
  }

  printf("\n");
}

static const ares_dns_rr_t *has_opt(const ares_dns_record_t *dnsrec,
                                    ares_dns_section_t       section)
{
  size_t i;
  for (i = 0; i < ares_dns_record_rr_cnt(dnsrec, section); i++) {
    const ares_dns_rr_t *rr = ares_dns_record_rr_get_const(dnsrec, section, i);
    if (ares_dns_rr_get_type(rr) == ARES_REC_TYPE_OPT) {
      return rr;
    }
  }
  return NULL;
}

static void print_section(const ares_dns_record_t *dnsrec,
                          ares_dns_section_t       section)
{
  size_t i;

  if (ares_dns_record_rr_cnt(dnsrec, section) == 0 ||
      (ares_dns_record_rr_cnt(dnsrec, section) == 1 &&
       has_opt(dnsrec, section) != NULL)) {
    return;
  }

  if (global_config.opts.display_comments) {
    printf(";; %s SECTION:\n", ares_dns_section_tostr(section));
  }
  for (i = 0; i < ares_dns_record_rr_cnt(dnsrec, section); i++) {
    const ares_dns_rr_t *rr = ares_dns_record_rr_get_const(dnsrec, section, i);
    if (ares_dns_rr_get_type(rr) == ARES_REC_TYPE_OPT) {
      continue;
    }
    print_rr(rr);
  }
  if (global_config.opts.display_comments) {
    printf("\n");
  }
}

static void print_opt_psuedosection(const ares_dns_record_t *dnsrec)
{
  const ares_dns_rr_t *rr         = has_opt(dnsrec, ARES_SECTION_ADDITIONAL);
  const unsigned char *cookie     = NULL;
  size_t               cookie_len = 0;

  if (rr == NULL) {
    return;
  }

  if (!ares_dns_rr_get_opt_byid(rr, ARES_RR_OPT_OPTIONS, ARES_OPT_PARAM_COOKIE,
                                &cookie, &cookie_len)) {
    cookie = NULL;
  }

  printf(";; OPT PSEUDOSECTION:\n");
  printf("; EDNS: version: %u, flags: %u; udp: %u\n",
         (unsigned int)ares_dns_rr_get_u8(rr, ARES_RR_OPT_VERSION),
         (unsigned int)ares_dns_rr_get_u16(rr, ARES_RR_OPT_FLAGS),
         (unsigned int)ares_dns_rr_get_u16(rr, ARES_RR_OPT_UDP_SIZE));

  if (cookie) {
    printf("; COOKIE: ");
    print_opt_bin(cookie, cookie_len);
    printf(" (good)\n");
  }
}

static void print_record(const ares_dns_record_t *dnsrec)
{
  if (global_config.opts.display_comments) {
    print_header(dnsrec);
    print_opt_psuedosection(dnsrec);
  }

  if (global_config.opts.display_question) {
    print_question(dnsrec);
  }

  if (global_config.opts.display_answer) {
    print_section(dnsrec, ARES_SECTION_ANSWER);
  }

  if (global_config.opts.display_additional) {
    print_section(dnsrec, ARES_SECTION_ADDITIONAL);
  }

  if (global_config.opts.display_authority) {
    print_section(dnsrec, ARES_SECTION_AUTHORITY);
  }

  if (global_config.opts.display_stats) {
    unsigned char *abuf = NULL;
    size_t         alen = 0;
    ares_dns_write(dnsrec, &abuf, &alen);
    printf(";; MSG SIZE  rcvd: %d\n\n", (int)alen);
    ares_free_string(abuf);
  }
}

static void callback(void *arg, ares_status_t status, size_t timeouts,
                     const ares_dns_record_t *dnsrec)
{
  (void)arg;
  (void)timeouts;

  if (global_config.opts.display_comments) {
    /* We got a "Server status" */
    if (status >= ARES_SUCCESS && status <= ARES_EREFUSED) {
      printf(";; Got answer:");
    } else {
      printf(";;");
    }
    if (status != ARES_SUCCESS) {
      printf(" %s", ares_strerror((int)status));
    }
    printf("\n");
  }

  print_record(dnsrec);
}

static ares_status_t enqueue_query(ares_channel_t *channel)
{
  ares_dns_record_t *dnsrec = NULL;
  ares_dns_rr_t     *rr     = NULL;
  ares_status_t      status;
  unsigned short     flags    = 0;
  char              *nametemp = NULL;
  const char        *name     = global_config.name;

  if (global_config.opts.aa_flag) {
    flags |= ARES_FLAG_AA;
  }

  if (global_config.opts.ad_flag) {
    flags |= ARES_FLAG_AD;
  }

  if (global_config.opts.cd_flag) {
    flags |= ARES_FLAG_CD;
  }

  if (global_config.opts.rd_flag) {
    flags |= ARES_FLAG_RD;
  }

  status = ares_dns_record_create(&dnsrec, 0, flags, ARES_OPCODE_QUERY,
                                  ARES_RCODE_NOERROR);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* If it is a PTR record, convert from ip address into in-arpa form
   * automatically */
  if (global_config.qtype == ARES_REC_TYPE_PTR) {
    struct ares_addr addr;
    size_t           len;
    addr.family = AF_UNSPEC;

    if (ares_dns_pton(name, &addr, &len) != NULL) {
      nametemp = ares_dns_addr_to_ptr(&addr);
      name     = nametemp;
    }
  }

  status = ares_dns_record_query_add(dnsrec, name, global_config.qtype,
                                     global_config.qclass);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  if (global_config.opts.edns) {
    status = ares_dns_record_rr_add(&rr, dnsrec, ARES_SECTION_ADDITIONAL, "",
                                    ARES_REC_TYPE_OPT, ARES_CLASS_IN, 0);
    if (status != ARES_SUCCESS) {
      goto done;
    }
    ares_dns_rr_set_u16(rr, ARES_RR_OPT_UDP_SIZE,
                        (unsigned short)global_config.opts.udp_size);
    ares_dns_rr_set_u8(rr, ARES_RR_OPT_VERSION, 0);
  }

  if (global_config.opts.display_query) {
    printf(";; Sending:\n");
    print_record(dnsrec);
  }

  if (global_config.opts.do_search) {
    status = ares_search_dnsrec(channel, dnsrec, callback, NULL);
  } else {
    status = ares_send_dnsrec(channel, dnsrec, callback, NULL, NULL);
  }

done:
  ares_free_string(nametemp);
  ares_dns_record_destroy(dnsrec);
  return status;
}

static int event_loop(ares_channel_t *channel)
{
  while (1) {
    fd_set          read_fds;
    fd_set          write_fds;
    int             nfds;
    struct timeval  tv;
    struct timeval *tvp;
    int             count;

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    memset(&tv, 0, sizeof(tv));

    nfds = ares_fds(channel, &read_fds, &write_fds);
    if (nfds == 0) {
      break;
    }
    tvp = ares_timeout(channel, NULL, &tv);
    if (tvp == NULL) {
      break;
    }
    count = select(nfds, &read_fds, &write_fds, NULL, tvp);
    if (count < 0) {
#ifdef USE_WINSOCK
      int err = WSAGetLastError();
#else
      int err = errno;
#endif
      if (err != EAGAIN && err != EINTR) {
        fprintf(stderr, "select fail: %d", err);
        return 1;
      }
    }
    ares_process(channel, &read_fds, &write_fds);
  }
  return 0;
}

typedef enum {
  OPT_TYPE_BOOL,
  OPT_TYPE_STRING,
  OPT_TYPE_SIZE_T,
  OPT_TYPE_U16,
  OPT_TYPE_FUNC
} opt_type_t;

/* Callback called with OPT_TYPE_FUNC when processing options.
 * \param[in] prefix  prefix character for option
 * \param[in] name    name for option
 * \param[in] is_true ARES_TRUE unless option was prefixed with 'no'
 * \param[in] value   value for option
 * \return ARES_TRUE on success, ARES_FALSE on failure.  Should fill in
 *         global_config.error on error */
typedef ares_bool_t (*dig_opt_cb_t)(char prefix, const char *name,
                                    ares_bool_t is_true, const char *value);

static ares_bool_t opt_class_cb(char prefix, const char *name,
                                ares_bool_t is_true, const char *value)
{
  (void)prefix;
  (void)name;
  (void)is_true;

  if (!ares_dns_class_fromstr(&global_config.qclass, value)) {
    snprintf(global_config.error, sizeof(global_config.error),
             "unrecognized class %s", value);
    return ARES_FALSE;
  }

  return ARES_TRUE;
}

static ares_bool_t opt_type_cb(char prefix, const char *name,
                               ares_bool_t is_true, const char *value)
{
  (void)prefix;
  (void)name;
  (void)is_true;

  if (!ares_dns_rec_type_fromstr(&global_config.qtype, value)) {
    snprintf(global_config.error, sizeof(global_config.error),
             "unrecognized record type %s", value);
    return ARES_FALSE;
  }
  return ARES_TRUE;
}

static ares_bool_t opt_ptr_cb(char prefix, const char *name,
                              ares_bool_t is_true, const char *value)
{
  (void)prefix;
  (void)name;
  (void)is_true;
  global_config.qtype = ARES_REC_TYPE_PTR;
  ares_free(global_config.name);
  global_config.name = strdup(value);
  return ARES_TRUE;
}

static ares_bool_t opt_all_cb(char prefix, const char *name,
                              ares_bool_t is_true, const char *value)
{
  (void)prefix;
  (void)name;
  (void)value;

  global_config.opts.display_command    = is_true;
  global_config.opts.display_stats      = is_true;
  global_config.opts.display_question   = is_true;
  global_config.opts.display_answer     = is_true;
  global_config.opts.display_authority  = is_true;
  global_config.opts.display_additional = is_true;
  global_config.opts.display_comments   = is_true;
  return ARES_TRUE;
}

static ares_bool_t opt_edns_cb(char prefix, const char *name,
                               ares_bool_t is_true, const char *value)
{
  (void)prefix;
  (void)name;

  global_config.opts.edns = is_true;
  if (is_true && value != NULL && atoi(value) > 0) {
    snprintf(global_config.error, sizeof(global_config.error),
             "edns 0 only supported");
    return ARES_FALSE;
  }
  return ARES_TRUE;
}

static ares_bool_t opt_retry_cb(char prefix, const char *name,
                                ares_bool_t is_true, const char *value)
{
  (void)prefix;
  (void)name;
  (void)is_true;

  if (!ares_str_isnum(value)) {
    snprintf(global_config.error, sizeof(global_config.error),
             "value not numeric");
    return ARES_FALSE;
  }

  global_config.opts.tries = strtoul(value, NULL, 10) + 1;
  return ARES_TRUE;
}

static ares_bool_t opt_dig_bare_cb(char prefix, const char *name,
                                   ares_bool_t is_true, const char *value)
{
  (void)prefix;
  (void)name;
  (void)is_true;

  /* Handle @servers */
  if (*value == '@') {
    free(global_config.servers);
    global_config.servers = strdup(value + 1);
    return ARES_TRUE;
  }

  /* Make sure we don't pass options */
  if (*value == '-' || *value == '+') {
    snprintf(global_config.error, sizeof(global_config.error),
             "unrecognized argument %s", value);
    return ARES_FALSE;
  }

  /* See if it is a DNS class */
  if (ares_dns_class_fromstr(&global_config.qclass, value)) {
    return ARES_TRUE;
  }

  /* See if it is a DNS record type */
  if (ares_dns_rec_type_fromstr(&global_config.qtype, value)) {
    return ARES_TRUE;
  }

  /* See if it is a domain name */
  if (ares_is_hostname(value)) {
    free(global_config.name);
    global_config.name = strdup(value);
    return ARES_TRUE;
  }

  snprintf(global_config.error, sizeof(global_config.error),
           "unrecognized argument %s", value);
  return ARES_FALSE;
}

static const struct {
  /* Prefix for option.  If 0 then this param is a non-option and type must be
   * OPT_TYPE_FUNC where the entire value for the param will be passed */
  char         prefix;
  /* Name of option.  If null, there is none and the value is expected to be
   * immediately after the prefix character */
  const char  *name;
  /* Separator between key and value.  If 0 then uses the next argument as the
   * value, otherwise splits on the separator. BOOL types won't ever use a
   * separator and is ignored.*/
  char         separator;
  /* Type of parameter passed in.  If it is OPT_TYPE_FUNC, then it calls the
   * dig_opt_cb_t callback */
  opt_type_t   type;
  /* Pointer to argument to fill in */
  void        *opt;
  /* Callback if OPT_TYPE_FUNC */
  dig_opt_cb_t cb;
} dig_options[] = {
  /* -4 (ipv4 only) */
  /* -6 (ipv6 only) */
  /* { '-', "b",          0,   OPT_TYPE_FUNC,   NULL, opt_bind_address_cb },
   */
  { '-', "c",          0,   OPT_TYPE_FUNC,   NULL,                                   opt_class_cb    },
  /* -f file */
  { '-', "h",          0,   OPT_TYPE_BOOL,   &global_config.is_help,                 NULL            },
  /* -k keyfile */
  /* -m (memory usage debugging) */
  { '-', "p",          0,   OPT_TYPE_U16,    &global_config.opts.port,               NULL            },
  { '-', "q",          0,   OPT_TYPE_STRING, &global_config.name,                    NULL            },
  { '-', "r",          0,   OPT_TYPE_BOOL,   &global_config.no_rcfile,               NULL            },
  { '-', "s",          0,   OPT_TYPE_STRING, &global_config.servers,                 NULL            },
  { '-', "t",          0,   OPT_TYPE_FUNC,   NULL,                                   opt_type_cb     },
  /* -u (print microseconds instead of milliseconds) */
  { '-', "x",          0,   OPT_TYPE_FUNC,   NULL,                                   opt_ptr_cb      },
  /* -y [hmac:]keynam:secret */
  { '+', "aaflag",     0,   OPT_TYPE_BOOL,   &global_config.opts.aa_flag,            NULL            },
  { '+', "aaonly",     0,   OPT_TYPE_BOOL,   &global_config.opts.aa_flag,            NULL            },
  { '+', "additional", 0,   OPT_TYPE_BOOL,   &global_config.opts.display_additional,
   NULL                                                                                              },
  { '+', "adflag",     0,   OPT_TYPE_BOOL,   &global_config.opts.ad_flag,            NULL            },
  { '+', "aliases",    0,   OPT_TYPE_BOOL,   &global_config.opts.aliases,            NULL            },
  { '+', "all",        '=', OPT_TYPE_FUNC,   NULL,                                   opt_all_cb      },
  { '+', "answer",     0,   OPT_TYPE_BOOL,   &global_config.opts.display_answer,     NULL            },
  { '+', "authority",  0,   OPT_TYPE_BOOL,   &global_config.opts.display_authority,
   NULL                                                                                              },
  { '+', "bufsize",    '=', OPT_TYPE_SIZE_T, &global_config.opts.udp_size,           NULL            },
  { '+', "cdflag",     0,   OPT_TYPE_BOOL,   &global_config.opts.cd_flag,            NULL            },
  { '+', "class",      0,   OPT_TYPE_BOOL,   &global_config.opts.display_class,      NULL            },
  { '+', "cmd",        0,   OPT_TYPE_BOOL,   &global_config.opts.display_command,    NULL            },
  { '+', "comments",   0,   OPT_TYPE_BOOL,   &global_config.opts.display_comments,
   NULL                                                                                              },
  { '+', "defname",    0,   OPT_TYPE_BOOL,   &global_config.opts.do_search,          NULL            },
  { '+', "dns0x20",    0,   OPT_TYPE_BOOL,   &global_config.opts.dns0x20,            NULL            },
  { '+', "domain",     '=', OPT_TYPE_STRING, &global_config.opts.search,             NULL            },
  { '+', "edns",       '=', OPT_TYPE_FUNC,   NULL,                                   opt_edns_cb     },
  { '+', "keepopen",   0,   OPT_TYPE_BOOL,   &global_config.opts.stayopen,           NULL            },
  { '+', "ignore",     0,   OPT_TYPE_BOOL,   &global_config.opts.ignore_tc,          NULL            },
  { '+', "ndots",      '=', OPT_TYPE_SIZE_T, &global_config.opts.ndots,              NULL            },
  { '+', "primary",    0,   OPT_TYPE_BOOL,   &global_config.opts.primary,            NULL            },
  { '+', "qr",         0,   OPT_TYPE_BOOL,   &global_config.opts.display_query,      NULL            },
  { '+', "question",   0,   OPT_TYPE_BOOL,   &global_config.opts.display_question,
   NULL                                                                                              },
  { '+', "recurse",    0,   OPT_TYPE_BOOL,   &global_config.opts.rd_flag,            NULL            },
  { '+', "retry",      '=', OPT_TYPE_FUNC,   NULL,                                   opt_retry_cb    },
  { '+', "search",     0,   OPT_TYPE_BOOL,   &global_config.opts.do_search,          NULL            },
  { '+', "stats",      0,   OPT_TYPE_BOOL,   &global_config.opts.display_stats,      NULL            },
  { '+', "tcp",        0,   OPT_TYPE_BOOL,   &global_config.opts.tcp,                NULL            },
  { '+', "tries",      '=', OPT_TYPE_SIZE_T, &global_config.opts.tries,              NULL            },
  { '+', "ttlid",      0,   OPT_TYPE_BOOL,   &global_config.opts.display_ttl,        NULL            },
  { '+', "vc",         0,   OPT_TYPE_BOOL,   &global_config.opts.tcp,                NULL            },
  { 0,   NULL,         0,   OPT_TYPE_FUNC,   NULL,                                   opt_dig_bare_cb },
  { 0,   NULL,         0,   0,               NULL,                                   NULL            }
};

static ares_bool_t read_cmdline(int argc, const char * const *argv,
                                int start_idx)
{
  int    arg;
  size_t opt;

  for (arg = start_idx; arg < argc; arg++) {
    ares_bool_t option_handled = ARES_FALSE;

    for (opt = 0; !option_handled &&
                  (dig_options[opt].opt != NULL || dig_options[opt].cb != NULL);
         opt++) {
      ares_bool_t is_true = ARES_TRUE;
      const char *value   = NULL;
      const char *nameptr = NULL;
      size_t      namelen;

      /* Match prefix character */
      if (dig_options[opt].prefix != 0 &&
          dig_options[opt].prefix != *(argv[arg])) {
        continue;
      }

      nameptr = argv[arg];

      /* skip prefix */
      if (dig_options[opt].prefix != 0) {
        nameptr++;
      }

      /* Negated option if it has a 'no' prefix */
      if (ares_streq_max(nameptr, "no", 2)) {
        is_true  = ARES_FALSE;
        nameptr += 2;
      }

      if (dig_options[opt].separator != 0) {
        const char *ptr = strchr(nameptr, dig_options[opt].separator);
        if (ptr == NULL) {
          namelen = ares_strlen(nameptr);
        } else {
          namelen = (size_t)(ptr - nameptr);
          value   = ptr + 1;
        }
      } else {
        namelen = ares_strlen(nameptr);
      }

      /* Match name */
      if (dig_options[opt].name != NULL &&
          !ares_streq_max(nameptr, dig_options[opt].name, namelen)) {
        continue;
      }

      if (dig_options[opt].name == NULL) {
        value = nameptr;
      }

      /* We need another argument for the value */
      if (dig_options[opt].type != OPT_TYPE_BOOL &&
          dig_options[opt].prefix != 0 && dig_options[opt].separator == 0) {
        if (arg == argc - 1) {
          snprintf(global_config.error, sizeof(global_config.error),
                   "insufficient arguments for %c%s", dig_options[opt].prefix,
                   dig_options[opt].name);
          return ARES_FALSE;
        }
        arg++;
        value = argv[arg];
      }

      switch (dig_options[opt].type) {
        case OPT_TYPE_BOOL:
          {
            ares_bool_t *b = dig_options[opt].opt;
            if (b == NULL) {
              snprintf(global_config.error, sizeof(global_config.error),
                       "invalid use for %c%s", dig_options[opt].prefix,
                       dig_options[opt].name);
              return ARES_FALSE;
            }
            *b = is_true;
          }
          break;
        case OPT_TYPE_STRING:
          {
            char **str = dig_options[opt].opt;
            if (str == NULL) {
              snprintf(global_config.error, sizeof(global_config.error),
                       "invalid use for %c%s", dig_options[opt].prefix,
                       dig_options[opt].name);
              return ARES_FALSE;
            }
            if (value == NULL) {
              snprintf(global_config.error, sizeof(global_config.error),
                       "missing value for %c%s", dig_options[opt].prefix,
                       dig_options[opt].name);
              return ARES_FALSE;
            }
            if (*str != NULL) {
              free(*str);
            }
            *str = strdup(value);
            break;
          }
        case OPT_TYPE_SIZE_T:
          {
            size_t *s = dig_options[opt].opt;
            if (s == NULL) {
              snprintf(global_config.error, sizeof(global_config.error),
                       "invalid use for %c%s", dig_options[opt].prefix,
                       dig_options[opt].name);
              return ARES_FALSE;
            }
            if (value == NULL) {
              snprintf(global_config.error, sizeof(global_config.error),
                       "missing value for %c%s", dig_options[opt].prefix,
                       dig_options[opt].name);
              return ARES_FALSE;
            }
            if (!ares_str_isnum(value)) {
              snprintf(global_config.error, sizeof(global_config.error),
                       "%c%s is not a numeric value", dig_options[opt].prefix,
                       dig_options[opt].name);
              return ARES_FALSE;
            }
            *s = strtoul(value, NULL, 10);
            break;
          }
        case OPT_TYPE_U16:
          {
            unsigned short *s = dig_options[opt].opt;
            if (s == NULL) {
              snprintf(global_config.error, sizeof(global_config.error),
                       "invalid use for %c%s", dig_options[opt].prefix,
                       dig_options[opt].name);
              return ARES_FALSE;
            }
            if (value == NULL) {
              snprintf(global_config.error, sizeof(global_config.error),
                       "missing value for %c%s", dig_options[opt].prefix,
                       dig_options[opt].name);
              return ARES_FALSE;
            }
            if (!ares_str_isnum(value)) {
              snprintf(global_config.error, sizeof(global_config.error),
                       "%c%s is not a numeric value", dig_options[opt].prefix,
                       dig_options[opt].name);
              return ARES_FALSE;
            }
            *s = (unsigned short)strtoul(value, NULL, 10);
            break;
          }
        case OPT_TYPE_FUNC:
          if (dig_options[opt].cb == NULL) {
            snprintf(global_config.error, sizeof(global_config.error),
                     "missing callback");
            return ARES_FALSE;
          }
          if (!dig_options[opt].cb(dig_options[opt].prefix,
                                   dig_options[opt].name, is_true, value)) {
            return ARES_FALSE;
          }
          break;
      }
      option_handled = ARES_TRUE;
    }

    if (!option_handled) {
      snprintf(global_config.error, sizeof(global_config.error),
               "unrecognized option %s", argv[arg]);
      return ARES_FALSE;
    }
  }

  return ARES_TRUE;
}

static ares_bool_t read_rcfile(void)
{
  char         configdir[PATH_MAX];
  unsigned int cdlen = 0;

#if !defined(WIN32)
#  if !defined(__APPLE__)
  char *configdir_xdg;
#  endif
  char *homedir;
#endif

  char          rcfile[PATH_MAX];
  unsigned int  rclen;

  size_t        rcargc;
  char        **rcargv;
  ares_buf_t   *rcbuf;
  ares_status_t rcstatus;

#if defined(WIN32)
  cdlen = (unsigned int)snprintf(configdir, sizeof(configdir), "%s/%s",
                                 getenv("APPDATA"), "c-ares");

#elif defined(__APPLE__)
  homedir = getenv("HOME");
  if (homedir != NULL) {
    cdlen = (unsigned int)snprintf(configdir, sizeof(configdir), "%s/%s/%s/%s",
                                   homedir, "Library", "Application Support",
                                   "c-ares");
  }

#else
  configdir_xdg = getenv("XDG_CONFIG_HOME");

  if (configdir_xdg == NULL) {
    homedir = getenv("HOME");
    if (homedir != NULL) {
      cdlen = (unsigned int)snprintf(configdir, sizeof(configdir), "%s/%s",
                                     homedir, ".config");
    }
  } else {
    cdlen =
      (unsigned int)snprintf(configdir, sizeof(configdir), "%s", configdir_xdg);
  }

#endif

  DEBUGF(fprintf(stderr, "read_cmdline() configdir: %s\n", configdir));

  if (cdlen == 0 || cdlen > sizeof(configdir)) {
    DEBUGF(
      fprintf(stderr, "read_cmdline() skipping rcfile parsing on directory\n"));
    return ARES_TRUE;
  }

  rclen =
    (unsigned int)snprintf(rcfile, sizeof(rcfile), "%s/adigrc", configdir);

  if (rclen > sizeof(rcfile)) {
    DEBUGF(fprintf(stderr, "read_cmdline() skipping rcfile parsing on file\n"));
    return ARES_TRUE;
  }

  rcbuf = ares_buf_create();
  if (ares_buf_load_file(rcfile, rcbuf) == ARES_SUCCESS) {
    rcstatus = ares_buf_split_str(rcbuf, (const unsigned char *)"\n ", 2,
                                  ARES_BUF_SPLIT_TRIM, 0, &rcargv, &rcargc);

    if (rcstatus == ARES_SUCCESS) {
      read_cmdline((int)rcargc, (const char * const *)rcargv, 0);

    } else {
      snprintf(global_config.error, sizeof(global_config.error),
               "rcfile is invalid: %s", ares_strerror((int)rcstatus));
    }

    ares_free_array(rcargv, rcargc, ares_free);

    if (rcstatus != ARES_SUCCESS) {
      ares_buf_destroy(rcbuf);
      return ARES_FALSE;
    }

  } else {
    DEBUGF(fprintf(stderr, "read_cmdline() failed to load rcfile"));
  }
  ares_buf_destroy(rcbuf);

  return ARES_TRUE;
}

static void config_defaults(void)
{
  memset(&global_config, 0, sizeof(global_config));

  global_config.opts.tries              = 3;
  global_config.opts.ndots              = 1;
  global_config.opts.rd_flag            = ARES_TRUE;
  global_config.opts.edns               = ARES_TRUE;
  global_config.opts.udp_size           = 1232;
  global_config.opts.aliases            = ARES_TRUE;
  global_config.opts.display_class      = ARES_TRUE;
  global_config.opts.display_ttl        = ARES_TRUE;
  global_config.opts.display_command    = ARES_TRUE;
  global_config.opts.display_stats      = ARES_TRUE;
  global_config.opts.display_question   = ARES_TRUE;
  global_config.opts.display_answer     = ARES_TRUE;
  global_config.opts.display_authority  = ARES_TRUE;
  global_config.opts.display_additional = ARES_TRUE;
  global_config.opts.display_comments   = ARES_TRUE;
  global_config.qclass                  = ARES_CLASS_IN;
  global_config.qtype                   = ARES_REC_TYPE_A;
}

static void config_opts(void)
{
  global_config.optmask = ARES_OPT_FLAGS;
  if (global_config.opts.tcp) {
    global_config.options.flags |= ARES_FLAG_USEVC;
  }
  if (global_config.opts.primary) {
    global_config.options.flags |= ARES_FLAG_PRIMARY;
  }
  if (global_config.opts.edns) {
    global_config.options.flags |= ARES_FLAG_EDNS;
  }
  if (global_config.opts.stayopen) {
    global_config.options.flags |= ARES_FLAG_STAYOPEN;
  }
  if (global_config.opts.dns0x20) {
    global_config.options.flags |= ARES_FLAG_DNS0x20;
  }
  if (!global_config.opts.aliases) {
    global_config.options.flags |= ARES_FLAG_NOALIASES;
  }
  if (!global_config.opts.rd_flag) {
    global_config.options.flags |= ARES_FLAG_NORECURSE;
  }
  if (!global_config.opts.do_search) {
    global_config.options.flags |= ARES_FLAG_NOSEARCH;
  }
  if (global_config.opts.ignore_tc) {
    global_config.options.flags |= ARES_FLAG_IGNTC;
  }
  if (global_config.opts.port) {
    global_config.optmask          |= ARES_OPT_UDP_PORT;
    global_config.optmask          |= ARES_OPT_TCP_PORT;
    global_config.options.udp_port  = global_config.opts.port;
    global_config.options.tcp_port  = global_config.opts.port;
  }

  global_config.optmask       |= ARES_OPT_TRIES;
  global_config.options.tries  = (int)global_config.opts.tries;

  global_config.optmask       |= ARES_OPT_NDOTS;
  global_config.options.ndots  = (int)global_config.opts.ndots;

  global_config.optmask         |= ARES_OPT_EDNSPSZ;
  global_config.options.ednspsz  = (int)global_config.opts.udp_size;

  if (global_config.opts.search != NULL) {
    global_config.optmask          |= ARES_OPT_DOMAINS;
    global_config.options.domains   = &global_config.opts.search;
    global_config.options.ndomains  = 1;
  }
}

int main(int argc, char **argv)
{
  ares_channel_t *channel = NULL;
  ares_status_t   status;
  int             rv = 0;

#ifdef USE_WINSOCK
  WORD    wVersionRequested = MAKEWORD(USE_WINSOCK, USE_WINSOCK);
  WSADATA wsaData;
  WSAStartup(wVersionRequested, &wsaData);
#endif

  status = (ares_status_t)ares_library_init(ARES_LIB_INIT_ALL);
  if (status != ARES_SUCCESS) {
    fprintf(stderr, "ares_library_init: %s\n", ares_strerror((int)status));
    return 1;
  }

  config_defaults();

  if (!read_cmdline(argc, (const char * const *)argv, 1)) {
    printf("\n** ERROR: %s\n\n", global_config.error);
    print_help();
    rv = 1;
    goto done;
  }

  if (global_config.no_rcfile && !read_rcfile()) {
    fprintf(stderr, "\n** ERROR: %s\n", global_config.error);
  }

  if (global_config.is_help) {
    print_help();
    goto done;
  }

  if (global_config.name == NULL) {
    printf("missing query name\n");
    print_help();
    rv = 1;
    goto done;
  }

  config_opts();

  status = (ares_status_t)ares_init_options(&channel, &global_config.options,
                                            global_config.optmask);
  if (status != ARES_SUCCESS) {
    fprintf(stderr, "ares_init_options: %s\n", ares_strerror((int)status));
    rv = 1;
    goto done;
  }

  if (global_config.servers) {
    status =
      (ares_status_t)ares_set_servers_ports_csv(channel, global_config.servers);
    if (status != ARES_SUCCESS) {
      fprintf(stderr, "ares_set_servers_ports_csv: %s: %s\n",
              ares_strerror((int)status), global_config.servers);
      rv = 1;
      goto done;
    }
  }

  /* Debug */
  if (global_config.opts.display_command) {
    printf("\n; <<>> c-ares DiG %s <<>>", ares_version(NULL));
    printf(" %s", global_config.name);
    printf("\n");
  }

  /* Enqueue a query for each separate name */
  status = enqueue_query(channel);
  if (status != ARES_SUCCESS) {
    fprintf(stderr, "Failed to create query for %s: %s\n", global_config.name,
            ares_strerror((int)status));
    rv = 1;
    goto done;
  }

  /* Process events */
  rv = event_loop(channel);

done:
  free_config();
  ares_destroy(channel);
  ares_library_cleanup();

#ifdef USE_WINSOCK
  WSACleanup();
#endif
  return rv;
}
