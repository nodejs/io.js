// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_CODE_POINTER_TABLE_H_
#define V8_WASM_WASM_CODE_POINTER_TABLE_H_

#include "include/v8-internal.h"
#include "src/common/segmented-table.h"

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

namespace v8::internal::wasm {

// Defines the entries in the WasmCodePointerTable and specifies the encoding.
// When entries are in use, they contain the address of a valid Wasm code entry,
// while free entries contain an index to the next element in the freelist.
//
// All reads and writes use relaxed memory ordering and need to be synchronized
// by the caller.
struct WasmCodePointerTableEntry {
  // We write-protect the WasmCodePointerTable on platforms that support it for
  // forward-edge CFI.
  static constexpr bool IsWriteProtected = true;

  // Set the entry to point to a given entrypoint.
  inline void MakeCodePointerEntry(Address entrypoint, uint64_t signature_hash);
  inline void UpdateCodePointerEntry(Address entrypoint,
                                     uint64_t signature_hash);

  // Make this entry a freelist entry, containing the index of the next entry
  // on the freelist.
  inline void MakeFreelistEntry(uint32_t next_entry_index);

  // Load code entrypoint pointer stored in this entry.
  inline Address GetEntrypoint(uint64_t signature_hash) const;

  // Load code entrypoint pointer stored in this entry.
  inline Address GetEntrypointWithoutSignatureCheck() const;

  // Get the index of the next entry on the freelist.
  inline uint32_t GetNextFreelistEntryIndex() const;

 private:
  friend class WasmCodePointerTable;

  std::atomic<Address> entrypoint_;
#ifdef V8_ENABLE_SANDBOX
  uint64_t signature_hash_;
#endif
};

// A table for storing valid Wasm code entrypoints. This table allows enforcing
// forward-edge CFI for Wasm calls:
// * The table gets write-protected (e.g. with pkeys) to prevent corruption of
//   entries.
// * At write time, we will check that the value is a valid entrypoint as
//   tracked in our CFI metadata.
// * Wasm calls can then be replaced with a bounds-checked table lookup + call
//   to enforce that only valid entrypoints can be called.
//
// All methods are thread-safe if not specified otherwise.
class V8_EXPORT_PRIVATE WasmCodePointerTable
    : public SegmentedTable<WasmCodePointerTableEntry,
                            kCodePointerTableReservationSize> {
  using Base = SegmentedTable<WasmCodePointerTableEntry,
                              kCodePointerTableReservationSize>;

 public:
  WasmCodePointerTable() = default;
  WasmCodePointerTable(const WasmCodePointerTable&) = delete;
  WasmCodePointerTable& operator=(const WasmCodePointerTable&) = delete;

  using Handle = uint32_t;
  static constexpr Handle kInvalidHandle = -1;
#ifdef V8_ENABLE_SANDBOX
  static constexpr int kOffsetOfSignatureHash =
      offsetof(WasmCodePointerTableEntry, signature_hash_);
#endif

#ifdef V8_TARGET_ARCH_64_BIT
  // 64-bit architectures reserve a large table upfront, hence there's a fixed
  // maximum number of Wasm code pointers.
  static constexpr size_t kMaxWasmCodePointers = kMaxCapacity;
#endif

  using WriteScope = CFIMetadataWriteScope;

  // The table should be initialized exactly once before use.
  void Initialize();

  // Free any resources used by the table.
  void TearDown();

  // Read the entrypoint at a given index.
  inline Address GetEntrypoint(WasmCodePointer index,
                               uint64_t signature_hash) const;

  inline Address GetEntrypointWithoutSignatureCheck(
      WasmCodePointer index) const;

  // Sets the entrypoint of the entry referenced by the given index.
  // The Unlocked version can be used in loops, but you need to hold a
  // `WriteScope` while calling it.
  inline void UpdateEntrypoint(WasmCodePointer index, Address value,
                               uint64_t signature_hash);
  inline void SetEntrypointAndSignature(WasmCodePointer index, Address value,
                                        uint64_t signature_hash);
  inline void SetEntrypointWithWriteScope(WasmCodePointer index, Address value,
                                          uint64_t signature_hash,
                                          WriteScope& write_scope);

  // Allocates a new entry in the table and optionally initialize it.
  inline WasmCodePointer AllocateAndInitializeEntry(Address entrypoint,
                                                    uint64_t signature_hash);
  inline WasmCodePointer AllocateUninitializedEntry();

  // Free an entry, which will add it to the free list.
  inline void FreeEntry(WasmCodePointer index);

  // Iterate through the freelist to find and unmap empty segments. Will return
  // early if there's less than `threshold` many elements in the freelist.
  void SweepSegments(size_t threshold = 2 * kEntriesPerSegment);

  // Add an entry for a native function address, used by the C API.
  WasmCodePointer GetOrCreateHandleForNativeFunction(Address addr);

  // Compare the address of the entry.
  bool EntrypointEqualTo(WasmCodePointer index, Address address);

 private:
  // Allow the ExternalReference to access the table base.
  friend class ::v8::internal::ExternalReference;

  // This marker is used to temporarily unlink the freelist to get exclusive
  // access.
  static constexpr FreelistHead kRetryMarker = FreelistHead(0xffffffff, 0);
  static bool IsRetryMarker(FreelistHead freelist) {
    return freelist.length() == kRetryMarker.length() &&
           freelist.next() == kRetryMarker.next();
  }

  // Access the Freelist head, retrying if the retry marker is seen.
  V8_INLINE FreelistHead ReadFreelistHead();

  // Allocate an entry either from the freelist or creating a new segment.
  uint32_t AllocateEntryImpl();

  // Atomically link a freelist into the current freelist head.
  V8_INLINE FreelistHead LinkFreelist(FreelistHead new_freelist,
                                      uint32_t last_element);

  // Helper functions for converting a freelist to a vector and back.
  // Freelist access is not atomic.
  std::vector<uint32_t> FreelistToVector(FreelistHead freelist);
  FreelistHead VectorToFreelist(std::vector<uint32_t> entries);

  // Try to allocate the first entry of the freelist.
  //
  // This method is mostly a wrapper around an atomic compare-and-swap which
  // replaces the current freelist head with the next entry in the freelist,
  // thereby allocating the entry at the start of the freelist.
  V8_INLINE bool TryAllocateFromFreelist(uint32_t* index);

  // Not atomic and should only be used if you have exclusive access to the
  // freelist.
  V8_INLINE uint32_t
  AllocateEntryFromFreelistNonAtomic(FreelistHead* freelist_head);

  // Free all handles in the `native_function_map_`.
  void FreeNativeFunctionHandles();

  std::atomic<FreelistHead> freelist_head_ = FreelistHead();
  // The mutex is used to avoid two threads from concurrently allocating
  // segments and using more memory than needed.
  base::SpinningMutex segment_allocation_mutex_;

  base::SpinningMutex native_function_map_mutex_;
  std::map<Address, WasmCodePointer> native_function_map_;

  friend class WasmCodePointerTableTest;
};

V8_EXPORT_PRIVATE WasmCodePointerTable* GetProcessWideWasmCodePointerTable();

}  // namespace v8::internal::wasm

#endif  // V8_WASM_WASM_CODE_POINTER_TABLE_H_
