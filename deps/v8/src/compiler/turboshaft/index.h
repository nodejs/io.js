// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_INDEX_H_
#define V8_COMPILER_TURBOSHAFT_INDEX_H_

#include <stdint.h>

#include <cstddef>
#include <optional>
#include <type_traits>

#include "src/base/logging.h"
#include "src/codegen/tnode.h"
#include "src/compiler/turboshaft/fast-hash.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/objects/heap-number.h"
#include "src/objects/js-function.h"
#include "src/objects/oddball.h"
#include "src/objects/string.h"
#include "src/objects/tagged.h"

#define TURBOSHAFT_ALLOW_IMPLICIT_OPINDEX_INITIALIZATION_FOR_V 1

namespace v8::internal::compiler::turboshaft {

// Operations are stored in possibly multiple sequential storage slots.
using OperationStorageSlot = uint64_t;
// Operations occupy at least 2 slots, therefore we assign one id per two slots.
constexpr size_t kSlotsPerId = 2;

template <typename T, typename C>
class ConstOrV;

// `OpIndex` is an offset from the beginning of the operations buffer.
// Compared to `Operation*`, it is more memory efficient (32bit) and stable when
// the operations buffer is re-allocated.
class OpIndex {
 protected:
  // We make this constructor protected so that integers are not easily
  // convertible to OpIndex. FromOffset should be used instead to create an
  // OpIndex from an offset.
  explicit constexpr OpIndex(uint32_t offset) : offset_(offset) {
    SLOW_DCHECK(CheckInvariants());
  }
  friend class OperationBuffer;

 public:
  static constexpr OpIndex FromOffset(uint32_t offset) {
    return OpIndex(offset);
  }
  constexpr OpIndex() : offset_(std::numeric_limits<uint32_t>::max()) {}
  template <typename T, typename C>
  OpIndex(const ConstOrV<T, C>&) {  // NOLINT(runtime/explicit)
    static_assert(base::tmp::lazy_false<T>::value,
                  "Cannot initialize OpIndex from ConstOrV<>. Did you forget "
                  "to resolve() it in the assembler?");
  }

  constexpr uint32_t id() const {
    // Operations are stored at an offset that's a multiple of
    // `sizeof(OperationStorageSlot)`. In addition, an operation occupies at
    // least `kSlotsPerId` many `OperationSlot`s. Therefore, we can assign id's
    // by dividing by `kSlotsPerId`. A compact id space is important, because it
    // makes side-tables smaller.
    SLOW_DCHECK(CheckInvariants());
    return offset_ / sizeof(OperationStorageSlot) / kSlotsPerId;
  }
  uint32_t hash() const {
    // It can be useful to hash OpIndex::Invalid(), so we have this `hash`
    // function, which returns the id, but without DCHECKing that Invalid is
    // valid.
    SLOW_DCHECK_IMPLIES(valid(), CheckInvariants());
    return offset_ / sizeof(OperationStorageSlot) / kSlotsPerId;
  }
  uint32_t offset() const {
    SLOW_DCHECK(CheckInvariants());
#ifdef DEBUG
    return offset_ & kUnmaskGenerationMask;
#else
    return offset_;
#endif
  }

  constexpr bool valid() const { return *this != Invalid(); }

  static constexpr OpIndex Invalid() { return OpIndex(); }

  // Encode a sea-of-nodes node id in the `OpIndex` type.
  // Only used for node origins that actually point to sea-of-nodes graph nodes.
  static OpIndex EncodeTurbofanNodeId(uint32_t id) {
    OpIndex result = OpIndex(id * sizeof(OperationStorageSlot));
    result.offset_ += kTurbofanNodeIdFlag;
    return result;
  }
  uint32_t DecodeTurbofanNodeId() const {
    DCHECK(IsTurbofanNodeId());
    return offset_ / sizeof(OperationStorageSlot);
  }
  bool IsTurbofanNodeId() const {
    return offset_ % sizeof(OperationStorageSlot) == kTurbofanNodeIdFlag;
  }

