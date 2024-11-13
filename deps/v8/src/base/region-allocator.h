// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_REGION_ALLOCATOR_H_
#define V8_BASE_REGION_ALLOCATOR_H_

#include <set>

#include "src/base/address-region.h"
#include "src/base/utils/random-number-generator.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace base {

// Helper class for managing used/free regions within [address, address+size)
// region. Minimum allocation unit is |page_size|. Requested allocation size
// is rounded up to |page_size|.
// The region allocation algorithm implements best-fit with coalescing strategy:
// it tries to find a smallest suitable free region upon allocation and tries
// to merge region with its neighbors upon freeing.
//
// This class does not perform any actual region reservation.
// Not thread-safe.
class V8_BASE_EXPORT RegionAllocator final {
 public:
  using Address = uintptr_t;

  using SplitMergeCallback = std::function<void(Address start, size_t size)>;

  static constexpr Address kAllocationFailure = static_cast<Address>(-1);

  enum class RegionState {
    // The region can be allocated from.
    kFree,
    // The region has been carved out of the wider area and is not allocatable.
    kExcluded,
    // The region has been allocated and is managed by a RegionAllocator.
    kAllocated,
  };

  RegionAllocator(Address address, size_t size, size_t page_size);
  RegionAllocator(const RegionAllocator&) = delete;
  RegionAllocator& operator=(const RegionAllocator&) = delete;
  ~RegionAllocator();

  // Split and merge callbacks.
  //
  // These callbacks can be installed to perform additional logic when regions
  // are split or merged. For example, when managing Windows placeholder
  // regions, a region must be split into sub-regions (using
  // VirtualFree(MEM_PRESERVE_PLACEHOLDER)) before a part of it can be replaced
  // with an actual memory mapping. Similarly, multiple sub-regions must be
  // merged (using VirtualFree(MEM_COALESCE_PLACEHOLDERS)) when coalescing them
  // into a larger, free region again.
  //
  // The on_split callback is called to signal that an existing region is split
  // so that [start, start+size) becomes a new region.
  void set_on_split_callback(SplitMergeCallback callback) {
    on_split_ = callback;
  }
  // The on_merge callback is called to signal that all regions in the range
  // [start, start+size) are merged into a single one.
  void set_on_merge_callback(SplitMergeCallback callback) {
    on_merge_ = callback;
  }

  // Allocates region of |size| (must be |page_size|-aligned). Returns
  // the address of the region on success or kAllocationFailure.
  Address AllocateRegion(size_t size);
  // Same as above but tries to randomize the region displacement.
  Address AllocateRegion(RandomNumberGenerator* rng, size_t size);

  // Allocates region of |size| at |requested_address| if it's free. Both the
  // address and the size must be |page_size|-aligned. On success returns
  // true.
  // This kind of allocation is supposed to be used during setup phase to mark
  // certain regions as used or for randomizing regions displacement.
  // By default regions are marked as used, but can also be allocated as
  // RegionState::kExcluded to prevent the RegionAllocator from using that
  // memory range, which is useful when reserving any area to remap shared
  // memory into.
  bool AllocateRegionAt(Address requested_address, size_t size,
                        RegionState region_state = RegionState::kAllocated);

  // Allocates a region of |size| aligned to |alignment|. The size and alignment
  // must be a multiple of |page_size|. Returns the address of the region on
  // success or kAllocationFailure.
  Address AllocateAlignedRegion(size_t size, size_t alignment);

  // Attempts to allocate a region of the given size and alignment at the
  // specified address but fall back to allocating the region elsewhere if
  // necessary.
  Address AllocateRegion(Address hint, size_t size, size_t alignment);

  // Frees region at given |address|, returns the size of the region.
  // There must be a used region starting at given address otherwise nothing
  // will be freed and 0 will be returned.
  size_t FreeRegion(Address address) { return TrimRegion(address, 0); }

  // Decreases size of the previously allocated region at |address|, returns
  // freed size. |new_size| must be |page_size|-aligned and
  // less than or equal to current region's size. Setting new size to zero
  // frees the region.
  size_t TrimRegion(Address address, size_t new_size);

  // If there is a used region starting at given address returns its size
  // otherwise 0.
  size_t CheckRegion(Address address);

  // Returns true if there are no pages allocated in given region.
  bool IsFree(Address address, size_t size);

  Address begin() const { return whole_region_.begin(); }
  Address end() const { return whole_region_.end(); }
  size_t size() const { return whole_region_.size(); }

  bool contains(Address address) const {
    return whole_region_.contains(address);
  }

  bool contains(Address address, size_t size) const {
    return whole_region_.contains(address, size);
  }

  // Total size of not yet acquired regions.
  size_t free_size() const { return free_size_; }

  // The alignment of the allocated region's addresses and granularity of
  // the allocated region's sizes.
  size_t page_size() const { return page_size_; }

  void Print(std::ostream& os) const;

 private:
  class Region : public AddressRegion {
   public:
    Region(Address address, size_t size, RegionState state)
        : AddressRegion(address, size), state_(state) {}

    bool is_free() const { return state_ == RegionState::kFree; }
    bool is_allocated() const { return state_ == RegionState::kAllocated; }
    bool is_excluded() const { return state_ == RegionState::kExcluded; }

    RegionState state() { return state_; }
    void set_state(RegionState state) { state_ = state; }

    void Print(std::ostream& os) const;

   private:
    RegionState state_;
  };

  // The whole region.
  const Region whole_region_;

  // Number of |page_size_| in the whole region.
  const size_t region_size_in_pages_;

  // If the free size is less than this value - stop trying to randomize the
  // allocation addresses.
  const size_t max_load_for_randomization_;

  // Size of all free regions.
  size_t free_size_;

  // Minimum region size. Must be a pow of 2.
  const size_t page_size_;

  struct AddressEndOrder {
    bool operator()(const Region* a, const Region* b) const {
      return a->end() < b->end();
    }
  };
  // All regions ordered by addresses.
  using AllRegionsSet = std::set<Region*, AddressEndOrder>;
  AllRegionsSet all_regions_;

  struct SizeAddressOrder {
    bool operator()(const Region* a, const Region* b) const {
      if (a->size() != b->size()) return a->size() < b->size();
      return a->begin() < b->begin();
    }
  };
  // Free regions ordered by sizes and addresses.
  std::set<Region*, SizeAddressOrder> free_regions_;

  // Callbacks called when regions are split or merged.
  SplitMergeCallback on_split_;
  SplitMergeCallback on_merge_;

  // Returns region containing given address or nullptr.
  AllRegionsSet::iterator FindRegion(Address address);

  // Adds given region to the set of free regions.
  void FreeListAddRegion(Region* region);

  // Finds best-fit free region for given size.
  Region* FreeListFindRegion(size_t size);

  // Removes given region from the set of free regions.
  void FreeListRemoveRegion(Region* region);

  // Splits given |region| into two: one of |new_size| size and a new one
  // having the rest. The new region is returned.
  Region* Split(Region* region, size_t new_size);

  // For two coalescing regions merges |next| to |prev| and deletes |next|.
  void Merge(AllRegionsSet::iterator prev_iter,
             AllRegionsSet::iterator next_iter);

  FRIEND_TEST(RegionAllocatorTest, AllocateExcluded);
  FRIEND_TEST(RegionAllocatorTest, AllocateRegionRandom);
  FRIEND_TEST(RegionAllocatorTest, Contains);
  FRIEND_TEST(RegionAllocatorTest, FindRegion);
  FRIEND_TEST(RegionAllocatorTest, Fragmentation);
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_REGION_ALLOCATOR_H_
