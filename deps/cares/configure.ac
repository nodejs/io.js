dnl Copyright (C) The c-ares project and its contributors
dnl SPDX-License-Identifier: MIT
AC_PREREQ([2.69])

AC_INIT([c-ares], [1.34.4],
  [c-ares mailing list: http://lists.haxx.se/listinfo/c-ares])

CARES_VERSION_INFO="21:3:19"
dnl This flag accepts an argument of the form current[:revision[:age]]. So,
dnl passing -version-info 3:12:1 sets current to 3, revision to 12, and age to
dnl 1.
dnl
dnl If either revision or age are omitted, they default to 0. Also note that age
dnl must be less than or equal to the current interface number.
dnl
dnl Here are a set of rules to help you update your library version information:
dnl
dnl 1.Start with version information of 0:0:0 for each libtool library.
dnl
dnl 2.Update the version information only immediately before a public release of
dnl your software. More frequent updates are unnecessary, and only guarantee
dnl that the current interface number gets larger faster.
dnl
dnl 3.If the library source code has changed at all since the last update, then
dnl increment revision (c:r+1:a)
dnl
dnl 4.If any interfaces have been added, removed, or changed since the last
dnl update, increment current, and set revision to 0. (c+1:r=0:a)
dnl
dnl 5.If any interfaces have been added since the last public release, then
dnl increment age. (c:r:a+1)
dnl
dnl 6.If any interfaces have been removed since the last public release, then
dnl set age to 0. (c:r:a=0)
dnl
AC_SUBST([CARES_VERSION_INFO])

AC_CONFIG_SRCDIR([src/lib/ares_ipv6.h])
AC_CONFIG_HEADERS([src/lib/ares_config.h include/ares_build.h])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_MACRO_DIR([m4])
AC_USE_SYSTEM_EXTENSIONS
AX_CXX_COMPILE_STDCXX_14([noext],[optional])
AM_INIT_AUTOMAKE([foreign subdir-objects 1.9.6])
LT_INIT([win32-dll,pic,disable-fast-install,aix-soname=svr4])
AC_LANG([C])
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_EGREP
AC_PROG_INSTALL
AC_CANONICAL_HOST
AX_COMPILER_VENDOR

AC_MSG_CHECKING([whether this is native windows])
ac_cv_native_windows=no
ac_cv_windows=no
case $host_os in
  mingw*)
    ac_cv_native_windows=yes
    ac_cv_windows=yes
    ;;
  cygwin*)
    ac_cv_windows=yes
    ;;
esac
if test "$ax_cv_c_compiler_vendor" = "microsoft" ; then
  ac_cv_native_windows=yes
  ac_cv_windows=yes
fi
AC_MSG_RESULT($ac_cv_native_windows)


AC_ENABLE_SHARED

dnl Disable static builds by default on Windows unless overwritten since Windows
dnl can't simultaneously build shared and static with autotools.
AS_IF([test "x$ac_cv_windows" = "xyes"], [AC_DISABLE_STATIC], [AC_ENABLE_STATIC])

AC_ARG_ENABLE(warnings,
  AS_HELP_STRING([--disable-warnings],[Disable strict compiler warnings]),
  [ enable_warnings=${enableval} ],
  [ enable_warnings=yes ])

AC_ARG_ENABLE(symbol-hiding,
  AS_HELP_STRING([--disable-symbol-hiding], [Disable symbol hiding. Enabled by default if the compiler supports it.]),
  [
    symbol_hiding="$enableval"
    if test "$symbol_hiding" = "no" -a "x$enable_shared" = "xyes" ; then
      case $host_os in
        cygwin* | mingw* | pw32* | cegcc*)
          AC_MSG_ERROR([Cannot disable symbol hiding on windows])
        ;;
      esac
    fi
  ],
  [
    if test "x$enable_shared" = "xyes" ; then
      symbol_hiding="maybe"
    else
      symbol_hiding="no"
    fi
  ]
)

AC_ARG_ENABLE(tests,
  AS_HELP_STRING([--disable-tests], [disable building of test suite. Built by default if GoogleTest is found.]),
  [ build_tests="$enableval" ],
  [ if test "x$HAVE_CXX14" = "x1" && test "x$cross_compiling" = "xno" ; then
      build_tests="maybe"
    else
      build_tests="no"
    fi
  ]
)

AC_ARG_ENABLE(cares-threads,
  AS_HELP_STRING([--disable-cares-threads], [Disable building of thread safety support]),
  [ CARES_THREADS=${enableval} ],
  [ CARES_THREADS=yes ])

AC_ARG_WITH(random,
  AS_HELP_STRING([--with-random=FILE],
                 [read randomness from FILE (default=/dev/urandom)]),
  [ CARES_RANDOM_FILE="$withval" ],
  [ CARES_RANDOM_FILE="/dev/urandom" ]
)
if test -n "$CARES_RANDOM_FILE" && test X"$CARES_RANDOM_FILE" != Xno ; then
  AC_SUBST(CARES_RANDOM_FILE)
  AC_DEFINE_UNQUOTED(CARES_RANDOM_FILE, "$CARES_RANDOM_FILE", [a suitable file/device to read random data from])
fi

AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])


dnl CARES_DEFINE_UNQUOTED (VARIABLE, [VALUE])
dnl -------------------------------------------------
dnl Like AC_DEFINE_UNQUOTED this macro will define a C preprocessor
dnl symbol that can be further used in custom template configuration
dnl files. This macro, unlike AC_DEFINE_UNQUOTED, does not use a third
dnl argument for the description. Symbol definitions done with this
dnl macro are intended to be exclusively used in handcrafted *.h.in
dnl template files. Contrary to what AC_DEFINE_UNQUOTED does, this one
dnl prevents autoheader generation and insertion of symbol template
dnl stub and definition into the first configuration header file. Do
dnl not use this macro as a replacement for AC_DEFINE_UNQUOTED, each
dnl one serves different functional needs.