  constexpr bool operator==(OpIndex other) const {
    return offset_ == other.offset_;
  }
  constexpr bool operator!=(OpIndex other) const {
    return offset_ != other.offset_;
  }
  constexpr bool operator<(OpIndex other) const {
    return offset_ < other.offset_;
  }
  constexpr bool operator>(OpIndex other) const {
    return offset_ > other.offset_;
  }
  constexpr bool operator<=(OpIndex other) const {
    return offset_ <= other.offset_;
  }
  constexpr bool operator>=(OpIndex other) const {
    return offset_ >= other.offset_;
  }

#ifdef DEBUG
  int generation_mod2() const {
    return (offset_ & kGenerationMask) >> kGenerationMaskShift;
  }
  void set_generation_mod2(int generation_mod2) {
    DCHECK_LE(generation_mod2, 1);
    offset_ |= generation_mod2 << kGenerationMaskShift;
  }

  constexpr bool CheckInvariants() const {
    DCHECK(valid());
    // The second lowest significant bit of the offset is used to store the
    // graph generation modulo 2. The lowest and 3rd lowest bits should always
    // be 0 (as long as sizeof(OperationStorageSlot) is 8).
    static_assert(sizeof(OperationStorageSlot) == 8);
    return (offset_ & 0b101) == 0;
  }
#endif

 protected:
  static constexpr uint32_t kGenerationMaskShift = 1;
  static constexpr uint32_t kGenerationMask = 1 << kGenerationMaskShift;
  static constexpr uint32_t kUnmaskGenerationMask = ~kGenerationMask;

  // In DEBUG builds, the offset's second lowest bit contains the graph
  // generation % 2, so one should keep this in mind when looking at the value
  // of the offset.
  uint32_t offset_;

  static constexpr uint32_t kTurbofanNodeIdFlag = 1;

  template <typename H>
  friend H AbslHashValue(H h, const OpIndex& idx) {
    return H::combine(std::move(h), idx.offset_);
  }
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os, OpIndex idx);

class OptionalOpIndex : protected OpIndex {
 public:
  using OpIndex::OpIndex;
  using OpIndex::valid;

  constexpr OptionalOpIndex(OpIndex other)  // NOLINT(runtime/explicit)
      : OpIndex(other) {}

  static constexpr OptionalOpIndex Nullopt() {
    return OptionalOpIndex{OpIndex::Invalid()};
  }

  uint32_t hash() const { return OpIndex::hash(); }

  constexpr bool has_value() const { return valid(); }
  constexpr OpIndex value() const {
    DCHECK(has_value());
    return OpIndex(*this);
  }
  constexpr OpIndex value_or_invalid() const { return OpIndex(*this); }

  template <typename H>
  friend H AbslHashValue(H h, const OptionalOpIndex& idx) {
    return H::combine(std::move(h), idx.offset_);
  }
};

V8_INLINE std::ostream& operator<<(std::ostream& os, OptionalOpIndex idx) {
  return os << idx.value_or_invalid();
}

// Dummy value for abstract representation classes that don't have a
// RegisterRepresentation.
struct nullrep_t {};
constexpr nullrep_t nullrep;
constexpr inline bool operator==(nullrep_t, nullrep_t) { return true; }
constexpr inline bool operator==(nullrep_t, RegisterRepresentation) {
  return false;
}
constexpr inline bool operator==(RegisterRepresentation, nullrep_t) {
  return false;
}
constexpr inline bool operator!=(nullrep_t, nullrep_t) { return false; }
constexpr inline bool operator!=(nullrep_t, RegisterRepresentation) {
  return true;
}
constexpr inline bool operator!=(RegisterRepresentation, nullrep_t) {
  return true;
}

// Abstract tag classes for V<>.
struct Any {};
struct None {};

template <size_t Bits>
struct WordWithBits : public Any {
  static constexpr int bits = Bits;
  static_assert(Bits == 32 || Bits == 64 || Bits == 128 || Bits == 256);
};

using Word32 = WordWithBits<32>;
using Word64 = WordWithBits<64>;
using WordPtr = std::conditional_t<Is64(), Word64, Word32>;

template <size_t Bits>
struct FloatWithBits : public Any {  // FloatAny {
  static constexpr int bits = Bits;
  static_assert(Bits == 32 || Bits == 64);
};

using Float32 = FloatWithBits<32>;
using Float64 = FloatWithBits<64>;

using Simd128 = WordWithBits<128>;
using Simd256 = WordWithBits<256>;

struct Compressed : public Any {};
struct InternalTag : public Any {};
struct FrameState : public InternalTag {};

// A Union type for untagged values. For Tagged types use `Union` for now.
// TODO(nicohartmann@): We should think about a more uniform solution some day.
template <typename... Ts>
struct UntaggedUnion : public Any {
  using to_list_t = base::tmp::list<Ts...>;
};

template <typename... Ts>
struct Tuple : public Any {
  using to_list_t = base::tmp::list<Ts...>;
  template <int Index>
  using element_t = base::tmp::element_t<to_list_t, Index>;
};

// Traits classes `v_traits<T>` to provide additional T-specific information for
// V<T> and ConstOrV<T>. If you need to provide non-default conversion behavior
// for a specific type, specialize the corresponding v_traits<>.
template <typename T, typename = void>
struct v_traits;

template <>
struct v_traits<Any> {
  static constexpr bool is_abstract_tag = true;
  using rep_type = RegisterRepresentation;
  static constexpr auto rep = nullrep;
  static constexpr bool allows_representation(RegisterRepresentation) {
    return true;
  }

