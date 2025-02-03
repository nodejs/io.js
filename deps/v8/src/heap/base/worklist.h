// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_BASE_WORKLIST_H_
#define V8_HEAP_BASE_WORKLIST_H_

#include <cstddef>
#include <utility>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/memory.h"
#include "src/base/platform/mutex.h"

namespace heap::base {
namespace internal {

class V8_EXPORT_PRIVATE SegmentBase {
 public:
  static SegmentBase* GetSentinelSegmentAddress();

  explicit constexpr SegmentBase(uint16_t capacity) : capacity_(capacity) {}

  size_t Size() const { return index_; }
  size_t Capacity() const { return capacity_; }
  bool IsEmpty() const { return index_ == 0; }
  bool IsFull() const { return index_ == capacity_; }
  void Clear() { index_ = 0; }

 protected:
  const uint16_t capacity_;
  uint16_t index_ = 0;
};
}  // namespace internal

class V8_EXPORT_PRIVATE WorklistBase final {
 public:
  // Enforces predictable order of push/pop sequences in single-threaded mode.
  static void EnforcePredictableOrder();
  static bool PredictableOrder() { return predictable_order_; }

 private:
  static bool predictable_order_;
};

// A global worklist based on segments which allows for a thread-local
// producer/consumer pattern with global work stealing.
//
// - Entries in the worklist are of type `EntryType`.
// - Segments have a capacity of at least `MinSegmentSize` but possibly more.
//
// All methods on the worklist itself are safe for concurrent usage but only
// consider published segments. Unpublished work in views using `Local` is not
// visible.
template <typename EntryType, uint16_t MinSegmentSize>
class Worklist final {
 public:
  // A thread-local view on the worklist. Any work that is not published from
  // the local view is not visible to the global worklist.
  class Local;
  class Segment;

  static constexpr int kMinSegmentSize = MinSegmentSize;

  Worklist() = default;
  ~Worklist() { CHECK(IsEmpty()); }

  Worklist(const Worklist&) = delete;
  Worklist& operator=(const Worklist&) = delete;

  // Returns true if the global worklist is empty and false otherwise. May be
  // read concurrently for an approximation.
  bool IsEmpty() const;
  // Returns the number of segments in the global worklist. May be read
  // concurrently for an approximation.
  size_t Size() const;

  // Moves the segments from `other` into this worklist, leaving behind `other`
  // as empty.
  void Merge(Worklist<EntryType, MinSegmentSize>& other);

  // Removes all segments from the worklist.
  void Clear();

  // Invokes `callback` on each item. Callback is of type `bool(EntryType&)` and
  // should return true if the entry should be kept and false if the entry
  // should be removed.
  template <typename Callback>
  void Update(Callback callback);

  // Invokes `callback` on each item. Callback is of type `void(EntryType&)`.
  template <typename Callback>
  void Iterate(Callback callback) const;

 private:
  void Push(Segment* segment);
  bool Pop(Segment** segment);

