// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-allocator.h"

#include <cinttypes>

#include "src/base/address-region.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/flags/flags.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/mutable-page.h"
#include "src/heap/read-only-spaces.h"
#include "src/heap/zapping.h"
#include "src/logging/log.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// MemoryAllocator
//

size_t MemoryAllocator::commit_page_size_ = 0;
size_t MemoryAllocator::commit_page_size_bits_ = 0;

MemoryAllocator::MemoryAllocator(Isolate* isolate,
                                 v8::PageAllocator* code_page_allocator,
                                 v8::PageAllocator* trusted_page_allocator,
                                 size_t capacity)
    : isolate_(isolate),
      data_page_allocator_(isolate->page_allocator()),
      code_page_allocator_(code_page_allocator),
      trusted_page_allocator_(trusted_page_allocator),
      capacity_(RoundUp(capacity, PageMetadata::kPageSize)),
      pool_(this) {
  DCHECK_NOT_NULL(data_page_allocator_);
  DCHECK_NOT_NULL(code_page_allocator_);
  DCHECK_NOT_NULL(trusted_page_allocator_);
}

void MemoryAllocator::TearDown() {
  pool()->ReleasePooledChunks();

  // Check that spaces were torn down before MemoryAllocator.
  DCHECK_EQ(size_, 0u);
  // TODO(gc) this will be true again when we fix FreeMemory.
  // DCHECK_EQ(0, size_executable_);
  capacity_ = 0;

  if (reserved_chunk_at_virtual_memory_limit_) {
    reserved_chunk_at_virtual_memory_limit_->Free();
  }

  code_page_allocator_ = nullptr;
  data_page_allocator_ = nullptr;
  trusted_page_allocator_ = nullptr;
}

void MemoryAllocator::Pool::ReleasePooledChunks() {
  std::vector<MutablePageMetadata*> copied_pooled;
  {
    base::MutexGuard guard(&mutex_);
    std::swap(copied_pooled, pooled_chunks_);
  }
  for (auto* chunk : copied_pooled) {
    DCHECK_NOT_NULL(chunk);
    VirtualMemory* reservation = chunk->reserved_memory();
    DCHECK(reservation->IsReserved());
    reservation->Free();
  }
}

size_t MemoryAllocator::Pool::NumberOfCommittedChunks() const {
  base::MutexGuard guard(&mutex_);
  return pooled_chunks_.size();
}

int MemoryAllocator::Pool::NumberOfChunks() const {
  base::MutexGuard guard(&mutex_);
  return static_cast<int>(pooled_chunks_.size());
}

size_t MemoryAllocator::Pool::CommittedBufferedMemory() const {
  return NumberOfCommittedChunks() * PageMetadata::kPageSize;
}

bool MemoryAllocator::CommitMemory(VirtualMemory* reservation,
                                   Executability executable) {
  Address base = reservation->address();
  size_t size = reservation->size();
  if (!reservation->SetPermissions(base, size, PageAllocator::kReadWrite)) {
    return false;
  }
  UpdateAllocatedSpaceLimits(base, base + size, executable);
  return true;
}

bool MemoryAllocator::UncommitMemory(VirtualMemory* reservation) {
  size_t size = reservation->size();
  if (!reservation->SetPermissions(reservation->address(), size,
                                   PageAllocator::kNoAccess)) {
    return false;
  }
  return true;
}

void MemoryAllocator::FreeMemoryRegion(v8::PageAllocator* page_allocator,
                                       Address base, size_t size) {
  FreePages(page_allocator, reinterpret_cast<void*>(base), size);
}

