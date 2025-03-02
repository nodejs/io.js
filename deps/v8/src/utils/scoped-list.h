// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_SCOPED_LIST_H_
#define V8_UTILS_SCOPED_LIST_H_

#include <type_traits>
#include <vector>

#include "src/base/logging.h"

namespace v8 {

namespace base {
template <typename T>
class Vector;
}  // namespace base

namespace internal {

template <typename T>
class ZoneList;

// ScopedList is a scope-lifetime list with a std::vector backing that can be
// reused between ScopedLists. Note that a ScopedList in an outer scope cannot
// add any entries if there is a ScopedList with the same backing in an inner
// scope.
template <typename T, typename TBacking = T>
class V8_NODISCARD ScopedList final {
  // The backing can either be the same type as the list type, or, for pointers,
  // we additionally allow a void* backing store.
  static_assert((std::is_same<TBacking, T>::value) ||
                    (std::is_same<TBacking, void*>::value &&
                     std::is_pointer<T>::value),
                "Incompatible combination of T and TBacking types");

 public:
  explicit ScopedList(std::vector<TBacking>* buffer)
      : buffer_(*buffer), start_(buffer->size()), end_(buffer->size()) {}

  ~ScopedList() { Rewind(); }

  void Rewind() {
    DCHECK_EQ(buffer_.size(), end_);
    buffer_.resize(start_);
    end_ = start_;
  }

  void MergeInto(ScopedList* parent) {
    DCHECK_EQ(parent->end_, start_);
    parent->end_ = end_;
    start_ = end_;
    DCHECK_EQ(0, length());
  }

  int length() const { return static_cast<int>(end_ - start_); }

  const T& at(int i) const {
    size_t index = start_ + i;
    DCHECK_LE(start_, index);
    DCHECK_LT(index, buffer_.size());
    return *reinterpret_cast<T*>(&buffer_[index]);
  }

  T& at(int i) {
    size_t index = start_ + i;
    DCHECK_LE(start_, index);
    DCHECK_LT(index, buffer_.size());
    return *reinterpret_cast<T*>(&buffer_[index]);
  }

  base::Vector<const T> ToConstVector() const {
    T* data = reinterpret_cast<T*>(buffer_.data() + start_);
    return base::Vector<const T>(data, length());
  }

  void Add(const T& value) {
    DCHECK_EQ(buffer_.size(), end_);
    buffer_.push_back(value);
    ++end_;
  }

  void AddAll(base::Vector<const T> list) {
    DCHECK_EQ(buffer_.size(), end_);
    buffer_.reserve(buffer_.size() + list.length());
    for (int i = 0; i < list.length(); i++) {
      buffer_.push_back(list.at(i));
    }
    end_ += list.length();
  }

  using iterator = T*;
  using const_iterator = const T*;

  inline iterator begin() {
    return reinterpret_cast<T*>(buffer_.data() + start_);
  }
  inline const_iterator begin() const {
    return reinterpret_cast<T*>(buffer_.data() + start_);
  }

  inline iterator end() { return reinterpret_cast<T*>(buffer_.data() + end_); }
  inline const_iterator end() const {
    return reinterpret_cast<T*>(buffer_.data() + end_);
  }

 private:
  std::vector<TBacking>& buffer_;
  size_t start_;
  size_t end_;
};

template <typename T>
using ScopedPtrList = ScopedList<T*, void*>;

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_SCOPED_LIST_H_
