// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OBJECTS_H_
#define V8_OBJECTS_OBJECTS_H_

#include <iosfwd>
#include <memory>

#include "include/v8-internal.h"
#include "include/v8config.h"
#include "src/base/bits.h"
#include "src/base/build_config.h"
#include "src/base/flags.h"
#include "src/base/logging.h"
#include "src/base/memory.h"
#include "src/codegen/constants-arch.h"
#include "src/common/assert-scope.h"
#include "src/common/checks.h"
#include "src/common/message-template.h"
#include "src/common/operation.h"
#include "src/common/ptr-compr.h"
#include "src/flags/flags.h"
#include "src/objects/elements-kind.h"
#include "src/objects/field-index.h"
#include "src/objects/object-list-macros.h"
#include "src/objects/objects-definitions.h"
#include "src/objects/property-details.h"
#include "src/objects/tagged-impl.h"
#include "src/objects/tagged.h"
#include "src/utils/utils.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

struct InliningPosition;
class LookupIterator;
class PropertyDescriptorObject;
class ReadOnlyRoots;
class RootVisitor;
class PropertyKey;

// UNSAFE_SKIP_WRITE_BARRIER skips the write barrier.
// SKIP_WRITE_BARRIER skips the write barrier and asserts that this is safe in
// the MemoryOptimizer
// UPDATE_WRITE_BARRIER is doing the full barrier, marking and generational.
enum WriteBarrierMode {
  SKIP_WRITE_BARRIER,
  UNSAFE_SKIP_WRITE_BARRIER,
  UPDATE_EPHEMERON_KEY_WRITE_BARRIER,
  UPDATE_WRITE_BARRIER
};

// PropertyNormalizationMode is used to specify whether to keep
// inobject properties when normalizing properties of a JSObject.
enum PropertyNormalizationMode {
  CLEAR_INOBJECT_PROPERTIES,
  KEEP_INOBJECT_PROPERTIES
};

// Indicates whether transitions can be added to a source map or not.
enum TransitionFlag { INSERT_TRANSITION, OMIT_TRANSITION };

// Indicates the kind of transition: the target map of the transition
// either extends the current map with a new property, or it modifies the
// property that was added last to the current map. Otherwise, it can
// be a prototype transition, or anything else.
enum TransitionKindFlag {
  SIMPLE_PROPERTY_TRANSITION,
  PROPERTY_TRANSITION,
  PROTOTYPE_TRANSITION,
  SPECIAL_TRANSITION
};

// Indicates whether we are only interested in the descriptors of a particular
// map, or in all descriptors in the descriptor array.
enum DescriptorFlag { ALL_DESCRIPTORS, OWN_DESCRIPTORS };

// Instance size sentinel for objects of variable size.
const int kVariableSizeSentinel = 0;

// We may store the unsigned bit field as signed Smi value and do not
// use the sign bit.
const int kStubMajorKeyBits = 8;
const int kStubMinorKeyBits = kSmiValueSize - kStubMajorKeyBits - 1;

// Result of an abstract relational comparison of x and y, implemented according
// to ES6 section 7.2.11 Abstract Relational Comparison.
enum class ComparisonResult {
  kLessThan = -1,    // x < y
  kEqual = 0,        // x = y
  kGreaterThan = 1,  // x > y
  kUndefined = 2     // at least one of x or y was undefined or NaN
};

// (Returns false whenever {result} is kUndefined.)
bool ComparisonResultToBool(Operation op, ComparisonResult result);

enum class OnNonExistent { kThrowReferenceError, kReturnUndefined };

// The element types selection for CreateListFromArrayLike.
enum class ElementTypes { kAll, kStringAndSymbol };

// Currently DefineOwnPropertyIgnoreAttributes invokes the setter
// interceptor and user-defined setters during define operations,
// even in places where it makes more sense to invoke the definer
// interceptor and not invoke the setter: e.g. both the definer and
// the setter interceptors are called in Object.defineProperty().
// kDefine allows us to implement the define semantics correctly
// in selected locations.
// TODO(joyee): see if we can deprecate the old behavior.
enum class EnforceDefineSemantics { kSet, kDefine };

// TODO(mythria): Move this to a better place.
ShouldThrow GetShouldThrow(Isolate* isolate, Maybe<ShouldThrow> should_throw);

// Object is the abstract superclass for all classes in the
// object hierarchy.
// Object does not use any virtual functions to avoid the
// allocation of the C++ vtable.
// There must only be a single data member in Object: the Address ptr,
// containing the tagged heap pointer that this Object instance refers to.
// For a design overview, see https://goo.gl/Ph4CGz.
class Object : public AllStatic {
 public:
  enum class Conversion {
    kToNumber,  // Number = Smi or HeapNumber
    kToNumeric  // Numeric = Smi or HeapNumber or BigInt
  };

  // ES6, #sec-isarray.  NOT to be confused with %_IsArray.
  V8_INLINE
  V8_WARN_UNUSED_RESULT static Maybe<bool> IsArray(Handle<Object> object);

