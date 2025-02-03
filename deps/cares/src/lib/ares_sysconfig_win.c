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

#if defined(USE_WINSOCK)
#  if defined(HAVE_IPHLPAPI_H)
#    include <iphlpapi.h>
#  endif
#  if defined(HAVE_NETIOAPI_H)
#    include <netioapi.h>
#  endif
#endif

#include "ares_inet_net_pton.h"

#if defined(USE_WINSOCK)
/*
 * get_REG_SZ()
 *
 * Given a 'hKey' handle to an open registry key and a 'leafKeyName' pointer
 * to the name of the registry leaf key to be queried, fetch it's string
 * value and return a pointer in *outptr to a newly allocated memory area
 * holding it as a null-terminated string.
 *
 * Returns 0 and nullifies *outptr upon inability to return a string value.
 *
 * Returns 1 and sets *outptr when returning a dynamically allocated string.
 *
 * Supported on Windows NT 3.5 and newer.
 */
static ares_bool_t get_REG_SZ(HKEY hKey, const char *leafKeyName, char **outptr)
{
  DWORD size = 0;
  int   res;

  *outptr = NULL;

  /* Find out size of string stored in registry */
  res = RegQueryValueExA(hKey, leafKeyName, 0, NULL, NULL, &size);
  if ((res != ERROR_SUCCESS && res != ERROR_MORE_DATA) || !size) {
    return ARES_FALSE;
  }

  /* Allocate buffer of indicated size plus one given that string
     might have been stored without null termination */
  *outptr = ares_malloc(size + 1);
  if (!*outptr) {
    return ARES_FALSE;
  }

  /* Get the value for real */
  res = RegQueryValueExA(hKey, leafKeyName, 0, NULL, (unsigned char *)*outptr,
                         &size);
  if ((res != ERROR_SUCCESS) || (size == 1)) {
    ares_free(*outptr);
    *outptr = NULL;
    return ARES_FALSE;
  }

  /* Null terminate buffer always */
  *(*outptr + size) = '\0';

  return ARES_TRUE;
}

static void commanjoin(char **dst, const char * const src, const size_t len)
{
  char  *newbuf;
  size_t newsize;

  /* 1 for terminating 0 and 2 for , and terminating 0 */
  newsize = len + (*dst ? (ares_strlen(*dst) + 2) : 1);
  newbuf  = ares_realloc(*dst, newsize);
  if (!newbuf) {
    return;
  }
  if (*dst == NULL) {
    *newbuf = '\0';
  }
  *dst = newbuf;
  if (ares_strlen(*dst) != 0) {
    strcat(*dst, ",");
  }
  strncat(*dst, src, len);
}

/*
 * commajoin()
 *
 * RTF code.
 */
static void commajoin(char **dst, const char *src)
{
  commanjoin(dst, src, ares_strlen(src));
}

/* A structure to hold the string form of IPv4 and IPv6 addresses so we can
 * sort them by a metric.
 */
typedef struct {
  /* The metric we sort them by. */
  ULONG  metric;

  /* Original index of the item, used as a secondary sort parameter to make
   * qsort() stable if the metrics are equal */
  size_t orig_idx;

  /* Room enough for the string form of any IPv4 or IPv6 address that
   * ares_inet_ntop() will create.  Based on the existing c-ares practice.
   */
  char   text[INET6_ADDRSTRLEN + 8 + 64]; /* [%s]:NNNNN%iface */
} Address;

/* Sort Address values \a left and \a right by metric, returning the usual
 * indicators for qsort().
 */
static int compareAddresses(const void *arg1, const void *arg2)
{
  const Address * const left  = arg1;
  const Address * const right = arg2;
  /* Lower metric the more preferred */
  if (left->metric < right->metric) {
    return -1;
  }
  if (left->metric > right->metric) {
    return 1;
  }
  /* If metrics are equal, lower original index more preferred */
  if (left->orig_idx < right->orig_idx) {
    return -1;
  }
  if (left->orig_idx > right->orig_idx) {
    return 1;
  }
  return 0;
}

