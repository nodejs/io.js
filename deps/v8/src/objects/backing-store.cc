// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/backing-store.h"

#include <cstring>
#include <optional>

#include "src/base/bits.h"
#include "src/execution/isolate.h"
#include "src/handles/global-handles.h"
#include "src/logging/counters.h"
#include "src/sandbox/sandbox.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#endif  // V8_ENABLE_WEBASSEMBLY

#define TRACE_BS(...)                                      \
  do {                                                     \
    if (v8_flags.trace_backing_store) PrintF(__VA_ARGS__); \
  } while (false)

namespace v8::internal {

namespace {

#if V8_ENABLE_WEBASSEMBLY && V8_TARGET_ARCH_64_BIT
constexpr size_t kFullGuardSize32 = uint64_t{8} * GB;
constexpr size_t kFullGuardSize64 = uint64_t{32} * GB;
#endif

std::atomic<uint32_t> next_backing_store_id_{1};

// Allocation results are reported to UMA
//
// See wasm_memory_allocation_result in counters-definitions.h
enum class AllocationStatus {
  kSuccess,  // Succeeded on the first try

  kSuccessAfterRetry,  // Succeeded after garbage collection

  kAddressSpaceLimitReachedFailure,  // Failed because Wasm is at its address
                                     // space limit

