// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SLOT_SET_H_
#define V8_HEAP_SLOT_SET_H_

#include <map>
#include <memory>
#include <stack>
#include <vector>

#include "src/base/bit-field.h"
#include "src/heap/base/basic-slot-set.h"
#include "src/objects/compressed-slots.h"
#include "src/objects/slots.h"
#include "src/utils/allocation.h"
#include "src/utils/utils.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace internal {

using ::heap::base::KEEP_SLOT;
using ::heap::base::REMOVE_SLOT;
using ::heap::base::SlotCallbackResult;

// Possibly empty buckets (buckets that do not contain any slots) are discovered
// by the scavenger. Buckets might become non-empty when promoting objects later
// or in another thread, so all those buckets need to be revisited.
// Track possibly empty buckets within a SlotSet in this data structure. The
// class contains a word-sized bitmap, in case more bits are needed the bitmap
// is replaced with a pointer to a malloc-allocated bitmap.
class PossiblyEmptyBuckets {
 public:
  PossiblyEmptyBuckets() = default;
  PossiblyEmptyBuckets(PossiblyEmptyBuckets&& other) V8_NOEXCEPT
      : bitmap_(other.bitmap_) {
    other.bitmap_ = kNullAddress;
  }

  ~PossiblyEmptyBuckets() { Release(); }

  PossiblyEmptyBuckets(const PossiblyEmptyBuckets&) = delete;
  PossiblyEmptyBuckets& operator=(const PossiblyEmptyBuckets&) = delete;

  void Release() {
    if (IsAllocated()) {
      AlignedFree(BitmapArray());
    }
    bitmap_ = kNullAddress;
    DCHECK(!IsAllocated());
  }

  void Insert(size_t bucket_index, size_t buckets) {
    if (IsAllocated()) {
      InsertAllocated(bucket_index);
    } else if (bucket_index + 1 < kBitsPerWord) {
      bitmap_ |= static_cast<uintptr_t>(1) << (bucket_index + 1);
    } else {
      Allocate(buckets);
      InsertAllocated(bucket_index);
    }
  }

  bool Contains(size_t bucket_index) {
    if (IsAllocated()) {
      size_t word_idx = bucket_index / kBitsPerWord;
      uintptr_t* word = BitmapArray() + word_idx;
      return *word &
             (static_cast<uintptr_t>(1) << (bucket_index % kBitsPerWord));
    } else if (bucket_index + 1 < kBitsPerWord) {
      return bitmap_ & (static_cast<uintptr_t>(1) << (bucket_index + 1));
    } else {
      return false;
    }
  }

  bool IsEmpty() const { return bitmap_ == kNullAddress; }

 private:
  static constexpr Address kPointerTag = 1;
  static constexpr int kWordSize = sizeof(uintptr_t);
  static constexpr int kBitsPerWord = kWordSize * kBitsPerByte;

  bool IsAllocated() { return bitmap_ & kPointerTag; }

  void Allocate(size_t buckets) {
    DCHECK(!IsAllocated());
    size_t words = WordsForBuckets(buckets);
    uintptr_t* ptr = reinterpret_cast<uintptr_t*>(
        AlignedAllocWithRetry(words * kWordSize, kSystemPointerSize));
    ptr[0] = bitmap_ >> 1;

    for (size_t word_idx = 1; word_idx < words; word_idx++) {
      ptr[word_idx] = 0;
    }
    bitmap_ = reinterpret_cast<Address>(ptr) + kPointerTag;
    DCHECK(IsAllocated());
  }

  void InsertAllocated(size_t bucket_index) {
    DCHECK(IsAllocated());
    size_t word_idx = bucket_index / kBitsPerWord;
    uintptr_t* word = BitmapArray() + word_idx;
    *word |= static_cast<uintptr_t>(1) << (bucket_index % kBitsPerWord);
  }

  static size_t WordsForBuckets(size_t buckets) {
    return (buckets + kBitsPerWord - 1) / kBitsPerWord;
  }

  uintptr_t* BitmapArray() {
    DCHECK(IsAllocated());
    return reinterpret_cast<uintptr_t*>(bitmap_ & ~kPointerTag);
  }

  Address bitmap_ = kNullAddress;

  FRIEND_TEST(PossiblyEmptyBucketsTest, WordsForBuckets);
};

static_assert(std::is_standard_layout<PossiblyEmptyBuckets>::value);
static_assert(sizeof(PossiblyEmptyBuckets) == kSystemPointerSize);

class SlotSet final : public ::heap::base::BasicSlotSet<kTaggedSize> {
  using BasicSlotSet = ::heap::base::BasicSlotSet<kTaggedSize>;

 public:
  static const int kBucketsRegularPage =
      (1 << kPageSizeBits) / kTaggedSize / kCellsPerBucket / kBitsPerCell;

  static SlotSet* Allocate(size_t buckets) {
    return static_cast<SlotSet*>(BasicSlotSet::Allocate(buckets));
  }

