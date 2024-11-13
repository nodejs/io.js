// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Platform-specific code for Cygwin goes here. For the POSIX-compatible
// parts, the implementation is in platform-posix.cc.

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <strings.h>    // index
#include <sys/mman.h>   // mmap & munmap
#include <sys/time.h>
#include <unistd.h>     // sysconf

#include <cmath>

#undef MAP_TYPE

#include "src/base/macros.h"
#include "src/base/platform/platform-posix.h"
#include "src/base/platform/platform.h"
#include "src/base/win32-headers.h"

namespace v8 {
namespace base {

namespace {

// The memory allocation implementation is taken from platform-win32.cc.

DWORD GetProtectionFromMemoryPermission(OS::MemoryPermission access) {
  switch (access) {
    case OS::MemoryPermission::kNoAccess:
    case OS::MemoryPermission::kNoAccessWillJitLater:
      return PAGE_NOACCESS;
    case OS::MemoryPermission::kRead:
      return PAGE_READONLY;
    case OS::MemoryPermission::kReadWrite:
      return PAGE_READWRITE;
    case OS::MemoryPermission::kReadWriteExecute:
      return PAGE_EXECUTE_READWRITE;
    case OS::MemoryPermission::kReadExecute:
      return PAGE_EXECUTE_READ;
  }
  UNREACHABLE();
}

uint8_t* RandomizedVirtualAlloc(size_t size, DWORD flags, DWORD protect,
                                void* hint) {
  LPVOID base = nullptr;

  // For executable or reserved pages try to use the address hint.
  if (protect != PAGE_READWRITE) {
    base = VirtualAlloc(hint, size, flags, protect);
  }

  // If that fails, let the OS find an address to use.
  if (base == nullptr) {
    base = VirtualAlloc(nullptr, size, flags, protect);
  }

  return reinterpret_cast<uint8_t*>(base);
}

}  // namespace

class CygwinTimezoneCache : public PosixTimezoneCache {
  const char* LocalTimezone(double time) override;

  double LocalTimeOffset(double time_ms, bool is_utc) override;

  ~CygwinTimezoneCache() override {}
};

const char* CygwinTimezoneCache::LocalTimezone(double time) {
  if (std::isnan(time)) return "";
  time_t tv = static_cast<time_t>(std::floor(time/msPerSecond));
  struct tm tm;
  struct tm* t = localtime_r(&tv, &tm);
  if (nullptr == t) return "";
  return tzname[0];  // The location of the timezone string on Cygwin.
}

double LocalTimeOffset(double time_ms, bool is_utc) {
  // On Cygwin, struct tm does not contain a tm_gmtoff field.
  time_t utc = time(nullptr);
  DCHECK_NE(utc, -1);
  struct tm tm;
  struct tm* loc = localtime_r(&utc, &tm);
  DCHECK_NOT_NULL(loc);
  // time - localtime includes any daylight savings offset, so subtract it.
  return static_cast<double>((mktime(loc) - utc) * msPerSecond -
                             (loc->tm_isdst > 0 ? 3600 * msPerSecond : 0));
}

// static
void* OS::Allocate(void* hint, size_t size, size_t alignment,
                   MemoryPermission access) {
  size_t page_size = AllocatePageSize();
  DCHECK_EQ(0, size % page_size);
  DCHECK_EQ(0, alignment % page_size);
  DCHECK_LE(page_size, alignment);
  hint = AlignedAddress(hint, alignment);

  DWORD flags = (access == OS::MemoryPermission::kNoAccess)
                    ? MEM_RESERVE
                    : MEM_RESERVE | MEM_COMMIT;
  DWORD protect = GetProtectionFromMemoryPermission(access);

  // First, try an exact size aligned allocation.
  uint8_t* base = RandomizedVirtualAlloc(size, flags, protect, hint);
  if (base == nullptr) return nullptr;  // Can't allocate, we're OOM.

  // If address is suitably aligned, we're done.
  uint8_t* aligned_base = RoundUp(base, alignment);
  if (base == aligned_base) return reinterpret_cast<void*>(base);

  // Otherwise, free it and try a larger allocation.
  Free(base, size);

  // Clear the hint. It's unlikely we can allocate at this address.
  hint = nullptr;

  // Add the maximum misalignment so we are guaranteed an aligned base address
  // in the allocated region.
  size_t padded_size = size + (alignment - page_size);
  const int kMaxAttempts = 3;
  aligned_base = nullptr;
  for (int i = 0; i < kMaxAttempts; ++i) {
    base = RandomizedVirtualAlloc(padded_size, flags, protect, hint);
    if (base == nullptr) return nullptr;  // Can't allocate, we're OOM.

    // Try to trim the allocation by freeing the padded allocation and then
    // calling VirtualAlloc at the aligned base.
    Free(base, padded_size);
    aligned_base = RoundUp(base, alignment);
    base = reinterpret_cast<uint8_t*>(
        VirtualAlloc(aligned_base, size, flags, protect));
    // We might not get the reduced allocation due to a race. In that case,
    // base will be nullptr.
    if (base != nullptr) break;
  }
  DCHECK_IMPLIES(base, base == aligned_base);
  return reinterpret_cast<void*>(base);
}

// static
void OS::Free(void* address, const size_t size) {
  DCHECK_EQ(0, static_cast<uintptr_t>(address) % AllocatePageSize());
  DCHECK_EQ(0, size % AllocatePageSize());
  USE(size);
  CHECK_NE(0, VirtualFree(address, 0, MEM_RELEASE));
}

// static
void OS::Release(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  CHECK_NE(0, VirtualFree(address, size, MEM_DECOMMIT));
}

// static
bool OS::SetPermissions(void* address, size_t size, MemoryPermission access) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  if (access == MemoryPermission::kNoAccess) {
    return VirtualFree(address, size, MEM_DECOMMIT) != 0;
  }
  DWORD protect = GetProtectionFromMemoryPermission(access);
  return VirtualAlloc(address, size, MEM_COMMIT, protect) != nullptr;
}

// static
bool OS::RecommitPages(void* address, size_t size, MemoryPermission access) {
  return SetPermissions(address, size, access);
}

// static
bool OS::DiscardSystemPages(void* address, size_t size) {
  // On Windows, discarded pages are not returned to the system immediately and
  // not guaranteed to be zeroed when returned to the application.
  using DiscardVirtualMemoryFunction =
      DWORD(WINAPI*)(PVOID virtualAddress, SIZE_T size);
  static std::atomic<DiscardVirtualMemoryFunction> discard_virtual_memory(
      reinterpret_cast<DiscardVirtualMemoryFunction>(-1));
  if (discard_virtual_memory ==
      reinterpret_cast<DiscardVirtualMemoryFunction>(-1))
    discard_virtual_memory =
        reinterpret_cast<DiscardVirtualMemoryFunction>(GetProcAddress(
            GetModuleHandle(L"Kernel32.dll"), "DiscardVirtualMemory"));
  // Use DiscardVirtualMemory when available because it releases faster than
  // MEM_RESET.
  DiscardVirtualMemoryFunction discard_function = discard_virtual_memory.load();
  if (discard_function) {
    DWORD ret = discard_function(address, size);
    if (!ret) return true;
  }
  // DiscardVirtualMemory is buggy in Win10 SP0, so fall back to MEM_RESET on
  // failure.
  void* ptr = VirtualAlloc(address, size, MEM_RESET, PAGE_READWRITE);
  CHECK(ptr);
  return ptr;
}

// static
bool OS::HasLazyCommits() {
  // TODO(alph): implement for the platform.
  return false;
}

std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  std::vector<SharedLibraryAddresses> result;
  // This function assumes that the layout of the file is as follows:
  // hex_start_addr-hex_end_addr rwxp <unused data> [binary_file_name]
  // If we encounter an unexpected situation we abort scanning further entries.
  FILE* fp = fopen("/proc/self/maps", "r");
  if (fp == nullptr) return result;

