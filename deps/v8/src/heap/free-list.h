// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FREE_LIST_H_
#define V8_HEAP_FREE_LIST_H_

#include <atomic>

#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/heap/allocation-result.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/objects/free-space.h"
#include "src/objects/map.h"
#include "src/utils/utils.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace internal {

namespace heap {
class HeapTester;
class TestCodePageAllocatorScope;
}  // namespace heap

class AllocationObserver;
class FreeList;
class Isolate;
class LargeObjectSpace;
class LargePageMetadata;
class LinearAllocationArea;
class PageMetadata;
class PagedSpace;
class SemiSpace;

using FreeListCategoryType = int32_t;

static constexpr FreeListCategoryType kFirstCategory = 0;
static constexpr FreeListCategoryType kInvalidCategory = -1;

enum FreeMode { kLinkCategory, kDoNotLinkCategory };

// A free list category maintains a linked list of free memory blocks.
class FreeListCategory {
 public:
  void Initialize(FreeListCategoryType type) {
    type_ = type;
    available_ = 0;
    prev_ = nullptr;
    next_ = nullptr;
  }

  // Unlinks the category from the freelist.
  void Unlink(FreeList* owner);
  // Resets all the fields of the category.
  void Reset(FreeList* owner);

  void RepairFreeList(Heap* heap);

  // Relinks the category into the currently owning free list. Requires that the
  // category is currently unlinked.
  void Relink(FreeList* owner);

  void Free(const WritableFreeSpace& writable_free_space, FreeMode mode,
            FreeList* owner);

  // Performs a single try to pick a node of at least |minimum_size| from the
  // category. Stores the actual size in |node_size|. Returns nullptr if no
  // node is found.
  V8_EXPORT_PRIVATE Tagged<FreeSpace> PickNodeFromList(size_t minimum_size,
                                                       size_t* node_size);

  // Picks a node of at least |minimum_size| from the category. Stores the
  // actual size in |node_size|. Returns nullptr if no node is found.
  Tagged<FreeSpace> SearchForNodeInList(size_t minimum_size, size_t* node_size);

  inline bool is_linked(FreeList* owner) const;
  bool is_empty() { return top().is_null(); }
  uint32_t available() const { return available_; }

  size_t SumFreeList();
  int FreeListLength();

  template <typename Callback>
  void IterateNodesForTesting(Callback callback) {
    for (Tagged<FreeSpace> cur_node = top(); !cur_node.is_null();
         cur_node = cur_node->next()) {
      callback(cur_node);
    }
  }

 private:
  // For debug builds we accurately compute free lists lengths up until
  // {kVeryLongFreeList} by manually walking the list.
  static constexpr int kVeryLongFreeList = 500;

  // Updates |available_|, |length_| and free_list_->Available() after an
  // allocation of size |allocation_size|.
  inline void UpdateCountersAfterAllocation(size_t allocation_size);

  Tagged<FreeSpace> top() { return top_; }
  void set_top(Tagged<FreeSpace> top) { top_ = top; }
  FreeListCategory* prev() { return prev_; }
  void set_prev(FreeListCategory* prev) { prev_ = prev; }
  FreeListCategory* next() { return next_; }
  void set_next(FreeListCategory* next) { next_ = next; }

  // |type_|: The type of this free list category.
  FreeListCategoryType type_ = kInvalidCategory;

  // |available_|: Total available bytes in all blocks of this free list
  // category.
  uint32_t available_ = 0;

  // |top_|: Points to the top FreeSpace in the free list category.
  Tagged<FreeSpace> top_;

  FreeListCategory* prev_ = nullptr;
  FreeListCategory* next_ = nullptr;

  friend class FreeList;
  friend class FreeListManyCached;
  friend class PagedSpace;
  friend class MapSpace;
};

