//
// Copyright 2017 The Abseil Authors.
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
// File: string_view.h
// -----------------------------------------------------------------------------
//
// This file contains the definition of the `absl::string_view` class. A
// `string_view` points to a contiguous span of characters, often part or all of
// another `std::string`, double-quoted string literal, character array, or even
// another `string_view`.
//
// This `absl::string_view` abstraction is designed to be a drop-in
// replacement for the C++17 `std::string_view` abstraction.
#ifndef ABSL_STRINGS_STRING_VIEW_H_
#define ABSL_STRINGS_STRING_VIEW_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iosfwd>
#include <iterator>
#include <limits>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/config.h"
#include "absl/base/internal/throw_delegate.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"

#ifdef ABSL_USES_STD_STRING_VIEW

#include <string_view>  // IWYU pragma: export

namespace absl {
ABSL_NAMESPACE_BEGIN
using string_view = std::string_view;
ABSL_NAMESPACE_END
}  // namespace absl

#else  // ABSL_USES_STD_STRING_VIEW

#if ABSL_HAVE_BUILTIN(__builtin_memcmp) ||        \
    (defined(__GNUC__) && !defined(__clang__)) || \
    (defined(_MSC_VER) && _MSC_VER >= 1928)
#define ABSL_INTERNAL_STRING_VIEW_MEMCMP __builtin_memcmp
#else  // ABSL_HAVE_BUILTIN(__builtin_memcmp)
#define ABSL_INTERNAL_STRING_VIEW_MEMCMP memcmp
#endif  // ABSL_HAVE_BUILTIN(__builtin_memcmp)

namespace absl {
ABSL_NAMESPACE_BEGIN

// absl::string_view
//
// A `string_view` provides a lightweight view into the string data provided by
// a `std::string`, double-quoted string literal, character array, or even
// another `string_view`. A `string_view` does *not* own the string to which it
// points, and that data cannot be modified through the view.
//
// You can use `string_view` as a function or method parameter anywhere a
// parameter can receive a double-quoted string literal, `const char*`,
// `std::string`, or another `absl::string_view` argument with no need to copy
// the string data. Systematic use of `string_view` within function arguments
// reduces data copies and `strlen()` calls.
//
// Because of its small size, prefer passing `string_view` by value:
//
//   void MyFunction(absl::string_view arg);
//
// If circumstances require, you may also pass one by const reference:
//
//   void MyFunction(const absl::string_view& arg);  // not preferred
//
// Passing by value generates slightly smaller code for many architectures.
//
// In either case, the source data of the `string_view` must outlive the
// `string_view` itself.
//
// A `string_view` is also suitable for local variables if you know that the
// lifetime of the underlying object is longer than the lifetime of your
// `string_view` variable. However, beware of binding a `string_view` to a
// temporary value:
//
//   // BAD use of string_view: lifetime problem
//   absl::string_view sv = obj.ReturnAString();
//
//   // GOOD use of string_view: str outlives sv
//   std::string str = obj.ReturnAString();
//   absl::string_view sv = str;
//
// Due to lifetime issues, a `string_view` is sometimes a poor choice for a
// return value and usually a poor choice for a data member. If you do use a
// `string_view` this way, it is your responsibility to ensure that the object
// pointed to by the `string_view` outlives the `string_view`.
//
// A `string_view` may represent a whole string or just part of a string. For
// example, when splitting a string, `std::vector<absl::string_view>` is a
// natural data type for the output.
//
// For another example, a Cord is a non-contiguous, potentially very
// long string-like object.  The Cord class has an interface that iteratively
// provides string_view objects that point to the successive pieces of a Cord
// object.
//
// When constructed from a source which is NUL-terminated, the `string_view`
// itself will not include the NUL-terminator unless a specific size (including
// the NUL) is passed to the constructor. As a result, common idioms that work
// on NUL-terminated strings do not work on `string_view` objects. If you write
// code that scans a `string_view`, you must check its length rather than test
// for nul, for example. Note, however, that nuls may still be embedded within
// a `string_view` explicitly.
//
// You may create a null `string_view` in two ways:
//
//   absl::string_view sv;
//   absl::string_view sv(nullptr, 0);
//
// For the above, `sv.data() == nullptr`, `sv.length() == 0`, and
// `sv.empty() == true`. Also, if you create a `string_view` with a non-null
// pointer then `sv.data() != nullptr`. Thus, you can use `string_view()` to
// signal an undefined value that is different from other `string_view` values
// in a similar fashion to how `const char* p1 = nullptr;` is different from
// `const char* p2 = "";`. However, in practice, it is not recommended to rely
// on this behavior.
//
// Be careful not to confuse a null `string_view` with an empty one. A null
// `string_view` is an empty `string_view`, but some empty `string_view`s are
// not null. Prefer checking for emptiness over checking for null.
//
// There are many ways to create an empty string_view:
//
//   const char* nullcp = nullptr;
//   // string_view.size() will return 0 in all cases.
//   absl::string_view();
//   absl::string_view(nullcp, 0);
//   absl::string_view("");
//   absl::string_view("", 0);
//   absl::string_view("abcdef", 0);
//   absl::string_view("abcdef" + 6, 0);
//
// All empty `string_view` objects whether null or not, are equal:
//
//   absl::string_view() == absl::string_view("", 0)
//   absl::string_view(nullptr, 0) == absl::string_view("abcdef"+6, 0)
class ABSL_ATTRIBUTE_VIEW string_view {
 public:
  using traits_type = std::char_traits<char>;
  using value_type = char;
  using pointer = absl::Nullable<char*>;
  using const_pointer = absl::Nullable<const char*>;
  using reference = char&;
  using const_reference = const char&;
  using const_iterator = absl::Nullable<const char*>;
  using iterator = const_iterator;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reverse_iterator = const_reverse_iterator;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;
  using absl_internal_is_view = std::true_type;

