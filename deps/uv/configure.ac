# Copyright (c) 2013, Ben Noordhuis <info@bnoordhuis.nl>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

AC_PREREQ(2.57)
AC_INIT([libuv], [1.48.0], [https://github.com/libuv/libuv/issues])
AC_CONFIG_MACRO_DIR([m4])
m4_include([m4/libuv-extra-automake-flags.m4])
m4_include([m4/as_case.m4])
m4_include([m4/libuv-check-flags.m4])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects] UV_EXTRA_AUTOMAKE_FLAGS)
AM_MAINTAINER_MODE([enable]) # pass --disable-maintainer-mode if autotools may be unavailable
AC_CANONICAL_HOST
AC_ENABLE_SHARED
AC_ENABLE_STATIC
AC_PROG_CC
AM_PROG_CC_C_O

CC_ATTRIBUTE_VISIBILITY([default], [
  CC_FLAG_VISIBILITY([CFLAGS="${CFLAGS} -fvisibility=hidden"])
])
# Xlc has a flag "-f<filename>". Need to use CC_CHECK_FLAG_SUPPORTED_APPEND so
# we exclude -fno-strict-aliasing for xlc
CC_CHECK_FLAG_SUPPORTED_APPEND([-fno-strict-aliasing])
CC_CHECK_CFLAGS_APPEND([-g])
CC_CHECK_CFLAGS_APPEND([-std=gnu89])
CC_CHECK_CFLAGS_APPEND([-Wall])
CC_CHECK_CFLAGS_APPEND([-Wextra])
CC_CHECK_CFLAGS_APPEND([-Wno-long-long])
CC_CHECK_CFLAGS_APPEND([-Wno-unused-parameter])
CC_CHECK_CFLAGS_APPEND([-Wstrict-prototypes])
# AM_PROG_AR is not available in automake v0.11 but it's essential in v0.12.
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
# autoconf complains if AC_PROG_LIBTOOL precedes AM_PROG_AR.
AC_PROG_LIBTOOL
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
LT_INIT
AX_PTHREAD([
    LIBS="$LIBS $PTHREAD_LIBS"
    CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
])
AC_SEARCH_LIBS([dlopen], [dl])
AC_SEARCH_LIBS([kstat_lookup], [kstat])
AC_SEARCH_LIBS([gethostbyname], [nsl])
AC_SEARCH_LIBS([perfstat_cpu], [perfstat])
AC_SEARCH_LIBS([clock_gettime], [rt])
AC_SEARCH_LIBS([sendfile], [sendfile])
AC_SEARCH_LIBS([socket], [socket])
AC_SYS_LARGEFILE
AM_CONDITIONAL([AIX],      [AS_CASE([$host_os],[aix*],          [true], [false])])
AM_CONDITIONAL([ANDROID],  [AS_CASE([$host_os],[linux-android*],[true], [false])])
AM_CONDITIONAL([CYGWIN],   [AS_CASE([$host_os],[cygwin*],       [true], [false])])
AM_CONDITIONAL([DARWIN],   [AS_CASE([$host_os],[darwin*],       [true], [false])])
AM_CONDITIONAL([DRAGONFLY],[AS_CASE([$host_os],[dragonfly*],    [true], [false])])
AM_CONDITIONAL([FREEBSD],  [AS_CASE([$host_os],[freebsd*],      [true], [false])])
AM_CONDITIONAL([HAIKU],    [AS_CASE([$host_os],[haiku],         [true], [false])])
AM_CONDITIONAL([HURD],     [AS_CASE([$host_os],[gnu*],          [true], [false])])
AM_CONDITIONAL([LINUX],    [AS_CASE([$host_os],[linux*],        [true], [false])])
AM_CONDITIONAL([MSYS],     [AS_CASE([$host_os],[msys*],         [true], [false])])
AM_CONDITIONAL([NETBSD],   [AS_CASE([$host_os],[netbsd*],       [true], [false])])
AM_CONDITIONAL([OPENBSD],  [AS_CASE([$host_os],[openbsd*],      [true], [false])])
AM_CONDITIONAL([OS390],    [AS_CASE([$host_os],[openedition*],  [true], [false])])
AM_CONDITIONAL([OS400],    [AS_CASE([$host_os],[os400],         [true], [false])])
AM_CONDITIONAL([SUNOS],    [AS_CASE([$host_os],[solaris*],      [true], [false])])
AM_CONDITIONAL([WINNT],    [AS_CASE([$host_os],[mingw*],        [true], [false])])
AS_CASE([$host_os],[mingw*], [
    LIBS="$LIBS -lws2_32 -lpsapi -liphlpapi -luserenv -luser32 -ldbghelp -lole32 -lshell32"
])
AS_CASE([$host_os], [solaris2.10], [
    CFLAGS="$CFLAGS -DSUNOS_NO_IFADDRS"
])
AS_CASE([$host_os], [netbsd*], [AC_CHECK_LIB([kvm], [kvm_open])])
AS_CASE([$host_os], [haiku], [
    LIBS="$LIBS -lnetwork"
])
AC_CHECK_HEADERS([sys/ahafs_evProds.h])
AC_CONFIG_FILES([Makefile libuv.pc])
AC_CONFIG_LINKS([test/fixtures/empty_file:test/fixtures/empty_file])
AC_CONFIG_LINKS([test/fixtures/load_error.node:test/fixtures/load_error.node])
AC_CONFIG_LINKS([test/fixtures/lorem_ipsum.txt:test/fixtures/lorem_ipsum.txt])
AC_CONFIG_LINKS([test/fixtures/one_file/one_file:test/fixtures/one_file/one_file])
AC_OUTPUT
