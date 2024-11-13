/* MIT License
 *
 * Copyright (c) 2024 Brad House
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

#ifndef __ARES_SOCKET_H
#define __ARES_SOCKET_H

/* Macro SOCKERRNO / SET_SOCKERRNO() returns / sets the *socket-related* errno
 * (or equivalent) on this platform to hide platform details to code using it.
 */
#ifdef USE_WINSOCK
#  define SOCKERRNO        ((int)WSAGetLastError())
#  define SET_SOCKERRNO(x) (WSASetLastError((int)(x)))
#else
#  define SOCKERRNO        (errno)
#  define SET_SOCKERRNO(x) (errno = (x))
#endif

/* Portable error number symbolic names defined to Winsock error codes. */
#ifdef USE_WINSOCK
#  undef EBADF           /* override definition in errno.h */
#  define EBADF WSAEBADF
#  undef EINTR           /* override definition in errno.h */
#  define EINTR WSAEINTR
#  undef EINVAL          /* override definition in errno.h */
#  define EINVAL WSAEINVAL
#  undef EWOULDBLOCK     /* override definition in errno.h */
#  define EWOULDBLOCK WSAEWOULDBLOCK
#  undef EINPROGRESS     /* override definition in errno.h */
#  define EINPROGRESS WSAEINPROGRESS
#  undef EALREADY        /* override definition in errno.h */
#  define EALREADY WSAEALREADY
#  undef ENOTSOCK        /* override definition in errno.h */
#  define ENOTSOCK WSAENOTSOCK
#  undef EDESTADDRREQ    /* override definition in errno.h */
#  define EDESTADDRREQ WSAEDESTADDRREQ
#  undef EMSGSIZE        /* override definition in errno.h */
#  define EMSGSIZE WSAEMSGSIZE
#  undef EPROTOTYPE      /* override definition in errno.h */
#  define EPROTOTYPE WSAEPROTOTYPE
#  undef ENOPROTOOPT     /* override definition in errno.h */
#  define ENOPROTOOPT WSAENOPROTOOPT
#  undef EPROTONOSUPPORT /* override definition in errno.h */
#  define EPROTONOSUPPORT WSAEPROTONOSUPPORT
#  define ESOCKTNOSUPPORT WSAESOCKTNOSUPPORT
#  undef EOPNOTSUPP /* override definition in errno.h */
#  define EOPNOTSUPP WSAEOPNOTSUPP
#  undef ENOSYS     /* override definition in errno.h */
#  define ENOSYS       WSAEOPNOTSUPP
#  define EPFNOSUPPORT WSAEPFNOSUPPORT
#  undef EAFNOSUPPORT  /* override definition in errno.h */
#  define EAFNOSUPPORT WSAEAFNOSUPPORT
#  undef EADDRINUSE    /* override definition in errno.h */
#  define EADDRINUSE WSAEADDRINUSE
#  undef EADDRNOTAVAIL /* override definition in errno.h */
#  define EADDRNOTAVAIL WSAEADDRNOTAVAIL
#  undef ENETDOWN      /* override definition in errno.h */
#  define ENETDOWN WSAENETDOWN
#  undef ENETUNREACH   /* override definition in errno.h */
#  define ENETUNREACH WSAENETUNREACH
#  undef ENETRESET     /* override definition in errno.h */
#  define ENETRESET WSAENETRESET
#  undef ECONNABORTED  /* override definition in errno.h */
#  define ECONNABORTED WSAECONNABORTED
#  undef ECONNRESET    /* override definition in errno.h */
#  define ECONNRESET WSAECONNRESET
#  undef ENOBUFS       /* override definition in errno.h */
#  define ENOBUFS WSAENOBUFS
#  undef EISCONN       /* override definition in errno.h */
#  define EISCONN WSAEISCONN
#  undef ENOTCONN      /* override definition in errno.h */
#  define ENOTCONN     WSAENOTCONN
#  define ESHUTDOWN    WSAESHUTDOWN
#  define ETOOMANYREFS WSAETOOMANYREFS
#  undef ETIMEDOUT     /* override definition in errno.h */
#  define ETIMEDOUT WSAETIMEDOUT
#  undef ECONNREFUSED  /* override definition in errno.h */
#  define ECONNREFUSED WSAECONNREFUSED
#  undef ELOOP         /* override definition in errno.h */
#  define ELOOP WSAELOOP
#  ifndef ENAMETOOLONG /* possible previous definition in errno.h */
#    define ENAMETOOLONG WSAENAMETOOLONG
#  endif
#  define EHOSTDOWN WSAEHOSTDOWN
#  undef EHOSTUNREACH /* override definition in errno.h */
#  define EHOSTUNREACH WSAEHOSTUNREACH
#  ifndef ENOTEMPTY   /* possible previous definition in errno.h */
#    define ENOTEMPTY WSAENOTEMPTY
#  endif
#  define EPROCLIM WSAEPROCLIM
#  define EUSERS   WSAEUSERS
#  define EDQUOT   WSAEDQUOT
#  define ESTALE   WSAESTALE
#  define EREMOTE  WSAEREMOTE
#endif

