// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_INSTANCE_TYPE_INL_H_
#define V8_OBJECTS_INSTANCE_TYPE_INL_H_

#include "src/base/bounds.h"
#include "src/execution/isolate-utils-inl.h"
#include "src/objects/instance-type-checker.h"
#include "src/objects/instance-type.h"
#include "src/objects/map-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

namespace InstanceTypeChecker {

// INSTANCE_TYPE_CHECKERS macro defines some "types" that do not have
// respective C++ classes (see TypedArrayConstructor, FixedArrayExact) or
// the respective C++ counterpart is actually a template (see HashTable).
// So in order to be able to customize IsType() implementations for specific
// types, we declare a parallel set of "types" that can be compared using
// std::is_same<>.
namespace InstanceTypeTraits {

#define DECL_TYPE(type, ...) class type;
INSTANCE_TYPE_CHECKERS(DECL_TYPE)
TORQUE_INSTANCE_CHECKERS_MULTIPLE_FULLY_DEFINED(DECL_TYPE)
TORQUE_INSTANCE_CHECKERS_MULTIPLE_ONLY_DECLARED(DECL_TYPE)
HEAP_OBJECT_TYPE_LIST(DECL_TYPE)
#undef DECL_TYPE

}  // namespace InstanceTypeTraits

// Instance types which are associated with one unique map.

template <class type>
V8_INLINE constexpr base::Optional<RootIndex> UniqueMapOfInstanceTypeCheck() {
  return {};
}

#define INSTANCE_TYPE_MAP(V, rootIndexName, rootAccessorName, class_name) \
  template <>                                                             \
  V8_INLINE constexpr base::Optional<RootIndex>                           \
  UniqueMapOfInstanceTypeCheck<InstanceTypeTraits::class_name>() {        \
    return {RootIndex::k##rootIndexName};                                 \
  }
UNIQUE_INSTANCE_TYPE_MAP_LIST_GENERATOR(INSTANCE_TYPE_MAP, _)
#undef INSTANCE_TYPE_MAP

V8_INLINE constexpr base::Optional<RootIndex> UniqueMapOfInstanceType(
    InstanceType type) {
#define INSTANCE_TYPE_CHECK(it, forinstancetype)              \
  if (type == forinstancetype) {                              \
    return InstanceTypeChecker::UniqueMapOfInstanceTypeCheck< \
        InstanceTypeChecker::InstanceTypeTraits::it>();       \
  }
  INSTANCE_TYPE_CHECKERS_SINGLE(INSTANCE_TYPE_CHECK);
#undef INSTANCE_TYPE_CHECK

  return Map::TryGetMapRootIdxFor(type);
}

template <InstanceType type>
constexpr bool kHasUniqueMapOfInstanceType =
    UniqueMapOfInstanceType(type).has_value();

template <InstanceType type>
constexpr RootIndex kUniqueMapOfInstanceType =
  kHasUniqueMapOfInstanceType<type>?
    *UniqueMapOfInstanceType(type):
    RootIndex::kRootListLength;

// Manually curated list of instance type ranges which are associated with a
// unique range of map addresses on the read only heap. Both ranges are
// inclusive.

using InstanceTypeRange = std::pair<InstanceType, InstanceType>;
using TaggedAddressRange = std::pair<Tagged_t, Tagged_t>;

#if V8_STATIC_ROOTS_BOOL
constexpr std::array<std::pair<InstanceTypeRange, TaggedAddressRange>, 6>
    kUniqueMapRangeOfInstanceTypeRangeList = {
        {{{ALLOCATION_SITE_TYPE, ALLOCATION_SITE_TYPE},
          {StaticReadOnlyRoot::kAllocationSiteWithWeakNextMap,
           StaticReadOnlyRoot::kAllocationSiteWithoutWeakNextMap}},
         {{FIRST_STRING_TYPE, LAST_STRING_TYPE},
          {StaticReadOnlyRoot::kSeqTwoByteStringMap,
           StaticReadOnlyRoot::kSharedSeqOneByteStringMap}},
         {{FIRST_NAME_TYPE, LAST_NAME_TYPE},
          {StaticReadOnlyRoot::kSeqTwoByteStringMap,
           StaticReadOnlyRoot::kSymbolMap}},
         {{FIRST_SMALL_ORDERED_HASH_TABLE_TYPE,
           LAST_SMALL_ORDERED_HASH_TABLE_TYPE},
          {StaticReadOnlyRoot::kSmallOrderedHashMapMap,
           StaticReadOnlyRoot::kSmallOrderedNameDictionaryMap}},
         {{FIRST_ABSTRACT_INTERNAL_CLASS_TYPE,
           LAST_ABSTRACT_INTERNAL_CLASS_TYPE},
          {StaticReadOnlyRoot::kAbstractInternalClassSubclass1Map,
           StaticReadOnlyRoot::kAbstractInternalClassSubclass2Map}},
         {{FIRST_TURBOFAN_TYPE_TYPE, LAST_TURBOFAN_TYPE_TYPE},
          {StaticReadOnlyRoot::kTurbofanBitsetTypeMap,
           StaticReadOnlyRoot::kTurbofanOtherNumberConstantTypeMap}}}};

struct kUniqueMapRangeOfStringType {
  static constexpr TaggedAddressRange kInternalizedString = {
      StaticReadOnlyRoot::kExternalInternalizedTwoByteStringMap,
      StaticReadOnlyRoot::kInternalizedOneByteStringMap};
  static constexpr TaggedAddressRange kExternalString = {
      StaticReadOnlyRoot::kExternalTwoByteStringMap,
      StaticReadOnlyRoot::kUncachedExternalInternalizedOneByteStringMap};
  static constexpr TaggedAddressRange kThinString = {
      StaticReadOnlyRoot::kThinTwoByteStringMap,
      StaticReadOnlyRoot::kThinOneByteStringMap};
};

inline constexpr base::Optional<TaggedAddressRange>
UniqueMapRangeOfInstanceTypeRange(InstanceType first, InstanceType last) {
  // Doesn't use range based for loop due to LLVM <11 bug re. constexpr
  // functions.
  for (size_t i = 0; i < kUniqueMapRangeOfInstanceTypeRangeList.size(); ++i) {
    if (kUniqueMapRangeOfInstanceTypeRangeList[i].first.first == first &&
        kUniqueMapRangeOfInstanceTypeRangeList[i].first.second == last) {
      return {kUniqueMapRangeOfInstanceTypeRangeList[i].second};
    }
  }
  return {};
}

constexpr inline TaggedAddressRange NULL_ADDRESS_RANGE{kNullAddress, kNullAddress};

template <InstanceType first, InstanceType last>
constexpr bool kHasUniqueMapRangeOfInstanceTypeRange =
    UniqueMapRangeOfInstanceTypeRange(first, last).has_value();

template <InstanceType first, InstanceType last>
constexpr TaggedAddressRange kUniqueMapRangeOfInstanceTypeRange =
  kHasUniqueMapRangeOfInstanceTypeRange<first, last>?
    *UniqueMapRangeOfInstanceTypeRange(first, last):
    NULL_ADDRESS_RANGE;

inline constexpr base::Optional<TaggedAddressRange>
UniqueMapRangeOfInstanceType(InstanceType type) {
  return UniqueMapRangeOfInstanceTypeRange(type, type);
}

template <InstanceType type>
constexpr bool kHasUniqueMapRangeOfInstanceType =
    UniqueMapRangeOfInstanceType(type).has_value();

template <InstanceType type>
constexpr TaggedAddressRange kUniqueMapRangeOfInstanceType =
  kHasUniqueMapRangeOfInstanceType<type>?
    *UniqueMapRangeOfInstanceType(type):
    NULL_ADDRESS_RANGE;

inline bool MayHaveMapCheckFastCase(InstanceType type) {
  if (UniqueMapOfInstanceType(type)) return true;
  for (auto& el : kUniqueMapRangeOfInstanceTypeRangeList) {
    if (el.first.first <= type && type <= el.first.second) {
      return true;
    }
  }
  return false;
}

inline bool CheckInstanceMap(RootIndex expected, Tagged<Map> map) {
  return V8HeapCompressionScheme::CompressObject(map.ptr()) ==
         StaticReadOnlyRootsPointerTable[static_cast<size_t>(expected)];
}

inline bool CheckInstanceMapRange(TaggedAddressRange expected,
                                  Tagged<Map> map) {
  Tagged_t ptr = V8HeapCompressionScheme::CompressObject(map.ptr());
  return base::IsInRange(ptr, expected.first, expected.second);
}

#else

inline bool MayHaveMapCheckFastCase(InstanceType type) { return false; }

#endif  // V8_STATIC_ROOTS_BOOL

// Define type checkers for classes with single instance type.
// INSTANCE_TYPE_CHECKER1 is to be used if the instance type is already loaded.
// INSTANCE_TYPE_CHECKER2 is preferred since it can sometimes avoid loading the
// instance type from the map, if the checked instance type corresponds to a
// known map or range of maps.

#define INSTANCE_TYPE_CHECKER1(type, forinstancetype)             \
  V8_INLINE constexpr bool Is##type(InstanceType instance_type) { \
    return instance_type == forinstancetype;                      \
  }

#if V8_STATIC_ROOTS_BOOL

#define INSTANCE_TYPE_CHECKER2(type, forinstancetype_)                   \
  V8_INLINE bool Is##type(Tagged<Map> map_object) {                      \
    constexpr InstanceType forinstancetype =                             \
        static_cast<InstanceType>(forinstancetype_);                     \
    if constexpr (kHasUniqueMapOfInstanceType<forinstancetype>) {        \
      return CheckInstanceMap(kUniqueMapOfInstanceType<forinstancetype>, \
                              map_object);                               \
    }                                                                    \
    if constexpr (kHasUniqueMapRangeOfInstanceType<forinstancetype>) {   \
      return CheckInstanceMapRange(                                      \
          kUniqueMapRangeOfInstanceType<forinstancetype>, map_object);   \
    }                                                                    \
    return Is##type(map_object->instance_type());                        \
  }

#else

#define INSTANCE_TYPE_CHECKER2(type, forinstancetype) \
  V8_INLINE bool Is##type(Tagged<Map> map_object) {   \
    return Is##type(map_object->instance_type());     \
  }

#endif  // V8_STATIC_ROOTS_BOOL

INSTANCE_TYPE_CHECKERS_SINGLE(INSTANCE_TYPE_CHECKER1)
INSTANCE_TYPE_CHECKERS_SINGLE(INSTANCE_TYPE_CHECKER2)
#undef INSTANCE_TYPE_CHECKER1
#undef INSTANCE_TYPE_CHECKER2

// Checks if value is in range [lower_limit, higher_limit] using a single
// branch. Assumes that the input instance type is valid.
template <InstanceType lower_limit, InstanceType upper_limit>
struct InstanceRangeChecker {
  static constexpr bool Check(InstanceType value) {
    return base::IsInRange(value, lower_limit, upper_limit);
  }
};
template <InstanceType upper_limit>
struct InstanceRangeChecker<FIRST_TYPE, upper_limit> {
  static constexpr bool Check(InstanceType value) {
    DCHECK_LE(FIRST_TYPE, value);
    return value <= upper_limit;
  }
};
template <InstanceType lower_limit>
struct InstanceRangeChecker<lower_limit, LAST_TYPE> {
  static constexpr bool Check(InstanceType value) {
    DCHECK_GE(LAST_TYPE, value);
    return value >= lower_limit;
  }
};

