// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_CONTAINER_UTILS_H_
#define V8_BASE_CONTAINER_UTILS_H_

#include <stddef.h>

#include <algorithm>
#include <iterator>
#include <optional>
#include <vector>

namespace v8::base {

// Returns true iff the {element} is found in the {container}.
template <typename C, typename T>
bool contains(const C& container, const T& element) {
  const auto e = std::end(container);
  return std::find(std::begin(container), e, element) != e;
}

// Returns the first index of {element} in {container}. Returns std::nullopt if
// {container} does not contain {element}.
template <typename C, typename T>
std::optional<size_t> index_of(const C& container, const T& element) {
  const auto b = std::begin(container);
  const auto e = std::end(container);
  if (auto it = std::find(b, e, element); it != e) {
    return {std::distance(b, it)};
  }
  return std::nullopt;
}

// Returns the index of the first element in {container} that satisfies
// {predicate}. Returns std::nullopt if no element satisfies {predicate}.
template <typename C, typename P>
std::optional<size_t> index_of_if(const C& container, const P& predicate) {
  const auto b = std::begin(container);
  const auto e = std::end(container);
  if (auto it = std::find_if(b, e, predicate); it != e) {
    return {std::distance(b, it)};
  }
  return std::nullopt;
}

// Removes {count} elements from {container} starting at {index}. If {count} is
// larger than the number of elements after {index}, all elements after {index}
// are removed. Returns the number of removed elements.
template <typename C>
inline size_t erase_at(C& container, size_t index, size_t count = 1) {
  // TODO(C++20): Replace with std::erase.
  if (std::size(container) <= index) return 0;
  auto start = std::begin(container) + index;
  count = std::min<size_t>(count, std::distance(start, std::end(container)));
  container.erase(start, start + count);
  return count;
}

// Removes all elements from {container} that satisfy {predicate}. Returns the
// number of removed elements.
// TODO(C++20): Replace with std::erase_if.
template <typename C, typename P>
inline size_t erase_if(C& container, const P& predicate) {
  auto it =
      std::remove_if(std::begin(container), std::end(container), predicate);
  auto count = std::distance(it, std::end(container));
  container.erase(it, std::end(container));
  return count;
}

// Helper for std::count_if.
template <typename C, typename P>
inline size_t count_if(const C& container, const P& predicate) {
  return std::count_if(std::begin(container), std::end(container), predicate);
}

// Helper for std::all_of.
template <typename C, typename P>
inline bool all_of(const C& container, const P& predicate) {
  return std::all_of(std::begin(container), std::end(container), predicate);
}
template <typename C>
inline bool all_of(const C& container) {
  return std::all_of(
      std::begin(container), std::end(container),
      [](const auto& value) { return static_cast<bool>(value); });
}

// Helper for std::any_of.
template <typename C, typename P>
inline bool any_of(const C& container, const P& predicate) {
  return std::any_of(std::begin(container), std::end(container), predicate);
}
template <typename C>
inline bool any_of(const C& container) {
  return std::any_of(
      std::begin(container), std::end(container),
      [](const auto& value) { return static_cast<bool>(value); });
}

// Helper for std::none_of.
template <typename C, typename P>
inline bool none_of(const C& container, const P& predicate) {
  return std::none_of(std::begin(container), std::end(container), predicate);
}

// Helper for std::sort.
template <typename C>
inline void sort(C& container) {
  std::sort(std::begin(container), std::end(container));
}
template <typename C, typename Comp>
inline void sort(C& container, Comp comp) {
  std::sort(std::begin(container), std::end(container), comp);
}

// Returns true iff all elements of {container} compare equal using operator==.
template <typename C>
inline bool all_equal(const C& container) {
  if (std::size(container) <= 1) return true;
  auto b = std::begin(container);
  const auto& value = *b;
  return std::all_of(++b, std::end(container),
                     [&](const auto& v) { return v == value; });
}

// Returns true iff all elements of {container} compare equal to {value} using
// operator==.
template <typename C, typename T>
inline bool all_equal(const C& container, const T& value) {
  return std::all_of(std::begin(container), std::end(container),
                     [&](const auto& v) { return v == value; });
}

// Appends to vector {v} all the elements in the range {std::begin(container)}
// and {std::end(container)}.
template <typename V, typename C>
inline void vector_append(V& v, const C& container) {
  v.insert(std::end(v), std::begin(container), std::end(container));
}

}  // namespace v8::base

#endif  // V8_BASE_CONTAINER_UTILS_H_
