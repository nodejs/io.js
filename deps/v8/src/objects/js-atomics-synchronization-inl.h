// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_ATOMICS_SYNCHRONIZATION_INL_H_
#define V8_OBJECTS_JS_ATOMICS_SYNCHRONIZATION_INL_H_

#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/js-atomics-synchronization.h"
#include "src/objects/js-struct-inl.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-atomics-synchronization-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSSynchronizationPrimitive)

std::atomic<JSSynchronizationPrimitive::StateT>*
JSSynchronizationPrimitive::AtomicStatePtr() {
  StateT* state_ptr = reinterpret_cast<StateT*>(field_address(kStateOffset));
  DCHECK(IsAligned(reinterpret_cast<uintptr_t>(state_ptr), sizeof(StateT)));
  return base::AsAtomicPtr(state_ptr);
}

void JSSynchronizationPrimitive::SetNullWaiterQueueHead() {
#if V8_COMPRESS_POINTERS
  base::AsAtomic32::Relaxed_Store(waiter_queue_head_handle_location(),
                                  kNullExternalPointerHandle);
#else
  base::AsAtomicPointer::Relaxed_Store(waiter_queue_head_location(), nullptr);
#endif  // V8_COMPRESS_POINTERS
}

#if V8_COMPRESS_POINTERS
ExternalPointerHandle*
JSSynchronizationPrimitive::waiter_queue_head_handle_location() const {
  Address location = field_address(kWaiterQueueHeadOffset);
  return reinterpret_cast<ExternalPointerHandle*>(location);
}
#else
WaiterQueueNode** JSSynchronizationPrimitive::waiter_queue_head_location()
    const {
  Address location = field_address(kWaiterQueueHeadOffset);
  return reinterpret_cast<WaiterQueueNode**>(location);
}
#endif  // V8_COMPRESS_POINTERS

WaiterQueueNode* JSSynchronizationPrimitive::DestructivelyGetWaiterQueueHead(
    Isolate* requester) {
  if (V8_UNLIKELY(DEBUG_BOOL)) {
    StateT state = AtomicStatePtr()->load(std::memory_order_relaxed);
    DCHECK(IsWaiterQueueLockedField::decode(state));
    USE(state);
  }
#if V8_COMPRESS_POINTERS
  ExternalPointerHandle handle =
      base::AsAtomic32::Relaxed_Load(waiter_queue_head_handle_location());
  if (handle == kNullExternalPointerHandle) return nullptr;
  // Clear external pointer after decoding as a safeguard, no other thread
  // should be trying to access though the same non-null handle.
  WaiterQueueNode* waiter_head = reinterpret_cast<WaiterQueueNode*>(
      requester->shared_external_pointer_table().Exchange(handle, kNullAddress,
                                                          kWaiterQueueNodeTag));
  CHECK_NOT_NULL(waiter_head);
  return waiter_head;
#else
  return base::AsAtomicPointer::Relaxed_Load(waiter_queue_head_location());
#endif  // V8_COMPRESS_POINTERS
}

JSSynchronizationPrimitive::StateT
JSSynchronizationPrimitive::SetWaiterQueueHead(Isolate* requester,
                                               WaiterQueueNode* waiter_head,
                                               StateT new_state) {
  if (V8_UNLIKELY(DEBUG_BOOL)) {
    StateT state = AtomicStatePtr()->load(std::memory_order_relaxed);
    DCHECK(IsWaiterQueueLockedField::decode(state));
    USE(state);
  }
#if V8_COMPRESS_POINTERS
  if (waiter_head) {
    new_state = HasWaitersField::update(new_state, true);
    ExternalPointerHandle handle =
        base::AsAtomic32::Relaxed_Load(waiter_queue_head_handle_location());
    ExternalPointerTable& table = requester->shared_external_pointer_table();
    if (handle == kNullExternalPointerHandle) {
      handle = table.AllocateAndInitializeEntry(
          requester->shared_external_pointer_space(),
          reinterpret_cast<Address>(waiter_head), kWaiterQueueNodeTag);
      // Use a Release_Store to ensure that the store of the pointer into the
      // table is not reordered after the store of the handle. Otherwise, other
      // threads may access an uninitialized table entry and crash.
      base::AsAtomic32::Release_Store(waiter_queue_head_handle_location(),
                                      handle);
      return new_state;
    }
    if (DEBUG_BOOL) {
      Address old = requester->shared_external_pointer_table().Exchange(
          handle, reinterpret_cast<Address>(waiter_head), kWaiterQueueNodeTag);
      DCHECK_EQ(kNullAddress, old);
      USE(old);
    } else {
      requester->shared_external_pointer_table().Set(
          handle, reinterpret_cast<Address>(waiter_head), kWaiterQueueNodeTag);
    }
  } else {
    new_state = HasWaitersField::update(new_state, false);
    base::AsAtomic32::Relaxed_Store(waiter_queue_head_handle_location(),
                                    kNullExternalPointerHandle);
  }
#else
  new_state = HasWaitersField::update(new_state, waiter_head);
  base::AsAtomicPointer::Relaxed_Store(waiter_queue_head_location(),
                                       waiter_head);
#endif  // V8_COMPRESS_POINTERS
  return new_state;
}

