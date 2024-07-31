// Copyright 2005, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Low-level types and utilities for porting Google Test to various
// platforms.  All macros ending with _ and symbols defined in an
// internal namespace are subject to change without notice.  Code
// outside Google Test MUST NOT USE THEM DIRECTLY.  Macros that don't
// end with _ are part of Google Test's public API and can be used by
// code outside Google Test.
//
// This file is fundamental to Google Test.  All other Google Test source
// files are expected to #include this.  Therefore, it cannot #include
// any other Google Test header.

// IWYU pragma: private, include "gtest/gtest.h"
// IWYU pragma: friend gtest/.*
// IWYU pragma: friend gmock/.*

#ifndef GOOGLETEST_INCLUDE_GTEST_INTERNAL_GTEST_PORT_H_
#define GOOGLETEST_INCLUDE_GTEST_INTERNAL_GTEST_PORT_H_

// Environment-describing macros
// -----------------------------
//
// Google Test can be used in many different environments.  Macros in
// this section tell Google Test what kind of environment it is being
// used in, such that Google Test can provide environment-specific
// features and implementations.
//
// Google Test tries to automatically detect the properties of its
// environment, so users usually don't need to worry about these
// macros.  However, the automatic detection is not perfect.
// Sometimes it's necessary for a user to define some of the following
// macros in the build script to override Google Test's decisions.
//
// If the user doesn't define a macro in the list, Google Test will
// provide a default definition.  After this header is #included, all
// macros in this list will be defined to either 1 or 0.
//
// Notes to maintainers:
//   - Each macro here is a user-tweakable knob; do not grow the list
//     lightly.
//   - Use #if to key off these macros.  Don't use #ifdef or "#if
//     defined(...)", which will not work as these macros are ALWAYS
//     defined.
//
//   GTEST_HAS_CLONE          - Define it to 1/0 to indicate that clone(2)
//                              is/isn't available.
//   GTEST_HAS_EXCEPTIONS     - Define it to 1/0 to indicate that exceptions
//                              are enabled.
//   GTEST_HAS_POSIX_RE       - Define it to 1/0 to indicate that POSIX regular
//                              expressions are/aren't available.
//   GTEST_HAS_PTHREAD        - Define it to 1/0 to indicate that <pthread.h>
//                              is/isn't available.
//   GTEST_HAS_RTTI           - Define it to 1/0 to indicate that RTTI is/isn't
//                              enabled.
//   GTEST_HAS_STD_WSTRING    - Define it to 1/0 to indicate that
//                              std::wstring does/doesn't work (Google Test can
//                              be used where std::wstring is unavailable).
//   GTEST_HAS_FILE_SYSTEM    - Define it to 1/0 to indicate whether or not a
//                              file system is/isn't available.
//   GTEST_HAS_SEH            - Define it to 1/0 to indicate whether the
//                              compiler supports Microsoft's "Structured
//                              Exception Handling".
//   GTEST_HAS_STREAM_REDIRECTION
//                            - Define it to 1/0 to indicate whether the
//                              platform supports I/O stream redirection using
//                              dup() and dup2().
//   GTEST_LINKED_AS_SHARED_LIBRARY
//                            - Define to 1 when compiling tests that use
//                              Google Test as a shared library (known as
//                              DLL on Windows).
//   GTEST_CREATE_SHARED_LIBRARY
//                            - Define to 1 when compiling Google Test itself
//                              as a shared library.
//   GTEST_DEFAULT_DEATH_TEST_STYLE
//                            - The default value of --gtest_death_test_style.
//                              The legacy default has been "fast" in the open
//                              source version since 2008. The recommended value
//                              is "threadsafe", and can be set in
//                              custom/gtest-port.h.

// Platform-indicating macros
// --------------------------
//
// Macros indicating the platform on which Google Test is being used
// (a macro is defined to 1 if compiled on the given platform;
// otherwise UNDEFINED -- it's never defined to 0.).  Google Test
// defines these macros automatically.  Code outside Google Test MUST
// NOT define them.
//
//   GTEST_OS_AIX      - IBM AIX
//   GTEST_OS_CYGWIN   - Cygwin
//   GTEST_OS_DRAGONFLY - DragonFlyBSD
//   GTEST_OS_FREEBSD  - FreeBSD
//   GTEST_OS_FUCHSIA  - Fuchsia
//   GTEST_OS_GNU_HURD - GNU/Hurd
//   GTEST_OS_GNU_KFREEBSD - GNU/kFreeBSD
//   GTEST_OS_HAIKU    - Haiku
//   GTEST_OS_HPUX     - HP-UX
//   GTEST_OS_LINUX    - Linux
//     GTEST_OS_LINUX_ANDROID - Google Android
//   GTEST_OS_MAC      - Mac OS X
//     GTEST_OS_IOS    - iOS
//   GTEST_OS_NACL     - Google Native Client (NaCl)
//   GTEST_OS_NETBSD   - NetBSD
//   GTEST_OS_OPENBSD  - OpenBSD
//   GTEST_OS_OS2      - OS/2
//   GTEST_OS_QNX      - QNX
//   GTEST_OS_SOLARIS  - Sun Solaris
//   GTEST_OS_WINDOWS  - Windows (Desktop, MinGW, or Mobile)
//     GTEST_OS_WINDOWS_DESKTOP  - Windows Desktop
//     GTEST_OS_WINDOWS_MINGW    - MinGW
//     GTEST_OS_WINDOWS_MOBILE   - Windows Mobile
//     GTEST_OS_WINDOWS_PHONE    - Windows Phone
//     GTEST_OS_WINDOWS_RT       - Windows Store App/WinRT
//   GTEST_OS_ZOS      - z/OS
//
// Among the platforms, Cygwin, Linux, Mac OS X, and Windows have the
// most stable support.  Since core members of the Google Test project
// don't have access to other platforms, support for them may be less
// stable.  If you notice any problems on your platform, please notify
// googletestframework@googlegroups.com (patches for fixing them are
// even more welcome!).
//
// It is possible that none of the GTEST_OS_* macros are defined.

// Feature-indicating macros
// -------------------------
//
// Macros indicating which Google Test features are available (a macro
// is defined to 1 if the corresponding feature is supported;
// otherwise UNDEFINED -- it's never defined to 0.).  Google Test
// defines these macros automatically.  Code outside Google Test MUST
// NOT define them.
//
// These macros are public so that portable tests can be written.
// Such tests typically surround code using a feature with an #ifdef
// which controls that code.  For example:
//
// #ifdef GTEST_HAS_DEATH_TEST
//   EXPECT_DEATH(DoSomethingDeadly());
// #endif
//
//   GTEST_HAS_DEATH_TEST   - death tests
//   GTEST_HAS_TYPED_TEST   - typed tests
//   GTEST_HAS_TYPED_TEST_P - type-parameterized tests
//   GTEST_IS_THREADSAFE    - Google Test is thread-safe.
//   GTEST_USES_RE2         - the RE2 regular expression library is used
//   GTEST_USES_POSIX_RE    - enhanced POSIX regex is used. Do not confuse with
//                            GTEST_HAS_POSIX_RE (see above) which users can
//                            define themselves.
//   GTEST_USES_SIMPLE_RE   - our own simple regex is used;
//                            the above RE\b(s) are mutually exclusive.
//   GTEST_HAS_ABSL         - Google Test is compiled with Abseil.

// Misc public macros
// ------------------
//
//   GTEST_FLAG(flag_name)  - references the variable corresponding to
//                            the given Google Test flag.

// Internal utilities
// ------------------
//
// The following macros and utilities are for Google Test's INTERNAL
// use only.  Code outside Google Test MUST NOT USE THEM DIRECTLY.
//
// Macros for basic C++ coding:
//   GTEST_AMBIGUOUS_ELSE_BLOCKER_ - for disabling a gcc warning.
//   GTEST_MUST_USE_RESULT_   - declares that a function's result must be used.
//   GTEST_INTENTIONAL_CONST_COND_PUSH_ - start code section where MSVC C4127 is
//                                        suppressed (constant conditional).
//   GTEST_INTENTIONAL_CONST_COND_POP_  - finish code section where MSVC C4127
//                                        is suppressed.
//   GTEST_INTERNAL_HAS_ANY - for enabling UniversalPrinter<std::any> or
//                            UniversalPrinter<absl::any> specializations.
//                            Always defined to 0 or 1.
//   GTEST_INTERNAL_HAS_OPTIONAL - for enabling UniversalPrinter<std::optional>
//   or
//                                 UniversalPrinter<absl::optional>
//                                 specializations. Always defined to 0 or 1.
//   GTEST_INTERNAL_HAS_STD_SPAN - for enabling UniversalPrinter<std::span>
//                                 specializations. Always defined to 0 or 1
//   GTEST_INTERNAL_HAS_STRING_VIEW - for enabling Matcher<std::string_view> or
//                                    Matcher<absl::string_view>
//                                    specializations. Always defined to 0 or 1.
//   GTEST_INTERNAL_HAS_VARIANT - for enabling UniversalPrinter<std::variant> or
//                                UniversalPrinter<absl::variant>
//                                specializations. Always defined to 0 or 1.
//   GTEST_USE_OWN_FLAGFILE_FLAG_ - Always defined to 0 or 1.
//   GTEST_HAS_CXXABI_H_ - Always defined to 0 or 1.
//   GTEST_CAN_STREAM_RESULTS_ - Always defined to 0 or 1.
//   GTEST_HAS_ALT_PATH_SEP_ - Always defined to 0 or 1.
//   GTEST_WIDE_STRING_USES_UTF16_ - Always defined to 0 or 1.
//   GTEST_HAS_MUTEX_AND_THREAD_LOCAL_ - Always defined to 0 or 1.
//   GTEST_HAS_NOTIFICATION_- Always defined to 0 or 1.
//
// Synchronization:
//   Mutex, MutexLock, ThreadLocal, GetThreadCount()
//                            - synchronization primitives.
//
// Regular expressions:
//   RE             - a simple regular expression class using
//                     1) the RE2 syntax on all platforms when built with RE2
//                        and Abseil as dependencies
//                     2) the POSIX Extended Regular Expression syntax on
//                        UNIX-like platforms,
//                     3) A reduced regular exception syntax on other platforms,
//                        including Windows.
// Logging:
//   GTEST_LOG_()   - logs messages at the specified severity level.
//   LogToStderr()  - directs all log messages to stderr.
//   FlushInfoLog() - flushes informational log messages.
//
// Stdout and stderr capturing:
//   CaptureStdout()     - starts capturing stdout.
//   GetCapturedStdout() - stops capturing stdout and returns the captured
//                         string.
//   CaptureStderr()     - starts capturing stderr.
//   GetCapturedStderr() - stops capturing stderr and returns the captured
//                         string.
//
// Integer types:
//   TypeWithSize   - maps an integer to a int type.
//   TimeInMillis   - integers of known sizes.
//   BiggestInt     - the biggest signed integer type.
//
// Command-line utilities:
//   GetInjectableArgvs() - returns the command line as a vector of strings.
//
// Environment variable utilities:
//   GetEnv()             - gets the value of an environment variable.
//   BoolFromGTestEnv()   - parses a bool environment variable.
//   Int32FromGTestEnv()  - parses an int32_t environment variable.
//   StringFromGTestEnv() - parses a string environment variable.
//
// Deprecation warnings:
//   GTEST_INTERNAL_DEPRECATED(message) - attribute marking a function as
//                                        deprecated; calling a marked function
//                                        should generate a compiler warning

// The definition of GTEST_INTERNAL_CPLUSPLUS_LANG comes first because it can
// potentially be used as an #include guard.
#if defined(_MSVC_LANG)
#define GTEST_INTERNAL_CPLUSPLUS_LANG _MSVC_LANG
#elif defined(__cplusplus)
#define GTEST_INTERNAL_CPLUSPLUS_LANG __cplusplus
#endif

#if !defined(GTEST_INTERNAL_CPLUSPLUS_LANG) || \
    GTEST_INTERNAL_CPLUSPLUS_LANG < 201402L
#error C++ versions less than C++14 are not supported.
#endif

// MSVC >= 19.11 (VS 2017 Update 3) supports __has_include.
#ifdef __has_include
#define GTEST_INTERNAL_HAS_INCLUDE __has_include
#else
#define GTEST_INTERNAL_HAS_INCLUDE(...) 0
#endif

// Detect C++ feature test macros as gracefully as possible.
// MSVC >= 19.15, Clang >= 3.4.1, and GCC >= 4.1.2 support feature test macros.
#if GTEST_INTERNAL_CPLUSPLUS_LANG >= 202002L && \
    (!defined(__has_include) || GTEST_INTERNAL_HAS_INCLUDE(<version>))
#include <version>  // C++20 and later
#elif (!defined(__has_include) || GTEST_INTERNAL_HAS_INCLUDE(<ciso646>))
#include <ciso646>  // Pre-C++20
#endif

