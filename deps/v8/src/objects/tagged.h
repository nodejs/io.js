// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TAGGED_H_
#define V8_OBJECTS_TAGGED_H_

#include <type_traits>

#include "src/common/globals.h"
#include "src/objects/tagged-impl.h"
#include "src/objects/union.h"

namespace v8 {
namespace internal {

class BigInt;
class FieldType;
class HeapObject;
class HeapNumber;
class HeapObjectLayout;
class Object;
class TaggedIndex;
class Smi;

// Tagged<T> represents an uncompressed V8 tagged pointer.
//
// The tagged pointer is a pointer-sized value with a tag in the LSB. The value
// is either:
//
//   * A small integer (Smi), shifted right, with the tag set to 0
//   * A strong pointer to an object on the V8 heap, with the tag set to 01
//   * A weak pointer to an object on the V8 heap, with the tag set to 11
//   * A cleared weak pointer, with the value 11
//
// The exact encoding differs depending on 32- vs 64-bit architectures, and in
// the latter case, whether or not pointer compression is enabled.
//
// On 32-bit architectures, this is:
//             |----- 32 bits -----|
// Pointer:    |______address____w1|
//    Smi:     |____int31_value___0|
//
// On 64-bit architectures with pointer compression:
//             |----- 32 bits -----|----- 32 bits -----|
// Pointer:    |________base_______|______offset_____w1|
//    Smi:     |......garbage......|____int31_value___0|
//
// On 64-bit architectures without pointer compression:
//             |----- 32 bits -----|----- 32 bits -----|
// Pointer:    |________________address______________w1|
//    Smi:     |____int32_value____|00...............00|
//
// where `w` is the "weak" bit.
//
// We specialise Tagged separately for Object, Smi and HeapObject, and then all
// other types T, so that:
//
//                    Tagged<Object> -> StrongTaggedBase
//                       Tagged<Smi> -> StrongTaggedBase
//   Tagged<T> -> Tagged<HeapObject> -> StrongTaggedBase
//
// We also specialize it separately for MaybeWeak types, with a parallel
// hierarchy:
//
//                               Tagged<MaybeWeak<Object>> -> WeakTaggedBase
//                                  Tagged<MaybeWeak<Smi>> -> WeakTaggedBase
//   Tagged<MaybeWeak<T>> -> Tagged<MaybeWeak<HeapObject>> -> WeakTaggedBase
template <typename T>
class Tagged;

// MaybeWeak<T> represents a reference to T that may be either a strong or weak.
//
// MaybeWeak doesn't really exist by itself, but is rather a sentinel type for
// templates on tagged interfaces (like Tagged). For example, where Tagged<T>
// represents a strong reference to T, Tagged<MaybeWeak<T>> represents a
// potentially weak reference to T, and it is the responsibility of the Tagged
// interface to provide some mechanism (likely template specialization) to
// distinguish between the two and provide accessors to the T reference itself
// (which will always be strong).
template <typename T>
class MaybeWeak {};

template <typename T>
struct is_maybe_weak : public std::false_type {};
template <typename T>
struct is_maybe_weak<MaybeWeak<T>> : public std::true_type {};
template <typename T>
static constexpr bool is_maybe_weak_v = is_maybe_weak<T>::value;

// ClearedWeakValue is a sentinel type for cleared weak values.
class ClearedWeakValue {};

// Convert a strong reference to T into a weak reference to T.
template <typename T>
inline Tagged<MaybeWeak<T>> MakeWeak(Tagged<T> value);
template <typename T>
inline Tagged<MaybeWeak<T>> MakeWeak(Tagged<MaybeWeak<T>> value);

// Convert a weak reference to T into a strong reference to T.
template <typename T>
inline Tagged<T> MakeStrong(Tagged<T> value);
template <typename T>
inline Tagged<T> MakeStrong(Tagged<MaybeWeak<T>> value);

// Base class for all Tagged<T> classes.
using StrongTaggedBase = TaggedImpl<HeapObjectReferenceType::STRONG, Address>;
using WeakTaggedBase = TaggedImpl<HeapObjectReferenceType::WEAK, Address>;

// `is_subtype<Derived, Base>::value` is true when Derived is a subtype of Base
// according to our object hierarchy. In particular, Smi is considered a
// subtype of Object.
template <typename Derived, typename Base, typename Enabled = void>
struct is_subtype;
template <typename Derived, typename Base>
static constexpr bool is_subtype_v = is_subtype<Derived, Base>::value;

namespace detail {
template <typename Derived, typename Base, typename Enabled = void>
struct is_simple_subtype;
template <typename Derived, typename Base, typename Enabled = void>
struct is_complex_subtype;
}  // namespace detail

// `is_subtype<Derived, Base>` tries is_simple_subtype first, and if that fails,
// is_complex_subtype. This is to prevent instantiating the is_complex_subtype
// template when is_simple_subtype, to avoid trying std::is_base_of. This allows
// subtype checks to pass, for simple subtypes, with forward declarations.
template <typename Derived, typename Base, typename Enabled>
struct is_subtype
    : public std::disjunction<detail::is_simple_subtype<Derived, Base>,
                              detail::is_complex_subtype<Derived, Base>> {};

// Forward declarations for is_simple_subtype hack, remove once those
// specializations are removed.
class FixedArrayBase;
class FixedArray;
class FixedDoubleArray;
class ByteArray;
class NameDictionary;
class NumberDictionary;
class OrderedHashMap;
class OrderedHashSet;
class OrderedNameDictionary;
class ScriptContextTable;
class ArrayList;

namespace detail {
// `is_simple_subtype<Derived, Base>::value` is true when Derived is a simple
// subtype of Base according to our object hierarchy, in a way that doesn't
// require object definitions (in particular, we don't need to known anything
// about C++ base classes). False, in this case, doesn't mean "not a subtype",
// it just means "not a _simple_ subtype".
template <typename Derived, typename Base, typename Enabled>
struct is_simple_subtype : public std::false_type {};
template <typename Derived, typename Base>
static constexpr bool is_simple_subtype_v =
    is_simple_subtype<Derived, Base>::value;

template <typename T>
struct is_simple_subtype<T, T> : public std::true_type {};
template <>
struct is_simple_subtype<Object, Object> : public std::true_type {};
template <>
struct is_simple_subtype<Smi, Object> : public std::true_type {};
template <>
struct is_simple_subtype<TaggedIndex, Object> : public std::true_type {};
template <>
struct is_simple_subtype<FieldType, Object> : public std::true_type {};
template <>
struct is_simple_subtype<HeapObject, Object> : public std::true_type {};
template <>
struct is_simple_subtype<HeapObjectLayout, Object> : public std::true_type {};
template <typename T>
struct is_simple_subtype<T, MaybeWeak<T>> : public std::true_type {};
template <typename T>
struct is_simple_subtype<MaybeWeak<T>, MaybeWeak<T>> : public std::true_type {};
template <typename T>
struct is_simple_subtype<ClearedWeakValue, MaybeWeak<T>>
    : public std::true_type {};

// Specializations of is_simple_subtype for Union, which allows for trivial
// subtype checks of Unions without recursing into the full is_subtype trait,
// which might require object definitions.
//
// A couple of redundant looking specializations are necessary to disambiguate
// specializations when there are two Unions.
template <typename Derived, typename... BaseTs>
struct is_simple_subtype<Derived, Union<BaseTs...>>
    : public std::disjunction<is_simple_subtype<Derived, BaseTs>...> {};
template <typename... DerivedTs, typename Base>
struct is_simple_subtype<Union<DerivedTs...>, Base>
    : public std::conjunction<is_simple_subtype<DerivedTs, Base>...> {};
template <typename... DerivedTs, typename... BaseTs>
struct is_simple_subtype<Union<DerivedTs...>, Union<BaseTs...>>
    : public std::conjunction<
          is_simple_subtype<DerivedTs, Union<BaseTs...>>...> {};
template <typename... Ts>
struct is_simple_subtype<Union<Ts...>, Union<Ts...>> : public std::true_type {};

// TODO(jgruber): Clean up this artificial FixedArrayBase hierarchy. Only types
// that can be used as elements should be in it.
// TODO(jgruber): Replace FixedArrayBase with a union type, once they exist.
#define DEF_FIXED_ARRAY_SUBTYPE(Subtype)                                      \
  template <>                                                                 \
  struct is_simple_subtype<Subtype, FixedArrayBase> : public std::true_type { \
  };
DEF_FIXED_ARRAY_SUBTYPE(FixedArray)
DEF_FIXED_ARRAY_SUBTYPE(FixedDoubleArray)
DEF_FIXED_ARRAY_SUBTYPE(ByteArray)
DEF_FIXED_ARRAY_SUBTYPE(NameDictionary)
DEF_FIXED_ARRAY_SUBTYPE(NumberDictionary)
DEF_FIXED_ARRAY_SUBTYPE(OrderedHashMap)
DEF_FIXED_ARRAY_SUBTYPE(OrderedHashSet)
DEF_FIXED_ARRAY_SUBTYPE(OrderedNameDictionary)
DEF_FIXED_ARRAY_SUBTYPE(ScriptContextTable)
DEF_FIXED_ARRAY_SUBTYPE(ArrayList)
#undef DEF_FIXED_ARRAY_SUBTYPE

// `is_complex_subtype<Derived, Base>::value` is true when Derived is a
// non-simple subtype of Base according to our object hierarchy, in a way that
// might require object definitions or recursion into is_subtype (in particular,
// we do need to know about C++ base classes).
//
// This doesn't check the simple cases, so should not be used directly, but
// only via is_subtype.
template <typename Derived, typename Base, typename Enabled>
struct is_complex_subtype : public std::is_base_of<Base, Derived> {};
template <typename Derived, typename Base>
static constexpr bool is_complex_subtype_v =
    is_complex_subtype<Derived, Base>::value;

template <typename Derived>
struct is_complex_subtype<
    Derived, Object,
    std::enable_if_t<std::conjunction_v<std::negation<is_union<Derived>>,
                                        is_subtype<Derived, HeapObject>>>>
    : public std::true_type {};
template <typename Derived>
struct is_complex_subtype<Derived, HeapObject,
                          std::enable_if_t<std::disjunction_v<
                              std::is_base_of<HeapObject, Derived>,
                              std::is_base_of<HeapObjectLayout, Derived>>>>
    : public std::true_type {};

template <typename Derived, typename... BaseTs>
struct is_complex_subtype<Derived, Union<BaseTs...>>
    : public std::disjunction<is_subtype<Derived, BaseTs>...> {};
template <typename... DerivedTs, typename Base>
struct is_complex_subtype<Union<DerivedTs...>, Base>
    : public std::conjunction<is_subtype<DerivedTs, Base>...> {};
template <typename... DerivedTs, typename... BaseTs>
struct is_complex_subtype<Union<DerivedTs...>, Union<BaseTs...>>
    : public std::conjunction<is_subtype<DerivedTs, Union<BaseTs...>>...> {};
template <typename Derived, typename Base>
struct is_complex_subtype<
    Derived, MaybeWeak<Base>,
    std::enable_if_t<!is_union_v<Derived> && !is_maybe_weak_v<Derived>>>
    : public is_subtype<Derived, Base> {};
template <typename Derived, typename Base>
struct is_complex_subtype<MaybeWeak<Derived>, MaybeWeak<Base>>
    : public is_subtype<Derived, Base> {};
}  // namespace detail

static_assert(is_subtype_v<Smi, Object>);
static_assert(is_subtype_v<HeapObject, Object>);
static_assert(is_subtype_v<HeapObject, HeapObject>);

// `is_taggable<T>::value` is true when T is a valid type for Tagged. This means
// de-facto being a subtype of Object.
template <typename T>
using is_taggable = is_subtype<T, MaybeWeak<Object>>;
template <typename T>
static constexpr bool is_taggable_v = is_taggable<T>::value;

// `is_castable<From, To>::value` is true when you can use `::cast` to cast from
// From to To. This means an upcast or downcast, which in practice means
// checking `is_subtype` symmetrically.
template <typename From, typename To>
using is_castable =
    std::disjunction<is_subtype<To, From>, is_subtype<From, To>>;
template <typename From, typename To>
static constexpr bool is_castable_v = is_castable<From, To>::value;

// TODO(leszeks): Remove this once there are no more conversions between
// Tagged<Foo> and Foo.
static constexpr bool kTaggedCanConvertToRawObjects = true;

namespace detail {

// {TaggedOperatorArrowRef} is returned by {Tagged::operator->}. It should never
// be stored anywhere or used in any other code; no one should ever have to
// spell out {TaggedOperatorArrowRef} in code. Its only purpose is to be
// dereferenced immediately by "operator-> chaining". Returning the address of
// the field is valid because this objects lifetime only ends at the end of the
// full statement.
template <typename T>
class TaggedOperatorArrowRef {
 public:
  V8_INLINE constexpr T* operator->() { return &object_; }

