// Copyright 2020 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HIGHWAY_HWY_BASE_H_
#define HIGHWAY_HWY_BASE_H_

// Target-independent definitions.

// IWYU pragma: begin_exports
#include <stddef.h>
#include <stdint.h>

#if !defined(HWY_NO_LIBCXX)
#include <ostream>
#endif

#include "hwy/detect_compiler_arch.h"
#include "hwy/highway_export.h"

// API version (https://semver.org/); keep in sync with CMakeLists.txt.
#define HWY_MAJOR 1
#define HWY_MINOR 2
#define HWY_PATCH 0

// True if the Highway version >= major.minor.0. Added in 1.2.0.
#define HWY_VERSION_GE(major, minor) \
  (HWY_MAJOR > (major) || (HWY_MAJOR == (major) && HWY_MINOR >= (minor)))
// True if the Highway version < major.minor.0. Added in 1.2.0.
#define HWY_VERSION_LT(major, minor) \
  (HWY_MAJOR < (major) || (HWY_MAJOR == (major) && HWY_MINOR < (minor)))

// "IWYU pragma: keep" does not work for these includes, so hide from the IDE.
#if !HWY_IDE

#if !defined(HWY_NO_LIBCXX)
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS  // before inttypes.h
#endif
#include <inttypes.h>
#endif

#if (HWY_ARCH_X86 && !defined(HWY_NO_LIBCXX)) || HWY_COMPILER_MSVC
#include <atomic>
#endif

#endif  // !HWY_IDE

#ifndef HWY_HAVE_COMPARE_HEADER  // allow override
#define HWY_HAVE_COMPARE_HEADER 0
#if defined(__has_include)  // note: wrapper macro fails on Clang ~17
#if __has_include(<compare>)
#undef HWY_HAVE_COMPARE_HEADER
#define HWY_HAVE_COMPARE_HEADER 1
#endif  // __has_include
#endif  // defined(__has_include)
#endif  // HWY_HAVE_COMPARE_HEADER

#ifndef HWY_HAVE_CXX20_THREE_WAY_COMPARE  // allow override
#if !defined(HWY_NO_LIBCXX) && defined(__cpp_impl_three_way_comparison) && \
    __cpp_impl_three_way_comparison >= 201907L && HWY_HAVE_COMPARE_HEADER
#include <compare>
#define HWY_HAVE_CXX20_THREE_WAY_COMPARE 1
#else
#define HWY_HAVE_CXX20_THREE_WAY_COMPARE 0
#endif
#endif  // HWY_HAVE_CXX20_THREE_WAY_COMPARE

// IWYU pragma: end_exports

#if HWY_COMPILER_MSVC
#include <string.h>  // memcpy
#endif

//------------------------------------------------------------------------------
// Compiler-specific definitions

#define HWY_STR_IMPL(macro) #macro
#define HWY_STR(macro) HWY_STR_IMPL(macro)

#if HWY_COMPILER_MSVC

#include <intrin.h>

#define HWY_FUNCTION __FUNCSIG__  // function name + template args
#define HWY_RESTRICT __restrict
#define HWY_INLINE __forceinline
#define HWY_NOINLINE __declspec(noinline)
#define HWY_FLATTEN
#define HWY_NORETURN __declspec(noreturn)
#define HWY_LIKELY(expr) (expr)
#define HWY_UNLIKELY(expr) (expr)
#define HWY_PRAGMA(tokens) __pragma(tokens)
#define HWY_DIAGNOSTICS(tokens) HWY_PRAGMA(warning(tokens))
#define HWY_DIAGNOSTICS_OFF(msc, gcc) HWY_DIAGNOSTICS(msc)
#define HWY_MAYBE_UNUSED
#define HWY_HAS_ASSUME_ALIGNED 0
#if (_MSC_VER >= 1700)
#define HWY_MUST_USE_RESULT _Check_return_
#else
#define HWY_MUST_USE_RESULT
#endif

#else

#define HWY_FUNCTION __PRETTY_FUNCTION__  // function name + template args
#define HWY_RESTRICT __restrict__
// force inlining without optimization enabled creates very inefficient code
// that can cause compiler timeout
#ifdef __OPTIMIZE__
#define HWY_INLINE inline __attribute__((always_inline))
#else
#define HWY_INLINE inline
#endif
#define HWY_NOINLINE __attribute__((noinline))
#define HWY_FLATTEN __attribute__((flatten))
#define HWY_NORETURN __attribute__((noreturn))
#define HWY_LIKELY(expr) __builtin_expect(!!(expr), 1)
#define HWY_UNLIKELY(expr) __builtin_expect(!!(expr), 0)
#define HWY_PRAGMA(tokens) _Pragma(#tokens)
#define HWY_DIAGNOSTICS(tokens) HWY_PRAGMA(GCC diagnostic tokens)
#define HWY_DIAGNOSTICS_OFF(msc, gcc) HWY_DIAGNOSTICS(gcc)
// Encountered "attribute list cannot appear here" when using the C++17
// [[maybe_unused]], so only use the old style attribute for now.
#define HWY_MAYBE_UNUSED __attribute__((unused))
#define HWY_MUST_USE_RESULT __attribute__((warn_unused_result))

#endif  // !HWY_COMPILER_MSVC

//------------------------------------------------------------------------------
// Builtin/attributes (no more #include after this point due to namespace!)

