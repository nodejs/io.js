// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_MEMORY_SPAN_H_
#define INCLUDE_V8_MEMORY_SPAN_H_

#include <stddef.h>

#include <array>
#include <cstddef>
#include <iterator>
#include <type_traits>

#include "v8config.h"  // NOLINT(build/include_directory)

// TODO(pkasting): Use <compare>/spaceship unconditionally after dropping
// support for old libstdc++ versions.
#if __has_include(<version>)
#include <version>
#endif
#if defined(__cpp_lib_three_way_comparison) && \
    __cpp_lib_three_way_comparison >= 201711L
#define V8_HAVE_SPACESHIP_OPERATOR 1
#else
#define V8_HAVE_SPACESHIP_OPERATOR 0
#endif

// TODO(pkasting): Make this block unconditional after dropping support for old
// libstdc++ versions.
#if __has_include(<ranges>)
#include <ranges>

namespace v8 {

template <typename T>
class V8_EXPORT MemorySpan;

}  // namespace v8

// Mark `MemorySpan` as satisfying the `view` and `borrowed_range` concepts.
// This should be done before the definition of `MemorySpan`, so that any
// inlined calls to range functionality use the correct specializations.
template <typename T>
inline constexpr bool std::ranges::enable_view<v8::MemorySpan<T>> = true;
template <typename T>
inline constexpr bool std::ranges::enable_borrowed_range<v8::MemorySpan<T>> =
    true;
#endif

namespace v8 {

/**
 * Points to an unowned contiguous buffer holding a known number of elements.
 *
 * This is similar to std::span (under consideration for C++20), but does not
 * require advanced C++ support. In the (far) future, this may be replaced with
 * or aliased to std::span.
 *
 * To facilitate future migration, this class exposes a subset of the interface
 * implemented by std::span.
 */
template <typename T>
class V8_EXPORT MemorySpan {
 private:
  /** Some C++ machinery, brought from the future. */
  template <typename From, typename To>
  using is_array_convertible = std::is_convertible<From (*)[], To (*)[]>;
  template <typename From, typename To>
  static constexpr bool is_array_convertible_v =
      is_array_convertible<From, To>::value;

  template <typename It>
  using iter_reference_t = decltype(*std::declval<It&>());

  template <typename It, typename = void>
  struct is_compatible_iterator : std::false_type {};
  template <typename It>
  struct is_compatible_iterator<
      It,
      std::void_t<
          std::is_base_of<std::random_access_iterator_tag,
                          typename std::iterator_traits<It>::iterator_category>,
          is_array_convertible<std::remove_reference_t<iter_reference_t<It>>,
                               T>>> : std::true_type {};
  template <typename It>
  static constexpr bool is_compatible_iterator_v =
      is_compatible_iterator<It>::value;

  template <typename U>
  [[nodiscard]] static constexpr U* to_address(U* p) noexcept {
    return p;
  }

  template <typename It,
            typename = std::void_t<decltype(std::declval<It&>().operator->())>>
  [[nodiscard]] static constexpr auto to_address(It it) noexcept {
    return it.operator->();
  }

 public:
  /** The default constructor creates an empty span. */
  constexpr MemorySpan() = default;

  /** Constructor from nullptr and count, for backwards compatibility.
   * This is not compatible with C++20 std::span.
   */
  constexpr MemorySpan(std::nullptr_t, size_t) {}

  /** Constructor from "iterator" and count. */
  template <typename Iterator,
            std::enable_if_t<is_compatible_iterator_v<Iterator>, bool> = true>
  constexpr MemorySpan(Iterator first,
                       size_t count)  // NOLINT(runtime/explicit)
      : data_(to_address(first)), size_(count) {}

  /** Constructor from two "iterators". */
  template <typename Iterator,
            std::enable_if_t<is_compatible_iterator_v<Iterator> &&
                                 !std::is_convertible_v<Iterator, size_t>,
                             bool> = true>
  constexpr MemorySpan(Iterator first,
                       Iterator last)  // NOLINT(runtime/explicit)
      : data_(to_address(first)), size_(last - first) {}

  /** Implicit conversion from C-style array. */
  template <size_t N>
  constexpr MemorySpan(T (&a)[N]) noexcept  // NOLINT(runtime/explicit)
      : data_(a), size_(N) {}

  /** Implicit conversion from std::array. */
  template <typename U, size_t N,
            std::enable_if_t<is_array_convertible_v<U, T>, bool> = true>
  constexpr MemorySpan(
      std::array<U, N>& a) noexcept  // NOLINT(runtime/explicit)
      : data_(a.data()), size_{N} {}

  /** Implicit conversion from const std::array. */
  template <typename U, size_t N,
            std::enable_if_t<is_array_convertible_v<const U, T>, bool> = true>
  constexpr MemorySpan(
      const std::array<U, N>& a) noexcept  // NOLINT(runtime/explicit)
      : data_(a.data()), size_{N} {}

  /** Returns a pointer to the beginning of the buffer. */
  [[nodiscard]] constexpr T* data() const { return data_; }
  /** Returns the number of elements that the buffer holds. */
  [[nodiscard]] constexpr size_t size() const { return size_; }

  [[nodiscard]] constexpr T& operator[](size_t i) const { return data_[i]; }

  /** Returns true if the buffer is empty. */
  [[nodiscard]] constexpr bool empty() const { return size() == 0; }

