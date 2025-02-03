// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_BUFFER_TAG_H_
#define V8_SANDBOX_EXTERNAL_BUFFER_TAG_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

// Defines the list of valid external buffer tags.
//
// When accessing a buffer located outside the V8 sandbox, an ExternalBufferTag
// must be provided which indicates the expected type of the external buffer.
// When the sandbox is enabled, this tag is used to ensure type-safe access to
// the buffer data: if the provided tag doesn't match the tag of the buffer in
// the external buffer table, an inaccessible (external pointer, size) tuple
// will be returned.
//
// We use the AND-based type-checking mechanism in the ExternalBufferTable,
// similar to the one used by the ExternalPointerTable: the entry in the table
// is ORed with the tag and then ANDed with the inverse of the tag upon access.
// This has the benefit that the type check and the removal of the marking bit
// can be folded into a single bitwise operations.

constexpr uint64_t kExternalBufferMarkBit = 1ULL << 62;
constexpr uint64_t kExternalBufferTagMask = 0x40ff000000000000;
constexpr uint64_t kExternalBufferTagMaskWithoutMarkBit = 0xff000000000000;
constexpr uint64_t kExternalBufferTagShift = 48;

#define TAG(i)                                                       \
  ((kAllTagsForAndBasedTypeChecking[i] << kExternalBufferTagShift) | \
   kExternalBufferMarkBit)

// clang-format off

// Shared external buffers are owned by the shared Isolate and stored in the
// shared external buffer table associated with that Isolate, where they can
// be accessed from multiple threads at the same time. The objects referenced
// in this way must therefore always be thread-safe.
#define SHARED_EXTERNAL_BUFFER_TAGS(V) \
  V(kFirstSharedBufferTag, TAG(0))     \
  V(kLastSharedBufferTag, TAG(0))

// External buffers using these tags are kept in a per-Isolate external
// buffer table and can only be accessed when this Isolate is active.
#define PER_ISOLATE_EXTERNAL_BUFFER_TAGS(V)

// All external buffer tags.
#define ALL_EXTERNAL_BUFFER_TAGS(V) \
  SHARED_EXTERNAL_BUFFER_TAGS(V)    \
  PER_ISOLATE_EXTERNAL_BUFFER_TAGS(V)

#define EXTERNAL_BUFFER_TAG_ENUM(Name, Tag) Name = Tag,
#define MAKE_TAG(HasMarkBit, TypeTag)                            \
  ((static_cast<uint64_t>(TypeTag) << kExternalBufferTagShift) | \
  (HasMarkBit ? kExternalBufferMarkBit : 0))
enum ExternalBufferTag : uint64_t {
  // Empty tag value. Mostly used as placeholder.
  kExternalBufferNullTag = MAKE_TAG(1, 0b00000000),
  // The free entry tag has all type bits set so every type check with a
  // different type fails. It also doesn't have the mark bit set as free
  // entries are (by definition) not alive.
  kExternalBufferFreeEntryTag = MAKE_TAG(0, 0b11111111),
  // Evacuation entries are used during external buffer table compaction.
  kExternalBufferEvacuationEntryTag = MAKE_TAG(1, 0b11111110),

  ALL_EXTERNAL_BUFFER_TAGS(EXTERNAL_BUFFER_TAG_ENUM)
};

#undef MAKE_TAG
#undef TAG
#undef EXTERNAL_BUFFER_TAG_ENUM

// clang-format on

// True if the external pointer must be accessed from external buffer table.
V8_INLINE static constexpr bool IsSharedExternalBufferType(
    ExternalBufferTag tag) {
  return tag >= kFirstSharedBufferTag && tag <= kLastSharedBufferTag;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_EXTERNAL_BUFFER_TAG_H_
