// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/safepoint.h"

#include <atomic>
#include <vector>

#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/handles/local-handles.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/parked-scope.h"
#include "src/logging/counters-scopes.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

IsolateSafepoint::IsolateSafepoint(Heap* heap) : heap_(heap) {}

void IsolateSafepoint::EnterLocalSafepointScope() {
  // Safepoints need to be initiated on some main thread.
  DCHECK_NULL(LocalHeap::Current());
  DCHECK(AllowGarbageCollection::IsAllowed());

  LockMutex(isolate()->main_thread_local_heap());
  if (++active_safepoint_scopes_ > 1) return;

  // Local safepoint can only be initiated on the isolate's main thread.
  DCHECK_EQ(ThreadId::Current(), isolate()->thread_id());

  TimedHistogramScope timer(isolate()->counters()->gc_time_to_safepoint());
  TRACE_GC(heap_->tracer(), GCTracer::Scope::TIME_TO_SAFEPOINT);

  barrier_.Arm();
  RunningLocalHeaps running_local_heaps;
  SetSafepointRequestedFlags(IncludeMainThread::kNo, running_local_heaps);
  barrier_.WaitUntilRunningThreadsInSafepoint(running_local_heaps);
}

class PerClientSafepointData final {
 public:
  explicit PerClientSafepointData(Isolate* isolate) : isolate_(isolate) {}

  void set_locked() { locked_ = true; }

  IsolateSafepoint* safepoint() const { return heap()->safepoint(); }
  Heap* heap() const { return isolate_->heap(); }
  Isolate* isolate() const { return isolate_; }

  bool is_locked() const { return locked_; }

  IsolateSafepoint::RunningLocalHeaps& running() { return running_; }
  const IsolateSafepoint::RunningLocalHeaps& running() const {
    return running_;
  }

 private:
  Isolate* const isolate_;
  IsolateSafepoint::RunningLocalHeaps running_;
  bool locked_ = false;
};

void IsolateSafepoint::InitiateGlobalSafepointScope(
    Isolate* initiator, PerClientSafepointData* client_data) {
  shared_space_isolate()->global_safepoint()->AssertActive();
  LockMutex(initiator->main_thread_local_heap());
  InitiateGlobalSafepointScopeRaw(initiator, client_data);
}

void IsolateSafepoint::TryInitiateGlobalSafepointScope(
    Isolate* initiator, PerClientSafepointData* client_data) {
  shared_space_isolate()->global_safepoint()->AssertActive();
  if (!local_heaps_mutex_.TryLock()) return;
  InitiateGlobalSafepointScopeRaw(initiator, client_data);
}

class GlobalSafepointInterruptTask : public CancelableTask {
 public:
  explicit GlobalSafepointInterruptTask(Heap* heap)
      : CancelableTask(heap->isolate()), heap_(heap) {}

  ~GlobalSafepointInterruptTask() override = default;
  GlobalSafepointInterruptTask(const GlobalSafepointInterruptTask&) = delete;
  GlobalSafepointInterruptTask& operator=(const GlobalSafepointInterruptTask&) =
      delete;

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override { heap_->main_thread_local_heap()->Safepoint(); }

  Heap* heap_;
};

void IsolateSafepoint::InitiateGlobalSafepointScopeRaw(
    Isolate* initiator, PerClientSafepointData* client_data) {
  CHECK_EQ(++active_safepoint_scopes_, 1);
  barrier_.Arm();

  SetSafepointRequestedFlags(ShouldIncludeMainThread(initiator),
                             client_data->running());
  client_data->set_locked();

  if (isolate() != initiator) {
    // An isolate might be waiting in the event loop. Post a task in order to
    // wake it up.
    isolate()->heap()->GetForegroundTaskRunner()->PostTask(
        std::make_unique<GlobalSafepointInterruptTask>(heap_));

    // Request an interrupt in case of long-running code.
    isolate()->stack_guard()->RequestGlobalSafepoint();
  }
}

IsolateSafepoint::IncludeMainThread IsolateSafepoint::ShouldIncludeMainThread(
    Isolate* initiator) {
  const bool is_initiator = isolate() == initiator;
  return is_initiator ? IncludeMainThread::kNo : IncludeMainThread::kYes;
}

