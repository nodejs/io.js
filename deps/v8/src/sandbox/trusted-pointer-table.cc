// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/trusted-pointer-table.h"

#include "src/execution/isolate.h"
#include "src/logging/counters.h"
#include "src/sandbox/trusted-pointer-table-inl.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

uint32_t TrustedPointerTable::Sweep(Space* space, Counters* counters) {
  uint32_t num_live_entries = GenericSweep(space);
  counters->trusted_pointers_count()->AddSample(num_live_entries);
  return num_live_entries;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX
