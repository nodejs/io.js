// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-atomics-synchronization.h"

#include "src/base/macros.h"
#include "src/base/platform/yield-processor.h"
#include "src/execution/isolate-inl.h"
#include "src/objects/js-atomics-synchronization-inl.h"
#include "src/objects/js-promise-inl.h"
#include "src/objects/waiter-queue-node.h"
#include "src/sandbox/external-pointer-inl.h"

namespace v8 {
namespace internal {

namespace detail {
class WaiterQueueNode;
}

namespace {

// TODO(v8:12547): Move this logic into a static method JSPromise::PerformThen
// so that other callsites like this one can use it.
// Set fulfill/reject handlers for a JSPromise object.
MaybeDirectHandle<JSReceiver> PerformPromiseThen(
    Isolate* isolate, DirectHandle<JSReceiver> promise,
    DirectHandle<Object> fulfill_handler,
    MaybeDirectHandle<JSFunction> maybe_reject_handler = {}) {
  DCHECK(IsCallable(*fulfill_handler));
  DirectHandle<Object> reject_handler = isolate->factory()->undefined_value();
  if (!maybe_reject_handler.is_null()) {
    reject_handler = maybe_reject_handler.ToHandleChecked();
  }
  DirectHandle<Object> args[] = {fulfill_handler, reject_handler};

  DirectHandle<Object> then_result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, then_result,
      Execution::CallBuiltin(isolate, isolate->promise_then(), promise,
                             base::VectorOf(args)));

  return Cast<JSReceiver>(then_result);
}

MaybeDirectHandle<Context> SetAsyncUnlockHandlers(
    Isolate* isolate, DirectHandle<JSAtomicsMutex> mutex,
    DirectHandle<JSReceiver> waiting_for_callback_promise,
    DirectHandle<JSPromise> unlocked_promise) {
  DirectHandle<Context> handlers_context =
      isolate->factory()->NewBuiltinContext(
          isolate->native_context(), JSAtomicsMutex::kAsyncContextLength);
  handlers_context->set(JSAtomicsMutex::kMutexAsyncContextSlot, *mutex);
  handlers_context->set(JSAtomicsMutex::kUnlockedPromiseAsyncContextSlot,
                        *unlocked_promise);

  DirectHandle<SharedFunctionInfo> resolve_info(
      isolate->heap()->atomics_mutex_async_unlock_resolve_handler_sfi(),
      isolate);
  DirectHandle<JSFunction> resolver_callback =
      Factory::JSFunctionBuilder{isolate, resolve_info, handlers_context}
          .set_map(isolate->strict_function_without_prototype_map())
          .set_allocation_type(AllocationType::kYoung)
          .Build();

  DirectHandle<SharedFunctionInfo> reject_info(
      isolate->heap()->atomics_mutex_async_unlock_reject_handler_sfi(),
      isolate);
  DirectHandle<JSFunction> reject_callback =
      Factory::JSFunctionBuilder{isolate, reject_info, handlers_context}
          .set_map(isolate->strict_function_without_prototype_map())
          .set_allocation_type(AllocationType::kYoung)
          .Build();

  RETURN_ON_EXCEPTION(isolate,
                      PerformPromiseThen(isolate, waiting_for_callback_promise,
                                         resolver_callback, reject_callback));
  return handlers_context;
}

void AddPromiseToNativeContext(Isolate* isolate,
                               DirectHandle<JSPromise> promise) {
  DirectHandle<NativeContext> native_context(isolate->native_context());
  Handle<OrderedHashSet> promises(native_context->atomics_waitasync_promises(),
                                  isolate);
  promises = OrderedHashSet::Add(isolate, promises, promise).ToHandleChecked();
  native_context->set_atomics_waitasync_promises(*promises);
}

void RemovePromiseFromNativeContext(Isolate* isolate,
                                    DirectHandle<JSPromise> promise) {
  Handle<OrderedHashSet> promises(
      isolate->native_context()->atomics_waitasync_promises(), isolate);
  bool was_deleted = OrderedHashSet::Delete(isolate, *promises, *promise);
  DCHECK(was_deleted);
  USE(was_deleted);
  promises = OrderedHashSet::Shrink(isolate, promises);
  isolate->native_context()->set_atomics_waitasync_promises(*promises);
}

template <typename T>
Global<T> GetWeakGlobal(Isolate* isolate, Local<T> object) {
  auto* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  v8::Global<T> global(v8_isolate, object);
  global.SetWeak();
  return global;
}

}  // namespace

namespace detail {

// The waiter queue lock guard provides a RAII-style mechanism for locking the
// waiter queue. It is a non copyable and non movable object and a new state
// must be set before destroying the guard.
class V8_NODISCARD WaiterQueueLockGuard final {
  using StateT = JSSynchronizationPrimitive::StateT;

 public:
  // Spinlock to acquire the IsWaiterQueueLockedField bit. current_state is
  // updated to the last value of the state before the waiter queue lock was
  // acquired.
  explicit WaiterQueueLockGuard(std::atomic<StateT>* state,
                                StateT& current_state)
      : state_(state), new_state_(kInvalidState) {
    while (!JSSynchronizationPrimitive::TryLockWaiterQueueExplicit(
        state, current_state)) {
      YIELD_PROCESSOR;
    }
  }

  // Constructor for creating a wrapper around a state whose waiter queue
  // is already locked and owned by this thread.
  explicit WaiterQueueLockGuard(std::atomic<StateT>* state, bool is_locked)
      : state_(state), new_state_(kInvalidState) {
    CHECK(is_locked);
    DCHECK(JSSynchronizationPrimitive::IsWaiterQueueLockedField::decode(
        state->load()));
  }

  WaiterQueueLockGuard(const WaiterQueueLockGuard&) = delete;
  WaiterQueueLockGuard() = delete;

  ~WaiterQueueLockGuard() {
    DCHECK_NOT_NULL(state_);
    DCHECK_NE(new_state_, kInvalidState);
    DCHECK(JSSynchronizationPrimitive::IsWaiterQueueLockedField::decode(
        state_->load()));
    new_state_ = JSSynchronizationPrimitive::IsWaiterQueueLockedField::update(
        new_state_, false);
    state_->store(new_state_, std::memory_order_release);
  }

  void set_new_state(StateT new_state) { new_state_ = new_state; }

  static std::optional<WaiterQueueLockGuard>
  NewAlreadyLockedWaiterQueueLockGuard(std::atomic<StateT>* state) {
    return std::optional<WaiterQueueLockGuard>(std::in_place, state, true);
  }

 private:
  static constexpr StateT kInvalidState =
      ~JSSynchronizationPrimitive::kEmptyState;
  std::atomic<StateT>* state_;
  StateT new_state_;
};

class V8_NODISCARD SyncWaiterQueueNode final : public WaiterQueueNode {
 public:
  explicit SyncWaiterQueueNode(Isolate* requester)
      : WaiterQueueNode(requester), should_wait_(true) {}

  void Wait() {
    AllowGarbageCollection allow_before_parking;
    requester_->main_thread_local_heap()->ExecuteWhileParked([this]() {
      base::MutexGuard guard(&wait_lock_);
      while (should_wait_) {
        wait_cond_var_.Wait(&wait_lock_);
      }
    });
  }