 private:
  friend class Tagged<T>;
  V8_INLINE constexpr explicit TaggedOperatorArrowRef(T object)
      : object_(object) {}
  T object_;
};

template <typename T>
struct BaseForTagged {
  using type = Tagged<HeapObject>;
};

template <typename T>
struct BaseForTagged<MaybeWeak<T>> {
  using type = Tagged<MaybeWeak<HeapObject>>;
};

template <typename... T>
struct BaseForTagged<Union<T...>> {
  template <typename U>
  using is_non_heap_object =
      std::disjunction<std::is_same<U, Smi>, std::is_same<U, Object>,
                       std::is_same<U, TaggedIndex>,
                       std::is_same<U, FieldType>>;

  using type = std::conditional_t<
      std::disjunction_v<is_maybe_weak<T>...>, WeakTaggedBase,
      std::conditional_t<std::disjunction_v<is_non_heap_object<T>...>,
                         Tagged<Object>, Tagged<HeapObject>>>;
};

// FieldType is special, since it can be Smi or Map. It could probably even be
// its own specialization, to avoid exposing an operator->.
template <>
struct BaseForTagged<FieldType> {
  using type = Tagged<Object>;
};

}  // namespace detail

// Specialization for Object, where it's unknown whether this is a Smi or a
// HeapObject.
template <>
class Tagged<Object> : public StrongTaggedBase {
 public:
  // Allow Tagged<Object> to be created from any address.
  V8_INLINE constexpr explicit Tagged(Address o) : StrongTaggedBase(o) {}

