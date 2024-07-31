// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_PLATFORM_POSIX_H_
#define V8_BASE_PLATFORM_PLATFORM_POSIX_H_

#include "include/v8config.h"
#include "src/base/platform/platform.h"
#include "src/base/timezone-cache.h"

namespace v8 {
namespace base {

void PosixInitializeCommon(AbortMode abort_mode,
                           const char* const gc_fake_mmap);

class PosixTimezoneCache : public TimezoneCache {
 public:
  double DaylightSavingsOffset(double time_ms) override;
  void Clear(TimeZoneDetection) override {}
  ~PosixTimezoneCache() override = default;

 protected:
  static const int msPerSecond = 1000;
};

#if !V8_OS_FUCHSIA
int GetProtectionFromMemoryPermission(OS::MemoryPermission access);
#endif

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_PLATFORM_POSIX_H_