#include <ctype.h>   // for isspace, etc
#include <stddef.h>  // for ptrdiff_t
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerrno>
// #include <condition_variable>  // Guarded by GTEST_IS_THREADSAFE below
#include <cstdint>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <ostream>
#include <string>
// #include <mutex>  // Guarded by GTEST_IS_THREADSAFE below
#include <tuple>
#include <type_traits>
#include <vector>

#ifndef _WIN32_WCE
#include <sys/stat.h>
#include <sys/types.h>
#endif  // !_WIN32_WCE

#if defined __APPLE__
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>
#endif

#include "gtest/internal/custom/gtest-port.h"
#include "gtest/internal/gtest-port-arch.h"

#ifndef GTEST_HAS_MUTEX_AND_THREAD_LOCAL_
#define GTEST_HAS_MUTEX_AND_THREAD_LOCAL_ 0
#endif

#ifndef GTEST_HAS_NOTIFICATION_
#define GTEST_HAS_NOTIFICATION_ 0
#endif

#if defined(GTEST_HAS_ABSL) && !defined(GTEST_NO_ABSL_FLAGS)
#define GTEST_INTERNAL_HAS_ABSL_FLAGS  // Used only in this file.
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#endif

#if !defined(GTEST_DEV_EMAIL_)
#define GTEST_DEV_EMAIL_ "googletestframework@@googlegroups.com"
#define GTEST_FLAG_PREFIX_ "gtest_"
#define GTEST_FLAG_PREFIX_DASH_ "gtest-"
#define GTEST_FLAG_PREFIX_UPPER_ "GTEST_"
#define GTEST_NAME_ "Google Test"
#define GTEST_PROJECT_URL_ "https://github.com/google/googletest/"
#endif  // !defined(GTEST_DEV_EMAIL_)

#if !defined(GTEST_INIT_GOOGLE_TEST_NAME_)
#define GTEST_INIT_GOOGLE_TEST_NAME_ "testing::InitGoogleTest"
#endif  // !defined(GTEST_INIT_GOOGLE_TEST_NAME_)

// Determines the version of gcc that is used to compile this.
#ifdef __GNUC__
// 40302 means version 4.3.2.
#define GTEST_GCC_VER_ \
  (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#endif  // __GNUC__

// Macros for disabling Microsoft Visual C++ warnings.
//
//   GTEST_DISABLE_MSC_WARNINGS_PUSH_(4800 4385)
//   /* code that triggers warnings C4800 and C4385 */
//   GTEST_DISABLE_MSC_WARNINGS_POP_()
#if defined(_MSC_VER)
#define GTEST_DISABLE_MSC_WARNINGS_PUSH_(warnings) \
  __pragma(warning(push)) __pragma(warning(disable : warnings))
#define GTEST_DISABLE_MSC_WARNINGS_POP_() __pragma(warning(pop))
#else
// Not all compilers are MSVC
#define GTEST_DISABLE_MSC_WARNINGS_PUSH_(warnings)
#define GTEST_DISABLE_MSC_WARNINGS_POP_()
#endif

// Clang on Windows does not understand MSVC's pragma warning.
// We need clang-specific way to disable function deprecation warning.
#ifdef __clang__
#define GTEST_DISABLE_MSC_DEPRECATED_PUSH_()                            \
  _Pragma("clang diagnostic push")                                      \
      _Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"") \
          _Pragma("clang diagnostic ignored \"-Wdeprecated-implementations\"")
#define GTEST_DISABLE_MSC_DEPRECATED_POP_() _Pragma("clang diagnostic pop")
#else
#define GTEST_DISABLE_MSC_DEPRECATED_PUSH_() \
  GTEST_DISABLE_MSC_WARNINGS_PUSH_(4996)
#define GTEST_DISABLE_MSC_DEPRECATED_POP_() GTEST_DISABLE_MSC_WARNINGS_POP_()
#endif

// Brings in definitions for functions used in the testing::internal::posix
// namespace (read, write, close, chdir, isatty, stat). We do not currently
// use them on Windows Mobile.
#ifdef GTEST_OS_WINDOWS
#ifndef GTEST_OS_WINDOWS_MOBILE
#include <direct.h>
#include <io.h>
#endif
// In order to avoid having to include <windows.h>, use forward declaration
#if defined(GTEST_OS_WINDOWS_MINGW) && !defined(__MINGW64_VERSION_MAJOR)
// MinGW defined _CRITICAL_SECTION and _RTL_CRITICAL_SECTION as two
// separate (equivalent) structs, instead of using typedef
typedef struct _CRITICAL_SECTION GTEST_CRITICAL_SECTION;
#else
// Assume CRITICAL_SECTION is a typedef of _RTL_CRITICAL_SECTION.
// This assumption is verified by
// WindowsTypesTest.CRITICAL_SECTIONIs_RTL_CRITICAL_SECTION.
typedef struct _RTL_CRITICAL_SECTION GTEST_CRITICAL_SECTION;
#endif
#elif defined(GTEST_OS_XTENSA)
#include <unistd.h>
// Xtensa toolchains define strcasecmp in the string.h header instead of
// strings.h. string.h is already included.
#else
// This assumes that non-Windows OSes provide unistd.h. For OSes where this
// is not the case, we need to include headers that provide the functions
// mentioned above.
#include <strings.h>
#include <unistd.h>
#endif  // GTEST_OS_WINDOWS

#ifdef GTEST_OS_LINUX_ANDROID
// Used to define __ANDROID_API__ matching the target NDK API level.
#include <android/api-level.h>  // NOLINT
#endif

// Defines this to true if and only if Google Test can use POSIX regular
// expressions.
#ifndef GTEST_HAS_POSIX_RE
#ifdef GTEST_OS_LINUX_ANDROID
// On Android, <regex.h> is only available starting with Gingerbread.
#define GTEST_HAS_POSIX_RE (__ANDROID_API__ >= 9)
#else
#if !(defined(GTEST_OS_WINDOWS) || defined(GTEST_OS_XTENSA) || \
      defined(GTEST_OS_QURT))
#define GTEST_HAS_POSIX_RE 1
#else
#define GTEST_HAS_POSIX_RE 0
#endif
#endif  // GTEST_OS_LINUX_ANDROID
#endif

// Select the regular expression implementation.
#ifdef GTEST_HAS_ABSL
// When using Abseil, RE2 is required.
#include "absl/strings/string_view.h"
#include "re2/re2.h"
#define GTEST_USES_RE2 1
#elif GTEST_HAS_POSIX_RE
#include <regex.h>  // NOLINT
#define GTEST_USES_POSIX_RE 1
#else
// Use our own simple regex implementation.
#define GTEST_USES_SIMPLE_RE 1
#endif

#ifndef GTEST_HAS_EXCEPTIONS
// The user didn't tell us whether exceptions are enabled, so we need
// to figure it out.
#if defined(_MSC_VER) && defined(_CPPUNWIND)
// MSVC defines _CPPUNWIND to 1 if and only if exceptions are enabled.
#define GTEST_HAS_EXCEPTIONS 1
#elif defined(__BORLANDC__)
// C++Builder's implementation of the STL uses the _HAS_EXCEPTIONS
// macro to enable exceptions, so we'll do the same.
// Assumes that exceptions are enabled by default.
#ifndef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 1
#endif  // _HAS_EXCEPTIONS
#define GTEST_HAS_EXCEPTIONS _HAS_EXCEPTIONS
#elif defined(__clang__)
// clang defines __EXCEPTIONS if and only if exceptions are enabled before clang
// 220714, but if and only if cleanups are enabled after that. In Obj-C++ files,
// there can be cleanups for ObjC exceptions which also need cleanups, even if
// C++ exceptions are disabled. clang has __has_feature(cxx_exceptions) which
// checks for C++ exceptions starting at clang r206352, but which checked for
// cleanups prior to that. To reliably check for C++ exception availability with
// clang, check for
// __EXCEPTIONS && __has_feature(cxx_exceptions).
#if defined(__EXCEPTIONS) && __EXCEPTIONS && __has_feature(cxx_exceptions)
#define GTEST_HAS_EXCEPTIONS 1
#else
#define GTEST_HAS_EXCEPTIONS 0
#endif
#elif defined(__GNUC__) && defined(__EXCEPTIONS) && __EXCEPTIONS
// gcc defines __EXCEPTIONS to 1 if and only if exceptions are enabled.
#define GTEST_HAS_EXCEPTIONS 1
#elif defined(__SUNPRO_CC)
// Sun Pro CC supports exceptions.  However, there is no compile-time way of
// detecting whether they are enabled or not.  Therefore, we assume that
// they are enabled unless the user tells us otherwise.
#define GTEST_HAS_EXCEPTIONS 1
#elif defined(__IBMCPP__) && defined(__EXCEPTIONS) && __EXCEPTIONS
// xlC defines __EXCEPTIONS to 1 if and only if exceptions are enabled.
#define GTEST_HAS_EXCEPTIONS 1
#elif defined(__HP_aCC)
// Exception handling is in effect by default in HP aCC compiler. It has to
// be turned of by +noeh compiler option if desired.
#define GTEST_HAS_EXCEPTIONS 1
#else
// For other compilers, we assume exceptions are disabled to be
// conservative.
#define GTEST_HAS_EXCEPTIONS 0
#endif  // defined(_MSC_VER) || defined(__BORLANDC__)
#endif  // GTEST_HAS_EXCEPTIONS

#ifndef GTEST_HAS_STD_WSTRING
// The user didn't tell us whether ::std::wstring is available, so we need
// to figure it out.
// Cygwin 1.7 and below doesn't support ::std::wstring.
// Solaris' libc++ doesn't support it either.  Android has
// no support for it at least as recent as Froyo (2.2).
#if (!(defined(GTEST_OS_LINUX_ANDROID) || defined(GTEST_OS_CYGWIN) || \
       defined(GTEST_OS_SOLARIS) || defined(GTEST_OS_HAIKU) ||        \
       defined(GTEST_OS_ESP32) || defined(GTEST_OS_ESP8266) ||        \
       defined(GTEST_OS_XTENSA) || defined(GTEST_OS_QURT) ||          \
       defined(GTEST_OS_NXP_QN9090) || defined(GTEST_OS_NRF52)))
#define GTEST_HAS_STD_WSTRING 1
#else
#define GTEST_HAS_STD_WSTRING 0
#endif
#endif  // GTEST_HAS_STD_WSTRING

#ifndef GTEST_HAS_FILE_SYSTEM
// Most platforms support a file system.
#define GTEST_HAS_FILE_SYSTEM 1
#endif  // GTEST_HAS_FILE_SYSTEM

// Determines whether RTTI is available.
#ifndef GTEST_HAS_RTTI
// The user didn't tell us whether RTTI is enabled, so we need to
// figure it out.

#ifdef _MSC_VER

#ifdef _CPPRTTI  // MSVC defines this macro if and only if RTTI is enabled.
#define GTEST_HAS_RTTI 1
#else
#define GTEST_HAS_RTTI 0
#endif

// Starting with version 4.3.2, gcc defines __GXX_RTTI if and only if RTTI is
// enabled.
#elif defined(__GNUC__)

#ifdef __GXX_RTTI
// When building against STLport with the Android NDK and with
// -frtti -fno-exceptions, the build fails at link time with undefined
// references to __cxa_bad_typeid. Note sure if STL or toolchain bug,
// so disable RTTI when detected.
#if defined(GTEST_OS_LINUX_ANDROID) && defined(_STLPORT_MAJOR) && \
    !defined(__EXCEPTIONS)
#define GTEST_HAS_RTTI 0
#else
#define GTEST_HAS_RTTI 1
#endif  // GTEST_OS_LINUX_ANDROID && __STLPORT_MAJOR && !__EXCEPTIONS
#else
#define GTEST_HAS_RTTI 0
#endif  // __GXX_RTTI

// Clang defines __GXX_RTTI starting with version 3.0, but its manual recommends
// using has_feature instead. has_feature(cxx_rtti) is supported since 2.7, the
// first version with C++ support.
#elif defined(__clang__)

#define GTEST_HAS_RTTI __has_feature(cxx_rtti)

// Starting with version 9.0 IBM Visual Age defines __RTTI_ALL__ to 1 if
// both the typeid and dynamic_cast features are present.
#elif defined(__IBMCPP__) && (__IBMCPP__ >= 900)

#ifdef __RTTI_ALL__
#define GTEST_HAS_RTTI 1
#else
#define GTEST_HAS_RTTI 0
#endif

#else

// For all other compilers, we assume RTTI is enabled.
#define GTEST_HAS_RTTI 1

#endif  // _MSC_VER

#endif  // GTEST_HAS_RTTI

// It's this header's responsibility to #include <typeinfo> when RTTI
// is enabled.
#if GTEST_HAS_RTTI
#include <typeinfo>
#endif

