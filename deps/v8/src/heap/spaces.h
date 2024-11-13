// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SPACES_H_
#define V8_HEAP_SPACES_H_

#include <atomic>
#include <memory>

#include "src/base/iterator.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/heap/allocation-observer.h"
#include "src/heap/base-space.h"
#include "src/heap/base/active-system-pages.h"
#include "src/heap/free-list.h"
#include "src/heap/linear-allocation-area.h"
#include "src/heap/list.h"
#include "src/heap/main-allocator.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/page-metadata.h"
#include "src/heap/slot-set.h"
#include "src/objects/objects.h"
#include "src/utils/allocation.h"
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
class Heap;
class Isolate;
class LargeObjectSpace;
class LargePageMetadata;
class ObjectIterator;
class PagedSpaceBase;
class SemiSpace;

// Some assertion macros used in the debugging mode.

#define DCHECK_OBJECT_SIZE(size) \
  DCHECK((0 < size) && (size <= kMaxRegularHeapObjectSize))

#define DCHECK_CODEOBJECT_SIZE(size) \
  DCHECK((0 < size) && (size <= MemoryChunkLayout::MaxRegularCodeObjectSize()))

template <typename Enum, typename Callback>
void ForAll(Callback callback) {
  for (int i = 0; i < static_cast<int>(Enum::kNumValues); i++) {
    callback(static_cast<Enum>(i), i);
  }
}

// ----------------------------------------------------------------------------
// Space is the abstract superclass for all allocation spaces that are not
// sealed after startup (i.e. not ReadOnlySpace).
class V8_EXPORT_PRIVATE Space : public BaseSpace {
 public:
  static inline void MoveExternalBackingStoreBytes(
      ExternalBackingStoreType type, Space* from, Space* to, size_t amount);

  Space(Heap* heap, AllocationSpace id, std::unique_ptr<FreeList> free_list)
      : BaseSpace(heap, id), free_list_(std::move(free_list)) {}

  ~Space() override = default;

  Space(const Space&) = delete;
  Space& operator=(const Space&) = delete;

  // Returns size of objects. Can differ from the allocated size
  // (e.g. see OldLargeObjectSpace).
  virtual size_t SizeOfObjects() const { return Size(); }

  // Return the available bytes without growing.
  virtual size_t Available() const = 0;

  virtual std::unique_ptr<ObjectIterator> GetObjectIterator(Heap* heap) = 0;

  inline void IncrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                                 size_t amount);
  inline void DecrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                                 size_t amount);

  // Returns amount of off-heap memory in-use by objects in this Space.
  virtual size_t ExternalBackingStoreBytes(
      ExternalBackingStoreType type) const {
    return external_backing_store_bytes_[static_cast<int>(type)];
  }

  virtual MutablePageMetadata* first_page() {
    return memory_chunk_list_.front();
  }
  virtual MutablePageMetadata* last_page() { return memory_chunk_list_.back(); }

  virtual const MutablePageMetadata* first_page() const {
    return memory_chunk_list_.front();
  }
  virtual const MutablePageMetadata* last_page() const {
    return memory_chunk_list_.back();
  }

  virtual heap::List<MutablePageMetadata>& memory_chunk_list() {
    return memory_chunk_list_;
  }

  virtual PageMetadata* InitializePage(MutablePageMetadata* chunk) {
    UNREACHABLE();
  }

  virtual void NotifyBlackAreaCreated(size_t size) {}
  virtual void NotifyBlackAreaDestroyed(size_t size) {}

  FreeList* free_list() { return free_list_.get(); }

  Address FirstPageAddress() const {
    DCHECK_NOT_NULL(first_page());
    return first_page()->ChunkAddress();
  }

#ifdef DEBUG
  virtual void Print() = 0;
#endif

 protected:
  // The List manages the pages that belong to the given space.
  heap::List<MutablePageMetadata> memory_chunk_list_;
  // Tracks off-heap memory used by this space.
  std::atomic<size_t> external_backing_store_bytes_[static_cast<int>(
      ExternalBackingStoreType::kNumValues)] = {0};
  std::unique_ptr<FreeList> free_list_;
};