  static constexpr size_type npos = static_cast<size_type>(-1);

  // Null `string_view` constructor
  constexpr string_view() noexcept : ptr_(nullptr), length_(0) {}

  // Implicit constructors

  template <typename Allocator>
  string_view(  // NOLINT(runtime/explicit)
      const std::basic_string<char, std::char_traits<char>, Allocator>& str
          ABSL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      // This is implemented in terms of `string_view(p, n)` so `str.size()`
      // doesn't need to be reevaluated after `ptr_` is set.
      // The length check is also skipped since it is unnecessary and causes
      // code bloat.
      : string_view(str.data(), str.size(), SkipCheckLengthTag{}) {}

  // Implicit constructor of a `string_view` from NUL-terminated `str`. When
  // accepting possibly null strings, use `absl::NullSafeStringView(str)`
  // instead (see below).
  // The length check is skipped since it is unnecessary and causes code bloat.
  constexpr string_view(  // NOLINT(runtime/explicit)
      absl::Nonnull<const char*> str)
      : ptr_(str), length_(str ? StrlenInternal(str) : 0) {}

  // Constructor of a `string_view` from a `const char*` and length.
  constexpr string_view(absl::Nullable<const char*> data, size_type len)
      : ptr_(data), length_(CheckLengthInternal(len)) {}

  constexpr string_view(const string_view&) noexcept = default;
  string_view& operator=(const string_view&) noexcept = default;

  // Iterators

  // string_view::begin()
  //
  // Returns an iterator pointing to the first character at the beginning of the
  // `string_view`, or `end()` if the `string_view` is empty.
  constexpr const_iterator begin() const noexcept { return ptr_; }

  // string_view::end()
  //
  // Returns an iterator pointing just beyond the last character at the end of
  // the `string_view`. This iterator acts as a placeholder; attempting to
  // access it results in undefined behavior.
  constexpr const_iterator end() const noexcept { return ptr_ + length_; }

  // string_view::cbegin()
  //
  // Returns a const iterator pointing to the first character at the beginning
  // of the `string_view`, or `end()` if the `string_view` is empty.
  constexpr const_iterator cbegin() const noexcept { return begin(); }

  // string_view::cend()
  //
  // Returns a const iterator pointing just beyond the last character at the end
  // of the `string_view`. This pointer acts as a placeholder; attempting to
  // access its element results in undefined behavior.
  constexpr const_iterator cend() const noexcept { return end(); }

  // string_view::rbegin()
  //
  // Returns a reverse iterator pointing to the last character at the end of the
  // `string_view`, or `rend()` if the `string_view` is empty.
  const_reverse_iterator rbegin() const noexcept {
    return const_reverse_iterator(end());
  }

  // string_view::rend()
  //
  // Returns a reverse iterator pointing just before the first character at the
  // beginning of the `string_view`. This pointer acts as a placeholder;
  // attempting to access its element results in undefined behavior.
  const_reverse_iterator rend() const noexcept {
    return const_reverse_iterator(begin());
  }

  // string_view::crbegin()
  //
  // Returns a const reverse iterator pointing to the last character at the end
  // of the `string_view`, or `crend()` if the `string_view` is empty.
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }

