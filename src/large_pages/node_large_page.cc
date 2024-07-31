// Copyright (C) 2018 Intel Corporation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom
// the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
// OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.
//
// SPDX-License-Identifier: MIT

// The functions in this file map the .text section of Node.js into 2MB pages.
// They perform the following steps:
//
// 1: Find the Node.js binary's `.text` section in memory. This is done below in
//    `FindNodeTextRegion`. It is accomplished in a platform-specific way. On
//    Linux and FreeBSD, `dl_iterate_phdr(3)` is used. When the region is found,
//    it is "trimmed" as follows:
//    * Modify the start to point to the very beginning of the Node.js `.text`
//      section (from symbol `__node_text_start` declared in node_text_start.S).
//    * Possibly modify the end to account for the `lpstub` section which
//      contains `MoveTextRegionToLargePages`, the function we do not wish to
//      move (see below).
//    * Align the address of the start to its nearest higher large page
//      boundary.
//    * Align the address of the end to its nearest lower large page boundary.
//
// 2: Move the text region to large pages. This is done below in
//    `MoveTextRegionToLargePages`. We need to be very careful:
//    a) `MoveTextRegionToLargePages` itself should not be moved.
//       We use gcc attributes
//       (__section__) to put it outside the `.text` section,
//       (__aligned__) to align it at the 2M boundary, and
//       (__noline__) to not inline this function.
//    b) `MoveTextRegionToLargePages` should not call any function(s) that might
//       be moved.
//    To move the .text section, perform the following steps:
//      * Map a new, temporary area and copy the original code there.
//      * Use mmap using the start address with MAP_FIXED so we get exactly the
//        same virtual address (except on macOS). On platforms other than Linux,
//        use mmap flags to request hugepages.
//      * On Linux use madvise with MADV_HUGEPAGE to use anonymous 2MB pages.
//      * If successful copy the code to the newly mapped area and protect it to
//        be readable and executable.
//      * Unmap the temporary area.

#include "node_large_page.h"

#include <cerrno>   // NOLINT(build/include)

// Besides returning ENOTSUP at runtime we do nothing if this define is missing.
#if defined(NODE_ENABLE_LARGE_CODE_PAGES) && NODE_ENABLE_LARGE_CODE_PAGES
#include "debug_utils-inl.h"

#if defined(__linux__) || defined(__FreeBSD__)
#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif  // ifndef _GNU_SOURCE
#include <sys/prctl.h>
#if !defined(PR_SET_VMA)
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif
#elif defined(__FreeBSD__)
#include "uv.h"  // uv_exepath
#endif  // defined(__linux__)
#include <link.h>
#endif  // defined(__linux__) || defined(__FreeBSD__)

#include <sys/types.h>
#include <sys/mman.h>
#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#elif defined(__APPLE__)
#include <mach/vm_map.h>
#endif

#include <climits>  // PATH_MAX
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <fstream>

#if defined(__linux__) || defined(__FreeBSD__)
extern "C" {
// This symbol must be declared weak because this file becomes part of all
// Node.js targets (like node_mksnapshot, node_mkcodecache, and cctest) and
// those files do not supply the symbol.
extern char __attribute__((weak)) __node_text_start;
extern char __start_lpstub;
}  // extern "C"
#endif  // defined(__linux__) || defined(__FreeBSD__)

#endif  // defined(NODE_ENABLE_LARGE_CODE_PAGES) && NODE_ENABLE_LARGE_CODE_PAGES
namespace node {
#if defined(NODE_ENABLE_LARGE_CODE_PAGES) && NODE_ENABLE_LARGE_CODE_PAGES

namespace {

struct text_region {
  char* from = nullptr;
  char* to = nullptr;
  bool found_text_region = false;
};

static const size_t hps = 2L * 1024 * 1024;

template <typename... Args>
inline void Debug(std::string fmt, Args&&... args) {
  node::Debug(&per_process::enabled_debug_list,
              DebugCategory::HUGEPAGES,
              (std::string("Hugepages info: ") + fmt).c_str(),
              std::forward<Args>(args)...);
}

inline void PrintWarning(const char* warn) {
  fprintf(stderr, "Hugepages WARNING: %s\n", warn);
}

inline void PrintSystemError(int error) {
  PrintWarning(strerror(error));
}

inline uintptr_t hugepage_align_up(uintptr_t addr) {
  return (((addr) + (hps) - 1) & ~((hps) - 1));
}

inline uintptr_t hugepage_align_down(uintptr_t addr) {
  return ((addr) & ~((hps) - 1));
}

#if defined(__linux__) || defined(__FreeBSD__)
#if defined(__FreeBSD__)
#ifndef ElfW
#define ElfW(name) Elf_##name
#endif  // ifndef ElfW
#endif  // defined(__FreeBSD__)

struct dl_iterate_params {
  uintptr_t start = 0;
  uintptr_t end = 0;
  uintptr_t reference_sym = reinterpret_cast<uintptr_t>(&__node_text_start);
  std::string exename;
};

int FindMapping(struct dl_phdr_info* info, size_t, void* data) {
  auto dl_params = static_cast<dl_iterate_params*>(data);
  if (dl_params->exename == std::string(info->dlpi_name)) {
    for (int idx = 0; idx < info->dlpi_phnum; idx++) {
      const ElfW(Phdr)* phdr = &info->dlpi_phdr[idx];
      if (phdr->p_type == PT_LOAD && (phdr->p_flags & PF_X)) {
        uintptr_t start = info->dlpi_addr + phdr->p_vaddr;
        uintptr_t end = start + phdr->p_memsz;

        if (dl_params->reference_sym >= start &&
            dl_params->reference_sym <= end) {
          dl_params->start = start;
          dl_params->end = end;
          return 1;
        }
      }
    }
  }
  return 0;
}
#endif  // defined(__linux__) || defined(__FreeBSD__)

struct text_region FindNodeTextRegion() {
  struct text_region nregion;
#if defined(__linux__) || defined(__FreeBSD__)
  dl_iterate_params dl_params;
  uintptr_t lpstub_start = reinterpret_cast<uintptr_t>(&__start_lpstub);

#if defined(__FreeBSD__)
  // On FreeBSD we need the name of the binary, because `dl_iterate_phdr` does
  // not pass in an empty string as the `dlpi_name` of the binary but rather its
  // absolute path.
  {
    char selfexe[PATH_MAX];
    size_t count = sizeof(selfexe);
    if (uv_exepath(selfexe, &count))
      return nregion;
    dl_params.exename = std::string(selfexe, count);
  }
#endif  // defined(__FreeBSD__)

  if (dl_iterate_phdr(FindMapping, &dl_params) == 1) {
    Debug("start: %p - sym: %p - end: %p\n",
          reinterpret_cast<void*>(dl_params.start),
          reinterpret_cast<void*>(dl_params.reference_sym),
          reinterpret_cast<void*>(dl_params.end));

    dl_params.start = dl_params.reference_sym;
    if (lpstub_start > dl_params.start && lpstub_start <= dl_params.end) {
      Debug("Trimming end for lpstub: %p\n",
            reinterpret_cast<void*>(lpstub_start));
      dl_params.end = lpstub_start;
    }

    if (dl_params.start < dl_params.end) {
      char* from = reinterpret_cast<char*>(hugepage_align_up(dl_params.start));
      char* to = reinterpret_cast<char*>(hugepage_align_down(dl_params.end));
      Debug("Aligned range is %p - %p\n", from, to);
      if (from < to) {
        size_t pagecount = (to - from) / hps;
        if (pagecount > 0) {
          nregion.found_text_region = true;
          nregion.from = from;
          nregion.to = to;
        }
      }
    }
  }
#elif defined(__APPLE__)
  struct vm_region_submap_info_64 map;
  mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
  vm_address_t addr = 0UL;
  vm_size_t size = 0;
  natural_t depth = 1;

