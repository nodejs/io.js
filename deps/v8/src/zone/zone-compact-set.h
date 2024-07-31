// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ZONE_ZONE_COMPACT_SET_H_
#define V8_ZONE_ZONE_COMPACT_SET_H_

#include <algorithm>
#include <initializer_list>
#include <type_traits>

#include "src/base/compiler-specific.h"
#include "src/base/pointer-with-payload.h"
#include "src/common/assert-scope.h"
#include "src/handles/handles.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

template <typename T, typename Enable = void>
struct ZoneCompactSetTraits;

template <typename T>
struct ZoneCompactSetTraits<Handle<T>> {
  using handle_type = Handle<T>;
  using data_type = Address;

  static data_type* HandleToPointer(handle_type handle) {
    // Use address() instead of location() to get around handle access checks
    // (we're not actually dereferencing the handle so it's safe to read its
    // location)
    return reinterpret_cast<Address*>(handle.address());
  }
  static handle_type PointerToHandle(data_type* ptr) {
    return handle_type(ptr);
  }
};

// A Zone-allocated set which has a compact encoding of zero and one values.
// Two or more values will be stored as a sorted list, which is copied on write
// to keep the ZoneCompactSet copy constructor trivial. Note that this means
// that insertions past the first value will trigger an allocation and copy of
// the existing elements -- ZoneCompactSet should be preferred for cases where
// we mostly have only zero or one values.
//
// T must be a Handle-like type with a specialization of ZoneCompactSetTraits.
// In particular, it must be a trivial wrapper of a pointer to actual data --
// ZoneCompactSet will store this pointer rather than the T type.
template <typename T>
class ZoneCompactSet final {
  static_assert(std::is_trivially_copyable_v<T>);
  static_assert(std::is_trivially_destructible_v<T>);

  using Traits = ZoneCompactSetTraits<T>;
  using handle_type = typename Traits::handle_type;
  using data_type = typename Traits::data_type;

 public:
  ZoneCompactSet() : data_(kEmptyTag) {}
  explicit ZoneCompactSet(T handle)
      : data_(Traits::HandleToPointer(handle), kSingletonTag) {}
  explicit ZoneCompactSet(std::initializer_list<T> handles, Zone* zone)
      : ZoneCompactSet(handles.begin(), handles.end(), zone) {}

  ZoneCompactSet(const ZoneCompactSet& other) V8_NOEXCEPT = default;
  ZoneCompactSet& operator=(const ZoneCompactSet& other) V8_NOEXCEPT = default;
  ZoneCompactSet(ZoneCompactSet&& other) V8_NOEXCEPT = default;
  ZoneCompactSet& operator=(ZoneCompactSet&& other) V8_NOEXCEPT = default;

  template <class It,
            typename = typename std::iterator_traits<It>::iterator_category>
  explicit ZoneCompactSet(It first, It last, Zone* zone) {
    auto size = last - first;
    if (size == 0) {
      data_ = EmptyValue();
    } else if (size == 1) {
      data_ =
          PointerWithPayload(Traits::HandleToPointer(*first), kSingletonTag);
    } else {
      List* list = NewList(size, zone);
      auto list_it = list->begin();
      for (auto it = first; it != last; ++it) {
        *list_it = Traits::HandleToPointer(*it);
        list_it++;
      }
      std::sort(list->begin(), list->end());
      data_ = PointerWithPayload(list, kListTag);
    }
  }

  bool is_empty() const { return data_ == EmptyValue(); }

  size_t size() const {
    if (is_empty()) return 0;
    if (is_singleton()) return 1;
    return list()->size();
  }

  T at(size_t i) const {
    DCHECK_NE(kEmptyTag, data_.GetPayload());
    if (is_singleton()) {
      DCHECK_EQ(0u, i);
      return Traits::PointerToHandle(singleton());
    }
    return Traits::PointerToHandle(list()->at(static_cast<int>(i)));
  }

  T operator[](size_t i) const { return at(i); }

