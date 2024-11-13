// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_STRING_FORWARDING_TABLE_INL_H_
#define V8_OBJECTS_STRING_FORWARDING_TABLE_INL_H_

#include "src/base/atomicops.h"
#include "src/common/globals.h"
#include "src/heap/safepoint.h"
#include "src/objects/name-inl.h"
#include "src/objects/slots-inl.h"
#include "src/objects/slots.h"
#include "src/objects/string-forwarding-table.h"
#include "src/objects/string-inl.h"
// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class StringForwardingTable::Record final {
 public:
  Tagged<String> original_string(PtrComprCageBase cage_base) const {
    return Cast<String>(OriginalStringObject(cage_base));
  }

  Tagged<String> forward_string(PtrComprCageBase cage_base) const {
    return Cast<String>(ForwardStringObjectOrHash(cage_base));
  }

  inline uint32_t raw_hash(PtrComprCageBase cage_base) const;
  inline v8::String::ExternalStringResourceBase* external_resource(
      bool* is_one_byte) const;

  Tagged<Object> OriginalStringObject(PtrComprCageBase cage_base) const {
    return OriginalStringSlot().Acquire_Load(cage_base);
  }

  Tagged<Object> ForwardStringObjectOrHash(PtrComprCageBase cage_base) const {
    return ForwardStringOrHashSlot().Acquire_Load(cage_base);
  }

  Address ExternalResourceAddress() const {
    return base::AsAtomicPointer::Acquire_Load(&external_resource_);
  }

  void set_original_string(Tagged<Object> object) {
    OriginalStringSlot().Release_Store(object);
  }

  void set_forward_string(Tagged<Object> object) {
    ForwardStringOrHashSlot().Release_Store(object);
  }

  inline void set_raw_hash_if_empty(uint32_t raw_hash);
  inline void set_external_resource(
      v8::String::ExternalStringResourceBase* resource, bool is_one_byte);
  void set_external_resource(Address address) {
    base::AsAtomicPointer::Release_Store(&external_resource_, address);
  }

  inline void SetInternalized(Tagged<String> string, Tagged<String> forward_to);
  inline void SetExternal(Tagged<String> string,
                          v8::String::ExternalStringResourceBase*,
                          bool is_one_byte, uint32_t raw_hash);
  inline bool TryUpdateExternalResource(
      v8::String::ExternalStringResourceBase* resource, bool is_one_byte);
  inline bool TryUpdateExternalResource(Address address);
  inline void DisposeExternalResource();
  // Dispose the external resource if the original string has transitioned
  // to an external string and the resource used for the transition is different
  // than the one in the record.
  inline void DisposeUnusedExternalResource(Tagged<String> original_string);

 private:
  OffHeapObjectSlot OriginalStringSlot() const {
    return OffHeapObjectSlot(&original_string_);
  }

  OffHeapObjectSlot ForwardStringOrHashSlot() const {
    return OffHeapObjectSlot(&forward_string_or_hash_);
  }

  static constexpr intptr_t kExternalResourceIsOneByteTag = 1;
  static constexpr intptr_t kExternalResourceEncodingMask = 1;
  static constexpr intptr_t kExternalResourceAddressMask =
      ~kExternalResourceEncodingMask;

  // Always a pointer to the string that needs to be transitioned.
  Tagged_t original_string_;
  // The field either stores the forward string object, or a raw hash.
  // For strings forwarded to an internalized string (to be converted to a
  // ThinString during GC), this field always contrains the internalized string
  // object.
  // It is guaranteed that only computed hash values (LSB = 0) are stored,
  // therefore a raw hash is distinguishable from a string object by the
  // heap object tag.
  // Raw hashes can be overwritten by forward string objects, whereas
  // forward string objects will never be overwritten once set.
  Tagged_t forward_string_or_hash_;
  // Although this is an external pointer, we are using Address instead of
  // ExternalPointer_t to not have to deal with the ExternalPointerTable.
  // This is OK, as the StringForwardingTable is outside of the V8 sandbox.
  // The LSB is used to indicate whether the external resource is a one-byte
  // (LSB = 1) or two-byte (LSB = 0) external string resource.
  Address external_resource_;

  // Possible string transitions and how they affect the fields of the record:
  // Shared string (not in the table) --> Interalized
  //   forward_string_or_hash_ is set to the internalized string object.
  //   external_resource_ is nullptr.
  // Shared string (not in the table) --> External
  //   forward_string_or_hash_ is set to the computed hash value of the string.
  //   external_resource_ is set to the address of the external resource.
  // Shared string (in the table to be internalized) --> External
  //   forward_string_or_hash_ will not be overwritten. It will still contain
  //   the internalized string object from the previous transition.
  //   external_resource_ is set to the address of the external resource.
  // Shared string (in the table to be made external) --> Internalized
  //   forward_string_or_hash_ (previously contained the computed hash value) is
  //   overwritten with the internalized string object.
  //   external_resource_ is not overwritten (still the external resource).

  friend class StringForwardingTable::Block;
};

