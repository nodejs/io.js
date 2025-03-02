// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ZONE_ZONE_CONTAINERS_H_
#define V8_ZONE_ZONE_CONTAINERS_H_

#include <deque>
#include <forward_list>
#include <initializer_list>
#include <iterator>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <unordered_map>
#include <unordered_set>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "src/base/hashing.h"
#include "src/base/intrusive-set.h"
#include "src/base/small-map.h"
#include "src/base/small-vector.h"
#include "src/zone/zone-allocator.h"

namespace v8 {
namespace internal {

// A drop-in replacement for std::vector that uses a Zone for its allocations,
// and (contrary to a std::vector subclass with custom allocator) gives us
// precise control over its implementation and performance characteristics.
//
// When working on this code, keep the following rules of thumb in mind:
// - Everything between {data_} and {end_} (exclusive) is a live instance of T.
//   When writing to these slots, use the {CopyingOverwrite} or
//   {MovingOverwrite} helpers.
// - Everything between {end_} (inclusive) and {capacity_} (exclusive) is
//   considered uninitialized memory. When writing to these slots, use the
//   {CopyToNewStorage} or {MoveToNewStorage} helpers. Obviously, also use
//   these helpers to initialize slots in newly allocated backing stores.
// - When shrinking, call ~T on all slots between the new and the old position
//   of {end_} to maintain the above invariant. Also call ~T on all slots in
//   discarded backing stores.
// - The interface offered by {ZoneVector} should be a subset of
//   {std::vector}'s API, so that calling code doesn't need to be aware of
//   ZoneVector's implementation details and can assume standard C++ behavior.
//   (It's okay if we don't support everything that std::vector supports; we
//   can fill such gaps when use cases arise.)
template <typename T>
class ZoneVector {
 public:
  using iterator = T*;
  using const_iterator = const T*;
  using reverse_iterator = std::reverse_iterator<T*>;
  using const_reverse_iterator = std::reverse_iterator<const T*>;
  using value_type = T;
  using reference = T&;
  using const_reference = const T&;
  using size_type = size_t;

  // Constructs an empty vector.
  explicit ZoneVector(Zone* zone) : zone_(zone) {}

  // Constructs a new vector and fills it with {size} elements, each
  // constructed via the default constructor.
  ZoneVector(size_t size, Zone* zone) : zone_(zone) {
    data_ = size > 0 ? zone->AllocateArray<T>(size) : nullptr;
    end_ = capacity_ = data_ + size;
    for (T* p = data_; p < end_; p++) emplace(p);
  }

  // Constructs a new vector and fills it with {size} elements, each
  // having the value {def}.
  ZoneVector(size_t size, T def, Zone* zone) : zone_(zone) {
    data_ = size > 0 ? zone->AllocateArray<T>(size) : nullptr;
    end_ = capacity_ = data_ + size;
    for (T* p = data_; p < end_; p++) emplace(p, def);
  }

  // Constructs a new vector and fills it with the contents of the given
  // initializer list.
  ZoneVector(std::initializer_list<T> list, Zone* zone) : zone_(zone) {
    size_t size = list.size();
    if (size > 0) {
      data_ = zone->AllocateArray<T>(size);
      CopyToNewStorage(data_, list.begin(), list.end());
    } else {
      data_ = nullptr;
    }
    end_ = capacity_ = data_ + size;
  }

  // Constructs a new vector and fills it with the contents of the range
  // [first, last).
  template <class It,
            typename = typename std::iterator_traits<It>::iterator_category>
  ZoneVector(It first, It last, Zone* zone) : zone_(zone) {
    if constexpr (std::is_base_of_v<
                      std::random_access_iterator_tag,
                      typename std::iterator_traits<It>::iterator_category>) {
      size_t size = last - first;
      data_ = size > 0 ? zone->AllocateArray<T>(size) : nullptr;
      end_ = capacity_ = data_ + size;
      for (T* p = data_; p < end_; p++) emplace(p, *first++);
    } else {
      while (first != last) push_back(*first++);
    }
    DCHECK_EQ(first, last);
  }

