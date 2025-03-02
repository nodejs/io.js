// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_EMULATED_VIRTUAL_ADDRESS_SUBSPACE_H_
#define V8_BASE_EMULATED_VIRTUAL_ADDRESS_SUBSPACE_H_

#include "include/v8-platform.h"
#include "src/base/base-export.h"
#include "src/base/compiler-specific.h"
#include "src/base/platform/mutex.h"
#include "src/base/region-allocator.h"
#include "src/base/virtual-address-space.h"

namespace v8 {
namespace base {

/**
 * Emulates a virtual address subspace.
 *
 * This class is (optionally) backed by a page allocation and emulates a virtual
 * address space that is potentially larger than that mapping. It generally
 * first attempts to satisfy page allocation requests from its backing mapping,
 * but will also attempt to obtain new page mappings inside the unmapped space
 * through page allocation hints if necessary.
 *
 * Caveat: an emulated subspace violates the invariant that page allocations in
 * an address space will never end up inside a child space and so does not
 * provide the same security gurarantees.
 */
class V8_BASE_EXPORT EmulatedVirtualAddressSubspace final
    : public NON_EXPORTED_BASE(::v8::VirtualAddressSpace) {
 public:
  // Construct an emulated virtual address subspace of the specified total size,
  // potentially backed by a page allocation from the parent space. The newly
  // created instance takes ownership of the page allocation (if any) and frees
  // it during destruction.
  EmulatedVirtualAddressSubspace(v8::VirtualAddressSpace* parent_space,
                                 Address base, size_t mapped_size,
                                 size_t total_size);

  ~EmulatedVirtualAddressSubspace() override;

  void SetRandomSeed(int64_t seed) override;

  Address RandomPageAddress() override;

  Address AllocatePages(Address hint, size_t size, size_t alignment,
                        PagePermissions permissions) override;

  void FreePages(Address address, size_t size) override;

  Address AllocateSharedPages(Address hint, size_t size,
                              PagePermissions permissions,
                              PlatformSharedMemoryHandle handle,
                              uint64_t offset) override;

  void FreeSharedPages(Address address, size_t size) override;

  bool SetPagePermissions(Address address, size_t size,
                          PagePermissions permissions) override;

  bool AllocateGuardRegion(Address address, size_t size) override;

  void FreeGuardRegion(Address address, size_t size) override;

  bool CanAllocateSubspaces() override;

  std::unique_ptr<v8::VirtualAddressSpace> AllocateSubspace(
      Address hint, size_t size, size_t alignment,
      PagePermissions max_page_permissions) override;

  bool RecommitPages(Address address, size_t size,
                     PagePermissions permissions) override;

  bool DiscardSystemPages(Address address, size_t size) override;

  bool DecommitPages(Address address, size_t size) override;

 private:
  size_t mapped_size() const { return mapped_size_; }
  size_t unmapped_size() const { return size() - mapped_size_; }

  Address mapped_base() const { return base(); }
  Address unmapped_base() const { return base() + mapped_size_; }

  bool Contains(Address outer_start, size_t outer_size, Address inner_start,
                size_t inner_size) const {
    return (inner_start >= outer_start) &&
           ((inner_start + inner_size) <= (outer_start + outer_size));
  }

  bool Contains(Address addr, size_t length) const {
    return Contains(base(), size(), addr, length);
  }

  bool MappedRegionContains(Address addr, size_t length) const {
    return Contains(mapped_base(), mapped_size(), addr, length);
  }

  bool UnmappedRegionContains(Address addr, size_t length) const {
    return Contains(unmapped_base(), unmapped_size(), addr, length);
  }

  // Helper function to define a limit for the size of allocations in the
  // unmapped region. This limit makes it possible to estimate the expected
  // runtime of some loops in the Allocate methods.
  bool IsUsableSizeForUnmappedRegion(size_t size) const {
    return size <= (unmapped_size() / 2);
  }

  // Size of the mapped region located at the beginning of this address space.
  const size_t mapped_size_;

  // Pointer to the parent space from which the backing pages were allocated.
  // Must be kept alive by the owner of this instance.
  v8::VirtualAddressSpace* parent_space_;

  // Mutex guarding the non-threadsafe RegionAllocator and
  // RandomNumberGenerator.
  SpinningMutex mutex_;

  // RegionAllocator to manage the page allocation and divide it into further
  // regions as necessary.
  RegionAllocator region_allocator_;

  // Random number generator for generating random addresses.
  RandomNumberGenerator rng_;
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_EMULATED_VIRTUAL_ADDRESS_SUBSPACE_H_