TQ_OBJECT_CONSTRUCTORS_IMPL(JSAtomicsMutex)

JSAtomicsMutex::LockGuardBase::LockGuardBase(Isolate* isolate,
                                             Handle<JSAtomicsMutex> mutex,
                                             bool locked)
    : isolate_(isolate), mutex_(mutex), locked_(locked) {}

JSAtomicsMutex::LockGuardBase::~LockGuardBase() {
  if (locked_) mutex_->Unlock(isolate_);
}

JSAtomicsMutex::LockGuard::LockGuard(Isolate* isolate,
                                     Handle<JSAtomicsMutex> mutex,
                                     std::optional<base::TimeDelta> timeout)
    : LockGuardBase(isolate, mutex,
                    JSAtomicsMutex::Lock(isolate, mutex, timeout)) {}

JSAtomicsMutex::TryLockGuard::TryLockGuard(Isolate* isolate,
                                           Handle<JSAtomicsMutex> mutex)
    : LockGuardBase(isolate, mutex, mutex->TryLock()) {}

// static
bool JSAtomicsMutex::LockImpl(Isolate* requester,
                              DirectHandle<JSAtomicsMutex> mutex,
                              std::optional<base::TimeDelta> timeout,
                              LockSlowPathWrapper slow_path_wrapper) {
  DisallowGarbageCollection no_gc;
  // First try to lock an uncontended mutex, which should be the common case. If
  // this fails, then go to the slow path to possibly put the current thread to
  // sleep.
  //
  // The fast path is done using a weak CAS which may fail spuriously on
  // architectures with load-link/store-conditional instructions.
  std::atomic<StateT>* state = mutex->AtomicStatePtr();
  StateT expected = kUnlockedUncontended;
  bool locked;
  if (V8_LIKELY(state->compare_exchange_weak(expected, kLockedUncontended,
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed))) {
    locked = true;
  } else {
    locked = slow_path_wrapper(state);
  }
  if (V8_LIKELY(locked)) {
    mutex->SetCurrentThreadAsOwner();
  }
  return locked;
}

// static
bool JSAtomicsMutex::Lock(Isolate* requester, Handle<JSAtomicsMutex> mutex,
                          std::optional<base::TimeDelta> timeout) {
  return LockImpl(requester, mutex, timeout, [=](std::atomic<StateT>* state) {
    return LockSlowPath(requester, mutex, state, timeout);
  });
}

bool JSAtomicsMutex::TryLock() {
  DisallowGarbageCollection no_gc;
  StateT expected = kUnlockedUncontended;
  if (V8_LIKELY(AtomicStatePtr()->compare_exchange_strong(
          expected, kLockedUncontended, std::memory_order_acquire,
          std::memory_order_relaxed))) {
    SetCurrentThreadAsOwner();
    return true;
  }
  return false;
}

void JSAtomicsMutex::Unlock(Isolate* requester) {
  DisallowGarbageCollection no_gc;
  // First try to unlock an uncontended mutex, which should be the common
  // case. If this fails, then go to the slow path to wake a waiting thread.
  //
  // In contrast to Lock, the fast path is done using a strong CAS which does
  // not fail spuriously. This simplifies the slow path by guaranteeing that
  // there is at least one waiter to be notified.
  DCHECK(IsCurrentThreadOwner());
  ClearOwnerThread();
  std::atomic<StateT>* state = AtomicStatePtr();
  StateT expected = kLockedUncontended;
  if (V8_LIKELY(state->compare_exchange_strong(expected, kUnlockedUncontended,
                                               std::memory_order_release,
                                               std::memory_order_relaxed))) {
    return;
  }
  UnlockSlowPath(requester, state);
}

bool JSAtomicsMutex::IsHeld() {
  return IsLockedField::decode(
      AtomicStatePtr()->load(std::memory_order_relaxed));
}

bool JSAtomicsMutex::IsCurrentThreadOwner() {
  bool result = AtomicOwnerThreadIdPtr()->load(std::memory_order_relaxed) ==
                ThreadId::Current().ToInteger();
  DCHECK_IMPLIES(result, IsHeld());
  return result;
}

void JSAtomicsMutex::SetCurrentThreadAsOwner() {
  AtomicOwnerThreadIdPtr()->store(ThreadId::Current().ToInteger(),
                                  std::memory_order_relaxed);
}

void JSAtomicsMutex::ClearOwnerThread() {
  AtomicOwnerThreadIdPtr()->store(ThreadId::Invalid().ToInteger(),
                                  std::memory_order_relaxed);
}

std::atomic<int32_t>* JSAtomicsMutex::AtomicOwnerThreadIdPtr() {
  int32_t* owner_thread_id_ptr =
      reinterpret_cast<int32_t*>(field_address(kOwnerThreadIdOffset));
  return base::AsAtomicPtr(owner_thread_id_ptr);
}

TQ_OBJECT_CONSTRUCTORS_IMPL(JSAtomicsCondition)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_ATOMICS_SYNCHRONIZATION_INL_H_
