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


/* Definition of timeval struct for platforms that don't have it. */

#ifndef HAVE_STRUCT_TIMEVAL
struct timeval {
  ares_int64_t tv_sec;
  long         tv_usec;
};
#endif


/*
 * Macro used to include code only in debug builds.
 */

#ifdef DEBUGBUILD
#  define DEBUGF(x) x
#else
#  define DEBUGF(x) \
    do {            \
    } while (0)
#endif

#endif /* __ARES_SETUP_H */
