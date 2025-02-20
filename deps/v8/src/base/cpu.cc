// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/cpu.h"

#if defined(V8_OS_STARBOARD)
#include "starboard/cpu_features.h"
#endif

#if V8_LIBC_MSVCRT
#include <intrin.h>  // __cpuid()
#endif
#if V8_OS_LINUX
#include <linux/auxvec.h>  // AT_HWCAP
#endif
#if V8_GLIBC_PREREQ(2, 16) || V8_OS_ANDROID
#include <sys/auxv.h>  // getauxval()
#endif
#if V8_OS_QNX
#include <sys/syspage.h>  // cpuinfo
#endif
#if V8_OS_LINUX && V8_HOST_ARCH_PPC64
#include <elf.h>
#endif
#if V8_OS_AIX
#include <sys/systemcfg.h>  // _system_configuration
#ifndef POWER_8
#define POWER_8 0x10000
#endif
#ifndef POWER_9
#define POWER_9 0x20000
#endif
#ifndef POWER_10
#define POWER_10 0x40000
#endif
#endif
#if V8_OS_DARWIN
#include <sys/sysctl.h>  // sysctlbyname
#endif
#if V8_OS_POSIX
#include <unistd.h>  // sysconf()
#endif

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "src/base/logging.h"
#include "src/base/platform/wrappers.h"
#if V8_OS_WIN
#include <windows.h>
#endif