  template <typename U>
  struct implicitly_constructible_from : std::true_type {};
};

template <>
struct v_traits<None> {
  static constexpr bool is_abstract_tag = true;
  using rep_type = nullrep_t;
  static constexpr auto rep = nullrep;
  static constexpr bool allows_representation(RegisterRepresentation) {
    return false;
  }

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<std::is_same_v<U, None>> {};
};

template <>
struct v_traits<Compressed> {
  static constexpr bool is_abstract_tag = true;
  using rep_type = RegisterRepresentation;
  static constexpr auto rep = RegisterRepresentation::Compressed();
  static constexpr bool allows_representation(
      RegisterRepresentation maybe_allowed_rep) {
    return maybe_allowed_rep == RegisterRepresentation::Compressed();
  }

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<std::is_base_of_v<Compressed, U>> {};
};

template <>
struct v_traits<Word32> {
  static constexpr bool is_abstract_tag = true;
  using rep_type = WordRepresentation;
  static constexpr auto rep = WordRepresentation::Word32();
  using constexpr_type = uint32_t;
  static constexpr bool allows_representation(
      RegisterRepresentation maybe_allowed_rep) {
    return maybe_allowed_rep == RegisterRepresentation::Word32();
  }

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<std::is_base_of_v<Word32, U>> {};
};

template <>
struct v_traits<Word64> {
  static constexpr bool is_abstract_tag = true;
  using rep_type = WordRepresentation;
  static constexpr auto rep = WordRepresentation::Word64();
  using constexpr_type = uint64_t;
  static constexpr bool allows_representation(
      RegisterRepresentation maybe_allowed_rep) {
    return maybe_allowed_rep == RegisterRepresentation::Word64();
  }

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<std::is_base_of_v<Word64, U>> {};
};

template <>
struct v_traits<Float32> {
  static constexpr bool is_abstract_tag = true;
  using rep_type = FloatRepresentation;
  static constexpr auto rep = FloatRepresentation::Float32();
  using constexpr_type = float;
  static constexpr bool allows_representation(
      RegisterRepresentation maybe_allowed_rep) {
    return maybe_allowed_rep == RegisterRepresentation::Float32();
  }

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<std::is_base_of_v<Float32, U>> {};
};

template <>
struct v_traits<Float64> {
  static constexpr bool is_abstract_tag = true;
  using rep_type = FloatRepresentation;
  static constexpr auto rep = FloatRepresentation::Float64();
  using constexpr_type = double;
  static constexpr bool allows_representation(
      RegisterRepresentation maybe_allowed_rep) {
    return maybe_allowed_rep == RegisterRepresentation::Float64();
  }

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<std::is_base_of_v<Float64, U>> {};
};

template <>
struct v_traits<Simd128> {
  static constexpr bool is_abstract_tag = true;
  using rep_type = RegisterRepresentation;
  static constexpr auto rep = RegisterRepresentation::Simd128();
  using constexpr_type = uint8_t[kSimd128Size];
  static constexpr bool allows_representation(
      RegisterRepresentation maybe_allowed_rep) {
    return maybe_allowed_rep == RegisterRepresentation::Simd128();
  }

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<std::is_base_of_v<Simd128, U>> {};
};

template <>
struct v_traits<Simd256> {
  static constexpr bool is_abstract_tag = true;
  using rep_type = RegisterRepresentation;
  static constexpr auto rep = RegisterRepresentation::Simd256();
  using constexpr_type = uint8_t[kSimd256Size];
  static constexpr bool allows_representation(
      RegisterRepresentation maybe_allowed_rep) {
    return maybe_allowed_rep == RegisterRepresentation::Simd256();
  }

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<std::is_base_of_v<Simd256, U>> {};
};

template <typename T>
struct v_traits<T, std::enable_if_t<is_taggable_v<T> && !is_union_v<T>>> {
  static constexpr bool is_abstract_tag = false;
  using rep_type = RegisterRepresentation;
  static constexpr auto rep = RegisterRepresentation::Tagged();
  static constexpr bool allows_representation(
      RegisterRepresentation maybe_allowed_rep) {
    return maybe_allowed_rep == RegisterRepresentation::Tagged();
  }

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<is_subtype<U, T>::value> {};
  template <typename... Us>
  struct implicitly_constructible_from<UntaggedUnion<Us...>>
      : std::bool_constant<(
            v_traits<T>::template implicitly_constructible_from<Us>::value &&
            ...)> {};
};

template <typename T, typename... Ts>
struct v_traits<Union<T, Ts...>> {
  static_assert(!v_traits<T>::is_abstract_tag);
  static_assert((!v_traits<Ts>::is_abstract_tag && ...));
  static constexpr bool is_abstract_tag = false;
  static_assert(((v_traits<T>::rep == v_traits<Ts>::rep) && ...));
  static_assert((std::is_same_v<typename v_traits<T>::rep_type,
                                typename v_traits<Ts>::rep_type> &&
                 ...));
  using rep_type = typename v_traits<T>::rep_type;
  static constexpr auto rep = v_traits<T>::rep;
  static constexpr bool allows_representation(
      RegisterRepresentation maybe_allowed_rep) {
    return maybe_allowed_rep == rep;
  }

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<(
            v_traits<T>::template implicitly_constructible_from<U>::value ||
            ... ||
            v_traits<Ts>::template implicitly_constructible_from<U>::value)> {};
  template <typename... Us>
  struct implicitly_constructible_from<Union<Us...>>
      : std::bool_constant<(implicitly_constructible_from<Us>::value && ...)> {
  };
};

namespace detail {
template <typename T, bool SameStaticRep>
struct RepresentationForUnionBase {
  static constexpr auto rep = nullrep;
};
template <typename T>
struct RepresentationForUnionBase<T, true> {
  static constexpr auto rep = v_traits<T>::rep;
};
template <typename T>
struct RepresentationForUnion {};
template <typename T, typename... Ts>
struct RepresentationForUnion<UntaggedUnion<T, Ts...>>
    : RepresentationForUnionBase<T, ((v_traits<T>::rep == v_traits<Ts>::rep) &&
                                     ...)> {
 private:
  template <typename U>
  struct to_rep_type {
    using type = typename v_traits<U>::rep_type;
  };
  using rep_types = base::tmp::map_t<to_rep_type, base::tmp::list<T, Ts...>>;

 public:
  using rep_type =
      std::conditional_t<base::tmp::contains_v<rep_types, nullrep_t>, nullrep_t,
                         std::conditional_t<base::tmp::all_equal_v<rep_types>,
                                            typename v_traits<T>::rep_type,
                                            RegisterRepresentation>>;
};

}  // namespace detail

template <typename... Ts>
struct v_traits<UntaggedUnion<Ts...>> {
  using rep_type =
      typename detail::RepresentationForUnion<UntaggedUnion<Ts...>>::rep_type;
  static constexpr auto rep =
      detail::RepresentationForUnion<UntaggedUnion<Ts...>>::rep;
  static constexpr bool allows_representation(
      RegisterRepresentation maybe_allowed_rep) {
    return (v_traits<Ts>::allows_representation(maybe_allowed_rep) || ...);
  }

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<(
            v_traits<Ts>::template implicitly_constructible_from<U>::value ||
            ...)> {};
  template <typename... Us>
  struct implicitly_constructible_from<UntaggedUnion<Us...>>
      : std::bool_constant<(implicitly_constructible_from<Us>::value && ...)> {
  };
};

template <typename T>
struct v_traits<T, std::enable_if_t<std::is_base_of_v<InternalTag, T>>> {
  using rep_type = nullrep_t;
  static constexpr auto rep = nullrep;