  // Extract the double value of a Number (Smi or HeapNumber).
  static inline double NumberValue(Tagged<Number> obj);
  static inline double NumberValue(Tagged<Object> obj);
  static inline double NumberValue(Tagged<HeapNumber> obj);
  static inline double NumberValue(Tagged<Smi> obj);
  V8_EXPORT_PRIVATE static bool ToInt32(Tagged<Object> obj, int32_t* value);
  static inline bool ToUint32(Tagged<Object> obj, uint32_t* value);

  static inline Representation OptimalRepresentation(
      Tagged<Object> obj, PtrComprCageBase cage_base);

  static inline ElementsKind OptimalElementsKind(Tagged<Object> obj,
                                                 PtrComprCageBase cage_base);

  // If {allow_coercion} is true, then a Smi will be considered to fit
  // a Double representation, since it can be converted to a HeapNumber
  // and stored.
  static inline bool FitsRepresentation(Tagged<Object> obj,
                                        Representation representation,
                                        bool allow_coercion = true);

  static inline bool FilterKey(Tagged<Object> obj, PropertyFilter filter);

  static Handle<FieldType> OptimalType(Tagged<Object> obj, Isolate* isolate,
                                       Representation representation);

  V8_EXPORT_PRIVATE static Handle<UnionOf<JSAny, Hole>> NewStorageFor(
      Isolate* isolate, Handle<UnionOf<JSAny, Hole>> object,
      Representation representation);

  template <AllocationType allocation_type = AllocationType::kYoung,
            typename IsolateT>
  static Handle<JSAny> WrapForRead(IsolateT* isolate, Handle<JSAny> object,
                                   Representation representation);

  // Returns true if the object is of the correct type to be used as a
  // implementation of a JSObject's elements.
  static inline bool HasValidElements(Tagged<Object> obj);

  // ECMA-262 9.2.
  template <typename IsolateT>
  V8_EXPORT_PRIVATE static bool BooleanValue(Tagged<Object> obj,
                                             IsolateT* isolate);
  static Tagged<Object> ToBoolean(Tagged<Object> obj, Isolate* isolate);

  // ES6 section 7.2.11 Abstract Relational Comparison
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static Maybe<ComparisonResult>
  Compare(Isolate* isolate, Handle<Object> x, Handle<Object> y);

  // ES6 section 7.2.12 Abstract Equality Comparison
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static Maybe<bool> Equals(
      Isolate* isolate, Handle<Object> x, Handle<Object> y);

  // ES6 section 7.2.13 Strict Equality Comparison
  V8_EXPORT_PRIVATE static bool StrictEquals(Tagged<Object> obj,
                                             Tagged<Object> that);

  // ES6 section 7.1.13 ToObject
  // Convert to a JSObject if needed.
  // native_context is used when creating wrapper object.
  //
  // Passing a non-null method_name allows us to give a more informative
  // error message for those cases where ToObject is being called on
  // the receiver of a built-in method.
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<JSReceiver> ToObject(
      Isolate* isolate, Handle<Object> object,
      const char* method_name = nullptr);
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSReceiver> ToObjectImpl(
      Isolate* isolate, DirectHandle<Object> object,
      const char* method_name = nullptr);

  // ES6 section 9.2.1.2, OrdinaryCallBindThis for sloppy callee.
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSReceiver> ConvertReceiver(
      Isolate* isolate, Handle<Object> object);

  // ES6 section 7.1.14 ToPropertyKey
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Name> ToName(
      Isolate* isolate, Handle<Object> input);

  // ES6 section 7.1.1 ToPrimitive
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> ToPrimitive(
      Isolate* isolate, Handle<Object> input,
      ToPrimitiveHint hint = ToPrimitiveHint::kDefault);

  // ES6 section 7.1.3 ToNumber
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Number> ToNumber(
      Isolate* isolate, Handle<Object> input);

  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> ToNumeric(
      Isolate* isolate, Handle<Object> input);

  // ES6 section 7.1.4 ToInteger
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Number> ToInteger(
      Isolate* isolate, Handle<Object> input);

  // ES6 section 7.1.5 ToInt32
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Number> ToInt32(
      Isolate* isolate, Handle<Object> input);

  // ES6 section 7.1.6 ToUint32
  V8_WARN_UNUSED_RESULT inline static MaybeHandle<Number> ToUint32(
      Isolate* isolate, Handle<Object> input);

  // ES6 section 7.1.12 ToString
  // TODO(b/42203211): ToString is templatized so that passing a Handle<T>
  // is not ambiguous when T is a subtype of Object (it could be implicitly
  // converted both to Handle<Object> and to DirectHandle<Object>). Here, T
  // should be a subtype of Object, which is enforced by the second template
  // argument and the similar restriction on Handle's constructor. When the
  // migration to DirectHandle is complete, this function can accept simply
  // a DirectHandle<Object>.
  template <typename T, typename = std::enable_if_t<
                            std::is_convertible_v<Handle<T>, Handle<Object>>>>
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<String> ToString(
      Isolate* isolate, Handle<T> input);

#ifdef V8_ENABLE_DIRECT_HANDLE
  template <typename T, typename = std::enable_if_t<std::is_convertible_v<
                            DirectHandle<T>, DirectHandle<Object>>>>
  V8_WARN_UNUSED_RESULT static inline MaybeDirectHandle<String> ToString(
      Isolate* isolate, DirectHandle<T> input);
#endif

