// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_HEAP_SPACE_H_
#define V8_HEAP_CPPGC_HEAP_SPACE_H_

#include <vector>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/heap/cppgc/free-list.h"

namespace cppgc {
namespace internal {

class RawHeap;
class BasePage;

// BaseSpace is responsible for page management.
class V8_EXPORT_PRIVATE BaseSpace {
 public:
  using Pages = std::vector<BasePage*>;

  using iterator = Pages::iterator;
  using const_iterator = Pages::const_iterator;

  BaseSpace(const BaseSpace&) = delete;
  BaseSpace& operator=(const BaseSpace&) = delete;
  virtual ~BaseSpace();

  iterator begin() { return pages_.begin(); }
  const_iterator begin() const { return pages_.begin(); }
  iterator end() { return pages_.end(); }
  const_iterator end() const { return pages_.end(); }

  size_t size() const { return pages_.size(); }

  bool is_large() const { return type_ == PageType::kLarge; }
  size_t index() const { return index_; }

  RawHeap* raw_heap() { return heap_; }
  const RawHeap* raw_heap() const { return heap_; }

  // Page manipulation functions.
  void AddPage(BasePage*);
  void RemovePage(BasePage*);
  Pages RemoveAllPages();
  v8::base::SpinningMutex& pages_mutex() const { return pages_mutex_; }

  bool is_compactable() const { return is_compactable_; }

 protected:
  enum class PageType { kNormal, kLarge };
  explicit BaseSpace(RawHeap* heap, size_t index, PageType type,
                     bool is_compactable);

 private:
  RawHeap* heap_;
  Pages pages_;
  mutable v8::base::SpinningMutex pages_mutex_;
  const size_t index_;
  const PageType type_;
  const bool is_compactable_;
};

class V8_EXPORT_PRIVATE NormalPageSpace final : public BaseSpace {
 public:
  class LinearAllocationBuffer {
   public:
    Address Allocate(size_t alloc_size) {
      DCHECK_GE(size_, alloc_size);
      Address result = start_;
      start_ += alloc_size;
      size_ -= alloc_size;
      return result;
    }

    void Set(Address ptr, size_t size) {
      start_ = ptr;
      size_ = size;
    }

    Address start() const { return start_; }
    size_t size() const { return size_; }

   private:
    Address start_ = nullptr;
    size_t size_ = 0;
  };

  static NormalPageSpace& From(BaseSpace& space) {
    DCHECK(!space.is_large());
    return static_cast<NormalPageSpace&>(space);
  }
  static const NormalPageSpace& From(const BaseSpace& space) {
    return From(const_cast<BaseSpace&>(space));
  }

  NormalPageSpace(RawHeap* heap, size_t index, bool is_compactable);

  LinearAllocationBuffer& linear_allocation_buffer() { return current_lab_; }
  const LinearAllocationBuffer& linear_allocation_buffer() const {
    return current_lab_;
  }

  FreeList& free_list() { return free_list_; }
  const FreeList& free_list() const { return free_list_; }

 private:
  LinearAllocationBuffer current_lab_;
  FreeList free_list_;
};

class V8_EXPORT_PRIVATE LargePageSpace final : public BaseSpace {
 public:
  static LargePageSpace& From(BaseSpace& space) {
    DCHECK(space.is_large());
    return static_cast<LargePageSpace&>(space);
  }
  static const LargePageSpace& From(const BaseSpace& space) {
    return From(const_cast<BaseSpace&>(space));
  }

  LargePageSpace(RawHeap* heap, size_t index);
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_HEAP_SPACE_H_
