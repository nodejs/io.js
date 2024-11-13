// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_ATOMICS_SYNCHRONIZATION_H_
#define V8_OBJECTS_JS_ATOMICS_SYNCHRONIZATION_H_

#include <atomic>

#include "src/base/platform/time.h"
#include "src/execution/thread-id.h"
#include "src/objects/contexts.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-struct.h"
#include "src/objects/struct.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-atomics-synchronization-tq.inc"

namespace detail {
class WaiterQueueLockGuard;
class WaiterQueueNode;
template <typename T>
class AsyncWaiterQueueNode;
}  // namespace detail

using detail::WaiterQueueLockGuard;
using detail::WaiterQueueNode;
using LockAsyncWaiterQueueNode = detail::AsyncWaiterQueueNode<JSAtomicsMutex>;
using WaitAsyncWaiterQueueNode =
    detail::AsyncWaiterQueueNode<JSAtomicsCondition>;

// JSSynchronizationPrimitive is the base class for JSAtomicsMutex and
// JSAtomicsCondition. It contains a 32-bit state field and a pointer to a
// waiter queue head, used to manage the queue of waiting threads for both: the
// mutex and the condition variable.

class JSSynchronizationPrimitive
    : public TorqueGeneratedJSSynchronizationPrimitive<
          JSSynchronizationPrimitive, AlwaysSharedSpaceJSObject> {
 public:
  // Synchronization only store raw data as state.
  static constexpr int kEndOfTaggedFieldsOffset = JSObject::kHeaderSize;
  class BodyDescriptor;

  static void IsolateDeinit(Isolate* isolate);
  Tagged<Object> NumWaitersForTesting(Isolate* requester);

  TQ_OBJECT_CONSTRUCTORS(JSSynchronizationPrimitive)
  inline void SetNullWaiterQueueHead();

 protected:
  using StateT = uint32_t;

  // The `HasWaitersField` bitfield has the following properties:
  // - It isn't a lock bit, meaning that if this bit is 1,
  //   that doesn't imply that some thread has exclusive write access to the
  //   lock state.
  // - It is a metadata bit that's only written with the queue lock bit held.
  // - It is set iff the external pointer is non-null.
  // - It can be read without holding any lock bit.
  // - It allows for fast and threadsafe checking if there is a waiter,
  //   as dereferencing the waiter queue should be done only when the
  //   `IsWaiterQueueLockedField` bit is set.
  using HasWaitersField = base::BitField<bool, 0, 1>;

  // The `IsWaiterQueueLockedField` bitfield protects the waiter queue head from
  // concurrent modification. It is set through as CAS operation in a spinlock.
  using IsWaiterQueueLockedField = HasWaitersField::Next<bool, 1>;

  template <class T, int size>
  using NextBitField = IsWaiterQueueLockedField::Next<T, size>;

  inline std::atomic<StateT>* AtomicStatePtr();
  inline WaiterQueueNode* DestructivelyGetWaiterQueueHead(Isolate* requester);

  // Store the waiter queue head in the synchronization primitive. If the head
  // is not null, the returned state has the kHasWaitersBit set.
  // In case of pointer compression, the waiter queue head is encoded as an
  // `ExternalPointerHandle`.
  inline StateT SetWaiterQueueHead(Isolate* requester,
                                   WaiterQueueNode* waiter_head,
                                   StateT new_state);

  // Set the new state without modifying bits outside the waiter queue mask.
  static void SetWaiterQueueStateOnly(std::atomic<StateT>* state,
                                      StateT new_state);

  static bool TryLockWaiterQueueExplicit(std::atomic<StateT>* state,
                                         StateT& expected);

  using TorqueGeneratedJSSynchronizationPrimitive<
      JSSynchronizationPrimitive, AlwaysSharedSpaceJSObject>::state;
  using TorqueGeneratedJSSynchronizationPrimitive<
      JSSynchronizationPrimitive, AlwaysSharedSpaceJSObject>::set_state;
  using DequeueMatcher = std::function<bool(WaiterQueueNode*)>;

  static constexpr StateT kEmptyState = 0;
  static constexpr StateT kWaiterQueueMask =
      base::BitFieldUnion<HasWaitersField, IsWaiterQueueLockedField>::kMask;

 private:
  friend class WaiterQueueLockGuard;

#if V8_COMPRESS_POINTERS
  // When pointer compression is enabled, the pointer to the waiter queue head
  // is stored in the external pointer table and the object itself only contains
  // a 32-bit external pointer handles.
  inline ExternalPointerHandle* waiter_queue_head_handle_location() const;
#else
  inline WaiterQueueNode** waiter_queue_head_location() const;
#endif
  // Remove the matching async waiter queue nodes from the locked and unlocked
  // async waiter lists in the isolate.
  static void CleanupAsyncWaiterLists(Isolate* isolate, DequeueMatcher matcher);
};

