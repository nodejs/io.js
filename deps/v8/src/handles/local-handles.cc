// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/local-handles.h"

#include "src/api/api.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/handles/handles.h"
#include "src/heap/heap-inl.h"

namespace v8 {
namespace internal {

Address* LocalHandleScope::GetMainThreadHandle(LocalHeap* local_heap,
                                               Address value) {
  Isolate* isolate = local_heap->heap()->isolate();
  return HandleScope::CreateHandle(isolate, value);
}

void LocalHandleScope::OpenMainThreadScope(LocalHeap* local_heap) {
  Isolate* isolate = local_heap->heap()->isolate();
  HandleScopeData* data = isolate->handle_scope_data();
  local_heap_ = local_heap;
  prev_next_ = data->next;
  prev_limit_ = data->limit;
  data->level++;
#ifdef V8_ENABLE_CHECKS
  scope_level_ = data->level;
#endif
}

void LocalHandleScope::CloseMainThreadScope(LocalHeap* local_heap,
                                            Address* prev_next,
                                            Address* prev_limit) {
  Isolate* isolate = local_heap->heap()->isolate();
  HandleScope::CloseScope(isolate, prev_next, prev_limit);
}

#ifdef V8_ENABLE_CHECKS
void LocalHandleScope::VerifyMainThreadScope() const {
  Isolate* isolate = local_heap_->heap()->isolate();
  CHECK_EQ(scope_level_, isolate->handle_scope_data()->level);
}
#endif  // V8_ENABLE_CHECKS

LocalHandles::LocalHandles() { scope_.Initialize(); }
LocalHandles::~LocalHandles() {
  scope_.limit = nullptr;
  RemoveUnusedBlocks();
  DCHECK(blocks_.empty());
}

void LocalHandles::Iterate(RootVisitor* visitor) {
  for (int i = 0; i < static_cast<int>(blocks_.size()) - 1; i++) {
    Address* block = blocks_[i];
    visitor->VisitRootPointers(Root::kHandleScope, nullptr,
                               FullObjectSlot(block),
                               FullObjectSlot(&block[kHandleBlockSize]));
  }

  if (!blocks_.empty()) {
    Address* block = blocks_.back();
    visitor->VisitRootPointers(Root::kHandleScope, nullptr,
                               FullObjectSlot(block),
                               FullObjectSlot(scope_.next));
  }
}

#ifdef DEBUG
bool LocalHandles::Contains(Address* location) {
  // We have to search in all blocks since they have no guarantee of order.
  for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
    Address* lower_bound = *it;
    // The last block is a special case because it may have less than
    // block_size_ handles.
    Address* upper_bound = lower_bound != blocks_.back()
                               ? lower_bound + kHandleBlockSize
                               : scope_.next;
    if (lower_bound <= location && location < upper_bound) {
      return true;
    }
  }
  return false;
}
#endif

Address* LocalHandles::AddBlock() {
  DCHECK_EQ(scope_.next, scope_.limit);
  Address* block = NewArray<Address>(kHandleBlockSize);
  blocks_.push_back(block);
  scope_.next = block;
  scope_.limit = block + kHandleBlockSize;
  return block;
}

void LocalHandles::RemoveUnusedBlocks() {
  while (!blocks_.empty()) {
    Address* block_start = blocks_.back();
    Address* block_limit = block_start + kHandleBlockSize;

    if (block_limit == scope_.limit) {
      break;
    }

    blocks_.pop_back();

#ifdef ENABLE_HANDLE_ZAPPING
    ZapRange(block_start, block_limit);
#endif

    DeleteArray(block_start);
  }
}

#ifdef ENABLE_HANDLE_ZAPPING
void LocalHandles::ZapRange(Address* start, Address* end) {
  HandleScope::ZapRange(start, end);
}
#endif

}  // namespace internal
}  // namespace v8