  // Allow explicit uninitialized initialization.
  // TODO(leszeks): Consider zapping this instead, since it's odd that
  // Tagged<Object> implicitly initialises to Smi::zero().
  V8_INLINE constexpr Tagged() : StrongTaggedBase(kNullAddress) {}

  // Allow implicit conversion from const HeapObjectLayout* to Tagged<Object>.
  // TODO(leszeks): Make this more const-correct.
  // TODO(leszeks): Consider making this an explicit conversion.
  // NOLINTNEXTLINE
  V8_INLINE Tagged(const HeapObjectLayout* ptr)
      : Tagged(reinterpret_cast<Address>(ptr) + kHeapObjectTag) {}

  // Implicit conversion for subclasses -- all classes are subclasses of Object,
  // so allow all tagged pointers.
  // NOLINTNEXTLINE
  V8_INLINE constexpr Tagged(StrongTaggedBase other)
      : StrongTaggedBase(other.ptr()) {}
  V8_INLINE constexpr Tagged& operator=(StrongTaggedBase other) {
    return *this = Tagged(other);
  }
};

// Specialization for Smi disallowing any implicit creation or access via ->,
// but offering instead a cast from Object and an int32_t value() method.
template <>
class Tagged<Smi> : public StrongTaggedBase {
 public:
  V8_INLINE constexpr Tagged() = default;
  V8_INLINE constexpr explicit Tagged(Address ptr) : StrongTaggedBase(ptr) {}

