// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_ALLOCATOR_H_
#define V8_HEAP_MEMORY_ALLOCATOR_H_

#include <atomic>
#include <memory>
#include <optional>
#include <set>
#include <unordered_set>
#include <utility>

#include "include/v8-platform.h"
#include "src/base/bounded-page-allocator.h"
#include "src/base/export-template.h"
#include "src/base/functional.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/semaphore.h"
#include "src/common/globals.h"
#include "src/heap/code-range.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/spaces.h"
#include "src/tasks/cancelable-task.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

namespace heap {
class TestMemoryAllocatorScope;
}  // namespace heap

class Heap;
class Isolate;
class ReadOnlyPageMetadata;

// ----------------------------------------------------------------------------
// A space acquires chunks of memory from the operating system. The memory
// allocator allocates and deallocates pages for the paged heap spaces and large
// pages for large object space.
class MemoryAllocator {
 public:
  // Pool keeps pages allocated and accessible until explicitly flushed.
  class V8_EXPORT_PRIVATE Pool {
   public:
    explicit Pool(MemoryAllocator* allocator) : allocator_(allocator) {}

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    void Add(MutablePageMetadata* chunk) {
      // This method is called only on the main thread and only during the
      // atomic pause so a lock is not needed.
      DCHECK_NOT_NULL(chunk);
      DCHECK_EQ(chunk->size(), PageMetadata::kPageSize);
      DCHECK(!chunk->Chunk()->IsLargePage());
      DCHECK(!chunk->Chunk()->IsTrusted());
      DCHECK_NE(chunk->Chunk()->executable(), EXECUTABLE);
      chunk->ReleaseAllAllocatedMemory();
      pooled_chunks_.push_back(chunk);
    }

    MutablePageMetadata* TryGetPooled() {
      base::MutexGuard guard(&mutex_);
      if (pooled_chunks_.empty()) return nullptr;
      MutablePageMetadata* chunk = pooled_chunks_.back();
      pooled_chunks_.pop_back();
      return chunk;
    }

    void ReleasePooledChunks();

    size_t NumberOfCommittedChunks() const;
    size_t CommittedBufferedMemory() const;

   private:
    MemoryAllocator* const allocator_;
    std::vector<MutablePageMetadata*> pooled_chunks_;
    mutable base::Mutex mutex_;

    friend class MemoryAllocator;
  };

  enum class AllocationMode {
    // Regular allocation path. Does not use pool.
    kRegular,

    // Uses the pool for allocation first.
    kUsePool,
  };

  enum class FreeMode {
    // Frees page immediately on the main thread.
    kImmediately,

    // Postpone freeing, until MemoryAllocator::ReleaseQueuedPages() is called.
    // This is used in the major GC to allow the pointer-update phase to touch
    // dead memory.
    kPostpone,

    // Pool page.
    kPool,
  };

  // Initialize page sizes field in V8::Initialize.
  static void InitializeOncePerProcess();

  V8_INLINE static intptr_t GetCommitPageSize() {
    DCHECK_LT(0, commit_page_size_);
    return commit_page_size_;
  }

  V8_INLINE static intptr_t GetCommitPageSizeBits() {
    DCHECK_LT(0, commit_page_size_bits_);
    return commit_page_size_bits_;
  }

  // Computes the memory area of discardable memory within a given memory area
  // [addr, addr+size) and returns the result as base::AddressRegion. If the
  // memory is not discardable base::AddressRegion is an empty region.
  V8_EXPORT_PRIVATE static base::AddressRegion ComputeDiscardMemoryArea(
      Address addr, size_t size);

  V8_EXPORT_PRIVATE MemoryAllocator(Isolate* isolate,
                                    v8::PageAllocator* code_page_allocator,
                                    v8::PageAllocator* trusted_page_allocator,
                                    size_t max_capacity);

  V8_EXPORT_PRIVATE void TearDown();

  // Allocates a Page from the allocator. AllocationMode is used to indicate
  // whether pooled allocation, which only works for MemoryChunk::kPageSize,
  // should be tried first.
  V8_EXPORT_PRIVATE PageMetadata* AllocatePage(
      MemoryAllocator::AllocationMode alloc_mode, Space* space,
      Executability executable);

  V8_EXPORT_PRIVATE LargePageMetadata* AllocateLargePage(
      LargeObjectSpace* space, size_t object_size, Executability executable);

  ReadOnlyPageMetadata* AllocateReadOnlyPage(ReadOnlySpace* space,
                                             Address hint = kNullAddress);

  std::unique_ptr<::v8::PageAllocator::SharedMemoryMapping> RemapSharedPage(
      ::v8::PageAllocator::SharedMemory* shared_memory, Address new_address);

