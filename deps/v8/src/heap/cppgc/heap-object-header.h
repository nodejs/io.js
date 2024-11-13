// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_HEAP_OBJECT_HEADER_H_
#define V8_HEAP_CPPGC_HEAP_OBJECT_HEADER_H_

#include <stdint.h>

#include <atomic>

#include "include/cppgc/allocation.h"
#include "include/cppgc/internal/gc-info.h"
#include "include/cppgc/internal/member-storage.h"
#include "include/cppgc/internal/name-trait.h"
#include "src/base/atomic-utils.h"
#include "src/base/bit-field.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/heap/cppgc/gc-info-table.h"
#include "src/heap/cppgc/globals.h"

#if defined(CPPGC_CAGED_HEAP)
#include "src/heap/cppgc/caged-heap.h"
#endif  // defined(CPPGC_CAGED_HEAP)

namespace cppgc {

class Visitor;

namespace internal {

// HeapObjectHeader contains meta data per object and is prepended to each
// object.
//
// +-----------------+------+------------------------------------------+
// | name            | bits |                                          |
// +-----------------+------+------------------------------------------+
// | padding         |   32 | Only present on 64-bit platform.         |
// +-----------------+------+------------------------------------------+
// | GCInfoIndex     |   14 |                                          |
// | unused          |    1 |                                          |
// | in construction |    1 | In construction encoded as |false|.      |
// +-----------------+------+------------------------------------------+
// | size            |   15 | 17 bits because allocations are aligned. |
// | mark bit        |    1 |                                          |
// +-----------------+------+------------------------------------------+
//
// Notes:
// - See |GCInfoTable| for constraints on GCInfoIndex.
// - |size| for regular objects is encoded with 15 bits but can actually
//   represent sizes up to |kBlinkPageSize| (2^17) because allocations are
//   always 4 byte aligned (see kAllocationGranularity) on 32bit. 64bit uses
//   8 byte aligned allocations which leaves 1 bit unused.
// - |size| for large objects is encoded as 0. The size of a large object is
//   stored in |LargeObjectPage::PayloadSize()|.
// - |mark bit| and |in construction| bits are located in separate 16-bit halves
//    to allow potentially accessing them non-atomically.
class HeapObjectHeader {
 public:
  static constexpr size_t kSizeLog2 = 17;
  static constexpr size_t kMaxSize = (size_t{1} << kSizeLog2) - 1;
  static constexpr uint16_t kLargeObjectSizeInHeader = 0;

  inline static HeapObjectHeader& FromObject(void* address);
  inline static const HeapObjectHeader& FromObject(const void* address);

  inline HeapObjectHeader(size_t size, GCInfoIndex gc_info_index);

  // The object starts directly after the HeapObjectHeader.
  inline Address ObjectStart() const;
  template <AccessMode mode = AccessMode::kNonAtomic>
  inline Address ObjectEnd() const;

  template <AccessMode mode = AccessMode::kNonAtomic>
  inline GCInfoIndex GetGCInfoIndex() const;

  template <AccessMode mode = AccessMode::kNonAtomic>
  inline size_t AllocatedSize() const;
  inline void SetAllocatedSize(size_t size);

  template <AccessMode mode = AccessMode::kNonAtomic>
  inline size_t ObjectSize() const;

  template <AccessMode mode = AccessMode::kNonAtomic>
  inline bool IsLargeObject() const;

  template <AccessMode = AccessMode::kNonAtomic>
  bool IsInConstruction() const;
  inline void MarkAsFullyConstructed();
  // Use MarkObjectAsFullyConstructed() to mark an object as being constructed.

  template <AccessMode = AccessMode::kNonAtomic>
  bool IsMarked() const;
  template <AccessMode = AccessMode::kNonAtomic>
  void Unmark();
  inline bool TryMarkAtomic();

  inline void MarkNonAtomic();

  template <AccessMode = AccessMode::kNonAtomic>
  bool IsYoung() const;

  template <AccessMode = AccessMode::kNonAtomic>
  bool IsFree() const;