  // string_view::crend()
  //
  // Returns a const reverse iterator pointing just before the first character
  // at the beginning of the `string_view`. This pointer acts as a placeholder;
  // attempting to access its element results in undefined behavior.
  const_reverse_iterator crend() const noexcept { return rend(); }

  // Capacity Utilities

  // string_view::size()
  //
  // Returns the number of characters in the `string_view`.
  constexpr size_type size() const noexcept { return length_; }

  // string_view::length()
  //
  // Returns the number of characters in the `string_view`. Alias for `size()`.
  constexpr size_type length() const noexcept { return size(); }

  // string_view::max_size()
  //
  // Returns the maximum number of characters the `string_view` can hold.
  constexpr size_type max_size() const noexcept { return kMaxSize; }

  // string_view::empty()
  //
  // Checks if the `string_view` is empty (refers to no characters).
  constexpr bool empty() const noexcept { return length_ == 0; }

  // string_view::operator[]
  //
  // Returns the ith element of the `string_view` using the array operator.
  // Note that this operator does not perform any bounds checking.
  constexpr const_reference operator[](size_type i) const {
    ABSL_HARDENING_ASSERT(i < size());
    return ptr_[i];
  }

  // string_view::at()
  //
  // Returns the ith element of the `string_view`. Bounds checking is performed,
  // and an exception of type `std::out_of_range` will be thrown on invalid
  // access.
  constexpr const_reference at(size_type i) const {
    if (ABSL_PREDICT_FALSE(i >= size())) {
      base_internal::ThrowStdOutOfRange("absl::string_view::at");
    }
    return ptr_[i];
  }

  // string_view::front()
  //
  // Returns the first element of a `string_view`.
  constexpr const_reference front() const {
    ABSL_HARDENING_ASSERT(!empty());
    return ptr_[0];
  }

  // string_view::back()
  //
  // Returns the last element of a `string_view`.
  constexpr const_reference back() const {
    ABSL_HARDENING_ASSERT(!empty());
    return ptr_[size() - 1];
  }

  // string_view::data()
  //
  // Returns a pointer to the underlying character array (which is of course
  // stored elsewhere). Note that `string_view::data()` may contain embedded nul
  // characters, but the returned buffer may or may not be NUL-terminated;
  // therefore, do not pass `data()` to a routine that expects a NUL-terminated
  // string.
  constexpr const_pointer data() const noexcept { return ptr_; }

  // Modifiers

  // string_view::remove_prefix()
  //
  // Removes the first `n` characters from the `string_view`. Note that the
  // underlying string is not changed, only the view.
  constexpr void remove_prefix(size_type n) {
    ABSL_HARDENING_ASSERT(n <= length_);
    ptr_ += n;
    length_ -= n;
  }