  template <typename U>
  struct implicitly_constructible_from
      : std::bool_constant<std::is_same_v<T, U>> {};
};

template <typename... Ts>
struct v_traits<Tuple<Ts...>> {
  using rep_type = nullrep_t;
  static constexpr auto rep = nullrep;
  static constexpr bool allows_representation(RegisterRepresentation) {
    return false;
  }

  template <typename U>
  struct implicitly_constructible_from : std::false_type {};

  // NOTE: If you end up here with a compiler error
  // "pack expansion contains parameter packs 'Ts' and 'Us' that have different
  // lengths" this is most likely because you tried to convert between Tuple<>
  // types of different sizes.
  template <typename... Us>
  struct implicitly_constructible_from<Tuple<Us...>>
      : std::bool_constant<(
            v_traits<Ts>::template implicitly_constructible_from<Us>::value &&
            ...)> {};
};

using Word = UntaggedUnion<Word32, Word64>;
using Float = UntaggedUnion<Float32, Float64>;
using Float64OrWord32 = UntaggedUnion<Float64, Word32>;
using Untagged = UntaggedUnion<Word, Float>;
using BooleanOrNullOrUndefined = UnionOf<Boolean, Null, Undefined>;
using NumberOrString = UnionOf<Number, String>;
using PlainPrimitive = UnionOf<NumberOrString, BooleanOrNullOrUndefined>;
using StringOrNull = UnionOf<String, Null>;
using NumberOrUndefined = UnionOf<Number, Undefined>;
using AnyFixedArray = UnionOf<FixedArray, FixedDoubleArray>;
using NonBigIntPrimitive = UnionOf<Symbol, PlainPrimitive>;
using Primitive = UnionOf<BigInt, NonBigIntPrimitive>;
using CallTarget = UntaggedUnion<WordPtr, Code, JSFunction, Word32>;
using AnyOrNone = UntaggedUnion<Any, None>;
using Word32Pair = Tuple<Word32, Word32>;

template <typename T>
concept IsUntagged =
    !std::is_same_v<T, Any> &&
    v_traits<Untagged>::implicitly_constructible_from<T>::value;

template <typename T>
concept IsTagged = !std::is_same_v<T, Any> &&
                   v_traits<Object>::implicitly_constructible_from<T>::value;

#if V8_ENABLE_WEBASSEMBLY
using WasmArrayNullable = Union<WasmArray, WasmNull>;
using WasmStructNullable = Union<WasmStruct, WasmNull>;
// The type for a nullable ref.string (stringref proposal). For imported strings
// use StringOrNull instead.
using WasmStringRefNullable = Union<String, WasmNull>;
#endif

template <typename T>
constexpr bool IsWord() {
  return std::is_same_v<T, Word32> || std::is_same_v<T, Word64> ||
         std::is_same_v<T, Word>;
}

template <typename T>
constexpr bool IsValidTypeFor(RegisterRepresentation repr) {
  if (std::is_same_v<T, Any>) return true;

  switch (repr.value()) {
    case RegisterRepresentation::Enum::kWord32:
      return std::is_same_v<T, Word> || std::is_same_v<T, Word32> ||
             std::is_same_v<T, Untagged>;
    case RegisterRepresentation::Enum::kWord64:
      return std::is_same_v<T, Word> || std::is_same_v<T, Word64> ||
             std::is_same_v<T, Untagged>;
    case RegisterRepresentation::Enum::kFloat32:
      return std::is_same_v<T, Float> || std::is_same_v<T, Float32> ||
             std::is_same_v<T, Untagged>;
    case RegisterRepresentation::Enum::kFloat64:
      return std::is_same_v<T, Float> || std::is_same_v<T, Float64> ||
             std::is_same_v<T, Untagged>;
    case RegisterRepresentation::Enum::kTagged:
      return is_subtype_v<T, Object>;
    case RegisterRepresentation::Enum::kCompressed:
      return is_subtype_v<T, Object>;
    case RegisterRepresentation::Enum::kSimd128:
      return std::is_same_v<T, Simd128>;
    case RegisterRepresentation::Enum::kSimd256:
      return std::is_same_v<T, Simd256>;
  }
}

// V<> represents an SSA-value that is parameterized with the type of the value.
// Types from the `Object` hierarchy can be provided as well as the abstract
// representation classes (`Word32`, ...) defined above.
// Prefer using V<> instead of a plain OpIndex where possible.
template <typename T>
class V : public OpIndex {
  // V<T> is implicitly constructible from V<U> iff
  // `v_traits<T>::implicitly_constructible_from<U>::value`. This is typically
  // the case if T == U or T is a subclass of U. Different types may specify
  // different conversion rules in the corresponding `v_traits` when necessary.
  template <typename U>
  constexpr static bool implicitly_constructible_from =
      v_traits<T>::template implicitly_constructible_from<U>::value;

