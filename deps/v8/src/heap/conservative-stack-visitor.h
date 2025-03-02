// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CONSERVATIVE_STACK_VISITOR_H_
#define V8_HEAP_CONSERVATIVE_STACK_VISITOR_H_

#include "include/v8-internal.h"
#include "src/base/address-region.h"
#include "src/common/globals.h"
#include "src/heap/base/stack.h"
#include "src/heap/marking.h"
#include "src/heap/memory-chunk.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

class MemoryAllocator;
class RootVisitor;

// `ConcreteVisitor` must implement the following methods:
// 1) FilterPage(const MemoryChunk*) - returns true if a page may contain
//    interesting objects for the GC (e.g. a young page in a young gen GC).
// 2) FilterLargeObject(Tagged<HeapObject>, MapWord) - returns true if the
//    object should be handled by conservative stack scanning.
// 3) FilterNormalObject(Tagged<HeapObject>, MapWord, MarkingBitmap*) - returns
// true if the
//    object should be handled by conservative stack scanning.
// 4) HandleObjectFound(Tagged<HeapObject>, size_t, MarkingBitmap*) - Callback
// whenever
//    FindBasePtr finds a new object.
// 5) OnlyScanMainV8Heap() - returns true if the visitor does not handle the
// external code and trusted spaces.
template <typename ConcreteVisitor>
class V8_EXPORT_PRIVATE ConservativeStackVisitorBase
    : public ::heap::base::StackVisitor {
 public:
  ConservativeStackVisitorBase(Isolate* isolate, RootVisitor* root_visitor);

  void VisitPointer(const void* pointer) final;

  // This method finds an object header based on a `maybe_inner_ptr`. It returns
  // `kNullAddress` if the parameter does not point to (the interior of) a valid
  // heap object. The allocator_ field is used to provide the set of valid heap
  // pages. The collector_ field is used to determine which kind of heap objects
  // we are interested in. For MARK_COMPACTOR all heap objects are considered,
  // whereas for young generation collectors we only consider objects in the
  // young generation.
  Address FindBasePtr(Address maybe_inner_ptr,
                      PtrComprCageBase cage_base) const;

 private:
  void VisitConservativelyIfPointer(Address address);
  void VisitConservativelyIfPointer(Address address,
                                    PtrComprCageBase cage_base);

#ifdef V8_COMPRESS_POINTERS
  bool IsInterestingCage(PtrComprCageBase cage_base) const;
#endif

  // The "interesting" cages where we conservatively scan pointers are:
  // - The regular cage for the V8 heap.
  // - The cage used for code objects, if an external code space is used.
  // - The trusted space cage.
  const PtrComprCageBase cage_base_;
#ifdef V8_EXTERNAL_CODE_SPACE
  const PtrComprCageBase code_cage_base_;
  base::AddressRegion code_address_region_;
#endif
#ifdef V8_ENABLE_SANDBOX
  const PtrComprCageBase trusted_cage_base_;
#endif

  RootVisitor* const root_visitor_;
  MemoryAllocator* const allocator_;
};

class V8_EXPORT_PRIVATE ConservativeStackVisitor
    : public ConservativeStackVisitorBase<ConservativeStackVisitor> {
 public:
  ConservativeStackVisitor(Isolate* isolate, RootVisitor* root_visitor)
      : ConservativeStackVisitorBase(isolate, root_visitor) {}

 private:
  static constexpr bool kOnlyVisitMainV8Cage = false;

  static bool FilterPage(const MemoryChunk* chunk) {
    return v8_flags.sticky_mark_bits || !chunk->IsFromPage();
  }
  static bool FilterLargeObject(Tagged<HeapObject>, MapWord) { return true; }
  static bool FilterNormalObject(Tagged<HeapObject>, MapWord, MarkingBitmap*) {
    return true;
  }
  static void HandleObjectFound(Tagged<HeapObject>, size_t, MarkingBitmap*) {}

  friend class ConservativeStackVisitorBase<ConservativeStackVisitor>;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CONSERVATIVE_STACK_VISITOR_H_
