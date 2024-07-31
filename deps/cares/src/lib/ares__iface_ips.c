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
#include "ares_private.h"

#ifdef USE_WINSOCK
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  if defined(HAVE_IPHLPAPI_H)
#    include <iphlpapi.h>
#  endif
#  if defined(HAVE_NETIOAPI_H)
#    include <netioapi.h>
#  endif
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
#ifdef HAVE_IFADDRS_H
#  include <ifaddrs.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#ifdef HAVE_NETDB_H
#  include <netdb.h>
#endif


static ares_status_t ares__iface_ips_enumerate(ares__iface_ips_t *ips,
                                               const char        *name);

typedef struct {
  char                  *name;
  struct ares_addr       addr;
  unsigned char          netmask;
  unsigned int           ll_scope;
  ares__iface_ip_flags_t flags;
} ares__iface_ip_t;

struct ares__iface_ips {
  ares__iface_ip_t      *ips;
  size_t                 cnt;
  size_t                 alloc_size;
  ares__iface_ip_flags_t enum_flags;
};

static ares__iface_ips_t *ares__iface_ips_alloc(ares__iface_ip_flags_t flags)
{
  ares__iface_ips_t *ips = ares_malloc_zero(sizeof(*ips));
  if (ips == NULL) {
    return NULL; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  /* Prealloc 4 entries */
  ips->alloc_size = 4;
  ips->ips        = ares_malloc_zero(ips->alloc_size * sizeof(*ips->ips));
  if (ips->ips == NULL) {
    ares_free(ips); /* LCOV_EXCL_LINE: OutOfMemory */
    return NULL;    /* LCOV_EXCL_LINE: OutOfMemory */
  }
  ips->enum_flags = flags;
  return ips;
}

static void ares__iface_ip_destroy(ares__iface_ip_t *ip)
{
  if (ip == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }
  ares_free(ip->name);
  memset(ip, 0, sizeof(*ip));
}

void ares__iface_ips_destroy(ares__iface_ips_t *ips)
{
  size_t i;

  if (ips == NULL) {
    return;
  }

  for (i = 0; i < ips->cnt; i++) {
    ares__iface_ip_destroy(&ips->ips[i]);
  }
  ares_free(ips->ips);
  ares_free(ips);
}

ares_status_t ares__iface_ips(ares__iface_ips_t    **ips,
                              ares__iface_ip_flags_t flags, const char *name)
{
  ares_status_t status;

  if (ips == NULL) {
    return ARES_EFORMERR;
  }

  *ips = ares__iface_ips_alloc(flags);
  if (*ips == NULL) {
    return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares__iface_ips_enumerate(*ips, name);
  if (status != ARES_SUCCESS) {
    /* LCOV_EXCL_START: UntestablePath */
    ares__iface_ips_destroy(*ips);
    *ips = NULL;
    return status;
    /* LCOV_EXCL_STOP */
  }

  return ARES_SUCCESS;
}

static ares_status_t
  ares__iface_ips_add(ares__iface_ips_t *ips, ares__iface_ip_flags_t flags,
                      const char *name, const struct ares_addr *addr,
                      unsigned char netmask, unsigned int ll_scope)
{
  size_t idx;

  if (ips == NULL || name == NULL || addr == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* Don't want loopback */
  if (flags & ARES_IFACE_IP_LOOPBACK &&
      !(ips->enum_flags & ARES_IFACE_IP_LOOPBACK)) {
    return ARES_SUCCESS;
  }

  /* Don't want offline */
  if (flags & ARES_IFACE_IP_OFFLINE &&
      !(ips->enum_flags & ARES_IFACE_IP_OFFLINE)) {
    return ARES_SUCCESS;
  }

  /* Check for link-local */
  if (ares__addr_is_linklocal(addr)) {
    flags |= ARES_IFACE_IP_LINKLOCAL;
  }
  if (flags & ARES_IFACE_IP_LINKLOCAL &&
      !(ips->enum_flags & ARES_IFACE_IP_LINKLOCAL)) {
    return ARES_SUCCESS;
  }

  /* Set address flag based on address provided */
  if (addr->family == AF_INET) {
    flags |= ARES_IFACE_IP_V4;
  }

  if (addr->family == AF_INET6) {
    flags |= ARES_IFACE_IP_V6;
  }

  /* If they specified either v4 or v6 validate flags otherwise assume they
   * want to enumerate both */
  if (ips->enum_flags & (ARES_IFACE_IP_V4 | ARES_IFACE_IP_V6)) {
    if (flags & ARES_IFACE_IP_V4 && !(ips->enum_flags & ARES_IFACE_IP_V4)) {
      return ARES_SUCCESS;
    }
    if (flags & ARES_IFACE_IP_V6 && !(ips->enum_flags & ARES_IFACE_IP_V6)) {
      return ARES_SUCCESS;
    }
  }

  /* Allocate more ips */
  if (ips->cnt + 1 > ips->alloc_size) {
    void  *temp;
    size_t alloc_size;

    alloc_size = ares__round_up_pow2(ips->alloc_size + 1);
    temp = ares_realloc_zero(ips->ips, ips->alloc_size * sizeof(*ips->ips),
                             alloc_size * sizeof(*ips->ips));
    if (temp == NULL) {
      return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    }
    ips->ips        = temp;
    ips->alloc_size = alloc_size;
  }

  /* Add */
  idx = ips->cnt++;

  ips->ips[idx].flags   = flags;
  ips->ips[idx].netmask = netmask;
  if (flags & ARES_IFACE_IP_LINKLOCAL) {
    ips->ips[idx].ll_scope = ll_scope;
  }
  memcpy(&ips->ips[idx].addr, addr, sizeof(*addr));
  ips->ips[idx].name = ares_strdup(name);
  if (ips->ips[idx].name == NULL) {
    return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  return ARES_SUCCESS;
}

size_t ares__iface_ips_cnt(const ares__iface_ips_t *ips)
{
  if (ips == NULL) {
    return 0;
  }
  return ips->cnt;
}

const char *ares__iface_ips_get_name(const ares__iface_ips_t *ips, size_t idx)
{
  if (ips == NULL || idx >= ips->cnt) {
    return NULL;
  }
  return ips->ips[idx].name;
}

const struct ares_addr *ares__iface_ips_get_addr(const ares__iface_ips_t *ips,
                                                 size_t                   idx)
{
  if (ips == NULL || idx >= ips->cnt) {
    return NULL;
  }
  return &ips->ips[idx].addr;
}

ares__iface_ip_flags_t ares__iface_ips_get_flags(const ares__iface_ips_t *ips,
                                                 size_t                   idx)
{
  if (ips == NULL || idx >= ips->cnt) {
    return 0;
  }
  return ips->ips[idx].flags;
}

unsigned char ares__iface_ips_get_netmask(const ares__iface_ips_t *ips,
                                          size_t                   idx)
{
  if (ips == NULL || idx >= ips->cnt) {
    return 0;
  }
  return ips->ips[idx].netmask;
}

unsigned int ares__iface_ips_get_ll_scope(const ares__iface_ips_t *ips,
                                          size_t                   idx)
{
  if (ips == NULL || idx >= ips->cnt) {
    return 0;
  }
  return ips->ips[idx].ll_scope;
}


#ifdef USE_WINSOCK

#  if 0
static char *wcharp_to_charp(const wchar_t *in)
{
  char *out;
  int   len;

  len = WideCharToMultiByte(CP_UTF8, 0, in, -1, NULL, 0, NULL, NULL);
  if (len == -1) {
    return NULL;
  }

  out = ares_malloc_zero((size_t)len + 1);

  if (WideCharToMultiByte(CP_UTF8, 0, in, -1, out, len, NULL, NULL) == -1) {
    ares_free(out);
    return NULL;
  }

  return out;
}
#  endif

static ares_bool_t name_match(const char *name, const char *adapter_name,
                              unsigned int ll_scope)
{
  if (name == NULL || *name == 0) {
    return ARES_TRUE;
  }

  if (strcasecmp(name, adapter_name) == 0) {
    return ARES_TRUE;
  }

  if (ares_str_isnum(name) && (unsigned int)atoi(name) == ll_scope) {
    return ARES_TRUE;
  }

  return ARES_FALSE;
}

static ares_status_t ares__iface_ips_enumerate(ares__iface_ips_t *ips,
                                               const char        *name)
{
  ULONG myflags = GAA_FLAG_INCLUDE_PREFIX /*|GAA_FLAG_INCLUDE_ALL_INTERFACES */;
  ULONG outBufLen = 0;
  DWORD retval;
  IP_ADAPTER_ADDRESSES *addresses = NULL;
  IP_ADAPTER_ADDRESSES *address   = NULL;
  ares_status_t         status    = ARES_SUCCESS;

  /* Get necessary buffer size */
  GetAdaptersAddresses(AF_UNSPEC, myflags, NULL, NULL, &outBufLen);
  if (outBufLen == 0) {
    status = ARES_EFILE;
    goto done;
  }

  addresses = ares_malloc_zero(outBufLen);
  if (addresses == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  retval =
    GetAdaptersAddresses(AF_UNSPEC, myflags, NULL, addresses, &outBufLen);
  if (retval != ERROR_SUCCESS) {
    status = ARES_EFILE;
    goto done;
  }

  for (address = addresses; address != NULL; address = address->Next) {
    IP_ADAPTER_UNICAST_ADDRESS *ipaddr     = NULL;
    ares__iface_ip_flags_t      addrflag   = 0;
    char                        ifname[64] = "";

#  if defined(HAVE_CONVERTINTERFACEINDEXTOLUID) && \
    defined(HAVE_CONVERTINTERFACELUIDTONAMEA)
    /* Retrieve name from interface index.
     * address->AdapterName appears to be a GUID/UUID of some sort, not a name.
     * address->FriendlyName is user-changeable.
     * That said, this doesn't appear to help us out on systems that don't
     * have if_nametoindex() or if_indextoname() as they don't have these
     * functions either! */
    NET_LUID luid;
    ConvertInterfaceIndexToLuid(address->IfIndex, &luid);
    ConvertInterfaceLuidToNameA(&luid, ifname, sizeof(ifname));
#  else
    ares_strcpy(ifname, address->AdapterName, sizeof(ifname));
#  endif

    if (address->OperStatus != IfOperStatusUp) {
      addrflag |= ARES_IFACE_IP_OFFLINE;
    }

    if (address->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
      addrflag |= ARES_IFACE_IP_LOOPBACK;
    }

    for (ipaddr = address->FirstUnicastAddress; ipaddr != NULL;
         ipaddr = ipaddr->Next) {
      struct ares_addr addr;

      if (ipaddr->Address.lpSockaddr->sa_family == AF_INET) {
        const struct sockaddr_in *sockaddr_in =
          (const struct sockaddr_in *)((void *)ipaddr->Address.lpSockaddr);
        addr.family = AF_INET;
        memcpy(&addr.addr.addr4, &sockaddr_in->sin_addr,
               sizeof(addr.addr.addr4));
      } else if (ipaddr->Address.lpSockaddr->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sockaddr_in6 =
          (const struct sockaddr_in6 *)((void *)ipaddr->Address.lpSockaddr);
        addr.family = AF_INET6;
        memcpy(&addr.addr.addr6, &sockaddr_in6->sin6_addr,
               sizeof(addr.addr.addr6));
      } else {
        /* Unknown */
        continue;
      }

      /* Sometimes windows may use numerics to indicate a DNS server's adapter,
       * which corresponds to the index rather than the name.  Check and
       * validate both. */
      if (!name_match(name, ifname, address->Ipv6IfIndex)) {
        continue;
      }

      status = ares__iface_ips_add(ips, addrflag, ifname, &addr,
                                   ipaddr->OnLinkPrefixLength /* netmask */,
                                   address->Ipv6IfIndex /* ll_scope */);

      if (status != ARES_SUCCESS) {
        goto done;
      }
    }
  }

done:
  ares_free(addresses);
  return status;
}

#elif defined(HAVE_GETIFADDRS)

static unsigned char count_addr_bits(const unsigned char *addr, size_t addr_len)
{
  size_t        i;
  unsigned char count = 0;

  for (i = 0; i < addr_len; i++) {
    count += ares__count_bits_u8(addr[i]);
  }
  return count;
}

static ares_status_t ares__iface_ips_enumerate(ares__iface_ips_t *ips,
                                               const char        *name)
{
  struct ifaddrs *ifap   = NULL;
  struct ifaddrs *ifa    = NULL;
  ares_status_t   status = ARES_SUCCESS;

  if (getifaddrs(&ifap) != 0) {
    status = ARES_EFILE;
    goto done;
  }

  for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
    ares__iface_ip_flags_t addrflag = 0;
    struct ares_addr       addr;
    unsigned char          netmask  = 0;
    unsigned int           ll_scope = 0;

    if (ifa->ifa_addr == NULL) {
      continue;
    }

    if (!(ifa->ifa_flags & IFF_UP)) {
      addrflag |= ARES_IFACE_IP_OFFLINE;
    }

    if (ifa->ifa_flags & IFF_LOOPBACK) {
      addrflag |= ARES_IFACE_IP_LOOPBACK;
    }

    if (ifa->ifa_addr->sa_family == AF_INET) {
      const struct sockaddr_in *sockaddr_in =
        (const struct sockaddr_in *)((void *)ifa->ifa_addr);
      addr.family = AF_INET;
      memcpy(&addr.addr.addr4, &sockaddr_in->sin_addr, sizeof(addr.addr.addr4));
      /* netmask */
      sockaddr_in = (struct sockaddr_in *)((void *)ifa->ifa_netmask);
      netmask     = count_addr_bits((const void *)&sockaddr_in->sin_addr, 4);
    } else if (ifa->ifa_addr->sa_family == AF_INET6) {
      const struct sockaddr_in6 *sockaddr_in6 =
        (const struct sockaddr_in6 *)((void *)ifa->ifa_addr);
      addr.family = AF_INET6;
      memcpy(&addr.addr.addr6, &sockaddr_in6->sin6_addr,
             sizeof(addr.addr.addr6));
      /* netmask */
      sockaddr_in6 = (struct sockaddr_in6 *)((void *)ifa->ifa_netmask);
      netmask = count_addr_bits((const void *)&sockaddr_in6->sin6_addr, 16);
#  ifdef HAVE_STRUCT_SOCKADDR_IN6_SIN6_SCOPE_ID
      ll_scope = sockaddr_in6->sin6_scope_id;
#  endif
    } else {
      /* unknown */
      continue;
    }

    /* Name mismatch */
    if (name != NULL && strcasecmp(ifa->ifa_name, name) != 0) {
      continue;
    }

    status = ares__iface_ips_add(ips, addrflag, ifa->ifa_name, &addr, netmask,
                                 ll_scope);
    if (status != ARES_SUCCESS) {
      goto done;
    }
  }

done:
  freeifaddrs(ifap);
  return status;
}

#else

static ares_status_t ares__iface_ips_enumerate(ares__iface_ips_t *ips,
                                               const char        *name)
{
  (void)ips;
  (void)name;
  return ARES_ENOTIMP;
}

#endif


unsigned int ares__if_nametoindex(const char *name)
{
#ifdef HAVE_IF_NAMETOINDEX
  if (name == NULL) {
    return 0;
  }
  return if_nametoindex(name);
#else
  ares_status_t      status;
  ares__iface_ips_t *ips = NULL;
  size_t             i;
  unsigned int       index = 0;

  if (name == NULL) {
    return 0;
  }

  status =
    ares__iface_ips(&ips, ARES_IFACE_IP_V6 | ARES_IFACE_IP_LINKLOCAL, name);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  for (i = 0; i < ares__iface_ips_cnt(ips); i++) {
    if (ares__iface_ips_get_flags(ips, i) & ARES_IFACE_IP_LINKLOCAL) {
      index = ares__iface_ips_get_ll_scope(ips, i);
      goto done;
    }
  }

done:
  ares__iface_ips_destroy(ips);
  return index;
#endif
}

const char *ares__if_indextoname(unsigned int index, char *name,
                                 size_t name_len)
{
#ifdef HAVE_IF_INDEXTONAME
  if (name_len < IF_NAMESIZE) {
    return NULL;
  }
  return if_indextoname(index, name);
#else
  ares_status_t      status;
  ares__iface_ips_t *ips = NULL;
  size_t             i;
  const char        *ptr = NULL;

  if (name == NULL || name_len < IF_NAMESIZE) {
    goto done;
  }

  if (index == 0) {
    goto done;
  }

  status =
    ares__iface_ips(&ips, ARES_IFACE_IP_V6 | ARES_IFACE_IP_LINKLOCAL, NULL);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  for (i = 0; i < ares__iface_ips_cnt(ips); i++) {
    if (ares__iface_ips_get_flags(ips, i) & ARES_IFACE_IP_LINKLOCAL &&
        ares__iface_ips_get_ll_scope(ips, i) == index) {
      ares_strcpy(name, ares__iface_ips_get_name(ips, i), name_len);
      ptr = name;
      goto done;
    }
  }

done:
  ares__iface_ips_destroy(ips);
  return ptr;
#endif
}