// A non-recursive mutex that is exposed to JS.
//
// It has the following properties:
//   - Slim: 12-16 bytes. Lock state is 4 bytes, waiter queue head is 4 bytes
//     when V8_COMPRESS_POINTERS, and sizeof(void*) otherwise. Owner thread is
//     an additional 4 bytes.
//   - Fast when uncontended: a single weak CAS.
//   - Possibly unfair under contention.
//   - Moving GC safe. It uses an index into the shared Isolate's external
//     pointer table to store a queue of sleeping threads.
//   - Parks the main thread LocalHeap when the thread is blocked on acquiring
//     the lock. Unparks the main thread LocalHeap when unblocked. This means
//     that the lock can only be used with main thread isolates (including
//     workers) but not with helper threads that have their own LocalHeap.
//
// This mutex manages its own queue of waiting threads under contention, i.e.
// it implements a futex in userland. The algorithm is inspired by WebKit's
// ParkingLot.
//
// The state variable encodes the locking state as a single word: 0bLQW.
// - W: Whether there are waiter threads in the queue.
// - Q: Whether the waiter queue is locked.
// - L: Whether the lock itself is locked.

// The locking algorithm is as follows:
//  1. Fast Path. Unlocked+Uncontended(0b000) -> Locked+Uncontended(0b100).
//  2. Otherwise, slow path.
//    a. Attempt to acquire the L bit (set current state | 0b100) on the state
//       using a CAS spin loop bounded to some number of iterations.
//    b. If L bit cannot be acquired, park the current thread:
//     i.   Acquire the Q bit (set current state | 0b010) in a spinlock.
//     ii.  Destructively get the waiter queue head.
//     iii. Enqueue this thread's WaiterQueueNode to the tail of the list
//          pointed to by the head, possibly creating a new list.
//     iv.  Release the Q bit and set the W bit
//          (set (current state | 0b001) & ~0b010 in a single CAS operation).
//     iv.  Put the thread to sleep.
//     v.   Upon wake up, go to i.