uint32_t StringForwardingTable::Record::raw_hash(
    PtrComprCageBase cage_base) const {
  Tagged<Object> hash_or_string = ForwardStringObjectOrHash(cage_base);
  uint32_t raw_hash;
  if (IsHeapObject(hash_or_string)) {
    raw_hash = Cast<String>(hash_or_string)->RawHash();
  } else {
    raw_hash = static_cast<uint32_t>(hash_or_string.ptr());
  }
  DCHECK(Name::IsHashFieldComputed(raw_hash));
  return raw_hash;
}

v8::String::ExternalStringResourceBase*
StringForwardingTable::Record::external_resource(bool* is_one_byte) const {
  Address address = ExternalResourceAddress();
  *is_one_byte = (address & kExternalResourceEncodingMask) ==
                 kExternalResourceIsOneByteTag;
  address &= kExternalResourceAddressMask;
  return reinterpret_cast<v8::String::ExternalStringResourceBase*>(address);
}

void StringForwardingTable::Record::set_raw_hash_if_empty(uint32_t raw_hash) {
  // Assert that computed hash values don't overlap with heap object tag.
  static_assert((kHeapObjectTag & Name::kHashNotComputedMask) != 0);
  DCHECK(Name::IsHashFieldComputed(raw_hash));
  DCHECK_NE(raw_hash & kHeapObjectTagMask, kHeapObjectTag);
  AsAtomicTagged::Release_CompareAndSwap(&forward_string_or_hash_,
                                         unused_element().value(), raw_hash);
}

void StringForwardingTable::Record::set_external_resource(
    v8::String::ExternalStringResourceBase* resource, bool is_one_byte) {
  DCHECK_NOT_NULL(resource);
  Address address = reinterpret_cast<Address>(resource);
  if (is_one_byte && address != kNullAddress) {
    address |= kExternalResourceIsOneByteTag;
  }
  set_external_resource(address);
}

void StringForwardingTable::Record::SetInternalized(Tagged<String> string,
                                                    Tagged<String> forward_to) {
  set_original_string(string);
  set_forward_string(forward_to);
  set_external_resource(kNullExternalPointer);
}

void StringForwardingTable::Record::SetExternal(
    Tagged<String> string, v8::String::ExternalStringResourceBase* resource,
    bool is_one_byte, uint32_t raw_hash) {
  set_original_string(string);
  set_raw_hash_if_empty(raw_hash);
  set_external_resource(resource, is_one_byte);
}

bool StringForwardingTable::Record::TryUpdateExternalResource(
    v8::String::ExternalStringResourceBase* resource, bool is_one_byte) {
  DCHECK_NOT_NULL(resource);
  Address address = reinterpret_cast<Address>(resource);
  if (is_one_byte && address != kNullAddress) {
    address |= kExternalResourceIsOneByteTag;
  }
  return TryUpdateExternalResource(address);
}

bool StringForwardingTable::Record::TryUpdateExternalResource(Address address) {
  static_assert(kNullAddress == kNullExternalPointer);
  // Don't set the external resource if another one is already stored. If we
  // would simply overwrite the resource, the previously stored one would be
  // leaked.
  return base::AsAtomicPointer::AcquireRelease_CompareAndSwap(
             &external_resource_, kNullAddress, address) == kNullAddress;
}

void StringForwardingTable::Record::DisposeExternalResource() {
  bool is_one_byte;
  auto resource = external_resource(&is_one_byte);
  DCHECK_NOT_NULL(resource);
  resource->Dispose();
}

void StringForwardingTable::Record::DisposeUnusedExternalResource(
    Tagged<String> original) {
#ifdef DEBUG
  Tagged<String> stored_original =
      original_string(GetIsolateFromWritableObject(original));
  if (IsThinString(stored_original)) {
    stored_original = Cast<ThinString>(stored_original)->actual();
  }
  DCHECK_EQ(original, stored_original);
#endif
  if (!IsExternalString(original)) return;
  Address original_resource =
      Cast<ExternalString>(original)->resource_as_address();
  bool is_one_byte;
  auto resource = external_resource(&is_one_byte);
  if (resource != nullptr &&
      reinterpret_cast<Address>(resource) != original_resource) {
    resource->Dispose();
  }
}