/* There can be multiple routes to "the Internet".  And there can be different
 * DNS servers associated with each of the interfaces that offer those routes.
 * We have to assume that any DNS server can serve any request.  But, some DNS
 * servers may only respond if requested over their associated interface.  But
 * we also want to use "the preferred route to the Internet" whenever possible
 * (and not use DNS servers on a non-preferred route even by forcing request
 * to go out on the associated non-preferred interface).  i.e. We want to use
 * the DNS servers associated with the same interface that we would use to
 * make a general request to anything else.
 *
 * But, Windows won't sort the DNS servers by the metrics associated with the
 * routes and interfaces _even_ though it obviously sends IP packets based on
 * those same routes and metrics.  So, we must do it ourselves.
 *
 * So, we sort the DNS servers by the same metric values used to determine how
 * an outgoing IP packet will go, thus effectively using the DNS servers
 * associated with the interface that the DNS requests themselves will
 * travel.  This gives us optimal routing and avoids issues where DNS servers
 * won't respond to requests that don't arrive via some specific subnetwork
 * (and thus some specific interface).
 *
 * This function computes the metric we use to sort.  On the interface
 * identified by \a luid, it determines the best route to \a dest and combines
 * that route's metric with \a interfaceMetric to compute a metric for the
 * destination address on that interface.  This metric can be used as a weight
 * to sort the DNS server addresses associated with each interface (lower is
 * better).
 *
 * Note that by restricting the route search to the specific interface with
 * which the DNS servers are associated, this function asks the question "What
 * is the metric for sending IP packets to this DNS server?" which allows us
 * to sort the DNS servers correctly.
 */
static ULONG getBestRouteMetric(IF_LUID * const luid, /* Can't be const :( */
                                const SOCKADDR_INET * const dest,
                                const ULONG                 interfaceMetric)
{
  /* On this interface, get the best route to that destination. */
#  if defined(__WATCOMC__)
  /* OpenWatcom's builtin Windows SDK does not have a definition for
   * MIB_IPFORWARD_ROW2, and also does not allow the usage of SOCKADDR_INET
   * as a variable. Let's work around this by returning the worst possible
   * metric, but only when using the OpenWatcom compiler.
   * It may be worth investigating using a different version of the Windows
   * SDK with OpenWatcom in the future, though this may be fixed in OpenWatcom
   * 2.0.
   */
  return (ULONG)-1;
#  else
  MIB_IPFORWARD_ROW2 row;
  SOCKADDR_INET      ignored;
  if (GetBestRoute2(/* The interface to use.  The index is ignored since we are
                     * passing a LUID.
                     */
                    luid, 0,
                    /* No specific source address. */
                    NULL,
                    /* Our destination address. */
                    dest,
                    /* No options. */
                    0,
                    /* The route row. */
                    &row,
                    /* The best source address, which we don't need. */
                    &ignored) != NO_ERROR
      /* If the metric is "unused" (-1) or too large for us to add the two
       * metrics, use the worst possible, thus sorting this last.
       */
      || row.Metric == (ULONG)-1 ||
      row.Metric > ((ULONG)-1) - interfaceMetric) {
    /* Return the worst possible metric. */
    return (ULONG)-1;
  }

  /* Return the metric value from that row, plus the interface metric.
   *
   * See
   * http://msdn.microsoft.com/en-us/library/windows/desktop/aa814494(v=vs.85).aspx
   * which describes the combination as a "sum".
   */
  return row.Metric + interfaceMetric;
#  endif /* __WATCOMC__ */
}

/*
 * get_DNS_Windows()
 *
 * Locates DNS info using GetAdaptersAddresses() function from the Internet
 * Protocol Helper (IP Helper) API. When located, this returns a pointer
 * in *outptr to a newly allocated memory area holding a null-terminated
 * string with a space or comma separated list of DNS IP addresses.
 *
 * Returns 0 and nullifies *outptr upon inability to return DNSes string.
 *
 * Returns 1 and sets *outptr when returning a dynamically allocated string.
 *
 * Implementation supports Windows XP and newer.
 */
