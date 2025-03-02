// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/v8threads.h"

#include "include/v8-locker.h"
#include "src/api/api.h"
#include "src/debug/debug.h"
#include "src/execution/execution.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/stack-guard.h"
#include "src/init/bootstrapper.h"
#include "src/objects/visitors.h"
#include "src/regexp/regexp-stack.h"

namespace v8 {

namespace {

// Track whether this V8 instance has ever called v8::Locker. This allows the
// API code to verify that the lock is always held when V8 is being entered.
base::AtomicWord g_locker_was_ever_used_ = 0;

}  // namespace

// Once the Locker is initialized, the current thread will be guaranteed to have
// the lock for a given isolate.
void Locker::Initialize(v8::Isolate* isolate) {
  DCHECK_NOT_NULL(isolate);
  has_lock_ = false;
  top_level_ = true;
  isolate_ = reinterpret_cast<i::Isolate*>(isolate);

  // Record that the Locker has been used at least once.
  base::Relaxed_Store(&g_locker_was_ever_used_, 1);
  isolate_->set_was_locker_ever_used();

  // Get the big lock if necessary.
  if (!isolate_->thread_manager()->IsLockedByCurrentThread()) {
    isolate_->thread_manager()->Lock();
    has_lock_ = true;

    // This may be a locker within an unlocker in which case we have to
    // get the saved state for this thread and restore it.
    if (isolate_->thread_manager()->RestoreThread()) {
      top_level_ = false;
    }
  }
  DCHECK(isolate_->thread_manager()->IsLockedByCurrentThread());
}

bool Locker::IsLocked(v8::Isolate* isolate) {
  DCHECK_NOT_NULL(isolate);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  return i_isolate->thread_manager()->IsLockedByCurrentThread();
}

Locker::~Locker() {
  DCHECK(isolate_->thread_manager()->IsLockedByCurrentThread());
  if (has_lock_) {
    if (top_level_) {
      isolate_->thread_manager()->FreeThreadResources();
    } else {
      isolate_->thread_manager()->ArchiveThread();
    }
    isolate_->thread_manager()->Unlock();
  }
}

void Unlocker::Initialize(v8::Isolate* isolate) {
  DCHECK_NOT_NULL(isolate);
  isolate_ = reinterpret_cast<i::Isolate*>(isolate);
  DCHECK(isolate_->thread_manager()->IsLockedByCurrentThread());
  isolate_->thread_manager()->ArchiveThread();
  isolate_->thread_manager()->Unlock();
}

Unlocker::~Unlocker() {
  DCHECK(!isolate_->thread_manager()->IsLockedByCurrentThread());
  isolate_->thread_manager()->Lock();
  isolate_->thread_manager()->RestoreThread();
}

namespace internal {

void ThreadManager::InitThread(const ExecutionAccess& lock) {
  isolate_->InitializeThreadLocal();
  isolate_->stack_guard()->InitThread(lock);
  isolate_->debug()->InitThread(lock);
}

bool ThreadManager::RestoreThread() {
  DCHECK(IsLockedByCurrentThread());
  // First check whether the current thread has been 'lazily archived', i.e.
  // not archived at all.  If that is the case we put the state storage we
  // had prepared back in the free list, since we didn't need it after all.
  if (lazily_archived_thread_ == ThreadId::Current()) {
    lazily_archived_thread_ = ThreadId::Invalid();
    Isolate::PerIsolateThreadData* per_thread =
        isolate_->FindPerThreadDataForThisThread();
    DCHECK_NOT_NULL(per_thread);
    DCHECK(per_thread->thread_state() == lazily_archived_thread_state_);
    lazily_archived_thread_state_->set_id(ThreadId::Invalid());
    lazily_archived_thread_state_->LinkInto(ThreadState::FREE_LIST);
    lazily_archived_thread_state_ = nullptr;
    per_thread->set_thread_state(nullptr);
    return true;
  }

  // Make sure that the preemption thread cannot modify the thread state while
  // it is being archived or restored.
  ExecutionAccess access(isolate_);

  // If there is another thread that was lazily archived then we have to really
  // archive it now.
  if (lazily_archived_thread_.IsValid()) {
    EagerlyArchiveThread();
  }
  Isolate::PerIsolateThreadData* per_thread =
      isolate_->FindPerThreadDataForThisThread();
  if (per_thread == nullptr || per_thread->thread_state() == nullptr) {
    // This is a new thread.
    InitThread(access);
    return false;
  }
  // In case multi-cage pointer compression mode is enabled ensure that
  // current thread's cage base values are properly initialized.
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate_);

