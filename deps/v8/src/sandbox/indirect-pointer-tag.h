// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_INDIRECT_POINTER_TAG_H_
#define V8_SANDBOX_INDIRECT_POINTER_TAG_H_

#include "src/common/globals.h"
#include "src/objects/instance-type.h"

namespace v8 {
namespace internal {

// Defines the list of valid indirect pointer tags.
//
// When accessing a trusted/indirect pointer, an IndirectPointerTag must be
// provided which indicates the expected instance type of the pointed-to
// object. When the sandbox is enabled, this tag is used to ensure type-safe
// access to objects referenced via trusted pointers: if the provided tag
// doesn't match the tag of the object in the trusted pointer table, an
// inaccessible pointer will be returned.
//
// We use the shifted instance type as tag and an AND-based type-checking
// mechanism in the TrustedPointerTable, similar to the one used by the
// ExternalPointerTable: the entry in the table is ORed with the tag and then
// ANDed with the inverse of the tag upon access. This has the benefit that the
// type check and the removal of the marking bit can be folded into a single
// bitwise operations. However, it is not technically guaranteed that simply
// using the instance type as tag works for this scheme as the bits of one
// instance type may happen to be a superset of those of another instance type,
// thereby causing the type check to incorrectly pass. As such, the chance of
// getting "incompabitle" tags increases when adding more tags here so we may
// at some point want to consider manually assigning tag values that are
// guaranteed to work (similar for how we do it for ExternalPointerTags).

constexpr int kIndirectPointerTagShift = 48;
constexpr uint64_t kIndirectPointerTagMask = 0x7fff000000000000;
constexpr uint64_t kTrustedPointerTableMarkBit = 0x8000000000000000;
// We use a reserved bit for the free entry tag so that the
// kUnknownIndirectPointerTag cannot untag free entries. Due to that, not all
// tags in the kAllTagsForAndBasedTypeChecking are usable here (which is
// ensured by static asserts below, see VALIDATE_INDIRECT_POINTER_TAG).
// However, in practice this would probably be fine since the payload is a
// table index, and so would likely always crash when treated as a pointer. As
// such, if there is ever need for more tags, this can be reconsidered.
// Note that we use a bit in the 2nd most significant byte here due to top byte
// ignore (TBI), which allows dereferencing pointers even if bits in the most
// significant byte are set.
constexpr uint64_t kTrustedPointerTableFreeEntryBit = 0x0080000000000000;
constexpr uint64_t kIndirectPointerTagMaskWithoutFreeEntryBit =
    0x7f7f000000000000;

// Format is (name, instance type, tag id)
#define INDIRECT_POINTER_TAG_LIST(V)                        \
  V(kCodeIndirectPointerTag, 1)                             \
  V(kBytecodeArrayIndirectPointerTag, 2)                    \
  V(kInterpreterDataIndirectPointerTag, 3)                  \
  V(kUncompiledDataIndirectPointerTag, 4)                   \
  V(kRegExpDataIndirectPointerTag, 5)                       \
  IF_WASM(V, kWasmTrustedInstanceDataIndirectPointerTag, 6) \
  IF_WASM(V, kWasmInternalFunctionIndirectPointerTag, 7)    \
  IF_WASM(V, kWasmFunctionDataIndirectPointerTag, 8)

#define MAKE_TAG(i) \
  (kAllTagsForAndBasedTypeChecking[i] << kIndirectPointerTagShift)

// TODO(saelo): consider renaming this to something like TypeTag or
// InstanceTypeTag since that better captures what this represents.
enum IndirectPointerTag : uint64_t {
  // The null tag. Usually used to express the lack of a valid tag, for example
  // in non-sandbox builds.
  kIndirectPointerNullTag = 0,

  // This tag can be used when an indirect pointer field can legitimately refer
  // to objects of different types.
  // NOTE: this tag effectively disables the built-in type-checking mechanism.
  // As such, in virtually all cases the caller needs to perform runtime-type
  // checks (i.e. IsXyzObject(obj))` afterwards which need to be able to
  // correctly handle unexpected types. The last point is worth stressing
  // further. As an example, the following code is NOT correct:
  //
  //     auto obj = LoadTrustedPointerField<kUnknownIndirectPointerTag>(...);
  //     if (IsFoo(obj)) {
  //         Cast<Foo>(obj)->foo();
  //     } else if (IsBar(obj)) {
  //         Cast<Bar>(obj)->bar();
  //     } else {
  //         // Potential type confusion here!
  //         Cast<Baz>(obj)->baz();
  //     }
  //
  // This is because an attacker can swap trusted pointers and thereby cause an
  // object of a different/unexpected type to be returned. Instead, in this
  // case a CHECK can for example be used to make the code correct:
  //
  //     // ...
  //     } else {
  //         // Must be a Baz object
  //         CHECK(IsBaz(obj));
  //         Cast<Baz>(obj)->baz();
  //    }
  //
  kUnknownIndirectPointerTag = kIndirectPointerTagMaskWithoutFreeEntryBit,