Address MemoryAllocator::AllocateAlignedMemory(
    size_t chunk_size, size_t area_size, size_t alignment,
    AllocationSpace space, Executability executable, void* hint,
    VirtualMemory* controller) {
  DCHECK_EQ(space == CODE_SPACE || space == CODE_LO_SPACE,
            executable == EXECUTABLE);
  v8::PageAllocator* page_allocator = this->page_allocator(space);
  DCHECK_LT(area_size, chunk_size);

  VirtualMemory reservation(page_allocator, chunk_size, hint, alignment);
  if (!reservation.IsReserved()) return HandleAllocationFailure(executable);

  // We cannot use the last chunk in the address space because we would
  // overflow when comparing top and limit if this chunk is used for a
  // linear allocation area.
  if ((reservation.address() + static_cast<Address>(chunk_size)) == 0u) {
    CHECK(!reserved_chunk_at_virtual_memory_limit_);
    reserved_chunk_at_virtual_memory_limit_ = std::move(reservation);
    CHECK(reserved_chunk_at_virtual_memory_limit_);

    // Retry reserve virtual memory.
    reservation = VirtualMemory(page_allocator, chunk_size, hint, alignment);
    if (!reservation.IsReserved()) return HandleAllocationFailure(executable);
  }

  Address base = reservation.address();

  if (executable == EXECUTABLE) {
    if (!SetPermissionsOnExecutableMemoryChunk(&reservation, base, area_size,
                                               chunk_size)) {
      return HandleAllocationFailure(EXECUTABLE);
    }
  } else {
    // No guard page between page header and object area. This allows us to make
    // all OS pages for both regions readable+writable at once.
    const size_t commit_size = ::RoundUp(
        MemoryChunkLayout::ObjectStartOffsetInMemoryChunk(space) + area_size,
        GetCommitPageSize());

    if (reservation.SetPermissions(base, commit_size,
                                   PageAllocator::kReadWrite)) {
      UpdateAllocatedSpaceLimits(base, base + commit_size, NOT_EXECUTABLE);
    } else {
      return HandleAllocationFailure(NOT_EXECUTABLE);
    }
  }

  *controller = std::move(reservation);
  return base;
}

Address MemoryAllocator::HandleAllocationFailure(Executability executable) {
  Heap* heap = isolate_->heap();
  if (!heap->deserialization_complete()) {
    heap->FatalProcessOutOfMemory(
        executable == EXECUTABLE
            ? "Executable MemoryChunk allocation failed during deserialization."
            : "MemoryChunk allocation failed during deserialization.");
  }
  return kNullAddress;
}

size_t MemoryAllocator::ComputeChunkSize(size_t area_size,
                                         AllocationSpace space,
                                         Executability executable) {
  if (executable == EXECUTABLE) {
    //
    //             Executable
    // +----------------------------+<- base aligned at MemoryChunk::kAlignment
    // |           Header           |
    // +----------------------------+<- base + CodePageGuardStartOffset
    // |           Guard            |
    // +----------------------------+<- area_start_
    // |           Area             |
    // +----------------------------+<- area_end_ (area_start + area_size)
    // |   Committed but not used   |
    // +----------------------------+<- aligned at OS page boundary
    // |           Guard            |
    // +----------------------------+<- base + chunk_size
    //

    return ::RoundUp(MemoryChunkLayout::ObjectStartOffsetInCodePage() +
                         area_size + MemoryChunkLayout::CodePageGuardSize(),
                     GetCommitPageSize());
  }

  //
  //           Non-executable
  // +----------------------------+<- base aligned at MemoryChunk::kAlignment
  // |          Header            |
  // +----------------------------+<- area_start_ (base + area_start_)
  // |           Area             |
  // +----------------------------+<- area_end_ (area_start + area_size)
  // |  Committed but not used    |
  // +----------------------------+<- base + chunk_size
  //
  DCHECK_EQ(executable, NOT_EXECUTABLE);

  return ::RoundUp(
      MemoryChunkLayout::ObjectStartOffsetInMemoryChunk(space) + area_size,
      GetCommitPageSize());
}