#  define IPAA_INITIAL_BUF_SZ 15 * 1024
#  define IPAA_MAX_TRIES      3

static ares_bool_t get_DNS_Windows(char **outptr)
{
  IP_ADAPTER_DNS_SERVER_ADDRESS *ipaDNSAddr;
  IP_ADAPTER_ADDRESSES          *ipaa;
  IP_ADAPTER_ADDRESSES          *newipaa;
  IP_ADAPTER_ADDRESSES          *ipaaEntry;
  ULONG                          ReqBufsz  = IPAA_INITIAL_BUF_SZ;
  ULONG                          Bufsz     = IPAA_INITIAL_BUF_SZ;
  ULONG                          AddrFlags = 0;
  int                            trying    = IPAA_MAX_TRIES;
  ULONG                          res;

  /* The capacity of addresses, in elements. */
  size_t                         addressesSize;
  /* The number of elements in addresses. */
  size_t                         addressesIndex = 0;
  /* The addresses we will sort. */
  Address                       *addresses;

  union {
    struct sockaddr     *sa;
    struct sockaddr_in  *sa4;
    struct sockaddr_in6 *sa6;
  } namesrvr;

  *outptr = NULL;

  ipaa = ares_malloc(Bufsz);
  if (!ipaa) {
    return ARES_FALSE;
  }

  /* Start with enough room for a few DNS server addresses and we'll grow it
   * as we encounter more.
   */
  addressesSize = 4;
  addresses     = (Address *)ares_malloc(sizeof(Address) * addressesSize);
  if (addresses == NULL) {
    /* We need room for at least some addresses to function. */
    ares_free(ipaa);
    return ARES_FALSE;
  }

  /* Usually this call succeeds with initial buffer size */
  res = GetAdaptersAddresses(AF_UNSPEC, AddrFlags, NULL, ipaa, &ReqBufsz);
  if ((res != ERROR_BUFFER_OVERFLOW) && (res != ERROR_SUCCESS)) {
    goto done;
  }

  while ((res == ERROR_BUFFER_OVERFLOW) && (--trying)) {
    if (Bufsz < ReqBufsz) {
      newipaa = ares_realloc(ipaa, ReqBufsz);
      if (!newipaa) {
        goto done;
      }
      Bufsz = ReqBufsz;
      ipaa  = newipaa;
    }
    res = GetAdaptersAddresses(AF_UNSPEC, AddrFlags, NULL, ipaa, &ReqBufsz);
    if (res == ERROR_SUCCESS) {
      break;
    }
  }
  if (res != ERROR_SUCCESS) {
    goto done;
  }

  for (ipaaEntry = ipaa; ipaaEntry; ipaaEntry = ipaaEntry->Next) {
    if (ipaaEntry->OperStatus != IfOperStatusUp) {
      continue;
    }

    /* For each interface, find any associated DNS servers as IPv4 or IPv6
     * addresses.  For each found address, find the best route to that DNS
     * server address _on_ _that_ _interface_ (at this moment in time) and
     * compute the resulting total metric, just as Windows routing will do.
     * Then, sort all the addresses found by the metric.
     */
    for (ipaDNSAddr = ipaaEntry->FirstDnsServerAddress; ipaDNSAddr != NULL;
         ipaDNSAddr = ipaDNSAddr->Next) {
      char ipaddr[INET6_ADDRSTRLEN] = "";

      namesrvr.sa = ipaDNSAddr->Address.lpSockaddr;

      if (namesrvr.sa->sa_family == AF_INET) {
        if ((namesrvr.sa4->sin_addr.S_un.S_addr == INADDR_ANY) ||
            (namesrvr.sa4->sin_addr.S_un.S_addr == INADDR_NONE)) {
          continue;
        }

        /* Allocate room for another address, if necessary, else skip. */
        if (addressesIndex == addressesSize) {
          const size_t    newSize = addressesSize + 4;
          Address * const newMem =
            (Address *)ares_realloc(addresses, sizeof(Address) * newSize);
          if (newMem == NULL) {
            continue;
          }
          addresses     = newMem;
          addressesSize = newSize;
        }

        addresses[addressesIndex].metric = getBestRouteMetric(
          &ipaaEntry->Luid, (SOCKADDR_INET *)((void *)(namesrvr.sa)),
          ipaaEntry->Ipv4Metric);

        /* Record insertion index to make qsort stable */
        addresses[addressesIndex].orig_idx = addressesIndex;

        if (!ares_inet_ntop(AF_INET, &namesrvr.sa4->sin_addr, ipaddr,
                            sizeof(ipaddr))) {
          continue;
        }
        snprintf(addresses[addressesIndex].text,
                 sizeof(addresses[addressesIndex].text), "[%s]:%u", ipaddr,
                 ntohs(namesrvr.sa4->sin_port));
        ++addressesIndex;
      } else if (namesrvr.sa->sa_family == AF_INET6) {
        unsigned int     ll_scope = 0;
        struct ares_addr addr;

        if (memcmp(&namesrvr.sa6->sin6_addr, &ares_in6addr_any,
                   sizeof(namesrvr.sa6->sin6_addr)) == 0) {
          continue;
        }

        /* Allocate room for another address, if necessary, else skip. */
        if (addressesIndex == addressesSize) {
          const size_t    newSize = addressesSize + 4;
          Address * const newMem =
            (Address *)ares_realloc(addresses, sizeof(Address) * newSize);
          if (newMem == NULL) {
            continue;
          }
          addresses     = newMem;
          addressesSize = newSize;
        }

        /* See if its link-local */
        memset(&addr, 0, sizeof(addr));
        addr.family = AF_INET6;
        memcpy(&addr.addr.addr6, &namesrvr.sa6->sin6_addr, 16);
        if (ares_addr_is_linklocal(&addr)) {
          ll_scope = ipaaEntry->Ipv6IfIndex;
        }

        addresses[addressesIndex].metric = getBestRouteMetric(
          &ipaaEntry->Luid, (SOCKADDR_INET *)((void *)(namesrvr.sa)),
          ipaaEntry->Ipv6Metric);

        /* Record insertion index to make qsort stable */
        addresses[addressesIndex].orig_idx = addressesIndex;

        if (!ares_inet_ntop(AF_INET6, &namesrvr.sa6->sin6_addr, ipaddr,
                            sizeof(ipaddr))) {
          continue;
        }

        if (ll_scope) {
          snprintf(addresses[addressesIndex].text,
                   sizeof(addresses[addressesIndex].text), "[%s]:%u%%%u",
                   ipaddr, ntohs(namesrvr.sa6->sin6_port), ll_scope);
        } else {
          snprintf(addresses[addressesIndex].text,
                   sizeof(addresses[addressesIndex].text), "[%s]:%u", ipaddr,
                   ntohs(namesrvr.sa6->sin6_port));
        }
        ++addressesIndex;
      } else {
        /* Skip non-IPv4/IPv6 addresses completely. */
        continue;
      }
    }
  }

  /* Sort all of the textual addresses by their metric (and original index if
   * metrics are equal). */
  qsort(addresses, addressesIndex, sizeof(*addresses), compareAddresses);

  /* Join them all into a single string, removing duplicates. */
  {
    size_t i;
    for (i = 0; i < addressesIndex; ++i) {
      size_t j;
      /* Look for this address text appearing previously in the results. */
      for (j = 0; j < i; ++j) {
        if (strcmp(addresses[j].text, addresses[i].text) == 0) {
          break;
        }
      }
      /* Iff we didn't emit this address already, emit it now. */
      if (j == i) {
        /* Add that to outptr (if we can). */
        commajoin(outptr, addresses[i].text);
      }
    }
  }

done:
  ares_free(addresses);

  if (ipaa) {
    ares_free(ipaa);
  }

  if (!*outptr) {
    return ARES_FALSE;
  }

  return ARES_TRUE;
}

