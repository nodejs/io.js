#ifndef FASTFLOAT_CONSTEXPR_FEATURE_DETECT_H
#define FASTFLOAT_CONSTEXPR_FEATURE_DETECT_H

#ifdef __has_include
#if __has_include(<version>)
#include <version>
#endif
#endif

// Testing for https://wg21.link/N3652, adopted in C++14
#if __cpp_constexpr >= 201304
#define FASTFLOAT_CONSTEXPR14 constexpr
#else
#define FASTFLOAT_CONSTEXPR14
#endif

#if defined(__cpp_lib_bit_cast) && __cpp_lib_bit_cast >= 201806L
#define FASTFLOAT_HAS_BIT_CAST 1
#else
#define FASTFLOAT_HAS_BIT_CAST 0
#endif

#if defined(__cpp_lib_is_constant_evaluated) &&                                \
    __cpp_lib_is_constant_evaluated >= 201811L
#define FASTFLOAT_HAS_IS_CONSTANT_EVALUATED 1
#else
#define FASTFLOAT_HAS_IS_CONSTANT_EVALUATED 0
#endif

// Testing for relevant C++20 constexpr library features
#if FASTFLOAT_HAS_IS_CONSTANT_EVALUATED && FASTFLOAT_HAS_BIT_CAST &&           \
    __cpp_lib_constexpr_algorithms >= 201806L /*For std::copy and std::fill*/
#define FASTFLOAT_CONSTEXPR20 constexpr
#define FASTFLOAT_IS_CONSTEXPR 1
#else
#define FASTFLOAT_CONSTEXPR20
#define FASTFLOAT_IS_CONSTEXPR 0
#endif

#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#define FASTFLOAT_DETAIL_MUST_DEFINE_CONSTEXPR_VARIABLE 0
#else
#define FASTFLOAT_DETAIL_MUST_DEFINE_CONSTEXPR_VARIABLE 1
#endif

#endif // FASTFLOAT_CONSTEXPR_FEATURE_DETECT_H
