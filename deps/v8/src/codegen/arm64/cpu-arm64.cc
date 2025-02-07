// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// CPU specific code for arm independent of OS goes here.

#if V8_TARGET_ARCH_ARM64

#include "src/codegen/arm64/utils-arm64.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/flush-instruction-cache.h"

#if V8_OS_DARWIN
#include <libkern/OSCacheControl.h>
#endif

#if V8_OS_WIN
#include <windows.h>
#endif

namespace v8 {
namespace internal {

class CacheLineSizes {
 public:
  CacheLineSizes() {
#if !defined(V8_HOST_ARCH_ARM64) || defined(V8_OS_WIN) || defined(__APPLE__)
    cache_type_register_ = 0;
#else
    // Copy the content of the cache type register to a core register.
    __asm__ __volatile__("mrs %x[ctr], ctr_el0"
                         : [ctr] "=r"(cache_type_register_));
#endif
  }

  uint32_t icache_line_size() const { return ExtractCacheLineSize(0); }
  uint32_t dcache_line_size() const { return ExtractCacheLineSize(16); }

 private:
  uint32_t ExtractCacheLineSize(int cache_line_size_shift) const {
    // The cache type register holds the size of cache lines in words as a
    // power of two.
    return 4 << ((cache_type_register_ >> cache_line_size_shift) & 0xF);
  }

  uint32_t cache_type_register_;
};

void CpuFeatures::FlushICache(void* address, size_t length) {
#if defined(V8_HOST_ARCH_ARM64)
#if defined(V8_OS_WIN)
  ::FlushInstructionCache(GetCurrentProcess(), address, length);
#elif defined(V8_OS_DARWIN)
  sys_icache_invalidate(address, length);
#elif defined(V8_OS_LINUX)
  char* begin = reinterpret_cast<char*>(address);

  __builtin___clear_cache(begin, begin + length);
#else
  // The code below assumes user space cache operations are allowed. The goal
  // of this routine is to make sure the code generated is visible to the I
  // side of the CPU.

  uintptr_t start = reinterpret_cast<uintptr_t>(address);
  // Sizes will be used to generate a mask big enough to cover a pointer.
  CacheLineSizes sizes;
  uintptr_t dsize = sizes.dcache_line_size();
  uintptr_t isize = sizes.icache_line_size();
  // Cache line sizes are always a power of 2.
  DCHECK_EQ(CountSetBits(dsize, 64), 1);
  DCHECK_EQ(CountSetBits(isize, 64), 1);
  uintptr_t dstart = start & ~(dsize - 1);
  uintptr_t istart = start & ~(isize - 1);
  uintptr_t end = start + length;

  __asm__ __volatile__(
      // Clean every line of the D cache containing the target data.
      "0:                                \n\t"
      // dc       : Data Cache maintenance
      //    c     : Clean
      //     i    : Invalidate
      //      va  : by (Virtual) Address
      //        c : to the point of Coherency
      // See ARM DDI 0406B page B2-12 for more information.
      // We would prefer to use "cvau" (clean to the point of unification) here
      // but we use "civac" to work around Cortex-A53 errata 819472, 826319,
      // 827319 and 824069.
      "dc   civac, %[dline]               \n\t"
      "add  %[dline], %[dline], %[dsize]  \n\t"
      "cmp  %[dline], %[end]              \n\t"
      "b.lt 0b                            \n\t"
      // Barrier to make sure the effect of the code above is visible to the
      // rest of the world. dsb    : Data Synchronisation Barrier
      //    ish : Inner SHareable domain
      // The point of unification for an Inner Shareable shareability domain is
      // the point by which the instruction and data caches of all the
      // processors in that Inner Shareable shareability domain are guaranteed
      // to see the same copy of a memory location.  See ARM DDI 0406B page
      // B2-12 for more information.
      "dsb  ish                           \n\t"
      // Invalidate every line of the I cache containing the target data.
      "1:                                 \n\t"
      // ic      : instruction cache maintenance
      //    i    : invalidate
      //     va  : by address
      //       u : to the point of unification
      "ic   ivau, %[iline]                \n\t"
      "add  %[iline], %[iline], %[isize]  \n\t"
      "cmp  %[iline], %[end]              \n\t"
      "b.lt 1b                            \n\t"
      // Barrier to make sure the effect of the code above is visible to the
      // rest of the world.
      "dsb  ish                           \n\t"
      // Barrier to ensure any prefetching which happened before this code is
      // discarded.
      // isb : Instruction Synchronisation Barrier
      "isb                                \n\t"
      : [dline] "+r"(dstart), [iline] "+r"(istart)
      : [dsize] "r"(dsize), [isize] "r"(isize), [end] "r"(end)
      // This code does not write to memory but without the dependency gcc might
      // move this code before the code is generated.
      : "cc", "memory");
#endif  // V8_OS_WIN
#endif  // V8_HOST_ARCH_ARM64
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM64