// Determines whether Google Test can use the pthreads library.
#ifndef GTEST_HAS_PTHREAD
// The user didn't tell us explicitly, so we make reasonable assumptions about
// which platforms have pthreads support.
//
// To disable threading support in Google Test, add -DGTEST_HAS_PTHREAD=0
// to your compiler flags.
#if (defined(GTEST_OS_LINUX) || defined(GTEST_OS_MAC) ||              \
     defined(GTEST_OS_HPUX) || defined(GTEST_OS_QNX) ||               \
     defined(GTEST_OS_FREEBSD) || defined(GTEST_OS_NACL) ||           \
     defined(GTEST_OS_NETBSD) || defined(GTEST_OS_FUCHSIA) ||         \
     defined(GTEST_OS_DRAGONFLY) || defined(GTEST_OS_GNU_KFREEBSD) || \
     defined(GTEST_OS_OPENBSD) || defined(GTEST_OS_HAIKU) ||          \
     defined(GTEST_OS_GNU_HURD) || defined(GTEST_OS_SOLARIS) ||       \
     defined(GTEST_OS_AIX) || defined(GTEST_OS_ZOS))
#define GTEST_HAS_PTHREAD 1
#else
#define GTEST_HAS_PTHREAD 0
#endif
#endif  // GTEST_HAS_PTHREAD

#if GTEST_HAS_PTHREAD
// gtest-port.h guarantees to #include <pthread.h> when GTEST_HAS_PTHREAD is
// true.
#include <pthread.h>  // NOLINT

// For timespec and nanosleep, used below.
#include <time.h>  // NOLINT
#endif

// Determines whether clone(2) is supported.
// Usually it will only be available on Linux, excluding
// Linux on the Itanium architecture.
// Also see https://linux.die.net/man/2/clone.
#ifndef GTEST_HAS_CLONE
// The user didn't tell us, so we need to figure it out.

#if defined(GTEST_OS_LINUX) && !defined(__ia64__)
#if defined(GTEST_OS_LINUX_ANDROID)
// On Android, clone() became available at different API levels for each 32-bit
// architecture.
#if defined(__LP64__) || (defined(__arm__) && __ANDROID_API__ >= 9) || \
    (defined(__mips__) && __ANDROID_API__ >= 12) ||                    \
    (defined(__i386__) && __ANDROID_API__ >= 17)
#define GTEST_HAS_CLONE 1
#else
#define GTEST_HAS_CLONE 0
#endif
#else
#define GTEST_HAS_CLONE 1
#endif
#else
#define GTEST_HAS_CLONE 0
#endif  // GTEST_OS_LINUX && !defined(__ia64__)

#endif  // GTEST_HAS_CLONE

// Determines whether to support stream redirection. This is used to test
// output correctness and to implement death tests.
#ifndef GTEST_HAS_STREAM_REDIRECTION
// By default, we assume that stream redirection is supported on all
// platforms except known mobile / embedded ones. Also, if the port doesn't have
// a file system, stream redirection is not supported.
#if defined(GTEST_OS_WINDOWS_MOBILE) || defined(GTEST_OS_WINDOWS_PHONE) || \
    defined(GTEST_OS_WINDOWS_RT) || defined(GTEST_OS_WINDOWS_GAMES) ||     \
    defined(GTEST_OS_ESP8266) || defined(GTEST_OS_XTENSA) ||               \
    defined(GTEST_OS_QURT) || !GTEST_HAS_FILE_SYSTEM
#define GTEST_HAS_STREAM_REDIRECTION 0
#else
#define GTEST_HAS_STREAM_REDIRECTION 1
#endif  // !GTEST_OS_WINDOWS_MOBILE
#endif  // GTEST_HAS_STREAM_REDIRECTION

// Determines whether to support death tests.
// pops up a dialog window that cannot be suppressed programmatically.
#if (defined(GTEST_OS_LINUX) || defined(GTEST_OS_CYGWIN) ||           \
     defined(GTEST_OS_SOLARIS) || defined(GTEST_OS_ZOS) ||            \
     (defined(GTEST_OS_MAC) && !defined(GTEST_OS_IOS)) ||             \
     (defined(GTEST_OS_WINDOWS_DESKTOP) && _MSC_VER) ||               \
     defined(GTEST_OS_WINDOWS_MINGW) || defined(GTEST_OS_AIX) ||      \
     defined(GTEST_OS_HPUX) || defined(GTEST_OS_OPENBSD) ||           \
     defined(GTEST_OS_QNX) || defined(GTEST_OS_FREEBSD) ||            \
     defined(GTEST_OS_NETBSD) || defined(GTEST_OS_FUCHSIA) ||         \
     defined(GTEST_OS_DRAGONFLY) || defined(GTEST_OS_GNU_KFREEBSD) || \
     defined(GTEST_OS_HAIKU) || defined(GTEST_OS_GNU_HURD))
// Death tests require a file system to work properly.
#if GTEST_HAS_FILE_SYSTEM
#define GTEST_HAS_DEATH_TEST 1
#endif  // GTEST_HAS_FILE_SYSTEM
#endif

// Determines whether to support type-driven tests.

// Typed tests need <typeinfo> and variadic macros, which GCC, VC++ 8.0,
// Sun Pro CC, IBM Visual Age, and HP aCC support.
#if defined(__GNUC__) || defined(_MSC_VER) || defined(__SUNPRO_CC) || \
    defined(__IBMCPP__) || defined(__HP_aCC)
#define GTEST_HAS_TYPED_TEST 1
#define GTEST_HAS_TYPED_TEST_P 1
#endif

// Determines whether the system compiler uses UTF-16 for encoding wide strings.
#if defined(GTEST_OS_WINDOWS) || defined(GTEST_OS_CYGWIN) || \
    defined(GTEST_OS_AIX) || defined(GTEST_OS_OS2)
#define GTEST_WIDE_STRING_USES_UTF16_ 1
#else
#define GTEST_WIDE_STRING_USES_UTF16_ 0
#endif

// Determines whether test results can be streamed to a socket.
#if defined(GTEST_OS_LINUX) || defined(GTEST_OS_GNU_KFREEBSD) || \
    defined(GTEST_OS_DRAGONFLY) || defined(GTEST_OS_FREEBSD) ||  \
    defined(GTEST_OS_NETBSD) || defined(GTEST_OS_OPENBSD) ||     \
    defined(GTEST_OS_GNU_HURD) || defined(GTEST_OS_MAC)
#define GTEST_CAN_STREAM_RESULTS_ 1
#else
#define GTEST_CAN_STREAM_RESULTS_ 0
#endif

// Defines some utility macros.

// The GNU compiler emits a warning if nested "if" statements are followed by
// an "else" statement and braces are not used to explicitly disambiguate the
// "else" binding.  This leads to problems with code like:
//
//   if (gate)
//     ASSERT_*(condition) << "Some message";
//
// The "switch (0) case 0:" idiom is used to suppress this.
#ifdef __INTEL_COMPILER
#define GTEST_AMBIGUOUS_ELSE_BLOCKER_
#else
#define GTEST_AMBIGUOUS_ELSE_BLOCKER_ \
  switch (0)                          \
  case 0:                             \
  default:  // NOLINT
#endif

// GTEST_HAVE_ATTRIBUTE_
//
// A function-like feature checking macro that is a wrapper around
// `__has_attribute`, which is defined by GCC 5+ and Clang and evaluates to a
// nonzero constant integer if the attribute is supported or 0 if not.
//
// It evaluates to zero if `__has_attribute` is not defined by the compiler.
//
// GCC: https://gcc.gnu.org/gcc-5/changes.html
// Clang: https://clang.llvm.org/docs/LanguageExtensions.html
#ifdef __has_attribute
#define GTEST_HAVE_ATTRIBUTE_(x) __has_attribute(x)
#else
#define GTEST_HAVE_ATTRIBUTE_(x) 0
#endif

// GTEST_INTERNAL_HAVE_CPP_ATTRIBUTE
//
// A function-like feature checking macro that accepts C++11 style attributes.
// It's a wrapper around `__has_cpp_attribute`, defined by ISO C++ SD-6
// (https://en.cppreference.com/w/cpp/experimental/feature_test). If we don't
// find `__has_cpp_attribute`, will evaluate to 0.
#if defined(__has_cpp_attribute)
// NOTE: requiring __cplusplus above should not be necessary, but
// works around https://bugs.llvm.org/show_bug.cgi?id=23435.
#define GTEST_INTERNAL_HAVE_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define GTEST_INTERNAL_HAVE_CPP_ATTRIBUTE(x) 0
#endif

// GTEST_HAVE_FEATURE_
//
// A function-like feature checking macro that is a wrapper around
// `__has_feature`.
#ifdef __has_feature
#define GTEST_HAVE_FEATURE_(x) __has_feature(x)
#else
#define GTEST_HAVE_FEATURE_(x) 0
#endif

// Use this annotation after a variable or parameter declaration to tell the
// compiler the variable/parameter may be used.
// Example:
//
//   GTEST_INTERNAL_ATTRIBUTE_MAYBE_UNUSED int foo = bar();
//
// This can be removed once we only support only C++17 or newer and
// [[maybe_unused]] is available on all supported platforms.
#if GTEST_INTERNAL_HAVE_CPP_ATTRIBUTE(maybe_unused)
#define GTEST_INTERNAL_ATTRIBUTE_MAYBE_UNUSED [[maybe_unused]]
#elif GTEST_HAVE_ATTRIBUTE_(unused)
// This is inferior to [[maybe_unused]] as it can produce a
// -Wused-but-marked-unused warning on optionally used symbols, but it is all we
// have.
#define GTEST_INTERNAL_ATTRIBUTE_MAYBE_UNUSED __attribute__((__unused__))
#else
#define GTEST_INTERNAL_ATTRIBUTE_MAYBE_UNUSED
#endif

// Use this annotation before a function that takes a printf format string.
#if GTEST_HAVE_ATTRIBUTE_(format) && defined(__MINGW_PRINTF_FORMAT)
// MinGW has two different printf implementations. Ensure the format macro
// matches the selected implementation. See
// https://sourceforge.net/p/mingw-w64/wiki2/gnu%20printf/.
#define GTEST_ATTRIBUTE_PRINTF_(string_index, first_to_check) \
  __attribute__((format(__MINGW_PRINTF_FORMAT, string_index, first_to_check)))
#elif GTEST_HAVE_ATTRIBUTE_(format)
#define GTEST_ATTRIBUTE_PRINTF_(string_index, first_to_check) \
  __attribute__((format(printf, string_index, first_to_check)))
#else
#define GTEST_ATTRIBUTE_PRINTF_(string_index, first_to_check)
#endif

// Tell the compiler to warn about unused return values for functions declared
// with this macro.  The macro should be used on function declarations
// following the argument list:
//
//   Sprocket* AllocateSprocket() GTEST_MUST_USE_RESULT_;
#if GTEST_HAVE_ATTRIBUTE_(warn_unused_result)
#define GTEST_MUST_USE_RESULT_ __attribute__((warn_unused_result))
#else
#define GTEST_MUST_USE_RESULT_
#endif

// MS C++ compiler emits warning when a conditional expression is compile time
// constant. In some contexts this warning is false positive and needs to be
// suppressed. Use the following two macros in such cases:
//
// GTEST_INTENTIONAL_CONST_COND_PUSH_()
// while (true) {
// GTEST_INTENTIONAL_CONST_COND_POP_()
// }
#define GTEST_INTENTIONAL_CONST_COND_PUSH_() \
  GTEST_DISABLE_MSC_WARNINGS_PUSH_(4127)
#define GTEST_INTENTIONAL_CONST_COND_POP_() GTEST_DISABLE_MSC_WARNINGS_POP_()

// Determine whether the compiler supports Microsoft's Structured Exception
// Handling.  This is supported by several Windows compilers but generally
// does not exist on any other system.
#ifndef GTEST_HAS_SEH
// The user didn't tell us, so we need to figure it out.

#if defined(_MSC_VER) || defined(__BORLANDC__)
// These two compilers are known to support SEH.
#define GTEST_HAS_SEH 1
#else
// Assume no SEH.
#define GTEST_HAS_SEH 0
#endif

#endif  // GTEST_HAS_SEH

#ifndef GTEST_IS_THREADSAFE

#if (GTEST_HAS_MUTEX_AND_THREAD_LOCAL_ ||                              \
     (defined(GTEST_OS_WINDOWS) && !defined(GTEST_OS_WINDOWS_PHONE) && \
      !defined(GTEST_OS_WINDOWS_RT)) ||                                \
     GTEST_HAS_PTHREAD)
#define GTEST_IS_THREADSAFE 1
#endif

#endif  // GTEST_IS_THREADSAFE