  V8_EXPORT_PRIVATE static MaybeDirectHandle<String> NoSideEffectsToMaybeString(
      Isolate* isolate, DirectHandle<Object> input);

  V8_EXPORT_PRIVATE static DirectHandle<String> NoSideEffectsToString(
      Isolate* isolate, DirectHandle<Object> input);

  // ES6 section 7.1.14 ToPropertyKey
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> ToPropertyKey(
      Isolate* isolate, Handle<Object> value);

  // ES6 section 7.1.15 ToLength
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> ToLength(
      Isolate* isolate, Handle<Object> input);

  // ES6 section 7.1.17 ToIndex
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> ToIndex(
      Isolate* isolate, Handle<Object> input, MessageTemplate error_index);

  // ES6 section 7.3.9 GetMethod
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> GetMethod(
      Isolate* isolate, Handle<JSReceiver> receiver, Handle<Name> name);

  // ES6 section 7.3.17 CreateListFromArrayLike
  V8_WARN_UNUSED_RESULT static MaybeHandle<FixedArray> CreateListFromArrayLike(
      Isolate* isolate, Handle<Object> object, ElementTypes element_types);

  // Get length property and apply ToLength.
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> GetLengthFromArrayLike(
      Isolate* isolate, Handle<JSReceiver> object);

  // ES6 section 12.5.6 The typeof Operator
  static Handle<String> TypeOf(Isolate* isolate, DirectHandle<Object> object);

  // ES6 section 12.7 Additive Operators
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> Add(Isolate* isolate,
                                                       Handle<Object> lhs,
                                                       Handle<Object> rhs);

  // ES6 section 12.9 Relational Operators
  V8_WARN_UNUSED_RESULT static inline Maybe<bool> GreaterThan(Isolate* isolate,
                                                              Handle<Object> x,
                                                              Handle<Object> y);
  V8_WARN_UNUSED_RESULT static inline Maybe<bool> GreaterThanOrEqual(
      Isolate* isolate, Handle<Object> x, Handle<Object> y);
  V8_WARN_UNUSED_RESULT static inline Maybe<bool> LessThan(Isolate* isolate,
                                                           Handle<Object> x,
                                                           Handle<Object> y);
  V8_WARN_UNUSED_RESULT static inline Maybe<bool> LessThanOrEqual(
      Isolate* isolate, Handle<Object> x, Handle<Object> y);

  // ES6 section 7.3.19 OrdinaryHasInstance (C, O).
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> OrdinaryHasInstance(
      Isolate* isolate, Handle<Object> callable, Handle<Object> object);

  // ES6 section 12.10.4 Runtime Semantics: InstanceofOperator(O, C)
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> InstanceOf(
      Isolate* isolate, Handle<Object> object, Handle<Object> callable);

  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Object>
  GetProperty(LookupIterator* it, bool is_global_reference = false);

  // ES6 [[Set]] (when passed kDontThrow)
  // Invariants for this and related functions (unless stated otherwise):
  // 1) When the result is Nothing, an exception is pending.
  // 2) When passed kThrowOnError, the result is never Just(false).
  // In some cases, an exception is thrown regardless of the ShouldThrow
  // argument.  These cases are either in accordance with the spec or not
  // covered by it (eg., concerning API callbacks).
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static Maybe<bool> SetProperty(
      LookupIterator* it, Handle<Object> value, StoreOrigin store_origin,
      Maybe<ShouldThrow> should_throw = Nothing<ShouldThrow>());
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Object>
  SetProperty(Isolate* isolate, Handle<Object> object, Handle<Name> name,
              Handle<Object> value,
              StoreOrigin store_origin = StoreOrigin::kMaybeKeyed,
              Maybe<ShouldThrow> should_throw = Nothing<ShouldThrow>());
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> SetPropertyOrElement(
      Isolate* isolate, Handle<Object> object, Handle<Name> name,
      Handle<Object> value,
      Maybe<ShouldThrow> should_throw = Nothing<ShouldThrow>(),
      StoreOrigin store_origin = StoreOrigin::kMaybeKeyed);

  V8_WARN_UNUSED_RESULT static Maybe<bool> SetSuperProperty(
      LookupIterator* it, Handle<Object> value, StoreOrigin store_origin,
      Maybe<ShouldThrow> should_throw = Nothing<ShouldThrow>());

