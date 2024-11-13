// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Review notes:
//
// - The use of macros in these inline functions may seem superfluous
// but it is absolutely needed to make sure gcc generates optimal
// code. gcc is not happy when attempting to inline too deep.
//

#ifndef V8_OBJECTS_OBJECTS_INL_H_
#define V8_OBJECTS_OBJECTS_INL_H_

#include "include/v8-internal.h"
#include "src/base/bits.h"
#include "src/base/memory.h"
#include "src/base/numbers/double.h"
#include "src/builtins/builtins.h"
#include "src/common/globals.h"
#include "src/common/ptr-compr-inl.h"
#include "src/handles/handles-inl.h"
#include "src/heap/factory.h"
#include "src/heap/heap-verifier.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/heap/read-only-heap-inl.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/bigint-inl.h"
#include "src/objects/casting.h"
#include "src/objects/deoptimization-data.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/hole-inl.h"
#include "src/objects/instance-type-checker.h"
#include "src/objects/js-proxy-inl.h"  // TODO(jkummerow): Drop.
#include "src/objects/keys.h"
#include "src/objects/literal-objects.h"
#include "src/objects/lookup-inl.h"  // TODO(jkummerow): Drop.
#include "src/objects/object-list-macros.h"
#include "src/objects/objects.h"
#include "src/objects/oddball-inl.h"
#include "src/objects/property-details.h"
#include "src/objects/property.h"
#include "src/objects/regexp-match-info-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/slots-inl.h"
#include "src/objects/slots.h"
#include "src/objects/smi-inl.h"
#include "src/objects/tagged-field-inl.h"
#include "src/objects/tagged-impl-inl.h"
#include "src/objects/tagged-index.h"
#include "src/objects/templates.h"
#include "src/roots/roots.h"
#include "src/sandbox/bounded-size-inl.h"
#include "src/sandbox/code-pointer-inl.h"
#include "src/sandbox/cppheap-pointer-inl.h"
#include "src/sandbox/external-pointer-inl.h"
#include "src/sandbox/indirect-pointer-inl.h"
#include "src/sandbox/isolate-inl.h"
#include "src/sandbox/isolate.h"
#include "src/sandbox/sandboxed-pointer-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

template <typename T>
class Managed;
template <typename T>
class TrustedManaged;

PropertyDetails::PropertyDetails(Tagged<Smi> smi) { value_ = smi.value(); }

Tagged<Smi> PropertyDetails::AsSmi() const {
  // Ensure the upper 2 bits have the same value by sign extending it. This is
  // necessary to be able to use the 31st bit of the property details.
  int value = value_ << 1;
  return Smi::FromInt(value >> 1);
}

int PropertyDetails::field_width_in_words() const {
  DCHECK_EQ(location(), PropertyLocation::kField);
  return 1;
}

bool IsTaggedIndex(Tagged<Object> obj) {
  return IsSmi(obj) &&
         TaggedIndex::IsValid(Tagged<TaggedIndex>(obj.ptr()).value());
}

bool IsJSObjectThatCanBeTrackedAsPrototype(Tagged<Object> obj) {
  return IsHeapObject(obj) &&
         IsJSObjectThatCanBeTrackedAsPrototype(Cast<HeapObject>(obj));
}

#define IS_TYPE_FUNCTION_DEF(type_)                                          \
  bool Is##type_(Tagged<Object> obj) {                                       \
    return IsHeapObject(obj) && Is##type_(Cast<HeapObject>(obj));            \
  }                                                                          \
  bool Is##type_(Tagged<Object> obj, PtrComprCageBase cage_base) {           \
    return IsHeapObject(obj) && Is##type_(Cast<HeapObject>(obj), cage_base); \
  }                                                                          \
  bool Is##type_(HeapObject obj) {                                           \
    static_assert(kTaggedCanConvertToRawObjects);                            \
    return Is##type_(Tagged<HeapObject>(obj));                               \
  }                                                                          \
  bool Is##type_(HeapObject obj, PtrComprCageBase cage_base) {               \
    static_assert(kTaggedCanConvertToRawObjects);                            \
    return Is##type_(Tagged<HeapObject>(obj), cage_base);                    \
  }                                                                          \
  bool Is##type_(const HeapObjectLayout* obj) {                              \
    return Is##type_(Tagged<HeapObject>(obj));                               \
  }                                                                          \
  bool Is##type_(const HeapObjectLayout* obj, PtrComprCageBase cage_base) {  \
    return Is##type_(Tagged<HeapObject>(obj), cage_base);                    \
  }
HEAP_OBJECT_TYPE_LIST(IS_TYPE_FUNCTION_DEF)
IS_TYPE_FUNCTION_DEF(HashTableBase)
IS_TYPE_FUNCTION_DEF(SmallOrderedHashTable)
IS_TYPE_FUNCTION_DEF(PropertyDictionary)
#undef IS_TYPE_FUNCTION_DEF

bool IsAnyHole(Tagged<Object> obj, PtrComprCageBase cage_base) {
  return IsHole(obj, cage_base);
}

bool IsAnyHole(Tagged<Object> obj) { return IsHole(obj); }

#define IS_TYPE_FUNCTION_DEF(Type, Value, _)                     \
  bool Is##Type(Tagged<Object> obj, Isolate* isolate) {          \
    return Is##Type(obj, ReadOnlyRoots(isolate));                \
  }                                                              \
  bool Is##Type(Tagged<Object> obj, LocalIsolate* isolate) {     \
    return Is##Type(obj, ReadOnlyRoots(isolate));                \
  }                                                              \
  bool Is##Type(Tagged<Object> obj) {                            \
    return IsHeapObject(obj) && Is##Type(Cast<HeapObject>(obj)); \
  }                                                              \
  bool Is##Type(Tagged<HeapObject> obj) {                        \
    return Is##Type(obj, obj->GetReadOnlyRoots());               \
  }                                                              \
  bool Is##Type(HeapObject obj) {                                \
    static_assert(kTaggedCanConvertToRawObjects);                \
    return Is##Type(Tagged<HeapObject>(obj));                    \
  }                                                              \
  bool Is##Type(const HeapObjectLayout* obj, Isolate* isolate) { \
    return Is##Type(Tagged<HeapObject>(obj), isolate);           \
  }                                                              \
  bool Is##Type(const HeapObjectLayout* obj) {                   \
    return Is##Type(Tagged<HeapObject>(obj));                    \
  }
ODDBALL_LIST(IS_TYPE_FUNCTION_DEF)
HOLE_LIST(IS_TYPE_FUNCTION_DEF)
#undef IS_TYPE_FUNCTION_DEF

#if V8_STATIC_ROOTS_BOOL
#define IS_TYPE_FUNCTION_DEF(Type, Value, CamelName)                           \
  bool Is##Type(Tagged<Object> obj, ReadOnlyRoots roots) {                     \
    SLOW_DCHECK(CheckObjectComparisonAllowed(obj.ptr(), roots.Value().ptr())); \
    return V8HeapCompressionScheme::CompressObject(obj.ptr()) ==               \
           StaticReadOnlyRoot::k##CamelName;                                   \
  }
#else
#define IS_TYPE_FUNCTION_DEF(Type, Value, _)               \
  bool Is##Type(Tagged<Object> obj, ReadOnlyRoots roots) { \
    return obj == roots.Value();                           \
  }
#endif
ODDBALL_LIST(IS_TYPE_FUNCTION_DEF)
HOLE_LIST(IS_TYPE_FUNCTION_DEF)
#undef IS_TYPE_FUNCTION_DEF

bool IsNullOrUndefined(Tagged<Object> obj, Isolate* isolate) {
  return IsNullOrUndefined(obj, ReadOnlyRoots(isolate));
}

bool IsNullOrUndefined(Tagged<Object> obj, LocalIsolate* local_isolate) {
  return IsNullOrUndefined(obj, ReadOnlyRoots(local_isolate));
}

bool IsNullOrUndefined(Tagged<Object> obj, ReadOnlyRoots roots) {
  return IsNull(obj, roots) || IsUndefined(obj, roots);
}

bool IsNullOrUndefined(Tagged<Object> obj) {
  return IsHeapObject(obj) && IsNullOrUndefined(Cast<HeapObject>(obj));
}

bool IsNullOrUndefined(Tagged<HeapObject> obj) {
  return IsNullOrUndefined(obj, obj->GetReadOnlyRoots());
}

bool IsZero(Tagged<Object> obj) { return obj == Smi::zero(); }

bool IsPublicSymbol(Tagged<Object> obj) {
  return IsSymbol(obj) && !Cast<Symbol>(obj)->is_private();
}
bool IsPrivateSymbol(Tagged<Object> obj) {
  return IsSymbol(obj) && Cast<Symbol>(obj)->is_private();
}

bool IsNoSharedNameSentinel(Tagged<Object> obj) {
  return obj == SharedFunctionInfo::kNoSharedNameSentinel;
}

// TODO(leszeks): Expand Is<T> to all types.
#define IS_HELPER_DEF(Type, ...)                             \
  template <>                                                \
  struct CastTraits<Type> {                                  \
    static inline bool AllowFrom(Tagged<Object> value) {     \
      return Is##Type(value);                                \
    }                                                        \
    static inline bool AllowFrom(Tagged<HeapObject> value) { \
      return Is##Type(value);                                \
    }                                                        \
  };