 public:
  using type = T;
  static constexpr auto rep = v_traits<type>::rep;
  constexpr V() : OpIndex() {}

  // V<T> is implicitly constructible from V<U> iff
  // `v_traits<T>::implicitly_constructible_from<U>::value`. This is typically
  // the case if T == U or T is a subclass of U. Different types may specify
  // different conversion rules in the corresponding `v_traits` when necessary.
  template <typename U>
    requires implicitly_constructible_from<U>
  V(V<U> index) : OpIndex(index) {}  // NOLINT(runtime/explicit)

  static V Invalid() { return V<T>(OpIndex::Invalid()); }

  template <typename U>
  static V<T> Cast(V<U> index) {
    return V<T>(OpIndex{index});
  }
  static V<T> Cast(OpIndex index) { return V<T>(index); }

  static constexpr bool allows_representation(
      RegisterRepresentation maybe_allowed_rep) {
    return v_traits<T>::allows_representation(maybe_allowed_rep);
  }

#if !defined(TURBOSHAFT_ALLOW_IMPLICIT_OPINDEX_INITIALIZATION_FOR_V)

 protected:
#endif
  // V<T> is implicitly constructible from plain OpIndex.
  template <typename U>
    requires(std::is_same_v<U, OpIndex>)
  V(U index) : OpIndex(index) {}  // NOLINT(runtime/explicit)
};

template <typename T>
class OptionalV : public OptionalOpIndex {
  // OptionalV<T> is implicitly constructible from OptionalV<U> iff
  // `v_traits<T>::implicitly_constructible_from<U>::value`. This is typically
  // the case if T == U or T is a subclass of U. Different types may specify
  // different conversion rules in the corresponding `v_traits` when necessary.
  template <typename U>
  constexpr static bool implicitly_constructible_from =
      v_traits<T>::template implicitly_constructible_from<U>::value;

