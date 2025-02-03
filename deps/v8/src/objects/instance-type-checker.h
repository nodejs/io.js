// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_INSTANCE_TYPE_CHECKER_H_
#define V8_OBJECTS_INSTANCE_TYPE_CHECKER_H_

#include "src/objects/instance-type.h"
#include "src/roots/roots.h"
#include "src/roots/static-roots.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class Map;

// List of object types that have a single unique instance type.
#define INSTANCE_TYPE_CHECKERS_SINGLE(V)           \
  TORQUE_INSTANCE_CHECKERS_SINGLE_FULLY_DEFINED(V) \
  TORQUE_INSTANCE_CHECKERS_SINGLE_ONLY_DECLARED(V) \
  V(BigInt, BIGINT_TYPE)                           \
  V(FixedArrayExact, FIXED_ARRAY_TYPE)

#define INSTANCE_TYPE_CHECKERS_RANGE(V)                  \
  TORQUE_INSTANCE_CHECKERS_RANGE_FULLY_DEFINED(V)        \
  TORQUE_INSTANCE_CHECKERS_RANGE_ONLY_DECLARED(V)        \
  V(CallableJSFunction, FIRST_CALLABLE_JS_FUNCTION_TYPE, \
    LAST_CALLABLE_JS_FUNCTION_TYPE)

#define INSTANCE_TYPE_CHECKERS_CUSTOM(V) \
  V(AbstractCode)                        \
  V(ExternalString)                      \
  V(FreeSpaceOrFiller)                   \
  V(GcSafeCode)                          \
  V(InternalizedString)                  \
  V(PropertyDictionary)

#define INSTANCE_TYPE_CHECKERS(V)  \
  INSTANCE_TYPE_CHECKERS_SINGLE(V) \
  INSTANCE_TYPE_CHECKERS_RANGE(V)  \
  INSTANCE_TYPE_CHECKERS_CUSTOM(V)

namespace InstanceTypeChecker {
#define IS_TYPE_FUNCTION_DECL(Type, ...)                         \
  V8_INLINE constexpr bool Is##Type(InstanceType instance_type); \
  V8_INLINE bool Is##Type(Tagged<Map> map);

INSTANCE_TYPE_CHECKERS(IS_TYPE_FUNCTION_DECL)

#undef IS_TYPE_FUNCTION_DECL

#if V8_STATIC_ROOTS_BOOL

// Maps for primitive objects and a select few JS objects are allocated in r/o
// space. All JS_RECEIVER maps must come after primitive object maps, i.e. they
// have a compressed address above the last primitive object map root. If we
// have a receiver and need to distinguish whether it is either a primitive
// object or a JS receiver, it suffices to check if its map is allocated above
// the following limit address.
constexpr Tagged_t kNonJsReceiverMapLimit =
    StaticReadOnlyRootsPointerTable[static_cast<size_t>(
        RootIndex::kFirstJSReceiverMapRoot)] &
    ~0xFFF;

// Maps for strings allocated as the first maps in r/o space, so their lower
// bound is zero.
constexpr Tagged_t kStringMapLowerBound = 0;
// If we have a receiver and need to distinguish whether it is a string or not,
// it suffices to check whether it is less-than-equal to the following value.
constexpr Tagged_t kStringMapUpperBound =
    StaticReadOnlyRoot::kThinOneByteStringMap;

#define ASSERT_IS_LAST_STRING_MAP(instance_type, size, name, Name) \
  static_assert(StaticReadOnlyRoot::k##Name##Map <= kStringMapUpperBound);
STRING_TYPE_LIST(ASSERT_IS_LAST_STRING_MAP)
#undef ASSERT_IS_LAST_STRING_MAP

// For performance, the limit is chosen to be encodable as an Arm64
// constant. See Assembler::IsImmAddSub in assembler-arm64.cc.
//
// If this assert fails, then you have perturbed the allocation pattern in
// Heap::CreateReadOnlyHeapObjects. Currently this limit is ensured to exist by
// allocating the first JSReceiver map in RO space a sufficiently large distance
// away from the last non-JSReceiver map.
static_assert(kNonJsReceiverMapLimit != 0 &&
              is_uint12(kNonJsReceiverMapLimit >> 12) &&
              ((kNonJsReceiverMapLimit & 0xFFF) == 0));

#else

constexpr Tagged_t kNonJsReceiverMapLimit = 0x0;

#endif  // V8_STATIC_ROOTS_BOOL
}  // namespace InstanceTypeChecker

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_INSTANCE_TYPE_CHECKER_H_
