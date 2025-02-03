// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_EVACUATION_ALLOCATOR_H_
#define V8_HEAP_EVACUATION_ALLOCATOR_H_

#include <optional>

#include "src/common/globals.h"
#include "src/heap/heap.h"
#include "src/heap/new-spaces.h"
#include "src/heap/paged-spaces.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

// Allocator encapsulating thread-local allocation durning collection. Assumes
// that all other allocations also go through EvacuationAllocator.
class EvacuationAllocator {
 public:
  EvacuationAllocator(Heap* heap, CompactionSpaceKind compaction_space_kind);

  // Needs to be called from the main thread to finalize this
  // EvacuationAllocator.
  void Finalize();

  inline AllocationResult Allocate(AllocationSpace space, int object_size,
                                   AllocationAlignment alignment);
  void FreeLast(AllocationSpace space, Tagged<HeapObject> object,
                int object_size);

 private:
  void FreeLastInMainAllocator(MainAllocator* allocator,
                               Tagged<HeapObject> object, int object_size);

  MainAllocator* new_space_allocator() { return &new_space_allocator_.value(); }
  MainAllocator* old_space_allocator() { return &old_space_allocator_.value(); }
  MainAllocator* code_space_allocator() {
    return &code_space_allocator_.value();
  }
  MainAllocator* shared_space_allocator() {
    return &shared_space_allocator_.value();
  }
  MainAllocator* trusted_space_allocator() {
    return &trusted_space_allocator_.value();
  }

  Heap* const heap_;
  NewSpace* const new_space_;
  CompactionSpaceCollection compaction_spaces_;
  std::optional<MainAllocator> new_space_allocator_;
  std::optional<MainAllocator> old_space_allocator_;
  std::optional<MainAllocator> code_space_allocator_;
  std::optional<MainAllocator> shared_space_allocator_;
  std::optional<MainAllocator> trusted_space_allocator_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_EVACUATION_ALLOCATOR_H_
