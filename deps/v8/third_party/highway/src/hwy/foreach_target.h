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

#ifndef HIGHWAY_HWY_FOREACH_TARGET_H_
#define HIGHWAY_HWY_FOREACH_TARGET_H_

// Re-includes the translation unit zero or more times to compile for any
// targets except HWY_STATIC_TARGET. Defines unique HWY_TARGET each time so that
// highway.h defines the corresponding macro/namespace.

#include "hwy/detect_targets.h"

// *_inl.h may include other headers, which requires include guards to prevent
// repeated inclusion. The guards must be reset after compiling each target, so
// the header is again visible. This is done by flipping HWY_TARGET_TOGGLE,
// defining it if undefined and vice versa. This macro is initially undefined
// so that IDEs don't gray out the contents of each header.
#ifdef HWY_TARGET_TOGGLE
#error "This macro must not be defined outside foreach_target.h"
#endif

#ifdef HWY_HIGHWAY_INCLUDED  // highway.h include guard
// Trigger fixup at the bottom of this header.
#define HWY_ALREADY_INCLUDED

// The next highway.h must re-include set_macros-inl.h because the first
// highway.h chose the static target instead of what we will set below.
#undef HWY_SET_MACROS_PER_TARGET
#endif

// Disable HWY_EXPORT in user code until we have generated all targets. Note
// that a subsequent highway.h will not override this definition.
#undef HWY_ONCE
#define HWY_ONCE (0 || HWY_IDE)

// Avoid warnings on #include HWY_TARGET_INCLUDE by hiding them from the IDE;
// also skip if only 1 target defined (no re-inclusion will be necessary).
#if !HWY_IDE && (HWY_TARGETS != HWY_STATIC_TARGET)

#if !defined(HWY_TARGET_INCLUDE)
#error ">1 target enabled => define HWY_TARGET_INCLUDE before foreach_target.h"
#endif

// ------------------------------ HWY_ARCH_X86

#if (HWY_TARGETS & HWY_SSE2) && (HWY_STATIC_TARGET != HWY_SSE2)
#undef HWY_TARGET
#define HWY_TARGET HWY_SSE2
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_SSSE3) && (HWY_STATIC_TARGET != HWY_SSSE3)
#undef HWY_TARGET
#define HWY_TARGET HWY_SSSE3
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_SSE4) && (HWY_STATIC_TARGET != HWY_SSE4)
#undef HWY_TARGET
#define HWY_TARGET HWY_SSE4
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_AVX2) && (HWY_STATIC_TARGET != HWY_AVX2)
#undef HWY_TARGET
#define HWY_TARGET HWY_AVX2
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_AVX3) && (HWY_STATIC_TARGET != HWY_AVX3)
#undef HWY_TARGET
#define HWY_TARGET HWY_AVX3
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_AVX3_DL) && (HWY_STATIC_TARGET != HWY_AVX3_DL)
#undef HWY_TARGET
#define HWY_TARGET HWY_AVX3_DL
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_AVX3_ZEN4) && (HWY_STATIC_TARGET != HWY_AVX3_ZEN4)
#undef HWY_TARGET
#define HWY_TARGET HWY_AVX3_ZEN4
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_AVX3_SPR) && (HWY_STATIC_TARGET != HWY_AVX3_SPR)
#undef HWY_TARGET
#define HWY_TARGET HWY_AVX3_SPR
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

// ------------------------------ HWY_ARCH_ARM

#if (HWY_TARGETS & HWY_NEON_WITHOUT_AES) && \
    (HWY_STATIC_TARGET != HWY_NEON_WITHOUT_AES)
