// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_TRUSTED_POINTER_TABLE_INL_H_
#define V8_SANDBOX_TRUSTED_POINTER_TABLE_INL_H_

#include "src/sandbox/external-entity-table-inl.h"
#include "src/sandbox/sandbox.h"
#include "src/sandbox/trusted-pointer-table.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

void TrustedPointerTableEntry::MakeTrustedPointerEntry(Address pointer,
                                                       IndirectPointerTag tag,
                                                       bool mark_as_alive) {
  auto payload = Payload::ForTrustedPointerEntry(pointer, tag);
  if (mark_as_alive) payload.SetMarkBit();
  payload_.store(payload, std::memory_order_relaxed);
}

void TrustedPointerTableEntry::MakeFreelistEntry(uint32_t next_entry_index) {
  auto payload = Payload::ForFreelistEntry(next_entry_index);
  payload_.store(payload, std::memory_order_relaxed);
}

void TrustedPointerTableEntry::MakeZappedEntry() {
  auto payload = Payload::ForZappedEntry();
  payload_.store(payload, std::memory_order_relaxed);
}

Address TrustedPointerTableEntry::GetPointer(IndirectPointerTag tag) const {
  DCHECK(!IsFreelistEntry());
  return payload_.load(std::memory_order_relaxed).Untag(tag);
}

void TrustedPointerTableEntry::SetPointer(Address pointer,
                                          IndirectPointerTag tag) {
  DCHECK(!IsFreelistEntry());
  // Currently, this method is only used when the mark bit is unset. If this
  // ever changes, we'd need to check the marking state of the old entry and
  // set the marking state of the new entry accordingly.
  DCHECK(!payload_.load(std::memory_order_relaxed).HasMarkBitSet());
  auto new_payload = Payload::ForTrustedPointerEntry(pointer, tag);
  DCHECK(!new_payload.HasMarkBitSet());
  payload_.store(new_payload, std::memory_order_relaxed);
}

bool TrustedPointerTableEntry::HasPointer(IndirectPointerTag tag) const {
  auto payload = payload_.load(std::memory_order_relaxed);
  if (!payload.ContainsPointer()) return false;
  return tag == kUnknownIndirectPointerTag || payload.IsTaggedWith(tag);
}

bool TrustedPointerTableEntry::IsFreelistEntry() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.ContainsFreelistLink();
}

uint32_t TrustedPointerTableEntry::GetNextFreelistEntryIndex() const {
  return payload_.load(std::memory_order_relaxed).ExtractFreelistLink();
}

void TrustedPointerTableEntry::Mark() {
  auto old_payload = payload_.load(std::memory_order_relaxed);
  DCHECK(old_payload.ContainsPointer());

  auto new_payload = old_payload;
  new_payload.SetMarkBit();

  // We don't need to perform the CAS in a loop since it can only fail if a new
  // value has been written into the entry. This, however, will also have set
  // the marking bit.
  bool success = payload_.compare_exchange_strong(old_payload, new_payload,
                                                  std::memory_order_relaxed);
  DCHECK(success || old_payload.HasMarkBitSet());
  USE(success);
}

void TrustedPointerTableEntry::Unmark() {
  auto payload = payload_.load(std::memory_order_relaxed);
  payload.ClearMarkBit();
  payload_.store(payload, std::memory_order_relaxed);
}

bool TrustedPointerTableEntry::IsMarked() const {
  return payload_.load(std::memory_order_relaxed).HasMarkBitSet();
}

Address TrustedPointerTable::Get(TrustedPointerHandle handle,
                                 IndirectPointerTag tag) const {
  uint32_t index = HandleToIndex(handle);
#if defined(V8_USE_ADDRESS_SANITIZER)
  // We rely on the tagging scheme to produce non-canonical addresses when an
  // entry isn't tagged with the expected tag. Such "safe" crashes can then be
  // filtered out by our sandbox crash filter. However, when ASan is active, it
  // may perform its shadow memory access prior to the actual memory access.
  // For a non-canonical address, this can lead to a segfault at a _canonical_
  // address, which our crash filter can then not distinguish from a "real"
  // crash. Therefore, in ASan builds, we perform an additional CHECK here that
  // the entry is tagged with the expected tag. The resulting CHECK failure
  // will then be ignored by the crash filter.
  // This check is, however, not needed when accessing the null entry, as that
  // is always valid (it just contains nullptr).
  CHECK(index == 0 || at(index).HasPointer(tag));
#else
  // Otherwise, this is just a DCHECK.
  DCHECK(index == 0 || at(index).HasPointer(tag));
#endif
  return at(index).GetPointer(tag);
}

void TrustedPointerTable::Set(TrustedPointerHandle handle, Address pointer,
                              IndirectPointerTag tag) {
  DCHECK_NE(kNullTrustedPointerHandle, handle);
  Validate(pointer, tag);
  uint32_t index = HandleToIndex(handle);
  at(index).SetPointer(pointer, tag);
}

TrustedPointerHandle TrustedPointerTable::AllocateAndInitializeEntry(
    Space* space, Address pointer, IndirectPointerTag tag) {
  DCHECK(space->BelongsTo(this));
  Validate(pointer, tag);
  uint32_t index = AllocateEntry(space);
  at(index).MakeTrustedPointerEntry(pointer, tag, space->allocate_black());
  return IndexToHandle(index);
}

void TrustedPointerTable::Mark(Space* space, TrustedPointerHandle handle) {
  DCHECK(space->BelongsTo(this));
  // The null entry is immortal and immutable, so no need to mark it as alive.
  if (handle == kNullTrustedPointerHandle) return;

  uint32_t index = HandleToIndex(handle);
  DCHECK(space->Contains(index));

  at(index).Mark();
}

void TrustedPointerTable::Zap(TrustedPointerHandle handle) {
  uint32_t index = HandleToIndex(handle);
  at(index).MakeZappedEntry();
}

template <typename Callback>
void TrustedPointerTable::IterateActiveEntriesIn(Space* space,
                                                 Callback callback) {
  IterateEntriesIn(space, [&](uint32_t index) {
    if (!at(index).IsFreelistEntry()) {
      Address pointer = at(index).GetPointer(kUnknownIndirectPointerTag);
      callback(IndexToHandle(index), pointer);
    }
  });
}

uint32_t TrustedPointerTable::HandleToIndex(TrustedPointerHandle handle) const {
  uint32_t index = handle >> kTrustedPointerHandleShift;
  DCHECK_EQ(handle, index << kTrustedPointerHandleShift);
  return index;
}

TrustedPointerHandle TrustedPointerTable::IndexToHandle(uint32_t index) const {
  TrustedPointerHandle handle = index << kTrustedPointerHandleShift;
  DCHECK_EQ(index, handle >> kTrustedPointerHandleShift);
  return handle;
}

void TrustedPointerTable::Validate(Address pointer, IndirectPointerTag tag) {
  if (IsTrustedSpaceMigrationInProgressForObjectsWithTag(tag)) {
    // This CHECK is mostly just here to force tags to be taken out of the
    // IsTrustedSpaceMigrationInProgressForObjectsWithTag function once the
    // objects are fully migrated into trusted space.
    DCHECK(GetProcessWideSandbox()->Contains(pointer));
    return;
  }

  // Entries must never point into the sandbox, as they couldn't be trusted in
  // that case. This CHECK is a defense-in-depth mechanism to guarantee this.
  CHECK(!InsideSandbox(pointer));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX

#endif  // V8_SANDBOX_TRUSTED_POINTER_TABLE_INL_H_