// A free list maintains free blocks of memory. The free list is organized in
// a way to encourage objects allocated around the same time to be near each
// other. The normal way to allocate is intended to be by bumping a 'top'
// pointer until it hits a 'limit' pointer.  When the limit is hit we need to
// find a new space to allocate from. This is done with the free list, which is
// divided up into rough categories to cut down on waste. Having finer
// categories would scatter allocation more.
class FreeList {
 public:
  // Creates a Freelist of the default class.
  V8_EXPORT_PRIVATE static std::unique_ptr<FreeList> CreateFreeList();
  // Creates a Freelist for new space.
  V8_EXPORT_PRIVATE static std::unique_ptr<FreeList>
  CreateFreeListForNewSpace();

  FreeList(int number_of_categories, size_t min_block_size);
  virtual ~FreeList() = default;

  // Adds a node on the free list. The block of size {size_in_bytes} starting
  // at {start} is placed on the free list. The return value is the number of
  // bytes that were not added to the free list, because the freed memory block
  // was too small. Bookkeeping information will be written to the block, i.e.,
  // its contents will be destroyed. The start address should be word aligned,
  // and the size should be a non-zero multiple of the word size.
  virtual size_t Free(const WritableFreeSpace& free_space, FreeMode mode);

  // Allocates a free space node from the free list of at least size_in_bytes
  // bytes. Returns the actual node size in node_size which can be bigger than
  // size_in_bytes. This method returns null if the allocation request cannot be
  // handled by the free list.
  virtual V8_WARN_UNUSED_RESULT Tagged<FreeSpace> Allocate(
      size_t size_in_bytes, size_t* node_size, AllocationOrigin origin) = 0;

  // Returns a page containing an entry for a given type, or nullptr otherwise.
  V8_EXPORT_PRIVATE virtual PageMetadata* GetPageForSize(
      size_t size_in_bytes) = 0;

  virtual void Reset();
  virtual void ResetForNonBlackAllocatedPages();

  // Return the number of bytes available on the free list.
  size_t Available() {
    VerifyAvailable();
    return available_;
  }

  // Update number of available  bytes on the Freelists.
  void IncreaseAvailableBytes(size_t bytes) { available_ += bytes; }
  void DecreaseAvailableBytes(size_t bytes) { available_ -= bytes; }

  size_t wasted_bytes() const {
    return wasted_bytes_.load(std::memory_order_relaxed);
  }
  void increase_wasted_bytes(size_t bytes) {
    wasted_bytes_.fetch_add(bytes, std::memory_order_relaxed);
  }
  void decrease_wasted_bytes(size_t bytes) {
    wasted_bytes_.fetch_sub(bytes, std::memory_order_relaxed);
  }

  inline bool IsEmpty();

  // Used after booting the VM.
  void RepairLists(Heap* heap);

  V8_EXPORT_PRIVATE void EvictFreeListItems(PageMetadata* page);

  int number_of_categories() { return number_of_categories_; }
  FreeListCategoryType last_category() { return last_category_; }

  size_t min_block_size() const { return min_block_size_; }

  template <typename Callback>
  void ForAllFreeListCategories(FreeListCategoryType type, Callback callback) {
    FreeListCategory* current = categories_[type];
    while (current != nullptr) {
      FreeListCategory* next = current->next();
      callback(current);
      current = next;
    }
  }

  template <typename Callback>
  void ForAllFreeListCategories(Callback callback) {
    for (int i = kFirstCategory; i < number_of_categories(); i++) {
      ForAllFreeListCategories(static_cast<FreeListCategoryType>(i), callback);
    }
  }

  virtual bool AddCategory(FreeListCategory* category);
  virtual V8_EXPORT_PRIVATE void RemoveCategory(FreeListCategory* category);
  void PrintCategories(FreeListCategoryType type);

 protected:
  class FreeListCategoryIterator final {
   public:
    FreeListCategoryIterator(FreeList* free_list, FreeListCategoryType type)
        : current_(free_list->categories_[type]) {}

    bool HasNext() const { return current_ != nullptr; }