class StringForwardingTable::Block {
 public:
  static std::unique_ptr<Block> New(int capacity);
  explicit Block(int capacity);
  int capacity() const { return capacity_; }
  void* operator new(size_t size, int capacity);
  void* operator new(size_t size) = delete;
  void operator delete(void* data);

  Record* record(int index) {
    DCHECK_LT(index, capacity());
    return &elements_[index];
  }

  const Record* record(int index) const {
    DCHECK_LT(index, capacity());
    return &elements_[index];
  }

  void UpdateAfterYoungEvacuation(PtrComprCageBase cage_base);
  void UpdateAfterYoungEvacuation(PtrComprCageBase cage_base, int up_to_index);
  void UpdateAfterFullEvacuation(PtrComprCageBase cage_base);
  void UpdateAfterFullEvacuation(PtrComprCageBase cage_base, int up_to_index);

 private:
  const int capacity_;
  Record elements_[1];
};

class StringForwardingTable::BlockVector {
 public:
  using Block = StringForwardingTable::Block;
  using Allocator = std::allocator<Block*>;

  explicit BlockVector(size_t capacity);
  ~BlockVector();
  size_t capacity() const { return capacity_; }

  Block* LoadBlock(size_t index, AcquireLoadTag) {
    DCHECK_LT(index, size());
    return base::AsAtomicPointer::Acquire_Load(&begin_[index]);
  }

  Block* LoadBlock(size_t index) {
    DCHECK_LT(index, size());
    return begin_[index];
  }

  void AddBlock(std::unique_ptr<Block> block) {
    DCHECK_LT(size(), capacity());
    base::AsAtomicPointer::Release_Store(&begin_[size_], block.release());
    size_++;
  }

  static std::unique_ptr<BlockVector> Grow(BlockVector* data, size_t capacity,
                                           const base::Mutex& mutex);

  size_t size() const { return size_; }

 private:
  V8_NO_UNIQUE_ADDRESS Allocator allocator_;
  const size_t capacity_;
  std::atomic<size_t> size_;
  Block** begin_;
};

int StringForwardingTable::size() const { return next_free_index_; }
bool StringForwardingTable::empty() const { return size() == 0; }

// static
uint32_t StringForwardingTable::BlockForIndex(int index,
                                              uint32_t* index_in_block) {
  DCHECK_GE(index, 0);
  DCHECK_NOT_NULL(index_in_block);
  // The block is the leftmost set bit of the index, corrected by the size
  // of the first block.
  const uint32_t block_index =
      kBitsPerInt -
      base::bits::CountLeadingZeros(
          static_cast<uint32_t>(index + kInitialBlockSize)) -
      kInitialBlockSizeHighestBit - 1;
  *index_in_block = IndexInBlock(index, block_index);
  return block_index;
}

// static
uint32_t StringForwardingTable::IndexInBlock(int index, uint32_t block_index) {
  DCHECK_GE(index, 0);
  // Clear out the leftmost set bit (the block index) to get the index within
  // the block.
  return static_cast<uint32_t>(index + kInitialBlockSize) &
         ~(1u << (block_index + kInitialBlockSizeHighestBit));
}

// static
uint32_t StringForwardingTable::CapacityForBlock(uint32_t block_index) {
  return 1u << (block_index + kInitialBlockSizeHighestBit);
}

template <typename Func>
void StringForwardingTable::IterateElements(Func&& callback) {
  if (empty()) return;
  BlockVector* blocks = blocks_.load(std::memory_order_relaxed);
  const uint32_t last_block_index = static_cast<uint32_t>(blocks->size() - 1);
  for (uint32_t block_index = 0; block_index < last_block_index;
       ++block_index) {
    Block* block = blocks->LoadBlock(block_index);
    for (int index = 0; index < block->capacity(); ++index) {
      Record* rec = block->record(index);
      callback(rec);
    }
  }
  // Handle last block separately, as it is not filled to capacity.
  const uint32_t max_index = IndexInBlock(size() - 1, last_block_index) + 1;
  Block* block = blocks->LoadBlock(last_block_index);
  for (uint32_t index = 0; index < max_index; ++index) {
    Record* rec = block->record(index);
    callback(rec);
  }
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_STRING_FORWARDING_TABLE_INL_H_