void IsolateSafepoint::SetSafepointRequestedFlags(
    IncludeMainThread include_main_thread,
    IsolateSafepoint::RunningLocalHeaps& running_local_heaps) {
  // There needs to be at least one LocalHeap for the main thread.
  DCHECK_NOT_NULL(local_heaps_head_);

  DCHECK(running_local_heaps.empty());

  for (LocalHeap* local_heap = local_heaps_head_; local_heap;
       local_heap = local_heap->next_) {
    if (local_heap->is_main_thread() &&
        include_main_thread == IncludeMainThread::kNo) {
      continue;
    }

    const LocalHeap::ThreadState old_state =
        local_heap->state_.SetSafepointRequested();

    if (old_state.IsRunning()) {
#if V8_OS_DARWIN
      pthread_override_t qos_override = nullptr;

      if (v8_flags.safepoint_bump_qos_class) {
        // Bump the quality-of-service class to prevent priority inversion (high
        // priority main thread blocking on lower priority background threads).
        qos_override = pthread_override_qos_class_start_np(
            local_heap->thread_handle(), QOS_CLASS_USER_INTERACTIVE, 0);
        CHECK_NOT_NULL(qos_override);
      }

      running_local_heaps.emplace_back(local_heap, qos_override);
#else
      running_local_heaps.emplace_back(local_heap);
#endif
    }
    CHECK_IMPLIES(old_state.IsCollectionRequested(),
                  local_heap->is_main_thread());
    CHECK(!old_state.IsSafepointRequested());
  }
}

void IsolateSafepoint::LockMutex(LocalHeap* local_heap) {
  if (!local_heaps_mutex_.TryLock()) {
    // Safepoints are only used for GCs, so GC requests should be ignored by
    // default when parking for a safepoint.
    IgnoreLocalGCRequests ignore_gc_requests(local_heap->heap());
    local_heap->ExecuteWhileParked([this]() { local_heaps_mutex_.Lock(); });
  }
}

void IsolateSafepoint::LeaveGlobalSafepointScope(Isolate* initiator) {
  local_heaps_mutex_.AssertHeld();
  CHECK_EQ(--active_safepoint_scopes_, 0);
  ClearSafepointRequestedFlags(ShouldIncludeMainThread(initiator));
  barrier_.Disarm();
  local_heaps_mutex_.Unlock();
}

void IsolateSafepoint::LeaveLocalSafepointScope() {
  local_heaps_mutex_.AssertHeld();
  DCHECK_GT(active_safepoint_scopes_, 0);

  if (--active_safepoint_scopes_ == 0) {
    ClearSafepointRequestedFlags(IncludeMainThread::kNo);
    barrier_.Disarm();
  }

  local_heaps_mutex_.Unlock();
}

void IsolateSafepoint::ClearSafepointRequestedFlags(
    IncludeMainThread include_main_thread) {
  for (LocalHeap* local_heap = local_heaps_head_; local_heap;
       local_heap = local_heap->next_) {
    if (local_heap->is_main_thread() &&
        include_main_thread == IncludeMainThread::kNo) {
      continue;
    }

    const LocalHeap::ThreadState old_state =
        local_heap->state_.ClearSafepointRequested();

    CHECK(old_state.IsParked());
    CHECK(old_state.IsSafepointRequested());
    CHECK_IMPLIES(old_state.IsCollectionRequested(),
                  local_heap->is_main_thread());
  }
}

void IsolateSafepoint::WaitInSafepoint() { barrier_.WaitInSafepoint(); }

void IsolateSafepoint::WaitInUnpark() { barrier_.WaitInUnpark(); }

void IsolateSafepoint::NotifyPark() { barrier_.NotifyPark(); }

void IsolateSafepoint::WaitUntilRunningThreadsInSafepoint(
    const PerClientSafepointData* client_data) {
  barrier_.WaitUntilRunningThreadsInSafepoint(client_data->running());
}

void IsolateSafepoint::Barrier::Arm() {
  base::MutexGuard guard(&mutex_);
  DCHECK(!IsArmed());
  armed_ = true;
  stopped_ = 0;
}

