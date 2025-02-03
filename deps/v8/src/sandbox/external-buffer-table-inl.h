// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_BUFFER_TABLE_INL_H_
#define V8_SANDBOX_EXTERNAL_BUFFER_TABLE_INL_H_

#include "src/sandbox/compactible-external-entity-table-inl.h"
#include "src/sandbox/external-buffer-table.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

void ExternalBufferTableEntry::MakeExternalBufferEntry(
    std::pair<Address, size_t> buffer, ExternalBufferTag tag) {
  DCHECK_EQ(0, buffer.first & kExternalBufferTagMask);
  DCHECK(tag & kExternalBufferMarkBit);
  DCHECK_NE(tag, kExternalBufferFreeEntryTag);
  DCHECK_NE(tag, kExternalBufferEvacuationEntryTag);

  Payload new_payload(buffer.first, tag);
  payload_.store(new_payload, std::memory_order_relaxed);
  size_.store(buffer.second, std::memory_order_relaxed);
}

std::pair<Address, size_t> ExternalBufferTableEntry::GetExternalBuffer(
    ExternalBufferTag tag) const {
  auto payload = payload_.load(std::memory_order_relaxed);
  auto size = size_.load(std::memory_order_relaxed);
  DCHECK(payload.ContainsPointer());
  return {payload.Untag(tag), size};
}

bool ExternalBufferTableEntry::HasExternalBuffer(ExternalBufferTag tag) const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.IsTaggedWith(tag);
}

void ExternalBufferTableEntry::MakeFreelistEntry(uint32_t next_entry_index) {
  // The next freelist entry is stored in the lower bits of the entry.
  Payload new_payload(next_entry_index, kExternalBufferFreeEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
  size_.store(0, std::memory_order_relaxed);
}

uint32_t ExternalBufferTableEntry::GetNextFreelistEntryIndex() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.ExtractFreelistLink();
}

void ExternalBufferTableEntry::Mark() {
  auto old_payload = payload_.load(std::memory_order_relaxed);
  DCHECK(old_payload.ContainsPointer());

  auto new_payload = old_payload;
  new_payload.SetMarkBit();

  // We don't need to perform the CAS in a loop: if the new value is not equal
  // to the old value, then the mutator must've just written a new value into
  // the entry. This in turn must've set the marking bit already (see e.g.
  // StoreExternalPointer), so we don't need to do it again.
  bool success = payload_.compare_exchange_strong(old_payload, new_payload,
                                                  std::memory_order_relaxed);
  DCHECK(success || old_payload.HasMarkBitSet());
  USE(success);
}

void ExternalBufferTableEntry::MakeEvacuationEntry(Address handle_location) {
  Payload new_payload(handle_location, kExternalBufferEvacuationEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

bool ExternalBufferTableEntry::HasEvacuationEntry() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.ContainsEvacuationEntry();
}

void ExternalBufferTableEntry::MigrateInto(ExternalBufferTableEntry& other) {
  auto payload = payload_.load(std::memory_order_relaxed);
  auto size = size_.load(std::memory_order_relaxed);
  // We expect to only migrate entries containing external pointers.
  DCHECK(payload.ContainsPointer());

  other.payload_.store(payload, std::memory_order_relaxed);
  other.size_.store(size, std::memory_order_relaxed);

#ifdef DEBUG
  // In debug builds, we clobber this old entry so that any sharing of table
  // entries is easily detected. Shared entries would require write barriers,
  // so we'd like to avoid them. See the compaction algorithm explanation in
  // compactible-external-entity-table.h for more details.
  constexpr Address kClobberedEntryMarker = static_cast<Address>(-1);
  Payload clobbered(kClobberedEntryMarker, kExternalBufferNullTag);
  DCHECK_NE(payload, clobbered);
  payload_.store(clobbered, std::memory_order_relaxed);
#endif  // DEBUG
}

std::pair<Address, size_t> ExternalBufferTable::Get(
    ExternalBufferHandle handle, ExternalBufferTag tag) const {
  uint32_t index = HandleToIndex(handle);
  DCHECK(index == 0 || at(index).HasExternalBuffer(tag));
  return at(index).GetExternalBuffer(tag);
}

ExternalBufferHandle ExternalBufferTable::AllocateAndInitializeEntry(
    Space* space, std::pair<Address, size_t> initial_buffer,
    ExternalBufferTag tag) {
  DCHECK(space->BelongsTo(this));
  uint32_t index = AllocateEntry(space);
  at(index).MakeExternalBufferEntry(initial_buffer, tag);

  return IndexToHandle(index);
}

void ExternalBufferTable::Mark(Space* space, ExternalBufferHandle handle,
                               Address handle_location) {
  DCHECK(space->BelongsTo(this));

  // The null entry is immortal and immutable, so no need to mark it as alive.
  if (handle == kNullExternalBufferHandle) return;

  // The handle_location must always contain the given handle.
  DCHECK_EQ(handle,
            base::AsAtomic32::Acquire_Load(
                reinterpret_cast<ExternalBufferHandle*>(handle_location)));

  uint32_t index = HandleToIndex(handle);
  DCHECK(space->Contains(index));

  // If the table is being compacted and the entry is inside the evacuation
  // area, then allocate and set up an evacuation entry for it.
  MaybeCreateEvacuationEntry(space, index, handle_location);

  // Even if the entry is marked for evacuation, it still needs to be marked as
  // alive as it may be visited during sweeping before being evacuation.
  at(index).Mark();
}

// static
bool ExternalBufferTable::IsValidHandle(ExternalBufferHandle handle) {
  uint32_t index = handle >> kExternalBufferHandleShift;
  return handle == index << kExternalBufferHandleShift;
}

// static
uint32_t ExternalBufferTable::HandleToIndex(ExternalBufferHandle handle) {
  DCHECK(IsValidHandle(handle));
  uint32_t index = handle >> kExternalBufferHandleShift;
  DCHECK_LE(index, kMaxExternalBufferPointers);
  return index;
}

// static
ExternalBufferHandle ExternalBufferTable::IndexToHandle(uint32_t index) {
  DCHECK_LE(index, kMaxExternalBufferPointers);
  ExternalBufferHandle handle = index << kExternalBufferHandleShift;
  DCHECK_NE(handle, kNullExternalBufferHandle);
  return handle;
}

void ExternalBufferTable::Space::NotifyExternalPointerFieldInvalidated(
    Address field_address) {
#ifdef DEBUG
  ExternalBufferHandle handle = base::AsAtomic32::Acquire_Load(
      reinterpret_cast<ExternalBufferHandle*>(field_address));
  DCHECK(Contains(HandleToIndex(handle)));
#endif
  AddInvalidatedField(field_address);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX

#endif  // V8_SANDBOX_EXTERNAL_BUFFER_TABLE_INL_H_