#ifdef GTEST_IS_THREADSAFE
// Some platforms don't support including these threading related headers.
#include <condition_variable>  // NOLINT
#include <mutex>               // NOLINT
#endif                         // GTEST_IS_THREADSAFE

// GTEST_API_ qualifies all symbols that must be exported. The definitions below
// are guarded by #ifndef to give embedders a chance to define GTEST_API_ in
// gtest/internal/custom/gtest-port.h
#ifndef GTEST_API_

#ifdef _MSC_VER
#if defined(GTEST_LINKED_AS_SHARED_LIBRARY) && GTEST_LINKED_AS_SHARED_LIBRARY
#define GTEST_API_ __declspec(dllimport)
#elif defined(GTEST_CREATE_SHARED_LIBRARY) && GTEST_CREATE_SHARED_LIBRARY
#define GTEST_API_ __declspec(dllexport)
#endif
#elif GTEST_HAVE_ATTRIBUTE_(visibility)
#define GTEST_API_ __attribute__((visibility("default")))
#endif  // _MSC_VER

#endif  // GTEST_API_

#ifndef GTEST_API_
#define GTEST_API_
#endif  // GTEST_API_

#ifndef GTEST_DEFAULT_DEATH_TEST_STYLE
#define GTEST_DEFAULT_DEATH_TEST_STYLE "fast"
#endif  // GTEST_DEFAULT_DEATH_TEST_STYLE

#if GTEST_HAVE_ATTRIBUTE_(noinline)
// Ask the compiler to never inline a given function.
#define GTEST_NO_INLINE_ __attribute__((noinline))
#else
#define GTEST_NO_INLINE_
#endif

#if GTEST_HAVE_ATTRIBUTE_(disable_tail_calls)
// Ask the compiler not to perform tail call optimization inside
// the marked function.
#define GTEST_NO_TAIL_CALL_ __attribute__((disable_tail_calls))
#elif defined(__GNUC__) && !defined(__NVCOMPILER)
#define GTEST_NO_TAIL_CALL_ \
  __attribute__((optimize("no-optimize-sibling-calls")))
#else
#define GTEST_NO_TAIL_CALL_
#endif

// _LIBCPP_VERSION is defined by the libc++ library from the LLVM project.
#if !defined(GTEST_HAS_CXXABI_H_)
#if defined(__GLIBCXX__) || (defined(_LIBCPP_VERSION) && !defined(_MSC_VER))
#define GTEST_HAS_CXXABI_H_ 1
#else
#define GTEST_HAS_CXXABI_H_ 0
#endif
#endif

// A function level attribute to disable checking for use of uninitialized
// memory when built with MemorySanitizer.
#if GTEST_HAVE_ATTRIBUTE_(no_sanitize_memory)
#define GTEST_ATTRIBUTE_NO_SANITIZE_MEMORY_ __attribute__((no_sanitize_memory))
#else
#define GTEST_ATTRIBUTE_NO_SANITIZE_MEMORY_
#endif

// A function level attribute to disable AddressSanitizer instrumentation.
#if GTEST_HAVE_ATTRIBUTE_(no_sanitize_address)
#define GTEST_ATTRIBUTE_NO_SANITIZE_ADDRESS_ \
  __attribute__((no_sanitize_address))
#else
#define GTEST_ATTRIBUTE_NO_SANITIZE_ADDRESS_
#endif

// A function level attribute to disable HWAddressSanitizer instrumentation.
#if GTEST_HAVE_FEATURE_(hwaddress_sanitizer) && \
    GTEST_HAVE_ATTRIBUTE_(no_sanitize)
#define GTEST_ATTRIBUTE_NO_SANITIZE_HWADDRESS_ \
  __attribute__((no_sanitize("hwaddress")))
#else
#define GTEST_ATTRIBUTE_NO_SANITIZE_HWADDRESS_
#endif

// A function level attribute to disable ThreadSanitizer instrumentation.
#if GTEST_HAVE_ATTRIBUTE_(no_sanitize_thread)
#define GTEST_ATTRIBUTE_NO_SANITIZE_THREAD_ __attribute((no_sanitize_thread))
#else
#define GTEST_ATTRIBUTE_NO_SANITIZE_THREAD_
#endif

namespace testing {

class Message;

// Legacy imports for backwards compatibility.
// New code should use std:: names directly.
using std::get;
using std::make_tuple;
using std::tuple;
using std::tuple_element;
using std::tuple_size;

namespace internal {

// A secret type that Google Test users don't know about.  It has no
// accessible constructors on purpose.  Therefore it's impossible to create a
// Secret object, which is what we want.
class Secret {
  Secret(const Secret&) = delete;
};

// A helper for suppressing warnings on constant condition.  It just
// returns 'condition'.
GTEST_API_ bool IsTrue(bool condition);

// Defines RE.

#ifdef GTEST_USES_RE2

// This is almost `using RE = ::RE2`, except it is copy-constructible, and it
// needs to disambiguate the `std::string`, `absl::string_view`, and `const
// char*` constructors.
class GTEST_API_ RE {
 public:
  RE(absl::string_view regex) : regex_(regex) {}                  // NOLINT
  RE(const char* regex) : RE(absl::string_view(regex)) {}         // NOLINT
  RE(const std::string& regex) : RE(absl::string_view(regex)) {}  // NOLINT
  RE(const RE& other) : RE(other.pattern()) {}

  const std::string& pattern() const { return regex_.pattern(); }

  static bool FullMatch(absl::string_view str, const RE& re) {
    return RE2::FullMatch(str, re.regex_);
  }
  static bool PartialMatch(absl::string_view str, const RE& re) {
    return RE2::PartialMatch(str, re.regex_);
  }

 private:
  RE2 regex_;
};

#elif defined(GTEST_USES_POSIX_RE) || defined(GTEST_USES_SIMPLE_RE)
GTEST_DISABLE_MSC_WARNINGS_PUSH_(4251 \
/* class A needs to have dll-interface to be used by clients of class B */)

// A simple C++ wrapper for <regex.h>.  It uses the POSIX Extended
// Regular Expression syntax.
class GTEST_API_ RE {
 public:
  // A copy constructor is required by the Standard to initialize object
  // references from r-values.
  RE(const RE& other) { Init(other.pattern()); }

  // Constructs an RE from a string.
  RE(const ::std::string& regex) { Init(regex.c_str()); }  // NOLINT

  RE(const char* regex) { Init(regex); }  // NOLINT
  ~RE();

  // Returns the string representation of the regex.
  const char* pattern() const { return pattern_.c_str(); }

  // FullMatch(str, re) returns true if and only if regular expression re
  // matches the entire str.
  // PartialMatch(str, re) returns true if and only if regular expression re
  // matches a substring of str (including str itself).
  static bool FullMatch(const ::std::string& str, const RE& re) {
    return FullMatch(str.c_str(), re);
  }
  static bool PartialMatch(const ::std::string& str, const RE& re) {
    return PartialMatch(str.c_str(), re);
  }

  static bool FullMatch(const char* str, const RE& re);
  static bool PartialMatch(const char* str, const RE& re);

 private:
  void Init(const char* regex);
  std::string pattern_;
  bool is_valid_;

#ifdef GTEST_USES_POSIX_RE

  regex_t full_regex_;     // For FullMatch().
  regex_t partial_regex_;  // For PartialMatch().

#else  // GTEST_USES_SIMPLE_RE

  std::string full_pattern_;  // For FullMatch();

#endif
};
GTEST_DISABLE_MSC_WARNINGS_POP_()  // 4251
#endif  // ::testing::internal::RE implementation

// Formats a source file path and a line number as they would appear
// in an error message from the compiler used to compile this code.
GTEST_API_ ::std::string FormatFileLocation(const char* file, int line);

// Formats a file location for compiler-independent XML output.
// Although this function is not platform dependent, we put it next to
// FormatFileLocation in order to contrast the two functions.
GTEST_API_ ::std::string FormatCompilerIndependentFileLocation(const char* file,
                                                               int line);

// Defines logging utilities:
//   GTEST_LOG_(severity) - logs messages at the specified severity level. The
//                          message itself is streamed into the macro.
//   LogToStderr()  - directs all log messages to stderr.
//   FlushInfoLog() - flushes informational log messages.

enum GTestLogSeverity { GTEST_INFO, GTEST_WARNING, GTEST_ERROR, GTEST_FATAL };

// Formats log entry severity, provides a stream object for streaming the
// log message, and terminates the message with a newline when going out of
// scope.
class GTEST_API_ GTestLog {
 public:
  GTestLog(GTestLogSeverity severity, const char* file, int line);

  // Flushes the buffers and, if severity is GTEST_FATAL, aborts the program.
  ~GTestLog();

  ::std::ostream& GetStream() { return ::std::cerr; }

 private:
  const GTestLogSeverity severity_;

  GTestLog(const GTestLog&) = delete;
  GTestLog& operator=(const GTestLog&) = delete;
};

#if !defined(GTEST_LOG_)

#define GTEST_LOG_(severity)                                           \
  ::testing::internal::GTestLog(::testing::internal::GTEST_##severity, \
                                __FILE__, __LINE__)                    \
      .GetStream()

inline void LogToStderr() {}
inline void FlushInfoLog() { fflush(nullptr); }

#endif  // !defined(GTEST_LOG_)

#if !defined(GTEST_CHECK_)
// INTERNAL IMPLEMENTATION - DO NOT USE.
//
// GTEST_CHECK_ is an all-mode assert. It aborts the program if the condition
// is not satisfied.
//  Synopsis:
//    GTEST_CHECK_(boolean_condition);
//     or
//    GTEST_CHECK_(boolean_condition) << "Additional message";
//
//    This checks the condition and if the condition is not satisfied
//    it prints message about the condition violation, including the
//    condition itself, plus additional message streamed into it, if any,
//    and then it aborts the program. It aborts the program irrespective of
//    whether it is built in the debug mode or not.
#define GTEST_CHECK_(condition)               \
  GTEST_AMBIGUOUS_ELSE_BLOCKER_               \
  if (::testing::internal::IsTrue(condition)) \
    ;                                         \
  else                                        \
    GTEST_LOG_(FATAL) << "Condition " #condition " failed. "
#endif  // !defined(GTEST_CHECK_)

// An all-mode assert to verify that the given POSIX-style function
// call returns 0 (indicating success).  Known limitation: this
// doesn't expand to a balanced 'if' statement, so enclose the macro
// in {} if you need to use it as the only statement in an 'if'
// branch.
#define GTEST_CHECK_POSIX_SUCCESS_(posix_call) \
  if (const int gtest_error = (posix_call))    \
  GTEST_LOG_(FATAL) << #posix_call << "failed with error " << gtest_error

// Transforms "T" into "const T&" according to standard reference collapsing
// rules (this is only needed as a backport for C++98 compilers that do not
// support reference collapsing). Specifically, it transforms:
//
//   char         ==> const char&
//   const char   ==> const char&
//   char&        ==> char&
//   const char&  ==> const char&
//
// Note that the non-const reference will not have "const" added. This is
// standard, and necessary so that "T" can always bind to "const T&".
template <typename T>
struct ConstRef {
  typedef const T& type;
};
template <typename T>
struct ConstRef<T&> {
  typedef T& type;
};

// The argument T must depend on some template parameters.
#define GTEST_REFERENCE_TO_CONST_(T) \
  typename ::testing::internal::ConstRef<T>::type

// INTERNAL IMPLEMENTATION - DO NOT USE IN USER CODE.
//
// Use ImplicitCast_ as a safe version of static_cast for upcasting in
// the type hierarchy (e.g. casting a Foo* to a SuperclassOfFoo* or a
// const Foo*).  When you use ImplicitCast_, the compiler checks that
// the cast is safe.  Such explicit ImplicitCast_s are necessary in
// surprisingly many situations where C++ demands an exact type match
// instead of an argument type convertible to a target type.
//
// The syntax for using ImplicitCast_ is the same as for static_cast:
//
//   ImplicitCast_<ToType>(expr)
//
// ImplicitCast_ would have been part of the C++ standard library,
// but the proposal was submitted too late.  It will probably make
// its way into the language in the future.
//
// This relatively ugly name is intentional. It prevents clashes with
// similar functions users may have (e.g., implicit_cast). The internal
// namespace alone is not enough because the function can be found by ADL.
template <typename To>
inline To ImplicitCast_(To x) {
  return x;
}

// Downcasts the pointer of type Base to Derived.
// Derived must be a subclass of Base. The parameter MUST
// point to a class of type Derived, not any subclass of it.
// When RTTI is available, the function performs a runtime
// check to enforce this.
template <class Derived, class Base>
Derived* CheckedDowncastToActualType(Base* base) {
  static_assert(std::is_base_of<Base, Derived>::value,
                "target type not derived from source type");
#if GTEST_HAS_RTTI
  GTEST_CHECK_(base == nullptr || dynamic_cast<Derived*>(base) != nullptr);
#endif
  return static_cast<Derived*>(base);
}

#if GTEST_HAS_STREAM_REDIRECTION

// Defines the stderr capturer:
//   CaptureStdout     - starts capturing stdout.
//   GetCapturedStdout - stops capturing stdout and returns the captured string.
//   CaptureStderr     - starts capturing stderr.
//   GetCapturedStderr - stops capturing stderr and returns the captured string.
//
GTEST_API_ void CaptureStdout();
GTEST_API_ std::string GetCapturedStdout();
GTEST_API_ void CaptureStderr();
GTEST_API_ std::string GetCapturedStderr();

#endif  // GTEST_HAS_STREAM_REDIRECTION
// Returns the size (in bytes) of a file.
GTEST_API_ size_t GetFileSize(FILE* file);

// Reads the entire content of a file as a string.
GTEST_API_ std::string ReadEntireFile(FILE* file);

// All command line arguments.
GTEST_API_ std::vector<std::string> GetArgvs();

#ifdef GTEST_HAS_DEATH_TEST

std::vector<std::string> GetInjectableArgvs();
// Deprecated: pass the args vector by value instead.
void SetInjectableArgvs(const std::vector<std::string>* new_argvs);
void SetInjectableArgvs(const std::vector<std::string>& new_argvs);
void ClearInjectableArgvs();

#endif  // GTEST_HAS_DEATH_TEST

// Defines synchronization primitives.
#ifdef GTEST_IS_THREADSAFE

#ifdef GTEST_OS_WINDOWS
// Provides leak-safe Windows kernel handle ownership.
// Used in death tests and in threading support.
class GTEST_API_ AutoHandle {
 public:
  // Assume that Win32 HANDLE type is equivalent to void*. Doing so allows us to
  // avoid including <windows.h> in this header file. Including <windows.h> is
  // undesirable because it defines a lot of symbols and macros that tend to
  // conflict with client code. This assumption is verified by
  // WindowsTypesTest.HANDLEIsVoidStar.
  typedef void* Handle;
  AutoHandle();
  explicit AutoHandle(Handle handle);

