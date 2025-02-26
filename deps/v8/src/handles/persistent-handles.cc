// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/persistent-handles.h"

#include "src/api/api.h"
#include "src/heap/heap-inl.h"
#include "src/heap/safepoint.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

PersistentHandles::PersistentHandles(Isolate* isolate)
    : isolate_(isolate),
      block_next_(nullptr),
      block_limit_(nullptr),
      prev_(nullptr),
      next_(nullptr) {
  isolate->persistent_handles_list()->Add(this);
}

PersistentHandles::~PersistentHandles() {
  isolate_->persistent_handles_list()->Remove(this);

  for (Address* block_start : blocks_) {
#if ENABLE_GLOBAL_HANDLE_ZAPPING
    HandleScope::ZapRange(block_start, block_start + kHandleBlockSize,
                          kPersistentHandleZapValue);
#endif
    DeleteArray(block_start);
  }
}

#ifdef DEBUG
void PersistentHandles::Attach(LocalHeap* local_heap) {
  DCHECK_NULL(owner_);
  owner_ = local_heap;
}

void PersistentHandles::Detach() {
  DCHECK_NOT_NULL(owner_);
  owner_ = nullptr;
}

void PersistentHandles::CheckOwnerIsNotParked() {
  if (owner_) DCHECK(!owner_->IsParked());
}

bool PersistentHandles::Contains(Address* location) {
  auto it = ordered_blocks_.upper_bound(location);
  if (it == ordered_blocks_.begin()) return false;
  --it;
  DCHECK_LE(*it, location);
  if (*it == blocks_.back()) {
    // The last block is a special case because it may have
    // less than block_size_ handles.
    return location < block_next_;
  }
  return location < *it + kHandleBlockSize;
}
#endif

void PersistentHandles::AddBlock() {
  DCHECK_EQ(block_next_, block_limit_);

  Address* block_start = NewArray<Address>(kHandleBlockSize);
  blocks_.push_back(block_start);

  block_next_ = block_start;
  block_limit_ = block_start + kHandleBlockSize;

#ifdef DEBUG
  ordered_blocks_.insert(block_start);
#endif
}

Address* PersistentHandles::GetHandle(Address value) {
  if (block_next_ == block_limit_) {
    AddBlock();
  }

  DCHECK_LT(block_next_, block_limit_);
  *block_next_ = value;
  return block_next_++;
}

void PersistentHandles::Iterate(RootVisitor* visitor) {
  for (int i = 0; i < static_cast<int>(blocks_.size()) - 1; i++) {
    Address* block_start = blocks_[i];
    Address* block_end = block_start + kHandleBlockSize;
    visitor->VisitRootPointers(Root::kHandleScope, nullptr,
                               FullObjectSlot(block_start),
                               FullObjectSlot(block_end));
  }

  if (!blocks_.empty()) {
    Address* block_start = blocks_.back();
    visitor->VisitRootPointers(Root::kHandleScope, nullptr,
                               FullObjectSlot(block_start),
                               FullObjectSlot(block_next_));
  }
}

void PersistentHandlesList::Add(PersistentHandles* persistent_handles) {
  base::SpinningMutexGuard guard(&persistent_handles_mutex_);
  if (persistent_handles_head_)
    persistent_handles_head_->prev_ = persistent_handles;
  persistent_handles->prev_ = nullptr;
  persistent_handles->next_ = persistent_handles_head_;
  persistent_handles_head_ = persistent_handles;
}

void PersistentHandlesList::Remove(PersistentHandles* persistent_handles) {
  base::SpinningMutexGuard guard(&persistent_handles_mutex_);
  if (persistent_handles->next_)
    persistent_handles->next_->prev_ = persistent_handles->prev_;
  if (persistent_handles->prev_)
    persistent_handles->prev_->next_ = persistent_handles->next_;
  else
    persistent_handles_head_ = persistent_handles->next_;
}

void PersistentHandlesList::Iterate(RootVisitor* visitor, Isolate* isolate) {
  isolate->heap()->safepoint()->AssertActive();
  base::SpinningMutexGuard guard(&persistent_handles_mutex_);
  for (PersistentHandles* current = persistent_handles_head_; current;
       current = current->next_) {
    current->Iterate(visitor);
  }
}

PersistentHandlesScope::PersistentHandlesScope(Isolate* isolate)
    : impl_(isolate->handle_scope_implementer()) {
  impl_->BeginPersistentScope();
  HandleScopeData* data = impl_->isolate()->handle_scope_data();
  Address* new_next = impl_->GetSpareOrNewBlock();
  Address* new_limit = &new_next[kHandleBlockSize];
  impl_->blocks()->push_back(new_next);

#ifdef DEBUG
  prev_level_ = data->level;
#endif
  data->level++;
  first_block_ = new_next;
  prev_limit_ = data->limit;
  prev_next_ = data->next;
  data->next = new_next;
  data->limit = new_limit;
}

PersistentHandlesScope::~PersistentHandlesScope() {
  DCHECK(handles_detached_);
  impl_->isolate()->handle_scope_data()->level--;
  DCHECK_EQ(impl_->isolate()->handle_scope_data()->level, prev_level_);
}

std::unique_ptr<PersistentHandles> PersistentHandlesScope::Detach() {
  std::unique_ptr<PersistentHandles> ph = impl_->DetachPersistent(first_block_);
  HandleScopeData* data = impl_->isolate()->handle_scope_data();
  data->next = prev_next_;
  data->limit = prev_limit_;
#ifdef DEBUG
  handles_detached_ = true;
#endif
  return ph;
}

// static
bool PersistentHandlesScope::IsActive(Isolate* isolate) {
  return isolate->handle_scope_implementer()->HasPersistentScope();
}

}  // namespace internal
}  // namespace v8