base::Optional<MemoryAllocator::MemoryChunkAllocationResult>
MemoryAllocator::AllocateUninitializedChunkAt(BaseSpace* space,
                                              size_t area_size,
                                              Executability executable,
                                              Address hint,
                                              PageSize page_size) {
#ifndef V8_COMPRESS_POINTERS
  // When pointer compression is enabled, spaces are expected to be at a
  // predictable address (see mkgrokdump) so we don't supply a hint and rely on
  // the deterministic behaviour of the BoundedPageAllocator.
  if (hint == kNullAddress) {
    hint = reinterpret_cast<Address>(
        AlignedAddress(isolate_->heap()->GetRandomMmapAddr(),
                       MemoryChunk::GetAlignmentForAllocation()));
  }
#endif

  VirtualMemory reservation;
  size_t chunk_size =
      ComputeChunkSize(area_size, space->identity(), executable);
  DCHECK_EQ(chunk_size % GetCommitPageSize(), 0);

  Address base = AllocateAlignedMemory(
      chunk_size, area_size, MemoryChunk::GetAlignmentForAllocation(),
      space->identity(), executable, reinterpret_cast<void*>(hint),
      &reservation);
  if (base == kNullAddress) return {};

  size_ += reservation.size();

  // Update executable memory size.
  if (executable == EXECUTABLE) {
    size_executable_ += reservation.size();
  }

  if (heap::ShouldZapGarbage()) {
    if (executable == EXECUTABLE) {
      // Page header and object area is split by guard page. Zap page header
      // first.
      heap::ZapBlock(base, MemoryChunkLayout::CodePageGuardStartOffset(),
                     kZapValue);
      // Now zap object area.
      Address code_start =
          base + MemoryChunkLayout::ObjectPageOffsetInCodePage();
      CodePageMemoryModificationScopeForDebugging memory_write_scope(
          isolate_->heap(), &reservation,
          base::AddressRegion(code_start,
                              RoundUp(area_size, GetCommitPageSize())));
      heap::ZapBlock(base + MemoryChunkLayout::ObjectPageOffsetInCodePage(),
                     area_size, kZapValue);
    } else {
      DCHECK_EQ(executable, NOT_EXECUTABLE);
      // Zap both page header and object area at once. No guard page in-between.
      heap::ZapBlock(
          base,
          MemoryChunkLayout::ObjectStartOffsetInMemoryChunk(space->identity()) +
              area_size,
          kZapValue);
    }
  }

  LOG(isolate_,
      NewEvent("MemoryChunk", reinterpret_cast<void*>(base), chunk_size));

  Address area_start = base + MemoryChunkLayout::ObjectStartOffsetInMemoryChunk(
                                  space->identity());
  Address area_end = area_start + area_size;

  return MemoryChunkAllocationResult{
      reinterpret_cast<void*>(base), chunk_size, area_start, area_end,
      std::move(reservation),
  };
}

void MemoryAllocator::PartialFreeMemory(MemoryChunkMetadata* chunk,
                                        Address start_free,
                                        size_t bytes_to_free,
                                        Address new_area_end) {
  VirtualMemory* reservation = chunk->reserved_memory();
  DCHECK(reservation->IsReserved());
  chunk->set_size(chunk->size() - bytes_to_free);
  chunk->set_area_end(new_area_end);
  if (chunk->Chunk()->IsFlagSet(MemoryChunk::IS_EXECUTABLE)) {
    // Add guard page at the end.
    size_t page_size = GetCommitPageSize();
    DCHECK_EQ(0, chunk->area_end() % static_cast<Address>(page_size));
    DCHECK_EQ(chunk->ChunkAddress() + chunk->size(),
              chunk->area_end() + MemoryChunkLayout::CodePageGuardSize());

    if (V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT && !isolate_->jitless()) {
      DCHECK(isolate_->RequiresCodeRange());
      reservation->DiscardSystemPages(chunk->area_end(), page_size);
    } else {
      CHECK(reservation->SetPermissions(chunk->area_end(), page_size,
                                        PageAllocator::kNoAccess));
    }
  }
  // On e.g. Windows, a reservation may be larger than a page and releasing
  // partially starting at |start_free| will also release the potentially
  // unused part behind the current page.
  const size_t released_bytes = reservation->Release(start_free);
  DCHECK_GE(size_, released_bytes);
  size_ -= released_bytes;
}

void MemoryAllocator::UnregisterSharedBasicMemoryChunk(
    MemoryChunkMetadata* chunk) {
  VirtualMemory* reservation = chunk->reserved_memory();
  const size_t size =
      reservation->IsReserved() ? reservation->size() : chunk->size();
  DCHECK_GE(size_, static_cast<size_t>(size));
  size_ -= size;
}

