// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CPPHEAP_POINTER_TABLE_INL_H_
#define V8_SANDBOX_CPPHEAP_POINTER_TABLE_INL_H_

#include "src/sandbox/compactible-external-entity-table-inl.h"
#include "src/sandbox/cppheap-pointer-table.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

void CppHeapPointerTableEntry::MakePointerEntry(Address value,
                                                CppHeapPointerTag tag) {
  // Top bits must be zero, otherwise we'd loose information when shifting.
  DCHECK_EQ(0, value >> (kBitsPerSystemPointer - kCppHeapPointerPayloadShift));
  DCHECK_NE(tag, CppHeapPointerTag::kFreeEntryTag);
  DCHECK_NE(tag, CppHeapPointerTag::kEvacuationEntryTag);

  Payload new_payload(value, tag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

Address CppHeapPointerTableEntry::GetPointer(
    CppHeapPointerTagRange tag_range) const {
  auto payload = payload_.load(std::memory_order_relaxed);
  DCHECK(payload.ContainsPointer());
  return payload.Untag(tag_range);
}

void CppHeapPointerTableEntry::SetPointer(Address value,
                                          CppHeapPointerTag tag) {
  // Top bits must be zero, otherwise we'd loose information when shifting.
  DCHECK_EQ(0, value >> (kBitsPerSystemPointer - kCppHeapPointerPayloadShift));
  DCHECK_NE(tag, CppHeapPointerTag::kFreeEntryTag);
  DCHECK_NE(tag, CppHeapPointerTag::kEvacuationEntryTag);
  DCHECK(payload_.load(std::memory_order_relaxed).ContainsPointer());

  Payload new_payload(value, tag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

bool CppHeapPointerTableEntry::HasPointer(
    CppHeapPointerTagRange tag_range) const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.IsTaggedWithTagIn(tag_range);
}

void CppHeapPointerTableEntry::MakeZappedEntry() {
  Payload new_payload(kNullAddress, CppHeapPointerTag::kZappedEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

void CppHeapPointerTableEntry::MakeFreelistEntry(uint32_t next_entry_index) {
  static_assert(kMaxCppHeapPointers <= std::numeric_limits<uint32_t>::max());
  Payload new_payload(next_entry_index, CppHeapPointerTag::kFreeEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

uint32_t CppHeapPointerTableEntry::GetNextFreelistEntryIndex() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.ExtractFreelistLink();
}

void CppHeapPointerTableEntry::Mark() {
  auto old_payload = payload_.load(std::memory_order_relaxed);
  DCHECK(old_payload.ContainsPointer());

  auto new_payload = old_payload;
  new_payload.SetMarkBit();

  // We don't need to perform the CAS in a loop: if the new value is not equal
  // to the old value, then the mutator must've just written a new value into
  // the entry. This in turn must've set the marking bit already (see e.g.
  // SetPointer()), so we don't need to do it again.
  bool success = payload_.compare_exchange_strong(old_payload, new_payload,
                                                  std::memory_order_relaxed);
  DCHECK(success || old_payload.HasMarkBitSet());
  USE(success);
}

void CppHeapPointerTableEntry::MakeEvacuationEntry(Address handle_location) {
  Payload new_payload(handle_location, CppHeapPointerTag::kEvacuationEntryTag);
  payload_.store(new_payload, std::memory_order_relaxed);
}

bool CppHeapPointerTableEntry::HasEvacuationEntry() const {
  auto payload = payload_.load(std::memory_order_relaxed);
  return payload.ContainsEvacuationEntry();
}

void CppHeapPointerTableEntry::Evacuate(CppHeapPointerTableEntry& dest) {
  auto payload = payload_.load(std::memory_order_relaxed);
  // We expect to only evacuate entries containing external pointers.
  DCHECK(payload.ContainsPointer());
  // Currently, evacuation only happens during table compaction. In that case,
  // the marking bit must be unset as the entry has already been visited by the
  // sweeper (which clears the marking bit). If this ever changes, we'll need
  // to let the caller specify what to do with the marking bit during
  // evacuation.
  DCHECK(!payload.HasMarkBitSet());

  dest.payload_.store(payload, std::memory_order_relaxed);

  // The destination entry takes ownership of the pointer.
  MakeZappedEntry();
}

Address CppHeapPointerTable::Get(CppHeapPointerHandle handle,
                                 CppHeapPointerTagRange tag_range) const {
  uint32_t index = HandleToIndex(handle);
  DCHECK(index == 0 || at(index).HasPointer(tag_range));
  return at(index).GetPointer(tag_range);
}

void CppHeapPointerTable::Set(CppHeapPointerHandle handle, Address value,
                              CppHeapPointerTag tag) {
  DCHECK_NE(kNullCppHeapPointerHandle, handle);
  uint32_t index = HandleToIndex(handle);
  at(index).SetPointer(value, tag);
}

CppHeapPointerHandle CppHeapPointerTable::AllocateAndInitializeEntry(
    Space* space, Address initial_value, CppHeapPointerTag tag) {
  DCHECK(space->BelongsTo(this));
  uint32_t index = AllocateEntry(space);
  at(index).MakePointerEntry(initial_value, tag);

  CppHeapPointerHandle handle = IndexToHandle(index);

  return handle;
}

void CppHeapPointerTable::Mark(Space* space, CppHeapPointerHandle handle,
                               Address handle_location) {
  DCHECK(space->BelongsTo(this));

  // The handle_location must always contain the given handle. Except if the
  // slot is lazily-initialized. In that case, the handle may transition from
  // the null handle to a valid handle. However, in that case the
  // newly-allocated entry will already have been marked as alive during
  // allocation, and so we don't need to do anything here.
#ifdef DEBUG
  CppHeapPointerHandle current_handle = base::AsAtomic32::Acquire_Load(
      reinterpret_cast<CppHeapPointerHandle*>(handle_location));
  DCHECK(handle == kNullCppHeapPointerHandle || handle == current_handle);
#endif

  // If the handle is null, it doesn't have an EPT entry; no mark is needed.
  if (handle == kNullCppHeapPointerHandle) return;

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
bool CppHeapPointerTable::IsValidHandle(CppHeapPointerHandle handle) {
  uint32_t index = handle >> kCppHeapPointerIndexShift;
  return handle == index << kCppHeapPointerIndexShift;
}

// static
uint32_t CppHeapPointerTable::HandleToIndex(CppHeapPointerHandle handle) {
  DCHECK(IsValidHandle(handle));
  uint32_t index = handle >> kCppHeapPointerIndexShift;
  DCHECK_LE(index, kMaxCppHeapPointers);
  return index;
}

// static
CppHeapPointerHandle CppHeapPointerTable::IndexToHandle(uint32_t index) {
  DCHECK_LE(index, kMaxCppHeapPointers);
  CppHeapPointerHandle handle = index << kCppHeapPointerIndexShift;
  DCHECK_NE(handle, kNullCppHeapPointerHandle);
  return handle;
}

bool CppHeapPointerTable::Contains(Space* space,
                                   CppHeapPointerHandle handle) const {
  DCHECK(space->BelongsTo(this));
  return space->Contains(HandleToIndex(handle));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_CPPHEAP_POINTER_TABLE_INL_H_
