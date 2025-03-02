// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This header file defines macros for declaring attributes for functions,
// types, and variables.
//
// These macros are used within Abseil and allow the compiler to optimize, where
// applicable, certain function calls.
//
// Most macros here are exposing GCC or Clang features, and are stubbed out for
// other compilers.
//
// GCC attributes documentation:
//   https://gcc.gnu.org/onlinedocs/gcc-4.7.0/gcc/Function-Attributes.html
//   https://gcc.gnu.org/onlinedocs/gcc-4.7.0/gcc/Variable-Attributes.html
//   https://gcc.gnu.org/onlinedocs/gcc-4.7.0/gcc/Type-Attributes.html
//
// Most attributes in this file are already supported by GCC 4.7. However, some
// of them are not supported in older version of Clang. Thus, we check
// `__has_attribute()` first. If the check fails, we check if we are on GCC and
// assume the attribute exists on GCC (which is verified on GCC 4.7).

// SKIP_ABSL_INLINE_NAMESPACE_CHECK

#ifndef ABSL_BASE_ATTRIBUTES_H_
#define ABSL_BASE_ATTRIBUTES_H_

#include "absl/base/config.h"

// ABSL_HAVE_ATTRIBUTE
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
#define ABSL_HAVE_ATTRIBUTE(x) __has_attribute(x)
#else
#define ABSL_HAVE_ATTRIBUTE(x) 0
#endif

// ABSL_HAVE_CPP_ATTRIBUTE
//
// A function-like feature checking macro that accepts C++11 style attributes.
// It's a wrapper around `__has_cpp_attribute`, defined by ISO C++ SD-6
// (https://en.cppreference.com/w/cpp/experimental/feature_test). If we don't
// find `__has_cpp_attribute`, will evaluate to 0.
#if defined(__cplusplus) && defined(__has_cpp_attribute)
// NOTE: requiring __cplusplus above should not be necessary, but
// works around https://bugs.llvm.org/show_bug.cgi?id=23435.
#define ABSL_HAVE_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define ABSL_HAVE_CPP_ATTRIBUTE(x) 0
#endif

// -----------------------------------------------------------------------------
// Function Attributes
// -----------------------------------------------------------------------------
//
// GCC: https://gcc.gnu.org/onlinedocs/gcc/Function-Attributes.html
// Clang: https://clang.llvm.org/docs/AttributeReference.html

// ABSL_PRINTF_ATTRIBUTE
// ABSL_SCANF_ATTRIBUTE
//
// Tells the compiler to perform `printf` format string checking if the
// compiler supports it; see the 'format' attribute in
// <https://gcc.gnu.org/onlinedocs/gcc-4.7.0/gcc/Function-Attributes.html>.
//
// Note: As the GCC manual states, "[s]ince non-static C++ methods
// have an implicit 'this' argument, the arguments of such methods
// should be counted from two, not one."
#if ABSL_HAVE_ATTRIBUTE(format) || (defined(__GNUC__) && !defined(__clang__))
#define ABSL_PRINTF_ATTRIBUTE(string_index, first_to_check) \
  __attribute__((__format__(__printf__, string_index, first_to_check)))
#define ABSL_SCANF_ATTRIBUTE(string_index, first_to_check) \
  __attribute__((__format__(__scanf__, string_index, first_to_check)))
#else
#define ABSL_PRINTF_ATTRIBUTE(string_index, first_to_check)
#define ABSL_SCANF_ATTRIBUTE(string_index, first_to_check)
#endif

// ABSL_ATTRIBUTE_ALWAYS_INLINE
// ABSL_ATTRIBUTE_NOINLINE
//
// Forces functions to either inline or not inline. Introduced in gcc 3.1.
#if ABSL_HAVE_ATTRIBUTE(always_inline) || \
    (defined(__GNUC__) && !defined(__clang__))
#define ABSL_ATTRIBUTE_ALWAYS_INLINE __attribute__((always_inline))
#define ABSL_HAVE_ATTRIBUTE_ALWAYS_INLINE 1
#else
#define ABSL_ATTRIBUTE_ALWAYS_INLINE
#endif

#if ABSL_HAVE_ATTRIBUTE(noinline) || (defined(__GNUC__) && !defined(__clang__))
#define ABSL_ATTRIBUTE_NOINLINE __attribute__((noinline))
#define ABSL_HAVE_ATTRIBUTE_NOINLINE 1
#else
#define ABSL_ATTRIBUTE_NOINLINE
#endif

// ABSL_ATTRIBUTE_NO_TAIL_CALL
//
// Prevents the compiler from optimizing away stack frames for functions which
// end in a call to another function.
#if ABSL_HAVE_ATTRIBUTE(disable_tail_calls)
#define ABSL_HAVE_ATTRIBUTE_NO_TAIL_CALL 1
#define ABSL_ATTRIBUTE_NO_TAIL_CALL __attribute__((disable_tail_calls))
#elif defined(__GNUC__) && !defined(__clang__) && !defined(__e2k__)
#define ABSL_HAVE_ATTRIBUTE_NO_TAIL_CALL 1
#define ABSL_ATTRIBUTE_NO_TAIL_CALL \
  __attribute__((optimize("no-optimize-sibling-calls")))
#else
#define ABSL_ATTRIBUTE_NO_TAIL_CALL
#define ABSL_HAVE_ATTRIBUTE_NO_TAIL_CALL 0
#endif

