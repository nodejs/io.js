// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_VECTOR_H_
#define V8_BASE_VECTOR_H_

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <type_traits>

#include "src/base/functional.h"
#include "src/base/logging.h"
#include "src/base/macros.h"

namespace v8 {
namespace base {

template <typename T>
class Vector {
 public:
  using value_type = T;
  using iterator = T*;
  using const_iterator = const T*;

  constexpr Vector() : start_(nullptr), length_(0) {}

  constexpr Vector(T* data, size_t length) : start_(data), length_(length) {
    DCHECK(length == 0 || data != nullptr);
  }

  static Vector<T> New(size_t length) {
    return Vector<T>(new T[length], length);
  }

  // Returns a vector using the same backing storage as this one,
  // spanning from and including 'from', to but not including 'to'.
  Vector<T> SubVector(size_t from, size_t to) const {
    DCHECK_LE(from, to);
    DCHECK_LE(to, length_);
    return Vector<T>(begin() + from, to - from);
  }
  Vector<T> SubVectorFrom(size_t from) const {
    return SubVector(from, length_);
  }

  template <class U>
  void OverwriteWith(Vector<U> other) {
    DCHECK_EQ(size(), other.size());
    std::copy(other.begin(), other.end(), begin());
  }

  template <class U, size_t n>
  void OverwriteWith(const std::array<U, n>& other) {
    DCHECK_EQ(size(), other.size());
    std::copy(other.begin(), other.end(), begin());
  }

  // Returns the length of the vector. Only use this if you really need an
  // integer return value. Use {size()} otherwise.
  int length() const {
    DCHECK_GE(std::numeric_limits<int>::max(), length_);
    return static_cast<int>(length_);
  }

  // Returns the length of the vector as a size_t.
  constexpr size_t size() const { return length_; }

  // Returns whether or not the vector is empty.
  constexpr bool empty() const { return length_ == 0; }

  // Access individual vector elements - checks bounds in debug mode.
  T& operator[](size_t index) const {
    DCHECK_LT(index, length_);
    return start_[index];
  }

  const T& at(size_t index) const { return operator[](index); }

  T& first() { return start_[0]; }
  const T& first() const { return start_[0]; }

  T& last() {
    DCHECK_LT(0, length_);
    return start_[length_ - 1];
  }
  const T& last() const {
    DCHECK_LT(0, length_);
    return start_[length_ - 1];
  }

  // Returns a pointer to the start of the data in the vector.
  constexpr T* begin() const { return start_; }
  constexpr const T* cbegin() const { return start_; }

  // For consistency with other containers, do also provide a {data} accessor.
  constexpr T* data() const { return start_; }

  // Returns a pointer past the end of the data in the vector.
  constexpr T* end() const { return start_ + length_; }
  constexpr const T* cend() const { return start_ + length_; }

  constexpr std::reverse_iterator<T*> rbegin() const {
    return std::make_reverse_iterator(end());
  }
  constexpr std::reverse_iterator<T*> rend() const {
    return std::make_reverse_iterator(begin());
  }

  // Returns a clone of this vector with a new backing store.
  Vector<T> Clone() const {
    T* result = new T[length_];
    for (size_t i = 0; i < length_; i++) result[i] = start_[i];
    return Vector<T>(result, length_);
  }

  void Truncate(size_t length) {
    DCHECK(length <= length_);
    length_ = length;
  }

  // Releases the array underlying this vector. Once disposed the
  // vector is empty.
  void Dispose() {
    delete[] start_;
    start_ = nullptr;
    length_ = 0;
  }

  Vector<T> operator+(size_t offset) {
    DCHECK_LE(offset, length_);
    return Vector<T>(start_ + offset, length_ - offset);
  }

  Vector<T> operator+=(size_t offset) {
    DCHECK_LE(offset, length_);
    start_ += offset;
    length_ -= offset;
    return *this;
  }

  // Implicit conversion from Vector<T> to Vector<const T>.
  operator Vector<const T>() const { return {start_, length_}; }

