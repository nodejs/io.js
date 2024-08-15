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
#ifndef HEADER_CARES_CONFIG_WIN32_H
#define HEADER_CARES_CONFIG_WIN32_H

/* ================================================================ */
/*   c-ares/config-win32.h - Hand crafted config file for Windows   */
/* ================================================================ */

/* ---------------------------------------------------------------- */
/*                          HEADER FILES                            */
/* ---------------------------------------------------------------- */

/* Define if you have the <assert.h> header file. */
#define HAVE_ASSERT_H 1

/* Define if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* Define if you have the <getopt.h> header file. */
#if defined(__MINGW32__) || defined(__POCC__)
#  define HAVE_GETOPT_H 1
#endif

/* Define if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* Define if you have the <process.h> header file. */
#ifndef __SALFORDC__
#  define HAVE_PROCESS_H 1
#endif

/* Define if you have the <signal.h> header file. */
#define HAVE_SIGNAL_H 1

/* Define if you have the <sys/time.h> header file */
/* #define HAVE_SYS_TIME_H 1 */

/* Define if you have the <time.h> header file. */
#define HAVE_TIME_H 1

/* Define if you have the <unistd.h> header file. */
#if defined(__MINGW32__) || defined(__WATCOMC__) || defined(__LCC__) || \
  defined(__POCC__)
#  define HAVE_UNISTD_H 1
#endif

/* Define if you have the <windows.h> header file. */
#define HAVE_WINDOWS_H 1

/* Define if you have the <winsock.h> header file. */
#define HAVE_WINSOCK_H 1

/* Define if you have the <winsock2.h> header file. */
#ifndef __SALFORDC__
#  define HAVE_WINSOCK2_H 1
#endif

/* Define if you have the <ws2tcpip.h> header file. */
#ifndef __SALFORDC__
#  define HAVE_WS2TCPIP_H 1
#endif

/* Define if you have <iphlpapi.h> header file */
#define HAVE_IPHLPAPI_H 1

/* Define if you have <netioapi.h> header file */
#if !defined(__WATCOMC__) && !defined(WATT32)
#  define HAVE_NETIOAPI_H 1
#endif

#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H  1

/* If we are building with OpenWatcom, we need to specify that we have
 * <stdint.h>. */
#if defined(__WATCOMC__)
#  define HAVE_STDINT_H
#endif

/* ---------------------------------------------------------------- */
/*                        OTHER HEADER INFO                         */
/* ---------------------------------------------------------------- */

/* Define if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* ---------------------------------------------------------------- */
/*                             FUNCTIONS                            */
/* ---------------------------------------------------------------- */

/* Define if you have the closesocket function. */
#define HAVE_CLOSESOCKET 1

/* Define if you have the getenv function. */
#define HAVE_GETENV 1

/* Define if you have the gethostname function. */
#define HAVE_GETHOSTNAME 1

/* Define if you have the ioctlsocket function. */
#define HAVE_IOCTLSOCKET 1

/* Define if you have a working ioctlsocket FIONBIO function. */
#define HAVE_IOCTLSOCKET_FIONBIO 1

/* Define if you have the strcasecmp function. */
/* #define HAVE_STRCASECMP 1 */

/* Define if you have the strdup function. */
#define HAVE_STRDUP 1

/* Define if you have the stricmp function. */
#define HAVE_STRICMP 1

/* Define if you have the strncasecmp function. */
/* #define HAVE_STRNCASECMP 1 */

/* Define if you have the strnicmp function. */
#define HAVE_STRNICMP 1

/* Define if you have the recv function. */
#define HAVE_RECV 1

/* Define to the type of arg 1 for recv. */
#define RECV_TYPE_ARG1 SOCKET

/* Define to the type of arg 2 for recv. */
#define RECV_TYPE_ARG2 char *

/* Define to the type of arg 3 for recv. */
#define RECV_TYPE_ARG3 int

/* Define to the type of arg 4 for recv. */
#define RECV_TYPE_ARG4 int

/* Define to the function return type for recv. */
#define RECV_TYPE_RETV int

/* Define if you have the recvfrom function. */
#define HAVE_RECVFROM 1

