/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (c) 2010 Daniel Stenberg
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
#ifndef __ARES_PRIVATE_H
#define __ARES_PRIVATE_H

/* ============================================================================
 * NOTE: All c-ares source files should include ares_private.h as the first
 *       header.
 * ============================================================================
 */

#include "ares_setup.h"
#include "ares.h"

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include "ares_mem.h"
#include "ares_ipv6.h"
#include "util/ares_math.h"
#include "util/ares_time.h"
#include "util/ares_rand.h"
#include "ares_array.h"
#include "ares_llist.h"
#include "dsa/ares_slist.h"
#include "ares_htable_strvp.h"
#include "ares_htable_szvp.h"
#include "ares_htable_asvp.h"
#include "ares_htable_dict.h"
#include "ares_htable_vpvp.h"
#include "ares_htable_vpstr.h"
#include "record/ares_dns_multistring.h"
#include "ares_buf.h"
#include "record/ares_dns_private.h"
#include "util/ares_iface_ips.h"
#include "util/ares_threads.h"
#include "ares_socket.h"
#include "ares_conn.h"
#include "ares_str.h"
#include "str/ares_strsplit.h"
#include "util/ares_uri.h"

#ifndef HAVE_GETENV
#  include "ares_getenv.h"
#  define getenv(ptr) ares_getenv(ptr)
#endif

#define DEFAULT_TIMEOUT 2000 /* milliseconds */
#define DEFAULT_TRIES   3
#ifndef INADDR_NONE
#  define INADDR_NONE 0xffffffff
#endif

/* By using a double cast, we can get rid of the bogus warning of
 * warning: cast from 'const struct sockaddr *' to 'const struct sockaddr_in6 *'
 * increases required alignment from 1 to 4 [-Wcast-align]
 */
#define CARES_INADDR_CAST(type, var) ((type)((const void *)var))

#if defined(USE_WINSOCK)

#  define WIN_NS_9X     "System\\CurrentControlSet\\Services\\VxD\\MSTCP"
#  define WIN_NS_NT_KEY "System\\CurrentControlSet\\Services\\Tcpip\\Parameters"
#  define WIN_DNSCLIENT "Software\\Policies\\Microsoft\\System\\DNSClient"
#  define WIN_NT_DNSCLIENT \
    "Software\\Policies\\Microsoft\\Windows NT\\DNSClient"
#  define NAMESERVER           "NameServer"
#  define DHCPNAMESERVER       "DhcpNameServer"
#  define DATABASEPATH         "DatabasePath"
#  define WIN_PATH_HOSTS       "\\hosts"
#  define SEARCHLIST_KEY       "SearchList"
#  define PRIMARYDNSSUFFIX_KEY "PrimaryDNSSuffix"
#  define INTERFACES_KEY       "Interfaces"
#  define DOMAIN_KEY           "Domain"
#  define DHCPDOMAIN_KEY       "DhcpDomain"
#  define PATH_RESOLV_CONF     ""
#elif defined(WATT32)

#  define PATH_RESOLV_CONF "/dev/ENV/etc/resolv.conf"
W32_FUNC const char *_w32_GetHostsFile(void);

#elif defined(NETWARE)

#  define PATH_RESOLV_CONF "sys:/etc/resolv.cfg"
#  define PATH_HOSTS       "sys:/etc/hosts"

#elif defined(__riscos__)

#  define PATH_RESOLV_CONF ""
#  define PATH_HOSTS       "InetDBase:Hosts"

#elif defined(__HAIKU__)

#  define PATH_RESOLV_CONF "/system/settings/network/resolv.conf"
#  define PATH_HOSTS       "/system/settings/network/hosts"

#else

#  define PATH_RESOLV_CONF "/etc/resolv.conf"
#  ifdef ETC_INET
#    define PATH_HOSTS "/etc/inet/hosts"
#  else
#    define PATH_HOSTS "/etc/hosts"
#  endif

#endif

/********* EDNS defines section ******/
#define EDNSPACKETSZ                                          \
  1232 /* Reasonable UDP payload size, as agreed by operators \
          https://www.dnsflagday.net/2020/#faq */