AC_DEFUN([CARES_DEFINE_UNQUOTED], [
cat >>confdefs.h <<_EOF
[@%:@define] $1 ifelse($#, 2, [$2], 1)
_EOF
])

AX_CODE_COVERAGE
AC_SYS_LARGEFILE

case $host_os in
  solaris*)
    AC_DEFINE(ETC_INET, 1, [if a /etc/inet dir is being used])
    ;;
esac

dnl solaris needed flag
case $host_os in
  solaris2*)
    if test "x$GCC" = 'xyes'; then
      AX_APPEND_LINK_FLAGS([-mimpure-text])
    fi
    ;;
  *)
    ;;
esac

dnl -no-undefined libtool (not linker) flag for windows
cares_use_no_undefined=no
case $host_os in
  cygwin* | mingw* | pw32* | cegcc* | os2* | aix*)
    cares_use_no_undefined=yes
    ;;
  *)
    ;;
esac
AM_CONDITIONAL([CARES_USE_NO_UNDEFINED], [test "$cares_use_no_undefined" = 'yes'])


if test "$ac_cv_native_windows" = "yes" ; then
  AM_CPPFLAGS="$AM_CPPFLAGS -D_WIN32_WINNT=0x0602 -DWIN32_LEAN_AND_MEAN"
fi

dnl Windows can only build shared or static, not both at the same time
if test "$ac_cv_native_windows" = "yes" -a "x$enable_shared" = "xyes" -a "x$enable_static" = "xyes" ; then
  AC_MSG_ERROR([Windows cannot build both static and shared simultaneously, specify --disable-shared or --disable-static])
fi

dnl Only windows requires CARES_STATICLIB definition
if test "x$enable_shared" = "xno" -a "x$enable_static" = "xyes" ; then
  AC_MSG_CHECKING([whether we need CARES_STATICLIB definition])
  if test "$ac_cv_native_windows" = "yes" ; then
    AX_APPEND_FLAG([-DCARES_STATICLIB], [AM_CPPFLAGS])
    PKGCONFIG_CFLAGS="-DCARES_STATICLIB"
    AC_MSG_RESULT([yes])
  else
    AC_MSG_RESULT([no])
  fi
fi

dnl Test for symbol hiding
CARES_SYMBOL_HIDING_CFLAG=""
if test "$symbol_hiding" != "no" ; then
  compiler_supports_symbol_hiding="no"
  if test "$ac_cv_windows" = "yes" ; then
    compiler_supports_symbol_hiding="yes"
  else
    case "$ax_cv_c_compiler_vendor" in
      clang|gnu|intel)
        AX_APPEND_COMPILE_FLAGS([-fvisibility=hidden], [CARES_SYMBOL_HIDING_CFLAG])
        if test "x$CARES_SYMBOL_HIDING_CFLAG" != "x" ; then
          compiler_supports_symbol_hiding="yes"
        fi
        ;;
      sun)
        AX_APPEND_COMPILE_FLAGS([-xldscope=hidden], [CARES_SYMBOL_HIDING_CFLAG])
        if test "x$CARES_SYMBOL_HIDING_CFLAG" != "x" ; then
          compiler_supports_symbol_hiding="yes"
        fi
        ;;
    esac
  fi
  if test "$compiler_supports_symbol_hiding" = "no" ; then
    if test "$symbol_hiding" = "yes" ; then
      AC_MSG_ERROR([Compiler does not support symbol hiding])
    else
      symbol_hiding="no"
    fi
  else
    AC_DEFINE([CARES_SYMBOL_HIDING], [ 1 ], [Set to 1 if non-pubilc shared library symbols are hidden])
    symbol_hiding="yes"
  fi
fi
AM_CONDITIONAL(CARES_SYMBOL_HIDING, test "x$symbol_hiding" = "xyes")
AC_SUBST(CARES_SYMBOL_HIDING_CFLAG)


if test "$enable_warnings" = "yes"; then
  AX_APPEND_COMPILE_FLAGS([-Wall -Wextra -Waggregate-return -Wcast-align -Wcast-qual -Wconversion -Wdeclaration-after-statement -Wdouble-promotion -Wfloat-equal -Wformat-security -Winit-self -Wjump-misses-init -Wlogical-op -Wmissing-braces -Wmissing-declarations -Wmissing-format-attribute -Wmissing-include-dirs -Wmissing-prototypes -Wnested-externs -Wno-coverage-mismatch -Wold-style-definition -Wpacked -Wpedantic -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-conversion -Wstrict-overflow -Wstrict-prototypes -Wtrampolines -Wundef -Wunreachable-code -Wunused -Wvariadic-macros -Wvla -Wwrite-strings -Werror=implicit-int -Werror=implicit-function-declaration -Werror=partial-availability -Wno-long-long ],
    [AM_CFLAGS], [-Werror])
fi

dnl Android and QNX require c99, all others should use c90
case $host_os in
  *qnx*|*android*)
    AX_APPEND_COMPILE_FLAGS([-std=c99], [AM_CFLAGS], [-Werror])
    ;;
  *)
    AX_APPEND_COMPILE_FLAGS([-std=c90], [AM_CFLAGS], [-Werror])
    ;;
esac

dnl QNX needs -D_QNX_SOURCE
case $host_os in
  *qnx*)
    AX_APPEND_COMPILE_FLAGS([-D_QNX_SOURCE], [AM_CPPFLAGS], [-Werror])
  ;;
esac

if test "$ax_cv_c_compiler_vendor" = "intel"; then
  AX_APPEND_COMPILE_FLAGS([-shared-intel], [AM_CFLAGS])
fi