  mutable v8::base::Mutex lock_;
  Segment* top_ = nullptr;
  std::atomic<size_t> size_{0};
};

template <typename EntryType, uint16_t MinSegmentSize>
void Worklist<EntryType, MinSegmentSize>::Push(Segment* segment) {
  DCHECK(!segment->IsEmpty());
  v8::base::MutexGuard guard(&lock_);
  segment->set_next(top_);
  top_ = segment;
  size_.fetch_add(1, std::memory_order_relaxed);
}

template <typename EntryType, uint16_t MinSegmentSize>
bool Worklist<EntryType, MinSegmentSize>::Pop(Segment** segment) {
  v8::base::MutexGuard guard(&lock_);
  if (top_ == nullptr) return false;
  DCHECK_LT(0U, size_);
  size_.fetch_sub(1, std::memory_order_relaxed);
  *segment = top_;
  top_ = top_->next();
  return true;
}

template <typename EntryType, uint16_t MinSegmentSize>
bool Worklist<EntryType, MinSegmentSize>::IsEmpty() const {
  return Size() == 0;
}

template <typename EntryType, uint16_t MinSegmentSize>
size_t Worklist<EntryType, MinSegmentSize>::Size() const {
  // It is safe to read |size_| without a lock since this variable is
  // atomic, keeping in mind that threads may not immediately see the new
  // value when it is updated.
  return size_.load(std::memory_order_relaxed);
}

template <typename EntryType, uint16_t MinSegmentSize>
void Worklist<EntryType, MinSegmentSize>::Clear() {
  v8::base::MutexGuard guard(&lock_);
  size_.store(0, std::memory_order_relaxed);
  Segment* current = top_;
  while (current != nullptr) {
    Segment* tmp = current;
    current = current->next();
    Segment::Delete(tmp);
  }
  top_ = nullptr;
}

template <typename EntryType, uint16_t MinSegmentSize>
template <typename Callback>
void Worklist<EntryType, MinSegmentSize>::Update(Callback callback) {
  v8::base::MutexGuard guard(&lock_);
  Segment* prev = nullptr;
  Segment* current = top_;
  size_t num_deleted = 0;
  while (current != nullptr) {
    current->Update(callback);
    if (current->IsEmpty()) {
      DCHECK_LT(0U, size_);
      ++num_deleted;
      if (prev == nullptr) {
        top_ = current->next();
      } else {
        prev->set_next(current->next());
      }
      Segment* tmp = current;
      current = current->next();
      Segment::Delete(tmp);
    } else {
      prev = current;
      current = current->next();
    }
  }
  size_.fetch_sub(num_deleted, std::memory_order_relaxed);
}

template <typename EntryType, uint16_t MinSegmentSize>
template <typename Callback>
void Worklist<EntryType, MinSegmentSize>::Iterate(Callback callback) const {
  v8::base::MutexGuard guard(&lock_);
  for (Segment* current = top_; current != nullptr; current = current->next()) {
    current->Iterate(callback);
  }
}

template <typename EntryType, uint16_t MinSegmentSize>
void Worklist<EntryType, MinSegmentSize>::Merge(
    Worklist<EntryType, MinSegmentSize>& other) {
  Segment* other_top;
  size_t other_size;
  {
    v8::base::MutexGuard guard(&other.lock_);
    if (!other.top_) return;

    other_top = std::exchange(other.top_, nullptr);
    other_size = other.size_.exchange(0, std::memory_order_relaxed);
  }

  // It's safe to iterate through these segments because the top was
  // extracted from `other`.
  Segment* end = other_top;
  while (end->next()) end = end->next();

  {
    v8::base::MutexGuard guard(&lock_);
    size_.fetch_add(other_size, std::memory_order_relaxed);
    end->set_next(top_);
    top_ = other_top;
  }
}

template <typename EntryType, uint16_t MinSegmentSize>
class Worklist<EntryType, MinSegmentSize>::Segment final
    : public internal::SegmentBase {
 public:
  static Segment* Create(uint16_t min_segment_size) {
    const auto wanted_bytes = MallocSizeForCapacity(min_segment_size);
    v8::base::AllocationResult<char*> result;
    if (WorklistBase::PredictableOrder()) {
      result.ptr = static_cast<char*>(v8::base::Malloc(wanted_bytes));
      result.count = wanted_bytes;
    } else {
      result = v8::base::AllocateAtLeast<char>(wanted_bytes);
    }
    CHECK_NOT_NULL(result.ptr);
    return new (result.ptr)
        Segment(CapacityForMallocSize(result.count * sizeof(char)));
  }

  static void Delete(Segment* segment) { v8::base::Free(segment); }

  V8_INLINE void Push(EntryType entry);
  V8_INLINE void Pop(EntryType* entry);

  template <typename Callback>
  void Update(Callback callback);
  template <typename Callback>
  void Iterate(Callback callback) const;

  Segment* next() const { return next_; }
  void set_next(Segment* segment) { next_ = segment; }

 private:
  static constexpr size_t MallocSizeForCapacity(size_t num_entries) {
    return sizeof(Segment) + sizeof(EntryType) * num_entries;
  }
  static constexpr size_t CapacityForMallocSize(size_t malloc_size) {
    return (malloc_size - sizeof(Segment)) / sizeof(EntryType);
  }

  constexpr explicit Segment(size_t capacity)
      : internal::SegmentBase(capacity) {}

  EntryType& entry(size_t index) {
    return reinterpret_cast<EntryType*>(this + 1)[index];
  }
  const EntryType& entry(size_t index) const {
    return reinterpret_cast<const EntryType*>(this + 1)[index];
  }

  Segment* next_ = nullptr;
};

template <typename EntryType, uint16_t MinSegmentSize>
void Worklist<EntryType, MinSegmentSize>::Segment::Push(EntryType e) {
  DCHECK(!IsFull());
  entry(index_++) = e;
}

template <typename EntryType, uint16_t MinSegmentSize>
void Worklist<EntryType, MinSegmentSize>::Segment::Pop(EntryType* e) {
  DCHECK(!IsEmpty());
  *e = entry(--index_);
}

template <typename EntryType, uint16_t MinSegmentSize>
template <typename Callback>
void Worklist<EntryType, MinSegmentSize>::Segment::Update(Callback callback) {
  size_t new_index = 0;
  for (size_t i = 0; i < index_; i++) {
    if (callback(entry(i), &entry(new_index))) {
      new_index++;
    }
  }
  index_ = new_index;
}

template <typename EntryType, uint16_t MinSegmentSize>
template <typename Callback>
void Worklist<EntryType, MinSegmentSize>::Segment::Iterate(
    Callback callback) const {
  for (size_t i = 0; i < index_; i++) {
    callback(entry(i));
  }
}

// A thread-local on a given worklist.
template <typename EntryType, uint16_t MinSegmentSize>
class Worklist<EntryType, MinSegmentSize>::Local final {
 public:
  using ItemType = EntryType;