// The unlocking algorithm is as follows:
//  1. Fast Path. Locked+Uncontended(0b100) -> Unlocked+Uncontended(0b000).
//  2. Otherwise, slow path.
//    a. Acquire the Q bit (set current state | 0b010) in a spinlock.
//    b. Destructively get the waiter queue head.
//    c. If the head is not null, dequeue the head.
//    d. Store the new waiter queue head (possibly null).
//    f. If the list is empty, clear the W bit (set current state & ~0b001).
//    g. Release the Q bit and clear the L bit (set current state & ~0b100).
//       (The W and Q bits must be set in a single CAS operation).
//    h. If the list was not empty, notify the dequeued head.
class JSAtomicsMutex
    : public TorqueGeneratedJSAtomicsMutex<JSAtomicsMutex,
                                           JSSynchronizationPrimitive> {
 public:
  using AsyncWaiterNodeType = LockAsyncWaiterQueueNode;
  // A non-copyable wrapper class that provides an RAII-style mechanism for
  // owning the `JSAtomicsMutex`.
  class V8_NODISCARD LockGuardBase {
   public:
    LockGuardBase(const LockGuardBase&) = delete;
    LockGuardBase& operator=(const LockGuardBase&) = delete;
    inline ~LockGuardBase();
    bool locked() const { return locked_; }

   protected:
    inline LockGuardBase(Isolate* isolate, Handle<JSAtomicsMutex> mutex,
                         bool locked);

   private:
    Isolate* isolate_;
    Handle<JSAtomicsMutex> mutex_;
    bool locked_;
  };

  // The mutex is attempted to be locked via `Lock` when a `LockGuard`
  // object is created, the lock will be acquired unless the timeout is reached.
  // If the mutex was acquired, then it is released when the `LockGuard` object
  // is destructed.
  class V8_NODISCARD LockGuard final : public LockGuardBase {
   public:
    inline LockGuard(Isolate* isolate, Handle<JSAtomicsMutex> mutex,
                     std::optional<base::TimeDelta> timeout = std::nullopt);
  };

  // The mutex is attempted to be locked via `TryLock` when a `TryLockGuard`
  // object is created. If the mutex was acquired, then it is released when the
  // `TryLockGuard` object is destructed.
  class V8_NODISCARD TryLockGuard final : public LockGuardBase {
   public:
    inline TryLockGuard(Isolate* isolate, Handle<JSAtomicsMutex> mutex);
  };

  DECL_PRINTER(JSAtomicsMutex)
  EXPORT_DECL_VERIFIER(JSAtomicsMutex)

  static Handle<JSObject> CreateResultObject(Isolate* isolate,
                                             DirectHandle<Object> value,
                                             bool success);

  // Lock the mutex, blocking if it's currently owned by another thread.
  // Returns false if the lock times out, true otherwise.
  static inline bool Lock(
      Isolate* requester, Handle<JSAtomicsMutex> mutex,
      std::optional<base::TimeDelta> timeout = std::nullopt);

  V8_WARN_UNUSED_RESULT inline bool TryLock();

  // Try to lock the mutex, if it's currently owned by another thread, creates
  // a LockAsyncWaiterQueueNode and enqueue it in the mutex's waiter queue.
  // The `internal_locked_promise` is resolved when the node is notified.
  // Returns true if the lock was acquired, false otherwise.
  static bool LockAsync(Isolate* requester, Handle<JSAtomicsMutex> mutex,
                        Handle<JSPromise> internal_locked_promise,
                        MaybeHandle<JSPromise> unlocked_promise,
                        AsyncWaiterNodeType** waiter_node,
                        std::optional<base::TimeDelta> timeout = std::nullopt);

  // A wrapper for LockAsync called when an asyncWait call returns control
  // to the lockAsync callback. It calls `LockAsync` without setting all the
  // logic to run the callback, since the callback is already running.
  static Handle<JSPromise> LockAsyncWrapperForWait(
      Isolate* requester, Handle<JSAtomicsMutex> mutex);

  // Try to take the lock and set up the promise logic to asynchronously run
  // the callback under the lock. Always returns a promise that settles when the
  // promise is unlocked or times out.
  static MaybeHandle<JSPromise> LockOrEnqueuePromise(
      Isolate* isolate, Handle<JSAtomicsMutex> mutex, Handle<Object> callback,
      std::optional<base::TimeDelta> timeout);

  // Try to take the lock or requeue an existing node.
  static bool LockOrEnqueueAsyncNode(Isolate* isolate,
                                     DirectHandle<JSAtomicsMutex> mutex,
                                     LockAsyncWaiterQueueNode* node);
  static void HandleAsyncNotify(LockAsyncWaiterQueueNode* node);
  static void HandleAsyncTimeout(LockAsyncWaiterQueueNode* node);

  inline void Unlock(Isolate* requester);

  inline bool IsHeld();
  inline bool IsCurrentThreadOwner();

  void UnlockAsyncLockedMutex(
      Isolate* requester, DirectHandle<Foreign> async_locked_waiter_wrapper);

  static void CleanupMatchingAsyncWaiters(Isolate* isolate,
                                          WaiterQueueNode* node,
                                          DequeueMatcher matcher);

  // The context slots for the artificial context created for the resolve and
  // reject handlers in charge of unlocking the mutex after the callback passed
  // to Atomics.Mutex.lockAsync is executed.
  enum {
    // The context slot for the js mutex that is locked asynchronously.
    kMutexAsyncContextSlot = Context::MIN_CONTEXT_SLOTS,
    // The context slot for the js exposed promise returned by the call to
    // Atomics.Mutex.lockAsync, it should be resolved or rejected after the
    // mutex is released.
    kUnlockedPromiseAsyncContextSlot,
    // The isolate keeps track of WaiterQueueNodes for each mutex locked
    // asynchronously, this is so that the lock can be released in case worker
    // termination. The kAsyncLockedWaiterAsyncContextSlot slot is used to store
    // a Foreign wrapping aroung and ExternalPointerHandle (or raw
    // pointer when pointer compression is disabled) pointing to the
    // WaiterQueueNode so that it can be removed from the list when the lock is
    // released through the usual path.
    kAsyncLockedWaiterAsyncContextSlot,
    kAsyncContextLength
  };

  TQ_OBJECT_CONSTRUCTORS(JSAtomicsMutex)

 private:
  friend class Factory;

  // There are 3 state bits: whether there are waiter threads in the queue,
  // whether the waiter queue is locked (both inherited from the base class),
  // and whether the lock itself is locked (IsLockedField).
  using IsLockedField = JSSynchronizationPrimitive::NextBitField<bool, 1>;

  static constexpr StateT kUnlockedUncontended = kEmptyState;
  static constexpr StateT kLockedUncontended = IsLockedField::encode(true);

  inline void SetCurrentThreadAsOwner();
  inline void ClearOwnerThread();

  inline std::atomic<int32_t>* AtomicOwnerThreadIdPtr();

  V8_EXPORT_PRIVATE static bool LockSlowPath(
      Isolate* requester, DirectHandle<JSAtomicsMutex> mutex,
      std::atomic<StateT>* state, std::optional<base::TimeDelta> timeout);
  static bool LockAsyncSlowPath(Isolate* isolate, Handle<JSAtomicsMutex> mutex,
                                std::atomic<StateT>* state,
                                Handle<JSPromise> internal_locked_promise,
                                MaybeHandle<JSPromise> unlocked_promise,
                                AsyncWaiterNodeType** waiter_node,
                                std::optional<base::TimeDelta> timeout);

  V8_EXPORT_PRIVATE void UnlockSlowPath(Isolate* requester,
                                        std::atomic<StateT>* state);

  // Returns true if the JS mutex was taken and false otherwise.
  bool LockJSMutexOrDequeueTimedOutWaiter(Isolate* requester,
                                          std::atomic<StateT>* state,
                                          WaiterQueueNode* timed_out_waiter);

  static bool TryLockExplicit(std::atomic<StateT>* state, StateT& expected);
  // Returns nullopt if the JS mutex is acquired, otherwise return an optional
  // with a `WaiterQueueLockGuard` object.
  static std::optional<WaiterQueueLockGuard> LockWaiterQueueOrJSMutex(
      std::atomic<StateT>* state, StateT& current_state);
  V8_EXPORT_PRIVATE static bool SpinningMutexTryLock(
      Isolate* requester, Handle<JSAtomicsMutex> mutex,
      std::atomic<StateT>* state);
  V8_INLINE static bool BackoffTryLock(Isolate* requester,
                                       DirectHandle<JSAtomicsMutex> mutex,
                                       std::atomic<StateT>* state);
  static bool DequeueTimedOutAsyncWaiter(Isolate* requester,
                                         DirectHandle<JSAtomicsMutex> mutex,
                                         std::atomic<StateT>* state,
                                         WaiterQueueNode* timed_out_waiter);

  V8_EXPORT_PRIVATE static bool MaybeEnqueueNode(
      Isolate* requester, DirectHandle<JSAtomicsMutex> mutex,
      std::atomic<StateT>* state, WaiterQueueNode* this_waiter);

  using LockSlowPathWrapper = std::function<bool(std::atomic<StateT>* state)>;

  static inline bool LockImpl(Isolate* requester,
                              DirectHandle<JSAtomicsMutex> mutex,
                              std::optional<base::TimeDelta> timeout,
                              LockSlowPathWrapper slow_path_wrapper);

  using TorqueGeneratedJSAtomicsMutex<
      JSAtomicsMutex, JSSynchronizationPrimitive>::owner_thread_id;
  using TorqueGeneratedJSAtomicsMutex<
      JSAtomicsMutex, JSSynchronizationPrimitive>::set_owner_thread_id;
};

