// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: fixed_array.h
// -----------------------------------------------------------------------------
//
// A `FixedArray<T>` represents a non-resizable array of `T` where the length of
// the array can be determined at run-time. It is a good replacement for
// non-standard and deprecated uses of `alloca()` and variable length arrays
// within the GCC extension. (See
// https://gcc.gnu.org/onlinedocs/gcc/Variable-Length.html).
//
// `FixedArray` allocates small arrays inline, keeping performance fast by
// avoiding heap operations. It also helps reduce the chances of
// accidentally overflowing your stack if large input is passed to
// your function.

#ifndef ABSL_CONTAINER_FIXED_ARRAY_H_
#define ABSL_CONTAINER_FIXED_ARRAY_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

#include "absl/algorithm/algorithm.h"
#include "absl/base/config.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/throw_delegate.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/container/internal/compressed_tuple.h"
#include "absl/memory/memory.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

constexpr static auto kFixedArrayUseDefault = static_cast<size_t>(-1);

// -----------------------------------------------------------------------------
// FixedArray
// -----------------------------------------------------------------------------
//
// A `FixedArray` provides a run-time fixed-size array, allocating a small array
// inline for efficiency.
//
// Most users should not specify the `N` template parameter and let `FixedArray`
// automatically determine the number of elements to store inline based on
// `sizeof(T)`. If `N` is specified, the `FixedArray` implementation will use
// inline storage for arrays with a length <= `N`.
//
// Note that a `FixedArray` constructed with a `size_type` argument will
// default-initialize its values by leaving trivially constructible types
// uninitialized (e.g. int, int[4], double), and others default-constructed.
// This matches the behavior of c-style arrays and `std::array`, but not
// `std::vector`.
template <typename T, size_t N = kFixedArrayUseDefault,
          typename A = std::allocator<T>>
class FixedArray {
  static_assert(!std::is_array<T>::value || std::extent<T>::value > 0,
                "Arrays with unknown bounds cannot be used with FixedArray.");

  static constexpr size_t kInlineBytesDefault = 256;

  using AllocatorTraits = std::allocator_traits<A>;
  // std::iterator_traits isn't guaranteed to be SFINAE-friendly until C++17,
  // but this seems to be mostly pedantic.
  template <typename Iterator>
  using EnableIfForwardIterator = absl::enable_if_t<std::is_convertible<
      typename std::iterator_traits<Iterator>::iterator_category,
      std::forward_iterator_tag>::value>;
  static constexpr bool NoexceptCopyable() {
    return std::is_nothrow_copy_constructible<StorageElement>::value &&
           absl::allocator_is_nothrow<allocator_type>::value;
  }
  static constexpr bool NoexceptMovable() {
    return std::is_nothrow_move_constructible<StorageElement>::value &&
           absl::allocator_is_nothrow<allocator_type>::value;
  }
  static constexpr bool DefaultConstructorIsNonTrivial() {
    return !absl::is_trivially_default_constructible<StorageElement>::value;
  }

 public:
  using allocator_type = typename AllocatorTraits::allocator_type;
  using value_type = typename AllocatorTraits::value_type;
  using pointer = typename AllocatorTraits::pointer;
  using const_pointer = typename AllocatorTraits::const_pointer;
  using reference = value_type&;
  using const_reference = const value_type&;
  using size_type = typename AllocatorTraits::size_type;
  using difference_type = typename AllocatorTraits::difference_type;
  using iterator = pointer;
  using const_iterator = const_pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  static constexpr size_type inline_elements =
      (N == kFixedArrayUseDefault ? kInlineBytesDefault / sizeof(value_type)
                                  : static_cast<size_type>(N));

  FixedArray(const FixedArray& other) noexcept(NoexceptCopyable())
      : FixedArray(other,
                   AllocatorTraits::select_on_container_copy_construction(
                       other.storage_.alloc())) {}

  FixedArray(const FixedArray& other,
             const allocator_type& a) noexcept(NoexceptCopyable())
      : FixedArray(other.begin(), other.end(), a) {}

  FixedArray(FixedArray&& other) noexcept(NoexceptMovable())
      : FixedArray(std::move(other), other.storage_.alloc()) {}

  FixedArray(FixedArray&& other,
             const allocator_type& a) noexcept(NoexceptMovable())
      : FixedArray(std::make_move_iterator(other.begin()),
                   std::make_move_iterator(other.end()), a) {}

  // Creates an array object that can store `n` elements.
  // Note that trivially constructible elements will be uninitialized.
  explicit FixedArray(size_type n, const allocator_type& a = allocator_type())
      : storage_(n, a) {
    if (DefaultConstructorIsNonTrivial()) {
      memory_internal::ConstructRange(storage_.alloc(), storage_.begin(),
                                      storage_.end());
    }
  }