  kOtherFailure  // Failed for an unknown reason
};

base::AddressRegion GetReservedRegion(bool has_guard_regions,
                                      bool is_wasm_memory64, void* buffer_start,
                                      size_t byte_capacity) {
#if V8_ENABLE_WEBASSEMBLY && V8_TARGET_ARCH_64_BIT
  if (has_guard_regions) {
    Address start = reinterpret_cast<Address>(buffer_start);
    DCHECK_EQ(8, sizeof(size_t));  // only use on 64-bit
    DCHECK_EQ(0, start % AllocatePageSize());
    size_t guard_size = kFullGuardSize32;
    if (is_wasm_memory64) {
      DCHECK(v8_flags.wasm_memory64_trap_handling);
      static_assert(kFullGuardSize64 ==
                    2 * wasm::kV8MaxWasmMemory64Pages * wasm::kWasmPageSize);
      DCHECK_LE(byte_capacity,
                wasm::kV8MaxWasmMemory64Pages * wasm::kWasmPageSize);
      guard_size =
          1ULL << wasm::WasmMemory::GetMemory64GuardsShift(byte_capacity);
    }
    return base::AddressRegion(start, guard_size);
  }
#endif

  DCHECK(!has_guard_regions);
  return base::AddressRegion(reinterpret_cast<Address>(buffer_start),
                             byte_capacity);
}

size_t GetReservationSize(bool has_guard_regions, size_t byte_capacity,
                          bool is_wasm_memory64) {
#if V8_TARGET_ARCH_64_BIT && V8_ENABLE_WEBASSEMBLY
  if (has_guard_regions) {
    if (is_wasm_memory64) {
      DCHECK(v8_flags.wasm_memory64_trap_handling);
      static_assert(kFullGuardSize64 ==
                    2 * wasm::kV8MaxWasmMemory64Pages * wasm::kWasmPageSize);
      DCHECK_LE(byte_capacity,
                wasm::kV8MaxWasmMemory64Pages * wasm::kWasmPageSize);
      return 1ULL << wasm::WasmMemory::GetMemory64GuardsShift(byte_capacity);
    } else {
      static_assert(kFullGuardSize32 >= size_t{4} * GB);
      DCHECK_LE(byte_capacity, size_t{4} * GB);
      return kFullGuardSize32;
    }
  }
#else
  DCHECK(!has_guard_regions);
#endif

  return byte_capacity;
}

void RecordStatus(Isolate* isolate, AllocationStatus status) {
  isolate->counters()->wasm_memory_allocation_result()->AddSample(
      static_cast<int>(status));
}

}  // namespace

// The backing store for a Wasm shared memory remembers all the isolates
// with which it has been shared.
struct SharedWasmMemoryData {
  std::vector<Isolate*> isolates_;
};

BackingStore::BackingStore(void* buffer_start, size_t byte_length,
                           size_t max_byte_length, size_t byte_capacity,
                           SharedFlag shared, ResizableFlag resizable,
                           bool is_wasm_memory, bool is_wasm_memory64,
                           bool has_guard_regions, bool custom_deleter,
                           bool empty_deleter)
    : buffer_start_(buffer_start),
      byte_length_(byte_length),
      max_byte_length_(max_byte_length),
      byte_capacity_(byte_capacity),
      id_(next_backing_store_id_.fetch_add(1)),
      is_shared_(shared == SharedFlag::kShared),
      is_resizable_by_js_(resizable == ResizableFlag::kResizable),
      is_wasm_memory_(is_wasm_memory),
      is_wasm_memory64_(is_wasm_memory64),
      holds_shared_ptr_to_allocator_(false),
      has_guard_regions_(has_guard_regions),
      globally_registered_(false),
      custom_deleter_(custom_deleter),
      empty_deleter_(empty_deleter) {
  // TODO(v8:11111): RAB / GSAB - Wasm integration.
  DCHECK_IMPLIES(is_wasm_memory_, !is_resizable_by_js_);
  DCHECK_IMPLIES(is_resizable_by_js_, !custom_deleter_);
  DCHECK_IMPLIES(!is_wasm_memory && !is_resizable_by_js_,
                 byte_length_ == max_byte_length_);
  DCHECK_GE(max_byte_length_, byte_length_);
  DCHECK_GE(byte_capacity_, max_byte_length_);
  // TODO(1445003): Demote to a DCHECK once we found the issue.
  // Wasm memory should never be empty (== zero capacity). Otherwise
  // {JSArrayBuffer::Attach} would replace it by the {EmptyBackingStore} and we
  // loose information.
  // This is particularly important for shared Wasm memory.
  CHECK_IMPLIES(is_wasm_memory_, byte_capacity_ != 0);
}

BackingStore::~BackingStore() {
  GlobalBackingStoreRegistry::Unregister(this);

  struct ClearSharedAllocator {
    BackingStore* const bs;

    ~ClearSharedAllocator() {
      if (!bs->holds_shared_ptr_to_allocator_) return;
      bs->type_specific_data_.v8_api_array_buffer_allocator_shared
          .std::shared_ptr<v8::ArrayBuffer::Allocator>::~shared_ptr();
    }
  } clear_shared_allocator{this};

  if (buffer_start_ == nullptr) return;

  auto FreeResizableMemory = [this] {
    DCHECK(!custom_deleter_);
    DCHECK(is_resizable_by_js_ || is_wasm_memory_);
    auto region = GetReservedRegion(has_guard_regions_, is_wasm_memory64_,
                                    buffer_start_, byte_capacity_);

    PageAllocator* page_allocator = GetArrayBufferPageAllocator();
    if (!region.is_empty()) {
      FreePages(page_allocator, reinterpret_cast<void*>(region.begin()),
                region.size());
    }
  };

#if V8_ENABLE_WEBASSEMBLY
  if (is_wasm_memory_) {
    // TODO(v8:11111): RAB / GSAB - Wasm integration.
    DCHECK(!is_resizable_by_js_);
    size_t reservation_size = GetReservationSize(
        has_guard_regions_, byte_capacity_, is_wasm_memory64_);
    TRACE_BS(
        "BSw:free  bs=%p mem=%p (length=%zu, capacity=%zu, reservation=%zu)\n",
        this, buffer_start_, byte_length(), byte_capacity_, reservation_size);
    if (is_shared_) {
      // Deallocate the list of attached memory objects.
      SharedWasmMemoryData* shared_data = get_shared_wasm_memory_data();
      delete shared_data;
    }
    // Wasm memories are always allocated through the page allocator.
    FreeResizableMemory();
    return;
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  if (is_resizable_by_js_) {
    FreeResizableMemory();
    return;
  }

  if (custom_deleter_) {
    TRACE_BS("BS:custom deleter bs=%p mem=%p (length=%zu, capacity=%zu)\n",
             this, buffer_start_, byte_length(), byte_capacity_);
    type_specific_data_.deleter.callback(buffer_start_, byte_length_,
                                         type_specific_data_.deleter.data);
    return;
  }

  // JSArrayBuffer backing store. Deallocate through the embedder's allocator.
  auto allocator = get_v8_api_array_buffer_allocator();
  TRACE_BS("BS:free   bs=%p mem=%p (length=%zu, capacity=%zu)\n", this,
           buffer_start_, byte_length(), byte_capacity_);
  allocator->Free(buffer_start_, byte_length_);
}

// Allocate a backing store using the array buffer allocator from the embedder.
std::unique_ptr<BackingStore> BackingStore::Allocate(
    Isolate* isolate, size_t byte_length, SharedFlag shared,
    InitializedFlag initialized) {
  void* buffer_start = nullptr;
  auto allocator = isolate->array_buffer_allocator();
  CHECK_NOT_NULL(allocator);
  if (byte_length != 0) {
    auto counters = isolate->counters();
    int mb_length = static_cast<int>(byte_length / MB);
    if (mb_length > 0) {
      counters->array_buffer_big_allocations()->AddSample(mb_length);
    }
    if (shared == SharedFlag::kShared) {
      counters->shared_array_allocations()->AddSample(mb_length);
    }
    auto allocate_buffer = [allocator, initialized](size_t byte_length) {
      if (initialized == InitializedFlag::kUninitialized) {
        return allocator->AllocateUninitialized(byte_length);
      }
      return allocator->Allocate(byte_length);
    };

    buffer_start = isolate->heap()->AllocateExternalBackingStore(
        allocate_buffer, byte_length);

    if (buffer_start == nullptr) {
      // Allocation failed.
      counters->array_buffer_new_size_failures()->AddSample(mb_length);
      return {};
    }
#ifdef V8_ENABLE_SANDBOX
    // Check to catch use of a non-sandbox-compatible ArrayBufferAllocator.
    CHECK_WITH_MSG(GetProcessWideSandbox()->Contains(buffer_start),
                   "When the V8 Sandbox is enabled, ArrayBuffer backing stores "
                   "must be allocated inside the sandbox address space. Please "
                   "use an appropriate ArrayBuffer::Allocator to allocate "
                   "these buffers, or disable the sandbox.");
#endif
  }

  auto result = new BackingStore(buffer_start,                  // start
                                 byte_length,                   // length
                                 byte_length,                   // max length
                                 byte_length,                   // capacity
                                 shared,                        // shared
                                 ResizableFlag::kNotResizable,  // resizable
                                 false,   // is_wasm_memory
                                 false,   // is_wasm_memory64
                                 false,   // has_guard_regions
                                 false,   // custom_deleter
                                 false);  // empty_deleter

  TRACE_BS("BS:alloc  bs=%p mem=%p (length=%zu)\n", result,
           result->buffer_start(), byte_length);
  result->SetAllocatorFromIsolate(isolate);
  return std::unique_ptr<BackingStore>(result);
}

void BackingStore::SetAllocatorFromIsolate(Isolate* isolate) {
  if (auto allocator_shared = isolate->array_buffer_allocator_shared()) {
    holds_shared_ptr_to_allocator_ = true;
    new (&type_specific_data_.v8_api_array_buffer_allocator_shared)
        std::shared_ptr<v8::ArrayBuffer::Allocator>(
            std::move(allocator_shared));
  } else {
    type_specific_data_.v8_api_array_buffer_allocator =
        isolate->array_buffer_allocator();
  }
}

std::unique_ptr<BackingStore> BackingStore::TryAllocateAndPartiallyCommitMemory(
    Isolate* isolate, size_t byte_length, size_t max_byte_length,
    size_t page_size, size_t initial_pages, size_t maximum_pages,
    WasmMemoryFlag wasm_memory, SharedFlag shared) {
  // Enforce engine limitation on the maximum number of pages.
  if (maximum_pages > std::numeric_limits<size_t>::max() / page_size) {
    return nullptr;
  }

  // Cannot reserve 0 pages on some OSes.
  if (maximum_pages == 0) maximum_pages = 1;

  TRACE_BS("BSw:try   %zu pages, %zu max\n", initial_pages, maximum_pages);

#if V8_ENABLE_WEBASSEMBLY
  bool is_wasm_memory64 = wasm_memory == WasmMemoryFlag::kWasmMemory64;
  bool guards = trap_handler::IsTrapHandlerEnabled() &&
                (wasm_memory == WasmMemoryFlag::kWasmMemory32 ||
                 (is_wasm_memory64 && v8_flags.wasm_memory64_trap_handling));
#else
  CHECK_EQ(WasmMemoryFlag::kNotWasm, wasm_memory);
  constexpr bool is_wasm_memory64 = false;
  constexpr bool guards = false;
#endif  // V8_ENABLE_WEBASSEMBLY

  // For accounting purposes, whether a GC was necessary.
  bool did_retry = false;

  // A helper to try running a function up to 3 times, executing a GC
  // if the first and second attempts failed.
  auto gc_retry = [&](const std::function<bool()>& fn) {
    for (int i = 0; i < 3; i++) {
      if (fn()) return true;
      // Collect garbage and retry.
      did_retry = true;
      if (isolate != nullptr) {
        isolate->heap()->MemoryPressureNotification(
            MemoryPressureLevel::kCritical, true);
      }
    }
    return false;
  };

  size_t byte_capacity = maximum_pages * page_size;
  size_t reservation_size =
      GetReservationSize(guards, byte_capacity, is_wasm_memory64);

  //--------------------------------------------------------------------------
  // Allocate pages (inaccessible by default).
  //--------------------------------------------------------------------------
  void* allocation_base = nullptr;
  PageAllocator* page_allocator = GetArrayBufferPageAllocator();
  auto allocate_pages = [&] {
    allocation_base = AllocatePages(page_allocator, nullptr, reservation_size,
                                    page_size, PageAllocator::kNoAccess);
    return allocation_base != nullptr;
  };
  if (!gc_retry(allocate_pages)) {
    // Page allocator could not reserve enough pages.
    if (isolate != nullptr) {
      RecordStatus(isolate, AllocationStatus::kOtherFailure);
    }
    TRACE_BS("BSw:try   failed to allocate pages\n");
    return {};
  }

  uint8_t* buffer_start = reinterpret_cast<uint8_t*>(allocation_base);

  //--------------------------------------------------------------------------
  // Commit the initial pages (allow read/write).
  //--------------------------------------------------------------------------
  size_t committed_byte_length = initial_pages * page_size;
  auto commit_memory = [&] {
    return committed_byte_length == 0 ||
           SetPermissions(page_allocator, buffer_start, committed_byte_length,
                          PageAllocator::kReadWrite);
  };
  if (!gc_retry(commit_memory)) {
    TRACE_BS("BSw:try   failed to set permissions (%p, %zu)\n", buffer_start,
             committed_byte_length);
    FreePages(page_allocator, allocation_base, reservation_size);
    // SetPermissions put us over the process memory limit.
    // We return an empty result so that the caller can throw an exception.
    return {};
  }

  if (isolate != nullptr) {
    RecordStatus(isolate, did_retry ? AllocationStatus::kSuccessAfterRetry
                                    : AllocationStatus::kSuccess);
  }

  const bool is_wasm_memory = wasm_memory != WasmMemoryFlag::kNotWasm;
  ResizableFlag resizable =
      is_wasm_memory ? ResizableFlag::kNotResizable : ResizableFlag::kResizable;

  auto result = new BackingStore(buffer_start,      // start
                                 byte_length,       // length
                                 max_byte_length,   // max_byte_length
                                 byte_capacity,     // capacity
                                 shared,            // shared
                                 resizable,         // resizable
                                 is_wasm_memory,    // is_wasm_memory
                                 is_wasm_memory64,  // is_wasm_memory64
                                 guards,            // has_guard_regions
                                 false,             // custom_deleter
                                 false);            // empty_deleter

  TRACE_BS(
      "BSw:alloc bs=%p mem=%p (length=%zu, capacity=%zu, reservation=%zu)\n",
      result, result->buffer_start(), byte_length, byte_capacity,
      reservation_size);

  return std::unique_ptr<BackingStore>(result);
}

#if V8_ENABLE_WEBASSEMBLY
// Allocate a backing store for a Wasm memory. Always use the page allocator
// and add guard regions.
std::unique_ptr<BackingStore> BackingStore::AllocateWasmMemory(
    Isolate* isolate, size_t initial_pages, size_t maximum_pages,
    WasmMemoryFlag wasm_memory, SharedFlag shared) {
  // Wasm pages must be a multiple of the allocation page size.
  DCHECK_EQ(0, wasm::kWasmPageSize % AllocatePageSize());
  DCHECK_LE(initial_pages, maximum_pages);

  DCHECK(wasm_memory == WasmMemoryFlag::kWasmMemory32 ||
         wasm_memory == WasmMemoryFlag::kWasmMemory64);

  auto TryAllocate = [isolate, initial_pages, wasm_memory,
                      shared](size_t maximum_pages) {
    auto result = TryAllocateAndPartiallyCommitMemory(
        isolate, initial_pages * wasm::kWasmPageSize,
        maximum_pages * wasm::kWasmPageSize, wasm::kWasmPageSize, initial_pages,
        maximum_pages, wasm_memory, shared);
    if (result && shared == SharedFlag::kShared) {
      result->type_specific_data_.shared_wasm_memory_data =
          new SharedWasmMemoryData();
    }
    return result;
  };
  auto backing_store = TryAllocate(maximum_pages);
  if (!backing_store && maximum_pages - initial_pages >= 4) {
    // Retry with smaller maximum pages at each retry.
    auto delta = (maximum_pages - initial_pages) / 4;
    size_t sizes[] = {maximum_pages - delta, maximum_pages - 2 * delta,
                      maximum_pages - 3 * delta, initial_pages};

    for (size_t reduced_maximum_pages : sizes) {
      backing_store = TryAllocate(reduced_maximum_pages);
      if (backing_store) break;
    }
  }
  return backing_store;
}

std::unique_ptr<BackingStore> BackingStore::CopyWasmMemory(
    Isolate* isolate, size_t new_pages, size_t max_pages,
    WasmMemoryFlag wasm_memory) {
  // Note that we could allocate uninitialized to save initialization cost here,
  // but since Wasm memories are allocated by the page allocator, the zeroing
  // cost is already built-in.
  auto new_backing_store = BackingStore::AllocateWasmMemory(
      isolate, new_pages, max_pages, wasm_memory,
      is_shared() ? SharedFlag::kShared : SharedFlag::kNotShared);

  if (!new_backing_store ||
      new_backing_store->has_guard_regions() != has_guard_regions_) {
    return {};
  }

  if (byte_length_ > 0) {
    // If the allocation was successful, then the new buffer must be at least
    // as big as the old one.
    DCHECK_GE(new_pages * wasm::kWasmPageSize, byte_length_);
    memcpy(new_backing_store->buffer_start(), buffer_start_, byte_length_);
  }

  return new_backing_store;
}

// Try to grow the size of a wasm memory in place, without realloc + copy.
std::optional<size_t> BackingStore::GrowWasmMemoryInPlace(Isolate* isolate,
                                                          size_t delta_pages,
                                                          size_t max_pages) {
  // This function grows wasm memory by
  // * changing the permissions of additional {delta_pages} pages to kReadWrite;
  // * increment {byte_length_};
  //
  // As this code is executed concurrently, the following steps are executed:
  // 1) Read the current value of {byte_length_};
  // 2) Change the permission of all pages from {buffer_start_} to
  //    {byte_length_} + {delta_pages} * {page_size} to kReadWrite;
  //    * This operation may be executed racefully. The OS takes care of
  //      synchronization.
  // 3) Try to update {byte_length_} with a compare_exchange;
  // 4) Repeat 1) to 3) until the compare_exchange in 3) succeeds;
  //
  // The result of this function is the {byte_length_} before growing in pages.
  // The result of this function appears like the result of an RMW-update on
  // {byte_length_}, i.e. two concurrent calls to this function will result in
  // different return values if {delta_pages} != 0.
  //
  // Invariants:
  // * Permissions are always set incrementally, i.e. for any page {b} with
  //   kReadWrite permission, all pages between the first page {a} and page {b}
  //   also have kReadWrite permission.
  // * {byte_length_} is always lower or equal than the amount of memory with
  //   permissions set to kReadWrite;
  //     * This is guaranteed by incrementing {byte_length_} with a
  //       compare_exchange after changing the permissions.
  //     * This invariant is the reason why we cannot use a fetch_add.
  DCHECK(is_wasm_memory_);
  max_pages = std::min(max_pages, byte_capacity_ / wasm::kWasmPageSize);

  // Do a compare-exchange loop, because we also need to adjust page
  // permissions. Note that multiple racing grows both try to set page
  // permissions for the entire range (to be RW), so the operating system
  // should deal with that raciness. We know we succeeded when we can
  // compare/swap the old length with the new length.
  size_t old_length = byte_length_.load(std::memory_order_relaxed);

  if (delta_pages == 0)
    return {old_length / wasm::kWasmPageSize};  // degenerate grow.
  if (delta_pages > max_pages) return {};       // would never work.

  size_t new_length = 0;
  while (true) {
    size_t current_pages = old_length / wasm::kWasmPageSize;

    // Check if we have exceed the supplied maximum.
    if (current_pages > (max_pages - delta_pages)) return {};

    new_length = (current_pages + delta_pages) * wasm::kWasmPageSize;

    // Try to adjust the permissions on the memory.
    if (!i::SetPermissions(GetPlatformPageAllocator(), buffer_start_,
                           new_length, PageAllocator::kReadWrite)) {
      return {};
    }
    if (byte_length_.compare_exchange_weak(old_length, new_length,
                                           std::memory_order_acq_rel)) {
      // Successfully updated both the length and permissions.
      break;
    }
  }

  if (!is_shared_) {
    // Only do per-isolate accounting for non-shared backing stores.
    reinterpret_cast<v8::Isolate*>(isolate)
        ->AdjustAmountOfExternalAllocatedMemory(new_length - old_length);
  }
  return {old_length / wasm::kWasmPageSize};
}

void BackingStore::AttachSharedWasmMemoryObject(
    Isolate* isolate, Handle<WasmMemoryObject> memory_object) {
  DCHECK(is_wasm_memory_);
  DCHECK(is_shared_);
  // We need to take the global registry lock for this operation.
  GlobalBackingStoreRegistry::AddSharedWasmMemoryObject(isolate, this,
                                                        memory_object);
}

void BackingStore::BroadcastSharedWasmMemoryGrow(Isolate* isolate) const {
  GlobalBackingStoreRegistry::BroadcastSharedWasmMemoryGrow(isolate, this);
}

void BackingStore::RemoveSharedWasmMemoryObjects(Isolate* isolate) {
  GlobalBackingStoreRegistry::Purge(isolate);
}

void BackingStore::UpdateSharedWasmMemoryObjects(Isolate* isolate) {
  GlobalBackingStoreRegistry::UpdateSharedWasmMemoryObjects(isolate);
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Commit already reserved memory (for RAB backing stores (not shared)).
BackingStore::ResizeOrGrowResult BackingStore::ResizeInPlace(
    Isolate* isolate, size_t new_byte_length) {
  size_t page_size = AllocatePageSize();
  size_t new_committed_pages;
  bool round_return_value =
      RoundUpToPageSize(new_byte_length, page_size,
                        JSArrayBuffer::kMaxByteLength, &new_committed_pages);
  CHECK(round_return_value);

  size_t new_committed_length = new_committed_pages * page_size;
  DCHECK_LE(new_byte_length, new_committed_length);
  DCHECK(!is_shared());

  if (new_byte_length < byte_length_) {
    // Zero the memory so that in case the buffer is grown later, we have
    // zeroed the contents already. This is especially needed for the portion of
    // the memory we're not going to decommit below (since it belongs to a
    // committed page). In addition, we don't rely on all platforms always
    // zeroing decommitted-then-recommitted memory, but zero the memory
    // explicitly here.
    memset(reinterpret_cast<uint8_t*>(buffer_start_) + new_byte_length, 0,
           byte_length_ - new_byte_length);

    // Check if we can un-commit some pages.
    size_t old_committed_pages;
    round_return_value =
        RoundUpToPageSize(byte_length_, page_size,
                          JSArrayBuffer::kMaxByteLength, &old_committed_pages);
    CHECK(round_return_value);
    DCHECK_LE(new_committed_pages, old_committed_pages);

    if (new_committed_pages < old_committed_pages) {
      size_t old_committed_length = old_committed_pages * page_size;
      if (!i::SetPermissions(
              GetPlatformPageAllocator(),
              reinterpret_cast<uint8_t*>(buffer_start_) + new_committed_length,
              old_committed_length - new_committed_length,
              PageAllocator::kNoAccess)) {
        return kFailure;
      }
    }

    // Changing the byte length wouldn't strictly speaking be needed, since
    // the JSArrayBuffer already stores the updated length. This is to keep
    // the BackingStore and JSArrayBuffer in sync.
    byte_length_ = new_byte_length;
    return kSuccess;
  }
  if (new_byte_length == byte_length_) {
    // i::SetPermissions with size 0 fails on some platforms, so special
    // handling for the case byte_length_ == new_byte_length == 0 is required.
    return kSuccess;
  }

  // Try to adjust the permissions on the memory.
  if (!i::SetPermissions(GetPlatformPageAllocator(), buffer_start_,
                         new_committed_length, PageAllocator::kReadWrite)) {
    return kFailure;
  }

  // Do per-isolate accounting for non-shared backing stores.
  reinterpret_cast<v8::Isolate*>(isolate)
      ->AdjustAmountOfExternalAllocatedMemory(new_byte_length - byte_length_);
  byte_length_ = new_byte_length;
  return kSuccess;
}

// Commit already reserved memory (for GSAB backing stores (shared)).
BackingStore::ResizeOrGrowResult BackingStore::GrowInPlace(
    Isolate* isolate, size_t new_byte_length) {
  size_t page_size = AllocatePageSize();
  size_t new_committed_pages;
  bool round_return_value =
      RoundUpToPageSize(new_byte_length, page_size,
                        JSArrayBuffer::kMaxByteLength, &new_committed_pages);
  CHECK(round_return_value);

  size_t new_committed_length = new_committed_pages * page_size;
  DCHECK_LE(new_byte_length, new_committed_length);
  DCHECK(is_shared());
  // See comment in GrowWasmMemoryInPlace.
  // GrowableSharedArrayBuffer.prototype.grow can be called from several
  // threads. If two threads try to grow() in a racy way, the spec allows the
  // larger grow to throw also if the smaller grow succeeds first. The
  // implementation below doesn't throw in that case - instead, it retries and
  // succeeds. If the larger grow finishes first though, the smaller grow must
  // throw.
  size_t old_byte_length = byte_length_.load(std::memory_order_seq_cst);
  while (true) {
    if (new_byte_length < old_byte_length) {
      // The caller checks for the new_byte_length < old_byte_length_ case. This
      // can only happen if another thread grew the memory after that.
      return kRace;
    }
    if (new_byte_length == old_byte_length) {
      // i::SetPermissions with size 0 fails on some platforms, so special
      // handling for the case old_byte_length == new_byte_length == 0 is
      // required.
      return kSuccess;
    }

    // Try to adjust the permissions on the memory.
    if (!i::SetPermissions(GetPlatformPageAllocator(), buffer_start_,
                           new_committed_length, PageAllocator::kReadWrite)) {
      return kFailure;
    }

    // compare_exchange_weak updates old_byte_length.
    if (byte_length_.compare_exchange_weak(old_byte_length, new_byte_length,
                                           std::memory_order_seq_cst)) {
      // Successfully updated both the length and permissions.
      break;
    }
  }
  return kSuccess;
}

std::unique_ptr<BackingStore> BackingStore::WrapAllocation(
    void* allocation_base, size_t allocation_length,
    v8::BackingStore::DeleterCallback deleter, void* deleter_data,
    SharedFlag shared) {
  bool is_empty_deleter = (deleter == v8::BackingStore::EmptyDeleter);
  auto result = new BackingStore(allocation_base,               // start
                                 allocation_length,             // length
                                 allocation_length,             // max length
                                 allocation_length,             // capacity
                                 shared,                        // shared
                                 ResizableFlag::kNotResizable,  // resizable
                                 false,              // is_wasm_memory
                                 false,              // is_wasm_memory64
                                 false,              // has_guard_regions
                                 true,               // custom_deleter
                                 is_empty_deleter);  // empty_deleter
  result->type_specific_data_.deleter = {deleter, deleter_data};
  TRACE_BS("BS:wrap   bs=%p mem=%p (length=%zu)\n", result,
           result->buffer_start(), result->byte_length());
  return std::unique_ptr<BackingStore>(result);
}

std::unique_ptr<BackingStore> BackingStore::EmptyBackingStore(
    SharedFlag shared) {
  auto result = new BackingStore(nullptr,                       // start
                                 0,                             // length
                                 0,                             // max length
                                 0,                             // capacity
                                 shared,                        // shared
                                 ResizableFlag::kNotResizable,  // resizable
                                 false,   // is_wasm_memory
                                 false,   // is_wasm_memory64
                                 false,   // has_guard_regions
                                 false,   // custom_deleter
                                 false);  // empty_deleter

  return std::unique_ptr<BackingStore>(result);
}

bool BackingStore::Reallocate(Isolate* isolate, size_t new_byte_length) {
  CHECK(CanReallocate());
  auto allocator = get_v8_api_array_buffer_allocator();
  CHECK_EQ(isolate->array_buffer_allocator(), allocator);
  CHECK_EQ(byte_length_, byte_capacity_);
  START_ALLOW_USE_DEPRECATED()
  void* new_start =
      allocator->Reallocate(buffer_start_, byte_length_, new_byte_length);
  END_ALLOW_USE_DEPRECATED()
  if (!new_start) return false;
  buffer_start_ = new_start;
  byte_capacity_ = new_byte_length;
  byte_length_ = new_byte_length;
  max_byte_length_ = new_byte_length;
  return true;
}

v8::ArrayBuffer::Allocator* BackingStore::get_v8_api_array_buffer_allocator() {
  CHECK(!is_wasm_memory_);
  auto array_buffer_allocator =
      holds_shared_ptr_to_allocator_
          ? type_specific_data_.v8_api_array_buffer_allocator_shared.get()
          : type_specific_data_.v8_api_array_buffer_allocator;
  CHECK_NOT_NULL(array_buffer_allocator);
  return array_buffer_allocator;
}

SharedWasmMemoryData* BackingStore::get_shared_wasm_memory_data() const {
  CHECK(is_wasm_memory_ && is_shared_);
  auto shared_wasm_memory_data = type_specific_data_.shared_wasm_memory_data;
  CHECK(shared_wasm_memory_data);
  return shared_wasm_memory_data;
}

namespace {
// Implementation details of GlobalBackingStoreRegistry.
struct GlobalBackingStoreRegistryImpl {
  GlobalBackingStoreRegistryImpl() = default;
  base::Mutex mutex_;
  std::unordered_map<const void*, std::weak_ptr<BackingStore>> map_;
};

DEFINE_LAZY_LEAKY_OBJECT_GETTER(GlobalBackingStoreRegistryImpl,
                                GetGlobalBackingStoreRegistryImpl)
}  // namespace

void GlobalBackingStoreRegistry::Register(
    std::shared_ptr<BackingStore> backing_store) {
  if (!backing_store || !backing_store->buffer_start()) return;
  // Only wasm memory backing stores need to be registered globally.
  CHECK(backing_store->is_wasm_memory());

  GlobalBackingStoreRegistryImpl* impl = GetGlobalBackingStoreRegistryImpl();
  base::MutexGuard scope_lock(&impl->mutex_);
  if (backing_store->globally_registered_) return;
  TRACE_BS("BS:reg    bs=%p mem=%p (length=%zu, capacity=%zu)\n",
           backing_store.get(), backing_store->buffer_start(),
           backing_store->byte_length(), backing_store->byte_capacity());
  std::weak_ptr<BackingStore> weak = backing_store;
  auto result = impl->map_.insert({backing_store->buffer_start(), weak});
  CHECK(result.second);
  backing_store->globally_registered_ = true;
}

void GlobalBackingStoreRegistry::Unregister(BackingStore* backing_store) {
  if (!backing_store->globally_registered_) return;

  CHECK(backing_store->is_wasm_memory());

  DCHECK_NOT_NULL(backing_store->buffer_start());

  GlobalBackingStoreRegistryImpl* impl = GetGlobalBackingStoreRegistryImpl();
  base::MutexGuard scope_lock(&impl->mutex_);
  const auto& result = impl->map_.find(backing_store->buffer_start());
  if (result != impl->map_.end()) {
    DCHECK(!result->second.lock());
    impl->map_.erase(result);
  }
  backing_store->globally_registered_ = false;
}

void GlobalBackingStoreRegistry::Purge(Isolate* isolate) {
  // We need to keep a reference to all backing stores that are inspected
  // in the purging loop below. Otherwise, we might get a deadlock
  // if the temporary backing store reference created in the loop is
  // the last reference. In that case the destructor of the backing store
  // may try to take the &impl->mutex_ in order to unregister itself.
  std::vector<std::shared_ptr<BackingStore>> prevent_destruction_under_lock;
  GlobalBackingStoreRegistryImpl* impl = GetGlobalBackingStoreRegistryImpl();
  base::MutexGuard scope_lock(&impl->mutex_);
  // Purge all entries in the map that refer to the given isolate.
  for (auto& entry : impl->map_) {
    auto backing_store = entry.second.lock();
    prevent_destruction_under_lock.emplace_back(backing_store);
    if (!backing_store) continue;  // skip entries where weak ptr is null
    CHECK(backing_store->is_wasm_memory());
    if (!backing_store->is_shared()) continue;       // skip non-shared memory
    SharedWasmMemoryData* shared_data =
        backing_store->get_shared_wasm_memory_data();
    // Remove this isolate from the isolates list.
    std::vector<Isolate*>& isolates = shared_data->isolates_;
    auto isolates_it = std::find(isolates.begin(), isolates.end(), isolate);
    if (isolates_it != isolates.end()) {
      *isolates_it = isolates.back();
      isolates.pop_back();
    }
    DCHECK_EQ(isolates.end(),
              std::find(isolates.begin(), isolates.end(), isolate));
  }
}

#if V8_ENABLE_WEBASSEMBLY
void GlobalBackingStoreRegistry::AddSharedWasmMemoryObject(
    Isolate* isolate, BackingStore* backing_store,
    Handle<WasmMemoryObject> memory_object) {
  // Add to the weak array list of shared memory objects in the isolate.
  isolate->AddSharedWasmMemory(memory_object);

  // Add the isolate to the list of isolates sharing this backing store.
  GlobalBackingStoreRegistryImpl* impl = GetGlobalBackingStoreRegistryImpl();
  base::MutexGuard scope_lock(&impl->mutex_);
  SharedWasmMemoryData* shared_data =
      backing_store->get_shared_wasm_memory_data();
  auto& isolates = shared_data->isolates_;
  int free_entry = -1;
  for (size_t i = 0; i < isolates.size(); i++) {
    if (isolates[i] == isolate) return;
    if (isolates[i] == nullptr) free_entry = static_cast<int>(i);
  }
  if (free_entry >= 0)
    isolates[free_entry] = isolate;
  else
    isolates.push_back(isolate);
}

void GlobalBackingStoreRegistry::BroadcastSharedWasmMemoryGrow(
    Isolate* isolate, const BackingStore* backing_store) {
  {
    GlobalBackingStoreRegistryImpl* impl = GetGlobalBackingStoreRegistryImpl();
    // The global lock protects the list of isolates per backing store.
    base::MutexGuard scope_lock(&impl->mutex_);
    SharedWasmMemoryData* shared_data =
        backing_store->get_shared_wasm_memory_data();
    for (Isolate* other : shared_data->isolates_) {
      if (other == isolate) continue;
      other->stack_guard()->RequestGrowSharedMemory();
    }
  }
  // Update memory objects in this isolate.
  UpdateSharedWasmMemoryObjects(isolate);
}

void GlobalBackingStoreRegistry::UpdateSharedWasmMemoryObjects(
    Isolate* isolate) {
  // TODO(1445003): Remove the {AlwaysAllocateScope} after finding the root
  // cause.
  AlwaysAllocateScope always_allocate_scope{isolate->heap()};

  HandleScope scope(isolate);
  DirectHandle<WeakArrayList> shared_wasm_memories =
      isolate->factory()->shared_wasm_memories();

  for (int i = 0, e = shared_wasm_memories->length(); i < e; ++i) {
    Tagged<HeapObject> obj;
    if (!shared_wasm_memories->Get(i).GetHeapObject(&obj)) continue;

    DirectHandle<WasmMemoryObject> memory_object(Cast<WasmMemoryObject>(obj),
                                                 isolate);
    DirectHandle<JSArrayBuffer> old_buffer(memory_object->array_buffer(),
                                           isolate);
    std::shared_ptr<BackingStore> backing_store = old_buffer->GetBackingStore();
    // Wasm memory always has a BackingStore.
    CHECK_NOT_NULL(backing_store);
    CHECK(backing_store->is_wasm_memory());
    CHECK(backing_store->is_shared());

    // Keep a raw pointer to the backing store for a CHECK later one. Make it
    // {void*} so we do not accidentally try to use it for anything else.
    void* expected_backing_store = backing_store.get();

    DirectHandle<JSArrayBuffer> new_buffer =
        isolate->factory()->NewJSSharedArrayBuffer(std::move(backing_store));
    CHECK_EQ(expected_backing_store, new_buffer->GetBackingStore().get());
    memory_object->SetNewBuffer(*new_buffer);
  }
}
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace v8::internal

#undef TRACE_BS