  template <v8::internal::AccessMode access_mode>
  static constexpr BasicSlotSet::AccessMode ConvertAccessMode() {
    switch (access_mode) {
      case v8::internal::AccessMode::ATOMIC:
        return BasicSlotSet::AccessMode::ATOMIC;
      case v8::internal::AccessMode::NON_ATOMIC:
        return BasicSlotSet::AccessMode::NON_ATOMIC;
    }
  }

  // Similar to BasicSlotSet::Iterate() but Callback takes the parameter of type
  // MaybeObjectSlot.
  template <
      v8::internal::AccessMode access_mode = v8::internal::AccessMode::ATOMIC,
      typename Callback>
  size_t Iterate(Address chunk_start, size_t start_bucket, size_t end_bucket,
                 Callback callback, EmptyBucketMode mode) {
    return BasicSlotSet::Iterate<ConvertAccessMode<access_mode>()>(
        chunk_start, start_bucket, end_bucket,
        [&callback](Address slot) { return callback(MaybeObjectSlot(slot)); },
        [this, mode](size_t bucket_index) {
          if (mode == EmptyBucketMode::FREE_EMPTY_BUCKETS) {
            ReleaseBucket(bucket_index);
          }
        });
  }

  // Similar to SlotSet::Iterate() but marks potentially empty buckets
  // internally. Stores true in empty_bucket_found in case a potentially empty
  // bucket was found. Assumes that the possibly empty-array was already cleared
  // by CheckPossiblyEmptyBuckets.
  template <typename Callback>
  size_t IterateAndTrackEmptyBuckets(
      Address chunk_start, size_t start_bucket, size_t end_bucket,
      Callback callback, PossiblyEmptyBuckets* possibly_empty_buckets) {
    return BasicSlotSet::Iterate(
        chunk_start, start_bucket, end_bucket,
        [&callback](Address slot) { return callback(MaybeObjectSlot(slot)); },
        [possibly_empty_buckets, end_bucket](size_t bucket_index) {
          possibly_empty_buckets->Insert(bucket_index, end_bucket);
        });
  }

  // Check whether possibly empty buckets are really empty. Empty buckets are
  // freed and the possibly empty state is cleared for all buckets.
  bool CheckPossiblyEmptyBuckets(size_t buckets,
                                 PossiblyEmptyBuckets* possibly_empty_buckets) {
    bool empty = true;
    for (size_t bucket_index = 0; bucket_index < buckets; bucket_index++) {
      Bucket* bucket = LoadBucket<AccessMode::NON_ATOMIC>(bucket_index);
      if (bucket) {
        if (possibly_empty_buckets->Contains(bucket_index)) {
          if (bucket->IsEmpty()) {
            ReleaseBucket<AccessMode::NON_ATOMIC>(bucket_index);
          } else {
            empty = false;
          }
        } else {
          empty = false;
        }
      } else {
        // Unfortunately we cannot DCHECK here that the corresponding bit in
        // possibly_empty_buckets is not set. After scavenge, the
        // MergeOldToNewRememberedSets operation might remove a recorded bucket.
      }
    }

    possibly_empty_buckets->Release();

    return empty;
  }

  void Merge(SlotSet* other, size_t buckets) {
    for (size_t bucket_index = 0; bucket_index < buckets; bucket_index++) {
      Bucket* other_bucket =
          other->LoadBucket<AccessMode::NON_ATOMIC>(bucket_index);
      if (!other_bucket) continue;
      Bucket* bucket = LoadBucket<AccessMode::NON_ATOMIC>(bucket_index);
      if (bucket == nullptr) {
        other->StoreBucket<AccessMode::NON_ATOMIC>(bucket_index, nullptr);
        StoreBucket<AccessMode::NON_ATOMIC>(bucket_index, other_bucket);
      } else {
        for (int cell_index = 0; cell_index < kCellsPerBucket; cell_index++) {
          bucket->SetCellBits<AccessMode::NON_ATOMIC>(
              cell_index,
              other_bucket->LoadCell<AccessMode::NON_ATOMIC>(cell_index));
        }
      }
    }
  }
};

static_assert(std::is_standard_layout<SlotSet>::value);
static_assert(std::is_standard_layout<SlotSet::Bucket>::value);

enum class SlotType : uint8_t {
  // Full pointer sized slot storing an object start address.
  // RelocInfo::target_object/RelocInfo::set_target_object methods are used for
  // accessing. Used when pointer is stored in the instruction stream.
  kEmbeddedObjectFull,

  // Tagged sized slot storing an object start address.
  // RelocInfo::target_object/RelocInfo::set_target_object methods are used for
  // accessing. Used when pointer is stored in the instruction stream.
  kEmbeddedObjectCompressed,

  // Full pointer sized slot storing instruction start of Code object.
  // RelocInfo::target_address/RelocInfo::set_target_address methods are used
  // for accessing. Used when pointer is stored in the instruction stream.
  kCodeEntry,

  // Raw full pointer sized slot. Slot is accessed directly. Used when pointer
  // is stored in constant pool.
  kConstPoolEmbeddedObjectFull,

  // Raw tagged sized slot. Slot is accessed directly. Used when pointer is
  // stored in constant pool.
  kConstPoolEmbeddedObjectCompressed,