#define MAXENDSSZ   4096 /* Maximum (local) limit for edns packet size */
#define EDNSFIXEDSZ 11   /* Size of EDNS header */

/********* EDNS defines section ******/

/* Default values for server failover behavior. We retry failed servers with
 * a 10% probability and a minimum delay of 5 seconds between retries.
 */
#define DEFAULT_SERVER_RETRY_CHANCE 10
#define DEFAULT_SERVER_RETRY_DELAY  5000

struct ares_query;
typedef struct ares_query ares_query_t;

/* State to represent a DNS query */
struct ares_query {
  /* Query ID from qbuf, for faster lookup, and current timeout */
  unsigned short       qid; /* host byte order */
  ares_timeval_t       ts;  /*!< Timestamp query was sent */
  ares_timeval_t       timeout;
  ares_channel_t      *channel;

  /*
   * Node object for each list entry the query belongs to in order to
   * make removal operations O(1).
   */
  ares_slist_node_t   *node_queries_by_timeout;
  ares_llist_node_t   *node_queries_to_conn;
  ares_llist_node_t   *node_all_queries;

  /* connection handle query is associated with */
  ares_conn_t         *conn;

  /* Query */
  ares_dns_record_t   *query;

  ares_callback_dnsrec callback;
  void                *arg;

  /* Query status */
  size_t        try_count; /* Number of times we tried this query already. */
  size_t        cookie_try_count; /* Attempt count for cookie resends */
  ares_bool_t   using_tcp;
  ares_status_t error_status;
  size_t        timeouts;   /* number of timeouts we saw for this request */
  ares_bool_t   no_retries; /* do not perform any additional retries, this is
                             * set when a query is to be canceled */
};

struct apattern {
  struct ares_addr addr;
  unsigned char    mask;
};

struct ares_qcache;
typedef struct ares_qcache ares_qcache_t;

struct ares_hosts_file;
typedef struct ares_hosts_file ares_hosts_file_t;

struct ares_channeldata {
  /* Configuration data */
  unsigned int         flags;
  size_t               timeout; /* in milliseconds */
  size_t               tries;
  size_t               ndots;
  size_t               maxtimeout;                 /* in milliseconds */
  ares_bool_t          rotate;
  unsigned short       udp_port;                   /* stored in network order */
  unsigned short       tcp_port;                   /* stored in network order */
  int                  socket_send_buffer_size;    /* setsockopt takes int */
  int                  socket_receive_buffer_size; /* setsockopt takes int */
  char               **domains;
  size_t               ndomains;
  struct apattern     *sortlist;
  size_t               nsort;
  char                *lookups;
  size_t               ednspsz;
  unsigned int         qcache_max_ttl;
  ares_evsys_t         evsys;
  unsigned int         optmask;

  /* For binding to local devices and/or IP addresses.  Leave
   * them null/zero for no binding.
   */
  char                 local_dev_name[32];
  unsigned int         local_ip4;
  unsigned char        local_ip6[16];

  /* Thread safety lock */
  ares_thread_mutex_t *lock;

  /* Conditional to wake waiters when queue is empty */
  ares_thread_cond_t  *cond_empty;

  /* Server addresses and communications state. Sorted by least consecutive
   * failures, followed by the configuration order if failures are equal. */
  ares_slist_t        *servers;

  /* random state to use when generating new ids and generating retry penalties
   */
  ares_rand_state     *rand_state;

  /* All active queries in a single list */
  ares_llist_t        *all_queries;
  /* Queries bucketed by qid, for quickly dispatching DNS responses: */
  ares_htable_szvp_t  *queries_by_qid;

  /* Queries bucketed by timeout, for quickly handling timeouts: */
  ares_slist_t        *queries_by_timeout;

  /* Map linked list node member for connection to file descriptor.  We use
   * the node instead of the connection object itself so we can quickly look
   * up a connection and remove it if necessary (as otherwise we'd have to
   * scan all connections) */
  ares_htable_asvp_t  *connnode_by_socket;