  ~AutoHandle();

  Handle Get() const;
  void Reset();
  void Reset(Handle handle);

 private:
  // Returns true if and only if the handle is a valid handle object that can be
  // closed.
  bool IsCloseable() const;

  Handle handle_;

  AutoHandle(const AutoHandle&) = delete;
  AutoHandle& operator=(const AutoHandle&) = delete;
};
#endif

#if GTEST_HAS_NOTIFICATION_
// Notification has already been imported into the namespace.
// Nothing to do here.

#else
GTEST_DISABLE_MSC_WARNINGS_PUSH_(4251 \
/* class A needs to have dll-interface to be used by clients of class B */)

// Allows a controller thread to pause execution of newly created
// threads until notified.  Instances of this class must be created
// and destroyed in the controller thread.
//
// This class is only for testing Google Test's own constructs. Do not
// use it in user tests, either directly or indirectly.
// TODO(b/203539622): Replace unconditionally with absl::Notification.
class GTEST_API_ Notification {
 public:
  Notification() : notified_(false) {}
  Notification(const Notification&) = delete;
  Notification& operator=(const Notification&) = delete;

  // Notifies all threads created with this notification to start. Must
  // be called from the controller thread.
  void Notify() {
    std::lock_guard<std::mutex> lock(mu_);
    notified_ = true;
    cv_.notify_all();
  }

  // Blocks until the controller thread notifies. Must be called from a test
  // thread.
  void WaitForNotification() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this]() { return notified_; });
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  bool notified_;
};
GTEST_DISABLE_MSC_WARNINGS_POP_()  // 4251
#endif  // GTEST_HAS_NOTIFICATION_

// On MinGW, we can have both GTEST_OS_WINDOWS and GTEST_HAS_PTHREAD
// defined, but we don't want to use MinGW's pthreads implementation, which
// has conformance problems with some versions of the POSIX standard.
#if GTEST_HAS_PTHREAD && !defined(GTEST_OS_WINDOWS_MINGW)

// As a C-function, ThreadFuncWithCLinkage cannot be templated itself.
// Consequently, it cannot select a correct instantiation of ThreadWithParam
// in order to call its Run(). Introducing ThreadWithParamBase as a
// non-templated base class for ThreadWithParam allows us to bypass this
// problem.
class ThreadWithParamBase {
 public:
  virtual ~ThreadWithParamBase() = default;
  virtual void Run() = 0;
};

// pthread_create() accepts a pointer to a function type with the C linkage.
// According to the Standard (7.5/1), function types with different linkages
// are different even if they are otherwise identical.  Some compilers (for
// example, SunStudio) treat them as different types.  Since class methods
// cannot be defined with C-linkage we need to define a free C-function to
// pass into pthread_create().
extern "C" inline void* ThreadFuncWithCLinkage(void* thread) {
  static_cast<ThreadWithParamBase*>(thread)->Run();
  return nullptr;
}

// Helper class for testing Google Test's multi-threading constructs.
// To use it, write:
//
//   void ThreadFunc(int param) { /* Do things with param */ }
//   Notification thread_can_start;
//   ...
//   // The thread_can_start parameter is optional; you can supply NULL.
//   ThreadWithParam<int> thread(&ThreadFunc, 5, &thread_can_start);
//   thread_can_start.Notify();
//
// These classes are only for testing Google Test's own constructs. Do
// not use them in user tests, either directly or indirectly.
template <typename T>
class ThreadWithParam : public ThreadWithParamBase {
 public:
  typedef void UserThreadFunc(T);

  ThreadWithParam(UserThreadFunc* func, T param, Notification* thread_can_start)
      : func_(func),
        param_(param),
        thread_can_start_(thread_can_start),
        finished_(false) {
    ThreadWithParamBase* const base = this;
    // The thread can be created only after all fields except thread_
    // have been initialized.
    GTEST_CHECK_POSIX_SUCCESS_(
        pthread_create(&thread_, nullptr, &ThreadFuncWithCLinkage, base));
  }
  ~ThreadWithParam() override { Join(); }

  void Join() {
    if (!finished_) {
      GTEST_CHECK_POSIX_SUCCESS_(pthread_join(thread_, nullptr));
      finished_ = true;
    }
  }

  void Run() override {
    if (thread_can_start_ != nullptr) thread_can_start_->WaitForNotification();
    func_(param_);
  }

 private:
  UserThreadFunc* const func_;  // User-supplied thread function.
  const T param_;  // User-supplied parameter to the thread function.
  // When non-NULL, used to block execution until the controller thread
  // notifies.
  Notification* const thread_can_start_;
  bool finished_;  // true if and only if we know that the thread function has
                   // finished.
  pthread_t thread_;  // The native thread object.

  ThreadWithParam(const ThreadWithParam&) = delete;
  ThreadWithParam& operator=(const ThreadWithParam&) = delete;
};
#endif  // !GTEST_OS_WINDOWS && GTEST_HAS_PTHREAD ||
        // GTEST_HAS_MUTEX_AND_THREAD_LOCAL_

#if GTEST_HAS_MUTEX_AND_THREAD_LOCAL_
// Mutex and ThreadLocal have already been imported into the namespace.
// Nothing to do here.

#elif defined(GTEST_OS_WINDOWS) && !defined(GTEST_OS_WINDOWS_PHONE) && \
    !defined(GTEST_OS_WINDOWS_RT)

// Mutex implements mutex on Windows platforms.  It is used in conjunction
// with class MutexLock:
//
//   Mutex mutex;
//   ...
//   MutexLock lock(&mutex);  // Acquires the mutex and releases it at the
//                            // end of the current scope.
//
// A static Mutex *must* be defined or declared using one of the following
// macros:
//   GTEST_DEFINE_STATIC_MUTEX_(g_some_mutex);
//   GTEST_DECLARE_STATIC_MUTEX_(g_some_mutex);
//
// (A non-static Mutex is defined/declared in the usual way).
class GTEST_API_ Mutex {
 public:
  enum MutexType { kStatic = 0, kDynamic = 1 };
  // We rely on kStaticMutex being 0 as it is to what the linker initializes
  // type_ in static mutexes.  critical_section_ will be initialized lazily
  // in ThreadSafeLazyInit().
  enum StaticConstructorSelector { kStaticMutex = 0 };

  // This constructor intentionally does nothing.  It relies on type_ being
  // statically initialized to 0 (effectively setting it to kStatic) and on
  // ThreadSafeLazyInit() to lazily initialize the rest of the members.
  explicit Mutex(StaticConstructorSelector /*dummy*/) {}

  Mutex();
  ~Mutex();

  void Lock();

  void Unlock();

  // Does nothing if the current thread holds the mutex. Otherwise, crashes
  // with high probability.
  void AssertHeld();

 private:
  // Initializes owner_thread_id_ and critical_section_ in static mutexes.
  void ThreadSafeLazyInit();

  // Per https://blogs.msdn.microsoft.com/oldnewthing/20040223-00/?p=40503,
  // we assume that 0 is an invalid value for thread IDs.
  unsigned int owner_thread_id_;

  // For static mutexes, we rely on these members being initialized to zeros
  // by the linker.
  MutexType type_;
  long critical_section_init_phase_;  // NOLINT
  GTEST_CRITICAL_SECTION* critical_section_;

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
};

#define GTEST_DECLARE_STATIC_MUTEX_(mutex) \
  extern ::testing::internal::Mutex mutex

#define GTEST_DEFINE_STATIC_MUTEX_(mutex) \
  ::testing::internal::Mutex mutex(::testing::internal::Mutex::kStaticMutex)

// We cannot name this class MutexLock because the ctor declaration would
// conflict with a macro named MutexLock, which is defined on some
// platforms. That macro is used as a defensive measure to prevent against
// inadvertent misuses of MutexLock like "MutexLock(&mu)" rather than
// "MutexLock l(&mu)".  Hence the typedef trick below.
class GTestMutexLock {
 public:
  explicit GTestMutexLock(Mutex* mutex) : mutex_(mutex) { mutex_->Lock(); }

  ~GTestMutexLock() { mutex_->Unlock(); }

 private:
  Mutex* const mutex_;

  GTestMutexLock(const GTestMutexLock&) = delete;
  GTestMutexLock& operator=(const GTestMutexLock&) = delete;
};

typedef GTestMutexLock MutexLock;

// Base class for ValueHolder<T>.  Allows a caller to hold and delete a value
// without knowing its type.
class ThreadLocalValueHolderBase {
 public:
  virtual ~ThreadLocalValueHolderBase() {}
};

// Provides a way for a thread to send notifications to a ThreadLocal
// regardless of its parameter type.
class ThreadLocalBase {
 public:
  // Creates a new ValueHolder<T> object holding a default value passed to
  // this ThreadLocal<T>'s constructor and returns it.  It is the caller's
  // responsibility not to call this when the ThreadLocal<T> instance already
  // has a value on the current thread.
  virtual ThreadLocalValueHolderBase* NewValueForCurrentThread() const = 0;

 protected:
  ThreadLocalBase() {}
  virtual ~ThreadLocalBase() {}

 private:
  ThreadLocalBase(const ThreadLocalBase&) = delete;
  ThreadLocalBase& operator=(const ThreadLocalBase&) = delete;
};

// Maps a thread to a set of ThreadLocals that have values instantiated on that
// thread and notifies them when the thread exits.  A ThreadLocal instance is
// expected to persist until all threads it has values on have terminated.
class GTEST_API_ ThreadLocalRegistry {
 public:
  // Registers thread_local_instance as having value on the current thread.
  // Returns a value that can be used to identify the thread from other threads.
  static ThreadLocalValueHolderBase* GetValueOnCurrentThread(
      const ThreadLocalBase* thread_local_instance);

  // Invoked when a ThreadLocal instance is destroyed.
  static void OnThreadLocalDestroyed(
      const ThreadLocalBase* thread_local_instance);
};

class GTEST_API_ ThreadWithParamBase {
 public:
  void Join();

 protected:
  class Runnable {
   public:
    virtual ~Runnable() {}
    virtual void Run() = 0;
  };

  ThreadWithParamBase(Runnable* runnable, Notification* thread_can_start);
  virtual ~ThreadWithParamBase();

 private:
  AutoHandle thread_;
};

// Helper class for testing Google Test's multi-threading constructs.
template <typename T>
class ThreadWithParam : public ThreadWithParamBase {
 public:
  typedef void UserThreadFunc(T);

  ThreadWithParam(UserThreadFunc* func, T param, Notification* thread_can_start)
      : ThreadWithParamBase(new RunnableImpl(func, param), thread_can_start) {}
  virtual ~ThreadWithParam() {}

 private:
  class RunnableImpl : public Runnable {
   public:
    RunnableImpl(UserThreadFunc* func, T param) : func_(func), param_(param) {}
    virtual ~RunnableImpl() {}
    virtual void Run() { func_(param_); }

