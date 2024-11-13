// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OFF_HEAP_HASH_TABLE_H_
#define V8_OBJECTS_OFF_HEAP_HASH_TABLE_H_

#include "src/common/globals.h"
#include "src/execution/isolate-utils.h"
#include "src/objects/compressed-slots.h"
#include "src/objects/slots.h"
#include "src/objects/smi.h"
#include "src/objects/visitors.h"
#include "src/roots/roots.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// A base class for building off-heap hash tables (e.g. the string table) that
// stores tagged values.
//
// It is a variable sized structure, with a "header" followed directly in memory
// by the elements themselves. These are accessed as offsets from the elements_
// field, which itself provides storage for the first element.
//
// The elements themselves are stored as an open-addressed hash table, with
// quadratic probing and Smi 0 and Smi 1 as the empty and deleted sentinels,
// respectively.
//
// It is a CRTP class whose derived class must provide the following
// definitions:
//
//  // The number of elements per table entry.
//  static constexpr int kEntrySize;
//
//  // The factor by which to decide if the table ought to shrink.
//  static constexpr int kMaxEmptyFactor;
//
//  // The minimum number of elements for new tables.
//  static constexpr int kMinCapacity;
//
//  // Computes the hash of a key {obj}.
//  static uint32_t Hash(Tagged<Object> obj);
//
//  // Returns whether the lookup key {key} matches the key element {obj}.
//  template <typename IsolateT, typename Key>
//  static bool KeyIsMatch(IsolateT* isolate, Key key, Tagged<Object> obj);
//
//  // Load the key object at entry {index}, decompressing it if needed.
//  Tagged<Object> GetKey(PtrComprCageBase, InternalIndex index);
//
//  // Store the key object at the entry {index}.
//  void SetKey(InternalIndex index, Tagged<Object> key);
//
//  // Store an entire entry at {index}. The arity of this function must be
//  // kEntrySize + 1.
//  void Set(InternalIndex index, Tagged<Object>...);
//
//  // Copy an entry in this table at {from_index} into the entry in {to} at
//  // {to_index}, exclusive of the key.
//  void CopyEntryExcludingKeyInto(PtrComprCageBase, InternalIndex from_index,
//                                 Derived* to, InternalIndex to_index);
//
template <typename Derived>
class OffHeapHashTableBase {
 public:
  static constexpr Tagged<Smi> empty_element() { return Smi::FromInt(0); }
  static constexpr Tagged<Smi> deleted_element() { return Smi::FromInt(1); }

  static bool IsKey(Tagged<Object> k) {
    return k != empty_element() && k != deleted_element();
  }

  int capacity() const { return capacity_; }
  int number_of_elements() const { return number_of_elements_; }
  int number_of_deleted_elements() const { return number_of_deleted_elements_; }

  OffHeapObjectSlot slot(InternalIndex index, int offset = 0) const {
    DCHECK_LT(offset, Derived::kEntrySize);
    return OffHeapObjectSlot(
        &elements_[index.as_uint32() * Derived::kEntrySize + offset]);
  }

  template <typename... Args>
  void AddAt(PtrComprCageBase cage_base, InternalIndex entry, Args&&... args) {
    Derived* derived_this = static_cast<Derived*>(this);

    DCHECK_EQ(derived_this->GetKey(cage_base, entry), empty_element());
    DCHECK_LT(number_of_elements_ + 1, capacity());
    DCHECK(HasSufficientCapacityToAdd(1));

    derived_this->Set(entry, std::forward<Args>(args)...);
    number_of_elements_++;
  }

  template <typename... Args>
  void OverwriteDeletedAt(PtrComprCageBase cage_base, InternalIndex entry,
                          Args&&... args) {
    Derived* derived_this = static_cast<Derived*>(this);

    DCHECK_EQ(derived_this->GetKey(cage_base, entry), deleted_element());
    DCHECK_LT(number_of_elements_ + 1, capacity());
    DCHECK(HasSufficientCapacityToAdd(capacity(), number_of_elements(),
                                      number_of_deleted_elements() - 1, 1));

    derived_this->Set(entry, std::forward<Args>(args)...);
    number_of_elements_++;
    number_of_deleted_elements_--;
  }

  void ElementsRemoved(int count) {
    DCHECK_LE(count, number_of_elements_);
    number_of_elements_ -= count;
    number_of_deleted_elements_ += count;
  }

  size_t GetSizeExcludingHeader() const {
    return GetSizeExcludingHeader(capacity_);
  }

  template <typename IsolateT, typename FindKey>
  inline InternalIndex FindEntry(IsolateT* isolate, FindKey key,
                                 uint32_t hash) const;

  inline InternalIndex FindInsertionEntry(PtrComprCageBase cage_base,
                                          uint32_t hash) const;

  template <typename IsolateT, typename FindKey>
  inline InternalIndex FindEntryOrInsertionEntry(IsolateT* isolate, FindKey key,
                                                 uint32_t hash) const;

  inline bool ShouldResizeToAdd(int number_of_additional_elements,
                                int* new_capacity);

  inline void RehashInto(PtrComprCageBase cage_base, Derived* new_table);

  inline void IterateElements(Root root, RootVisitor* visitor);

 protected:
  explicit OffHeapHashTableBase(int capacity);

  // Returns probe entry.
  static inline InternalIndex FirstProbe(uint32_t hash, uint32_t size) {
    return InternalIndex(hash & (size - 1));
  }

  static inline InternalIndex NextProbe(InternalIndex last, uint32_t number,
                                        uint32_t size) {
    return InternalIndex((last.as_uint32() + number) & (size - 1));
  }

  bool HasSufficientCapacityToAdd(int number_of_additional_elements) const {
    return HasSufficientCapacityToAdd(capacity(), number_of_elements(),
                                      number_of_deleted_elements(),
                                      number_of_additional_elements);
  }
  static inline bool HasSufficientCapacityToAdd(
      int capacity, int number_of_elements, int number_of_deleted_elements,
      int number_of_additional_elements);
  static inline int ComputeCapacity(int at_least_space_for);
  static inline int ComputeCapacityWithShrink(int current_capacity,
                                              int at_least_space_for);

  static inline size_t GetSizeExcludingHeader(int capacity) {
    // Subtract sizeof(Tagged_t) from the result, as the member elements_
    // already supplies the storage for the first element.
    return (capacity * sizeof(Tagged_t) * Derived::kEntrySize) -
           sizeof(Tagged_t);
  }

  // Returns memory to hold a Derived, which may be inline inside Container. The
  // offset of the elements_ field relative to Container must be passed for
  // static layout checks.
  template <typename Container, size_t OffsetOfElementsInContainer>
  static inline void* Allocate(int capacity);

  static inline void Free(void* container);

  int number_of_elements_;
  int number_of_deleted_elements_;
  const int capacity_;
  Tagged_t elements_[1];
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_OFF_HEAP_HASH_TABLE_H_