#undef HWY_TARGET
#define HWY_TARGET HWY_NEON_WITHOUT_AES
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_NEON) && (HWY_STATIC_TARGET != HWY_NEON)
#undef HWY_TARGET
#define HWY_TARGET HWY_NEON
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_NEON_BF16) && (HWY_STATIC_TARGET != HWY_NEON_BF16)
#undef HWY_TARGET
#define HWY_TARGET HWY_NEON_BF16
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_SVE) && (HWY_STATIC_TARGET != HWY_SVE)
#undef HWY_TARGET
#define HWY_TARGET HWY_SVE
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_SVE2) && (HWY_STATIC_TARGET != HWY_SVE2)
#undef HWY_TARGET
#define HWY_TARGET HWY_SVE2
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_SVE_256) && (HWY_STATIC_TARGET != HWY_SVE_256)
#undef HWY_TARGET
#define HWY_TARGET HWY_SVE_256
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_SVE2_128) && (HWY_STATIC_TARGET != HWY_SVE2_128)
#undef HWY_TARGET
#define HWY_TARGET HWY_SVE2_128
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

// ------------------------------ HWY_ARCH_WASM

#if (HWY_TARGETS & HWY_WASM_EMU256) && (HWY_STATIC_TARGET != HWY_WASM_EMU256)
#undef HWY_TARGET
#define HWY_TARGET HWY_WASM_EMU256
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_WASM) && (HWY_STATIC_TARGET != HWY_WASM)
#undef HWY_TARGET
#define HWY_TARGET HWY_WASM
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

// ------------------------------ HWY_ARCH_PPC

#if (HWY_TARGETS & HWY_PPC8) && (HWY_STATIC_TARGET != HWY_PPC8)
#undef HWY_TARGET
#define HWY_TARGET HWY_PPC8
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_PPC9) && (HWY_STATIC_TARGET != HWY_PPC9)
#undef HWY_TARGET
#define HWY_TARGET HWY_PPC9
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_PPC10) && (HWY_STATIC_TARGET != HWY_PPC10)
#undef HWY_TARGET
#define HWY_TARGET HWY_PPC10
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

// ------------------------------ HWY_ARCH_S390X

#if (HWY_TARGETS & HWY_Z14) && (HWY_STATIC_TARGET != HWY_Z14)
#undef HWY_TARGET
#define HWY_TARGET HWY_Z14
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_Z15) && (HWY_STATIC_TARGET != HWY_Z15)
#undef HWY_TARGET
#define HWY_TARGET HWY_Z15
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

// ------------------------------ HWY_ARCH_RISCV

#if (HWY_TARGETS & HWY_RVV) && (HWY_STATIC_TARGET != HWY_RVV)
#undef HWY_TARGET
#define HWY_TARGET HWY_RVV
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

// ------------------------------ Scalar

#if (HWY_TARGETS & HWY_EMU128) && (HWY_STATIC_TARGET != HWY_EMU128)
#undef HWY_TARGET
#define HWY_TARGET HWY_EMU128
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#if (HWY_TARGETS & HWY_SCALAR) && (HWY_STATIC_TARGET != HWY_SCALAR)
#undef HWY_TARGET
#define HWY_TARGET HWY_SCALAR
#include HWY_TARGET_INCLUDE
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif
#endif

#endif  // !HWY_IDE && (HWY_TARGETS != HWY_STATIC_TARGET)

// Now that all but the static target have been generated, re-enable HWY_EXPORT.
#undef HWY_ONCE
#define HWY_ONCE 1

// If we re-include once per enabled target, the translation unit's
// implementation would have to be skipped via #if to avoid redefining symbols.
// We instead skip the re-include for HWY_STATIC_TARGET, and generate its
// implementation when resuming compilation of the translation unit.
#undef HWY_TARGET
#define HWY_TARGET HWY_STATIC_TARGET

#ifdef HWY_ALREADY_INCLUDED
// Revert the previous toggle to prevent redefinitions for the static target.
#ifdef HWY_TARGET_TOGGLE
#undef HWY_TARGET_TOGGLE
#else
#define HWY_TARGET_TOGGLE
#endif

// Force re-inclusion of set_macros-inl.h now that HWY_TARGET is restored.
#ifdef HWY_SET_MACROS_PER_TARGET
#undef HWY_SET_MACROS_PER_TARGET
#else
#define HWY_SET_MACROS_PER_TARGET
#endif
#endif

#endif  // HIGHWAY_HWY_FOREACH_TARGET_H_
