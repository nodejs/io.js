// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Platform-specific code for z/OS goes here. For the POSIX-compatible
// parts, the implementation is in platform-posix.cc.

// TODO(gabylb): zos - most OS class members here will be removed once mmap
// is fully implemented on z/OS, after which those in platform-posix.cc will
// be used.

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "src/base/platform/platform-posix.h"
#include "src/base/platform/platform.h"

namespace {

__attribute__((constructor)) void init() {
  zoslib_config_t config;
  init_zoslib_config(&config);
  init_zoslib(config);
}

}  // namespace

namespace v8 {
namespace base {

void OS::Free(void* address, const size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % AllocatePageSize());
  DCHECK_EQ(0, size % AllocatePageSize());
  CHECK_EQ(0, __zfree(address, size));
}

void OS::Release(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  CHECK_EQ(0, __zfree(address, size));
}

void* OS::Allocate(void* hint, size_t size, size_t alignment,
                   MemoryPermission access) {
  return __zalloc(size, alignment);
}

class ZOSTimezoneCache : public PosixTimezoneCache {
  const char* LocalTimezone(double time) override;
  double LocalTimeOffset(double time_ms, bool is_utc) override;
  ~ZOSTimezoneCache() override {}
};

const char* ZOSTimezoneCache::LocalTimezone(double time) {
  if (isnan(time)) return "";
  time_t tv = static_cast<time_t>(std::floor(time / msPerSecond));
  struct tm tm;
  struct tm* t = localtime_r(&tv, &tm);
  if (t == nullptr) return "";
  return tzname[0];
}

double ZOSTimezoneCache::LocalTimeOffset(double time_ms, bool is_utc) {
  time_t tv = time(nullptr);
  struct tm tmv;
  struct tm* gmt = gmtime_r(&tv, &tmv);
  double gm_secs = gmt->tm_sec + (gmt->tm_min * 60) + (gmt->tm_hour * 3600);
  struct tm* localt = localtime_r(&tv, &tmv);
  double local_secs =
      localt->tm_sec + (localt->tm_min * 60) + (localt->tm_hour * 3600);
  return (local_secs - gm_secs) * msPerSecond -
         (localt->tm_isdst > 0 ? 3600 * msPerSecond : 0);
}

TimezoneCache* OS::CreateTimezoneCache() { return new ZOSTimezoneCache(); }

// static
void* OS::AllocateShared(void* hint, size_t size, MemoryPermission access,
                         PlatformSharedMemoryHandle handle, uint64_t offset) {
  DCHECK_EQ(0, size % AllocatePageSize());
  int prot = GetProtectionFromMemoryPermission(access);
  int fd = FileDescriptorFromSharedMemoryHandle(handle);
  return mmap(hint, size, prot, MAP_SHARED, fd, offset);
}

// static
void OS::FreeShared(void* address, size_t size) {
  DCHECK_EQ(0, size % AllocatePageSize());
  CHECK_EQ(0, munmap(address, size));
}

bool AddressSpaceReservation::AllocateShared(void* address, size_t size,
                                             OS::MemoryPermission access,
                                             PlatformSharedMemoryHandle handle,
                                             uint64_t offset) {
  DCHECK(Contains(address, size));
  int prot = GetProtectionFromMemoryPermission(access);
  int fd = FileDescriptorFromSharedMemoryHandle(handle);
  return mmap(address, size, prot, MAP_SHARED | MAP_FIXED, fd, offset) !=
         MAP_FAILED;
}

bool AddressSpaceReservation::FreeShared(void* address, size_t size) {
  DCHECK(Contains(address, size));
  return mmap(address, size, PROT_NONE, MAP_FIXED | MAP_PRIVATE, -1, 0) !=
         MAP_FAILED;
}

std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  std::vector<SharedLibraryAddress> result;
  return result;
}

void OS::SignalCodeMovingGC() {}

void OS::AdjustSchedulingParams() {}

// static
bool OS::SetPermissions(void* address, size_t size, MemoryPermission access) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  return true;
}

void OS::SetDataReadOnly(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
}

// static
bool OS::RecommitPages(void* address, size_t size, MemoryPermission access) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  return SetPermissions(address, size, access);
}

// static
bool OS::DiscardSystemPages(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  return true;
}

// static
bool OS::DecommitPages(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  return true;
}

// static
bool OS::HasLazyCommits() { return false; }

class PosixMemoryMappedFile final : public OS::MemoryMappedFile {
 public:
  PosixMemoryMappedFile(FILE* file, void* memory, size_t size, bool ismmap)
      : file_(file), memory_(memory), size_(size), ismmap_(ismmap) {}
  ~PosixMemoryMappedFile() final;
  void* memory() const final { return memory_; }
  size_t size() const final { return size_; }

 private:
  FILE* const file_;
  void* const memory_;
  size_t const size_;
  bool ismmap_;
};

// static
OS::MemoryMappedFile* OS::MemoryMappedFile::open(const char* name,
                                                 FileMode mode) {
  const char* fopen_mode = (mode == FileMode::kReadOnly) ? "r" : "r+";
  int open_mode = (mode == FileMode::kReadOnly) ? O_RDONLY : O_RDWR;
  // use open() instead of fopen() to prevent auto-conversion
  // (which doesn't support untagged file with ASCII content)
  void* memory = nullptr;
  if (int fd = ::open(name, open_mode)) {
    FILE* file = fdopen(fd, fopen_mode);  // for PosixMemoryMappedFile()
    off_t size = lseek(fd, 0, SEEK_END);
    if (size == 0) return new PosixMemoryMappedFile(file, nullptr, 0, false);
    bool ismmap;
    if (size > 0) {
      int prot = PROT_READ;
      int flags = MAP_PRIVATE;
      if (mode == FileMode::kReadWrite) {
        prot |= PROT_WRITE;
        flags = MAP_SHARED;
        memory = mmap(OS::GetRandomMmapAddr(), size, prot, flags, fd, 0);
        ismmap = true;
      } else {
        memory = __zalloc_for_fd(size, name, fd, 0);
        ismmap = false;
      }
      if (memory != MAP_FAILED)
        return new PosixMemoryMappedFile(file, memory, size, ismmap);
    } else {
      perror("lseek");
    }
    fclose(file);  // also closes fd
  }
  return nullptr;
}

// static
OS::MemoryMappedFile* OS::MemoryMappedFile::create(const char* name,
                                                   size_t size, void* initial) {
  if (FILE* file = fopen(name, "w+")) {
    if (size == 0) return new PosixMemoryMappedFile(file, 0, 0, false);
    size_t result = fwrite(initial, 1, size, file);
    if (result == size && !ferror(file)) {
      void* memory = mmap(OS::GetRandomMmapAddr(), result,
                          PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), 0);
      if (memory != MAP_FAILED) {
        return new PosixMemoryMappedFile(file, memory, result, true);
      }
    }
    fclose(file);
  }
  return nullptr;
}

PosixMemoryMappedFile::~PosixMemoryMappedFile() {
  if (memory_ != nullptr) {
    if (ismmap_)
      munmap(memory_, RoundUp(size_, OS::AllocatePageSize()));
    else
      __zfree(memory_, size_);
  }
  fclose(file_);
}

}  // namespace base
}  // namespace v8
