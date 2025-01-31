#############################################################
#
# Copyright (C) the Massachusetts Institute of Technology.
# Copyright (C) Daniel Stenberg
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# SPDX-License-Identifier: MIT
#
#############################################################

AUTOMAKE_OPTIONS = foreign nostdinc 1.9.6
ACLOCAL_AMFLAGS = -I m4 --install

MSVCFILES = buildconf.bat

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
