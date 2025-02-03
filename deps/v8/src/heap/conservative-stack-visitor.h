// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CONSERVATIVE_STACK_VISITOR_H_
#define V8_HEAP_CONSERVATIVE_STACK_VISITOR_H_

#include "include/v8-internal.h"
#include "src/base/address-region.h"
#include "src/common/globals.h"
#include "src/heap/base/stack.h"

namespace v8 {
namespace internal {

class MemoryAllocator;
class RootVisitor;

class V8_EXPORT_PRIVATE ConservativeStackVisitor
    : public ::heap::base::StackVisitor {
 public:
  ConservativeStackVisitor(Isolate* isolate, RootVisitor* delegate);

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

  static ConservativeStackVisitor ForTesting(Isolate* isolate,
                                             GarbageCollector collector) {
    return ConservativeStackVisitor(isolate, nullptr, collector);
  }

 private:
  ConservativeStackVisitor(Isolate* isolate, RootVisitor* delegate,
                           GarbageCollector collector);

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

  RootVisitor* const delegate_;
  MemoryAllocator* const allocator_;
  const GarbageCollector collector_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CONSERVATIVE_STACK_VISITOR_H_