  // Returns false if timed out, true otherwise.
  bool WaitFor(const base::TimeDelta& rel_time) {
    bool result;
    AllowGarbageCollection allow_before_parking;
    requester_->main_thread_local_heap()->ExecuteWhileParked([this, rel_time,
                                                              &result]() {
      base::MutexGuard guard(&wait_lock_);
      base::TimeTicks current_time = base::TimeTicks::Now();
      base::TimeTicks timeout_time = current_time + rel_time;
      for (;;) {
        if (!should_wait_) {
          result = true;
          return;
        }
        current_time = base::TimeTicks::Now();
        if (current_time >= timeout_time) {
          result = false;
          return;
        }
        base::TimeDelta time_until_timeout = timeout_time - current_time;
        bool wait_res = wait_cond_var_.WaitFor(&wait_lock_, time_until_timeout);
        USE(wait_res);
        // The wake up may have been spurious, so loop again.
      }
    });
    return result;
  }

  void Notify() override {
    base::MutexGuard guard(&wait_lock_);
    should_wait_ = false;
    wait_cond_var_.NotifyOne();
    SetNotInListForVerification();
  }

  bool IsSameIsolateForAsyncCleanup(Isolate* isolate) override {
    // Sync waiters are only queued while the thread is sleeping, so there
    // should not be sync nodes while cleaning up the isolate.
    DCHECK_NE(requester_, isolate);
    return false;
  }

  void CleanupMatchingAsyncWaiters(const DequeueMatcher& matcher) override {
    UNREACHABLE();
  }

 private:
  void SetReadyForAsyncCleanup() override { UNREACHABLE(); }

  base::Mutex wait_lock_;
  base::ConditionVariable wait_cond_var_;
  bool should_wait_;
};

template <typename T>
class AsyncWaiterNotifyTask : public CancelableTask {
 public:
  AsyncWaiterNotifyTask(CancelableTaskManager* cancelable_task_manager,
                        typename T::AsyncWaiterNodeType* node)
      : CancelableTask(cancelable_task_manager), node_(node) {}

  void RunInternal() override {
    if (node_->requester_->cancelable_task_manager()->canceled()) return;
    T::HandleAsyncNotify(node_);
  }

 private:
  typename T::AsyncWaiterNodeType* node_;
};
template <typename T>
class AsyncWaiterTimeoutTask : public CancelableTask {
 public:
  AsyncWaiterTimeoutTask(CancelableTaskManager* cancelable_task_manager,
                         typename T::AsyncWaiterNodeType* node)
      : CancelableTask(cancelable_task_manager), node_(node) {}

  void RunInternal() override {
    if (node_->requester_->cancelable_task_manager()->canceled()) return;
    T::HandleAsyncTimeout(node_);
  }

 private:
  typename T::AsyncWaiterNodeType* node_;
};

template <typename T>
class V8_NODISCARD AsyncWaiterQueueNode final : public WaiterQueueNode {
 public:
  static AsyncWaiterQueueNode<T>* NewAsyncWaiterStoredInIsolate(
      Isolate* requester, DirectHandle<T> synchronization_primitive,
      Handle<JSPromise> internal_waiting_promise,
      MaybeHandle<JSPromise> unlocked_promise = {}) {
    auto waiter =
        std::unique_ptr<AsyncWaiterQueueNode<T>>(new AsyncWaiterQueueNode<T>(
            requester, synchronization_primitive, internal_waiting_promise,
            unlocked_promise));
    AsyncWaiterQueueNode<T>* raw_waiter = waiter.get();
    requester->async_waiter_queue_nodes().push_back(std::move(waiter));
    return raw_waiter;
  }

  // Creates a minimal LockAsyncWaiterQueueNode so that the isolate can keep
  // track of the locked mutexes and release them in case of isolate deinit.
  static AsyncWaiterQueueNode<T>* NewLockedAsyncWaiterStoredInIsolate(
      Isolate* requester, DirectHandle<T> synchronization_primitive) {
    DCHECK(IsJSAtomicsMutex(*synchronization_primitive));
    auto waiter = std::unique_ptr<AsyncWaiterQueueNode<T>>(
        new AsyncWaiterQueueNode<T>(requester, synchronization_primitive));
    AsyncWaiterQueueNode<T>* raw_waiter = waiter.get();
    requester->async_waiter_queue_nodes().push_back(std::move(waiter));
    return raw_waiter;
  }

  TaskRunner* task_runner() { return task_runner_.get(); }

  Local<v8::Context> GetNativeContext() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    return native_context_.Get(v8_isolate);
  }

  Handle<JSPromise> GetInternalWaitingPromise() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    Handle<JSPromise> internal_waiting_promise =
        Utils::OpenHandle(*internal_waiting_promise_.Get(v8_isolate));
    return internal_waiting_promise;
  }

  bool IsEmpty() const { return synchronization_primitive_.IsEmpty(); }

  Handle<T> GetSynchronizationPrimitive() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    Handle<T> synchronization_primitive =
        Cast<T>(Utils::OpenHandle(*synchronization_primitive_.Get(v8_isolate)));
    return synchronization_primitive;
  }

  Handle<JSPromise> GetUnlockedPromise() {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester_);
    Handle<JSPromise> unlocked_promise =
        Cast<JSPromise>(Utils::OpenHandle(*unlocked_promise_.Get(v8_isolate)));
    return unlocked_promise;
  }

  void Notify() override {
    SetNotInListForVerification();
    CancelableTaskManager* task_manager = requester_->cancelable_task_manager();
    if (task_manager->canceled()) return;
    auto notify_task =
        std::make_unique<AsyncWaiterNotifyTask<T>>(task_manager, this);
    notify_task_id_ = notify_task->id();
    task_runner_->PostNonNestableTask(std::move(notify_task));
  }

  bool IsSameIsolateForAsyncCleanup(Isolate* isolate) override {
    return requester_ == isolate;
  }

  void CleanupMatchingAsyncWaiters(const DequeueMatcher& matcher) override {
    T::CleanupMatchingAsyncWaiters(requester_, this, matcher);
  }

  // Removes the node from the isolate's `async_waiter_queue_nodes` list; the
  // passing node will be invalid after this call since the corresponding
  // unique_ptr is deleted upon removal.
  static void RemoveFromAsyncWaiterQueueList(AsyncWaiterQueueNode<T>* node) {
    node->requester_->async_waiter_queue_nodes().remove_if(
        [=](std::unique_ptr<WaiterQueueNode>& n) { return n.get() == node; });
  }

 private:
  friend JSAtomicsMutex;
  friend JSAtomicsCondition;
  friend AsyncWaiterNotifyTask<T>;
  friend AsyncWaiterTimeoutTask<T>;

  explicit AsyncWaiterQueueNode(Isolate* requester,
                                DirectHandle<T> synchronization_primitive)
      : WaiterQueueNode(requester),
        notify_task_id_(CancelableTaskManager::kInvalidTaskId) {
    native_context_ =
        GetWeakGlobal(requester, Utils::ToLocal(requester->native_context()));
    synchronization_primitive_ = GetWeakGlobal(
        requester, Utils::ToLocal(Cast<JSObject>(synchronization_primitive)));
  }

  explicit AsyncWaiterQueueNode(
      Isolate* requester, DirectHandle<T> synchronization_primitive,
      DirectHandle<JSPromise> internal_waiting_promise,
      MaybeDirectHandle<JSPromise> unlocked_promise)
      : WaiterQueueNode(requester),
        notify_task_id_(CancelableTaskManager::kInvalidTaskId) {
    v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(requester);
    task_runner_ =
        V8::GetCurrentPlatform()->GetForegroundTaskRunner(v8_isolate);
    timeout_task_id_ = CancelableTaskManager::kInvalidTaskId;
    native_context_ =
        GetWeakGlobal(requester, Utils::ToLocal(requester->native_context()));
    synchronization_primitive_ = GetWeakGlobal(
        requester, Utils::ToLocal(Cast<JSObject>(synchronization_primitive)));
    internal_waiting_promise_ = GetWeakGlobal(
        requester, Utils::PromiseToLocal(internal_waiting_promise));
    if (!unlocked_promise.is_null()) {
      DCHECK(IsJSAtomicsMutex(*synchronization_primitive));
      unlocked_promise_ = GetWeakGlobal(
          requester, Utils::PromiseToLocal(unlocked_promise.ToHandleChecked()));
    }
  }

  void SetReadyForAsyncCleanup() override { ready_for_async_cleanup_ = true; }

  std::shared_ptr<TaskRunner> task_runner_;
  CancelableTaskManager::Id timeout_task_id_;
  CancelableTaskManager::Id notify_task_id_;
  bool ready_for_async_cleanup_ = false;

  // The node holds weak global handles to the v8 required to handle the
  // notification.
  Global<v8::Context> native_context_;
  // `internal_waiting_promise_` is the internal first promise of the chain. See
  // comments in `JSAtomicsMutex` and `JSAtomicsCondition`.
  Global<v8::Promise> internal_waiting_promise_;
  Global<v8::Object> synchronization_primitive_;
  // `unlocked_promise` is the user exposed promise used to handle timeouts,
  // it should be empty for `JSAtomicsCondition`.
  Global<v8::Promise> unlocked_promise_;
};
}  // namespace detail

