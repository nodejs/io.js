/* MIT License
 *
 * Copyright (c) 2004 Daniel Stenberg
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
#ifndef __ARES_SETUP_H
#define __ARES_SETUP_H

/* ============================================================================
 * NOTE: This file is automatically included by ares_private.h and should not
 *       typically be included directly.
 *       All c-ares source files should include ares_private.h as the
 *       first header.
 * ============================================================================
 */

/*
 * Include configuration script results or hand-crafted
 * configuration file for platforms which lack config tool.
 */

#ifdef HAVE_CONFIG_H
#  include "ares_config.h"
#else
#  ifdef _WIN32
#    include "config-win32.h"
#  endif
#endif /* HAVE_CONFIG_H */

/*
 * c-ares external interface definitions are also used internally,
 * and might also include required system header files to define them.
 */

#include "ares_build.h"

/* ================================================================= */
/* No system header file shall be included in this file before this  */
/* point. The only allowed ones are those included from ares_build.h */
/* ================================================================= */

/*
 * Include header files for windows builds before redefining anything.
 * Use this preproessor block only to include or exclude windows.h,
 * winsock2.h, ws2tcpip.h or winsock.h. Any other windows thing belongs
 * to any other further and independent block.  Under Cygwin things work
 * just as under linux (e.g. <sys/socket.h>) and the winsock headers should
 * never be included when __CYGWIN__ is defined.  configure script takes
 * care of this, not defining HAVE_WINDOWS_H, HAVE_WINSOCK_H, HAVE_WINSOCK2_H,
 * neither HAVE_WS2TCPIP_H when __CYGWIN__ is defined.
 */

#ifdef USE_WINSOCK
#  undef USE_WINSOCK
#endif

#ifdef HAVE_WINDOWS_H
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  ifdef HAVE_WINSOCK2_H
#    include <winsock2.h>
#    define USE_WINSOCK 2
#    ifdef HAVE_WS2TCPIP_H
#      include <ws2tcpip.h>
#    endif
#  else
#    ifdef HAVE_WINSOCK_H
#      include <winsock.h>
#      define USE_WINSOCK 1
#    endif
#  endif
#endif


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef HAVE_ERRNO_H
#  include <errno.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#ifdef HAVE_MALLOC_H
#  include <malloc.h>
#endif

#ifdef HAVE_SYS_STAT_H
#  include <sys/stat.h>
#endif

#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif

#ifdef HAVE_TIME_H
#  include <time.h>
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#endif

/*
 * Arg 2 type for gethostname in case it hasn't been defined in config file.
 */

#ifndef GETHOSTNAME_TYPE_ARG2
#  ifdef USE_WINSOCK
#    define GETHOSTNAME_TYPE_ARG2 int
#  else
#    define GETHOSTNAME_TYPE_ARG2 size_t
#  endif
#endif

#ifdef __POCC__
#  include <sys/types.h>
#  include <unistd.h>
#  define ESRCH 3
#endif

/*
 * Android does have the arpa/nameser.h header which is detected by configure
 * but it appears to be empty with recent NDK r7b / r7c, so we undefine here.
 * z/OS does have the arpa/nameser.h header which is detected by configure
 * but it is not fully implemented and missing identifiers, so udefine here.
 */
#if (defined(ANDROID) || defined(__ANDROID__) || defined(__MVS__)) && \
  defined(HAVE_ARPA_NAMESER_H)
#  undef HAVE_ARPA_NAMESER_H
#endif

/*
 * Recent autoconf versions define these symbols in ares_config.h. We don't
 * want them (since they collide with the libcurl ones when we build
 *  --enable-debug) so we undef them again here.
 */

#ifdef PACKAGE_STRING
#  undef PACKAGE_STRING
#endif
#ifdef PACKAGE_TARNAME
#  undef PACKAGE_TARNAME
#endif
#ifdef PACKAGE_VERSION
#  undef PACKAGE_VERSION
#endif
#ifdef PACKAGE_BUGREPORT
#  undef PACKAGE_BUGREPORT
#endif
#ifdef PACKAGE_NAME
#  undef PACKAGE_NAME
#endif
#ifdef VERSION
#  undef VERSION
#endif
#ifdef PACKAGE
#  undef PACKAGE
#endif

/* IPv6 compatibility */
#if !defined(HAVE_AF_INET6)
#  if defined(HAVE_PF_INET6)
#    define AF_INET6 PF_INET6
#  else
#    define AF_INET6 AF_MAX + 1
#  endif
#endif


#ifdef __hpux
#  if !defined(_XOPEN_SOURCE_EXTENDED) || defined(_KERNEL)
#    ifdef _APP32_64BIT_OFF_T
#      define OLD_APP32_64BIT_OFF_T _APP32_64BIT_OFF_T
#      undef _APP32_64BIT_OFF_T
#    else
#      undef OLD_APP32_64BIT_OFF_T
#    endif
#  endif
#endif

#ifdef __hpux
#  if !defined(_XOPEN_SOURCE_EXTENDED) || defined(_KERNEL)
#    ifdef OLD_APP32_64BIT_OFF_T
#      define _APP32_64BIT_OFF_T OLD_APP32_64BIT_OFF_T
#      undef OLD_APP32_64BIT_OFF_T
#    endif
#  endif
#endif


/*
 * Definition of timeval struct for platforms that don't have it.
 */

#ifndef HAVE_STRUCT_TIMEVAL
struct timeval {
  long tv_sec;
  long tv_usec;
};
#endif


