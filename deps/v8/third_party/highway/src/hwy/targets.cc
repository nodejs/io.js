// Copyright 2019 Google LLC
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

#include "hwy/targets.h"

#include <stdint.h>
#include <stdio.h>

#include "hwy/base.h"
#include "hwy/detect_targets.h"
#include "hwy/highway.h"
#include "hwy/per_target.h"  // VectorBytes

#if HWY_ARCH_X86
#include <xmmintrin.h>
#if HWY_COMPILER_MSVC
#include <intrin.h>
#else  // !HWY_COMPILER_MSVC
#include <cpuid.h>
#endif  // HWY_COMPILER_MSVC

#elif (HWY_ARCH_ARM || HWY_ARCH_PPC || HWY_ARCH_S390X || HWY_ARCH_RISCV) && \
    HWY_OS_LINUX
// sys/auxv.h does not always include asm/hwcap.h, or define HWCAP*, hence we
// still include this directly. See #1199.
#ifndef TOOLCHAIN_MISS_ASM_HWCAP_H
#include <asm/hwcap.h>
#endif
#if HWY_HAVE_AUXV
#include <sys/auxv.h>
#endif

#endif  // HWY_ARCH_*

#if HWY_OS_APPLE
#include <sys/sysctl.h>
#include <sys/utsname.h>
#endif  // HWY_OS_APPLE

namespace hwy {
namespace {

// When running tests, this value can be set to the mocked supported targets
// mask. Only written to from a single thread before the test starts.
int64_t supported_targets_for_test_ = 0;

// Mask of targets disabled at runtime with DisableTargets.
int64_t supported_mask_ = LimitsMax<int64_t>();

#if HWY_OS_APPLE
static HWY_INLINE HWY_MAYBE_UNUSED bool HasCpuFeature(
    const char* feature_name) {
  int result = 0;
  size_t len = sizeof(int);
  return (sysctlbyname(feature_name, &result, &len, nullptr, 0) == 0 &&
          result != 0);
}

static HWY_INLINE HWY_MAYBE_UNUSED bool ParseU32(const char*& ptr,
                                                 uint32_t& parsed_val) {
  uint64_t parsed_u64 = 0;

  const char* start_ptr = ptr;
  for (char ch; (ch = (*ptr)) != '\0'; ++ptr) {
    unsigned digit = static_cast<unsigned>(static_cast<unsigned char>(ch)) -
                     static_cast<unsigned>(static_cast<unsigned char>('0'));
    if (digit > 9u) {
      break;
    }

    parsed_u64 = (parsed_u64 * 10u) + digit;
    if (parsed_u64 > 0xFFFFFFFFu) {
      return false;
    }
  }

  parsed_val = static_cast<uint32_t>(parsed_u64);
  return (ptr != start_ptr);
}

static HWY_INLINE HWY_MAYBE_UNUSED bool IsMacOs12_2OrLater() {
  utsname uname_buf;
  ZeroBytes(&uname_buf, sizeof(utsname));

  if ((uname(&uname_buf)) != 0) {
    return false;
  }

  const char* ptr = uname_buf.release;
  if (!ptr) {
    return false;
  }

  uint32_t major;
  uint32_t minor;
  if (!ParseU32(ptr, major)) {
    return false;
  }

  if (*ptr != '.') {
    return false;
  }

  ++ptr;
  if (!ParseU32(ptr, minor)) {
    return false;
  }

  // We are running on macOS 12.2 or later if the Darwin kernel version is 21.3
  // or later
  return (major > 21 || (major == 21 && minor >= 3));
}
#endif  // HWY_OS_APPLE

#if HWY_ARCH_X86 && HWY_HAVE_RUNTIME_DISPATCH
namespace x86 {

// Calls CPUID instruction with eax=level and ecx=count and returns the result
// in abcd array where abcd = {eax, ebx, ecx, edx} (hence the name abcd).
HWY_INLINE void Cpuid(const uint32_t level, const uint32_t count,
                      uint32_t* HWY_RESTRICT abcd) {
#if HWY_COMPILER_MSVC
  int regs[4];
  __cpuidex(regs, level, count);
  for (int i = 0; i < 4; ++i) {
    abcd[i] = regs[i];
  }
#else   // HWY_COMPILER_MSVC
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  __cpuid_count(level, count, a, b, c, d);
  abcd[0] = a;
  abcd[1] = b;
  abcd[2] = c;
  abcd[3] = d;
#endif  // HWY_COMPILER_MSVC
}

HWY_INLINE bool IsBitSet(const uint32_t reg, const int index) {
  return (reg & (1U << index)) != 0;
}

// Returns the lower 32 bits of extended control register 0.
// Requires CPU support for "OSXSAVE" (see below).
uint32_t ReadXCR0() {
#if HWY_COMPILER_MSVC
  return static_cast<uint32_t>(_xgetbv(0));
#else   // HWY_COMPILER_MSVC
  uint32_t xcr0, xcr0_high;
  const uint32_t index = 0;
  asm volatile(".byte 0x0F, 0x01, 0xD0"
               : "=a"(xcr0), "=d"(xcr0_high)
               : "c"(index));
  return xcr0;
#endif  // HWY_COMPILER_MSVC
}

bool IsAMD() {
  uint32_t abcd[4];
  Cpuid(0, 0, abcd);
  const uint32_t max_level = abcd[0];
  return max_level >= 1 && abcd[1] == 0x68747541 && abcd[2] == 0x444d4163 &&
         abcd[3] == 0x69746e65;
}

// Arbitrary bit indices indicating which instruction set extensions are
// supported. Use enum to ensure values are distinct.
enum class FeatureIndex : uint32_t {
  kSSE = 0,
  kSSE2,
  kSSE3,
  kSSSE3,

