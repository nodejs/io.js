// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CPPHEAP_POINTER_TABLE_H_
#define V8_SANDBOX_CPPHEAP_POINTER_TABLE_H_

#include "include/v8-sandbox.h"
#include "include/v8config.h"
#include "src/base/atomicops.h"
#include "src/base/bounds.h"
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

/**
 * The entries of an CppHeapPointerTable.
 *
 * Each entry consists of a single pointer-sized word containing the pointer,
 * the marking bit, and a type tag. An entry can either be:
 *  - A "regular" entry, containing the pointer together with a type tag and
 *    the marking bit, or
 *  - A freelist entry, tagged with the kFreeEntryTag and containing the index
 *    of the next free entry, or
 *  - An evacuation entry, tagged with the kEvacuationEntryTag and containing
 *    the address of the CppHeapPointerSlot referencing the entry that will be
 *    evacuated into this entry.
 */
struct CppHeapPointerTableEntry {
  // Make this entry a cpp heap pointer entry containing the given pointer
  // tagged with the given tag.
  inline void MakePointerEntry(Address value, CppHeapPointerTag tag);

  // Load and untag the pointer stored in this entry.
  // This entry must be a pointer entry.
  // If the tag of the entry is not within the specified tag range, the
  // resulting pointer will be invalid and cannot be dereferenced.
  inline Address GetPointer(CppHeapPointerTagRange tag_range) const;

  // Tag and store the given pointer in this entry.
  // This entry must be a pointer entry.
  inline void SetPointer(Address value, CppHeapPointerTag tag);

  // Returns true if this entry contains a pointer whose tag is within the
  // specified tag range.
  inline bool HasPointer(CppHeapPointerTagRange tag_range) const;

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
  inline void Evacuate(CppHeapPointerTableEntry& dest);

  // Mark this entry as alive during table garbage collection.
  inline void Mark();

  static constexpr bool IsWriteProtected = false;

 private:
  friend class CppHeapPointerTable;

  struct Payload {
    Payload(Address pointer, CppHeapPointerTag tag)
        : encoded_word_(Tag(pointer, tag)) {}

    Address Untag(CppHeapPointerTagRange tag_range) const {
      Address content = encoded_word_;
      if (V8_LIKELY(tag_range.CheckTagOf(content))) {
        content >>= kCppHeapPointerPayloadShift;
      } else {
        // If the type check failed, we simply return nullptr here. That way:
        //  1. The null handle always results in nullptr being returned here,
        //     which is a desired property. Otherwise, we may need an explicit
        //     check for the null handle in the caller, and therefore an
        //     additional branch. This works because the 0th entry of the table
        //     always contains nullptr tagged with the null tag (i.e. an
        //     all-zeros entry). As such, regardless of whether the type check
        //     succeeds, the result will always be nullptr.
        //  2. The returned pointer is guaranteed to crash even on platforms
        //     with top byte ignore (TBI), such as Arm64. The alternative would
        //     be to simply return the original entry with the left-shifted
        //     payload. However, due to TBI, an access to that may not always
        //     result in a crash (specifically, if the second most significant
        //     byte happens to be zero). In addition, there shouldn't be a
        //     difference on Arm64 between returning nullptr or the original
        //     entry, since it will simply compile to a `csel x0, x8, xzr, lo`
        //     instead of a `csel x0, x10, x8, lo` instruction.
        content = 0;
      }
      return content;
    }

    Address Untag(CppHeapPointerTag tag) const {
      return Untag(CppHeapPointerTagRange(tag, tag));
    }

    static Address Tag(Address pointer, CppHeapPointerTag tag) {
      return (pointer << kCppHeapPointerPayloadShift) |
             (static_cast<uint16_t>(tag) << kCppHeapPointerTagShift) |
             kCppHeapPointerMarkBit;
    }

    bool IsTaggedWithTagIn(CppHeapPointerTagRange tag_range) const {
      return tag_range.CheckTagOf(encoded_word_);
    }

    bool IsTaggedWith(CppHeapPointerTag tag) const {
      return IsTaggedWithTagIn(CppHeapPointerTagRange(tag, tag));
    }

    void SetMarkBit() { encoded_word_ |= kCppHeapPointerMarkBit; }

    void ClearMarkBit() { encoded_word_ &= ~kCppHeapPointerMarkBit; }

