/* cpu_features.c -- Processor features detection.
 *
 * Copyright 2018 The Chromium Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the Chromium source repository LICENSE file.
 */

#include "cpu_features.h"
#include "zutil.h"

#include <stdint.h>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(ADLER32_SIMD_SSSE3)
#include <cpuid.h>
#endif

/* TODO(cavalcantii): remove checks for x86_flags on deflate.
 */
#if defined(ARMV8_OS_MACOS)
/* Crypto extensions (crc32/pmull) are a baseline feature in ARMv8.1-A, and
 * OSX running on arm64 is new enough that these can be assumed without
 * runtime detection.
 */
int ZLIB_INTERNAL arm_cpu_enable_crc32 = 1;
int ZLIB_INTERNAL arm_cpu_enable_pmull = 1;
#else
int ZLIB_INTERNAL arm_cpu_enable_crc32 = 0;
int ZLIB_INTERNAL arm_cpu_enable_pmull = 0;
#endif
int ZLIB_INTERNAL x86_cpu_enable_sse2 = 0;
int ZLIB_INTERNAL x86_cpu_enable_ssse3 = 0;
int ZLIB_INTERNAL x86_cpu_enable_simd = 0;
int ZLIB_INTERNAL x86_cpu_enable_avx512 = 0;

int ZLIB_INTERNAL riscv_cpu_enable_rvv = 0;
int ZLIB_INTERNAL riscv_cpu_enable_vclmul = 0;

#ifndef CPU_NO_SIMD

#if defined(ARMV8_OS_ANDROID) || defined(ARMV8_OS_LINUX) || \
    defined(ARMV8_OS_FUCHSIA) || defined(ARMV8_OS_IOS)
#include <pthread.h>
#endif

#if defined(ARMV8_OS_ANDROID)
#include <cpu-features.h>
#elif defined(ARMV8_OS_LINUX)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#elif defined(ARMV8_OS_FUCHSIA)
#include <zircon/features.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#elif defined(ARMV8_OS_WINDOWS) || defined(X86_WINDOWS)
#include <windows.h>
#elif defined(ARMV8_OS_IOS)
#include <sys/sysctl.h>
#elif !defined(_MSC_VER)
#include <pthread.h>
#else
#error cpu_features.c CPU feature detection in not defined for your platform
#endif

#if !defined(CPU_NO_SIMD) && !defined(ARMV8_OS_MACOS)
static void _cpu_check_features(void);
#endif

#if defined(ARMV8_OS_ANDROID) || defined(ARMV8_OS_LINUX) || \
    defined(ARMV8_OS_MACOS) || defined(ARMV8_OS_FUCHSIA) || \
    defined(X86_NOT_WINDOWS) || defined(ARMV8_OS_IOS) || \
    defined(RISCV_RVV)
#if !defined(ARMV8_OS_MACOS)
// _cpu_check_features() doesn't need to do anything on mac/arm since all
// features are known at build time, so don't call it.
// Do provide cpu_check_features() (with a no-op implementation) so that we
// don't have to make all callers of it check for mac/arm.
static pthread_once_t cpu_check_inited_once = PTHREAD_ONCE_INIT;
#endif
void ZLIB_INTERNAL cpu_check_features(void)
{
#if !defined(ARMV8_OS_MACOS)
    pthread_once(&cpu_check_inited_once, _cpu_check_features);
#endif
}
#elif defined(ARMV8_OS_WINDOWS) || defined(X86_WINDOWS)
static INIT_ONCE cpu_check_inited_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK _cpu_check_features_forwarder(PINIT_ONCE once, PVOID param, PVOID* context)
{
    _cpu_check_features();
    return TRUE;
}
void ZLIB_INTERNAL cpu_check_features(void)
{
    InitOnceExecuteOnce(&cpu_check_inited_once, _cpu_check_features_forwarder,
                        NULL, NULL);
}
#endif

#if (defined(__ARM_NEON__) || defined(__ARM_NEON))
#if !defined(ARMV8_OS_MACOS)
/*
 * See http://bit.ly/2CcoEsr for run-time detection of ARM features and also
 * crbug.com/931275 for android_getCpuFeatures() use in the Android sandbox.
 */
