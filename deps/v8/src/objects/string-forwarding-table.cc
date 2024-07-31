// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/string-forwarding-table.h"

#include "src/base/atomicops.h"
#include "src/common/globals.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots-inl.h"
#include "src/objects/slots.h"
#include "src/objects/string-forwarding-table-inl.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

StringForwardingTable::Block::Block(int capacity) : capacity_(capacity) {
  static_assert(unused_element().ptr() == 0);
  static_assert(kNullAddress == 0);
  static_assert(sizeof(Record) % sizeof(Address) == 0);
  static_assert(offsetof(Record, original_string_) == 0);
  constexpr int kRecordPointerSize = sizeof(Record) / sizeof(Address);
  MemsetPointer(reinterpret_cast<Address*>(&elements_[0]), 0,
                capacity_ * kRecordPointerSize);
}

void* StringForwardingTable::Block::operator new(size_t size, int capacity) {
  // Make sure the size given is the size of the Block structure.
  DCHECK_EQ(size, sizeof(StringForwardingTable::Block));
  // Make sure the Record class is trivial and has standard layout.
  static_assert(std::is_trivial_v<Record>);
  static_assert(std::is_standard_layout_v<Record>);
  // Make sure that the elements_ array is at the end of Block, with no padding,
  // so that subsequent elements can be accessed as offsets from elements_.
  static_assert(offsetof(StringForwardingTable::Block, elements_) ==
                sizeof(StringForwardingTable::Block) - sizeof(Record));
  // Make sure that elements_ is aligned when StringTable::Block is aligned.
  static_assert((alignof(StringForwardingTable::Block) +
                 offsetof(StringForwardingTable::Block, elements_)) %
                    kTaggedSize ==
                0);

  const size_t elements_size = capacity * sizeof(Record);
  // Storage for the first element is already supplied by elements_, so subtract
  // sizeof(Record).
  const size_t new_size = size + elements_size - sizeof(Record);
  DCHECK_LE(alignof(StringForwardingTable::Block), kSystemPointerSize);
  return AlignedAllocWithRetry(new_size, kSystemPointerSize);
}

void StringForwardingTable::Block::operator delete(void* block) {
  AlignedFree(block);
}

std::unique_ptr<StringForwardingTable::Block> StringForwardingTable::Block::New(
    int capacity) {
  return std::unique_ptr<Block>(new (capacity) Block(capacity));
}

void StringForwardingTable::Block::UpdateAfterYoungEvacuation(
    PtrComprCageBase cage_base) {
  UpdateAfterYoungEvacuation(cage_base, capacity_);
}

void StringForwardingTable::Block::UpdateAfterFullEvacuation(
    PtrComprCageBase cage_base) {
  UpdateAfterFullEvacuation(cage_base, capacity_);
}

namespace {

bool UpdateForwardedSlot(Tagged<HeapObject> object, OffHeapObjectSlot slot) {
  MapWord map_word = object->map_word(kRelaxedLoad);
  if (map_word.IsForwardingAddress()) {
    Tagged<HeapObject> forwarded_object = map_word.ToForwardingAddress(object);
    slot.Release_Store(forwarded_object);
    return true;
  }
  return false;
}

bool UpdateForwardedSlot(Tagged<Object> object, OffHeapObjectSlot slot) {
  if (!IsHeapObject(object)) return false;
  return UpdateForwardedSlot(HeapObject::cast(object), slot);
}

}  // namespace