  ares_sock_state_cb   sock_state_cb;
  void                *sock_state_cb_data;

  ares_sock_create_callback           sock_create_cb;
  void                               *sock_create_cb_data;

  ares_sock_config_callback           sock_config_cb;
  void                               *sock_config_cb_data;

  struct ares_socket_functions_ex     sock_funcs;
  void                               *sock_func_cb_data;
  const struct ares_socket_functions *legacy_sock_funcs;
  void                               *legacy_sock_funcs_cb_data;

  ares_pending_write_cb               notify_pending_write_cb;
  void                               *notify_pending_write_cb_data;
  ares_bool_t                         notify_pending_write;

  /* Path for resolv.conf file, configurable via ares_options */
  char                               *resolvconf_path;

  /* Path for hosts file, configurable via ares_options */
  char                               *hosts_path;

  /* Maximum UDP queries per connection allowed */
  size_t                              udp_max_queries;

  /* Cache of local hosts file */
  ares_hosts_file_t                  *hf;

  /* Query Cache */
  ares_qcache_t                      *qcache;

  /* Fields controlling server failover behavior.
   * The retry chance is the probability (1/N) by which we will retry a failed
   * server instead of the best server when selecting a server to send queries
   * to.
   * The retry delay is the minimum time in milliseconds to wait between doing
   * such retries (applied per-server).
   */
  unsigned short                      server_retry_chance;
  size_t                              server_retry_delay;

  /* Callback triggered when a server has a successful or failed response */
  ares_server_state_callback          server_state_cb;
  void                               *server_state_cb_data;

  /* TRUE if a reinit is pending.  Reinit spawns a thread to read the system
   * configuration and then apply the configuration since configuration
   * reading may block.  The thread handle is provided for waiting on thread
   * exit. */
  ares_bool_t                         reinit_pending;
  ares_thread_t                      *reinit_thread;

  /* Whether the system is up or not.  This is mainly to prevent deadlocks
   * and access violations during the cleanup process.  Some things like
   * system config changes might get triggered and we need a flag to make
   * sure we don't take action. */
  ares_bool_t                         sys_up;
};

/* Does the domain end in ".onion" or ".onion."? Case-insensitive. */
ares_bool_t   ares_is_onion_domain(const char *name);

/* Returns one of the normal ares status codes like ARES_SUCCESS */
ares_status_t ares_send_query(ares_server_t *requested_server /* Optional */,
                              ares_query_t *query, const ares_timeval_t *now);
ares_status_t ares_requeue_query(ares_query_t *query, const ares_timeval_t *now,
                                 ares_status_t            status,
                                 ares_bool_t              inc_try_count,
                                 const ares_dns_record_t *dnsrec);

/*! Count the number of labels (dots+1) in a domain */
size_t        ares_name_label_cnt(const char *name);

/*! Retrieve a list of names to use for searching.  The first successful
 *  query in the list wins.  This function also uses the HOSTSALIASES file
 *  as well as uses channel configuration to determine the search order.
 *
 *  \param[in]  channel   initialized ares channel
 *  \param[in]  name      initial name being searched
 *  \param[out] names     array of names to attempt, use ares_strsplit_free()
 *                        when no longer needed.
 *  \param[out] names_len number of names in array
 *  \return ARES_SUCCESS on success, otherwise one of the other error codes.
 */
ares_status_t ares_search_name_list(const ares_channel_t *channel,
                                    const char *name, char ***names,
                                    size_t *names_len);

/*! Function to create callback arg for converting from ares_callback_dnsrec
 *  to ares_calback */
void         *ares_dnsrec_convert_arg(ares_callback callback, void *arg);

/*! Callback function used to convert from the ares_callback_dnsrec prototype to
 *  the ares_callback prototype, by writing the result and passing that to
 *  the inner callback.
 */
void ares_dnsrec_convert_cb(void *arg, ares_status_t status, size_t timeouts,
                            const ares_dns_record_t *dnsrec);

void ares_free_query(ares_query_t *query);