   private:
    UserThreadFunc* const func_;
    const T param_;

    RunnableImpl(const RunnableImpl&) = delete;
    RunnableImpl& operator=(const RunnableImpl&) = delete;
  };

  ThreadWithParam(const ThreadWithParam&) = delete;
  ThreadWithParam& operator=(const ThreadWithParam&) = delete;
};

// Implements thread-local storage on Windows systems.
//
//   // Thread 1
//   ThreadLocal<int> tl(100);  // 100 is the default value for each thread.
//
//   // Thread 2
//   tl.set(150);  // Changes the value for thread 2 only.
//   EXPECT_EQ(150, tl.get());
//
//   // Thread 1
//   EXPECT_EQ(100, tl.get());  // In thread 1, tl has the original value.
//   tl.set(200);
//   EXPECT_EQ(200, tl.get());
//
// The template type argument T must have a public copy constructor.
// In addition, the default ThreadLocal constructor requires T to have
// a public default constructor.
//
// The users of a TheadLocal instance have to make sure that all but one
// threads (including the main one) using that instance have exited before
// destroying it. Otherwise, the per-thread objects managed for them by the
// ThreadLocal instance are not guaranteed to be destroyed on all platforms.
//
// Google Test only uses global ThreadLocal objects.  That means they
// will die after main() has returned.  Therefore, no per-thread
// object managed by Google Test will be leaked as long as all threads
// using Google Test have exited when main() returns.
template <typename T>
class ThreadLocal : public ThreadLocalBase {
 public:
  ThreadLocal() : default_factory_(new DefaultValueHolderFactory()) {}
  explicit ThreadLocal(const T& value)
      : default_factory_(new InstanceValueHolderFactory(value)) {}

  ~ThreadLocal() override { ThreadLocalRegistry::OnThreadLocalDestroyed(this); }

  T* pointer() { return GetOrCreateValue(); }
  const T* pointer() const { return GetOrCreateValue(); }
  const T& get() const { return *pointer(); }
  void set(const T& value) { *pointer() = value; }

 private:
  // Holds a value of T.  Can be deleted via its base class without the caller
  // knowing the type of T.
  class ValueHolder : public ThreadLocalValueHolderBase {
   public:
    ValueHolder() : value_() {}
    explicit ValueHolder(const T& value) : value_(value) {}

    T* pointer() { return &value_; }

   private:
    T value_;
    ValueHolder(const ValueHolder&) = delete;
    ValueHolder& operator=(const ValueHolder&) = delete;
  };

  T* GetOrCreateValue() const {
    return static_cast<ValueHolder*>(
               ThreadLocalRegistry::GetValueOnCurrentThread(this))
        ->pointer();
  }

  ThreadLocalValueHolderBase* NewValueForCurrentThread() const override {
    return default_factory_->MakeNewHolder();
  }

  class ValueHolderFactory {
   public:
    ValueHolderFactory() {}
    virtual ~ValueHolderFactory() {}
    virtual ValueHolder* MakeNewHolder() const = 0;

   private:
    ValueHolderFactory(const ValueHolderFactory&) = delete;
    ValueHolderFactory& operator=(const ValueHolderFactory&) = delete;
  };

  class DefaultValueHolderFactory : public ValueHolderFactory {
   public:
    DefaultValueHolderFactory() {}
    ValueHolder* MakeNewHolder() const override { return new ValueHolder(); }

   private:
    DefaultValueHolderFactory(const DefaultValueHolderFactory&) = delete;
    DefaultValueHolderFactory& operator=(const DefaultValueHolderFactory&) =
        delete;
  };

  class InstanceValueHolderFactory : public ValueHolderFactory {
   public:
    explicit InstanceValueHolderFactory(const T& value) : value_(value) {}
    ValueHolder* MakeNewHolder() const override {
      return new ValueHolder(value_);
    }

   private:
    const T value_;  // The value for each thread.

    InstanceValueHolderFactory(const InstanceValueHolderFactory&) = delete;
    InstanceValueHolderFactory& operator=(const InstanceValueHolderFactory&) =
        delete;
  };

  std::unique_ptr<ValueHolderFactory> default_factory_;

  ThreadLocal(const ThreadLocal&) = delete;
  ThreadLocal& operator=(const ThreadLocal&) = delete;
};

#elif GTEST_HAS_PTHREAD

// MutexBase and Mutex implement mutex on pthreads-based platforms.
class MutexBase {
 public:
  // Acquires this mutex.
  void Lock() {
    GTEST_CHECK_POSIX_SUCCESS_(pthread_mutex_lock(&mutex_));
    owner_ = pthread_self();
    has_owner_ = true;
  }

  // Releases this mutex.
  void Unlock() {
    // Since the lock is being released the owner_ field should no longer be
    // considered valid. We don't protect writing to has_owner_ here, as it's
    // the caller's responsibility to ensure that the current thread holds the
    // mutex when this is called.
    has_owner_ = false;
    GTEST_CHECK_POSIX_SUCCESS_(pthread_mutex_unlock(&mutex_));
  }

  // Does nothing if the current thread holds the mutex. Otherwise, crashes
  // with high probability.
  void AssertHeld() const {
    GTEST_CHECK_(has_owner_ && pthread_equal(owner_, pthread_self()))
        << "The current thread is not holding the mutex @" << this;
  }

  // A static mutex may be used before main() is entered.  It may even
  // be used before the dynamic initialization stage.  Therefore we
  // must be able to initialize a static mutex object at link time.
  // This means MutexBase has to be a POD and its member variables
  // have to be public.
 public:
  pthread_mutex_t mutex_;  // The underlying pthread mutex.
  // has_owner_ indicates whether the owner_ field below contains a valid thread
  // ID and is therefore safe to inspect (e.g., to use in pthread_equal()). All
  // accesses to the owner_ field should be protected by a check of this field.
  // An alternative might be to memset() owner_ to all zeros, but there's no
  // guarantee that a zero'd pthread_t is necessarily invalid or even different
  // from pthread_self().
  bool has_owner_;
  pthread_t owner_;  // The thread holding the mutex.
};

// Forward-declares a static mutex.
#define GTEST_DECLARE_STATIC_MUTEX_(mutex) \
  extern ::testing::internal::MutexBase mutex

// Defines and statically (i.e. at link time) initializes a static mutex.
// The initialization list here does not explicitly initialize each field,
// instead relying on default initialization for the unspecified fields. In
// particular, the owner_ field (a pthread_t) is not explicitly initialized.
// This allows initialization to work whether pthread_t is a scalar or struct.
// The flag -Wmissing-field-initializers must not be specified for this to work.
#define GTEST_DEFINE_STATIC_MUTEX_(mutex) \
  ::testing::internal::MutexBase mutex = {PTHREAD_MUTEX_INITIALIZER, false, 0}

// The Mutex class can only be used for mutexes created at runtime. It
// shares its API with MutexBase otherwise.
class Mutex : public MutexBase {
 public:
  Mutex() {
    GTEST_CHECK_POSIX_SUCCESS_(pthread_mutex_init(&mutex_, nullptr));
    has_owner_ = false;
  }
  ~Mutex() { GTEST_CHECK_POSIX_SUCCESS_(pthread_mutex_destroy(&mutex_)); }

 private:
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
};

// We cannot name this class MutexLock because the ctor declaration would
// conflict with a macro named MutexLock, which is defined on some
// platforms. That macro is used as a defensive measure to prevent against
// inadvertent misuses of MutexLock like "MutexLock(&mu)" rather than
// "MutexLock l(&mu)".  Hence the typedef trick below.
class GTestMutexLock {
 public:
  explicit GTestMutexLock(MutexBase* mutex) : mutex_(mutex) { mutex_->Lock(); }

  ~GTestMutexLock() { mutex_->Unlock(); }

 private:
  MutexBase* const mutex_;

  GTestMutexLock(const GTestMutexLock&) = delete;
  GTestMutexLock& operator=(const GTestMutexLock&) = delete;
};

typedef GTestMutexLock MutexLock;

// Helpers for ThreadLocal.

// pthread_key_create() requires DeleteThreadLocalValue() to have
// C-linkage.  Therefore it cannot be templatized to access
// ThreadLocal<T>.  Hence the need for class
// ThreadLocalValueHolderBase.
class GTEST_API_ ThreadLocalValueHolderBase {
 public:
  virtual ~ThreadLocalValueHolderBase() = default;
};

// Called by pthread to delete thread-local data stored by
// pthread_setspecific().
extern "C" inline void DeleteThreadLocalValue(void* value_holder) {
  delete static_cast<ThreadLocalValueHolderBase*>(value_holder);
}

// Implements thread-local storage on pthreads-based systems.
template <typename T>
class GTEST_API_ ThreadLocal {
 public:
  ThreadLocal()
      : key_(CreateKey()), default_factory_(new DefaultValueHolderFactory()) {}
  explicit ThreadLocal(const T& value)
      : key_(CreateKey()),
        default_factory_(new InstanceValueHolderFactory(value)) {}

  ~ThreadLocal() {
    // Destroys the managed object for the current thread, if any.
    DeleteThreadLocalValue(pthread_getspecific(key_));

    // Releases resources associated with the key.  This will *not*
    // delete managed objects for other threads.
    GTEST_CHECK_POSIX_SUCCESS_(pthread_key_delete(key_));
  }

  T* pointer() { return GetOrCreateValue(); }
  const T* pointer() const { return GetOrCreateValue(); }
  const T& get() const { return *pointer(); }
  void set(const T& value) { *pointer() = value; }

 private:
  // Holds a value of type T.
  class ValueHolder : public ThreadLocalValueHolderBase {
   public:
    ValueHolder() : value_() {}
    explicit ValueHolder(const T& value) : value_(value) {}

    T* pointer() { return &value_; }

   private:
    T value_;
    ValueHolder(const ValueHolder&) = delete;
    ValueHolder& operator=(const ValueHolder&) = delete;
  };

  static pthread_key_t CreateKey() {
    pthread_key_t key;
    // When a thread exits, DeleteThreadLocalValue() will be called on
    // the object managed for that thread.
    GTEST_CHECK_POSIX_SUCCESS_(
        pthread_key_create(&key, &DeleteThreadLocalValue));
    return key;
  }

  T* GetOrCreateValue() const {
    ThreadLocalValueHolderBase* const holder =
        static_cast<ThreadLocalValueHolderBase*>(pthread_getspecific(key_));
    if (holder != nullptr) {
      return CheckedDowncastToActualType<ValueHolder>(holder)->pointer();
    }

    ValueHolder* const new_holder = default_factory_->MakeNewHolder();
    ThreadLocalValueHolderBase* const holder_base = new_holder;
    GTEST_CHECK_POSIX_SUCCESS_(pthread_setspecific(key_, holder_base));
    return new_holder->pointer();
  }

  class ValueHolderFactory {
   public:
    ValueHolderFactory() = default;
    virtual ~ValueHolderFactory() = default;
    virtual ValueHolder* MakeNewHolder() const = 0;

   private:
    ValueHolderFactory(const ValueHolderFactory&) = delete;
    ValueHolderFactory& operator=(const ValueHolderFactory&) = delete;
  };

  class DefaultValueHolderFactory : public ValueHolderFactory {
   public:
    DefaultValueHolderFactory() = default;
    ValueHolder* MakeNewHolder() const override { return new ValueHolder(); }

   private:
    DefaultValueHolderFactory(const DefaultValueHolderFactory&) = delete;
    DefaultValueHolderFactory& operator=(const DefaultValueHolderFactory&) =
        delete;
  };

  class InstanceValueHolderFactory : public ValueHolderFactory {
   public:
    explicit InstanceValueHolderFactory(const T& value) : value_(value) {}
    ValueHolder* MakeNewHolder() const override {
      return new ValueHolder(value_);
    }

   private:
    const T value_;  // The value for each thread.

    InstanceValueHolderFactory(const InstanceValueHolderFactory&) = delete;
    InstanceValueHolderFactory& operator=(const InstanceValueHolderFactory&) =
        delete;
  };

  // A key pthreads uses for looking up per-thread values.
  const pthread_key_t key_;
  std::unique_ptr<ValueHolderFactory> default_factory_;

  ThreadLocal(const ThreadLocal&) = delete;
  ThreadLocal& operator=(const ThreadLocal&) = delete;
};

#endif  // GTEST_HAS_MUTEX_AND_THREAD_LOCAL_

#else  // GTEST_IS_THREADSAFE

// A dummy implementation of synchronization primitives (mutex, lock,
// and thread-local variable).  Necessary for compiling Google Test where
// mutex is not supported - using Google Test in multiple threads is not
// supported on such platforms.

class Mutex {
 public:
  Mutex() {}
  void Lock() {}
  void Unlock() {}
  void AssertHeld() const {}
};

#define GTEST_DECLARE_STATIC_MUTEX_(mutex) \
  extern ::testing::internal::Mutex mutex

