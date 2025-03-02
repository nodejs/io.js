// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/utils/identity-map.h"

#include "src/base/hashing.h"
#include "src/base/logging.h"
#include "src/heap/heap.h"
#include "src/roots/roots-inl.h"

namespace v8 {
namespace internal {

static const int kInitialIdentityMapSize = 4;
static const int kResizeFactor = 2;

IdentityMapBase::~IdentityMapBase() {
  // Clear must be called by the subclass to avoid calling the virtual
  // DeleteArray function from the destructor.
  DCHECK_NULL(keys_);
}

void IdentityMapBase::Clear() {
  if (keys_) {
    DCHECK(!is_iterable());
    DCHECK_NOT_NULL(strong_roots_entry_);
    heap_->UnregisterStrongRoots(strong_roots_entry_);
    DeletePointerArray(reinterpret_cast<uintptr_t*>(keys_), capacity_);
    DeletePointerArray(values_, capacity_);
    keys_ = nullptr;
    strong_roots_entry_ = nullptr;
    values_ = nullptr;
    size_ = 0;
    capacity_ = 0;
    mask_ = 0;
  }
}

void IdentityMapBase::EnableIteration() {
  CHECK(!is_iterable());
  is_iterable_ = true;
}

void IdentityMapBase::DisableIteration() {
  CHECK(is_iterable());
  is_iterable_ = false;
}

std::pair<int, bool> IdentityMapBase::ScanKeysFor(Address address,
                                                  uint32_t hash) const {
  int start = hash & mask_;
  Address not_mapped = ReadOnlyRoots(heap_).not_mapped_symbol().ptr();
  for (int index = start; index < capacity_; index++) {
    if (keys_[index] == address) return {index, true};      // Found.
    if (keys_[index] == not_mapped) return {index, false};  // Not found.
  }
  for (int index = 0; index < start; index++) {
    if (keys_[index] == address) return {index, true};      // Found.
    if (keys_[index] == not_mapped) return {index, false};  // Not found.
  }
  return {-1, false};
}

bool IdentityMapBase::ShouldGrow() const {
  // Grow the map if we reached >= 80% occupancy.
  return size_ + size_ / 4 >= capacity_;
}

std::pair<int, bool> IdentityMapBase::InsertKey(Address address,
                                                uint32_t hash) {
  DCHECK_NE(heap_->gc_state(), Heap::MARK_COMPACT);
  DCHECK_EQ(gc_counter_, heap_->gc_count());

  if (ShouldGrow()) {
    Resize(capacity_ * kResizeFactor);
  }

  Address not_mapped = ReadOnlyRoots(heap_).not_mapped_symbol().ptr();

  int start = hash & mask_;
  // Guaranteed to terminate since size_ < capacity_, there must be at least
  // one empty slot.
  int index = start;
  while (true) {
    if (keys_[index] == address) return {index, true};  // Found.
    if (keys_[index] == not_mapped) {                   // Free entry.
      size_++;
      DCHECK_LE(size_, capacity_);
      keys_[index] = address;
      return {index, false};
    }
    index = (index + 1) & mask_;
    // We should never loop back to the start.
    DCHECK_NE(index, start);
  }
}

bool IdentityMapBase::DeleteIndex(int index, uintptr_t* deleted_value) {
  DCHECK_NE(heap_->gc_state(), Heap::MARK_COMPACT);
  if (deleted_value != nullptr) *deleted_value = values_[index];
  Address not_mapped = ReadOnlyRoots(heap_).not_mapped_symbol().ptr();
  DCHECK_NE(keys_[index], not_mapped);
  keys_[index] = not_mapped;
  values_[index] = 0;
  size_--;
  DCHECK_GE(size_, 0);

  if (capacity_ > kInitialIdentityMapSize &&
      size_ * kResizeFactor < capacity_ / kResizeFactor) {
    Resize(capacity_ / kResizeFactor);
    return true;  // No need to fix collisions as resize reinserts keys.
  }

  // Move any collisions to their new correct location.
  int next_index = index;
  for (;;) {
    next_index = (next_index + 1) & mask_;
    Address key = keys_[next_index];
    if (key == not_mapped) break;

    int expected_index = Hash(key) & mask_;
    if (index < next_index) {
      if (index < expected_index && expected_index <= next_index) continue;
    } else {
      DCHECK_GT(index, next_index);
      if (index < expected_index || expected_index <= next_index) continue;
    }

    DCHECK_EQ(not_mapped, keys_[index]);
    DCHECK_EQ(values_[index], 0);
    std::swap(keys_[index], keys_[next_index]);
    std::swap(values_[index], values_[next_index]);
    index = next_index;
  }

  return true;
}

int IdentityMapBase::Lookup(Address key) const {
  DCHECK_NE(heap_->gc_state(), Heap::MARK_COMPACT);
  uint32_t hash = Hash(key);
  int index;
  bool found;
  std::tie(index, found) = ScanKeysFor(key, hash);
  if (!found && gc_counter_ != heap_->gc_count()) {
    // Miss; rehash if there was a GC, then lookup again.
    const_cast<IdentityMapBase*>(this)->Rehash();
    std::tie(index, found) = ScanKeysFor(key, hash);
  }
  return found ? index : -1;
}

std::pair<int, bool> IdentityMapBase::LookupOrInsert(Address key) {
  DCHECK_NE(heap_->gc_state(), Heap::MARK_COMPACT);
  uint32_t hash = Hash(key);
  // Perform an optimistic lookup.
  int index;
  bool already_exists;
  std::tie(index, already_exists) = ScanKeysFor(key, hash);
  if (!already_exists) {
    // Miss; rehash if there was a GC, then insert.
    if (gc_counter_ != heap_->gc_count()) {
      Rehash();
      index = -1;
    }
    if (index < 0 || ShouldGrow()) {
      std::tie(index, already_exists) = InsertKey(key, hash);
    } else {
      // If rehashing is not necessary, and the table is already big enough,
      // then avoid calling InsertKey because it would search the table again
      // and we already found an adequate location to insert the new key.
      size_++;
      DCHECK_LE(size_, capacity_);
      DCHECK_EQ(keys_[index], ReadOnlyRoots(heap_).not_mapped_symbol().ptr());
      keys_[index] = key;
    }
  }
  DCHECK_GE(index, 0);
  return {index, already_exists};
}

uint32_t IdentityMapBase::Hash(Address address) const {
  CHECK_NE(address, ReadOnlyRoots(heap_).not_mapped_symbol().ptr());
  return static_cast<uint32_t>(hasher_(address));
}

// Searches this map for the given key using the object's address
// as the identity, returning:
//    found => a pointer to the storage location for the value, true
//    not found => a pointer to a new storage location for the value, false
IdentityMapFindResult<uintptr_t> IdentityMapBase::FindOrInsertEntry(
    Address key) {
  DCHECK_NE(heap_->gc_state(), Heap::MARK_COMPACT);
  CHECK(!is_iterable());  // Don't allow insertion while iterable.
  if (capacity_ == 0) {
    return {InsertEntry(key), false};
  }
  auto lookup_result = LookupOrInsert(key);
  return {&values_[lookup_result.first], lookup_result.second};
}

// Searches this map for the given key using the object's address
// as the identity, returning:
//    found => a pointer to the storage location for the value
//    not found => {nullptr}
IdentityMapBase::RawEntry IdentityMapBase::FindEntry(Address key) const {
  DCHECK_NE(heap_->gc_state(), Heap::MARK_COMPACT);
  // Don't allow find by key while iterable (might rehash).
  CHECK(!is_iterable());
  if (size_ == 0) return nullptr;
  int index = Lookup(key);
  return index >= 0 ? &values_[index] : nullptr;
}

// Inserts the given key using the object's address as the identity, returning
// a pointer to the new storage location for the value.
IdentityMapBase::RawEntry IdentityMapBase::InsertEntry(Address key) {
  DCHECK_NE(heap_->gc_state(), Heap::MARK_COMPACT);
  // Don't allow find by key while iterable (might rehash).
  CHECK(!is_iterable());
  if (capacity_ == 0) {
    // Allocate the initial storage for keys and values.
    capacity_ = kInitialIdentityMapSize;
    mask_ = kInitialIdentityMapSize - 1;
    gc_counter_ = heap_->gc_count();

    uintptr_t not_mapped = ReadOnlyRoots(heap_).not_mapped_symbol().ptr();
    keys_ = reinterpret_cast<Address*>(NewPointerArray(capacity_, not_mapped));
    for (int i = 0; i < capacity_; i++) keys_[i] = not_mapped;
    values_ = NewPointerArray(capacity_, 0);

    strong_roots_entry_ =
        heap_->RegisterStrongRoots("IdentityMapBase", FullObjectSlot(keys_),
                                   FullObjectSlot(keys_ + capacity_));
  } else {
    // Rehash if there was a GC, then insert.
    if (gc_counter_ != heap_->gc_count()) Rehash();
  }

  int index;
  bool already_exists;
  std::tie(index, already_exists) = InsertKey(key, Hash(key));
  DCHECK(!already_exists);
  return &values_[index];
}

// Deletes the given key from the map using the object's address as the
// identity, returning true iff the key was found (in which case, the value
// argument will be set to the deleted entry's value).
bool IdentityMapBase::DeleteEntry(Address key, uintptr_t* deleted_value) {
  CHECK(!is_iterable());  // Don't allow deletion by key while iterable.
  if (size_ == 0) return false;
  int index = Lookup(key);
  if (index < 0) return false;  // No entry found.
  return DeleteIndex(index, deleted_value);
}

Address IdentityMapBase::KeyAtIndex(int index) const {
  DCHECK_LE(0, index);
  DCHECK_LT(index, capacity_);
  DCHECK_NE(keys_[index], ReadOnlyRoots(heap_).not_mapped_symbol().ptr());
  CHECK(is_iterable());  // Must be iterable to access by index;
  return keys_[index];
}

IdentityMapBase::RawEntry IdentityMapBase::EntryAtIndex(int index) const {
  DCHECK_LE(0, index);
  DCHECK_LT(index, capacity_);
  DCHECK_NE(keys_[index], ReadOnlyRoots(heap_).not_mapped_symbol().ptr());
  CHECK(is_iterable());  // Must be iterable to access by index;
  return &values_[index];
}

int IdentityMapBase::NextIndex(int index) const {
  DCHECK_LE(-1, index);
  DCHECK_LE(index, capacity_);
  CHECK(is_iterable());  // Must be iterable to access by index;
  Address not_mapped = ReadOnlyRoots(heap_).not_mapped_symbol().ptr();
  for (++index; index < capacity_; ++index) {
    if (keys_[index] != not_mapped) {
      return index;
    }
  }
  return capacity_;
}

void IdentityMapBase::Rehash() {
  DCHECK_NE(heap_->gc_state(), Heap::MARK_COMPACT);
  CHECK(!is_iterable());  // Can't rehash while iterating.
  // Record the current GC counter.
  gc_counter_ = heap_->gc_count();
  // Assume that most objects won't be moved.
  std::vector<std::pair<Address, uintptr_t>> reinsert;
  // Search the table looking for keys that wouldn't be found with their
  // current hashcode and evacuate them.
  int last_empty = -1;
  Address not_mapped = ReadOnlyRoots(heap_).not_mapped_symbol().ptr();
  for (int i = 0; i < capacity_; i++) {
    if (keys_[i] == not_mapped) {
      last_empty = i;
    } else {
      int pos = Hash(keys_[i]) & mask_;
      if (pos <= last_empty || pos > i) {
        // Evacuate an entry that is in the wrong place.
        reinsert.push_back(std::pair<Address, uintptr_t>(keys_[i], values_[i]));
        keys_[i] = not_mapped;
        values_[i] = 0;
        last_empty = i;
        size_--;
      }
    }
  }
  // Reinsert all the key/value pairs that were in the wrong place.
  for (auto pair : reinsert) {
    int index = InsertKey(pair.first, Hash(pair.first)).first;
    DCHECK_GE(index, 0);
    values_[index] = pair.second;
  }
}

void IdentityMapBase::Resize(int new_capacity) {
  DCHECK_NE(heap_->gc_state(), Heap::MARK_COMPACT);
  CHECK(!is_iterable());  // Can't resize while iterating.
  // Resize the internal storage and reinsert all the key/value pairs.
  DCHECK_GT(new_capacity, size_);
  int old_capacity = capacity_;
  Address* old_keys = keys_;
  uintptr_t* old_values = values_;

  capacity_ = new_capacity;
  mask_ = capacity_ - 1;
  gc_counter_ = heap_->gc_count();
  size_ = 0;

  Address not_mapped = ReadOnlyRoots(heap_).not_mapped_symbol().ptr();
  keys_ = reinterpret_cast<Address*>(NewPointerArray(capacity_, not_mapped));
  values_ = NewPointerArray(capacity_, 0);

  for (int i = 0; i < old_capacity; i++) {
    if (old_keys[i] == not_mapped) continue;
    int index = InsertKey(old_keys[i], Hash(old_keys[i])).first;
    DCHECK_GE(index, 0);
    values_[index] = old_values[i];
  }

  // Unregister old keys and register new keys.
  DCHECK_NOT_NULL(strong_roots_entry_);
  heap_->UpdateStrongRoots(strong_roots_entry_, FullObjectSlot(keys_),
                           FullObjectSlot(keys_ + capacity_));

  // Delete old storage;
  DeletePointerArray(reinterpret_cast<uintptr_t*>(old_keys), old_capacity);
  DeletePointerArray(old_values, old_capacity);
}

}  // namespace internal
}  // namespace v8