  // string_view::remove_suffix()
  //
  // Removes the last `n` characters from the `string_view`. Note that the
  // underlying string is not changed, only the view.
  constexpr void remove_suffix(size_type n) {
    ABSL_HARDENING_ASSERT(n <= length_);
    length_ -= n;
  }

  // string_view::swap()
  //
  // Swaps this `string_view` with another `string_view`.
  constexpr void swap(string_view& s) noexcept {
    auto t = *this;
    *this = s;
    s = t;
  }

  // Explicit conversion operators

  // Converts to `std::basic_string`.
  template <typename A>
  explicit operator std::basic_string<char, traits_type, A>() const {
    if (!data()) return {};
    return std::basic_string<char, traits_type, A>(data(), size());
  }

  // string_view::copy()
  //
  // Copies the contents of the `string_view` at offset `pos` and length `n`
  // into `buf`.
  size_type copy(char* buf, size_type n, size_type pos = 0) const {
    if (ABSL_PREDICT_FALSE(pos > length_)) {
      base_internal::ThrowStdOutOfRange("absl::string_view::copy");
    }
    size_type rlen = (std::min)(length_ - pos, n);
    if (rlen > 0) {
      const char* start = ptr_ + pos;
      traits_type::copy(buf, start, rlen);
    }
    return rlen;
  }

  // string_view::substr()
  //
  // Returns a "substring" of the `string_view` (at offset `pos` and length
  // `n`) as another string_view. This function throws `std::out_of_bounds` if
  // `pos > size`.
  // Use absl::ClippedSubstr if you need a truncating substr operation.
  constexpr string_view substr(size_type pos = 0, size_type n = npos) const {
    if (ABSL_PREDICT_FALSE(pos > length_)) {
      base_internal::ThrowStdOutOfRange("absl::string_view::substr");
    }
    return string_view(ptr_ + pos, Min(n, length_ - pos));
  }

  // string_view::compare()
  //
  // Performs a lexicographical comparison between this `string_view` and
  // another `string_view` `x`, returning a negative value if `*this` is less
  // than `x`, 0 if `*this` is equal to `x`, and a positive value if `*this`
  // is greater than `x`.
  constexpr int compare(string_view x) const noexcept {
    return CompareImpl(length_, x.length_,
                       Min(length_, x.length_) == 0
                           ? 0
                           : ABSL_INTERNAL_STRING_VIEW_MEMCMP(
                                 ptr_, x.ptr_, Min(length_, x.length_)));
  }

  // Overload of `string_view::compare()` for comparing a substring of the
  // 'string_view` and another `absl::string_view`.
  constexpr int compare(size_type pos1, size_type count1, string_view v) const {
    return substr(pos1, count1).compare(v);
  }

  // Overload of `string_view::compare()` for comparing a substring of the
  // `string_view` and a substring of another `absl::string_view`.
  constexpr int compare(size_type pos1, size_type count1, string_view v,
                        size_type pos2, size_type count2) const {
    return substr(pos1, count1).compare(v.substr(pos2, count2));
  }

  // Overload of `string_view::compare()` for comparing a `string_view` and a
  // a different C-style string `s`.
  constexpr int compare(absl::Nonnull<const char*> s) const {
    return compare(string_view(s));
  }

  // Overload of `string_view::compare()` for comparing a substring of the
  // `string_view` and a different string C-style string `s`.
  constexpr int compare(size_type pos1, size_type count1,
                        absl::Nonnull<const char*> s) const {
    return substr(pos1, count1).compare(string_view(s));
  }

  // Overload of `string_view::compare()` for comparing a substring of the
  // `string_view` and a substring of a different C-style string `s`.
  constexpr int compare(size_type pos1, size_type count1,
                        absl::Nonnull<const char*> s, size_type count2) const {
    return substr(pos1, count1).compare(string_view(s, count2));
  }