static void _cpu_check_features(void)
{
#if defined(ARMV8_OS_ANDROID) && defined(__aarch64__)
    uint64_t features = android_getCpuFeatures();
    arm_cpu_enable_crc32 = !!(features & ANDROID_CPU_ARM64_FEATURE_CRC32);
    arm_cpu_enable_pmull = !!(features & ANDROID_CPU_ARM64_FEATURE_PMULL);
#elif defined(ARMV8_OS_ANDROID) /* aarch32 */
    uint64_t features = android_getCpuFeatures();
    arm_cpu_enable_crc32 = !!(features & ANDROID_CPU_ARM_FEATURE_CRC32);
    arm_cpu_enable_pmull = !!(features & ANDROID_CPU_ARM_FEATURE_PMULL);
#elif defined(ARMV8_OS_LINUX) && defined(__aarch64__)
    unsigned long features = getauxval(AT_HWCAP);
    arm_cpu_enable_crc32 = !!(features & HWCAP_CRC32);
    arm_cpu_enable_pmull = !!(features & HWCAP_PMULL);
#elif defined(ARMV8_OS_LINUX) && (defined(__ARM_NEON) || defined(__ARM_NEON__))
    /* Query HWCAP2 for ARMV8-A SoCs running in aarch32 mode */
    unsigned long features = getauxval(AT_HWCAP2);
    arm_cpu_enable_crc32 = !!(features & HWCAP2_CRC32);
    arm_cpu_enable_pmull = !!(features & HWCAP2_PMULL);
#elif defined(ARMV8_OS_FUCHSIA)
    uint32_t features;
    zx_status_t rc = zx_system_get_features(ZX_FEATURE_KIND_CPU, &features);
    if (rc != ZX_OK || (features & ZX_ARM64_FEATURE_ISA_ASIMD) == 0)
        return;  /* Report nothing if ASIMD(NEON) is missing */
    arm_cpu_enable_crc32 = !!(features & ZX_ARM64_FEATURE_ISA_CRC32);
    arm_cpu_enable_pmull = !!(features & ZX_ARM64_FEATURE_ISA_PMULL);
#elif defined(ARMV8_OS_WINDOWS)
    arm_cpu_enable_crc32 = IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE);
    arm_cpu_enable_pmull = IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE);
#elif defined(ARMV8_OS_IOS)
    // Determine what features are supported dynamically. This code is applicable to macOS
    // as well if we wish to do that dynamically on that platform in the future.
    // See https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_instruction_set_characteristics
    int val = 0;
    size_t len = sizeof(val);
    arm_cpu_enable_crc32 = sysctlbyname("hw.optional.armv8_crc32", &val, &len, 0, 0) == 0
               && val != 0;
    val = 0;
    len = sizeof(val);
    arm_cpu_enable_pmull = sysctlbyname("hw.optional.arm.FEAT_PMULL", &val, &len, 0, 0) == 0
               && val != 0;
#endif
}
#endif
#elif defined(X86_NOT_WINDOWS) || defined(X86_WINDOWS)
/*
 * iOS@x86 (i.e. emulator) is another special case where we disable
 * SIMD optimizations.
 */
#ifndef CPU_NO_SIMD
/* On x86 we simply use a instruction to check the CPU features.
 * (i.e. CPUID).
 */
#ifdef CRC32_SIMD_AVX512_PCLMUL
#include <immintrin.h>
#include <xsaveintrin.h>
#endif
static void _cpu_check_features(void)
{
    int x86_cpu_has_sse2;
    int x86_cpu_has_ssse3;
    int x86_cpu_has_sse42;
    int x86_cpu_has_pclmulqdq;
    int abcd[4];

#ifdef _MSC_VER
    __cpuid(abcd, 1);
#else
    __cpuid(1, abcd[0], abcd[1], abcd[2], abcd[3]);
#endif

    x86_cpu_has_sse2 = abcd[3] & 0x4000000;
    x86_cpu_has_ssse3 = abcd[2] & 0x000200;
    x86_cpu_has_sse42 = abcd[2] & 0x100000;
    x86_cpu_has_pclmulqdq = abcd[2] & 0x2;

    x86_cpu_enable_sse2 = x86_cpu_has_sse2;

    x86_cpu_enable_ssse3 = x86_cpu_has_ssse3;

    x86_cpu_enable_simd = x86_cpu_has_sse2 &&
                          x86_cpu_has_sse42 &&
                          x86_cpu_has_pclmulqdq;

#ifdef CRC32_SIMD_AVX512_PCLMUL
    x86_cpu_enable_avx512 = _xgetbv(0) & 0x00000040;
#endif
}
#endif // x86 & NO_SIMD

#elif defined(RISCV_RVV)
#include <sys/auxv.h>

#ifndef ZLIB_HWCAP_RVV
#define ZLIB_HWCAP_RVV (1 << ('v' - 'a'))
#endif

/* TODO(cavalcantii)
 * - add support for Android@RISCV i.e. __riscv_hwprobe().
 * - detect vclmul (crypto extensions).
 */
static void _cpu_check_features(void)
{
  unsigned long features = getauxval(AT_HWCAP);
  riscv_cpu_enable_rvv = !!(features & ZLIB_HWCAP_RVV);
}
#endif // ARM | x86 | RISCV
#endif // NO SIMD CPU
