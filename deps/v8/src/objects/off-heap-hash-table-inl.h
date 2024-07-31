// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OFF_HEAP_HASH_TABLE_INL_H_
#define V8_OBJECTS_OFF_HEAP_HASH_TABLE_INL_H_

#include "src/objects/compressed-slots-inl.h"
#include "src/objects/off-heap-hash-table.h"

namespace v8 {
namespace internal {

template <typename Derived>
OffHeapHashTableBase<Derived>::OffHeapHashTableBase(int capacity)
    : number_of_elements_(0),
      number_of_deleted_elements_(0),
      capacity_(capacity) {
  MemsetTagged(slot(InternalIndex(0)), empty_element(),
               capacity * Derived::kEntrySize);
}

template <typename Derived>
void OffHeapHashTableBase<Derived>::RehashInto(PtrComprCageBase cage_base,
                                               Derived* new_table) {
  DCHECK_LT(number_of_elements(), new_table->capacity());
  DCHECK(new_table->HasSufficientCapacityToAdd(number_of_elements()));

  Derived* derived_this = static_cast<Derived*>(this);
  // Rehash the elements and copy them into new_table.
  for (InternalIndex i : InternalIndex::Range(capacity())) {
    Tagged<Object> key = derived_this->GetKey(cage_base, i);
    if (!IsKey(key)) continue;
    uint32_t hash = Derived::Hash(cage_base, key);
    InternalIndex insertion_index =
        new_table->FindInsertionEntry(cage_base, hash);
    new_table->SetKey(insertion_index, key);
    derived_this->CopyEntryExcludingKeyInto(cage_base, i, new_table,
                                            insertion_index);
  }
  new_table->number_of_elements_ = number_of_elements();
}

template <typename Derived>
inline bool OffHeapHashTableBase<Derived>::ShouldResizeToAdd(
    int additional_elements, int* new_capacity) {
  DCHECK_NOT_NULL(new_capacity);
  // Grow or shrink table if needed. We first try to shrink the table, if it
  // is sufficiently empty; otherwise we make sure to grow it so that it has
  // enough space.
  int capacity_after_shrinking = ComputeCapacityWithShrink(
      capacity_, number_of_elements_ + additional_elements);

  if (capacity_after_shrinking < capacity_) {
    DCHECK(HasSufficientCapacityToAdd(
        capacity_after_shrinking, number_of_elements_, 0, additional_elements));
    *new_capacity = capacity_after_shrinking;
    return true;
  } else if (!HasSufficientCapacityToAdd(additional_elements)) {
    *new_capacity = ComputeCapacity(number_of_elements_ + additional_elements);
    return true;
  } else {
    *new_capacity = -1;
    return false;
  }
}

// static
template <typename Derived>
bool OffHeapHashTableBase<Derived>::HasSufficientCapacityToAdd(
    int capacity, int number_of_elements, int number_of_deleted_elements,
    int number_of_additional_elements) {
  int nof = number_of_elements + number_of_additional_elements;
  // Return true if:
  //   50% is still free after adding number_of_additional_elements elements and
  //   at most 50% of the free elements are deleted elements.
  if ((nof < capacity) &&
      ((number_of_deleted_elements <= (capacity - nof) / 2))) {
    int needed_free = nof / 2;
    if (nof + needed_free <= capacity) return true;
  }
  return false;
}

// static
template <typename Derived>
int OffHeapHashTableBase<Derived>::ComputeCapacity(int at_least_space_for) {
  // Add 50% slack to make slot collisions sufficiently unlikely.
  // See matching computation in HasSufficientCapacityToAdd().
  int raw_capacity = at_least_space_for + (at_least_space_for >> 1);
  int capacity = base::bits::RoundUpToPowerOfTwo32(raw_capacity);
  return std::max(capacity, Derived::kMinCapacity);
}

// static
template <typename Derived>
int OffHeapHashTableBase<Derived>::ComputeCapacityWithShrink(
    int current_capacity, int at_least_space_for) {
  // Only shrink if the table is very empty to avoid performance penalty.
  DCHECK_GE(current_capacity, Derived::kMinCapacity);
  if (at_least_space_for > (current_capacity / Derived::kMaxEmptyFactor)) {
    return current_capacity;
  }

  // Recalculate the smaller capacity actually needed.
  int new_capacity = ComputeCapacity(at_least_space_for);
  DCHECK_GE(new_capacity, at_least_space_for);
  // Don't go lower than room for {kMinCapacity} elements.
  if (new_capacity < Derived::kMinCapacity) return current_capacity;
  return new_capacity;
}

template <typename Derived>
void OffHeapHashTableBase<Derived>::IterateElements(Root root,
                                                    RootVisitor* visitor) {
  OffHeapObjectSlot first_slot = slot(InternalIndex(0));
  OffHeapObjectSlot end_slot = slot(InternalIndex(capacity_));
  visitor->VisitRootPointers(root, nullptr, first_slot, end_slot);
}

template <typename Derived>
template <typename IsolateT, typename FindKey>
InternalIndex OffHeapHashTableBase<Derived>::FindEntry(IsolateT* isolate,
                                                       FindKey key,
                                                       uint32_t hash) const {
  const Derived* derived_this = static_cast<const Derived*>(this);
  uint32_t count = 1;
  for (InternalIndex entry = FirstProbe(hash, capacity_);;
       entry = NextProbe(entry, count++, capacity_)) {
    // TODO(leszeks): Consider delaying the decompression until after the
    // comparisons against empty/deleted.
    Tagged<Object> element = derived_this->GetKey(isolate, entry);
    if (element == empty_element()) return InternalIndex::NotFound();
    if (element == deleted_element()) continue;
    if (Derived::KeyIsMatch(isolate, key, element)) return entry;
  }
}

template <typename Derived>
InternalIndex OffHeapHashTableBase<Derived>::FindInsertionEntry(
    PtrComprCageBase cage_base, uint32_t hash) const {
  // The derived class must guarantee the hash table is never full.
  DCHECK(HasSufficientCapacityToAdd(1));
  const Derived* derived_this = static_cast<const Derived*>(this);
  uint32_t count = 1;
  for (InternalIndex entry = FirstProbe(hash, capacity_);;
       entry = NextProbe(entry, count++, capacity_)) {
    // TODO(leszeks): Consider delaying the decompression until after the
    // comparisons against empty/deleted.
    Tagged<Object> element = derived_this->GetKey(cage_base, entry);
    if (!IsKey(element)) return entry;
  }
}

template <typename Derived>
template <typename IsolateT, typename FindKey>
InternalIndex OffHeapHashTableBase<Derived>::FindEntryOrInsertionEntry(
    IsolateT* isolate, FindKey key, uint32_t hash) const {
  // The derived class must guarantee the hash table is never full.
  DCHECK(HasSufficientCapacityToAdd(1));
  const Derived* derived_this = static_cast<const Derived*>(this);
  InternalIndex insertion_entry = InternalIndex::NotFound();
  uint32_t count = 1;
  for (InternalIndex entry = FirstProbe(hash, capacity_);;
       entry = NextProbe(entry, count++, capacity_)) {
    // TODO(leszeks): Consider delaying the decompression until after the
    // comparisons against empty/deleted.
    Tagged<Object> element = derived_this->GetKey(isolate, entry);
    if (element == empty_element()) {
      // Empty entry, it's our insertion entry if there was no previous Hole.
      if (insertion_entry.is_not_found()) return entry;
      return insertion_entry;
    }

    if (element == deleted_element()) {
      // Holes are potential insertion candidates, but we continue the search
      // in case we find the actual matching entry.
      if (insertion_entry.is_not_found()) insertion_entry = entry;
      continue;
    }

    if (Derived::KeyIsMatch(isolate, key, element)) return entry;
  }
}

// static
template <typename Derived>
template <typename Container, size_t OffsetOfElementsInContainer>
void* OffHeapHashTableBase<Derived>::Allocate(int capacity) {
  // Make sure that the elements_ array is at the end of Container, with no
  // padding, so that subsequent elements can be accessed as offsets from
  // elements_.
  static_assert(OffsetOfElementsInContainer ==
                sizeof(Container) - sizeof(Tagged_t));
  // Make sure that elements_ is aligned when Container is aligned.
  static_assert(OffsetOfElementsInContainer % kTaggedSize == 0);

  return AlignedAllocWithRetry(
      sizeof(Container) + GetSizeExcludingHeader(capacity),
      std::max(alignof(Container), alignof(void*)));
}

// static
template <typename Derived>
void OffHeapHashTableBase<Derived>::Free(void* table) {
  AlignedFree(table);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_OFF_HEAP_HASH_TABLE_INL_H_