  // Tag used internally by the trusted pointer table to mark free entries.
  // See also the comment above kTrustedPointerTableFreeEntryBit for why this
  // uses a dedicated bit.
  kFreeTrustedPointerTableEntryTag = kTrustedPointerTableFreeEntryBit,

// "Regular" tags. One per supported instance type.
#define INDIRECT_POINTER_TAG_ENUM_DECL(name, tag_id) name = MAKE_TAG(tag_id),
  INDIRECT_POINTER_TAG_LIST(INDIRECT_POINTER_TAG_ENUM_DECL)
#undef INDIRECT_POINTER_TAG_ENUM_DECL
};

#define VALIDATE_INDIRECT_POINTER_TAG(name, tag_id)        \
  static_assert((name & kIndirectPointerTagMask) == name); \
  static_assert((name & kIndirectPointerTagMaskWithoutFreeEntryBit) == name);
INDIRECT_POINTER_TAG_LIST(VALIDATE_INDIRECT_POINTER_TAG)
#undef VALIDATE_INDIRECT_POINTER_TAG
static_assert((kFreeTrustedPointerTableEntryTag & kIndirectPointerTagMask) ==
              kFreeTrustedPointerTableEntryTag);
static_assert((kFreeTrustedPointerTableEntryTag &
               kIndirectPointerTagMaskWithoutFreeEntryBit) == 0);

V8_INLINE constexpr bool IsValidIndirectPointerTag(IndirectPointerTag tag) {
#define VALID_INDIRECT_POINTER_TAG_CASE(tag, tag_id) case tag:
  switch (tag) {
    INDIRECT_POINTER_TAG_LIST(VALID_INDIRECT_POINTER_TAG_CASE)
    return true;
    default:
      return false;
  }
#undef VALID_INDIRECT_POINTER_TAG_CASE
}

// Migrating objects into trusted space is typically performed in multiple
// steps, where all references to the object from inside the sandbox are first
// changed to indirect pointers before actually moving the object out of the
// sandbox. As we have CHECKs that trusted pointer table entries point outside
// of the sandbox, we need this helper function to disable that CHECK for
// objects that are in the process of being migrated into trusted space.
V8_INLINE constexpr bool IsTrustedSpaceMigrationInProgressForObjectsWithTag(
    IndirectPointerTag tag) {
  return false;
}

// The null tag is also considered an invalid tag since no indirect pointer
// field should be using this tag.
static_assert(!IsValidIndirectPointerTag(kIndirectPointerNullTag));

V8_INLINE IndirectPointerTag
IndirectPointerTagFromInstanceType(InstanceType instance_type) {
  switch (instance_type) {
    case CODE_TYPE:
      return kCodeIndirectPointerTag;
    case BYTECODE_ARRAY_TYPE:
      return kBytecodeArrayIndirectPointerTag;
    case INTERPRETER_DATA_TYPE:
      return kInterpreterDataIndirectPointerTag;
    case UNCOMPILED_DATA_WITHOUT_PREPARSE_DATA_TYPE:
    case UNCOMPILED_DATA_WITH_PREPARSE_DATA_TYPE:
    case UNCOMPILED_DATA_WITHOUT_PREPARSE_DATA_WITH_JOB_TYPE:
    case UNCOMPILED_DATA_WITH_PREPARSE_DATA_AND_JOB_TYPE:
      // TODO(saelo): Consider adding support for inheritance hierarchies in
      // our tag checking mechanism.
      return kUncompiledDataIndirectPointerTag;
    case ATOM_REG_EXP_DATA_TYPE:
    case IR_REG_EXP_DATA_TYPE:
      // TODO(saelo): Consider adding support for inheritance hierarchies in
      // our tag checking mechanism.
      return kRegExpDataIndirectPointerTag;
#if V8_ENABLE_WEBASSEMBLY
    case WASM_TRUSTED_INSTANCE_DATA_TYPE:
      return kWasmTrustedInstanceDataIndirectPointerTag;
    case WASM_INTERNAL_FUNCTION_TYPE:
      return kWasmInternalFunctionIndirectPointerTag;
    case WASM_FUNCTION_DATA_TYPE:
    case WASM_EXPORTED_FUNCTION_DATA_TYPE:
    case WASM_JS_FUNCTION_DATA_TYPE:
    case WASM_CAPI_FUNCTION_DATA_TYPE:
      // TODO(saelo): Consider adding support for inheritance hierarchies in
      // our tag checking mechanism.
      return kWasmFunctionDataIndirectPointerTag;
#endif  // V8_ENABLE_WEBASSEMBLY
    default:
      UNREACHABLE();
  }
}

V8_INLINE InstanceType
InstanceTypeFromIndirectPointerTag(IndirectPointerTag tag) {
  DCHECK(IsValidIndirectPointerTag(tag));
  switch (tag) {
#define CASE(name, instance_type, tag_id) \
  case MAKE_TAG(tag_id):                  \
    return instance_type;                 \
    break;
#undef CASE
    default:
      UNREACHABLE();
  }
}

#undef MAKE_TAG
#undef INDIRECT_POINTER_TAG_LIST

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_INDIRECT_POINTER_TAG_H_