  ZoneVector(const ZoneVector& other) V8_NOEXCEPT : zone_(other.zone_) {
    *this = other;
  }

  ZoneVector(ZoneVector&& other) V8_NOEXCEPT { *this = std::move(other); }

  ~ZoneVector() {
    for (T* p = data_; p < end_; p++) p->~T();
    if (data_) zone_->DeleteArray(data_, capacity());
  }

  // Assignment operators.
  ZoneVector& operator=(const ZoneVector& other) V8_NOEXCEPT {
    // Self-assignment would cause undefined behavior in the !copy_assignable
    // branch, but likely indicates a bug in calling code anyway.
    DCHECK_NE(this, &other);
    T* src = other.data_;
    if (capacity() >= other.size() && zone_ == other.zone_) {
      T* dst = data_;
      if constexpr (std::is_trivially_copyable_v<T>) {
        size_t size = other.size();
        if (size) memcpy(dst, src, size * sizeof(T));
        end_ = dst + size;
      } else if constexpr (std::is_copy_assignable_v<T>) {
        while (dst < end_ && src < other.end_) *dst++ = *src++;
        while (src < other.end_) emplace(dst++, *src++);
        T* old_end = end_;
        end_ = dst;
        for (T* p = end_; p < old_end; p++) p->~T();
      } else {
        for (T* p = data_; p < end_; p++) p->~T();
        while (src < other.end_) emplace(dst++, *src++);
        end_ = dst;
      }
    } else {
      for (T* p = data_; p < end_; p++) p->~T();
      if (data_) zone_->DeleteArray(data_, capacity());
      size_t new_cap = other.capacity();
      if (new_cap > 0) {
        data_ = zone_->AllocateArray<T>(new_cap);
        CopyToNewStorage(data_, other.data_, other.end_);
      } else {
        data_ = nullptr;
      }
      capacity_ = data_ + new_cap;
      end_ = data_ + other.size();
    }
    return *this;
  }

  ZoneVector& operator=(ZoneVector&& other) V8_NOEXCEPT {
    // Self-assignment would cause undefined behavior, and is probably a bug.
    DCHECK_NE(this, &other);
    // Move-assigning vectors from different zones would have surprising
    // lifetime semantics regardless of how we choose to implement it (keep
    // the old zone? Take the new zone?).
    if (zone_ == nullptr) {
      zone_ = other.zone_;
    } else {
      DCHECK_EQ(zone_, other.zone_);
    }
    for (T* p = data_; p < end_; p++) p->~T();
    if (data_) zone_->DeleteArray(data_, capacity());
    data_ = other.data_;
    end_ = other.end_;
    capacity_ = other.capacity_;
    // {other.zone_} may stay.
    other.data_ = other.end_ = other.capacity_ = nullptr;
    return *this;
  }

  ZoneVector& operator=(std::initializer_list<T> ilist) {
    clear();
    EnsureCapacity(ilist.size());
    CopyToNewStorage(data_, ilist.begin(), ilist.end());
    end_ = data_ + ilist.size();
    return *this;
  }

  void swap(ZoneVector<T>& other) noexcept {
    DCHECK_EQ(zone_, other.zone_);
    std::swap(data_, other.data_);
    std::swap(end_, other.end_);
    std::swap(capacity_, other.capacity_);
  }

  void resize(size_t new_size) {
    EnsureCapacity(new_size);
    T* new_end = data_ + new_size;
    for (T* p = end_; p < new_end; p++) emplace(p);
    for (T* p = new_end; p < end_; p++) p->~T();
    end_ = new_end;
  }

  void resize(size_t new_size, const T& value) {
    EnsureCapacity(new_size);
    T* new_end = data_ + new_size;
    for (T* p = end_; p < new_end; p++) emplace(p, value);
    for (T* p = new_end; p < end_; p++) p->~T();
    end_ = new_end;
  }

