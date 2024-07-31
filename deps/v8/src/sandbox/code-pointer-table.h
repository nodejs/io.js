// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CODE_POINTER_TABLE_H_
#define V8_SANDBOX_CODE_POINTER_TABLE_H_

#include "include/v8config.h"
#include "src/base/atomicops.h"
#include "src/base/memory.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/sandbox/code-entrypoint-tag.h"
#include "src/sandbox/external-entity-table.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

class Isolate;
class Counters;

/**
 * The entries of a CodePointerTable.
 *
 * Each entry contains a pointer to a Code object as well as a raw pointer to
 * the Code's entrypoint.
 */
struct CodePointerTableEntry {
  // Make this entry a code pointer entry for the given code object and
  // entrypoint.
  inline void MakeCodePointerEntry(Address code, Address entrypoint,
                                   CodeEntrypointTag tag, bool mark_as_alive);

  // Make this entry a freelist entry, containing the index of the next entry
  // on the freelist.
  inline void MakeFreelistEntry(uint32_t next_entry_index);

  // Load code entrypoint pointer stored in this entry.
  // This entry must be a code pointer entry.
  inline Address GetEntrypoint(CodeEntrypointTag tag) const;

  // Store the given code entrypoint pointer in this entry.
  // This entry must be a code pointer entry.
  inline void SetEntrypoint(Address value, CodeEntrypointTag tag);

  // Load the code object pointer stored in this entry.
  // This entry must be a code pointer entry.
  inline Address GetCodeObject() const;

  // Store the given code object pointer in this entry.
  // This entry must be a code pointer entry.
  inline void SetCodeObject(Address value);

  // Returns true if this entry is a freelist entry.
  inline bool IsFreelistEntry() const;

  // Get the index of the next entry on the freelist. This method may be
  // called even when the entry is not a freelist entry. However, the result
  // is only valid if this is a freelist entry. This behaviour is required
  // for efficient entry allocation, see TryAllocateEntryFromFreelist.
  inline uint32_t GetNextFreelistEntryIndex() const;

  // Mark this entry as alive during garbage collection.
  inline void Mark();

  // Unmark this entry during sweeping.
  inline void Unmark();

  // Test whether this entry is currently marked as alive.
  inline bool IsMarked() const;

 private:
  friend class CodePointerTable;

  // Freelist entries contain the index of the next free entry in their lower 32
  // bits and are tagged with the kFreeCodePointerTableEntryTag.
  static constexpr Address kFreeEntryTag = kFreeCodePointerTableEntryTag;

  // The marking bit is stored in the code_ field, see below.
  static constexpr Address kMarkingBit = 1;

  std::atomic<Address> entrypoint_;
  // The pointer to the Code object also contains the marking bit: since this is
  // a tagged pointer to a V8 HeapObject, we know that it will be 4-byte aligned
  // and that the LSB should always be set. We therefore use the LSB as marking
  // bit. In this way:
  //  - When loading the pointer, we only need to perform an unconditional OR 1
  //  to get the correctly tagged pointer
  //  - When storing the pointer we don't need to do anything since the tagged
  //  pointer will automatically be marked
  std::atomic<Address> code_;
};

static_assert(sizeof(CodePointerTableEntry) == kCodePointerTableEntrySize);

/**
 * A table containing pointers to Code.
 *
 * Essentially a specialized version of the trusted pointer table (TPT). A
 * code pointer table entry contains both a pointer to a Code object as well as
 * a pointer to the entrypoint. This way, the performance sensitive code paths
 * that for example call a JSFunction can directly load the entrypoint from the
 * table without having to load it from the Code object.
 *
 * When the sandbox is enabled, a code pointer table (CPT) is used to ensure
 * basic control-flow integrity in the absence of special hardware support
 * (such as landing pad instructions): by referencing code through an index
 * into a CPT, and ensuring that only valid code entrypoints are stored inside
 * the table, it is then guaranteed that any indirect control-flow transfer
 * ends up on a valid entrypoint as long as an attacker is still confined to
 * the sandbox.
 */
class V8_EXPORT_PRIVATE CodePointerTable
    : public ExternalEntityTable<CodePointerTableEntry,
                                 kCodePointerTableReservationSize> {
 public:
  // Size of a CodePointerTable, for layout computation in IsolateData.
  static int constexpr kSize = 2 * kSystemPointerSize;
  static_assert(kMaxCodePointers == kMaxCapacity);

  CodePointerTable() = default;
  CodePointerTable(const CodePointerTable&) = delete;
  CodePointerTable& operator=(const CodePointerTable&) = delete;

  // The Spaces used by a CodePointerTable.
  using Space = ExternalEntityTable<
      CodePointerTableEntry,
      kCodePointerTableReservationSize>::SpaceWithBlackAllocationSupport;

  //
  // This method is atomic and can be called from background threads.
  inline Address GetEntrypoint(CodePointerHandle handle,
                               CodeEntrypointTag tag) const;

  // Retrieves the code object of the entry referenced by the given handle.
  //
  // This method is atomic and can be called from background threads.
  inline Address GetCodeObject(CodePointerHandle handle) const;

  // Sets the entrypoint of the entry referenced by the given handle.
  //
  // This method is atomic and can be called from background threads.
  inline void SetEntrypoint(CodePointerHandle handle, Address value,
                            CodeEntrypointTag tag);

  // Sets the code object of the entry referenced by the given handle.
  //
  // This method is atomic and can be called from background threads.
  inline void SetCodeObject(CodePointerHandle handle, Address value);

  // Allocates a new entry in the table and initialize it.
  //
  // This method is atomic and can be called from background threads.
  inline CodePointerHandle AllocateAndInitializeEntry(Space* space,
                                                      Address code,
                                                      Address entrypoint,
                                                      CodeEntrypointTag tag);

  // Marks the specified entry as alive.
  //
  // This method is atomic and can be called from background threads.
  inline void Mark(Space* space, CodePointerHandle handle);

  // Frees all unmarked entries in the given space.
  //
  // This method must only be called while mutator threads are stopped as it is
  // not safe to allocate table entries while a space is being swept.
  //
  // Returns the number of live entries after sweeping.
  uint32_t Sweep(Space* space, Counters* counters);

  // Iterate over all active entries in the given space.
  //
  // The callback function will be invoked once for every entry that is
  // currently in use, i.e. has been allocated and not yet freed, and will
  // receive the handle and content (Code object pointer) of that entry.
  template <typename Callback>
  void IterateActiveEntriesIn(Space* space, Callback callback);

  // The base address of this table, for use in JIT compilers.
  Address base_address() const { return base(); }

 private:
  inline uint32_t HandleToIndex(CodePointerHandle handle) const;
  inline CodePointerHandle IndexToHandle(uint32_t index) const;
};

static_assert(sizeof(CodePointerTable) == CodePointerTable::kSize);

V8_EXPORT_PRIVATE CodePointerTable* GetProcessWideCodePointerTable();

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_CODE_POINTER_TABLE_H_
