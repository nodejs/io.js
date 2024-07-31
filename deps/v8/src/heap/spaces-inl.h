// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SPACES_INL_H_
#define V8_HEAP_SPACES_INL_H_

#include "src/base/atomic-utils.h"
#include "src/common/globals.h"
#include "src/heap/heap-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/large-page.h"
#include "src/heap/large-spaces.h"
#include "src/heap/main-allocator-inl.h"
#include "src/heap/mutable-page-inl.h"
#include "src/heap/new-spaces.h"
#include "src/heap/paged-spaces.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

template <class PageType>
PageIteratorImpl<PageType>& PageIteratorImpl<PageType>::operator++() {
  p_ = p_->next_page();
  return *this;
}

template <class PageType>
PageIteratorImpl<PageType> PageIteratorImpl<PageType>::operator++(int) {
  PageIteratorImpl<PageType> tmp(*this);
  operator++();
  return tmp;
}

void Space::IncrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                               size_t amount) {
  base::CheckedIncrement(&external_backing_store_bytes_[static_cast<int>(type)],
                         amount);
  heap()->IncrementExternalBackingStoreBytes(type, amount);
}

void Space::DecrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                               size_t amount) {
  base::CheckedDecrement(&external_backing_store_bytes_[static_cast<int>(type)],
                         amount);
  heap()->DecrementExternalBackingStoreBytes(type, amount);
}

void Space::MoveExternalBackingStoreBytes(ExternalBackingStoreType type,
                                          Space* from, Space* to,
                                          size_t amount) {
  if (from == to) return;

  base::CheckedDecrement(
      &(from->external_backing_store_bytes_[static_cast<int>(type)]), amount);
  base::CheckedIncrement(
      &(to->external_backing_store_bytes_[static_cast<int>(type)]), amount);
}

PageRange::PageRange(PageMetadata* page) : PageRange(page, page->next_page()) {}
ConstPageRange::ConstPageRange(const PageMetadata* page)
    : ConstPageRange(page, page->next_page()) {}

OldGenerationMemoryChunkIterator::OldGenerationMemoryChunkIterator(Heap* heap)
    : heap_(heap), state_(kOldSpace), iterator_(heap->old_space()->begin()) {}

MutablePageMetadata* OldGenerationMemoryChunkIterator::next() {
  switch (state_) {
    case kOldSpace: {
      PageIterator& iterator = std::get<PageIterator>(iterator_);
      if (iterator != heap_->old_space()->end()) return *(iterator++);
      state_ = kCodeSpace;
      iterator_ = heap_->code_space()->begin();
      [[fallthrough]];
    }
    case kCodeSpace: {
      PageIterator& iterator = std::get<PageIterator>(iterator_);
      if (iterator != heap_->code_space()->end()) return *(iterator++);
      state_ = kLargeObjectSpace;
      iterator_ = heap_->lo_space()->begin();
      [[fallthrough]];
    }
    case kLargeObjectSpace: {
      LargePageIterator& iterator = std::get<LargePageIterator>(iterator_);
      if (iterator != heap_->lo_space()->end()) return *(iterator++);
      state_ = kCodeLargeObjectSpace;
      iterator_ = heap_->code_lo_space()->begin();
      [[fallthrough]];
    }
    case kCodeLargeObjectSpace: {
      LargePageIterator& iterator = std::get<LargePageIterator>(iterator_);
      if (iterator != heap_->code_lo_space()->end()) return *(iterator++);
      state_ = kTrustedSpace;
      iterator_ = heap_->trusted_space()->begin();
      [[fallthrough]];
    }
    case kTrustedSpace: {
      PageIterator& iterator = std::get<PageIterator>(iterator_);
      if (iterator != heap_->trusted_space()->end()) return *(iterator++);
      state_ = kTrustedLargeObjectSpace;
      iterator_ = heap_->trusted_lo_space()->begin();
      [[fallthrough]];
    }
    case kTrustedLargeObjectSpace: {
      LargePageIterator& iterator = std::get<LargePageIterator>(iterator_);
      if (iterator != heap_->trusted_lo_space()->end()) return *(iterator++);
      state_ = kFinished;
      [[fallthrough]];
    }
    case kFinished:
      return nullptr;
  }
}

bool MemoryChunkIterator::HasNext() {
  if (current_chunk_) return true;

  while (space_iterator_.HasNext()) {
    Space* space = space_iterator_.Next();
    current_chunk_ = space->first_page();
    if (current_chunk_) return true;
  }

  return false;
}

MutablePageMetadata* MemoryChunkIterator::Next() {
  MutablePageMetadata* chunk = current_chunk_;
  current_chunk_ = chunk->list_node().next();
  return chunk;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SPACES_INL_H_