if test "$ac_cv_native_windows" = "yes" ; then
  dnl we use [ - ] in the 4th argument to tell AC_CHECK_HEADERS to simply
  dnl check for existence of the headers, not usability.  This is because
  dnl on windows, header order matters, and you need to include headers *after*
  dnl other headers, AC_CHECK_HEADERS only allows you to specify headers that
  dnl must be included *before* the header being checked.

  AC_CHECK_HEADERS([windows.h winsock2.h ws2tcpip.h iphlpapi.h netioapi.h ws2ipdef.h winternl.h ntdef.h ntstatus.h mswsock.h ],
    [], [], [-])

  dnl Windows builds require linking to iphlpapi
  if test "$ac_cv_header_winsock2_h" = "yes"; then
    LIBS="$LIBS -lws2_32 -liphlpapi"
  fi
fi

dnl **********************************************************************
dnl Checks for libraries.
dnl **********************************************************************

dnl see if libnsl or libsocket are required
AC_SEARCH_LIBS([getservbyport], [nsl socket resolv])

AC_MSG_CHECKING([if libxnet is required])
need_xnet=no
case $host_os in
  hpux*)
    XNET_LIBS=""
    AX_APPEND_LINK_FLAGS([-lxnet], [XNET_LIBS])
    if test "x$XNET_LIBS" != "x" ; then
      LIBS="$LIBS $XNET_LIBS"
      need_xnet=yes
    fi
    ;;
esac
AC_MSG_RESULT($need_xnet)

dnl resolv lib for z/OS
AS_IF([test "x$host_vendor" = "xibm" -a "x$host_os" = "xopenedition" ], [
  AC_SEARCH_LIBS([res_init], [resolv], [
    AC_DEFINE([CARES_USE_LIBRESOLV], [1], [Use resolver library to configure cares])
  ], [
    AC_MSG_ERROR([Unable to find libresolv which is required for z/OS])
  ])
])


dnl iOS 10?
AS_IF([test "x$host_vendor" = "xapple"], [
  AC_MSG_CHECKING([for iOS minimum version 10 or later])
  AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([[
#include <stdio.h>
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>
    ]], [[
#if TARGET_OS_IPHONE == 0 || __IPHONE_OS_VERSION_MIN_REQUIRED < 100000
#error Not iOS 10 or later
#endif
return 0;
   ]])
  ],[
    AC_MSG_RESULT([yes])
    ac_cv_ios_10="yes"
  ],[
    AC_MSG_RESULT([no])
  ])
])

dnl macOS 10.12?
AS_IF([test "x$host_vendor" = "xapple"], [
  AC_MSG_CHECKING([for macOS minimum version 10.12 or later])
  AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([[
#include <stdio.h>
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>
    ]], [[
#ifndef MAC_OS_X_VERSION_10_12
#  define MAC_OS_X_VERSION_10_12 101200
#endif
#if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_12
#error Not macOS 10.12 or later
#endif
return 0;
   ]])
  ],[
    AC_MSG_RESULT([yes])
    ac_cv_macos_10_12="yes"
  ],[
    AC_MSG_RESULT([no])
  ])
])

AC_MSG_CHECKING([whether to use libgcc])
AC_ARG_ENABLE(libgcc,
AS_HELP_STRING([--enable-libgcc],[use libgcc when linking]),
[ case "$enableval" in
  yes)
    LIBS="$LIBS -lgcc"
    AC_MSG_RESULT(yes)
    ;;
  *)
    AC_MSG_RESULT(no)
    ;;
  esac ],
       AC_MSG_RESULT(no)
)

dnl check for a few basic system headers we need.  It would be nice if we could
dnl split these on separate lines, but for some reason autotools on Windows doesn't
dnl allow this, even tried ending lines with a backslash.
AC_CHECK_HEADERS([malloc.h memory.h AvailabilityMacros.h sys/types.h sys/time.h sys/select.h sys/socket.h sys/filio.h sys/ioctl.h sys/param.h sys/uio.h sys/random.h sys/event.h sys/epoll.h assert.h iphlpapi.h netioapi.h netdb.h netinet/in.h netinet6/in6.h netinet/tcp.h net/if.h ifaddrs.h fcntl.h errno.h socket.h strings.h stdbool.h time.h poll.h limits.h arpa/nameser.h arpa/nameser_compat.h arpa/inet.h sys/system_properties.h ],
dnl to do if not found
[],
dnl to do if found
[],
dnl default includes
[
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
dnl We do this default-include simply to make sure that the nameser_compat.h
dnl header *REALLY* can be include after the new nameser.h. It seems AIX 5.1
dnl (and others?) is not designed to allow this.
#ifdef HAVE_ARPA_NAMESER_H
#include <arpa/nameser.h>
#endif

dnl *Sigh* these are needed in order for net/if.h to get properly detected.
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
]
)


