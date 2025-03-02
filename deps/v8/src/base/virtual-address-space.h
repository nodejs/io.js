// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_VIRTUAL_ADDRESS_SPACE_H_
#define V8_BASE_VIRTUAL_ADDRESS_SPACE_H_

#include "include/v8-platform.h"
#include "src/base/base-export.h"
#include "src/base/compiler-specific.h"
#include "src/base/platform/platform.h"
#include "src/base/region-allocator.h"

namespace v8 {
namespace base {

using Address = uintptr_t;
constexpr Address kNullAddress = 0;

class VirtualAddressSubspace;

/*
 * Common parent class to implement deletion of subspaces.
 */
class VirtualAddressSpaceBase
    : public NON_EXPORTED_BASE(::v8::VirtualAddressSpace) {
 public:
  using VirtualAddressSpace::VirtualAddressSpace;

 private:
  friend VirtualAddressSubspace;
  // Called by a subspace during destruction. Responsible for freeing the
  // address space reservation and any other data associated with the subspace
  // in the parent space.
  virtual void FreeSubspace(VirtualAddressSubspace* subspace) = 0;
};

/*
 * Helper routine to determine whether one set of page permissions (the lhs) is
 * a subset of another one (the rhs).
 */
V8_BASE_EXPORT bool IsSubset(PagePermissions lhs, PagePermissions rhs);

/*
 * The virtual address space of the current process. Conceptionally, there
 * should only be one such "root" instance. However, in practice there is no
 * issue with having multiple instances as the actual resources are managed by
 * the OS kernel.
 */
class V8_BASE_EXPORT VirtualAddressSpace : public VirtualAddressSpaceBase {
 public:
  VirtualAddressSpace();
  ~VirtualAddressSpace() override = default;

  void SetRandomSeed(int64_t seed) override;

  Address RandomPageAddress() override;

  Address AllocatePages(Address hint, size_t size, size_t alignment,
                        PagePermissions access) override;

  void FreePages(Address address, size_t size) override;

  bool SetPagePermissions(Address address, size_t size,
                          PagePermissions access) override;

  bool AllocateGuardRegion(Address address, size_t size) override;

  void FreeGuardRegion(Address address, size_t size) override;

  Address AllocateSharedPages(Address hint, size_t size,
                              PagePermissions permissions,
                              PlatformSharedMemoryHandle handle,
                              uint64_t offset) override;

  void FreeSharedPages(Address address, size_t size) override;

  bool CanAllocateSubspaces() override;

  std::unique_ptr<v8::VirtualAddressSpace> AllocateSubspace(
      Address hint, size_t size, size_t alignment,
      PagePermissions max_page_permissions) override;

  bool RecommitPages(Address address, size_t size,
                     PagePermissions access) override;

  bool DiscardSystemPages(Address address, size_t size) override;

  bool DecommitPages(Address address, size_t size) override;

 private:
  void FreeSubspace(VirtualAddressSubspace* subspace) override;
};

/*
 * A subspace of a parent virtual address space. This represents a reserved
 * contiguous region of virtual address space in the current process.
 */
class V8_BASE_EXPORT VirtualAddressSubspace : public VirtualAddressSpaceBase {
 public:
  ~VirtualAddressSubspace() override;

  void SetRandomSeed(int64_t seed) override;

  Address RandomPageAddress() override;

  Address AllocatePages(Address hint, size_t size, size_t alignment,
                        PagePermissions permissions) override;

  void FreePages(Address address, size_t size) override;

  bool SetPagePermissions(Address address, size_t size,
                          PagePermissions permissions) override;

  bool AllocateGuardRegion(Address address, size_t size) override;

  void FreeGuardRegion(Address address, size_t size) override;

  Address AllocateSharedPages(Address hint, size_t size,
                              PagePermissions permissions,
                              PlatformSharedMemoryHandle handle,
                              uint64_t offset) override;

  void FreeSharedPages(Address address, size_t size) override;

  bool CanAllocateSubspaces() override { return true; }

  std::unique_ptr<v8::VirtualAddressSpace> AllocateSubspace(
      Address hint, size_t size, size_t alignment,
      PagePermissions max_page_permissions) override;

  bool RecommitPages(Address address, size_t size,
                     PagePermissions permissions) override;

  bool DiscardSystemPages(Address address, size_t size) override;

  bool DecommitPages(Address address, size_t size) override;

 private:
  // The VirtualAddressSpace class creates instances of this class when
  // allocating sub spaces.
  friend class v8::base::VirtualAddressSpace;

  void FreeSubspace(VirtualAddressSubspace* subspace) override;

  VirtualAddressSubspace(AddressSpaceReservation reservation,
                         VirtualAddressSpaceBase* parent_space,
                         PagePermissions max_page_permissions);

  // The address space reservation backing this subspace.
  AddressSpaceReservation reservation_;

  // Mutex guarding the non-threadsafe RegionAllocator and
  // RandomNumberGenerator.
  SpinningMutex mutex_;

  // RegionAllocator to manage the virtual address reservation and divide it
  // into further regions as necessary.
  RegionAllocator region_allocator_;

  // Random number generator for generating random addresses.
  RandomNumberGenerator rng_;

  // Pointer to the parent space. Must be kept alive by the owner of this
  // instance during its lifetime.
  VirtualAddressSpaceBase* parent_space_;
};

}  // namespace base
}  // namespace v8
#endif  // V8_BASE_VIRTUAL_ADDRESS_SPACE_H_
