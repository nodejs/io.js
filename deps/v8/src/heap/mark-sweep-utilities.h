// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARK_SWEEP_UTILITIES_H_
#define V8_HEAP_MARK_SWEEP_UTILITIES_H_

#include <memory>
#include <vector>

#include "src/common/globals.h"
#include "src/heap/heap.h"
#include "src/heap/marking-state.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/spaces.h"
#include "src/objects/string-forwarding-table.h"
#include "src/objects/visitors.h"

namespace v8 {
namespace internal {

#ifdef VERIFY_HEAP
class MarkingVerifierBase : public ObjectVisitorWithCageBases,
                            public RootVisitor {
 public:
  virtual void Run() = 0;

 protected:
  explicit MarkingVerifierBase(Heap* heap);

  virtual const MarkingBitmap* bitmap(const MutablePageMetadata* chunk) = 0;

  virtual void VerifyMap(Tagged<Map> map) = 0;
  virtual void VerifyPointers(ObjectSlot start, ObjectSlot end) = 0;
  virtual void VerifyPointers(MaybeObjectSlot start, MaybeObjectSlot end) = 0;
  virtual void VerifyCodePointer(InstructionStreamSlot slot) = 0;
  virtual void VerifyRootPointers(FullObjectSlot start, FullObjectSlot end) = 0;

  virtual bool IsMarked(Tagged<HeapObject> object) = 0;

  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) override {
    VerifyPointers(start, end);
  }

  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) override {
    VerifyPointers(start, end);
  }

  void VisitInstructionStreamPointer(Tagged<Code> host,
                                     InstructionStreamSlot slot) override {
    VerifyCodePointer(slot);
  }

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    VerifyRootPointers(start, end);
  }

  void VisitMapPointer(Tagged<HeapObject> object) override;

  void VerifyRoots();
  void VerifyMarkingOnPage(const PageMetadata* page, Address start,
                           Address end);
  void VerifyMarking(NewSpace* new_space);
  void VerifyMarking(PagedSpaceBase* paged_space);
  void VerifyMarking(LargeObjectSpace* lo_space);

  Heap* heap_;
};
#endif  // VERIFY_HEAP

enum class ExternalStringTableCleaningMode { kAll, kYoungOnly };
template <ExternalStringTableCleaningMode mode>
class ExternalStringTableCleanerVisitor final : public RootVisitor {
 public:
  explicit ExternalStringTableCleanerVisitor(Heap* heap) : heap_(heap) {}

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) final;

 private:
  Heap* heap_;
};

class StringForwardingTableCleanerBase {
 protected:
  explicit StringForwardingTableCleanerBase(Heap* heap);

  // Dispose external resource, if it wasn't disposed already.
  // We can have multiple entries of the same external resource in the string
  // forwarding table (i.e. concurrent externalization of a string with the
  // same resource), therefore we keep track of already disposed resources to
  // not dispose a resource more than once.
  void DisposeExternalResource(StringForwardingTable::Record* record);

  Isolate* const isolate_;
  NonAtomicMarkingState* const marking_state_;
  std::unordered_set<Address> disposed_resources_;
};

bool IsCppHeapMarkingFinished(Heap* heap,
                              MarkingWorklists::Local* local_marking_worklists);

#if DEBUG
void VerifyRememberedSetsAfterEvacuation(Heap* heap,
                                         GarbageCollector garbage_collector);
#endif  // DEBUG

template class ExternalStringTableCleanerVisitor<
    ExternalStringTableCleaningMode::kAll>;
template class ExternalStringTableCleanerVisitor<
    ExternalStringTableCleaningMode::kYoungOnly>;

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARK_SWEEP_UTILITIES_H_