  V8_EXPORT_PRIVATE void Free(MemoryAllocator::FreeMode mode,
                              MutablePageMetadata* chunk);
  void FreeReadOnlyPage(ReadOnlyPageMetadata* chunk);

  // Returns allocated spaces in bytes.
  size_t Size() const { return size_; }

  // Returns allocated executable spaces in bytes.
  size_t SizeExecutable() const { return size_executable_; }

  // Returns the maximum available bytes of heaps.
  size_t Available() const {
    const size_t size = Size();
    return capacity_ < size ? 0 : capacity_ - size;
  }

  // Returns an indication of whether a pointer is in a space that has
  // been allocated by this MemoryAllocator. It is conservative, allowing
  // false negatives (i.e., if a pointer is outside the allocated space, it may
  // return false) but not false positives (i.e., if a pointer is inside the
  // allocated space, it will definitely return false).
  V8_INLINE bool IsOutsideAllocatedSpace(Address address) const {
    return IsOutsideAllocatedSpace(address, NOT_EXECUTABLE) &&
           IsOutsideAllocatedSpace(address, EXECUTABLE);
  }
  V8_INLINE bool IsOutsideAllocatedSpace(Address address,
                                         Executability executable) const {
    switch (executable) {
      case NOT_EXECUTABLE:
        return address < lowest_not_executable_ever_allocated_ ||
               address >= highest_not_executable_ever_allocated_;
      case EXECUTABLE:
        return address < lowest_executable_ever_allocated_ ||
               address >= highest_executable_ever_allocated_;
    }
  }

  // Partially release |bytes_to_free| bytes starting at |start_free|. Note that
  // internally memory is freed from |start_free| to the end of the reservation.
  // Additional memory beyond the page is not accounted though, so
  // |bytes_to_free| is computed by the caller.
  void PartialFreeMemory(MemoryChunkMetadata* chunk, Address start_free,
                         size_t bytes_to_free, Address new_area_end);

#ifdef DEBUG
  // Checks if an allocated MemoryChunk was intended to be used for executable
  // memory.
  bool IsMemoryChunkExecutable(MutablePageMetadata* chunk) {
    base::MutexGuard guard(&executable_memory_mutex_);
    return executable_memory_.find(chunk) != executable_memory_.end();
  }
#endif  // DEBUG

  // Page allocator instance for allocating non-executable pages.
  // Guaranteed to be a valid pointer.
  v8::PageAllocator* data_page_allocator() { return data_page_allocator_; }

  // Page allocator instance for allocating executable pages.
  // Guaranteed to be a valid pointer.
  v8::PageAllocator* code_page_allocator() { return code_page_allocator_; }

  // Page allocator instance for allocating "trusted" pages. When the sandbox is
  // enabled, these pages are guaranteed to be allocated outside of the sandbox,
  // so their content cannot be corrupted by an attacker.
  // Guaranteed to be a valid pointer.
  v8::PageAllocator* trusted_page_allocator() {
    return trusted_page_allocator_;
  }

  // Returns page allocator suitable for allocating pages for the given space.
  v8::PageAllocator* page_allocator(AllocationSpace space) {
    switch (space) {
      case CODE_SPACE:
      case CODE_LO_SPACE:
        return code_page_allocator_;
      case TRUSTED_SPACE:
      case SHARED_TRUSTED_SPACE:
      case TRUSTED_LO_SPACE:
      case SHARED_TRUSTED_LO_SPACE:
        return trusted_page_allocator_;
      default:
        return data_page_allocator_;
    }
  }

  Pool* pool() { return &pool_; }

  void UnregisterReadOnlyPage(ReadOnlyPageMetadata* page);

  Address HandleAllocationFailure(Executability executable);

#if defined(V8_ENABLE_CONSERVATIVE_STACK_SCANNING) || defined(DEBUG)
  // Return the normal or large page that contains this address, if it is owned
  // by this heap, otherwise a nullptr.
  V8_EXPORT_PRIVATE const MemoryChunk* LookupChunkContainingAddress(
      Address addr) const;
#endif  // V8_ENABLE_CONSERVATIVE_STACK_SCANNING || DEBUG

  // Insert and remove normal and large pages that are owned by this heap.
  void RecordMemoryChunkCreated(const MemoryChunk* chunk);
  void RecordMemoryChunkDestroyed(const MemoryChunk* chunk);

  // We postpone page freeing until the pointer-update phase is done (updating
  // slots may happen for dead objects which point to dead memory).
  void ReleaseQueuedPages();

