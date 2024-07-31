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

#if !defined(_WIN32) || defined(WATT32)
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#endif

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#include "ares.h"
#include "ares_dns.h"
#include "ares_getopt.h"
#include "ares_ipv6.h"

#ifndef HAVE_STRDUP
#  include "ares_str.h"
#  define strdup(ptr) ares_strdup(ptr)
#endif

#ifndef HAVE_STRCASECMP
#  include "ares_strcasecmp.h"
#  define strcasecmp(p1, p2) ares_strcasecmp(p1, p2)
#endif

#ifndef HAVE_STRNCASECMP
#  include "ares_strcasecmp.h"
#  define strncasecmp(p1, p2, n) ares_strncasecmp(p1, p2, n)
#endif

static void callback(void *arg, int status, int timeouts, struct hostent *host);
static void ai_callback(void *arg, int status, int timeouts,
                        struct ares_addrinfo *result);
static void usage(void);
static void print_help_info_ahost(void);

int         main(int argc, char **argv)
{
  struct ares_options  options;
  int                  optmask = 0;
  ares_channel_t      *channel;
  int                  status;
  int                  nfds;
  int                  c;
  int                  addr_family = AF_UNSPEC;
  fd_set               read_fds;
  fd_set               write_fds;
  struct timeval      *tvp;
  struct timeval       tv;
  struct in_addr       addr4;
  struct ares_in6_addr addr6;
  ares_getopt_state_t  state;
  char                *servers = NULL;

#ifdef USE_WINSOCK
  WORD    wVersionRequested = MAKEWORD(USE_WINSOCK, USE_WINSOCK);
  WSADATA wsaData;
  WSAStartup(wVersionRequested, &wsaData);
#endif

  memset(&options, 0, sizeof(options));

  status = ares_library_init(ARES_LIB_INIT_ALL);
  if (status != ARES_SUCCESS) {
    fprintf(stderr, "ares_library_init: %s\n", ares_strerror(status));
    return 1;
  }

  ares_getopt_init(&state, argc, (const char * const *)argv);
  while ((c = ares_getopt(&state, "dt:h?D:s:")) != -1) {
    switch (c) {
      case 'd':
#ifdef WATT32
        dbug_init();
#endif
        break;
      case 'D':
        optmask |= ARES_OPT_DOMAINS;
        options.ndomains++;
        options.domains = (char **)realloc(
          options.domains, (size_t)options.ndomains * sizeof(char *));
        options.domains[options.ndomains - 1] = strdup(state.optarg);
        break;
      case 't':
        if (!strcasecmp(state.optarg, "a")) {
          addr_family = AF_INET;
        } else if (!strcasecmp(state.optarg, "aaaa")) {
          addr_family = AF_INET6;
        } else if (!strcasecmp(state.optarg, "u")) {
          addr_family = AF_UNSPEC;
        } else {
          usage();
        }
        break;
      case 's':
        if (state.optarg == NULL) {
          fprintf(stderr, "%s", "missing servers");
          usage();
          break;
        }
        if (servers) {
          free(servers);
        }
        servers = strdup(state.optarg);
        break;
      case 'h':
      case '?':
        print_help_info_ahost();
        break;
      default:
        usage();
        break;
    }
  }

  argc -= state.optind;
  argv += state.optind;
  if (argc < 1) {
    usage();
  }

  status = ares_init_options(&channel, &options, optmask);
  if (status != ARES_SUCCESS) {
    free(servers);
    fprintf(stderr, "ares_init: %s\n", ares_strerror(status));
    return 1;
  }

  if (servers) {
    status = ares_set_servers_csv(channel, servers);
    if (status != ARES_SUCCESS) {
      fprintf(stderr, "ares_set_serveres_csv: %s\n", ares_strerror(status));
      free(servers);
      usage();
      return 1;
    }
    free(servers);
  }

  /* Initiate the queries, one per command-line argument. */
  for (; *argv; argv++) {
    if (ares_inet_pton(AF_INET, *argv, &addr4) == 1) {
      ares_gethostbyaddr(channel, &addr4, sizeof(addr4), AF_INET, callback,
                         *argv);
    } else if (ares_inet_pton(AF_INET6, *argv, &addr6) == 1) {
      ares_gethostbyaddr(channel, &addr6, sizeof(addr6), AF_INET6, callback,
                         *argv);
    } else {
      struct ares_addrinfo_hints hints;
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = addr_family;
      ares_getaddrinfo(channel, *argv, NULL, &hints, ai_callback, *argv);
    }
  }

  /* Wait for all queries to complete. */
  for (;;) {
    int res;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    nfds = ares_fds(channel, &read_fds, &write_fds);
    if (nfds == 0) {
      break;
    }
    tvp = ares_timeout(channel, NULL, &tv);
    if (tvp == NULL) {
      break;
    }
    res = select(nfds, &read_fds, &write_fds, NULL, tvp);
    if (-1 == res) {
      break;
    }
    ares_process(channel, &read_fds, &write_fds);
  }

  ares_destroy(channel);

  ares_library_cleanup();

#ifdef USE_WINSOCK
  WSACleanup();
#endif

  return 0;
}

