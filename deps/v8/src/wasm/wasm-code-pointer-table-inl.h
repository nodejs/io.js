// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_CODE_POINTER_TABLE_INL_H_
#define V8_WASM_WASM_CODE_POINTER_TABLE_INL_H_

#include "src/common/code-memory-access-inl.h"
#include "src/common/segmented-table-inl.h"
#include "src/wasm/wasm-code-pointer-table.h"

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

namespace v8::internal::wasm {

void WasmCodePointerTableEntry::MakeCodePointerEntry(Address entrypoint) {
  entrypoint_.store(entrypoint, std::memory_order_relaxed);
}

Address WasmCodePointerTableEntry::GetEntrypoint() const {
  return entrypoint_.load(std::memory_order_relaxed);
}

void WasmCodePointerTableEntry::MakeFreelistEntry(uint32_t next_entry_index) {
  entrypoint_.store(next_entry_index, std::memory_order_relaxed);
}

uint32_t WasmCodePointerTableEntry::GetNextFreelistEntryIndex() const {
  return static_cast<uint32_t>(entrypoint_.load(std::memory_order_relaxed));
}

Address WasmCodePointerTable::GetEntrypoint(uint32_t index) const {
  return at(index).GetEntrypoint();
}

void WasmCodePointerTable::SetEntrypoint(uint32_t index, Address value) {
  WriteScope write_scope("WasmCodePointerTable write");
  SetEntrypointWithWriteScope(index, value, write_scope);
}

void WasmCodePointerTable::SetEntrypointWithWriteScope(
    uint32_t index, Address value, WriteScope& write_scope) {
  at(index).MakeCodePointerEntry(value);
}

uint32_t WasmCodePointerTable::AllocateAndInitializeEntry(Address entrypoint) {
  uint32_t index = AllocateUninitializedEntry();
  WriteScope write_scope("WasmCodePointerTable write");
  at(index).MakeCodePointerEntry(entrypoint);
  return index;
}

WasmCodePointerTable::FreelistHead WasmCodePointerTable::ReadFreelistHead() {
  while (true) {
    FreelistHead freelist = freelist_head_.load(std::memory_order_acquire);
    if (IsRetryMarker(freelist)) {
      // The retry marker will only be stored for a short amount of time. We can
      // check for it in a busy loop.
      continue;
    }
    return freelist;
  }
}

uint32_t WasmCodePointerTable::AllocateUninitializedEntry() {
  DCHECK(is_initialized());

  while (true) {
    // Fast path, try to take an entry from the freelist.
    uint32_t allocated_entry;
    if (TryAllocateFromFreelist(&allocated_entry)) {
      return allocated_entry;
    }

    // This is essentially DCLP (see
    // https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11/)
    // and so requires an acquire load as well as a release store in
    // AllocateTableSegment() to prevent reordering of memory accesses, which
    // could for example cause one thread to read a freelist entry before it
    // has been properly initialized.

    // The freelist is empty. We take a lock to avoid another thread from
    // allocating a new segment in the meantime. However, the freelist can
    // still grow if another thread frees an entry, so we'll merge the
    // freelists atomically in the end.
    base::MutexGuard guard(&segment_allocation_mutex_);

    // Reload freelist head in case another thread already grew the table.
    if (!freelist_head_.load(std::memory_order_relaxed).is_empty()) {
      // Something changed, retry.
      continue;
    }

    // Freelist is (still) empty so extend this space by another segment.
    auto [segment, freelist] = AllocateAndInitializeSegment();

    // Take out the first entry before we link it to the freelist_head.
    allocated_entry = AllocateEntryFromFreelistNonAtomic(&freelist);

    // Merge the new freelist entries into our freelist.
    LinkFreelist(freelist, segment.last_entry());

    return allocated_entry;
  }
}

bool WasmCodePointerTable::TryAllocateFromFreelist(uint32_t* index) {
  while (true) {
    FreelistHead current_freelist_head = ReadFreelistHead();
    if (current_freelist_head.is_empty()) {
      return false;
    }

    // Temporarily replace the freelist head with a marker to gain exclusive
    // access to it. This avoids a race condition where another thread could
    // unmap the memory while we're trying to read from it.
    if (!freelist_head_.compare_exchange_strong(current_freelist_head,
                                                kRetryMarker)) {
      continue;
    }

    uint32_t next_freelist_entry =
        at(current_freelist_head.next()).GetNextFreelistEntryIndex();
    FreelistHead new_freelist_head(next_freelist_entry,
                                   current_freelist_head.length() - 1);

    // We are allowed to overwrite the freelist_head_ since we stored the
    // kRetryMarker in there.
    freelist_head_.store(new_freelist_head, std::memory_order_relaxed);

    *index = current_freelist_head.next();

    return true;
  }
}

uint32_t WasmCodePointerTable::AllocateEntryFromFreelistNonAtomic(
    FreelistHead* freelist_head) {
  DCHECK(!freelist_head->is_empty());
  uint32_t index = freelist_head->next();
  uint32_t next_next = at(freelist_head->next()).GetNextFreelistEntryIndex();
  *freelist_head = FreelistHead(next_next, freelist_head->length() - 1);
  return index;
}

void WasmCodePointerTable::FreeEntry(uint32_t entry) {
  // TODO(sroettger): adding to the inline freelist requires a WriteScope. We
  // could keep a second fixed size out-of-line freelist to avoid frequent
  // permission changes here.
  LinkFreelist(FreelistHead(entry, 1), entry);
}

WasmCodePointerTable::FreelistHead WasmCodePointerTable::LinkFreelist(
    FreelistHead freelist_to_link, uint32_t last_element) {
  DCHECK(!freelist_to_link.is_empty());

  FreelistHead current_head, new_head;
  do {
    current_head = ReadFreelistHead();
    new_head = FreelistHead(freelist_to_link.next(),
                            freelist_to_link.length() + current_head.length());

    WriteScope write_scope("write free list entry");
    at(last_element).MakeFreelistEntry(current_head.next());
    // This must be a release store since we previously wrote the freelist
    // entries in AllocateTableSegment() and we need to prevent the writes from
    // being reordered past this store. See AllocateEntry() for more details.
  } while (!freelist_head_.compare_exchange_strong(current_head, new_head,
                                                   std::memory_order_release));

  return new_head;
}

}  // namespace v8::internal::wasm

#endif  // V8_WASM_WASM_CODE_POINTER_TABLE_INL_H_