unsigned short ares_generate_new_id(ares_rand_state *state);
ares_status_t  ares_expand_name_validated(const unsigned char *encoded,
                                          const unsigned char *abuf, size_t alen,
                                          char **s, size_t *enclen,
                                          ares_bool_t is_hostname);
ares_status_t  ares_expand_string_ex(const unsigned char *encoded,
                                     const unsigned char *abuf, size_t alen,
                                     unsigned char **s, size_t *enclen);
ares_status_t  ares_init_servers_state(ares_channel_t *channel);
ares_status_t  ares_init_by_options(ares_channel_t            *channel,
                                    const struct ares_options *options,
                                    int                        optmask);
ares_status_t  ares_init_by_sysconfig(ares_channel_t *channel);
void           ares_set_socket_functions_def(ares_channel_t *channel);

typedef struct {
  ares_llist_t    *sconfig;
  struct apattern *sortlist;
  size_t           nsortlist;
  char           **domains;
  size_t           ndomains;
  char            *lookups;
  size_t           ndots;
  size_t           tries;
  ares_bool_t      rotate;
  size_t           timeout_ms;
  ares_bool_t      usevc;
} ares_sysconfig_t;

ares_status_t ares_sysconfig_set_options(ares_sysconfig_t *sysconfig,
                                         const char       *str);

ares_status_t ares_init_by_environment(ares_sysconfig_t *sysconfig);

ares_status_t ares_init_sysconfig_files(const ares_channel_t *channel,
                                        ares_sysconfig_t     *sysconfig);
#ifdef __APPLE__
ares_status_t ares_init_sysconfig_macos(const ares_channel_t *channel,
                                        ares_sysconfig_t     *sysconfig);
#endif
#ifdef USE_WINSOCK
ares_status_t ares_init_sysconfig_windows(const ares_channel_t *channel,
                                          ares_sysconfig_t     *sysconfig);
#endif

ares_status_t ares_parse_sortlist(struct apattern **sortlist, size_t *nsort,
                                  const char *str);

/* Returns ARES_SUCCESS if alias found, alias is set.  Returns ARES_ENOTFOUND
 * if not alias found.  Returns other errors on critical failure like
 * ARES_ENOMEM */
ares_status_t ares_lookup_hostaliases(const ares_channel_t *channel,
                                      const char *name, char **alias);

ares_status_t ares_cat_domain(const char *name, const char *domain, char **s);
ares_status_t ares_sortaddrinfo(ares_channel_t            *channel,
                                struct ares_addrinfo_node *ai_node);

void          ares_freeaddrinfo_nodes(struct ares_addrinfo_node *ai_node);
ares_bool_t   ares_is_localhost(const char *name);

struct ares_addrinfo_node    *
  ares_append_addrinfo_node(struct ares_addrinfo_node **ai_node);
void ares_addrinfo_cat_nodes(struct ares_addrinfo_node **head,
                             struct ares_addrinfo_node  *tail);

void ares_freeaddrinfo_cnames(struct ares_addrinfo_cname *ai_cname);

struct ares_addrinfo_cname             *
  ares_append_addrinfo_cname(struct ares_addrinfo_cname **ai_cname);

ares_status_t ares_append_ai_node(int aftype, unsigned short port,
                                  unsigned int ttl, const void *adata,
                                  struct ares_addrinfo_node **nodes);

void          ares_addrinfo_cat_cnames(struct ares_addrinfo_cname **head,
                                       struct ares_addrinfo_cname  *tail);

ares_status_t ares_parse_into_addrinfo(const ares_dns_record_t *dnsrec,
                                       ares_bool_t    cname_only_is_enodata,
                                       unsigned short port,
                                       struct ares_addrinfo *ai);
ares_status_t ares_parse_ptr_reply_dnsrec(const ares_dns_record_t *dnsrec,
                                          const void *addr, int addrlen,
                                          int family, struct hostent **host);

ares_status_t ares_addrinfo2hostent(const struct ares_addrinfo *ai, int family,
                                    struct hostent **host);
ares_status_t ares_addrinfo2addrttl(const struct ares_addrinfo *ai, int family,
                                    size_t                req_naddrttls,
                                    struct ares_addrttl  *addrttls,
                                    struct ares_addr6ttl *addr6ttls,
                                    size_t               *naddrttls);