#define GTEST_DEFINE_STATIC_MUTEX_(mutex) ::testing::internal::Mutex mutex

// We cannot name this class MutexLock because the ctor declaration would
// conflict with a macro named MutexLock, which is defined on some
// platforms. That macro is used as a defensive measure to prevent against
// inadvertent misuses of MutexLock like "MutexLock(&mu)" rather than
// "MutexLock l(&mu)".  Hence the typedef trick below.
class GTestMutexLock {
 public:
  explicit GTestMutexLock(Mutex*) {}  // NOLINT
};

typedef GTestMutexLock MutexLock;

template <typename T>
class GTEST_API_ ThreadLocal {
 public:
  ThreadLocal() : value_() {}
  explicit ThreadLocal(const T& value) : value_(value) {}
  T* pointer() { return &value_; }
  const T* pointer() const { return &value_; }
  const T& get() const { return value_; }
  void set(const T& value) { value_ = value; }

 private:
  T value_;
};

#endif  // GTEST_IS_THREADSAFE

// Returns the number of threads running in the process, or 0 to indicate that
// we cannot detect it.
GTEST_API_ size_t GetThreadCount();

#ifdef GTEST_OS_WINDOWS
#define GTEST_PATH_SEP_ "\\"
#define GTEST_HAS_ALT_PATH_SEP_ 1
#else
#define GTEST_PATH_SEP_ "/"
#define GTEST_HAS_ALT_PATH_SEP_ 0
#endif  // GTEST_OS_WINDOWS

// Utilities for char.

// isspace(int ch) and friends accept an unsigned char or EOF.  char
// may be signed, depending on the compiler (or compiler flags).
// Therefore we need to cast a char to unsigned char before calling
// isspace(), etc.

inline bool IsAlpha(char ch) {
  return isalpha(static_cast<unsigned char>(ch)) != 0;
}
inline bool IsAlNum(char ch) {
  return isalnum(static_cast<unsigned char>(ch)) != 0;
}
inline bool IsDigit(char ch) {
  return isdigit(static_cast<unsigned char>(ch)) != 0;
}
inline bool IsLower(char ch) {
  return islower(static_cast<unsigned char>(ch)) != 0;
}
inline bool IsSpace(char ch) {
  return isspace(static_cast<unsigned char>(ch)) != 0;
}
inline bool IsUpper(char ch) {
  return isupper(static_cast<unsigned char>(ch)) != 0;
}
inline bool IsXDigit(char ch) {
  return isxdigit(static_cast<unsigned char>(ch)) != 0;
}
#ifdef __cpp_lib_char8_t
inline bool IsXDigit(char8_t ch) {
  return isxdigit(static_cast<unsigned char>(ch)) != 0;
}
#endif
inline bool IsXDigit(char16_t ch) {
  const unsigned char low_byte = static_cast<unsigned char>(ch);
  return ch == low_byte && isxdigit(low_byte) != 0;
}
inline bool IsXDigit(char32_t ch) {
  const unsigned char low_byte = static_cast<unsigned char>(ch);
  return ch == low_byte && isxdigit(low_byte) != 0;
}
inline bool IsXDigit(wchar_t ch) {
  const unsigned char low_byte = static_cast<unsigned char>(ch);
  return ch == low_byte && isxdigit(low_byte) != 0;
}

inline char ToLower(char ch) {
  return static_cast<char>(tolower(static_cast<unsigned char>(ch)));
}
inline char ToUpper(char ch) {
  return static_cast<char>(toupper(static_cast<unsigned char>(ch)));
}

inline std::string StripTrailingSpaces(std::string str) {
  std::string::iterator it = str.end();
  while (it != str.begin() && IsSpace(*--it)) it = str.erase(it);
  return str;
}

// The testing::internal::posix namespace holds wrappers for common
// POSIX functions.  These wrappers hide the differences between
// Windows/MSVC and POSIX systems.  Since some compilers define these
// standard functions as macros, the wrapper cannot have the same name
// as the wrapped function.

namespace posix {

// File system porting.
// Note: Not every I/O-related function is related to file systems, so don't
// just disable all of them here. For example, fileno() and isatty(), etc. must
// always be available in order to detect if a pipe points to a terminal.
#ifdef GTEST_OS_WINDOWS

typedef struct _stat StatStruct;

#ifdef GTEST_OS_WINDOWS_MOBILE
inline int FileNo(FILE* file) { return reinterpret_cast<int>(_fileno(file)); }
// Stat(), RmDir(), and IsDir() are not needed on Windows CE at this
// time and thus not defined there.
#else
inline int FileNo(FILE* file) { return _fileno(file); }
#if GTEST_HAS_FILE_SYSTEM
inline int Stat(const char* path, StatStruct* buf) { return _stat(path, buf); }
inline int RmDir(const char* dir) { return _rmdir(dir); }
inline bool IsDir(const StatStruct& st) { return (_S_IFDIR & st.st_mode) != 0; }
#endif
#endif  // GTEST_OS_WINDOWS_MOBILE

#elif defined(GTEST_OS_ESP8266)
typedef struct stat StatStruct;

inline int FileNo(FILE* file) { return fileno(file); }
#if GTEST_HAS_FILE_SYSTEM
inline int Stat(const char* path, StatStruct* buf) {
  // stat function not implemented on ESP8266
  return 0;
}
inline int RmDir(const char* dir) { return rmdir(dir); }
inline bool IsDir(const StatStruct& st) { return S_ISDIR(st.st_mode); }
#endif

#else

typedef struct stat StatStruct;

inline int FileNo(FILE* file) { return fileno(file); }
#if GTEST_HAS_FILE_SYSTEM
inline int Stat(const char* path, StatStruct* buf) { return stat(path, buf); }
#ifdef GTEST_OS_QURT
// QuRT doesn't support any directory functions, including rmdir
inline int RmDir(const char*) { return 0; }
#else
inline int RmDir(const char* dir) { return rmdir(dir); }
#endif
inline bool IsDir(const StatStruct& st) { return S_ISDIR(st.st_mode); }
#endif

#endif  // GTEST_OS_WINDOWS

// Other functions with a different name on Windows.

#ifdef GTEST_OS_WINDOWS

#ifdef __BORLANDC__
inline int DoIsATTY(int fd) { return isatty(fd); }
inline int StrCaseCmp(const char* s1, const char* s2) {
  return stricmp(s1, s2);
}
#else  // !__BORLANDC__
#if defined(GTEST_OS_WINDOWS_MOBILE) || defined(GTEST_OS_ZOS) || \
    defined(GTEST_OS_IOS) || defined(GTEST_OS_WINDOWS_PHONE) ||  \
    defined(GTEST_OS_WINDOWS_RT) || defined(ESP_PLATFORM)
inline int DoIsATTY(int /* fd */) { return 0; }
#else
inline int DoIsATTY(int fd) { return _isatty(fd); }
#endif  // GTEST_OS_WINDOWS_MOBILE
inline int StrCaseCmp(const char* s1, const char* s2) {
  return _stricmp(s1, s2);
}
#endif  // __BORLANDC__

#else

inline int DoIsATTY(int fd) { return isatty(fd); }
inline int StrCaseCmp(const char* s1, const char* s2) {
  return strcasecmp(s1, s2);
}

#endif  // GTEST_OS_WINDOWS

inline int IsATTY(int fd) {
  // DoIsATTY might change errno (for example ENOTTY in case you redirect stdout
  // to a file on Linux), which is unexpected, so save the previous value, and
  // restore it after the call.
  int savedErrno = errno;
  int isAttyValue = DoIsATTY(fd);
  errno = savedErrno;

  return isAttyValue;
}

// Functions deprecated by MSVC 8.0.

GTEST_DISABLE_MSC_DEPRECATED_PUSH_()

// ChDir(), FReopen(), FDOpen(), Read(), Write(), Close(), and
// StrError() aren't needed on Windows CE at this time and thus not
// defined there.
#if GTEST_HAS_FILE_SYSTEM
#if !defined(GTEST_OS_WINDOWS_MOBILE) && !defined(GTEST_OS_WINDOWS_PHONE) && \
    !defined(GTEST_OS_WINDOWS_RT) && !defined(GTEST_OS_WINDOWS_GAMES) &&     \
    !defined(GTEST_OS_ESP8266) && !defined(GTEST_OS_XTENSA) &&               \
    !defined(GTEST_OS_QURT)
inline int ChDir(const char* dir) { return chdir(dir); }
#endif
inline FILE* FOpen(const char* path, const char* mode) {
#if defined(GTEST_OS_WINDOWS) && !defined(GTEST_OS_WINDOWS_MINGW)
  struct wchar_codecvt : public std::codecvt<wchar_t, char, std::mbstate_t> {};
  std::wstring_convert<wchar_codecvt> converter;
  std::wstring wide_path = converter.from_bytes(path);
  std::wstring wide_mode = converter.from_bytes(mode);
  return _wfopen(wide_path.c_str(), wide_mode.c_str());
#else   // GTEST_OS_WINDOWS && !GTEST_OS_WINDOWS_MINGW
  return fopen(path, mode);
#endif  // GTEST_OS_WINDOWS && !GTEST_OS_WINDOWS_MINGW
}
#if !defined(GTEST_OS_WINDOWS_MOBILE) && !defined(GTEST_OS_QURT)
inline FILE* FReopen(const char* path, const char* mode, FILE* stream) {
  return freopen(path, mode, stream);
}
inline FILE* FDOpen(int fd, const char* mode) { return fdopen(fd, mode); }
#endif  // !GTEST_OS_WINDOWS_MOBILE && !GTEST_OS_QURT
inline int FClose(FILE* fp) { return fclose(fp); }
#if !defined(GTEST_OS_WINDOWS_MOBILE) && !defined(GTEST_OS_QURT)
inline int Read(int fd, void* buf, unsigned int count) {
  return static_cast<int>(read(fd, buf, count));
}
inline int Write(int fd, const void* buf, unsigned int count) {
  return static_cast<int>(write(fd, buf, count));
}
inline int Close(int fd) { return close(fd); }
#endif  // !GTEST_OS_WINDOWS_MOBILE && !GTEST_OS_QURT
#endif  // GTEST_HAS_FILE_SYSTEM

#if !defined(GTEST_OS_WINDOWS_MOBILE) && !defined(GTEST_OS_QURT)
inline const char* StrError(int errnum) { return strerror(errnum); }
#endif  // !GTEST_OS_WINDOWS_MOBILE && !GTEST_OS_QURT

inline const char* GetEnv(const char* name) {
#if defined(GTEST_OS_WINDOWS_MOBILE) || defined(GTEST_OS_WINDOWS_PHONE) || \
    defined(GTEST_OS_ESP8266) || defined(GTEST_OS_XTENSA) ||               \
    defined(GTEST_OS_QURT)
  // We are on an embedded platform, which has no environment variables.
  static_cast<void>(name);  // To prevent 'unused argument' warning.
  return nullptr;
#elif defined(__BORLANDC__) || defined(__SunOS_5_8) || defined(__SunOS_5_9)
  // Environment variables which we programmatically clear will be set to the
  // empty string rather than unset (NULL).  Handle that case.
  const char* const env = getenv(name);
  return (env != nullptr && env[0] != '\0') ? env : nullptr;
#else
  return getenv(name);
#endif
}

GTEST_DISABLE_MSC_DEPRECATED_POP_()

#ifdef GTEST_OS_WINDOWS_MOBILE
// Windows CE has no C library. The abort() function is used in
// several places in Google Test. This implementation provides a reasonable
// imitation of standard behaviour.
[[noreturn]] void Abort();
#else
[[noreturn]] inline void Abort() { abort(); }
#endif  // GTEST_OS_WINDOWS_MOBILE

}  // namespace posix

// MSVC "deprecates" snprintf and issues warnings wherever it is used.  In
// order to avoid these warnings, we need to use _snprintf or _snprintf_s on
// MSVC-based platforms.  We map the GTEST_SNPRINTF_ macro to the appropriate
// function in order to achieve that.  We use macro definition here because
// snprintf is a variadic function.
#if defined(_MSC_VER) && !defined(GTEST_OS_WINDOWS_MOBILE)
// MSVC 2005 and above support variadic macros.
#define GTEST_SNPRINTF_(buffer, size, format, ...) \
  _snprintf_s(buffer, size, size, format, __VA_ARGS__)
#elif defined(_MSC_VER)
// Windows CE does not define _snprintf_s
#define GTEST_SNPRINTF_ _snprintf
#else
#define GTEST_SNPRINTF_ snprintf
#endif

// The biggest signed integer type the compiler supports.
//
// long long is guaranteed to be at least 64-bits in C++11.
using BiggestInt = long long;  // NOLINT

// The maximum number a BiggestInt can represent.
constexpr BiggestInt kMaxBiggestInt = (std::numeric_limits<BiggestInt>::max)();