void StringForwardingTable::Block::UpdateAfterYoungEvacuation(
    PtrComprCageBase cage_base, int up_to_index) {
  for (int index = 0; index < up_to_index; ++index) {
    OffHeapObjectSlot slot = record(index)->OriginalStringSlot();
    Tagged<Object> original = slot.Acquire_Load(cage_base);
    if (!IsHeapObject(original)) continue;
    Tagged<HeapObject> object = HeapObject::cast(original);
    if (Heap::InFromPage(object)) {
      DCHECK(!InWritableSharedSpace(object));
      const bool was_forwarded = UpdateForwardedSlot(object, slot);
      if (!was_forwarded) {
        // The object died in young space.
        slot.Release_Store(deleted_element());
      }
    } else {
      DCHECK(!object->map_word(kRelaxedLoad).IsForwardingAddress());
    }
// No need to update forwarded (internalized) strings as they are never
// in young space.
#ifdef DEBUG
    Tagged<Object> forward =
        record(index)->ForwardStringObjectOrHash(cage_base);
    if (IsHeapObject(forward)) {
      DCHECK(!Heap::InYoungGeneration(HeapObject::cast(forward)));
    }
#endif
  }
}

void StringForwardingTable::Block::UpdateAfterFullEvacuation(
    PtrComprCageBase cage_base, int up_to_index) {
  for (int index = 0; index < up_to_index; ++index) {
    OffHeapObjectSlot original_slot = record(index)->OriginalStringSlot();
    Tagged<Object> original = original_slot.Acquire_Load(cage_base);
    if (!IsHeapObject(original)) continue;
    UpdateForwardedSlot(HeapObject::cast(original), original_slot);
    // During mark compact the forwarded (internalized) string may have been
    // evacuated.
    OffHeapObjectSlot forward_slot = record(index)->ForwardStringOrHashSlot();
    Tagged<Object> forward = forward_slot.Acquire_Load(cage_base);
    UpdateForwardedSlot(forward, forward_slot);
  }
}

StringForwardingTable::BlockVector::BlockVector(size_t capacity)
    : allocator_(Allocator()), capacity_(capacity), size_(0) {
  begin_ = allocator_.allocate(capacity);
}

StringForwardingTable::BlockVector::~BlockVector() {
  allocator_.deallocate(begin_, capacity());
}

// static
std::unique_ptr<StringForwardingTable::BlockVector>
StringForwardingTable::BlockVector::Grow(
    StringForwardingTable::BlockVector* data, size_t capacity,
    const base::Mutex& mutex) {
  mutex.AssertHeld();
  std::unique_ptr<BlockVector> new_data =
      std::make_unique<BlockVector>(capacity);
  // Copy pointers to blocks from the old to the new vector.
  for (size_t i = 0; i < data->size(); i++) {
    new_data->begin_[i] = data->LoadBlock(i);
  }
  new_data->size_ = data->size();
  return new_data;
}

StringForwardingTable::StringForwardingTable(Isolate* isolate)
    : isolate_(isolate), next_free_index_(0) {
  InitializeBlockVector();
}

StringForwardingTable::~StringForwardingTable() {
  BlockVector* blocks = blocks_.load(std::memory_order_relaxed);
  for (uint32_t block_index = 0; block_index < blocks->size(); block_index++) {
    delete blocks->LoadBlock(block_index);
  }
}

void StringForwardingTable::InitializeBlockVector() {
  BlockVector* blocks = block_vector_storage_
                            .emplace_back(std::make_unique<BlockVector>(
                                kInitialBlockVectorCapacity))
                            .get();
  blocks->AddBlock(Block::New(kInitialBlockSize));
  blocks_.store(blocks, std::memory_order_relaxed);
}

StringForwardingTable::BlockVector* StringForwardingTable::EnsureCapacity(
    uint32_t block_index) {
  BlockVector* blocks = blocks_.load(std::memory_order_acquire);
  if (V8_UNLIKELY(block_index >= blocks->size())) {
    base::MutexGuard table_grow_guard(&grow_mutex_);
    // Reload the vector, as another thread could have grown it.
    blocks = blocks_.load(std::memory_order_relaxed);
    // Check again if we need to grow under lock.
    if (block_index >= blocks->size()) {
      // Grow the vector if the block to insert is greater than the vectors
      // capacity.
      if (block_index >= blocks->capacity()) {
        std::unique_ptr<BlockVector> new_blocks =
            BlockVector::Grow(blocks, blocks->capacity() * 2, grow_mutex_);
        block_vector_storage_.push_back(std::move(new_blocks));
        blocks = block_vector_storage_.back().get();
        blocks_.store(blocks, std::memory_order_release);
      }
      const uint32_t capacity = CapacityForBlock(block_index);
      std::unique_ptr<Block> new_block = Block::New(capacity);
      blocks->AddBlock(std::move(new_block));
    }
  }
  return blocks;
}