  ThreadState* state = per_thread->thread_state();
  char* from = state->data();
  from = isolate_->handle_scope_implementer()->RestoreThread(from);
  from = isolate_->RestoreThread(from);
  from = Relocatable::RestoreState(isolate_, from);
  // Stack guard should be restored before Debug, etc. since Debug etc. might
  // depend on a correct stack guard.
  from = isolate_->stack_guard()->RestoreStackGuard(from);
  from = isolate_->debug()->RestoreDebug(from);
  from = isolate_->regexp_stack()->RestoreStack(from);
  from = isolate_->bootstrapper()->RestoreState(from);
  per_thread->set_thread_state(nullptr);
  state->set_id(ThreadId::Invalid());
  state->Unlink();
  state->LinkInto(ThreadState::FREE_LIST);
  return true;
}

void ThreadManager::Lock() {
  mutex_.Lock();
  mutex_owner_.store(ThreadId::Current(), std::memory_order_relaxed);
  DCHECK(IsLockedByCurrentThread());
}

void ThreadManager::Unlock() {
  mutex_owner_.store(ThreadId::Invalid(), std::memory_order_relaxed);
  mutex_.Unlock();
}

static int ArchiveSpacePerThread() {
  return HandleScopeImplementer::ArchiveSpacePerThread() +
         Isolate::ArchiveSpacePerThread() + Debug::ArchiveSpacePerThread() +
         StackGuard::ArchiveSpacePerThread() +
         RegExpStack::ArchiveSpacePerThread() +
         Bootstrapper::ArchiveSpacePerThread() +
         Relocatable::ArchiveSpacePerThread();
}

ThreadState::ThreadState(ThreadManager* thread_manager)
    : id_(ThreadId::Invalid()),
      data_(nullptr),
      next_(this),
      previous_(this),
      thread_manager_(thread_manager) {}

ThreadState::~ThreadState() { DeleteArray<char>(data_); }

void ThreadState::AllocateSpace() {
  data_ = NewArray<char>(ArchiveSpacePerThread());
}

void ThreadState::Unlink() {
  next_->previous_ = previous_;
  previous_->next_ = next_;
}

void ThreadState::LinkInto(List list) {
  ThreadState* flying_anchor = list == FREE_LIST
                                   ? thread_manager_->free_anchor_
                                   : thread_manager_->in_use_anchor_;
  next_ = flying_anchor->next_;
  previous_ = flying_anchor;
  flying_anchor->next_ = this;
  next_->previous_ = this;
}

ThreadState* ThreadManager::GetFreeThreadState() {
  ThreadState* gotten = free_anchor_->next_;
  if (gotten == free_anchor_) {
    ThreadState* new_thread_state = new ThreadState(this);
    new_thread_state->AllocateSpace();
    return new_thread_state;
  }
  return gotten;
}

// Gets the first in the list of archived threads.
ThreadState* ThreadManager::FirstThreadStateInUse() {
  return in_use_anchor_->Next();
}

ThreadState* ThreadState::Next() {
  if (next_ == thread_manager_->in_use_anchor_) return nullptr;
  return next_;
}

// Thread ids must start with 1, because in TLS having thread id 0 can't
// be distinguished from not having a thread id at all (since NULL is
// defined as 0.)
ThreadManager::ThreadManager(Isolate* isolate)
    : mutex_owner_(ThreadId::Invalid()),
      lazily_archived_thread_(ThreadId::Invalid()),
      lazily_archived_thread_state_(nullptr),
      free_anchor_(nullptr),
      in_use_anchor_(nullptr),
      isolate_(isolate) {
  free_anchor_ = new ThreadState(this);
  in_use_anchor_ = new ThreadState(this);
}

ThreadManager::~ThreadManager() {
  DeleteThreadStateList(free_anchor_);
  DeleteThreadStateList(in_use_anchor_);
}

void ThreadManager::DeleteThreadStateList(ThreadState* anchor) {
  // The list starts and ends with the anchor.
  for (ThreadState* current = anchor->next_; current != anchor;) {
    ThreadState* next = current->next_;
    delete current;
    current = next;
  }
  delete anchor;
}

void ThreadManager::ArchiveThread() {
  DCHECK_EQ(lazily_archived_thread_, ThreadId::Invalid());
  DCHECK(!IsArchived());
  DCHECK(IsLockedByCurrentThread());
  ThreadState* state = GetFreeThreadState();
  state->Unlink();
  Isolate::PerIsolateThreadData* per_thread =
      isolate_->FindOrAllocatePerThreadDataForThisThread();
  per_thread->set_thread_state(state);
  lazily_archived_thread_ = ThreadId::Current();
  lazily_archived_thread_state_ = state;
  DCHECK_EQ(state->id(), ThreadId::Invalid());
  state->set_id(CurrentId());
  DCHECK_NE(state->id(), ThreadId::Invalid());
}

void ThreadManager::EagerlyArchiveThread() {
  DCHECK(IsLockedByCurrentThread());
  ThreadState* state = lazily_archived_thread_state_;
  state->LinkInto(ThreadState::IN_USE_LIST);
  char* to = state->data();
  // Ensure that data containing GC roots are archived first, and handle them
  // in ThreadManager::Iterate(RootVisitor*).
  to = isolate_->handle_scope_implementer()->ArchiveThread(to);
  to = isolate_->ArchiveThread(to);
  to = Relocatable::ArchiveState(isolate_, to);
  to = isolate_->stack_guard()->ArchiveStackGuard(to);
  to = isolate_->debug()->ArchiveDebug(to);
  to = isolate_->regexp_stack()->ArchiveStack(to);
  to = isolate_->bootstrapper()->ArchiveState(to);
  lazily_archived_thread_ = ThreadId::Invalid();
  lazily_archived_thread_state_ = nullptr;
}

void ThreadManager::FreeThreadResources() {
  // This method might be called on a thread that's not bound to any Isolate
  // and thus pointer compression schemes might have cage base value unset.
  // So, allow heap access here to let the checks work.
  PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate_);