// ABSL_ATTRIBUTE_WEAK
//
// Tags a function as weak for the purposes of compilation and linking.
// Weak attributes did not work properly in LLVM's Windows backend before
// 9.0.0, so disable them there. See https://bugs.llvm.org/show_bug.cgi?id=37598
// for further information. Weak attributes do not work across DLL boundary.
// The MinGW compiler doesn't complain about the weak attribute until the link
// step, presumably because Windows doesn't use ELF binaries.
#if (ABSL_HAVE_ATTRIBUTE(weak) ||                                 \
     (defined(__GNUC__) && !defined(__clang__))) &&               \
    (!defined(_WIN32) ||                                          \
     (defined(__clang__) && __clang_major__ >= 9 &&               \
      !defined(ABSL_BUILD_DLL) && !defined(ABSL_CONSUME_DLL))) && \
    !defined(__MINGW32__)
#undef ABSL_ATTRIBUTE_WEAK
#define ABSL_ATTRIBUTE_WEAK __attribute__((weak))
#define ABSL_HAVE_ATTRIBUTE_WEAK 1
#else
#define ABSL_ATTRIBUTE_WEAK
#define ABSL_HAVE_ATTRIBUTE_WEAK 0
#endif

// ABSL_ATTRIBUTE_NONNULL
//
// Tells the compiler either (a) that a particular function parameter
// should be a non-null pointer, or (b) that all pointer arguments should
// be non-null.
//
// Note: As the GCC manual states, "[s]ince non-static C++ methods
// have an implicit 'this' argument, the arguments of such methods
// should be counted from two, not one."
//
// Args are indexed starting at 1.
//
// For non-static class member functions, the implicit `this` argument
// is arg 1, and the first explicit argument is arg 2. For static class member
// functions, there is no implicit `this`, and the first explicit argument is
// arg 1.
//
// Example:
//
//   /* arg_a cannot be null, but arg_b can */
//   void Function(void* arg_a, void* arg_b) ABSL_ATTRIBUTE_NONNULL(1);
//
//   class C {
//     /* arg_a cannot be null, but arg_b can */
//     void Method(void* arg_a, void* arg_b) ABSL_ATTRIBUTE_NONNULL(2);
//
//     /* arg_a cannot be null, but arg_b can */
//     static void StaticMethod(void* arg_a, void* arg_b)
//     ABSL_ATTRIBUTE_NONNULL(1);
//   };
//
// If no arguments are provided, then all pointer arguments should be non-null.
//
//  /* No pointer arguments may be null. */
//  void Function(void* arg_a, void* arg_b, int arg_c) ABSL_ATTRIBUTE_NONNULL();
//
// NOTE: The GCC nonnull attribute actually accepts a list of arguments, but
// ABSL_ATTRIBUTE_NONNULL does not.
#if ABSL_HAVE_ATTRIBUTE(nonnull) || (defined(__GNUC__) && !defined(__clang__))
#define ABSL_ATTRIBUTE_NONNULL(arg_index) __attribute__((nonnull(arg_index)))
#else
#define ABSL_ATTRIBUTE_NONNULL(...)
#endif

// ABSL_ATTRIBUTE_NORETURN
//
// Tells the compiler that a given function never returns.
//
// Deprecated: Prefer the `[[noreturn]]` attribute standardized by C++11 over
// this macro.
#if ABSL_HAVE_ATTRIBUTE(noreturn) || (defined(__GNUC__) && !defined(__clang__))
#define ABSL_ATTRIBUTE_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define ABSL_ATTRIBUTE_NORETURN __declspec(noreturn)
#else
#define ABSL_ATTRIBUTE_NORETURN
#endif

// ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS
//
// Tells the AddressSanitizer (or other memory testing tools) to ignore a given
// function. Useful for cases when a function reads random locations on stack,
// calls _exit from a cloned subprocess, deliberately accesses buffer
// out of bounds or does other scary things with memory.
// NOTE: GCC supports AddressSanitizer(asan) since 4.8.
// https://gcc.gnu.org/gcc-4.8/changes.html
#if defined(ABSL_HAVE_ADDRESS_SANITIZER) && \
    ABSL_HAVE_ATTRIBUTE(no_sanitize_address)
#define ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))
#elif defined(ABSL_HAVE_ADDRESS_SANITIZER) && defined(_MSC_VER) && \
    _MSC_VER >= 1928
// https://docs.microsoft.com/en-us/cpp/cpp/no-sanitize-address
#define ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS __declspec(no_sanitize_address)
#elif defined(ABSL_HAVE_HWADDRESS_SANITIZER) && ABSL_HAVE_ATTRIBUTE(no_sanitize)
// HWAddressSanitizer is a sanitizer similar to AddressSanitizer, which uses CPU
// features to detect similar bugs with less CPU and memory overhead.
// NOTE: GCC supports HWAddressSanitizer(hwasan) since 11.
// https://gcc.gnu.org/gcc-11/changes.html
#define ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS \
  __attribute__((no_sanitize("hwaddress")))
#else
#define ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS
#endif

// ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY
//
// Tells the MemorySanitizer to relax the handling of a given function. All "Use
// of uninitialized value" warnings from such functions will be suppressed, and
// all values loaded from memory will be considered fully initialized.  This
// attribute is similar to the ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS attribute
// above, but deals with initialized-ness rather than addressability issues.
// NOTE: MemorySanitizer(msan) is supported by Clang but not GCC.
#if ABSL_HAVE_ATTRIBUTE(no_sanitize_memory)
#define ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY __attribute__((no_sanitize_memory))
#else
#define ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY
#endif

// ABSL_ATTRIBUTE_NO_SANITIZE_THREAD
//
// Tells the ThreadSanitizer to not instrument a given function.
// NOTE: GCC supports ThreadSanitizer(tsan) since 4.8.
// https://gcc.gnu.org/gcc-4.8/changes.html
#if ABSL_HAVE_ATTRIBUTE(no_sanitize_thread)
#define ABSL_ATTRIBUTE_NO_SANITIZE_THREAD __attribute__((no_sanitize_thread))
#else
#define ABSL_ATTRIBUTE_NO_SANITIZE_THREAD
#endif

// ABSL_ATTRIBUTE_NO_SANITIZE_UNDEFINED
//
// Tells the UndefinedSanitizer to ignore a given function. Useful for cases
// where certain behavior (eg. division by zero) is being used intentionally.
// NOTE: GCC supports UndefinedBehaviorSanitizer(ubsan) since 4.9.
// https://gcc.gnu.org/gcc-4.9/changes.html
#if ABSL_HAVE_ATTRIBUTE(no_sanitize_undefined)
#define ABSL_ATTRIBUTE_NO_SANITIZE_UNDEFINED \
  __attribute__((no_sanitize_undefined))
#elif ABSL_HAVE_ATTRIBUTE(no_sanitize)
#define ABSL_ATTRIBUTE_NO_SANITIZE_UNDEFINED \
  __attribute__((no_sanitize("undefined")))
#else
#define ABSL_ATTRIBUTE_NO_SANITIZE_UNDEFINED
#endif

// ABSL_ATTRIBUTE_NO_SANITIZE_CFI
//
// Tells the ControlFlowIntegrity sanitizer to not instrument a given function.
// See https://clang.llvm.org/docs/ControlFlowIntegrity.html for details.
#if ABSL_HAVE_ATTRIBUTE(no_sanitize) && defined(__llvm__)
#define ABSL_ATTRIBUTE_NO_SANITIZE_CFI __attribute__((no_sanitize("cfi")))
#else
#define ABSL_ATTRIBUTE_NO_SANITIZE_CFI
#endif

// ABSL_ATTRIBUTE_NO_SANITIZE_SAFESTACK
//
// Tells the SafeStack to not instrument a given function.
// See https://clang.llvm.org/docs/SafeStack.html for details.
#if ABSL_HAVE_ATTRIBUTE(no_sanitize)
#define ABSL_ATTRIBUTE_NO_SANITIZE_SAFESTACK \
  __attribute__((no_sanitize("safe-stack")))
#else
#define ABSL_ATTRIBUTE_NO_SANITIZE_SAFESTACK
#endif

// ABSL_ATTRIBUTE_RETURNS_NONNULL
//
// Tells the compiler that a particular function never returns a null pointer.
#if ABSL_HAVE_ATTRIBUTE(returns_nonnull)
#define ABSL_ATTRIBUTE_RETURNS_NONNULL __attribute__((returns_nonnull))
#else
#define ABSL_ATTRIBUTE_RETURNS_NONNULL
#endif

// ABSL_HAVE_ATTRIBUTE_SECTION
//
// Indicates whether labeled sections are supported. Weak symbol support is
// a prerequisite. Labeled sections are not supported on Darwin/iOS.
#ifdef ABSL_HAVE_ATTRIBUTE_SECTION
#error ABSL_HAVE_ATTRIBUTE_SECTION cannot be directly set
#elif (ABSL_HAVE_ATTRIBUTE(section) ||                \
       (defined(__GNUC__) && !defined(__clang__))) && \
    !defined(__APPLE__) && ABSL_HAVE_ATTRIBUTE_WEAK
#define ABSL_HAVE_ATTRIBUTE_SECTION 1

// ABSL_ATTRIBUTE_SECTION
//
// Tells the compiler/linker to put a given function into a section and define
// `__start_ ## name` and `__stop_ ## name` symbols to bracket the section.
// This functionality is supported by GNU linker.  Any function annotated with
// `ABSL_ATTRIBUTE_SECTION` must not be inlined, or it will be placed into
// whatever section its caller is placed into.
//
#ifndef ABSL_ATTRIBUTE_SECTION
#define ABSL_ATTRIBUTE_SECTION(name) \
  __attribute__((section(#name))) __attribute__((noinline))
#endif

// ABSL_ATTRIBUTE_SECTION_VARIABLE
//
// Tells the compiler/linker to put a given variable into a section and define
// `__start_ ## name` and `__stop_ ## name` symbols to bracket the section.
// This functionality is supported by GNU linker.
#ifndef ABSL_ATTRIBUTE_SECTION_VARIABLE
#ifdef _AIX
// __attribute__((section(#name))) on AIX is achieved by using the `.csect`
// psudo op which includes an additional integer as part of its syntax indcating
// alignment. If data fall under different alignments then you might get a
// compilation error indicating a `Section type conflict`.
#define ABSL_ATTRIBUTE_SECTION_VARIABLE(name)
#else
#define ABSL_ATTRIBUTE_SECTION_VARIABLE(name) __attribute__((section(#name)))
#endif
#endif

// ABSL_DECLARE_ATTRIBUTE_SECTION_VARS
//
// A weak section declaration to be used as a global declaration
// for ABSL_ATTRIBUTE_SECTION_START|STOP(name) to compile and link
// even without functions with ABSL_ATTRIBUTE_SECTION(name).
// ABSL_DEFINE_ATTRIBUTE_SECTION should be in the exactly one file; it's
// a no-op on ELF but not on Mach-O.
//
#ifndef ABSL_DECLARE_ATTRIBUTE_SECTION_VARS
#define ABSL_DECLARE_ATTRIBUTE_SECTION_VARS(name)   \
  extern char __start_##name[] ABSL_ATTRIBUTE_WEAK; \
  extern char __stop_##name[] ABSL_ATTRIBUTE_WEAK
#endif
#ifndef ABSL_DEFINE_ATTRIBUTE_SECTION_VARS
#define ABSL_INIT_ATTRIBUTE_SECTION_VARS(name)
#define ABSL_DEFINE_ATTRIBUTE_SECTION_VARS(name)
#endif

// ABSL_ATTRIBUTE_SECTION_START
//
// Returns `void*` pointers to start/end of a section of code with
// functions having ABSL_ATTRIBUTE_SECTION(name).
// Returns 0 if no such functions exist.
// One must ABSL_DECLARE_ATTRIBUTE_SECTION_VARS(name) for this to compile and
// link.
//
#define ABSL_ATTRIBUTE_SECTION_START(name) \
  (reinterpret_cast<void *>(__start_##name))
#define ABSL_ATTRIBUTE_SECTION_STOP(name) \
  (reinterpret_cast<void *>(__stop_##name))

#else  // !ABSL_HAVE_ATTRIBUTE_SECTION

#define ABSL_HAVE_ATTRIBUTE_SECTION 0

// provide dummy definitions
#define ABSL_ATTRIBUTE_SECTION(name)
#define ABSL_ATTRIBUTE_SECTION_VARIABLE(name)
#define ABSL_INIT_ATTRIBUTE_SECTION_VARS(name)
#define ABSL_DEFINE_ATTRIBUTE_SECTION_VARS(name)
#define ABSL_DECLARE_ATTRIBUTE_SECTION_VARS(name)
#define ABSL_ATTRIBUTE_SECTION_START(name) (reinterpret_cast<void *>(0))
#define ABSL_ATTRIBUTE_SECTION_STOP(name) (reinterpret_cast<void *>(0))

#endif  // ABSL_ATTRIBUTE_SECTION

// ABSL_ATTRIBUTE_STACK_ALIGN_FOR_OLD_LIBC
//
// Support for aligning the stack on 32-bit x86.
#if ABSL_HAVE_ATTRIBUTE(force_align_arg_pointer) || \
    (defined(__GNUC__) && !defined(__clang__))
#if defined(__i386__)
#define ABSL_ATTRIBUTE_STACK_ALIGN_FOR_OLD_LIBC \
  __attribute__((force_align_arg_pointer))
#define ABSL_REQUIRE_STACK_ALIGN_TRAMPOLINE (0)
#elif defined(__x86_64__)
#define ABSL_REQUIRE_STACK_ALIGN_TRAMPOLINE (1)
#define ABSL_ATTRIBUTE_STACK_ALIGN_FOR_OLD_LIBC
#else  // !__i386__ && !__x86_64
#define ABSL_REQUIRE_STACK_ALIGN_TRAMPOLINE (0)
#define ABSL_ATTRIBUTE_STACK_ALIGN_FOR_OLD_LIBC
#endif  // __i386__
#else
#define ABSL_ATTRIBUTE_STACK_ALIGN_FOR_OLD_LIBC
#define ABSL_REQUIRE_STACK_ALIGN_TRAMPOLINE (0)
#endif

// ABSL_MUST_USE_RESULT
//
// Tells the compiler to warn about unused results.
//
// For code or headers that are assured to only build with C++17 and up, prefer
// just using the standard `[[nodiscard]]` directly over this macro.
//
// When annotating a function, it must appear as the first part of the
// declaration or definition. The compiler will warn if the return value from
// such a function is unused:
//
//   ABSL_MUST_USE_RESULT Sprocket* AllocateSprocket();
//   AllocateSprocket();  // Triggers a warning.
//
// When annotating a class, it is equivalent to annotating every function which
// returns an instance.
//
//   class ABSL_MUST_USE_RESULT Sprocket {};
//   Sprocket();  // Triggers a warning.
//
//   Sprocket MakeSprocket();
//   MakeSprocket();  // Triggers a warning.
//
// Note that references and pointers are not instances:
//
//   Sprocket* SprocketPointer();
//   SprocketPointer();  // Does *not* trigger a warning.
//
// ABSL_MUST_USE_RESULT allows using cast-to-void to suppress the unused result
// warning. For that, warn_unused_result is used only for clang but not for gcc.
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66425
//
// Note: past advice was to place the macro after the argument list.
//
// TODO(b/176172494): Use ABSL_HAVE_CPP_ATTRIBUTE(nodiscard) when all code is
// compliant with the stricter [[nodiscard]].
#if defined(__clang__) && ABSL_HAVE_ATTRIBUTE(warn_unused_result)
#define ABSL_MUST_USE_RESULT __attribute__((warn_unused_result))
#else
#define ABSL_MUST_USE_RESULT
#endif

// ABSL_ATTRIBUTE_HOT, ABSL_ATTRIBUTE_COLD
//
// Tells GCC that a function is hot or cold. GCC can use this information to
// improve static analysis, i.e. a conditional branch to a cold function
// is likely to be not-taken.
// This annotation is used for function declarations.
//
// Example:
//
//   int foo() ABSL_ATTRIBUTE_HOT;
#if ABSL_HAVE_ATTRIBUTE(hot) || (defined(__GNUC__) && !defined(__clang__))
#define ABSL_ATTRIBUTE_HOT __attribute__((hot))
#else
#define ABSL_ATTRIBUTE_HOT
#endif

#if ABSL_HAVE_ATTRIBUTE(cold) || (defined(__GNUC__) && !defined(__clang__))
#define ABSL_ATTRIBUTE_COLD __attribute__((cold))
#else
#define ABSL_ATTRIBUTE_COLD
#endif

// ABSL_XRAY_ALWAYS_INSTRUMENT, ABSL_XRAY_NEVER_INSTRUMENT, ABSL_XRAY_LOG_ARGS
//
// We define the ABSL_XRAY_ALWAYS_INSTRUMENT and ABSL_XRAY_NEVER_INSTRUMENT
// macro used as an attribute to mark functions that must always or never be
// instrumented by XRay. Currently, this is only supported in Clang/LLVM.
//
// For reference on the LLVM XRay instrumentation, see
// http://llvm.org/docs/XRay.html.
//
// A function with the XRAY_ALWAYS_INSTRUMENT macro attribute in its declaration
// will always get the XRay instrumentation sleds. These sleds may introduce
// some binary size and runtime overhead and must be used sparingly.
//
// These attributes only take effect when the following conditions are met:
//
//   * The file/target is built in at least C++11 mode, with a Clang compiler
//     that supports XRay attributes.
//   * The file/target is built with the -fxray-instrument flag set for the
//     Clang/LLVM compiler.
//   * The function is defined in the translation unit (the compiler honors the
//     attribute in either the definition or the declaration, and must match).
//
// There are cases when, even when building with XRay instrumentation, users
// might want to control specifically which functions are instrumented for a
// particular build using special-case lists provided to the compiler. These
// special case lists are provided to Clang via the
// -fxray-always-instrument=... and -fxray-never-instrument=... flags. The
// attributes in source take precedence over these special-case lists.
//
// To disable the XRay attributes at build-time, users may define
// ABSL_NO_XRAY_ATTRIBUTES. Do NOT define ABSL_NO_XRAY_ATTRIBUTES on specific
// packages/targets, as this may lead to conflicting definitions of functions at
// link-time.
//
// XRay isn't currently supported on Android:
// https://github.com/android/ndk/issues/368
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::xray_always_instrument) && \
    !defined(ABSL_NO_XRAY_ATTRIBUTES) && !defined(__ANDROID__)
#define ABSL_XRAY_ALWAYS_INSTRUMENT [[clang::xray_always_instrument]]
#define ABSL_XRAY_NEVER_INSTRUMENT [[clang::xray_never_instrument]]
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::xray_log_args)
#define ABSL_XRAY_LOG_ARGS(N) \
  [[clang::xray_always_instrument, clang::xray_log_args(N)]]
#else
#define ABSL_XRAY_LOG_ARGS(N) [[clang::xray_always_instrument]]
#endif
#else
#define ABSL_XRAY_ALWAYS_INSTRUMENT
#define ABSL_XRAY_NEVER_INSTRUMENT
#define ABSL_XRAY_LOG_ARGS(N)
#endif

// ABSL_ATTRIBUTE_REINITIALIZES
//
// Indicates that a member function reinitializes the entire object to a known
// state, independent of the previous state of the object.
//
// The clang-tidy check bugprone-use-after-move allows member functions marked
// with this attribute to be called on objects that have been moved from;
// without the attribute, this would result in a use-after-move warning.
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::reinitializes)
#define ABSL_ATTRIBUTE_REINITIALIZES [[clang::reinitializes]]
#else
#define ABSL_ATTRIBUTE_REINITIALIZES
#endif

// -----------------------------------------------------------------------------
// Variable Attributes
// -----------------------------------------------------------------------------

// ABSL_ATTRIBUTE_UNUSED
//
// Prevents the compiler from complaining about variables that appear unused.
//
// For code or headers that are assured to only build with C++17 and up, prefer
// just using the standard '[[maybe_unused]]' directly over this macro.
//
// Due to differences in positioning requirements between the old, compiler
// specific __attribute__ syntax and the now standard [[maybe_unused]], this
// macro does not attempt to take advantage of '[[maybe_unused]]'.
#if ABSL_HAVE_ATTRIBUTE(unused) || (defined(__GNUC__) && !defined(__clang__))
#undef ABSL_ATTRIBUTE_UNUSED
#define ABSL_ATTRIBUTE_UNUSED __attribute__((__unused__))
#else
#define ABSL_ATTRIBUTE_UNUSED
#endif

// ABSL_ATTRIBUTE_INITIAL_EXEC
//
// Tells the compiler to use "initial-exec" mode for a thread-local variable.
// See http://people.redhat.com/drepper/tls.pdf for the gory details.
#if ABSL_HAVE_ATTRIBUTE(tls_model) || (defined(__GNUC__) && !defined(__clang__))
#define ABSL_ATTRIBUTE_INITIAL_EXEC __attribute__((tls_model("initial-exec")))
#else
#define ABSL_ATTRIBUTE_INITIAL_EXEC
#endif

// ABSL_ATTRIBUTE_PACKED
//
// Instructs the compiler not to use natural alignment for a tagged data
// structure, but instead to reduce its alignment to 1.
//
// Therefore, DO NOT APPLY THIS ATTRIBUTE TO STRUCTS CONTAINING ATOMICS. Doing
// so can cause atomic variables to be mis-aligned and silently violate
// atomicity on x86.
//
// This attribute can either be applied to members of a structure or to a
// structure in its entirety. Applying this attribute (judiciously) to a
// structure in its entirety to optimize the memory footprint of very
// commonly-used structs is fine. Do not apply this attribute to a structure in
// its entirety if the purpose is to control the offsets of the members in the
// structure. Instead, apply this attribute only to structure members that need
// it.
//
// When applying ABSL_ATTRIBUTE_PACKED only to specific structure members the
// natural alignment of structure members not annotated is preserved. Aligned
// member accesses are faster than non-aligned member accesses even if the
// targeted microprocessor supports non-aligned accesses.
#if ABSL_HAVE_ATTRIBUTE(packed) || (defined(__GNUC__) && !defined(__clang__))
#define ABSL_ATTRIBUTE_PACKED __attribute__((__packed__))
#else
#define ABSL_ATTRIBUTE_PACKED
#endif

// ABSL_ATTRIBUTE_FUNC_ALIGN
//
// Tells the compiler to align the function start at least to certain
// alignment boundary
#if ABSL_HAVE_ATTRIBUTE(aligned) || (defined(__GNUC__) && !defined(__clang__))
#define ABSL_ATTRIBUTE_FUNC_ALIGN(bytes) __attribute__((aligned(bytes)))
#else
#define ABSL_ATTRIBUTE_FUNC_ALIGN(bytes)
#endif

// ABSL_FALLTHROUGH_INTENDED
//
// Annotates implicit fall-through between switch labels, allowing a case to
// indicate intentional fallthrough and turn off warnings about any lack of a
// `break` statement. The ABSL_FALLTHROUGH_INTENDED macro should be followed by
// a semicolon and can be used in most places where `break` can, provided that
// no statements exist between it and the next switch label.
//
// Example:
//
//  switch (x) {
//    case 40:
//    case 41:
//      if (truth_is_out_there) {
//        ++x;
//        ABSL_FALLTHROUGH_INTENDED;  // Use instead of/along with annotations
//                                    // in comments
//      } else {
//        return x;
//      }
//    case 42:
//      ...
//
// Notes: When supported, GCC and Clang can issue a warning on switch labels
// with unannotated fallthrough using the warning `-Wimplicit-fallthrough`. See
// clang documentation on language extensions for details:
// https://clang.llvm.org/docs/AttributeReference.html#fallthrough-clang-fallthrough
//
// When used with unsupported compilers, the ABSL_FALLTHROUGH_INTENDED macro has
// no effect on diagnostics. In any case this macro has no effect on runtime
// behavior and performance of code.

#ifdef ABSL_FALLTHROUGH_INTENDED
#error "ABSL_FALLTHROUGH_INTENDED should not be defined."
#elif ABSL_HAVE_CPP_ATTRIBUTE(fallthrough)
#define ABSL_FALLTHROUGH_INTENDED [[fallthrough]]
#elif ABSL_HAVE_CPP_ATTRIBUTE(clang::fallthrough)
#define ABSL_FALLTHROUGH_INTENDED [[clang::fallthrough]]
#elif ABSL_HAVE_CPP_ATTRIBUTE(gnu::fallthrough)
#define ABSL_FALLTHROUGH_INTENDED [[gnu::fallthrough]]
#else
#define ABSL_FALLTHROUGH_INTENDED \
  do {                            \
  } while (0)
#endif

// ABSL_DEPRECATED()
//
// Marks a deprecated class, struct, enum, function, method and variable
// declarations. The macro argument is used as a custom diagnostic message (e.g.
// suggestion of a better alternative).
//
// For code or headers that are assured to only build with C++14 and up, prefer
// just using the standard `[[deprecated("message")]]` directly over this macro.
//
// Examples:
//
//   class ABSL_DEPRECATED("Use Bar instead") Foo {...};
//
//   ABSL_DEPRECATED("Use Baz() instead") void Bar() {...}
//
//   template <typename T>
//   ABSL_DEPRECATED("Use DoThat() instead")
//   void DoThis();
//
//   enum FooEnum {
//     kBar ABSL_DEPRECATED("Use kBaz instead"),
//   };
//
// Every usage of a deprecated entity will trigger a warning when compiled with
// GCC/Clang's `-Wdeprecated-declarations` option. Google's production toolchain
// turns this warning off by default, instead relying on clang-tidy to report
// new uses of deprecated code.
#if ABSL_HAVE_ATTRIBUTE(deprecated)
#define ABSL_DEPRECATED(message) __attribute__((deprecated(message)))
#else
#define ABSL_DEPRECATED(message)
#endif

// When deprecating Abseil code, it is sometimes necessary to turn off the
// warning within Abseil, until the deprecated code is actually removed. The
// deprecated code can be surrounded with these directives to achieve that
// result.
//
// class ABSL_DEPRECATED("Use Bar instead") Foo;
//
// ABSL_INTERNAL_DISABLE_DEPRECATED_DECLARATION_WARNING
// Baz ComputeBazFromFoo(Foo f);
// ABSL_INTERNAL_RESTORE_DEPRECATED_DECLARATION_WARNING
#if defined(__GNUC__) || defined(__clang__)
// Clang also supports these GCC pragmas.
#define ABSL_INTERNAL_DISABLE_DEPRECATED_DECLARATION_WARNING \
  _Pragma("GCC diagnostic push")             \
  _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define ABSL_INTERNAL_RESTORE_DEPRECATED_DECLARATION_WARNING \
  _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#define ABSL_INTERNAL_DISABLE_DEPRECATED_DECLARATION_WARNING \
  _Pragma("warning(push)") _Pragma("warning(disable: 4996)")
#define ABSL_INTERNAL_RESTORE_DEPRECATED_DECLARATION_WARNING \
  _Pragma("warning(pop)")
#else
#define ABSL_INTERNAL_DISABLE_DEPRECATED_DECLARATION_WARNING
#define ABSL_INTERNAL_RESTORE_DEPRECATED_DECLARATION_WARNING
#endif  // defined(__GNUC__) || defined(__clang__)

// ABSL_CONST_INIT
//
// A variable declaration annotated with the `ABSL_CONST_INIT` attribute will
// not compile (on supported platforms) unless the variable has a constant
// initializer. This is useful for variables with static and thread storage
// duration, because it guarantees that they will not suffer from the so-called
// "static init order fiasco".
//
// This attribute must be placed on the initializing declaration of the
// variable. Some compilers will give a -Wmissing-constinit warning when this
// attribute is placed on some other declaration but missing from the
// initializing declaration.
//
// In some cases (notably with thread_local variables), `ABSL_CONST_INIT` can
// also be used in a non-initializing declaration to tell the compiler that a
// variable is already initialized, reducing overhead that would otherwise be
// incurred by a hidden guard variable. Thus annotating all declarations with
// this attribute is recommended to potentially enhance optimization.
//
// Example:
//
//   class MyClass {
//    public:
//     ABSL_CONST_INIT static MyType my_var;
//   };
//
//   ABSL_CONST_INIT MyType MyClass::my_var = MakeMyType(...);
//
// For code or headers that are assured to only build with C++20 and up, prefer
// just using the standard `constinit` keyword directly over this macro.
//
// Note that this attribute is redundant if the variable is declared constexpr.
#if defined(__cpp_constinit) && __cpp_constinit >= 201907L
#define ABSL_CONST_INIT constinit
#elif ABSL_HAVE_CPP_ATTRIBUTE(clang::require_constant_initialization)
#define ABSL_CONST_INIT [[clang::require_constant_initialization]]
#else
#define ABSL_CONST_INIT
#endif

// ABSL_ATTRIBUTE_PURE_FUNCTION
//
// ABSL_ATTRIBUTE_PURE_FUNCTION is used to annotate declarations of "pure"
// functions. A function is pure if its return value is only a function of its
// arguments. The pure attribute prohibits a function from modifying the state
// of the program that is observable by means other than inspecting the
// function's return value. Declaring such functions with the pure attribute
// allows the compiler to avoid emitting some calls in repeated invocations of
// the function with the same argument values.
//
// Example:
//
//  ABSL_ATTRIBUTE_PURE_FUNCTION std::string FormatTime(Time t);
#if ABSL_HAVE_CPP_ATTRIBUTE(gnu::pure)
#define ABSL_ATTRIBUTE_PURE_FUNCTION [[gnu::pure]]
#elif ABSL_HAVE_ATTRIBUTE(pure)
#define ABSL_ATTRIBUTE_PURE_FUNCTION __attribute__((pure))
#else
// If the attribute isn't defined, we'll fallback to ABSL_MUST_USE_RESULT since
// pure functions are useless if its return is ignored.
#define ABSL_ATTRIBUTE_PURE_FUNCTION ABSL_MUST_USE_RESULT
#endif

// ABSL_ATTRIBUTE_CONST_FUNCTION
//
// ABSL_ATTRIBUTE_CONST_FUNCTION is used to annotate declarations of "const"
// functions. A const function is similar to a pure function, with one
// exception: Pure functions may return value that depend on a non-volatile
// object that isn't provided as a function argument, while the const function
// is guaranteed to return the same result given the same arguments.
//
// Example:
//
//  ABSL_ATTRIBUTE_CONST_FUNCTION int64_t ToInt64Milliseconds(Duration d);
#if defined(_MSC_VER) && !defined(__clang__)
// Put the MSVC case first since MSVC seems to parse const as a C++ keyword.
#define ABSL_ATTRIBUTE_CONST_FUNCTION ABSL_ATTRIBUTE_PURE_FUNCTION
#elif ABSL_HAVE_CPP_ATTRIBUTE(gnu::const)
#define ABSL_ATTRIBUTE_CONST_FUNCTION [[gnu::const]]
#elif ABSL_HAVE_ATTRIBUTE(const)
#define ABSL_ATTRIBUTE_CONST_FUNCTION __attribute__((const))
#else
// Since const functions are more restrictive pure function, we'll fallback to a
// pure function if the const attribute is not handled.
#define ABSL_ATTRIBUTE_CONST_FUNCTION ABSL_ATTRIBUTE_PURE_FUNCTION
#endif

// ABSL_ATTRIBUTE_LIFETIME_BOUND indicates that a resource owned by a function
// parameter or implicit object parameter is retained by the return value of the
// annotated function (or, for a parameter of a constructor, in the value of the
// constructed object). This attribute causes warnings to be produced if a
// temporary object does not live long enough.
//
// When applied to a reference parameter, the referenced object is assumed to be
// retained by the return value of the function. When applied to a non-reference
// parameter (for example, a pointer or a class type), all temporaries
// referenced by the parameter are assumed to be retained by the return value of
// the function.
//
// See also the upstream documentation:
// https://clang.llvm.org/docs/AttributeReference.html#lifetimebound
// https://learn.microsoft.com/en-us/cpp/code-quality/c26816?view=msvc-170
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::lifetimebound)
#define ABSL_ATTRIBUTE_LIFETIME_BOUND [[clang::lifetimebound]]
#elif ABSL_HAVE_CPP_ATTRIBUTE(msvc::lifetimebound)
#define ABSL_ATTRIBUTE_LIFETIME_BOUND [[msvc::lifetimebound]]
#elif ABSL_HAVE_ATTRIBUTE(lifetimebound)
#define ABSL_ATTRIBUTE_LIFETIME_BOUND __attribute__((lifetimebound))
#else
#define ABSL_ATTRIBUTE_LIFETIME_BOUND
#endif

// Internal attribute; name and documentation TBD.
//
// See the upstream documentation:
// https://clang.llvm.org/docs/AttributeReference.html#lifetime_capture_by
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::lifetime_capture_by)
#define ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY(Owner) \
  [[clang::lifetime_capture_by(Owner)]]
#else
#define ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY(Owner)
#endif

// ABSL_ATTRIBUTE_VIEW indicates that a type is solely a "view" of data that it
// points to, similarly to a span, string_view, or other non-owning reference
// type.
// This enables diagnosing certain lifetime issues similar to those enabled by
// ABSL_ATTRIBUTE_LIFETIME_BOUND, such as:
//
//   struct ABSL_ATTRIBUTE_VIEW StringView {
//     template<class R>
//     StringView(const R&);
//   };
//
//   StringView f(std::string s) {
//     return s;  // warning: address of stack memory returned
//   }
//
// We disable this on Clang versions < 13 because of the following
// false-positive:
//
//   absl::string_view f(absl::optional<absl::string_view> sv) { return *sv; }
//
// See the following links for details:
// https://reviews.llvm.org/D64448
// https://lists.llvm.org/pipermail/cfe-dev/2018-November/060355.html
#if ABSL_HAVE_CPP_ATTRIBUTE(gsl::Pointer) && \
    (!defined(__clang_major__) || __clang_major__ >= 13)
#define ABSL_ATTRIBUTE_VIEW [[gsl::Pointer]]
#else
#define ABSL_ATTRIBUTE_VIEW
#endif

// ABSL_ATTRIBUTE_OWNER indicates that a type is a container, smart pointer, or
// similar class that owns all the data that it points to.
// This enables diagnosing certain lifetime issues similar to those enabled by
// ABSL_ATTRIBUTE_LIFETIME_BOUND, such as:
//
//   struct ABSL_ATTRIBUTE_VIEW StringView {
//     template<class R>
//     StringView(const R&);
//   };
//
//   struct ABSL_ATTRIBUTE_OWNER String {};
//
//   StringView f(String s) {
//     return s;  // warning: address of stack memory returned
//   }
//
// We disable this on Clang versions < 13 because of the following
// false-positive:
//
//   absl::string_view f(absl::optional<absl::string_view> sv) { return *sv; }
//
// See the following links for details:
// https://reviews.llvm.org/D64448
// https://lists.llvm.org/pipermail/cfe-dev/2018-November/060355.html
#if ABSL_HAVE_CPP_ATTRIBUTE(gsl::Owner) && \
    (!defined(__clang_major__) || __clang_major__ >= 13)
#define ABSL_ATTRIBUTE_OWNER [[gsl::Owner]]
#else
#define ABSL_ATTRIBUTE_OWNER
#endif

// ABSL_ATTRIBUTE_TRIVIAL_ABI
// Indicates that a type is "trivially relocatable" -- meaning it can be
// relocated without invoking the constructor/destructor, using a form of move
// elision.
//
// From a memory safety point of view, putting aside destructor ordering, it's
// safe to apply ABSL_ATTRIBUTE_TRIVIAL_ABI if an object's location
// can change over the course of its lifetime: if a constructor can be run one
// place, and then the object magically teleports to another place where some
// methods are run, and then the object teleports to yet another place where it
// is destroyed. This is notably not true for self-referential types, where the
// move-constructor must keep the self-reference up to date. If the type changed
// location without invoking the move constructor, it would have a dangling
// self-reference.
//
// The use of this teleporting machinery means that the number of paired
// move/destroy operations can change, and so it is a bad idea to apply this to
// a type meant to count the number of moves.
//
// Warning: applying this can, rarely, break callers. Objects passed by value
// will be destroyed at the end of the call, instead of the end of the
// full-expression containing the call. In addition, it changes the ABI
// of functions accepting this type by value (e.g. to pass in registers).
//
// See also the upstream documentation:
// https://clang.llvm.org/docs/AttributeReference.html#trivial-abi
//
// b/321691395 - This is currently disabled in open-source builds since
// compiler support differs. If system libraries compiled with GCC are mixed
// with libraries compiled with Clang, types will have different ideas about
// their ABI, leading to hard to debug crashes.
#define ABSL_ATTRIBUTE_TRIVIAL_ABI

// ABSL_ATTRIBUTE_NO_UNIQUE_ADDRESS
//
// Indicates a data member can be optimized to occupy no space (if it is empty)
// and/or its tail padding can be used for other members.
//
// For code that is assured to only build with C++20 or later, prefer using
// the standard attribute `[[no_unique_address]]` directly instead of this
// macro.
//
// https://devblogs.microsoft.com/cppblog/msvc-cpp20-and-the-std-cpp20-switch/#c20-no_unique_address
// Current versions of MSVC have disabled `[[no_unique_address]]` since it
// breaks ABI compatibility, but offers `[[msvc::no_unique_address]]` for
// situations when it can be assured that it is desired. Since Abseil does not
// claim ABI compatibility in mixed builds, we can offer it unconditionally.
#if defined(_MSC_VER) && _MSC_VER >= 1929
#define ABSL_ATTRIBUTE_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#elif ABSL_HAVE_CPP_ATTRIBUTE(no_unique_address)
#define ABSL_ATTRIBUTE_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define ABSL_ATTRIBUTE_NO_UNIQUE_ADDRESS
#endif

// ABSL_ATTRIBUTE_UNINITIALIZED
//
// GCC and Clang support a flag `-ftrivial-auto-var-init=<option>` (<option>
// can be "zero" or "pattern") that can be used to initialize automatic stack
// variables. Variables with this attribute will be left uninitialized,
// overriding the compiler flag.
//
// See https://clang.llvm.org/docs/AttributeReference.html#uninitialized
// and https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-uninitialized-variable-attribute
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::uninitialized)
#define ABSL_ATTRIBUTE_UNINITIALIZED [[clang::uninitialized]]
#elif ABSL_HAVE_CPP_ATTRIBUTE(gnu::uninitialized)
#define ABSL_ATTRIBUTE_UNINITIALIZED [[gnu::uninitialized]]
#elif ABSL_HAVE_ATTRIBUTE(uninitialized)
#define ABSL_ATTRIBUTE_UNINITIALIZED __attribute__((uninitialized))
#else
#define ABSL_ATTRIBUTE_UNINITIALIZED
#endif

// ABSL_ATTRIBUTE_WARN_UNUSED
//
// Compilers routinely warn about trivial variables that are unused.  For
// non-trivial types, this warning is suppressed since the
// constructor/destructor may be intentional and load-bearing, for example, with
// a RAII scoped lock.
//
// For example:
//
// class ABSL_ATTRIBUTE_WARN_UNUSED MyType {
//  public:
//   MyType();
//   ~MyType();
// };
//
// void foo() {
//   // Warns with ABSL_ATTRIBUTE_WARN_UNUSED attribute present.
//   MyType unused;
// }
//
// See https://clang.llvm.org/docs/AttributeReference.html#warn-unused and
// https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Attributes.html#index-warn_005funused-type-attribute
#if ABSL_HAVE_CPP_ATTRIBUTE(gnu::warn_unused)
#define ABSL_ATTRIBUTE_WARN_UNUSED [[gnu::warn_unused]]
#else
#define ABSL_ATTRIBUTE_WARN_UNUSED
#endif

#endif  // ABSL_BASE_ATTRIBUTES_H_
