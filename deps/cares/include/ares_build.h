/* MIT License
 *
 * Copyright (c) 2009 Daniel Stenberg
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
#ifndef __CARES_BUILD_H
#define __CARES_BUILD_H


/* ================================================================ */
/*               NOTES FOR CONFIGURE CAPABLE SYSTEMS                */
/* ================================================================ */

/*
 * NOTE 1:
 * -------
 *
 * See file ares_build.h.in, run configure, and forget that this file
 * exists it is only used for non-configure systems.
 * But you can keep reading if you want ;-)
 *
 */

/* ================================================================ */
/*                 NOTES FOR NON-CONFIGURE SYSTEMS                  */
/* ================================================================ */

/*
 * NOTE 1:
 * -------
 *
 * Nothing in this file is intended to be modified or adjusted by the
 * c-ares library user nor by the c-ares library builder.
 *
 * If you think that something actually needs to be changed, adjusted
 * or fixed in this file, then, report it on the c-ares development
 * mailing list: http://lists.haxx.se/listinfo/c-ares/
 *
 * Try to keep one section per platform, compiler and architecture,
 * otherwise, if an existing section is reused for a different one and
 * later on the original is adjusted, probably the piggybacking one can
 * be adversely changed.
 *
 * In order to differentiate between platforms/compilers/architectures
 * use only compiler built in predefined preprocessor symbols.
 *
 * This header file shall only export symbols which are 'cares' or 'CARES'
 * prefixed, otherwise public name space would be polluted.
 *
 * NOTE 2:
 * -------
 *
 * Right now you might be staring at file ares_build.h.dist or ares_build.h,
 * this is due to the following reason: file ares_build.h.dist is renamed
 * to ares_build.h when the c-ares source code distribution archive file is
 * created.
 *
 * File ares_build.h.dist is not included in the distribution archive.
 * File ares_build.h is not present in the git tree.
 *
 * The distributed ares_build.h file is only intended to be used on systems
 * which can not run the also distributed configure script.
 *
 * On systems capable of running the configure script, the configure process
 * will overwrite the distributed ares_build.h file with one that is suitable
 * and specific to the library being configured and built, which is generated
 * from the ares_build.h.in template file.
 *
 * If you check out from git on a non-configure platform, you must run the
 * appropriate buildconf* script to set up ares_build.h and other local files.
 *
 */

/* ================================================================ */
/*  DEFINITION OF THESE SYMBOLS SHALL NOT TAKE PLACE ANYWHERE ELSE  */
/* ================================================================ */

#ifdef CARES_TYPEOF_ARES_SOCKLEN_T
#  error "CARES_TYPEOF_ARES_SOCKLEN_T shall not be defined except in ares_build.h"
   Error Compilation_aborted_CARES_TYPEOF_ARES_SOCKLEN_T_already_defined
#endif

/* ================================================================ */
/*    EXTERNAL INTERFACE SETTINGS FOR NON-CONFIGURE SYSTEMS ONLY    */
/* ================================================================ */

#if defined(__DJGPP__) || defined(__GO32__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__SALFORDC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__BORLANDC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__TURBOC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__WATCOMC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__POCC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__LCC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__SYMBIAN32__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T unsigned int

#elif defined(__MWERKS__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(_WIN32_WCE)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(__VMS)
#  define CARES_TYPEOF_ARES_SOCKLEN_T unsigned int

#elif defined(__OS400__)
#  if defined(__ILEC400__)
#    define CARES_TYPEOF_ARES_SOCKLEN_T socklen_t
#    define CARES_HAVE_SYS_TYPES_H      1
#    define CARES_HAVE_SYS_SOCKET_H     1
#    define CARES_HAVE_SYS_SELECT_H     1
#  endif

#elif defined(__MVS__)
#  if defined(__IBMC__) || defined(__IBMCPP__)
#    define CARES_TYPEOF_ARES_SOCKLEN_T socklen_t
#    define CARES_HAVE_SYS_TYPES_H      1
#    define CARES_HAVE_SYS_SOCKET_H     1
#    define CARES_HAVE_SYS_SELECT_H     1
#  endif

#elif defined(__370__)
#  if defined(__IBMC__) || defined(__IBMCPP__)
#    define CARES_TYPEOF_ARES_SOCKLEN_T socklen_t
#    define CARES_HAVE_SYS_TYPES_H      1
#    define CARES_HAVE_SYS_SOCKET_H     1
#    define CARES_HAVE_SYS_SELECT_H     1
#  endif

#elif defined(TPF)
#  define CARES_TYPEOF_ARES_SOCKLEN_T int

#elif defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define CARES_TYPEOF_ARES_SOCKLEN_T int
#  define CARES_HAVE_WINDOWS_H          1
#  define CARES_HAVE_SYS_TYPES_H        1

#  if defined(WATT32)
#    define CARES_HAVE_SYS_SOCKET_H     1
#    define CARES_HAVE_SYS_SELECT_H     1
#  else
#    define CARES_HAVE_WS2TCPIP_H       1
#    define CARES_HAVE_WINSOCK2_H       1
#  endif

#elif defined(__GNUC__)
#  define CARES_TYPEOF_ARES_SOCKLEN_T socklen_t
#  define CARES_HAVE_SYS_TYPES_H      1
#  define CARES_HAVE_SYS_SOCKET_H     1
#  define CARES_HAVE_SYS_SELECT_H     1

#else
#  error "Unknown non-configure build target!"
   Error Compilation_aborted_Unknown_non_configure_build_target
#endif

/* Data type definition of ares_ssize_t. */
#ifdef _WIN32
#  ifdef _WIN64
#    define CARES_TYPEOF_ARES_SSIZE_T __int64
#  else
#    define CARES_TYPEOF_ARES_SSIZE_T long
#  endif
#else
#  define CARES_TYPEOF_ARES_SSIZE_T ssize_t
#endif

#endif /* __CARES_BUILD_H */
