// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/code-range.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>

#include "src/base/bits.h"
#include "src/base/lazy-instance.h"
#include "src/base/once.h"
#include "src/codegen/constants-arch.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/heap/heap-inl.h"
#include "src/utils/allocation.h"
#if defined(V8_OS_WIN64)
#include "src/diagnostics/unwinding-info-win64.h"
#endif  // V8_OS_WIN64

namespace v8 {
namespace internal {

namespace {

DEFINE_LAZY_LEAKY_OBJECT_GETTER(CodeRangeAddressHint, GetCodeRangeAddressHint)

void FunctionInStaticBinaryForAddressHint() {}

}  // anonymous namespace

Address CodeRangeAddressHint::GetAddressHint(size_t code_range_size,
                                             size_t alignment) {
  base::MutexGuard guard(&mutex_);

  // Try to allocate code range in the preferred region where we can use
  // short instructions for calling/jumping to embedded builtins.
  base::AddressRegion preferred_region = Isolate::GetShortBuiltinsCallRegion();

  Address result = 0;
  auto it = recently_freed_.find(code_range_size);
  // No recently freed region has been found, try to provide a hint for placing
  // a code region.
  if (it == recently_freed_.end() || it->second.empty()) {
    if (V8_ENABLE_NEAR_CODE_RANGE_BOOL && !preferred_region.is_empty()) {
      const auto memory_ranges = base::OS::GetFirstFreeMemoryRangeWithin(
          preferred_region.begin(), preferred_region.end(), code_range_size,
          alignment);
      if (memory_ranges.has_value()) {
        result = memory_ranges.value().start;
        CHECK(IsAligned(result, alignment));
        return result;
      }
      // The empty memory_ranges means that GetFirstFreeMemoryRangeWithin() API
      // is not supported, so use the lowest address from the preferred region
      // as a hint because it'll be at least as good as the fallback hint but
      // with a higher chances to point to the free address space range.
      return RoundUp(preferred_region.begin(), alignment);
    }
    return RoundUp(FUNCTION_ADDR(&FunctionInStaticBinaryForAddressHint),
                   alignment);
  }

  // Try to reuse near code range first.
  if (V8_ENABLE_NEAR_CODE_RANGE_BOOL && !preferred_region.is_empty()) {
    auto freed_regions_for_size = it->second;
    for (auto it_freed = freed_regions_for_size.rbegin();
         it_freed != freed_regions_for_size.rend(); ++it_freed) {
      Address code_range_start = *it_freed;
      if (preferred_region.contains(code_range_start, code_range_size)) {
        CHECK(IsAligned(code_range_start, alignment));
        freed_regions_for_size.erase((it_freed + 1).base());
        return code_range_start;
      }
    }
  }

  result = it->second.back();
  CHECK(IsAligned(result, alignment));
  it->second.pop_back();
  return result;
}

void CodeRangeAddressHint::NotifyFreedCodeRange(Address code_range_start,
                                                size_t code_range_size) {
  base::MutexGuard guard(&mutex_);
  recently_freed_[code_range_size].push_back(code_range_start);
}

CodeRange::~CodeRange() { Free(); }

// static
size_t CodeRange::GetWritableReservedAreaSize() {
  return kReservedCodeRangePages * MemoryAllocator::GetCommitPageSize();
}

#define TRACE(...) \
  if (v8_flags.trace_code_range_allocation) PrintF(__VA_ARGS__)

bool CodeRange::InitReservation(v8::PageAllocator* page_allocator,
                                size_t requested, bool immutable) {
  DCHECK_NE(requested, 0);
  if (V8_EXTERNAL_CODE_SPACE_BOOL) {
    page_allocator = GetPlatformPageAllocator();
  }

  if (requested <= kMinimumCodeRangeSize) {
    requested = kMinimumCodeRangeSize;
  }

  const size_t kPageSize = MutablePageMetadata::kPageSize;
  CHECK(IsAligned(kPageSize, page_allocator->AllocatePageSize()));

  // When V8_EXTERNAL_CODE_SPACE_BOOL is enabled the allocatable region must
  // not cross the 4Gb boundary and thus the default compression scheme of
  // truncating the InstructionStream pointers to 32-bits still works. It's
  // achieved by specifying base_alignment parameter.
  const size_t base_alignment = V8_EXTERNAL_CODE_SPACE_BOOL
                                    ? base::bits::RoundUpToPowerOfTwo(requested)
                                    : kPageSize;

  DCHECK_IMPLIES(kPlatformRequiresCodeRange,
                 requested <= kMaximalCodeRangeSize);

  VirtualMemoryCage::ReservationParams params;
  params.page_allocator = page_allocator;
  params.reservation_size = requested;
  params.page_size = kPageSize;
  if (v8_flags.jitless) {
    params.permissions = PageAllocator::Permission::kNoAccess;
    params.page_initialization_mode =
        base::PageInitializationMode::kAllocatedPagesCanBeUninitialized;
    params.page_freeing_mode = base::PageFreeingMode::kMakeInaccessible;
  } else {
    params.permissions = PageAllocator::Permission::kNoAccessWillJitLater;
    params.page_initialization_mode =
        base::PageInitializationMode::kRecommitOnly;
    params.page_freeing_mode = base::PageFreeingMode::kDiscard;
  }

  const size_t allocate_page_size = page_allocator->AllocatePageSize();
  constexpr size_t kRadiusInMB =
      kMaxPCRelativeCodeRangeInMB > 1024 ? kMaxPCRelativeCodeRangeInMB : 4096;
  auto preferred_region = GetPreferredRegion(kRadiusInMB, kPageSize);

  TRACE("=== Preferred region: [%p, %p)\n",
        reinterpret_cast<void*>(preferred_region.begin()),
        reinterpret_cast<void*>(preferred_region.end()));

  // For configurations with enabled pointer compression and shared external
  // code range we can afford trying harder to allocate code range near .text
  // section.
  const bool kShouldTryHarder = V8_EXTERNAL_CODE_SPACE_BOOL &&
                                COMPRESS_POINTERS_IN_SHARED_CAGE_BOOL &&
                                v8_flags.better_code_range_allocation;

  if (kShouldTryHarder) {
    // Relax alignment requirement while trying to allocate code range inside
    // preferred region.
    params.base_alignment = kPageSize;

    // TODO(v8:11880): consider using base::OS::GetFirstFreeMemoryRangeWithin()
    // to avoid attempts that's going to fail anyway.

    VirtualMemoryCage candidate_cage;

    // Try to allocate code range at the end of preferred region, by going
    // towards the start in steps.
    const int kAllocationTries = 16;
    params.requested_start_hint =
        RoundDown(preferred_region.end() - requested, kPageSize);
    Address step =
        RoundDown(preferred_region.size() / kAllocationTries, kPageSize);
    for (int i = 0; i < kAllocationTries; i++) {
      TRACE("=== Attempt #%d, hint=%p\n", i,
            reinterpret_cast<void*>(params.requested_start_hint));
      if (candidate_cage.InitReservation(params)) {
        TRACE("=== Attempt #%d (%p): [%p, %p)\n", i,
              reinterpret_cast<void*>(params.requested_start_hint),
              reinterpret_cast<void*>(candidate_cage.region().begin()),
              reinterpret_cast<void*>(candidate_cage.region().end()));
        // Allocation succeeded, check if it's in the preferred range.
        if (preferred_region.contains(candidate_cage.region())) break;
        // This allocation is not the one we are searhing for.
        candidate_cage.Free();
      }
      if (step == 0) break;
      params.requested_start_hint -= step;
    }
    if (candidate_cage.IsReserved()) {
      *static_cast<VirtualMemoryCage*>(this) = std::move(candidate_cage);
    }
  }
  if (!IsReserved()) {
    // TODO(v8:11880): Use base_alignment here once ChromeOS issue is fixed.
    Address the_hint = GetCodeRangeAddressHint()->GetAddressHint(
        requested, allocate_page_size);
    the_hint = RoundDown(the_hint, base_alignment);
    // Last resort, use whatever region we get.
    params.base_alignment = base_alignment;
    params.requested_start_hint = the_hint;
    if (!VirtualMemoryCage::InitReservation(params)) {
      params.requested_start_hint = kNullAddress;
      if (!VirtualMemoryCage::InitReservation(params)) return false;
    }
    TRACE("=== Fallback attempt, hint=%p: [%p, %p)\n",
          reinterpret_cast<void*>(params.requested_start_hint),
          reinterpret_cast<void*>(region().begin()),
          reinterpret_cast<void*>(region().end()));
  }

  if (v8_flags.abort_on_far_code_range &&
      !preferred_region.contains(region())) {
    // We didn't manage to allocate the code range close enough.
    FATAL("Failed to allocate code range close to the .text section");
  }

  // On some platforms, specifically Win64, we need to reserve some pages at
  // the beginning of an executable space. See
  //   https://cs.chromium.org/chromium/src/components/crash/content/
  //     app/crashpad_win.cc?rcl=fd680447881449fba2edcf0589320e7253719212&l=204
  // for details.
  const size_t reserved_area = GetWritableReservedAreaSize();
  if (reserved_area > 0) {
    CHECK_LE(reserved_area, kPageSize);
    // Exclude the reserved area from further allocations.
    CHECK(page_allocator_->AllocatePagesAt(base(), kPageSize,
                                           PageAllocator::kNoAccess));
    // Commit required amount of writable memory.
    if (!reservation()->SetPermissions(base(), reserved_area,
                                       PageAllocator::kReadWrite)) {
      return false;
    }
#if defined(V8_OS_WIN64)
    if (win64_unwindinfo::CanRegisterUnwindInfoForNonABICompliantCodeRange()) {
      win64_unwindinfo::RegisterNonABICompliantCodeRange(
          reinterpret_cast<void*>(base()), size());
    }
#endif  // V8_OS_WIN64
  }

// Don't pre-commit the code cage on Windows since it uses memory and it's not
// required for recommit.
#if !defined(V8_OS_WIN)
  if (params.page_initialization_mode ==
      base::PageInitializationMode::kRecommitOnly) {
    void* base =
        reinterpret_cast<void*>(page_allocator_->begin() + reserved_area);
    size_t size = page_allocator_->size() - reserved_area;
    if (ThreadIsolation::Enabled()) {
      if (!ThreadIsolation::MakeExecutable(reinterpret_cast<Address>(base),
                                           size)) {
        return false;
      }
    } else if (!params.page_allocator->SetPermissions(
                   base, size, PageAllocator::kReadWriteExecute)) {
      return false;
    }
    if (immutable) {
#ifdef DEBUG
      immutable_ = true;
#endif
#ifdef V8_ENABLE_MEMORY_SEALING
      params.page_allocator->SealPages(base, size);
#endif
    }
    DiscardSealedMemoryScope discard_scope("Discard global code range.");
    if (!params.page_allocator->DiscardSystemPages(base, size)) return false;
  }
#endif  // !defined(V8_OS_WIN)

  return true;
}

// Preferred region for the code range is an intersection of the following
// regions:
// a) [builtins - kMaxPCRelativeDistance, builtins + kMaxPCRelativeDistance)
// b) [RoundDown(builtins, 4GB), RoundUp(builtins, 4GB)) in order to ensure
// Requirement (a) is there to avoid remaping of embedded builtins into
// the code for architectures where PC-relative jump/call distance is big
// enough.
// Requirement (b) is aiming at helping CPU branch predictors in general and
// in case V8_EXTERNAL_CODE_SPACE is enabled it ensures that
// ExternalCodeCompressionScheme works for all pointers in the code range.
// static
base::AddressRegion CodeRange::GetPreferredRegion(size_t radius_in_megabytes,
                                                  size_t allocate_page_size) {
#ifdef V8_TARGET_ARCH_64_BIT
  // Compute builtins location.
  Address embedded_blob_code_start =
      reinterpret_cast<Address>(Isolate::CurrentEmbeddedBlobCode());
  Address embedded_blob_code_end;
  if (embedded_blob_code_start == kNullAddress) {
    // When there's no embedded blob use address of a function from the binary
    // as an approximation.
    embedded_blob_code_start =
        FUNCTION_ADDR(&FunctionInStaticBinaryForAddressHint);
    embedded_blob_code_end = embedded_blob_code_start + 1;
  } else {
    embedded_blob_code_end =
        embedded_blob_code_start + Isolate::CurrentEmbeddedBlobCodeSize();
  }

  // Fulfil requirement (a).
  constexpr size_t max_size = std::numeric_limits<size_t>::max();
  size_t radius = radius_in_megabytes * MB;

  Address region_start =
      RoundUp(embedded_blob_code_end - radius, allocate_page_size);
  if (region_start > embedded_blob_code_end) {
    // |region_start| underflowed.
    region_start = 0;
  }
  Address region_end =
      RoundDown(embedded_blob_code_start + radius, allocate_page_size);
  if (region_end < embedded_blob_code_start) {
    // |region_end| overflowed.
    region_end = RoundDown(max_size, allocate_page_size);
  }

  // Fulfil requirement (b).
  constexpr size_t k4GB = size_t{4} * GB;
  Address four_gb_cage_start = RoundDown(embedded_blob_code_start, k4GB);
  Address four_gb_cage_end = four_gb_cage_start + k4GB;

  region_start = std::max(region_start, four_gb_cage_start);
  region_end = std::min(region_end, four_gb_cage_end);

#ifdef V8_EXTERNAL_CODE_SPACE
  // If ExternalCodeCompressionScheme ever changes then the requirements might
  // need to be updated.
  static_assert(k4GB <= kPtrComprCageReservationSize);
  DCHECK_EQ(four_gb_cage_start,
            ExternalCodeCompressionScheme::PrepareCageBaseAddress(
                embedded_blob_code_start));
#endif  // V8_EXTERNAL_CODE_SPACE

  return base::AddressRegion(region_start, region_end - region_start);
#else
  return {};
#endif  // V8_TARGET_ARCH_64_BIT
}

void CodeRange::Free() {
  // TODO(361480580): this DCHECK is temporarily disabled since we free the
  // global CodeRange in the PoolTest.
  // DCHECK(!immutable_);

  if (IsReserved()) {
#if defined(V8_OS_WIN64)
    if (win64_unwindinfo::CanRegisterUnwindInfoForNonABICompliantCodeRange()) {
      win64_unwindinfo::UnregisterNonABICompliantCodeRange(
          reinterpret_cast<void*>(base()));
    }
#endif  // V8_OS_WIN64
    GetCodeRangeAddressHint()->NotifyFreedCodeRange(
        reservation()->region().begin(), reservation()->region().size());
    VirtualMemoryCage::Free();
  }
}

uint8_t* CodeRange::RemapEmbeddedBuiltins(Isolate* isolate,
                                          const uint8_t* embedded_blob_code,
                                          size_t embedded_blob_code_size) {
  base::MutexGuard guard(&remap_embedded_builtins_mutex_);

  // Remap embedded builtins into the end of the address range controlled by
  // the BoundedPageAllocator.
  const base::AddressRegion code_region(page_allocator()->begin(),
                                        page_allocator()->size());
  CHECK_NE(code_region.begin(), kNullAddress);
  CHECK(!code_region.is_empty());

  uint8_t* embedded_blob_code_copy =
      embedded_blob_code_copy_.load(std::memory_order_acquire);
  if (embedded_blob_code_copy) {
    DCHECK(
        code_region.contains(reinterpret_cast<Address>(embedded_blob_code_copy),
                             embedded_blob_code_size));
    SLOW_DCHECK(memcmp(embedded_blob_code, embedded_blob_code_copy,
                       embedded_blob_code_size) == 0);
    return embedded_blob_code_copy;
  }

  const size_t kAllocatePageSize = page_allocator()->AllocatePageSize();
  const size_t kCommitPageSize = page_allocator()->CommitPageSize();
  size_t allocate_code_size =
      RoundUp(embedded_blob_code_size, kAllocatePageSize);

  // Allocate the re-embedded code blob in such a way that it will be reachable
  // by PC-relative addressing from biggest possible region.
  const size_t max_pc_relative_code_range = kMaxPCRelativeCodeRangeInMB * MB;
  size_t hint_offset =
      std::min(max_pc_relative_code_range, code_region.size()) -
      allocate_code_size;
  void* hint = reinterpret_cast<void*>(code_region.begin() + hint_offset);

  embedded_blob_code_copy =
      reinterpret_cast<uint8_t*>(page_allocator()->AllocatePages(
          hint, allocate_code_size, kAllocatePageSize,
          PageAllocator::kNoAccessWillJitLater));

  if (!embedded_blob_code_copy) {
    V8::FatalProcessOutOfMemory(
        isolate, "Can't allocate space for re-embedded builtins");
  }
  CHECK_EQ(embedded_blob_code_copy, hint);

  if (code_region.size() > max_pc_relative_code_range) {
    // The re-embedded code blob might not be reachable from the end part of
    // the code range, so ensure that code pages will never be allocated in
    // the "unreachable" area.
    Address unreachable_start =
        reinterpret_cast<Address>(embedded_blob_code_copy) +
        max_pc_relative_code_range;

    if (code_region.contains(unreachable_start)) {
      size_t unreachable_size = code_region.end() - unreachable_start;

      void* result = page_allocator()->AllocatePages(
          reinterpret_cast<void*>(unreachable_start), unreachable_size,
          kAllocatePageSize, PageAllocator::kNoAccess);
      CHECK_EQ(reinterpret_cast<Address>(result), unreachable_start);
    }
  }

  size_t code_size = RoundUp(embedded_blob_code_size, kCommitPageSize);
  if constexpr (base::OS::IsRemapPageSupported()) {
    // By default, the embedded builtins are not remapped, but copied. This
    // costs memory, since builtins become private dirty anonymous memory,
    // rather than shared, clean, file-backed memory for the embedded version.
    // If the OS supports it, we can remap the builtins *on top* of the space
    // allocated in the code range, making the "copy" shared, clean, file-backed
    // memory, and thus saving sizeof(builtins).
    //
    // Builtins should start at a page boundary, see
    // platform-embedded-file-writer-mac.cc. If it's not the case (e.g. if the
    // embedded builtins are not coming from the binary), fall back to copying.
    if (IsAligned(reinterpret_cast<uintptr_t>(embedded_blob_code),
                  kCommitPageSize)) {
      bool ok = base::OS::RemapPages(embedded_blob_code, code_size,
                                     embedded_blob_code_copy,
                                     base::OS::MemoryPermission::kReadExecute);

      if (ok) {
        embedded_blob_code_copy_.store(embedded_blob_code_copy,
                                       std::memory_order_release);
        return embedded_blob_code_copy;
      }
    }
  }

  if (V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT ||
      V8_HEAP_USE_BECORE_JIT_WRITE_PROTECT || ThreadIsolation::Enabled()) {
    if (!page_allocator()->RecommitPages(embedded_blob_code_copy, code_size,
                                         PageAllocator::kReadWriteExecute)) {
      V8::FatalProcessOutOfMemory(isolate,
                                  "Re-embedded builtins: recommit pages");
    }
    RwxMemoryWriteScope rwx_write_scope(
        "Enable write access to copy the blob code into the code range");
    memcpy(embedded_blob_code_copy, embedded_blob_code,
           embedded_blob_code_size);
  } else {
    if (!page_allocator()->SetPermissions(embedded_blob_code_copy, code_size,
                                          PageAllocator::kReadWrite)) {
      V8::FatalProcessOutOfMemory(isolate,
                                  "Re-embedded builtins: set permissions");
    }
    memcpy(embedded_blob_code_copy, embedded_blob_code,
           embedded_blob_code_size);

    if (!page_allocator()->SetPermissions(embedded_blob_code_copy, code_size,
                                          PageAllocator::kReadExecute)) {
      V8::FatalProcessOutOfMemory(isolate,
                                  "Re-embedded builtins: set permissions");
    }
  }
  embedded_blob_code_copy_.store(embedded_blob_code_copy,
                                 std::memory_order_release);
  return embedded_blob_code_copy;
}

}  // namespace internal
}  // namespace v8