 private:
  // Used to store all data about MemoryChunk allocation, e.g. in
  // AllocateUninitializedChunk.
  struct MemoryChunkAllocationResult {
    void* chunk;
    // If we reuse a pooled chunk return the metadata allocation here to be
    // reused.
    void* optional_metadata;
    size_t size;
    size_t area_start;
    size_t area_end;
    VirtualMemory reservation;
  };

  // Computes the size of a MemoryChunk from the size of the object_area.
  static size_t ComputeChunkSize(size_t area_size, AllocationSpace space);

  // Internal allocation method for all pages/memory chunks. Returns data about
  // the unintialized memory region.
  V8_WARN_UNUSED_RESULT std::optional<MemoryChunkAllocationResult>
  AllocateUninitializedChunk(BaseSpace* space, size_t area_size,
                             Executability executable, PageSize page_size) {
    return AllocateUninitializedChunkAt(space, area_size, executable,
                                        kNullAddress, page_size);
  }
  V8_WARN_UNUSED_RESULT std::optional<MemoryChunkAllocationResult>
  AllocateUninitializedChunkAt(BaseSpace* space, size_t area_size,
                               Executability executable, Address hint,
                               PageSize page_size);

  // Internal raw allocation method that allocates an aligned MemoryChunk and
  // sets the right memory permissions.
  Address AllocateAlignedMemory(size_t chunk_size, size_t area_size,
                                size_t alignment, AllocationSpace space,
                                Executability executable, void* hint,
                                VirtualMemory* controller);

  // Commit memory region owned by given reservation object.  Returns true if
  // it succeeded and false otherwise.
  bool CommitMemory(VirtualMemory* reservation, Executability executable);

  // Sets memory permissions on executable memory chunks. This entails page
  // header (RW), guard pages (no access) and the object area (code modification
  // permissions).
  V8_WARN_UNUSED_RESULT bool SetPermissionsOnExecutableMemoryChunk(
      VirtualMemory* vm, Address start, size_t reserved_size);

  // Disallows any access on memory region owned by given reservation object.
  // Returns true if it succeeded and false otherwise.
  bool UncommitMemory(VirtualMemory* reservation);

  // Frees the given memory region.
  void FreeMemoryRegion(v8::PageAllocator* page_allocator, Address addr,
                        size_t size);

  // PreFreeMemory logically frees the object, i.e., it unregisters the
  // memory, logs a delete event and adds the chunk to remembered unmapped
  // pages.
  void PreFreeMemory(MutablePageMetadata* chunk);

  // PerformFreeMemory can be called concurrently when PreFree was executed
  // before.
  void PerformFreeMemory(MutablePageMetadata* chunk);

  // See AllocatePage for public interface. Note that currently we only
  // support pools for NOT_EXECUTABLE pages of size MemoryChunk::kPageSize.
  std::optional<MemoryChunkAllocationResult> AllocateUninitializedPageFromPool(
      Space* space);

  // Initializes pages in a chunk. Returns the first page address.
  // This function and GetChunkId() are provided for the mark-compact
  // collector to rebuild page headers in the from space, which is
  // used as a marking stack and its page headers are destroyed.
  PageMetadata* InitializePagesInChunk(int chunk_id, int pages_in_chunk,
                                       PagedSpace* space);

  void UpdateAllocatedSpaceLimits(Address low, Address high,
                                  Executability executable) {
    // The use of atomic primitives does not guarantee correctness (wrt.
    // desired semantics) by default. The loop here ensures that we update the
    // values only if they did not change in between.
    Address ptr;
    switch (executable) {
      case NOT_EXECUTABLE:
        ptr = lowest_not_executable_ever_allocated_.load(
            std::memory_order_relaxed);
        while ((low < ptr) &&
               !lowest_not_executable_ever_allocated_.compare_exchange_weak(
                   ptr, low, std::memory_order_acq_rel)) {
        }
        ptr = highest_not_executable_ever_allocated_.load(
            std::memory_order_relaxed);
        while ((high > ptr) &&
               !highest_not_executable_ever_allocated_.compare_exchange_weak(
                   ptr, high, std::memory_order_acq_rel)) {
        }
        break;
      case EXECUTABLE:
        ptr = lowest_executable_ever_allocated_.load(std::memory_order_relaxed);
        while ((low < ptr) &&
               !lowest_executable_ever_allocated_.compare_exchange_weak(
                   ptr, low, std::memory_order_acq_rel)) {
        }
        ptr =
            highest_executable_ever_allocated_.load(std::memory_order_relaxed);
        while ((high > ptr) &&
               !highest_executable_ever_allocated_.compare_exchange_weak(
                   ptr, high, std::memory_order_acq_rel)) {
        }
        break;
    }
  }

