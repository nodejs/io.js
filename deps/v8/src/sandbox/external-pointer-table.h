// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_POINTER_TABLE_H_
#define V8_SANDBOX_EXTERNAL_POINTER_TABLE_H_

#include <vector>

#include "include/v8config.h"
#include "src/base/atomicops.h"
#include "src/base/memory.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/sandbox/external-entity-table.h"

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

  // Move the content of this entry into the provided entry while also clearing
  // the marking bit. Used during table compaction. This invalidates the entry.
  inline void UnmarkAndMigrateInto(ExternalPointerTableEntry& other);

  // Mark this entry as alive during table garbage collection.
  inline void Mark();

 private:
  friend class ExternalPointerTable;

  // ExternalPointerTable entries consist of a single pointer-sized word
  // containing a tag and marking bit together with the actual content (e.g. an
  // external pointer).
  struct Payload {
    Payload(Address pointer, ExternalPointerTag tag)
        : encoded_word_(Tag(pointer, tag)) {}

    Address Untag(ExternalPointerTag tag) const { return encoded_word_ & ~tag; }

    static Address Tag(Address pointer, ExternalPointerTag tag) {
      return pointer | tag;
    }

    bool IsTaggedWith(ExternalPointerTag tag) const {
      // We have to explicitly ignore the marking bit (which is part of the
      // tag) since an unmarked entry with tag kXyzTag is still considered to
      // be tagged with kXyzTag.
      uint64_t expected = tag & ~kExternalPointerMarkBit;
      uint64_t actual = encoded_word_ & kExternalPointerTagMaskWithoutMarkBit;
      return expected == actual;
    }

    void SetMarkBit() { encoded_word_ |= kExternalPointerMarkBit; }

    void ClearMarkBit() { encoded_word_ &= ~kExternalPointerMarkBit; }

    bool HasMarkBitSet() const {
      return (encoded_word_ & kExternalPointerMarkBit) != 0;
    }

    bool ContainsFreelistLink() const {
      return IsTaggedWith(kExternalPointerFreeEntryTag);
    }

    uint32_t ExtractFreelistLink() const {
      return static_cast<uint32_t>(encoded_word_);
    }

    bool ContainsEvacuationEntry() const {
      return IsTaggedWith(kExternalPointerEvacuationEntryTag);
    }

    Address ExtractEvacuationEntryHandleLocation() const {
      return Untag(kExternalPointerEvacuationEntryTag);
    }

    bool ContainsExternalPointer() const {
      return !ContainsFreelistLink() && !ContainsEvacuationEntry();
    }

    bool operator==(Payload other) const {
      return encoded_word_ == other.encoded_word_;
    }
    bool operator!=(Payload other) const {
      return encoded_word_ != other.encoded_word_;
    }

   private:
    Address encoded_word_;
  };

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
 * For the purpose of memory management, the table is partitioned into Segments
 * (for example 64kb memory chunks) that are grouped together in "Spaces". All
 * segments in a space share a freelist, and so entry allocation and garbage
 * collection happen on the level of spaces. The garbage collection algorithm
 * then works as follows:
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
 * The table's spaces are to some degree self-compacting: since the freelists
 * are sorted in ascending order (see SweepAndCompact()), segments at the start
 * of the table will usually be fairly well utilized, while later segments
 * might become completely free, in which case they will be deallocated.
 * However, as a single live entry may keep an entire segment alive, the
 * following simple algorithm is used to compact a space if that is deemed
 * necessary:
 *  - At the start of the GC marking phase, determine if a space needs to be
 *    compacted. This decisiont is mostly based on the absolute and relative
 *    size of the freelist.
 *  - If compaction is needed, this algorithm determines by how many segments
 *    it would like to shrink the space (N). It will then attempts to move all
 *    live entries out of these segments so that they can be deallocated
 *    afterwards during sweeping.
 *  - The algorithm then simply selects the last N segments for evacuation, and
 *    it "marks" them for evacuation simply by remembering the start of the
 *    first selected segment. Everything after this threshold value then
 *    becomes the evacuation area. In this way, it becomes very cheap to test
 *    if an entry or segment should be evacuated: only a single integer
 *    comparison against the threshold is required. It also establishes a
 *    simple compaction invariant: compaction always moves an entry at or above
 *    the threshold to a new position before the threshold.
 *  - During marking, whenever a live entry inside the evacuation area is
 *    found, a new "evacuation entry" is allocated from the freelist (which is
 *    assumed to have enough free slots) and the address of the handle in the
 *    object owning the table entry is written into it.
 *  - During sweeping, these evacuation entries are resolved: the content of
 *    the old entry is copied into the new entry and the handle in the object
 *    is updated to point to the new entry.
 *
 * When compacting, it is expected that the evacuation area contains few live
 * entries and that the freelist will be able to serve all evacuation entry
 * allocations. In that case, compaction is essentially free (very little
 * marking overhead, no memory overhead). However, it can happen that the
 * application allocates a large number of table entries during marking, in
 * which case we might end up allocating new entries inside the evacuation area
 * or even allocate entire new segments for the space that's being compacted.
 * If that situation is detected, compaction is aborted during marking.
 *
 * This algorithm assumes that table entries (except for the null entry) are
 * never shared between multiple objects. Otherwise, the following could
 * happen: object A initially has handle H1 and is scanned during incremental
 * marking. Next, object B with handle H2 is scanned and marked for
 * evacuation. Afterwards, object A copies the handle H2 from object B.
 * During sweeping, only object B's handle will be updated to point to the
 * new entry while object A's handle is now dangling. If shared entries ever
 * become necessary, setting external pointer handles would have to be
 * guarded by write barriers to avoid this scenario.
 */