  while (true) {
    if (vm_region_recurse_64(mach_task_self(), &addr, &size, &depth,
                             reinterpret_cast<vm_region_info_64_t>(&map),
                             &count) != KERN_SUCCESS) {
      break;
    }

    if (map.is_submap) {
      depth++;
    } else {
      char* start = reinterpret_cast<char*>(hugepage_align_up(addr));
      char* end = reinterpret_cast<char*>(hugepage_align_down(addr+size));

      if (end > start && (map.protection & VM_PROT_READ) != 0 &&
          (map.protection & VM_PROT_EXECUTE) != 0) {
        nregion.found_text_region = true;
        nregion.from = start;
        nregion.to = end;
        break;
      }

      addr += size;
      size = 0;
    }
  }
#endif
  Debug("Found %d huge pages\n", (nregion.to - nregion.from) / hps);
  return nregion;
}

#if defined(__linux__)
bool IsTransparentHugePagesEnabled() {
  // File format reference:
  // https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/mm/huge_memory.c?id=13391c60da3308ed9980de0168f74cce6c62ac1d#n163
  const char* filename = "/sys/kernel/mm/transparent_hugepage/enabled";
  std::ifstream config_stream(filename, std::ios::in);
  if (!config_stream.good()) {
    PrintWarning("could not open /sys/kernel/mm/transparent_hugepage/enabled");
    return false;
  }

  std::string token;
  config_stream >> token;
  if ("[always]" == token) return true;
  config_stream >> token;
  if ("[madvise]" == token) return true;
  return false;
}
#elif defined(__FreeBSD__)
bool IsSuperPagesEnabled() {
  // It is enabled by default on amd64.
  unsigned int super_pages = 0;
  size_t super_pages_length = sizeof(super_pages);
  return sysctlbyname("vm.pmap.pg_ps_enabled",
                      &super_pages,
                      &super_pages_length,
                      nullptr,
                      0) != -1 &&
         super_pages >= 1;
}
#endif

// Functions in this class must always be inlined because they must end up in
// the `lpstub` section rather than the `.text` section.
class MemoryMapPointer {
 public:
  FORCE_INLINE explicit MemoryMapPointer() {}
  FORCE_INLINE bool operator==(void* rhs) const { return mem_ == rhs; }
  FORCE_INLINE void* mem() const { return mem_; }
  MemoryMapPointer(const MemoryMapPointer&) = delete;
  MemoryMapPointer(MemoryMapPointer&&) = delete;
  void operator= (const MemoryMapPointer&) = delete;
  void operator= (const MemoryMapPointer&&) = delete;
  FORCE_INLINE void Reset(void* start,
                          size_t size,
                          int prot,
                          int flags,
                          int fd = -1,
                          size_t offset = 0) {
    mem_ = mmap(start, size, prot, flags, fd, offset);
    size_ = size;
  }
  FORCE_INLINE void Reset() {
    mem_ = nullptr;
    size_ = 0;
  }
  static void SetName(void* mem, size_t size, const char* name) {
#if defined(__linux__)
    // Available since the 5.17 kernel release and if the
    // CONFIG_ANON_VMA_NAME option, we can set an identifier
    // to an anonymous mapped region. However if the kernel
    // option is not present or it s an older kernel, it is a no-op.
    if (mem != MAP_FAILED && mem != nullptr)
        prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME,
            reinterpret_cast<uintptr_t>(mem),
            size,
            reinterpret_cast<uintptr_t>(name));
#else
    (void)name;
#endif
  }
  FORCE_INLINE ~MemoryMapPointer() {
    if (mem_ == nullptr) return;
    if (mem_ == MAP_FAILED) return;
    if (munmap(mem_, size_) == 0) return;
    PrintSystemError(errno);
  }