cares_all_includes="
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_AVAILABILITYMACROS_H
#  include <AvailabilityMacros.h>
#endif
#ifdef HAVE_SYS_UIO_H
#  include <sys/uio.h>
#endif
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#ifdef HAVE_TCP_H
#  include <tcp.h>
#endif
#ifdef HAVE_SYS_FILIO_H
#  include <sys/filio.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef HAVE_STRING_H
#  include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif
#ifdef HAVE_TIME_H
#  include <time.h>
#endif
#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#  include <sys/stat.h>
#endif
#ifdef HAVE_SYS_RANDOM_H
#  include <sys/random.h>
#endif
#ifdef HAVE_SYS_EVENT_H
#  include <sys/event.h>
#endif
#ifdef HAVE_SYS_EPOLL_H
#  include <sys/epoll.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif
#ifdef HAVE_FCNTL_H
#  include <fcntl.h>
#endif
#ifdef HAVE_POLL_H
#  include <poll.h>
#endif
#ifdef HAVE_NET_IF_H
#  include <net/if.h>
#endif
#ifdef HAVE_IFADDRS_H
#  include <ifaddrs.h>
#endif
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_TCP_H
#  include <netinet/tcp.h>
#endif
#ifdef HAVE_NETDB_H
#  include <netdb.h>
#endif
#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif
#ifdef HAVE_RESOLV_H
#  include <resolv.h>
#endif
#ifdef HAVE_SYS_SYSTEM_PROPERTIES_H
#  include <sys/system_properties.h>
#endif
#ifdef HAVE_IPHLPAPI_H
#  include <iphlpapi.h>
#endif
#ifdef HAVE_NETIOAPI_H
#  include <netioapi.h>
#endif
#ifdef HAVE_WINSOCK2_H
#  include <winsock2.h>
#endif
#ifdef HAVE_WS2IPDEF_H
#  include <ws2ipdef.h>
#endif
#ifdef HAVE_WS2TCPIP_H
#  include <ws2tcpip.h>
#endif
#ifdef HAVE_WINDOWS_H
#  include <windows.h>
#endif
"

AC_CHECK_DECL([HAVE_ARPA_NAMESER_H],[CARES_DEFINE_UNQUOTED([CARES_HAVE_ARPA_NAMESER_H])], [])
AC_CHECK_DECL([HAVE_ARPA_NAMESER_COMPAT_H],[CARES_DEFINE_UNQUOTED([CARES_HAVE_ARPA_NAMESER_COMPAT_H])],[])
AC_CHECK_TYPE(long long, [AC_DEFINE(HAVE_LONGLONG, 1, [Define to 1 if the compiler supports the 'long long' data type.])])
AC_CHECK_TYPE(ssize_t, [ CARES_TYPEOF_ARES_SSIZE_T=ssize_t ], [ CARES_TYPEOF_ARES_SSIZE_T=int ])
AC_DEFINE_UNQUOTED([CARES_TYPEOF_ARES_SSIZE_T], ${CARES_TYPEOF_ARES_SSIZE_T}, [the signed version of size_t])

AC_CHECK_TYPE(socklen_t,
  [
    AC_DEFINE(HAVE_SOCKLEN_T, [], [socklen_t])
    CARES_DEFINE_UNQUOTED([CARES_TYPEOF_ARES_SOCKLEN_T], [socklen_t])
  ],
  [ CARES_DEFINE_UNQUOTED([CARES_TYPEOF_ARES_SOCKLEN_T], [int]) ],
  $cares_all_includes
 )

AC_CHECK_TYPE(SOCKET, [], [], $cares_all_includes)

dnl ###############################################################################

dnl clock_gettime might require an external library
AC_SEARCH_LIBS([clock_gettime], [rt posix4])

dnl Use AC_CHECK_DECL not AC_CHECK_FUNCS, while this doesn't do a linkage test,
dnl it just makes sure the headers define it, this is the only thing without
dnl a complex workaround on Windows that will do what we need. See:
dnl https://github.com/msys2/msys2/wiki/Porting/f87a222118b1008ebc166ad237f04edb759c8f4c#calling-conventions-stdcall-and-autotools
dnl https://lists.gnu.org/archive/html/autoconf/2013-05/msg00085.html
dnl and for a more complex workaround, we'd need to use AC_LINK_IFELSE like:
dnl https://mailman.videolan.org/pipermail/vlc-devel/2015-March/101802.html
dnl which would require we check each individually and provide function arguments
dnl for the test.

