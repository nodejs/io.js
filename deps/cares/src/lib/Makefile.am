# Copyright (C) The c-ares project and its contributors
# SPDX-License-Identifier: MIT
AUTOMAKE_OPTIONS = foreign subdir-objects nostdinc 1.9.6
ACLOCAL_AMFLAGS = -I m4 --install

# Specify our include paths here, and do it relative to $(top_srcdir) and
# $(top_builddir), to ensure that these paths which belong to the library
# being currently built and tested are searched before the library which
# might possibly already be installed in the system.

AM_CPPFLAGS += -I$(top_builddir)/include \
               -I$(top_builddir)/src/lib \
               -I$(top_srcdir)/include \
               -I$(top_srcdir)/src/lib

lib_LTLIBRARIES = libcares.la

man_MANS = $(MANPAGES)

# adig and ahost are just sample programs and thus not mentioned with the
# regular sources and headers
EXTRA_DIST = Makefile.inc config-win32.h CMakeLists.txt \
  ares_config.h.in ares_config.h.cmake cares.rc \
  $(CSOURCES) $(HHEADERS) config-dos.h

DISTCLEANFILES = ares_config.h

DIST_SUBDIRS =

libcares_la_LDFLAGS = -version-info @CARES_VERSION_INFO@
if CARES_USE_NO_UNDEFINED
libcares_la_LDFLAGS += -no-undefined
endif

libcares_la_CFLAGS_EXTRA =

libcares_la_CPPFLAGS_EXTRA = -DCARES_BUILDING_LIBRARY

if CARES_SYMBOL_HIDING
libcares_la_CFLAGS_EXTRA += @CARES_SYMBOL_HIDING_CFLAG@
libcares_la_CPPFLAGS_EXTRA += -DCARES_SYMBOL_HIDING
endif

include $(top_srcdir)/aminclude_static.am
libcares_la_LIBS = $(CODE_COVERAGE_LIBS)
libcares_la_CFLAGS_EXTRA += $(CODE_COVERAGE_CFLAGS)
libcares_la_CPPFLAGS_EXTRA += $(CODE_COVERAGE_CPPFLAGS)

libcares_la_CFLAGS = $(AM_CFLAGS) $(libcares_la_CFLAGS_EXTRA)

libcares_la_CPPFLAGS = $(AM_CPPFLAGS) $(libcares_la_CPPFLAGS_EXTRA)

# Makefile.inc provides the CSOURCES and HHEADERS defines
include Makefile.inc

libcares_la_SOURCES = $(CSOURCES) $(HHEADERS)
