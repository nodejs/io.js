// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_READ_ONLY_SPACES_H_
#define V8_HEAP_READ_ONLY_SPACES_H_

#include <memory>
#include <optional>
#include <utility>

#include "include/v8-platform.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/heap/allocation-result.h"
#include "src/heap/allocation-stats.h"
#include "src/heap/base-space.h"
#include "src/heap/heap-verifier.h"
#include "src/heap/memory-chunk-metadata.h"

namespace v8 {
namespace internal {

class MemoryAllocator;
class ReadOnlyHeap;
class SnapshotByteSource;

class ReadOnlyPageMetadata : public MemoryChunkMetadata {
 public:
  ReadOnlyPageMetadata(Heap* heap, BaseSpace* space, size_t chunk_size,
                       Address area_start, Address area_end,
                       VirtualMemory reservation);
  MemoryChunk::MainThreadFlags InitialFlags() const;

  // Clears any pointers in the header that point out of the page that would
  // otherwise make the header non-relocatable.
  void MakeHeaderRelocatable();

  size_t ShrinkToHighWaterMark();

  // Returns the address for a given offset in this page.
  Address OffsetToAddress(size_t offset) const {
    Address address_in_page = ChunkAddress() + offset;
    if (COMPRESS_POINTERS_IN_MULTIPLE_CAGES_BOOL) {
      // Pointer compression with multiple pointer cages and shared
      // ReadOnlyPages means that the area_start and area_end cannot be defined
      // since they are stored within the pages which can be mapped at multiple
      // memory addresses.
      DCHECK_LT(offset, size());
    } else {
      DCHECK_GE(address_in_page, area_start());
      DCHECK_LT(address_in_page, area_end());
    }
    return address_in_page;
  }

  // Returns the start area of the page without using area_start() which cannot
  // return the correct result when the page is remapped multiple times.
  Address GetAreaStart() const {
    return ChunkAddress() +
           MemoryChunkLayout::ObjectStartOffsetInMemoryChunk(RO_SPACE);
  }

 private:
  friend class ReadOnlySpace;
};

// -----------------------------------------------------------------------------
// Artifacts used to construct a new SharedReadOnlySpace
class ReadOnlyArtifacts final {
 public:
  ReadOnlyArtifacts() = default;

  ~ReadOnlyArtifacts();

  // Initialize the ReadOnlyArtifacts from an Isolate that has just been created
  // either by serialization or by creating the objects directly.
  void Initialize(Isolate* isolate, std::vector<ReadOnlyPageMetadata*>&& pages,
                  const AllocationStats& stats);

  // This replaces the ReadOnlySpace in the given Heap with a newly constructed
  // SharedReadOnlySpace that has pages created from the ReadOnlyArtifacts. This
  // is only called for the first Isolate, where the ReadOnlySpace is created
  // during the bootstrap process.
  void ReinstallReadOnlySpace(Isolate* isolate);

  void VerifyHeapAndSpaceRelationships(Isolate* isolate);

  std::vector<ReadOnlyPageMetadata*>& pages() { return pages_; }

  const AllocationStats& accounting_stats() const { return stats_; }

  SharedReadOnlySpace* shared_read_only_space() {
    return shared_read_only_space_.get();
  }

  void set_read_only_heap(std::unique_ptr<ReadOnlyHeap> read_only_heap);
  ReadOnlyHeap* read_only_heap() const { return read_only_heap_.get(); }

  void set_initial_next_unique_sfi_id(uint32_t id) {
    initial_next_unique_sfi_id_ = id;
  }
  uint32_t initial_next_unique_sfi_id() const {
    return initial_next_unique_sfi_id_;
  }

  struct ExternalPointerRegistryEntry {
    ExternalPointerRegistryEntry(ExternalPointerHandle handle, Address value,
                                 ExternalPointerTag tag)
        : handle(handle), value(value), tag(tag) {}
    ExternalPointerHandle handle;
    Address value;
    ExternalPointerTag tag;
  };
  void set_external_pointer_registry(
      std::vector<ExternalPointerRegistryEntry>&& registry) {
    DCHECK(external_pointer_registry_.empty());
    external_pointer_registry_ = std::move(registry);
  }
  const std::vector<ExternalPointerRegistryEntry>& external_pointer_registry()
      const {
    return external_pointer_registry_;
  }

  void InitializeChecksum(SnapshotData* read_only_snapshot_data);
  void VerifyChecksum(SnapshotData* read_only_snapshot_data,
                      bool read_only_heap_created);

 private:
  friend class ReadOnlyHeap;

  std::vector<ReadOnlyPageMetadata*> pages_;
  AllocationStats stats_;
  std::unique_ptr<SharedReadOnlySpace> shared_read_only_space_;
  std::unique_ptr<ReadOnlyHeap> read_only_heap_;
  uint32_t initial_next_unique_sfi_id_ = 0;
  std::vector<ExternalPointerRegistryEntry> external_pointer_registry_;
#ifdef DEBUG
  // The checksum of the blob the read-only heap was deserialized from, if
  // any.
  std::optional<uint32_t> read_only_blob_checksum_;
#endif  // DEBUG
  v8::PageAllocator* page_allocator_ = nullptr;
};

// -----------------------------------------------------------------------------
// Read Only space for all Immortal Immovable and Immutable objects
class ReadOnlySpace : public BaseSpace {
 public:
  V8_EXPORT_PRIVATE explicit ReadOnlySpace(Heap* heap);

