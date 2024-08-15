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

#include "ares_nameser.h"

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
#include "ares_platform.h"
#include "event/ares_event.h"

int ares_init(ares_channel_t **channelptr)
{
  return ares_init_options(channelptr, NULL, 0);
}

static int ares_query_timeout_cmp_cb(const void *arg1, const void *arg2)
{
  const ares_query_t *q1 = arg1;
  const ares_query_t *q2 = arg2;

  if (q1->timeout.sec > q2->timeout.sec) {
    return 1;
  }
  if (q1->timeout.sec < q2->timeout.sec) {
    return -1;
  }

  if (q1->timeout.usec > q2->timeout.usec) {
    return 1;
  }
  if (q1->timeout.usec < q2->timeout.usec) {
    return -1;
  }

  return 0;
}

static int server_sort_cb(const void *data1, const void *data2)
{
  const ares_server_t *s1 = data1;
  const ares_server_t *s2 = data2;

  if (s1->consec_failures < s2->consec_failures) {
    return -1;
  }
  if (s1->consec_failures > s2->consec_failures) {
    return 1;
  }
  if (s1->idx < s2->idx) {
    return -1;
  }
  if (s1->idx > s2->idx) {
    return 1;
  }
  return 0;
}

static void server_destroy_cb(void *data)
{
  if (data == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }
  ares__destroy_server(data);
}