  // Performs all necessary bookkeeping to free the memory, but does not free
  // it.
  void UnregisterMutableMemoryChunk(MutablePageMetadata* chunk);
  void UnregisterSharedMemoryChunk(MemoryChunkMetadata* chunk);
  void UnregisterMemoryChunk(MemoryChunkMetadata* chunk,
                             Executability executable = NOT_EXECUTABLE);

  void RegisterReadOnlyMemory(ReadOnlyPageMetadata* page);

#ifdef DEBUG
  void RegisterExecutableMemoryChunk(MutablePageMetadata* chunk) {
    base::MutexGuard guard(&executable_memory_mutex_);
    DCHECK(chunk->Chunk()->IsFlagSet(MemoryChunk::IS_EXECUTABLE));
    DCHECK_EQ(executable_memory_.find(chunk), executable_memory_.end());
    executable_memory_.insert(chunk);
  }

  void UnregisterExecutableMemoryChunk(MutablePageMetadata* chunk) {
    base::MutexGuard guard(&executable_memory_mutex_);
    DCHECK_NE(executable_memory_.find(chunk), executable_memory_.end());
    executable_memory_.erase(chunk);
  }
#endif  // DEBUG

  Isolate* isolate_;

  // Page allocator used for allocating data pages. Depending on the
  // configuration it may be a page allocator instance provided by v8::Platform
  // or a BoundedPageAllocator (when pointer compression is enabled).
  v8::PageAllocator* data_page_allocator_;

  // Page allocator used for allocating code pages. Depending on the
  // configuration it may be a page allocator instance provided by v8::Platform
  // or a BoundedPageAllocator from Heap::code_range_ (when pointer compression
  // is enabled or on those 64-bit architectures where pc-relative 32-bit
  // displacement can be used for call and jump instructions).
  v8::PageAllocator* code_page_allocator_;

  // Page allocator used for allocating trusted pages. When the sandbox is
  // enabled, trusted pages are allocated outside of the sandbox so that their
  // content cannot be corrupted by an attacker. When the sandbox is disabled,
  // this is the same as data_page_allocator_.
  v8::PageAllocator* trusted_page_allocator_;

  // Maximum space size in bytes.
  size_t capacity_;

  // Allocated space size in bytes.
  std::atomic<size_t> size_ = 0;
  // Allocated executable space size in bytes.
  std::atomic<size_t> size_executable_ = 0;

  // We keep the lowest and highest addresses allocated as a quick way
  // of determining that pointers are outside the heap. The estimate is
  // conservative, i.e. not all addresses in 'allocated' space are allocated
  // to our heap. The range is [lowest, highest[, inclusive on the low end
  // and exclusive on the high end. Addresses are distinguished between
  // executable and not-executable, as they may generally be placed in distinct
  // areas of the heap.
  std::atomic<Address> lowest_not_executable_ever_allocated_{
      static_cast<Address>(-1ll)};
  std::atomic<Address> highest_not_executable_ever_allocated_{kNullAddress};
  std::atomic<Address> lowest_executable_ever_allocated_{
      static_cast<Address>(-1ll)};
  std::atomic<Address> highest_executable_ever_allocated_{kNullAddress};

  std::optional<VirtualMemory> reserved_chunk_at_virtual_memory_limit_;
  Pool pool_;
  std::vector<MutablePageMetadata*> queued_pages_to_be_freed_;

#ifdef DEBUG
  // Data structure to remember allocated executable memory chunks.
  // This data structure is used only in DCHECKs.
  std::unordered_set<MutablePageMetadata*, base::hash<MutablePageMetadata*>>
      executable_memory_;
  base::Mutex executable_memory_mutex_;
#endif  // DEBUG

#if defined(V8_ENABLE_CONSERVATIVE_STACK_SCANNING) || defined(DEBUG)
  // Allocated normal and large pages are stored here, to be used during
  // conservative stack scanning.
  std::unordered_set<const MemoryChunk*, base::hash<const MemoryChunk*>>
      normal_pages_;
  std::set<const MemoryChunk*> large_pages_;

  mutable base::Mutex chunks_mutex_;
#endif  // V8_ENABLE_CONSERVATIVE_STACK_SCANNING || DEBUG

  V8_EXPORT_PRIVATE static size_t commit_page_size_;
  V8_EXPORT_PRIVATE static size_t commit_page_size_bits_;

  friend class heap::TestCodePageAllocatorScope;
  friend class heap::TestMemoryAllocatorScope;

  DISALLOW_IMPLICIT_CONSTRUCTORS(MemoryAllocator);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_ALLOCATOR_H_
