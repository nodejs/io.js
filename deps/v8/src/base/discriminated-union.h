// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_DISCRIMINATED_UNION_H_
#define V8_BASE_DISCRIMINATED_UNION_H_

#include <type_traits>
#include <utility>

#include "src/base/compiler-specific.h"
#include "src/base/template-utils.h"

namespace v8 {
namespace base {

// A variant-like discriminated union type, which takes a discriminating enum
// and a set of types. The enum must have as many elements as the number of
// types, with each enum value corresponding to one type in the set.
//
// Example usage:
//
//     enum class FooType {
//       kBar,
//       kBaz
//     }
//     class Bar { ... };
//     class Baz { ... };
//
//     // FooType::kBar and FooType::kBaz match Bar and Baz, respectively.
//     DiscriminatedUnion<FooType, Bar, Baz> union;
//
//     switch (union.tag()) {
//       case FooType::kBar:
//         return process_bar(union.get<FooType::kBar>);
//       case FooType::kBaz:
//         return process_baz(union.get<FooType::kBaz>);
//     }
template <typename TagEnum, typename... Ts>
class DiscriminatedUnion {
 public:
  // All Ts must be trivially destructible to avoid DiscriminatedUnion needing a
  // destructor.
  static_assert((std::is_trivially_destructible_v<Ts> && ...));

  using Tag = TagEnum;

  DiscriminatedUnion(DiscriminatedUnion&& other) V8_NOEXCEPT = default;
  DiscriminatedUnion(const DiscriminatedUnion& other) V8_NOEXCEPT = default;
  DiscriminatedUnion& operator=(DiscriminatedUnion&& other)
      V8_NOEXCEPT = default;
  DiscriminatedUnion& operator=(const DiscriminatedUnion& other)
      V8_NOEXCEPT = default;

  // TODO(leszeks): Add in-place constructor.

  // Construct with known tag and type (the tag is DCHECKed).
  template <typename T>
  constexpr explicit DiscriminatedUnion(Tag tag, T&& data) V8_NOEXCEPT {
    constexpr size_t index = index_of_type_v<std::decay_t<T>, Ts...>;
    static_assert(index < sizeof...(Ts));
    static_assert(index < std::numeric_limits<uint8_t>::max());
    // TODO(leszeks): Support unions with repeated types.
    DCHECK_EQ(tag, static_cast<Tag>(index));
    tag_ = static_cast<uint8_t>(index);
    new (data_) T(std::forward<T>(data));
  }

  // Construct with known type.
  template <typename T>
  constexpr explicit DiscriminatedUnion(T&& data) V8_NOEXCEPT {
    constexpr size_t index = index_of_type_v<std::decay_t<T>, Ts...>;
    static_assert(index < sizeof...(Ts));
    static_assert(index < std::numeric_limits<uint8_t>::max());
    tag_ = static_cast<uint8_t>(index);
    new (data_) T(std::forward<T>(data));
  }

  constexpr Tag tag() const { return static_cast<Tag>(tag_); }

  // Get union member by tag.
  template <Tag tag>
  constexpr const auto& get() const {
    using T = nth_type_t<static_cast<size_t>(tag), Ts...>;
    DCHECK_EQ(tag, this->tag());
    return *reinterpret_cast<const T*>(data_);
  }

  // Get union member by tag.
  template <Tag tag>
  constexpr auto& get() {
    using T = nth_type_t<static_cast<size_t>(tag), Ts...>;
    DCHECK_EQ(tag, this->tag());
    return *reinterpret_cast<T*>(data_);
  }

  // Get union member by type.
  template <typename T>
  constexpr const auto& get() const {
    DCHECK_EQ(static_cast<Tag>(index_of_type_v<T, Ts...>), this->tag());
    return *reinterpret_cast<const T*>(data_);
  }

  // Get union member by type.
  template <typename T>
  constexpr auto& get() {
    DCHECK_EQ(static_cast<Tag>(index_of_type_v<T, Ts...>), this->tag());
    return *reinterpret_cast<T*>(data_);
  }

 private:
  alignas(std::max({alignof(Ts)...})) char data_[std::max({sizeof(Ts)...})];
  static_assert(sizeof...(Ts) <= std::numeric_limits<uint8_t>::max());
  uint8_t tag_;
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_DISCRIMINATED_UNION_H_
