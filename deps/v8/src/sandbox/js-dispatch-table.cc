// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/js-dispatch-table.h"

#include "src/common/code-memory-access-inl.h"
#include "src/execution/isolate.h"
#include "src/logging/counters.h"
#include "src/objects/code-inl.h"
#include "src/sandbox/js-dispatch-table-inl.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

Tagged<Code> JSDispatchTable::GetCode(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  Address ptr = at(index).GetCodePointer();
  DCHECK(Internals::HasHeapObjectTag(ptr));
  return Cast<Code>(Tagged<Object>(ptr));
}

void JSDispatchTable::SetCode(JSDispatchHandle handle, Tagged<Code> new_code) {
  // The new code must use JS linkage and its parameter count must match that
  // of the entry, unless the code does not assume a particular parameter count
  // (so uses the kDontAdaptArgumentsSentinel).
  CHECK_EQ(new_code->entrypoint_tag(), kJSEntrypointTag);
  CHECK(new_code->parameter_count() == kDontAdaptArgumentsSentinel ||
        new_code->parameter_count() == GetParameterCount(handle));

  // The object should be in old space to avoid creating old-to-new references.
  DCHECK(!Heap::InYoungGeneration(new_code));

  uint32_t index = HandleToIndex(handle);
  Address new_entrypoint = new_code->instruction_start();
  CFIMetadataWriteScope write_scope("JSDispatchTable update");
  at(index).SetCodeAndEntrypointPointer(new_code.ptr(), new_entrypoint);
}

JSDispatchHandle JSDispatchTable::AllocateAndInitializeEntry(
    Space* space, uint16_t parameter_count) {
  DCHECK(space->BelongsTo(this));
  uint32_t index = AllocateEntry(space);
  CFIMetadataWriteScope write_scope("JSDispatchTable initialize");
  at(index).MakeJSDispatchEntry(kNullAddress, kNullAddress, parameter_count,
                                space->allocate_black());
  return IndexToHandle(index);
}

bool JSDispatchTable::HasCode(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  Address ptr = at(index).GetCodePointer();
  return ptr != kTaggedNullAddress;
}

uint32_t JSDispatchTable::Sweep(Space* space, Counters* counters) {
  uint32_t num_live_entries = GenericSweep(space);
  // TODO(saelo): once we actually store HeapObject pointers in table entries,
  // we also need to update the pointers after compaction. See
  // MarkCompactCollector::UpdatePointersInPointerTables().
  counters->js_dispatch_table_entries_count()->AddSample(num_live_entries);
  return num_live_entries;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX
