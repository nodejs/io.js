// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_JS_DISPATCH_TABLE_INL_H_
#define V8_SANDBOX_JS_DISPATCH_TABLE_INL_H_

#include "src/common/code-memory-access-inl.h"
#include "src/sandbox/external-entity-table-inl.h"
#include "src/sandbox/js-dispatch-table.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

void JSDispatchEntry::MakeJSDispatchEntry(Address object, Address entrypoint,
                                          uint16_t parameter_count,
                                          bool mark_as_alive) {
  DCHECK_EQ(object & kHeapObjectTag, 0);
  DCHECK_EQ((object << kObjectPointerShift) >> kObjectPointerShift, object);

  Address payload = (object << kObjectPointerShift) | parameter_count;
  if (mark_as_alive) payload |= kMarkingBit;
  encoded_word_.store(payload, std::memory_order_relaxed);
  entrypoint_.store(entrypoint, std::memory_order_relaxed);
}

Address JSDispatchEntry::GetEntrypoint() const {
  DCHECK(!IsFreelistEntry());
  return entrypoint_.load(std::memory_order_relaxed);
}

Address JSDispatchEntry::GetCodePointer() const {
  DCHECK(!IsFreelistEntry());
  // The pointer tag bit (LSB) of the object pointer is used as marking bit,
  // and so may be 0 or 1 here. As the return value is a tagged pointer, the
  // bit must be 1 when returned, so we need to set it here.
  Address payload = encoded_word_.load(std::memory_order_relaxed);
  return (payload >> kObjectPointerShift) | kHeapObjectTag;
}

uint16_t JSDispatchEntry::GetParameterCount() const {
  // Loading a pointer out of a freed entry will always result in an invalid
  // pointer (e.g. upper bits set or nullptr). However, here we're just loading
  // an integer (the parameter count), so we probably want to make sure that
  // we're not getting that from a freed entry.
  CHECK(!IsFreelistEntry());
  Address payload = encoded_word_.load(std::memory_order_relaxed);
  return payload & kParameterCountMask;
}

void JSDispatchEntry::SetCodeAndEntrypointPointer(Address new_object,
                                                  Address new_entrypoint) {
  // The entry must be alive if it is being set, so make sure that the marking
  // bit (which is the heap object tag bit) is set. ::Mark relies on this.
  DCHECK_EQ(new_object & kHeapObjectTag, 1);

  uint16_t parameter_count = GetParameterCount();
  Address payload = (new_object << kObjectPointerShift) | parameter_count;
  encoded_word_.store(payload, std::memory_order_relaxed);
  entrypoint_.store(new_entrypoint, std::memory_order_relaxed);
}

void JSDispatchEntry::MakeFreelistEntry(uint32_t next_entry_index) {
  Address payload = kFreeEntryTag | next_entry_index;
  entrypoint_.store(payload, std::memory_order_relaxed);
  encoded_word_.store(kNullAddress, std::memory_order_relaxed);
}

bool JSDispatchEntry::IsFreelistEntry() const {
  auto entrypoint = entrypoint_.load(std::memory_order_relaxed);
  return (entrypoint & kFreeEntryTag) == kFreeEntryTag;
}

uint32_t JSDispatchEntry::GetNextFreelistEntryIndex() const {
  return static_cast<uint32_t>(entrypoint_.load(std::memory_order_relaxed));
}

void JSDispatchEntry::Mark() {
  Address old_value = encoded_word_.load(std::memory_order_relaxed);
  Address new_value = old_value | kMarkingBit;

  // We don't need to perform the CAS in a loop since it can only fail if a new
  // value has been written into the entry. This, however, will also have set
  // the marking bit.
  bool success = encoded_word_.compare_exchange_strong(
      old_value, new_value, std::memory_order_relaxed);
  DCHECK(success || (old_value & kMarkingBit) == kMarkingBit);
  USE(success);
}

void JSDispatchEntry::Unmark() {
  Address value = encoded_word_.load(std::memory_order_relaxed);
  value &= ~kMarkingBit;
  encoded_word_.store(value, std::memory_order_relaxed);
}

bool JSDispatchEntry::IsMarked() const {
  Address value = encoded_word_.load(std::memory_order_relaxed);
  return value & kMarkingBit;
}

Address JSDispatchTable::GetEntrypoint(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  return at(index).GetEntrypoint();
}

uint16_t JSDispatchTable::GetParameterCount(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  return at(index).GetParameterCount();
}

void JSDispatchTable::Mark(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);

  // The null entry is immortal and immutable, so no need to mark it as alive.
  if (handle == kNullJSDispatchHandle) return;

  CFIMetadataWriteScope write_scope("JSDispatchTable write");
  at(index).Mark();
}

#ifdef DEBUG
void JSDispatchTable::VerifyEntry(JSDispatchHandle handle, Space* space,
                                  Space* ro_space) {
  DCHECK(space->BelongsTo(this));
  DCHECK(ro_space->BelongsTo(this));
  if (handle == kNullJSDispatchHandle) {
    return;
  }
  uint32_t index = HandleToIndex(handle);
  if (ro_space->Contains(index)) {
    DCHECK(at(index).IsMarked());
  } else {
    DCHECK(space->Contains(index));
  }
}
#endif  // DEBUG

template <typename Callback>
void JSDispatchTable::IterateActiveEntriesIn(Space* space, Callback callback) {
  IterateEntriesIn(space, [&](uint32_t index) {
    if (!at(index).IsFreelistEntry()) {
      callback(IndexToHandle(index));
    }
  });
}

uint32_t JSDispatchTable::HandleToIndex(JSDispatchHandle handle) const {
  uint32_t index = handle >> kJSDispatchHandleShift;
  DCHECK_EQ(handle, index << kJSDispatchHandleShift);
  return index;
}

JSDispatchHandle JSDispatchTable::IndexToHandle(uint32_t index) const {
  JSDispatchHandle handle = index << kJSDispatchHandleShift;
  DCHECK_EQ(index, handle >> kJSDispatchHandleShift);
  return handle;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX

#endif  // V8_SANDBOX_JS_DISPATCH_TABLE_INL_H_