void IsolateSafepoint::Barrier::Disarm() {
  base::MutexGuard guard(&mutex_);
  DCHECK(IsArmed());
  armed_ = false;
  stopped_ = 0;
  cv_resume_.NotifyAll();
}

void IsolateSafepoint::Barrier::WaitUntilRunningThreadsInSafepoint(
    const IsolateSafepoint::RunningLocalHeaps& running_local_heaps) {
  base::MutexGuard guard(&mutex_);
  DCHECK(IsArmed());
  size_t running_count = running_local_heaps.size();
  while (stopped_ < running_count) {
    cv_stopped_.Wait(&mutex_);
  }
#if V8_OS_DARWIN
  if (v8_flags.safepoint_bump_qos_class) {
    for (auto& running_local_heap : running_local_heaps) {
      CHECK_EQ(
          pthread_override_qos_class_end_np(running_local_heap.qos_override),
          0);
    }
  }
#endif
  DCHECK_EQ(stopped_, running_count);
}

void IsolateSafepoint::Barrier::NotifyPark() {
  base::MutexGuard guard(&mutex_);
  CHECK(IsArmed());
  stopped_++;
  cv_stopped_.NotifyOne();
}

void IsolateSafepoint::Barrier::WaitInSafepoint() {
  const auto scoped_blocking_call =
      V8::GetCurrentPlatform()->CreateBlockingScope(BlockingType::kWillBlock);
  base::MutexGuard guard(&mutex_);
  CHECK(IsArmed());
  stopped_++;
  cv_stopped_.NotifyOne();

  while (IsArmed()) {
    cv_resume_.Wait(&mutex_);
  }
}

void IsolateSafepoint::Barrier::WaitInUnpark() {
  const auto scoped_blocking_call =
      V8::GetCurrentPlatform()->CreateBlockingScope(BlockingType::kWillBlock);
  base::MutexGuard guard(&mutex_);

  while (IsArmed()) {
    cv_resume_.Wait(&mutex_);
  }
}

void IsolateSafepoint::Iterate(RootVisitor* visitor) {
  AssertActive();
  for (LocalHeap* current = local_heaps_head_; current;
       current = current->next_) {
    current->handles()->Iterate(visitor);
  }
}

void IsolateSafepoint::AssertMainThreadIsOnlyThread() {
  DCHECK_EQ(local_heaps_head_, heap_->main_thread_local_heap());
  DCHECK_NULL(heap_->main_thread_local_heap()->next_);
}

Isolate* IsolateSafepoint::isolate() const { return heap_->isolate(); }

Isolate* IsolateSafepoint::shared_space_isolate() const {
  return isolate()->shared_space_isolate();
}

IsolateSafepointScope::IsolateSafepointScope(Heap* heap)
    : safepoint_(heap->safepoint()) {
  safepoint_->EnterLocalSafepointScope();
}

IsolateSafepointScope::~IsolateSafepointScope() {
  safepoint_->LeaveLocalSafepointScope();
}

GlobalSafepoint::GlobalSafepoint(Isolate* isolate)
    : shared_space_isolate_(isolate) {}

void GlobalSafepoint::AppendClient(Isolate* client) {
  clients_mutex_.AssertHeld();

  DCHECK_NULL(client->global_safepoint_prev_client_isolate_);
  DCHECK_NULL(client->global_safepoint_next_client_isolate_);
  DCHECK_NE(clients_head_, client);

  if (clients_head_) {
    clients_head_->global_safepoint_prev_client_isolate_ = client;
  }

  client->global_safepoint_prev_client_isolate_ = nullptr;
  client->global_safepoint_next_client_isolate_ = clients_head_;

  clients_head_ = client;
}

void GlobalSafepoint::RemoveClient(Isolate* client) {
  DCHECK_EQ(client->heap()->gc_state(), Heap::TEAR_DOWN);
  AssertActive();

  if (client->global_safepoint_next_client_isolate_) {
    client->global_safepoint_next_client_isolate_
        ->global_safepoint_prev_client_isolate_ =
        client->global_safepoint_prev_client_isolate_;
  }

  if (client->global_safepoint_prev_client_isolate_) {
    client->global_safepoint_prev_client_isolate_
        ->global_safepoint_next_client_isolate_ =
        client->global_safepoint_next_client_isolate_;
  } else {
    DCHECK_EQ(clients_head_, client);
    clients_head_ = client->global_safepoint_next_client_isolate_;
  }
}

