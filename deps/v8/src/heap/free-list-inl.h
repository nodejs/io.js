// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FREE_LIST_INL_H_
#define V8_HEAP_FREE_LIST_INL_H_

#include "src/heap/free-list.h"
#include "src/heap/page-metadata-inl.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

bool FreeListCategory::is_linked(FreeList* owner) const {
  return prev_ != nullptr || next_ != nullptr ||
         owner->categories_[type_] == this;
}

void FreeListCategory::UpdateCountersAfterAllocation(size_t allocation_size) {
  available_ -= allocation_size;
}

PageMetadata* FreeList::GetPageForCategoryType(FreeListCategoryType type) {
  FreeListCategory* category_top = top(type);
  if (category_top != nullptr) {
    DCHECK(!category_top->top().is_null());
    return PageMetadata::FromHeapObject(category_top->top());
  } else {
    return nullptr;
  }
}

bool FreeList::IsEmpty() {
  bool empty = true;
  ForAllFreeListCategories([&empty](FreeListCategory* category) {
    if (!category->is_empty()) empty = false;
  });
  return empty;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FREE_LIST_INL_H_