// Define type checkers for classes with ranges of instance types.
// INSTANCE_TYPE_CHECKER_RANGE1 is to be used if the instance type is already
// loaded. INSTANCE_TYPE_CHECKER_RANGE2 is preferred since it can sometimes
// avoid loading the instance type from the map, if the checked instance type
// range corresponds to a known range of maps.

#define INSTANCE_TYPE_CHECKER_RANGE1(type, first_instance_type,            \
                                     last_instance_type)                   \
  V8_INLINE constexpr bool Is##type(InstanceType instance_type) {          \
    return InstanceRangeChecker<first_instance_type,                       \
                                last_instance_type>::Check(instance_type); \
  }

#if V8_STATIC_ROOTS_BOOL

#define INSTANCE_TYPE_CHECKER_RANGE2(type, first_instance_type,                \
                                     last_instance_type)                       \
  V8_INLINE bool Is##type(Tagged<Map> map_object) {                            \
    if constexpr (kHasUniqueMapRangeOfInstanceTypeRange<first_instance_type,   \
                                                        last_instance_type>) { \
      return CheckInstanceMapRange(                                            \
          kUniqueMapRangeOfInstanceTypeRange<first_instance_type,              \
                                             last_instance_type>,              \
          map_object);                                                         \
    }                                                                          \
    return Is##type(map_object->instance_type());                              \
  }

#else

#define INSTANCE_TYPE_CHECKER_RANGE2(type, first_instance_type, \
                                     last_instance_type)        \
  V8_INLINE bool Is##type(Tagged<Map> map_object) {             \
    return Is##type(map_object->instance_type());               \
  }

#endif  // V8_STATIC_ROOTS_BOOL

INSTANCE_TYPE_CHECKERS_RANGE(INSTANCE_TYPE_CHECKER_RANGE1)
INSTANCE_TYPE_CHECKERS_RANGE(INSTANCE_TYPE_CHECKER_RANGE2)
#undef INSTANCE_TYPE_CHECKER_RANGE1
#undef INSTANCE_TYPE_CHECKER_RANGE2