  inline bool IsFinalizable() const;
  void Finalize();

#if defined(CPPGC_CAGED_HEAP)
  inline void SetNextUnfinalized(HeapObjectHeader* next);
  inline HeapObjectHeader* GetNextUnfinalized(uintptr_t cage_base) const;
#endif  // defined(CPPGC_CAGED_HEAP)

  // Default version will retrieve `HeapObjectNameForUnnamedObject` as it is
  // configured at runtime.
  V8_EXPORT_PRIVATE HeapObjectName GetName() const;
  // Override for verifying and testing where we always want to pass the naming
  // option explicitly.
  V8_EXPORT_PRIVATE HeapObjectName
      GetName(HeapObjectNameForUnnamedObject) const;

  template <AccessMode = AccessMode::kNonAtomic>
  void Trace(Visitor*) const;

 private:
  enum class EncodedHalf : uint8_t { kLow, kHigh };

  // Used in |encoded_high_|.
  using FullyConstructedField = v8::base::BitField16<bool, 0, 1>;
  using UnusedField1 = FullyConstructedField::Next<bool, 1>;
  using GCInfoIndexField = UnusedField1::Next<GCInfoIndex, 14>;
  // Used in |encoded_low_|.
  using MarkBitField = v8::base::BitField16<bool, 0, 1>;
  using SizeField =
      MarkBitField::Next<size_t, 15>;  // Use EncodeSize/DecodeSize instead.

  static constexpr size_t DecodeSize(uint16_t encoded) {
    // Essentially, gets optimized to << 1.
    return SizeField::decode(encoded) * kAllocationGranularity;
  }

  static constexpr uint16_t EncodeSize(size_t size) {
    // Essentially, gets optimized to >> 1.
    return SizeField::encode(size / kAllocationGranularity);
  }

  V8_EXPORT_PRIVATE void CheckApiConstants();

