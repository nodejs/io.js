// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Platform-specific code for FreeBSD goes here. For the POSIX-compatible
// parts, the implementation is in platform-posix.cc.

#include <pthread.h>
#include <pthread_np.h>
#include <semaphore.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <sys/user.h>

#include <sys/fcntl.h>  // open
#include <sys/mman.h>   // mmap & munmap
#include <sys/stat.h>   // open
#include <sys/sysctl.h>
#include <unistd.h>     // getpagesize
// If you don't have execinfo.h then you need devel/libexecinfo from ports.
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <strings.h>    // index

#include <cmath>

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

static unsigned StringToLong(char* buffer) {
  return static_cast<unsigned>(strtol(buffer, nullptr, 16));
}

std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  std::vector<SharedLibraryAddress> result;
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, getpid()};
  size_t miblen = sizeof(mib) / sizeof(mib[0]);
  size_t buffer_size;
  if (sysctl(mib, miblen, nullptr, &buffer_size, nullptr, 0) == 0) {
    // Overallocate the buffer by 1/3 to account for concurrent
    // kinfo_vmentry change. 1/3 is an arbitrary constant that
    // works in practice.
    buffer_size = buffer_size * 4 / 3;
    std::vector<char> buffer(buffer_size);
    int ret = sysctl(mib, miblen, buffer.data(), &buffer_size, nullptr, 0);

    if (ret == 0 || (ret == -1 && errno == ENOMEM)) {
      char* start = buffer.data();
      char* end = start + buffer_size;

      while (start < end) {
        struct kinfo_vmentry* map =
            reinterpret_cast<struct kinfo_vmentry*>(start);
        const size_t ssize = map->kve_structsize;
        char* path = map->kve_path;

        CHECK_NE(0, ssize);

        if ((map->kve_protection & KVME_PROT_READ) != 0 &&
            (map->kve_protection & KVME_PROT_EXEC) != 0 && path[0] != '\0') {
          char* sep = strrchr(path, '/');
          std::string lib_name;
          if (sep != nullptr) {
            lib_name = std::string(++sep);
          } else {
            lib_name = std::string(path);
          }
          result.push_back(SharedLibraryAddress(
              lib_name, reinterpret_cast<uintptr_t>(map->kve_start),
              reinterpret_cast<uintptr_t>(map->kve_end)));
        }

        start += ssize;
      }
    }
  }
  return result;
}

void OS::SignalCodeMovingGC() {}

void OS::AdjustSchedulingParams() {}

std::optional<OS::MemoryRange> OS::GetFirstFreeMemoryRangeWithin(
    OS::Address boundary_start, OS::Address boundary_end, size_t minimum_size,
    size_t alignment) {
  return std::nullopt;
}

// static
Stack::StackSlot Stack::ObtainCurrentThreadStackStart() {
  pthread_attr_t attr;
  int error;
  pthread_attr_init(&attr);
  error = pthread_attr_get_np(pthread_self(), &attr);
  if (!error) {
    void* base;
    size_t size;
    error = pthread_attr_getstack(&attr, &base, &size);
    CHECK(!error);
    pthread_attr_destroy(&attr);
    return reinterpret_cast<uint8_t*>(base) + size;
  }
  pthread_attr_destroy(&attr);
  return nullptr;
}

}  // namespace base
}  // namespace v8