 private:
  size_t size_ = 0;
  void* mem_ = nullptr;
};

}  // End of anonymous namespace

int
#if !defined(__APPLE__)
__attribute__((__section__("lpstub")))
#else
__attribute__((__section__("__TEXT,__lpstub")))
#endif
__attribute__((__aligned__(hps)))
__attribute__((__noinline__))
MoveTextRegionToLargePages(const text_region& r) {
  MemoryMapPointer nmem;
  MemoryMapPointer tmem;
  void* start = r.from;
  size_t size = r.to - r.from;

  // Allocate a temporary region and back up the code we will re-map.
  nmem.Reset(nullptr, size,
             PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS);
  if (nmem.mem() == MAP_FAILED) goto fail;
  memcpy(nmem.mem(), r.from, size);

#if defined(__linux__)
// We already know the original page is r-xp
// (PROT_READ, PROT_EXEC, MAP_PRIVATE)
// We want PROT_WRITE because we are writing into it.
// We want it at the fixed address and we use MAP_FIXED.
  tmem.Reset(start, size,
             PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED);
  if (tmem.mem() == MAP_FAILED) goto fail;
  if (madvise(tmem.mem(), size, 14 /* MADV_HUGEPAGE */) == -1) goto fail;
  memcpy(start, nmem.mem(), size);
#elif defined(__FreeBSD__)
  tmem.Reset(start, size,
             PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED |
             MAP_ALIGNED_SUPER);
  if (tmem.mem() == MAP_FAILED) goto fail;
  memcpy(start, nmem.mem(), size);
#elif defined(__APPLE__)
  // There is not enough room to reserve the mapping close
  // to the region address so we content to give a hint
  // without forcing the new address being closed to.
  // We explicitally gives all permission since we plan
  // to write into it.
  tmem.Reset(start, size,
             PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_PRIVATE | MAP_ANONYMOUS,
             VM_FLAGS_SUPERPAGE_SIZE_2MB);
  if (tmem.mem() == MAP_FAILED) goto fail;
  memcpy(tmem.mem(), nmem.mem(), size);
  if (mprotect(start, size, PROT_READ | PROT_WRITE | PROT_EXEC) == -1)
    goto fail;
  memcpy(start, tmem.mem(), size);
#endif

  if (mprotect(start, size, PROT_READ | PROT_EXEC) == -1) goto fail;
  MemoryMapPointer::SetName(start, size, "nodejs Large Page");

  // We need not `munmap(tmem, size)` on success.
  tmem.Reset();
  return 0;
fail:
  PrintSystemError(errno);
  return -1;
}
#endif  // defined(NODE_ENABLE_LARGE_CODE_PAGES) && NODE_ENABLE_LARGE_CODE_PAGES

// This is the primary API called from main.
int MapStaticCodeToLargePages() {
#if defined(NODE_ENABLE_LARGE_CODE_PAGES) && NODE_ENABLE_LARGE_CODE_PAGES
  bool have_thp = false;
#if defined(__linux__)
  have_thp = IsTransparentHugePagesEnabled();
#elif defined(__FreeBSD__)
  have_thp = IsSuperPagesEnabled();
#elif defined(__APPLE__)
  // pse-36 flag is present in recent mac x64 products.
  have_thp = true;
#endif
  if (!have_thp)
    return EACCES;

  struct text_region r = FindNodeTextRegion();
  if (r.found_text_region == false)
    return ENOENT;

  return MoveTextRegionToLargePages(r);
#else
  return ENOTSUP;
#endif
}

const char* LargePagesError(int status) {
  switch (status) {
    case ENOTSUP:
      return "Mapping to large pages is not supported.";

    case EACCES:
      return "Large pages are not enabled.";

    case ENOENT:
      return "failed to find text region";

    case -1:
      return "Mapping code to large pages failed. Reverting to default page "
          "size.";

    case 0:
      return "OK";

    default:
      return "Unknown error";
  }
}

}  // namespace node