// A condition variable that is exposed to JS.
//
// It has the following properties:
//   - Slim: 8-12 bytes. Lock state is 4 bytes, waiter queue head is 4 bytes
//     when V8_COMPRESS_POINTERS, and sizeof(void*) otherwise.
//   - Moving GC safe. It uses an index into the shared Isolate's external
//     pointer table to store a queue of sleeping threads.
//   - Parks the main thread LocalHeap when waiting. Unparks the main thread
//     LocalHeap after waking up.
//
// This condition variable manages its own queue of waiting threads, like
// JSAtomicsMutex. The algorithm is inspired by WebKit's ParkingLot.
//
// The state variable encodes the locking state as a single word: 0bQW.
// - W: Whether there are waiter threads in the queue.
// - Q: Whether the waiter queue is locked.
//
// The waiting algorithm is as follows:
// 1. Acquire the Q bit (set current state | 0b010) in a spinlock.
// 2. Destructively get the waiter queue head.
// 3. Enqueue this thread's WaiterQueueNode to the tail of the list pointed to
//    by the head, possibly creating a new list.
// 4. Release the Q bit and set the W bit (set (current state | 0b001) & ~0b010
//    in a single CAS operation).
// 5. Put the thread to sleep.
//
// The notification algorithm is as follows:
// 1. Acquire the Q bit (set current state | 0b010) in a spinlock.
// 2. Destructively get the waiter queue head.
// 3. If the head is not null, dequeue the head.
// 4. Store the new waiter queue head (possibly null).
// 5. If the list is empty, clear the W bit (set current state & ~0b001).
// 6. Release the Q bit (set current state & ~0b010).
//    (The W and Q bits must be set in a single CAS operation).
// 7. If the list was not empty, notify the dequeued head.