  // Raw full pointer sized slot storing instruction start of Code object. Slot
  // is accessed directly. Used when pointer is stored in constant pool.
  kConstPoolCodeEntry,

  // Slot got cleared but has not been removed from the slot set.
  kCleared,

  kLast = kCleared
};

// Data structure for maintaining a list of typed slots in a page.
// Typed slots can only appear in Code objects, so
// the maximum possible offset is limited by the
// LargePageMetadata::kMaxCodePageSize. The implementation is a chain of chunks,
// where each chunk is an array of encoded (slot type, slot offset) pairs. There
// is no duplicate detection and we do not expect many duplicates because typed
// slots contain V8 internal pointers that are not directly exposed to JS.
class V8_EXPORT_PRIVATE TypedSlots {
 public:
  static const int kMaxOffset = 1 << 29;
  TypedSlots() = default;
  virtual ~TypedSlots();
  void Insert(SlotType type, uint32_t offset);
  void Merge(TypedSlots* other);

 protected:
  using OffsetField = base::BitField<int, 0, 29>;
  using TypeField = base::BitField<SlotType, 29, 3>;
  struct TypedSlot {
    uint32_t type_and_offset;
  };
  struct Chunk {
    Chunk* next;
    std::vector<TypedSlot> buffer;
  };
  static const size_t kInitialBufferSize = 100;
  static const size_t kMaxBufferSize = 16 * KB;
  static size_t NextCapacity(size_t capacity) {
    return std::min({kMaxBufferSize, capacity * 2});
  }
  Chunk* EnsureChunk();
  Chunk* NewChunk(Chunk* next, size_t capacity);
  Chunk* head_ = nullptr;
  Chunk* tail_ = nullptr;
};

// A multiset of per-page typed slots that allows concurrent iteration
// clearing of invalid slots.
class V8_EXPORT_PRIVATE TypedSlotSet : public TypedSlots {
 public:
  using FreeRangesMap = std::map<uint32_t, uint32_t>;

  enum IterationMode { FREE_EMPTY_CHUNKS, KEEP_EMPTY_CHUNKS };

  explicit TypedSlotSet(Address page_start) : page_start_(page_start) {}

  // Iterate over all slots in the set and for each slot invoke the callback.
  // If the callback returns REMOVE_SLOT then the slot is removed from the set.
  // Returns the new number of slots.
  //
  // Sample usage:
  // Iterate([](SlotType slot_type, Address slot_address) {
  //    if (good(slot_type, slot_address)) return KEEP_SLOT;
  //    else return REMOVE_SLOT;
  // });
  // This can run concurrently to ClearInvalidSlots().
  template <typename Callback>
  int Iterate(Callback callback, IterationMode mode) {
    static_assert(static_cast<uint8_t>(SlotType::kLast) < 8);
    Chunk* chunk = head_;
    Chunk* previous = nullptr;
    int new_count = 0;
    while (chunk != nullptr) {
      bool empty = true;
      for (TypedSlot& slot : chunk->buffer) {
        SlotType type = TypeField::decode(slot.type_and_offset);
        if (type != SlotType::kCleared) {
          uint32_t offset = OffsetField::decode(slot.type_and_offset);
          Address addr = page_start_ + offset;
          if (callback(type, addr) == KEEP_SLOT) {
            new_count++;
            empty = false;
          } else {
            slot = ClearedTypedSlot();
          }
        }
      }
      Chunk* next = chunk->next;
      if (mode == FREE_EMPTY_CHUNKS && empty) {
        // We remove the chunk from the list but let it still point its next
        // chunk to allow concurrent iteration.
        if (previous) {
          StoreNext(previous, next);
        } else {
          StoreHead(next);
        }

        delete chunk;
      } else {
        previous = chunk;
      }
      chunk = next;
    }
    return new_count;
  }

  // Clears all slots that have the offset in the specified ranges.
  // This can run concurrently to Iterate().
  void ClearInvalidSlots(const FreeRangesMap& invalid_ranges);

  // Asserts that there are no recorded slots in the specified ranges.
  void AssertNoInvalidSlots(const FreeRangesMap& invalid_ranges);

  // Frees empty chunks accumulated by PREFREE_EMPTY_CHUNKS.
  void FreeToBeFreedChunks();

 private:
  template <typename Callback>
  void IterateSlotsInRanges(Callback callback,
                            const FreeRangesMap& invalid_ranges);

  // Atomic operations used by Iterate and ClearInvalidSlots;
  Chunk* LoadNext(Chunk* chunk) {
    return base::AsAtomicPointer::Relaxed_Load(&chunk->next);
  }
  void StoreNext(Chunk* chunk, Chunk* next) {
    return base::AsAtomicPointer::Relaxed_Store(&chunk->next, next);
  }
  Chunk* LoadHead() { return base::AsAtomicPointer::Relaxed_Load(&head_); }
  void StoreHead(Chunk* chunk) {
    base::AsAtomicPointer::Relaxed_Store(&head_, chunk);
  }
  static TypedSlot ClearedTypedSlot() {
    return TypedSlot{TypeField::encode(SlotType::kCleared) |
                     OffsetField::encode(0)};
  }

  Address page_start_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SLOT_SET_H_