  void assign(size_t new_size, const T& value) {
    if (capacity() >= new_size) {
      T* new_end = data_ + new_size;
      T* assignable = data_ + std::min(size(), new_size);
      for (T* p = data_; p < assignable; p++) CopyingOverwrite(p, &value);
      for (T* p = assignable; p < new_end; p++) CopyToNewStorage(p, &value);
      for (T* p = new_end; p < end_; p++) p->~T();
      end_ = new_end;
    } else {
      clear();
      EnsureCapacity(new_size);
      T* new_end = data_ + new_size;
      for (T* p = data_; p < new_end; p++) emplace(p, value);
      end_ = new_end;
    }
  }

  void clear() {
    for (T* p = data_; p < end_; p++) p->~T();
    end_ = data_;
  }

  size_t size() const { return end_ - data_; }
  bool empty() const { return end_ == data_; }
  size_t capacity() const { return capacity_ - data_; }
  void reserve(size_t new_cap) { EnsureCapacity(new_cap); }
  T* data() { return data_; }
  const T* data() const { return data_; }
  Zone* zone() const { return zone_; }

  T& at(size_t pos) {
    DCHECK_LT(pos, size());
    return data_[pos];
  }
  const T& at(size_t pos) const {
    DCHECK_LT(pos, size());
    return data_[pos];
  }

  T& operator[](size_t pos) { return at(pos); }
  const T& operator[](size_t pos) const { return at(pos); }

  T& front() {
    DCHECK_GT(end_, data_);
    return *data_;
  }
  const T& front() const {
    DCHECK_GT(end_, data_);
    return *data_;
  }

  T& back() {
    DCHECK_GT(end_, data_);
    return *(end_ - 1);
  }
  const T& back() const {
    DCHECK_GT(end_, data_);
    return *(end_ - 1);
  }

  T* begin() V8_NOEXCEPT { return data_; }
  const T* begin() const V8_NOEXCEPT { return data_; }
  const T* cbegin() const V8_NOEXCEPT { return data_; }

  T* end() V8_NOEXCEPT { return end_; }
  const T* end() const V8_NOEXCEPT { return end_; }
  const T* cend() const V8_NOEXCEPT { return end_; }

  reverse_iterator rbegin() V8_NOEXCEPT {
    return std::make_reverse_iterator(end());
  }
  const_reverse_iterator rbegin() const V8_NOEXCEPT {
    return std::make_reverse_iterator(end());
  }
  const_reverse_iterator crbegin() const V8_NOEXCEPT {
    return std::make_reverse_iterator(cend());
  }
  reverse_iterator rend() V8_NOEXCEPT {
    return std::make_reverse_iterator(begin());
  }
  const_reverse_iterator rend() const V8_NOEXCEPT {
    return std::make_reverse_iterator(begin());
  }
  const_reverse_iterator crend() const V8_NOEXCEPT {
    return std::make_reverse_iterator(cbegin());
  }

  void push_back(const T& value) {
    EnsureOneMoreCapacity();
    emplace(end_++, value);
  }
  void push_back(T&& value) { emplace_back(std::move(value)); }

  void pop_back() {
    DCHECK_GT(end_, data_);
    (--end_)->~T();
  }

  template <typename... Args>
  T& emplace_back(Args&&... args) {
    EnsureOneMoreCapacity();
    T* ptr = end_++;
    new (ptr) T(std::forward<Args>(args)...);
    return *ptr;
  }

  template <class It,
            typename = typename std::iterator_traits<It>::iterator_category>
  T* insert(const T* pos, It first, It last) {
    T* position;
    if constexpr (std::is_base_of_v<
                      std::random_access_iterator_tag,
                      typename std::iterator_traits<It>::iterator_category>) {
      DCHECK_LE(0, last - first);
      size_t count = last - first;
      size_t assignable;
      position = PrepareForInsertion(pos, count, &assignable);
      if constexpr (std::is_trivially_copyable_v<T>) {
        if (count > 0) memcpy(position, first, count * sizeof(T));
      } else {
        CopyingOverwrite(position, first, first + assignable);
        CopyToNewStorage(position + assignable, first + assignable, last);
      }
    } else if (pos == end()) {
      position = end_;
      while (first != last) {
        EnsureOneMoreCapacity();
        emplace(end_++, *first++);
      }
    } else {
      UNIMPLEMENTED();
      // We currently have no users of this case.
      // It could be implemented inefficiently as a combination of the two
      // cases above: while (first != last) { PrepareForInsertion(_, 1, _); }.
      // A more efficient approach would be to accumulate the input iterator's
      // results into a temporary vector first, then grow {this} only once
      // (by calling PrepareForInsertion(_, count, _)), then copy over the
      // accumulated elements.
    }
    return position;
  }
  T* insert(const T* pos, size_t count, const T& value) {
    size_t assignable;
    T* position = PrepareForInsertion(pos, count, &assignable);
    T* dst = position;
    T* stop = dst + assignable;
    while (dst < stop) {
      CopyingOverwrite(dst++, &value);
    }
    stop = position + count;
    while (dst < stop) emplace(dst++, value);
    return position;
  }

