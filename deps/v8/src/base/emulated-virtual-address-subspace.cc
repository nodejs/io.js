// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/emulated-virtual-address-subspace.h"

#include "src/base/bits.h"
#include "src/base/platform/platform.h"

namespace v8 {
namespace base {

EmulatedVirtualAddressSubspace::EmulatedVirtualAddressSubspace(
    VirtualAddressSpace* parent_space, Address base, size_t mapped_size,
    size_t total_size)
    : VirtualAddressSpace(parent_space->page_size(),
                          parent_space->allocation_granularity(), base,
                          total_size, parent_space->max_page_permissions()),
      mapped_size_(mapped_size),
      parent_space_(parent_space),
      region_allocator_(base, mapped_size, parent_space_->page_size()) {
  // For simplicity, we currently require both the mapped and total size to be
  // a power of two. This simplifies some things later on, for example, random
  // addresses can be generated with a simply bitmask, and will then be inside
  // the unmapped space with a probability >= 50% (mapped size == unmapped
  // size) or never (mapped size == total size).
  DCHECK(base::bits::IsPowerOfTwo(mapped_size));
  DCHECK(base::bits::IsPowerOfTwo(total_size));
}

EmulatedVirtualAddressSubspace::~EmulatedVirtualAddressSubspace() {
  parent_space_->FreePages(base(), mapped_size_);
}

void EmulatedVirtualAddressSubspace::SetRandomSeed(int64_t seed) {
  SpinningMutexGuard guard(&mutex_);
  rng_.SetSeed(seed);
}

Address EmulatedVirtualAddressSubspace::RandomPageAddress() {
  SpinningMutexGuard guard(&mutex_);
  Address addr = base() + (static_cast<uint64_t>(rng_.NextInt64()) % size());
  return RoundDown(addr, allocation_granularity());
}

Address EmulatedVirtualAddressSubspace::AllocatePages(
    Address hint, size_t size, size_t alignment, PagePermissions permissions) {
  if (hint == kNoHint || MappedRegionContains(hint, size)) {
    SpinningMutexGuard guard(&mutex_);

    // Attempt to find a region in the mapped region.
    Address address = region_allocator_.AllocateRegion(hint, size, alignment);
    if (address != RegionAllocator::kAllocationFailure) {
      // Success. Only need to adjust the page permissions.
      if (parent_space_->SetPagePermissions(address, size, permissions)) {
        return address;
      }
      // Probably ran out of memory, but still try to allocate in the unmapped
      // space.
      CHECK_EQ(size, region_allocator_.FreeRegion(address));
    }
  }

  // No luck or hint is outside of the mapped region. Try to allocate pages in
  // the unmapped space using page allocation hints instead.
  if (!IsUsableSizeForUnmappedRegion(size)) return kNullAddress;

  static constexpr int kMaxAttempts = 10;
  for (int i = 0; i < kMaxAttempts; i++) {
    // If an unmapped region exists, it must cover at least 50% of the whole
    // space (unmapped + mapped region). Since we limit the size of allocation
    // to 50% of the unmapped region (see IsUsableSizeForUnmappedRegion), a
    // random page address has at least a 25% chance of being a usable base. As
    // such, this loop should usually terminate quickly.
    DCHECK_GE(unmapped_size(), mapped_size());
    while (!UnmappedRegionContains(hint, size)) {
      hint = RandomPageAddress();
    }
    hint = RoundDown(hint, alignment);

    const Address result =
        parent_space_->AllocatePages(hint, size, alignment, permissions);
    if (UnmappedRegionContains(result, size)) {
      return result;
    } else if (result) {
      parent_space_->FreePages(result, size);
    }

    // Retry at a different address.
    hint = RandomPageAddress();
  }

  return kNullAddress;
}

void EmulatedVirtualAddressSubspace::FreePages(Address address, size_t size) {
  if (MappedRegionContains(address, size)) {
    SpinningMutexGuard guard(&mutex_);
    CHECK_EQ(size, region_allocator_.FreeRegion(address));
    CHECK(parent_space_->DecommitPages(address, size));
  } else {
    DCHECK(UnmappedRegionContains(address, size));
    parent_space_->FreePages(address, size);
  }
}

Address EmulatedVirtualAddressSubspace::AllocateSharedPages(
    Address hint, size_t size, PagePermissions permissions,
    PlatformSharedMemoryHandle handle, uint64_t offset) {
  // Can only allocate shared pages in the unmapped region.
  if (!IsUsableSizeForUnmappedRegion(size)) return kNullAddress;

  static constexpr int kMaxAttempts = 10;
  for (int i = 0; i < kMaxAttempts; i++) {
    // See AllocatePages() for why this loop usually terminates quickly.
    DCHECK_GE(unmapped_size(), mapped_size());
    while (!UnmappedRegionContains(hint, size)) {
      hint = RandomPageAddress();
    }

    Address region = parent_space_->AllocateSharedPages(hint, size, permissions,
                                                        handle, offset);
    if (UnmappedRegionContains(region, size)) {
      return region;
    } else if (region) {
      parent_space_->FreeSharedPages(region, size);
    }

    hint = RandomPageAddress();
  }

  return kNullAddress;
}

void EmulatedVirtualAddressSubspace::FreeSharedPages(Address address,
                                                     size_t size) {
  DCHECK(UnmappedRegionContains(address, size));
  parent_space_->FreeSharedPages(address, size);
}

bool EmulatedVirtualAddressSubspace::SetPagePermissions(
    Address address, size_t size, PagePermissions permissions) {
  DCHECK(Contains(address, size));
  return parent_space_->SetPagePermissions(address, size, permissions);
}

bool EmulatedVirtualAddressSubspace::AllocateGuardRegion(Address address,
                                                         size_t size) {
  if (MappedRegionContains(address, size)) {
    SpinningMutexGuard guard(&mutex_);
    return region_allocator_.AllocateRegionAt(address, size);
  }
  if (!UnmappedRegionContains(address, size)) return false;
  return parent_space_->AllocateGuardRegion(address, size);
}

void EmulatedVirtualAddressSubspace::FreeGuardRegion(Address address,
                                                     size_t size) {
  if (MappedRegionContains(address, size)) {
    SpinningMutexGuard guard(&mutex_);
    CHECK_EQ(size, region_allocator_.FreeRegion(address));
  } else {
    DCHECK(UnmappedRegionContains(address, size));
    parent_space_->FreeGuardRegion(address, size);
  }
}

bool EmulatedVirtualAddressSubspace::CanAllocateSubspaces() {
  // This is not supported, mostly because it's not (yet) needed in practice.
  return false;
}

std::unique_ptr<v8::VirtualAddressSpace>
EmulatedVirtualAddressSubspace::AllocateSubspace(
    Address hint, size_t size, size_t alignment,
    PagePermissions max_page_permissions) {
  UNREACHABLE();
}

bool EmulatedVirtualAddressSubspace::RecommitPages(
    Address address, size_t size, PagePermissions permissions) {
  DCHECK(Contains(address, size));
  return parent_space_->RecommitPages(address, size, permissions);
}

bool EmulatedVirtualAddressSubspace::DiscardSystemPages(Address address,
                                                        size_t size) {
  DCHECK(Contains(address, size));
  return parent_space_->DiscardSystemPages(address, size);
}

bool EmulatedVirtualAddressSubspace::DecommitPages(Address address,
                                                   size_t size) {
  DCHECK(Contains(address, size));
  return parent_space_->DecommitPages(address, size);
}

}  // namespace base
}  // namespace v8