/*
 * If we have the MSG_NOSIGNAL define, make sure we use
 * it as the fourth argument of function send()
 */

#ifdef HAVE_MSG_NOSIGNAL
#  define SEND_4TH_ARG MSG_NOSIGNAL
#else
#  define SEND_4TH_ARG 0
#endif


#if defined(__minix)
/* Minix doesn't support recv on TCP sockets */
#  define sread(x, y, z) \
    (ares_ssize_t)       \
      read((RECV_TYPE_ARG1)(x), (RECV_TYPE_ARG2)(y), (RECV_TYPE_ARG3)(z))

#elif defined(HAVE_RECV)
/*
 * The definitions for the return type and arguments types
 * of functions recv() and send() belong and come from the
 * configuration file. Do not define them in any other place.
 *
 * HAVE_RECV is defined if you have a function named recv()
 * which is used to read incoming data from sockets. If your
 * function has another name then don't define HAVE_RECV.
 *
 * If HAVE_RECV is defined then RECV_TYPE_ARG1, RECV_TYPE_ARG2,
 * RECV_TYPE_ARG3, RECV_TYPE_ARG4 and RECV_TYPE_RETV must also
 * be defined.
 *
 * HAVE_SEND is defined if you have a function named send()
 * which is used to write outgoing data on a connected socket.
 * If yours has another name then don't define HAVE_SEND.
 *
 * If HAVE_SEND is defined then SEND_TYPE_ARG1,
 * SEND_TYPE_ARG2, SEND_TYPE_ARG3, SEND_TYPE_ARG4 and
 * SEND_TYPE_RETV must also be defined.
 */

#  if !defined(RECV_TYPE_ARG1) || !defined(RECV_TYPE_ARG2) || \
    !defined(RECV_TYPE_ARG3) || !defined(RECV_TYPE_ARG4) ||   \
    !defined(RECV_TYPE_RETV)
/* */
Error Missing_definition_of_return_and_arguments_types_of_recv
/* */
#  else
#    define sread(x, y, z)                                          \
      (ares_ssize_t) recv((RECV_TYPE_ARG1)(x), (RECV_TYPE_ARG2)(y), \
                          (RECV_TYPE_ARG3)(z), (RECV_TYPE_ARG4)(0))
#  endif
#else /* HAVE_RECV */
#  ifndef sread
/* */
Error Missing_definition_of_macro_sread
/* */
#  endif
#endif /* HAVE_RECV */


#if defined(__minix)
/* Minix doesn't support send on TCP sockets */
#  define swrite(x, y, z) \
    (ares_ssize_t)        \
      write((SEND_TYPE_ARG1)(x), (SEND_TYPE_ARG2)(y), (SEND_TYPE_ARG3)(z))

#elif defined(HAVE_SEND)
#  if !defined(SEND_TYPE_ARG1) || \
    !defined(SEND_TYPE_ARG2) || !defined(SEND_TYPE_ARG3) ||   \
    !defined(SEND_TYPE_ARG4) || !defined(SEND_TYPE_RETV)
  /* */
  Error Missing_definition_of_return_and_arguments_types_of_send
/* */
#  else
#    define swrite(x, y, z)                                         \
      (ares_ssize_t) send((SEND_TYPE_ARG1)(x), (SEND_TYPE_ARG2)(y), \
                          (SEND_TYPE_ARG3)(z), (SEND_TYPE_ARG4)(SEND_4TH_ARG))
#  endif
#else /* HAVE_SEND */
#  ifndef swrite
  /* */
  Error Missing_definition_of_macro_swrite
/* */
#  endif
#endif /* HAVE_SEND */


/*
 * Function-like macro definition used to close a socket.
 */

#if defined(HAVE_CLOSESOCKET)
#  define sclose(x) closesocket((x))
#elif defined(HAVE_CLOSESOCKET_CAMEL)
#  define sclose(x) CloseSocket((x))
#elif defined(HAVE_CLOSE_S)
#  define sclose(x) close_s((x))
#else
#  define sclose(x) close((x))
#endif

/*
 * Macro used to include code only in debug builds.
 */

#ifdef DEBUGBUILD
#  define DEBUGF(x) x
#else
#  define DEBUGF(x) \
    do {            \
    } while(0)
#endif

/*
 * Macro SOCKERRNO / SET_SOCKERRNO() returns / sets the *socket-related* errno
 * (or equivalent) on this platform to hide platform details to code using it.
 */

#ifdef USE_WINSOCK
#  define SOCKERRNO        ((int)WSAGetLastError())
#  define SET_SOCKERRNO(x) (WSASetLastError((int)(x)))
#else
#  define SOCKERRNO        (errno)
#  define SET_SOCKERRNO(x) (errno = (x))
#endif


/*
 * Macro ERRNO / SET_ERRNO() returns / sets the NOT *socket-related* errno
 * (or equivalent) on this platform to hide platform details to code using it.
 */

#if defined(WIN32) && !defined(WATT32)
#  define ERRNO        ((int)GetLastError())
#  define SET_ERRNO(x) (SetLastError((DWORD)(x)))
#else
#  define ERRNO        (errno)
#  define SET_ERRNO(x) (errno = (x))
#endif


/*
 * Portable error number symbolic names defined to Winsock error codes.
 */

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
#  define EOPNOTSUPP   WSAEOPNOTSUPP
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

#endif /* __ARES_SETUP_H */