  T* erase(const T* pos) {
    DCHECK(data_ <= pos && pos <= end());
    if (pos == end()) return const_cast<T*>(pos);
    return erase(pos, 1);
  }
  T* erase(const T* first, const T* last) {
    DCHECK(data_ <= first && first <= last && last <= end());
    if (first == last) return const_cast<T*>(first);
    return erase(first, last - first);
  }

 private:
  static constexpr size_t kMinCapacity = 2;
  size_t NewCapacity(size_t minimum) {
    // We can ignore possible overflow here: on 32-bit platforms, if the
    // multiplication overflows, there's no better way to handle it than
    // relying on the "new_capacity < minimum" check; in particular, a
    // saturating multiplication would make no sense. On 64-bit platforms,
    // overflow is effectively impossible anyway.
    size_t new_capacity = data_ == capacity_ ? kMinCapacity : capacity() * 2;
    return new_capacity < minimum ? minimum : new_capacity;
  }

  V8_INLINE void EnsureOneMoreCapacity() {
    if (V8_LIKELY(end_ < capacity_)) return;
    Grow(capacity() + 1);
  }

  V8_INLINE void EnsureCapacity(size_t minimum) {
    if (V8_LIKELY(minimum <= capacity())) return;
    Grow(minimum);
  }

  V8_INLINE void CopyToNewStorage(T* dst, const T* src) { emplace(dst, *src); }

  V8_INLINE void MoveToNewStorage(T* dst, T* src) {
    if constexpr (std::is_move_constructible_v<T>) {
      emplace(dst, std::move(*src));
    } else {
      CopyToNewStorage(dst, src);
    }
  }

  V8_INLINE void CopyingOverwrite(T* dst, const T* src) {
    if constexpr (std::is_copy_assignable_v<T>) {
      *dst = *src;
    } else {
      dst->~T();
      CopyToNewStorage(dst, src);
    }
  }

  V8_INLINE void MovingOverwrite(T* dst, T* src) {
    if constexpr (std::is_move_assignable_v<T>) {
      *dst = std::move(*src);
    } else {
      CopyingOverwrite(dst, src);
    }
  }

#define EMIT_TRIVIAL_CASE(memcpy_function)                 \
  DCHECK_LE(src, src_end);                                 \
  if constexpr (std::is_trivially_copyable_v<T>) {         \
    size_t count = src_end - src;                          \
    /* Add V8_ASSUME to silence gcc null check warning. */ \
    V8_ASSUME(src != nullptr);                             \
    memcpy_function(dst, src, count * sizeof(T));          \
    return;                                                \
  }

  V8_INLINE void CopyToNewStorage(T* dst, const T* src, const T* src_end) {
    EMIT_TRIVIAL_CASE(memcpy)
    for (; src < src_end; dst++, src++) {
      CopyToNewStorage(dst, src);
    }
  }

  V8_INLINE void MoveToNewStorage(T* dst, T* src, const T* src_end) {
    EMIT_TRIVIAL_CASE(memcpy)
    for (; src < src_end; dst++, src++) {
      MoveToNewStorage(dst, src);
      src->~T();
    }
  }

  V8_INLINE void CopyingOverwrite(T* dst, const T* src, const T* src_end) {
    EMIT_TRIVIAL_CASE(memmove)
    for (; src < src_end; dst++, src++) {
      CopyingOverwrite(dst, src);
    }
  }

