// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_COMPILER_SPECIFIC_H_
#define INCLUDE_CPPGC_INTERNAL_COMPILER_SPECIFIC_H_

#include "v8config.h"  // NOLINT(build/include_directory)

namespace cppgc {

#if defined(__has_attribute)
#define CPPGC_HAS_ATTRIBUTE(FEATURE) __has_attribute(FEATURE)
#else
#define CPPGC_HAS_ATTRIBUTE(FEATURE) 0
#endif

#if defined(__has_cpp_attribute)
#define CPPGC_HAS_CPP_ATTRIBUTE(FEATURE) __has_cpp_attribute(FEATURE)
#else
#define CPPGC_HAS_CPP_ATTRIBUTE(FEATURE) 0
#endif

// [[no_unique_address]] comes in C++20 but supported in clang with -std >=
// c++11.
#if defined(V8_CC_MSVC) && CPPGC_HAS_CPP_ATTRIBUTE(msvc::no_unique_address)
// Unfortunately MSVC ignores [[no_unique_address]] (see
// https://devblogs.microsoft.com/cppblog/msvc-cpp20-and-the-std-cpp20-switch/#msvc-extensions-and-abi),
// and clang-cl matches it for ABI compatibility reasons. We need to prefer
// [[msvc::no_unique_address]] when available if we actually want any effect.
#define CPPGC_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#elif CPPGC_HAS_CPP_ATTRIBUTE(no_unique_address)
#define CPPGC_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define CPPGC_NO_UNIQUE_ADDRESS
#endif

#if CPPGC_HAS_ATTRIBUTE(unused)
#define CPPGC_UNUSED __attribute__((unused))
#else
#define CPPGC_UNUSED
#endif

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_COMPILER_SPECIFIC_H_
