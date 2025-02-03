// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_BUFFER_TABLE_H_
#define V8_SANDBOX_EXTERNAL_BUFFER_TABLE_H_

#include "include/v8config.h"
#include "src/common/globals.h"
#include "src/sandbox/compactible-external-entity-table.h"
#include "src/sandbox/external-buffer-tag.h"
#include "src/sandbox/tagged-payload.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8 {
namespace internal {

class Counters;

/**
 * The entries of an ExternalBufferTable.
 *
 * Each entry consists of two pointer-sized words where the first word
 * contains the external pointer, the marking bit, and a type tag. The second
 * word contains the buffer size. An entry can either be:
 *  - A "regular" entry, containing the external pointer (with a type
 *    tag and the marking bit in the unused upper bits) and the buffer size, or
 *  - A freelist entry, tagged with the kExternalPointerFreeEntryTag and
 *    containing the index of the next free entry in the lower 32 bits of the
 *    first pointer-size word, or
 *  - An evacuation entry, tagged with the kExternalPointerEvacuationEntryTag
 *    and containing the address of the ExternalBufferSlot referencing the
 *    entry that will be evacuated into this entry. See the compaction
 *    algorithm overview for more details about these entries.
 */
struct ExternalBufferTableEntry {
  // Make this entry an external buffer entry containing the given pointer
  // tagged with the given tag and the given buffer size.
  inline void MakeExternalBufferEntry(std::pair<Address, size_t> buffer,
                                      ExternalBufferTag tag);

  // Load and untag the external buffer stored in this entry.
  // This entry must be an external buffer entry.
  // If the specified tag doesn't match the actual tag of this entry, the
  // resulting pointer will be invalid and cannot be dereferenced.
  inline std::pair<Address, size_t> GetExternalBuffer(
      ExternalBufferTag tag) const;

  // Returns true if this entry contains an external buffer with the given tag.
  inline bool HasExternalBuffer(ExternalBufferTag tag) const;

  // Make this entry a freelist entry, containing the index of the next entry
  // on the freelist.
  inline void MakeFreelistEntry(uint32_t next_entry_index);

  // Get the index of the next entry on the freelist. This method may be
  // called even when the entry is not a freelist entry. However, the result
  // is only valid if this is a freelist entry. This behaviour is required
  // for efficient entry allocation, see TryAllocateEntryFromFreelist.
  inline uint32_t GetNextFreelistEntryIndex() const;

  // Make this entry an evacuation entry containing the address of the handle to
  // the entry being evacuated.
  inline void MakeEvacuationEntry(Address handle_location);

  // Returns true if this entry contains an evacuation entry.
  inline bool HasEvacuationEntry() const;

  // Move the content of this entry into the provided entry.
  // Used during table compaction. This invalidates the entry.
  inline void MigrateInto(ExternalBufferTableEntry& other);

  // Mark this entry as alive during table garbage collection.
  inline void Mark();

  static constexpr bool IsWriteProtected = false;

 private:
  friend class ExternalBufferTable;

  struct ExternalBufferTaggingScheme {
    using TagType = ExternalBufferTag;
    static constexpr uint64_t kMarkBit = kExternalBufferMarkBit;
    static constexpr uint64_t kTagMask = kExternalBufferTagMask;
    static constexpr TagType kFreeEntryTag = kExternalBufferFreeEntryTag;
    static constexpr TagType kEvacuationEntryTag =
        kExternalBufferEvacuationEntryTag;
    static constexpr bool kSupportsEvacuation = true;
  };

  using Payload = TaggedPayload<ExternalBufferTaggingScheme>;

  inline Payload GetRawPayload() {
    return payload_.load(std::memory_order_relaxed);
  }
  inline void SetRawPayload(Payload new_payload) {
    return payload_.store(new_payload, std::memory_order_relaxed);
  }

  // ExternalBufferTable entries consist of two pointer-sized words where the
  // first word contains a tag and marking bit together with the actual content
  // (e.g. an external pointer) and the second word contains the buffer size.
  std::atomic<Payload> payload_;

