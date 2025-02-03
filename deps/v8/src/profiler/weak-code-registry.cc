// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/weak-code-registry.h"

#include "src/handles/global-handles-inl.h"
#include "src/objects/code-inl.h"
#include "src/objects/instance-type-inl.h"

namespace v8 {
namespace internal {

namespace {

void Untrack(CodeEntry* entry) {
  if (Address** heap_object_location_address =
          entry->heap_object_location_address()) {
    GlobalHandles::Destroy(*heap_object_location_address);
    *heap_object_location_address = nullptr;
  }
}

}  // namespace

void WeakCodeRegistry::Track(CodeEntry* entry,
                             DirectHandle<AbstractCode> code) {
  DCHECK(!*entry->heap_object_location_address());
  DisallowGarbageCollection no_gc;
  Handle<AbstractCode> handle = isolate_->global_handles()->Create(*code);

  Address** heap_object_location_address =
      entry->heap_object_location_address();
  *heap_object_location_address = handle.location();
  GlobalHandles::MakeWeak(heap_object_location_address);

  entries_.push_back(entry);
}

void WeakCodeRegistry::Sweep(WeakCodeRegistry::Listener* listener) {
  std::vector<CodeEntry*> alive_entries;
  for (CodeEntry* entry : entries_) {
    // Mark the CodeEntry as being deleted on the heap if the heap object
    // location was nulled, indicating the object was freed.
    if (!*entry->heap_object_location_address()) {
      if (listener) {
        listener->OnHeapObjectDeletion(entry);
      }
    } else {
      alive_entries.push_back(entry);
    }
  }
  entries_ = std::move(alive_entries);
}

void WeakCodeRegistry::Clear() {
  for (CodeEntry* entry : entries_) {
    Untrack(entry);
  }
  entries_.clear();
}

}  // namespace internal
}  // namespace v8