static_assert(sizeof(std::atomic<intptr_t>) == kSystemPointerSize);

// -----------------------------------------------------------------------------
// Interface for heap object iterator to be implemented by all object space
// object iterators.

class V8_EXPORT_PRIVATE ObjectIterator : public Malloced {
 public:
  // Note: The destructor can not be marked as `= default` as this causes
  // the compiler on C++20 to define it as `constexpr` resulting in the
  // compiler producing warnings about undefined inlines for Next()
  // on classes inheriting from it.
  virtual ~ObjectIterator() {}
  virtual Tagged<HeapObject> Next() = 0;
};

template <class PageType>
class PageIteratorImpl
    : public base::iterator<std::forward_iterator_tag, PageType> {
 public:
  explicit PageIteratorImpl(PageType* p) : p_(p) {}
  PageIteratorImpl(const PageIteratorImpl&) V8_NOEXCEPT = default;
  PageIteratorImpl& operator=(const PageIteratorImpl&) V8_NOEXCEPT = default;

  PageType* operator*() { return p_; }
  bool operator==(const PageIteratorImpl<PageType>& rhs) const {
    return rhs.p_ == p_;
  }
  bool operator!=(const PageIteratorImpl<PageType>& rhs) const {
    return rhs.p_ != p_;
  }
  inline PageIteratorImpl<PageType>& operator++();
  inline PageIteratorImpl<PageType> operator++(int);

 private:
  PageType* p_;
};

using PageIterator = PageIteratorImpl<PageMetadata>;
using ConstPageIterator = PageIteratorImpl<const PageMetadata>;
using LargePageIterator = PageIteratorImpl<LargePageMetadata>;
using ConstLargePageIterator = PageIteratorImpl<const LargePageMetadata>;

class PageRange {
 public:
  using iterator = PageIterator;
  PageRange(PageMetadata* begin, PageMetadata* end)
      : begin_(begin), end_(end) {}
  inline explicit PageRange(PageMetadata* page);

  iterator begin() { return iterator(begin_); }
  iterator end() { return iterator(end_); }

 private:
  PageMetadata* begin_;
  PageMetadata* end_;
};

class ConstPageRange {
 public:
  using iterator = ConstPageIterator;
  ConstPageRange(const PageMetadata* begin, const PageMetadata* end)
      : begin_(begin), end_(end) {}
  inline explicit ConstPageRange(const PageMetadata* page);

  iterator begin() { return iterator(begin_); }
  iterator end() { return iterator(end_); }

 private:
  const PageMetadata* begin_;
  const PageMetadata* end_;
};

class V8_EXPORT_PRIVATE SpaceWithLinearArea : public Space {
 public:
  // Creates this space and uses the existing `allocator`. It doesn't create a
  // new MainAllocator instance.
  SpaceWithLinearArea(Heap* heap, AllocationSpace id,
                      std::unique_ptr<FreeList> free_list);

  virtual AllocatorPolicy* CreateAllocatorPolicy(MainAllocator* allocator) = 0;

  friend class MainAllocator;
};

class V8_EXPORT_PRIVATE SpaceIterator : public Malloced {
 public:
  explicit SpaceIterator(Heap* heap);
  virtual ~SpaceIterator();

  bool HasNext();
  Space* Next();

 private:
  Heap* heap_;
  int current_space_;  // from enum AllocationSpace.
};

// Iterates over all memory chunks in the heap (across all spaces).
class MemoryChunkIterator {
 public:
  explicit MemoryChunkIterator(Heap* heap) : space_iterator_(heap) {}

  V8_INLINE bool HasNext();
  V8_INLINE MutablePageMetadata* Next();

 private:
  SpaceIterator space_iterator_;
  MutablePageMetadata* current_chunk_ = nullptr;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SPACES_H_
