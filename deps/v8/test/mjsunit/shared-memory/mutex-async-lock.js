// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

(function TestAsyncLock() {
  let mutex = new Atomics.Mutex;

  let lockPromise = Atomics.Mutex.lockAsync(mutex, function() {
    return 42;
  })
  // Lock is taken but the callback won't be executed until the next microtask.
  assertFalse(Atomics.Mutex.tryLock(mutex, function() {}).success);
  assertEquals(1, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
  let resolverExecuted = false;
  lockPromise.then((result) => {
    // The lock was released before the then callback is executed.
    assertTrue(Atomics.Mutex.tryLock(mutex, function() {}).success);
    // Ensure the promise's result value property is set to the return
    // value of the callback.
    assertEquals(42, result.value);
    assertEquals(true, result.success);
    resolverExecuted = true;
  });
  setTimeout(() => {
    // The lock and then callbacks will be executed when the microtask queue
    // is flushed, before proceeding to the next task.
    assertEquals(resolverExecuted, true);
    assertEquals(0, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
  }, 0);
})();