  template <AccessMode, EncodedHalf part,
            std::memory_order memory_order = std::memory_order_seq_cst>
  inline uint16_t LoadEncoded() const;
  template <AccessMode mode, EncodedHalf part,
            std::memory_order memory_order = std::memory_order_seq_cst>
  inline void StoreEncoded(uint16_t bits, uint16_t mask);

#if defined(V8_HOST_ARCH_64_BIT)
  // If cage is enabled, to save on space required by sweeper metadata, we store
  // the list of to-be-finalized objects inlined in HeapObjectHeader.
#if defined(CPPGC_CAGED_HEAP)
  uint32_t next_unfinalized_ = 0;
#else   // !defined(CPPGC_CAGED_HEAP)
  uint32_t padding_ = 0;
#endif  // !defined(CPPGC_CAGED_HEAP)
#endif  // defined(V8_HOST_ARCH_64_BIT)
  uint16_t encoded_high_;
  uint16_t encoded_low_;
};

static_assert(kAllocationGranularity == sizeof(HeapObjectHeader),
              "sizeof(HeapObjectHeader) must match allocation granularity to "
              "guarantee alignment");

// static
HeapObjectHeader& HeapObjectHeader::FromObject(void* object) {
  return *reinterpret_cast<HeapObjectHeader*>(static_cast<Address>(object) -
                                              sizeof(HeapObjectHeader));
}

// static
const HeapObjectHeader& HeapObjectHeader::FromObject(const void* object) {
  return *reinterpret_cast<const HeapObjectHeader*>(
      static_cast<ConstAddress>(object) - sizeof(HeapObjectHeader));
}

HeapObjectHeader::HeapObjectHeader(size_t size, GCInfoIndex gc_info_index) {
#if defined(V8_HOST_ARCH_64_BIT) && !defined(CPPGC_CAGED_HEAP)
  USE(padding_);
#endif  // defined(V8_HOST_ARCH_64_BIT) && !defined(CPPGC_CAGED_HEAP)
  DCHECK_LT(gc_info_index, GCInfoTable::kMaxIndex);
  DCHECK_EQ(0u, size & (sizeof(HeapObjectHeader) - 1));
  DCHECK_GE(kMaxSize, size);
  encoded_low_ = EncodeSize(size);
  // Objects may get published to the marker without any other synchronization
  // (e.g., write barrier) in which case the in-construction bit is read
  // concurrently which requires reading encoded_high_ atomically. It is ok if
  // this write is not observed by the marker, since the sweeper  sets the
  // in-construction bit to 0 and we can rely on that to guarantee a correct
  // answer when checking if objects are in-construction.
  v8::base::AsAtomicPtr(&encoded_high_)
      ->store(GCInfoIndexField::encode(gc_info_index),
              std::memory_order_relaxed);
  DCHECK(IsInConstruction());
#ifdef DEBUG
  CheckApiConstants();
#endif  // DEBUG
}

Address HeapObjectHeader::ObjectStart() const {
  return reinterpret_cast<Address>(const_cast<HeapObjectHeader*>(this)) +
         sizeof(HeapObjectHeader);
}

template <AccessMode mode>
Address HeapObjectHeader::ObjectEnd() const {
  DCHECK(!IsLargeObject<mode>());
  return reinterpret_cast<Address>(const_cast<HeapObjectHeader*>(this)) +
         AllocatedSize<mode>();
}

template <AccessMode mode>
GCInfoIndex HeapObjectHeader::GetGCInfoIndex() const {
  const uint16_t encoded =
      LoadEncoded<mode, EncodedHalf::kHigh, std::memory_order_acquire>();
  return GCInfoIndexField::decode(encoded);
}

template <AccessMode mode>
size_t HeapObjectHeader::AllocatedSize() const {
  // Size is immutable after construction while either marking or sweeping
  // is running so relaxed load (if mode == kAtomic) is enough.
  uint16_t encoded_low_value =
      LoadEncoded<mode, EncodedHalf::kLow, std::memory_order_relaxed>();
  const size_t size = DecodeSize(encoded_low_value);
  return size;
}

void HeapObjectHeader::SetAllocatedSize(size_t size) {
#if !defined(CPPGC_YOUNG_GENERATION)
  // With sticky bits, marked objects correspond to old objects.
  // TODO(bikineev:1029379): Consider disallowing old/marked objects to be
  // resized.
  DCHECK(!IsMarked());
#endif
  // The object may be marked (i.e. old, in case young generation is enabled).
  // Make sure to not overwrite the mark bit.
  encoded_low_ &= ~SizeField::encode(SizeField::kMax);
  encoded_low_ |= EncodeSize(size);
}

template <AccessMode mode>
size_t HeapObjectHeader::ObjectSize() const {
  // The following DCHECK also fails for large objects.
  DCHECK_GT(AllocatedSize<mode>(), sizeof(HeapObjectHeader));
  return AllocatedSize<mode>() - sizeof(HeapObjectHeader);
}

template <AccessMode mode>
bool HeapObjectHeader::IsLargeObject() const {
  return AllocatedSize<mode>() == kLargeObjectSizeInHeader;
}

template <AccessMode mode>
bool HeapObjectHeader::IsInConstruction() const {
  const uint16_t encoded =
      LoadEncoded<mode, EncodedHalf::kHigh, std::memory_order_acquire>();
  return !FullyConstructedField::decode(encoded);
}

void HeapObjectHeader::MarkAsFullyConstructed() {
  MakeGarbageCollectedTraitInternal::MarkObjectAsFullyConstructed(
      ObjectStart());
}

template <AccessMode mode>
bool HeapObjectHeader::IsMarked() const {
  const uint16_t encoded =
      LoadEncoded<mode, EncodedHalf::kLow, std::memory_order_relaxed>();
  return MarkBitField::decode(encoded);
}

template <AccessMode mode>
void HeapObjectHeader::Unmark() {
  DCHECK(IsMarked<mode>());
  StoreEncoded<mode, EncodedHalf::kLow, std::memory_order_relaxed>(
      MarkBitField::encode(false), MarkBitField::kMask);
}

bool HeapObjectHeader::TryMarkAtomic() {
  auto* atomic_encoded = v8::base::AsAtomicPtr(&encoded_low_);
  uint16_t old_value = atomic_encoded->load(std::memory_order_relaxed);
  const uint16_t new_value = old_value | MarkBitField::encode(true);
  if (new_value == old_value) {
    return false;
  }
  return atomic_encoded->compare_exchange_strong(old_value, new_value,
                                                 std::memory_order_relaxed);
}

void HeapObjectHeader::MarkNonAtomic() {
  DCHECK(!IsMarked<AccessMode::kNonAtomic>());
  encoded_low_ |= MarkBitField::encode(true);
}

template <AccessMode mode>
bool HeapObjectHeader::IsYoung() const {
  return !IsMarked<mode>();
}

template <AccessMode mode>
bool HeapObjectHeader::IsFree() const {
  return GetGCInfoIndex<mode>() == kFreeListGCInfoIndex;
}

bool HeapObjectHeader::IsFinalizable() const {
  const GCInfo& gc_info = GlobalGCInfoTable::GCInfoFromIndex(GetGCInfoIndex());
  return gc_info.finalize;
}

#if defined(CPPGC_CAGED_HEAP)
void HeapObjectHeader::SetNextUnfinalized(HeapObjectHeader* next) {
#if defined(CPPGC_POINTER_COMPRESSION)
  next_unfinalized_ = CompressedPointer::Compress(next);
#else   // !defined(CPPGC_POINTER_COMPRESSION)
  next_unfinalized_ = CagedHeap::OffsetFromAddress<uint32_t>(next);
#endif  // !defined(CPPGC_POINTER_COMPRESSION)
}

HeapObjectHeader* HeapObjectHeader::GetNextUnfinalized(
    uintptr_t cage_base_or_mask) const {
  DCHECK(cage_base_or_mask);
#if defined(CPPGC_POINTER_COMPRESSION)
  DCHECK_EQ(
      api_constants::kCagedHeapReservationAlignment - 1,
      CagedHeap::OffsetFromAddress(reinterpret_cast<void*>(cage_base_or_mask)));
  return reinterpret_cast<HeapObjectHeader*>(
      CompressedPointer::Decompress(next_unfinalized_, cage_base_or_mask));
#else   // !defined(CPPGC_POINTER_COMPRESSION)
  DCHECK_EQ(0, CagedHeap::OffsetFromAddress(
                   reinterpret_cast<void*>(cage_base_or_mask)));
  return next_unfinalized_ ? reinterpret_cast<HeapObjectHeader*>(
                                 cage_base_or_mask + next_unfinalized_)
                           : nullptr;
#endif  // !defined(CPPGC_POINTER_COMPRESSION)
}
#endif  // defined(CPPGC_CAGED_HEAP)

template <AccessMode mode>
void HeapObjectHeader::Trace(Visitor* visitor) const {
  const GCInfo& gc_info =
      GlobalGCInfoTable::GCInfoFromIndex(GetGCInfoIndex<mode>());
  return gc_info.trace(visitor, ObjectStart());
}

template <AccessMode mode, HeapObjectHeader::EncodedHalf part,
          std::memory_order memory_order>
uint16_t HeapObjectHeader::LoadEncoded() const {
  const uint16_t& half =
      part == EncodedHalf::kLow ? encoded_low_ : encoded_high_;
  if (mode == AccessMode::kNonAtomic) return half;
  return v8::base::AsAtomicPtr(&half)->load(memory_order);
}

template <AccessMode mode, HeapObjectHeader::EncodedHalf part,
          std::memory_order memory_order>
void HeapObjectHeader::StoreEncoded(uint16_t bits, uint16_t mask) {
  // Caveat: Not all changes to HeapObjectHeader's bitfields go through
  // StoreEncoded. The following have their own implementations and need to be
  // kept in sync:
  // - HeapObjectHeader::TryMarkAtomic
  // - MarkObjectAsFullyConstructed (API)
  DCHECK_EQ(0u, bits & ~mask);
  uint16_t& half = part == EncodedHalf::kLow ? encoded_low_ : encoded_high_;
  if (mode == AccessMode::kNonAtomic) {
    half = (half & ~mask) | bits;
    return;
  }
  // We don't perform CAS loop here assuming that only none of the info that
  // shares the same encoded halfs change at the same time.
  auto* atomic_encoded = v8::base::AsAtomicPtr(&half);
  uint16_t value = atomic_encoded->load(std::memory_order_relaxed);
  value = (value & ~mask) | bits;
  atomic_encoded->store(value, memory_order);
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_HEAP_OBJECT_HEADER_H_