ares_status_t ares_addrinfo_localhost(const char *name, unsigned short port,
                                      const struct ares_addrinfo_hints *hints,
                                      struct ares_addrinfo             *ai);

ares_status_t ares_servers_update(ares_channel_t *channel,
                                  ares_llist_t   *server_list,
                                  ares_bool_t     user_specified);
ares_status_t
  ares_sconfig_append(const ares_channel_t *channel, ares_llist_t **sconfig,
                      const struct ares_addr *addr, unsigned short udp_port,
                      unsigned short tcp_port, const char *ll_iface);
ares_status_t ares_sconfig_append_fromstr(const ares_channel_t *channel,
                                          ares_llist_t        **sconfig,
                                          const char           *str,
                                          ares_bool_t           ignore_invalid);
ares_status_t ares_in_addr_to_sconfig_llist(const struct in_addr *servers,
                                            size_t                nservers,
                                            ares_llist_t        **llist);
ares_status_t ares_get_server_addr(const ares_server_t *server,
                                   ares_buf_t          *buf);

struct ares_hosts_entry;
typedef struct ares_hosts_entry ares_hosts_entry_t;

void                            ares_hosts_file_destroy(ares_hosts_file_t *hf);
ares_status_t ares_hosts_search_ipaddr(ares_channel_t *channel,
                                       ares_bool_t use_env, const char *ipaddr,
                                       const ares_hosts_entry_t **entry);
ares_status_t ares_hosts_search_host(ares_channel_t *channel,
                                     ares_bool_t use_env, const char *host,
                                     const ares_hosts_entry_t **entry);
ares_status_t ares_hosts_entry_to_hostent(const ares_hosts_entry_t *entry,
                                          int family, struct hostent **hostent);
ares_status_t ares_hosts_entry_to_addrinfo(const ares_hosts_entry_t *entry,
                                           const char *name, int family,
                                           unsigned short        port,
                                           ares_bool_t           want_cnames,
                                           struct ares_addrinfo *ai);

/* Same as ares_query_dnsrec() except does not take a channel lock.  Use this
 * if a channel lock is already held */
ares_status_t ares_query_nolock(ares_channel_t *channel, const char *name,
                                ares_dns_class_t     dnsclass,
                                ares_dns_rec_type_t  type,
                                ares_callback_dnsrec callback, void *arg,
                                unsigned short *qid);

/*! Flags controlling behavior for ares_send_nolock() */
typedef enum {
  ARES_SEND_FLAG_NOCACHE = 1 << 0, /*!< Do not query the cache */
  ARES_SEND_FLAG_NORETRY = 1 << 1  /*!< Do not retry this query on error */
} ares_send_flags_t;

/* Similar to ares_send_dnsrec() except does not take a channel lock, allows
 * specifying a particular server to use, and also flags controlling behavior.
 */
ares_status_t ares_send_nolock(ares_channel_t *channel, ares_server_t *server,
                               ares_send_flags_t        flags,
                               const ares_dns_record_t *dnsrec,
                               ares_callback_dnsrec callback, void *arg,
                               unsigned short *qid);

/* Same as ares_gethostbyaddr() except does not take a channel lock.  Use this
 * if a channel lock is already held */
void ares_gethostbyaddr_nolock(ares_channel_t *channel, const void *addr,
                               int addrlen, int family,
                               ares_host_callback callback, void *arg);

/*! Parse a compressed DNS name as defined in RFC1035 starting at the current
 *  offset within the buffer.
 *
 *  It is assumed that either a const buffer is being used, or before
 *  the message processing was started that ares_buf_reclaim() was called.
 *
 *  \param[in]  buf        Initialized buffer object
 *  \param[out] name       Pointer passed by reference to be filled in with
 *                         allocated string of the parsed name that must be
 *                         ares_free()'d by the caller.
 *  \param[in] is_hostname if ARES_TRUE, will validate the character set for
 *                         a valid hostname or will return error.
 *  \return ARES_SUCCESS on success
 */
