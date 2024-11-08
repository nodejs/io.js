/* MIT License
 *
 * Copyright (c) 2024 The c-ares project and its contributors
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

#ifdef __APPLE__

/* The DNS configuration for apple is stored in the system configuration
 * database.  Apple does provide an emulated `/etc/resolv.conf` on MacOS (but
 * not iOS), it cannot, however, represent the entirety of the DNS
 * configuration.  Alternatively, libresolv could be used to also retrieve some
 * system configuration, but it too is not capable of retrieving the entirety
 * of the DNS configuration.
 *
 * Attempts to use the preferred public API of `SCDynamicStoreCreate()` and
 * friends yielded incomplete DNS information.  Instead, that leaves some apple
 * "internal" symbols from `configd` that we need to access in order to get the
 * entire configuration.  We can see that we're not the only ones to do this as
 * Google Chrome also does:
 * https://chromium.googlesource.com/chromium/src/+/HEAD/net/dns/dns_config_watcher_mac.cc
 * These internal functions are what `libresolv` and `scutil` use to retrieve
 * the dns configuration.  Since these symbols are not publicly available, we
 * will dynamically load the symbols from `libSystem` and import the `dnsinfo.h`
 * private header extracted from:
 * https://opensource.apple.com/source/configd/configd-1109.140.1/dnsinfo/dnsinfo.h
 */

/* The apple header uses anonymous unions which came with C11 */
#  if defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wc11-extensions"
#  endif

#  include "ares_private.h"
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <dlfcn.h>
#  include <arpa/inet.h>
#  include "thirdparty/apple/dnsinfo.h"
#  include <AvailabilityMacros.h>
#  if MAC_OS_X_VERSION_MIN_REQUIRED >= 1080 /* MacOS 10.8 */
#    include <SystemConfiguration/SCNetworkConfiguration.h>
#  endif

typedef struct {
  void *handle;
  dns_config_t *(*dns_configuration_copy)(void);
  void (*dns_configuration_free)(dns_config_t *config);
} dnsinfo_t;

static void dnsinfo_destroy(dnsinfo_t *dnsinfo)
{
  if (dnsinfo == NULL) {
    return;
  }

  if (dnsinfo->handle) {
    dlclose(dnsinfo->handle);
  }

  ares_free(dnsinfo);
}