  // Find Utilities

  // string_view::find()
  //
  // Finds the first occurrence of the substring `s` within the `string_view`,
  // returning the position of the first character's match, or `npos` if no
  // match was found.
  size_type find(string_view s, size_type pos = 0) const noexcept;

  // Overload of `string_view::find()` for finding the given character `c`
  // within the `string_view`.
  size_type find(char c, size_type pos = 0) const noexcept;

  // Overload of `string_view::find()` for finding a substring of a different
  // C-style string `s` within the `string_view`.
  size_type find(absl::Nonnull<const char*> s, size_type pos,
                 size_type count) const {
    return find(string_view(s, count), pos);
  }

  // Overload of `string_view::find()` for finding a different C-style string
  // `s` within the `string_view`.
  size_type find(absl::Nonnull<const char *> s, size_type pos = 0) const {
    return find(string_view(s), pos);
  }

  // string_view::rfind()
  //
  // Finds the last occurrence of a substring `s` within the `string_view`,
  // returning the position of the first character's match, or `npos` if no
  // match was found.
  size_type rfind(string_view s, size_type pos = npos) const noexcept;

  // Overload of `string_view::rfind()` for finding the last given character `c`
  // within the `string_view`.
  size_type rfind(char c, size_type pos = npos) const noexcept;

  // Overload of `string_view::rfind()` for finding a substring of a different
  // C-style string `s` within the `string_view`.
  size_type rfind(absl::Nonnull<const char*> s, size_type pos,
                  size_type count) const {
    return rfind(string_view(s, count), pos);
  }

  // Overload of `string_view::rfind()` for finding a different C-style string
  // `s` within the `string_view`.
  size_type rfind(absl::Nonnull<const char*> s, size_type pos = npos) const {
    return rfind(string_view(s), pos);
  }

  // string_view::find_first_of()
  //
  // Finds the first occurrence of any of the characters in `s` within the
  // `string_view`, returning the start position of the match, or `npos` if no
  // match was found.
  size_type find_first_of(string_view s, size_type pos = 0) const noexcept;

  // Overload of `string_view::find_first_of()` for finding a character `c`
  // within the `string_view`.
  size_type find_first_of(char c, size_type pos = 0) const noexcept {
    return find(c, pos);
  }

  // Overload of `string_view::find_first_of()` for finding a substring of a
  // different C-style string `s` within the `string_view`.
  size_type find_first_of(absl::Nonnull<const char*> s, size_type pos,
                          size_type count) const {
    return find_first_of(string_view(s, count), pos);
  }

  // Overload of `string_view::find_first_of()` for finding a different C-style
  // string `s` within the `string_view`.
  size_type find_first_of(absl::Nonnull<const char*> s,
                          size_type pos = 0) const {
    return find_first_of(string_view(s), pos);
  }

  // string_view::find_last_of()
  //
  // Finds the last occurrence of any of the characters in `s` within the
  // `string_view`, returning the start position of the match, or `npos` if no
  // match was found.
  size_type find_last_of(string_view s, size_type pos = npos) const noexcept;

  // Overload of `string_view::find_last_of()` for finding a character `c`
  // within the `string_view`.
  size_type find_last_of(char c, size_type pos = npos) const noexcept {
    return rfind(c, pos);
  }

  // Overload of `string_view::find_last_of()` for finding a substring of a
  // different C-style string `s` within the `string_view`.
  size_type find_last_of(absl::Nonnull<const char*> s, size_type pos,
                         size_type count) const {
    return find_last_of(string_view(s, count), pos);
  }

  // Overload of `string_view::find_last_of()` for finding a different C-style
  // string `s` within the `string_view`.
  size_type find_last_of(absl::Nonnull<const char*> s,
                         size_type pos = npos) const {
    return find_last_of(string_view(s), pos);
  }