  DCHECK(!isolate_->has_exception());
  DCHECK_NULL(isolate_->try_catch_handler());
  isolate_->handle_scope_implementer()->FreeThreadResources();
  isolate_->FreeThreadResources();
  isolate_->debug()->FreeThreadResources();
  isolate_->stack_guard()->FreeThreadResources();
  isolate_->regexp_stack()->FreeThreadResources();
  isolate_->bootstrapper()->FreeThreadResources();
}

bool ThreadManager::IsArchived() {
  Isolate::PerIsolateThreadData* data =
      isolate_->FindPerThreadDataForThisThread();
  return data != nullptr && data->thread_state() != nullptr;
}

void ThreadManager::Iterate(RootVisitor* v) {
  // Expecting no threads during serialization/deserialization
  for (ThreadState* state = FirstThreadStateInUse(); state != nullptr;
       state = state->Next()) {
    char* data = state->data();
    data = HandleScopeImplementer::Iterate(v, data);
    data = isolate_->Iterate(v, data);
    data = Relocatable::Iterate(v, data);
    data = StackGuard::Iterate(v, data);
    data = Debug::Iterate(v, data);
  }
}

void ThreadManager::IterateArchivedThreads(ThreadVisitor* v) {
  for (ThreadState* state = FirstThreadStateInUse(); state != nullptr;
       state = state->Next()) {
    char* data = state->data();
    data += HandleScopeImplementer::ArchiveSpacePerThread();
    isolate_->IterateThread(v, data);
  }
}

ThreadId ThreadManager::CurrentId() { return ThreadId::Current(); }

}  // namespace internal
}  // namespace v8
