// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/virtual-address-space.h"

#include <optional>

#include "include/v8-platform.h"
#include "src/base/bits.h"
#include "src/base/platform/platform.h"

namespace v8 {
namespace base {

#define STATIC_ASSERT_ENUM(a, b)                            \
  static_assert(static_cast<int>(a) == static_cast<int>(b), \
                "mismatching enum: " #a)

STATIC_ASSERT_ENUM(PagePermissions::kNoAccess, OS::MemoryPermission::kNoAccess);
STATIC_ASSERT_ENUM(PagePermissions::kReadWrite,
                   OS::MemoryPermission::kReadWrite);
STATIC_ASSERT_ENUM(PagePermissions::kReadWriteExecute,
                   OS::MemoryPermission::kReadWriteExecute);
STATIC_ASSERT_ENUM(PagePermissions::kReadExecute,
                   OS::MemoryPermission::kReadExecute);

#undef STATIC_ASSERT_ENUM

namespace {
uint8_t PagePermissionsToBitset(PagePermissions permissions) {
  switch (permissions) {
    case PagePermissions::kNoAccess:
      return 0b000;
    case PagePermissions::kRead:
      return 0b100;
    case PagePermissions::kReadWrite:
      return 0b110;
    case PagePermissions::kReadWriteExecute:
      return 0b111;
    case PagePermissions::kReadExecute:
      return 0b101;
  }
}
}  // namespace

bool IsSubset(PagePermissions lhs, PagePermissions rhs) {
  uint8_t lhs_bits = PagePermissionsToBitset(lhs);
  uint8_t rhs_bits = PagePermissionsToBitset(rhs);
  return (lhs_bits & rhs_bits) == lhs_bits;
}

VirtualAddressSpace::VirtualAddressSpace()
    : VirtualAddressSpaceBase(OS::CommitPageSize(), OS::AllocatePageSize(),
                              kNullAddress,
                              std::numeric_limits<uintptr_t>::max(),
                              PagePermissions::kReadWriteExecute) {
#if V8_OS_WIN
  // On Windows, this additional step is required to lookup the VirtualAlloc2
  // and friends functions.
  OS::EnsureWin32MemoryAPILoaded();
#endif  // V8_OS_WIN
  DCHECK(bits::IsPowerOfTwo(page_size()));
  DCHECK(bits::IsPowerOfTwo(allocation_granularity()));
  DCHECK_GE(allocation_granularity(), page_size());
  DCHECK(IsAligned(allocation_granularity(), page_size()));
}

void VirtualAddressSpace::SetRandomSeed(int64_t seed) {
  OS::SetRandomMmapSeed(seed);
}

Address VirtualAddressSpace::RandomPageAddress() {
  return reinterpret_cast<Address>(OS::GetRandomMmapAddr());
}

Address VirtualAddressSpace::AllocatePages(Address hint, size_t size,
                                           size_t alignment,
                                           PagePermissions permissions) {
  DCHECK(IsAligned(alignment, allocation_granularity()));
  DCHECK(IsAligned(hint, alignment));
  DCHECK(IsAligned(size, allocation_granularity()));

  return reinterpret_cast<Address>(
      OS::Allocate(reinterpret_cast<void*>(hint), size, alignment,
                   static_cast<OS::MemoryPermission>(permissions)));
}

void VirtualAddressSpace::FreePages(Address address, size_t size) {
  DCHECK(IsAligned(address, allocation_granularity()));
  DCHECK(IsAligned(size, allocation_granularity()));

  OS::Free(reinterpret_cast<void*>(address), size);
}

bool VirtualAddressSpace::SetPagePermissions(Address address, size_t size,
                                             PagePermissions permissions) {
  DCHECK(IsAligned(address, page_size()));
  DCHECK(IsAligned(size, page_size()));

  return OS::SetPermissions(reinterpret_cast<void*>(address), size,
                            static_cast<OS::MemoryPermission>(permissions));
}

bool VirtualAddressSpace::AllocateGuardRegion(Address address, size_t size) {
  DCHECK(IsAligned(address, allocation_granularity()));
  DCHECK(IsAligned(size, allocation_granularity()));

  void* hint = reinterpret_cast<void*>(address);
  void* result = OS::Allocate(hint, size, allocation_granularity(),
                              OS::MemoryPermission::kNoAccess);
  if (result && result != hint) {
    OS::Free(result, size);
  }
  return result == hint;
}

void VirtualAddressSpace::FreeGuardRegion(Address address, size_t size) {
  DCHECK(IsAligned(address, allocation_granularity()));
  DCHECK(IsAligned(size, allocation_granularity()));

  OS::Free(reinterpret_cast<void*>(address), size);
}

bool VirtualAddressSpace::CanAllocateSubspaces() {
  return OS::CanReserveAddressSpace();
}

Address VirtualAddressSpace::AllocateSharedPages(
    Address hint, size_t size, PagePermissions permissions,
    PlatformSharedMemoryHandle handle, uint64_t offset) {
  DCHECK(IsAligned(hint, allocation_granularity()));
  DCHECK(IsAligned(size, allocation_granularity()));
  DCHECK(IsAligned(offset, allocation_granularity()));

  return reinterpret_cast<Address>(OS::AllocateShared(
      reinterpret_cast<void*>(hint), size,
      static_cast<OS::MemoryPermission>(permissions), handle, offset));
}

void VirtualAddressSpace::FreeSharedPages(Address address, size_t size) {
  DCHECK(IsAligned(address, allocation_granularity()));
  DCHECK(IsAligned(size, allocation_granularity()));

  OS::FreeShared(reinterpret_cast<void*>(address), size);
}

std::unique_ptr<v8::VirtualAddressSpace> VirtualAddressSpace::AllocateSubspace(
    Address hint, size_t size, size_t alignment,
    PagePermissions max_page_permissions) {
  DCHECK(IsAligned(alignment, allocation_granularity()));
  DCHECK(IsAligned(hint, alignment));
  DCHECK(IsAligned(size, allocation_granularity()));

  std::optional<AddressSpaceReservation> reservation =
      OS::CreateAddressSpaceReservation(
          reinterpret_cast<void*>(hint), size, alignment,
          static_cast<OS::MemoryPermission>(max_page_permissions));
  if (!reservation.has_value())
    return std::unique_ptr<v8::VirtualAddressSpace>();
  return std::unique_ptr<v8::VirtualAddressSpace>(
      new VirtualAddressSubspace(*reservation, this, max_page_permissions));
}

bool VirtualAddressSpace::RecommitPages(Address address, size_t size,
                                        PagePermissions permissions) {
  DCHECK(IsAligned(address, page_size()));
  DCHECK(IsAligned(size, page_size()));

  return OS::RecommitPages(reinterpret_cast<void*>(address), size,
                           static_cast<OS::MemoryPermission>(permissions));
}

bool VirtualAddressSpace::DiscardSystemPages(Address address, size_t size) {
  DCHECK(IsAligned(address, page_size()));
  DCHECK(IsAligned(size, page_size()));

  return OS::DiscardSystemPages(reinterpret_cast<void*>(address), size);
}

bool VirtualAddressSpace::DecommitPages(Address address, size_t size) {
  DCHECK(IsAligned(address, page_size()));
  DCHECK(IsAligned(size, page_size()));

  return OS::DecommitPages(reinterpret_cast<void*>(address), size);
}

void VirtualAddressSpace::FreeSubspace(VirtualAddressSubspace* subspace) {
  OS::FreeAddressSpaceReservation(subspace->reservation_);
}

VirtualAddressSubspace::VirtualAddressSubspace(
    AddressSpaceReservation reservation, VirtualAddressSpaceBase* parent_space,
    PagePermissions max_page_permissions)
    : VirtualAddressSpaceBase(parent_space->page_size(),
                              parent_space->allocation_granularity(),
                              reinterpret_cast<Address>(reservation.base()),
                              reservation.size(), max_page_permissions),
      reservation_(reservation),
      region_allocator_(reinterpret_cast<Address>(reservation.base()),
                        reservation.size(),
                        parent_space->allocation_granularity()),
      parent_space_(parent_space) {
#if V8_OS_WIN
  // On Windows, the address space reservation needs to be split and merged at
  // the OS level as well.
  region_allocator_.set_on_split_callback([this](Address start, size_t size) {
    DCHECK(IsAligned(start, allocation_granularity()));
    CHECK(reservation_.SplitPlaceholder(reinterpret_cast<void*>(start), size));
  });
  region_allocator_.set_on_merge_callback([this](Address start, size_t size) {
    DCHECK(IsAligned(start, allocation_granularity()));
    CHECK(reservation_.MergePlaceholders(reinterpret_cast<void*>(start), size));
  });
#endif  // V8_OS_WIN
}

VirtualAddressSubspace::~VirtualAddressSubspace() {
  // TODO(chromium:1218005) here or in the RegionAllocator destructor we should
  // assert that all allocations have been freed. Otherwise we may end up
  // leaking memory on Windows because VirtualFree(subspace_base, 0) will then
  // only free the first allocation in the subspace, not the entire subspace.
  parent_space_->FreeSubspace(this);
}

void VirtualAddressSubspace::SetRandomSeed(int64_t seed) {
  MutexGuard guard(&mutex_);
  rng_.SetSeed(seed);
}

Address VirtualAddressSubspace::RandomPageAddress() {
  MutexGuard guard(&mutex_);
  // Note: the random numbers generated here aren't uniformly distributed if the
  // size isn't a power of two.
  Address addr = base() + (static_cast<uint64_t>(rng_.NextInt64()) % size());
  return RoundDown(addr, allocation_granularity());
}

Address VirtualAddressSubspace::AllocatePages(Address hint, size_t size,
                                              size_t alignment,
                                              PagePermissions permissions) {
  DCHECK(IsAligned(alignment, allocation_granularity()));
  DCHECK(IsAligned(hint, alignment));
  DCHECK(IsAligned(size, allocation_granularity()));
  DCHECK(IsSubset(permissions, max_page_permissions()));

  MutexGuard guard(&mutex_);

  Address address = region_allocator_.AllocateRegion(hint, size, alignment);
  if (address == RegionAllocator::kAllocationFailure) return kNullAddress;

  if (!reservation_.Allocate(reinterpret_cast<void*>(address), size,
                             static_cast<OS::MemoryPermission>(permissions))) {
    // This most likely means that we ran out of memory.
    CHECK_EQ(size, region_allocator_.FreeRegion(address));
    return kNullAddress;
  }

  return address;
}

void VirtualAddressSubspace::FreePages(Address address, size_t size) {
  DCHECK(IsAligned(address, allocation_granularity()));
  DCHECK(IsAligned(size, allocation_granularity()));

  MutexGuard guard(&mutex_);
  // The order here is important: on Windows, the allocation first has to be
  // freed to a placeholder before the placeholder can be merged (during the
  // merge_callback) with any surrounding placeholder mappings.
  if (!reservation_.Free(reinterpret_cast<void*>(address), size)) {
    // This can happen due to an out-of-memory condition, such as running out
    // of available VMAs for the process.
    FatalOOM(OOMType::kProcess, "VirtualAddressSubspace::FreePages");
  }
  CHECK_EQ(size, region_allocator_.FreeRegion(address));
}

bool VirtualAddressSubspace::SetPagePermissions(Address address, size_t size,
                                                PagePermissions permissions) {
  DCHECK(IsAligned(address, page_size()));
  DCHECK(IsAligned(size, page_size()));
  DCHECK(IsSubset(permissions, max_page_permissions()));

  return reservation_.SetPermissions(
      reinterpret_cast<void*>(address), size,
      static_cast<OS::MemoryPermission>(permissions));
}

bool VirtualAddressSubspace::AllocateGuardRegion(Address address, size_t size) {
  DCHECK(IsAligned(address, allocation_granularity()));
  DCHECK(IsAligned(size, allocation_granularity()));

  MutexGuard guard(&mutex_);

  // It is guaranteed that reserved address space is inaccessible, so we just
  // need to mark the region as in-use in the region allocator.
  return region_allocator_.AllocateRegionAt(address, size);
}

void VirtualAddressSubspace::FreeGuardRegion(Address address, size_t size) {
  DCHECK(IsAligned(address, allocation_granularity()));
  DCHECK(IsAligned(size, allocation_granularity()));

  MutexGuard guard(&mutex_);
  CHECK_EQ(size, region_allocator_.FreeRegion(address));
}

Address VirtualAddressSubspace::AllocateSharedPages(
    Address hint, size_t size, PagePermissions permissions,
    PlatformSharedMemoryHandle handle, uint64_t offset) {
  DCHECK(IsAligned(hint, allocation_granularity()));
  DCHECK(IsAligned(size, allocation_granularity()));
  DCHECK(IsAligned(offset, allocation_granularity()));

  MutexGuard guard(&mutex_);

  Address address =
      region_allocator_.AllocateRegion(hint, size, allocation_granularity());
  if (address == RegionAllocator::kAllocationFailure) return kNullAddress;

  if (!reservation_.AllocateShared(
          reinterpret_cast<void*>(address), size,
          static_cast<OS::MemoryPermission>(permissions), handle, offset)) {
    CHECK_EQ(size, region_allocator_.FreeRegion(address));
    return kNullAddress;
  }

  return address;
}

void VirtualAddressSubspace::FreeSharedPages(Address address, size_t size) {
  DCHECK(IsAligned(address, allocation_granularity()));
  DCHECK(IsAligned(size, allocation_granularity()));

  MutexGuard guard(&mutex_);
  // The order here is important: on Windows, the allocation first has to be
  // freed to a placeholder before the placeholder can be merged (during the
  // merge_callback) with any surrounding placeholder mappings.
  CHECK(reservation_.FreeShared(reinterpret_cast<void*>(address), size));
  CHECK_EQ(size, region_allocator_.FreeRegion(address));
}

std::unique_ptr<v8::VirtualAddressSpace>
VirtualAddressSubspace::AllocateSubspace(Address hint, size_t size,
                                         size_t alignment,
                                         PagePermissions max_page_permissions) {
  DCHECK(IsAligned(alignment, allocation_granularity()));
  DCHECK(IsAligned(hint, alignment));
  DCHECK(IsAligned(size, allocation_granularity()));
  DCHECK(IsSubset(max_page_permissions, this->max_page_permissions()));

  MutexGuard guard(&mutex_);

  Address address = region_allocator_.AllocateRegion(hint, size, alignment);
  if (address == RegionAllocator::kAllocationFailure) {
    return std::unique_ptr<v8::VirtualAddressSpace>();
  }

  std::optional<AddressSpaceReservation> reservation =
      reservation_.CreateSubReservation(
          reinterpret_cast<void*>(address), size,
          static_cast<OS::MemoryPermission>(max_page_permissions));
  if (!reservation.has_value()) {
    CHECK_EQ(size, region_allocator_.FreeRegion(address));
    return nullptr;
  }
  return std::unique_ptr<v8::VirtualAddressSpace>(
      new VirtualAddressSubspace(*reservation, this, max_page_permissions));
}

bool VirtualAddressSubspace::RecommitPages(Address address, size_t size,
                                           PagePermissions permissions) {
  DCHECK(IsAligned(address, page_size()));
  DCHECK(IsAligned(size, page_size()));
  DCHECK(IsSubset(permissions, max_page_permissions()));

  return reservation_.RecommitPages(
      reinterpret_cast<void*>(address), size,
      static_cast<OS::MemoryPermission>(permissions));
}

bool VirtualAddressSubspace::DiscardSystemPages(Address address, size_t size) {
  DCHECK(IsAligned(address, page_size()));
  DCHECK(IsAligned(size, page_size()));

  return reservation_.DiscardSystemPages(reinterpret_cast<void*>(address),
                                         size);
}

bool VirtualAddressSubspace::DecommitPages(Address address, size_t size) {
  DCHECK(IsAligned(address, page_size()));
  DCHECK(IsAligned(size, page_size()));

  return reservation_.DecommitPages(reinterpret_cast<void*>(address), size);
}

void VirtualAddressSubspace::FreeSubspace(VirtualAddressSubspace* subspace) {
  MutexGuard guard(&mutex_);

  AddressSpaceReservation reservation = subspace->reservation_;
  Address base = reinterpret_cast<Address>(reservation.base());
  CHECK_EQ(reservation.size(), region_allocator_.FreeRegion(base));
  CHECK(reservation_.FreeSubReservation(reservation));
}

}  // namespace base
}  // namespace v8