  void insert(T handle, Zone* zone) {
    data_type* const value = Traits::HandleToPointer(handle);
    if (is_empty()) {
      data_ = PointerWithPayload(value, kSingletonTag);
    } else if (is_singleton()) {
      if (singleton() == value) return;
      List* list = NewList(2, zone);
      if (singleton() < value) {
        (*list)[0] = singleton();
        (*list)[1] = value;
      } else {
        (*list)[0] = value;
        (*list)[1] = singleton();
      }
      data_ = PointerWithPayload(list, kListTag);
    } else {
      const List* current_list = list();
      auto it =
          std::lower_bound(current_list->begin(), current_list->end(), value);
      if (it != current_list->end() && *it == value) {
        // Already in the list.
        return;
      }
      // Otherwise, lower_bound returned the insertion position to keep the list
      // sorted.
      DCHECK(it == current_list->end() || *it > value);
      // We need to copy the list to mutate it, so that trivial copies of the
      // data_ pointer don't observe changes to the list.
      // TODO(leszeks): Avoid copying on every insertion by introducing some
      // concept of mutable/immutable/frozen/CoW sets.
      List* new_list = NewList(current_list->size() + 1, zone);
      auto new_it = new_list->begin();
      new_it = std::copy(current_list->begin(), it, new_it);
      *new_it++ = value;
      new_it = std::copy(it, current_list->end(), new_it);
      DCHECK_EQ(new_it, new_list->end());
      DCHECK(std::is_sorted(new_list->begin(), new_list->end()));
      data_ = PointerWithPayload(new_list, kListTag);
    }
  }

  void Union(ZoneCompactSet<T> const& other, Zone* zone) {
    for (size_t i = 0; i < other.size(); ++i) {
      insert(other.at(i), zone);
    }
  }

  bool contains(ZoneCompactSet<T> const& other) const {
    if (data_ == other.data_) return true;
    if (is_empty()) return false;
    if (other.is_empty()) return true;
    if (is_singleton()) {
      DCHECK_IMPLIES(other.is_singleton(), other.singleton() != singleton());
      return false;
    }
    const List* list = this->list();
    DCHECK(std::is_sorted(list->begin(), list->end()));
    if (other.is_singleton()) {
      return std::binary_search(list->begin(), list->end(), other.singleton());
    }
    DCHECK(other.is_list());
    DCHECK(std::is_sorted(other.list()->begin(), other.list()->end()));
    // For each element in the `other` list, find the matching element in this
    // list. Since both lists are sorted, each search candidate will be larger
    // than the previous, and each found element will be the lower bound for
    // the search of the next element.
    auto it = list->begin();
    for (const data_type* pointer : *other.list()) {
      it = std::lower_bound(it, list->end(), pointer);
      if (it == list->end() || *it != pointer) return false;
    }
    return true;
  }

  bool contains(T handle) const {
    if (is_empty()) return false;
    data_type* pointer = Traits::HandleToPointer(handle);
    if (is_singleton()) {
      return singleton() == pointer;
    }
    const List* list = this->list();
    DCHECK(std::is_sorted(list->begin(), list->end()));
    return std::binary_search(list->begin(), list->end(), pointer);
  }

  void remove(T handle, Zone* zone) {
    if (is_empty()) return;
    data_type* pointer = Traits::HandleToPointer(handle);
    if (is_singleton()) {
      if (singleton() == pointer) {
        data_ = EmptyValue();
      }
      return;
    }
    const List* current_list = list();
    auto found_it =
        std::lower_bound(current_list->begin(), current_list->end(), pointer);
    if (found_it == current_list->end() || *found_it != pointer) {
      // Not in the list.
      return;
    }
    // Otherwise, lower_bound returned the location of the value.

    // Drop back down to singleton mode if the size will drops to 1 -- this is
    // needed to ensure that comparisons are correct. We never have to drop down
    // from list to zero size.
    DCHECK_GE(current_list->size(), 2);
    if (current_list->size() == 2) {
      data_type* other_value;
      if (found_it == current_list->begin()) {
        other_value = current_list->at(1);
      } else {
        other_value = current_list->at(0);
      }
      data_ = PointerWithPayload(other_value, kSingletonTag);
      return;
    }

    // We need to copy the list to mutate it, so that trivial copies of the
    // data_ pointer don't observe changes to the list.
    List* new_list = NewList(current_list->size() - 1, zone);
    auto new_it = new_list->begin();
    new_it = std::copy(current_list->begin(), found_it, new_it);
    new_it = std::copy(found_it + 1, current_list->end(), new_it);
    DCHECK_EQ(new_it, new_list->end());
    DCHECK(std::is_sorted(new_list->begin(), new_list->end()));
    data_ = PointerWithPayload(new_list, kListTag);
  }