  // string_view::find_first_not_of()
  //
  // Finds the first occurrence of any of the characters not in `s` within the
  // `string_view`, returning the start position of the first non-match, or
  // `npos` if no non-match was found.
  size_type find_first_not_of(string_view s, size_type pos = 0) const noexcept;

  // Overload of `string_view::find_first_not_of()` for finding a character
  // that is not `c` within the `string_view`.
  size_type find_first_not_of(char c, size_type pos = 0) const noexcept;

  // Overload of `string_view::find_first_not_of()` for finding a substring of a
  // different C-style string `s` within the `string_view`.
  size_type find_first_not_of(absl::Nonnull<const char*> s, size_type pos,
                              size_type count) const {
    return find_first_not_of(string_view(s, count), pos);
  }

  // Overload of `string_view::find_first_not_of()` for finding a different
  // C-style string `s` within the `string_view`.
  size_type find_first_not_of(absl::Nonnull<const char*> s,
                              size_type pos = 0) const {
    return find_first_not_of(string_view(s), pos);
  }

  // string_view::find_last_not_of()
  //
  // Finds the last occurrence of any of the characters not in `s` within the
  // `string_view`, returning the start position of the last non-match, or
  // `npos` if no non-match was found.
  size_type find_last_not_of(string_view s,
                             size_type pos = npos) const noexcept;

  // Overload of `string_view::find_last_not_of()` for finding a character
  // that is not `c` within the `string_view`.
  size_type find_last_not_of(char c, size_type pos = npos) const noexcept;

  // Overload of `string_view::find_last_not_of()` for finding a substring of a
  // different C-style string `s` within the `string_view`.
  size_type find_last_not_of(absl::Nonnull<const char*> s, size_type pos,
                             size_type count) const {
    return find_last_not_of(string_view(s, count), pos);
  }

  // Overload of `string_view::find_last_not_of()` for finding a different
  // C-style string `s` within the `string_view`.
  size_type find_last_not_of(absl::Nonnull<const char*> s,
                             size_type pos = npos) const {
    return find_last_not_of(string_view(s), pos);
  }

#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L
  // string_view::starts_with()
  //
  // Returns true if the `string_view` starts with the prefix `s`.
  //
  // This method only exists when targeting at least C++20.
  // If support for C++ prior to C++20 is required, use `absl::StartsWith()`
  // from `//absl/strings/match.h` for compatibility.
  constexpr bool starts_with(string_view s) const noexcept {
    return s.empty() ||
           (size() >= s.size() &&
            ABSL_INTERNAL_STRING_VIEW_MEMCMP(data(), s.data(), s.size()) == 0);
  }

  // Overload of `string_view::starts_with()` that returns true if `c` is the
  // first character of the `string_view`.
  constexpr bool starts_with(char c) const noexcept {
    return !empty() && front() == c;
  }

  // Overload of `string_view::starts_with()` that returns true if the
  // `string_view` starts with the C-style prefix `s`.
  constexpr bool starts_with(const char* s) const {
    return starts_with(string_view(s));
  }

  // string_view::ends_with()
  //
  // Returns true if the `string_view` ends with the suffix `s`.
  //
  // This method only exists when targeting at least C++20.
  // If support for C++ prior to C++20 is required, use `absl::EndsWith()`
  // from `//absl/strings/match.h` for compatibility.
  constexpr bool ends_with(string_view s) const noexcept {
    return s.empty() || (size() >= s.size() && ABSL_INTERNAL_STRING_VIEW_MEMCMP(
                                                   data() + (size() - s.size()),
                                                   s.data(), s.size()) == 0);
  }

  // Overload of `string_view::ends_with()` that returns true if `c` is the
  // last character of the `string_view`.
  constexpr bool ends_with(char c) const noexcept {
    return !empty() && back() == c;
  }

  // Overload of `string_view::ends_with()` that returns true if the
  // `string_view` ends with the C-style suffix `s`.
  constexpr bool ends_with(const char* s) const {
    return ends_with(string_view(s));
  }
#endif  // ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L