  // Creates an array initialized with `n` copies of `val`.
  FixedArray(size_type n, const value_type& val,
             const allocator_type& a = allocator_type())
      : storage_(n, a) {
    memory_internal::ConstructRange(storage_.alloc(), storage_.begin(),
                                    storage_.end(), val);
  }

  // Creates an array initialized with the size and contents of `init_list`.
  FixedArray(std::initializer_list<value_type> init_list,
             const allocator_type& a = allocator_type())
      : FixedArray(init_list.begin(), init_list.end(), a) {}

  // Creates an array initialized with the elements from the input
  // range. The array's size will always be `std::distance(first, last)`.
  // REQUIRES: Iterator must be a forward_iterator or better.
  template <typename Iterator, EnableIfForwardIterator<Iterator>* = nullptr>
  FixedArray(Iterator first, Iterator last,
             const allocator_type& a = allocator_type())
      : storage_(std::distance(first, last), a) {
    memory_internal::CopyRange(storage_.alloc(), storage_.begin(), first, last);
  }

  ~FixedArray() noexcept {
    for (auto* cur = storage_.begin(); cur != storage_.end(); ++cur) {
      AllocatorTraits::destroy(storage_.alloc(), cur);
    }
  }

  // Assignments are deleted because they break the invariant that the size of a
  // `FixedArray` never changes.
  void operator=(FixedArray&&) = delete;
  void operator=(const FixedArray&) = delete;

  // FixedArray::size()
  //
  // Returns the length of the fixed array.
  size_type size() const { return storage_.size(); }

  // FixedArray::max_size()
  //
  // Returns the largest possible value of `std::distance(begin(), end())` for a
  // `FixedArray<T>`. This is equivalent to the most possible addressable bytes
  // over the number of bytes taken by T.
  constexpr size_type max_size() const {
    return (std::numeric_limits<difference_type>::max)() / sizeof(value_type);
  }

  // FixedArray::empty()
  //
  // Returns whether or not the fixed array is empty.
  bool empty() const { return size() == 0; }

  // FixedArray::memsize()
  //
  // Returns the memory size of the fixed array in bytes.
  size_t memsize() const { return size() * sizeof(value_type); }