  void clear() { data_ = EmptyValue(); }

  friend bool operator==(ZoneCompactSet<T> const& lhs,
                         ZoneCompactSet<T> const& rhs) {
    if (lhs.data_ == rhs.data_) return true;
    if (lhs.is_list() && rhs.is_list()) {
      List const* const lhs_list = lhs.list();
      List const* const rhs_list = rhs.list();
      return std::equal(lhs_list->begin(), lhs_list->end(), rhs_list->begin(),
                        rhs_list->end());
    }
    return false;
  }

  friend bool operator!=(ZoneCompactSet<T> const& lhs,
                         ZoneCompactSet<T> const& rhs) {
    return !(lhs == rhs);
  }

  friend uintptr_t hash_value(ZoneCompactSet<T> const& set) {
    return set.data_.raw();
  }

  class const_iterator;
  inline const_iterator begin() const;
  inline const_iterator end() const;

 private:
  enum Tag { kSingletonTag = 0, kEmptyTag = 1, kListTag = 2 };

  using List = base::Vector<data_type*>;
  using PointerWithPayload = base::PointerWithPayload<void, Tag, 2>;

  bool is_singleton() const { return data_.GetPayload() == kSingletonTag; }
  bool is_list() const { return data_.GetPayload() == kListTag; }

  List const* list() const {
    DCHECK(is_list());
    return static_cast<List const*>(data_.GetPointerWithKnownPayload(kListTag));
  }

  data_type* singleton() const {
    return static_cast<data_type*>(
        data_.GetPointerWithKnownPayload(kSingletonTag));
  }

  List* NewList(size_t size, Zone* zone) {
    // We need to allocate both the List, and the backing store of the list, in
    // the zone, so that we have a List pointer and not an on-stack List (which
    // we can't use in the `data_` pointer).
    return zone->New<List>(zone->AllocateArray<data_type*>(size), size);
  }

  static PointerWithPayload EmptyValue() {
    return PointerWithPayload(nullptr, kEmptyTag);
  }

  PointerWithPayload data_;
};

template <typename T>
std::ostream& operator<<(std::ostream& os, ZoneCompactSet<T> set) {
  for (size_t i = 0; i < set.size(); ++i) {
    if (i > 0) os << ", ";
    os << set.at(i);
  }
  return os;
}

template <typename T>
class ZoneCompactSet<T>::const_iterator {
 public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = T;
  using reference = value_type;
  using pointer = value_type*;

  const_iterator(const const_iterator& other) = default;
  const_iterator& operator=(const const_iterator& other) = default;

  reference operator*() const { return (*set_)[current_]; }
  bool operator==(const const_iterator& other) const {
    return set_ == other.set_ && current_ == other.current_;
  }
  bool operator!=(const const_iterator& other) const {
    return !(*this == other);
  }
  const_iterator& operator++() {
    DCHECK(current_ < set_->size());
    current_ += 1;
    return *this;
  }
  const_iterator operator++(int);

 private:
  friend class ZoneCompactSet<T>;

  explicit const_iterator(const ZoneCompactSet<T>* set, size_t current)
      : set_(set), current_(current) {}

  const ZoneCompactSet<T>* set_;
  size_t current_;
};

template <typename T>
typename ZoneCompactSet<T>::const_iterator ZoneCompactSet<T>::begin() const {
  return ZoneCompactSet<T>::const_iterator(this, 0);
}

template <typename T>
typename ZoneCompactSet<T>::const_iterator ZoneCompactSet<T>::end() const {
  return ZoneCompactSet<T>::const_iterator(this, size());
}

template <typename T>
using ZoneHandleSet = ZoneCompactSet<Handle<T>>;

}  // namespace internal
}  // namespace v8

#endif  // V8_ZONE_ZONE_COMPACT_SET_H_