  V8_WARN_UNUSED_RESULT static Maybe<bool> CannotCreateProperty(
      Isolate* isolate, Handle<Object> receiver, Handle<Object> name,
      DirectHandle<Object> value, Maybe<ShouldThrow> should_throw);
  V8_WARN_UNUSED_RESULT static Maybe<bool> WriteToReadOnlyProperty(
      LookupIterator* it, DirectHandle<Object> value,
      Maybe<ShouldThrow> should_throw);
  V8_WARN_UNUSED_RESULT static Maybe<bool> WriteToReadOnlyProperty(
      Isolate* isolate, Handle<Object> receiver, Handle<Object> name,
      DirectHandle<Object> value, ShouldThrow should_throw);
  V8_WARN_UNUSED_RESULT static Maybe<bool> RedefineIncompatibleProperty(
      Isolate* isolate, Handle<Object> name, DirectHandle<Object> value,
      Maybe<ShouldThrow> should_throw);
  V8_WARN_UNUSED_RESULT static Maybe<bool> SetDataProperty(
      LookupIterator* it, Handle<Object> value);
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static Maybe<bool> AddDataProperty(
      LookupIterator* it, DirectHandle<Object> value,
      PropertyAttributes attributes, Maybe<ShouldThrow> should_throw,
      StoreOrigin store_origin,
      EnforceDefineSemantics semantics = EnforceDefineSemantics::kSet);

  V8_WARN_UNUSED_RESULT static Maybe<bool> TransitionAndWriteDataProperty(
      LookupIterator* it, DirectHandle<Object> value,
      PropertyAttributes attributes, Maybe<ShouldThrow> should_throw,
      StoreOrigin store_origin);

  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> GetPropertyOrElement(
      Isolate* isolate, Handle<Object> object, Handle<Name> name);
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> GetPropertyOrElement(
      Handle<Object> receiver, Handle<Name> name, Handle<JSReceiver> holder);
  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> GetProperty(
      Isolate* isolate, Handle<Object> object, Handle<Name> name);

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSAny> GetPropertyWithAccessor(
      LookupIterator* it);
  V8_WARN_UNUSED_RESULT static Maybe<bool> SetPropertyWithAccessor(
      LookupIterator* it, Handle<Object> value,
      Maybe<ShouldThrow> should_throw);

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSAny> GetPropertyWithDefinedGetter(
      Handle<Object> receiver, Handle<JSReceiver> getter);
  V8_WARN_UNUSED_RESULT static Maybe<bool> SetPropertyWithDefinedSetter(
      Handle<Object> receiver, Handle<JSReceiver> setter, Handle<Object> value,
      Maybe<ShouldThrow> should_throw);

  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> GetElement(
      Isolate* isolate, Handle<Object> object, uint32_t index);

  V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> SetElement(
      Isolate* isolate, Handle<Object> object, uint32_t index,
      Handle<Object> value, ShouldThrow should_throw);

  // Returns the permanent hash code associated with this object. May return
  // undefined if not yet created.
  static inline Tagged<Object> GetHash(Tagged<Object> obj);

  // Returns the permanent hash code associated with this object depending on
  // the actual object type. May create and store a hash code if needed and none
  // exists.
  V8_EXPORT_PRIVATE static Tagged<Smi> GetOrCreateHash(Tagged<Object> obj,
                                                       Isolate* isolate);

  // Checks whether this object has the same value as the given one.  This
  // function is implemented according to ES5, section 9.12 and can be used
  // to implement the Object.is function.
  V8_EXPORT_PRIVATE static bool SameValue(Tagged<Object> obj,
                                          Tagged<Object> other);

  // A part of SameValue which handles Number vs. Number case.
  // Treats NaN == NaN and +0 != -0.
  inline static bool SameNumberValue(double number1, double number2);

  // Checks whether this object has the same value as the given one.
  // +0 and -0 are treated equal. Everything else is the same as SameValue.
  // This function is implemented according to ES6, section 7.2.4 and is used
  // by ES6 Map and Set.
  static bool SameValueZero(Tagged<Object> obj, Tagged<Object> other);

  // ES6 section 9.4.2.3 ArraySpeciesCreate (part of it)
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> ArraySpeciesConstructor(
      Isolate* isolate, Handle<Object> original_array);

  // ES6 section 7.3.20 SpeciesConstructor ( O, defaultConstructor )
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> SpeciesConstructor(
      Isolate* isolate, Handle<JSReceiver> recv,
      Handle<JSFunction> default_ctor);

  // Tries to convert an object to an array length. Returns true and sets the
  // output parameter if it succeeds.
  static inline bool ToArrayLength(Tagged<Object> obj, uint32_t* index);

  // Tries to convert an object to an array index. Returns true and sets the
  // output parameter if it succeeds. Equivalent to ToArrayLength, but does not
  // allow kMaxUInt32.
  static V8_WARN_UNUSED_RESULT inline bool ToArrayIndex(Tagged<Object> obj,
                                                        uint32_t* index);

  // Tries to convert an object to an index (in the range 0..size_t::max).
  // Returns true and sets the output parameter if it succeeds.
  static inline bool ToIntegerIndex(Tagged<Object> obj, size_t* index);