class JSAtomicsCondition
    : public TorqueGeneratedJSAtomicsCondition<JSAtomicsCondition,
                                               JSSynchronizationPrimitive> {
 public:
  using AsyncWaiterNodeType = WaitAsyncWaiterQueueNode;
  DECL_PRINTER(JSAtomicsCondition)
  EXPORT_DECL_VERIFIER(JSAtomicsCondition)

  V8_EXPORT_PRIVATE static bool WaitFor(Isolate* requester,
                                        DirectHandle<JSAtomicsCondition> cv,
                                        Handle<JSAtomicsMutex> mutex,
                                        std::optional<base::TimeDelta> timeout);

  V8_EXPORT_PRIVATE static MaybeHandle<JSReceiver> WaitAsync(
      Isolate* requester, Handle<JSAtomicsCondition> cv,
      DirectHandle<JSAtomicsMutex> mutex,
      std::optional<base::TimeDelta> timeout);

  static void HandleAsyncNotify(WaitAsyncWaiterQueueNode* node);
  static void HandleAsyncTimeout(WaitAsyncWaiterQueueNode* node);

  static constexpr uint32_t kAllWaiters = UINT32_MAX;

  // Notify {count} waiters. Returns the number of waiters woken up.
  static V8_EXPORT_PRIVATE uint32_t Notify(Isolate* requester,
                                           DirectHandle<JSAtomicsCondition> cv,
                                           uint32_t count);

  static void CleanupMatchingAsyncWaiters(Isolate* isolate,
                                          WaiterQueueNode* node,
                                          DequeueMatcher matcher);

  enum {
    kMutexAsyncContextSlot = Context::MIN_CONTEXT_SLOTS,
    kConditionVariableAsyncContextSlot,
    kAsyncContextLength
  };

  TQ_OBJECT_CONSTRUCTORS(JSAtomicsCondition)

 private:
  friend class Factory;

  static void QueueWaiter(Isolate* requester,
                          DirectHandle<JSAtomicsCondition> cv,
                          WaiterQueueNode* waiter);

  using DequeueAction = std::function<uint32_t(WaiterQueueNode**)>;
  static uint32_t DequeueExplicit(Isolate* requester,
                                  DirectHandle<JSAtomicsCondition> cv,
                                  std::atomic<StateT>* state,
                                  const DequeueAction& dequeue_action);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_ATOMICS_SYNCHRONIZATION_H_
