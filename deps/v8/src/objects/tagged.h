// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TAGGED_H_
#define V8_OBJECTS_TAGGED_H_

#include <type_traits>

#include "src/common/checks.h"
#include "src/common/globals.h"
#include "src/objects/tagged-impl.h"

namespace v8 {
namespace internal {

class Object;
class Smi;
class HeapObject;
class HeapObjectLayout;
class TaggedIndex;
class FieldType;

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

// `is_maybe_weak<T>::value` is true when T is a MaybeWeak type.
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

// `is_subtype<Derived, Base>::value` is true when Derived is a subtype of Base
// according to our object hierarchy. In particular, Smi is considered a subtype
// of Object.
//
// A `precedence` parameter allows for specializations which are otherwise
// ambiguous with one of the specializations here to be applied after checking
// all of these specializations.
template <typename Derived, typename Base, unsigned precedence = 0,
          typename Enabled = void>
struct is_subtype
    : public std::conditional_t<precedence == 1, std::is_base_of<Base, Derived>,
                                is_subtype<Derived, Base, precedence + 1>> {
  static_assert(precedence <= 1);
};
template <typename Derived, typename Base>
static constexpr bool is_subtype_v = is_subtype<Derived, Base>::value;

template <>
struct is_subtype<Object, Object, 0> : public std::true_type {};
template <>
struct is_subtype<Smi, Object, 0> : public std::true_type {};
template <>
struct is_subtype<TaggedIndex, Object, 0> : public std::true_type {};
template <>
struct is_subtype<FieldType, Object, 0> : public std::true_type {};
template <typename Base>
struct is_subtype<Base, Object, 0,
                  std::enable_if_t<std::disjunction_v<
                      std::is_base_of<HeapObject, Base>,
                      std::is_base_of<HeapObjectLayout, Base>>>>
    : public std::true_type {};
template <typename Base>
struct is_subtype<Base, HeapObject, 0,
                  std::enable_if_t<std::disjunction_v<
                      std::is_base_of<HeapObject, Base>,
                      std::is_base_of<HeapObjectLayout, Base>>>>
    : public std::true_type {};
// Any strong reference to a value is also a maybe weak reference.
template <typename T, typename U>
struct is_subtype<T, MaybeWeak<U>, 0> : public is_subtype<T, U> {};
template <typename T, typename U>
struct is_subtype<MaybeWeak<T>, MaybeWeak<U>, 0> : public is_subtype<T, U> {};
// ClearedWeakValue can be used in place of any weak value, hence it behaves as
// a subtype of all cleared values.
template <typename T>
struct is_subtype<ClearedWeakValue, MaybeWeak<T>, 0> : public std::true_type {};

// TODO(jgruber): Clean up this artificial FixedArrayBase hierarchy. Only types
// that can be used as elements should be in it.
// TODO(jgruber): Replace FixedArrayBase with a union type, once they exist.
class FixedArrayBase;
#define DEF_FIXED_ARRAY_SUBTYPE(Subtype) \
  class Subtype;                         \
  template <>                            \
  struct is_subtype<Subtype, FixedArrayBase> : public std::true_type {};
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

// Base class for all Tagged<T> classes.
using StrongTaggedBase = TaggedImpl<HeapObjectReferenceType::STRONG, Address>;
using WeakTaggedBase = TaggedImpl<HeapObjectReferenceType::WEAK, Address>;

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
  // Allow all strong casts -- all classes are subclasses of Object, nothing to
  // check here.
  static V8_INLINE constexpr Tagged<Object> cast(StrongTaggedBase other) {
    return Tagged<Object>(other);
  }
  static V8_INLINE constexpr Tagged<Object> cast(WeakTaggedBase other) {
    DCHECK(other.IsObject());
    return Tagged<Object>(other.ptr());
  }
  static V8_INLINE constexpr Tagged<Object> unchecked_cast(
      StrongTaggedBase other) {
    return Tagged<Object>(other);
  }
  static V8_INLINE constexpr Tagged<Object> unchecked_cast(
      WeakTaggedBase other) {
    return Tagged<Object>(other.ptr());
  }

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
  // Explicit cast for sub- and superclasses (in practice, only Object will pass
  // this static assert).
  template <typename U>
  static constexpr Tagged<Smi> cast(Tagged<U> other) {
    static_assert(is_castable_v<U, Smi>);
    DCHECK(other.IsSmi());
    return Tagged<Smi>(other.ptr());
  }
  V8_INLINE static constexpr Tagged<Smi> unchecked_cast(
      StrongTaggedBase other) {
    return Tagged<Smi>(other.ptr());
  }

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
  // Explicit cast for sub- and superclasses (in practice, only Object will pass
  // this static assert).
  template <typename U>
  static constexpr Tagged<TaggedIndex> cast(Tagged<U> other) {
    static_assert(is_castable_v<U, TaggedIndex>);
    DCHECK(IsTaggedIndex(other));
    return Tagged<TaggedIndex>(other.ptr());
  }
  static V8_INLINE constexpr Tagged<TaggedIndex> unchecked_cast(
      StrongTaggedBase other) {
    return Tagged<TaggedIndex>(other.ptr());
  }

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
  // Explicit cast for sub- and superclasses.
  template <typename U>
  static constexpr Tagged<HeapObject> cast(Tagged<U> other) {
    static_assert(is_castable_v<U, HeapObject>);
    DCHECK(other.IsHeapObject());
    return Tagged<HeapObject>(other.ptr());
  }
  static V8_INLINE constexpr Tagged<HeapObject> unchecked_cast(
      StrongTaggedBase other) {
    // Don't check incoming type for unchecked casts, in case the object
    // definitions are not available.
    return Tagged<HeapObject>(other.ptr());
  }

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
    return Tagged<HeapObject>::cast(Tagged<U>(other));
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
  // Allow all casts -- all classes are subclasses of Object, nothing to check
  // here.
  static V8_INLINE constexpr Tagged<MaybeWeak<Object>> cast(
      WeakTaggedBase other) {
    return Tagged<MaybeWeak<Object>>(other);
  }
  static V8_INLINE constexpr Tagged<MaybeWeak<Object>> unchecked_cast(
      WeakTaggedBase other) {
    return Tagged<MaybeWeak<Object>>(other);
  }
  // Also allow strong cases
  static V8_INLINE constexpr Tagged<MaybeWeak<Object>> cast(
      StrongTaggedBase other) {
    return Tagged<MaybeWeak<Object>>(other);
  }
  static V8_INLINE constexpr Tagged<MaybeWeak<Object>> unchecked_cast(
      StrongTaggedBase other) {
    return Tagged<MaybeWeak<Object>>(other);
  }

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
  // Explicit cast for sub- and superclasses.
  template <typename U>
  static constexpr Tagged<MaybeWeak<HeapObject>> cast(Tagged<U> other) {
    static_assert(is_castable_v<U, HeapObject>);
    DCHECK(other.IsHeapObject());
    return Tagged<MaybeWeak<HeapObject>>(other.ptr());
  }
  template <typename U>
  static constexpr Tagged<MaybeWeak<HeapObject>> cast(
      Tagged<MaybeWeak<U>> other) {
    static_assert(is_castable_v<U, HeapObject>);
    // Allow strong, weak and cleared values, i.e. anything that's not a Smi.
    DCHECK(!other.IsSmi());
    return Tagged<MaybeWeak<HeapObject>>(other.ptr());
  }
  static V8_INLINE constexpr Tagged<MaybeWeak<HeapObject>> unchecked_cast(
      WeakTaggedBase other) {
    // Don't check incoming type for unchecked casts, in case the object
    // definitions are not available.
    return Tagged<MaybeWeak<HeapObject>>(other.ptr());
  }
  static V8_INLINE constexpr Tagged<MaybeWeak<HeapObject>> unchecked_cast(
      StrongTaggedBase other) {
    // Don't check incoming type for unchecked casts, in case the object
    // definitions are not available.
    return Tagged<MaybeWeak<HeapObject>>(other.ptr());
  }

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