  // Returns true if the result of iterating over the object is the same
  // (including observable effects) as simply accessing the properties between 0
  // and length.
  V8_EXPORT_PRIVATE static bool IterationHasObservableEffects(
      Tagged<Object> obj);

  // TC39 "Dynamic Code Brand Checks"
  static bool IsCodeLike(Tagged<Object> obj, Isolate* isolate);

  EXPORT_DECL_STATIC_VERIFIER(Object)

#ifdef VERIFY_HEAP
  // Verify a pointer is a valid (non-InstructionStream) object pointer.
  // When V8_EXTERNAL_CODE_SPACE is enabled InstructionStream objects are
  // not allowed.
  static void VerifyPointer(Isolate* isolate, Tagged<Object> p);
  // Verify a pointer is a valid (non-InstructionStream) object pointer,
  // potentially a weak one.
  // When V8_EXTERNAL_CODE_SPACE is enabled InstructionStream objects are
  // not allowed.
  static void VerifyMaybeObjectPointer(Isolate* isolate, Tagged<MaybeObject> p);
  // Verify a pointer is a valid object pointer.
  // InstructionStream objects are allowed regardless of the
  // V8_EXTERNAL_CODE_SPACE mode.
  static void VerifyAnyTagged(Isolate* isolate, Tagged<Object> p);
#endif

  // Layout description.
  static const int kHeaderSize = 0;  // Object does not take up any space.

  // For use with std::unordered_set.
  struct Hasher {
    size_t operator()(const Tagged<Object> o) const {
      return std::hash<v8::internal::Address>{}(static_cast<Tagged_t>(o.ptr()));
    }
  };

  // For use with std::unordered_set/unordered_map when one of the objects may
  // be located outside the main pointer compression cage, for example in
  // trusted space. In this case, we must use full pointer comparison.
  struct KeyEqualSafe {
    bool operator()(const Tagged<Object> a, const Tagged<Object> b) const {
      return a.SafeEquals(b);
    }
  };

  // For use with std::map.
  struct Comparer {
    bool operator()(const Tagged<Object> a, const Tagged<Object> b) const {
      return a < b;
    }
  };

  // Same as above, but can be used when one of the objects may be located
  // outside of the main pointer compression cage, for example in trusted
  // space. In this case, we must use full pointer comparison.
  struct FullPtrComparer {
    bool operator()(const Tagged<Object> a, const Tagged<Object> b) const {
      return a.ptr() < b.ptr();
    }
  };

  // If the receiver is the JSGlobalObject, the store was contextual. In case
  // the property did not exist yet on the global object itself, we have to
  // throw a reference error in strict mode.  In sloppy mode, we continue.
  // Returns false if the exception was thrown, otherwise true.
  static bool CheckContextualStoreToJSGlobalObject(
      LookupIterator* it, Maybe<ShouldThrow> should_throw);

  // Returns an equivalent value that's safe to share across Isolates if
  // possible. Acts as the identity function when value->IsShared().
  static inline MaybeHandle<Object> Share(
      Isolate* isolate, Handle<Object> value,
      ShouldThrow throw_if_cannot_be_shared);

  static MaybeHandle<Object> ShareSlow(Isolate* isolate,
                                       Handle<HeapObject> value,
                                       ShouldThrow throw_if_cannot_be_shared);

  // Whether this Object can be held weakly, i.e. whether it can be used as a
  // key in WeakMap, as a key in WeakSet, as the target of a WeakRef, or as a
  // target or unregister token of a FinalizationRegistry.
  static inline bool CanBeHeldWeakly(Tagged<Object> obj);

 private:
  friend class CompressedObjectSlot;
  friend class FullObjectSlot;
  friend class LookupIterator;
  friend class StringStream;

  // Return the map of the root of object's prototype chain.
  static Tagged<Map> GetPrototypeChainRootMap(Tagged<Object> obj,
                                              Isolate* isolate);

  // Returns a non-SMI for JSReceivers, but returns the hash code forp
  // simple objects.  This avoids a double lookup in the cases where
  // we know we will add the hash to the JSReceiver if it does not
  // already exist.
  //
  // Despite its size, this needs to be inlined for performance
  // reasons.
  static inline Tagged<Object> GetSimpleHash(Tagged<Object> object);

  // Helper for SetProperty and SetSuperProperty.
  // Return value is only meaningful if [found] is set to true on return.
  V8_WARN_UNUSED_RESULT static Maybe<bool> SetPropertyInternal(
      LookupIterator* it, Handle<Object> value, Maybe<ShouldThrow> should_throw,
      StoreOrigin store_origin, bool* found);