V8_INLINE constexpr bool IsHeapObject(InstanceType instance_type) {
  return true;
}

V8_INLINE constexpr bool IsInternalizedString(InstanceType instance_type) {
  static_assert(kNotInternalizedTag != 0);
  return (instance_type & (kIsNotStringMask | kIsNotInternalizedMask)) ==
         (kStringTag | kInternalizedTag);
}

V8_INLINE bool IsInternalizedString(Tagged<Map> map_object) {
#if V8_STATIC_ROOTS_BOOL
  return CheckInstanceMapRange(kUniqueMapRangeOfStringType::kInternalizedString,
                               map_object);
#else
  return IsInternalizedString(map_object->instance_type());
#endif
}

V8_INLINE constexpr bool IsExternalString(InstanceType instance_type) {
  return (instance_type & (kIsNotStringMask | kStringRepresentationMask)) ==
         kExternalStringTag;
}

V8_INLINE bool IsExternalString(Tagged<Map> map_object) {
#if V8_STATIC_ROOTS_BOOL
  return CheckInstanceMapRange(kUniqueMapRangeOfStringType::kExternalString,
                               map_object);
#else
  return IsExternalString(map_object->instance_type());
#endif
}

V8_INLINE constexpr bool IsThinString(InstanceType instance_type) {
  return (instance_type & kStringRepresentationMask) == kThinStringTag;
}

V8_INLINE bool IsThinString(Tagged<Map> map_object) {
#if V8_STATIC_ROOTS_BOOL
  return CheckInstanceMapRange(kUniqueMapRangeOfStringType::kThinString,
                               map_object);
#else
  return IsThinString(map_object->instance_type());
#endif
}

V8_INLINE constexpr bool IsReferenceComparable(InstanceType instance_type) {
  return !IsString(instance_type) && !IsBigInt(instance_type) &&
         instance_type != HEAP_NUMBER_TYPE;
}

V8_INLINE constexpr bool IsGcSafeCode(InstanceType instance_type) {
  return IsCode(instance_type);
}

V8_INLINE bool IsGcSafeCode(Tagged<Map> map_object) {
  return IsCode(map_object);
}

V8_INLINE constexpr bool IsAbstractCode(InstanceType instance_type) {
  return IsBytecodeArray(instance_type) || IsCode(instance_type);
}

V8_INLINE bool IsAbstractCode(Tagged<Map> map_object) {
  return IsAbstractCode(map_object->instance_type());
}

V8_INLINE constexpr bool IsFreeSpaceOrFiller(InstanceType instance_type) {
  return instance_type == FREE_SPACE_TYPE || instance_type == FILLER_TYPE;
}

V8_INLINE bool IsFreeSpaceOrFiller(Tagged<Map> map_object) {
  return IsFreeSpaceOrFiller(map_object->instance_type());
}

V8_INLINE constexpr bool IsPropertyDictionary(InstanceType instance_type) {
  return instance_type == PROPERTY_DICTIONARY_TYPE;
}

V8_INLINE bool IsPropertyDictionary(Tagged<Map> map_object) {
  return IsPropertyDictionary(map_object->instance_type());
}

// Returns true for those heap object types that must be tied to some native
// context.
V8_INLINE constexpr bool IsNativeContextSpecific(InstanceType instance_type) {
  // All context map are tied to some native context.
  if (IsContext(instance_type)) return true;
  // All non-JSReceivers are never tied to any native context.
  if (!IsJSReceiver(instance_type)) return false;

  // Most of the JSReceivers are tied to some native context modulo the
  // following exceptions.
  if (instance_type == JS_MESSAGE_OBJECT_TYPE ||
      instance_type == JS_EXTERNAL_OBJECT_TYPE) {
    // These JSObject types are wrappers around a set of primitive values
    // and exist only for the purpose of passing the data across V8 Api.
    // Thus they are not tied to any native context.
    return false;

  } else if (InstanceTypeChecker::IsAlwaysSharedSpaceJSObject(instance_type)) {
    // JSObjects allocated in shared space are never tied to a native context.
    return false;
  }
  return true;
}

V8_INLINE bool IsNativeContextSpecificMap(Tagged<Map> map_object) {
  return IsNativeContextSpecific(map_object->instance_type());
}

}  // namespace InstanceTypeChecker

#define TYPE_CHECKER(type, ...)                \
  bool Is##type##Map(Tagged<Map> map) {        \
    return InstanceTypeChecker::Is##type(map); \
  }

INSTANCE_TYPE_CHECKERS(TYPE_CHECKER)
#undef TYPE_CHECKER

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_INSTANCE_TYPE_INL_H_
