# nghttp2 - HTTP/2 C Library

# Copyright (c) 2012, 2013 Tatsuhiro Tsujikawa

# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:

# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
SUBDIRS = includes

EXTRA_DIST = Makefile.msvc CMakeLists.txt version.rc.in

AM_CFLAGS = $(WARNCFLAGS) $(EXTRACFLAG)
AM_CPPFLAGS = -I$(srcdir)/includes -I$(builddir)/includes -DBUILDING_NGHTTP2 \
	@DEFS@
AM_LDFLAGS = @LIBTOOL_LDFLAGS@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libnghttp2.pc
DISTCLEANFILES = $(pkgconfig_DATA)

lib_LTLIBRARIES = libnghttp2.la

OBJECTS = nghttp2_pq.c nghttp2_map.c nghttp2_queue.c \
	nghttp2_frame.c \
	nghttp2_buf.c \
	nghttp2_stream.c nghttp2_outbound_item.c \
	nghttp2_session.c nghttp2_submit.c \
	nghttp2_helper.c \
	nghttp2_alpn.c \
	nghttp2_hd.c nghttp2_hd_huffman.c nghttp2_hd_huffman_data.c \
	nghttp2_version.c \
	nghttp2_priority_spec.c \
	nghttp2_option.c \
	nghttp2_callbacks.c \
	nghttp2_mem.c \
	nghttp2_http.c \
	nghttp2_rcbuf.c \
	nghttp2_extpri.c \
	nghttp2_ratelim.c \
	nghttp2_time.c \
	nghttp2_debug.c \
	sfparse.c

HFILES = nghttp2_pq.h nghttp2_int.h nghttp2_map.h nghttp2_queue.h \
	nghttp2_frame.h \
	nghttp2_buf.h \
	nghttp2_session.h nghttp2_helper.h nghttp2_stream.h nghttp2_int.h \
	nghttp2_alpn.h \
	nghttp2_submit.h nghttp2_outbound_item.h \
	nghttp2_net.h \
	nghttp2_hd.h nghttp2_hd_huffman.h \
	nghttp2_priority_spec.h \
	nghttp2_option.h \
	nghttp2_callbacks.h \
	nghttp2_mem.h \
	nghttp2_http.h \
	nghttp2_rcbuf.h \
	nghttp2_extpri.h \
	nghttp2_ratelim.h \
	nghttp2_time.h \
	nghttp2_debug.h \
	sfparse.h

libnghttp2_la_SOURCES = $(HFILES) $(OBJECTS)
libnghttp2_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)
