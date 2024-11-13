// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_ENUM_SET_H_
#define V8_BASE_ENUM_SET_H_

#include <ostream>
#include <type_traits>

#include "src/base/bits.h"
#include "src/base/logging.h"

namespace v8 {
namespace base {

// A poor man's version of STL's bitset: A bit set of enums E (without explicit
// values), fitting into an integral type T.
template <class E, class T = int>
class EnumSet {
  static_assert(std::is_enum<E>::value, "EnumSet can only be used with enums");

 public:
  using StorageType = T;

  constexpr EnumSet() = default;

  constexpr EnumSet(std::initializer_list<E> init) {
    T bits = 0;
    for (E e : init) bits |= Mask(e);
    bits_ = bits;
  }

  constexpr bool empty() const { return bits_ == 0; }
  constexpr bool contains(E element) const {
    return (bits_ & Mask(element)) != 0;
  }
  constexpr bool contains_all(EnumSet set) const {
    return (bits_ & set.bits_) == set.bits_;
  }
  constexpr bool contains_any(EnumSet set) const {
    return (bits_ & set.bits_) != 0;
  }
  constexpr bool contains_only(E element) const {
    return bits_ == Mask(element);
  }
  constexpr bool is_subset_of(EnumSet set) const {
    return (bits_ & set.bits_) == bits_;
  }
  constexpr void Add(E element) { bits_ |= Mask(element); }
  constexpr void Add(EnumSet set) { bits_ |= set.bits_; }
  constexpr void Remove(E element) { bits_ &= ~Mask(element); }
  constexpr void Remove(EnumSet set) { bits_ &= ~set.bits_; }
  constexpr void RemoveAll() { bits_ = 0; }
  constexpr void Intersect(EnumSet set) { bits_ &= set.bits_; }
  constexpr T ToIntegral() const { return bits_; }

  constexpr EnumSet operator~() const { return EnumSet(~bits_); }

  constexpr bool operator==(EnumSet set) const { return bits_ == set.bits_; }
  constexpr bool operator!=(EnumSet set) const { return bits_ != set.bits_; }

  constexpr EnumSet operator|(EnumSet set) const {
    return EnumSet(bits_ | set.bits_);
  }
  constexpr EnumSet operator&(EnumSet set) const {
    return EnumSet(bits_ & set.bits_);
  }
  constexpr EnumSet operator-(EnumSet set) const {
    return EnumSet(bits_ & ~set.bits_);
  }

  EnumSet& operator|=(EnumSet set) { return *this = *this | set; }
  EnumSet& operator&=(EnumSet set) { return *this = *this & set; }
  EnumSet& operator-=(EnumSet set) { return *this = *this - set; }

  constexpr EnumSet operator|(E element) const {
    return EnumSet(bits_ | Mask(element));
  }
  constexpr EnumSet operator&(E element) const {
    return EnumSet(bits_ & Mask(element));
  }
  constexpr EnumSet operator-(E element) const {
    return EnumSet(bits_ & ~Mask(element));
  }

  EnumSet& operator|=(E element) { return *this = *this | element; }
  EnumSet& operator&=(E element) { return *this = *this & element; }
  EnumSet& operator-=(E element) { return *this = *this - element; }

  static constexpr EnumSet FromIntegral(T bits) { return EnumSet{bits}; }

 private:
  explicit constexpr EnumSet(T bits) : bits_(bits) {}

  static constexpr T Mask(E element) {
    DCHECK_GT(sizeof(T) * 8, static_cast<size_t>(element));
    return T{1} << static_cast<typename std::underlying_type<E>::type>(element);
  }

  T bits_ = 0;
};

template <typename E, typename T>
std::ostream& operator<<(std::ostream& os, EnumSet<E, T> set) {
  os << "{";
  bool first = true;
  while (!set.empty()) {
    if (!first) os << ", ";
    first = false;

    T bits = set.ToIntegral();
    E element = static_cast<E>(bits::CountTrailingZerosNonZero(bits));
    os << element;
    set.Remove(element);
  }
  os << "}";
  return os;
}

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_ENUM_SET_H_