namespace v8 {
namespace base {

#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64

// Define __cpuid() for non-MSVC libraries.
#if !V8_LIBC_MSVCRT

static V8_INLINE void __cpuid(int cpu_info[4], int info_type) {
// Clear ecx to align with __cpuid() of MSVC:
// https://msdn.microsoft.com/en-us/library/hskdteyh.aspx
#if defined(__i386__) && defined(__pic__)
  // Make sure to preserve ebx, which contains the pointer
  // to the GOT in case we're generating PIC.
  __asm__ volatile(
      "mov %%ebx, %%edi\n\t"
      "cpuid\n\t"
      "xchg %%edi, %%ebx\n\t"
      : "=a"(cpu_info[0]), "=D"(cpu_info[1]), "=c"(cpu_info[2]),
        "=d"(cpu_info[3])
      : "a"(info_type), "c"(0));
#else
  __asm__ volatile("cpuid \n\t"
                   : "=a"(cpu_info[0]), "=b"(cpu_info[1]), "=c"(cpu_info[2]),
                     "=d"(cpu_info[3])
                   : "a"(info_type), "c"(0));
#endif  // defined(__i386__) && defined(__pic__)
}

static V8_INLINE void __cpuidex(int cpu_info[4], int info_type,
                                int sub_info_type) {
// Gather additional information about the processor.
// Set the value of the ECX register to sub_info_type before it generates the
// cpuid instruction, align with __cpuidex() of MSVC:
// https://msdn.microsoft.com/en-us/library/hskdteyh.aspx
#if defined(__i386__) && defined(__pic__)
  // Make sure to preserve ebx, which contains the pointer
  // to the GOT in case we're generating PIC.
  __asm__ volatile(
      "mov %%ebx, %%edi\n\t"
      "cpuid\n\t"
      "xchg %%edi, %%ebx\n\t"
      : "=a"(cpu_info[0]), "=D"(cpu_info[1]), "=c"(cpu_info[2]),
        "=d"(cpu_info[3])
      : "a"(info_type), "c"(sub_info_type));
#else
  __asm__ volatile("cpuid \n\t"
                   : "=a"(cpu_info[0]), "=b"(cpu_info[1]), "=c"(cpu_info[2]),
                     "=d"(cpu_info[3])
                   : "a"(info_type), "c"(sub_info_type));
#endif  // defined(__i386__) && defined(__pic__)
}

#endif  // !V8_LIBC_MSVCRT

#elif V8_HOST_ARCH_ARM || V8_HOST_ARCH_ARM64 || V8_HOST_ARCH_MIPS64 || \
    V8_HOST_ARCH_RISCV64

#if V8_OS_LINUX

#if V8_HOST_ARCH_ARM

// See <uapi/asm/hwcap.h> kernel header.
/*
 * HWCAP flags - for elf_hwcap (in kernel) and AT_HWCAP
 */
#define HWCAP_SWP (1 << 0)
#define HWCAP_HALF  (1 << 1)
#define HWCAP_THUMB (1 << 2)
#define HWCAP_26BIT (1 << 3)  /* Play it safe */
#define HWCAP_FAST_MULT (1 << 4)
#define HWCAP_FPA (1 << 5)
#define HWCAP_VFP (1 << 6)
#define HWCAP_EDSP  (1 << 7)
#define HWCAP_JAVA  (1 << 8)
#define HWCAP_IWMMXT  (1 << 9)
#define HWCAP_CRUNCH  (1 << 10)
#define HWCAP_THUMBEE (1 << 11)
#define HWCAP_NEON  (1 << 12)
#define HWCAP_VFPv3 (1 << 13)
#define HWCAP_VFPv3D16  (1 << 14) /* also set for VFPv4-D16 */
#define HWCAP_TLS (1 << 15)
#define HWCAP_VFPv4 (1 << 16)
#define HWCAP_IDIVA (1 << 17)
#define HWCAP_IDIVT (1 << 18)
#define HWCAP_VFPD32  (1 << 19) /* set if VFP has 32 regs (not 16) */
#define HWCAP_IDIV  (HWCAP_IDIVA | HWCAP_IDIVT)
#define HWCAP_LPAE  (1 << 20)

#endif  // V8_HOST_ARCH_ARM

#if V8_HOST_ARCH_ARM64

// See <uapi/asm/hwcap.h> kernel header.
/*
 * HWCAP flags - for elf_hwcap (in kernel) and AT_HWCAP
 */
#define HWCAP_FP (1 << 0)
#define HWCAP_ASIMD (1 << 1)
#define HWCAP_EVTSTRM (1 << 2)
#define HWCAP_AES (1 << 3)
#define HWCAP_PMULL (1 << 4)
#define HWCAP_SHA1 (1 << 5)
#define HWCAP_SHA2 (1 << 6)
#define HWCAP_CRC32 (1 << 7)
#define HWCAP_ATOMICS (1 << 8)
#define HWCAP_FPHP (1 << 9)
#define HWCAP_ASIMDHP (1 << 10)
#define HWCAP_CPUID (1 << 11)
#define HWCAP_ASIMDRDM (1 << 12)
#define HWCAP_JSCVT (1 << 13)
#define HWCAP_FCMA (1 << 14)
#define HWCAP_LRCPC (1 << 15)
#define HWCAP_DCPOP (1 << 16)
#define HWCAP_SHA3 (1 << 17)
#define HWCAP_SM3 (1 << 18)
#define HWCAP_SM4 (1 << 19)
#define HWCAP_ASIMDDP (1 << 20)
#define HWCAP_SHA512 (1 << 21)
#define HWCAP_SVE (1 << 22)
#define HWCAP_ASIMDFHM (1 << 23)
#define HWCAP_DIT (1 << 24)
#define HWCAP_USCAT (1 << 25)
#define HWCAP_ILRCPC (1 << 26)
#define HWCAP_FLAGM (1 << 27)
#define HWCAP_SSBS (1 << 28)
#define HWCAP_SB (1 << 29)
#define HWCAP_PACA (1 << 30)
#define HWCAP_PACG (1UL << 31)
// See <uapi/asm/hwcap.h> kernel header.
/*
 * HWCAP2 flags - for elf_hwcap2 (in kernel) and AT_HWCAP2
 */
#define HWCAP2_MTE (1 << 18)
#endif  // V8_HOST_ARCH_ARM64

#if V8_HOST_ARCH_ARM || V8_HOST_ARCH_ARM64

static std::tuple<uint32_t, uint32_t> ReadELFHWCaps() {
  uint32_t hwcap = 0;
  uint32_t hwcap2 = 0;
#if (V8_GLIBC_PREREQ(2, 16) || V8_OS_ANDROID) && defined(AT_HWCAP)
  hwcap = static_cast<uint32_t>(getauxval(AT_HWCAP));
#if defined(AT_HWCAP2)
  hwcap2 = static_cast<uint32_t>(getauxval(AT_HWCAP2));
#endif  // AT_HWCAP2
#else
  // Read the ELF HWCAP flags by parsing /proc/self/auxv.
  // If getauxval is not available, the kernel/libc is also not new enough to
  // expose hwcap2.
  FILE* fp = base::Fopen("/proc/self/auxv", "r");
  if (fp != nullptr) {
    struct {
      uint32_t tag;
      uint32_t value;
    } entry;
    for (;;) {
      size_t n = fread(&entry, sizeof(entry), 1, fp);
      if (n == 0 || (entry.tag == 0 && entry.value == 0)) {
        break;
      }
      if (entry.tag == AT_HWCAP) {
        hwcap = entry.value;
        break;
      }
    }
    base::Fclose(fp);
  }
#endif
  return std::make_tuple(hwcap, hwcap2);
}

#endif  // V8_HOST_ARCH_ARM || V8_HOST_ARCH_ARM64

// Extract the information exposed by the kernel via /proc/cpuinfo.
class CPUInfo final {
 public:
  CPUInfo() : datalen_(0) {
    // Get the size of the cpuinfo file by reading it until the end. This is
    // required because files under /proc do not always return a valid size
    // when using fseek(0, SEEK_END) + ftell(). Nor can the be mmap()-ed.
    static const char PATHNAME[] = "/proc/cpuinfo";
    FILE* fp = base::Fopen(PATHNAME, "r");
    if (fp != nullptr) {
      for (;;) {
        char buffer[256];
        size_t n = fread(buffer, 1, sizeof(buffer), fp);
        if (n == 0) {
          break;
        }
        datalen_ += n;
      }
      base::Fclose(fp);
    }

    // Read the contents of the cpuinfo file.
    data_ = new char[datalen_ + 1];
    fp = base::Fopen(PATHNAME, "r");
    if (fp != nullptr) {
      for (size_t offset = 0; offset < datalen_; ) {
        size_t n = fread(data_ + offset, 1, datalen_ - offset, fp);
        if (n == 0) {
          break;
        }
        offset += n;
      }
      base::Fclose(fp);
    }

    // Zero-terminate the data.
    data_[datalen_] = '\0';
  }