static ares_status_t init_by_defaults(ares_channel_t *channel)
{
  char         *hostname = NULL;
  ares_status_t rc       = ARES_SUCCESS;
#ifdef HAVE_GETHOSTNAME
  const char *dot;
#endif
  struct ares_addr addr;
  ares__llist_t   *sconfig = NULL;

  /* Enable EDNS by default */
  if (!(channel->optmask & ARES_OPT_FLAGS)) {
    channel->flags = ARES_FLAG_EDNS;
  }
  if (channel->ednspsz == 0) {
    channel->ednspsz = EDNSPACKETSZ;
  }

  if (channel->timeout == 0) {
    channel->timeout = DEFAULT_TIMEOUT;
  }

  if (channel->tries == 0) {
    channel->tries = DEFAULT_TRIES;
  }

  if (ares__slist_len(channel->servers) == 0) {
    /* Add a default local named server to the channel unless configured not
     * to (in which case return an error).
     */
    if (channel->flags & ARES_FLAG_NO_DFLT_SVR) {
      rc = ARES_ENOSERVER;
      goto error;
    }

    addr.family            = AF_INET;
    addr.addr.addr4.s_addr = htonl(INADDR_LOOPBACK);

    rc = ares__sconfig_append(&sconfig, &addr, 0, 0, NULL);
    if (rc != ARES_SUCCESS) {
      goto error; /* LCOV_EXCL_LINE: OutOfMemory */
    }

    rc = ares__servers_update(channel, sconfig, ARES_FALSE);
    ares__llist_destroy(sconfig);

    if (rc != ARES_SUCCESS) {
      goto error;
    }
  }

#if defined(USE_WINSOCK)
#  define toolong(x) (x == -1) && (SOCKERRNO == WSAEFAULT)
#elif defined(ENAMETOOLONG)
#  define toolong(x) \
    (x == -1) && ((SOCKERRNO == ENAMETOOLONG) || (SOCKERRNO == EINVAL))
#else
#  define toolong(x) (x == -1) && (SOCKERRNO == EINVAL)
#endif

  if (channel->ndomains == 0) {
    /* Derive a default domain search list from the kernel hostname,
     * or set it to empty if the hostname isn't helpful.
     */
#ifndef HAVE_GETHOSTNAME
    channel->ndomains = 0; /* default to none */
#else
    GETHOSTNAME_TYPE_ARG2 lenv = 64;
    size_t                len  = 64;
    int                   res;
    channel->ndomains = 0; /* default to none */

    hostname = ares_malloc(len);
    if (!hostname) {
      rc = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto error;       /* LCOV_EXCL_LINE: OutOfMemory */
    }

    do {
      res = gethostname(hostname, lenv);

      if (toolong(res)) {
        char *p;
        len  *= 2;
        lenv *= 2;
        p     = ares_realloc(hostname, len);
        if (!p) {
          rc = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
          goto error;       /* LCOV_EXCL_LINE: OutOfMemory */
        }
        hostname = p;
        continue;
      } else if (res) {
        /* Lets not treat a gethostname failure as critical, since we
         * are ok if gethostname doesn't even exist */
        *hostname = '\0';
        break;
      }

    } while (res != 0);

    dot = strchr(hostname, '.');
    if (dot) {
      /* a dot was found */
      channel->domains = ares_malloc(sizeof(char *));
      if (!channel->domains) {
        rc = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
        goto error;       /* LCOV_EXCL_LINE: OutOfMemory */
      }
      channel->domains[0] = ares_strdup(dot + 1);
      if (!channel->domains[0]) {
        rc = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
        goto error;       /* LCOV_EXCL_LINE: OutOfMemory */
      }
      channel->ndomains = 1;
    }
#endif
  }

  if (channel->nsort == 0) {
    channel->sortlist = NULL;
  }

  if (!channel->lookups) {
    channel->lookups = ares_strdup("fb");
    if (!channel->lookups) {
      rc = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  /* Set default fields for server failover behavior */
  if (!(channel->optmask & ARES_OPT_SERVER_FAILOVER)) {
    channel->server_retry_chance = DEFAULT_SERVER_RETRY_CHANCE;
    channel->server_retry_delay  = DEFAULT_SERVER_RETRY_DELAY;
  }

error:
  if (hostname) {
    ares_free(hostname);
  }

  return rc;
}

int ares_init_options(ares_channel_t           **channelptr,
                      const struct ares_options *options, int optmask)
{
  ares_channel_t *channel;
  ares_status_t   status = ARES_SUCCESS;

  if (ares_library_initialized() != ARES_SUCCESS) {
    return ARES_ENOTINITIALIZED; /* LCOV_EXCL_LINE: n/a on non-WinSock */
  }

  channel = ares_malloc_zero(sizeof(*channel));
  if (!channel) {
    *channelptr = NULL;
    return ARES_ENOMEM;
  }

  /* We are in a good state */
  channel->sys_up = ARES_TRUE;

  /* One option where zero is valid, so set default value here */
  channel->ndots = 1;

  status = ares__channel_threading_init(channel);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* Generate random key */
  channel->rand_state = ares__init_rand_state();
  if (channel->rand_state == NULL) {
    status = ARES_ENOMEM;
    DEBUGF(fprintf(stderr, "Error: init_id_key failed: %s\n",
                   ares_strerror(status)));
    goto done;
  }

  /* Initialize Server List */
  channel->servers =
    ares__slist_create(channel->rand_state, server_sort_cb, server_destroy_cb);
  if (channel->servers == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  /* Initialize our lists of queries */
  channel->all_queries = ares__llist_create(NULL);
  if (channel->all_queries == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  channel->queries_by_qid = ares__htable_szvp_create(NULL);
  if (channel->queries_by_qid == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  channel->queries_by_timeout =
    ares__slist_create(channel->rand_state, ares_query_timeout_cmp_cb, NULL);
  if (channel->queries_by_timeout == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  channel->connnode_by_socket = ares__htable_asvp_create(NULL);
  if (channel->connnode_by_socket == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  /* Initialize configuration by each of the four sources, from highest
   * precedence to lowest.
   */

  status = ares__init_by_options(channel, options, optmask);
  if (status != ARES_SUCCESS) {
    DEBUGF(fprintf(stderr, "Error: init_by_options failed: %s\n",
                   ares_strerror(status)));
    /* If we fail to apply user-specified options, fail the whole init process
     */
    goto done;
  }

  /* Go ahead and let it initialize the query cache even if the ttl is 0 and
   * completely unused.  This reduces the number of different code paths that
   * might be followed even if there is a minor performance hit. */
  status = ares__qcache_create(channel->rand_state, channel->qcache_max_ttl,
                               &channel->qcache);
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  if (status == ARES_SUCCESS) {
    status = ares__init_by_sysconfig(channel);
    if (status != ARES_SUCCESS) {
      DEBUGF(fprintf(stderr, "Error: init_by_sysconfig failed: %s\n",
                     ares_strerror(status)));
    }
  }

  /*
   * No matter what failed or succeeded, seed defaults to provide
   * useful behavior for things that we missed.
   */
  status = init_by_defaults(channel);
  if (status != ARES_SUCCESS) {
    DEBUGF(fprintf(stderr, "Error: init_by_defaults failed: %s\n",
                   ares_strerror(status)));
    goto done;
  }

  /* Initialize the event thread */
  if (channel->optmask & ARES_OPT_EVENT_THREAD) {
    ares_event_thread_t *e = NULL;

    status = ares_event_thread_init(channel);
    if (status != ARES_SUCCESS) {
      goto done; /* LCOV_EXCL_LINE: UntestablePath */
    }

    /* Initialize monitor for configuration changes.  In some rare cases,
     * ARES_ENOTIMP may occur (OpenWatcom), ignore this. */
    e      = channel->sock_state_cb_data;
    status = ares_event_configchg_init(&e->configchg, e);
    if (status != ARES_SUCCESS && status != ARES_ENOTIMP) {
      goto done; /* LCOV_EXCL_LINE: UntestablePath */
    }
    status = ARES_SUCCESS;
  }

done:
  if (status != ARES_SUCCESS) {
    ares_destroy(channel);
    return (int)status;
  }

  *channelptr = channel;
  return ARES_SUCCESS;
}

static void *ares_reinit_thread(void *arg)
{
  ares_channel_t *channel = arg;
  ares_status_t   status;

  /* ares__init_by_sysconfig() will lock when applying the config, but not
   * when retrieving. */
  status = ares__init_by_sysconfig(channel);
  if (status != ARES_SUCCESS) {
    DEBUGF(fprintf(stderr, "Error: init_by_sysconfig failed: %s\n",
                   ares_strerror(status)));
  }

  ares__channel_lock(channel);

  /* Flush cached queries on reinit */
  if (status == ARES_SUCCESS && channel->qcache) {
    ares__qcache_flush(channel->qcache);
  }

  channel->reinit_pending = ARES_FALSE;
  ares__channel_unlock(channel);

  return NULL;
}

ares_status_t ares_reinit(ares_channel_t *channel)
{
  ares_status_t status = ARES_SUCCESS;

  if (channel == NULL) {
    return ARES_EFORMERR;
  }

  ares__channel_lock(channel);

  /* If a reinit is already in process, lets not do it again. Or if we are
   * shutting down, skip. */
  if (!channel->sys_up || channel->reinit_pending) {
    ares__channel_unlock(channel);
    return ARES_SUCCESS;
  }
  channel->reinit_pending = ARES_TRUE;
  ares__channel_unlock(channel);

  if (ares_threadsafety()) {
    /* clean up the prior reinit process's thread.  We know the thread isn't
     * running since reinit_pending was false */
    if (channel->reinit_thread != NULL) {
      void *rv;
      ares__thread_join(channel->reinit_thread, &rv);
      channel->reinit_thread = NULL;
    }

    /* Spawn a new thread */
    status =
      ares__thread_create(&channel->reinit_thread, ares_reinit_thread, channel);
    if (status != ARES_SUCCESS) {
      /* LCOV_EXCL_START: UntestablePath */
      ares__channel_lock(channel);
      channel->reinit_pending = ARES_FALSE;
      ares__channel_unlock(channel);
      /* LCOV_EXCL_STOP */
    }
  } else {
    /* Threading support not available, call directly */
    ares_reinit_thread(channel);
  }

  return status;
}

/* ares_dup() duplicates a channel handle with all its options and returns a
   new channel handle */
int ares_dup(ares_channel_t **dest, const ares_channel_t *src)
{
  struct ares_options opts;
  ares_status_t       rc;
  int                 optmask;

  if (dest == NULL || src == NULL) {
    return ARES_EFORMERR;
  }

  *dest = NULL; /* in case of failure return NULL explicitly */

  /* First get the options supported by the old ares_save_options() function,
     which is most of them */
  rc = (ares_status_t)ares_save_options(src, &opts, &optmask);
  if (rc != ARES_SUCCESS) {
    ares_destroy_options(&opts);
    goto done;
  }

  /* Then create the new channel with those options */
  rc = (ares_status_t)ares_init_options(dest, &opts, optmask);

  /* destroy the options copy to not leak any memory */
  ares_destroy_options(&opts);

  if (rc != ARES_SUCCESS) {
    goto done;
  }

  ares__channel_lock(src);
  /* Now clone the options that ares_save_options() doesn't support, but are
   * user-provided */
  (*dest)->sock_create_cb       = src->sock_create_cb;
  (*dest)->sock_create_cb_data  = src->sock_create_cb_data;
  (*dest)->sock_config_cb       = src->sock_config_cb;
  (*dest)->sock_config_cb_data  = src->sock_config_cb_data;
  (*dest)->sock_funcs           = src->sock_funcs;
  (*dest)->sock_func_cb_data    = src->sock_func_cb_data;
  (*dest)->server_state_cb      = src->server_state_cb;
  (*dest)->server_state_cb_data = src->server_state_cb_data;

  ares_strcpy((*dest)->local_dev_name, src->local_dev_name,
              sizeof((*dest)->local_dev_name));
  (*dest)->local_ip4 = src->local_ip4;
  memcpy((*dest)->local_ip6, src->local_ip6, sizeof(src->local_ip6));
  ares__channel_unlock(src);

  /* Servers are a bit unique as ares_init_options() only allows ipv4 servers
   * and not a port per server, but there are other user specified ways, that
   * too will toggle the optmask ARES_OPT_SERVERS to let us know.  If that's
   * the case, pull them in.
   *
   * We don't want to clone system-configuration servers though.
   *
   * We must use the "csv" format to get things like link-local address support
   */

  if (optmask & ARES_OPT_SERVERS) {
    char *csv = ares_get_servers_csv(src);
    if (csv == NULL) {
      /* LCOV_EXCL_START: OutOfMemory */
      ares_destroy(*dest);
      *dest = NULL;
      rc    = ARES_ENOMEM;
      goto done;
      /* LCOV_EXCL_STOP */
    }

    rc = (ares_status_t)ares_set_servers_ports_csv(*dest, csv);
    ares_free_string(csv);
    if (rc != ARES_SUCCESS) {
      /* LCOV_EXCL_START: OutOfMemory */
      ares_destroy(*dest);
      *dest = NULL;
      goto done;
      /* LCOV_EXCL_STOP */
    }
  }

  rc = ARES_SUCCESS;
done:
  return (int)rc; /* everything went fine */
}

void ares_set_local_ip4(ares_channel_t *channel, unsigned int local_ip)
{
  if (channel == NULL) {
    return;
  }
  ares__channel_lock(channel);
  channel->local_ip4 = local_ip;
  ares__channel_unlock(channel);
}

/* local_ip6 should be 16 bytes in length */
void ares_set_local_ip6(ares_channel_t *channel, const unsigned char *local_ip6)
{
  if (channel == NULL) {
    return;
  }
  ares__channel_lock(channel);
  memcpy(&channel->local_ip6, local_ip6, sizeof(channel->local_ip6));
  ares__channel_unlock(channel);
}

/* local_dev_name should be null terminated. */
void ares_set_local_dev(ares_channel_t *channel, const char *local_dev_name)
{
  if (channel == NULL) {
    return;
  }

  ares__channel_lock(channel);
  ares_strcpy(channel->local_dev_name, local_dev_name,
              sizeof(channel->local_dev_name));
  channel->local_dev_name[sizeof(channel->local_dev_name) - 1] = 0;
  ares__channel_unlock(channel);
}

int ares_set_sortlist(ares_channel_t *channel, const char *sortstr)
{
  size_t           nsort    = 0;
  struct apattern *sortlist = NULL;
  ares_status_t    status;

  if (!channel) {
    return ARES_ENODATA;
  }
  ares__channel_lock(channel);

  status = ares__parse_sortlist(&sortlist, &nsort, sortstr);
  if (status == ARES_SUCCESS && sortlist) {
    if (channel->sortlist) {
      ares_free(channel->sortlist);
    }
    channel->sortlist = sortlist;
    channel->nsort    = nsort;

    /* Save sortlist as if it was passed in as an option */
    channel->optmask |= ARES_OPT_SORTLIST;
  }
  ares__channel_unlock(channel);
  return (int)status;
}