AC_CHECK_DECL(strnlen,         [AC_DEFINE([HAVE_STRNLEN],           1, [Define to 1 if you have `strnlen`]        )], [], $cares_all_includes)
AC_CHECK_DECL(memmem,          [AC_DEFINE([HAVE_MEMMEM],            1, [Define to 1 if you have `memmem`]         )], [], $cares_all_includes)
AC_CHECK_DECL(recv,            [AC_DEFINE([HAVE_RECV],              1, [Define to 1 if you have `recv`]           )], [], $cares_all_includes)
AC_CHECK_DECL(recvfrom,        [AC_DEFINE([HAVE_RECVFROM],          1, [Define to 1 if you have `recvfrom`]       )], [], $cares_all_includes)
AC_CHECK_DECL(send,            [AC_DEFINE([HAVE_SEND],              1, [Define to 1 if you have `send`]           )], [], $cares_all_includes)
AC_CHECK_DECL(sendto,          [AC_DEFINE([HAVE_SENDTO],            1, [Define to 1 if you have `sendto`]         )], [], $cares_all_includes)
AC_CHECK_DECL(getnameinfo,     [AC_DEFINE([HAVE_GETNAMEINFO],       1, [Define to 1 if you have `getnameinfo`]    )], [], $cares_all_includes)
AC_CHECK_DECL(gethostname,     [AC_DEFINE([HAVE_GETHOSTNAME],       1, [Define to 1 if you have `gethostname`]    )], [], $cares_all_includes)
AC_CHECK_DECL(connect,         [AC_DEFINE([HAVE_CONNECT],           1, [Define to 1 if you have `connect`]        )], [], $cares_all_includes)
AC_CHECK_DECL(connectx,        [AC_DEFINE([HAVE_CONNECTX],          1, [Define to 1 if you have `connectx`]       )], [], $cares_all_includes)
AC_CHECK_DECL(closesocket,     [AC_DEFINE([HAVE_CLOSESOCKET],       1, [Define to 1 if you have `closesocket`]    )], [], $cares_all_includes)
AC_CHECK_DECL(CloseSocket,     [AC_DEFINE([HAVE_CLOSESOCKET_CAMEL], 1, [Define to 1 if you have `CloseSocket`]    )], [], $cares_all_includes)
AC_CHECK_DECL(fcntl,           [AC_DEFINE([HAVE_FCNTL],             1, [Define to 1 if you have `fcntl`]          )], [], $cares_all_includes)
AC_CHECK_DECL(getenv,          [AC_DEFINE([HAVE_GETENV],            1, [Define to 1 if you have `getenv`]         )], [], $cares_all_includes)
AC_CHECK_DECL(gethostname,     [AC_DEFINE([HAVE_GETHOSTNAME],       1, [Define to 1 if you have `gethostname`]    )], [], $cares_all_includes)
AC_CHECK_DECL(getrandom,       [AC_DEFINE([HAVE_GETRANDOM],         1, [Define to 1 if you have `getrandom`]      )], [], $cares_all_includes)
AC_CHECK_DECL(getservbyport_r, [AC_DEFINE([HAVE_GETSERVBYPORT_R],   1, [Define to 1 if you have `getservbyport_r`])], [], $cares_all_includes)
AC_CHECK_DECL(inet_net_pton,   [AC_DEFINE([HAVE_INET_NET_PTON],     1, [Define to 1 if you have `inet_net_pton`]  )], [], $cares_all_includes)
AC_CHECK_DECL(inet_ntop,       [AC_DEFINE([HAVE_INET_NTOP],         1, [Define to 1 if you have `inet_ntop`]      )], [], $cares_all_includes)
AC_CHECK_DECL(inet_pton,       [AC_DEFINE([HAVE_INET_PTON],         1, [Define to 1 if you have `inet_pton`]      )], [], $cares_all_includes)
AC_CHECK_DECL(ioctl,           [AC_DEFINE([HAVE_IOCTL],             1, [Define to 1 if you have `ioctl`]          )], [], $cares_all_includes)
AC_CHECK_DECL(ioctlsocket,     [AC_DEFINE([HAVE_IOCTLSOCKET],       1, [Define to 1 if you have `ioctlsocket`]    )], [], $cares_all_includes)
AC_CHECK_DECL(IoctlSocket,     [AC_DEFINE([HAVE_IOCTLSOCKET_CAMEL], 1, [Define to 1 if you have `IoctlSocket`]    )], [], $cares_all_includes)
AC_CHECK_DECL(setsockopt,      [AC_DEFINE([HAVE_SETSOCKOPT],        1, [Define to 1 if you have `setsockopt`]     )], [], $cares_all_includes)
AC_CHECK_DECL(socket,          [AC_DEFINE([HAVE_SOCKET],            1, [Define to 1 if you have `socket`]         )], [], $cares_all_includes)
AC_CHECK_DECL(strcasecmp,      [AC_DEFINE([HAVE_STRCASECMP],        1, [Define to 1 if you have `strcasecmp`]     )], [], $cares_all_includes)
AC_CHECK_DECL(strdup,          [AC_DEFINE([HAVE_STRDUP],            1, [Define to 1 if you have `strdup`]         )], [], $cares_all_includes)
AC_CHECK_DECL(stricmp,         [AC_DEFINE([HAVE_STRICMP],           1, [Define to 1 if you have `stricmp`]        )], [], $cares_all_includes)
AC_CHECK_DECL(strncasecmp,     [AC_DEFINE([HAVE_STRNCASECMP],       1, [Define to 1 if you have `strncasecmp`]    )], [], $cares_all_includes)
AC_CHECK_DECL(strncmpi,        [AC_DEFINE([HAVE_STRNCMPI],          1, [Define to 1 if you have `strncmpi`]       )], [], $cares_all_includes)
AC_CHECK_DECL(strnicmp,        [AC_DEFINE([HAVE_STRNICMP],          1, [Define to 1 if you have `strnicmp`]       )], [], $cares_all_includes)
AC_CHECK_DECL(writev,          [AC_DEFINE([HAVE_WRITEV],            1, [Define to 1 if you have `writev`]         )], [], $cares_all_includes)
AC_CHECK_DECL(arc4random_buf,  [AC_DEFINE([HAVE_ARC4RANDOM_BUF],    1, [Define to 1 if you have `arc4random_buf`] )], [], $cares_all_includes)
AC_CHECK_DECL(stat,            [AC_DEFINE([HAVE_STAT],              1, [Define to 1 if you have `stat`]           )], [], $cares_all_includes)
AC_CHECK_DECL(gettimeofday,    [AC_DEFINE([HAVE_GETTIMEOFDAY],      1, [Define to 1 if you have `gettimeofday`]   )], [], $cares_all_includes)
AC_CHECK_DECL(clock_gettime,   [AC_DEFINE([HAVE_CLOCK_GETTIME],     1, [Define to 1 if you have `clock_gettime`]  )], [], $cares_all_includes)
AC_CHECK_DECL(if_indextoname,  [AC_DEFINE([HAVE_IF_INDEXTONAME],    1, [Define to 1 if you have `if_indextoname`] )], [], $cares_all_includes)
AC_CHECK_DECL(if_nametoindex,  [AC_DEFINE([HAVE_IF_NAMETOINDEX],    1, [Define to 1 if you have `if_nametoindex`] )], [], $cares_all_includes)
AC_CHECK_DECL(getifaddrs,      [AC_DEFINE([HAVE_GETIFADDRS],        1, [Define to 1 if you have `getifaddrs`]     )], [], $cares_all_includes)
AC_CHECK_DECL(poll,            [AC_DEFINE([HAVE_POLL],              1, [Define to 1 if you have `poll`]           )], [], $cares_all_includes)
AC_CHECK_DECL(pipe,            [AC_DEFINE([HAVE_PIPE],              1, [Define to 1 if you have `pipe`]           )], [], $cares_all_includes)
AC_CHECK_DECL(pipe2,           [AC_DEFINE([HAVE_PIPE2],             1, [Define to 1 if you have `pipe2`]          )], [], $cares_all_includes)
AC_CHECK_DECL(kqueue,          [AC_DEFINE([HAVE_KQUEUE],            1, [Define to 1 if you have `kqueue`]         )], [], $cares_all_includes)
AC_CHECK_DECL(epoll_create1,   [AC_DEFINE([HAVE_EPOLL],             1, [Define to 1 if you have `epoll_{create1,ctl,wait}`])], [], $cares_all_includes)
AC_CHECK_DECL(ConvertInterfaceIndexToLuid, [AC_DEFINE([HAVE_CONVERTINTERFACEINDEXTOLUID], 1, [Define to 1 if you have `ConvertInterfaceIndexToLuid`])], [], $cares_all_includes)
AC_CHECK_DECL(ConvertInterfaceLuidToNameA, [AC_DEFINE([HAVE_CONVERTINTERFACELUIDTONAMEA], 1, [Define to 1 if you have `ConvertInterfaceLuidToNameA`])], [], $cares_all_includes)
AC_CHECK_DECL(NotifyIpInterfaceChange,     [AC_DEFINE([HAVE_NOTIFYIPINTERFACECHANGE],     1, [Define to 1 if you have `NotifyIpInterfaceChange`]    )], [], $cares_all_includes)
AC_CHECK_DECL(RegisterWaitForSingleObject, [AC_DEFINE([HAVE_REGISTERWAITFORSINGLEOBJECT], 1, [Define to 1 if you have `RegisterWaitForSingleObject`])], [], $cares_all_includes)
AC_CHECK_DECL(__system_property_get,       [AC_DEFINE([HAVE___SYSTEM_PROPERTY_GET],       1, [Define to 1 if you have `__system_property_get`]      )], [], $cares_all_includes)