void GlobalSafepoint::AssertNoClientsOnTearDown() {
  DCHECK_NULL(clients_head_);
}

void GlobalSafepoint::EnterGlobalSafepointScope(Isolate* initiator) {
  // Safepoints need to be initiated on some main thread.
  DCHECK_NULL(LocalHeap::Current());

  if (!clients_mutex_.TryLock()) {
    IgnoreLocalGCRequests ignore_gc_requests(initiator->heap());
    initiator->main_thread_local_heap()->ExecuteWhileParked(
        [this]() { clients_mutex_.Lock(); });
  }

  if (++active_safepoint_scopes_ > 1) return;

  TimedHistogramScope timer(
      initiator->counters()->gc_time_to_global_safepoint());
  TRACE_GC(initiator->heap()->tracer(),
           GCTracer::Scope::TIME_TO_GLOBAL_SAFEPOINT);

  std::vector<PerClientSafepointData> clients;

  // Try to initiate safepoint for all clients. Fail immediately when the
  // local_heaps_mutex_ can't be locked without blocking.
  IterateSharedSpaceAndClientIsolates([&clients, initiator](Isolate* client) {
    clients.emplace_back(client);
    client->heap()->safepoint()->TryInitiateGlobalSafepointScope(
        initiator, &clients.back());
  });

  // Iterate all clients again to initiate the safepoint for all of them - even
  // if that means blocking.
  for (PerClientSafepointData& client : clients) {
    if (client.is_locked()) continue;
    client.safepoint()->InitiateGlobalSafepointScope(initiator, &client);
  }

#if DEBUG
  for (const PerClientSafepointData& client : clients) {
    DCHECK_EQ(client.isolate()->shared_space_isolate(), shared_space_isolate_);
  }
#endif  // DEBUG

  // Now that safepoints were initiated for all clients, wait until all threads
  // of all clients reached a safepoint.
  for (const PerClientSafepointData& client : clients) {
    DCHECK(client.is_locked());
    client.safepoint()->WaitUntilRunningThreadsInSafepoint(&client);
  }
}

void GlobalSafepoint::LeaveGlobalSafepointScope(Isolate* initiator) {
  clients_mutex_.AssertHeld();
  DCHECK_GT(active_safepoint_scopes_, 0);

  if (--active_safepoint_scopes_ == 0) {
    IterateSharedSpaceAndClientIsolates([initiator](Isolate* client) {
      Heap* client_heap = client->heap();
      client_heap->safepoint()->LeaveGlobalSafepointScope(initiator);
    });
  }

  clients_mutex_.Unlock();
}

bool GlobalSafepoint::IsRequestedForTesting() {
  if (!clients_mutex_.TryLock()) return true;
  clients_mutex_.Unlock();
  return false;
}

GlobalSafepointScope::GlobalSafepointScope(Isolate* initiator)
    : initiator_(initiator),
      shared_space_isolate_(initiator->shared_space_isolate()) {
  shared_space_isolate_->global_safepoint()->EnterGlobalSafepointScope(
      initiator_);
}

GlobalSafepointScope::~GlobalSafepointScope() {
  shared_space_isolate_->global_safepoint()->LeaveGlobalSafepointScope(
      initiator_);
}

SafepointScope::SafepointScope(Isolate* initiator, SafepointKind kind) {
  if (kind == SafepointKind::kIsolate) {
    isolate_safepoint_.emplace(initiator->heap());
  } else {
    DCHECK_EQ(kind, SafepointKind::kGlobal);
    global_safepoint_.emplace(initiator);
  }
}

SafepointScope::SafepointScope(Isolate* initiator,
                               GlobalSafepointForSharedSpaceIsolateTag) {
  if (initiator->is_shared_space_isolate()) {
    global_safepoint_.emplace(initiator);
  } else {
    isolate_safepoint_.emplace(initiator->heap());
  }
}

}  // namespace internal
}  // namespace v8
