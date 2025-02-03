// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Platform-specific code for Linux goes here. For the POSIX-compatible
// parts, the implementation is in platform-posix.cc.

#include "src/base/platform/platform-linux.h"

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>

// Ubuntu Dapper requires memory pages to be marked as
// executable. Otherwise, OS raises an exception when executing code
// in that page.
#include <errno.h>
#include <fcntl.h>  // open
#include <stdarg.h>
#include <strings.h>   // index
#include <sys/mman.h>  // mmap & munmap & mremap
#include <sys/stat.h>  // open
#include <sys/sysmacros.h>
#include <sys/types.h>  // mmap & munmap
#include <unistd.h>     // sysconf

#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>

#include "src/base/logging.h"
#include "src/base/memory.h"

#undef MAP_TYPE

#include "src/base/macros.h"
#include "src/base/platform/platform-posix-time.h"
#include "src/base/platform/platform-posix.h"
#include "src/base/platform/platform.h"

namespace v8 {
namespace base {

TimezoneCache* OS::CreateTimezoneCache() {
  return new PosixDefaultTimezoneCache();
}

void OS::SignalCodeMovingGC() {
  // Support for ll_prof.py.
  //
  // The Linux profiler built into the kernel logs all mmap's with
  // PROT_EXEC so that analysis tools can properly attribute ticks. We
  // do a mmap with a name known by ll_prof.py and immediately munmap
  // it. This injects a GC marker into the stream of events generated
  // by the kernel and allows us to synchronize V8 code log and the
  // kernel log.
  long size = sysconf(_SC_PAGESIZE);  // NOLINT(runtime/int)
  FILE* f = fopen(OS::GetGCFakeMMapFile(), "w+");
  if (f == nullptr) {
    OS::PrintError("Failed to open %s\n", OS::GetGCFakeMMapFile());
    OS::Abort();
  }
  void* addr = mmap(OS::GetRandomMmapAddr(), size, PROT_READ | PROT_EXEC,
                    MAP_PRIVATE, fileno(f), 0);
  DCHECK_NE(MAP_FAILED, addr);
  Free(addr, size);
  fclose(f);
}

void OS::AdjustSchedulingParams() {}

void* OS::RemapShared(void* old_address, void* new_address, size_t size) {
  void* result =
      mremap(old_address, 0, size, MREMAP_FIXED | MREMAP_MAYMOVE, new_address);

  if (result == MAP_FAILED) {
    return nullptr;
  }
  DCHECK(result == new_address);
  return result;
}

std::optional<OS::MemoryRange> OS::GetFirstFreeMemoryRangeWithin(
    OS::Address boundary_start, OS::Address boundary_end, size_t minimum_size,
    size_t alignment) {
  std::optional<OS::MemoryRange> result;
  // This function assumes that the layout of the file is as follows:
  // hex_start_addr-hex_end_addr rwxp <unused data> [binary_file_name]
  // and the lines are arranged in increasing order of address.
  // If we encounter an unexpected situation we abort scanning further entries.
  FILE* fp = fopen("/proc/self/maps", "r");
  if (fp == nullptr) return {};

  // Search for the gaps between existing virtual memory (vm) areas. If the gap
  // contains enough space for the requested-size range that is within the
  // boundary, push the overlapped memory range to the vector.
  uintptr_t gap_start = 0, gap_end = 0;
  // This loop will terminate once the scanning hits an EOF or reaches the gap
  // at the higher address to the end of boundary.
  uintptr_t vm_start;
  uintptr_t vm_end;
  while (fscanf(fp, "%" V8PRIxPTR "-%" V8PRIxPTR, &vm_start, &vm_end) == 2 &&
         gap_start < boundary_end) {
    // Visit the gap at the lower address to this vm.
    gap_end = vm_start;
    // Skip the gaps at the lower address to the start of boundary.
    if (gap_end > boundary_start) {
      // The available area is the overlap of the gap and boundary. Push
      // the overlapped memory range to the vector if there is enough space.
      const uintptr_t overlap_start =
          RoundUp(std::max(gap_start, boundary_start), alignment);
      const uintptr_t overlap_end =
          RoundDown(std::min(gap_end, boundary_end), alignment);
      if (overlap_start < overlap_end &&
          overlap_end - overlap_start >= minimum_size) {
        result = {overlap_start, overlap_end};
        break;
      }
    }
    // Continue to visit the next gap.
    gap_start = vm_end;

    int c;
    // Skip characters until we reach the end of the line or EOF.
    do {
      c = getc(fp);
    } while ((c != EOF) && (c != '\n'));
    if (c == EOF) break;
  }

  fclose(fp);
  return result;
}

//  static
std::optional<MemoryRegion> MemoryRegion::FromMapsLine(const char* line) {
  MemoryRegion region;
  unsigned dev_major = 0, dev_minor = 0;
  uintptr_t inode = 0;
  int path_index = 0;
  uintptr_t offset = 0;
  // The format is:
  // address           perms offset  dev   inode   pathname
  // 08048000-08056000 r-xp 00000000 03:0c 64593   /usr/sbin/gpm
  //
  // The final %n term captures the offset in the input string, which is used
  // to determine the path name. It *does not* increment the return value.
  // Refer to man 3 sscanf for details.
  if (sscanf(line,
             "%" V8PRIxPTR "-%" V8PRIxPTR " %4c %" V8PRIxPTR
             " %x:%x %" V8PRIdPTR " %n",
             &region.start, &region.end, region.permissions, &offset,
             &dev_major, &dev_minor, &inode, &path_index) < 7) {
    return std::nullopt;
  }
  region.permissions[4] = '\0';
  region.inode = inode;
  region.offset = offset;
  region.dev = makedev(dev_major, dev_minor);
  region.pathname.assign(line + path_index);

  return region;
}

namespace {
// Parses /proc/self/maps.
std::unique_ptr<std::vector<MemoryRegion>> ParseProcSelfMaps(
    FILE* fp, std::function<bool(const MemoryRegion&)> predicate,
    bool early_stopping) {
  auto result = std::make_unique<std::vector<MemoryRegion>>();

  if (!fp) fp = fopen("/proc/self/maps", "r");
  if (!fp) return nullptr;

  // Allocate enough room to be able to store a full file name.
  // 55ac243aa000-55ac243ac000 r--p 00000000 fe:01 31594735 /usr/bin/head
  const int kMaxLineLength = 2 * FILENAME_MAX;
  std::unique_ptr<char[]> line = std::make_unique<char[]>(kMaxLineLength);

  // This loop will terminate once the scanning hits an EOF.
  bool error = false;
  while (true) {
    error = true;

    // Read to the end of the line. Exit if the read fails.
    if (fgets(line.get(), kMaxLineLength, fp) == nullptr) {
      if (feof(fp)) error = false;
      break;
    }

    size_t line_length = strlen(line.get());
    // Empty line at the end.
    if (!line_length) {
      error = false;
      break;
    }
    // Line was truncated.
    if (line.get()[line_length - 1] != '\n') break;
    line.get()[line_length - 1] = '\0';

    std::optional<MemoryRegion> region = MemoryRegion::FromMapsLine(line.get());
    if (!region) {
      break;
    }

    error = false;

    if (predicate(*region)) {
      result->push_back(std::move(*region));
      if (early_stopping) break;
    }
  }

  fclose(fp);
  if (!error && !result->empty()) return result;

  return nullptr;
}

MemoryRegion FindEnclosingMapping(uintptr_t target_start, size_t size) {
  auto result = ParseProcSelfMaps(
      nullptr,
      [=](const MemoryRegion& region) {
        return region.start <= target_start && target_start + size < region.end;
      },
      true);
  if (result)
    return (*result)[0];
  else
    return {};
}
}  // namespace

// static
std::vector<OS::SharedLibraryAddress> GetSharedLibraryAddresses(FILE* fp) {
  auto regions = ParseProcSelfMaps(
      fp,
      [](const MemoryRegion& region) {
        if (region.permissions[0] == 'r' && region.permissions[1] == '-' &&
            region.permissions[2] == 'x') {
          return true;
        }
        return false;
      },
      false);

  if (!regions) return {};

  std::vector<OS::SharedLibraryAddress> result;
  for (const MemoryRegion& region : *regions) {
    uintptr_t start = region.start;
#ifdef V8_OS_ANDROID
    if (region.pathname.size() < 4 ||
        region.pathname.compare(region.pathname.size() - 4, 4, ".apk") != 0) {
      // Only adjust {start} based on {offset} if the file isn't the APK,
      // since we load the library directly from the APK and don't want to
      // apply the offset of the .so in the APK as the libraries offset.
      start -= region.offset;
    }
#else
    start -= region.offset;
#endif
    result.emplace_back(region.pathname, start, region.end);
  }
  return result;
}

// static
std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  return ::v8::base::GetSharedLibraryAddresses(nullptr);
}

// static
bool OS::RemapPages(const void* address, size_t size, void* new_address,
                    MemoryPermission access) {
  uintptr_t address_addr = reinterpret_cast<uintptr_t>(address);

  DCHECK(IsAligned(address_addr, AllocatePageSize()));
  DCHECK(
      IsAligned(reinterpret_cast<uintptr_t>(new_address), AllocatePageSize()));
  DCHECK(IsAligned(size, AllocatePageSize()));

  MemoryRegion enclosing_region = FindEnclosingMapping(address_addr, size);
  // Not found.
  if (!enclosing_region.start) return false;

  // Anonymous mapping?
  if (enclosing_region.pathname.empty()) return false;

  // Since the file is already in use for executable code, this is most likely
  // to fail due to sandboxing, e.g. if open() is blocked outright.
  //
  // In Chromium on Android, the sandbox allows openat() but prohibits
  // open(). However, the libc uses openat() in its open() wrapper, and the
  // SELinux restrictions allow us to read from the path we want to look at,
  // so we are in the clear.
  //
  // Note that this may not be allowed by the sandbox on Linux (and Chrome
  // OS). On these systems, consider using mremap() with the MREMAP_DONTUNMAP
  // flag. However, since we need it on non-anonymous mapping, this would only
  // be available starting with version 5.13.
  int fd = open(enclosing_region.pathname.c_str(), O_RDONLY);
  if (fd == -1) return false;

  // Now we have a file descriptor to the same path the data we want to remap
  // comes from. But... is it the *same* file? This is not guaranteed (e.g. in
  // case of updates), so to avoid hard-to-track bugs, check that the
  // underlying file is the same using the device number and the inode. Inodes
  // are not unique across filesystems, and can be reused. The check works
  // here though, since we have the problems:
  // - Inode uniqueness: check device numbers.
  // - Inode reuse: the initial file is still open, since we are running code
  //   from it. So its inode cannot have been reused.
  struct stat stat_buf;
  if (fstat(fd, &stat_buf)) {
    close(fd);
    return false;
  }

  // Not the same file.
  if (stat_buf.st_dev != enclosing_region.dev ||
      stat_buf.st_ino != enclosing_region.inode) {
    close(fd);
    return false;
  }

  size_t offset_in_mapping = address_addr - enclosing_region.start;
  size_t offset_in_file = enclosing_region.offset + offset_in_mapping;
  int protection = GetProtectionFromMemoryPermission(access);

  void* mapped_address = mmap(new_address, size, protection,
                              MAP_FIXED | MAP_PRIVATE, fd, offset_in_file);
  // mmap() keeps the file open.
  close(fd);

  if (mapped_address != new_address) {
    // Should not happen, MAP_FIXED should always map where we want.
    UNREACHABLE();
  }

  return true;
}

}  // namespace base
}  // namespace v8