/* Define to the type of arg 1 for recvfrom. */
#define RECVFROM_TYPE_ARG1 SOCKET

/* Define to the type pointed by arg 2 for recvfrom. */
#define RECVFROM_TYPE_ARG2 char

/* Define to the type of arg 3 for recvfrom. */
#define RECVFROM_TYPE_ARG3 int

/* Define to the type of arg 4 for recvfrom. */
#define RECVFROM_TYPE_ARG4 int

/* Define to the type pointed by arg 5 for recvfrom. */
#define RECVFROM_TYPE_ARG5 struct sockaddr

/* Define to the type pointed by arg 6 for recvfrom. */
#define RECVFROM_TYPE_ARG6 int

/* Define to the function return type for recvfrom. */
#define RECVFROM_TYPE_RETV int

/* Define if you have the send function. */
#define HAVE_SEND 1

/* Define to the type of arg 1 for send. */
#define SEND_TYPE_ARG1 SOCKET

/* Define to the type of arg 2 for send. */
#define SEND_TYPE_ARG2 const char *

/* Define to the type of arg 3 for send. */
#define SEND_TYPE_ARG3 int

/* Define to the type of arg 4 for send. */
#define SEND_TYPE_ARG4 int

/* Define to the function return type for send. */
#define SEND_TYPE_RETV int

/* Specifics for the Watt-32 tcp/ip stack. */
#ifdef WATT32
#  undef RECV_TYPE_ARG1
#  define RECV_TYPE_ARG1 int
#  undef SEND_TYPE_ARG1
#  define SEND_TYPE_ARG1 int
#  undef RECVFROM_TYPE_ARG1
#  define RECVFROM_TYPE_ARG1       int
#  define NS_INADDRSZ              4
#  define HAVE_ARPA_NAMESER_H      1
#  define HAVE_ARPA_INET_H         1
#  define HAVE_NETDB_H             1
#  define HAVE_NETINET_IN_H        1
#  define HAVE_SYS_SOCKET_H        1
#  define HAVE_SYS_IOCTL_H         1
#  define HAVE_NETINET_TCP_H       1
#  define HAVE_AF_INET6            1
#  define HAVE_PF_INET6            1
#  define HAVE_STRUCT_IN6_ADDR     1
#  define HAVE_STRUCT_SOCKADDR_IN6 1
#  define HAVE_WRITEV              1
#  define HAVE_IF_NAMETOINDEX      1
#  define HAVE_IF_INDEXTONAME      1
#  define HAVE_GETSERVBYPORT_R     1
#  define GETSERVBYPORT_R_ARGS     6
#  undef HAVE_WINSOCK_H
#  undef HAVE_WINSOCK2_H
#  undef HAVE_WS2TCPIP_H
#  undef HAVE_IPHLPAPI_H
#  undef HAVE_NETIOAPI_H
#endif

/* Threading support enabled */
#define CARES_THREADS 1

/* ---------------------------------------------------------------- */
/*                       TYPEDEF REPLACEMENTS                       */
/* ---------------------------------------------------------------- */

/* ---------------------------------------------------------------- */
/*                            TYPE SIZES                            */
/* ---------------------------------------------------------------- */

/* ---------------------------------------------------------------- */
/*                          STRUCT RELATED                          */
/* ---------------------------------------------------------------- */

/* Define if you have struct addrinfo. */
#define HAVE_STRUCT_ADDRINFO 1

/* Define if you have struct sockaddr_storage. */
#if !defined(__SALFORDC__) && !defined(__BORLANDC__)
#  define HAVE_STRUCT_SOCKADDR_STORAGE 1
#endif

/* Define if you have struct timeval. */
#define HAVE_STRUCT_TIMEVAL 1

/* ---------------------------------------------------------------- */
/*                        COMPILER SPECIFIC                         */
/* ---------------------------------------------------------------- */

/* Define to avoid VS2005 complaining about portable C functions. */
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
#  define _CRT_SECURE_NO_DEPRECATE  1
#  define _CRT_NONSTDC_NO_DEPRECATE 1
#endif

/* Set the Target to Win8 */
#if defined(_MSC_VER) && (_MSC_VER >= 1500)
#  define MSVC_MIN_TARGET 0x0602
#endif