  ~CPUInfo() {
    delete[] data_;
  }

  // Extract the content of a the first occurrence of a given field in
  // the content of the cpuinfo file and return it as a heap-allocated
  // string that must be freed by the caller using delete[].
  // Return nullptr if not found.
  char* ExtractField(const char* field) const {
    DCHECK_NOT_NULL(field);

    // Look for first field occurrence, and ensure it starts the line.
    size_t fieldlen = strlen(field);
    char* p = data_;
    for (;;) {
      p = strstr(p, field);
      if (p == nullptr) {
        return nullptr;
      }
      if (p == data_ || p[-1] == '\n') {
        break;
      }
      p += fieldlen;
    }

    // Skip to the first colon followed by a space.
    p = strchr(p + fieldlen, ':');
    if (p == nullptr || !isspace(p[1])) {
      return nullptr;
    }
    p += 2;

    // Find the end of the line.
    char* q = strchr(p, '\n');
    if (q == nullptr) {
      q = data_ + datalen_;
    }

    // Copy the line into a heap-allocated buffer.
    size_t len = q - p;
    char* result = new char[len + 1];
    if (result != nullptr) {
      memcpy(result, p, len);
      result[len] = '\0';
    }
    return result;
  }

 private:
  char* data_;
  size_t datalen_;
};

// Checks that a space-separated list of items contains one given 'item'.
static bool HasListItem(const char* list, const char* item) {
  ssize_t item_len = strlen(item);
  const char* p = list;
  if (p != nullptr) {
    while (*p != '\0') {
      // Skip whitespace.
      while (isspace(*p)) ++p;

      // Find end of current list item.
      const char* q = p;
      while (*q != '\0' && !isspace(*q)) ++q;

      if (item_len == q - p && memcmp(p, item, item_len) == 0) {
        return true;
      }

      // Skip to next item.
      p = q;
    }
  }
  return false;
}

#endif  // V8_OS_LINUX

#endif  // V8_HOST_ARCH_ARM || V8_HOST_ARCH_ARM64 ||
        // V8_HOST_ARCH_MIPS64 || V8_HOST_ARCH_RISCV64

#if defined(V8_OS_STARBOARD)

bool CPU::StarboardDetectCPU() {
  SbCPUFeatures features;
  if (!SbCPUFeaturesGet(&features)) {
    return false;
  }
  architecture_ = features.arm.architecture_generation;
  switch (features.architecture) {
    case kSbCPUFeaturesArchitectureArm:
    case kSbCPUFeaturesArchitectureArm64:
      has_neon_ = features.arm.has_neon;
      has_thumb2_ = features.arm.has_thumb2;
      has_vfp_ = features.arm.has_vfp;
      has_vfp3_ = features.arm.has_vfp3;
      has_vfp3_d32_ = features.arm.has_vfp3_d32;
      has_idiva_ = features.arm.has_idiva;
      break;
    case kSbCPUFeaturesArchitectureX86:
    case kSbCPUFeaturesArchitectureX86_64:
      // Following flags are mandatory for V8
      has_cmov_ = features.x86.has_cmov;
      has_sse2_ = features.x86.has_sse2;
      // These flags are optional
      has_sse3_ = features.x86.has_sse3;
      has_ssse3_ = features.x86.has_ssse3;
      has_sse41_ = features.x86.has_sse41;
      has_sahf_ = features.x86.has_sahf;
      has_avx_ = features.x86.has_avx;
      has_avx2_ = features.x86.has_avx2;
      // TODO: Support AVX-VNNI on Starboard
      has_fma3_ = features.x86.has_fma3;
      has_bmi1_ = features.x86.has_bmi1;
      has_bmi2_ = features.x86.has_bmi2;
      has_lzcnt_ = features.x86.has_lzcnt;
      has_popcnt_ = features.x86.has_popcnt;
      has_f16c_ = features.x86.has_f16c;
      break;
    default:
      return false;
  }

  return true;
}

#endif

CPU::CPU()
    : stepping_(0),
      model_(0),
      ext_model_(0),
      family_(0),
      ext_family_(0),
      type_(0),
      implementer_(0),
      architecture_(0),
      variant_(-1),
      part_(0),
      icache_line_size_(kUnknownCacheLineSize),
      dcache_line_size_(kUnknownCacheLineSize),
      num_virtual_address_bits_(kUnknownNumVirtualAddressBits),
      has_fpu_(false),
      has_cmov_(false),
      has_sahf_(false),
      has_mmx_(false),
      has_sse_(false),
      has_sse2_(false),
      has_sse3_(false),
      has_ssse3_(false),
      has_sse41_(false),
      has_sse42_(false),
      is_atom_(false),
      has_intel_jcc_erratum_(false),
      has_osxsave_(false),
      has_avx_(false),
      has_avx2_(false),
      has_avx_vnni_(false),
      has_avx_vnni_int8_(false),
      has_fma3_(false),
      has_f16c_(false),
      has_bmi1_(false),
      has_bmi2_(false),
      has_lzcnt_(false),
      has_popcnt_(false),
      has_idiva_(false),
      has_neon_(false),
      has_thumb2_(false),
      has_vfp_(false),
      has_vfp3_(false),
      has_vfp3_d32_(false),
      has_jscvt_(false),
      has_dot_prod_(false),
      has_lse_(false),
      has_mte_(false),
      has_pmull1q_(false),
      has_fp16_(false),
      is_fp64_mode_(false),
      has_non_stop_time_stamp_counter_(false),
      is_running_in_vm_(false),
      has_msa_(false),
      riscv_mmu_(RV_MMU_MODE::kRiscvSV48),
      has_rvv_(false),
      has_zba_(false),
      has_zbb_(false),
      has_zbs_(false) {
  memcpy(vendor_, "Unknown", 8);

#if defined(V8_OS_STARBOARD)
  if (StarboardDetectCPU()) {
    return;
  }
#endif

#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
  int cpu_info[4];

  // __cpuid with an InfoType argument of 0 returns the number of
  // valid Ids in CPUInfo[0] and the CPU identification string in
  // the other three array elements. The CPU identification string is
  // not in linear order. The code below arranges the information
  // in a human readable form. The human readable order is CPUInfo[1] |
  // CPUInfo[3] | CPUInfo[2]. CPUInfo[2] and CPUInfo[3] are swapped
  // before using memcpy to copy these three array elements to cpu_string.
  __cpuid(cpu_info, 0);
  unsigned num_ids = cpu_info[0];
  std::swap(cpu_info[2], cpu_info[3]);
  memcpy(vendor_, cpu_info + 1, 12);
  vendor_[12] = '\0';

  // Interpret CPU feature information.
  if (num_ids > 0) {
    __cpuid(cpu_info, 1);

    int cpu_info70[4] = {0};
    int cpu_info71[4] = {0};
    if (num_ids >= 7) {
      __cpuid(cpu_info70, 7);
      // Check the maximum input value for supported leaf 7 sub-leaves
      if (cpu_info70[0] >= 1) {
        __cpuidex(cpu_info71, 7, 1);
      }
    }

    stepping_ = cpu_info[0] & 0xF;
    model_ = ((cpu_info[0] >> 4) & 0xF) + ((cpu_info[0] >> 12) & 0xF0);
    family_ = (cpu_info[0] >> 8) & 0xF;
    type_ = (cpu_info[0] >> 12) & 0x3;
    ext_model_ = (cpu_info[0] >> 16) & 0xF;
    ext_family_ = (cpu_info[0] >> 20) & 0xFF;
    has_fpu_ = (cpu_info[3] & 0x00000001) != 0;
    has_cmov_ = (cpu_info[3] & 0x00008000) != 0;
    has_mmx_ = (cpu_info[3] & 0x00800000) != 0;
    has_sse_ = (cpu_info[3] & 0x02000000) != 0;
    has_sse2_ = (cpu_info[3] & 0x04000000) != 0;
    has_sse3_ = (cpu_info[2] & 0x00000001) != 0;
    has_ssse3_ = (cpu_info[2] & 0x00000200) != 0;
    has_sse41_ = (cpu_info[2] & 0x00080000) != 0;
    has_sse42_ = (cpu_info[2] & 0x00100000) != 0;
    has_popcnt_ = (cpu_info[2] & 0x00800000) != 0;
    has_osxsave_ = (cpu_info[2] & 0x08000000) != 0;
    has_avx_ = (cpu_info[2] & 0x10000000) != 0;
    has_avx2_ = (cpu_info70[1] & 0x00000020) != 0;
    has_avx_vnni_ = (cpu_info71[0] & 0x00000010) != 0;
    has_avx_vnni_int8_ = (cpu_info71[3] & 0x00000020) != 0;
    has_fma3_ = (cpu_info[2] & 0x00001000) != 0;
    has_f16c_ = (cpu_info[2] & 0x20000000) != 0;
    // CET shadow stack feature flag. See
    // https://en.wikipedia.org/wiki/CPUID#EAX=7,_ECX=0:_Extended_Features
    has_cetss_ = (cpu_info70[2] & 0x00000080) != 0;
    // "Hypervisor Present Bit: Bit 31 of ECX of CPUID leaf 0x1."
    // See https://lwn.net/Articles/301888/
    // This is checking for any hypervisor. Hypervisors may choose not to
    // announce themselves. Hypervisors trap CPUID and sometimes return
    // different results to underlying hardware.
    is_running_in_vm_ = (cpu_info[2] & 0x80000000) != 0;

    if (family_ == 0x6) {
      switch (model_) {
        case 0x1C:  // SLT
        case 0x26:
        case 0x36:
        case 0x27:
        case 0x35:
        case 0x37:  // SLM
        case 0x4A:
        case 0x4D:
        case 0x4C:  // AMT
        case 0x6E:
          is_atom_ = true;
      }

      // CPUs that are affected by Intel JCC erratum:
      // https://www.intel.com/content/dam/support/us/en/documents/processors/mitigations-jump-conditional-code-erratum.pdf
      switch (model_) {
        case 0x4E:
          has_intel_jcc_erratum_ = stepping_ == 0x3;
          break;
        case 0x55:
          has_intel_jcc_erratum_ = stepping_ == 0x4 || stepping_ == 0x7;
          break;
        case 0x5E:
          has_intel_jcc_erratum_ = stepping_ == 0x3;
          break;
        case 0x8E:
          has_intel_jcc_erratum_ = stepping_ == 0x9 || stepping_ == 0xA ||
                                   stepping_ == 0xB || stepping_ == 0xC;
          break;
        case 0x9E:
          has_intel_jcc_erratum_ = stepping_ == 0x9 || stepping_ == 0xA ||
                                   stepping_ == 0xB || stepping_ == 0xD;
          break;
        case 0xA6:
          has_intel_jcc_erratum_ = stepping_ == 0x0;
          break;
        case 0xAE:
          has_intel_jcc_erratum_ = stepping_ == 0xA;
          break;
      }
    }
  }

  // There are separate feature flags for VEX-encoded GPR instructions.
  if (num_ids >= 7) {
    __cpuid(cpu_info, 7);
    has_bmi1_ = (cpu_info[1] & 0x00000008) != 0;
    has_bmi2_ = (cpu_info[1] & 0x00000100) != 0;
  }

  // Query extended IDs.
  __cpuid(cpu_info, 0x80000000);
  unsigned num_ext_ids = cpu_info[0];

  // Interpret extended CPU feature information.
  if (num_ext_ids > 0x80000000) {
    __cpuid(cpu_info, 0x80000001);
    has_lzcnt_ = (cpu_info[2] & 0x00000020) != 0;
    // SAHF must be probed in long mode.
    has_sahf_ = (cpu_info[2] & 0x00000001) != 0;
  }

  // Check if CPU has non stoppable time stamp counter.
  const unsigned parameter_containing_non_stop_time_stamp_counter = 0x80000007;
  if (num_ext_ids >= parameter_containing_non_stop_time_stamp_counter) {
    __cpuid(cpu_info, parameter_containing_non_stop_time_stamp_counter);
    has_non_stop_time_stamp_counter_ = (cpu_info[3] & (1 << 8)) != 0;
  }

  const unsigned virtual_physical_address_bits = 0x80000008;
  if (num_ext_ids >= virtual_physical_address_bits) {
    __cpuid(cpu_info, virtual_physical_address_bits);
    num_virtual_address_bits_ = (cpu_info[0] >> 8) & 0xff;
  }

  // This logic is replicated from cpu.cc present in chromium.src
  if (!has_non_stop_time_stamp_counter_ && is_running_in_vm_) {
    int cpu_info_hv[4] = {};
    __cpuid(cpu_info_hv, 0x40000000);
    if (cpu_info_hv[1] == 0x7263694D &&  // Micr
        cpu_info_hv[2] == 0x666F736F &&  // osof
        cpu_info_hv[3] == 0x76482074) {  // t Hv
      // If CPUID says we have a variant TSC and a hypervisor has identified
      // itself and the hypervisor says it is Microsoft Hyper-V, then treat
      // TSC as invariant.
      //
      // Microsoft Hyper-V hypervisor reports variant TSC as there are some
      // scenarios (eg. VM live migration) where the TSC is variant, but for
      // our purposes we can treat it as invariant.
      has_non_stop_time_stamp_counter_ = true;
    }
  }
#elif V8_HOST_ARCH_ARM

#if V8_OS_LINUX

  CPUInfo cpu_info;

  // Extract implementor from the "CPU implementer" field.
  char* implementer = cpu_info.ExtractField("CPU implementer");
  if (implementer != nullptr) {
    char* end;
    implementer_ = strtol(implementer, &end, 0);
    if (end == implementer) {
      implementer_ = 0;
    }
    delete[] implementer;
  }

  char* variant = cpu_info.ExtractField("CPU variant");
  if (variant != nullptr) {
    char* end;
    variant_ = strtol(variant, &end, 0);
    if (end == variant) {
      variant_ = -1;
    }
    delete[] variant;
  }

  // Extract part number from the "CPU part" field.
  char* part = cpu_info.ExtractField("CPU part");
  if (part != nullptr) {
    char* end;
    part_ = strtol(part, &end, 0);
    if (end == part) {
      part_ = 0;
    }
    delete[] part;
  }

  // Extract architecture from the "CPU Architecture" field.
  // The list is well-known, unlike the the output of
  // the 'Processor' field which can vary greatly.
  // See the definition of the 'proc_arch' array in
  // $KERNEL/arch/arm/kernel/setup.c and the 'c_show' function in
  // same file.
  char* architecture = cpu_info.ExtractField("CPU architecture");
  if (architecture != nullptr) {
    char* end;
    architecture_ = strtol(architecture, &end, 10);
    if (end == architecture) {
      // Kernels older than 3.18 report "CPU architecture: AArch64" on ARMv8.
      if (strcmp(architecture, "AArch64") == 0) {
        architecture_ = 8;
      } else {
        architecture_ = 0;
      }
    }
    delete[] architecture;

    // Unfortunately, it seems that certain ARMv6-based CPUs
    // report an incorrect architecture number of 7!
    //
    // See http://code.google.com/p/android/issues/detail?id=10812
    //
    // We try to correct this by looking at the 'elf_platform'
    // field reported by the 'Processor' field, which is of the
    // form of "(v7l)" for an ARMv7-based CPU, and "(v6l)" for
    // an ARMv6-one. For example, the Raspberry Pi is one popular
    // ARMv6 device that reports architecture 7.
    if (architecture_ == 7) {
      char* processor = cpu_info.ExtractField("Processor");
      if (HasListItem(processor, "(v6l)")) {
        architecture_ = 6;
      }
      delete[] processor;
    }

    // elf_platform moved to the model name field in Linux v3.8.
    if (architecture_ == 7) {
      char* processor = cpu_info.ExtractField("model name");
      if (HasListItem(processor, "(v6l)")) {
        architecture_ = 6;
      }
      delete[] processor;
    }
  }

  // Try to extract the list of CPU features from ELF hwcaps.
  uint32_t hwcaps, hwcaps2;
  std::tie(hwcaps, hwcaps2) = ReadELFHWCaps();
  if (hwcaps != 0) {
    has_idiva_ = (hwcaps & HWCAP_IDIVA) != 0;
    has_neon_ = (hwcaps & HWCAP_NEON) != 0;
    has_vfp_ = (hwcaps & HWCAP_VFP) != 0;
    has_vfp3_ = (hwcaps & (HWCAP_VFPv3 | HWCAP_VFPv3D16 | HWCAP_VFPv4)) != 0;
    has_vfp3_d32_ = (has_vfp3_ && ((hwcaps & HWCAP_VFPv3D16) == 0 ||
                                   (hwcaps & HWCAP_VFPD32) != 0));
  } else {
    // Try to fallback to "Features" CPUInfo field.
    char* features = cpu_info.ExtractField("Features");
    has_idiva_ = HasListItem(features, "idiva");
    has_neon_ = HasListItem(features, "neon");
    has_thumb2_ = HasListItem(features, "thumb2");
    has_vfp_ = HasListItem(features, "vfp");
    if (HasListItem(features, "vfpv3d16")) {
      has_vfp3_ = true;
    } else if (HasListItem(features, "vfpv3")) {
      has_vfp3_ = true;
      has_vfp3_d32_ = true;
    }
    delete[] features;
  }

  // Some old kernels will report vfp not vfpv3. Here we make an attempt
  // to detect vfpv3 by checking for vfp *and* neon, since neon is only
  // available on architectures with vfpv3. Checking neon on its own is
  // not enough as it is possible to have neon without vfp.
  if (has_vfp_ && has_neon_) {
    has_vfp3_ = true;
  }

  // VFPv3 implies ARMv7, see ARM DDI 0406B, page A1-6.
  if (architecture_ < 7 && has_vfp3_) {
    architecture_ = 7;
  }

  // ARMv7 implies Thumb2.
  if (architecture_ >= 7) {
    has_thumb2_ = true;
  }

  // The earliest architecture with Thumb2 is ARMv6T2.
  if (has_thumb2_ && architecture_ < 6) {
    architecture_ = 6;
  }

  // We don't support any FPUs other than VFP.
  has_fpu_ = has_vfp_;

#elif V8_OS_QNX

  uint32_t cpu_flags = SYSPAGE_ENTRY(cpuinfo)->flags;
  if (cpu_flags & ARM_CPU_FLAG_V7) {
    architecture_ = 7;
    has_thumb2_ = true;
  } else if (cpu_flags & ARM_CPU_FLAG_V6) {
    architecture_ = 6;
    // QNX doesn't say if Thumb2 is available.
    // Assume false for the architectures older than ARMv7.
  }
  DCHECK_GE(architecture_, 6);
  has_fpu_ = (cpu_flags & CPU_FLAG_FPU) != 0;
  has_vfp_ = has_fpu_;
  if (cpu_flags & ARM_CPU_FLAG_NEON) {
    has_neon_ = true;
    has_vfp3_ = has_vfp_;
#ifdef ARM_CPU_FLAG_VFP_D32
    has_vfp3_d32_ = (cpu_flags & ARM_CPU_FLAG_VFP_D32) != 0;
#endif
  }
  has_idiva_ = (cpu_flags & ARM_CPU_FLAG_IDIV) != 0;

#endif  // V8_OS_LINUX

#elif V8_HOST_ARCH_MIPS64

  // Simple detection of FPU at runtime for Linux.
  // It is based on /proc/cpuinfo, which reveals hardware configuration
  // to user-space applications.  According to MIPS (early 2010), no similar
  // facility is universally available on the MIPS architectures,
  // so it's up to individual OSes to provide such.
  CPUInfo cpu_info;
  char* cpu_model = cpu_info.ExtractField("cpu model");
  has_fpu_ = HasListItem(cpu_model, "FPU");
  char* ASEs = cpu_info.ExtractField("ASEs implemented");
  has_msa_ = HasListItem(ASEs, "msa");
  delete[] cpu_model;
  delete[] ASEs;

#elif V8_HOST_ARCH_ARM64
#ifdef V8_OS_WIN
  // Windows makes high-resolution thread timing information available in
  // user-space.
  has_non_stop_time_stamp_counter_ = true;

  // Defined in winnt.h, but only in 10.0.20348.0 version of the Windows SDK.
  // Copy the value here to support older versions as well.
#if !defined(PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE)
  constexpr int PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE = 44;
#endif
#if !defined(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE)
  constexpr int PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE = 43;
#endif
#if !defined(PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE)
  constexpr int PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE = 34;
#endif
#if !defined(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE)
  constexpr int PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE = 30;
#endif

  has_jscvt_ =
      IsProcessorFeaturePresent(PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE);
  has_dot_prod_ =
      IsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE);
  has_lse_ =
      IsProcessorFeaturePresent(PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE);
  has_pmull1q_ =
      IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE);

#elif V8_OS_LINUX
  // Try to extract the list of CPU features from ELF hwcaps.
  uint32_t hwcaps, hwcaps2;
  std::tie(hwcaps, hwcaps2) = ReadELFHWCaps();
  has_mte_ = (hwcaps2 & HWCAP2_MTE) != 0;
  if (hwcaps != 0) {
    has_jscvt_ = (hwcaps & HWCAP_JSCVT) != 0;
    has_dot_prod_ = (hwcaps & HWCAP_ASIMDDP) != 0;
    has_lse_ = (hwcaps & HWCAP_ATOMICS) != 0;
    has_pmull1q_ = (hwcaps & HWCAP_PMULL) != 0;
    has_fp16_ = (hwcaps & HWCAP_FPHP) != 0;
  } else {
    // Try to fallback to "Features" CPUInfo field
    CPUInfo cpu_info;
    char* features = cpu_info.ExtractField("Features");
    has_jscvt_ = HasListItem(features, "jscvt");
    has_dot_prod_ = HasListItem(features, "asimddp");
    has_lse_ = HasListItem(features, "atomics");
    has_pmull1q_ = HasListItem(features, "pmull");
    has_fp16_ = HasListItem(features, "half");
    delete[] features;
  }
#elif V8_OS_DARWIN
#if V8_OS_IOS
  int64_t feat_jscvt = 0;
  size_t feat_jscvt_size = sizeof(feat_jscvt);
  if (sysctlbyname("hw.optional.arm.FEAT_JSCVT", &feat_jscvt, &feat_jscvt_size,
                   nullptr, 0) == -1) {
    has_jscvt_ = false;
  } else {
    has_jscvt_ = feat_jscvt;
  }
  int64_t feat_dot_prod = 0;
  size_t feat_dot_prod_size = sizeof(feat_dot_prod);
  if (sysctlbyname("hw.optional.arm.FEAT_DotProd", &feat_dot_prod,
                   &feat_dot_prod_size, nullptr, 0) == -1) {
    has_dot_prod_ = false;
  } else {
    has_dot_prod_ = feat_dot_prod;
  }
  int64_t feat_lse = 0;
  size_t feat_lse_size = sizeof(feat_lse);
  if (sysctlbyname("hw.optional.arm.FEAT_LSE", &feat_lse, &feat_lse_size,
                   nullptr, 0) == -1) {
    has_lse_ = false;
  } else {
    has_lse_ = feat_lse;
  }
  int64_t feat_pmull = 0;
  size_t feat_pmull_size = sizeof(feat_pmull);
  if (sysctlbyname("hw.optional.arm.FEAT_PMULL", &feat_pmull, &feat_pmull_size,
                   nullptr, 0) == -1) {
    has_pmull1q_ = false;
  } else {
    has_pmull1q_ = feat_pmull;
  }
  int64_t fp16 = 0;
  size_t fp16_size = sizeof(fp16);
  if (sysctlbyname("hw.optional.arm.FEAT_FP16", &fp16, &fp16_size, nullptr,
                   0) == -1) {
    has_fp16_ = false;
  } else {
    has_fp16_ = fp16;
  }
#else
  // ARM64 Macs always have JSCVT, ASIMDDP, FP16 and LSE.
  has_jscvt_ = true;
  has_dot_prod_ = true;
  has_lse_ = true;
  has_pmull1q_ = true;
  has_fp16_ = true;
#endif  // V8_OS_IOS
#endif  // V8_OS_WIN

#elif V8_HOST_ARCH_PPC64

#ifndef USE_SIMULATOR
#if V8_OS_LINUX
  // Read processor info from /proc/self/auxv.
  char* auxv_cpu_type = nullptr;
  FILE* fp = base::Fopen("/proc/self/auxv", "r");
  if (fp != nullptr) {
    Elf64_auxv_t entry;
    for (;;) {
      size_t n = fread(&entry, sizeof(entry), 1, fp);
      if (n == 0 || entry.a_type == AT_NULL) {
        break;
      }
      switch (entry.a_type) {
        case AT_PLATFORM:
          auxv_cpu_type = reinterpret_cast<char*>(entry.a_un.a_val);
          break;
        case AT_ICACHEBSIZE:
          icache_line_size_ = entry.a_un.a_val;
          break;
        case AT_DCACHEBSIZE:
          dcache_line_size_ = entry.a_un.a_val;
          break;
      }
    }
    base::Fclose(fp);
  }