int StringForwardingTable::AddForwardString(Tagged<String> string,
                                            Tagged<String> forward_to) {
  DCHECK_IMPLIES(!v8_flags.always_use_string_forwarding_table,
                 InAnySharedSpace(string));
  DCHECK_IMPLIES(!v8_flags.always_use_string_forwarding_table,
                 InAnySharedSpace(forward_to));
  int index = next_free_index_++;
  uint32_t index_in_block;
  const uint32_t block_index = BlockForIndex(index, &index_in_block);

  BlockVector* blocks = EnsureCapacity(block_index);
  Block* block = blocks->LoadBlock(block_index, kAcquireLoad);
  block->record(index_in_block)->SetInternalized(string, forward_to);
  return index;
}

void StringForwardingTable::UpdateForwardString(int index,
                                                Tagged<String> forward_to) {
  CHECK_LT(index, size());
  uint32_t index_in_block;
  const uint32_t block_index = BlockForIndex(index, &index_in_block);
  Block* block = blocks_.load(std::memory_order_acquire)
                     ->LoadBlock(block_index, kAcquireLoad);
  block->record(index_in_block)->set_forward_string(forward_to);
}

template <typename T>
int StringForwardingTable::AddExternalResourceAndHash(Tagged<String> string,
                                                      T* resource,
                                                      uint32_t raw_hash) {
  constexpr bool is_one_byte =
      std::is_base_of_v<v8::String::ExternalOneByteStringResource, T>;

  DCHECK_IMPLIES(!v8_flags.always_use_string_forwarding_table,
                 InAnySharedSpace(string));
  int index = next_free_index_++;
  uint32_t index_in_block;
  const uint32_t block_index = BlockForIndex(index, &index_in_block);

  BlockVector* blocks = EnsureCapacity(block_index);
  Block* block = blocks->LoadBlock(block_index, kAcquireLoad);
  block->record(index_in_block)
      ->SetExternal(string, resource, is_one_byte, raw_hash);
  return index;
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) int StringForwardingTable::
    AddExternalResourceAndHash(Tagged<String> string,
                               v8::String::ExternalOneByteStringResource*,
                               uint32_t raw_hash);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) int StringForwardingTable::
    AddExternalResourceAndHash(Tagged<String> string,
                               v8::String::ExternalStringResource*,
                               uint32_t raw_hash);

template <typename T>
bool StringForwardingTable::TryUpdateExternalResource(int index, T* resource) {
  constexpr bool is_one_byte =
      std::is_base_of_v<v8::String::ExternalOneByteStringResource, T>;

  CHECK_LT(index, size());
  uint32_t index_in_block;
  const uint32_t block_index = BlockForIndex(index, &index_in_block);
  Block* block = blocks_.load(std::memory_order_acquire)
                     ->LoadBlock(block_index, kAcquireLoad);
  return block->record(index_in_block)
      ->TryUpdateExternalResource(resource, is_one_byte);
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) bool StringForwardingTable::
    TryUpdateExternalResource(
        int index, v8::String::ExternalOneByteStringResource* resource);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) bool StringForwardingTable::
    TryUpdateExternalResource(int index,
                              v8::String::ExternalStringResource* resource);

Tagged<String> StringForwardingTable::GetForwardString(
    PtrComprCageBase cage_base, int index) const {
  CHECK_LT(index, size());
  uint32_t index_in_block;
  const uint32_t block_index = BlockForIndex(index, &index_in_block);
  Block* block = blocks_.load(std::memory_order_acquire)
                     ->LoadBlock(block_index, kAcquireLoad);
  return block->record(index_in_block)->forward_string(cage_base);
}