void MemoryAllocator::UnregisterBasicMemoryChunk(MemoryChunkMetadata* chunk,
                                                 Executability executable) {
  DCHECK(!chunk->Chunk()->IsFlagSet(MemoryChunk::UNREGISTERED));
  VirtualMemory* reservation = chunk->reserved_memory();
  const size_t size =
      reservation->IsReserved() ? reservation->size() : chunk->size();
  DCHECK_GE(size_, static_cast<size_t>(size));

  size_ -= size;
  if (executable == EXECUTABLE) {
    DCHECK_GE(size_executable_, size);
    size_executable_ -= size;
#ifdef DEBUG
    UnregisterExecutableMemoryChunk(static_cast<MutablePageMetadata*>(chunk));
#endif  // DEBUG

    Address executable_page_start =
        chunk->ChunkAddress() + MemoryChunkLayout::ObjectPageOffsetInCodePage();
    size_t aligned_area_size =
        RoundUp(chunk->area_end() - executable_page_start, GetCommitPageSize());
    ThreadIsolation::UnregisterJitPage(executable_page_start,
                                       aligned_area_size);
  }
  chunk->Chunk()->SetFlag(MemoryChunk::UNREGISTERED);
}

void MemoryAllocator::UnregisterMemoryChunk(MutablePageMetadata* chunk) {
  UnregisterBasicMemoryChunk(chunk, chunk->Chunk()->executable());
}

void MemoryAllocator::UnregisterReadOnlyPage(ReadOnlyPageMetadata* page) {
  DCHECK(!page->Chunk()->executable());
  UnregisterBasicMemoryChunk(page, NOT_EXECUTABLE);
}

void MemoryAllocator::FreeReadOnlyPage(ReadOnlyPageMetadata* chunk) {
  DCHECK(!chunk->Chunk()->IsFlagSet(MemoryChunk::PRE_FREED));
  LOG(isolate_, DeleteEvent("MemoryChunk", chunk));

  UnregisterSharedBasicMemoryChunk(chunk);

  v8::PageAllocator* allocator = page_allocator(RO_SPACE);
  VirtualMemory* reservation = chunk->reserved_memory();
  if (reservation->IsReserved()) {
    reservation->FreeReadOnly();
  } else {
    // Only read-only pages can have a non-initialized reservation object. This
    // happens when the pages are remapped to multiple locations and where the
    // reservation would therefore be invalid.
    FreeMemoryRegion(allocator, chunk->ChunkAddress(),
                     RoundUp(chunk->size(), allocator->AllocatePageSize()));
  }
}

void MemoryAllocator::PreFreeMemory(MutablePageMetadata* chunk_metadata) {
  MemoryChunk* chunk = chunk_metadata->Chunk();
  DCHECK(!chunk->IsFlagSet(MemoryChunk::PRE_FREED));
  LOG(isolate_, DeleteEvent("MemoryChunk", chunk_metadata));
  UnregisterMemoryChunk(chunk_metadata);
  isolate_->heap()->RememberUnmappedPage(
      reinterpret_cast<Address>(chunk_metadata),
      chunk->IsEvacuationCandidate());
  chunk->SetFlag(MemoryChunk::PRE_FREED);
}

void MemoryAllocator::PerformFreeMemory(MutablePageMetadata* chunk_metadata) {
  MemoryChunk* chunk = chunk_metadata->Chunk();
  base::Optional<CodePageHeaderModificationScope> rwx_write_scope;
  if (chunk->executable() == EXECUTABLE) {
    rwx_write_scope.emplace(
        "We are going to modify the chunk's header, so ensure we have write "
        "access to Code page headers");
  }

  DCHECK(chunk->IsFlagSet(MemoryChunk::UNREGISTERED));
  DCHECK(chunk->IsFlagSet(MemoryChunk::PRE_FREED));
  DCHECK(!chunk->InReadOnlySpace());
  chunk_metadata->ReleaseAllAllocatedMemory();

  VirtualMemory* reservation = chunk_metadata->reserved_memory();
  DCHECK(reservation->IsReserved());
  reservation->Free();
}

void MemoryAllocator::Free(MemoryAllocator::FreeMode mode,
                           MutablePageMetadata* chunk_metadata) {
  MemoryChunk* chunk = chunk_metadata->Chunk();
  RecordMemoryChunkDestroyed(chunk);

  switch (mode) {
    case FreeMode::kImmediately:
      PreFreeMemory(chunk_metadata);
      PerformFreeMemory(chunk_metadata);
      break;
    case FreeMode::kPostpone:
      PreFreeMemory(chunk_metadata);
      // Record page to be freed later.
      queued_pages_to_be_freed_.push_back(chunk_metadata);
      break;
    case FreeMode::kPool:
      DCHECK_EQ(chunk_metadata->size(),
                static_cast<size_t>(MutablePageMetadata::kPageSize));
      DCHECK_EQ(chunk->executable(), NOT_EXECUTABLE);
      PreFreeMemory(chunk_metadata);
      // The chunks added to this queue will be cached until memory reducing GC.
      pool()->Add(chunk_metadata);
      break;
  }
}

