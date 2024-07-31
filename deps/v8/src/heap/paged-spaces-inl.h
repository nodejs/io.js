// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_PAGED_SPACES_INL_H_
#define V8_HEAP_PAGED_SPACES_INL_H_

#include "src/common/globals.h"
#include "src/heap/heap-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/paged-spaces.h"
#include "src/objects/heap-object.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

HeapObjectRange::iterator::iterator() : cage_base_(kNullAddress) {}

HeapObjectRange::iterator::iterator(const PageMetadata* page)
    : cage_base_(page->heap()->isolate()),
      cur_addr_(page->area_start()),
      cur_end_(page->area_end()) {
  AdvanceToNextObject();
}

HeapObjectRange::iterator& HeapObjectRange::iterator::operator++() {
  DCHECK_GT(cur_size_, 0);
  cur_addr_ += cur_size_;
  AdvanceToNextObject();
  return *this;
}

HeapObjectRange::iterator HeapObjectRange::iterator::operator++(int) {
  iterator retval = *this;
  ++(*this);
  return retval;
}

void HeapObjectRange::iterator::AdvanceToNextObject() {
  DCHECK_NE(cur_addr_, kNullAddress);
  while (cur_addr_ != cur_end_) {
    DCHECK_LT(cur_addr_, cur_end_);
    Tagged<HeapObject> obj = HeapObject::FromAddress(cur_addr_);
    cur_size_ = ALIGN_TO_ALLOCATION_ALIGNMENT(obj->Size(cage_base()));
    DCHECK_LE(cur_addr_ + cur_size_, cur_end_);
    if (IsFreeSpaceOrFiller(obj, cage_base())) {
      cur_addr_ += cur_size_;
    } else {
      if (IsInstructionStream(obj, cage_base())) {
        DCHECK_EQ(PageMetadata::FromHeapObject(obj)->owner_identity(),
                  CODE_SPACE);
        DCHECK_CODEOBJECT_SIZE(cur_size_);
      } else {
        DCHECK_OBJECT_SIZE(cur_size_);
      }
      return;
    }
  }
  cur_addr_ = kNullAddress;
}

HeapObjectRange::iterator HeapObjectRange::begin() { return iterator(page_); }

HeapObjectRange::iterator HeapObjectRange::end() { return iterator(); }

Tagged<HeapObject> PagedSpaceObjectIterator::Next() {
  do {
    if (cur_ != end_) {
      return *cur_++;
    }
  } while (AdvanceToNextPage());
  return Tagged<HeapObject>();
}

bool PagedSpaceBase::Contains(Address addr) const {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
    return true;
  }
  return PageMetadata::FromAddress(addr)->owner() == this;
}

bool PagedSpaceBase::Contains(Tagged<Object> o) const {
  if (!IsHeapObject(o)) return false;
  return PageMetadata::FromAddress(o.ptr())->owner() == this;
}

template <bool during_sweep>
size_t PagedSpaceBase::FreeInternal(Address start, size_t size_in_bytes) {
  if (size_in_bytes == 0) return 0;
  size_t wasted;
  if (executable_) {
    WritableJitPage jit_page(start, size_in_bytes);
    WritableFreeSpace free_space = jit_page.FreeRange(start, size_in_bytes);
    heap()->CreateFillerObjectAtBackground(free_space);
    wasted = free_list_->Free(
        free_space, during_sweep ? kDoNotLinkCategory : kLinkCategory);
  } else {
    WritableFreeSpace free_space =
        WritableFreeSpace::ForNonExecutableMemory(start, size_in_bytes);
    heap()->CreateFillerObjectAtBackground(free_space);
    wasted = free_list_->Free(
        free_space, during_sweep ? kDoNotLinkCategory : kLinkCategory);
  }

  if constexpr (!during_sweep) {
    PageMetadata* page = PageMetadata::FromAddress(start);
    accounting_stats_.DecreaseAllocatedBytes(size_in_bytes, page);
    free_list()->increase_wasted_bytes(wasted);
  }

  DCHECK_GE(size_in_bytes, wasted);
  return size_in_bytes - wasted;
}

size_t PagedSpaceBase::Free(Address start, size_t size_in_bytes) {
  return FreeInternal</*during_sweep=*/false>(start, size_in_bytes);
}

size_t PagedSpaceBase::FreeDuringSweep(Address start, size_t size_in_bytes) {
  return FreeInternal</*during_sweep=*/true>(start, size_in_bytes);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_PAGED_SPACES_INL_H_