  explicit Local(Worklist<EntryType, MinSegmentSize>& worklist);
  ~Local();

  // Moving needs to specify whether the `worklist_` pointer is preserved or
  // not.
  Local(Local&& other) V8_NOEXCEPT : worklist_(other.worklist_) {
    std::swap(push_segment_, other.push_segment_);
    std::swap(pop_segment_, other.pop_segment_);
  }
  Local& operator=(Local&&) V8_NOEXCEPT = delete;

  // Having multiple copies of the same local view may be unsafe.
  Local(const Local&) = delete;
  Local& operator=(const Local& other) = delete;

  V8_INLINE void Push(EntryType entry);
  V8_INLINE bool Pop(EntryType* entry);

  bool IsLocalAndGlobalEmpty() const;
  bool IsLocalEmpty() const;
  bool IsGlobalEmpty() const;

  size_t PushSegmentSize() const { return push_segment_->Size(); }

  void Publish();

  void Merge(Worklist<EntryType, MinSegmentSize>::Local& other);

  void Clear();

 private:
  void PublishPushSegment();
  void PublishPopSegment();
  bool StealPopSegment();

  Segment* NewSegment() const {
    // Bottleneck for filtering in crash dumps.
    return Segment::Create(MinSegmentSize);
  }
  void DeleteSegment(internal::SegmentBase* segment) const {
    if (segment == internal::SegmentBase::GetSentinelSegmentAddress()) return;
    Segment::Delete(static_cast<Segment*>(segment));
  }

  inline Segment* push_segment() {
    DCHECK_NE(internal::SegmentBase::GetSentinelSegmentAddress(),
              push_segment_);
    return static_cast<Segment*>(push_segment_);
  }
  inline const Segment* push_segment() const {
    DCHECK_NE(internal::SegmentBase::GetSentinelSegmentAddress(),
              push_segment_);
    return static_cast<const Segment*>(push_segment_);
  }

  inline Segment* pop_segment() {
    DCHECK_NE(internal::SegmentBase::GetSentinelSegmentAddress(), pop_segment_);
    return static_cast<Segment*>(pop_segment_);
  }
  inline const Segment* pop_segment() const {
    DCHECK_NE(internal::SegmentBase::GetSentinelSegmentAddress(), pop_segment_);
    return static_cast<const Segment*>(pop_segment_);
  }