  template <typename S>
  static Vector<T> cast(Vector<S> input) {
    // Casting is potentially dangerous, so be really restrictive here. This
    // might be lifted once we have use cases for that.
    static_assert(std::is_trivial_v<S> && std::is_standard_layout_v<S>);
    static_assert(std::is_trivial_v<T> && std::is_standard_layout_v<T>);
    DCHECK_EQ(0, (input.size() * sizeof(S)) % sizeof(T));
    DCHECK_EQ(0, reinterpret_cast<uintptr_t>(input.begin()) % alignof(T));
    return Vector<T>(reinterpret_cast<T*>(input.begin()),
                     input.size() * sizeof(S) / sizeof(T));
  }

  bool operator==(const Vector<T>& other) const {
    return std::equal(begin(), end(), other.begin(), other.end());
  }

  bool operator!=(const Vector<T>& other) const {
    return !operator==(other);
  }

  template<typename TT = T>
  std::enable_if_t<!std::is_const_v<TT>, bool> operator==(
      const Vector<const T>& other) const {
    return std::equal(begin(), end(), other.begin(), other.end());
  }

  template<typename TT = T>
  std::enable_if_t<!std::is_const_v<TT>, bool> operator!=(
      const Vector<const T>& other) const {
    return !operator==(other);
  }

 private:
  T* start_;
  size_t length_;
};

template <typename T>
V8_INLINE size_t hash_value(base::Vector<T> v) {
  return hash_range(v.begin(), v.end());
}

template <typename T>
class V8_NODISCARD ScopedVector : public Vector<T> {
 public:
  explicit ScopedVector(size_t length) : Vector<T>(new T[length], length) {}
  ~ScopedVector() { delete[] this->begin(); }

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(ScopedVector);
};

template <typename T>
class OwnedVector {
 public:
  OwnedVector() = default;

  OwnedVector(std::unique_ptr<T[]> data, size_t length)
      : data_(std::move(data)), length_(length) {
    DCHECK_IMPLIES(length_ > 0, data_ != nullptr);
  }

  // Disallow copying.
  OwnedVector(const OwnedVector&) = delete;
  OwnedVector& operator=(const OwnedVector&) = delete;

  // Move construction and move assignment from {OwnedVector<U>} to
  // {OwnedVector<T>}, instantiable if {std::unique_ptr<U>} can be converted to
  // {std::unique_ptr<T>}. Can also be used to convert {OwnedVector<T>} to
  // {OwnedVector<const T>}.
  // These also function as the standard move construction/assignment operator.
  // {other} is left as an empty vector.
  template <typename U,
            typename = typename std::enable_if<std::is_convertible<
                std::unique_ptr<U>, std::unique_ptr<T>>::value>::type>
  OwnedVector(OwnedVector<U>&& other) V8_NOEXCEPT {
    *this = std::move(other);
  }

  template <typename U,
            typename = typename std::enable_if<std::is_convertible<
                std::unique_ptr<U>, std::unique_ptr<T>>::value>::type>
  OwnedVector& operator=(OwnedVector<U>&& other) V8_NOEXCEPT {
    static_assert(sizeof(U) == sizeof(T));
    data_ = std::move(other.data_);
    length_ = other.length_;
    DCHECK_NULL(other.data_);
    other.length_ = 0;
    return *this;
  }

  // Returns the length of the vector as a size_t.
  constexpr size_t size() const { return length_; }

  // Returns whether or not the vector is empty.
  constexpr bool empty() const { return length_ == 0; }

  constexpr T* begin() const {
    DCHECK_IMPLIES(length_ > 0, data_ != nullptr);
    return data_.get();
  }

  constexpr T* end() const { return begin() + length_; }

  // In addition to {begin}, do provide a {data()} accessor for API
  // compatibility with other sequential containers.
  constexpr T* data() const { return begin(); }

  constexpr std::reverse_iterator<T*> rbegin() const {
    return std::make_reverse_iterator(end());
  }
  constexpr std::reverse_iterator<T*> rend() const {
    return std::make_reverse_iterator(begin());
  }

  // Access individual vector elements - checks bounds in debug mode.
  T& operator[](size_t index) const {
    DCHECK_LT(index, length_);
    return data_[index];
  }

  // Returns a {Vector<T>} view of the data in this vector.
  Vector<T> as_vector() const { return {begin(), size()}; }

  // Releases the backing data from this vector and transfers ownership to the
  // caller. This vector will be empty afterwards.
  std::unique_ptr<T[]> ReleaseData() {
    length_ = 0;
    return std::move(data_);
  }