HEAP_OBJECT_ORDINARY_TYPE_LIST(IS_HELPER_DEF)
HEAP_OBJECT_TRUSTED_TYPE_LIST(IS_HELPER_DEF)
ODDBALL_LIST(IS_HELPER_DEF)

#define IS_HELPER_DEF_STRUCT(NAME, Name, name) IS_HELPER_DEF(Name)
STRUCT_LIST(IS_HELPER_DEF_STRUCT)
#undef IS_HELPER_DEF_STRUCT

IS_HELPER_DEF(Number)
#undef IS_HELPER_DEF

template <typename... T>
struct CastTraits<Union<T...>> {
  static inline bool AllowFrom(Tagged<Object> value) {
    return (Is<T>(value) || ...);
  }
  static inline bool AllowFrom(Tagged<HeapObject> value) {
    return (Is<T>(value) || ...);
  }
};
template <>
struct CastTraits<JSPrimitive> {
  static inline bool AllowFrom(Tagged<Object> value) {
    return IsPrimitive(value);
  }
  static inline bool AllowFrom(Tagged<HeapObject> value) {
    return IsPrimitive(value);
  }
};
template <>
struct CastTraits<JSAny> {
  static inline bool AllowFrom(Tagged<Object> value) {
    return IsPrimitive(value) || IsJSReceiver(value);
  }
  static inline bool AllowFrom(Tagged<HeapObject> value) {
    return IsPrimitive(value) || IsJSReceiver(value);
  }
};

template <>
struct CastTraits<FieldType> {
  static inline bool AllowFrom(Tagged<Object> value) {
    return value == FieldType::None() || value == FieldType::Any() ||
           IsMap(value);
  }
  static inline bool AllowFrom(Tagged<HeapObject> value) {
    return IsMap(value);
  }
};

template <typename T>
struct CastTraits<Managed<T>> : public CastTraits<Foreign> {};
template <typename T>
struct CastTraits<TrustedManaged<T>> : public CastTraits<TrustedForeign> {};
template <typename T>
struct CastTraits<PodArray<T>> : public CastTraits<ByteArray> {};
template <typename T>
struct CastTraits<TrustedPodArray<T>> : public CastTraits<TrustedByteArray> {};
template <typename T, typename Base>
struct CastTraits<FixedIntegerArrayBase<T, Base>> : public CastTraits<Base> {};
template <typename Base>
struct CastTraits<FixedAddressArrayBase<Base>> : public CastTraits<Base> {};

template <>
struct CastTraits<JSRegExpResultIndices> : public CastTraits<JSArray> {};
template <>
struct CastTraits<DeoptimizationLiteralArray>
    : public CastTraits<TrustedWeakFixedArray> {};
template <>
struct CastTraits<FreshlyAllocatedBigInt> : public CastTraits<BigInt> {};
template <>
struct CastTraits<JSIteratorResult> : public CastTraits<JSObject> {};

template <>
struct CastTraits<DeoptimizationFrameTranslation>
    : public CastTraits<TrustedByteArray> {};

template <class T,
          typename std::enable_if<(std::is_arithmetic<T>::value ||
                                   std::is_enum<T>::value) &&
                                      !std::is_floating_point<T>::value,
                                  int>::type>
T HeapObject::Relaxed_ReadField(size_t offset) const {
  // Pointer compression causes types larger than kTaggedSize to be
  // unaligned. Atomic loads must be aligned.
  DCHECK_IMPLIES(COMPRESS_POINTERS_BOOL, sizeof(T) <= kTaggedSize);
  using AtomicT = typename base::AtomicTypeFromByteWidth<sizeof(T)>::type;
  return static_cast<T>(base::AsAtomicImpl<AtomicT>::Relaxed_Load(
      reinterpret_cast<AtomicT*>(field_address(offset))));
}

template <class T,
          typename std::enable_if<(std::is_arithmetic<T>::value ||
                                   std::is_enum<T>::value) &&
                                      !std::is_floating_point<T>::value,
                                  int>::type>
void HeapObject::Relaxed_WriteField(size_t offset, T value) {
  // Pointer compression causes types larger than kTaggedSize to be
  // unaligned. Atomic stores must be aligned.
  DCHECK_IMPLIES(COMPRESS_POINTERS_BOOL, sizeof(T) <= kTaggedSize);
  using AtomicT = typename base::AtomicTypeFromByteWidth<sizeof(T)>::type;
  base::AsAtomicImpl<AtomicT>::Relaxed_Store(
      reinterpret_cast<AtomicT*>(field_address(offset)),
      static_cast<AtomicT>(value));
}

// static
template <typename CompareAndSwapImpl>
Tagged<Object> HeapObject::SeqCst_CompareAndSwapField(
    Tagged<Object> expected, Tagged<Object> value,
    CompareAndSwapImpl compare_and_swap_impl) {
  Tagged<Object> actual_expected = expected;
  do {
    Tagged<Object> old_value = compare_and_swap_impl(actual_expected, value);
    if (old_value == actual_expected || !IsNumber(old_value) ||
        !IsNumber(actual_expected)) {
      return old_value;
    }
    if (!Object::SameNumberValue(
            Object::NumberValue(Cast<Number>(old_value)),
            Object::NumberValue(Cast<Number>(actual_expected)))) {
      return old_value;
    }
    // The pointer comparison failed, but the numbers are equal. This can
    // happen even if both numbers are HeapNumbers with the same value.
    // Try again in the next iteration.
    actual_expected = old_value;
  } while (true);
}

bool InAnySharedSpace(Tagged<HeapObject> obj) {
  if (IsReadOnlyHeapObject(obj)) return V8_SHARED_RO_HEAP_BOOL;
  return InWritableSharedSpace(obj);
}

bool InWritableSharedSpace(Tagged<HeapObject> obj) {
  return MemoryChunk::FromHeapObject(obj)->InWritableSharedSpace();
}

bool InReadOnlySpace(Tagged<HeapObject> obj) {
  return IsReadOnlyHeapObject(obj);
}

constexpr bool FastInReadOnlySpaceOrSmallSmi(Tagged_t obj) {
#if V8_STATIC_ROOTS_BOOL
  // The following assert ensures that the page size check covers all our static
  // roots. This is not strictly necessary and can be relaxed in future as the
  // most prominent static roots are anyways allocated at the beginning of the
  // first page.
  static_assert(StaticReadOnlyRoot::kLastAllocatedRoot < kRegularPageSize);
  return obj < kRegularPageSize;
#else   // !V8_STATIC_ROOTS_BOOL
  return false;
#endif  // !V8_STATIC_ROOTS_BOOL
}

bool OutsideSandboxOrInReadonlySpace(Tagged<HeapObject> obj) {
#ifdef V8_ENABLE_SANDBOX
  return !InsideSandbox(obj.address()) ||
         MemoryChunk::FromHeapObject(obj)->SandboxSafeInReadOnlySpace();
#else
  return true;
#endif
}

bool IsJSObjectThatCanBeTrackedAsPrototype(Tagged<HeapObject> obj) {
  // Do not optimize objects in the shared heap because it is not
  // threadsafe. Objects in the shared heap have fixed layouts and their maps
  // never change.
  return IsJSObject(obj) && !InWritableSharedSpace(*obj);
}

bool IsJSApiWrapperObject(Tagged<Map> map) {
  const InstanceType instance_type = map->instance_type();
  return InstanceTypeChecker::IsJSAPIObjectWithEmbedderSlots(instance_type) ||
         InstanceTypeChecker::IsJSSpecialObject(instance_type);
}