  V8_WARN_UNUSED_RESULT static MaybeHandle<Name> ConvertToName(
      Isolate* isolate, Handle<Object> input);
  V8_WARN_UNUSED_RESULT static MaybeHandle<Object> ConvertToPropertyKey(
      Isolate* isolate, Handle<Object> value);
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<String>
  ConvertToString(Isolate* isolate, Handle<Object> input);
  V8_WARN_UNUSED_RESULT static MaybeHandle<Number> ConvertToNumber(
      Isolate* isolate, Handle<Object> input);
  V8_WARN_UNUSED_RESULT static MaybeHandle<Numeric> ConvertToNumeric(
      Isolate* isolate, Handle<Object> input);
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Number>
  ConvertToInteger(Isolate* isolate, Handle<Object> input);
  V8_WARN_UNUSED_RESULT static MaybeHandle<Number> ConvertToInt32(
      Isolate* isolate, Handle<Object> input);
  V8_WARN_UNUSED_RESULT static MaybeHandle<Number> ConvertToUint32(
      Isolate* isolate, Handle<Object> input);
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Number>
  ConvertToLength(Isolate* isolate, Handle<Object> input);
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static MaybeHandle<Number>
  ConvertToIndex(Isolate* isolate, Handle<Object> input,
                 MessageTemplate error_index);
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           Tagged<Object> obj);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           Object::Conversion kind);

struct Brief {
  template <HeapObjectReferenceType kRefType>
  explicit Brief(TaggedImpl<kRefType, Address> v) : value{v.ptr()} {}
  template <typename T>
  explicit Brief(T* v) : value{v->ptr()} {}
  // {value} is a tagged heap object reference (weak or strong), equivalent to
  // a MaybeObject's payload. It has a plain Address type to keep #includes
  // lightweight.
  const Address value;
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os, const Brief& v);

// Objects should never have the weak tag; this variant is for overzealous
// checking.
V8_INLINE static bool HasWeakHeapObjectTag(const Tagged<Object> value) {
  return HAS_WEAK_HEAP_OBJECT_TAG(value.ptr());
}

// For compatibility with TaggedImpl, and users of this header that don't pull
// in objects-inl.h
// TODO(leszeks): Remove once no longer needed.
template <HeapObjectReferenceType kRefType, typename StorageType>
V8_INLINE constexpr bool IsObject(TaggedImpl<kRefType, StorageType> obj) {
  return obj.IsObject();
}
template <HeapObjectReferenceType kRefType, typename StorageType>
V8_INLINE constexpr bool IsSmi(TaggedImpl<kRefType, StorageType> obj) {
  return obj.IsSmi();
}
template <HeapObjectReferenceType kRefType, typename StorageType>
V8_INLINE constexpr bool IsHeapObject(TaggedImpl<kRefType, StorageType> obj) {
  return obj.IsHeapObject();
}
template <typename StorageType>
V8_INLINE constexpr bool IsWeak(
    TaggedImpl<HeapObjectReferenceType::WEAK, StorageType> obj) {
  return obj.IsWeak();
}

// TODO(leszeks): These exist both as free functions and members of Tagged. They
// probably want to be cleaned up at some point.
V8_INLINE bool IsSmi(Tagged<Object> obj) { return obj.IsSmi(); }
V8_INLINE bool IsSmi(Tagged<HeapObject> obj) { return false; }
V8_INLINE bool IsSmi(Tagged<Smi> obj) { return true; }

V8_INLINE bool IsHeapObject(Tagged<Object> obj) { return obj.IsHeapObject(); }
V8_INLINE bool IsHeapObject(Tagged<HeapObject> obj) { return true; }
V8_INLINE bool IsHeapObject(Tagged<Smi> obj) { return false; }

V8_INLINE bool IsTaggedIndex(Tagged<Object> obj);

#define IS_TYPE_FUNCTION_DECL(Type)            \
  V8_INLINE bool Is##Type(Tagged<Object> obj); \
  V8_INLINE bool Is##Type(Tagged<Object> obj, PtrComprCageBase cage_base);
OBJECT_TYPE_LIST(IS_TYPE_FUNCTION_DECL)
HEAP_OBJECT_TYPE_LIST(IS_TYPE_FUNCTION_DECL)
IS_TYPE_FUNCTION_DECL(HashTableBase)
IS_TYPE_FUNCTION_DECL(SmallOrderedHashTable)
IS_TYPE_FUNCTION_DECL(PropertyDictionary)
#undef IS_TYPE_FUNCTION_DECL
V8_INLINE bool IsNumber(Tagged<Object> obj, ReadOnlyRoots roots);

// A wrapper around IsHole to make it easier to distinguish from specific hole
// checks (e.g. IsTheHole).
V8_INLINE bool IsAnyHole(Tagged<Object> obj, PtrComprCageBase cage_base);
V8_INLINE bool IsAnyHole(Tagged<Object> obj);

// Oddball checks are faster when they are raw pointer comparisons, so the
// isolate/read-only roots overloads should be preferred where possible.
#define IS_TYPE_FUNCTION_DECL(Type, Value, _)                         \
  V8_INLINE bool Is##Type(Tagged<Object> obj, Isolate* isolate);      \
  V8_INLINE bool Is##Type(Tagged<Object> obj, LocalIsolate* isolate); \
  V8_INLINE bool Is##Type(Tagged<Object> obj, ReadOnlyRoots roots);   \
  V8_INLINE bool Is##Type(Tagged<Object> obj);
