// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_TNODE_H_
#define V8_CODEGEN_TNODE_H_

#include <type_traits>

#include "src/codegen/machine-type.h"
#include "src/objects/tagged.h"

namespace v8 {
namespace internal {

class HeapNumber;
class BigInt;

namespace compiler {

class Node;

}  // namespace compiler

struct UntaggedT {};

struct IntegralT : UntaggedT {};

struct WordT : IntegralT {
  static const MachineRepresentation kMachineRepresentation =
      MachineType::PointerRepresentation();
};

struct RawPtrT : WordT {
  static constexpr MachineType kMachineType = MachineType::Pointer();
};

// A RawPtrT that is guaranteed to point into the sandbox.
struct SandboxedPtrT : WordT {
  static constexpr MachineType kMachineType = MachineType::SandboxedPointer();
};

template <class To>
struct RawPtr : RawPtrT {};

struct Word32T : IntegralT {
  static const MachineRepresentation kMachineRepresentation =
      MachineRepresentation::kWord32;
};
struct Int32T : Word32T {
  static constexpr MachineType kMachineType = MachineType::Int32();
};
struct Uint32T : Word32T {
  static constexpr MachineType kMachineType = MachineType::Uint32();
};
struct Int16T : Int32T {
  static constexpr MachineType kMachineType = MachineType::Int16();
};
struct Uint16T : Uint32T, Int32T {
  static constexpr MachineType kMachineType = MachineType::Uint16();
};
struct Int8T : Int16T {
  static constexpr MachineType kMachineType = MachineType::Int8();
};
struct Uint8T : Uint16T, Int16T {
  static constexpr MachineType kMachineType = MachineType::Uint8();
};

struct Word64T : IntegralT {
  static const MachineRepresentation kMachineRepresentation =
      MachineRepresentation::kWord64;
};
struct Int64T : Word64T {
  static constexpr MachineType kMachineType = MachineType::Int64();
};
struct Uint64T : Word64T {
  static constexpr MachineType kMachineType = MachineType::Uint64();
};

struct IntPtrT : WordT {
  static constexpr MachineType kMachineType = MachineType::IntPtr();
};
struct UintPtrT : WordT {
  static constexpr MachineType kMachineType = MachineType::UintPtr();
};

struct ExternalPointerHandleT : Uint32T {
  static constexpr MachineType kMachineType = MachineType::Uint32();
};

struct IndirectPointerHandleT : Uint32T {
  static constexpr MachineType kMachineType = MachineType::Uint32();
};

#ifdef V8_ENABLE_SANDBOX
struct ExternalPointerT : Uint32T {
  static constexpr MachineType kMachineType = MachineType::Uint32();
};
#else
struct ExternalPointerT : UntaggedT {
  static constexpr MachineType kMachineType = MachineType::Pointer();
};
#endif

struct Float16T : Word32T {
  static constexpr MachineType kMachineType = MachineType::Uint16();
};

struct Float32T : UntaggedT {
  static const MachineRepresentation kMachineRepresentation =
      MachineRepresentation::kFloat32;
  static constexpr MachineType kMachineType = MachineType::Float32();
};

struct Float64T : UntaggedT {
  static const MachineRepresentation kMachineRepresentation =
      MachineRepresentation::kFloat64;
  static constexpr MachineType kMachineType = MachineType::Float64();
};

#ifdef V8_COMPRESS_POINTERS
using TaggedT = Int32T;
#else
using TaggedT = IntPtrT;
#endif

// Result of a comparison operation.
struct BoolT : Word32T {
  static constexpr MachineType kMachineType = MachineType::Int32();
};

// Value type of a Turbofan node with two results.
template <class T1, class T2>
struct PairT {};

template <class T1, class T2>
struct UnionT;
template <typename T>
struct is_union_t : public std::false_type {};
template <typename T1, typename T2>
struct is_union_t<UnionT<T1, T2>> : public std::true_type {};

struct Simd128T : UntaggedT {
  static const MachineRepresentation kMachineRepresentation =
      MachineRepresentation::kSimd128;
  static constexpr MachineType kMachineType = MachineType::Simd128();
};

struct I8x16T : Simd128T {};
struct I16x8T : Simd128T {};
struct I32x2T : Simd128T {};

inline constexpr MachineType CommonMachineType(MachineType type1,
                                               MachineType type2) {
  return (type1 == type2) ? type1
                          : ((type1.IsTagged() && type2.IsTagged())
                                 ? MachineType::AnyTagged()
                                 : MachineType::None());
}

template <class Type, class Enable = void>
struct MachineTypeOf {
  static constexpr MachineType value = Type::kMachineType;
};

template <class Type, class Enable>
constexpr MachineType MachineTypeOf<Type, Enable>::value;

template <>
struct MachineTypeOf<Object> {
  static constexpr MachineType value = MachineType::AnyTagged();
};
template <>
struct MachineTypeOf<MaybeObject> {
  static constexpr MachineType value = MachineType::AnyTagged();
};
template <>
struct MachineTypeOf<Smi> {
  static constexpr MachineType value = MachineType::TaggedSigned();
};
template <>
struct MachineTypeOf<TaggedIndex> {
  static constexpr MachineType value = MachineType::Pointer();
};
template <class HeapObjectSubtype>
struct MachineTypeOf<
    HeapObjectSubtype,
    std::enable_if_t<std::is_base_of_v<HeapObject, HeapObjectSubtype> ||
                     std::is_base_of_v<HeapObjectLayout, HeapObjectSubtype>>> {
  static constexpr MachineType value = MachineType::TaggedPointer();
};

template <class HeapObjectSubtype>
constexpr MachineType MachineTypeOf<
    HeapObjectSubtype,
    std::enable_if_t<std::is_base_of_v<HeapObject, HeapObjectSubtype> ||
                     std::is_base_of_v<HeapObjectLayout, HeapObjectSubtype>>>::
    value;

template <>
struct MachineTypeOf<ExternalReference> {
  static constexpr MachineType value = MachineType::Pointer();
};

template <class T1, class T2>
struct MachineTypeOf<UnionT<T1, T2>> {
  static constexpr MachineType value =
      CommonMachineType(MachineTypeOf<T1>::value, MachineTypeOf<T2>::value);
};

template <class Type, class Enable = void>
struct MachineRepresentationOf {
  static const MachineRepresentation value = Type::kMachineRepresentation;
};
// If T defines kMachineType, then we take the machine representation from
// there.
template <class T>
struct MachineRepresentationOf<T, std::void_t<decltype(T::kMachineType)>> {
  static constexpr MachineRepresentation value =
      T::kMachineType.representation();
};
template <class T>
struct MachineRepresentationOf<T, std::enable_if_t<is_taggable_v<T>>> {
  static constexpr MachineRepresentation value =
      MachineTypeOf<T>::value.representation();
};
template <>
struct MachineRepresentationOf<ExternalReference> {
  static constexpr MachineRepresentation value =
      RawPtrT::kMachineRepresentation;
};

template <typename T>
constexpr bool IsMachineRepresentationOf(MachineRepresentation r) {
  return MachineRepresentationOf<T>::value == r;
}

template <class T>
constexpr MachineRepresentation PhiMachineRepresentationOf =
    std::is_base_of<Word32T, T>::value ? MachineRepresentation::kWord32
                                       : MachineRepresentationOf<T>::value;

template <class T>
struct is_valid_type_tag {
  static const bool value = is_taggable_v<T> ||
                            std::is_base_of<UntaggedT, T>::value ||
                            std::is_same<ExternalReference, T>::value;
  static const bool is_tagged = is_taggable_v<T>;
};

template <class T1, class T2>
struct is_valid_type_tag<PairT<T1, T2>> {
  static const bool value =
      is_valid_type_tag<T1>::value && is_valid_type_tag<T2>::value;
  static const bool is_tagged = false;
};

template <class T1, class T2>
struct is_valid_type_tag<UnionT<T1, T2>> {
  static const bool is_tagged =
      is_valid_type_tag<T1>::is_tagged && is_valid_type_tag<T2>::is_tagged;
  static const bool value = is_tagged;
};

template <class T1, class T2>
struct UnionT {
  static_assert(CommonMachineType(MachineTypeOf<T1>::value,
                                  MachineTypeOf<T2>::value)
                        .representation() != MachineRepresentation::kNone,
                "no common representation");
  static_assert(is_valid_type_tag<T1>::is_tagged &&
                    is_valid_type_tag<T2>::is_tagged,
                "union types are only possible for tagged values");
};

using AnyTaggedT = UnionT<Object, MaybeObject>;
using Number = UnionT<Smi, HeapNumber>;
using Numeric = UnionT<Number, BigInt>;
using ContextOrEmptyContext = UnionT<Context, Smi>;

// A pointer to a builtin function, used by Torque's function pointers.
using BuiltinPtr = Smi;

template <class T1, class T2, class U>
struct is_subtype<UnionT<T1, T2>, U, 1>
    : public std::conjunction<is_subtype<T1, U>, is_subtype<T2, U>> {};
template <class T, class U1, class U2>
struct is_subtype<T, UnionT<U1, U2>, 1>
    : public std::disjunction<is_subtype<T, U1>, is_subtype<T, U2>> {};
template <class T1, class T2, class U1, class U2>
struct is_subtype<UnionT<T1, T2>, UnionT<U1, U2>, 1>
    : public std::conjunction<is_subtype<T1, UnionT<U1, U2>>,
                              is_subtype<T2, UnionT<U1, U2>>> {};
template <>
struct is_subtype<ExternalReference, RawPtrT> {
  static const bool value = true;
};

template <class T, class U>
struct types_have_common_values {
  static const bool value = is_subtype<T, U>::value || is_subtype<U, T>::value;
};
template <class U>
struct types_have_common_values<BoolT, U> {
  static const bool value = types_have_common_values<Word32T, U>::value;
};
template <class U>
struct types_have_common_values<Uint32T, U> {
  static const bool value = types_have_common_values<Word32T, U>::value;
};
template <class U>
struct types_have_common_values<Int32T, U> {
  static const bool value = types_have_common_values<Word32T, U>::value;
};
template <class U>
struct types_have_common_values<Uint64T, U> {
  static const bool value = types_have_common_values<Word64T, U>::value;
};
template <class U>
struct types_have_common_values<Int64T, U> {
  static const bool value = types_have_common_values<Word64T, U>::value;
};
template <class U>
struct types_have_common_values<IntPtrT, U> {
  static const bool value = types_have_common_values<WordT, U>::value;
};
template <class U>
struct types_have_common_values<UintPtrT, U> {
  static const bool value = types_have_common_values<WordT, U>::value;
};
template <class T1, class T2, class U>
struct types_have_common_values<UnionT<T1, T2>, U> {
  static const bool value = types_have_common_values<T1, U>::value ||
                            types_have_common_values<T2, U>::value;
};

template <class T, class U1, class U2>
struct types_have_common_values<T, UnionT<U1, U2>> {
  static const bool value = types_have_common_values<T, U1>::value ||
                            types_have_common_values<T, U2>::value;
};
template <class T1, class T2, class U1, class U2>
struct types_have_common_values<UnionT<T1, T2>, UnionT<U1, U2>> {
  static const bool value = types_have_common_values<T1, U1>::value ||
                            types_have_common_values<T1, U2>::value ||
                            types_have_common_values<T2, U1>::value ||
                            types_have_common_values<T2, U2>::value;
};

// TNode<T> is an SSA value with the static type tag T, which is one of the
// following:
//   - MaybeObject> represents the type of all tagged values, including weak
//     pointers.
//   - a subclass of internal::Object represents a non-weak tagged type.
//   - a subclass of internal::UntaggedT represents an untagged type
//   - ExternalReference
//   - PairT<T1, T2> for an operation returning two values, with types T1
//     and T2
//   - UnionT<T1, T2> represents either a value of type T1 or of type T2.
template <class T>
class TNode {
 public:
  template <class U, typename = std::enable_if_t<is_subtype<U, T>::value>>
  TNode(const TNode<U>& other) V8_NOEXCEPT : node_(other.node_) {
    LazyTemplateChecks();
  }