bool IsJSApiWrapperObject(Tagged<HeapObject> js_obj) {
  return IsJSApiWrapperObject(js_obj->map());
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsUniqueName) {
  return IsInternalizedString(obj, cage_base) || IsSymbol(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsCallable) {
  return obj->map(cage_base)->is_callable();
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsCallableJSProxy) {
  return IsCallable(obj, cage_base) && IsJSProxy(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsCallableApiObject) {
  InstanceType type = obj->map(cage_base)->instance_type();
  return IsCallable(obj, cage_base) &&
         (type == JS_API_OBJECT_TYPE || type == JS_SPECIAL_API_OBJECT_TYPE);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsNonNullForeign) {
  return IsForeign(obj, cage_base) &&
         Cast<Foreign>(obj)->foreign_address_unchecked() != kNullAddress;
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsConstructor) {
  return obj->map(cage_base)->is_constructor();
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsSourceTextModuleInfo) {
  return obj->map(cage_base) == obj->GetReadOnlyRoots().module_info_map();
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsConsString) {
  if (!IsString(obj, cage_base)) return false;
  return StringShape(Cast<String>(obj)->map()).IsCons();
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsThinString) {
  if (!IsString(obj, cage_base)) return false;
  return StringShape(Cast<String>(obj)->map()).IsThin();
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsSlicedString) {
  if (!IsString(obj, cage_base)) return false;
  return StringShape(Cast<String>(obj)->map()).IsSliced();
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsSeqString) {
  if (!IsString(obj, cage_base)) return false;
  return StringShape(Cast<String>(obj)->map()).IsSequential();
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsSeqOneByteString) {
  if (!IsString(obj, cage_base)) return false;
  return StringShape(Cast<String>(obj)->map()).IsSequentialOneByte();
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsSeqTwoByteString) {
  if (!IsString(obj, cage_base)) return false;
  return StringShape(Cast<String>(obj)->map()).IsSequentialTwoByte();
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsExternalOneByteString) {
  if (!IsString(obj, cage_base)) return false;
  return StringShape(Cast<String>(obj)->map()).IsExternalOneByte();
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsExternalTwoByteString) {
  if (!IsString(obj, cage_base)) return false;
  return StringShape(Cast<String>(obj)->map()).IsExternalTwoByte();
}

bool IsNumber(Tagged<Object> obj) {
  if (IsSmi(obj)) return true;
  Tagged<HeapObject> heap_object = Cast<HeapObject>(obj);
  PtrComprCageBase cage_base = GetPtrComprCageBase(heap_object);
  return IsHeapNumber(heap_object, cage_base);
}

bool IsNumber(Tagged<Object> obj, PtrComprCageBase cage_base) {
  return obj.IsSmi() || IsHeapNumber(obj, cage_base);
}

bool IsNumeric(Tagged<Object> obj) {
  if (IsSmi(obj)) return true;
  Tagged<HeapObject> heap_object = Cast<HeapObject>(obj);
  PtrComprCageBase cage_base = GetPtrComprCageBase(heap_object);
  return IsHeapNumber(heap_object, cage_base) ||
         IsBigInt(heap_object, cage_base);
}

bool IsNumeric(Tagged<Object> obj, PtrComprCageBase cage_base) {
  return IsNumber(obj, cage_base) || IsBigInt(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsTemplateLiteralObject) {
  return IsJSArray(obj, cage_base);
}

#if V8_INTL_SUPPORT
DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsJSSegmentDataObject) {
  return IsJSObject(obj, cage_base);
}
DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsJSSegmentDataObjectWithIsWordLike) {
  return IsJSObject(obj, cage_base);
}
#endif  // V8_INTL_SUPPORT

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsDeoptimizationData) {
  // Must be a (protected) fixed array.
  if (!IsProtectedFixedArray(obj, cage_base)) return false;

  // There's no sure way to detect the difference between a fixed array and
  // a deoptimization data array.  Since this is used for asserts we can
  // check that the length is zero or else the fixed size plus a multiple of
  // the entry size.
  int length = Cast<ProtectedFixedArray>(obj)->length();
  if (length == 0) return true;

  length -= DeoptimizationData::kFirstDeoptEntryIndex;
  return length >= 0 && length % DeoptimizationData::kDeoptEntrySize == 0;
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsHandlerTable) {
  return IsFixedArrayExact(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsDependentCode) {
  return IsWeakArrayList(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsOSROptimizedCodeCache) {
  return IsWeakFixedArray(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsStringWrapper) {
  return IsJSPrimitiveWrapper(obj, cage_base) &&
         IsString(Cast<JSPrimitiveWrapper>(obj)->value(), cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsBooleanWrapper) {
  return IsJSPrimitiveWrapper(obj, cage_base) &&
         IsBoolean(Cast<JSPrimitiveWrapper>(obj)->value(), cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsScriptWrapper) {
  return IsJSPrimitiveWrapper(obj, cage_base) &&
         IsScript(Cast<JSPrimitiveWrapper>(obj)->value(), cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsNumberWrapper) {
  return IsJSPrimitiveWrapper(obj, cage_base) &&
         IsNumber(Cast<JSPrimitiveWrapper>(obj)->value(), cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsBigIntWrapper) {
  return IsJSPrimitiveWrapper(obj, cage_base) &&
         IsBigInt(Cast<JSPrimitiveWrapper>(obj)->value(), cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsSymbolWrapper) {
  return IsJSPrimitiveWrapper(obj, cage_base) &&
         IsSymbol(Cast<JSPrimitiveWrapper>(obj)->value(), cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsStringSet) {
  return IsHashTable(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsObjectHashSet) {
  return IsHashTable(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsCompilationCacheTable) {
  return IsHashTable(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsMapCache) {
  return IsHashTable(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsObjectHashTable) {
  return IsHashTable(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsObjectTwoHashTable) {
  return IsHashTable(obj, cage_base);
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsHashTableBase) {
  return IsHashTable(obj, cage_base);
}

// static
bool IsPrimitive(Tagged<Object> obj) {
  if (obj.IsSmi()) return true;
  Tagged<HeapObject> this_heap_object = Cast<HeapObject>(obj);
  PtrComprCageBase cage_base = GetPtrComprCageBase(this_heap_object);
  return IsPrimitiveMap(this_heap_object->map(cage_base));
}

// static
bool IsPrimitive(Tagged<Object> obj, PtrComprCageBase cage_base) {
  return obj.IsSmi() || IsPrimitiveMap(Cast<HeapObject>(obj)->map(cage_base));
}

// static
Maybe<bool> Object::IsArray(Handle<Object> object) {
  if (IsSmi(*object)) return Just(false);
  auto heap_object = Cast<HeapObject>(object);
  if (IsJSArray(*heap_object)) return Just(true);
  if (!IsJSProxy(*heap_object)) return Just(false);
  return JSProxy::IsArray(Cast<JSProxy>(object));
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsUndetectable) {
  return obj->map(cage_base)->is_undetectable();
}

DEF_HEAP_OBJECT_PREDICATE(HeapObject, IsAccessCheckNeeded) {
  if (IsJSGlobalProxy(obj, cage_base)) {
    const Tagged<JSGlobalProxy> proxy = Cast<JSGlobalProxy>(obj);
    Tagged<JSGlobalObject> global =
        proxy->GetIsolate()->context()->global_object();
    return proxy->IsDetachedFrom(global);
  }
  return obj->map(cage_base)->is_access_check_needed();
}

#define MAKE_STRUCT_PREDICATE(NAME, Name, name)                             \
  bool Is##Name(Tagged<Object> obj) {                                       \
    return IsHeapObject(obj) && Is##Name(Cast<HeapObject>(obj));            \
  }                                                                         \
  bool Is##Name(Tagged<Object> obj, PtrComprCageBase cage_base) {           \
    return IsHeapObject(obj) && Is##Name(Cast<HeapObject>(obj), cage_base); \
  }                                                                         \
  bool Is##Name(HeapObject obj) {                                           \
    static_assert(kTaggedCanConvertToRawObjects);                           \
    return Is##Name(Tagged<HeapObject>(obj));                               \
  }                                                                         \
  bool Is##Name(HeapObject obj, PtrComprCageBase cage_base) {               \
    static_assert(kTaggedCanConvertToRawObjects);                           \
    return Is##Name(Tagged<HeapObject>(obj), cage_base);                    \
  }
// static
STRUCT_LIST(MAKE_STRUCT_PREDICATE)
#undef MAKE_STRUCT_PREDICATE

// static
double Object::NumberValue(Tagged<Number> obj) {
  DCHECK(IsNumber(obj));
  return IsSmi(obj) ? static_cast<double>(UncheckedCast<Smi>(obj).value())
                    : UncheckedCast<HeapNumber>(obj)->value();
}
// TODO(leszeks): Remove in favour of Tagged<Number>
// static
double Object::NumberValue(Tagged<Object> obj) {
  return NumberValue(Cast<Number>(obj));
}
double Object::NumberValue(Tagged<HeapNumber> obj) {
  return NumberValue(Cast<Number>(obj));
}
double Object::NumberValue(Tagged<Smi> obj) {
  return NumberValue(Cast<Number>(obj));
}

// static
bool Object::SameNumberValue(double value1, double value2) {
  // Compare values bitwise, to cover -0 being different from 0 -- we'd need to
  // look at sign bits anyway if we'd done a double comparison, so we may as
  // well compare bitwise immediately.
  uint64_t value1_bits = base::bit_cast<uint64_t>(value1);
  uint64_t value2_bits = base::bit_cast<uint64_t>(value2);
  if (value1_bits == value2_bits) {
    return true;
  }
  // SameNumberValue(NaN, NaN) is true even for NaNs with different bit
  // representations.
  return std::isnan(value1) && std::isnan(value2);
}

// static
bool IsNaN(Tagged<Object> obj) {
  return IsHeapNumber(obj) && std::isnan(Cast<HeapNumber>(obj)->value());
}

// static
bool IsMinusZero(Tagged<Object> obj) {
  return IsHeapNumber(obj) && i::IsMinusZero(Cast<HeapNumber>(obj)->value());
}

// static
bool Object::HasValidElements(Tagged<Object> obj) {
  // Dictionary is covered under FixedArray. ByteArray is used
  // for the JSTypedArray backing stores.
  return IsFixedArray(obj) || IsFixedDoubleArray(obj) || IsByteArray(obj);
}

// static
bool Object::FilterKey(Tagged<Object> obj, PropertyFilter filter) {
  DCHECK(!IsPropertyCell(obj));
  if (filter == PRIVATE_NAMES_ONLY) {
    if (!IsSymbol(obj)) return true;
    return !Cast<Symbol>(obj)->is_private_name();
  } else if (IsSymbol(obj)) {
    if (filter & SKIP_SYMBOLS) return true;

    if (Cast<Symbol>(obj)->is_private()) return true;
  } else {
    if (filter & SKIP_STRINGS) return true;
  }
  return false;
}

// static
Representation Object::OptimalRepresentation(Tagged<Object> obj,
                                             PtrComprCageBase cage_base) {
  if (IsSmi(obj)) {
    return Representation::Smi();
  }
  Tagged<HeapObject> heap_object = Cast<HeapObject>(obj);
  if (IsHeapNumber(heap_object, cage_base)) {
    return Representation::Double();
  } else if (IsUninitialized(heap_object,
                             heap_object->GetReadOnlyRoots(cage_base))) {
    return Representation::None();
  }
  return Representation::HeapObject();
}

// static
ElementsKind Object::OptimalElementsKind(Tagged<Object> obj,
                                         PtrComprCageBase cage_base) {
  if (IsSmi(obj)) return PACKED_SMI_ELEMENTS;
  if (IsNumber(obj, cage_base)) return PACKED_DOUBLE_ELEMENTS;
  return PACKED_ELEMENTS;
}

// static
bool Object::FitsRepresentation(Tagged<Object> obj,
                                Representation representation,
                                bool allow_coercion) {
  if (representation.IsSmi()) {
    return IsSmi(obj);
  } else if (representation.IsDouble()) {
    return allow_coercion ? IsNumber(obj) : IsHeapNumber(obj);
  } else if (representation.IsHeapObject()) {
    return IsHeapObject(obj);
  } else if (representation.IsNone()) {
    return false;
  }
  return true;
}

// static
bool Object::ToUint32(Tagged<Object> obj, uint32_t* value) {
  if (IsSmi(obj)) {
    int num = Smi::ToInt(obj);
    if (num < 0) return false;
    *value = static_cast<uint32_t>(num);
    return true;
  }
  if (IsHeapNumber(obj)) {
    double num = Cast<HeapNumber>(obj)->value();
    return DoubleToUint32IfEqualToSelf(num, value);
  }
  return false;
}

// static
MaybeHandle<JSReceiver> Object::ToObject(Isolate* isolate,
                                         Handle<Object> object,
                                         const char* method_name) {
  if (IsJSReceiver(*object)) return Cast<JSReceiver>(object);
  return ToObjectImpl(isolate, object, method_name);
}

// static
MaybeHandle<Name> Object::ToName(Isolate* isolate, Handle<Object> input) {
  if (IsName(*input)) return Cast<Name>(input);
  return ConvertToName(isolate, input);
}

// static
MaybeHandle<Object> Object::ToPropertyKey(Isolate* isolate,
                                          Handle<Object> value) {
  if (IsSmi(*value) || IsName(Cast<HeapObject>(*value))) return value;
  return ConvertToPropertyKey(isolate, value);
}

// static
MaybeHandle<Object> Object::ToPrimitive(Isolate* isolate, Handle<Object> input,
                                        ToPrimitiveHint hint) {
  if (IsPrimitive(*input)) return input;
  return JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(input), hint);
}

// static
MaybeHandle<Number> Object::ToNumber(Isolate* isolate, Handle<Object> input) {
  if (IsNumber(*input)) return Cast<Number>(input);  // Shortcut.
  return ConvertToNumber(isolate, input);
}

// static
MaybeHandle<Object> Object::ToNumeric(Isolate* isolate, Handle<Object> input) {
  if (IsNumber(*input) || IsBigInt(*input)) return input;  // Shortcut.
  return ConvertToNumeric(isolate, input);
}

// static
MaybeHandle<Number> Object::ToInteger(Isolate* isolate, Handle<Object> input) {
  if (IsSmi(*input)) return Cast<Smi>(input);
  return ConvertToInteger(isolate, input);
}

// static
MaybeHandle<Number> Object::ToInt32(Isolate* isolate, Handle<Object> input) {
  if (IsSmi(*input)) return Cast<Smi>(input);
  return ConvertToInt32(isolate, input);
}

// static
MaybeHandle<Number> Object::ToUint32(Isolate* isolate, Handle<Object> input) {
  if (IsSmi(*input)) {
    return handle(Smi::ToUint32Smi(Cast<Smi>(*input)), isolate);
  }
  return ConvertToUint32(isolate, input);
}

// static
template <typename T, typename>
MaybeHandle<String> Object::ToString(Isolate* isolate, Handle<T> input) {
  // T should be a subtype of Object, which is enforced by the second template
  // argument.
  if (IsString(*input)) return Cast<String>(input);
  return ConvertToString(isolate, input);
}

#ifdef V8_ENABLE_DIRECT_HANDLE
template <typename T, typename>
MaybeDirectHandle<String> Object::ToString(Isolate* isolate,
                                           DirectHandle<T> input) {
  if (IsString(*input)) return Cast<String>(input);
  return ConvertToString(isolate, indirect_handle(input, isolate));
}
#endif

// static
MaybeHandle<Object> Object::ToLength(Isolate* isolate, Handle<Object> input) {
  if (IsSmi(*input)) {
    int value = std::max(Smi::ToInt(*input), 0);
    return handle(Smi::FromInt(value), isolate);
  }
  return ConvertToLength(isolate, input);
}

// static
MaybeHandle<Object> Object::ToIndex(Isolate* isolate, Handle<Object> input,
                                    MessageTemplate error_index) {
  if (IsSmi(*input) && Smi::ToInt(*input) >= 0) return input;
  return ConvertToIndex(isolate, input, error_index);
}

MaybeHandle<Object> Object::GetProperty(Isolate* isolate, Handle<Object> object,
                                        Handle<Name> name) {
  LookupIterator it(isolate, object, name);
  if (!it.IsFound()) return it.factory()->undefined_value();
  return GetProperty(&it);
}

MaybeHandle<Object> Object::GetElement(Isolate* isolate, Handle<Object> object,
                                       uint32_t index) {
  LookupIterator it(isolate, object, index);
  if (!it.IsFound()) return it.factory()->undefined_value();
  return GetProperty(&it);
}

MaybeHandle<Object> Object::SetElement(Isolate* isolate, Handle<Object> object,
                                       uint32_t index, Handle<Object> value,
                                       ShouldThrow should_throw) {
  LookupIterator it(isolate, object, index);
  MAYBE_RETURN_NULL(
      SetProperty(&it, value, StoreOrigin::kMaybeKeyed, Just(should_throw)));
  return value;
}

Address HeapObject::ReadSandboxedPointerField(
    size_t offset, PtrComprCageBase cage_base) const {
  return i::ReadSandboxedPointerField(field_address(offset), cage_base);
}

void HeapObject::WriteSandboxedPointerField(size_t offset,
                                            PtrComprCageBase cage_base,
                                            Address value) {
  i::WriteSandboxedPointerField(field_address(offset), cage_base, value);
}

void HeapObject::WriteSandboxedPointerField(size_t offset, Isolate* isolate,
                                            Address value) {
  i::WriteSandboxedPointerField(field_address(offset),
                                PtrComprCageBase(isolate), value);
}

size_t HeapObject::ReadBoundedSizeField(size_t offset) const {
  return i::ReadBoundedSizeField(field_address(offset));
}

void HeapObject::WriteBoundedSizeField(size_t offset, size_t value) {
  i::WriteBoundedSizeField(field_address(offset), value);
}

template <ExternalPointerTag tag>
void HeapObject::InitExternalPointerField(size_t offset,
                                          IsolateForSandbox isolate,
                                          Address value) {
  i::InitExternalPointerField<tag>(address(), field_address(offset), isolate,
                                   value);
}

template <ExternalPointerTag tag>
Address HeapObject::ReadExternalPointerField(size_t offset,
                                             IsolateForSandbox isolate) const {
  return i::ReadExternalPointerField<tag>(field_address(offset), isolate);
}

template <CppHeapPointerTag lower_bound, CppHeapPointerTag upper_bound>
Address HeapObject::ReadCppHeapPointerField(
    size_t offset, IsolateForPointerCompression isolate) const {
  return i::ReadCppHeapPointerField<lower_bound, upper_bound>(
      field_address(offset), isolate);
}

Address HeapObject::ReadCppHeapPointerField(
    size_t offset, IsolateForPointerCompression isolate,
    CppHeapPointerTagRange tag_range) const {
  return i::ReadCppHeapPointerField(field_address(offset), isolate, tag_range);
}

template <ExternalPointerTag tag>
void HeapObject::WriteExternalPointerField(size_t offset,
                                           IsolateForSandbox isolate,
                                           Address value) {
  i::WriteExternalPointerField<tag>(field_address(offset), isolate, value);
}

template <ExternalPointerTag tag>
void HeapObject::WriteLazilyInitializedExternalPointerField(
    size_t offset, IsolateForSandbox isolate, Address value) {
  i::WriteLazilyInitializedExternalPointerField<tag>(
      address(), field_address(offset), isolate, value);
}

void HeapObject::SetupLazilyInitializedExternalPointerField(size_t offset) {
  i::SetupLazilyInitializedExternalPointerField(field_address(offset));
}

void HeapObject::SetupLazilyInitializedCppHeapPointerField(size_t offset) {
  CppHeapPointerSlot(field_address(offset)).init();
}

template <CppHeapPointerTag tag>
void HeapObject::WriteLazilyInitializedCppHeapPointerField(
    size_t offset, IsolateForPointerCompression isolate, Address value) {
  i::WriteLazilyInitializedCppHeapPointerField<tag>(field_address(offset),
                                                    isolate, value);
}

void HeapObject::WriteLazilyInitializedCppHeapPointerField(
    size_t offset, IsolateForPointerCompression isolate, Address value,
    CppHeapPointerTag tag) {
  i::WriteLazilyInitializedCppHeapPointerField(field_address(offset), isolate,
                                               value, tag);
}

void HeapObject::InitSelfIndirectPointerField(size_t offset,
                                              IsolateForSandbox isolate) {
  DCHECK(IsExposedTrustedObject(*this));
  InstanceType instance_type = map()->instance_type();
  IndirectPointerTag tag = IndirectPointerTagFromInstanceType(instance_type);
  i::InitSelfIndirectPointerField(field_address(offset), isolate, *this, tag);
}

template <IndirectPointerTag tag>
Tagged<ExposedTrustedObject> HeapObject::ReadTrustedPointerField(
    size_t offset, IsolateForSandbox isolate) const {
  // Currently, trusted pointer loads always use acquire semantics as the
  // under-the-hood indirect pointer loads use acquire loads anyway.
  return ReadTrustedPointerField<tag>(offset, isolate, kAcquireLoad);
}

template <IndirectPointerTag tag>
Tagged<ExposedTrustedObject> HeapObject::ReadTrustedPointerField(
    size_t offset, IsolateForSandbox isolate,
    AcquireLoadTag acquire_load) const {
  Tagged<Object> object =
      ReadMaybeEmptyTrustedPointerField<tag>(offset, isolate, acquire_load);
  DCHECK(IsExposedTrustedObject(object));
  return Cast<ExposedTrustedObject>(object);
}

template <IndirectPointerTag tag>
Tagged<Object> HeapObject::ReadMaybeEmptyTrustedPointerField(
    size_t offset, IsolateForSandbox isolate,
    AcquireLoadTag acquire_load) const {
#ifdef V8_ENABLE_SANDBOX
  return i::ReadIndirectPointerField<tag>(field_address(offset), isolate,
                                          acquire_load);
#else
  return TaggedField<Object>::Acquire_Load(*this, static_cast<int>(offset));
#endif
}

template <IndirectPointerTag tag>
void HeapObject::WriteTrustedPointerField(size_t offset,
                                          Tagged<ExposedTrustedObject> value) {
  // Currently, trusted pointer stores always use release semantics as the
  // under-the-hood indirect pointer stores use release stores anyway.
#ifdef V8_ENABLE_SANDBOX
  i::WriteIndirectPointerField<tag>(field_address(offset), value,
                                    kReleaseStore);
#else
  TaggedField<ExposedTrustedObject>::Release_Store(
      *this, static_cast<int>(offset), value);
#endif
}

bool HeapObject::IsTrustedPointerFieldEmpty(size_t offset) const {
#ifdef V8_ENABLE_SANDBOX
  IndirectPointerHandle handle = ACQUIRE_READ_UINT32_FIELD(*this, offset);
  return handle == kNullIndirectPointerHandle;
#else
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return IsSmi(TaggedField<Object>::Acquire_Load(cage_base, *this,
                                                 static_cast<int>(offset)));
#endif
}

void HeapObject::ClearTrustedPointerField(size_t offset) {
#ifdef V8_ENABLE_SANDBOX
  RELEASE_WRITE_UINT32_FIELD(*this, offset, kNullIndirectPointerHandle);
#else
  TaggedField<Smi>::Release_Store(*this, static_cast<int>(offset), Smi::zero());
#endif
}

void HeapObject::ClearTrustedPointerField(size_t offset, ReleaseStoreTag) {
  return ClearTrustedPointerField(offset);
}

Tagged<Code> HeapObject::ReadCodePointerField(size_t offset,
                                              IsolateForSandbox isolate) const {
  return Cast<Code>(
      ReadTrustedPointerField<kCodeIndirectPointerTag>(offset, isolate));
}

void HeapObject::WriteCodePointerField(size_t offset, Tagged<Code> value) {
  WriteTrustedPointerField<kCodeIndirectPointerTag>(offset, value);
}

bool HeapObject::IsCodePointerFieldEmpty(size_t offset) const {
  return IsTrustedPointerFieldEmpty(offset);
}

void HeapObject::ClearCodePointerField(size_t offset) {
  ClearTrustedPointerField(offset);
}

Address HeapObject::ReadCodeEntrypointViaCodePointerField(
    size_t offset, CodeEntrypointTag tag) const {
  return i::ReadCodeEntrypointViaCodePointerField(field_address(offset), tag);
}

void HeapObject::WriteCodeEntrypointViaCodePointerField(size_t offset,
                                                        Address value,
                                                        CodeEntrypointTag tag) {
  i::WriteCodeEntrypointViaCodePointerField(field_address(offset), value, tag);
}

void HeapObject::InitJSDispatchHandleField(size_t offset,
                                           IsolateForSandbox isolate,
                                           uint16_t parameter_count) {
#ifdef V8_ENABLE_LEAPTIERING
  JSDispatchTable::Space* space =
      isolate.GetJSDispatchTableSpaceFor(field_address(offset));
  JSDispatchHandle handle =
      GetProcessWideJSDispatchTable()->AllocateAndInitializeEntry(
          space, parameter_count);

  // Use a Release_Store to ensure that the store of the pointer into the table
  // is not reordered after the store of the handle. Otherwise, other threads
  // may access an uninitialized table entry and crash.
  auto location = reinterpret_cast<JSDispatchHandle*>(field_address(offset));
  base::AsAtomic32::Release_Store(location, handle);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_LEAPTIERING
}

ObjectSlot HeapObject::RawField(int byte_offset) const {
  return ObjectSlot(field_address(byte_offset));
}

MaybeObjectSlot HeapObject::RawMaybeWeakField(int byte_offset) const {
  return MaybeObjectSlot(field_address(byte_offset));
}

InstructionStreamSlot HeapObject::RawInstructionStreamField(
    int byte_offset) const {
  return InstructionStreamSlot(field_address(byte_offset));
}

ExternalPointerSlot HeapObject::RawExternalPointerField(
    int byte_offset, ExternalPointerTag tag) const {
  return ExternalPointerSlot(field_address(byte_offset), tag);
}

CppHeapPointerSlot HeapObject::RawCppHeapPointerField(int byte_offset) const {
  return CppHeapPointerSlot(field_address(byte_offset));
}

IndirectPointerSlot HeapObject::RawIndirectPointerField(
    int byte_offset, IndirectPointerTag tag) const {
  return IndirectPointerSlot(field_address(byte_offset), tag);
}

MapWord MapWord::FromMap(const Tagged<Map> map) {
  DCHECK(map.is_null() || !MapWord::IsPacked(map.ptr()));
#ifdef V8_MAP_PACKING
  return MapWord(Pack(map.ptr()));
#else
  return MapWord(map.ptr());
#endif
}

Tagged<Map> MapWord::ToMap() const {
#ifdef V8_MAP_PACKING
  return UncheckedCast<Map>(Tagged<Object>(Unpack(value_)));
#else
  return UncheckedCast<Map>(Tagged<Object>(value_));
#endif
}

bool MapWord::IsForwardingAddress() const {
#ifdef V8_EXTERNAL_CODE_SPACE
  // When external code space is enabled forwarding pointers are encoded as
  // Smi representing a diff from the source object address in kObjectAlignment
  // chunks.
  return HAS_SMI_TAG(value_);
#else
  return (value_ & kForwardingTagMask) == kForwardingTag;
#endif  // V8_EXTERNAL_CODE_SPACE
}

MapWord MapWord::FromForwardingAddress(Tagged<HeapObject> map_word_host,
                                       Tagged<HeapObject> object) {
#ifdef V8_EXTERNAL_CODE_SPACE
  // When external code space is enabled forwarding pointers are encoded as
  // Smi representing a diff from the source object address in kObjectAlignment
  // chunks.
  intptr_t diff = static_cast<intptr_t>(object.ptr() - map_word_host.ptr());
  DCHECK(IsAligned(diff, kObjectAlignment));
  MapWord map_word(Smi::FromIntptr(diff / kObjectAlignment).ptr());
  DCHECK(map_word.IsForwardingAddress());
  return map_word;
#else
  return MapWord(object.ptr() - kHeapObjectTag);
#endif  // V8_EXTERNAL_CODE_SPACE
}

Tagged<HeapObject> MapWord::ToForwardingAddress(
    Tagged<HeapObject> map_word_host) {
  DCHECK(IsForwardingAddress());
#ifdef V8_EXTERNAL_CODE_SPACE
  // When the sandbox or the external code space is enabled, forwarding
  // pointers are encoded as Smi representing a diff from the source object
  // address in kObjectAlignment chunks. This is required as we are using
  // multiple pointer compression cages in these scenarios.
  intptr_t diff =
      static_cast<intptr_t>(Tagged<Smi>(value_).value()) * kObjectAlignment;
  Address address = map_word_host.address() + diff;
  return HeapObject::FromAddress(address);
#else
  // The sandbox requires the external code space.
  DCHECK(!V8_ENABLE_SANDBOX_BOOL);
  return HeapObject::FromAddress(value_);
#endif  // V8_EXTERNAL_CODE_SPACE
}

#ifdef VERIFY_HEAP
void HeapObject::VerifyObjectField(Isolate* isolate, int offset) {
  Object::VerifyPointer(isolate,
                        TaggedField<Object>::load(isolate, *this, offset));
  static_assert(!COMPRESS_POINTERS_BOOL || kTaggedSize == kInt32Size);
}

void HeapObject::VerifyMaybeObjectField(Isolate* isolate, int offset) {
  Object::VerifyMaybeObjectPointer(
      isolate, TaggedField<MaybeObject>::load(isolate, *this, offset));
  static_assert(!COMPRESS_POINTERS_BOOL || kTaggedSize == kInt32Size);
}

void HeapObject::VerifySmiField(int offset) {
  CHECK(IsSmi(TaggedField<Object>::load(*this, offset)));
  static_assert(!COMPRESS_POINTERS_BOOL || kTaggedSize == kInt32Size);
}

#endif

ReadOnlyRoots HeapObject::EarlyGetReadOnlyRoots() const {
  return ReadOnlyHeap::EarlyGetReadOnlyRoots(*this);
}

ReadOnlyRoots HeapObjectLayout::EarlyGetReadOnlyRoots() const {
  return ReadOnlyHeap::EarlyGetReadOnlyRoots(Tagged(this));
}

ReadOnlyRoots HeapObject::GetReadOnlyRoots() const {
  return ReadOnlyHeap::GetReadOnlyRoots(*this);
}

ReadOnlyRoots HeapObjectLayout::GetReadOnlyRoots() const {
  return ReadOnlyHeap::GetReadOnlyRoots(Tagged(this));
}

// TODO(v8:13788): Remove this cage-ful accessor.
ReadOnlyRoots HeapObject::GetReadOnlyRoots(PtrComprCageBase cage_base) const {
  return GetReadOnlyRoots();
}

Tagged<Map> HeapObject::map() const {
  // This method is never used for objects located in code space
  // (InstructionStream and free space fillers) and thus it is fine to use
  // auto-computed cage base value.
  DCHECK_IMPLIES(V8_EXTERNAL_CODE_SPACE_BOOL, !IsCodeSpaceObject(*this));
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return HeapObject::map(cage_base);
}

Tagged<Map> HeapObject::map(PtrComprCageBase cage_base) const {
  return map_word(cage_base, kRelaxedLoad).ToMap();
}

Tagged<Map> HeapObjectLayout::map() const {
  // TODO(leszeks): Support MapWord members and access via that instead.
  return Tagged<HeapObject>(this)->map();
}

Tagged<Map> HeapObjectLayout::map(AcquireLoadTag) const {
  // TODO(leszeks): Support MapWord members and access via that instead.
  return Tagged<HeapObject>(this)->map(kAcquireLoad);
}

void HeapObjectLayout::set_map(Tagged<Map> value) {
  // TODO(leszeks): Support MapWord members and access via that instead.
  return Tagged<HeapObject>(this)->set_map(value);
}

void HeapObjectLayout::set_map(Tagged<Map> value, ReleaseStoreTag) {
  // TODO(leszeks): Support MapWord members and access via that instead.
  return Tagged<HeapObject>(this)->set_map(value, kReleaseStore);
}

void HeapObjectLayout::set_map_safe_transition(Tagged<Map> value,
                                               ReleaseStoreTag) {
  // TODO(leszeks): Support MapWord members and access via that instead.
  return Tagged<HeapObject>(this)->set_map_safe_transition(value,
                                                           kReleaseStore);
}

void HeapObject::set_map(Tagged<Map> value) {
  set_map<EmitWriteBarrier::kYes>(value, kRelaxedStore,
                                  VerificationMode::kPotentialLayoutChange);
}

void HeapObject::set_map(Tagged<Map> value, ReleaseStoreTag tag) {
  set_map<EmitWriteBarrier::kYes>(value, kReleaseStore,
                                  VerificationMode::kPotentialLayoutChange);
}

void HeapObject::set_map_safe_transition(Tagged<Map> value) {
  set_map<EmitWriteBarrier::kYes>(value, kRelaxedStore,
                                  VerificationMode::kSafeMapTransition);
}

void HeapObject::set_map_safe_transition(Tagged<Map> value,
                                         ReleaseStoreTag tag) {
  set_map<EmitWriteBarrier::kYes>(value, kReleaseStore,
                                  VerificationMode::kSafeMapTransition);
}

void HeapObjectLayout::set_map_safe_transition_no_write_barrier(
    Tagged<Map> value, RelaxedStoreTag tag) {
  // TODO(leszeks): Support MapWord members and access via that instead.
  return Tagged<HeapObject>(this)->set_map_safe_transition_no_write_barrier(
      value, tag);
}

void HeapObject::set_map_safe_transition_no_write_barrier(Tagged<Map> value,
                                                          RelaxedStoreTag tag) {
  set_map<EmitWriteBarrier::kNo>(value, kRelaxedStore,
                                 VerificationMode::kSafeMapTransition);
}

void HeapObject::set_map_safe_transition_no_write_barrier(Tagged<Map> value,
                                                          ReleaseStoreTag tag) {
  set_map<EmitWriteBarrier::kNo>(value, kReleaseStore,
                                 VerificationMode::kSafeMapTransition);
}

void HeapObjectLayout::set_map_no_write_barrier(Tagged<Map> value,
                                                RelaxedStoreTag tag) {
  // TODO(leszeks): Support MapWord members and access via that instead.
  Tagged<HeapObject>(this)->set_map_no_write_barrier(value, tag);
}

// Unsafe accessor omitting write barrier.
void HeapObject::set_map_no_write_barrier(Tagged<Map> value,
                                          RelaxedStoreTag tag) {
  set_map<EmitWriteBarrier::kNo>(value, kRelaxedStore,
                                 VerificationMode::kPotentialLayoutChange);
}

void HeapObject::set_map_no_write_barrier(Tagged<Map> value,
                                          ReleaseStoreTag tag) {
  set_map<EmitWriteBarrier::kNo>(value, kReleaseStore,
                                 VerificationMode::kPotentialLayoutChange);
}

template <HeapObject::EmitWriteBarrier emit_write_barrier, typename MemoryOrder>
void HeapObject::set_map(Tagged<Map> value, MemoryOrder order,
                         VerificationMode mode) {
#if V8_ENABLE_WEBASSEMBLY
  // In {WasmGraphBuilder::SetMap} and {WasmGraphBuilder::LoadMap}, we treat
  // maps as immutable. Therefore we are not allowed to mutate them here.
  DCHECK(!IsWasmStructMap(value) && !IsWasmArrayMap(value));
#endif
  // Object layout changes are currently not supported on background threads.
  // This method might change object layout and therefore can't be used on
  // background threads.
  DCHECK_IMPLIES(mode != VerificationMode::kSafeMapTransition,
                 !LocalHeap::Current());
  if (v8_flags.verify_heap && !value.is_null()) {
    Heap* heap = GetHeapFromWritableObject(*this);
    if (mode == VerificationMode::kSafeMapTransition) {
      HeapVerifier::VerifySafeMapTransition(heap, *this, value);
    } else {
      DCHECK_EQ(mode, VerificationMode::kPotentialLayoutChange);
      HeapVerifier::VerifyObjectLayoutChange(heap, *this, value);
    }
  }
  set_map_word(value, order);
  Heap::NotifyObjectLayoutChangeDone(*this);
#ifndef V8_DISABLE_WRITE_BARRIERS
  if (!value.is_null()) {
    if (emit_write_barrier == EmitWriteBarrier::kYes) {
      CombinedWriteBarrier(*this, map_slot(), value, UPDATE_WRITE_BARRIER);
    } else {
      DCHECK_EQ(emit_write_barrier, EmitWriteBarrier::kNo);
      SLOW_DCHECK(!WriteBarrier::IsRequired(*this, value));
    }
  }
#endif
}

void HeapObjectLayout::set_map_after_allocation(Tagged<Map> value,
                                                WriteBarrierMode mode) {
  // TODO(leszeks): Support MapWord members and access via that instead.
  Tagged<HeapObject>(this)->set_map_after_allocation(value, mode);
}

void HeapObject::set_map_after_allocation(Tagged<Map> value,
                                          WriteBarrierMode mode) {
  set_map_word(value, kRelaxedStore);
#ifndef V8_DISABLE_WRITE_BARRIERS
  if (mode != SKIP_WRITE_BARRIER) {
    DCHECK(!value.is_null());
    CombinedWriteBarrier(*this, map_slot(), value, mode);
  } else {
    SLOW_DCHECK(
        // We allow writes of a null map before root initialisation.
        value.is_null() ? !GetIsolateFromWritableObject(*this)
                               ->read_only_heap()
                               ->roots_init_complete()
                        : !WriteBarrier::IsRequired(*this, value));
  }
#endif
}

// static
void HeapObject::SetFillerMap(const WritableFreeSpace& writable_space,
                              Tagged<Map> value) {
  writable_space.WriteHeaderSlot<Map, kMapOffset>(value, kRelaxedStore);
}

DEF_ACQUIRE_GETTER(HeapObject, map, Tagged<Map>) {
  return map_word(cage_base, kAcquireLoad).ToMap();
}

ObjectSlot HeapObject::map_slot() const {
  return ObjectSlot(MapField::address(*this));
}

MapWord HeapObject::map_word(RelaxedLoadTag tag) const {
  // This method is never used for objects located in code space
  // (InstructionStream and free space fillers) and thus it is fine to use
  // auto-computed cage base value.
  DCHECK_IMPLIES(V8_EXTERNAL_CODE_SPACE_BOOL, !IsCodeSpaceObject(*this));
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return HeapObject::map_word(cage_base, tag);
}
MapWord HeapObject::map_word(PtrComprCageBase cage_base,
                             RelaxedLoadTag tag) const {
  return MapField::Relaxed_Load_Map_Word(cage_base, *this);
}

void HeapObject::set_map_word(Tagged<Map> map, RelaxedStoreTag) {
  MapField::Relaxed_Store_Map_Word(*this, MapWord::FromMap(map));
}

void HeapObject::set_map_word_forwarded(Tagged<HeapObject> target_object,
                                        RelaxedStoreTag) {
  MapField::Relaxed_Store_Map_Word(
      *this, MapWord::FromForwardingAddress(*this, target_object));
}

MapWord HeapObject::map_word(AcquireLoadTag tag) const {
  // This method is never used for objects located in code space
  // (InstructionStream and free space fillers) and thus it is fine to use
  // auto-computed cage base value.
  DCHECK_IMPLIES(V8_EXTERNAL_CODE_SPACE_BOOL, !IsCodeSpaceObject(*this));
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return HeapObject::map_word(cage_base, tag);
}
MapWord HeapObject::map_word(PtrComprCageBase cage_base,
                             AcquireLoadTag tag) const {
  return MapField::Acquire_Load_No_Unpack(cage_base, *this);
}

void HeapObject::set_map_word(Tagged<Map> map, ReleaseStoreTag) {
  MapField::Release_Store_Map_Word(*this, MapWord::FromMap(map));
}

void HeapObjectLayout::set_map_word_forwarded(Tagged<HeapObject> target_object,
                                              ReleaseStoreTag tag) {
  // TODO(leszeks): Support MapWord members and access via that instead.
  Tagged<HeapObject>(this)->set_map_word_forwarded(target_object, tag);
}

void HeapObject::set_map_word_forwarded(Tagged<HeapObject> target_object,
                                        ReleaseStoreTag) {
  MapField::Release_Store_Map_Word(
      *this, MapWord::FromForwardingAddress(*this, target_object));
}

bool HeapObject::release_compare_and_swap_map_word_forwarded(
    MapWord old_map_word, Tagged<HeapObject> new_target_object) {
  Tagged_t result = MapField::Release_CompareAndSwap(
      *this, old_map_word,
      MapWord::FromForwardingAddress(*this, new_target_object));
  return result == static_cast<Tagged_t>(old_map_word.ptr());
}

int HeapObjectLayout::Size() const { return Tagged<HeapObject>(this)->Size(); }

// TODO(v8:11880): consider dropping parameterless version.
int HeapObject::Size() const {
  DCHECK_IMPLIES(V8_EXTERNAL_CODE_SPACE_BOOL, !IsCodeSpaceObject(*this));
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return HeapObject::Size(cage_base);
}
int HeapObject::Size(PtrComprCageBase cage_base) const {
  return SizeFromMap(map(cage_base));
}

inline bool IsSpecialReceiverInstanceType(InstanceType instance_type) {
  return instance_type <= LAST_SPECIAL_RECEIVER_TYPE;
}

// This should be in objects/map-inl.h, but can't, because of a cyclic
// dependency.
bool IsSpecialReceiverMap(Tagged<Map> map) {
  bool result = IsSpecialReceiverInstanceType(map->instance_type());
  DCHECK_IMPLIES(
      !result, !map->has_named_interceptor() && !map->is_access_check_needed());
  return result;
}

inline bool IsCustomElementsReceiverInstanceType(InstanceType instance_type) {
  return instance_type <= LAST_CUSTOM_ELEMENTS_RECEIVER;
}

// This should be in objects/map-inl.h, but can't, because of a cyclic
// dependency.
bool IsCustomElementsReceiverMap(Tagged<Map> map) {
  return IsCustomElementsReceiverInstanceType(map->instance_type());
}

// static
bool Object::ToArrayLength(Tagged<Object> obj, uint32_t* index) {
  return Object::ToUint32(obj, index);
}

// static
bool Object::ToArrayIndex(Tagged<Object> obj, uint32_t* index) {
  return Object::ToUint32(obj, index) && *index != kMaxUInt32;
}

// static
bool Object::ToIntegerIndex(Tagged<Object> obj, size_t* index) {
  if (IsSmi(obj)) {
    int num = Smi::ToInt(obj);
    if (num < 0) return false;
    *index = static_cast<size_t>(num);
    return true;
  }
  if (IsHeapNumber(obj)) {
    double num = Cast<HeapNumber>(obj)->value();
    if (!(num >= 0)) return false;  // Negation to catch NaNs.
    constexpr double max =
        std::min(kMaxSafeInteger,
                 // The maximum size_t is reserved as "invalid" sentinel.
                 static_cast<double>(std::numeric_limits<size_t>::max() - 1));
    if (num > max) return false;
    size_t result = static_cast<size_t>(num);
    if (num != result) return false;  // Conversion lost fractional precision.
    *index = result;
    return true;
  }
  return false;
}

WriteBarrierMode HeapObjectLayout::GetWriteBarrierMode(
    const DisallowGarbageCollection& promise) {
  return GetWriteBarrierModeForObject(this, &promise);
}

WriteBarrierMode HeapObject::GetWriteBarrierMode(
    const DisallowGarbageCollection& promise) {
  return GetWriteBarrierModeForObject(*this, &promise);
}

// static
AllocationAlignment HeapObject::RequiredAlignment(Tagged<Map> map) {
  // TODO(v8:4153): We should think about requiring double alignment
  // in general for ByteArray, since they are used as backing store for typed
  // arrays now.
  // TODO(ishell, v8:8875): Consider using aligned allocations for BigInt.
  if (USE_ALLOCATION_ALIGNMENT_BOOL) {
    int instance_type = map->instance_type();

    static_assert(!USE_ALLOCATION_ALIGNMENT_BOOL ||
                  (FixedDoubleArray::kHeaderSize & kDoubleAlignmentMask) ==
                      kTaggedSize);
    if (instance_type == FIXED_DOUBLE_ARRAY_TYPE) return kDoubleAligned;

    static_assert(!USE_ALLOCATION_ALIGNMENT_BOOL ||
                  (offsetof(HeapNumber, value_) & kDoubleAlignmentMask) ==
                      kTaggedSize);
    if (instance_type == HEAP_NUMBER_TYPE) return kDoubleUnaligned;
  }
  return kTaggedAligned;
}

bool HeapObject::CheckRequiredAlignment(PtrComprCageBase cage_base) const {
  AllocationAlignment alignment = HeapObject::RequiredAlignment(map(cage_base));
  CHECK_EQ(0, Heap::GetFillToAlign(address(), alignment));
  return true;
}

Address HeapObject::GetFieldAddress(int field_offset) const {
  return field_address(field_offset);
}

// static
Maybe<bool> Object::GreaterThan(Isolate* isolate, Handle<Object> x,
                                Handle<Object> y) {
  Maybe<ComparisonResult> result = Compare(isolate, x, y);
  if (result.IsJust()) {
    switch (result.FromJust()) {
      case ComparisonResult::kGreaterThan:
        return Just(true);
      case ComparisonResult::kLessThan:
      case ComparisonResult::kEqual:
      case ComparisonResult::kUndefined:
        return Just(false);
    }
  }
  return Nothing<bool>();
}

// static
Maybe<bool> Object::GreaterThanOrEqual(Isolate* isolate, Handle<Object> x,
                                       Handle<Object> y) {
  Maybe<ComparisonResult> result = Compare(isolate, x, y);
  if (result.IsJust()) {
    switch (result.FromJust()) {
      case ComparisonResult::kEqual:
      case ComparisonResult::kGreaterThan:
        return Just(true);
      case ComparisonResult::kLessThan:
      case ComparisonResult::kUndefined:
        return Just(false);
    }
  }
  return Nothing<bool>();
}

// static
Maybe<bool> Object::LessThan(Isolate* isolate, Handle<Object> x,
                             Handle<Object> y) {
  Maybe<ComparisonResult> result = Compare(isolate, x, y);
  if (result.IsJust()) {
    switch (result.FromJust()) {
      case ComparisonResult::kLessThan:
        return Just(true);
      case ComparisonResult::kEqual:
      case ComparisonResult::kGreaterThan:
      case ComparisonResult::kUndefined:
        return Just(false);
    }
  }
  return Nothing<bool>();
}

// static
Maybe<bool> Object::LessThanOrEqual(Isolate* isolate, Handle<Object> x,
                                    Handle<Object> y) {
  Maybe<ComparisonResult> result = Compare(isolate, x, y);
  if (result.IsJust()) {
    switch (result.FromJust()) {
      case ComparisonResult::kEqual:
      case ComparisonResult::kLessThan:
        return Just(true);
      case ComparisonResult::kGreaterThan:
      case ComparisonResult::kUndefined:
        return Just(false);
    }
  }
  return Nothing<bool>();
}

MaybeHandle<Object> Object::GetPropertyOrElement(Isolate* isolate,
                                                 Handle<Object> object,
                                                 Handle<Name> name) {
  PropertyKey key(isolate, name);
  LookupIterator it(isolate, object, key);
  return GetProperty(&it);
}

MaybeHandle<Object> Object::SetPropertyOrElement(
    Isolate* isolate, Handle<Object> object, Handle<Name> name,
    Handle<Object> value, Maybe<ShouldThrow> should_throw,
    StoreOrigin store_origin) {
  PropertyKey key(isolate, name);
  LookupIterator it(isolate, object, key);
  MAYBE_RETURN_NULL(SetProperty(&it, value, store_origin, should_throw));
  return value;
}

MaybeHandle<Object> Object::GetPropertyOrElement(Handle<Object> receiver,
                                                 Handle<Name> name,
                                                 Handle<JSReceiver> holder) {
  Isolate* isolate = holder->GetIsolate();
  PropertyKey key(isolate, name);
  LookupIterator it(isolate, receiver, key, holder);
  return GetProperty(&it);
}

// static
Tagged<Object> Object::GetSimpleHash(Tagged<Object> object) {
  DisallowGarbageCollection no_gc;
  if (IsSmi(object)) {
    uint32_t hash = ComputeUnseededHash(Smi::ToInt(object));
    return Smi::FromInt(hash & Smi::kMaxValue);
  }
  auto instance_type = Cast<HeapObject>(object)->map()->instance_type();
  if (InstanceTypeChecker::IsHeapNumber(instance_type)) {
    double num = Cast<HeapNumber>(object)->value();
    if (std::isnan(num)) return Smi::FromInt(Smi::kMaxValue);
    // Use ComputeUnseededHash for all values in Signed32 range, including -0,
    // which is considered equal to 0 because collections use SameValueZero.
    uint32_t hash;
    // Check range before conversion to avoid undefined behavior.
    if (num >= kMinInt && num <= kMaxInt && FastI2D(FastD2I(num)) == num) {
      hash = ComputeUnseededHash(FastD2I(num));
    } else {
      hash = ComputeLongHash(base::double_to_uint64(num));
    }
    return Smi::FromInt(hash & Smi::kMaxValue);
  } else if (InstanceTypeChecker::IsName(instance_type)) {
    uint32_t hash = Cast<Name>(object)->EnsureHash();
    return Smi::FromInt(hash);
  } else if (InstanceTypeChecker::IsOddball(instance_type)) {
    uint32_t hash = Cast<Oddball>(object)->to_string()->EnsureHash();
    return Smi::FromInt(hash);
  } else if (InstanceTypeChecker::IsBigInt(instance_type)) {
    uint32_t hash = Cast<BigInt>(object)->Hash();
    return Smi::FromInt(hash & Smi::kMaxValue);
  } else if (InstanceTypeChecker::IsSharedFunctionInfo(instance_type)) {
    uint32_t hash = Cast<SharedFunctionInfo>(object)->Hash();
    return Smi::FromInt(hash & Smi::kMaxValue);
  } else if (InstanceTypeChecker::IsScopeInfo(instance_type)) {
    uint32_t hash = Cast<ScopeInfo>(object)->Hash();
    return Smi::FromInt(hash & Smi::kMaxValue);
  } else if (InstanceTypeChecker::IsScript(instance_type)) {
    int id = Cast<Script>(object)->id();
    return Smi::FromInt(ComputeUnseededHash(id) & Smi::kMaxValue);
  }

  DCHECK(!InstanceTypeChecker::IsHole(instance_type));
  DCHECK(IsJSReceiver(object));
  return object;
}

// static
Tagged<Object> Object::GetHash(Tagged<Object> obj) {
  DisallowGarbageCollection no_gc;
  Tagged<Object> hash = GetSimpleHash(obj);
  if (IsSmi(hash)) return hash;

  // Make sure that we never cast internal objects to JSReceivers.
  CHECK(IsJSReceiver(obj));
  Tagged<JSReceiver> receiver = Cast<JSReceiver>(obj);
  return receiver->GetIdentityHash();
}

bool IsShared(Tagged<Object> obj) {
  // This logic should be kept in sync with fast paths in
  // CodeStubAssembler::SharedValueBarrier.

  // Smis are trivially shared.
  if (IsSmi(obj)) return true;

  Tagged<HeapObject> object = Cast<HeapObject>(obj);

  // RO objects are shared when the RO space is shared.
  if (IsReadOnlyHeapObject(object)) {
    return ReadOnlyHeap::IsReadOnlySpaceShared();
  }

  // Check if this object is already shared.
  InstanceType instance_type = object->map()->instance_type();
  if (InstanceTypeChecker::IsAlwaysSharedSpaceJSObject(instance_type)) {
    DCHECK(InAnySharedSpace(object));
    return true;
  }
  switch (instance_type) {
    case SHARED_SEQ_TWO_BYTE_STRING_TYPE:
    case SHARED_SEQ_ONE_BYTE_STRING_TYPE:
    case SHARED_EXTERNAL_TWO_BYTE_STRING_TYPE:
    case SHARED_EXTERNAL_ONE_BYTE_STRING_TYPE:
    case SHARED_UNCACHED_EXTERNAL_TWO_BYTE_STRING_TYPE:
    case SHARED_UNCACHED_EXTERNAL_ONE_BYTE_STRING_TYPE:
      DCHECK(InAnySharedSpace(object));
      return true;
    case INTERNALIZED_TWO_BYTE_STRING_TYPE:
    case INTERNALIZED_ONE_BYTE_STRING_TYPE:
    case EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE:
    case EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE:
      if (v8_flags.shared_string_table) {
        DCHECK(InAnySharedSpace(object));
        return true;
      }
      return false;
    case HEAP_NUMBER_TYPE:
      return InWritableSharedSpace(object);
    default:
      return false;
  }
}

// static
MaybeHandle<Object> Object::Share(Isolate* isolate, Handle<Object> value,
                                  ShouldThrow throw_if_cannot_be_shared) {
  // Sharing values requires the RO space be shared.
  DCHECK(ReadOnlyHeap::IsReadOnlySpaceShared());
  if (IsShared(*value)) return value;
  return ShareSlow(isolate, Cast<HeapObject>(value), throw_if_cannot_be_shared);
}

// https://tc39.es/ecma262/#sec-canbeheldweakly
// static
bool Object::CanBeHeldWeakly(Tagged<Object> obj) {
  if (IsJSReceiver(obj)) {
    // TODO(v8:12547) Shared structs and arrays should only be able to point
    // to shared values in weak collections. For now, disallow them as weak
    // collection keys.
    if (v8_flags.harmony_struct) {
      return !IsJSSharedStruct(obj) && !IsJSSharedArray(obj);
    }
    return true;
  }
  return IsSymbol(obj) && !Cast<Symbol>(obj)->is_in_public_symbol_table();
}

Handle<Object> ObjectHashTableShape::AsHandle(Handle<Object> key) {
  return key;
}

Relocatable::Relocatable(Isolate* isolate) {
  isolate_ = isolate;
  prev_ = isolate->relocatable_top();
  isolate->set_relocatable_top(this);
}

Relocatable::~Relocatable() {
  DCHECK_EQ(isolate_->relocatable_top(), this);
  isolate_->set_relocatable_top(prev_);
}

// Predictably converts HeapObject or Address to uint32 by calculating
// offset of the address in respective MemoryChunk.
static inline uint32_t ObjectAddressForHashing(Address object) {
  return MemoryChunk::AddressToOffset(object);
}

static inline Handle<Object> MakeEntryPair(Isolate* isolate, size_t index,
                                           DirectHandle<Object> value) {
  DirectHandle<Object> key = isolate->factory()->SizeToString(index);
  DirectHandle<FixedArray> entry_storage = isolate->factory()->NewFixedArray(2);
  {
    entry_storage->set(0, *key, SKIP_WRITE_BARRIER);
    entry_storage->set(1, *value, SKIP_WRITE_BARRIER);
  }
  return isolate->factory()->NewJSArrayWithElements(entry_storage,
                                                    PACKED_ELEMENTS, 2);
}

static inline Handle<Object> MakeEntryPair(Isolate* isolate,
                                           DirectHandle<Object> key,
                                           DirectHandle<Object> value) {
  DirectHandle<FixedArray> entry_storage = isolate->factory()->NewFixedArray(2);
  {
    entry_storage->set(0, *key, SKIP_WRITE_BARRIER);
    entry_storage->set(1, *value, SKIP_WRITE_BARRIER);
  }
  return isolate->factory()->NewJSArrayWithElements(entry_storage,
                                                    PACKED_ELEMENTS, 2);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_OBJECTS_INL_H_