 public:
  using type = T;
  static constexpr auto rep = v_traits<type>::rep;
  constexpr OptionalV() : OptionalOpIndex() {}

  template <typename U>
    requires implicitly_constructible_from<U>
  OptionalV(OptionalV<U> index)  // NOLINT(runtime/explicit)
      : OptionalOpIndex(index) {}
  template <typename U>
    requires implicitly_constructible_from<U>
  OptionalV(V<U> index) : OptionalOpIndex(index) {}  // NOLINT(runtime/explicit)

  static OptionalV Nullopt() { return OptionalV(OptionalOpIndex::Nullopt()); }

  constexpr V<T> value() const {
    DCHECK(has_value());
    return V<T>::Cast(OptionalOpIndex::value());
  }
  constexpr V<T> value_or_invalid() const {
    return V<T>::Cast(OptionalOpIndex::value_or_invalid());
  }

  template <typename U>
  static OptionalV<T> Cast(OptionalV<U> index) {
    return OptionalV<T>(OptionalOpIndex{index});
  }
  static OptionalV<T> Cast(OptionalOpIndex index) {
    return OptionalV<T>(index);
  }

#if !defined(TURBOSHAFT_ALLOW_IMPLICIT_OPINDEX_INITIALIZATION_FOR_V)

 protected:
#endif
  // OptionalV<T> is implicitly constructible from plain OptionalOpIndex.
  template <typename U>
    requires(std::is_same_v<U, OptionalOpIndex> || std::is_same_v<U, OpIndex>)
  OptionalV(U index) : OptionalOpIndex(index) {}  // NOLINT(runtime/explicit)
};

// Deduction guide for `OptionalV`.
template <typename T>
OptionalV(V<T>) -> OptionalV<T>;

// ConstOrV<> is a generalization of V<> that allows constexpr values
// (constants) to be passed implicitly. This allows reducers to write things
// like
//
// __ Word32Add(value, 1)
//
// instead of having to write
//
// __ Word32Add(value, __ Word32Constant(1))
//
// which makes overall code more compact and easier to read. Functions need to
// call `resolve` on the assembler in order to convert to V<> (which will then
// construct the corresponding ConstantOp if the given ConstOrV<> holds a
// constexpr value).
// NOTICE: `ConstOrV<T>` can only be used if `v_traits<T>` provides a
// `constexpr_type`.
template <typename T, typename C = typename v_traits<T>::constexpr_type>
class ConstOrV {
 public:
  using type = T;
  using constant_type = C;