ODDBALL_LIST(IS_TYPE_FUNCTION_DECL)
HOLE_LIST(IS_TYPE_FUNCTION_DECL)
IS_TYPE_FUNCTION_DECL(NullOrUndefined, , /* unused */)
#undef IS_TYPE_FUNCTION_DECL

V8_INLINE bool IsZero(Tagged<Object> obj);
V8_INLINE bool IsNoSharedNameSentinel(Tagged<Object> obj);
V8_INLINE bool IsPrivateSymbol(Tagged<Object> obj);
V8_INLINE bool IsPublicSymbol(Tagged<Object> obj);
#if !V8_ENABLE_WEBASSEMBLY
// Dummy implementation on builds without WebAssembly.
template <typename T>
V8_INLINE bool IsWasmObject(T obj, Isolate* = nullptr) {
  return false;
}
#endif

V8_INLINE bool IsJSObjectThatCanBeTrackedAsPrototype(Tagged<Object> obj);
V8_INLINE bool IsJSObjectThatCanBeTrackedAsPrototype(Tagged<HeapObject> obj);

V8_INLINE bool IsJSApiWrapperObject(Tagged<HeapObject> obj);
V8_INLINE bool IsJSApiWrapperObject(Tagged<Map> map);

#define DECL_STRUCT_PREDICATE(NAME, Name, name) \
  V8_INLINE bool Is##Name(Tagged<Object> obj);  \
  V8_INLINE bool Is##Name(Tagged<Object> obj, PtrComprCageBase cage_base);
STRUCT_LIST(DECL_STRUCT_PREDICATE)
#undef DECL_STRUCT_PREDICATE

V8_INLINE bool IsNaN(Tagged<Object> obj);
V8_INLINE bool IsMinusZero(Tagged<Object> obj);

// Returns whether the object is safe to share across Isolates.
//
// Currently, the following kinds of values can be safely shared across
// Isolates:
// - Smis
// - Objects in RO space when the RO space is shared
// - HeapNumbers in the shared old space
// - Strings for which String::IsShared() is true
// - JSSharedStructs
// - JSSharedArrays
inline bool IsShared(Tagged<Object> obj);

// Prints this object without details.
V8_EXPORT_PRIVATE void ShortPrint(Tagged<Object> obj, FILE* out = stdout);

// Prints this object without details to a message accumulator.
V8_EXPORT_PRIVATE void ShortPrint(Tagged<Object> obj,
                                  StringStream* accumulator);

V8_EXPORT_PRIVATE void ShortPrint(Tagged<Object> obj, std::ostream& os);

#ifdef OBJECT_PRINT
// For our gdb macros, we should perhaps change these in the future.
V8_EXPORT_PRIVATE void Print(Tagged<Object> obj);

// Prints this object with details.
V8_EXPORT_PRIVATE void Print(Tagged<Object> obj, std::ostream& os);

#else
inline void Print(Tagged<Object> obj) { ShortPrint(obj); }
inline void Print(Tagged<Object> obj, std::ostream& os) { ShortPrint(obj, os); }
#endif

// Heap objects typically have a map pointer in their first word.  However,
// during GC other data (e.g. mark bits, forwarding addresses) is sometimes
// encoded in the first word.  The class MapWord is an abstraction of the
// value in a heap object's first word.
//
// When external code space is enabled forwarding pointers are encoded as
// Smi values representing a diff from the source or map word host object
// address in kObjectAlignment chunks. Such a representation has the following
// properties:
// a) it can hold both positive an negative diffs for full pointer compression
//    cage size (HeapObject address has only valuable 30 bits while Smis have
//    31 bits),
// b) it's independent of the pointer compression base and pointer compression
//    scheme.
class MapWord {
 public:
  // Normal state: the map word contains a map pointer.

  // Create a map word from a map pointer.
  static inline MapWord FromMap(const Tagged<Map> map);

  // View this map word as a map pointer.
  inline Tagged<Map> ToMap() const;

  // Scavenge collection: the map word of live objects in the from space
  // contains a forwarding address (a heap object pointer in the to space).

  // True if this map word is a forwarding address for a scavenge
  // collection.  Only valid during a scavenge collection (specifically,
  // when all map words are heap object pointers, i.e. not during a full GC).
  inline bool IsForwardingAddress() const;

  V8_EXPORT_PRIVATE static bool IsMapOrForwarded(Tagged<Map> map);

  // Create a map word from a forwarding address.
  static inline MapWord FromForwardingAddress(Tagged<HeapObject> map_word_host,
                                              Tagged<HeapObject> object);

  // View this map word as a forwarding address.
  inline Tagged<HeapObject> ToForwardingAddress(
      Tagged<HeapObject> map_word_host);

  constexpr inline Address ptr() const { return value_; }