PageMetadata* MemoryAllocator::AllocatePage(
    MemoryAllocator::AllocationMode alloc_mode, Space* space,
    Executability executable) {
  const size_t size =
      MemoryChunkLayout::AllocatableMemoryInMemoryChunk(space->identity());
  base::Optional<MemoryChunkAllocationResult> chunk_info;
  if (alloc_mode == AllocationMode::kUsePool) {
    DCHECK_EQ(executable, NOT_EXECUTABLE);
    chunk_info = AllocateUninitializedPageFromPool(space);
  }

  if (!chunk_info) {
    chunk_info =
        AllocateUninitializedChunk(space, size, executable, PageSize::kRegular);
  }

  if (!chunk_info) return nullptr;

  PageMetadata* page = new (chunk_info->start) PageMetadata(
      isolate_->heap(), space, chunk_info->size, chunk_info->area_start,
      chunk_info->area_end, std::move(chunk_info->reservation), executable);

  MemoryChunk* chunk = page->Chunk();

#ifdef DEBUG
  if (chunk->executable()) RegisterExecutableMemoryChunk(page);
#endif  // DEBUG

  space->InitializePage(page);
  RecordMemoryChunkCreated(chunk);
  return page;
}

ReadOnlyPageMetadata* MemoryAllocator::AllocateReadOnlyPage(
    ReadOnlySpace* space, Address hint) {
  DCHECK_EQ(space->identity(), RO_SPACE);
  size_t size = MemoryChunkLayout::AllocatableMemoryInMemoryChunk(RO_SPACE);
  base::Optional<MemoryChunkAllocationResult> chunk_info =
      AllocateUninitializedChunkAt(space, size, NOT_EXECUTABLE, hint,
                                   PageSize::kRegular);
  if (!chunk_info) return nullptr;
  return new (chunk_info->start) ReadOnlyPageMetadata(
      isolate_->heap(), space, chunk_info->size, chunk_info->area_start,
      chunk_info->area_end, std::move(chunk_info->reservation));
}

std::unique_ptr<::v8::PageAllocator::SharedMemoryMapping>
MemoryAllocator::RemapSharedPage(
    ::v8::PageAllocator::SharedMemory* shared_memory, Address new_address) {
  return shared_memory->RemapTo(reinterpret_cast<void*>(new_address));
}

LargePageMetadata* MemoryAllocator::AllocateLargePage(
    LargeObjectSpace* space, size_t object_size, Executability executable) {
  base::Optional<MemoryChunkAllocationResult> chunk_info =
      AllocateUninitializedChunk(space, object_size, executable,
                                 PageSize::kLarge);

  if (!chunk_info) return nullptr;

  LargePageMetadata* page = new (chunk_info->start) LargePageMetadata(
      isolate_->heap(), space, chunk_info->size, chunk_info->area_start,
      chunk_info->area_end, std::move(chunk_info->reservation), executable);
  MemoryChunk* chunk = page->Chunk();

#ifdef DEBUG
  if (chunk->executable()) RegisterExecutableMemoryChunk(page);
#endif  // DEBUG

  RecordMemoryChunkCreated(chunk);
  return page;
}

base::Optional<MemoryAllocator::MemoryChunkAllocationResult>
MemoryAllocator::AllocateUninitializedPageFromPool(Space* space) {
  MemoryChunkMetadata* chunk = pool()->TryGetPooled();
  if (chunk == nullptr) return {};
  const int size = MutablePageMetadata::kPageSize;
  const Address start = chunk->ChunkAddress();
  const Address area_start =
      start +
      MemoryChunkLayout::ObjectStartOffsetInMemoryChunk(space->identity());
  const Address area_end = start + size;
  // Pooled pages are always regular data pages.
  DCHECK_NE(CODE_SPACE, space->identity());
  DCHECK_NE(TRUSTED_SPACE, space->identity());
  VirtualMemory reservation(data_page_allocator(), start, size);
  if (heap::ShouldZapGarbage()) {
    heap::ZapBlock(start, size, kZapValue);
  }

  size_ += size;
  return MemoryChunkAllocationResult{
      chunk, size, area_start, area_end, std::move(reservation),
  };
}

