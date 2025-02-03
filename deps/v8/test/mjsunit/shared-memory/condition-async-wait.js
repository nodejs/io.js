// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax --expose-gc

(function TestAsyncWait() {
  let mutex = new Atomics.Mutex;
  let cv = new Atomics.Condition;
  let notified = false;

  let locked = Atomics.Mutex.lockAsync(mutex, async function() {
    await Atomics.Condition.waitAsync(cv, mutex);
    // There is only one waiter in this isolate's async waiter list, even though
    // the lock has been taked 2 times (once in the lockAsync and once after the
    // wait was notified).
    assertEquals(1, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
    notified = true;
  });

  // The lock is held, but the callback is not executed until the next
  // microtask.
  assertFalse(Atomics.Mutex.tryLock(mutex, function() {}).success);

  // Call gc to test that everything is kept alive while the lock is taken.
  gc();

  setTimeout(() => {
    // The lock callback has been executed and the condition variable
    // queued one waiter, and the lock is released.
    assertEquals(1, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv));
    // The condition variable waiters was added to this isolate's async waiter
    // list and the mutex waiter is still in the list.
    assertEquals(2, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
    assertTrue(Atomics.Mutex.tryLock(mutex, function() {}).success);
    Atomics.Condition.notify(cv, 1);
    // The notification task has been posted, but the execution will continue
    // in the next microtask.
    assertFalse(notified);
    setTimeout(() => {
      assertTrue(notified);
      // The waiter was removed from this isolate's async waiters list.
      assertEquals(
          0, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
    }, 0);
  }, 0);
})();