  // No implicit conversions from other tagged pointers.

  V8_INLINE constexpr bool IsHeapObject() const { return false; }
  V8_INLINE constexpr bool IsSmi() const { return true; }

  V8_INLINE constexpr int32_t value() const {
    return Internals::SmiValue(ptr());
  }
};

// Specialization for TaggedIndex disallowing any implicit creation or access
// via ->, but offering instead a cast from Object and an intptr_t value()
// method.
template <>
class Tagged<TaggedIndex> : public StrongTaggedBase {
 public:
  V8_INLINE constexpr Tagged() = default;
  V8_INLINE constexpr explicit Tagged(Address ptr) : StrongTaggedBase(ptr) {}

  // No implicit conversions from other tagged pointers.

  V8_INLINE constexpr bool IsHeapObject() const { return false; }
  V8_INLINE constexpr bool IsSmi() const { return true; }

  // Returns the integer value.
  V8_INLINE constexpr intptr_t value() const {
    // Truncate and shift down (requires >> to be sign extending).
    return static_cast<intptr_t>(ptr()) >> kSmiTagSize;
  }

  // Implicit conversions to/from raw pointers
  // TODO(leszeks): Remove once we're using Tagged everywhere.
  // NOLINTNEXTLINE
  V8_INLINE constexpr Tagged(TaggedIndex raw);

 private:
  // Handles of the same type are allowed to access the Address constructor.
  friend class Handle<TaggedIndex>;
#ifdef V8_ENABLE_DIRECT_HANDLE
  friend class DirectHandle<TaggedIndex>;
#endif
  template <typename TFieldType, int kFieldOffset, typename CompressionScheme>
  friend class TaggedField;
};

// Specialization for HeapObject, to group together functions shared between all
// HeapObjects
template <>
class Tagged<HeapObject> : public StrongTaggedBase {
  using Base = StrongTaggedBase;

 public:
  V8_INLINE constexpr Tagged() = default;
  // Allow implicit conversion from const HeapObjectLayout* to
  // Tagged<HeapObject>.
  // TODO(leszeks): Make this more const-correct.
  // TODO(leszeks): Consider making this an explicit conversion.
  // NOLINTNEXTLINE
  V8_INLINE Tagged(const HeapObjectLayout* ptr)
      : Tagged(reinterpret_cast<Address>(ptr) + kHeapObjectTag) {}