  friend Tagged<MaybeWeak<HeapObject>> MakeWeak<>(Tagged<HeapObject> value);
  friend Tagged<MaybeWeak<HeapObject>> MakeWeak<>(
      Tagged<MaybeWeak<HeapObject>> value);
};

// Generic Tagged<T> for any T that is a subclass of HeapObject. There are
// separate Tagged<T> specializations for T==Smi and T==Object, so we know that
// all other Tagged<T> are definitely pointers and not Smis.
template <typename T>
class Tagged : public detail::BaseForTagged<T>::type {
  using Base = typename detail::BaseForTagged<T>::type;

 public:
  // Explicit cast for sub- and superclasses.
  template <typename U>
  static constexpr Tagged<T> cast(Tagged<U> other) {
    static_assert(is_castable_v<T, U>);
    return T::cast(other);
  }
  template <typename U>
  static constexpr Tagged<T> cast(Tagged<MaybeWeak<U>> other) {
    static_assert(is_castable_v<T, U>);
    DCHECK(other.IsStrong() || other.IsSmi());
    return T::cast(Tagged<U>(other.ptr()));
  }
  static V8_INLINE constexpr Tagged<T> unchecked_cast(StrongTaggedBase other) {
    // Don't check incoming type for unchecked casts, in case the object
    // definitions are not available.
    return Tagged<T>(other.ptr());
  }

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
    return Tagged<T>::cast(Tagged<U>(other));
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