static void callback(void *arg, int status, int timeouts, struct hostent *host)
{
  char **p;

  (void)timeouts;

  if (status != ARES_SUCCESS) {
    fprintf(stderr, "%s: %s\n", (char *)arg, ares_strerror(status));
    return;
  }

  for (p = host->h_addr_list; *p; p++) {
    char addr_buf[46] = "??";

    ares_inet_ntop(host->h_addrtype, *p, addr_buf, sizeof(addr_buf));
    printf("%-32s\t%s", host->h_name, addr_buf);
    puts("");
  }
}

static void ai_callback(void *arg, int status, int timeouts,
                        struct ares_addrinfo *result)
{
  struct ares_addrinfo_node *node = NULL;
  (void)timeouts;


  if (status != ARES_SUCCESS) {
    fprintf(stderr, "%s: %s\n", (char *)arg, ares_strerror(status));
    return;
  }

  for (node = result->nodes; node != NULL; node = node->ai_next) {
    char        addr_buf[64] = "";
    const void *ptr          = NULL;
    if (node->ai_family == AF_INET) {
      const struct sockaddr_in *in_addr =
        (const struct sockaddr_in *)((void *)node->ai_addr);
      ptr = &in_addr->sin_addr;
    } else if (node->ai_family == AF_INET6) {
      const struct sockaddr_in6 *in_addr =
        (const struct sockaddr_in6 *)((void *)node->ai_addr);
      ptr = &in_addr->sin6_addr;
    } else {
      continue;
    }
    ares_inet_ntop(node->ai_family, ptr, addr_buf, sizeof(addr_buf));
    printf("%-32s\t%s\n", result->name, addr_buf);
  }

  ares_freeaddrinfo(result);
}

static void usage(void)
{
  fprintf(stderr, "usage: ahost [-h] [-d] [[-D {domain}] ...] [-s {server}] "
                  "[-t {a|aaaa|u}] {host|addr} ...\n");
  exit(1);
}

/* Information from the man page. Formatting taken from man -h */
static void print_help_info_ahost(void)
{
  /* Split due to maximum c89 string literal of 509 bytes */
  printf("ahost, version %s\n\n", ARES_VERSION_STR);
  printf(
    "usage: ahost [-h] [-d] [-D domain] [-s server] [-t a|aaaa|u] host|addr "
    "...\n\n");
  printf(
    "  -h : Display this help and exit.\n"
    "  -d : Print some extra debugging output.\n\n"
    "  -D domain : Specify the domain to search instead of using the default "
    "values\n");
  printf(
    "  -s server : Connect to the specified DNS server, instead of the\n"
    "              system's default one(s). Servers are tried in round-robin,\n"
    "              if the previous one failed.\n"
    "  -t type   : If type is \"a\", print the A record.\n");
  printf(
    "              If type is \"aaaa\", print the AAAA record.\n"
    "              If type is \"u\" (default), print both A and AAAA records.\n"
    "\n");
  exit(0);
}
