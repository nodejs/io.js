// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_ATOMICS_SYNCHRONIZATION_H_
#define V8_OBJECTS_JS_ATOMICS_SYNCHRONIZATION_H_

#include <atomic>

#include "src/base/platform/time.h"
#include "src/execution/thread-id.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-struct.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-atomics-synchronization-tq.inc"

namespace detail {
class WaiterQueueNode;
}  // namespace detail

using detail::WaiterQueueNode;

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

  using TorqueGeneratedJSSynchronizationPrimitive<
      JSSynchronizationPrimitive, AlwaysSharedSpaceJSObject>::state;
  using TorqueGeneratedJSSynchronizationPrimitive<
      JSSynchronizationPrimitive, AlwaysSharedSpaceJSObject>::set_state;

 private:
#if V8_COMPRESS_POINTERS
  // When pointer compression is enabled, the pointer to the waiter queue head
  // is stored in the external pointer table and the object itself only contains
  // a 32-bit external pointer handles.
  inline ExternalPointerHandle* waiter_queue_head_handle_location() const;
#else
  inline WaiterQueueNode** waiter_queue_head_location() const;
#endif
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
                     base::Optional<base::TimeDelta> timeout = base::nullopt);
  };

  // The mutex is attempted to be locked via `TryLock` when a `TryLockGuard`
  // object is created. If the mutex was acquired, then it is released when the
  // `TryLockGuard` object is destructed.
  class V8_NODISCARD TryLockGuard final : public LockGuardBase {
   public:
    inline TryLockGuard(Isolate* isolate, Handle<JSAtomicsMutex> mutex);
  };

  DECL_CAST(JSAtomicsMutex)
  DECL_PRINTER(JSAtomicsMutex)
  EXPORT_DECL_VERIFIER(JSAtomicsMutex)

  // Lock the mutex, blocking if it's currently owned by another thread.
  // Returns false if the lock times out, true otherwise.
  static inline bool Lock(
      Isolate* requester, Handle<JSAtomicsMutex> mutex,
      base::Optional<base::TimeDelta> timeout = base::nullopt);

  V8_WARN_UNUSED_RESULT inline bool TryLock();

  inline void Unlock(Isolate* requester);

  inline bool IsHeld();
  inline bool IsCurrentThreadOwner();

  TQ_OBJECT_CONSTRUCTORS(JSAtomicsMutex)

 private:
  friend class Factory;
  friend class WaiterQueueNode;

  // There are 3 state bits: whether there are waiter threads in the queue,
  // whether the waiter queue is locked (both inherited from the base class),
  // and whether the lock itself is locked (IsLockedField).
  using IsLockedField = JSSynchronizationPrimitive::NextBitField<bool, 1>;

  static constexpr StateT kUnlockedUncontended = 0;
  static constexpr StateT kLockedUncontended = IsLockedField::encode(true);

  inline void SetCurrentThreadAsOwner();
  inline void ClearOwnerThread();

  inline std::atomic<int32_t>* AtomicOwnerThreadIdPtr();

  V8_EXPORT_PRIVATE static bool LockSlowPath(
      Isolate* requester, Handle<JSAtomicsMutex> mutex,
      std::atomic<StateT>* state, base::Optional<base::TimeDelta> timeout);
  V8_EXPORT_PRIVATE void UnlockSlowPath(Isolate* requester,
                                        std::atomic<StateT>* state);

  // Returns true if the JS mutex was taken and false otherwise.
  bool LockJSMutexOrDequeueTimedOutWaiter(Isolate* requester,
                                          std::atomic<StateT>* state,
                                          WaiterQueueNode* timed_out_waiter);

  static bool TryLockExplicit(std::atomic<StateT>* state, StateT& expected);
  static bool TryLockWaiterQueueExplicit(std::atomic<StateT>* state,
                                         StateT& expected);
  static void UnlockWaiterQueueWithNewState(std::atomic<StateT>* state,
                                            StateT new_state);

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
  DECL_CAST(JSAtomicsCondition)
  DECL_PRINTER(JSAtomicsCondition)
  EXPORT_DECL_VERIFIER(JSAtomicsCondition)

  V8_EXPORT_PRIVATE static bool WaitFor(
      Isolate* requester, Handle<JSAtomicsCondition> cv,
      Handle<JSAtomicsMutex> mutex, base::Optional<base::TimeDelta> timeout);

  static constexpr uint32_t kAllWaiters = UINT32_MAX;

  // Notify {count} waiters. Returns the number of waiters woken up.
  static V8_EXPORT_PRIVATE uint32_t Notify(Isolate* requester,
                                           Handle<JSAtomicsCondition> cv,
                                           uint32_t count);

  Tagged<Object> NumWaitersForTesting(Isolate* isolate);

  TQ_OBJECT_CONSTRUCTORS(JSAtomicsCondition)

 private:
  friend class Factory;
  friend class WaiterQueueNode;

  static constexpr StateT kEmptyState = 0;

  static bool TryLockWaiterQueueExplicit(std::atomic<StateT>* state,
                                         StateT& expected);

  using DequeueAction = std::function<WaiterQueueNode*(WaiterQueueNode**)>;
  static WaiterQueueNode* DequeueExplicit(Isolate* requester,
                                          Handle<JSAtomicsCondition> cv,
                                          std::atomic<StateT>* state,
                                          const DequeueAction& dequeue_action);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_ATOMICS_SYNCHRONIZATION_H_