  V8_INLINE void MovingOverwrite(T* dst, T* src, const T* src_end) {
    EMIT_TRIVIAL_CASE(memmove)
    for (; src < src_end; dst++, src++) {
      MovingOverwrite(dst, src);
    }
  }

#undef EMIT_TRIVIAL_CASE

  V8_NOINLINE V8_PRESERVE_MOST void Grow(size_t minimum) {
    T* old_data = data_;
    T* old_end = end_;
    size_t old_size = size();
    size_t new_capacity = NewCapacity(minimum);
    data_ = zone_->AllocateArray<T>(new_capacity);
    end_ = data_ + old_size;
    if (old_data) {
      MoveToNewStorage(data_, old_data, old_end);
      zone_->DeleteArray(old_data, capacity_ - old_data);
    }
    capacity_ = data_ + new_capacity;
  }

  T* PrepareForInsertion(const T* pos, size_t count, size_t* assignable) {
    DCHECK(data_ <= pos && pos <= end_);
    CHECK(std::numeric_limits<size_t>::max() - size() >= count);
    size_t index = pos - data_;
    size_t to_shift = end() - pos;
    DCHECK_EQ(index + to_shift, size());
    if (capacity() < size() + count) {
      *assignable = 0;  // Fresh memory is not assignable (must be constructed).
      T* old_data = data_;
      T* old_end = end_;
      size_t old_size = size();
      size_t new_capacity = NewCapacity(old_size + count);
      data_ = zone_->AllocateArray<T>(new_capacity);
      end_ = data_ + old_size + count;
      if (old_data) {
        MoveToNewStorage(data_, old_data, pos);
        MoveToNewStorage(data_ + index + count, const_cast<T*>(pos), old_end);
        zone_->DeleteArray(old_data, capacity_ - old_data);
      }
      capacity_ = data_ + new_capacity;
    } else {
      // There are two interesting cases: we're inserting more elements
      // than we're shifting (top), or the other way round (bottom).
      //
      // Old: [ABCDEFGHIJ___________]
      //       <--used--><--empty-->
      //
      // Case 1: index=7, count=8, to_shift=3
      // New: [ABCDEFGaaacccccHIJ___]
      //              <-><------>
      //               ↑    ↑ to be in-place constructed
      //               ↑
      //               assignable_slots
      //
      // Case 2: index=3, count=3, to_shift=7
      // New: [ABCaaaDEFGHIJ________]
      //          <-----><->
      //             ↑    ↑ to be in-place constructed
      //             ↑
      //             This range can be assigned. We report the first 3
      //             as {assignable_slots} to the caller, and use the other 4
      //             in the loop below.
      // Observe that the number of old elements that are moved to the
      // new end by in-place construction always equals {assignable_slots}.
      size_t assignable_slots = std::min(to_shift, count);
      *assignable = assignable_slots;
      if constexpr (std::is_trivially_copyable_v<T>) {
        if (to_shift > 0) {
          // Add V8_ASSUME to silence gcc null check warning.
          V8_ASSUME(pos != nullptr);
          memmove(const_cast<T*>(pos + count), pos, to_shift * sizeof(T));
        }
        end_ += count;
        return data_ + index;
      }
      // Construct elements in previously-unused area ("HIJ" in the example
      // above). This frees up assignable slots.
      T* dst = end_ + count;
      T* src = end_;
      for (T* stop = dst - assignable_slots; dst > stop;) {
        MoveToNewStorage(--dst, --src);
      }
      // Move (by assignment) elements into previously used area. This is
      // "DEFG" in "case 2" in the example above.
      DCHECK_EQ(src > pos, to_shift > count);
      DCHECK_IMPLIES(src > pos, dst == end_);
      while (src > pos) MovingOverwrite(--dst, --src);
      // Not destructing {src} here because that'll happen either in a
      // future iteration (when that spot becomes {dst}) or in {insert()}.
      end_ += count;
    }
    return data_ + index;
  }

  T* erase(const T* first, size_t count) {
    DCHECK(data_ <= first && first <= end());
    DCHECK_LE(count, end() - first);
    T* position = const_cast<T*>(first);
    MovingOverwrite(position, position + count, end());
    T* old_end = end();
    end_ -= count;
    for (T* p = end_; p < old_end; p++) p->~T();
    return position;
  }