  // Implicit conversion for subclasses.
  template <typename U,
            typename = std::enable_if_t<is_subtype_v<U, HeapObject>>>
  V8_INLINE constexpr Tagged& operator=(Tagged<U> other) {
    return *this = Tagged(other);
  }

  // Implicit conversion for subclasses.
  template <typename U,
            typename = std::enable_if_t<is_subtype_v<U, HeapObject>>>
  // NOLINTNEXTLINE
  V8_INLINE constexpr Tagged(Tagged<U> other) : Base(other) {}

  V8_INLINE constexpr HeapObject operator*() const;
  V8_INLINE constexpr detail::TaggedOperatorArrowRef<HeapObject> operator->()
      const;

  V8_INLINE constexpr bool is_null() const {
    return static_cast<Tagged_t>(this->ptr()) ==
           static_cast<Tagged_t>(kNullAddress);
  }

  constexpr V8_INLINE bool IsHeapObject() const { return true; }
  constexpr V8_INLINE bool IsSmi() const { return false; }

  // Implicit conversions and explicit casts to/from raw pointers
  // TODO(leszeks): Remove once we're using Tagged everywhere.
  template <typename U,
            typename = std::enable_if_t<is_subtype_v<U, HeapObject>>>
  // NOLINTNEXTLINE
  constexpr Tagged(U raw) : Base(raw.ptr()) {
    static_assert(kTaggedCanConvertToRawObjects);
  }
  template <typename U>
  static constexpr Tagged<HeapObject> cast(U other) {
    static_assert(kTaggedCanConvertToRawObjects);
    return Cast<HeapObject>(Tagged<U>(other));
  }

  Address address() const { return this->ptr() - kHeapObjectTag; }

 protected:
  V8_INLINE constexpr explicit Tagged(Address ptr) : Base(ptr) {}

 private:
  friend class HeapObject;
  // Handles of the same type are allowed to access the Address constructor.
  friend class Handle<HeapObject>;
#ifdef V8_ENABLE_DIRECT_HANDLE
  friend class DirectHandle<HeapObject>;
#endif
  template <typename TFieldType, int kFieldOffset, typename CompressionScheme>
  friend class TaggedField;
  template <typename To, typename From>
  friend inline Tagged<To> UncheckedCast(Tagged<From> value);

  friend Tagged<HeapObject> MakeStrong<>(Tagged<HeapObject> value);
  friend Tagged<HeapObject> MakeStrong<>(Tagged<MaybeWeak<HeapObject>> value);

  V8_INLINE constexpr HeapObject ToRawPtr() const;
};

static_assert(Tagged<HeapObject>().is_null());

// Specialization for MaybeWeak<Object>, where it's unknown whether this is a
// Smi, a strong HeapObject, or a weak HeapObject
template <>
class Tagged<MaybeWeak<Object>> : public WeakTaggedBase {
 public:
  // Allow Tagged<MaybeWeak<Object>> to be created from any address.
  V8_INLINE constexpr explicit Tagged(Address o) : WeakTaggedBase(o) {}

  // Allow explicit uninitialized initialization.
  // TODO(leszeks): Consider zapping this instead, since it's odd that
  // Tagged<MaybeWeak<Object>> implicitly initialises to Smi::zero().
  V8_INLINE constexpr Tagged() : WeakTaggedBase(kNullAddress) {}

  // Allow implicit conversion from const HeapObjectLayout* to
  // Tagged<MaybeWeak<Object>>.
  // TODO(leszeks): Make this more const-correct.
  // TODO(leszeks): Consider making this an explicit conversion.
  // NOLINTNEXTLINE
  V8_INLINE Tagged(const HeapObjectLayout* ptr)
      : Tagged(reinterpret_cast<Address>(ptr) + kHeapObjectTag) {}

  // Implicit conversion for subclasses -- all classes are subclasses of Object,
  // so allow all tagged pointers, both weak and strong.
  // NOLINTNEXTLINE
  V8_INLINE constexpr Tagged(WeakTaggedBase other)
      : WeakTaggedBase(other.ptr()) {}
  // NOLINTNEXTLINE
  V8_INLINE constexpr Tagged(StrongTaggedBase other)
      : WeakTaggedBase(other.ptr()) {}
  V8_INLINE constexpr Tagged& operator=(WeakTaggedBase other) {
    return *this = Tagged(other);
  }
  V8_INLINE constexpr Tagged& operator=(StrongTaggedBase other) {
    return *this = Tagged(other);
  }
};

// Specialization for MaybeWeak<HeapObject>, to group together functions shared
// between all HeapObjects
template <>
class Tagged<MaybeWeak<HeapObject>> : public WeakTaggedBase {
  using Base = WeakTaggedBase;