void MemoryAllocator::InitializeOncePerProcess() {
  commit_page_size_ = v8_flags.v8_os_page_size > 0
                          ? v8_flags.v8_os_page_size * KB
                          : CommitPageSize();
  CHECK(base::bits::IsPowerOfTwo(commit_page_size_));
  commit_page_size_bits_ = base::bits::WhichPowerOfTwo(commit_page_size_);
}

base::AddressRegion MemoryAllocator::ComputeDiscardMemoryArea(Address addr,
                                                              size_t size) {
  size_t page_size = GetCommitPageSize();
  if (size < page_size + FreeSpace::kSize) {
    return base::AddressRegion(0, 0);
  }
  Address discardable_start = RoundUp(addr + FreeSpace::kSize, page_size);
  Address discardable_end = RoundDown(addr + size, page_size);
  if (discardable_start >= discardable_end) return base::AddressRegion(0, 0);
  return base::AddressRegion(discardable_start,
                             discardable_end - discardable_start);
}

bool MemoryAllocator::SetPermissionsOnExecutableMemoryChunk(VirtualMemory* vm,
                                                            Address start,
                                                            size_t area_size,
                                                            size_t chunk_size) {
  const size_t page_size = GetCommitPageSize();

  // The code area starts at an offset on the first page. To calculate the page
  // aligned size of the area, we have to add that offset and then round up to
  // commit page size.
  size_t area_offset = MemoryChunkLayout::ObjectStartOffsetInCodePage() -
                       MemoryChunkLayout::ObjectPageOffsetInCodePage();
  size_t aligned_area_size = RoundUp(area_offset + area_size, page_size);

  // All addresses and sizes must be aligned to the commit page size.
  DCHECK(IsAligned(start, page_size));
  DCHECK_EQ(0, chunk_size % page_size);

  const size_t guard_size = MemoryChunkLayout::CodePageGuardSize();
  const size_t pre_guard_offset = MemoryChunkLayout::CodePageGuardStartOffset();
  const size_t code_area_offset =
      MemoryChunkLayout::ObjectPageOffsetInCodePage();

  DCHECK_EQ(pre_guard_offset + guard_size + aligned_area_size + guard_size,
            chunk_size);

  const Address pre_guard_page = start + pre_guard_offset;
  const Address code_area = start + code_area_offset;
  const Address post_guard_page = start + chunk_size - guard_size;

  bool jitless = isolate_->jitless();

  ThreadIsolation::RegisterJitPage(code_area, aligned_area_size);

  if (V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT && !jitless) {
    DCHECK(isolate_->RequiresCodeRange());
    // Commit the header, from start to pre-code guard page.
    // We have to commit it as executable becase otherwise we'll not be able
    // to change permissions to anything else.
    if (vm->RecommitPages(start, pre_guard_offset,
                          PageAllocator::kReadWriteExecute)) {
      // Create the pre-code guard page, following the header.
      if (vm->DiscardSystemPages(pre_guard_page, page_size)) {
        // Commit the executable code body.
        if (vm->RecommitPages(code_area, aligned_area_size,
                              PageAllocator::kReadWriteExecute)) {
          // Create the post-code guard page.
          if (vm->DiscardSystemPages(post_guard_page, page_size)) {
            UpdateAllocatedSpaceLimits(start, code_area + aligned_area_size,
                                       EXECUTABLE);
            return true;
          }

          vm->DiscardSystemPages(code_area, aligned_area_size);
        }
      }
      vm->DiscardSystemPages(start, pre_guard_offset);
    }

  } else {
    // Commit the non-executable header, from start to pre-code guard page.
    if (vm->SetPermissions(start, pre_guard_offset,
                           PageAllocator::kReadWrite)) {
      // Create the pre-code guard page, following the header.
      if (vm->SetPermissions(pre_guard_page, page_size,
                             PageAllocator::kNoAccess)) {
        // Commit the executable code body.
        bool set_permission_successed = false;
        if (ThreadIsolation::Enabled()) {
          DCHECK(!jitless);
          set_permission_successed =
              ThreadIsolation::MakeExecutable(code_area, aligned_area_size);

        } else {
          set_permission_successed = vm->SetPermissions(
              code_area, aligned_area_size,
              jitless ? PageAllocator::kReadWrite
                      : MutablePageMetadata::GetCodeModificationPermission());
        }
        if (set_permission_successed) {
          // Create the post-code guard page.
          if (vm->SetPermissions(post_guard_page, page_size,
                                 PageAllocator::kNoAccess)) {
            UpdateAllocatedSpaceLimits(start, code_area + aligned_area_size,
                                       EXECUTABLE);
            return true;
          }

          CHECK(vm->SetPermissions(code_area, aligned_area_size,
                                   PageAllocator::kNoAccess));
        }
      }
      CHECK(vm->SetPermissions(start, pre_guard_offset,
                               PageAllocator::kNoAccess));
    }
  }

  ThreadIsolation::UnregisterJitPage(code_area, aligned_area_size);
  return false;
}