  // FixedArray::data()
  //
  // Returns a const T* pointer to elements of the `FixedArray`. This pointer
  // can be used to access (but not modify) the contained elements.
  const_pointer data() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return AsValueType(storage_.begin());
  }

  // Overload of FixedArray::data() to return a T* pointer to elements of the
  // fixed array. This pointer can be used to access and modify the contained
  // elements.
  pointer data() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return AsValueType(storage_.begin());
  }

  // FixedArray::operator[]
  //
  // Returns a reference the ith element of the fixed array.
  // REQUIRES: 0 <= i < size()
  reference operator[](size_type i) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_HARDENING_ASSERT(i < size());
    return data()[i];
  }

  // Overload of FixedArray::operator()[] to return a const reference to the
  // ith element of the fixed array.
  // REQUIRES: 0 <= i < size()
  const_reference operator[](size_type i) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_HARDENING_ASSERT(i < size());
    return data()[i];
  }

  // FixedArray::at
  //
  // Bounds-checked access.  Returns a reference to the ith element of the fixed
  // array, or throws std::out_of_range
  reference at(size_type i) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      base_internal::ThrowStdOutOfRange("FixedArray::at failed bounds check");
    }
    return data()[i];
  }

  // Overload of FixedArray::at() to return a const reference to the ith element
  // of the fixed array.
  const_reference at(size_type i) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      base_internal::ThrowStdOutOfRange("FixedArray::at failed bounds check");
    }
    return data()[i];
  }

  // FixedArray::front()
  //
  // Returns a reference to the first element of the fixed array.
  reference front() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_HARDENING_ASSERT(!empty());
    return data()[0];
  }

  // Overload of FixedArray::front() to return a reference to the first element
  // of a fixed array of const values.
  const_reference front() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_HARDENING_ASSERT(!empty());
    return data()[0];
  }

  // FixedArray::back()
  //
  // Returns a reference to the last element of the fixed array.
  reference back() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_HARDENING_ASSERT(!empty());
    return data()[size() - 1];
  }

  // Overload of FixedArray::back() to return a reference to the last element
  // of a fixed array of const values.
  const_reference back() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_HARDENING_ASSERT(!empty());
    return data()[size() - 1];
  }

  // FixedArray::begin()
  //
  // Returns an iterator to the beginning of the fixed array.
  iterator begin() ABSL_ATTRIBUTE_LIFETIME_BOUND { return data(); }

  // Overload of FixedArray::begin() to return a const iterator to the
  // beginning of the fixed array.
  const_iterator begin() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return data(); }

  // FixedArray::cbegin()
  //
  // Returns a const iterator to the beginning of the fixed array.
  const_iterator cbegin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return begin();
  }

  // FixedArray::end()
  //
  // Returns an iterator to the end of the fixed array.
  iterator end() ABSL_ATTRIBUTE_LIFETIME_BOUND { return data() + size(); }

  // Overload of FixedArray::end() to return a const iterator to the end of the
  // fixed array.
  const_iterator end() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return data() + size();
  }

  // FixedArray::cend()
  //
  // Returns a const iterator to the end of the fixed array.
  const_iterator cend() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return end(); }

  // FixedArray::rbegin()
  //
  // Returns a reverse iterator from the end of the fixed array.
  reverse_iterator rbegin() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return reverse_iterator(end());
  }

  // Overload of FixedArray::rbegin() to return a const reverse iterator from
  // the end of the fixed array.
  const_reverse_iterator rbegin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return const_reverse_iterator(end());
  }

  // FixedArray::crbegin()
  //
  // Returns a const reverse iterator from the end of the fixed array.
  const_reverse_iterator crbegin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return rbegin();
  }

  // FixedArray::rend()
  //
  // Returns a reverse iterator from the beginning of the fixed array.
  reverse_iterator rend() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return reverse_iterator(begin());
  }

  // Overload of FixedArray::rend() for returning a const reverse iterator
  // from the beginning of the fixed array.
  const_reverse_iterator rend() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return const_reverse_iterator(begin());
  }

  // FixedArray::crend()
  //
  // Returns a reverse iterator from the beginning of the fixed array.
  const_reverse_iterator crend() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return rend();
  }

  // FixedArray::fill()
  //
  // Assigns the given `value` to all elements in the fixed array.
  void fill(const value_type& val) { std::fill(begin(), end(), val); }

  // Relational operators. Equality operators are elementwise using
  // `operator==`, while order operators order FixedArrays lexicographically.
  friend bool operator==(const FixedArray& lhs, const FixedArray& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
  }

  friend bool operator!=(const FixedArray& lhs, const FixedArray& rhs) {
    return !(lhs == rhs);
  }

  friend bool operator<(const FixedArray& lhs, const FixedArray& rhs) {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(),
                                        rhs.end());
  }

  friend bool operator>(const FixedArray& lhs, const FixedArray& rhs) {
    return rhs < lhs;
  }

  friend bool operator<=(const FixedArray& lhs, const FixedArray& rhs) {
    return !(rhs < lhs);
  }

  friend bool operator>=(const FixedArray& lhs, const FixedArray& rhs) {
    return !(lhs < rhs);
  }

  template <typename H>
  friend H AbslHashValue(H h, const FixedArray& v) {
    return H::combine(H::combine_contiguous(std::move(h), v.data(), v.size()),
                      v.size());
  }

 private:
  // StorageElement
  //
  // For FixedArrays with a C-style-array value_type, StorageElement is a POD
  // wrapper struct called StorageElementWrapper that holds the value_type
  // instance inside. This is needed for construction and destruction of the
  // entire array regardless of how many dimensions it has. For all other cases,
  // StorageElement is just an alias of value_type.
  //
  // Maintainer's Note: The simpler solution would be to simply wrap value_type
  // in a struct whether it's an array or not. That causes some paranoid
  // diagnostics to misfire, believing that 'data()' returns a pointer to a
  // single element, rather than the packed array that it really is.
  // e.g.:
  //
  //     FixedArray<char> buf(1);
  //     sprintf(buf.data(), "foo");
  //
  //     error: call to int __builtin___sprintf_chk(etc...)
  //     will always overflow destination buffer [-Werror]
  //
  template <typename OuterT, typename InnerT = absl::remove_extent_t<OuterT>,
            size_t InnerN = std::extent<OuterT>::value>
  struct StorageElementWrapper {
    InnerT array[InnerN];
  };

  using StorageElement =
      absl::conditional_t<std::is_array<value_type>::value,
                          StorageElementWrapper<value_type>, value_type>;

  static pointer AsValueType(pointer ptr) { return ptr; }
  static pointer AsValueType(StorageElementWrapper<value_type>* ptr) {
    return std::addressof(ptr->array);
  }

  static_assert(sizeof(StorageElement) == sizeof(value_type), "");
  static_assert(alignof(StorageElement) == alignof(value_type), "");

  class NonEmptyInlinedStorage {
   public:
    StorageElement* data() { return reinterpret_cast<StorageElement*>(buff_); }
    void AnnotateConstruct(size_type n);
    void AnnotateDestruct(size_type n);

#ifdef ABSL_HAVE_ADDRESS_SANITIZER
    void* RedzoneBegin() { return &redzone_begin_; }
    void* RedzoneEnd() { return &redzone_end_ + 1; }
#endif  // ABSL_HAVE_ADDRESS_SANITIZER

   private:
    ABSL_ADDRESS_SANITIZER_REDZONE(redzone_begin_);
    alignas(StorageElement) char buff_[sizeof(StorageElement[inline_elements])];
    ABSL_ADDRESS_SANITIZER_REDZONE(redzone_end_);
  };

  class EmptyInlinedStorage {
   public:
    StorageElement* data() { return nullptr; }
    void AnnotateConstruct(size_type) {}
    void AnnotateDestruct(size_type) {}
  };

  using InlinedStorage =
      absl::conditional_t<inline_elements == 0, EmptyInlinedStorage,
                          NonEmptyInlinedStorage>;

  // Storage
  //
  // An instance of Storage manages the inline and out-of-line memory for
  // instances of FixedArray. This guarantees that even when construction of
  // individual elements fails in the FixedArray constructor body, the
  // destructor for Storage will still be called and out-of-line memory will be
  // properly deallocated.
  //
  class Storage : public InlinedStorage {
   public:
    Storage(size_type n, const allocator_type& a)
        : size_alloc_(n, a), data_(InitializeData()) {}

    ~Storage() noexcept {
      if (UsingInlinedStorage(size())) {
        InlinedStorage::AnnotateDestruct(size());
      } else {
        AllocatorTraits::deallocate(alloc(), AsValueType(begin()), size());
      }
    }

    size_type size() const { return size_alloc_.template get<0>(); }
    StorageElement* begin() const { return data_; }
    StorageElement* end() const { return begin() + size(); }
    allocator_type& alloc() { return size_alloc_.template get<1>(); }
    const allocator_type& alloc() const {
      return size_alloc_.template get<1>();
    }

   private:
    static bool UsingInlinedStorage(size_type n) {
      return n <= inline_elements;
    }

#ifdef ABSL_HAVE_ADDRESS_SANITIZER
    ABSL_ATTRIBUTE_NOINLINE
#endif  // ABSL_HAVE_ADDRESS_SANITIZER
    StorageElement* InitializeData() {
      if (UsingInlinedStorage(size())) {
        InlinedStorage::AnnotateConstruct(size());
        return InlinedStorage::data();
      } else {
        return reinterpret_cast<StorageElement*>(
            AllocatorTraits::allocate(alloc(), size()));
      }
    }

    // `CompressedTuple` takes advantage of EBCO for stateless `allocator_type`s
    container_internal::CompressedTuple<size_type, allocator_type> size_alloc_;
    StorageElement* data_;
  };

  Storage storage_;
};

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
template <typename T, size_t N, typename A>
constexpr size_t FixedArray<T, N, A>::kInlineBytesDefault;