  // Detach the pages and add them to artifacts for using in creating a
  // SharedReadOnlySpace. Since the current space no longer has any pages, it
  // should be replaced straight after this in its Heap.
  void DetachPagesAndAddToArtifacts(ReadOnlyArtifacts* artifacts);

  V8_EXPORT_PRIVATE ~ReadOnlySpace() override;
  V8_EXPORT_PRIVATE virtual void TearDown(MemoryAllocator* memory_allocator);

  bool IsDetached() const { return heap_ == nullptr; }

  bool writable() const { return !is_marked_read_only_; }

  bool Contains(Address a) = delete;
  bool Contains(Tagged<Object> o) = delete;

  V8_EXPORT_PRIVATE
  AllocationResult AllocateRaw(int size_in_bytes,
                               AllocationAlignment alignment);

  V8_EXPORT_PRIVATE void ClearStringPaddingIfNeeded();

  enum class SealMode {
    kDetachFromHeap,
    kDetachFromHeapAndUnregisterMemory,
    kDoNotDetachFromHeap
  };

  // Seal the space by marking it read-only, optionally detaching it
  // from the heap and forgetting it for memory bookkeeping purposes (e.g.
  // prevent space's memory from registering as leaked).
  V8_EXPORT_PRIVATE void Seal(SealMode ro_mode);

  // During boot the free_space_map is created, and afterwards we may need
  // to write it into the free space nodes that were already created.
  void RepairFreeSpacesAfterDeserialization();

  size_t Size() const override { return accounting_stats_.Size(); }
  V8_EXPORT_PRIVATE size_t CommittedPhysicalMemory() const override;

  const std::vector<ReadOnlyPageMetadata*>& pages() const { return pages_; }
  Address top() const { return top_; }
  Address limit() const { return limit_; }
  size_t Capacity() const { return capacity_; }

  // Returns the index within pages_. The chunk must be part of this space.
  size_t IndexOf(const MemoryChunkMetadata* chunk) const;

  bool ContainsSlow(Address addr) const;
  V8_EXPORT_PRIVATE void ShrinkPages();

#ifdef VERIFY_HEAP
  void Verify(Isolate* isolate, SpaceVerificationVisitor* visitor) const final;
#ifdef DEBUG
  void VerifyCounters(Heap* heap) const;
#endif  // DEBUG
#endif  // VERIFY_HEAP

  Address FirstPageAddress() const { return pages_.front()->ChunkAddress(); }

  // Ensure the read only space has at least one allocated page
  void EnsurePage();

 protected:
  void SetPermissionsForPages(MemoryAllocator* memory_allocator,
                              PageAllocator::Permission access);

  bool is_marked_read_only_ = false;

  // Accounting information for this space.
  AllocationStats accounting_stats_;

  std::vector<ReadOnlyPageMetadata*> pages_;

  Address top_ = kNullAddress;
  Address limit_ = kNullAddress;

 private:
  AllocationResult AllocateRawUnaligned(int size_in_bytes);
  AllocationResult AllocateRawAligned(int size_in_bytes,
                                      AllocationAlignment alignment);
  Tagged<HeapObject> TryAllocateLinearlyAligned(int size_in_bytes,
                                                AllocationAlignment alignment);

  // Return the index within pages_ of the newly allocated page.
  size_t AllocateNextPage();
  size_t AllocateNextPageAt(Address pos);
  void InitializePageForDeserialization(ReadOnlyPageMetadata* page,
                                        size_t area_size_in_bytes);
  void FinalizeSpaceForDeserialization();

  void EnsureSpaceForAllocation(int size_in_bytes);
  void FreeLinearAllocationArea();

  size_t capacity_ = 0;

  friend class Heap;
  friend class ReadOnlyHeapImageDeserializer;
};

class SharedReadOnlySpace : public ReadOnlySpace {
 public:
  SharedReadOnlySpace(Heap* heap, ReadOnlyArtifacts* artifacts);

  SharedReadOnlySpace(const SharedReadOnlySpace&) = delete;

  void TearDown(MemoryAllocator* memory_allocator) override;

 private:
  explicit SharedReadOnlySpace(Heap* heap) : ReadOnlySpace(heap) {
    is_marked_read_only_ = true;
  }
};

}  // namespace internal

namespace base {
// Define special hash function for page pointers, to be used with std data
// structures, e.g. std::unordered_set<ReadOnlyPageMetadata*,
// base::hash<ReadOnlyPageMetadata*>
template <>
struct hash<i::ReadOnlyPageMetadata*> : hash<i::MemoryChunkMetadata*> {};
template <>
struct hash<const i::ReadOnlyPageMetadata*>
    : hash<const i::MemoryChunkMetadata*> {};
}  // namespace base

}  // namespace v8

#endif  // V8_HEAP_READ_ONLY_SPACES_H_
