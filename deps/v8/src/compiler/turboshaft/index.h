// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_INDEX_H_
#define V8_COMPILER_TURBOSHAFT_INDEX_H_

#include <cstddef>
#include <type_traits>

#include "src/base/logging.h"
#include "src/codegen/tnode.h"
#include "src/compiler/turboshaft/fast-hash.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/objects/heap-number.h"
#include "src/objects/oddball.h"
#include "src/objects/string.h"
#include "src/objects/tagged.h"

namespace v8::internal::compiler::turboshaft {

namespace detail {
template <typename T>
struct lazy_false : std::false_type {};
}  // namespace detail

// Operations are stored in possibly muliple sequential storage slots.
using OperationStorageSlot = std::aligned_storage_t<8, 8>;
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
    DCHECK(CheckInvariants());
  }
  friend class OperationBuffer;

 public:
  static constexpr OpIndex FromOffset(uint32_t offset) {
    return OpIndex(offset);
  }
  constexpr OpIndex() : offset_(std::numeric_limits<uint32_t>::max()) {}
  template <typename T, typename C>
  OpIndex(const ConstOrV<T, C>&) {  // NOLINT(runtime/explicit)
    static_assert(detail::lazy_false<T>::value,
                  "Cannot initialize OpIndex from ConstOrV<>. Did you forget "
                  "to resolve() it in the assembler?");
  }

  constexpr uint32_t id() const {
    // Operations are stored at an offset that's a multiple of
    // `sizeof(OperationStorageSlot)`. In addition, an operation occupies at
    // least `kSlotsPerId` many `OperationSlot`s. Therefore, we can assign id's
    // by dividing by `kSlotsPerId`. A compact id space is important, because it
    // makes side-tables smaller.
    DCHECK(CheckInvariants());
    return offset_ / sizeof(OperationStorageSlot) / kSlotsPerId;
  }
  uint32_t hash() const {
    // It can be useful to hash OpIndex::Invalid(), so we have this `hash`
    // function, which returns the id, but without DCHECKing that Invalid is
    // valid.
    DCHECK_IMPLIES(valid(), CheckInvariants());
    return offset_ / sizeof(OperationStorageSlot) / kSlotsPerId;
  }
  uint32_t offset() const {
    DCHECK(CheckInvariants());
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

  static constexpr OptionalOpIndex Invalid() {
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

// Abstract tag classes for V<>.
struct Any {};

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

// Traits classes `v_traits<T>` to provide additional T-specific information for
// V<T> and ConstOrV<T>. If you need to provide non-default conversion behavior
// for a specific type, specialize the corresponding v_traits<>.
template <typename T, typename = void>
struct v_traits;

template <>
struct v_traits<Any> {
  static constexpr bool is_abstract_tag = true;
  static constexpr auto rep = nullrep;
  static constexpr bool allows_representation(RegisterRepresentation rep) {
    return true;
  }

  template <typename U>
  struct implicitly_convertible_to
      : std::bool_constant<std::is_same_v<U, Any>> {};
};

template <>
struct v_traits<Compressed> {
  static constexpr bool is_abstract_tag = true;
  static constexpr auto rep = nullrep;
  static constexpr bool allows_representation(RegisterRepresentation rep) {
    return rep == RegisterRepresentation::Compressed();
  }

  template <typename U>
  struct implicitly_convertible_to
      : std::bool_constant<std::is_base_of_v<U, Compressed>> {};
};

template <>
struct v_traits<Word32> {
  static constexpr bool is_abstract_tag = true;
  static constexpr WordRepresentation rep = WordRepresentation::Word32();
  using constexpr_type = uint32_t;
  static constexpr bool allows_representation(RegisterRepresentation rep) {
    return rep == RegisterRepresentation::Word32();
  }

  template <typename U>
  struct implicitly_convertible_to
      : std::bool_constant<std::is_base_of_v<U, Word32>> {};
};

template <>
struct v_traits<Word64> {
  static constexpr bool is_abstract_tag = true;
  static constexpr WordRepresentation rep = WordRepresentation::Word64();
  using constexpr_type = uint64_t;
  static constexpr bool allows_representation(RegisterRepresentation rep) {
    return rep == RegisterRepresentation::Word64();
  }

  template <typename U>
  struct implicitly_convertible_to
      : std::bool_constant<std::is_base_of_v<U, Word64>> {};
};

template <>
struct v_traits<Float32> {
  static constexpr bool is_abstract_tag = true;
  static constexpr FloatRepresentation rep = FloatRepresentation::Float32();
  using constexpr_type = float;
  static constexpr bool allows_representation(RegisterRepresentation rep) {
    return rep == RegisterRepresentation::Float32();
  }

  template <typename U>
  struct implicitly_convertible_to
      : std::bool_constant<std::is_base_of_v<U, Float32>> {};
};

template <>
struct v_traits<Float64> {
  static constexpr bool is_abstract_tag = true;
  static constexpr FloatRepresentation rep = FloatRepresentation::Float64();
  using constexpr_type = double;
  static constexpr bool allows_representation(RegisterRepresentation rep) {
    return rep == RegisterRepresentation::Float64();
  }

  template <typename U>
  struct implicitly_convertible_to
      : std::bool_constant<std::is_base_of_v<U, Float64>> {};
};

template <>
struct v_traits<Simd128> {
  static constexpr bool is_abstract_tag = true;
  static constexpr RegisterRepresentation rep =
      RegisterRepresentation::Simd128();
  using constexpr_type = uint8_t[kSimd128Size];
  static constexpr bool allows_representation(RegisterRepresentation rep) {
    return rep == RegisterRepresentation::Simd128();
  }

  template <typename U>
  struct implicitly_convertible_to
      : std::bool_constant<std::is_base_of_v<U, Simd128>> {};
};

template <>
struct v_traits<Simd256> {
  static constexpr bool is_abstract_tag = true;
  static constexpr RegisterRepresentation rep =
      RegisterRepresentation::Simd256();
  using constexpr_type = uint8_t[kSimd256Size];
  static constexpr bool allows_representation(RegisterRepresentation rep) {
    return rep == RegisterRepresentation::Simd256();
  }

  template <typename U>
  struct implicitly_convertible_to
      : std::bool_constant<std::is_base_of_v<U, Simd256>> {};
};

template <typename T>
struct v_traits<T, std::enable_if_t<is_taggable_v<T>>> {
  static constexpr bool is_abstract_tag = false;
  static constexpr auto rep = RegisterRepresentation::Tagged();
  static constexpr bool allows_representation(RegisterRepresentation rep) {
    return rep == RegisterRepresentation::Tagged();
  }

  template <typename U>
  struct implicitly_convertible_to
      : std::bool_constant<std::is_same_v<U, Any> || is_subtype<T, U>::value> {
  };
};

template <typename T1, typename T2>
struct v_traits<UnionT<T1, T2>,
                std::enable_if_t<is_taggable_v<UnionT<T1, T2>>>> {
  static_assert(!v_traits<T1>::is_abstract_tag);
  static_assert(!v_traits<T2>::is_abstract_tag);
  static constexpr bool is_abstract_tag = false;
  static_assert(v_traits<T1>::rep == v_traits<T2>::rep);
  static constexpr auto rep = v_traits<T1>::rep;
  static constexpr bool allows_representation(RegisterRepresentation r) {
    return r == rep;
  }

  template <typename U>
  struct implicitly_convertible_to
      : std::bool_constant<(
            v_traits<T1>::template implicitly_convertible_to<U>::value ||
            v_traits<T2>::template implicitly_convertible_to<U>::value)> {};
};

using BooleanOrNullOrUndefined = UnionT<UnionT<Boolean, Null>, Undefined>;
using NumberOrString = UnionT<Number, String>;
using PlainPrimitive = UnionT<NumberOrString, BooleanOrNullOrUndefined>;

// V<> represents an SSA-value that is parameterized with the type of the value.
// Types from the `Object` hierarchy can be provided as well as the abstract
// representation classes (`Word32`, ...) defined above.
// Prefer using V<> instead of a plain OpIndex where possible.
template <typename T>
class V : public OpIndex {
 public:
  using type = T;
  static constexpr auto rep = v_traits<type>::rep;
  constexpr V() : OpIndex() {}

  // V<T> is implicitly constructible from plain OpIndex.
  template <typename U, typename = std::enable_if_t<std::is_same_v<U, OpIndex>>>
  V(U index) : OpIndex(index) {}  // NOLINT(runtime/explicit)

  // V<T> is implicitly constructible from V<U> iff
  // `v_traits<U>::implicitly_convertible_to<T>::value`. This is typically the
  // case if T == U or T is a subclass of U. Different types may specify
  // different conversion rules in the corresponding `v_traits` when necessary.
  template <typename U, typename = std::enable_if_t<v_traits<
                            U>::template implicitly_convertible_to<T>::value>>
  V(V<U> index) : OpIndex(index) {}  // NOLINT(runtime/explicit)

  template <typename U>
  static V<T> Cast(V<U> index) {
    return V<T>(OpIndex{index});
  }
  static V<T> Cast(OpIndex index) { return V<T>(index); }

  static constexpr bool allows_representation(RegisterRepresentation rep) {
    return v_traits<T>::allows_representation(rep);
  }
};

template <typename T>
class OptionalV : public OptionalOpIndex {
 public:
  using type = T;
  static constexpr auto rep = v_traits<type>::rep;
  constexpr OptionalV() : OptionalOpIndex() {}

  // OptionalV<T> is implicitly constructible from plain OptionalOpIndex.
  template <typename U,
            typename = std::enable_if_t<std::is_same_v<U, OptionalOpIndex> ||
                                        std::is_same_v<U, OpIndex>>>
  OptionalV(U index) : OptionalOpIndex(index) {}  // NOLINT(runtime/explicit)

  // OptionalV<T> is implicitly constructible from OptionalV<U> iff
  // `v_traits<U>::implicitly_convertible_to<T>::value`. This is typically the
  // case if T == U or T is a subclass of U. Different types may specify
  // different conversion rules in the corresponding `v_traits` when necessary.
  template <typename U, typename = std::enable_if_t<v_traits<
                            U>::template implicitly_convertible_to<T>::value>>
  OptionalV(OptionalV<U> index)  // NOLINT(runtime/explicit)
      : OptionalOpIndex(index) {}
  template <typename U, typename = std::enable_if_t<v_traits<
                            U>::template implicitly_convertible_to<T>::value>>
  OptionalV(V<U> index) : OptionalOpIndex(index) {}  // NOLINT(runtime/explicit)

  template <typename U>
  static OptionalV<T> Cast(OptionalV<U> index) {
    return OptionalV<T>(OptionalOpIndex{index});
  }
  static OptionalV<T> Cast(OptionalOpIndex index) {
    return OptionalV<T>(index);
  }
};

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

  // ConstOrV<T> is implicitly constructible from plain OpIndex.
  template <typename U, typename = std::enable_if_t<std::is_same_v<U, OpIndex>>>
  ConstOrV(U index)  // NOLINT(runtime/explicit)
      : constant_value_(), value_(index) {}

  // ConstOrV<T> is implicitly constructible from V<U> iff V<T> is
  // constructible from V<U>.
  template <typename U,
            typename = std::enable_if_t<std::is_constructible_v<V<T>, V<U>>>>
  ConstOrV(V<U> index)  // NOLINT(runtime/explicit)
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

 private:
  base::Optional<constant_type> constant_value_;
  V<type> value_;
};

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

#endif  // V8_COMPILER_TURBOSHAFT_INDEX_H_