template <typename T, size_t N, typename A>
constexpr typename FixedArray<T, N, A>::size_type
    FixedArray<T, N, A>::inline_elements;
#endif

template <typename T, size_t N, typename A>
void FixedArray<T, N, A>::NonEmptyInlinedStorage::AnnotateConstruct(
    typename FixedArray<T, N, A>::size_type n) {
#ifdef ABSL_HAVE_ADDRESS_SANITIZER
  if (!n) return;
  ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(data(), RedzoneEnd(), RedzoneEnd(),
                                     data() + n);
  ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(RedzoneBegin(), data(), data(),
                                     RedzoneBegin());
#endif  // ABSL_HAVE_ADDRESS_SANITIZER
  static_cast<void>(n);  // Mark used when not in asan mode
}

template <typename T, size_t N, typename A>
void FixedArray<T, N, A>::NonEmptyInlinedStorage::AnnotateDestruct(
    typename FixedArray<T, N, A>::size_type n) {
#ifdef ABSL_HAVE_ADDRESS_SANITIZER
  if (!n) return;
  ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(data(), RedzoneEnd(), data() + n,
                                     RedzoneEnd());
  ABSL_ANNOTATE_CONTIGUOUS_CONTAINER(RedzoneBegin(), data(), RedzoneBegin(),
                                     data());
#endif  // ABSL_HAVE_ADDRESS_SANITIZER
  static_cast<void>(n);  // Mark used when not in asan mode
}
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_FIXED_ARRAY_H_