 private:
  // The constructor from std::string delegates to this constructor.
  // See the comment on that constructor for the rationale.
  struct SkipCheckLengthTag {};
  string_view(absl::Nullable<const char*> data, size_type len,
              SkipCheckLengthTag) noexcept
      : ptr_(data), length_(len) {}

  static constexpr size_type kMaxSize =
      (std::numeric_limits<difference_type>::max)();

  static constexpr size_type CheckLengthInternal(size_type len) {
    ABSL_HARDENING_ASSERT(len <= kMaxSize);
    return len;
  }

  static constexpr size_type StrlenInternal(absl::Nonnull<const char*> str) {
#if defined(_MSC_VER) && !defined(__clang__)
    // MSVC 2017+ can evaluate this at compile-time.
    const char* begin = str;
    while (*str != '\0') ++str;
    return str - begin;
#elif ABSL_HAVE_BUILTIN(__builtin_strlen) || \
    (defined(__GNUC__) && !defined(__clang__))
    // GCC has __builtin_strlen according to
    // https://gcc.gnu.org/onlinedocs/gcc-4.7.0/gcc/Other-Builtins.html, but
    // ABSL_HAVE_BUILTIN doesn't detect that, so we use the extra checks above.
    // __builtin_strlen is constexpr.
    return __builtin_strlen(str);
#else
    return str ? strlen(str) : 0;
#endif
  }

  static constexpr size_t Min(size_type length_a, size_type length_b) {
    return length_a < length_b ? length_a : length_b;
  }

  static constexpr int CompareImpl(size_type length_a, size_type length_b,
                                   int compare_result) {
    return compare_result == 0 ? static_cast<int>(length_a > length_b) -
                                     static_cast<int>(length_a < length_b)
                               : (compare_result < 0 ? -1 : 1);
  }

  absl::Nullable<const char*> ptr_;
  size_type length_;
};

// This large function is defined inline so that in a fairly common case where
// one of the arguments is a literal, the compiler can elide a lot of the
// following comparisons.
constexpr bool operator==(string_view x, string_view y) noexcept {
  return x.size() == y.size() &&
         (x.empty() ||
          ABSL_INTERNAL_STRING_VIEW_MEMCMP(x.data(), y.data(), x.size()) == 0);
}

constexpr bool operator!=(string_view x, string_view y) noexcept {
  return !(x == y);
}

constexpr bool operator<(string_view x, string_view y) noexcept {
  return x.compare(y) < 0;
}

constexpr bool operator>(string_view x, string_view y) noexcept {
  return y < x;
}

constexpr bool operator<=(string_view x, string_view y) noexcept {
  return !(y < x);
}

constexpr bool operator>=(string_view x, string_view y) noexcept {
  return !(x < y);
}

// IO Insertion Operator
std::ostream& operator<<(std::ostream& o, string_view piece);

ABSL_NAMESPACE_END
}  // namespace absl

#undef ABSL_INTERNAL_STRING_VIEW_MEMCMP

#endif  // ABSL_USES_STD_STRING_VIEW

namespace absl {
ABSL_NAMESPACE_BEGIN

// ClippedSubstr()
//
// Like `s.substr(pos, n)`, but clips `pos` to an upper bound of `s.size()`.
// Provided because std::string_view::substr throws if `pos > size()`
inline string_view ClippedSubstr(string_view s, size_t pos,
                                 size_t n = string_view::npos) {
  pos = (std::min)(pos, static_cast<size_t>(s.size()));
  return s.substr(pos, n);
}

// NullSafeStringView()
//
// Creates an `absl::string_view` from a pointer `p` even if it's null-valued.
// This function should be used where an `absl::string_view` can be created from
// a possibly-null pointer.
constexpr string_view NullSafeStringView(absl::Nullable<const char*> p) {
  return p ? string_view(p) : string_view();
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_STRING_VIEW_H_