using detail::SyncWaiterQueueNode;
using LockAsyncWaiterQueueNode = detail::AsyncWaiterQueueNode<JSAtomicsMutex>;
using WaitAsyncWaiterQueueNode =
    detail::AsyncWaiterQueueNode<JSAtomicsCondition>;
using AsyncWaitTimeoutTask = detail::AsyncWaiterTimeoutTask<JSAtomicsCondition>;
using AsyncLockTimeoutTask = detail::AsyncWaiterTimeoutTask<JSAtomicsMutex>;

// static
void JSSynchronizationPrimitive::IsolateDeinit(Isolate* isolate) {
  CleanupAsyncWaiterLists(isolate, [=](WaiterQueueNode* waiter) {
    return waiter->IsSameIsolateForAsyncCleanup(isolate);
  });
}

void JSSynchronizationPrimitive::CleanupAsyncWaiterLists(
    Isolate* isolate, DequeueMatcher matcher) {
  DisallowGarbageCollection no_gc;
  std::list<std::unique_ptr<WaiterQueueNode>>& async_waiter_queue_nodes_list =
      isolate->async_waiter_queue_nodes();
  if (!async_waiter_queue_nodes_list.empty()) {
    // There is no allocation in the following code, so there shouldn't be any
    // GC, but we use a HandleScope to dereference the global handles.
    HandleScope handle_scope(isolate);
    auto it = async_waiter_queue_nodes_list.begin();
    while (it != async_waiter_queue_nodes_list.end()) {
      WaiterQueueNode* async_node = it->get();
      if (!matcher(async_node)) {
        it++;
        continue;
      }
      async_node->CleanupMatchingAsyncWaiters(matcher);
      it = async_waiter_queue_nodes_list.erase(it);
    }
  }
}

// static
bool JSSynchronizationPrimitive::TryLockWaiterQueueExplicit(
    std::atomic<StateT>* state, StateT& expected) {
  // Try to acquire the queue lock.
  expected = IsWaiterQueueLockedField::update(expected, false);
  return state->compare_exchange_weak(
      expected, IsWaiterQueueLockedField::update(expected, true),
      std::memory_order_acquire, std::memory_order_relaxed);
}

// static
void JSSynchronizationPrimitive::SetWaiterQueueStateOnly(
    std::atomic<StateT>* state, StateT new_state) {
  // Set the new state changing only the waiter queue bits.
  DCHECK_EQ(new_state & ~kWaiterQueueMask, 0);
  StateT expected = state->load(std::memory_order_relaxed);
  StateT desired;
  do {
    desired = new_state | (expected & ~kWaiterQueueMask);
  } while (!state->compare_exchange_weak(
      expected, desired, std::memory_order_release, std::memory_order_relaxed));
}

Tagged<Object> JSSynchronizationPrimitive::NumWaitersForTesting(
    Isolate* requester) {
  DisallowGarbageCollection no_gc;
  std::atomic<StateT>* state = AtomicStatePtr();
  StateT current_state = state->load(std::memory_order_acquire);

  // There are no waiters.
  if (!HasWaitersField::decode(current_state)) return Smi::FromInt(0);

  int num_waiters;
  {
    // If this is counting the number of waiters on a mutex, the js mutex
    // can be taken by another thread without acquiring the queue lock. We
    // handle the state manually to release the queue lock without changing the
    // "is locked" bit.
    while (!TryLockWaiterQueueExplicit(state, current_state)) {
      YIELD_PROCESSOR;
    }

    if (!HasWaitersField::decode(current_state)) {
      // The queue was emptied while waiting for the queue lock.
      SetWaiterQueueStateOnly(state, kEmptyState);
      return Smi::FromInt(0);
    }

    // Get the waiter queue head.
    WaiterQueueNode* waiter_head = DestructivelyGetWaiterQueueHead(requester);
    DCHECK_NOT_NULL(waiter_head);
    num_waiters = WaiterQueueNode::LengthFromHead(waiter_head);

    // Release the queue lock and reinstall the same queue head by creating a
    // new state.
    DCHECK_EQ(state->load(),
              IsWaiterQueueLockedField::update(current_state, true));
    StateT new_state = SetWaiterQueueHead(requester, waiter_head, kEmptyState);
    new_state = IsWaiterQueueLockedField::update(new_state, false);
    SetWaiterQueueStateOnly(state, new_state);
  }

  return Smi::FromInt(num_waiters);
}

// TODO(lpardosixtos): Consider making and caching a canonical map for this
// result object, like we do for the iterator result object.
// static
DirectHandle<JSObject> JSAtomicsMutex::CreateResultObject(
    Isolate* isolate, DirectHandle<Object> value, bool success) {
  DirectHandle<JSObject> result =
      isolate->factory()->NewJSObject(isolate->object_function());
  DirectHandle<Object> success_value = isolate->factory()->ToBoolean(success);
  JSObject::AddProperty(isolate, result, "value", value,
                        PropertyAttributes::NONE);
  JSObject::AddProperty(isolate, result, "success", success_value,
                        PropertyAttributes::NONE);
  return result;
}