  Worklist<EntryType, MinSegmentSize>& worklist_;
  internal::SegmentBase* push_segment_ = nullptr;
  internal::SegmentBase* pop_segment_ = nullptr;
};

template <typename EntryType, uint16_t MinSegmentSize>
Worklist<EntryType, MinSegmentSize>::Local::Local(
    Worklist<EntryType, MinSegmentSize>& worklist)
    : worklist_(worklist),
      push_segment_(internal::SegmentBase::GetSentinelSegmentAddress()),
      pop_segment_(internal::SegmentBase::GetSentinelSegmentAddress()) {}

template <typename EntryType, uint16_t MinSegmentSize>
Worklist<EntryType, MinSegmentSize>::Local::~Local() {
  CHECK_IMPLIES(push_segment_, push_segment_->IsEmpty());
  CHECK_IMPLIES(pop_segment_, pop_segment_->IsEmpty());
  DeleteSegment(push_segment_);
  DeleteSegment(pop_segment_);
}

template <typename EntryType, uint16_t MinSegmentSize>
void Worklist<EntryType, MinSegmentSize>::Local::Push(EntryType entry) {
  if (V8_UNLIKELY(push_segment_->IsFull())) {
    PublishPushSegment();
    push_segment_ = NewSegment();
  }
  push_segment()->Push(entry);
}

template <typename EntryType, uint16_t MinSegmentSize>
bool Worklist<EntryType, MinSegmentSize>::Local::Pop(EntryType* entry) {
  if (pop_segment_->IsEmpty()) {
    if (!push_segment_->IsEmpty()) {
      std::swap(push_segment_, pop_segment_);
    } else if (!StealPopSegment()) {
      return false;
    }
  }
  pop_segment()->Pop(entry);
  return true;
}

template <typename EntryType, uint16_t MinSegmentSize>
bool Worklist<EntryType, MinSegmentSize>::Local::IsLocalAndGlobalEmpty() const {
  return IsLocalEmpty() && IsGlobalEmpty();
}

template <typename EntryType, uint16_t MinSegmentSize>
bool Worklist<EntryType, MinSegmentSize>::Local::IsLocalEmpty() const {
  return push_segment_->IsEmpty() && pop_segment_->IsEmpty();
}

template <typename EntryType, uint16_t MinSegmentSize>
bool Worklist<EntryType, MinSegmentSize>::Local::IsGlobalEmpty() const {
  return worklist_.IsEmpty();
}

template <typename EntryType, uint16_t MinSegmentSize>
void Worklist<EntryType, MinSegmentSize>::Local::Publish() {
  if (!push_segment_->IsEmpty()) {
    PublishPushSegment();
    push_segment_ = internal::SegmentBase::GetSentinelSegmentAddress();
  }
  if (!pop_segment_->IsEmpty()) {
    PublishPopSegment();
    pop_segment_ = internal::SegmentBase::GetSentinelSegmentAddress();
  }
}

template <typename EntryType, uint16_t MinSegmentSize>
void Worklist<EntryType, MinSegmentSize>::Local::Merge(
    Worklist<EntryType, MinSegmentSize>::Local& other) {
  worklist_.Merge(other.worklist_);
}

template <typename EntryType, uint16_t MinSegmentSize>
void Worklist<EntryType, MinSegmentSize>::Local::PublishPushSegment() {
  if (push_segment_ != internal::SegmentBase::GetSentinelSegmentAddress())
    worklist_.Push(push_segment());
}

template <typename EntryType, uint16_t MinSegmentSize>
void Worklist<EntryType, MinSegmentSize>::Local::PublishPopSegment() {
  if (pop_segment_ != internal::SegmentBase::GetSentinelSegmentAddress())
    worklist_.Push(pop_segment());
}

template <typename EntryType, uint16_t MinSegmentSize>
bool Worklist<EntryType, MinSegmentSize>::Local::StealPopSegment() {
  if (worklist_.IsEmpty()) return false;
  Segment* new_segment = nullptr;
  if (worklist_.Pop(&new_segment)) {
    DeleteSegment(pop_segment_);
    pop_segment_ = new_segment;
    return true;
  }
  return false;
}

template <typename EntryType, uint16_t MinSegmentSize>
void Worklist<EntryType, MinSegmentSize>::Local::Clear() {
  push_segment_->Clear();
  pop_segment_->Clear();
}

}  // namespace heap::base

#endif  // V8_HEAP_BASE_WORKLIST_H_