  kSSE41,
  kSSE42,
  kCLMUL,
  kAES,

  kAVX,
  kAVX2,
  kF16C,
  kFMA,
  kLZCNT,
  kBMI,
  kBMI2,

  kAVX512F,
  kAVX512VL,
  kAVX512CD,
  kAVX512DQ,
  kAVX512BW,
  kAVX512FP16,
  kAVX512BF16,

  kVNNI,
  kVPCLMULQDQ,
  kVBMI,
  kVBMI2,
  kVAES,
  kPOPCNTDQ,
  kBITALG,
  kGFNI,

  kSentinel
};
static_assert(static_cast<size_t>(FeatureIndex::kSentinel) < 64,
              "Too many bits for u64");

HWY_INLINE constexpr uint64_t Bit(FeatureIndex index) {
  return 1ull << static_cast<size_t>(index);
}

// Returns bit array of FeatureIndex from CPUID feature flags.
uint64_t FlagsFromCPUID() {
  uint64_t flags = 0;  // return value
  uint32_t abcd[4];
  Cpuid(0, 0, abcd);
  const uint32_t max_level = abcd[0];

  // Standard feature flags
  Cpuid(1, 0, abcd);
  flags |= IsBitSet(abcd[3], 25) ? Bit(FeatureIndex::kSSE) : 0;
  flags |= IsBitSet(abcd[3], 26) ? Bit(FeatureIndex::kSSE2) : 0;
  flags |= IsBitSet(abcd[2], 0) ? Bit(FeatureIndex::kSSE3) : 0;
  flags |= IsBitSet(abcd[2], 1) ? Bit(FeatureIndex::kCLMUL) : 0;
  flags |= IsBitSet(abcd[2], 9) ? Bit(FeatureIndex::kSSSE3) : 0;
  flags |= IsBitSet(abcd[2], 12) ? Bit(FeatureIndex::kFMA) : 0;
  flags |= IsBitSet(abcd[2], 19) ? Bit(FeatureIndex::kSSE41) : 0;
  flags |= IsBitSet(abcd[2], 20) ? Bit(FeatureIndex::kSSE42) : 0;
  flags |= IsBitSet(abcd[2], 25) ? Bit(FeatureIndex::kAES) : 0;
  flags |= IsBitSet(abcd[2], 28) ? Bit(FeatureIndex::kAVX) : 0;
  flags |= IsBitSet(abcd[2], 29) ? Bit(FeatureIndex::kF16C) : 0;

  // Extended feature flags
  Cpuid(0x80000001U, 0, abcd);
  flags |= IsBitSet(abcd[2], 5) ? Bit(FeatureIndex::kLZCNT) : 0;

  // Extended features
  if (max_level >= 7) {
    Cpuid(7, 0, abcd);
    flags |= IsBitSet(abcd[1], 3) ? Bit(FeatureIndex::kBMI) : 0;
    flags |= IsBitSet(abcd[1], 5) ? Bit(FeatureIndex::kAVX2) : 0;
    flags |= IsBitSet(abcd[1], 8) ? Bit(FeatureIndex::kBMI2) : 0;

    flags |= IsBitSet(abcd[1], 16) ? Bit(FeatureIndex::kAVX512F) : 0;
    flags |= IsBitSet(abcd[1], 17) ? Bit(FeatureIndex::kAVX512DQ) : 0;
    flags |= IsBitSet(abcd[1], 28) ? Bit(FeatureIndex::kAVX512CD) : 0;
    flags |= IsBitSet(abcd[1], 30) ? Bit(FeatureIndex::kAVX512BW) : 0;
    flags |= IsBitSet(abcd[1], 31) ? Bit(FeatureIndex::kAVX512VL) : 0;

    flags |= IsBitSet(abcd[2], 1) ? Bit(FeatureIndex::kVBMI) : 0;
    flags |= IsBitSet(abcd[2], 6) ? Bit(FeatureIndex::kVBMI2) : 0;
    flags |= IsBitSet(abcd[2], 8) ? Bit(FeatureIndex::kGFNI) : 0;
    flags |= IsBitSet(abcd[2], 9) ? Bit(FeatureIndex::kVAES) : 0;
    flags |= IsBitSet(abcd[2], 10) ? Bit(FeatureIndex::kVPCLMULQDQ) : 0;
    flags |= IsBitSet(abcd[2], 11) ? Bit(FeatureIndex::kVNNI) : 0;
    flags |= IsBitSet(abcd[2], 12) ? Bit(FeatureIndex::kBITALG) : 0;
    flags |= IsBitSet(abcd[2], 14) ? Bit(FeatureIndex::kPOPCNTDQ) : 0;

    flags |= IsBitSet(abcd[3], 23) ? Bit(FeatureIndex::kAVX512FP16) : 0;

    Cpuid(7, 1, abcd);
    flags |= IsBitSet(abcd[0], 5) ? Bit(FeatureIndex::kAVX512BF16) : 0;
  }

  return flags;
}

// Each Highway target requires a 'group' of multiple features/flags.
constexpr uint64_t kGroupSSE2 =
    Bit(FeatureIndex::kSSE) | Bit(FeatureIndex::kSSE2);

constexpr uint64_t kGroupSSSE3 =
    Bit(FeatureIndex::kSSE3) | Bit(FeatureIndex::kSSSE3) | kGroupSSE2;

constexpr uint64_t kGroupSSE4 =
    Bit(FeatureIndex::kSSE41) | Bit(FeatureIndex::kSSE42) |
    Bit(FeatureIndex::kCLMUL) | Bit(FeatureIndex::kAES) | kGroupSSSE3;

// We normally assume BMI/BMI2/FMA are available if AVX2 is. This allows us to
// use BZHI and (compiler-generated) MULX. However, VirtualBox lacks them
// [https://www.virtualbox.org/ticket/15471]. Thus we provide the option of
// avoiding using and requiring these so AVX2 can still be used.
#ifdef HWY_DISABLE_BMI2_FMA
constexpr uint64_t kGroupBMI2_FMA = 0;
#else
constexpr uint64_t kGroupBMI2_FMA = Bit(FeatureIndex::kBMI) |
                                    Bit(FeatureIndex::kBMI2) |
                                    Bit(FeatureIndex::kFMA);
#endif

#ifdef HWY_DISABLE_F16C
constexpr uint64_t kGroupF16C = 0;
#else
constexpr uint64_t kGroupF16C = Bit(FeatureIndex::kF16C);
#endif

constexpr uint64_t kGroupAVX2 =
    Bit(FeatureIndex::kAVX) | Bit(FeatureIndex::kAVX2) |
    Bit(FeatureIndex::kLZCNT) | kGroupBMI2_FMA | kGroupF16C | kGroupSSE4;

constexpr uint64_t kGroupAVX3 =
    Bit(FeatureIndex::kAVX512F) | Bit(FeatureIndex::kAVX512VL) |
    Bit(FeatureIndex::kAVX512DQ) | Bit(FeatureIndex::kAVX512BW) |
    Bit(FeatureIndex::kAVX512CD) | kGroupAVX2;

constexpr uint64_t kGroupAVX3_DL =
    Bit(FeatureIndex::kVNNI) | Bit(FeatureIndex::kVPCLMULQDQ) |
    Bit(FeatureIndex::kVBMI) | Bit(FeatureIndex::kVBMI2) |
    Bit(FeatureIndex::kVAES) | Bit(FeatureIndex::kPOPCNTDQ) |
    Bit(FeatureIndex::kBITALG) | Bit(FeatureIndex::kGFNI) | kGroupAVX3;

constexpr uint64_t kGroupAVX3_ZEN4 =
    Bit(FeatureIndex::kAVX512BF16) | kGroupAVX3_DL;

constexpr uint64_t kGroupAVX3_SPR =
    Bit(FeatureIndex::kAVX512FP16) | kGroupAVX3_ZEN4;

int64_t DetectTargets() {
  int64_t bits = 0;  // return value of supported targets.
  HWY_IF_CONSTEXPR(HWY_ARCH_X86_64) {
    bits |= HWY_SSE2;  // always present in x64
  }

  const uint64_t flags = FlagsFromCPUID();
  // Set target bit(s) if all their group's flags are all set.
  if ((flags & kGroupAVX3_SPR) == kGroupAVX3_SPR) {
    bits |= HWY_AVX3_SPR;
  }
  if ((flags & kGroupAVX3_DL) == kGroupAVX3_DL) {
    bits |= HWY_AVX3_DL;
  }
  if ((flags & kGroupAVX3) == kGroupAVX3) {
    bits |= HWY_AVX3;
  }
  if ((flags & kGroupAVX2) == kGroupAVX2) {
    bits |= HWY_AVX2;
  }
  if ((flags & kGroupSSE4) == kGroupSSE4) {
    bits |= HWY_SSE4;
  }
  if ((flags & kGroupSSSE3) == kGroupSSSE3) {
    bits |= HWY_SSSE3;
  }
  HWY_IF_CONSTEXPR(HWY_ARCH_X86_32) {
    if ((flags & kGroupSSE2) == kGroupSSE2) {
      bits |= HWY_SSE2;
    }
  }

  // Clear AVX2/AVX3 bits if the CPU or OS does not support XSAVE - otherwise,
  // YMM/ZMM registers are not preserved across context switches.

  // The lower 128 bits of XMM0-XMM15 are guaranteed to be preserved across
  // context switches on x86_64

  // The following OS's are known to preserve the lower 128 bits of XMM
  // registers across context switches on x86 CPU's that support SSE (even in
  // 32-bit mode):
  // - Windows 2000 or later
  // - Linux 2.4.0 or later
  // - Mac OS X 10.4 or later
  // - FreeBSD 4.4 or later
  // - NetBSD 1.6 or later
  // - OpenBSD 3.5 or later
  // - UnixWare 7 Release 7.1.1 or later
  // - Solaris 9 4/04 or later

  uint32_t abcd[4];
  Cpuid(1, 0, abcd);
  const bool has_xsave = IsBitSet(abcd[2], 26);
  const bool has_osxsave = IsBitSet(abcd[2], 27);
  constexpr int64_t min_avx2 = HWY_AVX2 | (HWY_AVX2 - 1);

  if (has_xsave && has_osxsave) {
#if HWY_OS_APPLE
    // On macOS, check for AVX3 XSAVE support by checking that we are running on
    // macOS 12.2 or later and HasCpuFeature("hw.optional.avx512f") returns true

    // There is a bug in macOS 12.1 or earlier that can cause ZMM16-ZMM31, the
    // upper 256 bits of the ZMM registers, and K0-K7 (the AVX512 mask
    // registers) to not be properly preserved across a context switch on
    // macOS 12.1 or earlier.

    // This bug on macOS 12.1 or earlier on x86_64 CPU's with AVX3 support is
    // described at
    // https://community.intel.com/t5/Software-Tuning-Performance/MacOS-Darwin-kernel-bug-clobbers-AVX-512-opmask-register-state/m-p/1327259,
    // https://github.com/golang/go/issues/49233, and
    // https://github.com/simdutf/simdutf/pull/236.

    // In addition to the bug that is there on macOS 12.1 or earlier, bits 5, 6,
    // and 7 can be set to 0 on x86_64 CPU's with AVX3 support on macOS until
    // the first AVX512 instruction is executed as macOS only preserves
    // ZMM16-ZMM31, the upper 256 bits of the ZMM registers, and K0-K7 across a
    // context switch on threads that have executed an AVX512 instruction.

    // Checking for AVX3 XSAVE support on macOS using
    // HasCpuFeature("hw.optional.avx512f") avoids false negative results
    // on x86_64 CPU's that have AVX3 support.
    const bool have_avx3_xsave_support =
        IsMacOs12_2OrLater() && HasCpuFeature("hw.optional.avx512f");
#endif

    const uint32_t xcr0 = ReadXCR0();
    constexpr int64_t min_avx3 = HWY_AVX3 | HWY_AVX3_DL | HWY_AVX3_SPR;
    // XMM/YMM
    if (!IsBitSet(xcr0, 1) || !IsBitSet(xcr0, 2)) {
      // Clear the AVX2/AVX3 bits if XMM/YMM XSAVE is not enabled
      bits &= ~min_avx2;
    }

#if !HWY_OS_APPLE
    // On OS's other than macOS, check for AVX3 XSAVE support by checking that
    // bits 5, 6, and 7 of XCR0 are set.
    const bool have_avx3_xsave_support =
        IsBitSet(xcr0, 5) && IsBitSet(xcr0, 6) && IsBitSet(xcr0, 7);
#endif

    // opmask, ZMM lo/hi
    if (!have_avx3_xsave_support) {
      bits &= ~min_avx3;
    }
  } else {  // !has_xsave || !has_osxsave
    // Clear the AVX2/AVX3 bits if the CPU or OS does not support XSAVE
    bits &= ~min_avx2;
  }

  // This is mainly to work around the slow Zen4 CompressStore. It's unclear
  // whether subsequent AMD models will be affected; assume yes.
  if ((bits & HWY_AVX3_DL) && (flags & kGroupAVX3_ZEN4) == kGroupAVX3_ZEN4 &&
      IsAMD()) {
    bits |= HWY_AVX3_ZEN4;
  }

  return bits;
}

}  // namespace x86
#elif HWY_ARCH_ARM && HWY_HAVE_RUNTIME_DISPATCH
namespace arm {
int64_t DetectTargets() {
  int64_t bits = 0;  // return value of supported targets.

  using CapBits = unsigned long;  // NOLINT
#if HWY_OS_APPLE
  const CapBits hw = 0UL;
#else
  // For Android, this has been supported since API 20 (2014).
  const CapBits hw = getauxval(AT_HWCAP);
#endif
  (void)hw;

#if HWY_ARCH_ARM_A64
  bits |= HWY_NEON_WITHOUT_AES;  // aarch64 always has NEON and VFPv4..

#if HWY_OS_APPLE
  if (HasCpuFeature("hw.optional.arm.FEAT_AES")) {
    bits |= HWY_NEON;

    if (HasCpuFeature("hw.optional.AdvSIMD_HPFPCvt") &&
        HasCpuFeature("hw.optional.arm.FEAT_DotProd") &&
        HasCpuFeature("hw.optional.arm.FEAT_BF16")) {
      bits |= HWY_NEON_BF16;
    }
  }
#else  // !HWY_OS_APPLE
  // .. but not necessarily AES, which is required for HWY_NEON.
#if defined(HWCAP_AES)
  if (hw & HWCAP_AES) {
    bits |= HWY_NEON;

#if defined(HWCAP_ASIMDHP) && defined(HWCAP_ASIMDDP) && defined(HWCAP2_BF16)
    const CapBits hw2 = getauxval(AT_HWCAP2);
    const int64_t kGroupF16Dot = HWCAP_ASIMDHP | HWCAP_ASIMDDP;
    if ((hw & kGroupF16Dot) == kGroupF16Dot && (hw2 & HWCAP2_BF16)) {
      bits |= HWY_NEON_BF16;
    }
#endif  // HWCAP_ASIMDHP && HWCAP_ASIMDDP && HWCAP2_BF16
  }
#endif  // HWCAP_AES

#if defined(HWCAP_SVE)
  if (hw & HWCAP_SVE) {
    bits |= HWY_SVE;
  }
#endif

#ifndef HWCAP2_SVE2
#define HWCAP2_SVE2 (1 << 1)
#endif
#ifndef HWCAP2_SVEAES
#define HWCAP2_SVEAES (1 << 2)
#endif
  const CapBits hw2 = getauxval(AT_HWCAP2);
  if ((hw2 & HWCAP2_SVE2) && (hw2 & HWCAP2_SVEAES)) {
    bits |= HWY_SVE2;
  }
#endif  // HWY_OS_APPLE

#else  // !HWY_ARCH_ARM_A64

// Some old auxv.h / hwcap.h do not define these. If not, treat as unsupported.
#if defined(HWCAP_NEON) && defined(HWCAP_VFPv4)
  if ((hw & HWCAP_NEON) && (hw & HWCAP_VFPv4)) {
    bits |= HWY_NEON_WITHOUT_AES;
  }
#endif

  // aarch32 would check getauxval(AT_HWCAP2) & HWCAP2_AES, but we do not yet
  // support that platform, and Armv7 lacks AES entirely. Because HWY_NEON
  // requires native AES instructions, we do not enable that target here.

#endif  // HWY_ARCH_ARM_A64
  return bits;
}
}  // namespace arm
#elif HWY_ARCH_PPC && HWY_HAVE_RUNTIME_DISPATCH
namespace ppc {

#ifndef PPC_FEATURE_HAS_ALTIVEC
#define PPC_FEATURE_HAS_ALTIVEC 0x10000000
#endif

#ifndef PPC_FEATURE_HAS_VSX
#define PPC_FEATURE_HAS_VSX 0x00000080
#endif

#ifndef PPC_FEATURE2_ARCH_2_07
#define PPC_FEATURE2_ARCH_2_07 0x80000000
#endif

#ifndef PPC_FEATURE2_VEC_CRYPTO
#define PPC_FEATURE2_VEC_CRYPTO 0x02000000
#endif

#ifndef PPC_FEATURE2_ARCH_3_00
#define PPC_FEATURE2_ARCH_3_00 0x00800000
#endif

#ifndef PPC_FEATURE2_ARCH_3_1
#define PPC_FEATURE2_ARCH_3_1 0x00040000
#endif

using CapBits = unsigned long;  // NOLINT

// For AT_HWCAP, the others are for AT_HWCAP2
constexpr CapBits kGroupVSX = PPC_FEATURE_HAS_ALTIVEC | PPC_FEATURE_HAS_VSX;

#if defined(HWY_DISABLE_PPC8_CRYPTO)
constexpr CapBits kGroupPPC8 = PPC_FEATURE2_ARCH_2_07;
#else
constexpr CapBits kGroupPPC8 = PPC_FEATURE2_ARCH_2_07 | PPC_FEATURE2_VEC_CRYPTO;
#endif
constexpr CapBits kGroupPPC9 = kGroupPPC8 | PPC_FEATURE2_ARCH_3_00;
constexpr CapBits kGroupPPC10 = kGroupPPC9 | PPC_FEATURE2_ARCH_3_1;

int64_t DetectTargets() {
  int64_t bits = 0;  // return value of supported targets.

#if defined(AT_HWCAP) && defined(AT_HWCAP2)
  const CapBits hw = getauxval(AT_HWCAP);

  if ((hw & kGroupVSX) == kGroupVSX) {
    const CapBits hw2 = getauxval(AT_HWCAP2);
    if ((hw2 & kGroupPPC8) == kGroupPPC8) {
      bits |= HWY_PPC8;
    }
    if ((hw2 & kGroupPPC9) == kGroupPPC9) {
      bits |= HWY_PPC9;
    }
    if ((hw2 & kGroupPPC10) == kGroupPPC10) {
      bits |= HWY_PPC10;
    }
  }  // VSX
#endif  // defined(AT_HWCAP) && defined(AT_HWCAP2)

  return bits;
}
}  // namespace ppc
#elif HWY_ARCH_S390X && HWY_HAVE_RUNTIME_DISPATCH
namespace s390x {

#ifndef HWCAP_S390_VX
#define HWCAP_S390_VX 2048
#endif

#ifndef HWCAP_S390_VXE
#define HWCAP_S390_VXE 8192
#endif

#ifndef HWCAP_S390_VXRS_EXT2
#define HWCAP_S390_VXRS_EXT2 32768
#endif

using CapBits = unsigned long;  // NOLINT

constexpr CapBits kGroupZ14 = HWCAP_S390_VX | HWCAP_S390_VXE;
constexpr CapBits kGroupZ15 =
    HWCAP_S390_VX | HWCAP_S390_VXE | HWCAP_S390_VXRS_EXT2;

int64_t DetectTargets() {
  int64_t bits = 0;

#if defined(AT_HWCAP)
  const CapBits hw = getauxval(AT_HWCAP);

  if ((hw & kGroupZ14) == kGroupZ14) {
    bits |= HWY_Z14;
  }

  if ((hw & kGroupZ15) == kGroupZ15) {
    bits |= HWY_Z15;
  }
#endif

  return bits;
}
}  // namespace s390x
#elif HWY_ARCH_RISCV && HWY_HAVE_RUNTIME_DISPATCH
namespace rvv {

#ifndef HWCAP_RVV
#define COMPAT_HWCAP_ISA_V (1 << ('V' - 'A'))
#endif

using CapBits = unsigned long;  // NOLINT

int64_t DetectTargets() {
  int64_t bits = 0;

  const CapBits hw = getauxval(AT_HWCAP);

  if ((hw & COMPAT_HWCAP_ISA_V) == COMPAT_HWCAP_ISA_V) {
    size_t e8m1_vec_len;
#if HWY_ARCH_RISCV_64
    int64_t vtype_reg_val;
#else
    int32_t vtype_reg_val;
#endif

    // Check that a vuint8m1_t vector is at least 16 bytes and that tail
    // agnostic and mask agnostic mode are supported
    asm volatile(
        // Avoid compiler error on GCC or Clang if -march=rv64gcv1p0 or
        // -march=rv32gcv1p0 option is not specified on the command line
        ".option push\n\t"
        ".option arch, +v\n\t"
        "vsetvli %0, zero, e8, m1, ta, ma\n\t"
        "csrr %1, vtype\n\t"
        ".option pop"
        : "=r"(e8m1_vec_len), "=r"(vtype_reg_val));

    // The RVV target is supported if the VILL bit of VTYPE (the MSB bit of
    // VTYPE) is not set and the length of a vuint8m1_t vector is at least 16
    // bytes
    if (vtype_reg_val >= 0 && e8m1_vec_len >= 16) {
      bits |= HWY_RVV;
    }
  }

  return bits;
}
}  // namespace rvv
#endif  // HWY_ARCH_*

// Returns targets supported by the CPU, independently of DisableTargets.
// Factored out of SupportedTargets to make its structure more obvious. Note
// that x86 CPUID may take several hundred cycles.
int64_t DetectTargets() {
  // Apps will use only one of these (the default is EMU128), but compile flags
  // for this TU may differ from that of the app, so allow both.
  int64_t bits = HWY_SCALAR | HWY_EMU128;

#if HWY_ARCH_X86 && HWY_HAVE_RUNTIME_DISPATCH
  bits |= x86::DetectTargets();
#elif HWY_ARCH_ARM && HWY_HAVE_RUNTIME_DISPATCH
  bits |= arm::DetectTargets();
#elif HWY_ARCH_PPC && HWY_HAVE_RUNTIME_DISPATCH
  bits |= ppc::DetectTargets();
#elif HWY_ARCH_S390X && HWY_HAVE_RUNTIME_DISPATCH
  bits |= s390x::DetectTargets();
#elif HWY_ARCH_RISCV && HWY_HAVE_RUNTIME_DISPATCH
  bits |= rvv::DetectTargets();

#else
  // TODO(janwas): detect support for WASM.
  // This file is typically compiled without HWY_IS_TEST, but targets_test has
  // it set, and will expect all of its HWY_TARGETS (= all attainable) to be
  // supported.
  bits |= HWY_ENABLED_BASELINE;
#endif  // HWY_ARCH_*

  if ((bits & HWY_ENABLED_BASELINE) != HWY_ENABLED_BASELINE) {
    const uint64_t bits_u = static_cast<uint64_t>(bits);
    const uint64_t enabled = static_cast<uint64_t>(HWY_ENABLED_BASELINE);
    fprintf(stderr,
            "WARNING: CPU supports 0x%08x%08x, software requires 0x%08x%08x\n",
            static_cast<uint32_t>(bits_u >> 32),
            static_cast<uint32_t>(bits_u & 0xFFFFFFFF),
            static_cast<uint32_t>(enabled >> 32),
            static_cast<uint32_t>(enabled & 0xFFFFFFFF));
  }

  return bits;
}

}  // namespace

HWY_DLLEXPORT void DisableTargets(int64_t disabled_targets) {
  supported_mask_ = static_cast<int64_t>(~disabled_targets);
  // This will take effect on the next call to SupportedTargets, which is
  // called right before GetChosenTarget::Update. However, calling Update here
  // would make it appear that HWY_DYNAMIC_DISPATCH was called, which we want
  // to check in tests. We instead de-initialize such that the next
  // HWY_DYNAMIC_DISPATCH calls GetChosenTarget::Update via FunctionCache.
  GetChosenTarget().DeInit();
}

HWY_DLLEXPORT void SetSupportedTargetsForTest(int64_t targets) {
  supported_targets_for_test_ = targets;
  GetChosenTarget().DeInit();  // see comment above
}

HWY_DLLEXPORT int64_t SupportedTargets() {
  int64_t targets = supported_targets_for_test_;
  if (HWY_LIKELY(targets == 0)) {
    // Mock not active. Re-detect instead of caching just in case we're on a
    // heterogeneous ISA (also requires some app support to pin threads). This
    // is only reached on the first HWY_DYNAMIC_DISPATCH or after each call to
    // DisableTargets or SetSupportedTargetsForTest.
    targets = DetectTargets();

    // VectorBytes invokes HWY_DYNAMIC_DISPATCH. To prevent infinite recursion,
    // first set up ChosenTarget. No need to Update() again afterwards with the
    // final targets - that will be done by a caller of this function.
    GetChosenTarget().Update(targets);

    // Now that we can call VectorBytes, check for targets with specific sizes.
    if (HWY_ARCH_ARM_A64) {
      const size_t vec_bytes = VectorBytes();  // uncached, see declaration
      if ((targets & HWY_SVE) && vec_bytes == 32) {
        targets = static_cast<int64_t>(targets | HWY_SVE_256);
      } else {
        targets = static_cast<int64_t>(targets & ~HWY_SVE_256);
      }
      if ((targets & HWY_SVE2) && vec_bytes == 16) {
        targets = static_cast<int64_t>(targets | HWY_SVE2_128);
      } else {
        targets = static_cast<int64_t>(targets & ~HWY_SVE2_128);
      }
    }  // HWY_ARCH_ARM_A64
  }

  targets &= supported_mask_;
  return targets == 0 ? HWY_STATIC_TARGET : targets;
}

HWY_DLLEXPORT ChosenTarget& GetChosenTarget() {
  static ChosenTarget chosen_target;
  return chosen_target;
}

}  // namespace hwy