  ConstOrV(constant_type value)  // NOLINT(runtime/explicit)
      : constant_value_(value), value_() {}

  // ConstOrV<T> is implicitly constructible from V<U> iff V<T> is
  // constructible from V<U>.
  template <typename U>
  ConstOrV(V<U> index)  // NOLINT(runtime/explicit)
    requires(std::is_constructible_v<V<T>, V<U>>)
      : constant_value_(std::nullopt), value_(index) {}

  bool is_constant() const { return constant_value_.has_value(); }
  constant_type constant_value() const {
    DCHECK(is_constant());
    return *constant_value_;
  }
  V<type> value() const {
    DCHECK(!is_constant());
    return value_;
  }

#if !defined(TURBOSHAFT_ALLOW_IMPLICIT_OPINDEX_INITIALIZATION_FOR_V)

 protected:
#endif
  // ConstOrV<T> is implicitly constructible from plain OpIndex.
  template <typename U>
  ConstOrV(U index)  // NOLINT(runtime/explicit)
    requires(std::is_same_v<U, OpIndex>)
      : constant_value_(), value_(index) {}

 private:
  std::optional<constant_type> constant_value_;
  V<type> value_;
};

// Deduction guide for `ConstOrV`.
template <typename T>
ConstOrV(V<T>) -> ConstOrV<T>;

template <>
struct fast_hash<OpIndex> {
  V8_INLINE size_t operator()(OpIndex op) const { return op.hash(); }
};

V8_INLINE size_t hash_value(OpIndex op) { return base::hash_value(op.hash()); }
V8_INLINE size_t hash_value(OptionalOpIndex op) {
  return base::hash_value(op.hash());
}

namespace detail {
template <typename T, typename = void>
struct ConstOrVTypeHelper {
  static constexpr bool exists = false;
  using type = V<T>;
};
template <typename T>
struct ConstOrVTypeHelper<T, std::void_t<ConstOrV<T>>> {
  static constexpr bool exists = true;
  using type = ConstOrV<T>;
};
}  // namespace detail

template <typename T>
using maybe_const_or_v_t = typename detail::ConstOrVTypeHelper<T>::type;
template <typename T>
constexpr bool const_or_v_exists_v = detail::ConstOrVTypeHelper<T>::exists;

// `ShadowyOpIndex` is a wrapper around `OpIndex` that allows implicit
// conversion to arbitrary `V<>`. This is required for generic code inside the
// `Assembler` and `CopyingPhase`. Once implicit initialization of `V<>` from
// `OpIndex` is disabled,
//
//   OpIndex new_index = ...
//   ReduceWordUnary(new_index, ...)
//
// will no longer compile, because `ReduceWordUnary` expects a `V<Word>` input.
// However,
//
//   OpIndex new_index = ...
//   ReduceWordUnary(ShadowyOpIndex{new_index}, ...)
//
// will still compile. **Do not use ShadowyOpIndex directly** in any operations
// or reducers.
class ShadowyOpIndex : public OpIndex {
 public:
  explicit ShadowyOpIndex(OpIndex index) : OpIndex(index) {}

  template <typename T>
  operator V<T>() const {  // NOLINT(runtime/explicit)
    return V<T>::Cast(*this);
  }
};

// Similarly to how `ShadowyOpIndex` is a wrapper around `OpIndex` that allows
// arbitrary conversion to `V<>`, `ShadowyOpIndexVectorWrapper` is a wrapper
// around `base::Vector<const OpIndex>` that allows implicit conversion to
// `base::Vector<const V<U>>` for any `U`.
class ShadowyOpIndexVectorWrapper {
 public:
  template <typename T>
  ShadowyOpIndexVectorWrapper(
      base::Vector<const V<T>> indices)  // NOLINT(runtime/explicit)
      : indices_(indices.data(), indices.size()) {}
  ShadowyOpIndexVectorWrapper(
      base::Vector<const OpIndex> indices)  // NOLINT(runtime/explicit)
      : indices_(indices) {}
  template <typename T>
  ShadowyOpIndexVectorWrapper(
      base::Vector<V<T>> indices)  // NOLINT(runtime/explicit)
      : indices_(indices.data(), indices.size()) {}
  ShadowyOpIndexVectorWrapper(
      base::Vector<OpIndex> indices)  // NOLINT(runtime/explicit)
      : indices_(indices) {}

