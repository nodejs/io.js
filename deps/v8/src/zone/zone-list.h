// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ZONE_ZONE_LIST_H_
#define V8_ZONE_ZONE_LIST_H_

#include "src/base/logging.h"
#include "src/zone/zone.h"

namespace v8 {

namespace base {
template <typename T>
class Vector;
}  // namespace base

namespace internal {

// ZoneLists are growable lists with constant-time access to the elements.
// The list itself and all its elements are supposed to be allocated in zone
// memory. Unlike ZoneVector container, the ZoneList instance has minimal
// possible size which makes it a good candidate for embedding into other
// often-allocated zone objects.
//
// Note, ZoneLists' elements cannot be deleted individually and the destructor
// intentionally does not free the backing store. Because of the latter, the
// ZoneList must not be used outsize of zone memory. Consider using ZoneVector
// or other containers instead.
template <typename T>
class ZoneList final : public ZoneObject {
 public:
  // Construct a new ZoneList with the given capacity; the length is
  // always zero. The capacity must be non-negative.
  ZoneList(int capacity, Zone* zone) : capacity_(capacity) {
    DCHECK_GE(capacity, 0);
    if (capacity > 0) {
      DCHECK_NOT_NULL(zone);
      data_ = zone->AllocateArray<T>(capacity);
    } else {
      data_ = nullptr;
    }
  }

  // Construct a new ZoneList by copying the elements of the given ZoneList.
  ZoneList(const ZoneList<T>& other, Zone* zone)
      : ZoneList(other.length(), zone) {
    AddAll(other, zone);
  }

  // Construct a new ZoneList by copying the elements of the given vector.
  ZoneList(base::Vector<const T> other, Zone* zone)
      : ZoneList(other.length(), zone) {
    AddAll(other, zone);
  }

  ZoneList(ZoneList<T>&& other) V8_NOEXCEPT { *this = std::move(other); }

  ZoneList(const ZoneList&) = delete;
  ZoneList& operator=(const ZoneList&) = delete;

  // The ZoneList objects are usually allocated as a fields in other
  // zone-allocated objects for which destructors are not called anyway, so
  // we are not going to clear the memory here as well.
  ~ZoneList() = default;

  ZoneList& operator=(ZoneList&& other) V8_NOEXCEPT {
    // We don't have a Zone object, so we'll have to drop the data_ array.
    // If this assert ever fails, consider calling Clear(Zone*) or
    // DropAndClear() before the move assignment to make it explicit what's
    // happenning with the lvalue.
    DCHECK_NULL(data_);
    data_ = other.data_;
    capacity_ = other.capacity_;
    length_ = other.length_;
    other.DropAndClear();
    return *this;
  }

  // Returns a reference to the element at index i. This reference is not safe
  // to use after operations that can change the list's backing store
  // (e.g. Add).
  inline T& operator[](int i) const {
    DCHECK_LE(0, i);
    DCHECK_GT(static_cast<unsigned>(length_), static_cast<unsigned>(i));
    return data_[i];
  }
  inline T& at(int i) const { return operator[](i); }
  inline T& last() const { return at(length_ - 1); }
  inline T& first() const { return at(0); }

  using iterator = T*;
  inline iterator begin() { return &data_[0]; }
  inline iterator end() { return &data_[length_]; }

  using const_iterator = const T*;
  inline const_iterator begin() const { return &data_[0]; }
  inline const_iterator end() const { return &data_[length_]; }

  V8_INLINE bool is_empty() const { return length_ == 0; }
  V8_INLINE int length() const { return length_; }
  V8_INLINE int capacity() const { return capacity_; }

  base::Vector<T> ToVector() const { return base::Vector<T>(data_, length_); }
  base::Vector<T> ToVector(int start, int length) const {
    DCHECK_LE(start, length_);
    return base::Vector<T>(&data_[start], std::min(length_ - start, length));
  }

  base::Vector<const T> ToConstVector() const {
    return base::Vector<const T>(data_, length_);
  }

  // Adds a copy of the given 'element' to the end of the list,
  // expanding the list if necessary.
  void Add(const T& element, Zone* zone);
  // Add all the elements from the argument list to this list.
  void AddAll(const ZoneList<T>& other, Zone* zone);
  // Add all the elements from the vector to this list.
  void AddAll(base::Vector<const T> other, Zone* zone);
  // Inserts the element at the specific index.
  void InsertAt(int index, const T& element, Zone* zone);

  // Added 'count' elements with the value 'value' and returns a
  // vector that allows access to the elements. The vector is valid
  // until the next change is made to this list.
  base::Vector<T> AddBlock(T value, int count, Zone* zone);

  // Overwrites the element at the specific index.
  void Set(int index, const T& element);

  // Removes the i'th element without deleting it even if T is a
  // pointer type; moves all elements above i "down". Returns the
  // removed element.  This function's complexity is linear in the
  // size of the list.
  T Remove(int i);

  // Removes the last element without deleting it even if T is a
  // pointer type. Returns the removed element.
  V8_INLINE T RemoveLast() { return Remove(length_ - 1); }

  // Clears the list by freeing the storage memory. If you want to keep the
  // memory, use Rewind(0) instead. Be aware, that even if T is a
  // pointer type, clearing the list doesn't delete the entries.
  V8_INLINE void Clear(Zone* zone);

  // Clears the list but unlike Clear(), it doesn't free the storage memory.
  // It's useful when the whole zone containing the backing store will be
  // released but the list will be used further.
  V8_INLINE void DropAndClear() {
    data_ = nullptr;
    capacity_ = 0;
    length_ = 0;
  }

  // Drops all but the first 'pos' elements from the list.
  V8_INLINE void Rewind(int pos);

  inline bool Contains(const T& elm) const {
    for (int i = 0; i < length_; i++) {
      if (data_[i] == elm) return true;
    }
    return false;
  }

  // Iterate through all list entries, starting at index 0.
  template <class Visitor>
  void Iterate(Visitor* visitor);

  // Sort all list entries (using QuickSort)
  template <typename CompareFunction>
  void Sort(CompareFunction cmp);
  template <typename CompareFunction>
  void StableSort(CompareFunction cmp, size_t start, size_t length);

 private:
  T* data_ = nullptr;
  int capacity_ = 0;
  int length_ = 0;

  // Increase the capacity of a full list, and add an element.
  // List must be full already.
  void ResizeAdd(const T& element, Zone* zone);

  // Inlined implementation of ResizeAdd, shared by inlined and
  // non-inlined versions of ResizeAdd.
  void ResizeAddInternal(const T& element, Zone* zone);

  // Resize the list.
  void Resize(int new_capacity, Zone* zone);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_ZONE_ZONE_LIST_H_
