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
constexpr uint64_t kIndirectPointerTagMask = 0x7fff'0000'0000'0000;
constexpr uint64_t kTrustedPointerTableMarkBit = 0x8000'0000'0000'0000;
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
constexpr uint64_t kTrustedPointerTableFreeEntryBit = 0x0080'0000'0000'0000;
constexpr uint64_t kIndirectPointerTagMaskWithoutFreeEntryBit =
    0x7f7f'0000'0000'0000;

// TODO(saelo): Also switch the trusted pointer table to use a range-based type
// checking mechanism instead of the AND-based one. This will allow us to
// support type hierarchies and allow for more tags. See the
// ExternalPointerTable for the type checking scheme that we should use here.
constexpr uint64_t kAllTagsForAndBasedTypeChecking[] = {
    0b00001111, 0b00010111, 0b00011011, 0b00011101, 0b00011110, 0b00100111,
    0b00101011, 0b00101101, 0b00101110, 0b00110011, 0b00110101, 0b00110110,
    0b00111001, 0b00111010, 0b00111100, 0b01000111, 0b01001011, 0b01001101,
    0b01001110, 0b01010011, 0b01010101, 0b01010110, 0b01011001, 0b01011010,
    0b01011100, 0b01100011, 0b01100101, 0b01100110, 0b01101001, 0b01101010,
    0b01101100, 0b01110001, 0b01110010, 0b01110100, 0b01111000, 0b10000111,
    0b10001011, 0b10001101, 0b10001110, 0b10010011, 0b10010101, 0b10010110,
    0b10011001, 0b10011010, 0b10011100, 0b10100011, 0b10100101, 0b10100110,
    0b10101001, 0b10101010, 0b10101100, 0b10110001, 0b10110010, 0b10110100,
    0b10111000, 0b11000011, 0b11000101, 0b11000110, 0b11001001, 0b11001010,
    0b11001100, 0b11010001, 0b11010010, 0b11010100, 0b11011000, 0b11100001,
    0b11100010, 0b11100100, 0b11101000, 0b11110000};

// Shared trusted pointers are owned by the shared Isolate and stored in the
// shared trusted pointer table associated with that Isolate, where they can
// be accessed from multiple threads at the same time. The objects referenced
// in this way must therefore always be thread-safe.
// TODO(358918874): Consider having explicitly shared types (e.g.
// `ExposedSharedTrustedObject`) and enforcing that shared tags are only ever
// used with shared types.
#define SHARED_TRUSTED_POINTER_TAG_LIST(V) \
  V(kFirstSharedTrustedTag, 1)             \
  V(kLastSharedTrustedTag, 1)
// Leave some space in the tag range here for future shared tags.

// Trusted pointers using these tags are kept in a per-Isolate trusted
// pointer table and can only be accessed when this Isolate is active.
#define PER_ISOLATE_INDIRECT_POINTER_TAG_LIST(V)             \
  V(kFirstPerIsolateTrustedTag, 6)                           \
  V(kCodeIndirectPointerTag, 6)                              \
  V(kBytecodeArrayIndirectPointerTag, 7)                     \
  V(kInterpreterDataIndirectPointerTag, 8)                   \
  V(kUncompiledDataIndirectPointerTag, 9)                    \
  V(kRegExpDataIndirectPointerTag, 10)                       \
  IF_WASM(V, kWasmTrustedInstanceDataIndirectPointerTag, 11) \
  IF_WASM(V, kWasmInternalFunctionIndirectPointerTag, 12)    \
  IF_WASM(V, kWasmFunctionDataIndirectPointerTag, 13)        \
  IF_WASM(V, kWasmDispatchTableIndirectPointerTag, 14)       \
  V(kLastPerIsolateTrustedTag, 14)

#define INDIRECT_POINTER_TAG_LIST(V) \
  SHARED_TRUSTED_POINTER_TAG_LIST(V) \
  PER_ISOLATE_INDIRECT_POINTER_TAG_LIST(V)

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

// True if the external pointer must be accessed from the shared isolate's
// external pointer table.
V8_INLINE static constexpr bool IsSharedTrustedPointerType(
    IndirectPointerTag tag) {
  static_assert(IndirectPointerTag::kFirstSharedTrustedTag <=
                IndirectPointerTag::kLastSharedTrustedTag);
  return tag >= IndirectPointerTag::kFirstSharedTrustedTag &&
         tag <= IndirectPointerTag::kLastSharedTrustedTag;
}

V8_INLINE static constexpr bool IsPerIsolateTrustedPointerType(
    IndirectPointerTag tag) {
  static_assert(IndirectPointerTag::kFirstPerIsolateTrustedTag <=
                IndirectPointerTag::kLastPerIsolateTrustedTag);
  return tag >= IndirectPointerTag::kFirstPerIsolateTrustedTag &&
         tag <= IndirectPointerTag::kLastPerIsolateTrustedTag;
}

V8_INLINE constexpr bool IsValidIndirectPointerTag(IndirectPointerTag tag) {
  return IsPerIsolateTrustedPointerType(tag) || IsSharedTrustedPointerType(tag);
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
    case WASM_DISPATCH_TABLE_TYPE:
      return kWasmDispatchTableIndirectPointerTag;
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

// Sanity checks.
#define CHECK_SHARED_TRUSTED_POINTER_TAGS(Tag, ...) \
  static_assert(IsSharedTrustedPointerType(Tag));
#define CHECK_NON_SHARED_TRUSTED_POINTER_TAGS(Tag, ...) \
  static_assert(!IsSharedTrustedPointerType(Tag));

SHARED_TRUSTED_POINTER_TAG_LIST(CHECK_SHARED_TRUSTED_POINTER_TAGS)
PER_ISOLATE_INDIRECT_POINTER_TAG_LIST(CHECK_NON_SHARED_TRUSTED_POINTER_TAGS)

#undef CHECK_NON_SHARED_TRUSTED_POINTER_TAGS
#undef CHECK_SHARED_TRUSTED_POINTER_TAGS

#undef SHARED_TRUSTED_POINTER_TAG_LIST
#undef PER_ISOLATE_INDIRECT_POINTER_TAG_LIST
#undef INDIRECT_POINTER_TAG_LIST

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_INDIRECT_POINTER_TAG_H_