  operator base::Vector<const OpIndex>() const {  // NOLINT(runtime/explicit)
    return indices_;
  }
  template <typename U>
  operator base::Vector<V<U>>() const {  // NOLINT(runtime/explicit)
    return base::Vector<V<U>>{indices_.data(), indices_.size()};
  }
  template <typename U>
  operator base::Vector<const V<U>>() const {  // NOLINT(runtime/explicit)
    return {static_cast<const V<U>*>(indices_.data()), indices_.size()};
  }

  size_t size() const noexcept { return indices_.size(); }

 private:
  base::Vector<const OpIndex> indices_;
};

// `BlockIndex` is the index of a bound block.
// A dominating block always has a smaller index.
// It corresponds to the ordering of basic blocks in the operations buffer.
class BlockIndex {
 public:
  explicit constexpr BlockIndex(uint32_t id) : id_(id) {}
  constexpr BlockIndex() : id_(std::numeric_limits<uint32_t>::max()) {}

  uint32_t id() const { return id_; }
  bool valid() const { return *this != Invalid(); }

  static constexpr BlockIndex Invalid() { return BlockIndex(); }

  bool operator==(BlockIndex other) const { return id_ == other.id_; }
  bool operator!=(BlockIndex other) const { return id_ != other.id_; }
  bool operator<(BlockIndex other) const { return id_ < other.id_; }
  bool operator>(BlockIndex other) const { return id_ > other.id_; }
  bool operator<=(BlockIndex other) const { return id_ <= other.id_; }
  bool operator>=(BlockIndex other) const { return id_ >= other.id_; }

  template <typename H>
  friend H AbslHashValue(H h, const BlockIndex& idx) {
    return H::combine(std::move(h), idx.id_);
  }

 private:
  uint32_t id_;
};

template <>
struct fast_hash<BlockIndex> {
  V8_INLINE size_t operator()(BlockIndex op) const { return op.id(); }
};

V8_INLINE size_t hash_value(BlockIndex op) { return base::hash_value(op.id()); }

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os, BlockIndex b);

#define DEFINE_STRONG_ORDERING_COMPARISON(lhs_type, rhs_type, lhs_access, \
                                          rhs_access)                     \
  V8_INLINE constexpr bool operator==(lhs_type l, rhs_type r) {           \
    return lhs_access == rhs_access;                                      \
  }                                                                       \
  V8_INLINE constexpr bool operator!=(lhs_type l, rhs_type r) {           \
    return lhs_access != rhs_access;                                      \
  }                                                                       \
  V8_INLINE constexpr bool operator<(lhs_type l, rhs_type r) {            \
    return lhs_access < rhs_access;                                       \
  }                                                                       \
  V8_INLINE constexpr bool operator<=(lhs_type l, rhs_type r) {           \
    return lhs_access <= rhs_access;                                      \
  }                                                                       \
  V8_INLINE constexpr bool operator>(lhs_type l, rhs_type r) {            \
    return lhs_access > rhs_access;                                       \
  }                                                                       \
  V8_INLINE constexpr bool operator>=(lhs_type l, rhs_type r) {           \
    return lhs_access >= rhs_access;                                      \
  }
DEFINE_STRONG_ORDERING_COMPARISON(OptionalOpIndex, OptionalOpIndex,
                                  l.value_or_invalid(), r.value_or_invalid())
DEFINE_STRONG_ORDERING_COMPARISON(OpIndex, OptionalOpIndex, l,
                                  r.value_or_invalid())
DEFINE_STRONG_ORDERING_COMPARISON(OptionalOpIndex, OpIndex,
                                  l.value_or_invalid(), r)
#undef DEFINE_STRONG_ORDERING_COMPARISON

}  // namespace v8::internal::compiler::turboshaft

template <>
struct std::hash<v8::internal::compiler::turboshaft::OpIndex> {
  std::size_t operator()(
      const v8::internal::compiler::turboshaft::OpIndex& index) const {
    return index.hash();
  }
};

#endif  // V8_COMPILER_TURBOSHAFT_INDEX_H_