class V8_EXPORT_PRIVATE ExternalPointerTable
    : public ExternalEntityTable<ExternalPointerTableEntry,
                                 kExternalPointerTableReservationSize> {
#if defined(LEAK_SANITIZER)
  //  When LSan is active, we use "fat" entries, see above.
  static_assert(kMaxExternalPointers == kMaxCapacity * 2);
#else
  static_assert(kMaxExternalPointers == kMaxCapacity);
#endif

 public:
  // Size of an ExternalPointerTable, for layout computation in IsolateData.
  static int constexpr kSize = 2 * kSystemPointerSize;

  ExternalPointerTable() = default;
  ExternalPointerTable(const ExternalPointerTable&) = delete;
  ExternalPointerTable& operator=(const ExternalPointerTable&) = delete;

  // The Spaces used by an ExternalPointerTable also contain the state related
  // to compaction.
  struct Space : public ExternalEntityTable<
                     ExternalPointerTableEntry,
                     kExternalPointerTableReservationSize>::Space {
   public:
    Space() : start_of_evacuation_area_(kNotCompactingMarker) {}

    // Determine if compaction is needed and if so start the compaction.
    // This is expected to be called at the start of the GC marking phase.
    void StartCompactingIfNeeded();

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

   private:
    friend class ExternalPointerTable;

    // Routines for compaction. See the comment about table compaction above.
    inline bool IsCompacting();
    inline void StartCompacting(uint32_t start_of_evacuation_area);
    inline void StopCompacting();
    inline void AbortCompacting(uint32_t start_of_evacuation_area);
    inline bool CompactingWasAborted();

    inline bool FieldWasInvalidated(Address field_address) const;
    inline void ClearInvalidatedFields();

    // This value indicates that this space is not currently being compacted. It
    // is set to uint32_t max so that determining whether an entry should be
    // evacuated becomes a single comparison:
    // `bool should_be_evacuated = index >= start_of_evacuation_area`.
    static constexpr uint32_t kNotCompactingMarker =
        std::numeric_limits<uint32_t>::max();

    // This value may be ORed into the start of evacuation area threshold
    // during the GC marking phase to indicate that compaction has been
    // aborted because the freelist grew to short and so evacuation entry
    // allocation is no longer possible. This will prevent any further
    // evacuation attempts as entries will be evacuated if their index is at or
    // above the start of the evacuation area, which is now a huge value.
    static constexpr uint32_t kCompactionAbortedMarker = 0xf0000000;

    // When compacting this space, this field contains the index of the first
    // entry in the evacuation area. The evacuation area then consists of all
    // segments above this threshold, and the goal of compaction is to move all
    // live entries out of these segments so that they can be deallocated after
    // sweeping. The field can have the following values:
    // - kNotCompactingMarker: compaction is not currently running.
    // - A kEntriesPerSegment aligned value within: compaction is running and
    //   all entries after this value should be evacuated.
    // - A value that has kCompactionAbortedMarker in its top bits:
    //   compaction has been aborted during marking. The original start of the
    //   evacuation area is still contained in the lower bits.
    std::atomic<uint32_t> start_of_evacuation_area_;

    // List of external pointer fields that have been invalidated. See
    // NotifyExternalPointerFieldInvalidated. Only used when table compaction
    // is running.
    // We expect very few (usually none at all) fields to be invalidated during
    // a GC, so a std::vector is probably better than a std::set or similar.
    std::vector<Address> invalidated_fields_;

    // Mutex guarding access to the invalidated_fields_ set.
    base::Mutex invalidated_fields_mutex_;
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

  // Frees unmarked entries and finishes space compaction (if running).
  //
  // This method must only be called while mutator threads are stopped as it is
  // not safe to allocate table entries while the table is being swept.
  //
  // Returns the number of live entries after sweeping.
  uint32_t SweepAndCompact(Space* space, Counters* counters);

 private:
  static inline bool IsValidHandle(ExternalPointerHandle handle);
  static inline uint32_t HandleToIndex(ExternalPointerHandle handle);
  static inline ExternalPointerHandle IndexToHandle(uint32_t index);

  inline void MaybeCreateEvacuationEntry(Space* space, uint32_t index,
                                         Address handle_location);

  bool TryResolveEvacuationEntryDuringSweeping(
      uint32_t index, ExternalPointerHandle* handle_location,
      uint32_t start_of_evacuation_area);

  // Outcome of external pointer table compaction to use for the
  // ExternalPointerTableCompactionOutcome histogram.
  enum class TableCompactionOutcome {
    // Table compaction was successful.
    kSuccess = 0,
    // Outcome 1, partial success, is no longer supported.
    // Table compaction was aborted because the freelist grew to short.
    kAborted = 2,
  };
};

static_assert(sizeof(ExternalPointerTable) == ExternalPointerTable::kSize);

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_EXTERNAL_POINTER_TABLE_H_
