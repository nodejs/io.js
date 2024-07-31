//
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
//

// Allow dynamic symbol lookup in the kernel VDSO page.
//
// VDSO stands for "Virtual Dynamic Shared Object" -- a page of
// executable code, which looks like a shared library, but doesn't
// necessarily exist anywhere on disk, and which gets mmap()ed into
// every process by kernels which support VDSO, such as 2.6.x for 32-bit
// executables, and 2.6.24 and above for 64-bit executables.
//
// More details could be found here:
// http://www.trilithium.com/johan/2005/08/linux-gate/
//
// VDSOSupport -- a class representing kernel VDSO (if present).
//
// Example usage:
//  VDSOSupport vdso;
//  VDSOSupport::SymbolInfo info;
//  typedef (*FN)(unsigned *, void *, void *);
//  FN fn = nullptr;
//  if (vdso.LookupSymbol("__vdso_getcpu", "LINUX_2.6", STT_FUNC, &info)) {
//     fn = reinterpret_cast<FN>(info.address);
//  }

#ifndef ABSL_DEBUGGING_INTERNAL_VDSO_SUPPORT_H_
#define ABSL_DEBUGGING_INTERNAL_VDSO_SUPPORT_H_

#include <atomic>

#include "absl/base/attributes.h"
#include "absl/debugging/internal/elf_mem_image.h"

#ifdef ABSL_HAVE_ELF_MEM_IMAGE

#ifdef ABSL_HAVE_VDSO_SUPPORT
#error ABSL_HAVE_VDSO_SUPPORT cannot be directly set
#else
#define ABSL_HAVE_VDSO_SUPPORT 1
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

// NOTE: this class may be used from within tcmalloc, and can not
// use any memory allocation routines.
class VDSOSupport {
 public:
  VDSOSupport();

  typedef ElfMemImage::SymbolInfo SymbolInfo;
  typedef ElfMemImage::SymbolIterator SymbolIterator;

  // On PowerPC64 VDSO symbols can either be of type STT_FUNC or STT_NOTYPE
  // depending on how the kernel is built.  The kernel is normally built with
  // STT_NOTYPE type VDSO symbols.  Let's make things simpler first by using a
  // compile-time constant.
#ifdef __powerpc64__
  enum { kVDSOSymbolType = STT_NOTYPE };
#else
  enum { kVDSOSymbolType = STT_FUNC };
#endif

  // Answers whether we have a vdso at all.
  bool IsPresent() const { return image_.IsPresent(); }

  // Allow to iterate over all VDSO symbols.
  SymbolIterator begin() const { return image_.begin(); }
  SymbolIterator end() const { return image_.end(); }

  // Look up versioned dynamic symbol in the kernel VDSO.
  // Returns false if VDSO is not present, or doesn't contain given
  // symbol/version/type combination.
  // If info_out != nullptr, additional details are filled in.
  bool LookupSymbol(const char *name, const char *version,
                    int symbol_type, SymbolInfo *info_out) const;

  // Find info about symbol (if any) which overlaps given address.
  // Returns true if symbol was found; false if VDSO isn't present
  // or doesn't have a symbol overlapping given address.
  // If info_out != nullptr, additional details are filled in.
  bool LookupSymbolByAddress(const void *address, SymbolInfo *info_out) const;

  // Used only for testing. Replace real VDSO base with a mock.
  // Returns previous value of vdso_base_. After you are done testing,
  // you are expected to call SetBase() with previous value, in order to
  // reset state to the way it was.
  const void *SetBase(const void *s);

  // Computes vdso_base_ and returns it. Should be called as early as
  // possible; before any thread creation, chroot or setuid.
  static const void *Init();

 private:
  // image_ represents VDSO ELF image in memory.
  // image_.ehdr_ == nullptr implies there is no VDSO.
  ElfMemImage image_;

  // Cached value of auxv AT_SYSINFO_EHDR, computed once.
  // This is a tri-state:
  //   kInvalidBase   => value hasn't been determined yet.
  //              0   => there is no VDSO.
  //           else   => vma of VDSO Elf{32,64}_Ehdr.
  //
  // When testing with mock VDSO, low bit is set.
  // The low bit is always available because vdso_base_ is
  // page-aligned.
  static std::atomic<const void *> vdso_base_;

  // NOLINT on 'long' because these routines mimic kernel api.
  // The 'cache' parameter may be used by some versions of the kernel,
  // and should be nullptr or point to a static buffer containing at
  // least two 'long's.
  static long InitAndGetCPU(unsigned *cpu, void *cache,     // NOLINT 'long'.
                            void *unused);
  static long GetCPUViaSyscall(unsigned *cpu, void *cache,  // NOLINT 'long'.
                               void *unused);
  typedef long (*GetCpuFn)(unsigned *cpu, void *cache,      // NOLINT 'long'.
                           void *unused);

  // This function pointer may point to InitAndGetCPU,
  // GetCPUViaSyscall, or __vdso_getcpu at different stages of initialization.
  ABSL_CONST_INIT static std::atomic<GetCpuFn> getcpu_fn_;

  friend int GetCPU(void);  // Needs access to getcpu_fn_.

  VDSOSupport(const VDSOSupport&) = delete;
  VDSOSupport& operator=(const VDSOSupport&) = delete;
};

// Same as sched_getcpu() on later glibc versions.
// Return current CPU, using (fast) __vdso_getcpu@LINUX_2.6 if present,
// otherwise use syscall(SYS_getcpu,...).
// May return -1 with errno == ENOSYS if the kernel doesn't
// support SYS_getcpu.
int GetCPU();

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_HAVE_ELF_MEM_IMAGE

#endif  // ABSL_DEBUGGING_INTERNAL_VDSO_SUPPORT_H_
