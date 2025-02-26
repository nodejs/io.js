// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/heap-visitor.h"

#include "src/heap/heap-inl.h"
#include "src/heap/heap-visitor-inl.h"
#include "src/heap/mark-compact-inl.h"
#include "src/objects/js-weak-refs.h"

namespace v8 {
namespace internal {

// We don't record weak slots during marking or scavenges. Instead we do it
// once when we complete mark-compact cycle.  Note that write barrier has no
// effect if we are already in the middle of compacting mark-sweep cycle and we
// have to record slots manually.
static bool MustRecordSlots(Heap* heap) {
  return heap->gc_state() == Heap::MARK_COMPACT &&
         heap->mark_compact_collector()->is_compacting();
}

template <class T>
struct WeakListVisitor;

template <class T>
Tagged<Object> VisitWeakList(Heap* heap, Tagged<Object> list,
                             WeakObjectRetainer* retainer) {
  Tagged<HeapObject> undefined = ReadOnlyRoots(heap).undefined_value();
  Tagged<Object> head = undefined;
  Tagged<T> tail;
  bool record_slots = MustRecordSlots(heap);

  while (list != undefined) {
    // Check whether to keep the candidate in the list.
    Tagged<T> candidate = Cast<T>(list);

    Tagged<Object> retained = retainer->RetainAs(list);

    // Move to the next element before the WeakNext is cleared.
    list = WeakListVisitor<T>::WeakNext(candidate);

    if (retained != Tagged<Object>()) {
      if (head == undefined) {
        // First element in the list.
        head = retained;
      } else {
        // Subsequent elements in the list.
        DCHECK(!tail.is_null());
        WeakListVisitor<T>::SetWeakNext(tail, Cast<HeapObject>(retained));
        if (record_slots) {
          Tagged<HeapObject> slot_holder =
              WeakListVisitor<T>::WeakNextHolder(tail);
          int slot_offset = WeakListVisitor<T>::WeakNextOffset();
          ObjectSlot slot = slot_holder->RawField(slot_offset);
          MarkCompactCollector::RecordSlot(slot_holder, slot,
                                           Cast<HeapObject>(retained));
        }
      }
      // Retained object is new tail.
      DCHECK(!IsUndefined(retained, heap->isolate()));
      candidate = Cast<T>(retained);
      tail = candidate;

      // tail is a live object, visit it.
      WeakListVisitor<T>::VisitLiveObject(heap, tail, retainer);

    } else {
      WeakListVisitor<T>::VisitPhantomObject(heap, candidate);
    }
  }

  // Terminate the list if there is one or more elements.
  if (!tail.is_null()) WeakListVisitor<T>::SetWeakNext(tail, undefined);
  return head;
}

template <class T>
static void ClearWeakList(Heap* heap, Tagged<Object> list) {
  Tagged<Object> undefined = ReadOnlyRoots(heap).undefined_value();
  while (list != undefined) {
    Tagged<T> candidate = Cast<T>(list);
    list = WeakListVisitor<T>::WeakNext(candidate);
    WeakListVisitor<T>::SetWeakNext(candidate, undefined);
  }
}

template <>
struct WeakListVisitor<Context> {
  static void SetWeakNext(Tagged<Context> context, Tagged<HeapObject> next) {
    context->set(Context::NEXT_CONTEXT_LINK, next, UPDATE_WRITE_BARRIER);
  }

  static Tagged<Object> WeakNext(Tagged<Context> context) {
    return context->next_context_link();
  }

  static Tagged<HeapObject> WeakNextHolder(Tagged<Context> context) {
    return context;
  }

  static int WeakNextOffset() {
    return FixedArray::SizeFor(Context::NEXT_CONTEXT_LINK);
  }

  static void VisitLiveObject(Heap* heap, Tagged<Context> context,
                              WeakObjectRetainer* retainer) {
    if (heap->gc_state() == Heap::MARK_COMPACT) {
      // Record the slots of the weak entries in the native context.
      for (int idx = Context::FIRST_WEAK_SLOT;
           idx < Context::NATIVE_CONTEXT_SLOTS; ++idx) {
        ObjectSlot slot = context->RawField(Context::OffsetOfElementAt(idx));
        MarkCompactCollector::RecordSlot(context, slot,
                                         Cast<HeapObject>(*slot));
      }
    }
  }

  template <class T>
  static void DoWeakList(Heap* heap, Tagged<Context> context,
                         WeakObjectRetainer* retainer, int index) {
    // Visit the weak list, removing dead intermediate elements.
    Tagged<Object> list_head =
        VisitWeakList<T>(heap, context->get(index), retainer);

    // Update the list head.
    context->set(index, list_head, UPDATE_WRITE_BARRIER);

    if (MustRecordSlots(heap)) {
      // Record the updated slot if necessary.
      ObjectSlot head_slot = context->RawField(FixedArray::SizeFor(index));
      heap->mark_compact_collector()->RecordSlot(context, head_slot,
                                                 Cast<HeapObject>(list_head));
    }
  }

  static void VisitPhantomObject(Heap* heap, Tagged<Context> context) {}
};

template <>
struct WeakListVisitor<AllocationSite> {
  static void SetWeakNext(Tagged<AllocationSite> obj, Tagged<HeapObject> next) {
    obj->set_weak_next(next, UPDATE_WRITE_BARRIER);
  }

  static Tagged<Object> WeakNext(Tagged<AllocationSite> obj) {
    return obj->weak_next();
  }

  static Tagged<HeapObject> WeakNextHolder(Tagged<AllocationSite> obj) {
    return obj;
  }

  static int WeakNextOffset() { return AllocationSite::kWeakNextOffset; }

  static void VisitLiveObject(Heap*, Tagged<AllocationSite>,
                              WeakObjectRetainer*) {}

  static void VisitPhantomObject(Heap*, Tagged<AllocationSite>) {}
};

template <>
struct WeakListVisitor<JSFinalizationRegistry> {
  static void SetWeakNext(Tagged<JSFinalizationRegistry> obj,
                          Tagged<HeapObject> next) {
    obj->set_next_dirty(Cast<UnionOf<Undefined, JSFinalizationRegistry>>(next),
                        UPDATE_WRITE_BARRIER);
  }

  static Tagged<Object> WeakNext(Tagged<JSFinalizationRegistry> obj) {
    return obj->next_dirty();
  }

  static Tagged<HeapObject> WeakNextHolder(Tagged<JSFinalizationRegistry> obj) {
    return obj;
  }

  static int WeakNextOffset() {
    return JSFinalizationRegistry::kNextDirtyOffset;
  }

  static void VisitLiveObject(Heap* heap, Tagged<JSFinalizationRegistry> obj,
                              WeakObjectRetainer*) {
    heap->set_dirty_js_finalization_registries_list_tail(obj);
  }

  static void VisitPhantomObject(Heap*, Tagged<JSFinalizationRegistry>) {}
};

template Tagged<Object> VisitWeakList<Context>(Heap* heap, Tagged<Object> list,
                                               WeakObjectRetainer* retainer);

template Tagged<Object> VisitWeakList<AllocationSite>(
    Heap* heap, Tagged<Object> list, WeakObjectRetainer* retainer);

template Tagged<Object> VisitWeakList<JSFinalizationRegistry>(
    Heap* heap, Tagged<Object> list, WeakObjectRetainer* retainer);
}  // namespace internal
}  // namespace v8
