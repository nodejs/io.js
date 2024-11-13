/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (c) 2007 Daniel Stenberg
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

#include "ares_private.h"

#ifdef HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#ifdef HAVE_NETDB_H
#  include <netdb.h>
#endif

#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif

#if defined(ANDROID) || defined(__ANDROID__)
#  include <sys/system_properties.h>
#  include "ares_android.h"
/* From the Bionic sources */
#  define DNS_PROP_NAME_PREFIX "net.dns"
#  define MAX_DNS_PROPERTIES   8
#endif

#if defined(CARES_USE_LIBRESOLV)
#  include <resolv.h>
#endif

#if defined(USE_WINSOCK) && defined(HAVE_IPHLPAPI_H)
#  include <iphlpapi.h>
#endif

#include "ares_inet_net_pton.h"

static unsigned char ip_natural_mask(const struct ares_addr *addr)
{
  const unsigned char *ptr = NULL;
  /* This is an odd one.  If a raw ipv4 address is specified, then we take
   * what is called a natural mask, which means we look at the first octet
   * of the ip address and for values 0-127 we assume it is a class A (/8),
   * for values 128-191 we assume it is a class B (/16), and for 192-223
   * we assume it is a class C (/24).  223-239 is Class D which and 240-255 is
   * Class E, however, there is no pre-defined mask for this, so we'll use
   * /24 as well as that's what the old code did.
   *
   * For IPv6, we'll use /64.
   */

  if (addr->family == AF_INET6) {
    return 64;
  }

  ptr = (const unsigned char *)&addr->addr.addr4;
  if (*ptr < 128) {
    return 8;
  }

  if (*ptr < 192) {
    return 16;
  }

  return 24;
}

