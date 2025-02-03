/* auto-generated on 2025-01-08 17:51:07 -0500. Do not edit! */
/* begin file include/simdutf.h */
#ifndef SIMDUTF_H
#define SIMDUTF_H
#include <cstring>

/* begin file include/simdutf/compiler_check.h */
#ifndef SIMDUTF_COMPILER_CHECK_H
#define SIMDUTF_COMPILER_CHECK_H

#ifndef __cplusplus
  #error simdutf requires a C++ compiler
#endif

#ifndef SIMDUTF_CPLUSPLUS
  #if defined(_MSVC_LANG) && !defined(__clang__)
    #define SIMDUTF_CPLUSPLUS (_MSC_VER == 1900 ? 201103L : _MSVC_LANG)
  #else
    #define SIMDUTF_CPLUSPLUS __cplusplus
  #endif
#endif

// C++ 23
#if !defined(SIMDUTF_CPLUSPLUS23) && (SIMDUTF_CPLUSPLUS >= 202302L)
  #define SIMDUTF_CPLUSPLUS23 1
#endif

// C++ 20
#if !defined(SIMDUTF_CPLUSPLUS20) && (SIMDUTF_CPLUSPLUS >= 202002L)
  #define SIMDUTF_CPLUSPLUS20 1
#endif

// C++ 17
#if !defined(SIMDUTF_CPLUSPLUS17) && (SIMDUTF_CPLUSPLUS >= 201703L)
  #define SIMDUTF_CPLUSPLUS17 1
#endif

// C++ 14
#if !defined(SIMDUTF_CPLUSPLUS14) && (SIMDUTF_CPLUSPLUS >= 201402L)
  #define SIMDUTF_CPLUSPLUS14 1
#endif

// C++ 11
#if !defined(SIMDUTF_CPLUSPLUS11) && (SIMDUTF_CPLUSPLUS >= 201103L)
  #define SIMDUTF_CPLUSPLUS11 1
#endif

#ifndef SIMDUTF_CPLUSPLUS11
  #error simdutf requires a compiler compliant with the C++11 standard
#endif

#endif // SIMDUTF_COMPILER_CHECK_H
/* end file include/simdutf/compiler_check.h */
/* begin file include/simdutf/common_defs.h */
#ifndef SIMDUTF_COMMON_DEFS_H
#define SIMDUTF_COMMON_DEFS_H

/* begin file include/simdutf/portability.h */
#ifndef SIMDUTF_PORTABILITY_H
#define SIMDUTF_PORTABILITY_H


#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#ifndef _WIN32
  // strcasecmp, strncasecmp
  #include <strings.h>
#endif

#if defined(__apple_build_version__)
  #if __apple_build_version__ < 14000000
    #define SIMDUTF_SPAN_DISABLED                                              \
      1 // apple-clang/13 doesn't support std::convertible_to
  #endif
#endif

#if SIMDUTF_CPLUSPLUS20
  #include <version>
  #if __cpp_concepts >= 201907L && __cpp_lib_span >= 202002L &&                \
      !defined(SIMDUTF_SPAN_DISABLED)
    #define SIMDUTF_SPAN 1
  #endif
#endif

/**
 * We want to check that it is actually a little endian system at
 * compile-time.
 */

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
  #define SIMDUTF_IS_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#elif defined(_WIN32)
  #define SIMDUTF_IS_BIG_ENDIAN 0
#else
  #if defined(__APPLE__) ||                                                    \
      defined(__FreeBSD__) // defined __BYTE_ORDER__ && defined
                           // __ORDER_BIG_ENDIAN__
    #include <machine/endian.h>
  #elif defined(sun) ||                                                        \
      defined(__sun) // defined(__APPLE__) || defined(__FreeBSD__)
    #include <sys/byteorder.h>
  #else // defined(__APPLE__) || defined(__FreeBSD__)

    #ifdef __has_include
      #if __has_include(<endian.h>)
        #include <endian.h>
      #endif //__has_include(<endian.h>)
    #endif   //__has_include

  #endif // defined(__APPLE__) || defined(__FreeBSD__)

  #ifndef !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__)
    #define SIMDUTF_IS_BIG_ENDIAN 0
  #endif

  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #define SIMDUTF_IS_BIG_ENDIAN 0
  #else // __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #define SIMDUTF_IS_BIG_ENDIAN 1
  #endif // __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

#endif // defined __BYTE_ORDER__ && defined __ORDER_BIG_ENDIAN__

/**
 * At this point in time, SIMDUTF_IS_BIG_ENDIAN is defined.
 */

#ifdef _MSC_VER
  #define SIMDUTF_VISUAL_STUDIO 1
  /**
   * We want to differentiate carefully between
   * clang under visual studio and regular visual
   * studio.
   *
   * Under clang for Windows, we enable:
   *  * target pragmas so that part and only part of the
   *     code gets compiled for advanced instructions.
   *
   */
  #ifdef __clang__
    // clang under visual studio
    #define SIMDUTF_CLANG_VISUAL_STUDIO 1
  #else
    // just regular visual studio (best guess)
    #define SIMDUTF_REGULAR_VISUAL_STUDIO 1
  #endif // __clang__
#endif   // _MSC_VER

#ifdef SIMDUTF_REGULAR_VISUAL_STUDIO
  // https://en.wikipedia.org/wiki/C_alternative_tokens
  // This header should have no effect, except maybe
  // under Visual Studio.
  #include <iso646.h>
#endif

#if (defined(__x86_64__) || defined(_M_AMD64)) && !defined(_M_ARM64EC)
  #define SIMDUTF_IS_X86_64 1
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(_M_ARM64EC)
  #define SIMDUTF_IS_ARM64 1
#elif defined(__PPC64__) || defined(_M_PPC64)
// #define SIMDUTF_IS_PPC64 1
//  The simdutf library does yet support SIMD acceleration under
//  POWER processors. Please see https://github.com/lemire/simdutf/issues/51
#elif defined(__s390__)
// s390 IBM system. Big endian.
#elif (defined(__riscv) || defined(__riscv__)) && __riscv_xlen == 64
  // RISC-V 64-bit
  #define SIMDUTF_IS_RISCV64 1

  // #if __riscv_v_intrinsic >= 1000000
  //   #define SIMDUTF_HAS_RVV_INTRINSICS 1
  //   #define SIMDUTF_HAS_RVV_TARGET_REGION 1
  // #elif ...
  //  Check for special compiler versions that implement pre v1.0 intrinsics
  #if __riscv_v_intrinsic >= 11000
    #define SIMDUTF_HAS_RVV_INTRINSICS 1
  #endif

  #define SIMDUTF_HAS_ZVBB_INTRINSICS                                          \
    0 // there is currently no way to detect this

  #if SIMDUTF_HAS_RVV_INTRINSICS && __riscv_vector &&                          \
      __riscv_v_min_vlen >= 128 && __riscv_v_elen >= 64
    // RISC-V V extension
    #define SIMDUTF_IS_RVV 1
    #if SIMDUTF_HAS_ZVBB_INTRINSICS && __riscv_zvbb >= 1000000
      // RISC-V Vector Basic Bit-manipulation
      #define SIMDUTF_IS_ZVBB 1
    #endif
  #endif

#elif defined(__loongarch_lp64)
  #if defined(__loongarch_sx) && defined(__loongarch_asx)
    #define SIMDUTF_IS_LSX 1
    #define SIMDUTF_IS_LASX 1
  #elif defined(__loongarch_sx)
    #define SIMDUTF_IS_LSX 1
  #endif
#else
  // The simdutf library is designed
  // for 64-bit processors and it seems that you are not
  // compiling for a known 64-bit platform. Please
  // use a 64-bit target such as x64 or 64-bit ARM for best performance.
  #define SIMDUTF_IS_32BITS 1

  // We do not support 32-bit platforms, but it can be
  // handy to identify them.
  #if defined(_M_IX86) || defined(__i386__)
    #define SIMDUTF_IS_X86_32BITS 1
  #elif defined(__arm__) || defined(_M_ARM)
    #define SIMDUTF_IS_ARM_32BITS 1
  #elif defined(__PPC__) || defined(_M_PPC)
    #define SIMDUTF_IS_PPC_32BITS 1
  #endif

#endif // defined(__x86_64__) || defined(_M_AMD64)

#ifdef SIMDUTF_IS_32BITS
  #ifndef SIMDUTF_NO_PORTABILITY_WARNING
  // In the future, we may want to warn users of 32-bit systems that
  // the simdutf does not support accelerated kernels for such systems.
  #endif // SIMDUTF_NO_PORTABILITY_WARNING
#endif   // SIMDUTF_IS_32BITS

// this is almost standard?
#define SIMDUTF_STRINGIFY_IMPLEMENTATION_(a) #a
#define SIMDUTF_STRINGIFY(a) SIMDUTF_STRINGIFY_IMPLEMENTATION_(a)

// Our fast kernels require 64-bit systems.
//
// On 32-bit x86, we lack 64-bit popcnt, lzcnt, blsr instructions.
// Furthermore, the number of SIMD registers is reduced.
//
// On 32-bit ARM, we would have smaller registers.
//
// The simdutf users should still have the fallback kernel. It is
// slower, but it should run everywhere.

//
// Enable valid runtime implementations, and select
// SIMDUTF_BUILTIN_IMPLEMENTATION
//

// We are going to use runtime dispatch.
#ifdef SIMDUTF_IS_X86_64
  #ifdef __clang__
    // clang does not have GCC push pop
    // warning: clang attribute push can't be used within a namespace in clang
    // up til 8.0 so SIMDUTF_TARGET_REGION and SIMDUTF_UNTARGET_REGION must be
    // *outside* of a namespace.
    #define SIMDUTF_TARGET_REGION(T)                                           \
      _Pragma(SIMDUTF_STRINGIFY(clang attribute push(                          \
          __attribute__((target(T))), apply_to = function)))
    #define SIMDUTF_UNTARGET_REGION _Pragma("clang attribute pop")
  #elif defined(__GNUC__)
    // GCC is easier
    #define SIMDUTF_TARGET_REGION(T)                                           \
      _Pragma("GCC push_options") _Pragma(SIMDUTF_STRINGIFY(GCC target(T)))
    #define SIMDUTF_UNTARGET_REGION _Pragma("GCC pop_options")
  #endif // clang then gcc

#endif // x86

// Default target region macros don't do anything.
#ifndef SIMDUTF_TARGET_REGION
  #define SIMDUTF_TARGET_REGION(T)
  #define SIMDUTF_UNTARGET_REGION
#endif

// Is threading enabled?
#if defined(_REENTRANT) || defined(_MT)
  #ifndef SIMDUTF_THREADS_ENABLED
    #define SIMDUTF_THREADS_ENABLED
  #endif
#endif

// workaround for large stack sizes under -O0.
// https://github.com/simdutf/simdutf/issues/691
#ifdef __APPLE__
  #ifndef __OPTIMIZE__
    // Apple systems have small stack sizes in secondary threads.
    // Lack of compiler optimization may generate high stack usage.
    // Users may want to disable threads for safety, but only when
    // in debug mode which we detect by the fact that the __OPTIMIZE__
    // macro is not defined.
    #undef SIMDUTF_THREADS_ENABLED
  #endif
#endif

#ifdef SIMDUTF_VISUAL_STUDIO
  // This is one case where we do not distinguish between
  // regular visual studio and clang under visual studio.
  // clang under Windows has _stricmp (like visual studio) but not strcasecmp
  // (as clang normally has)
  #define simdutf_strcasecmp _stricmp
  #define simdutf_strncasecmp _strnicmp
#else
  // The strcasecmp, strncasecmp, and strcasestr functions do not work with
  // multibyte strings (e.g. UTF-8). So they are only useful for ASCII in our
  // context.
  // https://www.gnu.org/software/libunistring/manual/libunistring.html#char-_002a-strings
  #define simdutf_strcasecmp strcasecmp
  #define simdutf_strncasecmp strncasecmp
#endif

#if defined(__GNUC__) && !defined(__clang__)
  #if __GNUC__ >= 11
    #define SIMDUTF_GCC11ORMORE 1
  #endif //  __GNUC__ >= 11
#endif   // defined(__GNUC__) && !defined(__clang__)

#endif // SIMDUTF_PORTABILITY_H
/* end file include/simdutf/portability.h */
/* begin file include/simdutf/avx512.h */
#ifndef SIMDUTF_AVX512_H_
#define SIMDUTF_AVX512_H_

/*
    It's possible to override AVX512 settings with cmake DCMAKE_CXX_FLAGS.

    All preprocessor directives has form `SIMDUTF_HAS_AVX512{feature}`,
    where a feature is a code name for extensions.

    Please see the listing below to find which are supported.
*/

#ifndef SIMDUTF_HAS_AVX512F
  #if defined(__AVX512F__) && __AVX512F__ == 1
    #define SIMDUTF_HAS_AVX512F 1
  #endif
#endif

#ifndef SIMDUTF_HAS_AVX512DQ
  #if defined(__AVX512DQ__) && __AVX512DQ__ == 1
    #define SIMDUTF_HAS_AVX512DQ 1
  #endif
#endif

#ifndef SIMDUTF_HAS_AVX512IFMA
  #if defined(__AVX512IFMA__) && __AVX512IFMA__ == 1
    #define SIMDUTF_HAS_AVX512IFMA 1
  #endif
#endif

#ifndef SIMDUTF_HAS_AVX512CD
  #if defined(__AVX512CD__) && __AVX512CD__ == 1
    #define SIMDUTF_HAS_AVX512CD 1
  #endif
#endif

#ifndef SIMDUTF_HAS_AVX512BW
  #if defined(__AVX512BW__) && __AVX512BW__ == 1
    #define SIMDUTF_HAS_AVX512BW 1
  #endif
#endif

#ifndef SIMDUTF_HAS_AVX512VL
  #if defined(__AVX512VL__) && __AVX512VL__ == 1
    #define SIMDUTF_HAS_AVX512VL 1
  #endif
#endif

#ifndef SIMDUTF_HAS_AVX512VBMI
  #if defined(__AVX512VBMI__) && __AVX512VBMI__ == 1
    #define SIMDUTF_HAS_AVX512VBMI 1
  #endif
#endif

#ifndef SIMDUTF_HAS_AVX512VBMI2
  #if defined(__AVX512VBMI2__) && __AVX512VBMI2__ == 1
    #define SIMDUTF_HAS_AVX512VBMI2 1
  #endif
#endif

#ifndef SIMDUTF_HAS_AVX512VNNI
  #if defined(__AVX512VNNI__) && __AVX512VNNI__ == 1
    #define SIMDUTF_HAS_AVX512VNNI 1
  #endif
#endif

#ifndef SIMDUTF_HAS_AVX512BITALG
  #if defined(__AVX512BITALG__) && __AVX512BITALG__ == 1
    #define SIMDUTF_HAS_AVX512BITALG 1
  #endif
#endif

#ifndef SIMDUTF_HAS_AVX512VPOPCNTDQ
  #if defined(__AVX512VPOPCNTDQ__) && __AVX512VPOPCNTDQ__ == 1
    #define SIMDUTF_HAS_AVX512VPOPCNTDQ 1
  #endif
#endif

#endif // SIMDUTF_AVX512_H_
/* end file include/simdutf/avx512.h */

#if defined(SIMDUTF_REGULAR_VISUAL_STUDIO)
  #define SIMDUTF_DEPRECATED __declspec(deprecated)

  #define simdutf_really_inline __forceinline // really inline in release mode
  #define simdutf_always_inline __forceinline // always inline, no matter what
  #define simdutf_never_inline __declspec(noinline)

  #define simdutf_unused
  #define simdutf_warn_unused

  #ifndef simdutf_likely
    #define simdutf_likely(x) x
  #endif
  #ifndef simdutf_unlikely
    #define simdutf_unlikely(x) x
  #endif

  #define SIMDUTF_PUSH_DISABLE_WARNINGS __pragma(warning(push))
  #define SIMDUTF_PUSH_DISABLE_ALL_WARNINGS __pragma(warning(push, 0))
  #define SIMDUTF_DISABLE_VS_WARNING(WARNING_NUMBER)                           \
    __pragma(warning(disable : WARNING_NUMBER))
  // Get rid of Intellisense-only warnings (Code Analysis)
  // Though __has_include is C++17, it is supported in Visual Studio 2017 or
  // better (_MSC_VER>=1910).
  #ifdef __has_include
    #if __has_include(<CppCoreCheck\Warnings.h>)
      #include <CppCoreCheck\Warnings.h>
      #define SIMDUTF_DISABLE_UNDESIRED_WARNINGS                               \
        SIMDUTF_DISABLE_VS_WARNING(ALL_CPPCORECHECK_WARNINGS)
    #endif
  #endif

  #ifndef SIMDUTF_DISABLE_UNDESIRED_WARNINGS
    #define SIMDUTF_DISABLE_UNDESIRED_WARNINGS
  #endif

  #define SIMDUTF_DISABLE_DEPRECATED_WARNING SIMDUTF_DISABLE_VS_WARNING(4996)
  #define SIMDUTF_DISABLE_STRICT_OVERFLOW_WARNING
  #define SIMDUTF_POP_DISABLE_WARNINGS __pragma(warning(pop))