ares_status_t ares_dns_name_parse(ares_buf_t *buf, char **name,
                                  ares_bool_t is_hostname);

/*! Write the DNS name to the buffer in the DNS domain-name syntax as a
 *  series of labels.  The maximum domain name length is 255 characters with
 *  each label being a maximum of 63 characters.  If the validate_hostname
 *  flag is set, it will strictly validate the character set.
 *
 *  \param[in,out]  buf   Initialized buffer object to write name to
 *  \param[in,out]  list  Pointer passed by reference to maintain a list of
 *                        domain name to indexes used for name compression.
 *                        Pass NULL (not by reference) if name compression isn't
 *                        desired.  Otherwise the list will be automatically
 *                        created upon first entry.
 *  \param[in]      validate_hostname Validate the hostname character set.
 *  \param[in]      name              Name to write out, it may have escape
 *                                    sequences.
 *  \return ARES_SUCCESS on success, most likely ARES_EBADNAME if the name is
 *          bad.
 */
ares_status_t ares_dns_name_write(ares_buf_t *buf, ares_llist_t **list,
                                  ares_bool_t validate_hostname,
                                  const char *name);

/*! Check if the queue is empty, if so, wake any waiters.  This is only
 *  effective if built with threading support.
 *
 *  Must be holding a channel lock when calling this function.
 *
 *  \param[in]  channel Initialized ares channel object
 */
void          ares_queue_notify_empty(ares_channel_t *channel);

#define ARES_CONFIG_CHECK(x)                                              \
  (x && x->lookups && ares_slist_len(x->servers) > 0 && x->timeout > 0 && \
   x->tries > 0)

ares_bool_t   ares_subnet_match(const struct ares_addr *addr,
                                const struct ares_addr *subnet,
                                unsigned char           netmask);
ares_bool_t   ares_addr_is_linklocal(const struct ares_addr *addr);

void          ares_qcache_destroy(ares_qcache_t *cache);
ares_status_t ares_qcache_create(ares_rand_state *rand_state,
                                 unsigned int     max_ttl,
                                 ares_qcache_t  **cache_out);
void          ares_qcache_flush(ares_qcache_t *cache);
ares_status_t ares_qcache_insert(ares_channel_t       *channel,
                                 const ares_timeval_t *now,
                                 const ares_query_t   *query,
                                 ares_dns_record_t    *dnsrec);
ares_status_t ares_qcache_fetch(ares_channel_t           *channel,
                                const ares_timeval_t     *now,
                                const ares_dns_record_t  *dnsrec,
                                const ares_dns_record_t **dnsrec_resp);

void   ares_metrics_record(const ares_query_t *query, ares_server_t *server,
                           ares_status_t status, const ares_dns_record_t *dnsrec);
size_t ares_metrics_server_timeout(const ares_server_t  *server,
                                   const ares_timeval_t *now);

ares_status_t ares_cookie_apply(ares_dns_record_t *dnsrec, ares_conn_t *conn,
                                const ares_timeval_t *now);
ares_status_t ares_cookie_validate(ares_query_t            *query,
                                   const ares_dns_record_t *dnsresp,
                                   ares_conn_t             *conn,
                                   const ares_timeval_t    *now);

ares_status_t ares_channel_threading_init(ares_channel_t *channel);
void          ares_channel_threading_destroy(ares_channel_t *channel);
void          ares_channel_lock(const ares_channel_t *channel);
void          ares_channel_unlock(const ares_channel_t *channel);

struct ares_event_thread;
typedef struct ares_event_thread ares_event_thread_t;

void          ares_event_thread_destroy(ares_channel_t *channel);
ares_status_t ares_event_thread_init(ares_channel_t *channel);


#ifdef _WIN32
#  define HOSTENT_ADDRTYPE_TYPE short
#  define HOSTENT_LENGTH_TYPE   short
#else
#  define HOSTENT_ADDRTYPE_TYPE int
#  define HOSTENT_LENGTH_TYPE   int
#endif

#endif /* __ARES_PRIVATE_H */
