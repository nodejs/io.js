// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_UNION_H_
#define V8_OBJECTS_UNION_H_

#include "src/base/template-utils.h"
#include "src/common/globals.h"

namespace v8::internal {

// Union<Ts...> represents a union of multiple V8 types.
//
// Unions are required to be non-nested (i.e. no unions of unions), and to
// have each type only once. The UnionOf<Ts...> helper can be used to flatten
// nested unions and remove duplicates.
//
// Inheritance from Unions is forbidden because it messes with `is_subtype`
// checking.
template <typename... Ts>
class Union;

// is_union<T> is a type trait that returns true if T is a union.
template <typename... Ts>
struct is_union : public std::false_type {};
template <typename... Ts>
struct is_union<Union<Ts...>> : public std::true_type {};
template <typename... Ts>
static constexpr bool is_union_v = is_union<Ts...>::value;

template <typename... Ts>
class Union final : public AllStatic {
  static_assert((!is_union_v<Ts> && ...),
                "Cannot have a union of unions -- use the UnionOf<T...> helper "
                "to flatten nested unions");
  static_assert(
      (base::has_type_v<Ts, Ts...> && ...),
      "Unions should have each type only once -- use the UnionOf<T...> "
      "helper to deduplicate unions");
};

namespace detail {

template <typename Accumulator, typename... InputTypes>
struct FlattenUnionHelper;

// Base case: No input types, return the accumulated types.
template <typename... OutputTs>
struct FlattenUnionHelper<Union<OutputTs...>> {
  using type = Union<OutputTs...>;
};

// Recursive case: Non-union input, accumulate and continue.
template <typename... OutputTs, typename Head, typename... Ts>
struct FlattenUnionHelper<Union<OutputTs...>, Head, Ts...> {
  // Don't accumulate duplicate types.
  using type = std::conditional_t<
      base::has_type_v<Head, OutputTs...>,
      typename FlattenUnionHelper<Union<OutputTs...>, Ts...>::type,
      typename FlattenUnionHelper<Union<OutputTs..., Head>, Ts...>::type>;
};

// Recursive case: Smi input, normalize to always be the first element.
//
// This is a small optimization to try reduce the number of template
// specializations -- ideally we would fully sort the types but this probably
// costs more templates than it saves.
template <typename... OutputTs, typename... Ts>
struct FlattenUnionHelper<Union<OutputTs...>, Smi, Ts...> {
  // Don't accumulate duplicate types.
  using type = std::conditional_t<
      base::has_type_v<Smi, OutputTs...>,
      typename FlattenUnionHelper<Union<OutputTs...>, Ts...>::type,
      typename FlattenUnionHelper<Union<Smi, OutputTs...>, Ts...>::type>;
};

// Recursive case: Union input, flatten and continue.
template <typename... OutputTs, typename... HeadTs, typename... Ts>
struct FlattenUnionHelper<Union<OutputTs...>, Union<HeadTs...>, Ts...> {
  using type =
      typename FlattenUnionHelper<Union<OutputTs...>, HeadTs..., Ts...>::type;
};

}  // namespace detail

// UnionOf<Ts...> is a helper that returns a union of multiple V8 types,
// flattening any nested unions and removing duplicate types.
template <typename... Ts>
using UnionOf = typename detail::FlattenUnionHelper<Union<>, Ts...>::type;

// Unions of unions are flattened.
static_assert(std::is_same_v<Union<Smi, HeapObject>,
                             UnionOf<UnionOf<Smi>, UnionOf<HeapObject>>>);
// Unions with duplicates are deduplicated.
static_assert(std::is_same_v<Union<Smi, HeapObject>,
                             UnionOf<HeapObject, Smi, Smi, HeapObject>>);
// Unions with Smis are normalized to have the Smi be the first element.
static_assert(std::is_same_v<Union<Smi, HeapObject>, UnionOf<HeapObject, Smi>>);

}  // namespace v8::internal

#endif  // V8_OBJECTS_UNION_H_