    FreeListCategory* Next() {
      DCHECK(HasNext());
      FreeListCategory* tmp = current_;
      current_ = current_->next();
      return tmp;
    }

   private:
    FreeListCategory* current_;
  };

#ifdef DEBUG
  V8_EXPORT_PRIVATE size_t SumFreeLists();
  V8_EXPORT_PRIVATE bool IsVeryLong();
#endif

  void VerifyAvailable() {
    DCHECK(IsVeryLong() || available_ == SumFreeLists());
  }

  // Tries to retrieve a node from the first category in a given |type|.
  // Returns nullptr if the category is empty or the top entry is smaller
  // than minimum_size.
  Tagged<FreeSpace> TryFindNodeIn(FreeListCategoryType type,
                                  size_t minimum_size, size_t* node_size);

  // Searches a given |type| for a node of at least |minimum_size|.
  Tagged<FreeSpace> SearchForNodeInList(FreeListCategoryType type,
                                        size_t minimum_size, size_t* node_size);

  // Returns the smallest category in which an object of |size_in_bytes| could
  // fit.
  virtual FreeListCategoryType SelectFreeListCategoryType(
      size_t size_in_bytes) = 0;

  FreeListCategory* top(FreeListCategoryType type) const {
    return categories_[type];
  }

  inline PageMetadata* GetPageForCategoryType(FreeListCategoryType type);

  const int number_of_categories_ = 0;
  const FreeListCategoryType last_category_ = 0;
  size_t min_block_size_ = 0;

  FreeListCategory** categories_ = nullptr;

  // The number of bytes in this freelist that are available for allocation.
  size_t available_ = 0;
  // Number of wasted bytes in this free list that are not available for
  // allocation.
  std::atomic<size_t> wasted_bytes_ = 0;

  friend class FreeListCategory;
  friend class PageMetadata;
  friend class MutablePageMetadata;
  friend class ReadOnlyPageMetadata;
  friend class MapSpace;
};

// Use 24 Freelists: on per 16 bytes between 24 and 256, and then a few ones for
// larger sizes. See the variable |categories_min| for the size of each
// Freelist.  Allocation is done using a best-fit strategy (considering only the
// first element of each category though).
// Performances are expected to be worst than FreeListLegacy, but memory
// consumption should be lower (since fragmentation should be lower).
class V8_EXPORT_PRIVATE FreeListMany : public FreeList {
 public:
  PageMetadata* GetPageForSize(size_t size_in_bytes) override;

  FreeListMany();
  ~FreeListMany() override;

  V8_WARN_UNUSED_RESULT Tagged<FreeSpace> Allocate(
      size_t size_in_bytes, size_t* node_size,
      AllocationOrigin origin) override;

 protected:
  static constexpr size_t kMinBlockSize = 3 * kTaggedSize;

  // This is a conservative upper bound. The actual maximum block size takes
  // padding and alignment of data and code pages into account.
  static constexpr size_t kMaxBlockSize = MutablePageMetadata::kPageSize;
  // Largest size for which categories are still precise, and for which we can
  // therefore compute the category in constant time.
  static constexpr size_t kPreciseCategoryMaxSize = 256;

  // Categories boundaries generated with:
  // perl -E '
  //      @cat = (24, map {$_*16} 2..16, 48, 64);
  //      while ($cat[-1] <= 32768) {
  //        push @cat, $cat[-1]*2
  //      }
  //      say join ", ", @cat;
  //      say "\n", scalar @cat'
  static constexpr int kNumberOfCategories = 24;
  static constexpr unsigned int categories_min[kNumberOfCategories] = {
      24,  32,  48,  64,  80,  96,   112,  128,  144,  160,   176,   192,
      208, 224, 240, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};