// static
Address StringForwardingTable::GetForwardStringAddress(Isolate* isolate,
                                                       int index) {
  return isolate->string_forwarding_table()
      ->GetForwardString(isolate, index)
      .ptr();
}

uint32_t StringForwardingTable::GetRawHash(PtrComprCageBase cage_base,
                                           int index) const {
  CHECK_LT(index, size());
  uint32_t index_in_block;
  const uint32_t block_index = BlockForIndex(index, &index_in_block);
  Block* block = blocks_.load(std::memory_order_acquire)
                     ->LoadBlock(block_index, kAcquireLoad);
  return block->record(index_in_block)->raw_hash(cage_base);
}

// static
uint32_t StringForwardingTable::GetRawHashStatic(Isolate* isolate, int index) {
  return isolate->string_forwarding_table()->GetRawHash(isolate, index);
}

v8::String::ExternalStringResourceBase*
StringForwardingTable::GetExternalResource(int index, bool* is_one_byte) const {
  CHECK_LT(index, size());
  uint32_t index_in_block;
  const uint32_t block_index = BlockForIndex(index, &index_in_block);
  Block* block = blocks_.load(std::memory_order_acquire)
                     ->LoadBlock(block_index, kAcquireLoad);
  return block->record(index_in_block)->external_resource(is_one_byte);
}

void StringForwardingTable::TearDown() {
  std::unordered_set<Address> disposed_resources;
  IterateElements([this, &disposed_resources](Record* record) {
    if (record->OriginalStringObject(isolate_) != deleted_element()) {
      Address resource = record->ExternalResourceAddress();
      if (resource != kNullAddress && disposed_resources.count(resource) == 0) {
        record->DisposeExternalResource();
        disposed_resources.insert(resource);
      }
    }
  });
  Reset();
}

void StringForwardingTable::Reset() {
  isolate_->heap()->safepoint()->AssertActive();
  DCHECK_NE(isolate_->heap()->gc_state(), Heap::NOT_IN_GC);

  BlockVector* blocks = blocks_.load(std::memory_order_relaxed);
  for (uint32_t block_index = 0; block_index < blocks->size(); ++block_index) {
    delete blocks->LoadBlock(block_index);
  }

  block_vector_storage_.clear();
  InitializeBlockVector();
  next_free_index_ = 0;
}

void StringForwardingTable::UpdateAfterYoungEvacuation() {
  // This is only used for the Scavenger.
  DCHECK(!v8_flags.minor_ms);
  DCHECK(v8_flags.always_use_string_forwarding_table);

  if (empty()) return;

  BlockVector* blocks = blocks_.load(std::memory_order_relaxed);
  const unsigned int last_block_index =
      static_cast<unsigned int>(blocks->size() - 1);
  for (unsigned int block_index = 0; block_index < last_block_index;
       ++block_index) {
    Block* block = blocks->LoadBlock(block_index, kAcquireLoad);
    block->UpdateAfterYoungEvacuation(isolate_);
  }
  // Handle last block separately, as it is not filled to capacity.
  const int max_index = IndexInBlock(size() - 1, last_block_index) + 1;
  blocks->LoadBlock(last_block_index, kAcquireLoad)
      ->UpdateAfterYoungEvacuation(isolate_, max_index);
}

void StringForwardingTable::UpdateAfterFullEvacuation() {
  if (empty()) return;

  BlockVector* blocks = blocks_.load(std::memory_order_relaxed);
  const unsigned int last_block_index =
      static_cast<unsigned int>(blocks->size() - 1);
  for (unsigned int block_index = 0; block_index < last_block_index;
       ++block_index) {
    Block* block = blocks->LoadBlock(block_index, kAcquireLoad);
    block->UpdateAfterFullEvacuation(isolate_);
  }
  // Handle last block separately, as it is not filled to capacity.
  const int max_index = IndexInBlock(size() - 1, last_block_index) + 1;
  blocks->LoadBlock(last_block_index, kAcquireLoad)
      ->UpdateAfterFullEvacuation(isolate_, max_index);
}

}  // namespace internal
}  // namespace v8