    bool HasMarkBitSet() const {
      return encoded_word_ & kCppHeapPointerMarkBit;
    }

    uint32_t ExtractFreelistLink() const {
      return static_cast<uint32_t>(encoded_word_ >>
                                   kCppHeapPointerPayloadShift);
    }

    CppHeapPointerTag ExtractTag() const { UNREACHABLE(); }

    bool ContainsFreelistLink() const {
      return IsTaggedWith(CppHeapPointerTag::kFreeEntryTag);
    }

    bool ContainsEvacuationEntry() const {
      return IsTaggedWith(CppHeapPointerTag::kEvacuationEntryTag);
    }

    Address ExtractEvacuationEntryHandleLocation() const {
      return Untag(CppHeapPointerTag::kEvacuationEntryTag);
    }

    bool ContainsPointer() const {
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

  // CppHeapPointerTable entries consist of a single pointer-sized word
  // containing a tag and marking bit together with the actual content.
  std::atomic<Payload> payload_;
};

//  We expect CppHeapPointerTable entries to consist of a single 64-bit word.
static_assert(sizeof(CppHeapPointerTableEntry) == 8);

/**
 * A table storing pointers to objects in the CppHeap
 *
 * This table is essentially a specialized version of the ExternalPointerTable
 * used for CppHeap objects. It uses a different type tagging scheme which
 * supports significantly more types and also supports type hierarchies. See
 * the CppHeapPointerTag enum for more details.
 *
 * Apart from that, this table mostly behaves like the external pointer table
 * and so uses a simple garbage collection algorithm to detect and free unused
 * entries and also supports table compaction.
 *
 */
class V8_EXPORT_PRIVATE CppHeapPointerTable
    : public CompactibleExternalEntityTable<
          CppHeapPointerTableEntry, kCppHeapPointerTableReservationSize> {
  using Base =
      CompactibleExternalEntityTable<CppHeapPointerTableEntry,
                                     kCppHeapPointerTableReservationSize>;
  static_assert(kMaxCppHeapPointers == kMaxCapacity);

 public:
  // Size of an CppHeapPointerTable, for layout computation in IsolateData.
  static int constexpr kSize = 2 * kSystemPointerSize;

  CppHeapPointerTable() = default;
  CppHeapPointerTable(const CppHeapPointerTable&) = delete;
  CppHeapPointerTable& operator=(const CppHeapPointerTable&) = delete;

  // The Spaces used by an CppHeapPointerTable.
  using Space = Base::Space;

  // Retrieves the entry referenced by the given handle.
  //
  // The tag of the entry must be within the specified range of tags.
  //
  // This method is atomic and can be called from background threads.
  inline Address Get(CppHeapPointerHandle handle,
                     CppHeapPointerTagRange tag_range) const;

  // Sets the entry referenced by the given handle.
  //
  // This method is atomic and can be called from background threads.
  inline void Set(CppHeapPointerHandle handle, Address value,
                  CppHeapPointerTag tag);

  // Allocates a new entry in the given space. The caller must provide the
  // initial value and tag for the entry.
  //
  // This method is atomic and can be called from background threads.
  inline CppHeapPointerHandle AllocateAndInitializeEntry(Space* space,
                                                         Address initial_value,
                                                         CppHeapPointerTag tag);

  // Marks the specified entry as alive.
  //
  // If the space to which the entry belongs is currently being compacted, this
  // may also mark the entry for evacuation for which the location of the
  // handle is required. See the comments about the compaction algorithm for
  // more details.
  //
  // This method is atomic and can be called from background threads.
  inline void Mark(Space* space, CppHeapPointerHandle handle,
                   Address handle_location);

  uint32_t SweepAndCompact(Space* space, Counters* counters);

  inline bool Contains(Space* space, CppHeapPointerHandle handle) const;

 private:
  static inline bool IsValidHandle(CppHeapPointerHandle handle);
  static inline uint32_t HandleToIndex(CppHeapPointerHandle handle);
  static inline CppHeapPointerHandle IndexToHandle(uint32_t index);

  void ResolveEvacuationEntryDuringSweeping(
      uint32_t index, CppHeapPointerHandle* handle_location,
      uint32_t start_of_evacuation_area);
};

static_assert(sizeof(CppHeapPointerTable) == CppHeapPointerTable::kSize);

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_CPPHEAP_POINTER_TABLE_H_