  template <typename... Args>
  void emplace(T* target, Args&&... args) {
    new (target) T(std::forward<Args>(args)...);
  }

  Zone* zone_{nullptr};
  T* data_{nullptr};
  T* end_{nullptr};
  T* capacity_{nullptr};
};

template <class T>
bool operator==(const ZoneVector<T>& lhs, const ZoneVector<T>& rhs) {
  return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <class T>
bool operator!=(const ZoneVector<T>& lhs, const ZoneVector<T>& rhs) {
  return !(lhs == rhs);
}

template <class T>
bool operator<(const ZoneVector<T>& lhs, const ZoneVector<T>& rhs) {
  return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(),
                                      rhs.end());
}

template <class T, class GetIntrusiveSetIndex>
class ZoneIntrusiveSet
    : public base::IntrusiveSet<T, GetIntrusiveSetIndex, ZoneVector<T>> {
 public:
  explicit ZoneIntrusiveSet(Zone* zone, GetIntrusiveSetIndex index_functor = {})
      : base::IntrusiveSet<T, GetIntrusiveSetIndex, ZoneVector<T>>(
            ZoneVector<T>(zone), std::move(index_functor)) {}
};
using base::IntrusiveSetIndex;

// A wrapper subclass for std::deque to make it easy to construct one
// that uses a zone allocator.
template <typename T>
class ZoneDeque : public std::deque<T, RecyclingZoneAllocator<T>> {
 public:
  // Constructs an empty deque.
  explicit ZoneDeque(Zone* zone)
      : std::deque<T, RecyclingZoneAllocator<T>>(
            RecyclingZoneAllocator<T>(zone)) {}
};

// A wrapper subclass for std::list to make it easy to construct one
// that uses a zone allocator.
// TODO(all): This should be renamed to ZoneList once we got rid of our own
// home-grown ZoneList that actually is a ZoneVector.
template <typename T>
class ZoneLinkedList : public std::list<T, ZoneAllocator<T>> {
 public:
  // Constructs an empty list.
  explicit ZoneLinkedList(Zone* zone)
      : std::list<T, ZoneAllocator<T>>(ZoneAllocator<T>(zone)) {}
};

// A wrapper subclass for std::forward_list to make it easy to construct one
// that uses a zone allocator.
template <typename T>
class ZoneForwardList : public std::forward_list<T, ZoneAllocator<T>> {
 public:
  // Constructs an empty list.
  explicit ZoneForwardList(Zone* zone)
      : std::forward_list<T, ZoneAllocator<T>>(ZoneAllocator<T>(zone)) {}
};

// A wrapper subclass for std::priority_queue to make it easy to construct one
// that uses a zone allocator.
template <typename T, typename Compare = std::less<T>>
class ZonePriorityQueue
    : public std::priority_queue<T, ZoneVector<T>, Compare> {
 public:
  // Constructs an empty list.
  explicit ZonePriorityQueue(Zone* zone)
      : std::priority_queue<T, ZoneVector<T>, Compare>(Compare(),
                                                       ZoneVector<T>(zone)) {}
};

// A wrapper subclass for std::queue to make it easy to construct one
// that uses a zone allocator.
template <typename T>
class ZoneQueue : public std::queue<T, ZoneDeque<T>> {
 public:
  // Constructs an empty queue.
  explicit ZoneQueue(Zone* zone)
      : std::queue<T, ZoneDeque<T>>(ZoneDeque<T>(zone)) {}
};

// A wrapper subclass for std::stack to make it easy to construct one that uses
// a zone allocator.
template <typename T>
class ZoneStack : public std::stack<T, ZoneDeque<T>> {
 public:
  // Constructs an empty stack.
  explicit ZoneStack(Zone* zone)
      : std::stack<T, ZoneDeque<T>>(ZoneDeque<T>(zone)) {}
};

// A wrapper subclass for std::set to make it easy to construct one that uses
// a zone allocator.
template <typename K, typename Compare = std::less<K>>
class ZoneSet : public std::set<K, Compare, ZoneAllocator<K>> {
 public:
  // Constructs an empty set.
  explicit ZoneSet(Zone* zone)
      : std::set<K, Compare, ZoneAllocator<K>>(Compare(),
                                               ZoneAllocator<K>(zone)) {}
};

// A wrapper subclass for std::multiset to make it easy to construct one that
// uses a zone allocator.
template <typename K, typename Compare = std::less<K>>
class ZoneMultiset : public std::multiset<K, Compare, ZoneAllocator<K>> {
 public:
  // Constructs an empty multiset.
  explicit ZoneMultiset(Zone* zone)
      : std::multiset<K, Compare, ZoneAllocator<K>>(Compare(),
                                                    ZoneAllocator<K>(zone)) {}
};

// A wrapper subclass for std::map to make it easy to construct one that uses
// a zone allocator.
template <typename K, typename V, typename Compare = std::less<K>>
class ZoneMap
    : public std::map<K, V, Compare, ZoneAllocator<std::pair<const K, V>>> {
 public:
  // Constructs an empty map.
  explicit ZoneMap(Zone* zone)
      : std::map<K, V, Compare, ZoneAllocator<std::pair<const K, V>>>(
            Compare(), ZoneAllocator<std::pair<const K, V>>(zone)) {}
};

// A wrapper subclass for std::unordered_map to make it easy to construct one
// that uses a zone allocator.
template <typename K, typename V, typename Hash = base::hash<K>,
          typename KeyEqual = std::equal_to<K>>
class ZoneUnorderedMap
    : public std::unordered_map<K, V, Hash, KeyEqual,
                                ZoneAllocator<std::pair<const K, V>>> {
 public:
  // Constructs an empty map.
  explicit ZoneUnorderedMap(Zone* zone, size_t bucket_count = 0)
      : std::unordered_map<K, V, Hash, KeyEqual,
                           ZoneAllocator<std::pair<const K, V>>>(
            bucket_count, Hash(), KeyEqual(),
            ZoneAllocator<std::pair<const K, V>>(zone)) {}
};

// A wrapper subclass for std::unordered_set to make it easy to construct one
// that uses a zone allocator.
template <typename K, typename Hash = base::hash<K>,
          typename KeyEqual = std::equal_to<K>>
class ZoneUnorderedSet
    : public std::unordered_set<K, Hash, KeyEqual, ZoneAllocator<K>> {
 public:
  // Constructs an empty set.
  explicit ZoneUnorderedSet(Zone* zone, size_t bucket_count = 0)
      : std::unordered_set<K, Hash, KeyEqual, ZoneAllocator<K>>(
            bucket_count, Hash(), KeyEqual(), ZoneAllocator<K>(zone)) {}
};

// A wrapper subclass for std::multimap to make it easy to construct one that
// uses a zone allocator.
template <typename K, typename V, typename Compare = std::less<K>>
class ZoneMultimap
    : public std::multimap<K, V, Compare,
                           ZoneAllocator<std::pair<const K, V>>> {
 public:
  // Constructs an empty multimap.
  explicit ZoneMultimap(Zone* zone)
      : std::multimap<K, V, Compare, ZoneAllocator<std::pair<const K, V>>>(
            Compare(), ZoneAllocator<std::pair<const K, V>>(zone)) {}
};

// A wrapper subclass for base::SmallVector to make it easy to construct one
// that uses a zone allocator.
template <typename T, size_t kSize>
class SmallZoneVector : public base::SmallVector<T, kSize, ZoneAllocator<T>> {
 public:
  // Constructs an empty small vector.
  explicit SmallZoneVector(Zone* zone)
      : base::SmallVector<T, kSize, ZoneAllocator<T>>(ZoneAllocator<T>(zone)) {}