static ares_status_t dnsinfo_init(dnsinfo_t **dnsinfo_out)
{
  dnsinfo_t    *dnsinfo = NULL;
  ares_status_t status  = ARES_SUCCESS;
  size_t        i;
  const char   *searchlibs[] = {
    "/usr/lib/libSystem.dylib",
    "/System/Library/Frameworks/SystemConfiguration.framework/"
      "SystemConfiguration",
    NULL
  };

  if (dnsinfo_out == NULL) {
    status = ARES_EFORMERR;
    goto done;
  }

  *dnsinfo_out = NULL;

  dnsinfo = ares_malloc_zero(sizeof(*dnsinfo));
  if (dnsinfo == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  for (i = 0; searchlibs[i] != NULL; i++) {
    dnsinfo->handle = dlopen(searchlibs[i], RTLD_LAZY /* | RTLD_NOLOAD */);
    if (dnsinfo->handle == NULL) {
      /* Fail, loop */
      continue;
    }

    dnsinfo->dns_configuration_copy = (dns_config_t * (*)(void))
      dlsym(dnsinfo->handle, "dns_configuration_copy");

    dnsinfo->dns_configuration_free = (void (*)(dns_config_t *))dlsym(
      dnsinfo->handle, "dns_configuration_free");

    if (dnsinfo->dns_configuration_copy != NULL &&
        dnsinfo->dns_configuration_free != NULL) {
      break;
    }

    /* Fail, loop */
    dlclose(dnsinfo->handle);
    dnsinfo->handle = NULL;
  }


  if (dnsinfo->dns_configuration_copy == NULL ||
      dnsinfo->dns_configuration_free == NULL) {
    status = ARES_ESERVFAIL;
    goto done;
  }


done:
  if (status == ARES_SUCCESS) {
    *dnsinfo_out = dnsinfo;
  } else {
    dnsinfo_destroy(dnsinfo);
  }

  return status;
}

static ares_bool_t search_is_duplicate(const ares_sysconfig_t *sysconfig,
                                       const char             *name)
{
  size_t i;
  for (i = 0; i < sysconfig->ndomains; i++) {
    if (ares_strcaseeq(sysconfig->domains[i], name)) {
      return ARES_TRUE;
    }
  }
  return ARES_FALSE;
}

static ares_status_t read_resolver(const ares_channel_t *channel,
                                   const dns_resolver_t *resolver,
                                   ares_sysconfig_t     *sysconfig)
{
  int            i;
  unsigned short port   = 0;
  ares_status_t  status = ARES_SUCCESS;

#  if MAC_OS_X_VERSION_MIN_REQUIRED >= 1080 /* MacOS 10.8 */
  /* XXX: resolver->domain is for domain-specific servers.  When we implement
   *      this support, we'll want to use this.  But for now, we're going to
   *      skip any servers which set this since we can't properly route.
   *      MacOS used to use this setting for a different purpose in the
   *      past however, so on versions of MacOS < 10.8 just ignore this
   *      completely. */
  if (resolver->domain != NULL) {
    return ARES_SUCCESS;
  }
#  endif

#  if MAC_OS_X_VERSION_MIN_REQUIRED >= 1080 /* MacOS 10.8 */
  /* Check to see if DNS server should be used, base this on if the server is
   * reachable or can be reachable automatically if we send traffic that
   * direction. */
  if (!(resolver->reach_flags &
        (kSCNetworkFlagsReachable |
         kSCNetworkReachabilityFlagsConnectionOnTraffic))) {
    return ARES_SUCCESS;
  }
#  endif

  /* NOTE: it doesn't look like resolver->flags is relevant */

  /* If there's no nameservers, nothing to do */
  if (resolver->n_nameserver <= 0) {
    return ARES_SUCCESS;
  }

  /* Default port */
  port = resolver->port;

  /* Append search list */
  if (resolver->n_search > 0) {
    char **new_domains = ares_realloc_zero(
      sysconfig->domains, sizeof(*sysconfig->domains) * sysconfig->ndomains,
      sizeof(*sysconfig->domains) *
        (sysconfig->ndomains + (size_t)resolver->n_search));
    if (new_domains == NULL) {
      return ARES_ENOMEM;
    }
    sysconfig->domains = new_domains;

    for (i = 0; i < resolver->n_search; i++) {
      const char *search;
      /* UBSAN: copy pointer using memcpy due to misalignment */
      memcpy(&search, resolver->search + i, sizeof(search));

      /* Skip duplicates */
      if (search_is_duplicate(sysconfig, search)) {
        continue;
      }
      sysconfig->domains[sysconfig->ndomains] = ares_strdup(search);
      if (sysconfig->domains[sysconfig->ndomains] == NULL) {
        return ARES_ENOMEM;
      }
      sysconfig->ndomains++;
    }
  }

  /* NOTE: we're going to skip importing the sort addresses for now.  Its
   *       likely not used, its not obvious how to even configure such a thing.
   */
#  if 0
  for (i=0; i<resolver->n_sortaddr; i++) {
    char val[256];
    inet_ntop(AF_INET, &resolver->sortaddr[i]->address, val, sizeof(val));
    printf("\t\t%s/", val);
    inet_ntop(AF_INET, &resolver->sortaddr[i]->mask, val, sizeof(val));
    printf("%s\n", val);
  }
#  endif

  if (resolver->options != NULL) {
    status = ares_sysconfig_set_options(sysconfig, resolver->options);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  /* NOTE:
   *   - resolver->timeout appears unused, always 0, so we ignore this
   *   - resolver->service_identifier doesn't appear relevant to us
   *   - resolver->cid also isn't relevant
   *   - resolver->if_name we won't use since it isn't available in MacOS 10.8
   *     or earlier, use resolver->if_index instead to then lookup the name.
   */

  /* XXX: resolver->search_order appears like it might be relevant, we might
   * need to sort the resulting list by this metric if we find in the future we
   * need to.  That said, due to the automatic re-sorting we do, I'm not sure it
   * matters.  Here's an article on this search order stuff:
   *      https://www.cnet.com/tech/computing/os-x-10-6-3-and-dns-server-priority-changes/
   */

  for (i = 0; i < resolver->n_nameserver; i++) {
    struct ares_addr       addr;
    unsigned short         addrport;
    const struct sockaddr *sockaddr;
    char                   if_name_str[256] = "";
    const char            *if_name          = NULL;

    /* UBSAN alignment workaround to fetch memory address */
    memcpy(&sockaddr, resolver->nameserver + i, sizeof(sockaddr));

    if (!ares_sockaddr_to_ares_addr(&addr, &addrport, sockaddr)) {
      continue;
    }

    if (addrport == 0) {
      addrport = port;
    }

    if (channel->sock_funcs.aif_indextoname != NULL) {
      if_name = channel->sock_funcs.aif_indextoname(
        resolver->if_index, if_name_str, sizeof(if_name_str),
        channel->sock_func_cb_data);
    }

    status = ares_sconfig_append(channel, &sysconfig->sconfig, &addr, addrport,
                                 addrport, if_name);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  return status;
}

static ares_status_t read_resolvers(const ares_channel_t *channel,
                                    dns_resolver_t **resolvers, int nresolvers,
                                    ares_sysconfig_t *sysconfig)
{
  ares_status_t status = ARES_SUCCESS;
  int           i;

  for (i = 0; status == ARES_SUCCESS && i < nresolvers; i++) {
    const dns_resolver_t *resolver_ptr;

    /* UBSAN doesn't like that this is unaligned, lets use memcpy to get the
     * address.  Equivalent to:
     *   resolver = resolvers[i]
     */
    memcpy(&resolver_ptr, resolvers + i, sizeof(resolver_ptr));

    status = read_resolver(channel, resolver_ptr, sysconfig);
  }

  return status;
}

ares_status_t ares_init_sysconfig_macos(const ares_channel_t *channel,
                                        ares_sysconfig_t     *sysconfig)
{
  dnsinfo_t    *dnsinfo = NULL;
  dns_config_t *sc_dns  = NULL;
  ares_status_t status  = ARES_SUCCESS;

  status = dnsinfo_init(&dnsinfo);

  if (status != ARES_SUCCESS) {
    goto done;
  }

  sc_dns = dnsinfo->dns_configuration_copy();
  if (sc_dns == NULL) {
    status = ARES_ESERVFAIL;
    goto done;
  }

  /* There are `resolver`, `scoped_resolver`, and `service_specific_resolver`
   * settings. The `scoped_resolver` settings appear to be already available via
   * the `resolver` settings and likely are only relevant to link-local dns
   * servers which we can already detect via the address itself, so we'll ignore
   * the `scoped_resolver` section.  It isn't clear what the
   * `service_specific_resolver` is used for, I haven't personally seen it
   * in use so we'll ignore this until at some point where we find we need it.
   * Likely this wasn't available via `/etc/resolv.conf` nor `libresolv` anyhow
   * so its not worse to prior configuration methods, worst case. */

  status =
    read_resolvers(channel, sc_dns->resolver, sc_dns->n_resolver, sysconfig);

done:
  if (dnsinfo) {
    dnsinfo->dns_configuration_free(sc_dns);
    dnsinfo_destroy(dnsinfo);
  }
  return status;
}

#  if defined(__clang__)
#    pragma GCC diagnostic pop
#  endif

#else

/* Prevent compiler warnings due to empty translation unit */
typedef int make_iso_compilers_happy;

#endif