 public:
  V8_INLINE constexpr Tagged() = default;
  // Allow implicit conversion from const HeapObjectLayout* to
  // Tagged<HeapObject>.
  // TODO(leszeks): Make this more const-correct.
  // TODO(leszeks): Consider making this an explicit conversion.
  // NOLINTNEXTLINE
  V8_INLINE Tagged(const HeapObjectLayout* ptr)
      : Tagged(reinterpret_cast<Address>(ptr) + kHeapObjectTag) {}

  // Implicit conversion for subclasses.
  template <typename U,
            typename = std::enable_if_t<is_subtype_v<U, MaybeWeak<HeapObject>>>>
  V8_INLINE constexpr Tagged& operator=(Tagged<U> other) {
    return *this = Tagged(other);
  }

  // Implicit conversion for subclasses.
  template <typename U,
            typename = std::enable_if_t<is_subtype_v<U, MaybeWeak<HeapObject>>>>
  // NOLINTNEXTLINE
  V8_INLINE constexpr Tagged(Tagged<U> other) : Base(other.ptr()) {}

  template <typename U,
            typename = std::enable_if_t<is_subtype_v<U, MaybeWeak<HeapObject>>>>
  V8_INLINE explicit constexpr Tagged(Tagged<U> other,
                                      HeapObjectReferenceType type)
      : Base(type == HeapObjectReferenceType::WEAK ? MakeWeak(other)
                                                   : MakeStrong(other)) {}

  V8_INLINE constexpr bool is_null() const {
    return static_cast<Tagged_t>(this->ptr()) ==
           static_cast<Tagged_t>(kNullAddress);
  }

  constexpr V8_INLINE bool IsSmi() const { return false; }

 protected:
  V8_INLINE constexpr explicit Tagged(Address ptr) : Base(ptr) {}

 private:
  // Handles of the same type are allowed to access the Address constructor.
  friend class Handle<MaybeWeak<HeapObject>>;
#ifdef V8_ENABLE_DIRECT_HANDLE
  friend class DirectHandle<MaybeWeak<HeapObject>>;
#endif
  template <typename To, typename From>
  friend inline Tagged<To> UncheckedCast(Tagged<From> value);

  friend Tagged<MaybeWeak<HeapObject>> MakeWeak<>(Tagged<HeapObject> value);
  friend Tagged<MaybeWeak<HeapObject>> MakeWeak<>(
      Tagged<MaybeWeak<HeapObject>> value);
};

// Generic Tagged<T> for Unions. This doesn't allow direct access to the object,
// aside from casting.
template <typename... Ts>
class Tagged<Union<Ts...>> : public detail::BaseForTagged<Union<Ts...>>::type {
  using This = Union<Ts...>;
  using Base = typename detail::BaseForTagged<This>::type;

 public:
  V8_INLINE constexpr Tagged() = default;

  // Implicit conversion for subclasses.
  template <typename U, typename = std::enable_if_t<is_subtype_v<U, This>>>
  V8_INLINE constexpr Tagged& operator=(Tagged<U> other) {
    *this = Tagged(other);
    return *this;
  }

  // Implicit conversion for subclasses.
  template <typename U, typename = std::enable_if_t<is_subtype_v<U, This>>>
  // NOLINTNEXTLINE
  V8_INLINE constexpr Tagged(Tagged<U> other) : Base(other.ptr()) {}

  // Implicit conversions and explicit casts to/from raw pointers
  // TODO(leszeks): Remove once we're using Tagged everywhere.
  template <typename U, typename = std::enable_if_t<is_subtype_v<U, This>>>
  // NOLINTNEXTLINE
  V8_INLINE constexpr Tagged(U raw) : Base(raw.ptr()) {
    static_assert(kTaggedCanConvertToRawObjects);
  }

 private:
  // Handles of the same type are allowed to access the Address constructor.
  friend class Handle<This>;
#ifdef V8_ENABLE_DIRECT_HANDLE
  friend class DirectHandle<This>;
#endif
  template <typename TFieldType, int kFieldOffset, typename CompressionScheme>
  friend class TaggedField;
  template <typename TFieldType, typename CompressionScheme>
  friend class TaggedMember;
  template <typename To, typename From>
  friend inline Tagged<To> UncheckedCast(Tagged<From> value);

  V8_INLINE constexpr explicit Tagged(Address ptr) : Base(ptr) {}
};

// Generic Tagged<T> for any T that is a subclass of HeapObject. There are
// separate Tagged<T> specialaizations for T==Smi and T==Object, so we know that
// all other Tagged<T> are definitely pointers and not Smis.
template <typename T>
class Tagged : public detail::BaseForTagged<T>::type {
  using Base = typename detail::BaseForTagged<T>::type;

