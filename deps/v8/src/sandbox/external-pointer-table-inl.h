// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_
#define V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_

#include "src/sandbox/compactible-external-entity-table-inl.h"
#include "src/sandbox/external-pointer-table.h"
#include "src/sandbox/external-pointer.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

void ExternalPointerTableEntry::MakeExternalPointerEntry(
    Address value, ExternalPointerTag tag) {
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  DCHECK(tag & kExternalPointerMarkBit);
  DCHECK_NE(tag, kExternalPointerFreeEntryTag);
  DCHECK_NE(tag, kExternalPointerEvacuationEntryTag);

  Payload new_payload(value, tag);
  payload_.store(new_payload, std::memory_order_relaxed);
  MaybeUpdateRawPointerForLSan(value);
}

Address ExternalPointerTableEntry::GetExternalPointer(
    ExternalPointerTag tag) const {
  auto payload = payload_.load(std::memory_order_relaxed);
  DCHECK(payload.ContainsPointer());
  return payload.Untag(tag);
}

void ExternalPointerTableEntry::SetExternalPointer(Address value,
                                                   ExternalPointerTag tag) {
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  DCHECK(tag & kExternalPointerMarkBit);
  DCHECK(payload_.load(std::memory_order_relaxed).ContainsPointer());

  Payload new_payload(value, tag);
  payload_.store(new_payload, std::memory_order_relaxed);
  MaybeUpdateRawPointerForLSan(value);
}

bool ExternalPointerTableEntry::HasExternalPointer(
    ExternalPointerTag tag) const {
  auto payload = payload_.load(std::memory_order_relaxed);
  if (!payload.ContainsPointer()) return false;
  return tag == kAnyExternalPointerTag || payload.IsTaggedWith(tag);
}

Address ExternalPointerTableEntry::ExchangeExternalPointer(
    Address value, ExternalPointerTag tag) {
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  DCHECK(tag & kExternalPointerMarkBit);

  Payload new_payload(value, tag);
  Payload old_payload =
      payload_.exchange(new_payload, std::memory_order_relaxed);
  DCHECK(old_payload.ContainsPointer());
  MaybeUpdateRawPointerForLSan(value);
  return old_payload.Untag(tag);
}

ExternalPointerTag ExternalPointerTableEntry::GetExternalPointerTag() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  DCHECK(payload.ContainsPointer());
  return payload.ExtractTag();
}

Address ExternalPointerTableEntry::ExtractManagedResourceOrNull() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  ExternalPointerTag tag = payload.ExtractTag();
  if (IsManagedExternalPointerType(tag)) {
    return payload.Untag(tag);
  }
  return kNullAddress;
}

