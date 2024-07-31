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
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#  include <sys/stat.h>
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
#include <time.h>
#include "ares_platform.h"

/* HOSTS FILE PROCESSING OVERVIEW
 * ==============================
 * The hosts file on the system contains static entries to be processed locally
 * rather than querying the nameserver.  Each row is an IP address followed by
 * a list of space delimited hostnames that match the ip address.  This is used
 * for both forward and reverse lookups.
 *
 * We are caching the entire parsed hosts file for performance reasons.  Some
 * files may be quite sizable and as per Issue #458 can approach 1/2MB in size,
 * and the parse overhead on a rapid succession of queries can be quite large.
 * The entries are stored in forwards and backwards hashtables so we can get
 * O(1) performance on lookup.  The file is cached until the file modification
 * timestamp changes.
 *
 * The hosts file processing is quite unique. It has to merge all related hosts
 * and ips into a single entry due to file formatting requirements.  For
 * instance take the below:
 *
 * 127.0.0.1    localhost.localdomain localhost
 * ::1          localhost.localdomain localhost
 * 192.168.1.1  host.example.com host
 * 192.168.1.5  host.example.com host
 * 2620:1234::1 host.example.com host6.example.com host6 host
 *
 * This will yield 2 entries.
 *  1) ips: 127.0.0.1,::1
 *     hosts: localhost.localdomain,localhost
 *  2) ips: 192.168.1.1,192.168.1.5,2620:1234::1
 *     hosts: host.example.com,host,host6.example.com,host6
 *
 * It could be argued that if searching for 192.168.1.1 that the 'host6'
 * hostnames should not be returned, but this implementation will return them
 * since they are related.  It is unlikely this will matter in the real world.
 */

struct ares_hosts_file {
  time_t                ts;
  /*! cache the filename so we know if the filename changes it automatically
   *  invalidates the cache */
  char                 *filename;
  /*! iphash is the owner of the 'entry' object as there is only ever a single
   *  match to the object. */
  ares__htable_strvp_t *iphash;
  /*! hosthash does not own the entry so won't free on destruction */
  ares__htable_strvp_t *hosthash;
};

struct ares_hosts_entry {
  size_t         refcnt; /*! If the entry is stored multiple times in the
                          *  ip address hash, we have to reference count it */
  ares__llist_t *ips;
  ares__llist_t *hosts;
};