/* MSVC default target settings */
#if defined(_MSC_VER) && (_MSC_VER >= 1500)
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT MSVC_MIN_TARGET
#  endif
#  ifndef WINVER
#    define WINVER MSVC_MIN_TARGET
#  endif
#endif

/* When no build target is specified Pelles C 5.00 and later default build
   target is Windows Vista. */
#if defined(__POCC__) && (__POCC__ >= 500)
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0602
#  endif
#  ifndef WINVER
#    define WINVER 0x0602
#  endif
#endif

/* Availability of freeaddrinfo, getaddrinfo and getnameinfo functions is
   quite convoluted, compiler dependent and even build target dependent. */
#if defined(HAVE_WS2TCPIP_H)
#  if defined(__POCC__)
#    define HAVE_FREEADDRINFO 1
#    define HAVE_GETADDRINFO  1
#    define HAVE_GETNAMEINFO  1
#  elif defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0501)
#    define HAVE_FREEADDRINFO 1
#    define HAVE_GETADDRINFO  1
#    define HAVE_GETNAMEINFO  1
#  elif defined(_MSC_VER) && (_MSC_VER >= 1200)
#    define HAVE_FREEADDRINFO 1
#    define HAVE_GETADDRINFO  1
#    define HAVE_GETNAMEINFO  1
#  endif
#endif

#if defined(__POCC__)
#  ifndef _MSC_VER
#    error Microsoft extensions /Ze compiler option is required
#  endif
#  ifndef __POCC__OLDNAMES
#    error Compatibility names /Go compiler option is required
#  endif
#endif

/* ---------------------------------------------------------------- */
/*                         IPV6 COMPATIBILITY                       */
/* ---------------------------------------------------------------- */

/* Define if you have address family AF_INET6. */
#ifdef HAVE_WINSOCK2_H
#  define HAVE_AF_INET6 1
#endif

/* Define if you have protocol family PF_INET6. */
#ifdef HAVE_WINSOCK2_H
#  define HAVE_PF_INET6 1
#endif

/* Define if you have struct in6_addr. */
#ifdef HAVE_WS2TCPIP_H
#  define HAVE_STRUCT_IN6_ADDR 1
#endif

/* Define if you have struct sockaddr_in6. */
#ifdef HAVE_WS2TCPIP_H
#  define HAVE_STRUCT_SOCKADDR_IN6 1
#endif

/* Define if you have sockaddr_in6 with scopeid. */
#ifdef HAVE_WS2TCPIP_H
#  define HAVE_STRUCT_SOCKADDR_IN6_SIN6_SCOPE_ID 1
#endif

/* Define to 1 if you have the `RegisterWaitForSingleObject' function. */
#define HAVE_REGISTERWAITFORSINGLEOBJECT 1

#if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0600) && \
  !defined(__WATCOMC__) && !defined(WATT32)
/* Define if you have if_nametoindex() */
#  define HAVE_IF_NAMETOINDEX 1
/* Define if you have if_indextoname() */
#  define HAVE_IF_INDEXTONAME 1
/* Define to 1 if you have the `ConvertInterfaceIndexToLuid' function. */
#  define HAVE_CONVERTINTERFACEINDEXTOLUID 1
/* Define to 1 if you have the `ConvertInterfaceLuidToNameA' function. */
#  define HAVE_CONVERTINTERFACELUIDTONAMEA 1
/* Define to 1 if you have the `NotifyIpInterfaceChange' function. */
#  define HAVE_NOTIFYIPINTERFACECHANGE 1
#endif

/* ---------------------------------------------------------------- */
/*                              Win CE                              */
/* ---------------------------------------------------------------- */

/* FIXME: A proper config-win32ce.h should be created to hold these */

/*
 *  System error codes for Windows CE
 */

#if defined(_WIN32_WCE) && !defined(HAVE_ERRNO_H)
#  define ENOENT ERROR_FILE_NOT_FOUND
#  define ESRCH  ERROR_PATH_NOT_FOUND
#  define ENOMEM ERROR_NOT_ENOUGH_MEMORY
#  define ENOSPC ERROR_INVALID_PARAMETER
#endif

#endif /* HEADER_CARES_CONFIG_WIN32_H */
