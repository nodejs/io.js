// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_SMALL_VECTOR_H_
#define V8_BASE_SMALL_VECTOR_H_

#include <algorithm>
#include <type_traits>
#include <utility>

#include "src/base/bits.h"
#include "src/base/macros.h"
#include "src/base/vector.h"

namespace v8 {
namespace base {

// Minimal SmallVector implementation. Uses inline storage first, switches to
// dynamic storage when it overflows.
template <typename T, size_t kSize, typename Allocator = std::allocator<T>>
class SmallVector {
  // Currently only support trivially copyable and trivially destructible data
  // types, as it uses memcpy to copy elements and never calls destructors.
  ASSERT_TRIVIALLY_COPYABLE(T);
  static_assert(std::is_trivially_destructible<T>::value);

 public:
  static constexpr size_t kInlineSize = kSize;
  using value_type = T;

  SmallVector() = default;
  explicit SmallVector(const Allocator& allocator) : allocator_(allocator) {}
  explicit V8_INLINE SmallVector(size_t size,
                                 const Allocator& allocator = Allocator())
      : allocator_(allocator) {
    resize_no_init(size);
  }
  SmallVector(const SmallVector& other) V8_NOEXCEPT
      : allocator_(other.allocator_) {
    *this = other;
  }
  SmallVector(const SmallVector& other, const Allocator& allocator) V8_NOEXCEPT
      : allocator_(allocator) {
    *this = other;
  }
  SmallVector(SmallVector&& other) V8_NOEXCEPT
      : allocator_(std::move(other.allocator_)) {
    *this = std::move(other);
  }
  SmallVector(SmallVector&& other, const Allocator& allocator) V8_NOEXCEPT
      : allocator_(allocator) {
    *this = std::move(other);
  }
  V8_INLINE SmallVector(std::initializer_list<T> init,
                        const Allocator& allocator = Allocator())
      : SmallVector(init.size(), allocator) {
    memcpy(begin_, init.begin(), sizeof(T) * init.size());
  }
  explicit V8_INLINE SmallVector(base::Vector<const T> init,
                                 const Allocator& allocator = Allocator())
      : SmallVector(init.size(), allocator) {
    memcpy(begin_, init.begin(), sizeof(T) * init.size());
  }

  ~SmallVector() {
    static_assert(std::is_trivially_destructible_v<T>);
    if (is_big()) FreeDynamicStorage();
  }

  SmallVector& operator=(const SmallVector& other) V8_NOEXCEPT {
    if (this == &other) return *this;
    size_t other_size = other.size();
    if (capacity() < other_size) {
      // Create large-enough heap-allocated storage.
      if (is_big()) FreeDynamicStorage();
      begin_ = AllocateDynamicStorage(other_size);
      end_of_storage_ = begin_ + other_size;
    }
    memcpy(begin_, other.begin_, sizeof(T) * other_size);
    end_ = begin_ + other_size;
    return *this;
  }

  SmallVector& operator=(SmallVector&& other) V8_NOEXCEPT {
    if (this == &other) return *this;
    if (other.is_big()) {
      if (is_big()) FreeDynamicStorage();
      begin_ = other.begin_;
      end_ = other.end_;
      end_of_storage_ = other.end_of_storage_;
    } else {
      DCHECK_GE(capacity(), other.size());  // Sanity check.
      size_t other_size = other.size();
      memcpy(begin_, other.begin_, sizeof(T) * other_size);
      end_ = begin_ + other_size;
    }
    other.reset_to_inline_storage();
    return *this;
  }

  T* data() { return begin_; }
  const T* data() const { return begin_; }

  T* begin() { return begin_; }
  const T* begin() const { return begin_; }

  T* end() { return end_; }
  const T* end() const { return end_; }

  auto rbegin() { return std::make_reverse_iterator(end_); }
  auto rbegin() const { return std::make_reverse_iterator(end_); }

  auto rend() { return std::make_reverse_iterator(begin_); }
  auto rend() const { return std::make_reverse_iterator(begin_); }

  size_t size() const { return end_ - begin_; }
  bool empty() const { return end_ == begin_; }
  size_t capacity() const { return end_of_storage_ - begin_; }

  T& front() {
    DCHECK_NE(0, size());
    return begin_[0];
  }
  const T& front() const {
    DCHECK_NE(0, size());
    return begin_[0];
  }

  T& back() {
    DCHECK_NE(0, size());
    return end_[-1];
  }
  const T& back() const {
    DCHECK_NE(0, size());
    return end_[-1];
  }

  T& operator[](size_t index) {
    DCHECK_GT(size(), index);
    return begin_[index];
  }

  const T& at(size_t index) const {
    DCHECK_GT(size(), index);
    return begin_[index];
  }