  // Allocates a new vector of the specified size via the default allocator.
  // Elements in the new vector are value-initialized.
  static OwnedVector<T> New(size_t size) {
    if (size == 0) return {};
    return OwnedVector<T>(std::make_unique<T[]>(size), size);
  }

  // Allocates a new vector of the specified size via the default allocator.
  // Elements in the new vector are default-initialized.
  static OwnedVector<T> NewForOverwrite(size_t size) {
    if (size == 0) return {};
    // TODO(v8): Use {std::make_unique_for_overwrite} once we allow C++20.
    return OwnedVector<T>(std::unique_ptr<T[]>(new T[size]), size);
  }

  // Allocates a new vector containing the specified collection of values.
  // {Iterator} is the common type of {std::begin} and {std::end} called on a
  // {const U&}. This function is only instantiable if that type exists.
  template <typename U, typename Iterator = typename std::common_type<
                            decltype(std::begin(std::declval<const U&>())),
                            decltype(std::end(std::declval<const U&>()))>::type>
  static OwnedVector<T> Of(const U& collection) {
    Iterator begin = std::begin(collection);
    Iterator end = std::end(collection);
    using non_const_t = typename std::remove_const<T>::type;
    auto vec =
        OwnedVector<non_const_t>::NewForOverwrite(std::distance(begin, end));
    std::copy(begin, end, vec.begin());
    return vec;
  }

  bool operator==(std::nullptr_t) const { return data_ == nullptr; }
  bool operator!=(std::nullptr_t) const { return data_ != nullptr; }

 private:
  template <typename U>
  friend class OwnedVector;

  std::unique_ptr<T[]> data_;
  size_t length_ = 0;
};

// The vectors returned by {StaticCharVector}, {CStrVector}, or {OneByteVector}
// do not contain a null-termination byte. If you want the null byte, use
// {ArrayVector}.

// Known length, constexpr.
template <size_t N>
constexpr Vector<const char> StaticCharVector(const char (&array)[N]) {
  return {array, N - 1};
}

// Unknown length, not constexpr.
inline Vector<const char> CStrVector(const char* data) {
  return {data, strlen(data)};
}

// OneByteVector is never constexpr because the data pointer is
// {reinterpret_cast}ed.
inline Vector<const uint8_t> OneByteVector(const char* data, size_t length) {
  return {reinterpret_cast<const uint8_t*>(data), length};
}

inline Vector<const uint8_t> OneByteVector(const char* data) {
  return OneByteVector(data, strlen(data));
}

template <size_t N>
Vector<const uint8_t> StaticOneByteVector(const char (&array)[N]) {
  return OneByteVector(array, N - 1);
}

// For string literals, ArrayVector("foo") returns a vector ['f', 'o', 'o', \0]
// with length 4 and null-termination.
// If you want ['f', 'o', 'o'], use CStrVector("foo").
template <typename T, size_t N>
inline constexpr Vector<T> ArrayVector(T (&arr)[N]) {
  return {arr, N};
}

// Construct a Vector from a start pointer and a size.
template <typename T>
inline constexpr Vector<T> VectorOf(T* start, size_t size) {
  return {start, size};
}

// Construct a Vector from anything compatible with std::data and std::size (ie,
// an array, or a container providing a {data()} and {size()} accessor).
template <typename Container>
inline constexpr auto VectorOf(Container&& c)
    -> decltype(VectorOf(std::data(c), std::size(c))) {
  return VectorOf(std::data(c), std::size(c));
}

// Construct a Vector from an initializer list. The vector can obviously only be
// used as long as the initializer list is live. Valid uses include direct use
// in parameter lists: F(VectorOf({1, 2, 3}));
template <typename T>
inline constexpr Vector<const T> VectorOf(std::initializer_list<T> list) {
  return VectorOf(list.begin(), list.size());
}

template <typename T, size_t kSize>
class EmbeddedVector : public Vector<T> {
 public:
  EmbeddedVector() : Vector<T>(buffer_, kSize) {}
  explicit EmbeddedVector(const T& initial_value) : Vector<T>(buffer_, kSize) {
    std::fill_n(buffer_, kSize, initial_value);
  }
  EmbeddedVector(const EmbeddedVector&) = delete;
  EmbeddedVector& operator=(const EmbeddedVector&) = delete;

 private:
  T buffer_[kSize];
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_VECTOR_H_