// This template class serves as a compile-time function from size to
// type.  It maps a size in bytes to a primitive type with that
// size. e.g.
//
//   TypeWithSize<4>::UInt
//
// is typedef-ed to be unsigned int (unsigned integer made up of 4
// bytes).
//
// Such functionality should belong to STL, but I cannot find it
// there.
//
// Google Test uses this class in the implementation of floating-point
// comparison.
//
// For now it only handles UInt (unsigned int) as that's all Google Test
// needs.  Other types can be easily added in the future if need
// arises.
template <size_t size>
class TypeWithSize {
 public:
  // This prevents the user from using TypeWithSize<N> with incorrect
  // values of N.
  using UInt = void;
};

// The specialization for size 4.
template <>
class TypeWithSize<4> {
 public:
  using Int = std::int32_t;
  using UInt = std::uint32_t;
};

// The specialization for size 8.
template <>
class TypeWithSize<8> {
 public:
  using Int = std::int64_t;
  using UInt = std::uint64_t;
};

// Integer types of known sizes.
using TimeInMillis = int64_t;  // Represents time in milliseconds.

// Utilities for command line flags and environment variables.

// Macro for referencing flags.
#if !defined(GTEST_FLAG)
#define GTEST_FLAG_NAME_(name) gtest_##name
#define GTEST_FLAG(name) FLAGS_gtest_##name
#endif  // !defined(GTEST_FLAG)

// Pick a command line flags implementation.
#ifdef GTEST_INTERNAL_HAS_ABSL_FLAGS

// Macros for defining flags.
#define GTEST_DEFINE_bool_(name, default_val, doc) \
  ABSL_FLAG(bool, GTEST_FLAG_NAME_(name), default_val, doc)
#define GTEST_DEFINE_int32_(name, default_val, doc) \
  ABSL_FLAG(int32_t, GTEST_FLAG_NAME_(name), default_val, doc)
#define GTEST_DEFINE_string_(name, default_val, doc) \
  ABSL_FLAG(std::string, GTEST_FLAG_NAME_(name), default_val, doc)

// Macros for declaring flags.
#define GTEST_DECLARE_bool_(name) \
  ABSL_DECLARE_FLAG(bool, GTEST_FLAG_NAME_(name))
#define GTEST_DECLARE_int32_(name) \
  ABSL_DECLARE_FLAG(int32_t, GTEST_FLAG_NAME_(name))
#define GTEST_DECLARE_string_(name) \
  ABSL_DECLARE_FLAG(std::string, GTEST_FLAG_NAME_(name))

#define GTEST_FLAG_SAVER_ ::absl::FlagSaver

#define GTEST_FLAG_GET(name) ::absl::GetFlag(GTEST_FLAG(name))
#define GTEST_FLAG_SET(name, value) \
  (void)(::absl::SetFlag(&GTEST_FLAG(name), value))
#define GTEST_USE_OWN_FLAGFILE_FLAG_ 0

#undef GTEST_INTERNAL_HAS_ABSL_FLAGS
#else  // ndef GTEST_INTERNAL_HAS_ABSL_FLAGS

// Macros for defining flags.
#define GTEST_DEFINE_bool_(name, default_val, doc)  \
  namespace testing {                               \
  GTEST_API_ bool GTEST_FLAG(name) = (default_val); \
  }                                                 \
  static_assert(true, "no-op to require trailing semicolon")
#define GTEST_DEFINE_int32_(name, default_val, doc)         \
  namespace testing {                                       \
  GTEST_API_ std::int32_t GTEST_FLAG(name) = (default_val); \
  }                                                         \
  static_assert(true, "no-op to require trailing semicolon")
#define GTEST_DEFINE_string_(name, default_val, doc)         \
  namespace testing {                                        \
  GTEST_API_ ::std::string GTEST_FLAG(name) = (default_val); \
  }                                                          \
  static_assert(true, "no-op to require trailing semicolon")

// Macros for declaring flags.
#define GTEST_DECLARE_bool_(name)          \
  namespace testing {                      \
  GTEST_API_ extern bool GTEST_FLAG(name); \
  }                                        \
  static_assert(true, "no-op to require trailing semicolon")
#define GTEST_DECLARE_int32_(name)                 \
  namespace testing {                              \
  GTEST_API_ extern std::int32_t GTEST_FLAG(name); \
  }                                                \
  static_assert(true, "no-op to require trailing semicolon")
#define GTEST_DECLARE_string_(name)                 \
  namespace testing {                               \
  GTEST_API_ extern ::std::string GTEST_FLAG(name); \
  }                                                 \
  static_assert(true, "no-op to require trailing semicolon")

#define GTEST_FLAG_SAVER_ ::testing::internal::GTestFlagSaver

#define GTEST_FLAG_GET(name) ::testing::GTEST_FLAG(name)
#define GTEST_FLAG_SET(name, value) (void)(::testing::GTEST_FLAG(name) = value)
#define GTEST_USE_OWN_FLAGFILE_FLAG_ 1

#endif  // GTEST_INTERNAL_HAS_ABSL_FLAGS

// Thread annotations
#if !defined(GTEST_EXCLUSIVE_LOCK_REQUIRED_)
#define GTEST_EXCLUSIVE_LOCK_REQUIRED_(locks)
#define GTEST_LOCK_EXCLUDED_(locks)
#endif  // !defined(GTEST_EXCLUSIVE_LOCK_REQUIRED_)

// Parses 'str' for a 32-bit signed integer.  If successful, writes the result
// to *value and returns true; otherwise leaves *value unchanged and returns
// false.
GTEST_API_ bool ParseInt32(const Message& src_text, const char* str,
                           int32_t* value);

// Parses a bool/int32_t/string from the environment variable
// corresponding to the given Google Test flag.
bool BoolFromGTestEnv(const char* flag, bool default_val);
GTEST_API_ int32_t Int32FromGTestEnv(const char* flag, int32_t default_val);
std::string OutputFlagAlsoCheckEnvVar();
const char* StringFromGTestEnv(const char* flag, const char* default_val);

}  // namespace internal
}  // namespace testing

#if !defined(GTEST_INTERNAL_DEPRECATED)

// Internal Macro to mark an API deprecated, for googletest usage only
// Usage: class GTEST_INTERNAL_DEPRECATED(message) MyClass or
// GTEST_INTERNAL_DEPRECATED(message) <return_type> myFunction(); Every usage of
// a deprecated entity will trigger a warning when compiled with
// `-Wdeprecated-declarations` option (clang, gcc, any __GNUC__ compiler).
// For msvc /W3 option will need to be used
// Note that for 'other' compilers this macro evaluates to nothing to prevent
// compilations errors.
#if defined(_MSC_VER)
#define GTEST_INTERNAL_DEPRECATED(message) __declspec(deprecated(message))
#elif defined(__GNUC__)
#define GTEST_INTERNAL_DEPRECATED(message) __attribute__((deprecated(message)))
#else
#define GTEST_INTERNAL_DEPRECATED(message)
#endif

#endif  // !defined(GTEST_INTERNAL_DEPRECATED)

#ifdef GTEST_HAS_ABSL
// Always use absl::any for UniversalPrinter<> specializations if googletest
// is built with absl support.
#define GTEST_INTERNAL_HAS_ANY 1
#include "absl/types/any.h"
namespace testing {
namespace internal {
using Any = ::absl::any;
}  // namespace internal
}  // namespace testing
#else
#if defined(__cpp_lib_any) || (GTEST_INTERNAL_HAS_INCLUDE(<any>) &&        \
                               GTEST_INTERNAL_CPLUSPLUS_LANG >= 201703L && \
                               (!defined(_MSC_VER) || GTEST_HAS_RTTI))
// Otherwise for C++17 and higher use std::any for UniversalPrinter<>
// specializations.
#define GTEST_INTERNAL_HAS_ANY 1
#include <any>
namespace testing {
namespace internal {
using Any = ::std::any;
}  // namespace internal
}  // namespace testing
// The case where absl is configured NOT to alias std::any is not
// supported.
#endif  // __cpp_lib_any
#endif  // GTEST_HAS_ABSL

#ifndef GTEST_INTERNAL_HAS_ANY
#define GTEST_INTERNAL_HAS_ANY 0
#endif

#ifdef GTEST_HAS_ABSL
// Always use absl::optional for UniversalPrinter<> specializations if
// googletest is built with absl support.
#define GTEST_INTERNAL_HAS_OPTIONAL 1
#include "absl/types/optional.h"
namespace testing {
namespace internal {
template <typename T>
using Optional = ::absl::optional<T>;
inline ::absl::nullopt_t Nullopt() { return ::absl::nullopt; }
}  // namespace internal
}  // namespace testing
#else
#if defined(__cpp_lib_optional) || (GTEST_INTERNAL_HAS_INCLUDE(<optional>) && \
                                    GTEST_INTERNAL_CPLUSPLUS_LANG >= 201703L)
// Otherwise for C++17 and higher use std::optional for UniversalPrinter<>
// specializations.
#define GTEST_INTERNAL_HAS_OPTIONAL 1
#include <optional>
namespace testing {
namespace internal {
template <typename T>
using Optional = ::std::optional<T>;
inline ::std::nullopt_t Nullopt() { return ::std::nullopt; }
}  // namespace internal
}  // namespace testing
// The case where absl is configured NOT to alias std::optional is not
// supported.
#endif  // __cpp_lib_optional
#endif  // GTEST_HAS_ABSL

#ifndef GTEST_INTERNAL_HAS_OPTIONAL
#define GTEST_INTERNAL_HAS_OPTIONAL 0
#endif

#if defined(__cpp_lib_span) || (GTEST_INTERNAL_HAS_INCLUDE(<span>) && \
                                GTEST_INTERNAL_CPLUSPLUS_LANG >= 202002L)
#define GTEST_INTERNAL_HAS_STD_SPAN 1
#endif  // __cpp_lib_span

#ifndef GTEST_INTERNAL_HAS_STD_SPAN
#define GTEST_INTERNAL_HAS_STD_SPAN 0
#endif

#ifdef GTEST_HAS_ABSL
// Always use absl::string_view for Matcher<> specializations if googletest
// is built with absl support.
#define GTEST_INTERNAL_HAS_STRING_VIEW 1
#include "absl/strings/string_view.h"
namespace testing {
namespace internal {
using StringView = ::absl::string_view;
}  // namespace internal
}  // namespace testing
#else
#if defined(__cpp_lib_string_view) ||             \
    (GTEST_INTERNAL_HAS_INCLUDE(<string_view>) && \
     GTEST_INTERNAL_CPLUSPLUS_LANG >= 201703L)
// Otherwise for C++17 and higher use std::string_view for Matcher<>
// specializations.
#define GTEST_INTERNAL_HAS_STRING_VIEW 1
#include <string_view>
namespace testing {
namespace internal {
using StringView = ::std::string_view;
}  // namespace internal
}  // namespace testing
// The case where absl is configured NOT to alias std::string_view is not
// supported.
#endif  // __cpp_lib_string_view
#endif  // GTEST_HAS_ABSL

#ifndef GTEST_INTERNAL_HAS_STRING_VIEW
#define GTEST_INTERNAL_HAS_STRING_VIEW 0
#endif

#ifdef GTEST_HAS_ABSL
// Always use absl::variant for UniversalPrinter<> specializations if googletest
// is built with absl support.
#define GTEST_INTERNAL_HAS_VARIANT 1
#include "absl/types/variant.h"
namespace testing {
namespace internal {
template <typename... T>
using Variant = ::absl::variant<T...>;
}  // namespace internal
}  // namespace testing
#else
#if defined(__cpp_lib_variant) || (GTEST_INTERNAL_HAS_INCLUDE(<variant>) && \
                                   GTEST_INTERNAL_CPLUSPLUS_LANG >= 201703L)
// Otherwise for C++17 and higher use std::variant for UniversalPrinter<>
// specializations.
#define GTEST_INTERNAL_HAS_VARIANT 1
#include <variant>
namespace testing {
namespace internal {
template <typename... T>
using Variant = ::std::variant<T...>;
}  // namespace internal
}  // namespace testing
// The case where absl is configured NOT to alias std::variant is not supported.
#endif  // __cpp_lib_variant
#endif  // GTEST_HAS_ABSL

#ifndef GTEST_INTERNAL_HAS_VARIANT
#define GTEST_INTERNAL_HAS_VARIANT 0
#endif

#if (defined(__cpp_constexpr) && !defined(__cpp_inline_variables)) || \
    (defined(GTEST_INTERNAL_CPLUSPLUS_LANG) &&                        \
     GTEST_INTERNAL_CPLUSPLUS_LANG < 201703L)
#define GTEST_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL 1
#endif

#endif  // GOOGLETEST_INCLUDE_GTEST_INTERNAL_GTEST_PORT_H_