  // Allocate enough room to be able to store a full file name.
  const int kLibNameLen = FILENAME_MAX + 1;
  char* lib_name = reinterpret_cast<char*>(malloc(kLibNameLen));

  // This loop will terminate once the scanning hits an EOF.
  while (true) {
    uintptr_t start, end;
    char attr_r, attr_w, attr_x, attr_p;
    // Parse the addresses and permission bits at the beginning of the line.
    if (fscanf(fp, "%" V8PRIxPTR "-%" V8PRIxPTR, &start, &end) != 2) break;
    if (fscanf(fp, " %c%c%c%c", &attr_r, &attr_w, &attr_x, &attr_p) != 4) break;

    int c;
    if (attr_r == 'r' && attr_w != 'w' && attr_x == 'x') {
      // Found a read-only executable entry. Skip characters until we reach
      // the beginning of the filename or the end of the line.
      do {
        c = getc(fp);
      } while ((c != EOF) && (c != '\n') && (c != '/'));
      if (c == EOF) break;  // EOF: Was unexpected, just exit.

      // Process the filename if found.
      if (c == '/') {
        ungetc(c, fp);  // Push the '/' back into the stream to be read below.

        // Read to the end of the line. Exit if the read fails.
        if (fgets(lib_name, kLibNameLen, fp) == nullptr) break;

        // Drop the newline character read by fgets. We do not need to check
        // for a zero-length string because we know that we at least read the
        // '/' character.
        lib_name[strlen(lib_name) - 1] = '\0';
      } else {
        // No library name found, just record the raw address range.
        snprintf(lib_name, kLibNameLen,
                 "%08" V8PRIxPTR "-%08" V8PRIxPTR, start, end);
      }
      result.push_back(SharedLibraryAddress(lib_name, start, end));
    } else {
      // Entry not describing executable data. Skip to end of line to set up
      // reading the next entry.
      do {
        c = getc(fp);
      } while ((c != EOF) && (c != '\n'));
      if (c == EOF) break;
    }
  }
  free(lib_name);
  fclose(fp);
  return result;
}

void OS::SignalCodeMovingGC() {
  // Nothing to do on Cygwin.
}

void OS::AdjustSchedulingParams() {}

std::optional<OS::MemoryRange> OS::GetFirstFreeMemoryRangeWithin(
    OS::Address boundary_start, OS::Address boundary_end, size_t minimum_size,
    size_t alignment) {
  return std::nullopt;
}

}  // namespace base
}  // namespace v8