dnl ###############################################################################
dnl recv, recvfrom, send, getnameinfo, gethostname
dnl ARGUMENTS AND RETURN VALUES

if test "x$ac_cv_type_ssize_t" = "xyes" -a "x$ac_cv_type_socklen_t" = "xyes" -a "x$ac_cv_native_windows" != "xyes" ; then
  recvfrom_type_retv="ssize_t"
  recvfrom_type_arg3="size_t"
else
  recvfrom_type_retv="int"
  recvfrom_type_arg3="int"
fi

if test "x$ac_cv_type_SOCKET" = "xyes" ; then
  dnl If the SOCKET type is defined, it uses socket ... should be windows only
  recvfrom_type_arg1="SOCKET"
else
  recvfrom_type_arg1="int"
fi

if test "x$ac_cv_type_socklen_t" = "xyes" ; then
  recvfrom_type_arg6="socklen_t *"
  getnameinfo_type_arg2="socklen_t"
  getnameinfo_type_arg46="socklen_t"
else
  recvfrom_type_arg6="int *"
  getnameinfo_type_arg2="int"
  getnameinfo_type_arg46="int"
fi

if test "x$ac_cv_native_windows" = "xyes" ; then
  recv_type_arg2="char *"
else
  recv_type_arg2="void *"
fi

dnl Functions are typically consistent so the equivalent fields map ... equivalently
recv_type_retv=${recvfrom_type_retv}
send_type_retv=${recvfrom_type_retv}
recv_type_arg1=${recvfrom_type_arg1}
recvfrom_type_arg2=${recv_type_arg2}
send_type_arg1=${recvfrom_type_arg1}
recv_type_arg3=${recvfrom_type_arg3}
send_type_arg3=${recvfrom_type_arg3}
gethostname_type_arg2=${recvfrom_type_arg3}

dnl These should always be "sane" values to use always
recvfrom_qual_arg5=
recvfrom_type_arg4=int
recvfrom_type_arg5="struct sockaddr *"
recv_type_arg4=int
getnameinfo_type_arg1="struct sockaddr *"
getnameinfo_type_arg7=int
send_type_arg2="const void *"
send_type_arg4=int

AC_DEFINE_UNQUOTED([RECVFROM_TYPE_RETV],     [ ${recvfrom_type_retv} ],     [ recvfrom() return value ])
AC_DEFINE_UNQUOTED([RECVFROM_TYPE_ARG1],     [ ${recvfrom_type_arg1} ],     [ recvfrom() arg1 type ])
AC_DEFINE_UNQUOTED([RECVFROM_TYPE_ARG2],     [ ${recvfrom_type_arg2} ],     [ recvfrom() arg2 type ])
AC_DEFINE_UNQUOTED([RECVFROM_TYPE_ARG3],     [ ${recvfrom_type_arg3} ],     [ recvfrom() arg3 type ])
AC_DEFINE_UNQUOTED([RECVFROM_TYPE_ARG4],     [ ${recvfrom_type_arg4} ],     [ recvfrom() arg4 type ])
AC_DEFINE_UNQUOTED([RECVFROM_TYPE_ARG5],     [ ${recvfrom_type_arg5} ],     [ recvfrom() arg5 type ])
AC_DEFINE_UNQUOTED([RECVFROM_QUAL_ARG5],     [ ${recvfrom_qual_arg5}],      [ recvfrom() arg5 qualifier])

AC_DEFINE_UNQUOTED([RECV_TYPE_RETV],         [ ${recv_type_retv} ],         [ recv() return value ])
AC_DEFINE_UNQUOTED([RECV_TYPE_ARG1],         [ ${recv_type_arg1} ],         [ recv() arg1 type ])
AC_DEFINE_UNQUOTED([RECV_TYPE_ARG2],         [ ${recv_type_arg2} ],         [ recv() arg2 type ])
AC_DEFINE_UNQUOTED([RECV_TYPE_ARG3],         [ ${recv_type_arg3} ],         [ recv() arg3 type ])
AC_DEFINE_UNQUOTED([RECV_TYPE_ARG4],         [ ${recv_type_arg4} ],         [ recv() arg4 type ])