  // Return the smallest category that could hold |size_in_bytes| bytes.
  FreeListCategoryType SelectFreeListCategoryType(
      size_t size_in_bytes) override {
    if (size_in_bytes <= kPreciseCategoryMaxSize) {
      if (size_in_bytes < categories_min[1]) return 0;
      return static_cast<FreeListCategoryType>(size_in_bytes >> 4) - 1;
    }
    for (int cat = (kPreciseCategoryMaxSize >> 4) - 1; cat < last_category_;
         cat++) {
      if (size_in_bytes < categories_min[cat + 1]) {
        return cat;
      }
    }
    return last_category_;
  }

  FRIEND_TEST(SpacesTest, FreeListManySelectFreeListCategoryType);
  FRIEND_TEST(SpacesTest, FreeListManyGuaranteedAllocatable);
};

// Same as FreeListMany but uses a cache to know which categories are empty.
// The cache (|next_nonempty_category|) is maintained in a way such that for
// each category c, next_nonempty_category[c] contains the first non-empty
// category greater or equal to c, that may hold an object of size c.
// Allocation is done using the same strategy as FreeListMany (ie, best fit).
class V8_EXPORT_PRIVATE FreeListManyCached : public FreeListMany {
 public:
  FreeListManyCached();

  V8_WARN_UNUSED_RESULT Tagged<FreeSpace> Allocate(
      size_t size_in_bytes, size_t* node_size,
      AllocationOrigin origin) override;

  size_t Free(const WritableFreeSpace& free_space, FreeMode mode) override;

  void Reset() override;
  void ResetForNonBlackAllocatedPages() override;

  bool AddCategory(FreeListCategory* category) override;
  void RemoveCategory(FreeListCategory* category) override;

 protected:
  // Updates the cache after adding something in the category |cat|.
  void UpdateCacheAfterAddition(FreeListCategoryType cat) {
    for (int i = cat; i >= kFirstCategory && next_nonempty_category[i] > cat;
         i--) {
      next_nonempty_category[i] = cat;
    }
  }

  // Updates the cache after emptying category |cat|.
  void UpdateCacheAfterRemoval(FreeListCategoryType cat) {
    for (int i = cat; i >= kFirstCategory && next_nonempty_category[i] == cat;
         i--) {
      next_nonempty_category[i] = next_nonempty_category[cat + 1];
    }
  }

#ifdef DEBUG
  void CheckCacheIntegrity() {
    for (int i = 0; i <= last_category_; i++) {
      DCHECK(next_nonempty_category[i] == last_category_ + 1 ||
             categories_[next_nonempty_category[i]] != nullptr);
      for (int j = i; j < next_nonempty_category[i]; j++) {
        DCHECK(categories_[j] == nullptr);
      }
    }
  }
#endif

  // The cache is overallocated by one so that the last element is always
  // defined, and when updating the cache, we can always use cache[i+1] as long
  // as i is < kNumberOfCategories.
  int next_nonempty_category[kNumberOfCategories + 1];

 private:
  void ResetCache() {
    for (int i = 0; i < kNumberOfCategories; i++) {
      next_nonempty_category[i] = kNumberOfCategories;
    }
    // Setting the after-last element as well, as explained in the cache's
    // declaration.
    next_nonempty_category[kNumberOfCategories] = kNumberOfCategories;
  }
};

