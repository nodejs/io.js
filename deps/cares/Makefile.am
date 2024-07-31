#############################################################
#
# Copyright (C) the Massachusetts Institute of Technology.
# Copyright (C) Daniel Stenberg
#
# Permission to use, copy, modify, and distribute this
# software and its documentation for any purpose and without
# fee is hereby granted, provided that the above copyright
# notice appear in all copies and that both that copyright
# notice and this permission notice appear in supporting
# documentation, and that the name of M.I.T. not be used in
# advertising or publicity pertaining to distribution of the
# software without specific, written prior permission.
# M.I.T. makes no representations about the suitability of
# this software for any purpose.  It is provided "as is"
# without express or implied warranty.
#
# SPDX-License-Identifier: MIT
#
#############################################################

AUTOMAKE_OPTIONS = foreign nostdinc 1.9.6
ACLOCAL_AMFLAGS = -I m4 --install

MSVCFILES = msvc_ver.inc buildconf.bat

# adig and ahost are just sample programs and thus not mentioned with the
# regular sources and headers
EXTRA_DIST = AUTHORS $(man_MANS) RELEASE-NOTES.md	\
 c-ares-config.cmake.in libcares.pc.cmake libcares.pc.in buildconf \
 README.msvc $(MSVCFILES) INSTALL.md README.md LICENSE.md \
 CMakeLists.txt Makefile.dj Makefile.m32 Makefile.netware Makefile.msvc \
 Makefile.Watcom CONTRIBUTING.md SECURITY.md DEVELOPER-NOTES.md \
 cmake/EnableWarnings.cmake

CLEANFILES = $(PDFPAGES) $(HTMLPAGES)

DISTCLEANFILES = include/ares_build.h

DIST_SUBDIRS = include src test docs

SUBDIRS = @BUILD_SUBDIRS@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libcares.pc

# where to install the c-ares headers
libcares_ladir = $(includedir)


# Make files named *.dist replace the file without .dist extension
dist-hook:
	find $(distdir) -name "*.dist" -exec rm {} \;
	(distit=`find $(srcdir) -name "*.dist"`; \
	for file in $$distit; do \
	  strip=`echo $$file | sed -e s/^$(srcdir)// -e s/\.dist//`; \
	  cp $$file $(distdir)$$strip; \
	done)