  part_ = -1;
  if (auxv_cpu_type) {
    if (strcmp(auxv_cpu_type, "power10") == 0) {
      part_ = kPPCPower10;
    } else if (strcmp(auxv_cpu_type, "power9") == 0) {
      part_ = kPPCPower9;
    } else if (strcmp(auxv_cpu_type, "power8") == 0) {
      part_ = kPPCPower8;
    }
  }

#elif V8_OS_AIX
  switch (_system_configuration.implementation) {
    case POWER_10:
      part_ = kPPCPower10;
      break;
    case POWER_9:
      part_ = kPPCPower9;
      break;
    case POWER_8:
      part_ = kPPCPower8;
      break;
  }
#endif  // V8_OS_AIX
#endif  // !USE_SIMULATOR

#elif V8_HOST_ARCH_RISCV64
#if V8_OS_LINUX
  CPUInfo cpu_info;
#if (V8_GLIBC_PREREQ(2, 39))
#include <asm/hwprobe.h>
#include <asm/unistd.h>
  riscv_hwprobe pairs[] = {{RISCV_HWPROBE_KEY_IMA_EXT_0, 0}};
  if (!syscall(__NR_riscv_hwprobe, &pairs,
               sizeof(pairs) / sizeof(riscv_hwprobe), 0, nullptr, 0)) {
    if (pairs[0].value & RISCV_HWPROBE_IMA_V) {
      has_rvv_ = true;
    }
    if (pairs[0].value & RISCV_HWPROBE_IMA_FD) {
      has_fpu_ = true;
    }
    if (pairs[0].value & RISCV_HWPROBE_EXT_ZBA) {
      has_zba_ = true;
    }
    if (pairs[0].value & RISCV_HWPROBE_EXT_ZBB) {
      has_zbb_ = true;
    }
    if (pairs[0].value & RISCV_HWPROBE_EXT_ZBS) {
      has_zbs_ = true;
    }
  }
#else
  char* features = cpu_info.ExtractField("isa");

  if (HasListItem(features, "rv64imafdc")) {
    has_fpu_ = true;
  }
  if (HasListItem(features, "rv64imafdcv")) {
    has_fpu_ = true;
    has_rvv_ = true;
  }
#endif

  char* mmu = cpu_info.ExtractField("mmu");
  if (HasListItem(mmu, "sv48")) {
    riscv_mmu_ = RV_MMU_MODE::kRiscvSV48;
  }
  if (HasListItem(mmu, "sv39")) {
    riscv_mmu_ = RV_MMU_MODE::kRiscvSV39;
  }
  if (HasListItem(mmu, "sv57")) {
    riscv_mmu_ = RV_MMU_MODE::kRiscvSV57;
  }
#endif
#endif  // V8_HOST_ARCH_RISCV64
}

}  // namespace base
}  // namespace v8