  const T& operator[](size_t index) const { return at(index); }

  template <typename... Args>
  void emplace_back(Args&&... args) {
    if (V8_UNLIKELY(end_ == end_of_storage_)) Grow();
    void* storage = end_;
    end_ += 1;
    new (storage) T(std::forward<Args>(args)...);
  }

  void push_back(T x) { emplace_back(std::move(x)); }

  void pop_back(size_t count = 1) {
    DCHECK_GE(size(), count);
    end_ -= count;
  }

  T* insert(T* pos, const T& value) { return insert(pos, 1, value); }
  T* insert(T* pos, size_t count, const T& value) {
    DCHECK_LE(pos, end_);
    size_t offset = pos - begin_;
    size_t old_size = size();
    resize_no_init(old_size + count);
    pos = begin_ + offset;
    T* old_end = begin_ + old_size;
    DCHECK_LE(old_end, end_);
    std::move_backward(pos, old_end, end_);
    std::fill_n(pos, count, value);
    return pos;
  }
  template <typename It>
  T* insert(T* pos, It begin, It end) {
    DCHECK_LE(pos, end_);
    size_t offset = pos - begin_;
    size_t count = std::distance(begin, end);
    size_t old_size = size();
    resize_no_init(old_size + count);
    pos = begin_ + offset;
    T* old_end = begin_ + old_size;
    DCHECK_LE(old_end, end_);
    std::move_backward(pos, old_end, end_);
    std::copy(begin, end, pos);
    return pos;
  }

  T* insert(T* pos, std::initializer_list<T> values) {
    return insert(pos, values.begin(), values.end());
  }

  void resize_no_init(size_t new_size) {
    // Resizing without initialization is safe if T is trivially copyable.
    ASSERT_TRIVIALLY_COPYABLE(T);
    if (new_size > capacity()) Grow(new_size);
    end_ = begin_ + new_size;
  }

  void resize_and_init(size_t new_size, const T& initial_value = {}) {
    static_assert(std::is_trivially_destructible_v<T>);
    if (new_size > capacity()) Grow(new_size);
    T* new_end = begin_ + new_size;
    if (new_end > end_) {
      std::uninitialized_fill(end_, new_end, initial_value);
    }
    end_ = new_end;
  }

  void reserve(size_t new_capacity) {
    if (new_capacity > capacity()) Grow(new_capacity);
  }

  // Clear without reverting back to inline storage.
  void clear() { end_ = begin_; }

  Allocator get_allocator() const { return allocator_; }

 private:
  // Grows the backing store by a factor of two. Returns the new end of the used
  // storage (this reduces binary size).
  V8_NOINLINE V8_PRESERVE_MOST void Grow() { Grow(0); }

  // Grows the backing store by a factor of two, and at least to {min_capacity}.
  V8_NOINLINE V8_PRESERVE_MOST void Grow(size_t min_capacity) {
    size_t in_use = end_ - begin_;
    size_t new_capacity =
        base::bits::RoundUpToPowerOfTwo(std::max(min_capacity, 2 * capacity()));
    T* new_storage = AllocateDynamicStorage(new_capacity);
    if (new_storage == nullptr) {
      FatalOOM(OOMType::kProcess, "base::SmallVector::Grow");
    }
    memcpy(new_storage, begin_, sizeof(T) * in_use);
    if (is_big()) FreeDynamicStorage();
    begin_ = new_storage;
    end_ = new_storage + in_use;
    end_of_storage_ = new_storage + new_capacity;
  }

  T* AllocateDynamicStorage(size_t number_of_elements) {
    return allocator_.allocate(number_of_elements);
  }

  V8_NOINLINE V8_PRESERVE_MOST void FreeDynamicStorage() {
    DCHECK(is_big());
    allocator_.deallocate(begin_, end_of_storage_ - begin_);
  }

  // Clear and go back to inline storage. Dynamic storage is *not* freed. For
  // internal use only.
  void reset_to_inline_storage() {
    begin_ = inline_storage_begin();
    end_ = begin_;
    end_of_storage_ = begin_ + kInlineSize;
  }

  bool is_big() const { return begin_ != inline_storage_begin(); }

  T* inline_storage_begin() { return reinterpret_cast<T*>(&inline_storage_); }
  const T* inline_storage_begin() const {
    return reinterpret_cast<const T*>(&inline_storage_);
  }

  V8_NO_UNIQUE_ADDRESS Allocator allocator_;

  T* begin_ = inline_storage_begin();
  T* end_ = begin_;
  T* end_of_storage_ = begin_ + kInlineSize;
  typename std::aligned_storage<sizeof(T) * kInlineSize, alignof(T)>::type
      inline_storage_;
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_SMALL_VECTOR_H_
