// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_JS_DISPATCH_TABLE_INL_H_
#define V8_SANDBOX_JS_DISPATCH_TABLE_INL_H_

#include "src/common/code-memory-access-inl.h"
#include "src/objects/objects-inl.h"
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
  CHECK(!IsFreelistEntry());
  return entrypoint_.load(std::memory_order_relaxed);
}

Address JSDispatchEntry::GetCodePointer() const {
  CHECK(!IsFreelistEntry());
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

Tagged<Code> JSDispatchTable::GetCode(JSDispatchHandle handle) {
  Address ptr = GetCodeAddress(handle);
  return Cast<Code>(Tagged<Object>(ptr));
}

void JSDispatchTable::SetCode(JSDispatchHandle handle, Tagged<Code> new_code) {
  // The new code must use JS linkage and its parameter count must match that
  // of the entry, unless the code does not assume a particular parameter count
  // (so uses the kDontAdaptArgumentsSentinel).
  CHECK_EQ(new_code->entrypoint_tag(), kJSEntrypointTag);
  CHECK(new_code->parameter_count() == kDontAdaptArgumentsSentinel ||
        new_code->parameter_count() == GetParameterCount(handle));

  // The object should be in old space to avoid creating old-to-new references.
  DCHECK(!Heap::InYoungGeneration(new_code));

  uint32_t index = HandleToIndex(handle);
  Address new_entrypoint = new_code->instruction_start();
  CFIMetadataWriteScope write_scope("JSDispatchTable update");
  at(index).SetCodeAndEntrypointPointer(new_code.ptr(), new_entrypoint);
}

JSDispatchHandle JSDispatchTable::AllocateAndInitializeEntry(
    Space* space, uint16_t parameter_count) {
  DCHECK(space->BelongsTo(this));
  uint32_t index = AllocateEntry(space);
  CFIMetadataWriteScope write_scope("JSDispatchTable initialize");
  at(index).MakeJSDispatchEntry(kNullAddress, kNullAddress, parameter_count,
                                space->allocate_black());
  return IndexToHandle(index);
}

JSDispatchHandle JSDispatchTable::AllocateAndInitializeEntry(
    Space* space, uint16_t parameter_count, Tagged<Code> new_code) {
  DCHECK(space->BelongsTo(this));
  CHECK_EQ(new_code->entrypoint_tag(), kJSEntrypointTag);
  CHECK(new_code->parameter_count() == kDontAdaptArgumentsSentinel ||
        new_code->parameter_count() == parameter_count);

  uint32_t index = AllocateEntry(space);
  JSDispatchEntry& entry = at(index);
  CFIMetadataWriteScope write_scope("JSDispatchTable initialize");
  entry.MakeJSDispatchEntry(new_code.address(), new_code->instruction_start(),
                            parameter_count, space->allocate_black());
  return IndexToHandle(index);
}

void JSDispatchEntry::SetCodeAndEntrypointPointer(Address new_object,
                                                  Address new_entrypoint) {
  // We currently need a CAS loop here since this can race with ::Mark() and so
  // could otherwise lead to us dropping the marking bit of an entry.
  // TODO(saelo): see if there's a better way to solve this than two CAS loops.
  bool success;
  do {
    Address old_payload = encoded_word_.load(std::memory_order_relaxed);
    Address marking_bit = old_payload & kMarkingBit;
    Address parameter_count = old_payload & kParameterCountMask;
    // We want to preserve the marking bit of the entry. Since that happens to
    // be the tag bit of the pointer, we need to explicitly clear it here.
    Address object = (new_object << kObjectPointerShift) & ~kMarkingBit;
    Address new_payload = object | marking_bit | parameter_count;
    success = encoded_word_.compare_exchange_strong(old_payload, new_payload,
                                                    std::memory_order_relaxed);
  } while (!success);

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
  // TODO(saelo): we probably don't need this loop: if another thread does a
  // SetCode in between, then that should trigger a write barrier which will
  // mark the entry as alive.
  bool success;
  do {
    Address old_value = encoded_word_.load(std::memory_order_relaxed);
    Address new_value = old_value | kMarkingBit;
    success = encoded_word_.compare_exchange_strong(old_value, new_value,
                                                    std::memory_order_relaxed);
  } while (!success);
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

Address JSDispatchTable::GetCodeAddress(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  Address ptr = at(index).GetCodePointer();
  DCHECK(Internals::HasHeapObjectTag(ptr));
  return ptr;
}

uint16_t JSDispatchTable::GetParameterCount(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);
  return at(index).GetParameterCount();
}

void JSDispatchTable::Mark(JSDispatchHandle handle) {
  uint32_t index = HandleToIndex(handle);

  // The read-only space is immortal and cannot be written to.
  if (index < kEndOfInternalReadOnlySegment) return;

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

template <typename Callback>
void JSDispatchTable::IterateMarkedEntriesIn(Space* space, Callback callback) {
  IterateEntriesIn(space, [&](uint32_t index) {
    if (at(index).IsMarked()) {
      callback(IndexToHandle(index));
    }
  });
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX

#endif  // V8_SANDBOX_JS_DISPATCH_TABLE_INL_H_