#if defined(V8_ENABLE_CONSERVATIVE_STACK_SCANNING) || defined(DEBUG)

const MemoryChunk* MemoryAllocator::LookupChunkContainingAddress(
    Address addr) const {
  // All threads should be either parked or in a safepoint whenever this method
  // is called, thus pages cannot be allocated or freed at the same time and a
  // mutex is not required here.
  // As the address may not correspond to a valid heap object, the chunk we
  // obtain below is not necessarily a valid chunk.
  MemoryChunk* chunk = MemoryChunk::FromAddress(addr);
  // Check if it corresponds to a known normal or large page.
  if (auto it = normal_pages_.find(chunk); it != normal_pages_.end()) {
    // The chunk is a normal page.
    // auto* normal_page = PageMetadata::cast(chunk);
    DCHECK_LE((*it)->address(), addr);
    if (chunk->Metadata()->Contains(addr)) return chunk;
  } else if (auto it = large_pages_.upper_bound(chunk);
             it != large_pages_.begin()) {
    // The chunk could be inside a large page.
    DCHECK_IMPLIES(it != large_pages_.end(), addr < (*it)->address());
    auto* large_page_chunk = *std::next(it, -1);
    DCHECK_NOT_NULL(large_page_chunk);
    DCHECK_LE(large_page_chunk->address(), addr);
    if (large_page_chunk->Metadata()->Contains(addr)) return large_page_chunk;
  }
  // Not found in any page.
  return nullptr;
}

#endif  // V8_ENABLE_CONSERVATIVE_STACK_SCANNING || DEBUG

void MemoryAllocator::RecordMemoryChunkCreated(const MemoryChunk* chunk) {
#ifdef V8_ENABLE_CONSERVATIVE_STACK_SCANNING
  base::MutexGuard guard(&chunks_mutex_);
  if (chunk->IsLargePage()) {
    auto result = large_pages_.insert(chunk);
    USE(result);
    DCHECK(result.second);
  } else {
    auto result = normal_pages_.insert(chunk);
    USE(result);
    DCHECK(result.second);
  }

#endif  // V8_ENABLE_CONSERVATIVE_STACK_SCANNING
}

void MemoryAllocator::RecordMemoryChunkDestroyed(const MemoryChunk* chunk) {
#ifdef V8_ENABLE_CONSERVATIVE_STACK_SCANNING
  base::MutexGuard guard(&chunks_mutex_);
  if (chunk->IsLargePage()) {
    auto size = large_pages_.erase(chunk);
    USE(size);
    DCHECK_EQ(1u, size);
  } else {
    auto size = normal_pages_.erase(chunk);
    USE(size);
    DCHECK_EQ(1u, size);
  }
#endif  // V8_ENABLE_CONSERVATIVE_STACK_SCANNING
}

void MemoryAllocator::ReleaseQueuedPages() {
  for (auto* chunk : queued_pages_to_be_freed_) {
    PerformFreeMemory(chunk);
  }
  queued_pages_to_be_freed_.clear();
}

}  // namespace internal
}  // namespace v8