/*! Socket errors */
typedef enum {
  ARES_CONN_ERR_SUCCESS      = 0,  /*!< Success */
  ARES_CONN_ERR_WOULDBLOCK   = 1,  /*!< Operation would block */
  ARES_CONN_ERR_CONNCLOSED   = 2,  /*!< Connection closed (gracefully) */
  ARES_CONN_ERR_CONNABORTED  = 3,  /*!< Connection Aborted */
  ARES_CONN_ERR_CONNRESET    = 4,  /*!< Connection Reset */
  ARES_CONN_ERR_CONNREFUSED  = 5,  /*!< Connection Refused */
  ARES_CONN_ERR_CONNTIMEDOUT = 6,  /*!< Connection Timed Out */
  ARES_CONN_ERR_HOSTDOWN     = 7,  /*!< Host Down */
  ARES_CONN_ERR_HOSTUNREACH  = 8,  /*!< Host Unreachable */
  ARES_CONN_ERR_NETDOWN      = 9,  /*!< Network Down */
  ARES_CONN_ERR_NETUNREACH   = 10, /*!< Network Unreachable */
  ARES_CONN_ERR_INTERRUPT    = 11, /*!< Call interrupted by signal, repeat */
  ARES_CONN_ERR_AFNOSUPPORT  = 12, /*!< Address family not supported */
  ARES_CONN_ERR_BADADDR      = 13, /*!< Bad Address / Unavailable */
  ARES_CONN_ERR_NOMEM        = 14, /*!< Out of memory */
  ARES_CONN_ERR_INVALID      = 15, /*!< Invalid Usage */
  ARES_CONN_ERR_TOOLARGE     = 16, /*!< Request size too large */
  ARES_CONN_ERR_NOTIMP       = 17, /*!< Not implemented */
  ARES_CONN_ERR_FAILURE      = 99  /*!< Generic failure */
} ares_conn_err_t;

ares_bool_t     ares_sockaddr_addr_eq(const struct sockaddr  *sa,
                                      const struct ares_addr *aa);
ares_status_t   ares_socket_configure(ares_channel_t *channel, int family,
                                      ares_bool_t is_tcp, ares_socket_t fd);
ares_conn_err_t ares_socket_enable_tfo(const ares_channel_t *channel,
                                       ares_socket_t         fd);
ares_conn_err_t ares_socket_open(ares_socket_t *sock, ares_channel_t *channel,
                                 int af, int type, int protocol);
ares_bool_t     ares_socket_try_again(int errnum);
void            ares_socket_close(ares_channel_t *channel, ares_socket_t s);
ares_conn_err_t ares_socket_connect(ares_channel_t *channel,
                                    ares_socket_t sockfd, ares_bool_t is_tfo,
                                    const struct sockaddr *addr,
                                    ares_socklen_t         addrlen);
ares_bool_t     ares_sockaddr_to_ares_addr(struct ares_addr      *ares_addr,
                                           unsigned short        *port,
                                           const struct sockaddr *sockaddr);
ares_conn_err_t ares_socket_write(ares_channel_t *channel, ares_socket_t fd,
                                  const void *data, size_t len, size_t *written,
                                  const struct sockaddr *sa,
                                  ares_socklen_t         salen);
#endif
