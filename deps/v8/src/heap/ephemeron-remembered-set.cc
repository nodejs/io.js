// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/ephemeron-remembered-set.h"

#include "src/heap/heap-inl.h"
#include "src/heap/heap-layout-inl.h"

namespace v8::internal {

void EphemeronRememberedSet::RecordEphemeronKeyWrite(
    Tagged<EphemeronHashTable> table, Address slot) {
  DCHECK(HeapLayout::InYoungGeneration(HeapObjectSlot(slot).ToHeapObject()));
  int slot_index = EphemeronHashTable::SlotToIndex(table.address(), slot);
  InternalIndex entry = EphemeronHashTable::IndexToEntry(slot_index);
  base::SpinningMutexGuard guard(&insertion_mutex_);
  auto it = tables_.insert({table, IndicesSet()});
  it.first->second.insert(entry.as_int());
}

void EphemeronRememberedSet::RecordEphemeronKeyWrites(
    Tagged<EphemeronHashTable> table, IndicesSet indices) {
  base::SpinningMutexGuard guard(&insertion_mutex_);
  auto it = tables_.find(table);
  if (it != tables_.end()) {
    it->second.merge(std::move(indices));
  } else {
    tables_.insert({table, std::move(indices)});
  }
}

}  // namespace v8::internal