// static
void JSAtomicsMutex::CleanupMatchingAsyncWaiters(Isolate* isolate,
                                                 WaiterQueueNode* node,
                                                 DequeueMatcher matcher) {
  auto* async_node = static_cast<LockAsyncWaiterQueueNode*>(node);
  if (async_node->ready_for_async_cleanup_) {
    // Whenever a node needs to be looked up in the waiter queue we also remove
    // any other matching nodes and mark them as ready for async cleanup. This
    // way we avoid taking the queue lock multiple times, which could slow down
    // other threads.
    return;
  }
  if (async_node->IsEmpty()) {
    // The node's underlying synchronization primitive has been collected, so
    // delete it.
    async_node->SetNotInListForVerification();
    return;
  }
  DirectHandle<JSAtomicsMutex> mutex =
      async_node->GetSynchronizationPrimitive();
  std::atomic<StateT>* state = mutex->AtomicStatePtr();
  StateT current_state = state->load(std::memory_order_relaxed);

  // The details of updating the state in this function are too complicated
  // for the waiter queue lock guard to manage, so handle the state manually.
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  bool was_locked_by_this_thread = mutex->IsCurrentThreadOwner();
  WaiterQueueNode* waiter_head =
      mutex->DestructivelyGetWaiterQueueHead(isolate);
  if (waiter_head) {
    // Dequeue all the matching waiters.
    WaiterQueueNode::DequeueAllMatchingForAsyncCleanup(&waiter_head, matcher);
    if (!async_node->ready_for_async_cleanup_) {
      // The node was not in the queue, so it has already being notified.
      // Notify the next head unless the lock is already taken by a different
      // thread or the queue may stall.
      if (waiter_head && (!IsLockedField::decode(current_state) ||
                          was_locked_by_this_thread)) {
        // Notify the next head unless the lock is already taken, in which case
        // the lock owner will notify the next waiter.
        WaiterQueueNode* old_head = WaiterQueueNode::Dequeue(&waiter_head);
        old_head->Notify();
      }
    }
  }
  StateT new_state = kUnlockedUncontended;
  new_state = mutex->SetWaiterQueueHead(isolate, waiter_head, new_state);
  new_state = IsWaiterQueueLockedField::update(new_state, false);
  if (was_locked_by_this_thread) {
    mutex->ClearOwnerThread();
    new_state = IsLockedField::update(new_state, false);
    state->store(new_state, std::memory_order_release);
  } else {
    SetWaiterQueueStateOnly(state, new_state);
  }
}

// static
bool JSAtomicsMutex::TryLockExplicit(std::atomic<StateT>* state,
                                     StateT& expected) {
  // Try to lock a possibly contended mutex.
  expected = IsLockedField::update(expected, false);
  return state->compare_exchange_weak(
      expected, IsLockedField::update(expected, true),
      std::memory_order_acquire, std::memory_order_relaxed);
}

bool JSAtomicsMutex::BackoffTryLock(Isolate* requester,
                                    DirectHandle<JSAtomicsMutex> mutex,
                                    std::atomic<StateT>* state) {
  // The backoff algorithm is copied from PartitionAlloc's SpinningMutex.
  constexpr int kSpinCount = 64;
  constexpr int kMaxBackoff = 16;

  int tries = 0;
  int backoff = 1;
  StateT current_state = state->load(std::memory_order_relaxed);
  do {
    if (JSAtomicsMutex::TryLockExplicit(state, current_state)) return true;

    for (int yields = 0; yields < backoff; yields++) {
      YIELD_PROCESSOR;
      tries++;
    }

    backoff = std::min(kMaxBackoff, backoff << 1);
  } while (tries < kSpinCount);
  return false;
}

bool JSAtomicsMutex::MaybeEnqueueNode(Isolate* requester,
                                      DirectHandle<JSAtomicsMutex> mutex,
                                      std::atomic<StateT>* state,
                                      WaiterQueueNode* this_waiter) {
  DCHECK_NOT_NULL(this_waiter);
  // Try to acquire the queue lock, which is itself a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  std::optional<WaiterQueueLockGuard> waiter_queue_lock_guard =
      LockWaiterQueueOrJSMutex(state, current_state);
  if (!waiter_queue_lock_guard.has_value()) {
    // There is no waiter queue lock guard, so the lock was acquired.
    DCHECK(IsLockedField::decode(state->load()));
    return false;
  }

  // With the queue lock held, enqueue the requester onto the waiter queue.
  WaiterQueueNode* waiter_head =
      mutex->DestructivelyGetWaiterQueueHead(requester);
  WaiterQueueNode::Enqueue(&waiter_head, this_waiter);

  // Enqueue a new waiter queue head and release the queue lock.
  DCHECK_EQ(state->load(),
            IsWaiterQueueLockedField::update(current_state, true));
  StateT new_state =
      mutex->SetWaiterQueueHead(requester, waiter_head, current_state);
  // The lock is held, just not by us, so don't set the current thread id as
  // the owner.
  DCHECK(IsLockedField::decode(current_state));
  new_state = IsLockedField::update(new_state, true);
  waiter_queue_lock_guard->set_new_state(new_state);
  return true;
}

// static
std::optional<WaiterQueueLockGuard> JSAtomicsMutex::LockWaiterQueueOrJSMutex(
    std::atomic<StateT>* state, StateT& current_state) {
  for (;;) {
    if (IsLockedField::decode(current_state) &&
        TryLockWaiterQueueExplicit(state, current_state)) {
      return WaiterQueueLockGuard::NewAlreadyLockedWaiterQueueLockGuard(state);
    }
    // Also check for the lock having been released by another thread during
    // attempts to acquire the queue lock.
    if (TryLockExplicit(state, current_state)) return std::nullopt;
    YIELD_PROCESSOR;
  }
}