AC_DEFINE_UNQUOTED([SEND_TYPE_RETV],         [ ${send_type_retv} ],         [ send() return value ])
AC_DEFINE_UNQUOTED([SEND_TYPE_ARG1],         [ ${send_type_arg1} ],         [ send() arg1 type ])
AC_DEFINE_UNQUOTED([SEND_TYPE_ARG2],         [ ${send_type_arg2} ],         [ send() arg2 type ])
AC_DEFINE_UNQUOTED([SEND_TYPE_ARG3],         [ ${send_type_arg3} ],         [ send() arg3 type ])
AC_DEFINE_UNQUOTED([SEND_TYPE_ARG4],         [ ${send_type_arg4} ],         [ send() arg4 type ])

AC_DEFINE_UNQUOTED([GETNAMEINFO_TYPE_ARG1],  [ ${getnameinfo_type_arg1} ],  [ getnameinfo() arg1 type ])
AC_DEFINE_UNQUOTED([GETNAMEINFO_TYPE_ARG2],  [ ${getnameinfo_type_arg2} ],  [ getnameinfo() arg2 type ])
AC_DEFINE_UNQUOTED([GETNAMEINFO_TYPE_ARG7],  [ ${getnameinfo_type_arg7} ],  [ getnameinfo() arg7 type ])
AC_DEFINE_UNQUOTED([GETNAMEINFO_TYPE_ARG46], [ ${getnameinfo_type_arg46} ], [ getnameinfo() arg4 and 6 type ])

AC_DEFINE_UNQUOTED([GETHOSTNAME_TYPE_ARG2],  [ ${gethostname_type_arg2} ],  [ gethostname() arg2 type ])
dnl ###############################################################################


if test "$ac_cv_have_decl_getservbyport_r" = "yes" ; then
  AC_MSG_CHECKING([number of arguments for getservbyport_r()])
  getservbyport_r_args=6
  case $host_os in
    solaris*)
      getservbyport_r_args=5
    ;;
    aix*|openbsd*)
      getservbyport_r_args=4
    ;;
  esac
  AC_MSG_RESULT([$getservbyport_r_args])
  AC_DEFINE_UNQUOTED([GETSERVBYPORT_R_ARGS], [ $getservbyport_r_args ], [ number of arguments for getservbyport_r() ])
fi

if test "$ac_cv_have_decl_getservbyname_r" = "yes" ; then
  AC_MSG_CHECKING([number of arguments for getservbyname_r()])
  getservbyname_r_args=6
  case $host_os in
    solaris*)
      getservbyname_r_args=5
    ;;
    aix*|openbsd*)
      getservbyname_r_args=4
    ;;
  esac
  AC_DEFINE_UNQUOTED([GETSERVBYNAME_R_ARGS], [ $getservbyname_r_args ], [ number of arguments for getservbyname_r() ])
  AC_MSG_RESULT([$getservbyname_r_args])
fi

AC_TYPE_SIZE_T
AC_CHECK_DECL(AF_INET6, [AC_DEFINE([HAVE_AF_INET6],1,[Define to 1 if you have AF_INET6])], [], $cares_all_includes)
AC_CHECK_DECL(PF_INET6, [AC_DEFINE([HAVE_PF_INET6],1,[Define to 1 if you have PF_INET6])], [], $cares_all_includes)
AC_CHECK_TYPES(struct in6_addr, [], [], $cares_all_includes)
AC_CHECK_TYPES(struct sockaddr_in6, [], [], $cares_all_includes)
AC_CHECK_TYPES(struct sockaddr_storage, [], [], $cares_all_includes)
AC_CHECK_TYPES(struct addrinfo, [], [], $cares_all_includes)
AC_CHECK_TYPES(struct timeval, [], [], $cares_all_includes)
AC_CHECK_MEMBERS(struct sockaddr_in6.sin6_scope_id, [], [], $cares_all_includes)
AC_CHECK_MEMBERS(struct addrinfo.ai_flags, [], [], $cares_all_includes)
AC_CHECK_DECL(FIONBIO, [], [], $cares_all_includes)
AC_CHECK_DECL(O_NONBLOCK, [], [], $cares_all_includes)
AC_CHECK_DECL(SO_NONBLOCK, [], [], $cares_all_includes)
AC_CHECK_DECL(MSG_NOSIGNAL, [], [], $cares_all_includes)
AC_CHECK_DECL(CLOCK_MONOTONIC, [], [], $cares_all_includes)

if test "$ac_cv_have_decl_CLOCK_MONOTONIC" = "yes" -a "$ac_cv_have_decl_clock_gettime" = "yes" ; then
  AC_DEFINE([HAVE_CLOCK_GETTIME_MONOTONIC], [ 1 ], [ clock_gettime() with CLOCK_MONOTONIC support ])
fi

if test "$ac_cv_have_decl_FIONBIO" = "yes" -a "$ac_cv_have_decl_ioctl" = "yes" ; then
  AC_DEFINE([HAVE_IOCTL_FIONBIO], [ 1 ], [ ioctl() with FIONBIO support ])
fi
if test "$ac_cv_have_decl_FIONBIO" = "yes" -a "$ac_cv_have_decl_ioctlsocket" = "yes" ; then
  AC_DEFINE([HAVE_IOCTLSOCKET_FIONBIO], [ 1 ], [ ioctlsocket() with FIONBIO support ])
fi
if test "$ac_cv_have_decl_SO_NONBLOCK" = "yes" -a "$ac_cv_have_decl_setsockopt" = "yes" ; then
  AC_DEFINE([HAVE_SETSOCKOPT_SO_NONBLOCK], [ 1 ], [ setsockopt() with SO_NONBLOCK support ])
