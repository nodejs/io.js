// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_COMPILER_SPECIFIC_H_
#define V8_BASE_COMPILER_SPECIFIC_H_

#include "include/v8config.h"

// Annotation to silence compiler warnings about unused
// types/functions/variables. Use like:
//
//   using V8_ALLOW_UNUSED Bar = Foo;
//   V8_ALLOW_UNUSED void foo() {}
#if V8_HAS_ATTRIBUTE_UNUSED
#define V8_ALLOW_UNUSED __attribute__((unused))
#else
#define V8_ALLOW_UNUSED
#endif

// Tell the compiler a function is using a printf-style format string.
// |format_param| is the one-based index of the format string parameter;
// |dots_param| is the one-based index of the "..." parameter.
// For v*printf functions (which take a va_list), pass 0 for dots_param.
// (This is undocumented but matches what the system C headers do.)
#if defined(__GNUC__)
#define PRINTF_FORMAT(format_param, dots_param) \
  __attribute__((format(printf, format_param, dots_param)))
#else
#define PRINTF_FORMAT(format_param, dots_param)
#endif

// The C++ standard requires that static const members have an out-of-class
// definition (in a single compilation unit), but MSVC chokes on this (when
// language extensions, which are required, are enabled). (You're only likely to
// notice the need for a definition if you take the address of the member or,
// more commonly, pass it to a function that takes it as a reference argument --
// probably an STL function.) This macro makes MSVC do the right thing. See
// http://msdn.microsoft.com/en-us/library/34h23df8(v=vs.100).aspx for more
// information. Use like:
//
// In .h file:
//   struct Foo {
//     static const int kBar = 5;
//   };
//
// In .cc file:
//   STATIC_CONST_MEMBER_DEFINITION const int Foo::kBar;
#if V8_HAS_DECLSPEC_SELECTANY
#define STATIC_CONST_MEMBER_DEFINITION __declspec(selectany)
#else
#define STATIC_CONST_MEMBER_DEFINITION
#endif

#if V8_CC_MSVC

#include <sal.h>

// Macros for suppressing and disabling warnings on MSVC.
//
// Warning numbers are enumerated at:
// http://msdn.microsoft.com/en-us/library/8x5x43k7(VS.80).aspx
//
// The warning pragma:
// http://msdn.microsoft.com/en-us/library/2c8f766e(VS.80).aspx
//
// Using __pragma instead of #pragma inside macros:
// http://msdn.microsoft.com/en-us/library/d9x1s805.aspx

// MSVC_SUPPRESS_WARNING disables warning |n| for the remainder of the line and
// for the next line of the source file.
#define MSVC_SUPPRESS_WARNING(n) __pragma(warning(suppress : n))

// Allows exporting a class that inherits from a non-exported base class.
// This uses suppress instead of push/pop because the delimiter after the
// declaration (either "," or "{") has to be placed before the pop macro.
//
// Example usage:
// class EXPORT_API Foo : NON_EXPORTED_BASE(public Bar) {
//
// MSVC Compiler warning C4275:
// non dll-interface class 'Bar' used as base for dll-interface class 'Foo'.
// Note that this is intended to be used only when no access to the base class'
// static data is done through derived classes or inline methods. For more info,
// see http://msdn.microsoft.com/en-us/library/3tdb471s(VS.80).aspx
#define NON_EXPORTED_BASE(code) \
  MSVC_SUPPRESS_WARNING(4275)   \
  code

#else  // Not MSVC

#define MSVC_SUPPRESS_WARNING(n)
#define NON_EXPORTED_BASE(code) code

#endif  // V8_CC_MSVC

// Allowing the use of noexcept by removing the keyword on older compilers that
// do not support adding noexcept to default members.
// Disabled on MSVC because constructors of standard containers are not noexcept
// there.
#if ((!defined(V8_CC_GNU) && !defined(V8_CC_MSVC) &&                        \
      !defined(V8_TARGET_ARCH_MIPS64) && !defined(V8_TARGET_ARCH_PPC) &&    \
      !defined(V8_TARGET_ARCH_PPC64) && !defined(V8_TARGET_ARCH_RISCV64) && \
      !defined(V8_TARGET_ARCH_RISCV32)) ||                                  \
     (defined(__clang__) && __cplusplus > 201300L))
#define V8_NOEXCEPT noexcept
#else
#define V8_NOEXCEPT
#endif

// Specify memory alignment for structs, classes, etc.
// Use like:
//   class ALIGNAS(16) MyClass { ... }
//   ALIGNAS(16) int array[4];
//
// In most places you can use the C++11 keyword "alignas", which is preferred.
//
// But compilers have trouble mixing __attribute__((...)) syntax with
// alignas(...) syntax.
//
// Doesn't work in clang or gcc:
//   struct alignas(16) __attribute__((packed)) S { char c; };
// Works in clang but not gcc:
//   struct __attribute__((packed)) alignas(16) S2 { char c; };
// Works in clang and gcc:
//   struct alignas(16) S3 { char c; } __attribute__((packed));
//
// There are also some attributes that must be specified *before* a class
// definition: visibility (used for exporting functions/classes) is one of
// these attributes. This means that it is not possible to use alignas() with a
// class that is marked as exported.
#if defined(V8_CC_MSVC)
#define ALIGNAS(byte_alignment) __declspec(align(byte_alignment))
#else
#define ALIGNAS(byte_alignment) __attribute__((aligned(byte_alignment)))
#endif

// Forces the linker to not GC the section corresponding to the symbol.
#if V8_HAS_ATTRIBUTE_USED && V8_HAS_ATTRIBUTE_RETAIN
#define V8_DONT_STRIP_SYMBOL __attribute__((used, retain))
#else
#define V8_DONT_STRIP_SYMBOL
#endif

#endif  // V8_BASE_COMPILER_SPECIFIC_H_