  class Iterator {
   public:
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using pointer = value_type*;
    using reference = value_type&;
    using iterator_category = std::random_access_iterator_tag;
    // There seems to be no feature-test macro covering this, so use the
    // presence of `<ranges>` as a crude proxy, since it was added to the
    // standard as part of the Ranges papers.
    // TODO(pkasting): Add this unconditionally after dropping support for old
    // libstdc++ versions.
#if __has_include(<ranges>)
    using iterator_concept = std::contiguous_iterator_tag;
#endif

    // Required to satisfy `std::semiregular<>`.
    constexpr Iterator() = default;

    [[nodiscard]] friend constexpr bool operator==(const Iterator& a,
                                                   const Iterator& b) {
      // TODO(pkasting): Replace this body with `= default` after dropping
      // support for old gcc versions.
      return a.ptr_ == b.ptr_;
    }
#if V8_HAVE_SPACESHIP_OPERATOR
    [[nodiscard]] friend constexpr auto operator<=>(const Iterator&,
                                                    const Iterator&) = default;
#else
    // Assume that if spaceship isn't present, operator rewriting might not be
    // either.
    [[nodiscard]] friend constexpr bool operator!=(const Iterator& a,
                                                   const Iterator& b) {
      return a.ptr_ != b.ptr_;
    }

    [[nodiscard]] friend constexpr bool operator<(const Iterator& a,
                                                  const Iterator& b) {
      return a.ptr_ < b.ptr_;
    }
    [[nodiscard]] friend constexpr bool operator<=(const Iterator& a,
                                                   const Iterator& b) {
      return a.ptr_ <= b.ptr_;
    }
    [[nodiscard]] friend constexpr bool operator>(const Iterator& a,
                                                  const Iterator& b) {
      return a.ptr_ > b.ptr_;
    }
    [[nodiscard]] friend constexpr bool operator>=(const Iterator& a,
                                                   const Iterator& b) {
      return a.ptr_ >= b.ptr_;
    }
#endif

    constexpr Iterator& operator++() {
      ++ptr_;
      return *this;
    }

    constexpr Iterator operator++(int) {
      Iterator temp = *this;
      ++*this;
      return temp;
    }

    constexpr Iterator& operator--() {
      --ptr_;
      return *this;
    }

    constexpr Iterator operator--(int) {
      Iterator temp = *this;
      --*this;
      return temp;
    }

    constexpr Iterator& operator+=(difference_type rhs) {
      ptr_ += rhs;
      return *this;
    }

    [[nodiscard]] friend constexpr Iterator operator+(Iterator lhs,
                                                      difference_type rhs) {
      lhs += rhs;
      return lhs;
    }

    [[nodiscard]] friend constexpr Iterator operator+(difference_type lhs,
                                                      const Iterator& rhs) {
      return rhs + lhs;
    }

    constexpr Iterator& operator-=(difference_type rhs) {
      ptr_ -= rhs;
      return *this;
    }

    [[nodiscard]] friend constexpr Iterator operator-(Iterator lhs,
                                                      difference_type rhs) {
      lhs -= rhs;
      return lhs;
    }

    [[nodiscard]] friend constexpr difference_type operator-(
        const Iterator& lhs, const Iterator& rhs) {
      return lhs.ptr_ - rhs.ptr_;
    }

    [[nodiscard]] constexpr reference operator*() const { return *ptr_; }
    [[nodiscard]] constexpr pointer operator->() const { return ptr_; }
    [[nodiscard]] constexpr reference operator[](size_t offset) const {
      return ptr_[offset];
    }

   private:
    friend class MemorySpan<T>;

    constexpr explicit Iterator(T* ptr) : ptr_(ptr) {}

    T* ptr_ = nullptr;
  };

  [[nodiscard]] Iterator begin() const { return Iterator(data_); }
  [[nodiscard]] Iterator end() const { return Iterator(data_ + size_); }

 private:
  T* data_ = nullptr;
  size_t size_ = 0;
};

/**
 * Helper function template to create an array of fixed length, initialized by
 * the provided initializer list, without explicitly specifying the array size,
 * e.g.
 *
 *   auto arr = v8::to_array<Local<String>>({v8_str("one"), v8_str("two")});
 *
 * In the future, this may be replaced with or aliased to std::to_array (under
 * consideration for C++20).
 */

namespace detail {
template <class T, std::size_t N, std::size_t... I>
[[nodiscard]] constexpr std::array<std::remove_cv_t<T>, N> to_array_lvalue_impl(
    T (&a)[N], std::index_sequence<I...>) {
  return {{a[I]...}};
}

template <class T, std::size_t N, std::size_t... I>
[[nodiscard]] constexpr std::array<std::remove_cv_t<T>, N> to_array_rvalue_impl(
    T (&&a)[N], std::index_sequence<I...>) {
  return {{std::move(a[I])...}};
}
}  // namespace detail

template <class T, std::size_t N>
[[nodiscard]] constexpr std::array<std::remove_cv_t<T>, N> to_array(T (&a)[N]) {
  return detail::to_array_lvalue_impl(a, std::make_index_sequence<N>{});
}

template <class T, std::size_t N>
[[nodiscard]] constexpr std::array<std::remove_cv_t<T>, N> to_array(
    T (&&a)[N]) {
  return detail::to_array_rvalue_impl(std::move(a),
                                      std::make_index_sequence<N>{});
}

}  // namespace v8
#endif  // INCLUDE_V8_MEMORY_SPAN_H_