bool JSAtomicsMutex::LockJSMutexOrDequeueTimedOutWaiter(
    Isolate* requester, std::atomic<StateT>* state,
    WaiterQueueNode* timed_out_waiter) {
  // First acquire the queue lock, which is itself a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  // There are no waiters, but the js mutex lock may be held by another thread.
  if (!HasWaitersField::decode(current_state)) return false;

  // The details of updating the state in this function are too complicated
  // for the waiter queue lock guard to manage, so handle the state manually.
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  WaiterQueueNode* waiter_head = DestructivelyGetWaiterQueueHead(requester);

  if (waiter_head == nullptr) {
    // The queue is empty but the js mutex lock may be held by another thread,
    // release the waiter queue bit without changing the "is locked" bit.
    DCHECK(!HasWaitersField::decode(current_state));
    SetWaiterQueueStateOnly(state, kUnlockedUncontended);
    return false;
  }

  WaiterQueueNode* dequeued_node = WaiterQueueNode::DequeueMatching(
      &waiter_head,
      [&](WaiterQueueNode* node) { return node == timed_out_waiter; });

  // Release the queue lock and install the new waiter queue head.
  DCHECK_EQ(state->load(),
            IsWaiterQueueLockedField::update(current_state, true));
  StateT new_state = kUnlockedUncontended;
  new_state = SetWaiterQueueHead(requester, waiter_head, new_state);

  if (!dequeued_node) {
    // The timed out waiter was not in the queue, so it must have been dequeued
    // and notified between the time this thread woke up and the time it
    // acquired the queue lock, so there is a risk that the next queue head is
    // never notified. Try to take the js mutex lock here, if we succeed, the
    // next node will be notified by this thread, otherwise, it will be notified
    // by the thread holding the lock now.

    // Since we use strong CAS below, we know that the js mutex lock will be
    // held by either this thread or another thread that can't go through the
    // unlock fast path because this thread is holding the waiter queue lock.
    // Hence, it is safe to always set the "is locked" bit in new_state.
    new_state = IsLockedField::update(new_state, true);
    DCHECK(!IsWaiterQueueLockedField::decode(new_state));
    current_state = IsLockedField::update(current_state, false);
    if (state->compare_exchange_strong(current_state, new_state,
                                       std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
      // The CAS atomically released the waiter queue lock and acquired the js
      // mutex lock.
      return true;
    }

    DCHECK(IsLockedField::decode(state->load()));
    state->store(new_state, std::memory_order_release);
    return false;
  }

  SetWaiterQueueStateOnly(state, new_state);
  return false;
}

// static
bool JSAtomicsMutex::LockSlowPath(Isolate* requester,
                                  DirectHandle<JSAtomicsMutex> mutex,
                                  std::atomic<StateT>* state,
                                  std::optional<base::TimeDelta> timeout) {
  for (;;) {
    // Spin for a little bit to try to acquire the lock, so as to be fast under
    // microcontention.
    if (BackoffTryLock(requester, mutex, state)) return true;

    // At this point the lock is considered contended, so try to go to sleep and
    // put the requester thread on the waiter queue.

    // Allocate a waiter queue node on-stack, since this thread is going to
    // sleep and will be blocked anyway.
    SyncWaiterQueueNode this_waiter(requester);
    if (!MaybeEnqueueNode(requester, mutex, state, &this_waiter)) return true;

    bool rv;
    // Wait for another thread to release the lock and wake us up.
    if (timeout) {
      rv = this_waiter.WaitFor(*timeout);
      // Reload the state pointer after wake up in case of shared GC while
      // blocked.
      state = mutex->AtomicStatePtr();
      if (!rv) {
        // If timed out, remove ourself from the waiter list, which is usually
        // done by the thread performing the notifying.
        rv = mutex->LockJSMutexOrDequeueTimedOutWaiter(requester, state,
                                                       &this_waiter);
        return rv;
      }
    } else {
      this_waiter.Wait();
      // Reload the state pointer after wake up in case of shared GC while
      // blocked.
      state = mutex->AtomicStatePtr();
    }

    // After wake up we try to acquire the lock again by spinning, as the
    // contention at the point of going to sleep should not be correlated with
    // contention at the point of waking up.
  }
}

void JSAtomicsMutex::UnlockSlowPath(Isolate* requester,
                                    std::atomic<StateT>* state) {
  // The fast path unconditionally cleared the owner thread.
  DCHECK_EQ(ThreadId::Invalid().ToInteger(),
            AtomicOwnerThreadIdPtr()->load(std::memory_order_relaxed));

  // To wake a sleeping thread, first acquire the queue lock, which is itself
  // a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  WaiterQueueLockGuard waiter_queue_lock_guard(state, current_state);

  if (!HasWaitersField::decode(current_state)) {
    // All waiters were removed while waiting for the queue lock, possibly by
    // timing out. Release both the lock and the queue lock.
    StateT new_state = IsLockedField::update(current_state, false);
    waiter_queue_lock_guard.set_new_state(new_state);
    return;
  }

  WaiterQueueNode* waiter_head = DestructivelyGetWaiterQueueHead(requester);
  DCHECK_NOT_NULL(waiter_head);
  WaiterQueueNode* old_head = WaiterQueueNode::Dequeue(&waiter_head);

  // Release both the lock and the queue lock, and install the new waiter queue
  // head.
  StateT new_state = IsLockedField::update(current_state, false);
  new_state = SetWaiterQueueHead(requester, waiter_head, new_state);
  waiter_queue_lock_guard.set_new_state(new_state);

  old_head->Notify();
}

// The lockAsync flow is controlled by a series of promises:
// 1. `internal_locked_promise`, a promise that settles when the mutex is
//    locked. When this promise is resolved, the callback is run. Not exposed to
//    user code.
// 2. `waiting_for_callback_promise`, a promise that settles when the callback
//    completes. When this promise settles, the mutex is unlocked
// 3. `unlocked_promise`, a promise that settles when the mutex is unlocked,
//    either explicitly or by timeout. Returned by lockAsync.
// static
MaybeDirectHandle<JSPromise> JSAtomicsMutex::LockOrEnqueuePromise(
    Isolate* requester, DirectHandle<JSAtomicsMutex> mutex,
    DirectHandle<Object> callback, std::optional<base::TimeDelta> timeout) {
  Handle<JSPromise> internal_locked_promise =
      requester->factory()->NewJSPromise();
  DirectHandle<JSReceiver> waiting_for_callback_promise;
  ASSIGN_RETURN_ON_EXCEPTION(
      requester, waiting_for_callback_promise,
      PerformPromiseThen(requester, internal_locked_promise, callback));
  Handle<JSPromise> unlocked_promise = requester->factory()->NewJSPromise();
  // Set the async unlock handlers here so we can throw without any additional
  // cleanup if the inner `promise_then` call fails. Keep a reference to
  // the handlers' synthetic context so we can store the waiter node in it once
  // the node is created.
  DirectHandle<Context> handlers_context;
  ASSIGN_RETURN_ON_EXCEPTION(
      requester, handlers_context,
      SetAsyncUnlockHandlers(requester, mutex, waiting_for_callback_promise,
                             unlocked_promise));
  LockAsyncWaiterQueueNode* waiter_node = nullptr;
  bool locked = LockAsync(requester, mutex, internal_locked_promise,
                          unlocked_promise, &waiter_node, timeout);
  if (locked) {
    // Create an LockAsyncWaiterQueueNode to be queued in the async locked
    // waiter queue.
    DCHECK(!waiter_node);
    waiter_node = LockAsyncWaiterQueueNode::NewLockedAsyncWaiterStoredInIsolate(
        requester, mutex);
  }
  // Don't use kWaiterQueueNodeTag here as that will cause the pointer to be
  // stored in the shared external pointer table, which is not necessary since
  // this object is only visible in this thread.
  DirectHandle<Foreign> wrapper =
      requester->factory()->NewForeign<kWaiterQueueForeignTag>(
          reinterpret_cast<Address>(waiter_node));
  handlers_context->set(JSAtomicsMutex::kAsyncLockedWaiterAsyncContextSlot,
                        *wrapper);
  return unlocked_promise;
}

// static
bool JSAtomicsMutex::LockAsync(Isolate* requester,
                               DirectHandle<JSAtomicsMutex> mutex,
                               Handle<JSPromise> internal_locked_promise,
                               MaybeHandle<JSPromise> unlocked_promise,
                               LockAsyncWaiterQueueNode** waiter_node,
                               std::optional<base::TimeDelta> timeout) {
  bool locked =
      LockImpl(requester, mutex, timeout, [=](std::atomic<StateT>* state) {
        return LockAsyncSlowPath(requester, mutex, state,
                                 internal_locked_promise, unlocked_promise,
                                 waiter_node, timeout);
      });
  if (locked) {
    // Resolve `internal_locked_promise` instead of synchronously running the
    // callback. This guarantees that the callback is run in a microtask
    // regardless of the current state of the mutex.
    MaybeDirectHandle<Object> result = JSPromise::Resolve(
        internal_locked_promise, requester->factory()->undefined_value());
    USE(result);
  } else {
    // If the promise is not resolved, keep it alive in a set in the native
    // context. The promise will be resolved and remove from the set in
    // `JSAtomicsMutex::HandleAsyncNotify` or
    // `JSAtomicsMutex::HandleAsyncTimeout`.
    AddPromiseToNativeContext(requester, internal_locked_promise);
  }
  return locked;
}

// static
DirectHandle<JSPromise> JSAtomicsMutex::LockAsyncWrapperForWait(
    Isolate* requester, DirectHandle<JSAtomicsMutex> mutex) {
  Handle<JSPromise> internal_locked_promise =
      requester->factory()->NewJSPromise();
  AsyncWaiterNodeType* waiter_node = nullptr;
  LockAsync(requester, mutex, internal_locked_promise, {}, &waiter_node);
  return internal_locked_promise;
}

// static
bool JSAtomicsMutex::LockAsyncSlowPath(
    Isolate* isolate, DirectHandle<JSAtomicsMutex> mutex,
    std::atomic<StateT>* state, Handle<JSPromise> internal_locked_promise,
    MaybeHandle<JSPromise> unlocked_promise,
    LockAsyncWaiterQueueNode** waiter_node,
    std::optional<base::TimeDelta> timeout) {
  // Spin for a little bit to try to acquire the lock, so as to be fast under
  // microcontention.
  if (BackoffTryLock(isolate, mutex, state)) {
    return true;
  }

  // At this point the lock is considered contended, create a new async waiter
  // node in the C++ heap. It's lifetime is managed by the requester's
  // `async_waiter_queue_nodes` list.
  LockAsyncWaiterQueueNode* this_waiter =
      LockAsyncWaiterQueueNode::NewAsyncWaiterStoredInIsolate(
          isolate, mutex, internal_locked_promise, unlocked_promise);
  if (!MaybeEnqueueNode(isolate, mutex, state, this_waiter)) {
    return true;
  }

  if (timeout) {
    // Start a timer to run the `AsyncLockTimeoutTask` after the timeout.
    TaskRunner* taks_runner = this_waiter->task_runner();
    auto task = std::make_unique<AsyncLockTimeoutTask>(
        isolate->cancelable_task_manager(), this_waiter);
    this_waiter->timeout_task_id_ = task->id();
    taks_runner->PostNonNestableDelayedTask(std::move(task),
                                            timeout->InSecondsF());
  }
  *waiter_node = this_waiter;
  return false;
}

// static
bool JSAtomicsMutex::LockOrEnqueueAsyncNode(Isolate* isolate,
                                            DirectHandle<JSAtomicsMutex> mutex,
                                            LockAsyncWaiterQueueNode* waiter) {
  std::atomic<StateT>* state = mutex->AtomicStatePtr();
  // Spin for a little bit to try to acquire the lock, so as to be fast under
  // microcontention.
  if (BackoffTryLock(isolate, mutex, state)) {
    return true;
  }

  return !MaybeEnqueueNode(isolate, mutex, state, waiter);
}

void JSAtomicsMutex::UnlockAsyncLockedMutex(
    Isolate* requester, DirectHandle<Foreign> async_locked_waiter_wrapper) {
  LockAsyncWaiterQueueNode* waiter_node =
      reinterpret_cast<LockAsyncWaiterQueueNode*>(
          async_locked_waiter_wrapper->foreign_address<kWaiterQueueForeignTag>(
              IsolateForSandbox(requester)));
  LockAsyncWaiterQueueNode::RemoveFromAsyncWaiterQueueList(waiter_node);
  if (IsCurrentThreadOwner()) {
    Unlock(requester);
    return;
  }
  // If this is reached, the lock was already released by this thread.
  // This can happen if waitAsync is called without awaiting or due to
  // promise prototype tampering. Setting Promise.prototype.then to a
  // non callable will cause the `waiting_for_callback_promise` (defined in
  // LockOrEnqueuePromise) reactions to be called even if the async callback
  // is not resolved; as a consequence, the following code will try to unlock
  // the mutex twice:
  //
  // let mutex = new Atomics.Mutex();
  // let cv = new Atomics.Condition();
  // Promise.prototype.then = undefined;
  // Atomics.Mutex.lockAsync(mutex, async function() {
  //   await Atomics.Condition.waitAsync(cv, mutex);
  // }
}

bool JSAtomicsMutex::DequeueTimedOutAsyncWaiter(
    Isolate* requester, DirectHandle<JSAtomicsMutex> mutex,
    std::atomic<StateT>* state, WaiterQueueNode* timed_out_waiter) {
  // First acquire the queue lock, which is itself a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  // There are no waiters, but the js mutex lock may be held by another thread.
  if (!HasWaitersField::decode(current_state)) return false;

  // The details of updating the state in this function are too complicated
  // for the waiter queue lock guard to manage, so handle the state manually.
  while (!TryLockWaiterQueueExplicit(state, current_state)) {
    YIELD_PROCESSOR;
  }

  // Get the waiter queue head.
  WaiterQueueNode* waiter_head =
      mutex->DestructivelyGetWaiterQueueHead(requester);

  if (waiter_head == nullptr) {
    // The queue is empty but the js mutex lock may be held by another thread,
    // release the waiter queue bit without changing the "is locked" bit.
    DCHECK(!HasWaitersField::decode(current_state));
    SetWaiterQueueStateOnly(state, kUnlockedUncontended);
    return false;
  }

  WaiterQueueNode* dequeued_node = WaiterQueueNode::DequeueMatching(
      &waiter_head,
      [&](WaiterQueueNode* node) { return node == timed_out_waiter; });

  // Release the queue lock and install the new waiter queue head.
  DCHECK_EQ(state->load(),
            IsWaiterQueueLockedField::update(current_state, true));
  StateT new_state = kUnlockedUncontended;
  new_state = mutex->SetWaiterQueueHead(requester, waiter_head, new_state);

  SetWaiterQueueStateOnly(state, new_state);
  return dequeued_node != nullptr;
}

// static
void JSAtomicsMutex::HandleAsyncTimeout(LockAsyncWaiterQueueNode* waiter) {
  Isolate* requester = waiter->requester_;
  HandleScope scope(requester);

  if (V8_UNLIKELY(waiter->native_context_.IsEmpty())) {
    // The native context was destroyed so the lock_promise was already removed
    // from the native context. Remove the node from the async unlocked waiter
    // list.
    LockAsyncWaiterQueueNode::RemoveFromAsyncWaiterQueueList(waiter);
    return;
  }

  v8::Context::Scope contextScope(waiter->GetNativeContext());
  DirectHandle<JSAtomicsMutex> js_mutex = waiter->GetSynchronizationPrimitive();

  bool dequeued = JSAtomicsMutex::DequeueTimedOutAsyncWaiter(
      requester, js_mutex, js_mutex->AtomicStatePtr(), waiter);
  // If the waiter is no longer in the queue, then its corresponding notify
  // task is already in the event loop, this doesn't guarantee that the lock
  // will be taken by the time the notify task runs, so cancel the notify task.
  if (!dequeued) {
    TryAbortResult abort_result =
        requester->cancelable_task_manager()->TryAbort(waiter->notify_task_id_);
    DCHECK_EQ(abort_result, TryAbortResult::kTaskAborted);
    USE(abort_result);
  }

  DirectHandle<JSPromise> lock_promise = waiter->GetInternalWaitingPromise();
  DirectHandle<JSPromise> lock_async_promise = waiter->GetUnlockedPromise();
  DirectHandle<JSObject> result = CreateResultObject(
      requester, requester->factory()->undefined_value(), false);
  auto resolve_result = JSPromise::Resolve(lock_async_promise, result);
  USE(resolve_result);
  LockAsyncWaiterQueueNode::RemoveFromAsyncWaiterQueueList(waiter);
  RemovePromiseFromNativeContext(requester, lock_promise);
}

// static
void JSAtomicsMutex::HandleAsyncNotify(LockAsyncWaiterQueueNode* waiter) {
  Isolate* requester = waiter->requester_;
  HandleScope scope(requester);

  if (V8_UNLIKELY(waiter->native_context_.IsEmpty())) {
    // The native context was destroyed, so the promise was already removed. But
    // it is possible that other threads are holding references to the
    // synchronization primitive. Try to notify the next waiter.
    if (!waiter->synchronization_primitive_.IsEmpty()) {
      DirectHandle<JSAtomicsMutex> js_mutex =
          waiter->GetSynchronizationPrimitive();
      std::atomic<StateT>* state = js_mutex->AtomicStatePtr();
      StateT current_state = state->load(std::memory_order_acquire);
      if (HasWaitersField::decode(current_state)) {
        // Another thread might take the lock while we are notifying the next
        // waiter, so manually release the queue lock without changing the
        // IsLockedField bit.
        while (!TryLockWaiterQueueExplicit(state, current_state)) {
          YIELD_PROCESSOR;
        }
        WaiterQueueNode* waiter_head =
            js_mutex->DestructivelyGetWaiterQueueHead(requester);
        if (waiter_head) {
          WaiterQueueNode* old_head = WaiterQueueNode::Dequeue(&waiter_head);
          old_head->Notify();
        }
        StateT new_state =
            js_mutex->SetWaiterQueueHead(requester, waiter_head, kEmptyState);
        new_state = IsWaiterQueueLockedField::update(new_state, false);
        SetWaiterQueueStateOnly(state, new_state);
      }
    }
    LockAsyncWaiterQueueNode::RemoveFromAsyncWaiterQueueList(waiter);
    return;
  }

  v8::Context::Scope contextScope(waiter->GetNativeContext());
  DirectHandle<JSAtomicsMutex> js_mutex = waiter->GetSynchronizationPrimitive();
  DirectHandle<JSPromise> promise = waiter->GetInternalWaitingPromise();
  bool locked = LockOrEnqueueAsyncNode(requester, js_mutex, waiter);
  if (locked) {
    if (waiter->timeout_task_id_ != CancelableTaskManager::kInvalidTaskId) {
      TryAbortResult abort_result =
          requester->cancelable_task_manager()->TryAbort(
              waiter->timeout_task_id_);
      DCHECK_EQ(abort_result, TryAbortResult::kTaskAborted);
      USE(abort_result);
    }
    if (waiter->unlocked_promise_.IsEmpty()) {
      // This node came from an async wait notify giving control back to an
      // async lock call, so we don't need to put the node in the locked waiter
      // list because the original LockAsycWaiterQueueNode is already in
      // the locked waiter list.
      LockAsyncWaiterQueueNode::RemoveFromAsyncWaiterQueueList(waiter);
    }
    js_mutex->SetCurrentThreadAsOwner();
    auto resolve_result =
        JSPromise::Resolve(promise, requester->factory()->undefined_value());
    USE(resolve_result);
    RemovePromiseFromNativeContext(requester, promise);
  }
}

// static
void JSAtomicsCondition::CleanupMatchingAsyncWaiters(Isolate* isolate,
                                                     WaiterQueueNode* node,
                                                     DequeueMatcher matcher) {
  auto* async_node = static_cast<WaitAsyncWaiterQueueNode*>(node);
  if (async_node->ready_for_async_cleanup_) {
    // The node is not in the waiter queue and there is no HandleNotify task
    // for it in the event loop. So it is safe to delete it.
    return;
  }
  if (async_node->IsEmpty()) {
    // The node's underlying synchronization primitive has been collected, so
    // delete it.
    async_node->SetNotInListForVerification();
    return;
  }
  DirectHandle<JSAtomicsCondition> cv =
      async_node->GetSynchronizationPrimitive();
  std::atomic<StateT>* state = cv->AtomicStatePtr();
  StateT current_state = state->load(std::memory_order_relaxed);

  WaiterQueueLockGuard waiter_queue_lock_guard(state, current_state);

  WaiterQueueNode* waiter_head = cv->DestructivelyGetWaiterQueueHead(isolate);
  if (waiter_head) {
    WaiterQueueNode::DequeueAllMatchingForAsyncCleanup(&waiter_head, matcher);
  }
  StateT new_state =
      cv->SetWaiterQueueHead(isolate, waiter_head, current_state);
  waiter_queue_lock_guard.set_new_state(new_state);
}

// static
void JSAtomicsCondition::QueueWaiter(Isolate* requester,
                                     DirectHandle<JSAtomicsCondition> cv,
                                     WaiterQueueNode* waiter) {
  // The state pointer should not be used outside of this block as a shared GC
  // may reallocate it after waiting.
  std::atomic<StateT>* state = cv->AtomicStatePtr();

  // Try to acquire the queue lock, which is itself a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);
  WaiterQueueLockGuard waiter_queue_lock_guard(state, current_state);

  // With the queue lock held, enqueue the requester onto the waiter queue.
  WaiterQueueNode* waiter_head = cv->DestructivelyGetWaiterQueueHead(requester);
  WaiterQueueNode::Enqueue(&waiter_head, waiter);

  // Release the queue lock and install the new waiter queue head.
  DCHECK_EQ(state->load(),
            IsWaiterQueueLockedField::update(current_state, true));
  StateT new_state =
      cv->SetWaiterQueueHead(requester, waiter_head, current_state);
  waiter_queue_lock_guard.set_new_state(new_state);
}

// static
bool JSAtomicsCondition::WaitFor(Isolate* requester,
                                 DirectHandle<JSAtomicsCondition> cv,
                                 DirectHandle<JSAtomicsMutex> mutex,
                                 std::optional<base::TimeDelta> timeout) {
  DisallowGarbageCollection no_gc;

  bool rv;
  {
    // Allocate a waiter queue node on-stack, since this thread is going to
    // sleep and will be blocked anyway.
    SyncWaiterQueueNode this_waiter(requester);

    JSAtomicsCondition::QueueWaiter(requester, cv, &this_waiter);

    // Release the mutex and wait for another thread to wake us up, reacquiring
    // the mutex upon wakeup.
    mutex->Unlock(requester);
    if (timeout) {
      rv = this_waiter.WaitFor(*timeout);
      if (!rv) {
        // If timed out, remove ourself from the waiter list, which is usually
        // done by the thread performing the notifying.
        std::atomic<StateT>* state = cv->AtomicStatePtr();
        DequeueExplicit(
            requester, cv, state, [&](WaiterQueueNode** waiter_head) {
              WaiterQueueNode* dequeued = WaiterQueueNode::DequeueMatching(
                  waiter_head,
                  [&](WaiterQueueNode* node) { return node == &this_waiter; });
              return dequeued ? 1 : 0;
            });
      }
    } else {
      this_waiter.Wait();
      rv = true;
    }
  }
  JSAtomicsMutex::Lock(requester, mutex);
  return rv;
}

// static
uint32_t JSAtomicsCondition::DequeueExplicit(
    Isolate* requester, DirectHandle<JSAtomicsCondition> cv,
    std::atomic<StateT>* state, const DequeueAction& action_under_lock) {
  // First acquire the queue lock, which is itself a spinlock.
  StateT current_state = state->load(std::memory_order_relaxed);

  if (!HasWaitersField::decode(current_state)) return 0;
  WaiterQueueLockGuard waiter_queue_lock_guard(state, current_state);

  // Get the waiter queue head.
  WaiterQueueNode* waiter_head = cv->DestructivelyGetWaiterQueueHead(requester);

  // There's no waiter to wake up, release the queue lock by setting it to the
  // empty state.
  if (waiter_head == nullptr) {
    StateT new_state = kEmptyState;
    waiter_queue_lock_guard.set_new_state(new_state);
    return 0;
  }

  uint32_t num_dequeued_waiters = action_under_lock(&waiter_head);

  // Release the queue lock and install the new waiter queue head.
  DCHECK_EQ(state->load(),
            IsWaiterQueueLockedField::update(current_state, true));
  StateT new_state =
      cv->SetWaiterQueueHead(requester, waiter_head, current_state);
  waiter_queue_lock_guard.set_new_state(new_state);

  return num_dequeued_waiters;
}

// static
uint32_t JSAtomicsCondition::Notify(Isolate* requester,
                                    DirectHandle<JSAtomicsCondition> cv,
                                    uint32_t count) {
  std::atomic<StateT>* state = cv->AtomicStatePtr();

  // Dequeue count waiters.
  return DequeueExplicit(
      requester, cv, state, [=](WaiterQueueNode** waiter_head) -> uint32_t {
        WaiterQueueNode* old_head;
        if (count == 1) {
          old_head = WaiterQueueNode::Dequeue(waiter_head);
          if (!old_head) return 0;
          old_head->Notify();
          return 1;
        }
        if (count == kAllWaiters) {
          old_head = *waiter_head;
          *waiter_head = nullptr;
        } else {
          old_head = WaiterQueueNode::Split(waiter_head, count);
        }
        if (!old_head) return 0;
        // Notify while holding the queue lock to avoid notifying
        // waiters that have been deleted in other threads.
        return old_head->NotifyAllInList();
      });
}

// The lockAsync flow is controlled 2 chained promises, with lock_promise being
// the return value of the API.
// 1. `internal_waiting_promise`, which will be resolved either in the notify
// task or in the
//    timeout task.
// 2. `lock_promise`, which will be resolved when the lock is acquired after
//    waiting.
// static
MaybeDirectHandle<JSReceiver> JSAtomicsCondition::WaitAsync(
    Isolate* requester, DirectHandle<JSAtomicsCondition> cv,
    DirectHandle<JSAtomicsMutex> mutex,
    std::optional<base::TimeDelta> timeout) {
  Handle<JSPromise> internal_waiting_promise =
      requester->factory()->NewJSPromise();
  DirectHandle<Context> handler_context =
      requester->factory()->NewBuiltinContext(requester->native_context(),
                                              kAsyncContextLength);
  handler_context->set(kMutexAsyncContextSlot, *mutex);
  handler_context->set(kConditionVariableAsyncContextSlot, *cv);

  DirectHandle<SharedFunctionInfo> info(
      requester->heap()->atomics_condition_acquire_lock_sfi(), requester);
  DirectHandle<JSFunction> lock_function =
      Factory::JSFunctionBuilder{requester, info, handler_context}
          .set_map(requester->strict_function_without_prototype_map())
          .Build();

  DirectHandle<JSReceiver> lock_promise;

  ASSIGN_RETURN_ON_EXCEPTION(
      requester, lock_promise,
      PerformPromiseThen(requester, internal_waiting_promise, lock_function));

  // Create a new async waiter node in the C++ heap. Its lifetime is managed by
  // the requester's `async_waiter_queue_nodes` list.
  WaitAsyncWaiterQueueNode* this_waiter =
      WaitAsyncWaiterQueueNode::NewAsyncWaiterStoredInIsolate(
          requester, cv, internal_waiting_promise);
  QueueWaiter(requester, cv, this_waiter);

  if (timeout) {
    TaskRunner* taks_runner = this_waiter->task_runner();
    auto task = std::make_unique<AsyncWaitTimeoutTask>(
        requester->cancelable_task_manager(), this_waiter);
    this_waiter->timeout_task_id_ = task->id();
    taks_runner->PostNonNestableDelayedTask(std::move(task),
                                            timeout->InSecondsF());
  }
  mutex->Unlock(requester);
  // Keep the wait promise alive in the native context.
  AddPromiseToNativeContext(requester, internal_waiting_promise);
  return lock_promise;
}

// static
void JSAtomicsCondition::HandleAsyncTimeout(WaitAsyncWaiterQueueNode* waiter) {
  Isolate* requester = waiter->requester_;
  if (V8_UNLIKELY(waiter->native_context_.IsEmpty())) {
    // The native context was destroyed so the promise was already removed
    // from the native context. Remove the node from the async unlocked waiter
    // list.
    WaitAsyncWaiterQueueNode::RemoveFromAsyncWaiterQueueList(waiter);
    return;
  }
  HandleScope scope(requester);
  DirectHandle<JSAtomicsCondition> cv = waiter->GetSynchronizationPrimitive();
  std::atomic<StateT>* state = cv->AtomicStatePtr();
  uint32_t num_dequeued =
      DequeueExplicit(requester, cv, state, [&](WaiterQueueNode** waiter_head) {
        WaiterQueueNode* dequeued = WaiterQueueNode::DequeueMatching(
            waiter_head, [&](WaiterQueueNode* node) { return node == waiter; });
        return dequeued ? 1 : 0;
      });
  // If the waiter is not in the queue, the notify task is already in the event
  // loop, so cancel the notify task.
  if (num_dequeued == 0) {
    TryAbortResult abort_result =
        requester->cancelable_task_manager()->TryAbort(waiter->notify_task_id_);
    DCHECK_EQ(abort_result, TryAbortResult::kTaskAborted);
    USE(abort_result);
  }
  // Reset the timeout task id to kInvalidTaskId, otherwise the notify task will
  // try to cancel it.
  waiter->timeout_task_id_ = CancelableTaskManager::kInvalidTaskId;
  JSAtomicsCondition::HandleAsyncNotify(waiter);
}

// static
void JSAtomicsCondition::HandleAsyncNotify(WaitAsyncWaiterQueueNode* waiter) {
  Isolate* requester = waiter->requester_;
  if (V8_UNLIKELY(waiter->native_context_.IsEmpty())) {
    // The native context was destroyed so the promise was already removed
    // from the native context. Remove the node from the async unlocked waiter
    // list.
    WaitAsyncWaiterQueueNode::RemoveFromAsyncWaiterQueueList(waiter);
    return;
  }
  HandleScope scope(requester);
  if (waiter->timeout_task_id_ != CancelableTaskManager::kInvalidTaskId) {
    TryAbortResult abort_result =
        requester->cancelable_task_manager()->TryAbort(
            waiter->timeout_task_id_);
    DCHECK_EQ(abort_result, TryAbortResult::kTaskAborted);
    USE(abort_result);
  }
  v8::Context::Scope contextScope(waiter->GetNativeContext());
  DirectHandle<JSPromise> promise = waiter->GetInternalWaitingPromise();
  MaybeDirectHandle<Object> result =
      JSPromise::Resolve(promise, requester->factory()->undefined_value());
  USE(result);
  WaitAsyncWaiterQueueNode::RemoveFromAsyncWaiterQueueList(waiter);
  RemovePromiseFromNativeContext(requester, promise);
}

}  // namespace internal
}  // namespace v8