// Same as FreeListManyCached but uses a fast path.
// The fast path overallocates by at least 1.85k bytes. The idea of this 1.85k
// is: we want the fast path to always overallocate, even for larger
// categories. Therefore, we have two choices: either overallocate by
// "size_in_bytes * something" or overallocate by "size_in_bytes +
// something". We choose the later, as the former will tend to overallocate too
// much for larger objects. The 1.85k (= 2048 - 128) has been chosen such that
// for tiny objects (size <= 128 bytes), the first category considered is the
// 36th (which holds objects of 2k to 3k), while for larger objects, the first
// category considered will be one that guarantees a 1.85k+ bytes
// overallocation. Using 2k rather than 1.85k would have resulted in either a
// more complex logic for SelectFastAllocationFreeListCategoryType, or the 36th
// category (2k to 3k) not being used; both of which are undesirable.
// A secondary fast path is used for tiny objects (size <= 128), in order to
// consider categories from 256 to 2048 bytes for them.
// Note that this class uses a precise GetPageForSize (inherited from
// FreeListMany), which makes its fast path less fast in the Scavenger. This is
// done on purpose, since this class's only purpose is to be used by
// FreeListManyCachedOrigin, which is precise for the scavenger.
class V8_EXPORT_PRIVATE FreeListManyCachedFastPathBase
    : public FreeListManyCached {
 public:
  enum class SmallBlocksMode { kAllow, kProhibit };

  explicit FreeListManyCachedFastPathBase(SmallBlocksMode small_blocks_mode)
      : small_blocks_mode_(small_blocks_mode) {
    if (small_blocks_mode_ == SmallBlocksMode::kProhibit) {
      min_block_size_ =
          (v8_flags.minor_ms && (v8_flags.minor_ms_min_lab_size_kb > 0))
              ? (v8_flags.minor_ms_min_lab_size_kb * KB)
              : kFastPathStart;
    }
  }

  V8_WARN_UNUSED_RESULT Tagged<FreeSpace> Allocate(
      size_t size_in_bytes, size_t* node_size,
      AllocationOrigin origin) override;

 protected:
  // Objects in the 18th category are at least 2048 bytes
  static const FreeListCategoryType kFastPathFirstCategory = 18;
  static const size_t kFastPathStart = 2048;
  static const size_t kTinyObjectMaxSize = 128;
  static const size_t kFastPathOffset = kFastPathStart - kTinyObjectMaxSize;
  // Objects in the 15th category are at least 256 bytes
  static const FreeListCategoryType kFastPathFallBackTiny = 15;

  static_assert(categories_min[kFastPathFirstCategory] == kFastPathStart);
  static_assert(categories_min[kFastPathFallBackTiny] ==
                kTinyObjectMaxSize * 2);

  FreeListCategoryType SelectFastAllocationFreeListCategoryType(
      size_t size_in_bytes) {
    DCHECK(size_in_bytes < kMaxBlockSize);

    if (size_in_bytes >= categories_min[last_category_]) return last_category_;

    size_in_bytes += kFastPathOffset;
    for (int cat = kFastPathFirstCategory; cat < last_category_; cat++) {
      if (size_in_bytes <= categories_min[cat]) {
        return cat;
      }
    }
    return last_category_;
  }

 private:
  SmallBlocksMode small_blocks_mode_;

  FRIEND_TEST(
      SpacesTest,
      FreeListManyCachedFastPathSelectFastAllocationFreeListCategoryType);
};

class FreeListManyCachedFastPath : public FreeListManyCachedFastPathBase {
 public:
  FreeListManyCachedFastPath()
      : FreeListManyCachedFastPathBase(SmallBlocksMode::kAllow) {}
};

class FreeListManyCachedFastPathForNewSpace
    : public FreeListManyCachedFastPathBase {
 public:
  FreeListManyCachedFastPathForNewSpace()
      : FreeListManyCachedFastPathBase(SmallBlocksMode::kProhibit) {}
};

// Uses FreeListManyCached if in the GC; FreeListManyCachedFastPath otherwise.
// The reasoning behind this FreeList is the following: the GC runs in
// parallel, and therefore, more expensive allocations there are less
// noticeable. On the other hand, the generated code and runtime need to be very
// fast. Therefore, the strategy for the former is one that is not very
// efficient, but reduces fragmentation (FreeListManyCached), while the strategy
// for the later is one that is very efficient, but introduces some
// fragmentation (FreeListManyCachedFastPath).
class V8_EXPORT_PRIVATE FreeListManyCachedOrigin
    : public FreeListManyCachedFastPath {
 public:
  V8_WARN_UNUSED_RESULT Tagged<FreeSpace> Allocate(
      size_t size_in_bytes, size_t* node_size,
      AllocationOrigin origin) override;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FREE_LIST_H_
