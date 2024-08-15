/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (c) 2008 Daniel Stenberg
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
#include "ares_private.h"

#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#endif
#ifdef HAVE_NET_IF_H
#  include <net/if.h>
#endif

#if defined(USE_WINSOCK)
#  if defined(HAVE_IPHLPAPI_H)
#    include <iphlpapi.h>
#  endif
#  if defined(HAVE_NETIOAPI_H)
#    include <netioapi.h>
#  endif
#endif

#include "ares_data.h"
#include "ares_inet_net_pton.h"

typedef struct {
  struct ares_addr addr;
  unsigned short   tcp_port;
  unsigned short   udp_port;

  char             ll_iface[IF_NAMESIZE];
  unsigned int     ll_scope;
} ares_sconfig_t;

static ares_bool_t ares__addr_match(const struct ares_addr *addr1,
                                    const struct ares_addr *addr2)
{
  if (addr1 == NULL && addr2 == NULL) {
    return ARES_TRUE; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (addr1 == NULL || addr2 == NULL) {
    return ARES_FALSE; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (addr1->family != addr2->family) {
    return ARES_FALSE;
  }

  if (addr1->family == AF_INET && memcmp(&addr1->addr.addr4, &addr2->addr.addr4,
                                         sizeof(addr1->addr.addr4)) == 0) {
    return ARES_TRUE;
  }

  if (addr1->family == AF_INET6 &&
      memcmp(&addr1->addr.addr6._S6_un._S6_u8, &addr2->addr.addr6._S6_un._S6_u8,
             sizeof(addr1->addr.addr6._S6_un._S6_u8)) == 0) {
    return ARES_TRUE;
  }

  return ARES_FALSE;
}

ares_bool_t ares__subnet_match(const struct ares_addr *addr,
                               const struct ares_addr *subnet,
                               unsigned char           netmask)
{
  const unsigned char *addr_ptr;
  const unsigned char *subnet_ptr;
  size_t               len;
  size_t               i;

  if (addr == NULL || subnet == NULL) {
    return ARES_FALSE; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (addr->family != subnet->family) {
    return ARES_FALSE;
  }

  if (addr->family == AF_INET) {
    addr_ptr   = (const unsigned char *)&addr->addr.addr4;
    subnet_ptr = (const unsigned char *)&subnet->addr.addr4;
    len        = 4;

    if (netmask > 32) {
      return ARES_FALSE; /* LCOV_EXCL_LINE: DefensiveCoding */
    }
  } else if (addr->family == AF_INET6) {
    addr_ptr   = (const unsigned char *)&addr->addr.addr6;
    subnet_ptr = (const unsigned char *)&subnet->addr.addr6;
    len        = 16;

    if (netmask > 128) {
      return ARES_FALSE; /* LCOV_EXCL_LINE: DefensiveCoding */
    }
  } else {
    return ARES_FALSE; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  for (i = 0; i < len && netmask > 0; i++) {
    unsigned char mask = 0xff;
    if (netmask < 8) {
      mask    <<= (8 - netmask);
      netmask   = 0;
    } else {
      netmask -= 8;
    }

    if ((addr_ptr[i] & mask) != (subnet_ptr[i] & mask)) {
      return ARES_FALSE;
    }
  }

  return ARES_TRUE;
}

ares_bool_t ares__addr_is_linklocal(const struct ares_addr *addr)
{
  struct ares_addr    subnet;
  const unsigned char subnetaddr[16] = { 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00 };

  /* fe80::/10 */
  subnet.family = AF_INET6;
  memcpy(&subnet.addr.addr6, subnetaddr, 16);

  return ares__subnet_match(addr, &subnet, 10);
}

static ares_bool_t ares_server_blacklisted(const struct ares_addr *addr)
{
  /* A list of blacklisted IPv6 subnets. */
  const struct {
    const unsigned char netbase[16];
    unsigned char       netmask;
  } blacklist_v6[] = {
    /* fec0::/10 was deprecated by [RFC3879] in September 2004. Formerly a
     * Site-Local scoped address prefix.  These are never valid DNS servers,
     * but are known to be returned at least sometimes on Windows and Android.
     */
    { { 0xfe, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00 },
     10 }
  };

  size_t i;

  if (addr->family != AF_INET6) {
    return ARES_FALSE;
  }

  /* See if ipaddr matches any of the entries in the blacklist. */
  for (i = 0; i < sizeof(blacklist_v6) / sizeof(*blacklist_v6); i++) {
    struct ares_addr subnet;
    subnet.family = AF_INET6;
    memcpy(&subnet.addr.addr6, blacklist_v6[i].netbase, 16);
    if (ares__subnet_match(addr, &subnet, blacklist_v6[i].netmask)) {
      return ARES_TRUE;
    }
  }
  return ARES_FALSE;
}

/* Parse address and port in these formats, either ipv4 or ipv6 addresses
 * are allowed:
 *   ipaddr
 *   ipv4addr:port
 *   [ipaddr]
 *   [ipaddr]:port
 *
 * Modifiers: %iface
 *
 * TODO: #domain modifier
 *
 * If a port is not specified, will set port to 0.
 *
 * Will fail if an IPv6 nameserver as detected by
 * ares_ipv6_server_blacklisted()
 *
 * Returns an error code on failure, else ARES_SUCCESS
 */

static ares_status_t parse_nameserver(ares__buf_t *buf, ares_sconfig_t *sconfig)
{
  ares_status_t status;
  char          ipaddr[INET6_ADDRSTRLEN] = "";
  size_t        addrlen;

  memset(sconfig, 0, sizeof(*sconfig));

  /* Consume any leading whitespace */
  ares__buf_consume_whitespace(buf, ARES_TRUE);

  /* pop off IP address.  If it is in [ ] then it can be ipv4 or ipv6.  If
   * not, ipv4 only */
  if (ares__buf_begins_with(buf, (const unsigned char *)"[", 1)) {
    /* Consume [ */
    ares__buf_consume(buf, 1);

    ares__buf_tag(buf);

    /* Consume until ] */
    if (ares__buf_consume_until_charset(buf, (const unsigned char *)"]", 1,
                                        ARES_TRUE) == 0) {
      return ARES_EBADSTR;
    }

    status = ares__buf_tag_fetch_string(buf, ipaddr, sizeof(ipaddr));
    if (status != ARES_SUCCESS) {
      return status;
    }

    /* Skip over ] */
    ares__buf_consume(buf, 1);
  } else {
    size_t offset;

    /* Not in [ ], see if '.' is in first 4 characters, if it is, then its ipv4,
     * otherwise treat as ipv6 */
    ares__buf_tag(buf);

    offset = ares__buf_consume_until_charset(buf, (const unsigned char *)".", 1,
                                             ARES_TRUE);
    ares__buf_tag_rollback(buf);
    ares__buf_tag(buf);

    if (offset > 0 && offset < 4) {
      /* IPv4 */
      if (ares__buf_consume_charset(buf, (const unsigned char *)"0123456789.",
                                    11) == 0) {
        return ARES_EBADSTR;
      }
    } else {
      /* IPv6 */
      const unsigned char ipv6_charset[] = "ABCDEFabcdef0123456789.:";
      if (ares__buf_consume_charset(buf, ipv6_charset,
                                    sizeof(ipv6_charset) - 1) == 0) {
        return ARES_EBADSTR;
      }
    }

    status = ares__buf_tag_fetch_string(buf, ipaddr, sizeof(ipaddr));
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  /* Convert ip address from string to network byte order */
  sconfig->addr.family = AF_UNSPEC;
  if (ares_dns_pton(ipaddr, &sconfig->addr, &addrlen) == NULL) {
    return ARES_EBADSTR;
  }

  /* Pull off port */
  if (ares__buf_begins_with(buf, (const unsigned char *)":", 1)) {
    char portstr[6];

    /* Consume : */
    ares__buf_consume(buf, 1);

    ares__buf_tag(buf);

    /* Read numbers */
    if (ares__buf_consume_charset(buf, (const unsigned char *)"0123456789",
                                  10) == 0) {
      return ARES_EBADSTR;
    }

    status = ares__buf_tag_fetch_string(buf, portstr, sizeof(portstr));
    if (status != ARES_SUCCESS) {
      return status;
    }

    sconfig->udp_port = (unsigned short)atoi(portstr);
    sconfig->tcp_port = sconfig->udp_port;
  }

  /* Pull off interface modifier */
  if (ares__buf_begins_with(buf, (const unsigned char *)"%", 1)) {
    const unsigned char iface_charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                          "abcdefghijklmnopqrstuvwxyz"
                                          "0123456789.-_\\:{}";
    /* Consume % */
    ares__buf_consume(buf, 1);

    ares__buf_tag(buf);

    if (ares__buf_consume_charset(buf, iface_charset,
                                  sizeof(iface_charset) - 1) == 0) {
      return ARES_EBADSTR;
    }

    status = ares__buf_tag_fetch_string(buf, sconfig->ll_iface,
                                        sizeof(sconfig->ll_iface));
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  /* Consume any trailing whitespace so we can bail out if there is something
   * after we didn't read */
  ares__buf_consume_whitespace(buf, ARES_TRUE);

  if (ares__buf_len(buf) != 0) {
    return ARES_EBADSTR;
  }

  return ARES_SUCCESS;
}

static ares_status_t ares__sconfig_linklocal(ares_sconfig_t *s,
                                             const char     *ll_iface)
{
  unsigned int ll_scope = 0;

  if (ares_str_isnum(ll_iface)) {
    char ifname[IF_NAMESIZE] = "";
    ll_scope                 = (unsigned int)atoi(ll_iface);
    if (ares__if_indextoname(ll_scope, ifname, sizeof(ifname)) == NULL) {
      DEBUGF(fprintf(stderr, "Interface %s for ipv6 Link Local not found\n",
                     ll_iface));
      return ARES_ENOTFOUND;
    }
    ares_strcpy(s->ll_iface, ifname, sizeof(s->ll_iface));
    s->ll_scope = ll_scope;
    return ARES_SUCCESS;
  }

  ll_scope = ares__if_nametoindex(ll_iface);
  if (ll_scope == 0) {
    DEBUGF(fprintf(stderr, "Interface %s for ipv6 Link Local not found\n",
                   ll_iface));
    return ARES_ENOTFOUND;
  }
  ares_strcpy(s->ll_iface, ll_iface, sizeof(s->ll_iface));
  s->ll_scope = ll_scope;
  return ARES_SUCCESS;
}

ares_status_t ares__sconfig_append(ares__llist_t         **sconfig,
                                   const struct ares_addr *addr,
                                   unsigned short          udp_port,
                                   unsigned short          tcp_port,
                                   const char             *ll_iface)
{
  ares_sconfig_t *s;
  ares_status_t   status;

  if (sconfig == NULL || addr == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* Silently skip blacklisted IPv6 servers. */
  if (ares_server_blacklisted(addr)) {
    return ARES_SUCCESS;
  }

  s = ares_malloc_zero(sizeof(*s));
  if (s == NULL) {
    return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  if (*sconfig == NULL) {
    *sconfig = ares__llist_create(ares_free);
    if (*sconfig == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto fail;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  memcpy(&s->addr, addr, sizeof(s->addr));
  s->udp_port = udp_port;
  s->tcp_port = tcp_port;

  /* Handle link-local enumeration. If an interface is specified on a
   * non-link-local address, we'll simply end up ignoring that */
  if (ares__addr_is_linklocal(&s->addr)) {
    if (ares_strlen(ll_iface) == 0) {
      /* Silently ignore this entry, we require an interface */
      status = ARES_SUCCESS;
      goto fail;
    }
    status = ares__sconfig_linklocal(s, ll_iface);
    /* Silently ignore this entry, we can't validate the interface */
    if (status != ARES_SUCCESS) {
      status = ARES_SUCCESS;
      goto fail;
    }
  }

  if (ares__llist_insert_last(*sconfig, s) == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto fail;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  return ARES_SUCCESS;

fail:
  ares_free(s);

  return status;
}

/* Add the IPv4 or IPv6 nameservers in str (separated by commas or spaces) to
 * the servers list, updating servers and nservers as required.
 *
 * If a nameserver is encapsulated in [ ] it may optionally include a port
 * suffix, e.g.:
 *    [127.0.0.1]:59591
 *
 * The extended format is required to support OpenBSD's resolv.conf format:
 *   https://man.openbsd.org/OpenBSD-5.1/resolv.conf.5
 * As well as MacOS libresolv that may include a non-default port number.
 *
 * This will silently ignore blacklisted IPv6 nameservers as detected by
 * ares_ipv6_server_blacklisted().
 *
 * Returns an error code on failure, else ARES_SUCCESS.
 */
ares_status_t ares__sconfig_append_fromstr(ares__llist_t **sconfig,
                                           const char     *str,
                                           ares_bool_t     ignore_invalid)
{
  ares_status_t       status = ARES_SUCCESS;
  ares__buf_t        *buf    = NULL;
  ares__llist_t      *list   = NULL;
  ares__llist_node_t *node;

  /* On Windows, there may be more than one nameserver specified in the same
   * registry key, so we parse input as a space or comma separated list.
   */
  buf = ares__buf_create_const((const unsigned char *)str, ares_strlen(str));
  if (buf == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  status = ares__buf_split(buf, (const unsigned char *)" ,", 2,
                           ARES_BUF_SPLIT_NONE, 0, &list);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  for (node = ares__llist_node_first(list); node != NULL;
       node = ares__llist_node_next(node)) {
    ares__buf_t   *entry = ares__llist_node_val(node);
    ares_sconfig_t s;

    status = parse_nameserver(entry, &s);
    if (status != ARES_SUCCESS) {
      if (ignore_invalid) {
        continue;
      } else {
        goto done;
      }
    }

    status = ares__sconfig_append(sconfig, &s.addr, s.udp_port, s.tcp_port,
                                  s.ll_iface);
    if (status != ARES_SUCCESS) {
      goto done; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  status = ARES_SUCCESS;

done:
  ares__llist_destroy(list);
  ares__buf_destroy(buf);
  return status;
}

static unsigned short ares__sconfig_get_port(const ares_channel_t *channel,
                                             const ares_sconfig_t *s,
                                             ares_bool_t           is_tcp)
{
  unsigned short port = is_tcp ? s->tcp_port : s->udp_port;

  if (port == 0) {
    port = is_tcp ? channel->tcp_port : channel->udp_port;
  }

  if (port == 0) {
    port = 53;
  }

  return port;
}

static ares__slist_node_t *ares__server_find(ares_channel_t       *channel,
                                             const ares_sconfig_t *s)
{
  ares__slist_node_t *node;

  for (node = ares__slist_node_first(channel->servers); node != NULL;
       node = ares__slist_node_next(node)) {
    const ares_server_t *server = ares__slist_node_val(node);

    if (!ares__addr_match(&server->addr, &s->addr)) {
      continue;
    }

    if (server->tcp_port != ares__sconfig_get_port(channel, s, ARES_TRUE)) {
      continue;
    }

    if (server->udp_port != ares__sconfig_get_port(channel, s, ARES_FALSE)) {
      continue;
    }

    return node;
  }
  return NULL;
}

static ares_bool_t ares__server_isdup(const ares_channel_t *channel,
                                      ares__llist_node_t   *s)
{
  /* Scan backwards to see if this is a duplicate */
  ares__llist_node_t   *prev;
  const ares_sconfig_t *server = ares__llist_node_val(s);

  for (prev = ares__llist_node_prev(s); prev != NULL;
       prev = ares__llist_node_prev(prev)) {
    const ares_sconfig_t *p = ares__llist_node_val(prev);

    if (!ares__addr_match(&server->addr, &p->addr)) {
      continue;
    }

    if (ares__sconfig_get_port(channel, server, ARES_TRUE) !=
        ares__sconfig_get_port(channel, p, ARES_TRUE)) {
      continue;
    }

    if (ares__sconfig_get_port(channel, server, ARES_FALSE) !=
        ares__sconfig_get_port(channel, p, ARES_FALSE)) {
      continue;
    }

    return ARES_TRUE;
  }

  return ARES_FALSE;
}

static ares_status_t ares__server_create(ares_channel_t       *channel,
                                         const ares_sconfig_t *sconfig,
                                         size_t                idx)
{
  ares_status_t  status;
  ares_server_t *server = ares_malloc_zero(sizeof(*server));

  if (server == NULL) {
    return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  server->idx         = idx;
  server->channel     = channel;
  server->udp_port    = ares__sconfig_get_port(channel, sconfig, ARES_FALSE);
  server->tcp_port    = ares__sconfig_get_port(channel, sconfig, ARES_TRUE);
  server->addr.family = sconfig->addr.family;
  server->next_retry_time.sec  = 0;
  server->next_retry_time.usec = 0;

  if (sconfig->addr.family == AF_INET) {
    memcpy(&server->addr.addr.addr4, &sconfig->addr.addr.addr4,
           sizeof(server->addr.addr.addr4));
  } else if (sconfig->addr.family == AF_INET6) {
    memcpy(&server->addr.addr.addr6, &sconfig->addr.addr.addr6,
           sizeof(server->addr.addr.addr6));
  }

  /* Copy over link-local settings */
  if (ares_strlen(sconfig->ll_iface)) {
    ares_strcpy(server->ll_iface, sconfig->ll_iface, sizeof(server->ll_iface));
    server->ll_scope = sconfig->ll_scope;
  }

  server->tcp_parser = ares__buf_create();
  if (server->tcp_parser == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  server->tcp_send = ares__buf_create();
  if (server->tcp_send == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  server->connections = ares__llist_create(NULL);
  if (server->connections == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  if (ares__slist_insert(channel->servers, server) == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ARES_SUCCESS;

done:
  if (status != ARES_SUCCESS) {
    ares__destroy_server(server); /* LCOV_EXCL_LINE: OutOfMemory */
  }

  return status;
}

static ares_bool_t ares__server_in_newconfig(const ares_server_t *server,
                                             ares__llist_t       *srvlist)
{
  ares__llist_node_t   *node;
  const ares_channel_t *channel = server->channel;

  for (node = ares__llist_node_first(srvlist); node != NULL;
       node = ares__llist_node_next(node)) {
    const ares_sconfig_t *s = ares__llist_node_val(node);

    if (!ares__addr_match(&server->addr, &s->addr)) {
      continue;
    }

    if (server->tcp_port != ares__sconfig_get_port(channel, s, ARES_TRUE)) {
      continue;
    }

    if (server->udp_port != ares__sconfig_get_port(channel, s, ARES_FALSE)) {
      continue;
    }

    return ARES_TRUE;
  }

  return ARES_FALSE;
}

static ares_bool_t ares__servers_remove_stale(ares_channel_t *channel,
                                              ares__llist_t  *srvlist)
{
  ares_bool_t         stale_removed = ARES_FALSE;
  ares__slist_node_t *snode         = ares__slist_node_first(channel->servers);

  while (snode != NULL) {
    ares__slist_node_t  *snext  = ares__slist_node_next(snode);
    const ares_server_t *server = ares__slist_node_val(snode);
    if (!ares__server_in_newconfig(server, srvlist)) {
      /* This will clean up all server state via the destruction callback and
       * move any queries to new servers */
      ares__slist_node_destroy(snode);
      stale_removed = ARES_TRUE;
    }
    snode = snext;
  }
  return stale_removed;
}

static void ares__servers_trim_single(ares_channel_t *channel)
{
  while (ares__slist_len(channel->servers) > 1) {
    ares__slist_node_destroy(ares__slist_node_last(channel->servers));
  }
}

ares_status_t ares__servers_update(ares_channel_t *channel,
                                   ares__llist_t  *server_list,
                                   ares_bool_t     user_specified)
{
  ares__llist_node_t *node;
  size_t              idx = 0;
  ares_status_t       status;
  ares_bool_t         list_changed = ARES_FALSE;

  if (channel == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* NOTE: a NULL or zero entry server list is considered valid due to
   *       real-world people needing support for this for their test harnesses
   */

  /* Add new entries */
  for (node = ares__llist_node_first(server_list); node != NULL;
       node = ares__llist_node_next(node)) {
    const ares_sconfig_t *sconfig = ares__llist_node_val(node);
    ares__slist_node_t   *snode;

    /* If a server has already appeared in the list of new servers, skip it. */
    if (ares__server_isdup(channel, node)) {
      continue;
    }

    snode = ares__server_find(channel, sconfig);
    if (snode != NULL) {
      ares_server_t *server = ares__slist_node_val(snode);

      /* Copy over link-local settings.  Its possible some of this data has
       * changed, maybe ...  */
      if (ares_strlen(sconfig->ll_iface)) {
        ares_strcpy(server->ll_iface, sconfig->ll_iface,
                    sizeof(server->ll_iface));
        server->ll_scope = sconfig->ll_scope;
      }

      if (server->idx != idx) {
        server->idx = idx;
        /* Index changed, reinsert node, doesn't require any memory
         * allocations so can't fail. */
        ares__slist_node_reinsert(snode);
      }
    } else {
      status = ares__server_create(channel, sconfig, idx);
      if (status != ARES_SUCCESS) {
        goto done;
      }

      list_changed = ARES_TRUE;
    }

    idx++;
  }

  /* Remove any servers that don't exist in the current configuration */
  if (ares__servers_remove_stale(channel, server_list)) {
    list_changed = ARES_TRUE;
  }

  /* Trim to one server if ARES_FLAG_PRIMARY is set. */
  if (channel->flags & ARES_FLAG_PRIMARY) {
    ares__servers_trim_single(channel);
  }

  if (user_specified) {
    /* Save servers as if they were passed in as an option */
    channel->optmask |= ARES_OPT_SERVERS;
  }

  /* Clear any cached query results only if the server list changed */
  if (list_changed) {
    ares__qcache_flush(channel->qcache);
  }

  status = ARES_SUCCESS;

done:
  return status;
}

static ares_status_t
  ares_addr_node_to_server_config_llist(const struct ares_addr_node *servers,
                                        ares__llist_t              **llist)
{
  const struct ares_addr_node *node;
  ares__llist_t               *s;

  *llist = NULL;

  s = ares__llist_create(ares_free);
  if (s == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  for (node = servers; node != NULL; node = node->next) {
    ares_sconfig_t *sconfig;

    /* Invalid entry */
    if (node->family != AF_INET && node->family != AF_INET6) {
      continue;
    }

    sconfig = ares_malloc_zero(sizeof(*sconfig));
    if (sconfig == NULL) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    sconfig->addr.family = node->family;
    if (node->family == AF_INET) {
      memcpy(&sconfig->addr.addr.addr4, &node->addr.addr4,
             sizeof(sconfig->addr.addr.addr4));
    } else if (sconfig->addr.family == AF_INET6) {
      memcpy(&sconfig->addr.addr.addr6, &node->addr.addr6,
             sizeof(sconfig->addr.addr.addr6));
    }

    if (ares__llist_insert_last(s, sconfig) == NULL) {
      ares_free(sconfig); /* LCOV_EXCL_LINE: OutOfMemory */
      goto fail;          /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  *llist = s;
  return ARES_SUCCESS;

/* LCOV_EXCL_START: OutOfMemory */
fail:
  ares__llist_destroy(s);
  return ARES_ENOMEM;
  /* LCOV_EXCL_STOP */
}

static ares_status_t ares_addr_port_node_to_server_config_llist(
  const struct ares_addr_port_node *servers, ares__llist_t **llist)
{
  const struct ares_addr_port_node *node;
  ares__llist_t                    *s;

  *llist = NULL;

  s = ares__llist_create(ares_free);
  if (s == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  for (node = servers; node != NULL; node = node->next) {
    ares_sconfig_t *sconfig;

    /* Invalid entry */
    if (node->family != AF_INET && node->family != AF_INET6) {
      continue;
    }

    sconfig = ares_malloc_zero(sizeof(*sconfig));
    if (sconfig == NULL) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    sconfig->addr.family = node->family;
    if (node->family == AF_INET) {
      memcpy(&sconfig->addr.addr.addr4, &node->addr.addr4,
             sizeof(sconfig->addr.addr.addr4));
    } else if (sconfig->addr.family == AF_INET6) {
      memcpy(&sconfig->addr.addr.addr6, &node->addr.addr6,
             sizeof(sconfig->addr.addr.addr6));
    }

    sconfig->tcp_port = (unsigned short)node->tcp_port;
    sconfig->udp_port = (unsigned short)node->udp_port;

    if (ares__llist_insert_last(s, sconfig) == NULL) {
      ares_free(sconfig); /* LCOV_EXCL_LINE: OutOfMemory */
      goto fail;          /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  *llist = s;
  return ARES_SUCCESS;

/* LCOV_EXCL_START: OutOfMemory */
fail:
  ares__llist_destroy(s);
  return ARES_ENOMEM;
  /* LCOV_EXCL_STOP */
}

ares_status_t ares_in_addr_to_server_config_llist(const struct in_addr *servers,
                                                  size_t          nservers,
                                                  ares__llist_t **llist)
{
  size_t         i;
  ares__llist_t *s;

  *llist = NULL;

  s = ares__llist_create(ares_free);
  if (s == NULL) {
    goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  for (i = 0; servers != NULL && i < nservers; i++) {
    ares_sconfig_t *sconfig;

    sconfig = ares_malloc_zero(sizeof(*sconfig));
    if (sconfig == NULL) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    sconfig->addr.family = AF_INET;
    memcpy(&sconfig->addr.addr.addr4, &servers[i],
           sizeof(sconfig->addr.addr.addr4));

    if (ares__llist_insert_last(s, sconfig) == NULL) {
      goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  *llist = s;
  return ARES_SUCCESS;

/* LCOV_EXCL_START: OutOfMemory */
fail:
  ares__llist_destroy(s);
  return ARES_ENOMEM;
  /* LCOV_EXCL_STOP */
}

/* Write out the details of a server to a buffer */
ares_status_t ares_get_server_addr(const ares_server_t *server,
                                   ares__buf_t         *buf)
{
  ares_status_t status;
  char          addr[INET6_ADDRSTRLEN];

  /* ipv4addr or [ipv6addr] */
  if (server->addr.family == AF_INET6) {
    status = ares__buf_append_byte(buf, '[');
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  ares_inet_ntop(server->addr.family, &server->addr.addr, addr, sizeof(addr));

  status = ares__buf_append_str(buf, addr);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  if (server->addr.family == AF_INET6) {
    status = ares__buf_append_byte(buf, ']');
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  /* :port */
  status = ares__buf_append_byte(buf, ':');
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares__buf_append_num_dec(buf, server->udp_port, 0);
  if (status != ARES_SUCCESS) {
    return status; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* %iface */
  if (ares_strlen(server->ll_iface)) {
    status = ares__buf_append_byte(buf, '%');
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    status = ares__buf_append_str(buf, server->ll_iface);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  return ARES_SUCCESS;
}

int ares_get_servers(const ares_channel_t   *channel,
                     struct ares_addr_node **servers)
{
  struct ares_addr_node *srvr_head = NULL;
  struct ares_addr_node *srvr_last = NULL;
  struct ares_addr_node *srvr_curr;
  ares_status_t          status = ARES_SUCCESS;
  ares__slist_node_t    *node;

  if (channel == NULL) {
    return ARES_ENODATA;
  }

  ares__channel_lock(channel);

  for (node = ares__slist_node_first(channel->servers); node != NULL;
       node = ares__slist_node_next(node)) {
    const ares_server_t *server = ares__slist_node_val(node);

    /* Allocate storage for this server node appending it to the list */
    srvr_curr = ares_malloc_data(ARES_DATATYPE_ADDR_NODE);
    if (!srvr_curr) {
      status = ARES_ENOMEM;
      break;
    }
    if (srvr_last) {
      srvr_last->next = srvr_curr;
    } else {
      srvr_head = srvr_curr;
    }
    srvr_last = srvr_curr;

    /* Fill this server node data */
    srvr_curr->family = server->addr.family;
    if (srvr_curr->family == AF_INET) {
      memcpy(&srvr_curr->addr.addr4, &server->addr.addr.addr4,
             sizeof(srvr_curr->addr.addr4));
    } else {
      memcpy(&srvr_curr->addr.addr6, &server->addr.addr.addr6,
             sizeof(srvr_curr->addr.addr6));
    }
  }

  if (status != ARES_SUCCESS) {
    ares_free_data(srvr_head);
    srvr_head = NULL;
  }

  *servers = srvr_head;

  ares__channel_unlock(channel);

  return (int)status;
}

int ares_get_servers_ports(const ares_channel_t        *channel,
                           struct ares_addr_port_node **servers)
{
  struct ares_addr_port_node *srvr_head = NULL;
  struct ares_addr_port_node *srvr_last = NULL;
  struct ares_addr_port_node *srvr_curr;
  ares_status_t               status = ARES_SUCCESS;
  ares__slist_node_t         *node;

  if (channel == NULL) {
    return ARES_ENODATA;
  }

  ares__channel_lock(channel);

  for (node = ares__slist_node_first(channel->servers); node != NULL;
       node = ares__slist_node_next(node)) {
    const ares_server_t *server = ares__slist_node_val(node);

    /* Allocate storage for this server node appending it to the list */
    srvr_curr = ares_malloc_data(ARES_DATATYPE_ADDR_PORT_NODE);
    if (!srvr_curr) {
      status = ARES_ENOMEM;
      break;
    }
    if (srvr_last) {
      srvr_last->next = srvr_curr;
    } else {
      srvr_head = srvr_curr;
    }
    srvr_last = srvr_curr;

    /* Fill this server node data */
    srvr_curr->family   = server->addr.family;
    srvr_curr->udp_port = server->udp_port;
    srvr_curr->tcp_port = server->tcp_port;

    if (srvr_curr->family == AF_INET) {
      memcpy(&srvr_curr->addr.addr4, &server->addr.addr.addr4,
             sizeof(srvr_curr->addr.addr4));
    } else {
      memcpy(&srvr_curr->addr.addr6, &server->addr.addr.addr6,
             sizeof(srvr_curr->addr.addr6));
    }
  }

  if (status != ARES_SUCCESS) {
    ares_free_data(srvr_head);
    srvr_head = NULL;
  }

  *servers = srvr_head;

  ares__channel_unlock(channel);
  return (int)status;
}

int ares_set_servers(ares_channel_t              *channel,
                     const struct ares_addr_node *servers)
{
  ares__llist_t *slist;
  ares_status_t  status;

  if (channel == NULL) {
    return ARES_ENODATA;
  }

  status = ares_addr_node_to_server_config_llist(servers, &slist);
  if (status != ARES_SUCCESS) {
    return (int)status;
  }

  ares__channel_lock(channel);
  status = ares__servers_update(channel, slist, ARES_TRUE);
  ares__channel_unlock(channel);

  ares__llist_destroy(slist);

  return (int)status;
}

int ares_set_servers_ports(ares_channel_t                   *channel,
                           const struct ares_addr_port_node *servers)
{
  ares__llist_t *slist;
  ares_status_t  status;

  if (channel == NULL) {
    return ARES_ENODATA;
  }

  status = ares_addr_port_node_to_server_config_llist(servers, &slist);
  if (status != ARES_SUCCESS) {
    return (int)status;
  }

  ares__channel_lock(channel);
  status = ares__servers_update(channel, slist, ARES_TRUE);
  ares__channel_unlock(channel);

  ares__llist_destroy(slist);

  return (int)status;
}

/* Incoming string format: host[:port][,host[:port]]... */
/* IPv6 addresses with ports require square brackets [fe80::1]:53 */
static ares_status_t set_servers_csv(ares_channel_t *channel, const char *_csv)
{
  ares_status_t  status;
  ares__llist_t *slist = NULL;

  if (channel == NULL) {
    return ARES_ENODATA;
  }

  if (ares_strlen(_csv) == 0) {
    /* blank all servers */
    ares__channel_lock(channel);
    status = ares__servers_update(channel, NULL, ARES_TRUE);
    ares__channel_unlock(channel);
    return status;
  }

  status = ares__sconfig_append_fromstr(&slist, _csv, ARES_FALSE);
  if (status != ARES_SUCCESS) {
    ares__llist_destroy(slist);
    return status;
  }

  ares__channel_lock(channel);
  status = ares__servers_update(channel, slist, ARES_TRUE);
  ares__channel_unlock(channel);

  ares__llist_destroy(slist);

  return status;
}

/* We'll go ahead and honor ports anyhow */
int ares_set_servers_csv(ares_channel_t *channel, const char *_csv)
{
  return (int)set_servers_csv(channel, _csv);
}

int ares_set_servers_ports_csv(ares_channel_t *channel, const char *_csv)
{
  return (int)set_servers_csv(channel, _csv);
}

char *ares_get_servers_csv(const ares_channel_t *channel)
{
  ares__buf_t        *buf = NULL;
  char               *out = NULL;
  ares__slist_node_t *node;

  ares__channel_lock(channel);

  buf = ares__buf_create();
  if (buf == NULL) {
    goto done; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  for (node = ares__slist_node_first(channel->servers); node != NULL;
       node = ares__slist_node_next(node)) {
    ares_status_t        status;
    const ares_server_t *server = ares__slist_node_val(node);

    if (ares__buf_len(buf)) {
      status = ares__buf_append_byte(buf, ',');
      if (status != ARES_SUCCESS) {
        goto done; /* LCOV_EXCL_LINE: OutOfMemory */
      }
    }

    status = ares_get_server_addr(server, buf);
    if (status != ARES_SUCCESS) {
      goto done; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  out = ares__buf_finish_str(buf, NULL);
  buf = NULL;

done:
  ares__channel_unlock(channel);
  ares__buf_destroy(buf);
  return out;
}

void ares_set_server_state_callback(ares_channel_t            *channel,
                                    ares_server_state_callback cb, void *data)
{
  if (channel == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }
  channel->server_state_cb      = cb;
  channel->server_state_cb_data = data;
}