fi
if test "$ac_cv_have_decl_O_NONBLOCK" = "yes" -a "$ac_cv_have_decl_fcntl" = "yes" ; then
  AC_DEFINE([HAVE_FCNTL_O_NONBLOCK], [ 1 ], [ fcntl() with O_NONBLOCK support ])
fi

dnl ares_build.h.in specific defines
if test "x$ac_cv_header_sys_types_h" = "xyes" ; then
  CARES_DEFINE_UNQUOTED([CARES_HAVE_SYS_TYPES_H],[1])
fi
if test "x$ac_cv_header_sys_socket_h" = "xyes" ; then
  CARES_DEFINE_UNQUOTED([CARES_HAVE_SYS_SOCKET_H],[1])
fi
if test "x$ac_cv_header_sys_select_h" = "xyes" ; then
  CARES_DEFINE_UNQUOTED([CARES_HAVE_SYS_SELECT_H],[1])
fi
if test "x$ac_cv_header_ws2tcpip_h" = "xyes" ; then
  CARES_DEFINE_UNQUOTED([CARES_HAVE_WS2TCPIP_H],[1])
fi
if test "x$ac_cv_header_winsock2_h" = "xyes" ; then
  CARES_DEFINE_UNQUOTED([CARES_HAVE_WINSOCK2_H],[1])
fi
if test "x$ac_cv_header_windows_h" = "xyes" ; then
  CARES_DEFINE_UNQUOTED([CARES_HAVE_WINDOWS_H],[1])
fi
if test "x$ac_cv_header_arpa_nameser_h" = "xyes" ; then
  CARES_DEFINE_UNQUOTED([CARES_HAVE_ARPA_NAMESER_H],[1])
fi
if test "x$ac_cv_header_arpa_nameser_compa_h" = "xyes" ; then
  CARES_DEFINE_UNQUOTED([CARES_HAVE_ARPA_NAMESER_COMPA_H],[1])
fi


dnl ------------ THREADING --------------

dnl windows always supports threads, only check non-windows systems.
if test "${CARES_THREADS}" = "yes" -a "x${ac_cv_native_windows}" != "xyes" ; then
  AX_PTHREAD([ ], [
    AC_MSG_WARN([threads requested but not supported])
    CARES_THREADS=no
  ])

  if test "${CARES_THREADS}" = "yes" ; then
    AC_CHECK_HEADERS([pthread.h pthread_np.h])
    LIBS="$PTHREAD_LIBS $LIBS"
    AM_CFLAGS="$AM_CFLAGS $PTHREAD_CFLAGS"
    CC="$PTHREAD_CC"
    CXX="$PTHREAD_CXX"
  fi
fi

if test "${CARES_THREADS}" = "yes" ; then
  AC_DEFINE([CARES_THREADS], [ 1 ], [Threading enabled])
fi

CARES_PRIVATE_LIBS="$LIBS"
AC_SUBST(CARES_PRIVATE_LIBS)

BUILD_SUBDIRS="include src docs"


dnl ******** TESTS *******

if test "x$build_tests" != "xno" -a "x$HAVE_CXX14" = "0" ; then
  if test "x$build_tests" = "xmaybe" ; then
    AC_MSG_WARN([cannot build tests without a CXX14 compiler])
    build_tests=no
  else
    AC_MSG_ERROR([*** Building tests requires a CXX14 compiler])
  fi
fi
if test "x$build_tests" != "xno" -a "x$cross_compiling" = "xyes" ; then
  if test "x$build_tests" = "xmaybe" ; then
    AC_MSG_WARN([cannot build tests when cross compiling])
    build_tests=no
  else
    AC_MSG_ERROR([*** Tests not supported when cross compiling])
  fi
fi

dnl Forces compiling of tests even when cross-compiling.
AC_ARG_ENABLE(tests-crossbuild,
  AS_HELP_STRING([--enable-tests-crossbuild], [Enable test building even when cross building]),
  [build_tests="$enableval"]
)

if test "x$build_tests" != "xno" ; then
  PKG_CHECK_MODULES([GMOCK], [gmock], [ have_gmock=yes ], [ have_gmock=no ])
  if test "x$have_gmock" = "xno" ; then
    if test "x$build_tests" = "xmaybe" ; then
      AC_MSG_WARN([gmock could not be found, not building tests])
      build_tests=no
    else
      AC_MSG_ERROR([tests require gmock])
    fi
  else
    PKG_CHECK_MODULES([GMOCK112], [gmock >= 1.12.0], [ have_gmock_v112=yes ], [ have_gmock_v112=no ])
    if test "x$have_gmock_v112" = "xyes" ; then
      ARES_CHECK_USER_NAMESPACE
      ARES_CHECK_UTS_NAMESPACE
    fi
  fi
fi
if test "x$build_tests" != "xno" ; then
  build_tests=yes

  AX_CXX_COMPILE_STDCXX_14([noext],[mandatory])
  if test "$ac_cv_native_windows" != "yes" ; then
    AX_PTHREAD([ CARES_TEST_PTHREADS="yes" ], [ AC_MSG_ERROR([threading required for tests]) ])
  fi

  BUILD_SUBDIRS="${BUILD_SUBDIRS} test"
fi
AC_MSG_CHECKING([whether to build tests])
AC_MSG_RESULT([$build_tests])

AM_CONDITIONAL(BUILD_TESTS, test "x$build_tests" = "xyes")

AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_CPPFLAGS)
AC_SUBST(PKGCONFIG_CFLAGS)
AC_SUBST(BUILD_SUBDIRS)

AC_CONFIG_FILES([Makefile
                 include/Makefile
                 src/Makefile
                 src/lib/Makefile
                 src/tools/Makefile
                 docs/Makefile
                 libcares.pc ])
AM_COND_IF([BUILD_TESTS],
           [AC_CONFIG_FILES([test/Makefile])])

AC_OUTPUT
