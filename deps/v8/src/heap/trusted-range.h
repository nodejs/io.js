// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_TRUSTED_RANGE_H_
#define V8_HEAP_TRUSTED_RANGE_H_

#include "src/common/globals.h"
#include "src/utils/allocation.h"
#include "v8-internal.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_SANDBOX

// When the sandbox is enabled, the heap's trusted spaces are located outside
// of the sandbox so that an attacker cannot corrupt their contents. This
// special virtual memory cage hosts them. It also acts as a pointer
// compression cage inside of which compressed pointers can be used to
// reference objects.
class TrustedRange final : public VirtualMemoryCage {
 public:
  bool InitReservation(size_t requested);

  // Initializes the process-wide TrustedRange if it hasn't been initialized
  // yet. Returns the (initialized) TrustedRange or terminates the process if
  // the virtual memory cannot be reserved.
  static TrustedRange* EnsureProcessWideTrustedRange(size_t requested_size);

  // Returns the process-wide TrustedRange if it has been initialized (via
  // EnsureProcessWideTrustedRange), otherwise nullptr.
  V8_EXPORT_PRIVATE static TrustedRange* GetProcessWideTrustedRange();
};

#endif  // V8_ENABLE_SANDBOX

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_TRUSTED_RANGE_H_