const void *ares_dns_pton(const char *ipaddr, struct ares_addr *addr,
                          size_t *out_len)
{
  const void *ptr     = NULL;
  size_t      ptr_len = 0;

  if (ipaddr == NULL || addr == NULL || out_len == NULL) {
    return NULL; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  *out_len = 0;

  if (addr->family == AF_INET &&
      ares_inet_pton(AF_INET, ipaddr, &addr->addr.addr4) > 0) {
    ptr     = &addr->addr.addr4;
    ptr_len = sizeof(addr->addr.addr4);
  } else if (addr->family == AF_INET6 &&
             ares_inet_pton(AF_INET6, ipaddr, &addr->addr.addr6) > 0) {
    ptr     = &addr->addr.addr6;
    ptr_len = sizeof(addr->addr.addr6);
  } else if (addr->family == AF_UNSPEC) {
    if (ares_inet_pton(AF_INET, ipaddr, &addr->addr.addr4) > 0) {
      addr->family = AF_INET;
      ptr          = &addr->addr.addr4;
      ptr_len      = sizeof(addr->addr.addr4);
    } else if (ares_inet_pton(AF_INET6, ipaddr, &addr->addr.addr6) > 0) {
      addr->family = AF_INET6;
      ptr          = &addr->addr.addr6;
      ptr_len      = sizeof(addr->addr.addr6);
    }
  }

  *out_len = ptr_len;
  return ptr;
}

static ares_bool_t ares__normalize_ipaddr(const char *ipaddr, char *out,
                                          size_t out_len)
{
  struct ares_addr data;
  const void      *addr;
  size_t           addr_len = 0;

  memset(&data, 0, sizeof(data));
  data.family = AF_UNSPEC;

  addr = ares_dns_pton(ipaddr, &data, &addr_len);
  if (addr == NULL) {
    return ARES_FALSE;
  }

  if (!ares_inet_ntop(data.family, addr, out, (ares_socklen_t)out_len)) {
    return ARES_FALSE; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  return ARES_TRUE;
}

static void ares__hosts_entry_destroy(ares_hosts_entry_t *entry)
{
  if (entry == NULL) {
    return;
  }

  /* Honor reference counting */
  if (entry->refcnt != 0) {
    entry->refcnt--;
  }

  if (entry->refcnt > 0) {
    return;
  }

  ares__llist_destroy(entry->hosts);
  ares__llist_destroy(entry->ips);
  ares_free(entry);
}

static void ares__hosts_entry_destroy_cb(void *entry)
{
  ares__hosts_entry_destroy(entry);
}

void ares__hosts_file_destroy(ares_hosts_file_t *hf)
{
  if (hf == NULL) {
    return;
  }

  ares_free(hf->filename);
  ares__htable_strvp_destroy(hf->hosthash);
  ares__htable_strvp_destroy(hf->iphash);
  ares_free(hf);
}

static ares_hosts_file_t *ares__hosts_file_create(const char *filename)
{
  ares_hosts_file_t *hf = ares_malloc_zero(sizeof(*hf));
  if (hf == NULL) {
    goto fail;
  }

  hf->ts = time(NULL);

  hf->filename = ares_strdup(filename);
  if (hf->filename == NULL) {
    goto fail;
  }

  hf->iphash = ares__htable_strvp_create(ares__hosts_entry_destroy_cb);
  if (hf->iphash == NULL) {
    goto fail;
  }

  hf->hosthash = ares__htable_strvp_create(NULL);
  if (hf->hosthash == NULL) {
    goto fail;
  }

  return hf;

fail:
  ares__hosts_file_destroy(hf);
  return NULL;
}

typedef enum {
  ARES_MATCH_NONE   = 0,
  ARES_MATCH_IPADDR = 1,
  ARES_MATCH_HOST   = 2
} ares_hosts_file_match_t;

static ares_status_t ares__hosts_file_merge_entry(
  const ares_hosts_file_t *hf, ares_hosts_entry_t *existing,
  ares_hosts_entry_t *entry, ares_hosts_file_match_t matchtype)
{
  ares__llist_node_t *node;

  /* If we matched on IP address, we know there can only be 1, so there's no
   * reason to do anything */
  if (matchtype != ARES_MATCH_IPADDR) {
    while ((node = ares__llist_node_first(entry->ips)) != NULL) {
      const char *ipaddr = ares__llist_node_val(node);

      if (ares__htable_strvp_get_direct(hf->iphash, ipaddr) != NULL) {
        ares__llist_node_destroy(node);
        continue;
      }

      ares__llist_node_move_parent_last(node, existing->ips);
    }
  }


  while ((node = ares__llist_node_first(entry->hosts)) != NULL) {
    const char *hostname = ares__llist_node_val(node);

    if (ares__htable_strvp_get_direct(hf->hosthash, hostname) != NULL) {
      ares__llist_node_destroy(node);
      continue;
    }

    ares__llist_node_move_parent_last(node, existing->hosts);
  }

  ares__hosts_entry_destroy(entry);
  return ARES_SUCCESS;
}

static ares_hosts_file_match_t
  ares__hosts_file_match(const ares_hosts_file_t *hf, ares_hosts_entry_t *entry,
                         ares_hosts_entry_t **match)
{
  ares__llist_node_t *node;
  *match = NULL;

  for (node = ares__llist_node_first(entry->ips); node != NULL;
       node = ares__llist_node_next(node)) {
    const char *ipaddr = ares__llist_node_val(node);
    *match             = ares__htable_strvp_get_direct(hf->iphash, ipaddr);
    if (*match != NULL) {
      return ARES_MATCH_IPADDR;
    }
  }

  for (node = ares__llist_node_first(entry->hosts); node != NULL;
       node = ares__llist_node_next(node)) {
    const char *host = ares__llist_node_val(node);
    *match           = ares__htable_strvp_get_direct(hf->hosthash, host);
    if (*match != NULL) {
      return ARES_MATCH_HOST;
    }
  }

  return ARES_MATCH_NONE;
}

/*! entry is invalidated upon calling this function, always, even on error */
static ares_status_t ares__hosts_file_add(ares_hosts_file_t  *hosts,
                                          ares_hosts_entry_t *entry)
{
  ares_hosts_entry_t     *match  = NULL;
  ares_status_t           status = ARES_SUCCESS;
  ares__llist_node_t     *node;
  ares_hosts_file_match_t matchtype;
  size_t                  num_hostnames;

  /* Record the number of hostnames in this entry file.  If we merge into an
   * existing record, these will be *appended* to the entry, so we'll count
   * backwards when adding to the hosts hashtable */
  num_hostnames = ares__llist_len(entry->hosts);

  matchtype = ares__hosts_file_match(hosts, entry, &match);

  if (matchtype != ARES_MATCH_NONE) {
    status = ares__hosts_file_merge_entry(hosts, match, entry, matchtype);
    if (status != ARES_SUCCESS) {
      ares__hosts_entry_destroy(entry); /* LCOV_EXCL_LINE: DefensiveCoding */
      return status;                    /* LCOV_EXCL_LINE: DefensiveCoding */
    }
    /* entry was invalidated above by merging */
    entry = match;
  }

  if (matchtype != ARES_MATCH_IPADDR) {
    const char *ipaddr = ares__llist_last_val(entry->ips);

    if (!ares__htable_strvp_get(hosts->iphash, ipaddr, NULL)) {
      if (!ares__htable_strvp_insert(hosts->iphash, ipaddr, entry)) {
        ares__hosts_entry_destroy(entry);
        return ARES_ENOMEM;
      }
      entry->refcnt++;
    }
  }

  /* Go backwards, on a merge, hostnames are appended.  Breakout once we've
   * consumed all the hosts that we appended */
  for (node = ares__llist_node_last(entry->hosts); node != NULL;
       node = ares__llist_node_prev(node)) {
    const char *val = ares__llist_node_val(node);

    if (num_hostnames == 0) {
      break;
    }

    num_hostnames--;

    /* first hostname match wins.  If we detect a duplicate hostname for another
     * ip it will automatically be added to the same entry */
    if (ares__htable_strvp_get(hosts->hosthash, val, NULL)) {
      continue;
    }

    if (!ares__htable_strvp_insert(hosts->hosthash, val, entry)) {
      return ARES_ENOMEM;
    }
  }

  return ARES_SUCCESS;
}

static ares_bool_t ares__hosts_entry_isdup(ares_hosts_entry_t *entry,
                                           const char         *host)
{
  ares__llist_node_t *node;

  for (node = ares__llist_node_first(entry->ips); node != NULL;
       node = ares__llist_node_next(node)) {
    const char *myhost = ares__llist_node_val(node);
    if (strcasecmp(myhost, host) == 0) {
      return ARES_TRUE;
    }
  }

  return ARES_FALSE;
}

static ares_status_t ares__parse_hosts_hostnames(ares__buf_t        *buf,
                                                 ares_hosts_entry_t *entry)
{
  entry->hosts = ares__llist_create(ares_free);
  if (entry->hosts == NULL) {
    return ARES_ENOMEM;
  }

  /* Parse hostnames and aliases */
  while (ares__buf_len(buf)) {
    char          hostname[256];
    char         *temp;
    ares_status_t status;
    unsigned char comment = '#';

    ares__buf_consume_whitespace(buf, ARES_FALSE);

    if (ares__buf_len(buf) == 0) {
      break;
    }

    /* See if it is a comment, if so stop processing */
    if (ares__buf_begins_with(buf, &comment, 1)) {
      break;
    }

    ares__buf_tag(buf);

    /* Must be at end of line */
    if (ares__buf_consume_nonwhitespace(buf) == 0) {
      break;
    }

    status = ares__buf_tag_fetch_string(buf, hostname, sizeof(hostname));
    if (status != ARES_SUCCESS) {
      /* Bad entry, just ignore as long as its not the first.  If its the first,
       * it must be valid */
      if (ares__llist_len(entry->hosts) == 0) {
        return ARES_EBADSTR;
      }

      continue;
    }

    /* Validate it is a valid hostname characterset */
    if (!ares__is_hostname(hostname)) {
      continue;
    }

    /* Don't add a duplicate to the same entry */
    if (ares__hosts_entry_isdup(entry, hostname)) {
      continue;
    }

    /* Add to list */
    temp = ares_strdup(hostname);
    if (temp == NULL) {
      return ARES_ENOMEM;
    }

    if (ares__llist_insert_last(entry->hosts, temp) == NULL) {
      ares_free(temp);
      return ARES_ENOMEM;
    }
  }

  /* Must have at least 1 entry */
  if (ares__llist_len(entry->hosts) == 0) {
    return ARES_EBADSTR;
  }

  return ARES_SUCCESS;
}

static ares_status_t ares__parse_hosts_ipaddr(ares__buf_t         *buf,
                                              ares_hosts_entry_t **entry_out)
{
  char                addr[INET6_ADDRSTRLEN];
  char               *temp;
  ares_hosts_entry_t *entry = NULL;
  ares_status_t       status;

  *entry_out = NULL;

  ares__buf_tag(buf);
  ares__buf_consume_nonwhitespace(buf);
  status = ares__buf_tag_fetch_string(buf, addr, sizeof(addr));
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Validate and normalize the ip address format */
  if (!ares__normalize_ipaddr(addr, addr, sizeof(addr))) {
    return ARES_EBADSTR;
  }

  entry = ares_malloc_zero(sizeof(*entry));
  if (entry == NULL) {
    return ARES_ENOMEM;
  }

  entry->ips = ares__llist_create(ares_free);
  if (entry->ips == NULL) {
    ares__hosts_entry_destroy(entry);
    return ARES_ENOMEM;
  }

  temp = ares_strdup(addr);
  if (temp == NULL) {
    ares__hosts_entry_destroy(entry);
    return ARES_ENOMEM;
  }

  if (ares__llist_insert_first(entry->ips, temp) == NULL) {
    ares_free(temp);
    ares__hosts_entry_destroy(entry);
    return ARES_ENOMEM;
  }

  *entry_out = entry;

  return ARES_SUCCESS;
}

static ares_status_t ares__parse_hosts(const char         *filename,
                                       ares_hosts_file_t **out)
{
  ares__buf_t        *buf    = NULL;
  ares_status_t       status = ARES_EBADRESP;
  ares_hosts_file_t  *hf     = NULL;
  ares_hosts_entry_t *entry  = NULL;

  *out = NULL;

  buf = ares__buf_create();
  if (buf == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  status = ares__buf_load_file(filename, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  hf = ares__hosts_file_create(filename);
  if (hf == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  while (ares__buf_len(buf)) {
    unsigned char comment = '#';

    /* -- Start of new line here -- */

    /* Consume any leading whitespace */
    ares__buf_consume_whitespace(buf, ARES_FALSE);

    if (ares__buf_len(buf) == 0) {
      break;
    }

    /* See if it is a comment, if so, consume remaining line */
    if (ares__buf_begins_with(buf, &comment, 1)) {
      ares__buf_consume_line(buf, ARES_TRUE);
      continue;
    }

    /* Pull off ip address */
    status = ares__parse_hosts_ipaddr(buf, &entry);
    if (status == ARES_ENOMEM) {
      goto done;
    }
    if (status != ARES_SUCCESS) {
      /* Bad line, consume and go onto next */
      ares__buf_consume_line(buf, ARES_TRUE);
      continue;
    }

    /* Parse of the hostnames */
    status = ares__parse_hosts_hostnames(buf, entry);
    if (status == ARES_ENOMEM) {
      goto done;
    } else if (status != ARES_SUCCESS) {
      /* Bad line, consume and go onto next */
      ares__hosts_entry_destroy(entry);
      entry = NULL;
      ares__buf_consume_line(buf, ARES_TRUE);
      continue;
    }

    /* Append the successful entry to the hosts file */
    status = ares__hosts_file_add(hf, entry);
    entry  = NULL; /* is always invalidated by this function, even on error */
    if (status != ARES_SUCCESS) {
      goto done;
    }

    /* Go to next line */
    ares__buf_consume_line(buf, ARES_TRUE);
  }

  status = ARES_SUCCESS;

done:
  ares__hosts_entry_destroy(entry);
  ares__buf_destroy(buf);
  if (status != ARES_SUCCESS) {
    ares__hosts_file_destroy(hf);
  } else {
    *out = hf;
  }
  return status;
}

static ares_bool_t ares__hosts_expired(const char              *filename,
                                       const ares_hosts_file_t *hf)
{
  time_t mod_ts = 0;

#ifdef HAVE_STAT
  struct stat st;
  if (stat(filename, &st) == 0) {
    mod_ts = st.st_mtime;
  }
#elif defined(_WIN32)
  struct _stat st;
  if (_stat(filename, &st) == 0) {
    mod_ts = st.st_mtime;
  }
#else
  (void)filename;
#endif

  if (hf == NULL) {
    return ARES_TRUE;
  }

  /* Expire every 60s if we can't get a time */
  if (mod_ts == 0) {
    mod_ts =
      time(NULL) - 60; /* LCOV_EXCL_LINE: only on systems without stat() */
  }

  /* If filenames are different, its expired */
  if (strcasecmp(hf->filename, filename) != 0) {
    return ARES_TRUE;
  }

  if (hf->ts <= mod_ts) {
    return ARES_TRUE;
  }

  return ARES_FALSE;
}

static ares_status_t ares__hosts_path(const ares_channel_t *channel,
                                      ares_bool_t use_env, char **path)
{
  char *path_hosts = NULL;

  *path = NULL;

  if (channel->hosts_path) {
    path_hosts = ares_strdup(channel->hosts_path);
    if (!path_hosts) {
      return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  if (use_env) {
    if (path_hosts) {
      ares_free(path_hosts);
    }

    path_hosts = ares_strdup(getenv("CARES_HOSTS"));
    if (!path_hosts) {
      return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  if (!path_hosts) {
#if defined(USE_WINSOCK)
    char  PATH_HOSTS[MAX_PATH] = "";
    char  tmp[MAX_PATH];
    HKEY  hkeyHosts;
    DWORD dwLength = sizeof(tmp);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, WIN_NS_NT_KEY, 0, KEY_READ,
                      &hkeyHosts) != ERROR_SUCCESS) {
      return ARES_ENOTFOUND;
    }
    RegQueryValueExA(hkeyHosts, DATABASEPATH, NULL, NULL, (LPBYTE)tmp,
                     &dwLength);
    ExpandEnvironmentStringsA(tmp, PATH_HOSTS, MAX_PATH);
    RegCloseKey(hkeyHosts);
    strcat(PATH_HOSTS, WIN_PATH_HOSTS);
#elif defined(WATT32)
    const char *PATH_HOSTS = _w32_GetHostsFile();

    if (!PATH_HOSTS) {
      return ARES_ENOTFOUND;
    }
#endif
    path_hosts = ares_strdup(PATH_HOSTS);
    if (!path_hosts) {
      return ARES_ENOMEM;
    }
  }

  *path = path_hosts;
  return ARES_SUCCESS;
}

static ares_status_t ares__hosts_update(ares_channel_t *channel,
                                        ares_bool_t     use_env)
{
  ares_status_t status;
  char         *filename = NULL;

  status = ares__hosts_path(channel, use_env, &filename);
  if (status != ARES_SUCCESS) {
    return status;
  }

  if (!ares__hosts_expired(filename, channel->hf)) {
    ares_free(filename);
    return ARES_SUCCESS;
  }

  ares__hosts_file_destroy(channel->hf);
  channel->hf = NULL;

  status = ares__parse_hosts(filename, &channel->hf);
  ares_free(filename);
  return status;
}

ares_status_t ares__hosts_search_ipaddr(ares_channel_t *channel,
                                        ares_bool_t use_env, const char *ipaddr,
                                        const ares_hosts_entry_t **entry)
{
  ares_status_t status;
  char          addr[INET6_ADDRSTRLEN];

  *entry = NULL;

  status = ares__hosts_update(channel, use_env);
  if (status != ARES_SUCCESS) {
    return status;
  }

  if (channel->hf == NULL) {
    return ARES_ENOTFOUND; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (!ares__normalize_ipaddr(ipaddr, addr, sizeof(addr))) {
    return ARES_EBADNAME;
  }

  *entry = ares__htable_strvp_get_direct(channel->hf->iphash, addr);
  if (*entry == NULL) {
    return ARES_ENOTFOUND;
  }

  return ARES_SUCCESS;
}

ares_status_t ares__hosts_search_host(ares_channel_t *channel,
                                      ares_bool_t use_env, const char *host,
                                      const ares_hosts_entry_t **entry)
{
  ares_status_t status;

  *entry = NULL;

  status = ares__hosts_update(channel, use_env);
  if (status != ARES_SUCCESS) {
    return status;
  }

  if (channel->hf == NULL) {
    return ARES_ENOTFOUND; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  *entry = ares__htable_strvp_get_direct(channel->hf->hosthash, host);
  if (*entry == NULL) {
    return ARES_ENOTFOUND;
  }

  return ARES_SUCCESS;
}

static ares_status_t
  ares__hosts_ai_append_cnames(const ares_hosts_entry_t    *entry,
                               struct ares_addrinfo_cname **cnames_out)
{
  struct ares_addrinfo_cname *cname  = NULL;
  struct ares_addrinfo_cname *cnames = NULL;
  const char                 *primaryhost;
  ares__llist_node_t         *node;
  ares_status_t               status;
  size_t                      cnt = 0;

  node        = ares__llist_node_first(entry->hosts);
  primaryhost = ares__llist_node_val(node);
  /* Skip to next node to start with aliases */
  node = ares__llist_node_next(node);

  while (node != NULL) {
    const char *host = ares__llist_node_val(node);

    /* Cap at 100 entries. , some people use
     * https://github.com/StevenBlack/hosts and we don't need 200k+ aliases */
    cnt++;
    if (cnt > 100) {
      break; /* LCOV_EXCL_LINE: DefensiveCoding */
    }

    cname = ares__append_addrinfo_cname(&cnames);
    if (cname == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }

    cname->alias = ares_strdup(host);
    if (cname->alias == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }

    cname->name = ares_strdup(primaryhost);
    if (cname->name == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }

    node = ares__llist_node_next(node);
  }

  /* No entries, add only primary */
  if (cnames == NULL) {
    cname = ares__append_addrinfo_cname(&cnames);
    if (cname == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }

    cname->name = ares_strdup(primaryhost);
    if (cname->name == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }
  status = ARES_SUCCESS;

done:
  if (status != ARES_SUCCESS) {
    ares__freeaddrinfo_cnames(cnames); /* LCOV_EXCL_LINE: DefensiveCoding */
    return status;                     /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  *cnames_out = cnames;
  return ARES_SUCCESS;
}

ares_status_t ares__hosts_entry_to_addrinfo(const ares_hosts_entry_t *entry,
                                            const char *name, int family,
                                            unsigned short        port,
                                            ares_bool_t           want_cnames,
                                            struct ares_addrinfo *ai)
{
  ares_status_t               status;
  struct ares_addrinfo_cname *cnames  = NULL;
  struct ares_addrinfo_node  *ainodes = NULL;
  ares__llist_node_t         *node;

  switch (family) {
    case AF_INET:
    case AF_INET6:
    case AF_UNSPEC:
      break;
    default:                  /* LCOV_EXCL_LINE: DefensiveCoding */
      return ARES_EBADFAMILY; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (name != NULL) {
    ai->name = ares_strdup(name);
    if (ai->name == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  for (node = ares__llist_node_first(entry->ips); node != NULL;
       node = ares__llist_node_next(node)) {
    struct ares_addr addr;
    const void      *ptr     = NULL;
    size_t           ptr_len = 0;
    const char      *ipaddr  = ares__llist_node_val(node);

    memset(&addr, 0, sizeof(addr));
    addr.family = family;
    ptr         = ares_dns_pton(ipaddr, &addr, &ptr_len);

    if (ptr == NULL) {
      continue;
    }

    status = ares_append_ai_node(addr.family, port, 0, ptr, &ainodes);
    if (status != ARES_SUCCESS) {
      goto done; /* LCOV_EXCL_LINE: DefensiveCoding */
    }
  }

  if (want_cnames) {
    status = ares__hosts_ai_append_cnames(entry, &cnames);
    if (status != ARES_SUCCESS) {
      goto done; /* LCOV_EXCL_LINE: DefensiveCoding */
    }
  }

  status = ARES_SUCCESS;

done:
  if (status != ARES_SUCCESS) {
    /* LCOV_EXCL_START: defensive coding */
    ares__freeaddrinfo_cnames(cnames);
    ares__freeaddrinfo_nodes(ainodes);
    ares_free(ai->name);
    ai->name = NULL;
    return status;
    /* LCOV_EXCL_STOP */
  }
  ares__addrinfo_cat_cnames(&ai->cnames, cnames);
  ares__addrinfo_cat_nodes(&ai->nodes, ainodes);

  return status;
}

ares_status_t ares__hosts_entry_to_hostent(const ares_hosts_entry_t *entry,
                                           int family, struct hostent **hostent)
{
  ares_status_t         status;
  struct ares_addrinfo *ai = ares_malloc_zero(sizeof(*ai));

  *hostent = NULL;

  if (ai == NULL) {
    return ARES_ENOMEM;
  }

  status = ares__hosts_entry_to_addrinfo(entry, NULL, family, 0, ARES_TRUE, ai);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares__addrinfo2hostent(ai, family, hostent);
  if (status != ARES_SUCCESS) {
    goto done;
  }

done:
  ares_freeaddrinfo(ai);
  if (status != ARES_SUCCESS) {
    ares_free_hostent(*hostent);
    *hostent = NULL;
  }

  return status;
}