 public:
  V8_INLINE constexpr Tagged() = default;
  template <typename U = T>
  // Allow implicit conversion from const T* to Tagged<T>.
  // TODO(leszeks): Make this more const-correct.
  // TODO(leszeks): Consider making this an explicit conversion.
  // NOLINTNEXTLINE
  V8_INLINE Tagged(const T* ptr)
      : Tagged(reinterpret_cast<Address>(ptr) + kHeapObjectTag) {
    static_assert(std::is_base_of_v<HeapObjectLayout, U>);
  }

  // Implicit conversion for subclasses.
  template <typename U, typename = std::enable_if_t<is_subtype_v<U, T>>>
  V8_INLINE constexpr Tagged& operator=(Tagged<U> other) {
    *this = Tagged(other);
    return *this;
  }

  // Implicit conversion for subclasses.
  template <typename U, typename = std::enable_if_t<is_subtype_v<U, T>>>
  // NOLINTNEXTLINE
  V8_INLINE constexpr Tagged(Tagged<U> other) : Base(other) {}

  template <typename U = T,
            typename = std::enable_if_t<std::is_base_of_v<HeapObjectLayout, U>>>
  V8_INLINE T& operator*() const {
    return *ToRawPtr();
  }
  template <typename U = T,
            typename = std::enable_if_t<std::is_base_of_v<HeapObjectLayout, U>>>
  V8_INLINE T* operator->() const {
    return ToRawPtr();
  }

  template <typename U = T, typename = std::enable_if_t<
                                !std::is_base_of_v<HeapObjectLayout, U>>>
  V8_INLINE constexpr T operator*() const {
    return ToRawPtr();
  }
  template <typename U = T, typename = std::enable_if_t<
                                !std::is_base_of_v<HeapObjectLayout, U>>>
  V8_INLINE constexpr detail::TaggedOperatorArrowRef<T> operator->() const {
    return detail::TaggedOperatorArrowRef<T>{ToRawPtr()};
  }

  // Implicit conversions and explicit casts to/from raw pointers
  // TODO(leszeks): Remove once we're using Tagged everywhere.
  template <typename U, typename = std::enable_if_t<is_subtype_v<U, T>>>
  // NOLINTNEXTLINE
  V8_INLINE constexpr Tagged(U raw) : Base(raw.ptr()) {
    static_assert(kTaggedCanConvertToRawObjects);
  }
  template <typename U>
  static constexpr Tagged<T> cast(U other) {
    static_assert(kTaggedCanConvertToRawObjects);
    return Cast<T>(Tagged<U>(other));
  }

 private:
  friend T;
  // Handles of the same type are allowed to access the Address constructor.
  friend class Handle<T>;
#ifdef V8_ENABLE_DIRECT_HANDLE
  friend class DirectHandle<T>;
#endif
  template <typename TFieldType, int kFieldOffset, typename CompressionScheme>
  friend class TaggedField;
  template <typename TFieldType, typename CompressionScheme>
  friend class TaggedMember;
  template <typename To, typename From>
  friend inline Tagged<To> UncheckedCast(Tagged<From> value);

  friend Tagged<T> MakeStrong<>(Tagged<T> value);
  friend Tagged<T> MakeStrong<>(Tagged<MaybeWeak<T>> value);

  V8_INLINE constexpr explicit Tagged(Address ptr) : Base(ptr) {}

  template <typename U = T,
            typename = std::enable_if_t<std::is_base_of_v<HeapObjectLayout, U>>>
  V8_INLINE T* ToRawPtr() const {
    // Check whether T is taggable on raw ptr access rather than top-level, to
    // allow forward declarations.
    static_assert(is_taggable_v<T>);
    return reinterpret_cast<T*>(this->ptr() - kHeapObjectTag);
  }

  template <typename U = T, typename = std::enable_if_t<
                                !std::is_base_of_v<HeapObjectLayout, U>>>
  V8_INLINE constexpr T ToRawPtr() const {
    // Check whether T is taggable on raw ptr access rather than top-level, to
    // allow forward declarations.
    static_assert(is_taggable_v<T>);
    return T(this->ptr(), typename T::SkipTypeCheckTag{});
  }
};

// Specialized Tagged<T> for cleared weak values. This is only used, in
// practice, for conversions from Tagged<ClearedWeakValue> to a
// Tagged<MaybeWeak<T>>, where subtyping rules mean that this works for
// aribitrary T.
template <>
class Tagged<ClearedWeakValue> : public WeakTaggedBase {
 public:
  V8_INLINE explicit Tagged(Address ptr) : WeakTaggedBase(ptr) {}
};

// Generic Tagged<T> for any T that is a subclass of HeapObject. There are
// separate Tagged<T> specializations for T==Smi and T==Object, so we know that
// all other Tagged<T> are definitely pointers and not Smis.
template <typename T>
class Tagged<MaybeWeak<T>> : public detail::BaseForTagged<MaybeWeak<T>>::type {
  using Base = typename detail::BaseForTagged<MaybeWeak<T>>::type;

