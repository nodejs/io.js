// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/code-pointer-table.h"

#include "src/execution/isolate.h"
#include "src/logging/counters.h"
#include "src/sandbox/code-pointer-table-inl.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

uint32_t CodePointerTable::Sweep(Space* space, Counters* counters) {
  uint32_t num_live_entries = GenericSweep(space);
  counters->code_pointers_count()->AddSample(num_live_entries);
  return num_live_entries;
}

DEFINE_LAZY_LEAKY_OBJECT_GETTER(CodePointerTable,
                                GetProcessWideCodePointerTable)

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS
