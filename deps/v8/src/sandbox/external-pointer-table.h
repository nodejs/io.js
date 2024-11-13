// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_POINTER_TABLE_H_
#define V8_SANDBOX_EXTERNAL_POINTER_TABLE_H_

#include "include/v8config.h"
#include "src/base/atomicops.h"
#include "src/base/memory.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/sandbox/compactible-external-entity-table.h"
#include "src/sandbox/tagged-payload.h"
#include "src/utils/allocation.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

class Isolate;
class Counters;
class ReadOnlyArtifacts;

/**
 * The entries of an ExternalPointerTable.
 *
 * Each entry consists of a single pointer-sized word containing the external
 * pointer, the marking bit, and a type tag. An entry can either be:
 *  - A "regular" entry, containing the external pointer together with a type
 *    tag and the marking bit in the unused upper bits, or
 *  - A freelist entry, tagged with the kExternalPointerFreeEntryTag and
 *    containing the index of the next free entry in the lower 32 bits, or
 *  - An evacuation entry, tagged with the kExternalPointerEvacuationEntryTag
 *    and containing the address of the ExternalPointerSlot referencing the
 *    entry that will be evacuated into this entry. See the compaction
 *    algorithm overview for more details about these entries.
 */
struct ExternalPointerTableEntry {
  enum class EvacuateMarkMode { kTransferMark, kLeaveUnmarked, kClearMark };

  // Make this entry an external pointer entry containing the given pointer
  // tagged with the given tag.
  inline void MakeExternalPointerEntry(Address value, ExternalPointerTag tag);

  // Load and untag the external pointer stored in this entry.
  // This entry must be an external pointer entry.
  // If the specified tag doesn't match the actual tag of this entry, the
  // resulting pointer will be invalid and cannot be dereferenced.
  inline Address GetExternalPointer(ExternalPointerTag tag) const;

  // Tag and store the given external pointer in this entry.
  // This entry must be an external pointer entry.
  inline void SetExternalPointer(Address value, ExternalPointerTag tag);

  // Returns true if this entry contains an external pointer with the given tag.
  inline bool HasExternalPointer(ExternalPointerTag tag) const;

  // Exchanges the external pointer stored in this entry with the provided one.
  // Returns the old external pointer. This entry must be an external pointer
  // entry. If the provided tag doesn't match the tag of the old entry, the
  // returned pointer will be invalid.
  inline Address ExchangeExternalPointer(Address value, ExternalPointerTag tag);

  // Load the tag of the external pointer stored in this entry.
  // This entry must be an external pointer entry.
  inline ExternalPointerTag GetExternalPointerTag() const;

  // Returns the address of the managed resource contained in this entry or
  // nullptr if this entry does not reference a managed resource.
  inline Address ExtractManagedResourceOrNull() const;

  // Invalidate the entry. Any access to a zapped entry will result in an
  // invalid pointer that will crash upon dereference.
  inline void MakeZappedEntry();

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

  // Move the content of this entry into the provided entry, possibly clearing
  // the marking bit. Used during table compaction and during promotion.
  // Invalidates the source entry.
  inline void Evacuate(ExternalPointerTableEntry& dest, EvacuateMarkMode mode);

  // Mark this entry as alive during table garbage collection.
  inline void Mark();

  static constexpr bool IsWriteProtected = false;

 private:
  friend class ExternalPointerTable;

  struct ExternalPointerTaggingScheme {
    using TagType = ExternalPointerTag;
    static constexpr uint64_t kMarkBit = kExternalPointerMarkBit;
    static constexpr uint64_t kTagMask = kExternalPointerTagMask;
    static constexpr TagType kFreeEntryTag = kExternalPointerFreeEntryTag;
    static constexpr TagType kEvacuationEntryTag =
        kExternalPointerEvacuationEntryTag;
    static constexpr bool kSupportsEvacuation = true;
  };

  using Payload = TaggedPayload<ExternalPointerTaggingScheme>;

  inline Payload GetRawPayload() {
    return payload_.load(std::memory_order_relaxed);
  }
  inline void SetRawPayload(Payload new_payload) {
    return payload_.store(new_payload, std::memory_order_relaxed);
  }

  inline void MaybeUpdateRawPointerForLSan(Address value) {
#if defined(LEAK_SANITIZER)
    raw_pointer_for_lsan_ = value;
#endif  // LEAK_SANITIZER
  }

  // ExternalPointerTable entries consist of a single pointer-sized word
  // containing a tag and marking bit together with the actual content (e.g. an
  // external pointer).
  std::atomic<Payload> payload_;

#if defined(LEAK_SANITIZER)
  //  When LSan is active, it must be able to detect live references to heap
  //  allocations from an external pointer table. It will, however, not be able
  //  to recognize the encoded pointers as they will have their top bits set. So
  //  instead, when LSan is active we use "fat" entries where the 2nd atomic
  //  words contains the unencoded raw pointer which LSan will be able to
  //  recognize as such.
  //  NOTE: THIS MODE IS NOT SECURE! Attackers are able to modify an
  //  ExternalPointerHandle to point to the raw pointer part, not the encoded
  //  part of an entry, thereby bypassing the type checks. If this mode is ever
  //  needed outside of testing environments, then the external pointer
  //  accessors (e.g. in the JIT) need to be made aware that entries are now 16
  //  bytes large so that all entry accesses are again guaranteed to access an
  //  encoded pointer.
  Address raw_pointer_for_lsan_;
#endif  // LEAK_SANITIZER
};

#if defined(LEAK_SANITIZER)
//  When LSan is active, we need "fat" entries, see above.
static_assert(sizeof(ExternalPointerTableEntry) == 16);
#else
//  We expect ExternalPointerTable entries to consist of a single 64-bit word.
static_assert(sizeof(ExternalPointerTableEntry) == 8);
#endif

/**
 * A table storing pointers to objects outside the V8 heap.
 *
 * When V8_ENABLE_SANDBOX, its primary use is for pointing to objects outside
 * the sandbox, as described below.
 * When V8_COMPRESS_POINTERS, external pointer tables are also used to ease
 * alignment requirements in heap object fields via indirection.
 *
 * A table's role for the V8 Sandbox:
 * --------------------------------
 * An external pointer table provides the basic mechanisms to ensure
 * memory-safe access to objects located outside the sandbox, but referenced
 * from within it. When an external pointer table is used, objects located
 * inside the sandbox reference outside objects through indices into the table.
 *
 * Type safety can be ensured by using type-specific tags for the external
 * pointers. These tags will be ORed into the unused top bits of the pointer
 * when storing them and will be ANDed away when loading the pointer later
 * again. If a pointer of the wrong type is accessed, some of the top bits will
 * remain in place, rendering the pointer inaccessible.
 *
 * Temporal memory safety is achieved through garbage collection of the table,
 * which ensures that every entry is either an invalid pointer or a valid
 * pointer pointing to a live object.
 *
 * Spatial memory safety can, if necessary, be ensured either by storing the
 * size of the referenced object together with the object itself outside the
 * sandbox, or by storing both the pointer and the size in one (double-width)
 * table entry.
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
 *    while also possibly clearing the marking bit from any live entry.
 *
 * Generational collection for tables:
 * -----------------------------------
 * Young-generation objects with external pointer slots allocate their
 * ExternalPointerTable entries in a spatially partitioned young external
 * pointer space.  There are two different mechanisms:
 *  - When using the semi-space nursery, promoting an object evacuates its EPT
 *    entries to the old external pointer space.
 *  - For the in-place MinorMS nursery, possibly-concurrent marking populates
 *    the SURVIVOR_TO_EXTERNAL_POINTER remembered sets.  In the pause, promoted
 *    objects use this remembered set to evacuate their EPT entries to the old
 *    external pointer space.  Survivors have their EPT entries are left in
 *    place.
 * In a full collection, segments from the young EPT space are eagerly promoted
 * during the pause, leaving the young generation empty.
 *
 * Table compaction:
 * -----------------
 * Additionally, the external pointer table supports compaction.
 * For details about the compaction algorithm see the
 * CompactibleExternalEntityTable class.
 */
class V8_EXPORT_PRIVATE ExternalPointerTable
    : public CompactibleExternalEntityTable<
          ExternalPointerTableEntry, kExternalPointerTableReservationSize> {
  using Base =
      CompactibleExternalEntityTable<ExternalPointerTableEntry,
                                     kExternalPointerTableReservationSize>;

#if defined(LEAK_SANITIZER)
  //  When LSan is active, we use "fat" entries, see above.
  static_assert(kMaxExternalPointers == kMaxCapacity * 2);
#else
  static_assert(kMaxExternalPointers == kMaxCapacity);
#endif

 public:
  using EvacuateMarkMode = ExternalPointerTableEntry::EvacuateMarkMode;

  // Size of an ExternalPointerTable, for layout computation in IsolateData.
  static int constexpr kSize = 2 * kSystemPointerSize;

  ExternalPointerTable() = default;
  ExternalPointerTable(const ExternalPointerTable&) = delete;
  ExternalPointerTable& operator=(const ExternalPointerTable&) = delete;

  // The Spaces used by an ExternalPointerTable.
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
    inline void NotifyExternalPointerFieldInvalidated(Address field_address,
                                                      ExternalPointerTag tag);

    // Not atomic.  Mutators and concurrent marking must be paused.
    void AssertEmpty() { CHECK(segments_.empty()); }
  };

  // Initializes all slots in the RO space from pre-existing artifacts.
  void SetUpFromReadOnlyArtifacts(Space* read_only_space,
                                  const ReadOnlyArtifacts* artifacts);

  // Retrieves the entry referenced by the given handle.
  //
  // This method is atomic and can be called from background threads.
  inline Address Get(ExternalPointerHandle handle,
                     ExternalPointerTag tag) const;

  // Sets the entry referenced by the given handle.
  //
  // This method is atomic and can be called from background threads.
  inline void Set(ExternalPointerHandle handle, Address value,
                  ExternalPointerTag tag);

  // Exchanges the entry referenced by the given handle with the given value,
  // returning the previous value. The same tag is applied both to decode the
  // previous value and encode the given value.
  //
  // This method is atomic and can be called from background threads.
  inline Address Exchange(ExternalPointerHandle handle, Address value,
                          ExternalPointerTag tag);

  // Retrieves the tag used for the entry referenced by the given handle.
  //
  // This method is atomic and can be called from background threads.
  inline ExternalPointerTag GetTag(ExternalPointerHandle handle) const;

  // Invalidates the entry referenced by the given handle.
  inline void Zap(ExternalPointerHandle handle);

  // Allocates a new entry in the given space. The caller must provide the
  // initial value and tag for the entry.
  //
  // This method is atomic and can be called from background threads.
  inline ExternalPointerHandle AllocateAndInitializeEntry(
      Space* space, Address initial_value, ExternalPointerTag tag);

  // Marks the specified entry as alive.
  //
  // If the space to which the entry belongs is currently being compacted, this
  // may also mark the entry for evacuation for which the location of the
  // handle is required. See the comments about the compaction algorithm for
  // more details.
  //
  // This method is atomic and can be called from background threads.
  inline void Mark(Space* space, ExternalPointerHandle handle,
                   Address handle_location);

  // Evacuate the specified entry from one space to another, updating the handle
  // location in place.
  //
  // This method is not atomic and can be called only when the mutator is
  // paused.
  inline void Evacuate(Space* from_space, Space* to_space,
                       ExternalPointerHandle handle, Address handle_location,
                       EvacuateMarkMode mode);

  // Evacuate all segments from from_space to to_space, leaving from_space empty
  // with an empty free list.  Then free unmarked entries, finishing compaction
  // if it was running, and collecting freed entries onto to_space's free list.
  //
  // The from_space will be left empty with an empty free list.
  //
  // This method must only be called while mutator threads are stopped as it is
  // not safe to allocate table entries while the table is being swept.
  //
  // SweepAndCompact is the same as EvacuateAndSweepAndCompact, except without
  // the evacuation phase.
  //
  // Sweep is the same as SweepAndCompact, but assumes that compaction was not
  // running.
  //
  // Returns the number of live entries after sweeping.
  uint32_t EvacuateAndSweepAndCompact(Space* to_space, Space* from_space,
                                      Counters* counters);
  uint32_t SweepAndCompact(Space* space, Counters* counters);
  uint32_t Sweep(Space* space, Counters* counters);

  // Updates all evacuation entries with new handle locations. The function
  // takes the old hanlde location and returns the new one.
  void UpdateAllEvacuationEntries(Space*, std::function<Address(Address)>);

  inline bool Contains(Space* space, ExternalPointerHandle handle) const;

  // A resource outside of the V8 heap whose lifetime is tied to something
  // inside the V8 heap. This class makes that relationship explicit.
  //
  // Knowing about such objects is important for the sandbox to guarantee
  // memory safety. In particular, it is necessary to prevent issues where the
  // external resource is destroyed before the entry in the
  // ExternalPointerTable (EPT) that references it is freed. In that case, the
  // EPT entry would then contain a dangling pointer which could be abused by
  // an attacker to cause a use-after-free outside of the sandbox.
  //
  // Currently, this is solved by remembering the EPT entry in the external
  // object and zapping/invalidating it when the resource is destroyed. An
  // alternative approach that might be preferable in the future would be to
  // destroy the external resource only when the EPT entry is freed. This would
  // avoid the need to manually keep track of the entry, for example.
  class ManagedResource : public Malloced {
   public:
    // This method must be called before destroying the external resource.
    // When the sandbox is enabled, it will take care of zapping its EPT entry.
    inline void ZapExternalPointerTableEntry();

   private:
    friend class ExternalPointerTable;
    // Currently required for snapshot stress mode, see deserializer.cc.
    template <typename IsolateT>
    friend class Deserializer;

    ExternalPointerTable* owning_table_ = nullptr;
    ExternalPointerHandle ept_entry_ = kNullExternalPointerHandle;
  };

 private:
  static inline bool IsValidHandle(ExternalPointerHandle handle);
  static inline uint32_t HandleToIndex(ExternalPointerHandle handle);
  static inline ExternalPointerHandle IndexToHandle(uint32_t index);

  inline void TakeOwnershipOfManagedResourceIfNecessary(
      Address value, ExternalPointerHandle handle, ExternalPointerTag tag);
  inline void FreeManagedResourceIfPresent(uint32_t entry_index);

  void ResolveEvacuationEntryDuringSweeping(
      uint32_t index, ExternalPointerHandle* handle_location,
      uint32_t start_of_evacuation_area);
};

static_assert(sizeof(ExternalPointerTable) == ExternalPointerTable::kSize);

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_EXTERNAL_POINTER_TABLE_H_