void ExternalPointerTableEntry::MakeZappedEntry() {
  Payload new_payload(kNullAddress, kExternalPointerZappedEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

void ExternalPointerTableEntry::MakeFreelistEntry(uint32_t next_entry_index) {
  // The next freelist entry is stored in the lower bits of the entry.
  static_assert(kMaxExternalPointers <= std::numeric_limits<uint32_t>::max());
  Payload new_payload(next_entry_index, kExternalPointerFreeEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

uint32_t ExternalPointerTableEntry::GetNextFreelistEntryIndex() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.ExtractFreelistLink();
}

void ExternalPointerTableEntry::Mark() {
  auto old_payload = payload_.load(std::memory_order_relaxed);
  DCHECK(old_payload.ContainsPointer());

  auto new_payload = old_payload;
  new_payload.SetMarkBit();

  // We don't need to perform the CAS in a loop: if the new value is not equal
  // to the old value, then the mutator must've just written a new value into
  // the entry. This in turn must've set the marking bit already (see e.g.
  // SetExternalPointer), so we don't need to do it again.
  bool success = payload_.compare_exchange_strong(old_payload, new_payload,
                                                  std::memory_order_relaxed);
  DCHECK(success || old_payload.HasMarkBitSet());
  USE(success);
}

void ExternalPointerTableEntry::MakeEvacuationEntry(Address handle_location) {
  Payload new_payload(handle_location, kExternalPointerEvacuationEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

bool ExternalPointerTableEntry::HasEvacuationEntry() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.ContainsEvacuationEntry();
}

void ExternalPointerTableEntry::Evacuate(ExternalPointerTableEntry& dest,
                                         EvacuateMarkMode mode) {
  auto payload = payload_.load(std::memory_order_relaxed);
  // We expect to only evacuate entries containing external pointers.
  DCHECK(payload.ContainsPointer());

  switch (mode) {
    case EvacuateMarkMode::kTransferMark:
      break;
    case EvacuateMarkMode::kLeaveUnmarked:
      DCHECK(!payload.HasMarkBitSet());
      break;
    case EvacuateMarkMode::kClearMark:
      DCHECK(payload.HasMarkBitSet());
      payload.ClearMarkBit();
      break;
  }

  dest.payload_.store(payload, std::memory_order_relaxed);
#if defined(LEAK_SANITIZER)
  dest.raw_pointer_for_lsan_ = raw_pointer_for_lsan_;
#endif  // LEAK_SANITIZER

  // The destination entry takes ownership of the pointer.
  MakeZappedEntry();
}

Address ExternalPointerTable::Get(ExternalPointerHandle handle,
                                  ExternalPointerTag tag) const {
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
  CHECK(index == 0 || at(index).HasExternalPointer(tag));
#else
  // Otherwise, this is just a DCHECK.
  DCHECK(index == 0 || at(index).HasExternalPointer(tag));
#endif
  return at(index).GetExternalPointer(tag);
}

void ExternalPointerTable::Set(ExternalPointerHandle handle, Address value,
                               ExternalPointerTag tag) {
  DCHECK_NE(kNullExternalPointerHandle, handle);
  uint32_t index = HandleToIndex(handle);
  // TODO(saelo): This works for now, but once we actually free the external
  // object here, this will probably become awkward: it's likely not intuitive
  // that a set_foo() call on some object causes another object to be freed.
  // Probably at that point we should instead just forbid re-setting the
  // external pointers if they are managed (via a DCHECK).
  FreeManagedResourceIfPresent(index);
  TakeOwnershipOfManagedResourceIfNecessary(value, handle, tag);
  at(index).SetExternalPointer(value, tag);
}

Address ExternalPointerTable::Exchange(ExternalPointerHandle handle,
                                       Address value, ExternalPointerTag tag) {
  DCHECK_NE(kNullExternalPointerHandle, handle);
  DCHECK(!IsManagedExternalPointerType(tag));
  uint32_t index = HandleToIndex(handle);
  return at(index).ExchangeExternalPointer(value, tag);
}

ExternalPointerTag ExternalPointerTable::GetTag(
    ExternalPointerHandle handle) const {
  uint32_t index = HandleToIndex(handle);
  return at(index).GetExternalPointerTag();
}

void ExternalPointerTable::Zap(ExternalPointerHandle handle) {
  // Zapping the null entry is a nop. This is useful as we reset the handle of
  // managed resources to the kNullExternalPointerHandle when the entry is
  // deleted. See SweepAndCompact.
  if (handle == kNullExternalPointerHandle) return;
  uint32_t index = HandleToIndex(handle);
  at(index).MakeZappedEntry();
}

ExternalPointerHandle ExternalPointerTable::AllocateAndInitializeEntry(
    Space* space, Address initial_value, ExternalPointerTag tag) {
  DCHECK(space->BelongsTo(this));
  uint32_t index = AllocateEntry(space);
  at(index).MakeExternalPointerEntry(initial_value, tag);
  ExternalPointerHandle handle = IndexToHandle(index);
  TakeOwnershipOfManagedResourceIfNecessary(initial_value, handle, tag);
  return handle;
}

void ExternalPointerTable::Mark(Space* space, ExternalPointerHandle handle,
                                Address handle_location) {
  DCHECK(space->BelongsTo(this));

  // The handle_location must always contain the given handle. Except if the
  // slot is lazily-initialized. In that case, the handle may transition from
  // the null handle to a valid handle. However, in that case the
  // newly-allocated entry will already have been marked as alive during
  // allocation, and so we don't need to do anything here.
#ifdef DEBUG
  ExternalPointerHandle current_handle = base::AsAtomic32::Acquire_Load(
      reinterpret_cast<ExternalPointerHandle*>(handle_location));
  DCHECK(handle == kNullExternalPointerHandle || handle == current_handle);
#endif

  // If the handle is null, it doesn't have an EPT entry; no mark is needed.
  if (handle == kNullExternalPointerHandle) return;

  uint32_t index = HandleToIndex(handle);
  DCHECK(space->Contains(index));

  // If the table is being compacted and the entry is inside the evacuation
  // area, then allocate and set up an evacuation entry for it.
  MaybeCreateEvacuationEntry(space, index, handle_location);

  // Even if the entry is marked for evacuation, it still needs to be marked as
  // alive as it may be visited during sweeping before being evacuation.
  at(index).Mark();
}

void ExternalPointerTable::Evacuate(Space* from_space, Space* to_space,
                                    ExternalPointerHandle handle,
                                    Address handle_location,
                                    EvacuateMarkMode mode) {
  DCHECK(from_space->BelongsTo(this));
  DCHECK(to_space->BelongsTo(this));

  auto handle_ptr = reinterpret_cast<ExternalPointerHandle*>(handle_location);

#ifdef DEBUG
  // Unlike Mark(), we require that the mutator is stopped, so we can simply
  // verify that the location stores the handle with a non-atomic load.
  DCHECK_EQ(handle, *handle_ptr);
#endif

  // If the handle is null, it doesn't have an EPT entry; no evacuation is
  // needed.
  if (handle == kNullExternalPointerHandle) return;

  uint32_t from_index = HandleToIndex(handle);
  DCHECK(from_space->Contains(from_index));
  uint32_t to_index = AllocateEntry(to_space);

  at(from_index).Evacuate(at(to_index), mode);
  ExternalPointerHandle new_handle = IndexToHandle(to_index);

  if (Address addr = at(to_index).ExtractManagedResourceOrNull()) {
    ManagedResource* resource = reinterpret_cast<ManagedResource*>(addr);
    DCHECK_EQ(resource->ept_entry_, handle);
    resource->ept_entry_ = new_handle;
  }

  // Update slot to point to new handle.
  base::AsAtomic32::Relaxed_Store(handle_ptr, new_handle);
}

// static
bool ExternalPointerTable::IsValidHandle(ExternalPointerHandle handle) {
  uint32_t index = handle >> kExternalPointerIndexShift;
  return handle == index << kExternalPointerIndexShift;
}

// static
uint32_t ExternalPointerTable::HandleToIndex(ExternalPointerHandle handle) {
  DCHECK(IsValidHandle(handle));
  uint32_t index = handle >> kExternalPointerIndexShift;
#if defined(LEAK_SANITIZER)
  // When LSan is active, we use "fat" entries that also store the raw pointer
  // to that LSan can find live references. However, we do this transparently:
  // we simply multiply the handle by two so that `(handle >> index_shift) * 8`
  // still produces the correct offset of the entry in the table. However, this
  // is not secure as an attacker could reference the raw pointer instead of
  // the encoded pointer in an entry, thereby bypassing the type checks. As
  // such, this mode must only be used in testing environments. Alternatively,
  // all places that access external pointer table entries must be made aware
  // that the entries are 16 bytes large when LSan is active.
  index /= 2;
#endif  // LEAK_SANITIZER
  DCHECK_LE(index, kMaxExternalPointers);
  return index;
}

// static
ExternalPointerHandle ExternalPointerTable::IndexToHandle(uint32_t index) {
  DCHECK_LE(index, kMaxExternalPointers);
  ExternalPointerHandle handle = index << kExternalPointerIndexShift;
#if defined(LEAK_SANITIZER)
  handle *= 2;
#endif  // LEAK_SANITIZER
  DCHECK_NE(handle, kNullExternalPointerHandle);
  return handle;
}

bool ExternalPointerTable::Contains(Space* space,
                                    ExternalPointerHandle handle) const {
  DCHECK(space->BelongsTo(this));
  return space->Contains(HandleToIndex(handle));
}

void ExternalPointerTable::Space::NotifyExternalPointerFieldInvalidated(
    Address field_address, ExternalPointerTag tag) {
  // We do not currently support invalidating fields containing managed
  // external pointers. If this is ever needed, we would probably need to free
  // the managed object here as we may otherwise fail to do so during sweeping.
  DCHECK(!IsManagedExternalPointerType(tag));
#ifdef DEBUG
  ExternalPointerHandle handle = base::AsAtomic32::Acquire_Load(
      reinterpret_cast<ExternalPointerHandle*>(field_address));
  DCHECK(Contains(HandleToIndex(handle)));
#endif
  AddInvalidatedField(field_address);
}

void ExternalPointerTable::ManagedResource::ZapExternalPointerTableEntry() {
  if (owning_table_) {
    owning_table_->Zap(ept_entry_);
  }
  ept_entry_ = kNullExternalPointerHandle;
}

void ExternalPointerTable::TakeOwnershipOfManagedResourceIfNecessary(
    Address value, ExternalPointerHandle handle, ExternalPointerTag tag) {
  if (IsManagedExternalPointerType(tag) && value != kNullAddress) {
    ManagedResource* resource = reinterpret_cast<ManagedResource*>(value);
    DCHECK_EQ(resource->ept_entry_, kNullExternalPointerHandle);
    resource->owning_table_ = this;
    resource->ept_entry_ = handle;
  }
}

void ExternalPointerTable::FreeManagedResourceIfPresent(uint32_t entry_index) {
  // In the future, this would be where we actually delete the external
  // resource. Currently, the deletion still happens elsewhere, and so here we
  // instead set the resource's handle to the null handle so that the resource
  // does not attempt to zap its entry when it is eventually destroyed.
  if (Address addr = at(entry_index).ExtractManagedResourceOrNull()) {
    ManagedResource* resource = reinterpret_cast<ManagedResource*>(addr);
    DCHECK_EQ(resource->ept_entry_, IndexToHandle(entry_index));
    resource->ept_entry_ = kNullExternalPointerHandle;
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_