 public:
  V8_INLINE constexpr Tagged() = default;
  template <typename U = T>
  // Allow implicit conversion from const T* to Tagged<MaybeWeak<T>>.
  // TODO(leszeks): Make this more const-correct.
  // TODO(leszeks): Consider making this an explicit conversion.
  // NOLINTNEXTLINE
  V8_INLINE Tagged(const T* ptr)
      : Tagged(reinterpret_cast<Address>(ptr) + kHeapObjectTag) {
    static_assert(std::is_base_of_v<HeapObjectLayout, U>);
  }

  // Implicit conversion for subclasses.
  template <typename U, typename = std::enable_if_t<is_subtype_v<U, T>>>
  V8_INLINE constexpr Tagged& operator=(Tagged<U> other) {
    *this = Tagged(other);
    return *this;
  }

  // Implicit conversion for subclasses.
  template <typename U, typename = std::enable_if_t<is_subtype_v<U, T>>>
  // NOLINTNEXTLINE
  V8_INLINE constexpr Tagged(Tagged<U> other) : Base(other) {}

 private:
  V8_INLINE constexpr explicit Tagged(Address ptr) : Base(ptr) {}

  friend T;
  // Handles of the same type are allowed to access the Address constructor.
  friend class Handle<MaybeWeak<T>>;
#ifdef V8_ENABLE_DIRECT_HANDLE
  friend class DirectHandle<MaybeWeak<T>>;
#endif
  friend Tagged<MaybeWeak<T>> MakeWeak<>(Tagged<T> value);
  friend Tagged<MaybeWeak<T>> MakeWeak<>(Tagged<MaybeWeak<T>> value);
  template <typename To, typename From>
  friend inline Tagged<To> UncheckedCast(Tagged<From> value);
};

using MaybeObject = MaybeWeak<Object>;
using HeapObjectReference = MaybeWeak<HeapObject>;

template <typename T>
inline Tagged<MaybeWeak<T>> MakeWeak(Tagged<T> value) {
  return Tagged<MaybeWeak<T>>(value.ptr() | kWeakHeapObjectTag);
}

template <typename T>
inline Tagged<MaybeWeak<T>> MakeWeak(Tagged<MaybeWeak<T>> value) {
  return Tagged<MaybeWeak<T>>(value.ptr() | kWeakHeapObjectTag);
}

template <typename T>
inline Tagged<T> MakeStrong(Tagged<T> value) {
  return Tagged<T>(value.ptr() & (~kWeakHeapObjectTag | kHeapObjectTag));
}

template <typename T>
inline Tagged<T> MakeStrong(Tagged<MaybeWeak<T>> value) {
  return Tagged<T>(value.ptr() & (~kWeakHeapObjectTag | kHeapObjectTag));
}

// Deduction guide to simplify Foo->Tagged<Foo> transition.
// TODO(leszeks): Remove once we're using Tagged everywhere.
static_assert(kTaggedCanConvertToRawObjects);
template <class T>
Tagged(T object) -> Tagged<T>;

Tagged(const HeapObjectLayout* object) -> Tagged<HeapObject>;

template <class T>
Tagged(const T* object) -> Tagged<T>;
template <class T>
Tagged(T* object) -> Tagged<T>;

template <typename T>
struct RemoveTagged {
  using type = T;
};

template <typename T>
struct RemoveTagged<Tagged<T>> {
  using type = T;
};

}  // namespace internal
}  // namespace v8

namespace std {

// Template specialize std::common_type to always return Object when compared
// against a subtype of Object.
//
// This is an incomplete specialization for objects and common_type, but
// sufficient for existing use-cases. A proper specialization would need to be
// conditionally enabled via `requires`, which is C++20, or with `enable_if`,
// which would require a custom common_type implementation.
template <class T>
struct common_type<T, i::Object> {
  static_assert(i::is_subtype_v<T, i::Object>,
                "common_type with Object is only partially specialized.");
  using type = i::Object;
};

}  // namespace std

#endif  // V8_OBJECTS_TAGGED_H_