  // When pointer compression is enabled, MapWord is uniquely identified by
  // the lower 32 bits. On the other hand full-value comparison is not correct
  // because map word in a forwarding state might have corrupted upper part.
  constexpr bool operator==(MapWord other) const {
    return static_cast<Tagged_t>(ptr()) == static_cast<Tagged_t>(other.ptr());
  }
  constexpr bool operator!=(MapWord other) const {
    return static_cast<Tagged_t>(ptr()) != static_cast<Tagged_t>(other.ptr());
  }

#ifdef V8_MAP_PACKING
  static constexpr Address Pack(Address map) {
    return map ^ Internals::kMapWordXorMask;
  }
  static constexpr Address Unpack(Address mapword) {
    // TODO(wenyuzhao): Clear header metadata.
    return mapword ^ Internals::kMapWordXorMask;
  }
  static constexpr bool IsPacked(Address mapword) {
    return (static_cast<intptr_t>(mapword) & Internals::kMapWordXorMask) ==
               Internals::kMapWordSignature &&
           (0xffffffff00000000 & static_cast<intptr_t>(mapword)) != 0;
  }
#else
  static constexpr bool IsPacked(Address) { return false; }
#endif

 private:
  // HeapObject calls the private constructor and directly reads the value.
  friend class HeapObject;
  template <typename TFieldType, int kFieldOffset, typename CompressionScheme>
  friend class TaggedField;

  explicit constexpr MapWord(Address value) : value_(value) {}

  Address value_;
};

template <int start_offset, int end_offset, int size>
class FixedBodyDescriptor;

template <int start_offset>
class FlexibleBodyDescriptor;

template <int start_offset>
class FlexibleWeakBodyDescriptor;

template <class ParentBodyDescriptor, class ChildBodyDescriptor>
class SubclassBodyDescriptor;

enum EnsureElementsMode {
  DONT_ALLOW_DOUBLE_ELEMENTS,
  ALLOW_COPIED_DOUBLE_ELEMENTS,
  ALLOW_CONVERTED_DOUBLE_ELEMENTS
};

// Indicator for one component of an AccessorPair.
enum AccessorComponent { ACCESSOR_GETTER, ACCESSOR_SETTER };

// Utility superclass for stack-allocated objects that must be updated
// on gc.  It provides two ways for the gc to update instances, either
// iterating or updating after gc.
class Relocatable {
 public:
  explicit inline Relocatable(Isolate* isolate);
  inline virtual ~Relocatable();
  virtual void IterateInstance(RootVisitor* v) {}
  virtual void PostGarbageCollection() {}

  static void PostGarbageCollectionProcessing(Isolate* isolate);
  static int ArchiveSpacePerThread();
  static char* ArchiveState(Isolate* isolate, char* to);
  static char* RestoreState(Isolate* isolate, char* from);
  static void Iterate(Isolate* isolate, RootVisitor* v);
  static void Iterate(RootVisitor* v, Relocatable* top);
  static char* Iterate(RootVisitor* v, char* t);

 private:
  Isolate* isolate_;
  Relocatable* prev_;
};

// BooleanBit is a helper class for setting and getting a bit in an integer.
class BooleanBit : public AllStatic {
 public:
  static inline bool get(int value, int bit_position) {
    return (value & (1 << bit_position)) != 0;
  }

  static inline int set(int value, int bit_position, bool v) {
    if (v) {
      value |= (1 << bit_position);
    } else {
      value &= ~(1 << bit_position);
    }
    return value;
  }
};

// This is an RAII helper class to emit a store-store memory barrier when
// publishing objects allocated in the shared heap.
//
// This helper must be used in every Factory method that allocates a shared
// JSObject visible user JS code. This is also used in Object::ShareSlow when
// publishing newly shared JS primitives.
//
// While there is no default ordering guarantee for shared JS objects
// (e.g. without the use of Atomics methods or postMessage, data races on
// fields are observable), the internal VM state of a JS object must be safe
// for publishing so that other threads do not crash.
//
// This barrier does not provide synchronization for publishing JS shared
// objects. It only ensures the weaker "do not crash the VM" guarantee.
//
// In particular, note that memory barriers are invisible to TSAN. When
// concurrent marking is active, field accesses are performed with relaxed
// atomics, and TSAN is unable to detect data races in shared JS objects. When
// concurrent marking is inactive, unordered publishes of shared JS objects in
// JS code are reported as data race warnings by TSAN.
class V8_NODISCARD SharedObjectSafePublishGuard final {
 public:
  ~SharedObjectSafePublishGuard() {
    // A release fence is used to prevent store-store reorderings of stores to
    // VM-internal state of shared objects past any subsequent stores (i.e. the
    // publish).
    //
    // On the loading side, we rely on neither the compiler nor the CPU
    // reordering loads that are dependent on observing the address of the
    // published shared object, like fields of the shared object.
    std::atomic_thread_fence(std::memory_order_release);
  }
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_OBJECTS_H_