#else // SIMDUTF_REGULAR_VISUAL_STUDIO
  #if defined(__OPTIMIZE__) || defined(NDEBUG)
    #define simdutf_really_inline inline __attribute__((always_inline))
  #else
    #define simdutf_really_inline inline
  #endif
  #define simdutf_always_inline                                                \
    inline __attribute__((always_inline)) // always inline, no matter what
  #define SIMDUTF_DEPRECATED __attribute__((deprecated))
  #define simdutf_never_inline inline __attribute__((noinline))

  #define simdutf_unused __attribute__((unused))
  #define simdutf_warn_unused __attribute__((warn_unused_result))

  #ifndef simdutf_likely
    #define simdutf_likely(x) __builtin_expect(!!(x), 1)
  #endif
  #ifndef simdutf_unlikely
    #define simdutf_unlikely(x) __builtin_expect(!!(x), 0)
  #endif

  // clang-format off
  #define SIMDUTF_PUSH_DISABLE_WARNINGS _Pragma("GCC diagnostic push")
  // gcc doesn't seem to disable all warnings with all and extra, add warnings
  // here as necessary
  #define SIMDUTF_PUSH_DISABLE_ALL_WARNINGS                                    \
    SIMDUTF_PUSH_DISABLE_WARNINGS                                              \
    SIMDUTF_DISABLE_GCC_WARNING(-Weffc++)                                      \
    SIMDUTF_DISABLE_GCC_WARNING(-Wall)                                         \
    SIMDUTF_DISABLE_GCC_WARNING(-Wconversion)                                  \
    SIMDUTF_DISABLE_GCC_WARNING(-Wextra)                                       \
    SIMDUTF_DISABLE_GCC_WARNING(-Wattributes)                                  \
    SIMDUTF_DISABLE_GCC_WARNING(-Wimplicit-fallthrough)                        \
    SIMDUTF_DISABLE_GCC_WARNING(-Wnon-virtual-dtor)                            \
    SIMDUTF_DISABLE_GCC_WARNING(-Wreturn-type)                                 \
    SIMDUTF_DISABLE_GCC_WARNING(-Wshadow)                                      \
    SIMDUTF_DISABLE_GCC_WARNING(-Wunused-parameter)                            \
    SIMDUTF_DISABLE_GCC_WARNING(-Wunused-variable)
  #define SIMDUTF_PRAGMA(P) _Pragma(#P)
  #define SIMDUTF_DISABLE_GCC_WARNING(WARNING)                                 \
    SIMDUTF_PRAGMA(GCC diagnostic ignored #WARNING)
  #if defined(SIMDUTF_CLANG_VISUAL_STUDIO)
    #define SIMDUTF_DISABLE_UNDESIRED_WARNINGS                                 \
      SIMDUTF_DISABLE_GCC_WARNING(-Wmicrosoft-include)
  #else
    #define SIMDUTF_DISABLE_UNDESIRED_WARNINGS
  #endif
  #define SIMDUTF_DISABLE_DEPRECATED_WARNING                                   \
    SIMDUTF_DISABLE_GCC_WARNING(-Wdeprecated-declarations)
  #define SIMDUTF_DISABLE_STRICT_OVERFLOW_WARNING                              \
    SIMDUTF_DISABLE_GCC_WARNING(-Wstrict-overflow)
  #define SIMDUTF_POP_DISABLE_WARNINGS _Pragma("GCC diagnostic pop")
  // clang-format on

#endif // MSC_VER

#ifndef SIMDUTF_DLLIMPORTEXPORT
  #if defined(SIMDUTF_VISUAL_STUDIO)
    /**
     * It does not matter here whether you are using
     * the regular visual studio or clang under visual
     * studio.
     */
    #if SIMDUTF_USING_LIBRARY
      #define SIMDUTF_DLLIMPORTEXPORT __declspec(dllimport)
    #else
      #define SIMDUTF_DLLIMPORTEXPORT __declspec(dllexport)
    #endif
  #else
    #define SIMDUTF_DLLIMPORTEXPORT
  #endif
#endif

#endif // SIMDUTF_COMMON_DEFS_H
/* end file include/simdutf/common_defs.h */
/* begin file include/simdutf/encoding_types.h */
#ifndef SIMDUTF_ENCODING_TYPES_H
#define SIMDUTF_ENCODING_TYPES_H
#include <string>

namespace simdutf {

enum encoding_type {
  UTF8 = 1,      // BOM 0xef 0xbb 0xbf
  UTF16_LE = 2,  // BOM 0xff 0xfe
  UTF16_BE = 4,  // BOM 0xfe 0xff
  UTF32_LE = 8,  // BOM 0xff 0xfe 0x00 0x00
  UTF32_BE = 16, // BOM 0x00 0x00 0xfe 0xff
  Latin1 = 32,

  unspecified = 0
};

enum endianness { LITTLE = 0, BIG = 1 };

bool match_system(endianness e);

std::string to_string(encoding_type bom);

// Note that BOM for UTF8 is discouraged.
namespace BOM {

/**
 * Checks for a BOM. If not, returns unspecified
 * @param input         the string to process
 * @param length        the length of the string in code units
 * @return the corresponding encoding
 */

encoding_type check_bom(const uint8_t *byte, size_t length);
encoding_type check_bom(const char *byte, size_t length);
/**
 * Returns the size, in bytes, of the BOM for a given encoding type.
 * Note that UTF8 BOM are discouraged.
 * @param bom         the encoding type
 * @return the size in bytes of the corresponding BOM
 */
size_t bom_byte_size(encoding_type bom);

} // namespace BOM
} // namespace simdutf
#endif
/* end file include/simdutf/encoding_types.h */
/* begin file include/simdutf/error.h */
#ifndef SIMDUTF_ERROR_H
#define SIMDUTF_ERROR_H
namespace simdutf {

enum error_code {
  SUCCESS = 0,
  HEADER_BITS, // Any byte must have fewer than 5 header bits.
  TOO_SHORT,   // The leading byte must be followed by N-1 continuation bytes,
               // where N is the UTF-8 character length This is also the error
               // when the input is truncated.
  TOO_LONG,    // We either have too many consecutive continuation bytes or the
               // string starts with a continuation byte.
  OVERLONG, // The decoded character must be above U+7F for two-byte characters,
            // U+7FF for three-byte characters, and U+FFFF for four-byte
            // characters.
  TOO_LARGE, // The decoded character must be less than or equal to
             // U+10FFFF,less than or equal than U+7F for ASCII OR less than
             // equal than U+FF for Latin1
  SURROGATE, // The decoded character must be not be in U+D800...DFFF (UTF-8 or
             // UTF-32) OR a high surrogate must be followed by a low surrogate
             // and a low surrogate must be preceded by a high surrogate
             // (UTF-16) OR there must be no surrogate at all (Latin1)
  INVALID_BASE64_CHARACTER, // Found a character that cannot be part of a valid
                            // base64 string. This may include a misplaced
                            // padding character ('=').
  BASE64_INPUT_REMAINDER,   // The base64 input terminates with a single
                            // character, excluding padding (=).
  BASE64_EXTRA_BITS,        // The base64 input terminates with non-zero
                            // padding bits.
  OUTPUT_BUFFER_TOO_SMALL,  // The provided buffer is too small.
  OTHER                     // Not related to validation/transcoding.
};

struct result {
  error_code error;
  size_t count; // In case of error, indicates the position of the error. In
                // case of success, indicates the number of code units
                // validated/written.

  simdutf_really_inline result() : error{error_code::SUCCESS}, count{0} {}

  simdutf_really_inline result(error_code err, size_t pos)
      : error{err}, count{pos} {}
};

struct full_result {
  error_code error;
  size_t input_count;
  size_t output_count;

  simdutf_really_inline full_result()
      : error{error_code::SUCCESS}, input_count{0}, output_count{0} {}

  simdutf_really_inline full_result(error_code err, size_t pos_in,
                                    size_t pos_out)
      : error{err}, input_count{pos_in}, output_count{pos_out} {}

  simdutf_really_inline operator result() const noexcept {
    if (error == error_code::SUCCESS ||
        error == error_code::BASE64_INPUT_REMAINDER) {
      return result{error, output_count};
    } else {
      return result{error, input_count};
    }
  }
};

} // namespace simdutf
#endif
/* end file include/simdutf/error.h */

SIMDUTF_PUSH_DISABLE_WARNINGS
SIMDUTF_DISABLE_UNDESIRED_WARNINGS

// Public API
/* begin file include/simdutf/simdutf_version.h */
// /include/simdutf/simdutf_version.h automatically generated by release.py,
// do not change by hand
#ifndef SIMDUTF_SIMDUTF_VERSION_H
#define SIMDUTF_SIMDUTF_VERSION_H

/** The version of simdutf being used (major.minor.revision) */
#define SIMDUTF_VERSION "6.0.3"

namespace simdutf {
enum {
  /**
   * The major version (MAJOR.minor.revision) of simdutf being used.
   */
  SIMDUTF_VERSION_MAJOR = 6,
  /**
   * The minor version (major.MINOR.revision) of simdutf being used.
   */
  SIMDUTF_VERSION_MINOR = 0,
  /**
   * The revision (major.minor.REVISION) of simdutf being used.
   */
  SIMDUTF_VERSION_REVISION = 3
};
} // namespace simdutf

#endif // SIMDUTF_SIMDUTF_VERSION_H
/* end file include/simdutf/simdutf_version.h */
/* begin file include/simdutf/implementation.h */
#ifndef SIMDUTF_IMPLEMENTATION_H
#define SIMDUTF_IMPLEMENTATION_H
#if !defined(SIMDUTF_NO_THREADS)
  #include <atomic>
#endif
#include <string>
#include <vector>
/* begin file include/simdutf/internal/isadetection.h */
/* From
https://github.com/endorno/pytorch/blob/master/torch/lib/TH/generic/simd/simd.h
Highly modified.

Copyright (c) 2016-     Facebook, Inc            (Adam Paszke)
Copyright (c) 2014-     Facebook, Inc            (Soumith Chintala)
Copyright (c) 2011-2014 Idiap Research Institute (Ronan Collobert)
Copyright (c) 2012-2014 Deepmind Technologies    (Koray Kavukcuoglu)
Copyright (c) 2011-2012 NEC Laboratories America (Koray Kavukcuoglu)
Copyright (c) 2011-2013 NYU                      (Clement Farabet)
Copyright (c) 2006-2010 NEC Laboratories America (Ronan Collobert, Leon Bottou,
Iain Melvin, Jason Weston) Copyright (c) 2006      Idiap Research Institute
(Samy Bengio) Copyright (c) 2001-2004 Idiap Research Institute (Ronan Collobert,
Samy Bengio, Johnny Mariethoz)

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

3. Neither the names of Facebook, Deepmind Technologies, NYU, NEC Laboratories
America and IDIAP Research Institute nor the names of its contributors may be
   used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef SIMDutf_INTERNAL_ISADETECTION_H
#define SIMDutf_INTERNAL_ISADETECTION_H

#include <cstdint>
#include <cstdlib>
#if defined(_MSC_VER)
  #include <intrin.h>
#elif defined(HAVE_GCC_GET_CPUID) && defined(USE_GCC_GET_CPUID)
  #include <cpuid.h>
#endif


// RISC-V ISA detection utilities
#if SIMDUTF_IS_RISCV64 && defined(__linux__)
  #include <unistd.h> // for syscall
// We define these ourselves, for backwards compatibility
struct simdutf_riscv_hwprobe {
  int64_t key;
  uint64_t value;
};
  #define simdutf_riscv_hwprobe(...) syscall(258, __VA_ARGS__)
  #define SIMDUTF_RISCV_HWPROBE_KEY_IMA_EXT_0 4
  #define SIMDUTF_RISCV_HWPROBE_IMA_V (1 << 2)
  #define SIMDUTF_RISCV_HWPROBE_EXT_ZVBB (1 << 17)
#endif // SIMDUTF_IS_RISCV64 && defined(__linux__)

namespace simdutf {
namespace internal {

enum instruction_set {
  DEFAULT = 0x0,
  NEON = 0x1,
  AVX2 = 0x4,
  SSE42 = 0x8,
  PCLMULQDQ = 0x10,
  BMI1 = 0x20,
  BMI2 = 0x40,
  ALTIVEC = 0x80,
  AVX512F = 0x100,
  AVX512DQ = 0x200,
  AVX512IFMA = 0x400,
  AVX512PF = 0x800,
  AVX512ER = 0x1000,
  AVX512CD = 0x2000,
  AVX512BW = 0x4000,
  AVX512VL = 0x8000,
  AVX512VBMI2 = 0x10000,
  AVX512VPOPCNTDQ = 0x2000,
  RVV = 0x4000,
  ZVBB = 0x8000,
  LSX = 0x40000,
  LASX = 0x80000,
};

#if defined(__PPC64__)

static inline uint32_t detect_supported_architectures() {
  return instruction_set::ALTIVEC;
}

#elif SIMDUTF_IS_RISCV64

static inline uint32_t detect_supported_architectures() {
  uint32_t host_isa = instruction_set::DEFAULT;
  #if SIMDUTF_IS_RVV
  host_isa |= instruction_set::RVV;
  #endif
  #if SIMDUTF_IS_ZVBB
  host_isa |= instruction_set::ZVBB;
  #endif
  #if defined(__linux__)
  simdutf_riscv_hwprobe probes[] = {{SIMDUTF_RISCV_HWPROBE_KEY_IMA_EXT_0, 0}};
  long ret = simdutf_riscv_hwprobe(&probes, sizeof probes / sizeof *probes, 0,
                                   nullptr, 0);
  if (ret == 0) {
    uint64_t extensions = probes[0].value;
    if (extensions & SIMDUTF_RISCV_HWPROBE_IMA_V)
      host_isa |= instruction_set::RVV;
    if (extensions & SIMDUTF_RISCV_HWPROBE_EXT_ZVBB)
      host_isa |= instruction_set::ZVBB;
  }
  #endif
  #if defined(RUN_IN_SPIKE_SIMULATOR)
  // Proxy Kernel does not implement yet hwprobe syscall
  host_isa |= instruction_set::RVV;
  #endif
  return host_isa;
}

#elif defined(__aarch64__) || defined(_M_ARM64) || defined(_M_ARM64EC)

static inline uint32_t detect_supported_architectures() {
  return instruction_set::NEON;
}

#elif defined(__x86_64__) || defined(_M_AMD64) // x64

namespace {
namespace cpuid_bit {
// Can be found on Intel ISA Reference for CPUID

// EAX = 0x01
constexpr uint32_t pclmulqdq = uint32_t(1)
                               << 1; ///< @private bit  1 of ECX for EAX=0x1
constexpr uint32_t sse42 = uint32_t(1)
                           << 20; ///< @private bit 20 of ECX for EAX=0x1
constexpr uint32_t osxsave =
    (uint32_t(1) << 26) |
    (uint32_t(1) << 27); ///< @private bits 26+27 of ECX for EAX=0x1

// EAX = 0x7f (Structured Extended Feature Flags), ECX = 0x00 (Sub-leaf)
// See: "Table 3-8. Information Returned by CPUID Instruction"
namespace ebx {
constexpr uint32_t bmi1 = uint32_t(1) << 3;
constexpr uint32_t avx2 = uint32_t(1) << 5;
constexpr uint32_t bmi2 = uint32_t(1) << 8;
constexpr uint32_t avx512f = uint32_t(1) << 16;
constexpr uint32_t avx512dq = uint32_t(1) << 17;
constexpr uint32_t avx512ifma = uint32_t(1) << 21;
constexpr uint32_t avx512cd = uint32_t(1) << 28;
constexpr uint32_t avx512bw = uint32_t(1) << 30;
constexpr uint32_t avx512vl = uint32_t(1) << 31;
} // namespace ebx

namespace ecx {
constexpr uint32_t avx512vbmi = uint32_t(1) << 1;
constexpr uint32_t avx512vbmi2 = uint32_t(1) << 6;
constexpr uint32_t avx512vnni = uint32_t(1) << 11;
constexpr uint32_t avx512bitalg = uint32_t(1) << 12;
constexpr uint32_t avx512vpopcnt = uint32_t(1) << 14;
} // namespace ecx
namespace edx {
constexpr uint32_t avx512vp2intersect = uint32_t(1) << 8;
}
namespace xcr0_bit {
constexpr uint64_t avx256_saved = uint64_t(1) << 2; ///< @private bit 2 = AVX
constexpr uint64_t avx512_saved =
    uint64_t(7) << 5; ///< @private bits 5,6,7 = opmask, ZMM_hi256, hi16_ZMM
} // namespace xcr0_bit
} // namespace cpuid_bit
} // namespace

static inline void cpuid(uint32_t *eax, uint32_t *ebx, uint32_t *ecx,
                         uint32_t *edx) {
  #if defined(_MSC_VER)
  int cpu_info[4];
  __cpuidex(cpu_info, *eax, *ecx);
  *eax = cpu_info[0];
  *ebx = cpu_info[1];
  *ecx = cpu_info[2];
  *edx = cpu_info[3];
  #elif defined(HAVE_GCC_GET_CPUID) && defined(USE_GCC_GET_CPUID)
  uint32_t level = *eax;
  __get_cpuid(level, eax, ebx, ecx, edx);
  #else
  uint32_t a = *eax, b, c = *ecx, d;
  asm volatile("cpuid\n\t" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
  *eax = a;
  *ebx = b;
  *ecx = c;
  *edx = d;
  #endif
}

static inline uint64_t xgetbv() {
  #if defined(_MSC_VER)
  return _xgetbv(0);
  #else
  uint32_t xcr0_lo, xcr0_hi;
  asm volatile("xgetbv\n\t" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
  return xcr0_lo | ((uint64_t)xcr0_hi << 32);
  #endif
}

static inline uint32_t detect_supported_architectures() {
  uint32_t eax;
  uint32_t ebx = 0;
  uint32_t ecx = 0;
  uint32_t edx = 0;
  uint32_t host_isa = 0x0;

  // EBX for EAX=0x1
  eax = 0x1;
  cpuid(&eax, &ebx, &ecx, &edx);

  if (ecx & cpuid_bit::sse42) {
    host_isa |= instruction_set::SSE42;
  }

  if (ecx & cpuid_bit::pclmulqdq) {
    host_isa |= instruction_set::PCLMULQDQ;
  }

  if ((ecx & cpuid_bit::osxsave) != cpuid_bit::osxsave) {
    return host_isa;
  }

  // xgetbv for checking if the OS saves registers
  uint64_t xcr0 = xgetbv();

  if ((xcr0 & cpuid_bit::xcr0_bit::avx256_saved) == 0) {
    return host_isa;
  }
  // ECX for EAX=0x7
  eax = 0x7;
  ecx = 0x0; // Sub-leaf = 0
  cpuid(&eax, &ebx, &ecx, &edx);
  if (ebx & cpuid_bit::ebx::avx2) {
    host_isa |= instruction_set::AVX2;
  }
  if (ebx & cpuid_bit::ebx::bmi1) {
    host_isa |= instruction_set::BMI1;
  }
  if (ebx & cpuid_bit::ebx::bmi2) {
    host_isa |= instruction_set::BMI2;
  }
  if (!((xcr0 & cpuid_bit::xcr0_bit::avx512_saved) ==
        cpuid_bit::xcr0_bit::avx512_saved)) {
    return host_isa;
  }
  if (ebx & cpuid_bit::ebx::avx512f) {
    host_isa |= instruction_set::AVX512F;
  }
  if (ebx & cpuid_bit::ebx::avx512bw) {
    host_isa |= instruction_set::AVX512BW;
  }
  if (ebx & cpuid_bit::ebx::avx512cd) {
    host_isa |= instruction_set::AVX512CD;
  }
  if (ebx & cpuid_bit::ebx::avx512dq) {
    host_isa |= instruction_set::AVX512DQ;
  }
  if (ebx & cpuid_bit::ebx::avx512vl) {
    host_isa |= instruction_set::AVX512VL;
  }
  if (ecx & cpuid_bit::ecx::avx512vbmi2) {
    host_isa |= instruction_set::AVX512VBMI2;
  }
  if (ecx & cpuid_bit::ecx::avx512vpopcnt) {
    host_isa |= instruction_set::AVX512VPOPCNTDQ;
  }
  return host_isa;
}
#elif defined(__loongarch__)
  #if defined(__linux__)
    #include <sys/auxv.h>
  // bits/hwcap.h
  // #define HWCAP_LOONGARCH_LSX             (1 << 4)
  // #define HWCAP_LOONGARCH_LASX            (1 << 5)
  #endif

static inline uint32_t detect_supported_architectures() {
  uint32_t host_isa = instruction_set::DEFAULT;
  #if defined(__linux__)
  uint64_t hwcap = 0;
  hwcap = getauxval(AT_HWCAP);
  if (hwcap & HWCAP_LOONGARCH_LSX) {
    host_isa |= instruction_set::LSX;
  }
  if (hwcap & HWCAP_LOONGARCH_LASX) {
    host_isa |= instruction_set::LASX;
  }
  #endif
  return host_isa;
}
#else // fallback

// includes 32-bit ARM.
static inline uint32_t detect_supported_architectures() {
  return instruction_set::DEFAULT;
}

#endif // end SIMD extension detection code

} // namespace internal
} // namespace simdutf

#endif // SIMDutf_INTERNAL_ISADETECTION_H
/* end file include/simdutf/internal/isadetection.h */

#if SIMDUTF_SPAN
  #include <concepts>
  #include <type_traits>
  #include <span>
#endif

namespace simdutf {

#if SIMDUTF_SPAN
/// helpers placed in namespace detail are not a part of the public API
namespace detail {
/**
 * matches a byte, in the many ways C++ allows. note that these
 * are all distinct types.
 */
template <typename T>
concept byte_like = std::is_same_v<T, std::byte> ||   //
                    std::is_same_v<T, char> ||        //
                    std::is_same_v<T, signed char> || //
                    std::is_same_v<T, unsigned char>;

template <typename T>
concept is_byte_like = byte_like<std::remove_cvref_t<T>>;

template <typename T>
concept is_pointer = std::is_pointer_v<T>;

/**
 * matches anything that behaves like std::span and points to character-like
 * data such as: std::byte, char, unsigned char, signed char, std::int8_t,
 * std::uint8_t
 */
template <typename T>
concept input_span_of_byte_like = requires(const T &t) {
  { t.size() } noexcept -> std::convertible_to<std::size_t>;
  { t.data() } noexcept -> is_pointer;
  { *t.data() } noexcept -> is_byte_like;
};

template <typename T>
concept is_mutable = !std::is_const_v<std::remove_reference_t<T>>;

/**
 * like span_of_byte_like, but for an output span (intended to be written to)
 */
template <typename T>
concept output_span_of_byte_like = requires(T &t) {
  { t.size() } noexcept -> std::convertible_to<std::size_t>;
  { t.data() } noexcept -> is_pointer;
  { *t.data() } noexcept -> is_byte_like;
  { *t.data() } noexcept -> is_mutable;
};
} // namespace detail
#endif

/**
 * Autodetect the encoding of the input, a single encoding is recommended.
 * E.g., the function might return simdutf::encoding_type::UTF8,
 * simdutf::encoding_type::UTF16_LE, simdutf::encoding_type::UTF16_BE, or
 * simdutf::encoding_type::UTF32_LE.
 *
 * @param input the string to analyze.
 * @param length the length of the string in bytes.
 * @return the detected encoding type
 */
simdutf_warn_unused simdutf::encoding_type
autodetect_encoding(const char *input, size_t length) noexcept;
simdutf_really_inline simdutf_warn_unused simdutf::encoding_type
autodetect_encoding(const uint8_t *input, size_t length) noexcept {
  return autodetect_encoding(reinterpret_cast<const char *>(input), length);
}
#if SIMDUTF_SPAN
/**
 * Autodetect the encoding of the input, a single encoding is recommended.
 * E.g., the function might return simdutf::encoding_type::UTF8,
 * simdutf::encoding_type::UTF16_LE, simdutf::encoding_type::UTF16_BE, or
 * simdutf::encoding_type::UTF32_LE.
 *
 * @param input the string to analyze. can be a anything span-like that has a
 * data() and size() that points to character data: std::string,
 * std::string_view, std::vector<char>, std::span<const std::byte> etc.
 * @return the detected encoding type
 */
simdutf_really_inline simdutf_warn_unused simdutf::encoding_type
autodetect_encoding(
    const detail::input_span_of_byte_like auto &input) noexcept {
  return autodetect_encoding(reinterpret_cast<const char *>(input.data()),
                             input.size());
}
#endif

/**
 * Autodetect the possible encodings of the input in one pass.
 * E.g., if the input might be UTF-16LE or UTF-8, this function returns
 * the value (simdutf::encoding_type::UTF8 | simdutf::encoding_type::UTF16_LE).
 *
 * Overridden by each implementation.
 *
 * @param input the string to analyze.
 * @param length the length of the string in bytes.
 * @return the detected encoding type
 */
simdutf_warn_unused int detect_encodings(const char *input,
                                         size_t length) noexcept;
simdutf_really_inline simdutf_warn_unused int
detect_encodings(const uint8_t *input, size_t length) noexcept {
  return detect_encodings(reinterpret_cast<const char *>(input), length);
}
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused int
detect_encodings(const detail::input_span_of_byte_like auto &input) noexcept {
  return detect_encodings(reinterpret_cast<const char *>(input.data()),
                          input.size());
}
#endif

/**
 * Validate the UTF-8 string. This function may be best when you expect
 * the input to be almost always valid. Otherwise, consider using
 * validate_utf8_with_errors.
 *
 * Overridden by each implementation.
 *
 * @param buf the UTF-8 string to validate.
 * @param len the length of the string in bytes.
 * @return true if and only if the string is valid UTF-8.
 */
simdutf_warn_unused bool validate_utf8(const char *buf, size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused bool
validate_utf8(const detail::input_span_of_byte_like auto &input) noexcept {
  return validate_utf8(reinterpret_cast<const char *>(input.data()),
                       input.size());
}
#endif

/**
 * Validate the UTF-8 string and stop on error.
 *
 * Overridden by each implementation.
 *
 * @param buf the UTF-8 string to validate.
 * @param len the length of the string in bytes.
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of code units validated if
 * successful.
 */
simdutf_warn_unused result validate_utf8_with_errors(const char *buf,
                                                     size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result validate_utf8_with_errors(
    const detail::input_span_of_byte_like auto &input) noexcept {
  return validate_utf8_with_errors(reinterpret_cast<const char *>(input.data()),
                                   input.size());
}
#endif

/**
 * Validate the ASCII string.
 *
 * Overridden by each implementation.
 *
 * @param buf the ASCII string to validate.
 * @param len the length of the string in bytes.
 * @return true if and only if the string is valid ASCII.
 */
simdutf_warn_unused bool validate_ascii(const char *buf, size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused bool
validate_ascii(const detail::input_span_of_byte_like auto &input) noexcept {
  return validate_ascii(reinterpret_cast<const char *>(input.data()),
                        input.size());
}
#endif

/**
 * Validate the ASCII string and stop on error. It might be faster than
 * validate_utf8 when an error is expected to occur early.
 *
 * Overridden by each implementation.
 *
 * @param buf the ASCII string to validate.
 * @param len the length of the string in bytes.
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of code units validated if
 * successful.
 */
simdutf_warn_unused result validate_ascii_with_errors(const char *buf,
                                                      size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result validate_ascii_with_errors(
    const detail::input_span_of_byte_like auto &input) noexcept {
  return validate_ascii_with_errors(
      reinterpret_cast<const char *>(input.data()), input.size());
}
#endif

/**
 * Using native endianness; Validate the UTF-16 string.
 * This function may be best when you expect the input to be almost always
 * valid. Otherwise, consider using validate_utf16_with_errors.
 *
 * Overridden by each implementation.
 *
 * This function is not BOM-aware.
 *
 * @param buf the UTF-16 string to validate.
 * @param len the length of the string in number of 2-byte code units
 * (char16_t).
 * @return true if and only if the string is valid UTF-16.
 */
simdutf_warn_unused bool validate_utf16(const char16_t *buf,
                                        size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused bool
validate_utf16(std::span<const char16_t> input) noexcept {
  return validate_utf16(input.data(), input.size());
}
#endif

/**
 * Validate the UTF-16LE string. This function may be best when you expect
 * the input to be almost always valid. Otherwise, consider using
 * validate_utf16le_with_errors.
 *
 * Overridden by each implementation.
 *
 * This function is not BOM-aware.
 *
 * @param buf the UTF-16LE string to validate.
 * @param len the length of the string in number of 2-byte code units
 * (char16_t).
 * @return true if and only if the string is valid UTF-16LE.
 */
simdutf_warn_unused bool validate_utf16le(const char16_t *buf,
                                          size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused bool
validate_utf16le(std::span<const char16_t> input) noexcept {
  return validate_utf16le(input.data(), input.size());
}
#endif

/**
 * Validate the UTF-16BE string. This function may be best when you expect
 * the input to be almost always valid. Otherwise, consider using
 * validate_utf16be_with_errors.
 *
 * Overridden by each implementation.
 *
 * This function is not BOM-aware.
 *
 * @param buf the UTF-16BE string to validate.
 * @param len the length of the string in number of 2-byte code units
 * (char16_t).
 * @return true if and only if the string is valid UTF-16BE.
 */
simdutf_warn_unused bool validate_utf16be(const char16_t *buf,
                                          size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused bool
validate_utf16be(std::span<const char16_t> input) noexcept {
  return validate_utf16be(input.data(), input.size());
}
#endif

/**
 * Using native endianness; Validate the UTF-16 string and stop on error.
 * It might be faster than validate_utf16 when an error is expected to occur
 * early.
 *
 * Overridden by each implementation.
 *
 * This function is not BOM-aware.
 *
 * @param buf the UTF-16 string to validate.
 * @param len the length of the string in number of 2-byte code units
 * (char16_t).
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of code units validated if
 * successful.
 */
simdutf_warn_unused result validate_utf16_with_errors(const char16_t *buf,
                                                      size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
validate_utf16_with_errors(std::span<const char16_t> input) noexcept {
  return validate_utf16_with_errors(input.data(), input.size());
}
#endif

/**
 * Validate the UTF-16LE string and stop on error. It might be faster than
 * validate_utf16le when an error is expected to occur early.
 *
 * Overridden by each implementation.
 *
 * This function is not BOM-aware.
 *
 * @param buf the UTF-16LE string to validate.
 * @param len the length of the string in number of 2-byte code units
 * (char16_t).
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of code units validated if
 * successful.
 */
simdutf_warn_unused result validate_utf16le_with_errors(const char16_t *buf,
                                                        size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
validate_utf16le_with_errors(std::span<const char16_t> input) noexcept {
  return validate_utf16le_with_errors(input.data(), input.size());
}
#endif

/**
 * Validate the UTF-16BE string and stop on error. It might be faster than
 * validate_utf16be when an error is expected to occur early.
 *
 * Overridden by each implementation.
 *
 * This function is not BOM-aware.
 *
 * @param buf the UTF-16BE string to validate.
 * @param len the length of the string in number of 2-byte code units
 * (char16_t).
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of code units validated if
 * successful.
 */
simdutf_warn_unused result validate_utf16be_with_errors(const char16_t *buf,
                                                        size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
validate_utf16be_with_errors(std::span<const char16_t> input) noexcept {
  return validate_utf16be_with_errors(input.data(), input.size());
}
#endif

/**
 * Validate the UTF-32 string. This function may be best when you expect
 * the input to be almost always valid. Otherwise, consider using
 * validate_utf32_with_errors.
 *
 * Overridden by each implementation.
 *
 * This function is not BOM-aware.
 *
 * @param buf the UTF-32 string to validate.
 * @param len the length of the string in number of 4-byte code units
 * (char32_t).
 * @return true if and only if the string is valid UTF-32.
 */
simdutf_warn_unused bool validate_utf32(const char32_t *buf,
                                        size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused bool
validate_utf32(std::span<const char32_t> input) noexcept {
  return validate_utf32(input.data(), input.size());
}
#endif

/**
 * Validate the UTF-32 string and stop on error. It might be faster than
 * validate_utf32 when an error is expected to occur early.
 *
 * Overridden by each implementation.
 *
 * This function is not BOM-aware.
 *
 * @param buf the UTF-32 string to validate.
 * @param len the length of the string in number of 4-byte code units
 * (char32_t).
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of code units validated if
 * successful.
 */
simdutf_warn_unused result validate_utf32_with_errors(const char32_t *buf,
                                                      size_t len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
validate_utf32_with_errors(std::span<const char32_t> input) noexcept {
  return validate_utf32_with_errors(input.data(), input.size());
}
#endif

/**
 * Convert Latin1 string into UTF8 string.
 *
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the Latin1 string to convert
 * @param length        the length of the string in bytes
 * @param utf8_output   the pointer to buffer that can hold conversion result
 * @return the number of written char; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_latin1_to_utf8(const char *input,
                                                  size_t length,
                                                  char *utf8_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_latin1_to_utf8(
    const detail::input_span_of_byte_like auto &latin1_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_latin1_to_utf8(
      reinterpret_cast<const char *>(latin1_input.data()), latin1_input.size(),
      utf8_output.data());
}
#endif

/**
 * Convert Latin1 string into UTF8 string with output limit.
 *
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the Latin1 string to convert
 * @param length        the length of the string in bytes
 * @param utf8_output  	the pointer to buffer that can hold conversion result
 * @param utf8_len      the maximum output length
 * @return the number of written char; 0 if conversion is not possible
 */
simdutf_warn_unused size_t
convert_latin1_to_utf8_safe(const char *input, size_t length, char *utf8_output,
                            size_t utf8_len) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_latin1_to_utf8_safe(
    const detail::input_span_of_byte_like auto &input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  // implementation note: outputspan is a forwarding ref to avoid copying and
  // allow both lvalues and rvalues. std::span can be copied without problems,
  // but std::vector should not, and this function should accept both. it will
  // allow using an owning rvalue ref (example: passing a temporary std::string)
  // as output, but the user will quickly find out that he has no way of getting
  // the data out of the object in that case.
  return convert_latin1_to_utf8_safe(
      input.data(), input.size(), reinterpret_cast<char *>(utf8_output.data()),
      utf8_output.size());
}
#endif

/**
 * Convert possibly Latin1 string into UTF-16LE string.
 *
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the Latin1  string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char16_t; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_latin1_to_utf16le(
    const char *input, size_t length, char16_t *utf16_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_latin1_to_utf16le(
    const detail::input_span_of_byte_like auto &latin1_input,
    std::span<char16_t> utf16_output) noexcept {
  return convert_latin1_to_utf16le(
      reinterpret_cast<const char *>(latin1_input.data()), latin1_input.size(),
      utf16_output.data());
}
#endif

/**
 * Convert Latin1 string into UTF-16BE string.
 *
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the Latin1 string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char16_t; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_latin1_to_utf16be(
    const char *input, size_t length, char16_t *utf16_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_latin1_to_utf16be(const detail::input_span_of_byte_like auto &input,
                          std::span<char16_t> output) noexcept {
  return convert_latin1_to_utf16be(reinterpret_cast<const char *>(input.data()),
                                   input.size(), output.data());
}
#endif

/**
 * Convert Latin1 string into UTF-32 string.
 *
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the Latin1 string to convert
 * @param length        the length of the string in bytes
 * @param utf32_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char32_t; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_latin1_to_utf32(
    const char *input, size_t length, char32_t *utf32_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_latin1_to_utf32(
    const detail::input_span_of_byte_like auto &latin1_input,
    std::span<char32_t> utf32_output) noexcept {
  return convert_latin1_to_utf32(
      reinterpret_cast<const char *>(latin1_input.data()), latin1_input.size(),
      utf32_output.data());
}
#endif

/**
 * Convert possibly broken UTF-8 string into latin1 string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param latin1_output  the pointer to buffer that can hold conversion result
 * @return the number of written char; 0 if the input was not valid UTF-8 string
 * or if it cannot be represented as Latin1
 */
simdutf_warn_unused size_t convert_utf8_to_latin1(const char *input,
                                                  size_t length,
                                                  char *latin1_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_utf8_to_latin1(
    const detail::input_span_of_byte_like auto &input,
    detail::output_span_of_byte_like auto &&output) noexcept {
  return convert_utf8_to_latin1(reinterpret_cast<const char *>(input.data()),
                                input.size(),
                                reinterpret_cast<char *>(output.data()));
}
#endif

/**
 * Using native endianness, convert possibly broken UTF-8 string into a UTF-16
 * string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char16_t; 0 if the input was not valid UTF-8
 * string
 */
simdutf_warn_unused size_t convert_utf8_to_utf16(
    const char *input, size_t length, char16_t *utf16_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_utf8_to_utf16(const detail::input_span_of_byte_like auto &input,
                      std::span<char16_t> output) noexcept {
  return convert_utf8_to_utf16(reinterpret_cast<const char *>(input.data()),
                               input.size(), output.data());
}
#endif

/**
 * Using native endianness, convert a Latin1 string into a UTF-16 string.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char16_t.
 */
simdutf_warn_unused size_t convert_latin1_to_utf16(
    const char *input, size_t length, char16_t *utf16_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_latin1_to_utf16(const detail::input_span_of_byte_like auto &input,
                        std::span<char16_t> output) noexcept {
  return convert_latin1_to_utf16(reinterpret_cast<const char *>(input.data()),
                                 input.size(), output.data());
}
#endif

/**
 * Convert possibly broken UTF-8 string into UTF-16LE string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char16_t; 0 if the input was not valid UTF-8
 * string
 */
simdutf_warn_unused size_t convert_utf8_to_utf16le(
    const char *input, size_t length, char16_t *utf16_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_utf8_to_utf16le(const detail::input_span_of_byte_like auto &utf8_input,
                        std::span<char16_t> utf16_output) noexcept {
  return convert_utf8_to_utf16le(
      reinterpret_cast<const char *>(utf8_input.data()), utf8_input.size(),
      utf16_output.data());
}
#endif

/**
 * Convert possibly broken UTF-8 string into UTF-16BE string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char16_t; 0 if the input was not valid UTF-8
 * string
 */
simdutf_warn_unused size_t convert_utf8_to_utf16be(
    const char *input, size_t length, char16_t *utf16_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_utf8_to_utf16be(const detail::input_span_of_byte_like auto &utf8_input,
                        std::span<char16_t> utf16_output) noexcept {
  return convert_utf8_to_utf16be(
      reinterpret_cast<const char *>(utf8_input.data()), utf8_input.size(),
      utf16_output.data());
}
#endif

/**
 * Convert possibly broken UTF-8 string into latin1 string with errors.
 * If the string cannot be represented as Latin1, an error
 * code is returned.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param latin1_output  the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of code units validated if
 * successful.
 */
simdutf_warn_unused result convert_utf8_to_latin1_with_errors(
    const char *input, size_t length, char *latin1_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf8_to_latin1_with_errors(
    const detail::input_span_of_byte_like auto &utf8_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_utf8_to_latin1_with_errors(
      reinterpret_cast<const char *>(utf8_input.data()), utf8_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Using native endianness, convert possibly broken UTF-8 string into UTF-16
 * string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char16_t written if
 * successful.
 */
simdutf_warn_unused result convert_utf8_to_utf16_with_errors(
    const char *input, size_t length, char16_t *utf16_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf8_to_utf16_with_errors(
    const detail::input_span_of_byte_like auto &utf8_input,
    std::span<char16_t> utf16_output) noexcept {
  return convert_utf8_to_utf16_with_errors(
      reinterpret_cast<const char *>(utf8_input.data()), utf8_input.size(),
      utf16_output.data());
}
#endif

/**
 * Convert possibly broken UTF-8 string into UTF-16LE string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char16_t written if
 * successful.
 */
simdutf_warn_unused result convert_utf8_to_utf16le_with_errors(
    const char *input, size_t length, char16_t *utf16_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf8_to_utf16le_with_errors(
    const detail::input_span_of_byte_like auto &utf8_input,
    std::span<char16_t> utf16_output) noexcept {
  return convert_utf8_to_utf16le_with_errors(
      reinterpret_cast<const char *>(utf8_input.data()), utf8_input.size(),
      utf16_output.data());
}
#endif

/**
 * Convert possibly broken UTF-8 string into UTF-16BE string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char16_t written if
 * successful.
 */
simdutf_warn_unused result convert_utf8_to_utf16be_with_errors(
    const char *input, size_t length, char16_t *utf16_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf8_to_utf16be_with_errors(
    const detail::input_span_of_byte_like auto &utf8_input,
    std::span<char16_t> utf16_output) noexcept {
  return convert_utf8_to_utf16be_with_errors(
      reinterpret_cast<const char *>(utf8_input.data()), utf8_input.size(),
      utf16_output.data());
}
#endif

/**
 * Convert possibly broken UTF-8 string into UTF-32 string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf32_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char32_t; 0 if the input was not valid UTF-8
 * string
 */
simdutf_warn_unused size_t convert_utf8_to_utf32(
    const char *input, size_t length, char32_t *utf32_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_utf8_to_utf32(const detail::input_span_of_byte_like auto &utf8_input,
                      std::span<char32_t> utf32_output) noexcept {
  return convert_utf8_to_utf32(
      reinterpret_cast<const char *>(utf8_input.data()), utf8_input.size(),
      utf32_output.data());
}
#endif

/**
 * Convert possibly broken UTF-8 string into UTF-32 string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf32_buffer  the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char32_t written if
 * successful.
 */
simdutf_warn_unused result convert_utf8_to_utf32_with_errors(
    const char *input, size_t length, char32_t *utf32_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf8_to_utf32_with_errors(
    const detail::input_span_of_byte_like auto &utf8_input,
    std::span<char32_t> utf32_output) noexcept {
  return convert_utf8_to_utf32_with_errors(
      reinterpret_cast<const char *>(utf8_input.data()), utf8_input.size(),
      utf32_output.data());
}
#endif

/**
 * Convert valid UTF-8 string into latin1 string.
 *
 * This function assumes that the input string is valid UTF-8 and that it can be
 * represented as Latin1. If you violate this assumption, the result is
 * implementation defined and may include system-dependent behavior such as
 * crashes.
 *
 * This function is for expert users only and not part of our public API. Use
 * convert_utf8_to_latin1 instead. The function may be removed from the library
 * in the future.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param latin1_output  the pointer to buffer that can hold conversion result
 * @return the number of written char; 0 if the input was not valid UTF-8 string
 */
simdutf_warn_unused size_t convert_valid_utf8_to_latin1(
    const char *input, size_t length, char *latin1_output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_valid_utf8_to_latin1(
    const detail::input_span_of_byte_like auto &valid_utf8_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_valid_utf8_to_latin1(
      reinterpret_cast<const char *>(valid_utf8_input.data()),
      valid_utf8_input.size(), latin1_output.data());
}
#endif

/**
 * Using native endianness, convert valid UTF-8 string into a UTF-16 string.
 *
 * This function assumes that the input string is valid UTF-8.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char16_t
 */
simdutf_warn_unused size_t convert_valid_utf8_to_utf16(
    const char *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_valid_utf8_to_utf16(
    const detail::input_span_of_byte_like auto &valid_utf8_input,
    std::span<char16_t> utf16_output) noexcept {
  return convert_valid_utf8_to_utf16(
      reinterpret_cast<const char *>(valid_utf8_input.data()),
      valid_utf8_input.size(), utf16_output.data());
}
#endif

/**
 * Convert valid UTF-8 string into UTF-16LE string.
 *
 * This function assumes that the input string is valid UTF-8.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char16_t
 */
simdutf_warn_unused size_t convert_valid_utf8_to_utf16le(
    const char *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_valid_utf8_to_utf16le(
    const detail::input_span_of_byte_like auto &valid_utf8_input,
    std::span<char16_t> utf16_output) noexcept {
  return convert_valid_utf8_to_utf16le(
      reinterpret_cast<const char *>(valid_utf8_input.data()),
      valid_utf8_input.size(), utf16_output.data());
}
#endif

/**
 * Convert valid UTF-8 string into UTF-16BE string.
 *
 * This function assumes that the input string is valid UTF-8.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf16_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char16_t
 */
simdutf_warn_unused size_t convert_valid_utf8_to_utf16be(
    const char *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_valid_utf8_to_utf16be(
    const detail::input_span_of_byte_like auto &valid_utf8_input,
    std::span<char16_t> utf16_output) noexcept {
  return convert_valid_utf8_to_utf16be(
      reinterpret_cast<const char *>(valid_utf8_input.data()),
      valid_utf8_input.size(), utf16_output.data());
}
#endif

/**
 * Convert valid UTF-8 string into UTF-32 string.
 *
 * This function assumes that the input string is valid UTF-8.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in bytes
 * @param utf32_buffer  the pointer to buffer that can hold conversion result
 * @return the number of written char32_t
 */
simdutf_warn_unused size_t convert_valid_utf8_to_utf32(
    const char *input, size_t length, char32_t *utf32_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_valid_utf8_to_utf32(
    const detail::input_span_of_byte_like auto &valid_utf8_input,
    std::span<char32_t> utf32_output) noexcept {
  return convert_valid_utf8_to_utf32(
      reinterpret_cast<const char *>(valid_utf8_input.data()),
      valid_utf8_input.size(), utf32_output.data());
}
#endif

/**
 * Return the number of bytes that this Latin1 string would require in UTF-8
 * format.
 *
 * @param input         the Latin1 string to convert
 * @param length        the length of the string bytes
 * @return the number of bytes required to encode the Latin1 string as UTF-8
 */
simdutf_warn_unused size_t utf8_length_from_latin1(const char *input,
                                                   size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t utf8_length_from_latin1(
    const detail::input_span_of_byte_like auto &latin1_input) noexcept {
  return utf8_length_from_latin1(
      reinterpret_cast<const char *>(latin1_input.data()), latin1_input.size());
}
#endif

/**
 * Compute the number of bytes that this UTF-8 string would require in Latin1
 * format.
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-8 strings but in such cases the result is implementation defined.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-8 string to convert
 * @param length        the length of the string in byte
 * @return the number of bytes required to encode the UTF-8 string as Latin1
 */
simdutf_warn_unused size_t latin1_length_from_utf8(const char *input,
                                                   size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t latin1_length_from_utf8(
    const detail::input_span_of_byte_like auto &valid_utf8_input) noexcept {
  return latin1_length_from_utf8(
      reinterpret_cast<const char *>(valid_utf8_input.data()),
      valid_utf8_input.size());
}
#endif

/**
 * Compute the number of 2-byte code units that this UTF-8 string would require
 * in UTF-16LE format.
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-8 strings but in such cases the result is implementation defined.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-8 string to process
 * @param length        the length of the string in bytes
 * @return the number of char16_t code units required to encode the UTF-8 string
 * as UTF-16LE
 */
simdutf_warn_unused size_t utf16_length_from_utf8(const char *input,
                                                  size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t utf16_length_from_utf8(
    const detail::input_span_of_byte_like auto &valid_utf8_input) noexcept {
  return utf16_length_from_utf8(
      reinterpret_cast<const char *>(valid_utf8_input.data()),
      valid_utf8_input.size());
}
#endif

/**
 * Compute the number of 4-byte code units that this UTF-8 string would require
 * in UTF-32 format.
 *
 * This function is equivalent to count_utf8
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-8 strings but in such cases the result is implementation defined.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-8 string to process
 * @param length        the length of the string in bytes
 * @return the number of char32_t code units required to encode the UTF-8 string
 * as UTF-32
 */
simdutf_warn_unused size_t utf32_length_from_utf8(const char *input,
                                                  size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t utf32_length_from_utf8(
    const detail::input_span_of_byte_like auto &valid_utf8_input) noexcept {
  return utf32_length_from_utf8(
      reinterpret_cast<const char *>(valid_utf8_input.data()),
      valid_utf8_input.size());
}
#endif

/**
 * Using native endianness, convert possibly broken UTF-16 string into UTF-8
 * string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf8_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-16LE
 * string
 */
simdutf_warn_unused size_t convert_utf16_to_utf8(const char16_t *input,
                                                 size_t length,
                                                 char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_utf16_to_utf8(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_utf16_to_utf8(utf16_input.data(), utf16_input.size(),
                               reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Using native endianness, convert possibly broken UTF-16 string into Latin1
 * string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param latin1_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-16 string
 * or if it cannot be represented as Latin1
 */
simdutf_warn_unused size_t convert_utf16_to_latin1(
    const char16_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_utf16_to_latin1(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_utf16_to_latin1(
      utf16_input.data(), utf16_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Convert possibly broken UTF-16LE string into Latin1 string.
 * If the string cannot be represented as Latin1, an error
 * is returned.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16LE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param latin1_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-16LE
 * string or if it cannot be represented as Latin1
 */
simdutf_warn_unused size_t convert_utf16le_to_latin1(
    const char16_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_utf16le_to_latin1(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_utf16le_to_latin1(
      utf16_input.data(), utf16_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Convert possibly broken UTF-16BE string into Latin1 string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16BE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param latin1_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-16BE
 * string or if it cannot be represented as Latin1
 */
simdutf_warn_unused size_t convert_utf16be_to_latin1(
    const char16_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_utf16be_to_latin1(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_utf16be_to_latin1(
      utf16_input.data(), utf16_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Convert possibly broken UTF-16LE string into UTF-8 string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16LE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf8_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-16LE
 * string
 */
simdutf_warn_unused size_t convert_utf16le_to_utf8(const char16_t *input,
                                                   size_t length,
                                                   char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_utf16le_to_utf8(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_utf16le_to_utf8(utf16_input.data(), utf16_input.size(),
                                 reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Convert possibly broken UTF-16BE string into UTF-8 string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16BE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf8_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-16LE
 * string
 */
simdutf_warn_unused size_t convert_utf16be_to_utf8(const char16_t *input,
                                                   size_t length,
                                                   char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_utf16be_to_utf8(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_utf16be_to_utf8(utf16_input.data(), utf16_input.size(),
                                 reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Using native endianness, convert possibly broken UTF-16 string into Latin1
 * string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param latin1_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char written if
 * successful.
 */
simdutf_warn_unused result convert_utf16_to_latin1_with_errors(
    const char16_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf16_to_latin1_with_errors(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_utf16_to_latin1_with_errors(
      utf16_input.data(), utf16_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Convert possibly broken UTF-16LE string into Latin1 string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16LE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param latin1_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char written if
 * successful.
 */
simdutf_warn_unused result convert_utf16le_to_latin1_with_errors(
    const char16_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf16le_to_latin1_with_errors(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_utf16le_to_latin1_with_errors(
      utf16_input.data(), utf16_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Convert possibly broken UTF-16BE string into Latin1 string.
 * If the string cannot be represented as Latin1, an error
 * is returned.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16BE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param latin1_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char written if
 * successful.
 */
simdutf_warn_unused result convert_utf16be_to_latin1_with_errors(
    const char16_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf16be_to_latin1_with_errors(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_utf16be_to_latin1_with_errors(
      utf16_input.data(), utf16_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Using native endianness, convert possibly broken UTF-16 string into UTF-8
 * string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf8_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char written if
 * successful.
 */
simdutf_warn_unused result convert_utf16_to_utf8_with_errors(
    const char16_t *input, size_t length, char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf16_to_utf8_with_errors(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_utf16_to_utf8_with_errors(
      utf16_input.data(), utf16_input.size(),
      reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Convert possibly broken UTF-16LE string into UTF-8 string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16LE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf8_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char written if
 * successful.
 */
simdutf_warn_unused result convert_utf16le_to_utf8_with_errors(
    const char16_t *input, size_t length, char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf16le_to_utf8_with_errors(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_utf16le_to_utf8_with_errors(
      utf16_input.data(), utf16_input.size(),
      reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Convert possibly broken UTF-16BE string into UTF-8 string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16BE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf8_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char written if
 * successful.
 */
simdutf_warn_unused result convert_utf16be_to_utf8_with_errors(
    const char16_t *input, size_t length, char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf16be_to_utf8_with_errors(
    std::span<char16_t> utf16_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_utf16be_to_utf8_with_errors(
      utf16_input.data(), utf16_input.size(),
      reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Using native endianness, convert valid UTF-16 string into UTF-8 string.
 *
 * This function assumes that the input string is valid UTF-16LE.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf8_buffer   the pointer to buffer that can hold the conversion
 * result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf16_to_utf8(
    const char16_t *input, size_t length, char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_valid_utf16_to_utf8(
    std::span<char16_t> valid_utf16_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_valid_utf16_to_utf8(
      valid_utf16_input.data(), valid_utf16_input.size(),
      reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Using native endianness, convert UTF-16 string into Latin1 string.
 *
 * This function assumes that the input string is valid UTF-16 and that it can
 * be represented as Latin1. If you violate this assumption, the result is
 * implementation defined and may include system-dependent behavior such as
 * crashes.
 *
 * This function is for expert users only and not part of our public API. Use
 * convert_utf16_to_latin1 instead. The function may be removed from the library
 * in the future.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param latin1_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf16_to_latin1(
    const char16_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_valid_utf16_to_latin1(
    std::span<char16_t> valid_utf16_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_valid_utf16_to_latin1(
      valid_utf16_input.data(), valid_utf16_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Convert valid UTF-16LE string into Latin1 string.
 *
 * This function assumes that the input string is valid UTF-16LE and that it can
 * be represented as Latin1. If you violate this assumption, the result is
 * implementation defined and may include system-dependent behavior such as
 * crashes.
 *
 * This function is for expert users only and not part of our public API. Use
 * convert_utf16le_to_latin1 instead. The function may be removed from the
 * library in the future.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16LE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param latin1_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf16le_to_latin1(
    const char16_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_valid_utf16le_to_latin1(
    std::span<char16_t> valid_utf16_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_valid_utf16le_to_latin1(
      valid_utf16_input.data(), valid_utf16_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Convert valid UTF-16BE string into Latin1 string.
 *
 * This function assumes that the input string is valid UTF-16BE and that it can
 * be represented as Latin1. If you violate this assumption, the result is
 * implementation defined and may include system-dependent behavior such as
 * crashes.
 *
 * This function is for expert users only and not part of our public API. Use
 * convert_utf16be_to_latin1 instead. The function may be removed from the
 * library in the future.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16BE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param latin1_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf16be_to_latin1(
    const char16_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_valid_utf16be_to_latin1(
    std::span<char16_t> valid_utf16_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_valid_utf16be_to_latin1(
      valid_utf16_input.data(), valid_utf16_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Convert valid UTF-16LE string into UTF-8 string.
 *
 * This function assumes that the input string is valid UTF-16LE and that it can
 * be represented as Latin1.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16LE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf8_buffer   the pointer to buffer that can hold the conversion
 * result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf16le_to_utf8(
    const char16_t *input, size_t length, char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_valid_utf16le_to_utf8(
    std::span<char16_t> valid_utf16_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_valid_utf16le_to_utf8(
      valid_utf16_input.data(), valid_utf16_input.size(),
      reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Convert valid UTF-16BE string into UTF-8 string.
 *
 * This function assumes that the input string is valid UTF-16BE.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16BE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf8_buffer   the pointer to buffer that can hold the conversion
 * result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf16be_to_utf8(
    const char16_t *input, size_t length, char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_valid_utf16be_to_utf8(
    std::span<char16_t> valid_utf16_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_valid_utf16be_to_utf8(
      valid_utf16_input.data(), valid_utf16_input.size(),
      reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Using native endianness, convert possibly broken UTF-16 string into UTF-32
 * string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf32_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-16LE
 * string
 */
simdutf_warn_unused size_t convert_utf16_to_utf32(
    const char16_t *input, size_t length, char32_t *utf32_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_utf16_to_utf32(std::span<const char16_t> utf16_input,
                       std::span<char32_t> utf32_output) noexcept {
  return convert_utf16_to_utf32(utf16_input.data(), utf16_input.size(),
                                utf32_output.data());
}
#endif

/**
 * Convert possibly broken UTF-16LE string into UTF-32 string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16LE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf32_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-16LE
 * string
 */
simdutf_warn_unused size_t convert_utf16le_to_utf32(
    const char16_t *input, size_t length, char32_t *utf32_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_utf16le_to_utf32(std::span<const char16_t> utf16_input,
                         std::span<char32_t> utf32_output) noexcept {
  return convert_utf16le_to_utf32(utf16_input.data(), utf16_input.size(),
                                  utf32_output.data());
}
#endif

/**
 * Convert possibly broken UTF-16BE string into UTF-32 string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16BE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf32_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-16LE
 * string
 */
simdutf_warn_unused size_t convert_utf16be_to_utf32(
    const char16_t *input, size_t length, char32_t *utf32_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_utf16be_to_utf32(std::span<const char16_t> utf16_input,
                         std::span<char32_t> utf32_output) noexcept {
  return convert_utf16be_to_utf32(utf16_input.data(), utf16_input.size(),
                                  utf32_output.data());
}
#endif

/**
 * Using native endianness, convert possibly broken UTF-16 string into
 * UTF-32 string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf32_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char32_t written if
 * successful.
 */
simdutf_warn_unused result convert_utf16_to_utf32_with_errors(
    const char16_t *input, size_t length, char32_t *utf32_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf16_to_utf32_with_errors(std::span<const char16_t> utf16_input,
                                   std::span<char32_t> utf32_output) noexcept {
  return convert_utf16_to_utf32_with_errors(
      utf16_input.data(), utf16_input.size(), utf32_output.data());
}
#endif

/**
 * Convert possibly broken UTF-16LE string into UTF-32 string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16LE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf32_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char32_t written if
 * successful.
 */
simdutf_warn_unused result convert_utf16le_to_utf32_with_errors(
    const char16_t *input, size_t length, char32_t *utf32_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf16le_to_utf32_with_errors(
    std::span<const char16_t> utf16_input,
    std::span<char32_t> utf32_output) noexcept {
  return convert_utf16le_to_utf32_with_errors(
      utf16_input.data(), utf16_input.size(), utf32_output.data());
}
#endif

/**
 * Convert possibly broken UTF-16BE string into UTF-32 string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16BE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf32_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char32_t written if
 * successful.
 */
simdutf_warn_unused result convert_utf16be_to_utf32_with_errors(
    const char16_t *input, size_t length, char32_t *utf32_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf16be_to_utf32_with_errors(
    std::span<const char16_t> utf16_input,
    std::span<char32_t> utf32_output) noexcept {
  return convert_utf16be_to_utf32_with_errors(
      utf16_input.data(), utf16_input.size(), utf32_output.data());
}
#endif

/**
 * Using native endianness, convert valid UTF-16 string into UTF-32 string.
 *
 * This function assumes that the input string is valid UTF-16 (native
 * endianness).
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf32_buffer   the pointer to buffer that can hold the conversion
 * result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf16_to_utf32(
    const char16_t *input, size_t length, char32_t *utf32_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_valid_utf16_to_utf32(std::span<const char16_t> valid_utf16_input,
                             std::span<char32_t> utf32_output) noexcept {
  return convert_valid_utf16_to_utf32(
      valid_utf16_input.data(), valid_utf16_input.size(), utf32_output.data());
}
#endif

/**
 * Convert valid UTF-16LE string into UTF-32 string.
 *
 * This function assumes that the input string is valid UTF-16LE.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16LE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf32_buffer   the pointer to buffer that can hold the conversion
 * result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf16le_to_utf32(
    const char16_t *input, size_t length, char32_t *utf32_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_valid_utf16le_to_utf32(std::span<const char16_t> valid_utf16_input,
                               std::span<char32_t> utf32_output) noexcept {
  return convert_valid_utf16le_to_utf32(
      valid_utf16_input.data(), valid_utf16_input.size(), utf32_output.data());
}
#endif

/**
 * Convert valid UTF-16BE string into UTF-32 string.
 *
 * This function assumes that the input string is valid UTF-16LE.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16BE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param utf32_buffer   the pointer to buffer that can hold the conversion
 * result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf16be_to_utf32(
    const char16_t *input, size_t length, char32_t *utf32_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_valid_utf16be_to_utf32(std::span<const char16_t> valid_utf16_input,
                               std::span<char32_t> utf32_output) noexcept {
  return convert_valid_utf16be_to_utf32(
      valid_utf16_input.data(), valid_utf16_input.size(), utf32_output.data());
}
#endif

/**
 * Compute the number of bytes that this UTF-16LE/BE string would require in
 * Latin1 format.
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-16 strings but in such cases the result is implementation defined.
 *
 * This function is not BOM-aware.
 *
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @return the number of bytes required to encode the UTF-16LE string as Latin1
 */
simdutf_warn_unused size_t latin1_length_from_utf16(size_t length) noexcept;

/**
 * Using native endianness; Compute the number of bytes that this UTF-16
 * string would require in UTF-8 format.
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-16 strings but in such cases the result is implementation defined.
 *
 * @param input         the UTF-16 string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @return the number of bytes required to encode the UTF-16LE string as UTF-8
 */
simdutf_warn_unused size_t utf8_length_from_utf16(const char16_t *input,
                                                  size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
utf8_length_from_utf16(std::span<const char16_t> valid_utf16_input) noexcept {
  return utf8_length_from_utf16(valid_utf16_input.data(),
                                valid_utf16_input.size());
}
#endif

/**
 * Compute the number of bytes that this UTF-16LE string would require in UTF-8
 * format.
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-16 strings but in such cases the result is implementation defined.
 *
 * @param input         the UTF-16LE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @return the number of bytes required to encode the UTF-16LE string as UTF-8
 */
simdutf_warn_unused size_t utf8_length_from_utf16le(const char16_t *input,
                                                    size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
utf8_length_from_utf16le(std::span<const char16_t> valid_utf16_input) noexcept {
  return utf8_length_from_utf16le(valid_utf16_input.data(),
                                  valid_utf16_input.size());
}
#endif

/**
 * Compute the number of bytes that this UTF-16BE string would require in UTF-8
 * format.
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-16 strings but in such cases the result is implementation defined.
 *
 * @param input         the UTF-16BE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @return the number of bytes required to encode the UTF-16BE string as UTF-8
 */
simdutf_warn_unused size_t utf8_length_from_utf16be(const char16_t *input,
                                                    size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
utf8_length_from_utf16be(std::span<const char16_t> valid_utf16_input) noexcept {
  return utf8_length_from_utf16be(valid_utf16_input.data(),
                                  valid_utf16_input.size());
}
#endif

/**
 * Convert possibly broken UTF-32 string into UTF-8 string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf8_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-32 string
 */
simdutf_warn_unused size_t convert_utf32_to_utf8(const char32_t *input,
                                                 size_t length,
                                                 char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_utf32_to_utf8(
    std::span<const char32_t> utf32_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_utf32_to_utf8(utf32_input.data(), utf32_input.size(),
                               reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Convert possibly broken UTF-32 string into UTF-8 string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf8_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char written if
 * successful.
 */
simdutf_warn_unused result convert_utf32_to_utf8_with_errors(
    const char32_t *input, size_t length, char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf32_to_utf8_with_errors(
    std::span<const char32_t> utf32_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_utf32_to_utf8_with_errors(
      utf32_input.data(), utf32_input.size(),
      reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Convert valid UTF-32 string into UTF-8 string.
 *
 * This function assumes that the input string is valid UTF-32.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf8_buffer   the pointer to buffer that can hold the conversion
 * result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf32_to_utf8(
    const char32_t *input, size_t length, char *utf8_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_valid_utf32_to_utf8(
    std::span<const char32_t> valid_utf32_input,
    detail::output_span_of_byte_like auto &&utf8_output) noexcept {
  return convert_valid_utf32_to_utf8(
      valid_utf32_input.data(), valid_utf32_input.size(),
      reinterpret_cast<char *>(utf8_output.data()));
}
#endif

/**
 * Using native endianness, convert possibly broken UTF-32 string into a UTF-16
 * string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf16_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-32 string
 */
simdutf_warn_unused size_t convert_utf32_to_utf16(
    const char32_t *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_utf32_to_utf16(std::span<const char32_t> utf32_input,
                       std::span<char16_t> utf16_output) noexcept {
  return convert_utf32_to_utf16(utf32_input.data(), utf32_input.size(),
                                utf16_output.data());
}
#endif

/**
 * Convert possibly broken UTF-32 string into UTF-16LE string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf16_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-32 string
 */
simdutf_warn_unused size_t convert_utf32_to_utf16le(
    const char32_t *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_utf32_to_utf16le(std::span<const char32_t> utf32_input,
                         std::span<char16_t> utf16_output) noexcept {
  return convert_utf32_to_utf16le(utf32_input.data(), utf32_input.size(),
                                  utf16_output.data());
}
#endif

/**
 * Convert possibly broken UTF-32 string into Latin1 string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param latin1_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-32 string
 * or if it cannot be represented as Latin1
 */
simdutf_warn_unused size_t convert_utf32_to_latin1(
    const char32_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_utf32_to_latin1(
    std::span<char32_t> utf32_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_utf32_to_latin1(
      utf32_input.data(), utf32_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Convert possibly broken UTF-32 string into Latin1 string and stop on error.
 * If the string cannot be represented as Latin1, an error is returned.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param latin1_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char written if
 * successful.
 */
simdutf_warn_unused result convert_utf32_to_latin1_with_errors(
    const char32_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf32_to_latin1_with_errors(
    std::span<char32_t> utf32_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_utf32_to_latin1_with_errors(
      utf32_input.data(), utf32_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Convert valid UTF-32 string into Latin1 string.
 *
 * This function assumes that the input string is valid UTF-32 and that it can
 * be represented as Latin1. If you violate this assumption, the result is
 * implementation defined and may include system-dependent behavior such as
 * crashes.
 *
 * This function is for expert users only and not part of our public API. Use
 * convert_utf32_to_latin1 instead. The function may be removed from the library
 * in the future.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param latin1_buffer   the pointer to buffer that can hold the conversion
 * result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf32_to_latin1(
    const char32_t *input, size_t length, char *latin1_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t convert_valid_utf32_to_latin1(
    std::span<char32_t> valid_utf32_input,
    detail::output_span_of_byte_like auto &&latin1_output) noexcept {
  return convert_valid_utf32_to_latin1(
      valid_utf32_input.data(), valid_utf32_input.size(),
      reinterpret_cast<char *>(latin1_output.data()));
}
#endif

/**
 * Convert possibly broken UTF-32 string into UTF-16BE string.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf16_buffer   the pointer to buffer that can hold conversion result
 * @return number of written code units; 0 if input is not a valid UTF-32 string
 */
simdutf_warn_unused size_t convert_utf32_to_utf16be(
    const char32_t *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_utf32_to_utf16be(std::span<const char32_t> utf32_input,
                         std::span<char16_t> utf16_output) noexcept {
  return convert_utf32_to_utf16be(utf32_input.data(), utf32_input.size(),
                                  utf16_output.data());
}
#endif

/**
 * Using native endianness, convert possibly broken UTF-32 string into UTF-16
 * string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf16_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char16_t written if
 * successful.
 */
simdutf_warn_unused result convert_utf32_to_utf16_with_errors(
    const char32_t *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf32_to_utf16_with_errors(std::span<const char32_t> utf32_input,
                                   std::span<char16_t> utf16_output) noexcept {
  return convert_utf32_to_utf16_with_errors(
      utf32_input.data(), utf32_input.size(), utf16_output.data());
}
#endif

/**
 * Convert possibly broken UTF-32 string into UTF-16LE string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf16_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char16_t written if
 * successful.
 */
simdutf_warn_unused result convert_utf32_to_utf16le_with_errors(
    const char32_t *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf32_to_utf16le_with_errors(
    std::span<const char32_t> utf32_input,
    std::span<char16_t> utf16_output) noexcept {
  return convert_utf32_to_utf16le_with_errors(
      utf32_input.data(), utf32_input.size(), utf16_output.data());
}
#endif

/**
 * Convert possibly broken UTF-32 string into UTF-16BE string and stop on error.
 *
 * During the conversion also validation of the input string is done.
 * This function is suitable to work with inputs from untrusted sources.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf16_buffer   the pointer to buffer that can hold conversion result
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in code units) if any, or the number of char16_t written if
 * successful.
 */
simdutf_warn_unused result convert_utf32_to_utf16be_with_errors(
    const char32_t *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result
convert_utf32_to_utf16be_with_errors(
    std::span<const char32_t> utf32_input,
    std::span<char16_t> utf16_output) noexcept {
  return convert_utf32_to_utf16be_with_errors(
      utf32_input.data(), utf32_input.size(), utf16_output.data());
}
#endif

/**
 * Using native endianness, convert valid UTF-32 string into a UTF-16 string.
 *
 * This function assumes that the input string is valid UTF-32.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf16_buffer   the pointer to buffer that can hold the conversion
 * result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf32_to_utf16(
    const char32_t *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_valid_utf32_to_utf16(std::span<const char32_t> valid_utf32_input,
                             std::span<char16_t> utf16_output) noexcept {
  return convert_valid_utf32_to_utf16(
      valid_utf32_input.data(), valid_utf32_input.size(), utf16_output.data());
}
#endif

/**
 * Convert valid UTF-32 string into UTF-16LE string.
 *
 * This function assumes that the input string is valid UTF-32.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf16_buffer   the pointer to buffer that can hold the conversion
 * result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf32_to_utf16le(
    const char32_t *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_valid_utf32_to_utf16le(std::span<const char32_t> valid_utf32_input,
                               std::span<char16_t> utf16_output) noexcept {
  return convert_valid_utf32_to_utf16le(
      valid_utf32_input.data(), valid_utf32_input.size(), utf16_output.data());
}
#endif

/**
 * Convert valid UTF-32 string into UTF-16BE string.
 *
 * This function assumes that the input string is valid UTF-32.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @param utf16_buffer   the pointer to buffer that can hold the conversion
 * result
 * @return number of written code units; 0 if conversion is not possible
 */
simdutf_warn_unused size_t convert_valid_utf32_to_utf16be(
    const char32_t *input, size_t length, char16_t *utf16_buffer) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
convert_valid_utf32_to_utf16be(std::span<const char32_t> valid_utf32_input,
                               std::span<char16_t> utf16_output) noexcept {
  return convert_valid_utf32_to_utf16be(
      valid_utf32_input.data(), valid_utf32_input.size(), utf16_output.data());
}
#endif

/**
 * Change the endianness of the input. Can be used to go from UTF-16LE to
 * UTF-16BE or from UTF-16BE to UTF-16LE.
 *
 * This function does not validate the input.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to process
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @param output        the pointer to buffer that can hold the conversion
 * result
 */
void change_endianness_utf16(const char16_t *input, size_t length,
                             char16_t *output) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline void
change_endianness_utf16(std::span<const char16_t> utf16_input,
                        std::span<char16_t> utf16_output) noexcept {
  return change_endianness_utf16(utf16_input.data(), utf16_input.size(),
                                 utf16_output.data());
}
#endif

/**
 * Compute the number of bytes that this UTF-32 string would require in UTF-8
 * format.
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-32 strings but in such cases the result is implementation defined.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @return the number of bytes required to encode the UTF-32 string as UTF-8
 */
simdutf_warn_unused size_t utf8_length_from_utf32(const char32_t *input,
                                                  size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
utf8_length_from_utf32(std::span<const char32_t> valid_utf32_input) noexcept {
  return utf8_length_from_utf32(valid_utf32_input.data(),
                                valid_utf32_input.size());
}
#endif

/**
 * Compute the number of two-byte code units that this UTF-32 string would
 * require in UTF-16 format.
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-32 strings but in such cases the result is implementation defined.
 *
 * @param input         the UTF-32 string to convert
 * @param length        the length of the string in 4-byte code units (char32_t)
 * @return the number of bytes required to encode the UTF-32 string as UTF-16
 */
simdutf_warn_unused size_t utf16_length_from_utf32(const char32_t *input,
                                                   size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
utf16_length_from_utf32(std::span<const char32_t> valid_utf32_input) noexcept {
  return utf16_length_from_utf32(valid_utf32_input.data(),
                                 valid_utf32_input.size());
}
#endif

/**
 * Using native endianness; Compute the number of bytes that this UTF-16
 * string would require in UTF-32 format.
 *
 * This function is equivalent to count_utf16.
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-16 strings but in such cases the result is implementation defined.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @return the number of bytes required to encode the UTF-16LE string as UTF-32
 */
simdutf_warn_unused size_t utf32_length_from_utf16(const char16_t *input,
                                                   size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
utf32_length_from_utf16(std::span<const char16_t> valid_utf16_input) noexcept {
  return utf32_length_from_utf16(valid_utf16_input.data(),
                                 valid_utf16_input.size());
}
#endif

/**
 * Compute the number of bytes that this UTF-16LE string would require in UTF-32
 * format.
 *
 * This function is equivalent to count_utf16le.
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-16 strings but in such cases the result is implementation defined.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16LE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @return the number of bytes required to encode the UTF-16LE string as UTF-32
 */
simdutf_warn_unused size_t utf32_length_from_utf16le(const char16_t *input,
                                                     size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t utf32_length_from_utf16le(
    std::span<const char16_t> valid_utf16_input) noexcept {
  return utf32_length_from_utf16le(valid_utf16_input.data(),
                                   valid_utf16_input.size());
}
#endif

/**
 * Compute the number of bytes that this UTF-16BE string would require in UTF-32
 * format.
 *
 * This function is equivalent to count_utf16be.
 *
 * This function does not validate the input. It is acceptable to pass invalid
 * UTF-16 strings but in such cases the result is implementation defined.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16BE string to convert
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @return the number of bytes required to encode the UTF-16BE string as UTF-32
 */
simdutf_warn_unused size_t utf32_length_from_utf16be(const char16_t *input,
                                                     size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t utf32_length_from_utf16be(
    std::span<const char16_t> valid_utf16_input) noexcept {
  return utf32_length_from_utf16be(valid_utf16_input.data(),
                                   valid_utf16_input.size());
}
#endif

/**
 * Count the number of code points (characters) in the string assuming that
 * it is valid.
 *
 * This function assumes that the input string is valid UTF-16 (native
 * endianness). It is acceptable to pass invalid UTF-16 strings but in such
 * cases the result is implementation defined.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16 string to process
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @return number of code points
 */
simdutf_warn_unused size_t count_utf16(const char16_t *input,
                                       size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
count_utf16(std::span<const char16_t> valid_utf16_input) noexcept {
  return count_utf16(valid_utf16_input.data(), valid_utf16_input.size());
}
#endif

/**
 * Count the number of code points (characters) in the string assuming that
 * it is valid.
 *
 * This function assumes that the input string is valid UTF-16LE.
 * It is acceptable to pass invalid UTF-16 strings but in such cases
 * the result is implementation defined.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16LE string to process
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @return number of code points
 */
simdutf_warn_unused size_t count_utf16le(const char16_t *input,
                                         size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
count_utf16le(std::span<const char16_t> valid_utf16_input) noexcept {
  return count_utf16le(valid_utf16_input.data(), valid_utf16_input.size());
}
#endif

/**
 * Count the number of code points (characters) in the string assuming that
 * it is valid.
 *
 * This function assumes that the input string is valid UTF-16BE.
 * It is acceptable to pass invalid UTF-16 strings but in such cases
 * the result is implementation defined.
 *
 * This function is not BOM-aware.
 *
 * @param input         the UTF-16BE string to process
 * @param length        the length of the string in 2-byte code units (char16_t)
 * @return number of code points
 */
simdutf_warn_unused size_t count_utf16be(const char16_t *input,
                                         size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
count_utf16be(std::span<const char16_t> valid_utf16_input) noexcept {
  return count_utf16be(valid_utf16_input.data(), valid_utf16_input.size());
}
#endif

/**
 * Count the number of code points (characters) in the string assuming that
 * it is valid.
 *
 * This function assumes that the input string is valid UTF-8.
 * It is acceptable to pass invalid UTF-8 strings but in such cases
 * the result is implementation defined.
 *
 * @param input         the UTF-8 string to process
 * @param length        the length of the string in bytes
 * @return number of code points
 */
simdutf_warn_unused size_t count_utf8(const char *input,
                                      size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t count_utf8(
    const detail::input_span_of_byte_like auto &valid_utf8_input) noexcept {
  return count_utf8(reinterpret_cast<const char *>(valid_utf8_input.data()),
                    valid_utf8_input.size());
}
#endif

/**
 * Given a valid UTF-8 string having a possibly truncated last character,
 * this function checks the end of string. If the last character is truncated
 * (or partial), then it returns a shorter length (shorter by 1 to 3 bytes) so
 * that the short UTF-8 strings only contain complete characters. If there is no
 * truncated character, the original length is returned.
 *
 * This function assumes that the input string is valid UTF-8, but possibly
 * truncated.
 *
 * @param input         the UTF-8 string to process
 * @param length        the length of the string in bytes
 * @return the length of the string in bytes, possibly shorter by 1 to 3 bytes
 */
simdutf_warn_unused size_t trim_partial_utf8(const char *input, size_t length);
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t trim_partial_utf8(
    const detail::input_span_of_byte_like auto &valid_utf8_input) noexcept {
  return trim_partial_utf8(
      reinterpret_cast<const char *>(valid_utf8_input.data()),
      valid_utf8_input.size());
}
#endif

/**
 * Given a valid UTF-16BE string having a possibly truncated last character,
 * this function checks the end of string. If the last character is truncated
 * (or partial), then it returns a shorter length (shorter by 1 unit) so that
 * the short UTF-16BE strings only contain complete characters. If there is no
 * truncated character, the original length is returned.
 *
 * This function assumes that the input string is valid UTF-16BE, but possibly
 * truncated.
 *
 * @param input         the UTF-16BE string to process
 * @param length        the length of the string in bytes
 * @return the length of the string in bytes, possibly shorter by 1 unit
 */
simdutf_warn_unused size_t trim_partial_utf16be(const char16_t *input,
                                                size_t length);
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
trim_partial_utf16be(std::span<const char16_t> valid_utf16_input) noexcept {
  return trim_partial_utf16be(valid_utf16_input.data(),
                              valid_utf16_input.size());
}
#endif

/**
 * Given a valid UTF-16LE string having a possibly truncated last character,
 * this function checks the end of string. If the last character is truncated
 * (or partial), then it returns a shorter length (shorter by 1 unit) so that
 * the short UTF-16LE strings only contain complete characters. If there is no
 * truncated character, the original length is returned.
 *
 * This function assumes that the input string is valid UTF-16LE, but possibly
 * truncated.
 *
 * @param input         the UTF-16LE string to process
 * @param length        the length of the string in bytes
 * @return the length of the string in unit, possibly shorter by 1 unit
 */
simdutf_warn_unused size_t trim_partial_utf16le(const char16_t *input,
                                                size_t length);
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
trim_partial_utf16le(std::span<const char16_t> valid_utf16_input) noexcept {
  return trim_partial_utf16le(valid_utf16_input.data(),
                              valid_utf16_input.size());
}
#endif

/**
 * Given a valid UTF-16 string having a possibly truncated last character,
 * this function checks the end of string. If the last character is truncated
 * (or partial), then it returns a shorter length (shorter by 1 unit) so that
 * the short UTF-16 strings only contain complete characters. If there is no
 * truncated character, the original length is returned.
 *
 * This function assumes that the input string is valid UTF-16, but possibly
 * truncated. We use the native endianness.
 *
 * @param input         the UTF-16 string to process
 * @param length        the length of the string in bytes
 * @return the length of the string in unit, possibly shorter by 1 unit
 */
simdutf_warn_unused size_t trim_partial_utf16(const char16_t *input,
                                              size_t length);
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
trim_partial_utf16(std::span<const char16_t> valid_utf16_input) noexcept {
  return trim_partial_utf16(valid_utf16_input.data(), valid_utf16_input.size());
}
#endif

// base64_options are used to specify the base64 encoding options.
// ASCII spaces are ' ', '\t', '\n', '\r', '\f'
// garbage characters are characters that are not part of the base64 alphabet
// nor ASCII spaces.
enum base64_options : uint64_t {
  base64_default = 0,         /* standard base64 format (with padding) */
  base64_url = 1,             /* base64url format (no padding) */
  base64_reverse_padding = 2, /* modifier for base64_default and base64_url */
  base64_default_no_padding =
      base64_default |
      base64_reverse_padding, /* standard base64 format without padding */
  base64_url_with_padding =
      base64_url | base64_reverse_padding, /* base64url with padding */
  base64_default_accept_garbage =
      4, /* standard base64 format accepting garbage characters */
  base64_url_accept_garbage =
      5, /* base64url format accepting garbage characters */
};

// last_chunk_handling_options are used to specify the handling of the last
// chunk in base64 decoding.
// https://tc39.es/proposal-arraybuffer-base64/spec/#sec-frombase64
enum last_chunk_handling_options : uint64_t {
  loose = 0,  /* standard base64 format, decode partial final chunk */
  strict = 1, /* error when the last chunk is partial, 2 or 3 chars, and
                 unpadded, or non-zero bit padding */
  stop_before_partial =
      2, /* if the last chunk is partial (2 or 3 chars), ignore it (no error) */
};

/**
 * Provide the maximal binary length in bytes given the base64 input.
 * In general, if the input contains ASCII spaces, the result will be less than
 * the maximum length.
 *
 * @param input         the base64 input to process
 * @param length        the length of the base64 input in bytes
 * @return maximum number of binary bytes
 */
simdutf_warn_unused size_t
maximal_binary_length_from_base64(const char *input, size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
maximal_binary_length_from_base64(
    const detail::input_span_of_byte_like auto &input) noexcept {
  return maximal_binary_length_from_base64(
      reinterpret_cast<const char *>(input.data()), input.size());
}
#endif

/**
 * Provide the maximal binary length in bytes given the base64 input.
 * In general, if the input contains ASCII spaces, the result will be less than
 * the maximum length.
 *
 * @param input         the base64 input to process, in ASCII stored as 16-bit
 * units
 * @param length        the length of the base64 input in 16-bit units
 * @return maximal number of binary bytes
 */
simdutf_warn_unused size_t maximal_binary_length_from_base64(
    const char16_t *input, size_t length) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
maximal_binary_length_from_base64(std::span<const char16_t> input) noexcept {
  return maximal_binary_length_from_base64(input.data(), input.size());
}
#endif

/**
 * Convert a base64 input to a binary output.
 *
 * This function follows the WHATWG forgiving-base64 format, which means that it
 * will ignore any ASCII spaces in the input. You may provide a padded input
 * (with one or two equal signs at the end) or an unpadded input (without any
 * equal signs at the end).
 *
 * See https://infra.spec.whatwg.org/#forgiving-base64-decode
 *
 * This function will fail in case of invalid input. When last_chunk_options =
 * loose, there are two possible reasons for failure: the input contains a
 * number of base64 characters that when divided by 4, leaves a single remainder
 * character (BASE64_INPUT_REMAINDER), or the input contains a character that is
 * not a valid base64 character (INVALID_BASE64_CHARACTER).
 *
 * When the error is INVALID_BASE64_CHARACTER, r.count contains the index in the
 * input where the invalid character was found. When the error is
 * BASE64_INPUT_REMAINDER, then r.count contains the number of bytes decoded.
 *
 * The default option (simdutf::base64_default) expects the characters `+` and
 * `/` as part of its alphabet. The URL option (simdutf::base64_url) expects the
 * characters `-` and `_` as part of its alphabet.
 *
 * The padding (`=`) is validated if present. There may be at most two padding
 * characters at the end of the input. If there are any padding characters, the
 * total number of characters (excluding spaces but including padding
 * characters) must be divisible by four.
 *
 * You should call this function with a buffer that is at least
 * maximal_binary_length_from_base64(input, length) bytes long. If you fail to
 * provide that much space, the function may cause a buffer overflow.
 *
 * Advanced users may want to taylor how the last chunk is handled. By default,
 * we use a loose (forgiving) approach but we also support a strict approach
 * as well as a stop_before_partial approach, as per the following proposal:
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-frombase64
 *
 * @param input         the base64 string to process
 * @param length        the length of the string in bytes
 * @param output        the pointer to buffer that can hold the conversion
 * result (should be at least maximal_binary_length_from_base64(input, length)
 * bytes long).
 * @param options       the base64 options to use, usually base64_default or
 * base64_url, and base64_default by default.
 * @param last_chunk_options the last chunk handling options,
 * last_chunk_handling_options::loose by default
 * but can also be last_chunk_handling_options::strict or
 * last_chunk_handling_options::stop_before_partial.
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and either position of the error
 * (in the input in bytes) if any, or the number of bytes written if successful.
 */
simdutf_warn_unused result base64_to_binary(
    const char *input, size_t length, char *output,
    base64_options options = base64_default,
    last_chunk_handling_options last_chunk_options = loose) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result base64_to_binary(
    const detail::input_span_of_byte_like auto &input,
    detail::output_span_of_byte_like auto &&binary_output,
    base64_options options = base64_default,
    last_chunk_handling_options last_chunk_options = loose) noexcept {
  return base64_to_binary(reinterpret_cast<const char *>(input.data()),
                          input.size(),
                          reinterpret_cast<char *>(binary_output.data()),
                          options, last_chunk_options);
}
#endif

/**
 * Provide the base64 length in bytes given the length of a binary input.
 *
 * @param length        the length of the input in bytes
 * @return number of base64 bytes
 */
simdutf_warn_unused size_t base64_length_from_binary(
    size_t length, base64_options options = base64_default) noexcept;

/**
 * Convert a binary input to a base64 output.
 *
 * The default option (simdutf::base64_default) uses the characters `+` and `/`
 * as part of its alphabet. Further, it adds padding (`=`) at the end of the
 * output to ensure that the output length is a multiple of four.
 *
 * The URL option (simdutf::base64_url) uses the characters `-` and `_` as part
 * of its alphabet. No padding is added at the end of the output.
 *
 * This function always succeeds.
 *
 * @param input         the binary to process
 * @param length        the length of the input in bytes
 * @param output        the pointer to buffer that can hold the conversion
 * result (should be at least base64_length_from_binary(length) bytes long)
 * @param options       the base64 options to use, can be base64_default or
 * base64_url, is base64_default by default.
 * @return number of written bytes, will be equal to
 * base64_length_from_binary(length, options)
 */
size_t binary_to_base64(const char *input, size_t length, char *output,
                        base64_options options = base64_default) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused size_t
binary_to_base64(const detail::input_span_of_byte_like auto &input,
                 detail::output_span_of_byte_like auto &&binary_output,
                 base64_options options = base64_default) noexcept {
  return binary_to_base64(
      reinterpret_cast<const char *>(input.data()), input.size(),
      reinterpret_cast<char *>(binary_output.data()), options);
}
#endif

/**
 * Convert a base64 input to a binary output.
 *
 * This function follows the WHATWG forgiving-base64 format, which means that it
 * will ignore any ASCII spaces in the input. You may provide a padded input
 * (with one or two equal signs at the end) or an unpadded input (without any
 * equal signs at the end).
 *
 * See https://infra.spec.whatwg.org/#forgiving-base64-decode
 *
 * This function will fail in case of invalid input. When last_chunk_options =
 * loose, there are two possible reasons for failure: the input contains a
 * number of base64 characters that when divided by 4, leaves a single remainder
 * character (BASE64_INPUT_REMAINDER), or the input contains a character that is
 * not a valid base64 character (INVALID_BASE64_CHARACTER).
 *
 * When the error is INVALID_BASE64_CHARACTER, r.count contains the index in the
 * input where the invalid character was found. When the error is
 * BASE64_INPUT_REMAINDER, then r.count contains the number of bytes decoded.
 *
 * The default option (simdutf::base64_default) expects the characters `+` and
 * `/` as part of its alphabet. The URL option (simdutf::base64_url) expects the
 * characters `-` and `_` as part of its alphabet.
 *
 * The padding (`=`) is validated if present. There may be at most two padding
 * characters at the end of the input. If there are any padding characters, the
 * total number of characters (excluding spaces but including padding
 * characters) must be divisible by four.
 *
 * You should call this function with a buffer that is at least
 * maximal_binary_length_from_utf6_base64(input, length) bytes long. If you fail
 * to provide that much space, the function may cause a buffer overflow.
 *
 * Advanced users may want to taylor how the last chunk is handled. By default,
 * we use a loose (forgiving) approach but we also support a strict approach
 * as well as a stop_before_partial approach, as per the following proposal:
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-frombase64
 *
 * @param input         the base64 string to process, in ASCII stored as 16-bit
 * units
 * @param length        the length of the string in 16-bit units
 * @param output        the pointer to buffer that can hold the conversion
 * result (should be at least maximal_binary_length_from_base64(input, length)
 * bytes long).
 * @param options       the base64 options to use, can be base64_default or
 * base64_url, is base64_default by default.
 * @param last_chunk_options the last chunk handling options,
 * last_chunk_handling_options::loose by default
 * but can also be last_chunk_handling_options::strict or
 * last_chunk_handling_options::stop_before_partial.
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and position of the
 * INVALID_BASE64_CHARACTER error (in the input in units) if any, or the number
 * of bytes written if successful.
 */
simdutf_warn_unused result
base64_to_binary(const char16_t *input, size_t length, char *output,
                 base64_options options = base64_default,
                 last_chunk_handling_options last_chunk_options =
                     last_chunk_handling_options::loose) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result base64_to_binary(
    std::span<const char16_t> input,
    detail::output_span_of_byte_like auto &&binary_output,
    base64_options options = base64_default,
    last_chunk_handling_options last_chunk_options = loose) noexcept {
  return base64_to_binary(input.data(), input.size(),
                          reinterpret_cast<char *>(binary_output.data()),
                          options, last_chunk_options);
}
#endif

/**
 * Convert a base64 input to a binary output.
 *
 * This function follows the WHATWG forgiving-base64 format, which means that it
 * will ignore any ASCII spaces in the input. You may provide a padded input
 * (with one or two equal signs at the end) or an unpadded input (without any
 * equal signs at the end).
 *
 * See https://infra.spec.whatwg.org/#forgiving-base64-decode
 *
 * This function will fail in case of invalid input. When last_chunk_options =
 * loose, there are three possible reasons for failure: the input contains a
 * number of base64 characters that when divided by 4, leaves a single remainder
 * character (BASE64_INPUT_REMAINDER), the input contains a character that is
 * not a valid base64 character (INVALID_BASE64_CHARACTER), or the output buffer
 * is too small (OUTPUT_BUFFER_TOO_SMALL).
 *
 * When OUTPUT_BUFFER_TOO_SMALL, we return both the number of bytes written
 * and the number of units processed, see description of the parameters and
 * returned value.
 *
 * When the error is INVALID_BASE64_CHARACTER, r.count contains the index in the
 * input where the invalid character was found. When the error is
 * BASE64_INPUT_REMAINDER, then r.count contains the number of bytes decoded.
 *
 * The default option (simdutf::base64_default) expects the characters `+` and
 * `/` as part of its alphabet. The URL option (simdutf::base64_url) expects the
 * characters `-` and `_` as part of its alphabet.
 *
 * The padding (`=`) is validated if present. There may be at most two padding
 * characters at the end of the input. If there are any padding characters, the
 * total number of characters (excluding spaces but including padding
 * characters) must be divisible by four.
 *
 * The INVALID_BASE64_CHARACTER cases are considered fatal and you are expected
 * to discard the output.
 *
 * Advanced users may want to taylor how the last chunk is handled. By default,
 * we use a loose (forgiving) approach but we also support a strict approach
 * as well as a stop_before_partial approach, as per the following proposal:
 *
 * https://tc39.es/proposal-arraybuffer-base64/spec/#sec-frombase64
 *
 * @param input         the base64 string to process, in ASCII stored as 8-bit
 * or 16-bit units
 * @param length        the length of the string in 8-bit or 16-bit units.
 * @param output        the pointer to buffer that can hold the conversion
 * result.
 * @param outlen        the number of bytes that can be written in the output
 * buffer. Upon return, it is modified to reflect how many bytes were written.
 * @param options       the base64 options to use, can be base64_default or
 * base64_url, is base64_default by default.
 * @param last_chunk_options the last chunk handling options,
 * last_chunk_handling_options::loose by default
 * but can also be last_chunk_handling_options::strict or
 * last_chunk_handling_options::stop_before_partial.
 * @return a result pair struct (of type simdutf::result containing the two
 * fields error and count) with an error code and position of the
 * INVALID_BASE64_CHARACTER error (in the input in units) if any, or the number
 * of units processed if successful.
 */
simdutf_warn_unused result
base64_to_binary_safe(const char *input, size_t length, char *output,
                      size_t &outlen, base64_options options = base64_default,
                      last_chunk_handling_options last_chunk_options =
                          last_chunk_handling_options::loose) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result base64_to_binary_safe(
    const detail::input_span_of_byte_like auto &input,
    detail::output_span_of_byte_like auto &&binary_output,
    base64_options options = base64_default,
    last_chunk_handling_options last_chunk_options = loose) noexcept {
  // we can't write the outlen to the provided output span, the user will have
  // to pick it up from the returned value instead (assuming success). we still
  // get the benefit of providing info of how long the output buffer is.
  size_t outlen = binary_output.size();
  return base64_to_binary_safe(reinterpret_cast<const char *>(input.data()),
                               input.size(),
                               reinterpret_cast<char *>(binary_output.data()),
                               outlen, options, last_chunk_options);
}
#endif

simdutf_warn_unused result
base64_to_binary_safe(const char16_t *input, size_t length, char *output,
                      size_t &outlen, base64_options options = base64_default,
                      last_chunk_handling_options last_chunk_options =
                          last_chunk_handling_options::loose) noexcept;
#if SIMDUTF_SPAN
simdutf_really_inline simdutf_warn_unused result base64_to_binary_safe(
    std::span<const char16_t> input,
    detail::output_span_of_byte_like auto &&binary_output,
    base64_options options = base64_default,
    last_chunk_handling_options last_chunk_options = loose) noexcept {
  // we can't write the outlen to the provided output span, the user will have
  // to pick it up from the returned value instead (assuming success). we still
  // get the benefit of providing info of how long the output buffer is.
  size_t outlen = binary_output.size();
  return base64_to_binary_safe(input.data(), input.size(),
                               reinterpret_cast<char *>(binary_output.data()),
                               outlen, options, last_chunk_options);
}
#endif

/**
 * An implementation of simdutf for a particular CPU architecture.
 *
 * Also used to maintain the currently active implementation. The active
 * implementation is automatically initialized on first use to the most advanced
 * implementation supported by the host.
 */
class implementation {
public:
  /**
   * The name of this implementation.
   *
   *     const implementation *impl = simdutf::active_implementation;
   *     cout << "simdutf is optimized for " << impl->name() << "(" <<
   * impl->description() << ")" << endl;
   *
   * @return the name of the implementation, e.g. "haswell", "westmere", "arm64"
   */
  virtual std::string name() const { return std::string(_name); }

  /**
   * The description of this implementation.
   *
   *     const implementation *impl = simdutf::active_implementation;
   *     cout << "simdutf is optimized for " << impl->name() << "(" <<
   * impl->description() << ")" << endl;
   *
   * @return the name of the implementation, e.g. "haswell", "westmere", "arm64"
   */
  virtual std::string description() const { return std::string(_description); }

  /**
   * The instruction sets this implementation is compiled against
   * and the current CPU match. This function may poll the current CPU/system
   * and should therefore not be called too often if performance is a concern.
   *
   *
   * @return true if the implementation can be safely used on the current system
   * (determined at runtime)
   */
  bool supported_by_runtime_system() const;

  /**
   * This function will try to detect the encoding
   * @param input the string to identify
   * @param length the length of the string in bytes.
   * @return the encoding type detected
   */
  virtual encoding_type autodetect_encoding(const char *input,
                                            size_t length) const noexcept;

  /**
   * This function will try to detect the possible encodings in one pass
   * @param input the string to identify
   * @param length the length of the string in bytes.
   * @return the encoding type detected
   */
  virtual int detect_encodings(const char *input,
                               size_t length) const noexcept = 0;

  /**
   * @private For internal implementation use
   *
   * The instruction sets this implementation is compiled against.
   *
   * @return a mask of all required `internal::instruction_set::` values
   */
  virtual uint32_t required_instruction_sets() const {
    return _required_instruction_sets;
  }

  /**
   * Validate the UTF-8 string.
   *
   * Overridden by each implementation.
   *
   * @param buf the UTF-8 string to validate.
   * @param len the length of the string in bytes.
   * @return true if and only if the string is valid UTF-8.
   */
  simdutf_warn_unused virtual bool validate_utf8(const char *buf,
                                                 size_t len) const noexcept = 0;

  /**
   * Validate the UTF-8 string and stop on errors.
   *
   * Overridden by each implementation.
   *
   * @param buf the UTF-8 string to validate.
   * @param len the length of the string in bytes.
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of code units validated
   * if successful.
   */
  simdutf_warn_unused virtual result
  validate_utf8_with_errors(const char *buf, size_t len) const noexcept = 0;

  /**
   * Validate the ASCII string.
   *
   * Overridden by each implementation.
   *
   * @param buf the ASCII string to validate.
   * @param len the length of the string in bytes.
   * @return true if and only if the string is valid ASCII.
   */
  simdutf_warn_unused virtual bool
  validate_ascii(const char *buf, size_t len) const noexcept = 0;

  /**
   * Validate the ASCII string and stop on error.
   *
   * Overridden by each implementation.
   *
   * @param buf the ASCII string to validate.
   * @param len the length of the string in bytes.
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of code units validated
   * if successful.
   */
  simdutf_warn_unused virtual result
  validate_ascii_with_errors(const char *buf, size_t len) const noexcept = 0;

  /**
   * Validate the UTF-16LE string.This function may be best when you expect
   * the input to be almost always valid. Otherwise, consider using
   * validate_utf16le_with_errors.
   *
   * Overridden by each implementation.
   *
   * This function is not BOM-aware.
   *
   * @param buf the UTF-16LE string to validate.
   * @param len the length of the string in number of 2-byte code units
   * (char16_t).
   * @return true if and only if the string is valid UTF-16LE.
   */
  simdutf_warn_unused virtual bool
  validate_utf16le(const char16_t *buf, size_t len) const noexcept = 0;

  /**
   * Validate the UTF-16BE string. This function may be best when you expect
   * the input to be almost always valid. Otherwise, consider using
   * validate_utf16be_with_errors.
   *
   * Overridden by each implementation.
   *
   * This function is not BOM-aware.
   *
   * @param buf the UTF-16BE string to validate.
   * @param len the length of the string in number of 2-byte code units
   * (char16_t).
   * @return true if and only if the string is valid UTF-16BE.
   */
  simdutf_warn_unused virtual bool
  validate_utf16be(const char16_t *buf, size_t len) const noexcept = 0;

  /**
   * Validate the UTF-16LE string and stop on error.  It might be faster than
   * validate_utf16le when an error is expected to occur early.
   *
   * Overridden by each implementation.
   *
   * This function is not BOM-aware.
   *
   * @param buf the UTF-16LE string to validate.
   * @param len the length of the string in number of 2-byte code units
   * (char16_t).
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of code units validated
   * if successful.
   */
  simdutf_warn_unused virtual result
  validate_utf16le_with_errors(const char16_t *buf,
                               size_t len) const noexcept = 0;

  /**
   * Validate the UTF-16BE string and stop on error. It might be faster than
   * validate_utf16be when an error is expected to occur early.
   *
   * Overridden by each implementation.
   *
   * This function is not BOM-aware.
   *
   * @param buf the UTF-16BE string to validate.
   * @param len the length of the string in number of 2-byte code units
   * (char16_t).
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of code units validated
   * if successful.
   */
  simdutf_warn_unused virtual result
  validate_utf16be_with_errors(const char16_t *buf,
                               size_t len) const noexcept = 0;

  /**
   * Validate the UTF-32 string.
   *
   * Overridden by each implementation.
   *
   * This function is not BOM-aware.
   *
   * @param buf the UTF-32 string to validate.
   * @param len the length of the string in number of 4-byte code units
   * (char32_t).
   * @return true if and only if the string is valid UTF-32.
   */
  simdutf_warn_unused virtual bool
  validate_utf32(const char32_t *buf, size_t len) const noexcept = 0;

  /**
   * Validate the UTF-32 string and stop on error.
   *
   * Overridden by each implementation.
   *
   * This function is not BOM-aware.
   *
   * @param buf the UTF-32 string to validate.
   * @param len the length of the string in number of 4-byte code units
   * (char32_t).
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of code units validated
   * if successful.
   */
  simdutf_warn_unused virtual result
  validate_utf32_with_errors(const char32_t *buf,
                             size_t len) const noexcept = 0;

  /**
   * Convert Latin1 string into UTF8 string.
   *
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the Latin1 string to convert
   * @param length        the length of the string in bytes
   * @param utf8_output  the pointer to buffer that can hold conversion result
   * @return the number of written char; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_latin1_to_utf8(const char *input, size_t length,
                         char *utf8_output) const noexcept = 0;

  /**
   * Convert possibly Latin1 string into UTF-16LE string.
   *
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the Latin1  string to convert
   * @param length        the length of the string in bytes
   * @param utf16_buffer  the pointer to buffer that can hold conversion result
   * @return the number of written char16_t; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_latin1_to_utf16le(const char *input, size_t length,
                            char16_t *utf16_output) const noexcept = 0;

  /**
   * Convert Latin1 string into UTF-16BE string.
   *
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the Latin1 string to convert
   * @param length        the length of the string in bytes
   * @param utf16_buffer  the pointer to buffer that can hold conversion result
   * @return the number of written char16_t; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_latin1_to_utf16be(const char *input, size_t length,
                            char16_t *utf16_output) const noexcept = 0;

  /**
   * Convert Latin1 string into UTF-32 string.
   *
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the Latin1 string to convert
   * @param length        the length of the string in bytes
   * @param utf32_buffer  the pointer to buffer that can hold conversion result
   * @return the number of written char32_t; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_latin1_to_utf32(const char *input, size_t length,
                          char32_t *utf32_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-8 string into latin1 string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param latin1_output  the pointer to buffer that can hold conversion result
   * @return the number of written char; 0 if the input was not valid UTF-8
   * string or if it cannot be represented as Latin1
   */
  simdutf_warn_unused virtual size_t
  convert_utf8_to_latin1(const char *input, size_t length,
                         char *latin1_output) const noexcept = 0;

  /**
   * Convert possibly broken UTF-8 string into latin1 string with errors.
   * If the string cannot be represented as Latin1, an error
   * code is returned.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param latin1_output  the pointer to buffer that can hold conversion result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of code units validated
   * if successful.
   */
  simdutf_warn_unused virtual result
  convert_utf8_to_latin1_with_errors(const char *input, size_t length,
                                     char *latin1_output) const noexcept = 0;

  /**
   * Convert valid UTF-8 string into latin1 string.
   *
   * This function assumes that the input string is valid UTF-8 and that it can
   * be represented as Latin1. If you violate this assumption, the result is
   * implementation defined and may include system-dependent behavior such as
   * crashes.
   *
   * This function is for expert users only and not part of our public API. Use
   * convert_utf8_to_latin1 instead.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param latin1_output  the pointer to buffer that can hold conversion result
   * @return the number of written char; 0 if the input was not valid UTF-8
   * string
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf8_to_latin1(const char *input, size_t length,
                               char *latin1_output) const noexcept = 0;

  /**
   * Convert possibly broken UTF-8 string into UTF-16LE string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param utf16_buffer  the pointer to buffer that can hold conversion result
   * @return the number of written char16_t; 0 if the input was not valid UTF-8
   * string
   */
  simdutf_warn_unused virtual size_t
  convert_utf8_to_utf16le(const char *input, size_t length,
                          char16_t *utf16_output) const noexcept = 0;

  /**
   * Convert possibly broken UTF-8 string into UTF-16BE string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param utf16_buffer  the pointer to buffer that can hold conversion result
   * @return the number of written char16_t; 0 if the input was not valid UTF-8
   * string
   */
  simdutf_warn_unused virtual size_t
  convert_utf8_to_utf16be(const char *input, size_t length,
                          char16_t *utf16_output) const noexcept = 0;

  /**
   * Convert possibly broken UTF-8 string into UTF-16LE string and stop on
   * error.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param utf16_buffer  the pointer to buffer that can hold conversion result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of code units validated
   * if successful.
   */
  simdutf_warn_unused virtual result convert_utf8_to_utf16le_with_errors(
      const char *input, size_t length,
      char16_t *utf16_output) const noexcept = 0;

  /**
   * Convert possibly broken UTF-8 string into UTF-16BE string and stop on
   * error.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param utf16_buffer  the pointer to buffer that can hold conversion result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of code units validated
   * if successful.
   */
  simdutf_warn_unused virtual result convert_utf8_to_utf16be_with_errors(
      const char *input, size_t length,
      char16_t *utf16_output) const noexcept = 0;

  /**
   * Convert possibly broken UTF-8 string into UTF-32 string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param utf32_buffer  the pointer to buffer that can hold conversion result
   * @return the number of written char16_t; 0 if the input was not valid UTF-8
   * string
   */
  simdutf_warn_unused virtual size_t
  convert_utf8_to_utf32(const char *input, size_t length,
                        char32_t *utf32_output) const noexcept = 0;

  /**
   * Convert possibly broken UTF-8 string into UTF-32 string and stop on error.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param utf32_buffer  the pointer to buffer that can hold conversion result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of char32_t written if
   * successful.
   */
  simdutf_warn_unused virtual result
  convert_utf8_to_utf32_with_errors(const char *input, size_t length,
                                    char32_t *utf32_output) const noexcept = 0;

  /**
   * Convert valid UTF-8 string into UTF-16LE string.
   *
   * This function assumes that the input string is valid UTF-8.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param utf16_buffer  the pointer to buffer that can hold conversion result
   * @return the number of written char16_t
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf8_to_utf16le(const char *input, size_t length,
                                char16_t *utf16_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-8 string into UTF-16BE string.
   *
   * This function assumes that the input string is valid UTF-8.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param utf16_buffer  the pointer to buffer that can hold conversion result
   * @return the number of written char16_t
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf8_to_utf16be(const char *input, size_t length,
                                char16_t *utf16_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-8 string into UTF-32 string.
   *
   * This function assumes that the input string is valid UTF-8.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in bytes
   * @param utf16_buffer  the pointer to buffer that can hold conversion result
   * @return the number of written char32_t
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf8_to_utf32(const char *input, size_t length,
                              char32_t *utf32_buffer) const noexcept = 0;

  /**
   * Compute the number of 2-byte code units that this UTF-8 string would
   * require in UTF-16LE format.
   *
   * This function does not validate the input. It is acceptable to pass invalid
   * UTF-8 strings but in such cases the result is implementation defined.
   *
   * @param input         the UTF-8 string to process
   * @param length        the length of the string in bytes
   * @return the number of char16_t code units required to encode the UTF-8
   * string as UTF-16LE
   */
  simdutf_warn_unused virtual size_t
  utf16_length_from_utf8(const char *input, size_t length) const noexcept = 0;

  /**
   * Compute the number of 4-byte code units that this UTF-8 string would
   * require in UTF-32 format.
   *
   * This function is equivalent to count_utf8. It is acceptable to pass invalid
   * UTF-8 strings but in such cases the result is implementation defined.
   *
   * This function does not validate the input.
   *
   * @param input         the UTF-8 string to process
   * @param length        the length of the string in bytes
   * @return the number of char32_t code units required to encode the UTF-8
   * string as UTF-32
   */
  simdutf_warn_unused virtual size_t
  utf32_length_from_utf8(const char *input, size_t length) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16LE string into Latin1 string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param latin1_buffer   the pointer to buffer that can hold conversion
   * result
   * @return number of written code units; 0 if input is not a valid UTF-16LE
   * string or if it cannot be represented as Latin1
   */
  simdutf_warn_unused virtual size_t
  convert_utf16le_to_latin1(const char16_t *input, size_t length,
                            char *latin1_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16BE string into Latin1 string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param latin1_buffer   the pointer to buffer that can hold conversion
   * result
   * @return number of written code units; 0 if input is not a valid UTF-16BE
   * string or if it cannot be represented as Latin1
   */
  simdutf_warn_unused virtual size_t
  convert_utf16be_to_latin1(const char16_t *input, size_t length,
                            char *latin1_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16LE string into Latin1 string.
   * If the string cannot be represented as Latin1, an error
   * is returned.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param latin1_buffer   the pointer to buffer that can hold conversion
   * result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of char written if
   * successful.
   */
  simdutf_warn_unused virtual result
  convert_utf16le_to_latin1_with_errors(const char16_t *input, size_t length,
                                        char *latin1_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16BE string into Latin1 string.
   * If the string cannot be represented as Latin1, an error
   * is returned.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param latin1_buffer   the pointer to buffer that can hold conversion
   * result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of char written if
   * successful.
   */
  simdutf_warn_unused virtual result
  convert_utf16be_to_latin1_with_errors(const char16_t *input, size_t length,
                                        char *latin1_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-16LE string into Latin1 string.
   *
   * This function assumes that the input string is valid UTF-L16LE and that it
   * can be represented as Latin1. If you violate this assumption, the result is
   * implementation defined and may include system-dependent behavior such as
   * crashes.
   *
   * This function is for expert users only and not part of our public API. Use
   * convert_utf16le_to_latin1 instead.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param latin1_buffer   the pointer to buffer that can hold conversion
   * result
   * @return number of written code units; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf16le_to_latin1(const char16_t *input, size_t length,
                                  char *latin1_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-16BE string into Latin1 string.
   *
   * This function assumes that the input string is valid UTF16-BE and that it
   * can be represented as Latin1. If you violate this assumption, the result is
   * implementation defined and may include system-dependent behavior such as
   * crashes.
   *
   * This function is for expert users only and not part of our public API. Use
   * convert_utf16be_to_latin1 instead.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param latin1_buffer   the pointer to buffer that can hold conversion
   * result
   * @return number of written code units; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf16be_to_latin1(const char16_t *input, size_t length,
                                  char *latin1_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16LE string into UTF-8 string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf8_buffer   the pointer to buffer that can hold conversion result
   * @return number of written code units; 0 if input is not a valid UTF-16LE
   * string
   */
  simdutf_warn_unused virtual size_t
  convert_utf16le_to_utf8(const char16_t *input, size_t length,
                          char *utf8_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16BE string into UTF-8 string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf8_buffer   the pointer to buffer that can hold conversion result
   * @return number of written code units; 0 if input is not a valid UTF-16BE
   * string
   */
  simdutf_warn_unused virtual size_t
  convert_utf16be_to_utf8(const char16_t *input, size_t length,
                          char *utf8_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16LE string into UTF-8 string and stop on
   * error.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf8_buffer   the pointer to buffer that can hold conversion result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of char written if
   * successful.
   */
  simdutf_warn_unused virtual result
  convert_utf16le_to_utf8_with_errors(const char16_t *input, size_t length,
                                      char *utf8_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16BE string into UTF-8 string and stop on
   * error.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf8_buffer   the pointer to buffer that can hold conversion result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of char written if
   * successful.
   */
  simdutf_warn_unused virtual result
  convert_utf16be_to_utf8_with_errors(const char16_t *input, size_t length,
                                      char *utf8_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-16LE string into UTF-8 string.
   *
   * This function assumes that the input string is valid UTF-16LE.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf8_buffer   the pointer to buffer that can hold the conversion
   * result
   * @return number of written code units; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf16le_to_utf8(const char16_t *input, size_t length,
                                char *utf8_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-16BE string into UTF-8 string.
   *
   * This function assumes that the input string is valid UTF-16BE.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf8_buffer   the pointer to buffer that can hold the conversion
   * result
   * @return number of written code units; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf16be_to_utf8(const char16_t *input, size_t length,
                                char *utf8_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16LE string into UTF-32 string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf32_buffer   the pointer to buffer that can hold conversion result
   * @return number of written code units; 0 if input is not a valid UTF-16LE
   * string
   */
  simdutf_warn_unused virtual size_t
  convert_utf16le_to_utf32(const char16_t *input, size_t length,
                           char32_t *utf32_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16BE string into UTF-32 string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf32_buffer   the pointer to buffer that can hold conversion result
   * @return number of written code units; 0 if input is not a valid UTF-16BE
   * string
   */
  simdutf_warn_unused virtual size_t
  convert_utf16be_to_utf32(const char16_t *input, size_t length,
                           char32_t *utf32_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16LE string into UTF-32 string and stop on
   * error.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf32_buffer   the pointer to buffer that can hold conversion result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of char32_t written if
   * successful.
   */
  simdutf_warn_unused virtual result convert_utf16le_to_utf32_with_errors(
      const char16_t *input, size_t length,
      char32_t *utf32_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-16BE string into UTF-32 string and stop on
   * error.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf32_buffer   the pointer to buffer that can hold conversion result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of char32_t written if
   * successful.
   */
  simdutf_warn_unused virtual result convert_utf16be_to_utf32_with_errors(
      const char16_t *input, size_t length,
      char32_t *utf32_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-16LE string into UTF-32 string.
   *
   * This function assumes that the input string is valid UTF-16LE.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf32_buffer   the pointer to buffer that can hold the conversion
   * result
   * @return number of written code units; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf16le_to_utf32(const char16_t *input, size_t length,
                                 char32_t *utf32_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-16LE string into UTF-32BE string.
   *
   * This function assumes that the input string is valid UTF-16BE.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param utf32_buffer   the pointer to buffer that can hold the conversion
   * result
   * @return number of written code units; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf16be_to_utf32(const char16_t *input, size_t length,
                                 char32_t *utf32_buffer) const noexcept = 0;

  /**
   * Compute the number of bytes that this UTF-16LE string would require in
   * UTF-8 format.
   *
   * This function does not validate the input. It is acceptable to pass invalid
   * UTF-16 strings but in such cases the result is implementation defined.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @return the number of bytes required to encode the UTF-16LE string as UTF-8
   */
  simdutf_warn_unused virtual size_t
  utf8_length_from_utf16le(const char16_t *input,
                           size_t length) const noexcept = 0;

  /**
   * Compute the number of bytes that this UTF-16BE string would require in
   * UTF-8 format.
   *
   * This function does not validate the input. It is acceptable to pass invalid
   * UTF-16 strings but in such cases the result is implementation defined.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @return the number of bytes required to encode the UTF-16BE string as UTF-8
   */
  simdutf_warn_unused virtual size_t
  utf8_length_from_utf16be(const char16_t *input,
                           size_t length) const noexcept = 0;

  /**
   * Convert possibly broken UTF-32 string into Latin1 string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param latin1_buffer   the pointer to buffer that can hold conversion
   * result
   * @return number of written code units; 0 if input is not a valid UTF-32
   * string
   */

  simdutf_warn_unused virtual size_t
  convert_utf32_to_latin1(const char32_t *input, size_t length,
                          char *latin1_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-32 string into Latin1 string and stop on error.
   * If the string cannot be represented as Latin1, an error is returned.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param latin1_buffer   the pointer to buffer that can hold conversion
   * result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of char written if
   * successful.
   */
  simdutf_warn_unused virtual result
  convert_utf32_to_latin1_with_errors(const char32_t *input, size_t length,
                                      char *latin1_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-32 string into Latin1 string.
   *
   * This function assumes that the input string is valid UTF-32 and can be
   * represented as Latin1. If you violate this assumption, the result is
   * implementation defined and may include system-dependent behavior such as
   * crashes.
   *
   * This function is for expert users only and not part of our public API. Use
   * convert_utf32_to_latin1 instead.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param latin1_buffer   the pointer to buffer that can hold the conversion
   * result
   * @return number of written code units; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf32_to_latin1(const char32_t *input, size_t length,
                                char *latin1_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-32 string into UTF-8 string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param utf8_buffer   the pointer to buffer that can hold conversion result
   * @return number of written code units; 0 if input is not a valid UTF-32
   * string
   */
  simdutf_warn_unused virtual size_t
  convert_utf32_to_utf8(const char32_t *input, size_t length,
                        char *utf8_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-32 string into UTF-8 string and stop on error.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param utf8_buffer   the pointer to buffer that can hold conversion result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of char written if
   * successful.
   */
  simdutf_warn_unused virtual result
  convert_utf32_to_utf8_with_errors(const char32_t *input, size_t length,
                                    char *utf8_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-32 string into UTF-8 string.
   *
   * This function assumes that the input string is valid UTF-32.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param utf8_buffer   the pointer to buffer that can hold the conversion
   * result
   * @return number of written code units; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf32_to_utf8(const char32_t *input, size_t length,
                              char *utf8_buffer) const noexcept = 0;

  /**
   * Return the number of bytes that this UTF-16 string would require in Latin1
   * format.
   *
   *
   * @param input         the UTF-16 string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @return the number of bytes required to encode the UTF-16 string as Latin1
   */
  simdutf_warn_unused virtual size_t
  utf16_length_from_latin1(size_t length) const noexcept = 0;

  /**
   * Convert possibly broken UTF-32 string into UTF-16LE string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param utf16_buffer   the pointer to buffer that can hold conversion result
   * @return number of written code units; 0 if input is not a valid UTF-32
   * string
   */
  simdutf_warn_unused virtual size_t
  convert_utf32_to_utf16le(const char32_t *input, size_t length,
                           char16_t *utf16_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-32 string into UTF-16BE string.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param utf16_buffer   the pointer to buffer that can hold conversion result
   * @return number of written code units; 0 if input is not a valid UTF-32
   * string
   */
  simdutf_warn_unused virtual size_t
  convert_utf32_to_utf16be(const char32_t *input, size_t length,
                           char16_t *utf16_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-32 string into UTF-16LE string and stop on
   * error.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param utf16_buffer   the pointer to buffer that can hold conversion result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of char16_t written if
   * successful.
   */
  simdutf_warn_unused virtual result convert_utf32_to_utf16le_with_errors(
      const char32_t *input, size_t length,
      char16_t *utf16_buffer) const noexcept = 0;

  /**
   * Convert possibly broken UTF-32 string into UTF-16BE string and stop on
   * error.
   *
   * During the conversion also validation of the input string is done.
   * This function is suitable to work with inputs from untrusted sources.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param utf16_buffer   the pointer to buffer that can hold conversion result
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in code units) if any, or the number of char16_t written if
   * successful.
   */
  simdutf_warn_unused virtual result convert_utf32_to_utf16be_with_errors(
      const char32_t *input, size_t length,
      char16_t *utf16_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-32 string into UTF-16LE string.
   *
   * This function assumes that the input string is valid UTF-32.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param utf16_buffer   the pointer to buffer that can hold the conversion
   * result
   * @return number of written code units; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf32_to_utf16le(const char32_t *input, size_t length,
                                 char16_t *utf16_buffer) const noexcept = 0;

  /**
   * Convert valid UTF-32 string into UTF-16BE string.
   *
   * This function assumes that the input string is valid UTF-32.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @param utf16_buffer   the pointer to buffer that can hold the conversion
   * result
   * @return number of written code units; 0 if conversion is not possible
   */
  simdutf_warn_unused virtual size_t
  convert_valid_utf32_to_utf16be(const char32_t *input, size_t length,
                                 char16_t *utf16_buffer) const noexcept = 0;

  /**
   * Change the endianness of the input. Can be used to go from UTF-16LE to
   * UTF-16BE or from UTF-16BE to UTF-16LE.
   *
   * This function does not validate the input.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16 string to process
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @param output        the pointer to buffer that can hold the conversion
   * result
   */
  virtual void change_endianness_utf16(const char16_t *input, size_t length,
                                       char16_t *output) const noexcept = 0;

  /**
   * Return the number of bytes that this Latin1 string would require in UTF-8
   * format.
   *
   * @param input         the Latin1 string to convert
   * @param length        the length of the string bytes
   * @return the number of bytes required to encode the Latin1 string as UTF-8
   */
  simdutf_warn_unused virtual size_t
  utf8_length_from_latin1(const char *input, size_t length) const noexcept = 0;

  /**
   * Compute the number of bytes that this UTF-32 string would require in UTF-8
   * format.
   *
   * This function does not validate the input. It is acceptable to pass invalid
   * UTF-32 strings but in such cases the result is implementation defined.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @return the number of bytes required to encode the UTF-32 string as UTF-8
   */
  simdutf_warn_unused virtual size_t
  utf8_length_from_utf32(const char32_t *input,
                         size_t length) const noexcept = 0;

  /**
   * Compute the number of bytes that this UTF-32 string would require in Latin1
   * format.
   *
   * This function does not validate the input. It is acceptable to pass invalid
   * UTF-32 strings but in such cases the result is implementation defined.
   *
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @return the number of bytes required to encode the UTF-32 string as Latin1
   */
  simdutf_warn_unused virtual size_t
  latin1_length_from_utf32(size_t length) const noexcept = 0;

  /**
   * Compute the number of bytes that this UTF-8 string would require in Latin1
   * format.
   *
   * This function does not validate the input. It is acceptable to pass invalid
   * UTF-8 strings but in such cases the result is implementation defined.
   *
   * @param input         the UTF-8 string to convert
   * @param length        the length of the string in byte
   * @return the number of bytes required to encode the UTF-8 string as Latin1
   */
  simdutf_warn_unused virtual size_t
  latin1_length_from_utf8(const char *input, size_t length) const noexcept = 0;

  /**
   * Compute the number of bytes that this UTF-16LE/BE string would require in
   * Latin1 format.
   *
   * This function does not validate the input. It is acceptable to pass invalid
   * UTF-16 strings but in such cases the result is implementation defined.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @return the number of bytes required to encode the UTF-16LE string as
   * Latin1
   */
  simdutf_warn_unused virtual size_t
  latin1_length_from_utf16(size_t length) const noexcept = 0;

  /**
   * Compute the number of two-byte code units that this UTF-32 string would
   * require in UTF-16 format.
   *
   * This function does not validate the input. It is acceptable to pass invalid
   * UTF-32 strings but in such cases the result is implementation defined.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @return the number of bytes required to encode the UTF-32 string as UTF-16
   */
  simdutf_warn_unused virtual size_t
  utf16_length_from_utf32(const char32_t *input,
                          size_t length) const noexcept = 0;

  /**
   * Return the number of bytes that this UTF-32 string would require in Latin1
   * format.
   *
   * @param input         the UTF-32 string to convert
   * @param length        the length of the string in 4-byte code units
   * (char32_t)
   * @return the number of bytes required to encode the UTF-32 string as Latin1
   */
  simdutf_warn_unused virtual size_t
  utf32_length_from_latin1(size_t length) const noexcept = 0;

  /**
   * Compute the number of bytes that this UTF-16LE string would require in
   * UTF-32 format.
   *
   * This function is equivalent to count_utf16le.
   *
   * This function does not validate the input. It is acceptable to pass invalid
   * UTF-16 strings but in such cases the result is implementation defined.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @return the number of bytes required to encode the UTF-16LE string as
   * UTF-32
   */
  simdutf_warn_unused virtual size_t
  utf32_length_from_utf16le(const char16_t *input,
                            size_t length) const noexcept = 0;

  /**
   * Compute the number of bytes that this UTF-16BE string would require in
   * UTF-32 format.
   *
   * This function is equivalent to count_utf16be.
   *
   * This function does not validate the input. It is acceptable to pass invalid
   * UTF-16 strings but in such cases the result is implementation defined.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to convert
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @return the number of bytes required to encode the UTF-16BE string as
   * UTF-32
   */
  simdutf_warn_unused virtual size_t
  utf32_length_from_utf16be(const char16_t *input,
                            size_t length) const noexcept = 0;

  /**
   * Count the number of code points (characters) in the string assuming that
   * it is valid.
   *
   * This function assumes that the input string is valid UTF-16LE.
   * It is acceptable to pass invalid UTF-16 strings but in such cases
   * the result is implementation defined.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16LE string to process
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @return number of code points
   */
  simdutf_warn_unused virtual size_t
  count_utf16le(const char16_t *input, size_t length) const noexcept = 0;

  /**
   * Count the number of code points (characters) in the string assuming that
   * it is valid.
   *
   * This function assumes that the input string is valid UTF-16BE.
   * It is acceptable to pass invalid UTF-16 strings but in such cases
   * the result is implementation defined.
   *
   * This function is not BOM-aware.
   *
   * @param input         the UTF-16BE string to process
   * @param length        the length of the string in 2-byte code units
   * (char16_t)
   * @return number of code points
   */
  simdutf_warn_unused virtual size_t
  count_utf16be(const char16_t *input, size_t length) const noexcept = 0;

  /**
   * Count the number of code points (characters) in the string assuming that
   * it is valid.
   *
   * This function assumes that the input string is valid UTF-8.
   * It is acceptable to pass invalid UTF-8 strings but in such cases
   * the result is implementation defined.
   *
   * @param input         the UTF-8 string to process
   * @param length        the length of the string in bytes
   * @return number of code points
   */
  simdutf_warn_unused virtual size_t
  count_utf8(const char *input, size_t length) const noexcept = 0;

  /**
   * Provide the maximal binary length in bytes given the base64 input.
   * In general, if the input contains ASCII spaces, the result will be less
   * than the maximum length. It is acceptable to pass invalid base64 strings
   * but in such cases the result is implementation defined.
   *
   * @param input         the base64 input to process
   * @param length        the length of the base64 input in bytes
   * @return maximal number of binary bytes
   */
  simdutf_warn_unused virtual size_t
  maximal_binary_length_from_base64(const char *input,
                                    size_t length) const noexcept = 0;

  /**
   * Provide the maximal binary length in bytes given the base64 input.
   * In general, if the input contains ASCII spaces, the result will be less
   * than the maximum length. It is acceptable to pass invalid base64 strings
   * but in such cases the result is implementation defined.
   *
   * @param input         the base64 input to process, in ASCII stored as 16-bit
   * units
   * @param length        the length of the base64 input in 16-bit units
   * @return maximal number of binary bytes
   */
  simdutf_warn_unused virtual size_t
  maximal_binary_length_from_base64(const char16_t *input,
                                    size_t length) const noexcept = 0;

  /**
   * Convert a base64 input to a binary output.
   *
   * This function follows the WHATWG forgiving-base64 format, which means that
   * it will ignore any ASCII spaces in the input. You may provide a padded
   * input (with one or two equal signs at the end) or an unpadded input
   * (without any equal signs at the end).
   *
   * See https://infra.spec.whatwg.org/#forgiving-base64-decode
   *
   * This function will fail in case of invalid input. When last_chunk_options =
   * loose, there are two possible reasons for failure: the input contains a
   * number of base64 characters that when divided by 4, leaves a single
   * remainder character (BASE64_INPUT_REMAINDER), or the input contains a
   * character that is not a valid base64 character (INVALID_BASE64_CHARACTER).
   *
   * You should call this function with a buffer that is at least
   * maximal_binary_length_from_base64(input, length) bytes long. If you fail to
   * provide that much space, the function may cause a buffer overflow.
   *
   * @param input         the base64 string to process
   * @param length        the length of the string in bytes
   * @param output        the pointer to buffer that can hold the conversion
   * result (should be at least maximal_binary_length_from_base64(input, length)
   * bytes long).
   * @param options       the base64 options to use, can be base64_default or
   * base64_url, is base64_default by default.
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and either position of the error
   * (in the input in bytes) if any, or the number of bytes written if
   * successful.
   */
  simdutf_warn_unused virtual result
  base64_to_binary(const char *input, size_t length, char *output,
                   base64_options options = base64_default,
                   last_chunk_handling_options last_chunk_options =
                       last_chunk_handling_options::loose) const noexcept = 0;

  /**
   * Convert a base64 input to a binary output while returning more details
   * than base64_to_binary.
   *
   * This function follows the WHATWG forgiving-base64 format, which means that
   * it will ignore any ASCII spaces in the input. You may provide a padded
   * input (with one or two equal signs at the end) or an unpadded input
   * (without any equal signs at the end).
   *
   * See https://infra.spec.whatwg.org/#forgiving-base64-decode
   *
   * This function will fail in case of invalid input. When last_chunk_options =
   * loose, there are two possible reasons for failure: the input contains a
   * number of base64 characters that when divided by 4, leaves a single
   * remainder character (BASE64_INPUT_REMAINDER), or the input contains a
   * character that is not a valid base64 character (INVALID_BASE64_CHARACTER).
   *
   * You should call this function with a buffer that is at least
   * maximal_binary_length_from_base64(input, length) bytes long. If you fail to
   * provide that much space, the function may cause a buffer overflow.
   *
   * @param input         the base64 string to process
   * @param length        the length of the string in bytes
   * @param output        the pointer to buffer that can hold the conversion
   * result (should be at least maximal_binary_length_from_base64(input, length)
   * bytes long).
   * @param options       the base64 options to use, can be base64_default or
   * base64_url, is base64_default by default.
   * @return a full_result pair struct (of type simdutf::result containing the
   * three fields error, input_count and output_count).
   */
  simdutf_warn_unused virtual full_result base64_to_binary_details(
      const char *input, size_t length, char *output,
      base64_options options = base64_default,
      last_chunk_handling_options last_chunk_options =
          last_chunk_handling_options::loose) const noexcept = 0;
  /**
   * Convert a base64 input to a binary output.
   *
   * This function follows the WHATWG forgiving-base64 format, which means that
   * it will ignore any ASCII spaces in the input. You may provide a padded
   * input (with one or two equal signs at the end) or an unpadded input
   * (without any equal signs at the end).
   *
   * See https://infra.spec.whatwg.org/#forgiving-base64-decode
   *
   * This function will fail in case of invalid input. When last_chunk_options =
   * loose, there are two possible reasons for failure: the input contains a
   * number of base64 characters that when divided by 4, leaves a single
   * remainder character (BASE64_INPUT_REMAINDER), or the input contains a
   * character that is not a valid base64 character (INVALID_BASE64_CHARACTER).
   *
   * You should call this function with a buffer that is at least
   * maximal_binary_length_from_utf6_base64(input, length) bytes long. If you
   * fail to provide that much space, the function may cause a buffer overflow.
   *
   * @param input         the base64 string to process, in ASCII stored as
   * 16-bit units
   * @param length        the length of the string in 16-bit units
   * @param output        the pointer to buffer that can hold the conversion
   * result (should be at least maximal_binary_length_from_base64(input, length)
   * bytes long).
   * @param options       the base64 options to use, can be base64_default or
   * base64_url, is base64_default by default.
   * @return a result pair struct (of type simdutf::result containing the two
   * fields error and count) with an error code and position of the
   * INVALID_BASE64_CHARACTER error (in the input in units) if any, or the
   * number of bytes written if successful.
   */
  simdutf_warn_unused virtual result
  base64_to_binary(const char16_t *input, size_t length, char *output,
                   base64_options options = base64_default,
                   last_chunk_handling_options last_chunk_options =
                       last_chunk_handling_options::loose) const noexcept = 0;

  /**
   * Convert a base64 input to a binary output while returning more details
   * than base64_to_binary.
   *
   * This function follows the WHATWG forgiving-base64 format, which means that
   * it will ignore any ASCII spaces in the input. You may provide a padded
   * input (with one or two equal signs at the end) or an unpadded input
   * (without any equal signs at the end).
   *
   * See https://infra.spec.whatwg.org/#forgiving-base64-decode
   *
   * This function will fail in case of invalid input. When last_chunk_options =
   * loose, there are two possible reasons for failure: the input contains a
   * number of base64 characters that when divided by 4, leaves a single
   * remainder character (BASE64_INPUT_REMAINDER), or the input contains a
   * character that is not a valid base64 character (INVALID_BASE64_CHARACTER).
   *
   * You should call this function with a buffer that is at least
   * maximal_binary_length_from_base64(input, length) bytes long. If you fail to
   * provide that much space, the function may cause a buffer overflow.
   *
   * @param input         the base64 string to process
   * @param length        the length of the string in bytes
   * @param output        the pointer to buffer that can hold the conversion
   * result (should be at least maximal_binary_length_from_base64(input, length)
   * bytes long).
   * @param options       the base64 options to use, can be base64_default or
   * base64_url, is base64_default by default.
   * @return a full_result pair struct (of type simdutf::result containing the
   * three fields error, input_count and output_count).
   */
  simdutf_warn_unused virtual full_result base64_to_binary_details(
      const char16_t *input, size_t length, char *output,
      base64_options options = base64_default,
      last_chunk_handling_options last_chunk_options =
          last_chunk_handling_options::loose) const noexcept = 0;
  /**
   * Provide the base64 length in bytes given the length of a binary input.
   *
   * @param length        the length of the input in bytes
   * @parem options       the base64 options to use, can be base64_default or
   * base64_url, is base64_default by default.
   * @return number of base64 bytes
   */
  simdutf_warn_unused virtual size_t base64_length_from_binary(
      size_t length,
      base64_options options = base64_default) const noexcept = 0;

  /**
   * Convert a binary input to a base64 output.
   *
   * The default option (simdutf::base64_default) uses the characters `+` and
   * `/` as part of its alphabet. Further, it adds padding (`=`) at the end of
   * the output to ensure that the output length is a multiple of four.
   *
   * The URL option (simdutf::base64_url) uses the characters `-` and `_` as
   * part of its alphabet. No padding is added at the end of the output.
   *
   * This function always succeeds.
   *
   * @param input         the binary to process
   * @param length        the length of the input in bytes
   * @param output        the pointer to buffer that can hold the conversion
   * result (should be at least base64_length_from_binary(length) bytes long)
   * @param options       the base64 options to use, can be base64_default or
   * base64_url, is base64_default by default.
   * @return number of written bytes, will be equal to
   * base64_length_from_binary(length, options)
   */
  virtual size_t
  binary_to_base64(const char *input, size_t length, char *output,
                   base64_options options = base64_default) const noexcept = 0;

protected:
  /** @private Construct an implementation with the given name and description.
   * For subclasses. */
  simdutf_really_inline implementation(const char *name,
                                       const char *description,
                                       uint32_t required_instruction_sets)
      : _name(name), _description(description),
        _required_instruction_sets(required_instruction_sets) {}

protected:
  ~implementation() = default;

private:
  /**
   * The name of this implementation.
   */
  const char *_name;

  /**
   * The description of this implementation.
   */
  const char *_description;

  /**
   * Instruction sets required for this implementation.
   */
  const uint32_t _required_instruction_sets;
};

/** @private */
namespace internal {

/**
 * The list of available implementations compiled into simdutf.
 */
class available_implementation_list {
public:
  /** Get the list of available implementations compiled into simdutf */
  simdutf_really_inline available_implementation_list() {}
  /** Number of implementations */
  size_t size() const noexcept;
  /** STL const begin() iterator */
  const implementation *const *begin() const noexcept;
  /** STL const end() iterator */
  const implementation *const *end() const noexcept;

  /**
   * Get the implementation with the given name.
   *
   * Case sensitive.
   *
   *     const implementation *impl =
   * simdutf::available_implementations["westmere"]; if (!impl) { exit(1); } if
   * (!imp->supported_by_runtime_system()) { exit(1); }
   *     simdutf::active_implementation = impl;
   *
   * @param name the implementation to find, e.g. "westmere", "haswell", "arm64"
   * @return the implementation, or nullptr if the parse failed.
   */
  const implementation *operator[](const std::string &name) const noexcept {
    for (const implementation *impl : *this) {
      if (impl->name() == name) {
        return impl;
      }
    }
    return nullptr;
  }

  /**
   * Detect the most advanced implementation supported by the current host.
   *
   * This is used to initialize the implementation on startup.
   *
   *     const implementation *impl =
   * simdutf::available_implementation::detect_best_supported();
   *     simdutf::active_implementation = impl;
   *
   * @return the most advanced supported implementation for the current host, or
   * an implementation that returns UNSUPPORTED_ARCHITECTURE if there is no
   * supported implementation. Will never return nullptr.
   */
  const implementation *detect_best_supported() const noexcept;
};

template <typename T> class atomic_ptr {
public:
  atomic_ptr(T *_ptr) : ptr{_ptr} {}

#if defined(SIMDUTF_NO_THREADS)
  operator const T *() const { return ptr; }
  const T &operator*() const { return *ptr; }
  const T *operator->() const { return ptr; }

  operator T *() { return ptr; }
  T &operator*() { return *ptr; }
  T *operator->() { return ptr; }
  atomic_ptr &operator=(T *_ptr) {
    ptr = _ptr;
    return *this;
  }

#else
  operator const T *() const { return ptr.load(); }
  const T &operator*() const { return *ptr; }
  const T *operator->() const { return ptr.load(); }

  operator T *() { return ptr.load(); }
  T &operator*() { return *ptr; }
  T *operator->() { return ptr.load(); }
  atomic_ptr &operator=(T *_ptr) {
    ptr = _ptr;
    return *this;
  }

#endif

private:
#if defined(SIMDUTF_NO_THREADS)
  T *ptr;
#else
  std::atomic<T *> ptr;
#endif
};

class detect_best_supported_implementation_on_first_use;

} // namespace internal

/**
 * The list of available implementations compiled into simdutf.
 */
extern SIMDUTF_DLLIMPORTEXPORT const internal::available_implementation_list &
get_available_implementations();

/**
 * The active implementation.
 *
 * Automatically initialized on first use to the most advanced implementation
 * supported by this hardware.
 */
extern SIMDUTF_DLLIMPORTEXPORT internal::atomic_ptr<const implementation> &
get_active_implementation();

} // namespace simdutf

#endif // SIMDUTF_IMPLEMENTATION_H
/* end file include/simdutf/implementation.h */

// Implementation-internal files (must be included before the implementations
// themselves, to keep amalgamation working--otherwise, the first time a file is
// included, it might be put inside the #ifdef
// SIMDUTF_IMPLEMENTATION_ARM64/FALLBACK/etc., which means the other
// implementations can't compile unless that implementation is turned on).

SIMDUTF_POP_DISABLE_WARNINGS

#endif // SIMDUTF_H
/* end file include/simdutf.h */
