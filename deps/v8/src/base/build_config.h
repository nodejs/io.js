// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_BUILD_CONFIG_H_
#define V8_BASE_BUILD_CONFIG_H_

#include "include/v8config.h"

#if defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || \
    defined(__ARM_ARCH_7__)
#define CAN_USE_ARMV7_INSTRUCTIONS 1
#ifdef __ARM_ARCH_EXT_IDIV__
#define CAN_USE_SUDIV 1
#endif
#ifndef CAN_USE_VFP3_INSTRUCTIONS
#define CAN_USE_VFP3_INSTRUCTIONS 1
#endif
#endif

#if defined(__ARM_ARCH_8A__)
#define CAN_USE_ARMV7_INSTRUCTIONS 1
#define CAN_USE_SUDIV 1
#define CAN_USE_ARMV8_INSTRUCTIONS 1
#ifndef CAN_USE_VFP3_INSTRUCTIONS
#define CAN_USE_VFP3_INSTRUCTIONS 1
#endif
#endif

// pthread_jit_write_protect is only available on arm64 Mac.
#if defined(V8_HOST_ARCH_ARM64) && defined(V8_OS_MACOS)
#define V8_HAS_PTHREAD_JIT_WRITE_PROTECT 1
#else
#define V8_HAS_PTHREAD_JIT_WRITE_PROTECT 0
#endif

// BrowserEngineCore JIT write protect is only available on iOS 17.4 and later.
#if defined(V8_HOST_ARCH_ARM64) && defined(V8_OS_IOS) && \
    defined(__IPHONE_17_4) &&                            \
    __IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_17_4
#define V8_HAS_BECORE_JIT_WRITE_PROTECT 1
#else
#define V8_HAS_BECORE_JIT_WRITE_PROTECT 0
#endif

#if defined(V8_OS_LINUX) && defined(V8_HOST_ARCH_X64)
#define V8_HAS_PKU_JIT_WRITE_PROTECT 1
#else
#define V8_HAS_PKU_JIT_WRITE_PROTECT 0
#endif

#if defined(V8_TARGET_ARCH_IA32) || defined(V8_TARGET_ARCH_X64)
#define V8_TARGET_ARCH_STORES_RETURN_ADDRESS_ON_STACK true
#else
#define V8_TARGET_ARCH_STORES_RETURN_ADDRESS_ON_STACK false
#endif
constexpr int kReturnAddressStackSlotCount =
    V8_TARGET_ARCH_STORES_RETURN_ADDRESS_ON_STACK ? 1 : 0;

// Number of bits to represent the page size for paged spaces.
#if defined(V8_HOST_ARCH_PPC64) && !defined(V8_OS_AIX)
// Native PPC linux has large (64KB) physical pages.
// Simulator (and Aix) need to use the same value as x64.
constexpr int kPageSizeBits = 19;
#elif defined(ENABLE_HUGEPAGE)
// When enabling huge pages, adjust V8 page size to take up exactly one huge
// page. This avoids huge-page-internal fragmentation for unused address ranges.
constexpr int kHugePageBits = 21;
constexpr int kHugePageSize = 1 << kHugePageBits;
constexpr int kPageSizeBits = kHugePageBits;
#else
// Arm64 supports up to 64k OS pages on Linux, however 4k pages are more common
// so we keep the V8 page size at 256k. Nonetheless, we need to make sure we
// don't decrease it further in the future due to reserving 3 OS pages for every
// executable V8 page.
constexpr int kPageSizeBits = 18;
#endif

constexpr int kRegularPageSize = 1 << kPageSizeBits;

// The minimal supported page size by the operation system. Any region aligned
// to that size needs to be individually protectable via
// {base::OS::SetPermission} and friends.
#if (defined(V8_OS_MACOS) && defined(V8_HOST_ARCH_ARM64)) ||         \
    (defined(V8_OS_ANDROID) &&                                       \
     (defined(V8_HOST_ARCH_ARM64) || defined(V8_HOST_ARCH_X64))) ||  \
    defined(V8_HOST_ARCH_LOONG64) || defined(V8_HOST_ARCH_MIPS64) || \
    defined(V8_OS_IOS)
// Android 64 bit has experimental support for 16kB pages.
// MacOS & iOS on arm64 uses 16kB pages.
// LOONG64 and MIPS64 also use 16kB pages.
constexpr int kMinimumOSPageSize = 16 * 1024;
#elif defined(V8_OS_LINUX) && !defined(V8_OS_ANDROID) && \
    (defined(V8_HOST_ARCH_ARM64) || defined(V8_HOST_ARCH_PPC64))
// Linux on arm64 (excluding android) and PPC64 can be configured for up to 64kB
// pages.
constexpr int kMinimumOSPageSize = 64 * 1024;
#else
// Everything else uses 4kB pages.
constexpr int kMinimumOSPageSize = 4 * 1024;
#endif

#endif  // V8_BASE_BUILD_CONFIG_H_
