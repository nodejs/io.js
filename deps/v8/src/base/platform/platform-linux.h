// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_PLATFORM_LINUX_H_
#define V8_BASE_PLATFORM_PLATFORM_LINUX_H_

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <string>

#include "src/base/base-export.h"
#include "src/base/platform/platform.h"

namespace v8 {
namespace base {

// Represents a memory region, as parsed from /proc/PID/maps.
// Visible for testing.
struct V8_BASE_EXPORT MemoryRegion {
  uintptr_t start;
  uintptr_t end;
  char permissions[5];
  off_t offset;
  dev_t dev;
  ino_t inode;
  std::string pathname;

  // |line| must not contains the tail '\n'.
  static std::optional<MemoryRegion> FromMapsLine(const char* line);
};

// The |fp| parameter is for testing, to pass a fake /proc/self/maps file.
V8_BASE_EXPORT std::vector<OS::SharedLibraryAddress> GetSharedLibraryAddresses(
    FILE* fp);

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_PLATFORM_LINUX_H_