  explicit SmallZoneVector(size_t size, Zone* zone)
      : base::SmallVector<T, kSize, ZoneAllocator<T>>(
            size, ZoneAllocator<T>(ZoneAllocator<T>(zone))) {}
};

// Used by SmallZoneMap below. Essentially a closure around placement-new of
// the "full" fallback ZoneMap. Called once SmallMap grows beyond kArraySize.
template <typename ZoneMap>
class ZoneMapInit {
 public:
  explicit ZoneMapInit(Zone* zone) : zone_(zone) {}
  void operator()(ZoneMap* map) const { new (map) ZoneMap(zone_); }

 private:
  Zone* zone_;
};

// A wrapper subclass for base::SmallMap to make it easy to construct one that
// uses a zone-allocated std::map as the fallback once the SmallMap outgrows
// its inline storage.
template <typename K, typename V, size_t kArraySize,
          typename Compare = std::less<K>, typename KeyEqual = std::equal_to<K>>
class SmallZoneMap
    : public base::SmallMap<ZoneMap<K, V, Compare>, kArraySize, KeyEqual,
                            ZoneMapInit<ZoneMap<K, V, Compare>>> {
 public:
  explicit SmallZoneMap(Zone* zone)
      : base::SmallMap<ZoneMap<K, V, Compare>, kArraySize, KeyEqual,
                       ZoneMapInit<ZoneMap<K, V, Compare>>>(
            ZoneMapInit<ZoneMap<K, V, Compare>>(zone)) {}
};

// A wrapper subclass for absl::flat_hash_map to make it easy to construct one
// that uses a zone allocator. If you want to use a user-defined type as key
// (K), you'll need to define a AbslHashValue function for it (see
// https://abseil.io/docs/cpp/guides/hash).
template <typename K, typename V,
          typename Hash = typename absl::flat_hash_map<K, V>::hasher,
          typename KeyEqual =
              typename absl::flat_hash_map<K, V, Hash>::key_equal>
class ZoneAbslFlatHashMap
    : public absl::flat_hash_map<K, V, Hash, KeyEqual,
                                 ZoneAllocator<std::pair<const K, V>>> {
 public:
  // Constructs an empty map.
  explicit ZoneAbslFlatHashMap(Zone* zone, size_t bucket_count = 0)
      : absl::flat_hash_map<K, V, Hash, KeyEqual,
                            ZoneAllocator<std::pair<const K, V>>>(
            bucket_count, Hash(), KeyEqual(),
            ZoneAllocator<std::pair<const K, V>>(zone)) {}
};

// A wrapper subclass for absl::flat_hash_set to make it easy to construct one
// that uses a zone allocator. If you want to use a user-defined type as key
// (K), you'll need to define a AbslHashValue function for it (see
// https://abseil.io/docs/cpp/guides/hash).
template <typename K, typename Hash = typename absl::flat_hash_set<K>::hasher,
          typename KeyEqual = typename absl::flat_hash_set<K, Hash>::key_equal>
class ZoneAbslFlatHashSet
    : public absl::flat_hash_set<K, Hash, KeyEqual, ZoneAllocator<K>> {
 public:
  // Constructs an empty map.
  explicit ZoneAbslFlatHashSet(Zone* zone, size_t bucket_count = 0)
      : absl::flat_hash_set<K, Hash, KeyEqual, ZoneAllocator<K>>(
            bucket_count, Hash(), KeyEqual(), ZoneAllocator<K>(zone)) {}
};

// A wrapper subclass for absl::btree_map to make it easy to construct one
// that uses a zone allocator. If you want to use a user-defined type as key
// (K), you'll need to define a AbslHashValue function for it (see
// https://abseil.io/docs/cpp/guides/hash).
template <typename K, typename V, typename Compare = std::less<K>>
class ZoneAbslBTreeMap
    : public absl::btree_map<K, V, Compare,
                             ZoneAllocator<std::pair<const K, V>>> {
 public:
  // Constructs an empty map.
  explicit ZoneAbslBTreeMap(Zone* zone)
      : absl::btree_map<K, V, Compare, ZoneAllocator<std::pair<const K, V>>>(
            ZoneAllocator<std::pair<const K, V>>(zone)) {}
};

// Typedefs to shorten commonly used vectors.
using IntVector = ZoneVector<int>;

}  // namespace internal
}  // namespace v8

#endif  // V8_ZONE_ZONE_CONTAINERS_H_