  TNode(const TNode& other) V8_NOEXCEPT : node_(other.node_) {}
  TNode() : node_(nullptr) {}

  TNode operator=(TNode other) {
    DCHECK_NOT_NULL(other.node_);
    node_ = other.node_;
    return *this;
  }

  operator compiler::Node*() const { return node_; }

  explicit operator bool() const { return node_ != nullptr; }

  static TNode UncheckedCast(compiler::Node* node) { return TNode(node); }

 protected:
  template <typename U>
  friend class TNode;

  explicit TNode(compiler::Node* node) : node_(node) { LazyTemplateChecks(); }
  // These checks shouldn't be checked before TNode is actually used.
  void LazyTemplateChecks() const {
    static_assert(is_valid_type_tag<T>::value, "invalid type tag");
  }

  compiler::Node* node_;
};

template <class T>
class TNode<Tagged<T>> {
  static_assert(!std::is_same_v<T, T>,
                "Don't write TNode<Tagged<T>>, just write TNode<T> directly.");
};

// SloppyTNode<T> is a variant of TNode<T> and allows implicit casts from
// Node*. It is intended for function arguments as long as some call sites
// still use untyped Node* arguments.
// TODO(turbofan): Delete this class once transition is finished.
template <class T>
class SloppyTNode : public TNode<T> {
 public:
  SloppyTNode(compiler::Node* node)  // NOLINT(runtime/explicit)
      : TNode<T>(node) {}
  template <class U, typename std::enable_if<is_subtype<U, T>::value,
                                             int>::type = 0>
  SloppyTNode(const TNode<U>& other) V8_NOEXCEPT  // NOLINT(runtime/explicit)
      : TNode<T>(other) {}
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_TNODE_H_