namespace hwy {

// Enables error-checking of format strings.
#if HWY_HAS_ATTRIBUTE(__format__)
#define HWY_FORMAT(idx_fmt, idx_arg) \
  __attribute__((__format__(__printf__, idx_fmt, idx_arg)))
#else
#define HWY_FORMAT(idx_fmt, idx_arg)
#endif

// Returns a void* pointer which the compiler then assumes is N-byte aligned.
// Example: float* HWY_RESTRICT aligned = (float*)HWY_ASSUME_ALIGNED(in, 32);
//
// The assignment semantics are required by GCC/Clang. ICC provides an in-place
// __assume_aligned, whereas MSVC's __assume appears unsuitable.
#if HWY_HAS_BUILTIN(__builtin_assume_aligned)
#define HWY_ASSUME_ALIGNED(ptr, align) __builtin_assume_aligned((ptr), (align))
#else
#define HWY_ASSUME_ALIGNED(ptr, align) (ptr) /* not supported */
#endif

// Returns a pointer whose type is `type` (T*), while allowing the compiler to
// assume that the untyped pointer `ptr` is aligned to a multiple of sizeof(T).
#define HWY_RCAST_ALIGNED(type, ptr) \
  reinterpret_cast<type>(            \
      HWY_ASSUME_ALIGNED((ptr), alignof(hwy::RemovePtr<type>)))

// Clang and GCC require attributes on each function into which SIMD intrinsics
// are inlined. Support both per-function annotation (HWY_ATTR) for lambdas and
// automatic annotation via pragmas.
#if HWY_COMPILER_ICC
// As of ICC 2021.{1-9} the pragma is neither implemented nor required.
#define HWY_PUSH_ATTRIBUTES(targets_str)
#define HWY_POP_ATTRIBUTES
#elif HWY_COMPILER_CLANG
#define HWY_PUSH_ATTRIBUTES(targets_str)                                \
  HWY_PRAGMA(clang attribute push(__attribute__((target(targets_str))), \
                                  apply_to = function))
#define HWY_POP_ATTRIBUTES HWY_PRAGMA(clang attribute pop)
#elif HWY_COMPILER_GCC_ACTUAL
#define HWY_PUSH_ATTRIBUTES(targets_str) \
  HWY_PRAGMA(GCC push_options) HWY_PRAGMA(GCC target targets_str)
#define HWY_POP_ATTRIBUTES HWY_PRAGMA(GCC pop_options)
#else
#define HWY_PUSH_ATTRIBUTES(targets_str)
#define HWY_POP_ATTRIBUTES
#endif

//------------------------------------------------------------------------------
// Macros

#define HWY_API static HWY_INLINE HWY_FLATTEN HWY_MAYBE_UNUSED

#define HWY_CONCAT_IMPL(a, b) a##b
#define HWY_CONCAT(a, b) HWY_CONCAT_IMPL(a, b)

#define HWY_MIN(a, b) ((a) < (b) ? (a) : (b))
#define HWY_MAX(a, b) ((a) > (b) ? (a) : (b))

#if HWY_COMPILER_GCC_ACTUAL
// nielskm: GCC does not support '#pragma GCC unroll' without the factor.
#define HWY_UNROLL(factor) HWY_PRAGMA(GCC unroll factor)
#define HWY_DEFAULT_UNROLL HWY_UNROLL(4)
#elif HWY_COMPILER_CLANG || HWY_COMPILER_ICC || HWY_COMPILER_ICX
#define HWY_UNROLL(factor) HWY_PRAGMA(unroll factor)
#define HWY_DEFAULT_UNROLL HWY_UNROLL()
#else
#define HWY_UNROLL(factor)
#define HWY_DEFAULT_UNROLL
#endif

// Tell a compiler that the expression always evaluates to true.
// The expression should be free from any side effects.
// Some older compilers may have trouble with complex expressions, therefore
// it is advisable to split multiple conditions into separate assume statements,
// and manually check the generated code.
// OK but could fail:
//   HWY_ASSUME(x == 2 && y == 3);
// Better:
//   HWY_ASSUME(x == 2);
//   HWY_ASSUME(y == 3);
#if HWY_HAS_CPP_ATTRIBUTE(assume)
#define HWY_ASSUME(expr) [[assume(expr)]]
#elif HWY_COMPILER_MSVC || HWY_COMPILER_ICC
#define HWY_ASSUME(expr) __assume(expr)
// __builtin_assume() was added in clang 3.6.
#elif HWY_COMPILER_CLANG && HWY_HAS_BUILTIN(__builtin_assume)
#define HWY_ASSUME(expr) __builtin_assume(expr)
// __builtin_unreachable() was added in GCC 4.5, but __has_builtin() was added
// later, so check for the compiler version directly.
#elif HWY_COMPILER_GCC_ACTUAL >= 405
#define HWY_ASSUME(expr) \
  ((expr) ? static_cast<void>(0) : __builtin_unreachable())
#else
#define HWY_ASSUME(expr) static_cast<void>(0)
#endif

// Compile-time fence to prevent undesirable code reordering. On Clang x86, the
// typical asm volatile("" : : : "memory") has no effect, whereas atomic fence
// does, without generating code.
#if HWY_ARCH_X86 && !defined(HWY_NO_LIBCXX)
#define HWY_FENCE std::atomic_thread_fence(std::memory_order_acq_rel)
#else
// TODO(janwas): investigate alternatives. On Arm, the above generates barriers.
#define HWY_FENCE
#endif

// 4 instances of a given literal value, useful as input to LoadDup128.
#define HWY_REP4(literal) literal, literal, literal, literal

HWY_DLLEXPORT HWY_NORETURN void HWY_FORMAT(3, 4)
    Abort(const char* file, int line, const char* format, ...);

#define HWY_ABORT(format, ...) \
  ::hwy::Abort(__FILE__, __LINE__, format, ##__VA_ARGS__)

// Always enabled.
#define HWY_ASSERT_M(condition, msg)               \
  do {                                             \
    if (!(condition)) {                            \
      HWY_ABORT("Assert %s: %s", #condition, msg); \
    }                                              \
  } while (0)
#define HWY_ASSERT(condition) HWY_ASSERT_M(condition, "")

#if HWY_HAS_FEATURE(memory_sanitizer) || defined(MEMORY_SANITIZER) || \
    defined(__SANITIZE_MEMORY__)
#define HWY_IS_MSAN 1
#else
#define HWY_IS_MSAN 0
#endif

#if HWY_HAS_FEATURE(address_sanitizer) || defined(ADDRESS_SANITIZER) || \
    defined(__SANITIZE_ADDRESS__)
#define HWY_IS_ASAN 1
#else
#define HWY_IS_ASAN 0
#endif

#if HWY_HAS_FEATURE(hwaddress_sanitizer) || defined(HWADDRESS_SANITIZER) || \
    defined(__SANITIZE_HWADDRESS__)
#define HWY_IS_HWASAN 1
#else
#define HWY_IS_HWASAN 0
#endif

#if HWY_HAS_FEATURE(thread_sanitizer) || defined(THREAD_SANITIZER) || \
    defined(__SANITIZE_THREAD__)
#define HWY_IS_TSAN 1
#else
#define HWY_IS_TSAN 0
#endif

#if HWY_HAS_FEATURE(undefined_behavior_sanitizer) || \
    defined(UNDEFINED_BEHAVIOR_SANITIZER)
#define HWY_IS_UBSAN 1
#else
#define HWY_IS_UBSAN 0
#endif

// MSAN may cause lengthy build times or false positives e.g. in AVX3 DemoteTo.
// You can disable MSAN by adding this attribute to the function that fails.
#if HWY_IS_MSAN
#define HWY_ATTR_NO_MSAN __attribute__((no_sanitize_memory))
#else
#define HWY_ATTR_NO_MSAN
#endif

// For enabling HWY_DASSERT and shortening tests in slower debug builds
#if !defined(HWY_IS_DEBUG_BUILD)
// Clang does not define NDEBUG, but it and GCC define __OPTIMIZE__, and recent
// MSVC defines NDEBUG (if not, could instead check _DEBUG).
#if (!defined(__OPTIMIZE__) && !defined(NDEBUG)) || HWY_IS_ASAN || \
    HWY_IS_HWASAN || HWY_IS_MSAN || HWY_IS_TSAN || HWY_IS_UBSAN || \
    defined(__clang_analyzer__)
#define HWY_IS_DEBUG_BUILD 1
#else
#define HWY_IS_DEBUG_BUILD 0
#endif
#endif  // HWY_IS_DEBUG_BUILD

#if HWY_IS_DEBUG_BUILD
#define HWY_DASSERT_M(condition, msg) HWY_ASSERT_M(condition, msg)
#define HWY_DASSERT(condition) HWY_ASSERT_M(condition, "")
#else
#define HWY_DASSERT_M(condition, msg) \
  do {                                \
  } while (0)
#define HWY_DASSERT(condition) \
  do {                         \
  } while (0)
#endif

//------------------------------------------------------------------------------
// CopyBytes / ZeroBytes

#if HWY_COMPILER_MSVC
#pragma intrinsic(memcpy)
#pragma intrinsic(memset)
#endif

template <size_t kBytes, typename From, typename To>
HWY_API void CopyBytes(const From* HWY_RESTRICT from, To* HWY_RESTRICT to) {
#if HWY_COMPILER_MSVC
  memcpy(to, from, kBytes);
#else
  __builtin_memcpy(to, from, kBytes);
#endif
}

HWY_API void CopyBytes(const void* HWY_RESTRICT from, void* HWY_RESTRICT to,
                       size_t num_of_bytes_to_copy) {
#if HWY_COMPILER_MSVC
  memcpy(to, from, num_of_bytes_to_copy);
#else
  __builtin_memcpy(to, from, num_of_bytes_to_copy);
#endif
}

// Same as CopyBytes, but for same-sized objects; avoids a size argument.
template <typename From, typename To>
HWY_API void CopySameSize(const From* HWY_RESTRICT from, To* HWY_RESTRICT to) {
  static_assert(sizeof(From) == sizeof(To), "");
  CopyBytes<sizeof(From)>(from, to);
}

template <size_t kBytes, typename To>
HWY_API void ZeroBytes(To* to) {
#if HWY_COMPILER_MSVC
  memset(to, 0, kBytes);
#else
  __builtin_memset(to, 0, kBytes);
#endif
}

HWY_API void ZeroBytes(void* to, size_t num_bytes) {
#if HWY_COMPILER_MSVC
  memset(to, 0, num_bytes);
#else
  __builtin_memset(to, 0, num_bytes);
#endif
}

//------------------------------------------------------------------------------
// kMaxVectorSize (undocumented, pending removal)

#if HWY_ARCH_X86
static constexpr HWY_MAYBE_UNUSED size_t kMaxVectorSize = 64;  // AVX-512
#elif HWY_ARCH_RISCV && defined(__riscv_v_intrinsic) && \
    __riscv_v_intrinsic >= 11000
// Not actually an upper bound on the size.
static constexpr HWY_MAYBE_UNUSED size_t kMaxVectorSize = 4096;
#else
static constexpr HWY_MAYBE_UNUSED size_t kMaxVectorSize = 16;
#endif

//------------------------------------------------------------------------------
// Alignment

// Potentially useful for LoadDup128 and capped vectors. In other cases, arrays
// should be allocated dynamically via aligned_allocator.h because Lanes() may
// exceed the stack size.
#if HWY_ARCH_X86
#define HWY_ALIGN_MAX alignas(64)
#elif HWY_ARCH_RISCV && defined(__riscv_v_intrinsic) && \
    __riscv_v_intrinsic >= 11000
#define HWY_ALIGN_MAX alignas(8)  // only elements need be aligned
#else
#define HWY_ALIGN_MAX alignas(16)
#endif

//------------------------------------------------------------------------------
// Lane types

// hwy::float16_t and hwy::bfloat16_t are forward declared here to allow
// BitCastScalar to be implemented before the implementations of the
// hwy::float16_t and hwy::bfloat16_t types
struct float16_t;
struct bfloat16_t;

using float32_t = float;
using float64_t = double;

#pragma pack(push, 1)

// Aligned 128-bit type. Cannot use __int128 because clang doesn't yet align it:
// https://reviews.llvm.org/D86310
struct alignas(16) uint128_t {
  uint64_t lo;  // little-endian layout
  uint64_t hi;
};

// 64 bit key plus 64 bit value. Faster than using uint128_t when only the key
// field is to be compared (Lt128Upper instead of Lt128).
struct alignas(16) K64V64 {
  uint64_t value;  // little-endian layout
  uint64_t key;
};

// 32 bit key plus 32 bit value. Allows vqsort recursions to terminate earlier
// than when considering both to be a 64-bit key.
struct alignas(8) K32V32 {
  uint32_t value;  // little-endian layout
  uint32_t key;
};

#pragma pack(pop)

static inline HWY_MAYBE_UNUSED bool operator<(const uint128_t& a,
                                              const uint128_t& b) {
  return (a.hi == b.hi) ? a.lo < b.lo : a.hi < b.hi;
}
// Required for std::greater.
static inline HWY_MAYBE_UNUSED bool operator>(const uint128_t& a,
                                              const uint128_t& b) {
  return b < a;
}
static inline HWY_MAYBE_UNUSED bool operator==(const uint128_t& a,
                                               const uint128_t& b) {
  return a.lo == b.lo && a.hi == b.hi;
}

#if !defined(HWY_NO_LIBCXX)
static inline HWY_MAYBE_UNUSED std::ostream& operator<<(std::ostream& os,
                                                        const uint128_t& n) {
  return os << "[hi=" << n.hi << ",lo=" << n.lo << "]";
}
#endif

static inline HWY_MAYBE_UNUSED bool operator<(const K64V64& a,
                                              const K64V64& b) {
  return a.key < b.key;
}
// Required for std::greater.
static inline HWY_MAYBE_UNUSED bool operator>(const K64V64& a,
                                              const K64V64& b) {
  return b < a;
}
static inline HWY_MAYBE_UNUSED bool operator==(const K64V64& a,
                                               const K64V64& b) {
  return a.key == b.key;
}

#if !defined(HWY_NO_LIBCXX)
static inline HWY_MAYBE_UNUSED std::ostream& operator<<(std::ostream& os,
                                                        const K64V64& n) {
  return os << "[k=" << n.key << ",v=" << n.value << "]";
}
#endif

static inline HWY_MAYBE_UNUSED bool operator<(const K32V32& a,
                                              const K32V32& b) {
  return a.key < b.key;
}
// Required for std::greater.
static inline HWY_MAYBE_UNUSED bool operator>(const K32V32& a,
                                              const K32V32& b) {
  return b < a;
}
static inline HWY_MAYBE_UNUSED bool operator==(const K32V32& a,
                                               const K32V32& b) {
  return a.key == b.key;
}

#if !defined(HWY_NO_LIBCXX)
static inline HWY_MAYBE_UNUSED std::ostream& operator<<(std::ostream& os,
                                                        const K32V32& n) {
  return os << "[k=" << n.key << ",v=" << n.value << "]";
}
#endif

//------------------------------------------------------------------------------
// Controlling overload resolution (SFINAE)

template <bool Condition>
struct EnableIfT {};
template <>
struct EnableIfT<true> {
  using type = void;
};

template <bool Condition>
using EnableIf = typename EnableIfT<Condition>::type;

template <typename T, typename U>
struct IsSameT {
  enum { value = 0 };
};

template <typename T>
struct IsSameT<T, T> {
  enum { value = 1 };
};

template <typename T, typename U>
HWY_API constexpr bool IsSame() {
  return IsSameT<T, U>::value;
}

// Returns whether T matches either of U1 or U2
template <typename T, typename U1, typename U2>
HWY_API constexpr bool IsSameEither() {
  return IsSameT<T, U1>::value || IsSameT<T, U2>::value;
}

template <bool Condition, typename Then, typename Else>
struct IfT {
  using type = Then;
};

template <class Then, class Else>
struct IfT<false, Then, Else> {
  using type = Else;
};

template <bool Condition, typename Then, typename Else>
using If = typename IfT<Condition, Then, Else>::type;

template <typename T>
struct IsConstT {
  enum { value = 0 };
};

template <typename T>
struct IsConstT<const T> {
  enum { value = 1 };
};

template <typename T>
HWY_API constexpr bool IsConst() {
  return IsConstT<T>::value;
}

template <class T>
struct RemoveConstT {
  using type = T;
};
template <class T>
struct RemoveConstT<const T> {
  using type = T;
};

template <class T>
using RemoveConst = typename RemoveConstT<T>::type;

template <class T>
struct RemoveVolatileT {
  using type = T;
};
template <class T>
struct RemoveVolatileT<volatile T> {
  using type = T;
};

template <class T>
using RemoveVolatile = typename RemoveVolatileT<T>::type;

template <class T>
struct RemoveRefT {
  using type = T;
};
template <class T>
struct RemoveRefT<T&> {
  using type = T;
};
template <class T>
struct RemoveRefT<T&&> {
  using type = T;
};

template <class T>
using RemoveRef = typename RemoveRefT<T>::type;

template <class T>
using RemoveCvRef = RemoveConst<RemoveVolatile<RemoveRef<T>>>;

template <class T>
struct RemovePtrT {
  using type = T;
};
template <class T>
struct RemovePtrT<T*> {
  using type = T;
};
template <class T>
struct RemovePtrT<const T*> {
  using type = T;
};
template <class T>
struct RemovePtrT<volatile T*> {
  using type = T;
};
template <class T>
struct RemovePtrT<const volatile T*> {
  using type = T;
};

template <class T>
using RemovePtr = typename RemovePtrT<T>::type;

// Insert into template/function arguments to enable this overload only for
// vectors of exactly, at most (LE), or more than (GT) this many bytes.
//
// As an example, checking for a total size of 16 bytes will match both
// Simd<uint8_t, 16, 0> and Simd<uint8_t, 8, 1>.
#define HWY_IF_V_SIZE(T, kN, bytes) \
  hwy::EnableIf<kN * sizeof(T) == bytes>* = nullptr
#define HWY_IF_V_SIZE_LE(T, kN, bytes) \
  hwy::EnableIf<kN * sizeof(T) <= bytes>* = nullptr
#define HWY_IF_V_SIZE_GT(T, kN, bytes) \
  hwy::EnableIf<(kN * sizeof(T) > bytes)>* = nullptr

#define HWY_IF_LANES(kN, lanes) hwy::EnableIf<(kN == lanes)>* = nullptr
#define HWY_IF_LANES_LE(kN, lanes) hwy::EnableIf<(kN <= lanes)>* = nullptr
#define HWY_IF_LANES_GT(kN, lanes) hwy::EnableIf<(kN > lanes)>* = nullptr

#define HWY_IF_UNSIGNED(T) hwy::EnableIf<!hwy::IsSigned<T>()>* = nullptr
#define HWY_IF_NOT_UNSIGNED(T) hwy::EnableIf<hwy::IsSigned<T>()>* = nullptr
#define HWY_IF_SIGNED(T)                                    \
  hwy::EnableIf<hwy::IsSigned<T>() && !hwy::IsFloat<T>() && \
                !hwy::IsSpecialFloat<T>()>* = nullptr
#define HWY_IF_FLOAT(T) hwy::EnableIf<hwy::IsFloat<T>()>* = nullptr
#define HWY_IF_NOT_FLOAT(T) hwy::EnableIf<!hwy::IsFloat<T>()>* = nullptr
#define HWY_IF_FLOAT3264(T) hwy::EnableIf<hwy::IsFloat3264<T>()>* = nullptr
#define HWY_IF_NOT_FLOAT3264(T) hwy::EnableIf<!hwy::IsFloat3264<T>()>* = nullptr
#define HWY_IF_SPECIAL_FLOAT(T) \
  hwy::EnableIf<hwy::IsSpecialFloat<T>()>* = nullptr
#define HWY_IF_NOT_SPECIAL_FLOAT(T) \
  hwy::EnableIf<!hwy::IsSpecialFloat<T>()>* = nullptr
#define HWY_IF_FLOAT_OR_SPECIAL(T) \
  hwy::EnableIf<hwy::IsFloat<T>() || hwy::IsSpecialFloat<T>()>* = nullptr
#define HWY_IF_NOT_FLOAT_NOR_SPECIAL(T) \
  hwy::EnableIf<!hwy::IsFloat<T>() && !hwy::IsSpecialFloat<T>()>* = nullptr
#define HWY_IF_INTEGER(T) hwy::EnableIf<hwy::IsInteger<T>()>* = nullptr

#define HWY_IF_T_SIZE(T, bytes) hwy::EnableIf<sizeof(T) == (bytes)>* = nullptr
#define HWY_IF_NOT_T_SIZE(T, bytes) \
  hwy::EnableIf<sizeof(T) != (bytes)>* = nullptr
// bit_array = 0x102 means 1 or 8 bytes. There is no NONE_OF because it sounds
// too similar. If you want the opposite of this (2 or 4 bytes), ask for those
// bits explicitly (0x14) instead of attempting to 'negate' 0x102.
#define HWY_IF_T_SIZE_ONE_OF(T, bit_array) \
  hwy::EnableIf<((size_t{1} << sizeof(T)) & (bit_array)) != 0>* = nullptr
#define HWY_IF_T_SIZE_LE(T, bytes) \
  hwy::EnableIf<(sizeof(T) <= (bytes))>* = nullptr
#define HWY_IF_T_SIZE_GT(T, bytes) \
  hwy::EnableIf<(sizeof(T) > (bytes))>* = nullptr

#define HWY_IF_SAME(T, expected) \
  hwy::EnableIf<hwy::IsSame<hwy::RemoveCvRef<T>, expected>()>* = nullptr
#define HWY_IF_NOT_SAME(T, expected) \
  hwy::EnableIf<!hwy::IsSame<hwy::RemoveCvRef<T>, expected>()>* = nullptr

// One of two expected types
#define HWY_IF_SAME2(T, expected1, expected2)                            \
  hwy::EnableIf<                                                         \
      hwy::IsSameEither<hwy::RemoveCvRef<T>, expected1, expected2>()>* = \
      nullptr

#define HWY_IF_U8(T) HWY_IF_SAME(T, uint8_t)
#define HWY_IF_U16(T) HWY_IF_SAME(T, uint16_t)
#define HWY_IF_U32(T) HWY_IF_SAME(T, uint32_t)
#define HWY_IF_U64(T) HWY_IF_SAME(T, uint64_t)

#define HWY_IF_I8(T) HWY_IF_SAME(T, int8_t)
#define HWY_IF_I16(T) HWY_IF_SAME(T, int16_t)
#define HWY_IF_I32(T) HWY_IF_SAME(T, int32_t)
#define HWY_IF_I64(T) HWY_IF_SAME(T, int64_t)

#define HWY_IF_BF16(T) HWY_IF_SAME(T, hwy::bfloat16_t)
#define HWY_IF_NOT_BF16(T) HWY_IF_NOT_SAME(T, hwy::bfloat16_t)

#define HWY_IF_F16(T) HWY_IF_SAME(T, hwy::float16_t)
#define HWY_IF_NOT_F16(T) HWY_IF_NOT_SAME(T, hwy::float16_t)

#define HWY_IF_F32(T) HWY_IF_SAME(T, float)
#define HWY_IF_F64(T) HWY_IF_SAME(T, double)

// Use instead of HWY_IF_T_SIZE to avoid ambiguity with float16_t/float/double
// overloads.
#define HWY_IF_UI8(T) HWY_IF_SAME2(T, uint8_t, int8_t)
#define HWY_IF_UI16(T) HWY_IF_SAME2(T, uint16_t, int16_t)
#define HWY_IF_UI32(T) HWY_IF_SAME2(T, uint32_t, int32_t)
#define HWY_IF_UI64(T) HWY_IF_SAME2(T, uint64_t, int64_t)

#define HWY_IF_LANES_PER_BLOCK(T, N, LANES) \
  hwy::EnableIf<HWY_MIN(sizeof(T) * N, 16) / sizeof(T) == (LANES)>* = nullptr

// Empty struct used as a size tag type.
template <size_t N>
struct SizeTag {};

template <class T>
class DeclValT {
 private:
  template <class U, class URef = U&&>
  static URef TryAddRValRef(int);
  template <class U, class Arg>
  static U TryAddRValRef(Arg);

 public:
  using type = decltype(TryAddRValRef<T>(0));
  enum { kDisableDeclValEvaluation = 1 };
};

// hwy::DeclVal<T>() can only be used in unevaluated contexts such as within an
// expression of a decltype specifier.

// hwy::DeclVal<T>() does not require that T have a public default constructor
template <class T>
HWY_API typename DeclValT<T>::type DeclVal() noexcept {
  static_assert(!DeclValT<T>::kDisableDeclValEvaluation,
                "DeclVal() cannot be used in an evaluated context");
}

template <class T>
struct IsArrayT {
  enum { value = 0 };
};

template <class T>
struct IsArrayT<T[]> {
  enum { value = 1 };
};

template <class T, size_t N>
struct IsArrayT<T[N]> {
  enum { value = 1 };
};

template <class T>
static constexpr bool IsArray() {
  return IsArrayT<T>::value;
}

#if HWY_COMPILER_MSVC
HWY_DIAGNOSTICS(push)
HWY_DIAGNOSTICS_OFF(disable : 4180, ignored "-Wignored-qualifiers")
#endif

template <class From, class To>
class IsConvertibleT {
 private:
  template <class T>
  static hwy::SizeTag<1> TestFuncWithToArg(T);

  template <class T, class U>
  static decltype(IsConvertibleT<T, U>::template TestFuncWithToArg<U>(
      DeclVal<T>()))
  TryConvTest(int);

  template <class T, class U, class Arg>
  static hwy::SizeTag<0> TryConvTest(Arg);

 public:
  enum {
    value = (IsSame<RemoveConst<RemoveVolatile<From>>, void>() &&
             IsSame<RemoveConst<RemoveVolatile<To>>, void>()) ||
            (!IsArray<To>() &&
             (IsSame<To, decltype(DeclVal<To>())>() ||
              !IsSame<const RemoveConst<To>, RemoveConst<To>>()) &&
             IsSame<decltype(TryConvTest<From, To>(0)), hwy::SizeTag<1>>())
  };
};

#if HWY_COMPILER_MSVC
HWY_DIAGNOSTICS(pop)
#endif

template <class From, class To>
HWY_API constexpr bool IsConvertible() {
  return IsConvertibleT<From, To>::value;
}

template <class From, class To>
class IsStaticCastableT {
 private:
  template <class T, class U, class = decltype(static_cast<U>(DeclVal<T>()))>
  static hwy::SizeTag<1> TryStaticCastTest(int);

  template <class T, class U, class Arg>
  static hwy::SizeTag<0> TryStaticCastTest(Arg);

 public:
  enum {
    value = IsSame<decltype(TryStaticCastTest<From, To>(0)), hwy::SizeTag<1>>()
  };
};

template <class From, class To>
static constexpr bool IsStaticCastable() {
  return IsStaticCastableT<From, To>::value;
}

#define HWY_IF_CASTABLE(From, To) \
  hwy::EnableIf<IsStaticCastable<From, To>()>* = nullptr

#define HWY_IF_OP_CASTABLE(op, T, Native) \
  HWY_IF_CASTABLE(decltype(DeclVal<Native>() op DeclVal<T>()), Native)

template <class T, class From>
class IsAssignableT {
 private:
  template <class T1, class T2, class = decltype(DeclVal<T1>() = DeclVal<T2>())>
  static hwy::SizeTag<1> TryAssignTest(int);

  template <class T1, class T2, class Arg>
  static hwy::SizeTag<0> TryAssignTest(Arg);

 public:
  enum {
    value = IsSame<decltype(TryAssignTest<T, From>(0)), hwy::SizeTag<1>>()
  };
};

template <class T, class From>
static constexpr bool IsAssignable() {
  return IsAssignableT<T, From>::value;
}

#define HWY_IF_ASSIGNABLE(T, From) \
  hwy::EnableIf<IsAssignable<T, From>()>* = nullptr

// ----------------------------------------------------------------------------
// IsSpecialFloat

// These types are often special-cased and not supported in all ops.
template <typename T>
HWY_API constexpr bool IsSpecialFloat() {
  return IsSameEither<RemoveCvRef<T>, hwy::float16_t, hwy::bfloat16_t>();
}

// -----------------------------------------------------------------------------
// IsIntegerLaneType and IsInteger

template <class T>
HWY_API constexpr bool IsIntegerLaneType() {
  return false;
}
template <>
HWY_INLINE constexpr bool IsIntegerLaneType<int8_t>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsIntegerLaneType<uint8_t>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsIntegerLaneType<int16_t>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsIntegerLaneType<uint16_t>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsIntegerLaneType<int32_t>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsIntegerLaneType<uint32_t>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsIntegerLaneType<int64_t>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsIntegerLaneType<uint64_t>() {
  return true;
}

namespace detail {

template <class T>
static HWY_INLINE constexpr bool IsNonCvInteger() {
  // NOTE: Do not add a IsNonCvInteger<wchar_t>() specialization below as it is
  // possible for IsSame<wchar_t, uint16_t>() to be true when compiled with MSVC
  // with the /Zc:wchar_t- option.
  return IsIntegerLaneType<T>() || IsSame<T, wchar_t>() ||
         IsSameEither<T, size_t, ptrdiff_t>() ||
         IsSameEither<T, intptr_t, uintptr_t>();
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<bool>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<char>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<signed char>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<unsigned char>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<short>() {  // NOLINT
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<unsigned short>() {  // NOLINT
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<int>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<unsigned>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<long>() {  // NOLINT
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<unsigned long>() {  // NOLINT
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<long long>() {  // NOLINT
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<unsigned long long>() {  // NOLINT
  return true;
}
#if defined(__cpp_char8_t) && __cpp_char8_t >= 201811L
template <>
HWY_INLINE constexpr bool IsNonCvInteger<char8_t>() {
  return true;
}
#endif
template <>
HWY_INLINE constexpr bool IsNonCvInteger<char16_t>() {
  return true;
}
template <>
HWY_INLINE constexpr bool IsNonCvInteger<char32_t>() {
  return true;
}

}  // namespace detail

template <class T>
HWY_API constexpr bool IsInteger() {
  return detail::IsNonCvInteger<RemoveCvRef<T>>();
}

// -----------------------------------------------------------------------------
// BitCastScalar

#if HWY_HAS_BUILTIN(__builtin_bit_cast) || HWY_COMPILER_MSVC >= 1926
#define HWY_BITCASTSCALAR_CONSTEXPR constexpr
#else
#define HWY_BITCASTSCALAR_CONSTEXPR
#endif

#if __cpp_constexpr >= 201304L
#define HWY_BITCASTSCALAR_CXX14_CONSTEXPR HWY_BITCASTSCALAR_CONSTEXPR
#else
#define HWY_BITCASTSCALAR_CXX14_CONSTEXPR
#endif

#if HWY_HAS_BUILTIN(__builtin_bit_cast) || HWY_COMPILER_MSVC >= 1926
namespace detail {

template <class From>
struct BitCastScalarSrcCastHelper {
  static HWY_INLINE constexpr const From& CastSrcValRef(const From& val) {
    return val;
  }
};

#if HWY_COMPILER_CLANG >= 900 && HWY_COMPILER_CLANG < 1000
// Workaround for Clang 9 constexpr __builtin_bit_cast bug
template <class To, class From,
          hwy::EnableIf<hwy::IsInteger<RemoveCvRef<To>>() &&
                        hwy::IsInteger<RemoveCvRef<From>>()>* = nullptr>
static HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR To
BuiltinBitCastScalar(const From& val) {
  static_assert(sizeof(To) == sizeof(From),
                "sizeof(To) == sizeof(From) must be true");
  return static_cast<To>(val);
}

template <class To, class From,
          hwy::EnableIf<!(hwy::IsInteger<RemoveCvRef<To>>() &&
                          hwy::IsInteger<RemoveCvRef<From>>())>* = nullptr>
static HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR To
BuiltinBitCastScalar(const From& val) {
  return __builtin_bit_cast(To, val);
}
#endif  // HWY_COMPILER_CLANG >= 900 && HWY_COMPILER_CLANG < 1000

}  // namespace detail

template <class To, class From, HWY_IF_NOT_SPECIAL_FLOAT(To)>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR To BitCastScalar(const From& val) {
  // If From is hwy::float16_t or hwy::bfloat16_t, first cast val to either
  // const typename From::Native& or const uint16_t& using
  // detail::BitCastScalarSrcCastHelper<RemoveCvRef<From>>::CastSrcValRef to
  // allow BitCastScalar from hwy::float16_t or hwy::bfloat16_t to be constexpr
  // if To is not a pointer type, union type, or a struct/class containing a
  // pointer, union, or reference subobject
#if HWY_COMPILER_CLANG >= 900 && HWY_COMPILER_CLANG < 1000
  return detail::BuiltinBitCastScalar<To>(
      detail::BitCastScalarSrcCastHelper<RemoveCvRef<From>>::CastSrcValRef(
          val));
#else
  return __builtin_bit_cast(
      To, detail::BitCastScalarSrcCastHelper<RemoveCvRef<From>>::CastSrcValRef(
              val));
#endif
}
template <class To, class From, HWY_IF_SPECIAL_FLOAT(To)>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR To BitCastScalar(const From& val) {
  // If To is hwy::float16_t or hwy::bfloat16_t, first do a BitCastScalar of val
  // to uint16_t, and then bit cast the uint16_t value to To using To::FromBits
  // as hwy::float16_t::FromBits and hwy::bfloat16_t::FromBits are guaranteed to
  // be constexpr if the __builtin_bit_cast intrinsic is available.
  return To::FromBits(BitCastScalar<uint16_t>(val));
}
#else
template <class To, class From>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR To BitCastScalar(const From& val) {
  To result;
  CopySameSize(&val, &result);
  return result;
}
#endif

//------------------------------------------------------------------------------
// F16 lane type

#pragma pack(push, 1)

// Compiler supports __fp16 and load/store/conversion NEON intrinsics, which are
// included in Armv8 and VFPv4 (except with MSVC). On Armv7 Clang requires
// __ARM_FP & 2 whereas Armv7 GCC requires -mfp16-format=ieee.
#if (HWY_ARCH_ARM_A64 && !HWY_COMPILER_MSVC) ||                    \
    (HWY_COMPILER_CLANG && defined(__ARM_FP) && (__ARM_FP & 2)) || \
    (HWY_COMPILER_GCC_ACTUAL && defined(__ARM_FP16_FORMAT_IEEE))
#define HWY_NEON_HAVE_F16C 1
#else
#define HWY_NEON_HAVE_F16C 0
#endif

// RVV with f16 extension supports _Float16 and f16 vector ops. If set, implies
// HWY_HAVE_FLOAT16.
#if HWY_ARCH_RISCV && defined(__riscv_zvfh) && HWY_COMPILER_CLANG >= 1600
#define HWY_RVV_HAVE_F16_VEC 1
#else
#define HWY_RVV_HAVE_F16_VEC 0
#endif

// x86 compiler supports _Float16, not necessarily with operators.
// Avoid clang-cl because it lacks __extendhfsf2.
#if HWY_ARCH_X86 && defined(__SSE2__) && defined(__FLT16_MAX__) && \
    ((HWY_COMPILER_CLANG >= 1500 && !HWY_COMPILER_CLANGCL) ||      \
     HWY_COMPILER_GCC_ACTUAL >= 1200)
#define HWY_SSE2_HAVE_F16_TYPE 1
#else
#define HWY_SSE2_HAVE_F16_TYPE 0
#endif

#ifndef HWY_HAVE_SCALAR_F16_TYPE
// Compiler supports _Float16, not necessarily with operators.
#if HWY_NEON_HAVE_F16C || HWY_RVV_HAVE_F16_VEC || HWY_SSE2_HAVE_F16_TYPE
#define HWY_HAVE_SCALAR_F16_TYPE 1
#else
#define HWY_HAVE_SCALAR_F16_TYPE 0
#endif
#endif  // HWY_HAVE_SCALAR_F16_TYPE

#ifndef HWY_HAVE_SCALAR_F16_OPERATORS
// Recent enough compiler also has operators.
#if HWY_HAVE_SCALAR_F16_TYPE &&                                       \
    (HWY_COMPILER_CLANG >= 1800 || HWY_COMPILER_GCC_ACTUAL >= 1200 || \
     (HWY_COMPILER_CLANG >= 1500 && !HWY_COMPILER_CLANGCL &&          \
      !defined(_WIN32)) ||                                            \
     (HWY_ARCH_ARM &&                                                 \
      (HWY_COMPILER_CLANG >= 900 || HWY_COMPILER_GCC_ACTUAL >= 800)))
#define HWY_HAVE_SCALAR_F16_OPERATORS 1
#else
#define HWY_HAVE_SCALAR_F16_OPERATORS 0
#endif
#endif  // HWY_HAVE_SCALAR_F16_OPERATORS

namespace detail {

template <class T, class TVal = RemoveCvRef<T>, bool = IsSpecialFloat<TVal>()>
struct SpecialFloatUnwrapArithOpOperandT {};

template <class T, class TVal>
struct SpecialFloatUnwrapArithOpOperandT<T, TVal, false> {
  using type = T;
};

template <class T>
using SpecialFloatUnwrapArithOpOperand =
    typename SpecialFloatUnwrapArithOpOperandT<T>::type;

template <class T, class TVal = RemoveCvRef<T>>
struct NativeSpecialFloatToWrapperT {
  using type = T;
};

template <class T>
using NativeSpecialFloatToWrapper =
    typename NativeSpecialFloatToWrapperT<T>::type;

}  // namespace detail

// Match [u]int##_t naming scheme so rvv-inl.h macros can obtain the type name
// by concatenating base type and bits. We use a wrapper class instead of a
// typedef to the native type to ensure that the same symbols, e.g. for VQSort,
// are generated regardless of F16 support; see #1684.
struct alignas(2) float16_t {
#if HWY_HAVE_SCALAR_F16_TYPE
#if HWY_RVV_HAVE_F16_VEC || HWY_SSE2_HAVE_F16_TYPE
  using Native = _Float16;
#elif HWY_NEON_HAVE_F16C
  using Native = __fp16;
#else
#error "Logic error: condition should be 'all but NEON_HAVE_F16C'"
#endif
#elif HWY_IDE
  using Native = uint16_t;
#endif  // HWY_HAVE_SCALAR_F16_TYPE

  union {
#if HWY_HAVE_SCALAR_F16_TYPE || HWY_IDE
    // Accessed via NativeLaneType, and used directly if
    // HWY_HAVE_SCALAR_F16_OPERATORS.
    Native native;
#endif
    // Only accessed via NativeLaneType or U16LaneType.
    uint16_t bits;
  };

  // Default init and copying.
  float16_t() noexcept = default;
  constexpr float16_t(const float16_t&) noexcept = default;
  constexpr float16_t(float16_t&&) noexcept = default;
  float16_t& operator=(const float16_t&) noexcept = default;
  float16_t& operator=(float16_t&&) noexcept = default;

#if HWY_HAVE_SCALAR_F16_TYPE
  // NEON vget/set_lane intrinsics and SVE `svaddv` could use explicit
  // float16_t(intrinsic()), but user code expects implicit conversions.
  constexpr float16_t(Native arg) noexcept : native(arg) {}
  constexpr operator Native() const noexcept { return native; }
#endif

#if HWY_HAVE_SCALAR_F16_TYPE
  static HWY_BITCASTSCALAR_CONSTEXPR float16_t FromBits(uint16_t bits) {
    return float16_t(BitCastScalar<Native>(bits));
  }
#else

 private:
  struct F16FromU16BitsTag {};
  constexpr float16_t(F16FromU16BitsTag /*tag*/, uint16_t u16_bits)
      : bits(u16_bits) {}

 public:
  static constexpr float16_t FromBits(uint16_t bits) {
    return float16_t(F16FromU16BitsTag(), bits);
  }
#endif

  // When backed by a native type, ensure the wrapper behaves like the native
  // type by forwarding all operators. Unfortunately it seems difficult to reuse
  // this code in a base class, so we repeat it in float16_t.
#if HWY_HAVE_SCALAR_F16_OPERATORS || HWY_IDE
  template <typename T, hwy::EnableIf<!IsSame<RemoveCvRef<T>, float16_t>() &&
                                      IsConvertible<T, Native>()>* = nullptr>
  constexpr float16_t(T&& arg) noexcept
      : native(static_cast<Native>(static_cast<T&&>(arg))) {}

  template <typename T, hwy::EnableIf<!IsSame<RemoveCvRef<T>, float16_t>() &&
                                      !IsConvertible<T, Native>() &&
                                      IsStaticCastable<T, Native>()>* = nullptr>
  explicit constexpr float16_t(T&& arg) noexcept
      : native(static_cast<Native>(static_cast<T&&>(arg))) {}

  // pre-decrement operator (--x)
  HWY_CXX14_CONSTEXPR float16_t& operator--() noexcept {
    native = static_cast<Native>(native - Native{1});
    return *this;
  }

  // post-decrement operator (x--)
  HWY_CXX14_CONSTEXPR float16_t operator--(int) noexcept {
    float16_t result = *this;
    native = static_cast<Native>(native - Native{1});
    return result;
  }

  // pre-increment operator (++x)
  HWY_CXX14_CONSTEXPR float16_t& operator++() noexcept {
    native = static_cast<Native>(native + Native{1});
    return *this;
  }

  // post-increment operator (x++)
  HWY_CXX14_CONSTEXPR float16_t operator++(int) noexcept {
    float16_t result = *this;
    native = static_cast<Native>(native + Native{1});
    return result;
  }

  constexpr float16_t operator-() const noexcept {
    return float16_t(static_cast<Native>(-native));
  }
  constexpr float16_t operator+() const noexcept { return *this; }

  // Reduce clutter by generating `operator+` and `operator+=` etc. Note that
  // we cannot token-paste `operator` and `+`, so pass it in as `op_func`.
#define HWY_FLOAT16_BINARY_OP(op, op_func, assign_func)                      \
  constexpr float16_t op_func(const float16_t& rhs) const noexcept {         \
    return float16_t(static_cast<Native>(native op rhs.native));             \
  }                                                                          \
  template <typename T, HWY_IF_NOT_F16(T),                                   \
            typename UnwrappedT =                                            \
                detail::SpecialFloatUnwrapArithOpOperand<const T&>,          \
            typename RawResultT =                                            \
                decltype(DeclVal<Native>() op DeclVal<UnwrappedT>()),        \
            typename ResultT =                                               \
                detail::NativeSpecialFloatToWrapper<RawResultT>,             \
            HWY_IF_CASTABLE(RawResultT, ResultT)>                            \
  constexpr ResultT op_func(const T& rhs) const noexcept(noexcept(           \
      static_cast<ResultT>(DeclVal<Native>() op DeclVal<UnwrappedT>()))) {   \
    return static_cast<ResultT>(native op static_cast<UnwrappedT>(rhs));     \
  }                                                                          \
  HWY_CXX14_CONSTEXPR hwy::float16_t& assign_func(                           \
      const hwy::float16_t& rhs) noexcept {                                  \
    native = static_cast<Native>(native op rhs.native);                      \
    return *this;                                                            \
  }                                                                          \
  template <typename T, HWY_IF_NOT_F16(T),                                   \
            HWY_IF_OP_CASTABLE(op, const T&, Native),                        \
            HWY_IF_ASSIGNABLE(                                               \
                Native, decltype(DeclVal<Native>() op DeclVal<const T&>()))> \
  HWY_CXX14_CONSTEXPR hwy::float16_t& assign_func(const T& rhs) noexcept(    \
      noexcept(                                                              \
          static_cast<Native>(DeclVal<Native>() op DeclVal<const T&>()))) {  \
    native = static_cast<Native>(native op rhs);                             \
    return *this;                                                            \
  }

  HWY_FLOAT16_BINARY_OP(+, operator+, operator+=)
  HWY_FLOAT16_BINARY_OP(-, operator-, operator-=)
  HWY_FLOAT16_BINARY_OP(*, operator*, operator*=)
  HWY_FLOAT16_BINARY_OP(/, operator/, operator/=)
#undef HWY_FLOAT16_BINARY_OP

#endif  // HWY_HAVE_SCALAR_F16_OPERATORS
};
static_assert(sizeof(hwy::float16_t) == 2, "Wrong size of float16_t");

#if HWY_HAVE_SCALAR_F16_TYPE
namespace detail {

#if HWY_HAVE_SCALAR_F16_OPERATORS
template <class T>
struct SpecialFloatUnwrapArithOpOperandT<T, hwy::float16_t, true> {
  using type = hwy::float16_t::Native;
};
#endif

template <class T>
struct NativeSpecialFloatToWrapperT<T, hwy::float16_t::Native> {
  using type = hwy::float16_t;
};

}  // namespace detail
#endif  // HWY_HAVE_SCALAR_F16_TYPE

#if HWY_HAS_BUILTIN(__builtin_bit_cast) || HWY_COMPILER_MSVC >= 1926
namespace detail {

template <>
struct BitCastScalarSrcCastHelper<hwy::float16_t> {
#if HWY_HAVE_SCALAR_F16_TYPE
  static HWY_INLINE constexpr const hwy::float16_t::Native& CastSrcValRef(
      const hwy::float16_t& val) {
    return val.native;
  }
#else
  static HWY_INLINE constexpr const uint16_t& CastSrcValRef(
      const hwy::float16_t& val) {
    return val.bits;
  }
#endif
};

}  // namespace detail
#endif  // HWY_HAS_BUILTIN(__builtin_bit_cast) || HWY_COMPILER_MSVC >= 1926

#if HWY_HAVE_SCALAR_F16_OPERATORS
#define HWY_F16_CONSTEXPR constexpr
#else
#define HWY_F16_CONSTEXPR HWY_BITCASTSCALAR_CXX14_CONSTEXPR
#endif  // HWY_HAVE_SCALAR_F16_OPERATORS

HWY_API HWY_F16_CONSTEXPR float F32FromF16(float16_t f16) {
#if HWY_HAVE_SCALAR_F16_OPERATORS && !HWY_IDE
  return static_cast<float>(f16);
#endif
#if !HWY_HAVE_SCALAR_F16_OPERATORS || HWY_IDE
  const uint16_t bits16 = BitCastScalar<uint16_t>(f16);
  const uint32_t sign = static_cast<uint32_t>(bits16 >> 15);
  const uint32_t biased_exp = (bits16 >> 10) & 0x1F;
  const uint32_t mantissa = bits16 & 0x3FF;

  // Subnormal or zero
  if (biased_exp == 0) {
    const float subnormal =
        (1.0f / 16384) * (static_cast<float>(mantissa) * (1.0f / 1024));
    return sign ? -subnormal : subnormal;
  }

  // Normalized, infinity or NaN: convert the representation directly
  // (faster than ldexp/tables).
  const uint32_t biased_exp32 =
      biased_exp == 31 ? 0xFF : biased_exp + (127 - 15);
  const uint32_t mantissa32 = mantissa << (23 - 10);
  const uint32_t bits32 = (sign << 31) | (biased_exp32 << 23) | mantissa32;

  return BitCastScalar<float>(bits32);
#endif  // !HWY_HAVE_SCALAR_F16_OPERATORS
}

#if HWY_IS_DEBUG_BUILD && \
    (HWY_HAS_BUILTIN(__builtin_bit_cast) || HWY_COMPILER_MSVC >= 1926)
#if defined(__cpp_if_consteval) && __cpp_if_consteval >= 202106L
// If C++23 if !consteval support is available, only execute
// HWY_DASSERT(condition) if F16FromF32 is not called from a constant-evaluated
// context to avoid compilation errors.
#define HWY_F16_FROM_F32_DASSERT(condition) \
  do {                                      \
    if !consteval {                         \
      HWY_DASSERT(condition);               \
    }                                       \
  } while (0)
#elif HWY_HAS_BUILTIN(__builtin_is_constant_evaluated) || \
    HWY_COMPILER_MSVC >= 1926
// If the __builtin_is_constant_evaluated() intrinsic is available,
// only do HWY_DASSERT(condition) if __builtin_is_constant_evaluated() returns
// false to avoid compilation errors if F16FromF32 is called from a
// constant-evaluated context.
#define HWY_F16_FROM_F32_DASSERT(condition)   \
  do {                                        \
    if (!__builtin_is_constant_evaluated()) { \
      HWY_DASSERT(condition);                 \
    }                                         \
  } while (0)
#else
// If C++23 if !consteval support is not available,
// the __builtin_is_constant_evaluated() intrinsic is not available,
// HWY_IS_DEBUG_BUILD is 1, and the __builtin_bit_cast intrinsic is available,
// do not do a HWY_DASSERT to avoid compilation errors if F16FromF32 is
// called from a constant-evaluated context.
#define HWY_F16_FROM_F32_DASSERT(condition) \
  do {                                      \
  } while (0)
#endif  // defined(__cpp_if_consteval) && __cpp_if_consteval >= 202106L
#else
// If HWY_IS_DEBUG_BUILD is 0 or the __builtin_bit_cast intrinsic is not
// available, define HWY_F16_FROM_F32_DASSERT(condition) as
// HWY_DASSERT(condition)
#define HWY_F16_FROM_F32_DASSERT(condition) HWY_DASSERT(condition)
#endif  // HWY_IS_DEBUG_BUILD && (HWY_HAS_BUILTIN(__builtin_bit_cast) ||
        // HWY_COMPILER_MSVC >= 1926)

HWY_API HWY_F16_CONSTEXPR float16_t F16FromF32(float f32) {
#if HWY_HAVE_SCALAR_F16_OPERATORS && !HWY_IDE
  return float16_t(static_cast<float16_t::Native>(f32));
#endif
#if !HWY_HAVE_SCALAR_F16_OPERATORS || HWY_IDE
  const uint32_t bits32 = BitCastScalar<uint32_t>(f32);
  const uint32_t sign = bits32 >> 31;
  const uint32_t biased_exp32 = (bits32 >> 23) & 0xFF;
  constexpr uint32_t kMantissaMask = 0x7FFFFF;
  const uint32_t mantissa32 = bits32 & kMantissaMask;

  // Before shifting (truncation), round to nearest even to reduce bias. If
  // the lowest remaining mantissa bit is odd, increase the offset. Example
  // with the lowest remaining bit (left) and next lower two bits; the
  // latter, plus two more, will be truncated.
  // 0[00] +  1 =  0[01]
  // 0[01] +  1 =  0[10]
  // 0[10] +  1 =  0[11]  (round down toward even)
  // 0[11] +  1 =  1[00]  (round up)
  // 1[00] + 10 =  1[10]
  // 1[01] + 10 =  1[11]
  // 1[10] + 10 = C0[00]  (round up toward even with C=1 carry out)
  // 1[11] + 10 = C0[01]  (round up toward even with C=1 carry out)

  // If |f32| >= 2^-24, f16_ulp_bit_idx is the index of the F32 mantissa bit
  // that will be shifted down into the ULP bit of the rounded down F16 result

  // The biased F32 exponent of 2^-14 (the smallest positive normal F16 value)
  // is 113, and bit 13 of the F32 mantissa will be shifted down to into the ULP
  // bit of the rounded down F16 result if |f32| >= 2^14

  // If |f32| < 2^-24, f16_ulp_bit_idx is equal to 24 as there are 24 mantissa
  // bits (including the implied 1 bit) in the mantissa of a normal F32 value
  // and as we want to round up the mantissa if |f32| > 2^-25 && |f32| < 2^-24
  const int32_t f16_ulp_bit_idx =
      HWY_MIN(HWY_MAX(126 - static_cast<int32_t>(biased_exp32), 13), 24);
  const uint32_t odd_bit = ((mantissa32 | 0x800000u) >> f16_ulp_bit_idx) & 1;
  const uint32_t rounded =
      mantissa32 + odd_bit + (uint32_t{1} << (f16_ulp_bit_idx - 1)) - 1u;
  const bool carry = rounded >= (1u << 23);

  const int32_t exp = static_cast<int32_t>(biased_exp32) - 127 + carry;

  // Tiny or zero => zero.
  if (exp < -24) {
    // restore original sign
    return float16_t::FromBits(static_cast<uint16_t>(sign << 15));
  }

  // If biased_exp16 would be >= 31, first check whether the input was NaN so we
  // can set the mantissa to nonzero.
  const bool is_nan = (biased_exp32 == 255) && mantissa32 != 0;
  const bool overflowed = exp >= 16;
  const uint32_t biased_exp16 =
      static_cast<uint32_t>(HWY_MIN(HWY_MAX(0, exp + 15), 31));
  // exp = [-24, -15] => subnormal, shift the mantissa.
  const uint32_t sub_exp = static_cast<uint32_t>(HWY_MAX(-14 - exp, 0));
  HWY_F16_FROM_F32_DASSERT(sub_exp < 11);
  const uint32_t shifted_mantissa =
      (rounded & kMantissaMask) >> (23 - 10 + sub_exp);
  const uint32_t leading = sub_exp == 0u ? 0u : (1024u >> sub_exp);
  const uint32_t mantissa16 = is_nan       ? 0x3FF
                              : overflowed ? 0u
                                           : (leading + shifted_mantissa);

#if HWY_IS_DEBUG_BUILD
  if (exp < -14) {
    HWY_F16_FROM_F32_DASSERT(biased_exp16 == 0);
    HWY_F16_FROM_F32_DASSERT(sub_exp >= 1);
  } else if (exp <= 15) {
    HWY_F16_FROM_F32_DASSERT(1 <= biased_exp16 && biased_exp16 < 31);
    HWY_F16_FROM_F32_DASSERT(sub_exp == 0);
  }
#endif

  HWY_F16_FROM_F32_DASSERT(mantissa16 < 1024);
  const uint32_t bits16 = (sign << 15) | (biased_exp16 << 10) | mantissa16;
  HWY_F16_FROM_F32_DASSERT(bits16 < 0x10000);
  const uint16_t narrowed = static_cast<uint16_t>(bits16);  // big-endian safe
  return float16_t::FromBits(narrowed);
#endif  // !HWY_HAVE_SCALAR_F16_OPERATORS
}

HWY_API HWY_F16_CONSTEXPR float16_t F16FromF64(double f64) {
#if HWY_HAVE_SCALAR_F16_OPERATORS
  return float16_t(static_cast<float16_t::Native>(f64));
#else
  // The mantissa bits of f64 are first rounded using round-to-odd rounding
  // to the nearest f64 value that has the lower 29 bits zeroed out to
  // ensure that the result is correctly rounded to a F16.

  // The F64 round-to-odd operation below will round a normal F64 value
  // (using round-to-odd rounding) to a F64 value that has 24 bits of precision.

  // It is okay if the magnitude of a denormal F64 value is rounded up in the
  // F64 round-to-odd step below as the magnitude of a denormal F64 value is
  // much smaller than 2^(-24) (the smallest positive denormal F16 value).

  // It is also okay if bit 29 of a NaN F64 value is changed by the F64
  // round-to-odd step below as the lower 13 bits of a F32 NaN value are usually
  // discarded or ignored by the conversion of a F32 NaN value to a F16.

  // If f64 is a NaN value, the result of the F64 round-to-odd step will be a
  // NaN value as the result of the F64 round-to-odd step will have at least one
  // mantissa bit if f64 is a NaN value.

  // The F64 round-to-odd step will ensure that the F64 to F32 conversion is
  // exact if the magnitude of the rounded F64 value (using round-to-odd
  // rounding) is between 2^(-126) (the smallest normal F32 value) and
  // HighestValue<float>() (the largest finite F32 value)

  // It is okay if the F64 to F32 conversion is inexact for F64 values that have
  // a magnitude that is less than 2^(-126) as the magnitude of a denormal F32
  // value is much smaller than 2^(-24) (the smallest positive denormal F16
  // value).

  return F16FromF32(
      static_cast<float>(BitCastScalar<double>(static_cast<uint64_t>(
          (BitCastScalar<uint64_t>(f64) & 0xFFFFFFFFE0000000ULL) |
          ((BitCastScalar<uint64_t>(f64) + 0x000000001FFFFFFFULL) &
           0x0000000020000000ULL)))));
#endif
}

// More convenient to define outside float16_t because these may use
// F32FromF16, which is defined after the struct.
HWY_F16_CONSTEXPR inline bool operator==(float16_t lhs,
                                         float16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_F16_OPERATORS
  return lhs.native == rhs.native;
#else
  return F32FromF16(lhs) == F32FromF16(rhs);
#endif
}
HWY_F16_CONSTEXPR inline bool operator!=(float16_t lhs,
                                         float16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_F16_OPERATORS
  return lhs.native != rhs.native;
#else
  return F32FromF16(lhs) != F32FromF16(rhs);
#endif
}
HWY_F16_CONSTEXPR inline bool operator<(float16_t lhs, float16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_F16_OPERATORS
  return lhs.native < rhs.native;
#else
  return F32FromF16(lhs) < F32FromF16(rhs);
#endif
}
HWY_F16_CONSTEXPR inline bool operator<=(float16_t lhs,
                                         float16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_F16_OPERATORS
  return lhs.native <= rhs.native;
#else
  return F32FromF16(lhs) <= F32FromF16(rhs);
#endif
}
HWY_F16_CONSTEXPR inline bool operator>(float16_t lhs, float16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_F16_OPERATORS
  return lhs.native > rhs.native;
#else
  return F32FromF16(lhs) > F32FromF16(rhs);
#endif
}
HWY_F16_CONSTEXPR inline bool operator>=(float16_t lhs,
                                         float16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_F16_OPERATORS
  return lhs.native >= rhs.native;
#else
  return F32FromF16(lhs) >= F32FromF16(rhs);
#endif
}
#if HWY_HAVE_CXX20_THREE_WAY_COMPARE
HWY_F16_CONSTEXPR inline std::partial_ordering operator<=>(
    float16_t lhs, float16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_F16_OPERATORS
  return lhs.native <=> rhs.native;
#else
  return F32FromF16(lhs) <=> F32FromF16(rhs);
#endif
}
#endif  // HWY_HAVE_CXX20_THREE_WAY_COMPARE

//------------------------------------------------------------------------------
// BF16 lane type

// Compiler supports ACLE __bf16, not necessarily with operators.

// Disable the __bf16 type on AArch64 with GCC 13 or earlier as there is a bug
// in GCC 13 and earlier that sometimes causes BF16 constant values to be
// incorrectly loaded on AArch64, and this GCC bug on AArch64 is
// described at https://gcc.gnu.org/bugzilla/show_bug.cgi?id=111867.

#if HWY_ARCH_ARM_A64 && \
    (HWY_COMPILER_CLANG >= 1700 || HWY_COMPILER_GCC_ACTUAL >= 1400)
#define HWY_ARM_HAVE_SCALAR_BF16_TYPE 1
#else
#define HWY_ARM_HAVE_SCALAR_BF16_TYPE 0
#endif

// x86 compiler supports __bf16, not necessarily with operators.
#ifndef HWY_SSE2_HAVE_SCALAR_BF16_TYPE
#if HWY_ARCH_X86 && defined(__SSE2__) &&                      \
    ((HWY_COMPILER_CLANG >= 1700 && !HWY_COMPILER_CLANGCL) || \
     HWY_COMPILER_GCC_ACTUAL >= 1300)
#define HWY_SSE2_HAVE_SCALAR_BF16_TYPE 1
#else
#define HWY_SSE2_HAVE_SCALAR_BF16_TYPE 0
#endif
#endif  // HWY_SSE2_HAVE_SCALAR_BF16_TYPE

// Compiler supports __bf16, not necessarily with operators.
#if HWY_ARM_HAVE_SCALAR_BF16_TYPE || HWY_SSE2_HAVE_SCALAR_BF16_TYPE
#define HWY_HAVE_SCALAR_BF16_TYPE 1
#else
#define HWY_HAVE_SCALAR_BF16_TYPE 0
#endif

#ifndef HWY_HAVE_SCALAR_BF16_OPERATORS
// Recent enough compiler also has operators. aarch64 clang 18 hits internal
// compiler errors on bf16 ToString, hence only enable on GCC for now.
#if HWY_HAVE_SCALAR_BF16_TYPE && (HWY_COMPILER_GCC_ACTUAL >= 1300)
#define HWY_HAVE_SCALAR_BF16_OPERATORS 1
#else
#define HWY_HAVE_SCALAR_BF16_OPERATORS 0
#endif
#endif  // HWY_HAVE_SCALAR_BF16_OPERATORS

#if HWY_HAVE_SCALAR_BF16_OPERATORS
#define HWY_BF16_CONSTEXPR constexpr
#else
#define HWY_BF16_CONSTEXPR HWY_BITCASTSCALAR_CONSTEXPR
#endif

struct alignas(2) bfloat16_t {
#if HWY_HAVE_SCALAR_BF16_TYPE
  using Native = __bf16;
#elif HWY_IDE
  using Native = uint16_t;
#endif

  union {
#if HWY_HAVE_SCALAR_BF16_TYPE || HWY_IDE
    // Accessed via NativeLaneType, and used directly if
    // HWY_HAVE_SCALAR_BF16_OPERATORS.
    Native native;
#endif
    // Only accessed via NativeLaneType or U16LaneType.
    uint16_t bits;
  };

  // Default init and copying
  bfloat16_t() noexcept = default;
  constexpr bfloat16_t(bfloat16_t&&) noexcept = default;
  constexpr bfloat16_t(const bfloat16_t&) noexcept = default;
  bfloat16_t& operator=(bfloat16_t&& arg) noexcept = default;
  bfloat16_t& operator=(const bfloat16_t& arg) noexcept = default;

// Only enable implicit conversions if we have a native type.
#if HWY_HAVE_SCALAR_BF16_TYPE || HWY_IDE
  constexpr bfloat16_t(Native arg) noexcept : native(arg) {}
  constexpr operator Native() const noexcept { return native; }
#endif

#if HWY_HAVE_SCALAR_BF16_TYPE
  static HWY_BITCASTSCALAR_CONSTEXPR bfloat16_t FromBits(uint16_t bits) {
    return bfloat16_t(BitCastScalar<Native>(bits));
  }
#else

 private:
  struct BF16FromU16BitsTag {};
  constexpr bfloat16_t(BF16FromU16BitsTag /*tag*/, uint16_t u16_bits)
      : bits(u16_bits) {}

 public:
  static constexpr bfloat16_t FromBits(uint16_t bits) {
    return bfloat16_t(BF16FromU16BitsTag(), bits);
  }
#endif

  // When backed by a native type, ensure the wrapper behaves like the native
  // type by forwarding all operators. Unfortunately it seems difficult to reuse
  // this code in a base class, so we repeat it in float16_t.
#if HWY_HAVE_SCALAR_BF16_OPERATORS || HWY_IDE
  template <typename T, hwy::EnableIf<!IsSame<RemoveCvRef<T>, Native>() &&
                                      !IsSame<RemoveCvRef<T>, bfloat16_t>() &&
                                      IsConvertible<T, Native>()>* = nullptr>
  constexpr bfloat16_t(T&& arg) noexcept(
      noexcept(static_cast<Native>(DeclVal<T>())))
      : native(static_cast<Native>(static_cast<T&&>(arg))) {}

  template <typename T, hwy::EnableIf<!IsSame<RemoveCvRef<T>, Native>() &&
                                      !IsSame<RemoveCvRef<T>, bfloat16_t>() &&
                                      !IsConvertible<T, Native>() &&
                                      IsStaticCastable<T, Native>()>* = nullptr>
  explicit constexpr bfloat16_t(T&& arg) noexcept(
      noexcept(static_cast<Native>(DeclVal<T>())))
      : native(static_cast<Native>(static_cast<T&&>(arg))) {}

  HWY_CXX14_CONSTEXPR bfloat16_t& operator=(Native arg) noexcept {
    native = arg;
    return *this;
  }

  // pre-decrement operator (--x)
  HWY_CXX14_CONSTEXPR bfloat16_t& operator--() noexcept {
    native = static_cast<Native>(native - Native{1});
    return *this;
  }

  // post-decrement operator (x--)
  HWY_CXX14_CONSTEXPR bfloat16_t operator--(int) noexcept {
    bfloat16_t result = *this;
    native = static_cast<Native>(native - Native{1});
    return result;
  }

  // pre-increment operator (++x)
  HWY_CXX14_CONSTEXPR bfloat16_t& operator++() noexcept {
    native = static_cast<Native>(native + Native{1});
    return *this;
  }

  // post-increment operator (x++)
  HWY_CXX14_CONSTEXPR bfloat16_t operator++(int) noexcept {
    bfloat16_t result = *this;
    native = static_cast<Native>(native + Native{1});
    return result;
  }

  constexpr bfloat16_t operator-() const noexcept {
    return bfloat16_t(static_cast<Native>(-native));
  }
  constexpr bfloat16_t operator+() const noexcept { return *this; }

  // Reduce clutter by generating `operator+` and `operator+=` etc. Note that
  // we cannot token-paste `operator` and `+`, so pass it in as `op_func`.
#define HWY_BFLOAT16_BINARY_OP(op, op_func, assign_func)                     \
  constexpr bfloat16_t op_func(const bfloat16_t& rhs) const noexcept {       \
    return bfloat16_t(static_cast<Native>(native op rhs.native));            \
  }                                                                          \
  template <typename T, HWY_IF_NOT_BF16(T),                                  \
            typename UnwrappedT =                                            \
                detail::SpecialFloatUnwrapArithOpOperand<const T&>,          \
            typename RawResultT =                                            \
                decltype(DeclVal<Native>() op DeclVal<UnwrappedT>()),        \
            typename ResultT =                                               \
                detail::NativeSpecialFloatToWrapper<RawResultT>,             \
            HWY_IF_CASTABLE(RawResultT, ResultT)>                            \
  constexpr ResultT op_func(const T& rhs) const noexcept(noexcept(           \
      static_cast<ResultT>(DeclVal<Native>() op DeclVal<UnwrappedT>()))) {   \
    return static_cast<ResultT>(native op static_cast<UnwrappedT>(rhs));     \
  }                                                                          \
  HWY_CXX14_CONSTEXPR hwy::bfloat16_t& assign_func(                          \
      const hwy::bfloat16_t& rhs) noexcept {                                 \
    native = static_cast<Native>(native op rhs.native);                      \
    return *this;                                                            \
  }                                                                          \
  template <typename T, HWY_IF_NOT_BF16(T),                                  \
            HWY_IF_OP_CASTABLE(op, const T&, Native),                        \
            HWY_IF_ASSIGNABLE(                                               \
                Native, decltype(DeclVal<Native>() op DeclVal<const T&>()))> \
  HWY_CXX14_CONSTEXPR hwy::bfloat16_t& assign_func(const T& rhs) noexcept(   \
      noexcept(                                                              \
          static_cast<Native>(DeclVal<Native>() op DeclVal<const T&>()))) {  \
    native = static_cast<Native>(native op rhs);                             \
    return *this;                                                            \
  }
  HWY_BFLOAT16_BINARY_OP(+, operator+, operator+=)
  HWY_BFLOAT16_BINARY_OP(-, operator-, operator-=)
  HWY_BFLOAT16_BINARY_OP(*, operator*, operator*=)
  HWY_BFLOAT16_BINARY_OP(/, operator/, operator/=)
#undef HWY_BFLOAT16_BINARY_OP

#endif  // HWY_HAVE_SCALAR_BF16_OPERATORS
};
static_assert(sizeof(hwy::bfloat16_t) == 2, "Wrong size of bfloat16_t");

#pragma pack(pop)

#if HWY_HAVE_SCALAR_BF16_TYPE
namespace detail {

#if HWY_HAVE_SCALAR_BF16_OPERATORS
template <class T>
struct SpecialFloatUnwrapArithOpOperandT<T, hwy::bfloat16_t, true> {
  using type = hwy::bfloat16_t::Native;
};
#endif

template <class T>
struct NativeSpecialFloatToWrapperT<T, hwy::bfloat16_t::Native> {
  using type = hwy::bfloat16_t;
};

}  // namespace detail
#endif  // HWY_HAVE_SCALAR_BF16_TYPE

#if HWY_HAS_BUILTIN(__builtin_bit_cast) || HWY_COMPILER_MSVC >= 1926
namespace detail {

template <>
struct BitCastScalarSrcCastHelper<hwy::bfloat16_t> {
#if HWY_HAVE_SCALAR_BF16_TYPE
  static HWY_INLINE constexpr const hwy::bfloat16_t::Native& CastSrcValRef(
      const hwy::bfloat16_t& val) {
    return val.native;
  }
#else
  static HWY_INLINE constexpr const uint16_t& CastSrcValRef(
      const hwy::bfloat16_t& val) {
    return val.bits;
  }
#endif
};

}  // namespace detail
#endif  // HWY_HAS_BUILTIN(__builtin_bit_cast) || HWY_COMPILER_MSVC >= 1926

HWY_API HWY_BF16_CONSTEXPR float F32FromBF16(bfloat16_t bf) {
#if HWY_HAVE_SCALAR_BF16_OPERATORS
  return static_cast<float>(bf);
#else
  return BitCastScalar<float>(static_cast<uint32_t>(
      static_cast<uint32_t>(BitCastScalar<uint16_t>(bf)) << 16));
#endif
}

namespace detail {

// Returns the increment to add to the bits of a finite F32 value to round a
// finite F32 to the nearest BF16 value
static HWY_INLINE HWY_MAYBE_UNUSED constexpr uint32_t F32BitsToBF16RoundIncr(
    const uint32_t f32_bits) {
  return static_cast<uint32_t>(((f32_bits & 0x7FFFFFFFu) < 0x7F800000u)
                                   ? (0x7FFFu + ((f32_bits >> 16) & 1u))
                                   : 0u);
}

// Converts f32_bits (which is the bits of a F32 value) to BF16 bits,
// rounded to the nearest F16 value
static HWY_INLINE HWY_MAYBE_UNUSED constexpr uint16_t F32BitsToBF16Bits(
    const uint32_t f32_bits) {
  // Round f32_bits to the nearest BF16 by first adding
  // F32BitsToBF16RoundIncr(f32_bits) to f32_bits and then right shifting
  // f32_bits + F32BitsToBF16RoundIncr(f32_bits) by 16

  // If f32_bits is the bit representation of a NaN F32 value, make sure that
  // bit 6 of the BF16 result is set to convert SNaN F32 values to QNaN BF16
  // values and to prevent NaN F32 values from being converted to an infinite
  // BF16 value
  return static_cast<uint16_t>(
      ((f32_bits + F32BitsToBF16RoundIncr(f32_bits)) >> 16) |
      (static_cast<uint32_t>((f32_bits & 0x7FFFFFFFu) > 0x7F800000u) << 6));
}

}  // namespace detail

HWY_API HWY_BF16_CONSTEXPR bfloat16_t BF16FromF32(float f) {
#if HWY_HAVE_SCALAR_BF16_OPERATORS
  return static_cast<bfloat16_t>(f);
#else
  return bfloat16_t::FromBits(
      detail::F32BitsToBF16Bits(BitCastScalar<uint32_t>(f)));
#endif
}

HWY_API HWY_BF16_CONSTEXPR bfloat16_t BF16FromF64(double f64) {
#if HWY_HAVE_SCALAR_BF16_OPERATORS
  return static_cast<bfloat16_t>(f64);
#else
  // The mantissa bits of f64 are first rounded using round-to-odd rounding
  // to the nearest f64 value that has the lower 38 bits zeroed out to
  // ensure that the result is correctly rounded to a BF16.

  // The F64 round-to-odd operation below will round a normal F64 value
  // (using round-to-odd rounding) to a F64 value that has 15 bits of precision.

  // It is okay if the magnitude of a denormal F64 value is rounded up in the
  // F64 round-to-odd step below as the magnitude of a denormal F64 value is
  // much smaller than 2^(-133) (the smallest positive denormal BF16 value).

  // It is also okay if bit 38 of a NaN F64 value is changed by the F64
  // round-to-odd step below as the lower 16 bits of a F32 NaN value are usually
  // discarded or ignored by the conversion of a F32 NaN value to a BF16.

  // If f64 is a NaN value, the result of the F64 round-to-odd step will be a
  // NaN value as the result of the F64 round-to-odd step will have at least one
  // mantissa bit if f64 is a NaN value.

  // The F64 round-to-odd step below will ensure that the F64 to F32 conversion
  // is exact if the magnitude of the rounded F64 value (using round-to-odd
  // rounding) is between 2^(-135) (one-fourth of the smallest positive denormal
  // BF16 value) and HighestValue<float>() (the largest finite F32 value).

  // If |f64| is less than 2^(-135), the magnitude of the result of the F64 to
  // F32 conversion is guaranteed to be less than or equal to 2^(-135), which
  // ensures that the F32 to BF16 conversion is correctly rounded, even if the
  // conversion of a rounded F64 value whose magnitude is less than 2^(-135)
  // to a F32 is inexact.

  return BF16FromF32(
      static_cast<float>(BitCastScalar<double>(static_cast<uint64_t>(
          (BitCastScalar<uint64_t>(f64) & 0xFFFFFFC000000000ULL) |
          ((BitCastScalar<uint64_t>(f64) + 0x0000003FFFFFFFFFULL) &
           0x0000004000000000ULL)))));
#endif
}

// More convenient to define outside bfloat16_t because these may use
// F32FromBF16, which is defined after the struct.

HWY_BF16_CONSTEXPR inline bool operator==(bfloat16_t lhs,
                                          bfloat16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_BF16_OPERATORS
  return lhs.native == rhs.native;
#else
  return F32FromBF16(lhs) == F32FromBF16(rhs);
#endif
}

HWY_BF16_CONSTEXPR inline bool operator!=(bfloat16_t lhs,
                                          bfloat16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_BF16_OPERATORS
  return lhs.native != rhs.native;
#else
  return F32FromBF16(lhs) != F32FromBF16(rhs);
#endif
}
HWY_BF16_CONSTEXPR inline bool operator<(bfloat16_t lhs,
                                         bfloat16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_BF16_OPERATORS
  return lhs.native < rhs.native;
#else
  return F32FromBF16(lhs) < F32FromBF16(rhs);
#endif
}
HWY_BF16_CONSTEXPR inline bool operator<=(bfloat16_t lhs,
                                          bfloat16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_BF16_OPERATORS
  return lhs.native <= rhs.native;
#else
  return F32FromBF16(lhs) <= F32FromBF16(rhs);
#endif
}
HWY_BF16_CONSTEXPR inline bool operator>(bfloat16_t lhs,
                                         bfloat16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_BF16_OPERATORS
  return lhs.native > rhs.native;
#else
  return F32FromBF16(lhs) > F32FromBF16(rhs);
#endif
}
HWY_BF16_CONSTEXPR inline bool operator>=(bfloat16_t lhs,
                                          bfloat16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_BF16_OPERATORS
  return lhs.native >= rhs.native;
#else
  return F32FromBF16(lhs) >= F32FromBF16(rhs);
#endif
}
#if HWY_HAVE_CXX20_THREE_WAY_COMPARE
HWY_BF16_CONSTEXPR inline std::partial_ordering operator<=>(
    bfloat16_t lhs, bfloat16_t rhs) noexcept {
#if HWY_HAVE_SCALAR_BF16_OPERATORS
  return lhs.native <=> rhs.native;
#else
  return F32FromBF16(lhs) <=> F32FromBF16(rhs);
#endif
}
#endif  // HWY_HAVE_CXX20_THREE_WAY_COMPARE

//------------------------------------------------------------------------------
// Type relations

namespace detail {

template <typename T>
struct Relations;
template <>
struct Relations<uint8_t> {
  using Unsigned = uint8_t;
  using Signed = int8_t;
  using Wide = uint16_t;
  enum { is_signed = 0, is_float = 0, is_bf16 = 0 };
};
template <>
struct Relations<int8_t> {
  using Unsigned = uint8_t;
  using Signed = int8_t;
  using Wide = int16_t;
  enum { is_signed = 1, is_float = 0, is_bf16 = 0 };
};
template <>
struct Relations<uint16_t> {
  using Unsigned = uint16_t;
  using Signed = int16_t;
  using Float = float16_t;
  using Wide = uint32_t;
  using Narrow = uint8_t;
  enum { is_signed = 0, is_float = 0, is_bf16 = 0 };
};
template <>
struct Relations<int16_t> {
  using Unsigned = uint16_t;
  using Signed = int16_t;
  using Float = float16_t;
  using Wide = int32_t;
  using Narrow = int8_t;
  enum { is_signed = 1, is_float = 0, is_bf16 = 0 };
};
template <>
struct Relations<uint32_t> {
  using Unsigned = uint32_t;
  using Signed = int32_t;
  using Float = float;
  using Wide = uint64_t;
  using Narrow = uint16_t;
  enum { is_signed = 0, is_float = 0, is_bf16 = 0 };
};
template <>
struct Relations<int32_t> {
  using Unsigned = uint32_t;
  using Signed = int32_t;
  using Float = float;
  using Wide = int64_t;
  using Narrow = int16_t;
  enum { is_signed = 1, is_float = 0, is_bf16 = 0 };
};
template <>
struct Relations<uint64_t> {
  using Unsigned = uint64_t;
  using Signed = int64_t;
  using Float = double;
  using Wide = uint128_t;
  using Narrow = uint32_t;
  enum { is_signed = 0, is_float = 0, is_bf16 = 0 };
};
template <>
struct Relations<int64_t> {
  using Unsigned = uint64_t;
  using Signed = int64_t;
  using Float = double;
  using Narrow = int32_t;
  enum { is_signed = 1, is_float = 0, is_bf16 = 0 };
};
template <>
struct Relations<uint128_t> {
  using Unsigned = uint128_t;
  using Narrow = uint64_t;
  enum { is_signed = 0, is_float = 0, is_bf16 = 0 };
};
template <>
struct Relations<float16_t> {
  using Unsigned = uint16_t;
  using Signed = int16_t;
  using Float = float16_t;
  using Wide = float;
  enum { is_signed = 1, is_float = 1, is_bf16 = 0 };
};
template <>
struct Relations<bfloat16_t> {
  using Unsigned = uint16_t;
  using Signed = int16_t;
  using Wide = float;
  enum { is_signed = 1, is_float = 1, is_bf16 = 1 };
};
template <>
struct Relations<float> {
  using Unsigned = uint32_t;
  using Signed = int32_t;
  using Float = float;
  using Wide = double;
  using Narrow = float16_t;
  enum { is_signed = 1, is_float = 1, is_bf16 = 0 };
};
template <>
struct Relations<double> {
  using Unsigned = uint64_t;
  using Signed = int64_t;
  using Float = double;
  using Narrow = float;
  enum { is_signed = 1, is_float = 1, is_bf16 = 0 };
};

template <size_t N>
struct TypeFromSize;
template <>
struct TypeFromSize<1> {
  using Unsigned = uint8_t;
  using Signed = int8_t;
};
template <>
struct TypeFromSize<2> {
  using Unsigned = uint16_t;
  using Signed = int16_t;
  using Float = float16_t;
};
template <>
struct TypeFromSize<4> {
  using Unsigned = uint32_t;
  using Signed = int32_t;
  using Float = float;
};
template <>
struct TypeFromSize<8> {
  using Unsigned = uint64_t;
  using Signed = int64_t;
  using Float = double;
};
template <>
struct TypeFromSize<16> {
  using Unsigned = uint128_t;
};

}  // namespace detail

// Aliases for types of a different category, but the same size.
template <typename T>
using MakeUnsigned = typename detail::Relations<T>::Unsigned;
template <typename T>
using MakeSigned = typename detail::Relations<T>::Signed;
template <typename T>
using MakeFloat = typename detail::Relations<T>::Float;

// Aliases for types of the same category, but different size.
template <typename T>
using MakeWide = typename detail::Relations<T>::Wide;
template <typename T>
using MakeNarrow = typename detail::Relations<T>::Narrow;

// Obtain type from its size [bytes].
template <size_t N>
using UnsignedFromSize = typename detail::TypeFromSize<N>::Unsigned;
template <size_t N>
using SignedFromSize = typename detail::TypeFromSize<N>::Signed;
template <size_t N>
using FloatFromSize = typename detail::TypeFromSize<N>::Float;

// Avoid confusion with SizeTag where the parameter is a lane size.
using UnsignedTag = SizeTag<0>;
using SignedTag = SizeTag<0x100>;  // integer
using FloatTag = SizeTag<0x200>;
using SpecialTag = SizeTag<0x300>;

template <typename T, class R = detail::Relations<T>>
constexpr auto TypeTag()
    -> hwy::SizeTag<((R::is_signed + R::is_float + R::is_bf16) << 8)> {
  return hwy::SizeTag<((R::is_signed + R::is_float + R::is_bf16) << 8)>();
}

// For when we only want to distinguish FloatTag from everything else.
using NonFloatTag = SizeTag<0x400>;

template <typename T, class R = detail::Relations<T>>
constexpr auto IsFloatTag() -> hwy::SizeTag<(R::is_float ? 0x200 : 0x400)> {
  return hwy::SizeTag<(R::is_float ? 0x200 : 0x400)>();
}

//------------------------------------------------------------------------------
// Type traits

template <typename T>
HWY_API constexpr bool IsFloat3264() {
  return IsSameEither<RemoveCvRef<T>, float, double>();
}

template <typename T>
HWY_API constexpr bool IsFloat() {
  // Cannot use T(1.25) != T(1) for float16_t, which can only be converted to or
  // from a float, not compared. Include float16_t in case HWY_HAVE_FLOAT16=1.
  return IsSame<RemoveCvRef<T>, float16_t>() || IsFloat3264<T>();
}

template <typename T>
HWY_API constexpr bool IsSigned() {
  return static_cast<T>(0) > static_cast<T>(-1);
}
template <>
constexpr bool IsSigned<float16_t>() {
  return true;
}
template <>
constexpr bool IsSigned<bfloat16_t>() {
  return true;
}
template <>
constexpr bool IsSigned<hwy::uint128_t>() {
  return false;
}
template <>
constexpr bool IsSigned<hwy::K64V64>() {
  return false;
}
template <>
constexpr bool IsSigned<hwy::K32V32>() {
  return false;
}

template <typename T, bool = IsInteger<T>() && !IsIntegerLaneType<T>()>
struct MakeLaneTypeIfIntegerT {
  using type = T;
};

template <typename T>
struct MakeLaneTypeIfIntegerT<T, true> {
  using type = hwy::If<IsSigned<T>(), SignedFromSize<sizeof(T)>,
                       UnsignedFromSize<sizeof(T)>>;
};

template <typename T>
using MakeLaneTypeIfInteger = typename MakeLaneTypeIfIntegerT<T>::type;

// Largest/smallest representable integer values.
template <typename T>
HWY_API constexpr T LimitsMax() {
  static_assert(IsInteger<T>(), "Only for integer types");
  using TU = UnsignedFromSize<sizeof(T)>;
  return static_cast<T>(IsSigned<T>() ? (static_cast<TU>(~TU(0)) >> 1)
                                      : static_cast<TU>(~TU(0)));
}
template <typename T>
HWY_API constexpr T LimitsMin() {
  static_assert(IsInteger<T>(), "Only for integer types");
  return IsSigned<T>() ? static_cast<T>(-1) - LimitsMax<T>()
                       : static_cast<T>(0);
}

// Largest/smallest representable value (integer or float). This naming avoids
// confusion with numeric_limits<float>::min() (the smallest positive value).
// Cannot be constexpr because we use CopySameSize for [b]float16_t.
template <typename T>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR T LowestValue() {
  return LimitsMin<T>();
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR bfloat16_t LowestValue<bfloat16_t>() {
  return bfloat16_t::FromBits(uint16_t{0xFF7Fu});  // -1.1111111 x 2^127
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR float16_t LowestValue<float16_t>() {
  return float16_t::FromBits(uint16_t{0xFBFFu});  // -1.1111111111 x 2^15
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR float LowestValue<float>() {
  return -3.402823466e+38F;
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR double LowestValue<double>() {
  return -1.7976931348623158e+308;
}

template <typename T>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR T HighestValue() {
  return LimitsMax<T>();
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR bfloat16_t HighestValue<bfloat16_t>() {
  return bfloat16_t::FromBits(uint16_t{0x7F7Fu});  // 1.1111111 x 2^127
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR float16_t HighestValue<float16_t>() {
  return float16_t::FromBits(uint16_t{0x7BFFu});  // 1.1111111111 x 2^15
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR float HighestValue<float>() {
  return 3.402823466e+38F;
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR double HighestValue<double>() {
  return 1.7976931348623158e+308;
}

// Difference between 1.0 and the next representable value. Equal to
// 1 / (1ULL << MantissaBits<T>()), but hard-coding ensures precision.
template <typename T>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR T Epsilon() {
  return 1;
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR bfloat16_t Epsilon<bfloat16_t>() {
  return bfloat16_t::FromBits(uint16_t{0x3C00u});  // 0.0078125
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR float16_t Epsilon<float16_t>() {
  return float16_t::FromBits(uint16_t{0x1400u});  // 0.0009765625
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR float Epsilon<float>() {
  return 1.192092896e-7f;
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR double Epsilon<double>() {
  return 2.2204460492503131e-16;
}

// Returns width in bits of the mantissa field in IEEE binary16/32/64.
template <typename T>
constexpr int MantissaBits() {
  static_assert(sizeof(T) == 0, "Only instantiate the specializations");
  return 0;
}
template <>
constexpr int MantissaBits<bfloat16_t>() {
  return 7;
}
template <>
constexpr int MantissaBits<float16_t>() {
  return 10;
}
template <>
constexpr int MantissaBits<float>() {
  return 23;
}
template <>
constexpr int MantissaBits<double>() {
  return 52;
}

// Returns the (left-shifted by one bit) IEEE binary16/32/64 representation with
// the largest possible (biased) exponent field. Used by IsInf.
template <typename T>
constexpr MakeSigned<T> MaxExponentTimes2() {
  return -(MakeSigned<T>{1} << (MantissaBits<T>() + 1));
}

// Returns bitmask of the sign bit in IEEE binary16/32/64.
template <typename T>
constexpr MakeUnsigned<T> SignMask() {
  return MakeUnsigned<T>{1} << (sizeof(T) * 8 - 1);
}

// Returns bitmask of the exponent field in IEEE binary16/32/64.
template <typename T>
constexpr MakeUnsigned<T> ExponentMask() {
  return (~(MakeUnsigned<T>{1} << MantissaBits<T>()) + 1) &
         static_cast<MakeUnsigned<T>>(~SignMask<T>());
}

// Returns bitmask of the mantissa field in IEEE binary16/32/64.
template <typename T>
constexpr MakeUnsigned<T> MantissaMask() {
  return (MakeUnsigned<T>{1} << MantissaBits<T>()) - 1;
}

// Returns 1 << mantissa_bits as a floating-point number. All integers whose
// absolute value are less than this can be represented exactly.
template <typename T>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR T MantissaEnd() {
  static_assert(sizeof(T) == 0, "Only instantiate the specializations");
  return 0;
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR bfloat16_t MantissaEnd<bfloat16_t>() {
  return bfloat16_t::FromBits(uint16_t{0x4300u});  // 1.0 x 2^7
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR float16_t MantissaEnd<float16_t>() {
  return float16_t::FromBits(uint16_t{0x6400u});  // 1.0 x 2^10
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR float MantissaEnd<float>() {
  return 8388608.0f;  // 1 << 23
}
template <>
HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR double MantissaEnd<double>() {
  // floating point literal with p52 requires C++17.
  return 4503599627370496.0;  // 1 << 52
}

// Returns width in bits of the exponent field in IEEE binary16/32/64.
template <typename T>
constexpr int ExponentBits() {
  // Exponent := remaining bits after deducting sign and mantissa.
  return 8 * sizeof(T) - 1 - MantissaBits<T>();
}

// Returns largest value of the biased exponent field in IEEE binary16/32/64,
// right-shifted so that the LSB is bit zero. Example: 0xFF for float.
// This is expressed as a signed integer for more efficient comparison.
template <typename T>
constexpr MakeSigned<T> MaxExponentField() {
  return (MakeSigned<T>{1} << ExponentBits<T>()) - 1;
}

//------------------------------------------------------------------------------
// Additional F16/BF16 operators

#if HWY_HAVE_SCALAR_F16_OPERATORS || HWY_HAVE_SCALAR_BF16_OPERATORS

#define HWY_RHS_SPECIAL_FLOAT_ARITH_OP(op, op_func, T2)                       \
  template <                                                                  \
      typename T1,                                                            \
      hwy::EnableIf<hwy::IsInteger<RemoveCvRef<T1>>() ||                      \
                    hwy::IsFloat3264<RemoveCvRef<T1>>()>* = nullptr,          \
      typename RawResultT = decltype(DeclVal<T1>() op DeclVal<T2::Native>()), \
      typename ResultT = detail::NativeSpecialFloatToWrapper<RawResultT>,     \
      HWY_IF_CASTABLE(RawResultT, ResultT)>                                   \
  static HWY_INLINE constexpr ResultT op_func(T1 a, T2 b) noexcept {          \
    return static_cast<ResultT>(a op b.native);                               \
  }

#define HWY_RHS_SPECIAL_FLOAT_ASSIGN_OP(op, assign_op, T2)                 \
  template <typename T1,                                                   \
            hwy::EnableIf<hwy::IsInteger<RemoveCvRef<T1>>() ||             \
                          hwy::IsFloat3264<RemoveCvRef<T1>>()>* = nullptr, \
            typename ResultT =                                             \
                decltype(DeclVal<T1&>() assign_op DeclVal<T2::Native>())>  \
  static HWY_INLINE constexpr ResultT operator assign_op(T1& a,            \
                                                         T2 b) noexcept {  \
    return (a assign_op b.native);                                         \
  }

#define HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(op, op_func, T1)         \
  HWY_RHS_SPECIAL_FLOAT_ARITH_OP(op, op_func, T1)                             \
  template <                                                                  \
      typename T2,                                                            \
      hwy::EnableIf<hwy::IsInteger<RemoveCvRef<T2>>() ||                      \
                    hwy::IsFloat3264<RemoveCvRef<T2>>()>* = nullptr,          \
      typename RawResultT = decltype(DeclVal<T1::Native>() op DeclVal<T2>()), \
      typename ResultT = detail::NativeSpecialFloatToWrapper<RawResultT>,     \
      HWY_IF_CASTABLE(RawResultT, ResultT)>                                   \
  static HWY_INLINE constexpr ResultT op_func(T1 a, T2 b) noexcept {          \
    return static_cast<ResultT>(a.native op b);                               \
  }

#if HWY_HAVE_SCALAR_F16_OPERATORS
HWY_RHS_SPECIAL_FLOAT_ARITH_OP(+, operator+, float16_t)
HWY_RHS_SPECIAL_FLOAT_ARITH_OP(-, operator-, float16_t)
HWY_RHS_SPECIAL_FLOAT_ARITH_OP(*, operator*, float16_t)
HWY_RHS_SPECIAL_FLOAT_ARITH_OP(/, operator/, float16_t)
HWY_RHS_SPECIAL_FLOAT_ASSIGN_OP(+, +=, float16_t)
HWY_RHS_SPECIAL_FLOAT_ASSIGN_OP(-, -=, float16_t)
HWY_RHS_SPECIAL_FLOAT_ASSIGN_OP(*, *=, float16_t)
HWY_RHS_SPECIAL_FLOAT_ASSIGN_OP(/, /=, float16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(==, operator==, float16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(!=, operator!=, float16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(<, operator<, float16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(<=, operator<=, float16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(>, operator>, float16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(>=, operator>=, float16_t)
#if HWY_HAVE_CXX20_THREE_WAY_COMPARE
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(<=>, operator<=>, float16_t)
#endif
#endif  // HWY_HAVE_SCALAR_F16_OPERATORS

#if HWY_HAVE_SCALAR_BF16_OPERATORS
HWY_RHS_SPECIAL_FLOAT_ARITH_OP(+, operator+, bfloat16_t)
HWY_RHS_SPECIAL_FLOAT_ARITH_OP(-, operator-, bfloat16_t)
HWY_RHS_SPECIAL_FLOAT_ARITH_OP(*, operator*, bfloat16_t)
HWY_RHS_SPECIAL_FLOAT_ARITH_OP(/, operator/, bfloat16_t)
HWY_RHS_SPECIAL_FLOAT_ASSIGN_OP(+, +=, bfloat16_t)
HWY_RHS_SPECIAL_FLOAT_ASSIGN_OP(-, -=, bfloat16_t)
HWY_RHS_SPECIAL_FLOAT_ASSIGN_OP(*, *=, bfloat16_t)
HWY_RHS_SPECIAL_FLOAT_ASSIGN_OP(/, /=, bfloat16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(==, operator==, bfloat16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(!=, operator!=, bfloat16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(<, operator<, bfloat16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(<=, operator<=, bfloat16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(>, operator>, bfloat16_t)
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(>=, operator>=, bfloat16_t)
#if HWY_HAVE_CXX20_THREE_WAY_COMPARE
HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP(<=>, operator<=>, bfloat16_t)
#endif
#endif  // HWY_HAVE_SCALAR_BF16_OPERATORS

#undef HWY_RHS_SPECIAL_FLOAT_ARITH_OP
#undef HWY_RHS_SPECIAL_FLOAT_ASSIGN_OP
#undef HWY_SPECIAL_FLOAT_CMP_AGAINST_NON_SPECIAL_OP

#endif  // HWY_HAVE_SCALAR_F16_OPERATORS || HWY_HAVE_SCALAR_BF16_OPERATORS

//------------------------------------------------------------------------------
// Type conversions (after IsSpecialFloat)

HWY_API float F32FromF16Mem(const void* ptr) {
  float16_t f16;
  CopyBytes<2>(HWY_ASSUME_ALIGNED(ptr, 2), &f16);
  return F32FromF16(f16);
}

HWY_API float F32FromBF16Mem(const void* ptr) {
  bfloat16_t bf;
  CopyBytes<2>(HWY_ASSUME_ALIGNED(ptr, 2), &bf);
  return F32FromBF16(bf);
}

#if HWY_HAVE_SCALAR_F16_OPERATORS
#define HWY_BF16_TO_F16_CONSTEXPR HWY_BF16_CONSTEXPR
#else
#define HWY_BF16_TO_F16_CONSTEXPR HWY_F16_CONSTEXPR
#endif

// For casting from TFrom to TTo
template <typename TTo, typename TFrom, HWY_IF_NOT_SPECIAL_FLOAT(TTo),
          HWY_IF_NOT_SPECIAL_FLOAT(TFrom), HWY_IF_NOT_SAME(TTo, TFrom)>
HWY_API constexpr TTo ConvertScalarTo(const TFrom in) {
  return static_cast<TTo>(in);
}
template <typename TTo, typename TFrom, HWY_IF_F16(TTo),
          HWY_IF_NOT_SPECIAL_FLOAT(TFrom), HWY_IF_NOT_SAME(TFrom, double)>
HWY_API constexpr TTo ConvertScalarTo(const TFrom in) {
  return F16FromF32(static_cast<float>(in));
}
template <typename TTo, HWY_IF_F16(TTo)>
HWY_API HWY_BF16_TO_F16_CONSTEXPR TTo
ConvertScalarTo(const hwy::bfloat16_t in) {
  return F16FromF32(F32FromBF16(in));
}
template <typename TTo, HWY_IF_F16(TTo)>
HWY_API HWY_F16_CONSTEXPR TTo ConvertScalarTo(const double in) {
  return F16FromF64(in);
}
template <typename TTo, typename TFrom, HWY_IF_BF16(TTo),
          HWY_IF_NOT_SPECIAL_FLOAT(TFrom), HWY_IF_NOT_SAME(TFrom, double)>
HWY_API HWY_BF16_CONSTEXPR TTo ConvertScalarTo(const TFrom in) {
  return BF16FromF32(static_cast<float>(in));
}
template <typename TTo, HWY_IF_BF16(TTo)>
HWY_API HWY_BF16_TO_F16_CONSTEXPR TTo ConvertScalarTo(const hwy::float16_t in) {
  return BF16FromF32(F32FromF16(in));
}
template <typename TTo, HWY_IF_BF16(TTo)>
HWY_API HWY_BF16_CONSTEXPR TTo ConvertScalarTo(const double in) {
  return BF16FromF64(in);
}
template <typename TTo, typename TFrom, HWY_IF_F16(TFrom),
          HWY_IF_NOT_SPECIAL_FLOAT(TTo)>
HWY_API HWY_F16_CONSTEXPR TTo ConvertScalarTo(const TFrom in) {
  return static_cast<TTo>(F32FromF16(in));
}
template <typename TTo, typename TFrom, HWY_IF_BF16(TFrom),
          HWY_IF_NOT_SPECIAL_FLOAT(TTo)>
HWY_API HWY_BF16_CONSTEXPR TTo ConvertScalarTo(TFrom in) {
  return static_cast<TTo>(F32FromBF16(in));
}
// Same: return unchanged
template <typename TTo>
HWY_API constexpr TTo ConvertScalarTo(TTo in) {
  return in;
}

//------------------------------------------------------------------------------
// Helper functions

template <typename T1, typename T2>
constexpr inline T1 DivCeil(T1 a, T2 b) {
  return (a + b - 1) / b;
}

// Works for any `align`; if a power of two, compiler emits ADD+AND.
constexpr inline size_t RoundUpTo(size_t what, size_t align) {
  return DivCeil(what, align) * align;
}

// Works for any `align`; if a power of two, compiler emits AND.
constexpr inline size_t RoundDownTo(size_t what, size_t align) {
  return what - (what % align);
}

namespace detail {

// T is unsigned or T is signed and (val >> shift_amt) is an arithmetic right
// shift
template <class T>
static HWY_INLINE constexpr T ScalarShr(hwy::UnsignedTag /*type_tag*/, T val,
                                        int shift_amt) {
  return static_cast<T>(val >> shift_amt);
}

// T is signed and (val >> shift_amt) is a non-arithmetic right shift
template <class T>
static HWY_INLINE constexpr T ScalarShr(hwy::SignedTag /*type_tag*/, T val,
                                        int shift_amt) {
  using TU = MakeUnsigned<MakeLaneTypeIfInteger<T>>;
  return static_cast<T>(
      (val < 0) ? static_cast<TU>(
                      ~(static_cast<TU>(~static_cast<TU>(val)) >> shift_amt))
                : static_cast<TU>(static_cast<TU>(val) >> shift_amt));
}

}  // namespace detail

// If T is an signed integer type, ScalarShr is guaranteed to perform an
// arithmetic right shift

// Otherwise, if T is an unsigned integer type, ScalarShr is guaranteed to
// perform a logical right shift
template <class T, HWY_IF_INTEGER(RemoveCvRef<T>)>
HWY_API constexpr RemoveCvRef<T> ScalarShr(T val, int shift_amt) {
  using NonCvRefT = RemoveCvRef<T>;
  return detail::ScalarShr(
      hwy::SizeTag<((IsSigned<NonCvRefT>() &&
                     (LimitsMin<NonCvRefT>() >> (sizeof(T) * 8 - 1)) !=
                         static_cast<NonCvRefT>(-1))
                        ? 0x100
                        : 0)>(),
      static_cast<NonCvRefT>(val), shift_amt);
}

// Undefined results for x == 0.
HWY_API size_t Num0BitsBelowLS1Bit_Nonzero32(const uint32_t x) {
  HWY_DASSERT(x != 0);
#if HWY_COMPILER_MSVC
  unsigned long index;  // NOLINT
  _BitScanForward(&index, x);
  return index;
#else   // HWY_COMPILER_MSVC
  return static_cast<size_t>(__builtin_ctz(x));
#endif  // HWY_COMPILER_MSVC
}

HWY_API size_t Num0BitsBelowLS1Bit_Nonzero64(const uint64_t x) {
  HWY_DASSERT(x != 0);
#if HWY_COMPILER_MSVC
#if HWY_ARCH_X86_64
  unsigned long index;  // NOLINT
  _BitScanForward64(&index, x);
  return index;
#else   // HWY_ARCH_X86_64
  // _BitScanForward64 not available
  uint32_t lsb = static_cast<uint32_t>(x & 0xFFFFFFFF);
  unsigned long index;  // NOLINT
  if (lsb == 0) {
    uint32_t msb = static_cast<uint32_t>(x >> 32u);
    _BitScanForward(&index, msb);
    return 32 + index;
  } else {
    _BitScanForward(&index, lsb);
    return index;
  }
#endif  // HWY_ARCH_X86_64
#else   // HWY_COMPILER_MSVC
  return static_cast<size_t>(__builtin_ctzll(x));
#endif  // HWY_COMPILER_MSVC
}

// Undefined results for x == 0.
HWY_API size_t Num0BitsAboveMS1Bit_Nonzero32(const uint32_t x) {
  HWY_DASSERT(x != 0);
#if HWY_COMPILER_MSVC
  unsigned long index;  // NOLINT
  _BitScanReverse(&index, x);
  return 31 - index;
#else   // HWY_COMPILER_MSVC
  return static_cast<size_t>(__builtin_clz(x));
#endif  // HWY_COMPILER_MSVC
}

HWY_API size_t Num0BitsAboveMS1Bit_Nonzero64(const uint64_t x) {
  HWY_DASSERT(x != 0);
#if HWY_COMPILER_MSVC
#if HWY_ARCH_X86_64
  unsigned long index;  // NOLINT
  _BitScanReverse64(&index, x);
  return 63 - index;
#else   // HWY_ARCH_X86_64
  // _BitScanReverse64 not available
  const uint32_t msb = static_cast<uint32_t>(x >> 32u);
  unsigned long index;  // NOLINT
  if (msb == 0) {
    const uint32_t lsb = static_cast<uint32_t>(x & 0xFFFFFFFF);
    _BitScanReverse(&index, lsb);
    return 63 - index;
  } else {
    _BitScanReverse(&index, msb);
    return 31 - index;
  }
#endif  // HWY_ARCH_X86_64
#else   // HWY_COMPILER_MSVC
  return static_cast<size_t>(__builtin_clzll(x));
#endif  // HWY_COMPILER_MSVC
}

template <class T, HWY_IF_INTEGER(RemoveCvRef<T>),
          HWY_IF_T_SIZE_ONE_OF(RemoveCvRef<T>, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API size_t PopCount(T x) {
  uint32_t u32_x = static_cast<uint32_t>(
      static_cast<UnsignedFromSize<sizeof(RemoveCvRef<T>)>>(x));

#if HWY_COMPILER_GCC || HWY_COMPILER_CLANG
  return static_cast<size_t>(__builtin_popcountl(u32_x));
#elif HWY_COMPILER_MSVC && HWY_ARCH_X86_32 && defined(__AVX__)
  return static_cast<size_t>(_mm_popcnt_u32(u32_x));
#else
  u32_x -= ((u32_x >> 1) & 0x55555555u);
  u32_x = (((u32_x >> 2) & 0x33333333u) + (u32_x & 0x33333333u));
  u32_x = (((u32_x >> 4) + u32_x) & 0x0F0F0F0Fu);
  u32_x += (u32_x >> 8);
  u32_x += (u32_x >> 16);
  return static_cast<size_t>(u32_x & 0x3Fu);
#endif
}

template <class T, HWY_IF_INTEGER(RemoveCvRef<T>),
          HWY_IF_T_SIZE(RemoveCvRef<T>, 8)>
HWY_API size_t PopCount(T x) {
  uint64_t u64_x = static_cast<uint64_t>(
      static_cast<UnsignedFromSize<sizeof(RemoveCvRef<T>)>>(x));

#if HWY_COMPILER_GCC || HWY_COMPILER_CLANG
  return static_cast<size_t>(__builtin_popcountll(u64_x));
#elif HWY_COMPILER_MSVC && HWY_ARCH_X86_64 && defined(__AVX__)
  return _mm_popcnt_u64(u64_x);
#elif HWY_COMPILER_MSVC && HWY_ARCH_X86_32 && defined(__AVX__)
  return _mm_popcnt_u32(static_cast<uint32_t>(u64_x & 0xFFFFFFFFu)) +
         _mm_popcnt_u32(static_cast<uint32_t>(u64_x >> 32));
#else
  u64_x -= ((u64_x >> 1) & 0x5555555555555555ULL);
  u64_x = (((u64_x >> 2) & 0x3333333333333333ULL) +
           (u64_x & 0x3333333333333333ULL));
  u64_x = (((u64_x >> 4) + u64_x) & 0x0F0F0F0F0F0F0F0FULL);
  u64_x += (u64_x >> 8);
  u64_x += (u64_x >> 16);
  u64_x += (u64_x >> 32);
  return static_cast<size_t>(u64_x & 0x7Fu);
#endif
}

// Skip HWY_API due to GCC "function not considered for inlining". Previously
// such errors were caused by underlying type mismatches, but it's not clear
// what is still mismatched despite all the casts.
template <typename TI>
/*HWY_API*/ constexpr size_t FloorLog2(TI x) {
  return x == TI{1}
             ? 0
             : static_cast<size_t>(FloorLog2(static_cast<TI>(x >> 1)) + 1);
}

template <typename TI>
/*HWY_API*/ constexpr size_t CeilLog2(TI x) {
  return x == TI{1}
             ? 0
             : static_cast<size_t>(FloorLog2(static_cast<TI>(x - 1)) + 1);
}

template <typename T, typename T2, HWY_IF_FLOAT(T), HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_INLINE constexpr T AddWithWraparound(T t, T2 increment) {
  return t + static_cast<T>(increment);
}

template <typename T, typename T2, HWY_IF_SPECIAL_FLOAT(T)>
HWY_INLINE constexpr T AddWithWraparound(T t, T2 increment) {
  return ConvertScalarTo<T>(ConvertScalarTo<float>(t) +
                            ConvertScalarTo<float>(increment));
}

template <typename T, typename T2, HWY_IF_NOT_FLOAT(T)>
HWY_INLINE constexpr T AddWithWraparound(T t, T2 n) {
  using TU = MakeUnsigned<T>;
  // Sub-int types would promote to int, not unsigned, which would trigger
  // warnings, so first promote to the largest unsigned type. Due to
  // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=87519, which affected GCC 8
  // until fixed in 9.3, we use built-in types rather than uint64_t.
  return static_cast<T>(static_cast<TU>(
      static_cast<unsigned long long>(static_cast<unsigned long long>(t) +
                                      static_cast<unsigned long long>(n)) &
      uint64_t{hwy::LimitsMax<TU>()}));
}

#if HWY_COMPILER_MSVC && HWY_ARCH_X86_64
#pragma intrinsic(_mul128)
#pragma intrinsic(_umul128)
#endif

// 64 x 64 = 128 bit multiplication
HWY_API uint64_t Mul128(uint64_t a, uint64_t b, uint64_t* HWY_RESTRICT upper) {
#if defined(__SIZEOF_INT128__)
  __uint128_t product = (__uint128_t)a * (__uint128_t)b;
  *upper = (uint64_t)(product >> 64);
  return (uint64_t)(product & 0xFFFFFFFFFFFFFFFFULL);
#elif HWY_COMPILER_MSVC && HWY_ARCH_X86_64
  return _umul128(a, b, upper);
#else
  constexpr uint64_t kLo32 = 0xFFFFFFFFU;
  const uint64_t lo_lo = (a & kLo32) * (b & kLo32);
  const uint64_t hi_lo = (a >> 32) * (b & kLo32);
  const uint64_t lo_hi = (a & kLo32) * (b >> 32);
  const uint64_t hi_hi = (a >> 32) * (b >> 32);
  const uint64_t t = (lo_lo >> 32) + (hi_lo & kLo32) + lo_hi;
  *upper = (hi_lo >> 32) + (t >> 32) + hi_hi;
  return (t << 32) | (lo_lo & kLo32);
#endif
}

HWY_API int64_t Mul128(int64_t a, int64_t b, int64_t* HWY_RESTRICT upper) {
#if defined(__SIZEOF_INT128__)
  __int128_t product = (__int128_t)a * (__int128_t)b;
  *upper = (int64_t)(product >> 64);
  return (int64_t)(product & 0xFFFFFFFFFFFFFFFFULL);
#elif HWY_COMPILER_MSVC && HWY_ARCH_X86_64
  return _mul128(a, b, upper);
#else
  uint64_t unsigned_upper;
  const int64_t lower = static_cast<int64_t>(Mul128(
      static_cast<uint64_t>(a), static_cast<uint64_t>(b), &unsigned_upper));
  *upper = static_cast<int64_t>(
      unsigned_upper -
      (static_cast<uint64_t>(ScalarShr(a, 63)) & static_cast<uint64_t>(b)) -
      (static_cast<uint64_t>(ScalarShr(b, 63)) & static_cast<uint64_t>(a)));
  return lower;
#endif
}

// Precomputation for fast n / divisor and n % divisor, where n is a variable
// and divisor is unchanging but unknown at compile-time.
class Divisor {
 public:
  explicit Divisor(uint32_t divisor) : divisor_(divisor) {
    if (divisor <= 1) return;

    const uint32_t len =
        static_cast<uint32_t>(31 - Num0BitsAboveMS1Bit_Nonzero32(divisor - 1));
    const uint64_t u_hi = (2ULL << len) - divisor;
    const uint32_t q = Truncate((u_hi << 32) / divisor);

    mul_ = q + 1;
    shift1_ = 1;
    shift2_ = len;
  }

  uint32_t GetDivisor() const { return divisor_; }

  // Returns n / divisor_.
  uint32_t Divide(uint32_t n) const {
    const uint64_t mul = mul_;
    const uint32_t t = Truncate((mul * n) >> 32);
    return (t + ((n - t) >> shift1_)) >> shift2_;
  }

  // Returns n % divisor_.
  uint32_t Remainder(uint32_t n) const { return n - (Divide(n) * divisor_); }

 private:
  static uint32_t Truncate(uint64_t x) {
    return static_cast<uint32_t>(x & 0xFFFFFFFFu);
  }

  uint32_t divisor_;
  uint32_t mul_ = 1;
  uint32_t shift1_ = 0;
  uint32_t shift2_ = 0;
};

namespace detail {

template <typename T>
static HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR T ScalarAbs(hwy::FloatTag /*tag*/,
                                                          T val) {
  using TU = MakeUnsigned<T>;
  return BitCastScalar<T>(
      static_cast<TU>(BitCastScalar<TU>(val) & (~SignMask<T>())));
}

template <typename T>
static HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR T
ScalarAbs(hwy::SpecialTag /*tag*/, T val) {
  return ScalarAbs(hwy::FloatTag(), val);
}

template <typename T>
static HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR T
ScalarAbs(hwy::SignedTag /*tag*/, T val) {
  using TU = MakeUnsigned<T>;
  return (val < T{0}) ? static_cast<T>(TU{0} - static_cast<TU>(val)) : val;
}

template <typename T>
static HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR T
ScalarAbs(hwy::UnsignedTag /*tag*/, T val) {
  return val;
}

}  // namespace detail

template <typename T>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR RemoveCvRef<T> ScalarAbs(T val) {
  using TVal = MakeLaneTypeIfInteger<
      detail::NativeSpecialFloatToWrapper<RemoveCvRef<T>>>;
  return detail::ScalarAbs(hwy::TypeTag<TVal>(), static_cast<TVal>(val));
}

template <typename T>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR bool ScalarIsNaN(T val) {
  using TF = detail::NativeSpecialFloatToWrapper<RemoveCvRef<T>>;
  using TU = MakeUnsigned<TF>;
  return (BitCastScalar<TU>(ScalarAbs(val)) > ExponentMask<TF>());
}

template <typename T>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR bool ScalarIsInf(T val) {
  using TF = detail::NativeSpecialFloatToWrapper<RemoveCvRef<T>>;
  using TU = MakeUnsigned<TF>;
  return static_cast<TU>(BitCastScalar<TU>(static_cast<TF>(val)) << 1) ==
         static_cast<TU>(MaxExponentTimes2<TF>());
}

namespace detail {

template <typename T>
static HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR bool ScalarIsFinite(
    hwy::FloatTag /*tag*/, T val) {
  using TU = MakeUnsigned<T>;
  return (BitCastScalar<TU>(hwy::ScalarAbs(val)) < ExponentMask<T>());
}

template <typename T>
static HWY_INLINE HWY_BITCASTSCALAR_CONSTEXPR bool ScalarIsFinite(
    hwy::NonFloatTag /*tag*/, T /*val*/) {
  // Integer values are always finite
  return true;
}

}  // namespace detail

template <typename T>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR bool ScalarIsFinite(T val) {
  using TVal = MakeLaneTypeIfInteger<
      detail::NativeSpecialFloatToWrapper<RemoveCvRef<T>>>;
  return detail::ScalarIsFinite(hwy::IsFloatTag<TVal>(),
                                static_cast<TVal>(val));
}

template <typename T>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR RemoveCvRef<T> ScalarCopySign(T magn,
                                                                  T sign) {
  using TF = RemoveCvRef<detail::NativeSpecialFloatToWrapper<RemoveCvRef<T>>>;
  using TU = MakeUnsigned<TF>;
  return BitCastScalar<TF>(static_cast<TU>(
      (BitCastScalar<TU>(static_cast<TF>(magn)) & (~SignMask<TF>())) |
      (BitCastScalar<TU>(static_cast<TF>(sign)) & SignMask<TF>())));
}

template <typename T>
HWY_API HWY_BITCASTSCALAR_CONSTEXPR bool ScalarSignBit(T val) {
  using TVal = MakeLaneTypeIfInteger<
      detail::NativeSpecialFloatToWrapper<RemoveCvRef<T>>>;
  using TU = MakeUnsigned<TVal>;
  return ((BitCastScalar<TU>(static_cast<TVal>(val)) & SignMask<TVal>()) != 0);
}

// Prevents the compiler from eliding the computations that led to "output".
#if HWY_ARCH_PPC && (HWY_COMPILER_GCC || HWY_COMPILER_CLANG) && \
    !defined(_SOFT_FLOAT)
// Workaround to avoid test failures on PPC if compiled with Clang
template <class T, HWY_IF_F32(T)>
HWY_API void PreventElision(T&& output) {
  asm volatile("" : "+f"(output)::"memory");
}
template <class T, HWY_IF_F64(T)>
HWY_API void PreventElision(T&& output) {
  asm volatile("" : "+d"(output)::"memory");
}
template <class T, HWY_IF_NOT_FLOAT3264(T)>
HWY_API void PreventElision(T&& output) {
  asm volatile("" : "+r"(output)::"memory");
}
#else
template <class T>
HWY_API void PreventElision(T&& output) {
#if HWY_COMPILER_MSVC
  // MSVC does not support inline assembly anymore (and never supported GCC's
  // RTL constraints). Self-assignment with #pragma optimize("off") might be
  // expected to prevent elision, but it does not with MSVC 2015. Type-punning
  // with volatile pointers generates inefficient code on MSVC 2017.
  static std::atomic<RemoveCvRef<T>> sink;
  sink.store(output, std::memory_order_relaxed);
#else
  // Works by indicating to the compiler that "output" is being read and
  // modified. The +r constraint avoids unnecessary writes to memory, but only
  // works for built-in types (typically FuncOutput).
  asm volatile("" : "+r"(output) : : "memory");
#endif
}
#endif

}  // namespace hwy

#endif  // HIGHWAY_HWY_BASE_H_
