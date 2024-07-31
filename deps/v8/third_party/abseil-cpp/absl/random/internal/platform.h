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

#ifndef ABSL_RANDOM_INTERNAL_PLATFORM_H_
#define ABSL_RANDOM_INTERNAL_PLATFORM_H_

// HERMETIC NOTE: The randen_hwaes target must not introduce duplicate
// symbols from arbitrary system and other headers, since it may be built
// with different flags from other targets, using different levels of
// optimization, potentially introducing ODR violations.

// -----------------------------------------------------------------------------
// Platform Feature Checks
// -----------------------------------------------------------------------------

// Currently supported operating systems and associated preprocessor
// symbols:
//
//   Linux and Linux-derived           __linux__
//   Android                           __ANDROID__ (implies __linux__)
//   Linux (non-Android)               __linux__ && !__ANDROID__
//   Darwin (macOS and iOS)            __APPLE__
//   Akaros (http://akaros.org)        __ros__
//   Windows                           _WIN32
//   NaCL                              __native_client__
//   AsmJS                             __asmjs__
//   WebAssembly                       __wasm__
//   Fuchsia                           __Fuchsia__
//
// Note that since Android defines both __ANDROID__ and __linux__, one
// may probe for either Linux or Android by simply testing for __linux__.
//
// NOTE: For __APPLE__ platforms, we use #include <TargetConditionals.h>
// to distinguish os variants.
//
// http://nadeausoftware.com/articles/2012/01/c_c_tip_how_use_compiler_predefined_macros_detect_operating_system

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// -----------------------------------------------------------------------------
// Architecture Checks
// -----------------------------------------------------------------------------

// These preprocessor directives are trying to determine CPU architecture,
// including necessary headers to support hardware AES.
//
// ABSL_ARCH_{X86/PPC/ARM} macros determine the platform.
#if defined(__x86_64__) || defined(__x86_64) || defined(_M_AMD64) || \
    defined(_M_X64)
#define ABSL_ARCH_X86_64
#elif defined(__i386) || defined(_M_IX86)
#define ABSL_ARCH_X86_32
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#define ABSL_ARCH_AARCH64
#elif defined(__arm__) || defined(__ARMEL__) || defined(_M_ARM)
#define ABSL_ARCH_ARM
#elif defined(__powerpc64__) || defined(__PPC64__) || defined(__powerpc__) || \
    defined(__ppc__) || defined(__PPC__)
#define ABSL_ARCH_PPC
#else
// Unsupported architecture.
//  * https://sourceforge.net/p/predef/wiki/Architectures/
//  * https://msdn.microsoft.com/en-us/library/b0084kay.aspx
//  * for gcc, clang: "echo | gcc -E -dM -"
#endif

// -----------------------------------------------------------------------------
// Attribute Checks
// -----------------------------------------------------------------------------

// ABSL_RANDOM_INTERNAL_RESTRICT annotates whether pointers may be considered
// to be unaliased.
#if defined(__clang__) || defined(__GNUC__)
#define ABSL_RANDOM_INTERNAL_RESTRICT __restrict__
#elif defined(_MSC_VER)
#define ABSL_RANDOM_INTERNAL_RESTRICT __restrict
#else
#define ABSL_RANDOM_INTERNAL_RESTRICT
#endif

// ABSL_HAVE_ACCELERATED_AES indicates whether the currently active compiler
// flags (e.g. -maes) allow using hardware accelerated AES instructions, which
// implies us assuming that the target platform supports them.
#define ABSL_HAVE_ACCELERATED_AES 0

#if defined(ABSL_ARCH_X86_64)

#if defined(__AES__) || defined(__AVX__)
#undef ABSL_HAVE_ACCELERATED_AES
#define ABSL_HAVE_ACCELERATED_AES 1
#endif

#elif defined(ABSL_ARCH_PPC)

// Rely on VSX and CRYPTO extensions for vcipher on PowerPC.
#if (defined(__VEC__) || defined(__ALTIVEC__)) && defined(__VSX__) && \
    defined(__CRYPTO__)
#undef ABSL_HAVE_ACCELERATED_AES
#define ABSL_HAVE_ACCELERATED_AES 1
#endif

#elif defined(ABSL_ARCH_ARM) || defined(ABSL_ARCH_AARCH64)

// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0053c/IHI0053C_acle_2_0.pdf
// Rely on NEON+CRYPTO extensions for ARM.
#if defined(__ARM_NEON) && defined(__ARM_FEATURE_CRYPTO)
#undef ABSL_HAVE_ACCELERATED_AES
#define ABSL_HAVE_ACCELERATED_AES 1
#endif

#endif

// NaCl does not allow AES.
#if defined(__native_client__)
#undef ABSL_HAVE_ACCELERATED_AES
#define ABSL_HAVE_ACCELERATED_AES 0
#endif

// ABSL_RANDOM_INTERNAL_AES_DISPATCH indicates whether the currently active
// platform has, or should use run-time dispatch for selecting the
// accelerated Randen implementation.
#define ABSL_RANDOM_INTERNAL_AES_DISPATCH 0

#if defined(ABSL_ARCH_X86_64)
// Dispatch is available on x86_64
#undef ABSL_RANDOM_INTERNAL_AES_DISPATCH
#define ABSL_RANDOM_INTERNAL_AES_DISPATCH 1
#elif defined(__linux__) && defined(ABSL_ARCH_PPC)
// Or when running linux PPC
#undef ABSL_RANDOM_INTERNAL_AES_DISPATCH
#define ABSL_RANDOM_INTERNAL_AES_DISPATCH 1
#elif defined(__linux__) && defined(ABSL_ARCH_AARCH64)
// Or when running linux AArch64
#undef ABSL_RANDOM_INTERNAL_AES_DISPATCH
#define ABSL_RANDOM_INTERNAL_AES_DISPATCH 1
#elif defined(__linux__) && defined(ABSL_ARCH_ARM) && (__ARM_ARCH >= 8)
// Or when running linux ARM v8 or higher.
// (This captures a lot of Android configurations.)
#undef ABSL_RANDOM_INTERNAL_AES_DISPATCH
#define ABSL_RANDOM_INTERNAL_AES_DISPATCH 1
#endif

// NaCl does not allow dispatch.
#if defined(__native_client__)
#undef ABSL_RANDOM_INTERNAL_AES_DISPATCH
#define ABSL_RANDOM_INTERNAL_AES_DISPATCH 0
#endif

// iOS does not support dispatch, even on x86, since applications
// should be bundled as fat binaries, with a different build tailored for
// each specific supported platform/architecture.
#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || \
    (defined(TARGET_OS_IPHONE_SIMULATOR) && TARGET_OS_IPHONE_SIMULATOR)
#undef ABSL_RANDOM_INTERNAL_AES_DISPATCH
#define ABSL_RANDOM_INTERNAL_AES_DISPATCH 0
#endif

#endif  // ABSL_RANDOM_INTERNAL_PLATFORM_H_