  // The size is not part of the payload since the compiler fails to generate
  // 128-bit atomic operations on x86_64 platforms.
  std::atomic<size_t> size_;
};

//  We expect ExternalBufferTable entries to consist of two 64-bit word.
static_assert(sizeof(ExternalBufferTableEntry) == 16);

/**
 * A table storing pointer and size to buffer data located outside the sandbox.
 *
 * When the sandbox is enabled, the external buffer table (EBT) is used to
 * safely reference buffer data located outside of the sandbox. The EBT
 * guarantees that every access to the buffer data via an external pointer
 * either results in an invalid pointer or a valid pointer to a valid (live)
 * buffer of the expected type. The EBT also stores the size of the buffer data
 * as part of each entry to allow for bounds checking.
 *
 * Table memory management:
 * ------------------------
 * The garbage collection algorithm works as follows:
 *  - One bit of every entry is reserved for the marking bit.
 *  - Every store to an entry automatically sets the marking bit when ORing
 *    with the tag. This avoids the need for write barriers.
 *  - Every load of an entry automatically removes the marking bit when ANDing
 *    with the inverted tag.
 *  - When the GC marking visitor finds a live object with an external pointer,
 *    it marks the corresponding entry as alive through Mark(), which sets the
 *    marking bit using an atomic CAS operation.
 *  - When marking is finished, SweepAndCompact() iterates over a Space once
 *    while the mutator is stopped and builds a freelist from all dead entries
 *    while also removing the marking bit from any live entry.
 *
 * Table compaction:
 * -----------------
 * Additionally, the external buffer table supports compaction.
 * For details about the compaction algorithm see the
 * CompactibleExternalEntityTable class.
 */
class V8_EXPORT_PRIVATE ExternalBufferTable
    : public CompactibleExternalEntityTable<
          ExternalBufferTableEntry, kExternalBufferTableReservationSize> {
  using Base =
      CompactibleExternalEntityTable<ExternalBufferTableEntry,
                                     kExternalBufferTableReservationSize>;

 public:
  // Size of a ExternalBufferTable, for layout computation in IsolateData.
  static int constexpr kSize = 2 * kSystemPointerSize;
  static_assert(kMaxExternalBufferPointers == kMaxCapacity);

  ExternalBufferTable() = default;
  ExternalBufferTable(const ExternalBufferTable&) = delete;
  ExternalBufferTable& operator=(const ExternalBufferTable&) = delete;

  // The Spaces used by an ExternalBufferTable also contain the state related
  // to compaction.
  struct Space : public Base::Space {
   public:
    // During table compaction, we may record the addresses of fields
    // containing external pointer handles (if they are evacuation candidates).
    // As such, if such a field is invalidated (for example because the host
    // object is converted to another object type), we need to be notified of
    // that. Note that we do not need to care about "re-validated" fields here:
    // if an external pointer field is first converted to different kind of
    // field, then again converted to a external pointer field, then it will be
    // re-initialized, at which point it will obtain a new entry in the
    // external pointer table which cannot be a candidate for evacuation.
    inline void NotifyExternalPointerFieldInvalidated(Address field_address);
  };

  // Note: The table currently does not support a setter method since
  // we cannot guarantee atomicity of the method with the getter.

  // Retrieves the entry referenced by the given handle.
  inline std::pair<Address, size_t> Get(ExternalBufferHandle handle,
                                        ExternalBufferTag tag) const;

  // Allocates a new entry in the given space. The caller must provide the
  // initial value and tag for the entry.
  inline ExternalBufferHandle AllocateAndInitializeEntry(
      Space* space, std::pair<Address, size_t> initial_buffer,
      ExternalBufferTag tag);

  // Marks the specified entry as alive.
  //
  // If the space to which the entry belongs is currently being compacted, this
  // may also mark the entry for evacuation for which the location of the
  // handle is required. See the comments about the compaction algorithm for
  // more details.
  //
  // This method is atomic and can be called from background threads.
  inline void Mark(Space* space, ExternalBufferHandle handle,
                   Address handle_location);

  // Frees unmarked entries and finishes space compaction (if running).
  //
  // This method must only be called while mutator threads are stopped as it is
  // not safe to allocate table entries while the table is being swept.
  //
  // Returns the number of live entries after sweeping.
  uint32_t SweepAndCompact(Space* space, Counters* counters);

 private:
  static inline bool IsValidHandle(ExternalBufferHandle handle);
  static inline uint32_t HandleToIndex(ExternalBufferHandle handle);
  static inline ExternalBufferHandle IndexToHandle(uint32_t index);

  bool TryResolveEvacuationEntryDuringSweeping(
      uint32_t index, ExternalBufferHandle* handle_location,
      uint32_t start_of_evacuation_area);
};

static_assert(sizeof(ExternalBufferTable) == ExternalBufferTable::kSize);

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX

#endif  // V8_SANDBOX_EXTERNAL_BUFFER_TABLE_H_