static ares_bool_t sortlist_append(struct apattern **sortlist, size_t *nsort,
                                   const struct apattern *pat)
{
  struct apattern *newsort;

  newsort = ares_realloc(*sortlist, (*nsort + 1) * sizeof(*newsort));
  if (newsort == NULL) {
    return ARES_FALSE; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  *sortlist = newsort;

  memcpy(&(*sortlist)[*nsort], pat, sizeof(**sortlist));
  (*nsort)++;

  return ARES_TRUE;
}

static ares_status_t parse_sort(ares_buf_t *buf, struct apattern *pat)
{
  ares_status_t       status;
  const unsigned char ip_charset[]             = "ABCDEFabcdef0123456789.:";
  char                ipaddr[INET6_ADDRSTRLEN] = "";
  size_t              addrlen;

  memset(pat, 0, sizeof(*pat));

  /* Consume any leading whitespace */
  ares_buf_consume_whitespace(buf, ARES_TRUE);

  /* If no length, just ignore, return ENOTFOUND as an indicator */
  if (ares_buf_len(buf) == 0) {
    return ARES_ENOTFOUND;
  }

  ares_buf_tag(buf);

  /* Consume ip address */
  if (ares_buf_consume_charset(buf, ip_charset, sizeof(ip_charset) - 1) == 0) {
    return ARES_EBADSTR;
  }

  /* Fetch ip address */
  status = ares_buf_tag_fetch_string(buf, ipaddr, sizeof(ipaddr));
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Parse it to make sure its valid */
  pat->addr.family = AF_UNSPEC;
  if (ares_dns_pton(ipaddr, &pat->addr, &addrlen) == NULL) {
    return ARES_EBADSTR;
  }

  /* See if there is a subnet mask */
  if (ares_buf_begins_with(buf, (const unsigned char *)"/", 1)) {
    char                maskstr[16];
    const unsigned char ipv4_charset[] = "0123456789.";


    /* Consume / */
    ares_buf_consume(buf, 1);

    ares_buf_tag(buf);

    /* Consume mask */
    if (ares_buf_consume_charset(buf, ipv4_charset, sizeof(ipv4_charset) - 1) ==
        0) {
      return ARES_EBADSTR;
    }

    /* Fetch mask */
    status = ares_buf_tag_fetch_string(buf, maskstr, sizeof(maskstr));
    if (status != ARES_SUCCESS) {
      return status;
    }

    if (ares_str_isnum(maskstr)) {
      /* Numeric mask */
      int mask = atoi(maskstr);
      if (mask < 0 || mask > 128) {
        return ARES_EBADSTR;
      }
      if (pat->addr.family == AF_INET && mask > 32) {
        return ARES_EBADSTR;
      }
      pat->mask = (unsigned char)mask;
    } else {
      /* Ipv4 subnet style mask */
      struct ares_addr     maskaddr;
      const unsigned char *ptr;

      memset(&maskaddr, 0, sizeof(maskaddr));
      maskaddr.family = AF_INET;
      if (ares_dns_pton(maskstr, &maskaddr, &addrlen) == NULL) {
        return ARES_EBADSTR;
      }
      ptr       = (const unsigned char *)&maskaddr.addr.addr4;
      pat->mask = (unsigned char)(ares_count_bits_u8(ptr[0]) +
                                  ares_count_bits_u8(ptr[1]) +
                                  ares_count_bits_u8(ptr[2]) +
                                  ares_count_bits_u8(ptr[3]));
    }
  } else {
    pat->mask = ip_natural_mask(&pat->addr);
  }

  /* Consume any trailing whitespace */
  ares_buf_consume_whitespace(buf, ARES_TRUE);

  /* If we have any trailing bytes other than whitespace, its a parse failure */
  if (ares_buf_len(buf) != 0) {
    return ARES_EBADSTR;
  }

  return ARES_SUCCESS;
}

ares_status_t ares_parse_sortlist(struct apattern **sortlist, size_t *nsort,
                                  const char *str)
{
  ares_buf_t   *buf    = NULL;
  ares_status_t status = ARES_SUCCESS;
  ares_array_t *arr    = NULL;
  size_t        num    = 0;
  size_t        i;

  if (sortlist == NULL || nsort == NULL || str == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (*sortlist != NULL) {
    ares_free(*sortlist);
  }

  *sortlist = NULL;
  *nsort    = 0;

  buf = ares_buf_create_const((const unsigned char *)str, ares_strlen(str));
  if (buf == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  /* Split on space or semicolon */
  status = ares_buf_split(buf, (const unsigned char *)" ;", 2,
                          ARES_BUF_SPLIT_NONE, 0, &arr);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  num = ares_array_len(arr);
  for (i = 0; i < num; i++) {
    ares_buf_t    **bufptr = ares_array_at(arr, i);
    ares_buf_t     *entry  = *bufptr;

    struct apattern pat;

    status = parse_sort(entry, &pat);
    if (status != ARES_SUCCESS && status != ARES_ENOTFOUND) {
      goto done;
    }

    if (status != ARES_SUCCESS) {
      continue;
    }

    if (!sortlist_append(sortlist, nsort, &pat)) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  status = ARES_SUCCESS;

done:
  ares_buf_destroy(buf);
  ares_array_destroy(arr);

  if (status != ARES_SUCCESS) {
    ares_free(*sortlist);
    *sortlist = NULL;
    *nsort    = 0;
  }

  return status;
}

static ares_status_t config_search(ares_sysconfig_t *sysconfig, const char *str,
                                   size_t max_domains)
{
  if (sysconfig->domains && sysconfig->ndomains > 0) {
    /* if we already have some domains present, free them first */
    ares_strsplit_free(sysconfig->domains, sysconfig->ndomains);
    sysconfig->domains  = NULL;
    sysconfig->ndomains = 0;
  }

  sysconfig->domains = ares_strsplit(str, ", ", &sysconfig->ndomains);
  if (sysconfig->domains == NULL) {
    return ARES_ENOMEM;
  }

  /* Truncate if necessary */
  if (max_domains && sysconfig->ndomains > max_domains) {
    size_t i;
    for (i = max_domains; i < sysconfig->ndomains; i++) {
      ares_free(sysconfig->domains[i]);
      sysconfig->domains[i] = NULL;
    }
    sysconfig->ndomains = max_domains;
  }

  return ARES_SUCCESS;
}

static ares_status_t buf_fetch_string(ares_buf_t *buf, char *str,
                                      size_t str_len)
{
  ares_status_t status;
  ares_buf_tag(buf);
  ares_buf_consume(buf, ares_buf_len(buf));

  status = ares_buf_tag_fetch_string(buf, str, str_len);
  return status;
}

static ares_status_t config_lookup(ares_sysconfig_t *sysconfig, ares_buf_t *buf,
                                   const char *separators)
{
  ares_status_t status;
  char          lookupstr[32];
  size_t        lookupstr_cnt = 0;
  char        **lookups       = NULL;
  size_t        num           = 0;
  size_t        i;
  size_t        separators_len = ares_strlen(separators);

  status =
    ares_buf_split_str(buf, (const unsigned char *)separators, separators_len,
                       ARES_BUF_SPLIT_TRIM, 0, &lookups, &num);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  for (i = 0; i < num; i++) {
    const char *value = lookups[i];
    char        ch;

    if (ares_strcaseeq(value, "dns") || ares_strcaseeq(value, "bind") ||
        ares_strcaseeq(value, "resolv") || ares_strcaseeq(value, "resolve")) {
      ch = 'b';
    } else if (ares_strcaseeq(value, "files") ||
               ares_strcaseeq(value, "file") ||
               ares_strcaseeq(value, "local")) {
      ch = 'f';
    } else {
      continue;
    }

    /* Look for a duplicate and ignore */
    if (memchr(lookupstr, ch, lookupstr_cnt) == NULL) {
      lookupstr[lookupstr_cnt++] = ch;
    }
  }

  if (lookupstr_cnt) {
    lookupstr[lookupstr_cnt] = 0;
    ares_free(sysconfig->lookups);
    sysconfig->lookups = ares_strdup(lookupstr);
    if (sysconfig->lookups == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  status = ARES_SUCCESS;

done:
  if (status != ARES_ENOMEM) {
    status = ARES_SUCCESS;
  }
  ares_free_array(lookups, num, ares_free);
  return status;
}

static ares_status_t process_option(ares_sysconfig_t *sysconfig,
                                    ares_buf_t       *option)
{
  char        **kv  = NULL;
  size_t        num = 0;
  const char   *key;
  const char   *val;
  unsigned int  valint = 0;
  ares_status_t status;

  /* Split on : */
  status = ares_buf_split_str(option, (const unsigned char *)":", 1,
                              ARES_BUF_SPLIT_TRIM, 2, &kv, &num);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  if (num < 1) {
    status = ARES_EBADSTR;
    goto done;
  }

  key = kv[0];
  if (num == 2) {
    val    = kv[1];
    valint = (unsigned int)strtoul(val, NULL, 10);
  }

  if (ares_streq(key, "ndots")) {
    sysconfig->ndots = valint;
  } else if (ares_streq(key, "retrans") || ares_streq(key, "timeout")) {
    if (valint == 0) {
      return ARES_EFORMERR;
    }
    sysconfig->timeout_ms = valint * 1000;
  } else if (ares_streq(key, "retry") || ares_streq(key, "attempts")) {
    if (valint == 0) {
      return ARES_EFORMERR;
    }
    sysconfig->tries = valint;
  } else if (ares_streq(key, "rotate")) {
    sysconfig->rotate = ARES_TRUE;
  } else if (ares_streq(key, "use-vc") || ares_streq(key, "usevc")) {
    sysconfig->usevc = ARES_TRUE;
  }

done:
  ares_free_array(kv, num, ares_free);
  return status;
}

ares_status_t ares_sysconfig_set_options(ares_sysconfig_t *sysconfig,
                                         const char       *str)
{
  ares_buf_t   *buf     = NULL;
  ares_array_t *options = NULL;
  size_t        num;
  size_t        i;
  ares_status_t status;

  buf = ares_buf_create_const((const unsigned char *)str, ares_strlen(str));
  if (buf == NULL) {
    return ARES_ENOMEM;
  }

  status = ares_buf_split(buf, (const unsigned char *)" \t", 2,
                          ARES_BUF_SPLIT_TRIM, 0, &options);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  num = ares_array_len(options);
  for (i = 0; i < num; i++) {
    ares_buf_t **bufptr = ares_array_at(options, i);
    ares_buf_t  *valbuf = *bufptr;

    status = process_option(sysconfig, valbuf);
    /* Out of memory is the only fatal condition */
    if (status == ARES_ENOMEM) {
      goto done; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  status = ARES_SUCCESS;

done:
  ares_array_destroy(options);
  ares_buf_destroy(buf);
  return status;
}

ares_status_t ares_init_by_environment(ares_sysconfig_t *sysconfig)
{
  const char   *localdomain;
  const char   *res_options;
  ares_status_t status;

  localdomain = getenv("LOCALDOMAIN");
  if (localdomain) {
    char *temp = ares_strdup(localdomain);
    if (temp == NULL) {
      return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    }
    status = config_search(sysconfig, temp, 1);
    ares_free(temp);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  res_options = getenv("RES_OPTIONS");
  if (res_options) {
    status = ares_sysconfig_set_options(sysconfig, res_options);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  return ARES_SUCCESS;
}

/* Configuration Files:
 *  /etc/resolv.conf
 *    - All Unix-like systems
 *    - Comments start with ; or #
 *    - Lines have a keyword followed by a value that is interpreted specific
 *      to the keyword:
 *    - Keywords:
 *      - nameserver - IP address of nameserver with optional port (using a :
 *        prefix). If using an ipv6 address and specifying a port, the ipv6
 *        address must be encapsulated in brackets. For link-local ipv6
 *        addresses, the interface can also be specified with a % prefix. e.g.:
 *          "nameserver [fe80::1]:1234%iface"
 *        This keyword may be specified multiple times.
 *      - search - whitespace separated list of domains
 *      - domain - obsolete, same as search except only a single domain
 *      - lookup / hostresorder - local, bind, file, files
 *      - sortlist - whitespace separated ip-address/netmask pairs
 *      - options - options controlling resolver variables
 *        - ndots:n - set ndots option
 *        - timeout:n (retrans:n) - timeout per query attempt in seconds
 *        - attempts:n (retry:n) - number of times resolver will send query
 *        - rotate - round-robin selection of name servers
 *        - use-vc / usevc - force tcp
 *  /etc/nsswitch.conf
 *    - Modern Linux, FreeBSD, HP-UX, Solaris
 *    - Search order set via:
 *      "hosts: files dns mdns4_minimal mdns4"
 *      - files is /etc/hosts
 *      - dns is dns
 *      - mdns4_minimal does mdns only if ending in .local
 *      - mdns4 does not limit to domains ending in .local
 *  /etc/netsvc.conf
 *    - AIX
 *    - Search order set via:
 *      "hosts = local , bind"
 *      - bind is dns
 *      - local is /etc/hosts
 *  /etc/svc.conf
 *    - Tru64
 *    - Same format as /etc/netsvc.conf
 *  /etc/host.conf
 *    - Early FreeBSD, Early Linux
 *    - Not worth supporting, format varied based on system, FreeBSD used
 *      just a line per search order, Linux used "order " and a comma
 *      delimited list of "bind" and "hosts"
 */


/* This function will only return ARES_SUCCESS or ARES_ENOMEM.  Any other
 * conditions are ignored.  Users may mess up config files, but we want to
 * process anything we can. */
static ares_status_t parse_resolvconf_line(const ares_channel_t *channel,
                                           ares_sysconfig_t     *sysconfig,
                                           ares_buf_t           *line)
{
  char          option[32];
  char          value[512];
  ares_status_t status = ARES_SUCCESS;

  /* Ignore lines beginning with a comment */
  if (ares_buf_begins_with(line, (const unsigned char *)"#", 1) ||
      ares_buf_begins_with(line, (const unsigned char *)";", 1)) {
    return ARES_SUCCESS;
  }

  ares_buf_tag(line);

  /* Shouldn't be possible, but if it happens, ignore the line. */
  if (ares_buf_consume_nonwhitespace(line) == 0) {
    return ARES_SUCCESS;
  }

  status = ares_buf_tag_fetch_string(line, option, sizeof(option));
  if (status != ARES_SUCCESS) {
    return ARES_SUCCESS;
  }

  ares_buf_consume_whitespace(line, ARES_TRUE);

  status = buf_fetch_string(line, value, sizeof(value));
  if (status != ARES_SUCCESS) {
    return ARES_SUCCESS;
  }

  ares_str_trim(value);
  if (*value == 0) {
    return ARES_SUCCESS;
  }

  /* At this point we have a string option and a string value, both trimmed
   * of leading and trailing whitespace.  Lets try to evaluate them */
  if (ares_streq(option, "domain")) {
    /* Domain is legacy, don't overwrite an existing config set by search */
    if (sysconfig->domains == NULL) {
      status = config_search(sysconfig, value, 1);
    }
  } else if (ares_streq(option, "lookup") ||
             ares_streq(option, "hostresorder")) {
    ares_buf_tag_rollback(line);
    status = config_lookup(sysconfig, line, " \t");
  } else if (ares_streq(option, "search")) {
    status = config_search(sysconfig, value, 0);
  } else if (ares_streq(option, "nameserver")) {
    status = ares_sconfig_append_fromstr(channel, &sysconfig->sconfig, value,
                                         ARES_TRUE);
  } else if (ares_streq(option, "sortlist")) {
    /* Ignore all failures except ENOMEM.  If the sysadmin set a bad
     * sortlist, just ignore the sortlist, don't cause an inoperable
     * channel */
    status =
      ares_parse_sortlist(&sysconfig->sortlist, &sysconfig->nsortlist, value);
    if (status != ARES_ENOMEM) {
      status = ARES_SUCCESS;
    }
  } else if (ares_streq(option, "options")) {
    status = ares_sysconfig_set_options(sysconfig, value);
  }

  return status;
}

/* This function will only return ARES_SUCCESS or ARES_ENOMEM.  Any other
 * conditions are ignored.  Users may mess up config files, but we want to
 * process anything we can. */
static ares_status_t parse_nsswitch_line(const ares_channel_t *channel,
                                         ares_sysconfig_t     *sysconfig,
                                         ares_buf_t           *line)
{
  char          option[32];
  ares_status_t status = ARES_SUCCESS;
  ares_array_t *sects  = NULL;
  ares_buf_t  **bufptr;
  ares_buf_t   *buf;

  (void)channel;

  /* Ignore lines beginning with a comment */
  if (ares_buf_begins_with(line, (const unsigned char *)"#", 1)) {
    return ARES_SUCCESS;
  }

  /* database : values (space delimited) */
  status = ares_buf_split(line, (const unsigned char *)":", 1,
                          ARES_BUF_SPLIT_TRIM, 2, &sects);

  if (status != ARES_SUCCESS || ares_array_len(sects) != 2) {
    goto done;
  }

  bufptr = ares_array_at(sects, 0);
  buf    = *bufptr;

  status = buf_fetch_string(buf, option, sizeof(option));
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* Only support "hosts:" */
  if (!ares_streq(option, "hosts")) {
    goto done;
  }

  /* Values are space separated */
  bufptr = ares_array_at(sects, 1);
  buf    = *bufptr;
  status = config_lookup(sysconfig, buf, " \t");

done:
  ares_array_destroy(sects);
  if (status != ARES_ENOMEM) {
    status = ARES_SUCCESS;
  }
  return status;
}

/* This function will only return ARES_SUCCESS or ARES_ENOMEM.  Any other
 * conditions are ignored.  Users may mess up config files, but we want to
 * process anything we can. */
static ares_status_t parse_svcconf_line(const ares_channel_t *channel,
                                        ares_sysconfig_t     *sysconfig,
                                        ares_buf_t           *line)
{
  char          option[32];
  ares_buf_t  **bufptr;
  ares_buf_t   *buf;
  ares_status_t status = ARES_SUCCESS;
  ares_array_t *sects  = NULL;

  (void)channel;

  /* Ignore lines beginning with a comment */
  if (ares_buf_begins_with(line, (const unsigned char *)"#", 1)) {
    return ARES_SUCCESS;
  }

  /* database = values (comma delimited)*/
  status = ares_buf_split(line, (const unsigned char *)"=", 1,
                          ARES_BUF_SPLIT_TRIM, 2, &sects);

  if (status != ARES_SUCCESS || ares_array_len(sects) != 2) {
    goto done;
  }

  bufptr = ares_array_at(sects, 0);
  buf    = *bufptr;
  status = buf_fetch_string(buf, option, sizeof(option));
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* Only support "hosts=" */
  if (!ares_streq(option, "hosts")) {
    goto done;
  }

  /* Values are comma separated */
  bufptr = ares_array_at(sects, 1);
  buf    = *bufptr;
  status = config_lookup(sysconfig, buf, ",");

done:
  ares_array_destroy(sects);
  if (status != ARES_ENOMEM) {
    status = ARES_SUCCESS;
  }
  return status;
}

typedef ares_status_t (*line_callback_t)(const ares_channel_t *channel,
                                         ares_sysconfig_t     *sysconfig,
                                         ares_buf_t           *line);

/* Should only return:
 *  ARES_ENOTFOUND - file not found
 *  ARES_EFILE     - error reading file (perms)
 *  ARES_ENOMEM    - out of memory
 *  ARES_SUCCESS   - file processed, doesn't necessarily mean it was a good
 *                   file, but we're not erroring out if we can't parse
 *                   something (or anything at all) */
static ares_status_t process_config_lines(const ares_channel_t *channel,
                                          const char           *filename,
                                          ares_sysconfig_t     *sysconfig,
                                          line_callback_t       cb)
{
  ares_status_t status = ARES_SUCCESS;
  ares_array_t *lines  = NULL;
  ares_buf_t   *buf    = NULL;
  size_t        num;
  size_t        i;

  buf = ares_buf_create();
  if (buf == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  status = ares_buf_load_file(filename, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_buf_split(buf, (const unsigned char *)"\n", 1,
                          ARES_BUF_SPLIT_TRIM, 0, &lines);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  num = ares_array_len(lines);
  for (i = 0; i < num; i++) {
    ares_buf_t **bufptr = ares_array_at(lines, i);
    ares_buf_t  *line   = *bufptr;

    status = cb(channel, sysconfig, line);
    if (status != ARES_SUCCESS) {
      goto done;
    }
  }

done:
  ares_buf_destroy(buf);
  ares_array_destroy(lines);

  return status;
}

ares_status_t ares_init_sysconfig_files(const ares_channel_t *channel,
                                        ares_sysconfig_t     *sysconfig)
{
  ares_status_t status = ARES_SUCCESS;

  /* Resolv.conf */
  status = process_config_lines(channel,
                                (channel->resolvconf_path != NULL)
                                  ? channel->resolvconf_path
                                  : PATH_RESOLV_CONF,
                                sysconfig, parse_resolvconf_line);
  if (status != ARES_SUCCESS && status != ARES_ENOTFOUND) {
    goto done;
  }

  /* Nsswitch.conf */
  status = process_config_lines(channel, "/etc/nsswitch.conf", sysconfig,
                                parse_nsswitch_line);
  if (status != ARES_SUCCESS && status != ARES_ENOTFOUND) {
    goto done;
  }

  /* netsvc.conf */
  status = process_config_lines(channel, "/etc/netsvc.conf", sysconfig,
                                parse_svcconf_line);
  if (status != ARES_SUCCESS && status != ARES_ENOTFOUND) {
    goto done;
  }

  /* svc.conf */
  status = process_config_lines(channel, "/etc/svc.conf", sysconfig,
                                parse_svcconf_line);
  if (status != ARES_SUCCESS && status != ARES_ENOTFOUND) {
    goto done;
  }

  status = ARES_SUCCESS;

done:
  return status;
}
