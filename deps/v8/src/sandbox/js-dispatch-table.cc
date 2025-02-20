// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/js-dispatch-table.h"

#include "src/common/code-memory-access-inl.h"
#include "src/execution/isolate.h"
#include "src/logging/counters.h"
#include "src/objects/code-inl.h"
#include "src/sandbox/js-dispatch-table-inl.h"

#ifdef V8_ENABLE_LEAPTIERING

namespace v8 {
namespace internal {

void JSDispatchEntry::CheckFieldOffsets() {
  static_assert(JSDispatchEntry::kEntrypointOffset ==
                offsetof(JSDispatchEntry, entrypoint_));
  static_assert(JSDispatchEntry::kCodeObjectOffset ==
                offsetof(JSDispatchEntry, encoded_word_));
#if defined(V8_TARGET_ARCH_64_BIT)
#ifdef V8_TARGET_BIG_ENDIAN
  // 2-byte parameter count is on the least significant side of encoded_word_.
  constexpr int kBigEndianParamCountOffset = sizeof(Address) - sizeof(uint16_t);
  static_assert(sizeof(encoded_word_) == sizeof(Address));
  static_assert(JSDispatchEntry::kParameterCountOffset ==
                offsetof(JSDispatchEntry, encoded_word_) +
                    kBigEndianParamCountOffset);
#else
  static_assert(JSDispatchEntry::kParameterCountOffset ==
                offsetof(JSDispatchEntry, encoded_word_));
#endif  // V8_TARGET_BIG_ENDIAN
  static_assert(kParameterCountMask == 0xffff);
  static_assert(kParameterCountSize == 2);
#elif defined(V8_TARGET_ARCH_32_BIT)
  static_assert(JSDispatchEntry::kParameterCountOffset ==
                offsetof(JSDispatchEntry, parameter_count_));
  static_assert(kParameterCountSize ==
                sizeof(JSDispatchEntry::parameter_count_));
#else
#error "Unsupported Architecture"
#endif
}

JSDispatchHandle JSDispatchTable::PreAllocateEntries(
    Space* space, int count, bool ensure_static_handles) {
  DCHECK(space->BelongsTo(this));
  DCHECK_IMPLIES(ensure_static_handles, space->is_internal_read_only_space());
  JSDispatchHandle first;
  for (int i = 0; i < count; ++i) {
    uint32_t idx = AllocateEntry(space);
    if (i == 0) {
      first = IndexToHandle(idx);
    } else {
      // Pre-allocated entries should be consecutive.
      DCHECK_EQ(IndexToHandle(idx), IndexToHandle(HandleToIndex(first) + i));
    }
#if V8_STATIC_DISPATCH_HANDLES_BOOL
    if (ensure_static_handles) {
      CHECK_EQ(IndexToHandle(idx), GetStaticHandleForReadOnlySegmentEntry(i));
    }
#else
    CHECK(!ensure_static_handles);
#endif
  }
  return first;
}

bool JSDispatchTable::PreAllocatedEntryNeedsInitialization(
    Space* space, JSDispatchHandle handle) {
  DCHECK(space->BelongsTo(this));
  uint32_t index = HandleToIndex(handle);
  return at(index).IsFreelistEntry();
}

void JSDispatchTable::InitializePreAllocatedEntry(Space* space,
                                                  JSDispatchHandle handle,
                                                  Tagged<Code> code,
                                                  uint16_t parameter_count) {
  DCHECK(space->BelongsTo(this));
  uint32_t index = HandleToIndex(handle);
  DCHECK(space->Contains(index));
  DCHECK(at(index).IsFreelistEntry());
  CFIMetadataWriteScope write_scope(
      "JSDispatchTable initialize pre-allocated entry");
  at(index).MakeJSDispatchEntry(code.address(), code->instruction_start(),
                                parameter_count, space->allocate_black());
}

#ifdef DEBUG
bool JSDispatchTable::IsMarked(JSDispatchHandle handle) {
  return at(HandleToIndex(handle)).IsMarked();
}
#endif  // DEBUG

void JSDispatchTable::PrintEntry(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  i::PrintF("JSDispatchEntry @ %p\n", &at(index));
  i::PrintF("* code %p\n", reinterpret_cast<void*>(GetCode(handle).address()));
  i::PrintF("* params %d\n", at(HandleToIndex(handle)).GetParameterCount());
  i::PrintF("* entrypoint %p\n",
            reinterpret_cast<void*>(GetEntrypoint(handle)));
}

void JSDispatchTable::PrintCurrentTieringRequest(JSDispatchHandle handle,
                                                 Isolate* isolate,
                                                 std::ostream& os) {
#define CASE(name, ...)                                               \
  if (IsTieringRequested(handle, TieringBuiltin::k##name, isolate)) { \
    os << #name;                                                      \
    return;                                                           \
  }
  BUILTIN_LIST_BASE_TIERING(CASE)
#undef CASE
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_LEAPTIERING