/*
 * get_SuffixList_Windows()
 *
 * Reads the "DNS Suffix Search List" from registry and writes the list items
 * whitespace separated to outptr. If the Search List is empty, the
 * "Primary Dns Suffix" is written to outptr.
 *
 * Returns 0 and nullifies *outptr upon inability to return the suffix list.
 *
 * Returns 1 and sets *outptr when returning a dynamically allocated string.
 *
 * Implementation supports Windows Server 2003 and newer
 */
static ares_bool_t get_SuffixList_Windows(char **outptr)
{
  HKEY  hKey;
  HKEY  hKeyEnum;
  char  keyName[256];
  DWORD keyNameBuffSize;
  DWORD keyIdx = 0;
  char *p      = NULL;

  *outptr = NULL;

  /* 1. Global DNS Suffix Search List */
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, WIN_NS_NT_KEY, 0, KEY_READ, &hKey) ==
      ERROR_SUCCESS) {
    get_REG_SZ(hKey, SEARCHLIST_KEY, outptr);
    if (get_REG_SZ(hKey, DOMAIN_KEY, &p)) {
      commajoin(outptr, p);
      ares_free(p);
      p = NULL;
    }
    RegCloseKey(hKey);
  }

  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, WIN_NT_DNSCLIENT, 0, KEY_READ, &hKey) ==
      ERROR_SUCCESS) {
    if (get_REG_SZ(hKey, SEARCHLIST_KEY, &p)) {
      commajoin(outptr, p);
      ares_free(p);
      p = NULL;
    }
    RegCloseKey(hKey);
  }

  /* 2. Connection Specific Search List composed of:
   *  a. Primary DNS Suffix */
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, WIN_DNSCLIENT, 0, KEY_READ, &hKey) ==
      ERROR_SUCCESS) {
    if (get_REG_SZ(hKey, PRIMARYDNSSUFFIX_KEY, &p)) {
      commajoin(outptr, p);
      ares_free(p);
      p = NULL;
    }
    RegCloseKey(hKey);
  }

  /*  b. Interface SearchList, Domain, DhcpDomain */
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, WIN_NS_NT_KEY "\\" INTERFACES_KEY, 0,
                    KEY_READ, &hKey) == ERROR_SUCCESS) {
    for (;;) {
      keyNameBuffSize = sizeof(keyName);
      if (RegEnumKeyExA(hKey, keyIdx++, keyName, &keyNameBuffSize, 0, NULL,
                        NULL, NULL) != ERROR_SUCCESS) {
        break;
      }
      if (RegOpenKeyExA(hKey, keyName, 0, KEY_QUERY_VALUE, &hKeyEnum) !=
          ERROR_SUCCESS) {
        continue;
      }
      /* p can be comma separated (SearchList) */
      if (get_REG_SZ(hKeyEnum, SEARCHLIST_KEY, &p)) {
        commajoin(outptr, p);
        ares_free(p);
        p = NULL;
      }
      if (get_REG_SZ(hKeyEnum, DOMAIN_KEY, &p)) {
        commajoin(outptr, p);
        ares_free(p);
        p = NULL;
      }
      if (get_REG_SZ(hKeyEnum, DHCPDOMAIN_KEY, &p)) {
        commajoin(outptr, p);
        ares_free(p);
        p = NULL;
      }
      RegCloseKey(hKeyEnum);
    }
    RegCloseKey(hKey);
  }

  return *outptr != NULL ? ARES_TRUE : ARES_FALSE;
}

ares_status_t ares_init_sysconfig_windows(const ares_channel_t *channel,
                                          ares_sysconfig_t     *sysconfig)
{
  char         *line   = NULL;
  ares_status_t status = ARES_SUCCESS;

  if (get_DNS_Windows(&line)) {
    status = ares_sconfig_append_fromstr(channel, &sysconfig->sconfig, line,
                                         ARES_TRUE);
    ares_free(line);
    if (status != ARES_SUCCESS) {
      goto done;
    }
  }

  if (get_SuffixList_Windows(&line)) {
    sysconfig->domains = ares_strsplit(line, ", ", &sysconfig->ndomains);
    ares_free(line);
    if (sysconfig->domains == NULL) {
      status = ARES_EFILE;
    }
    if (status != ARES_SUCCESS) {
      goto done;
    }
  }

done:
  return status;
}
#endif
