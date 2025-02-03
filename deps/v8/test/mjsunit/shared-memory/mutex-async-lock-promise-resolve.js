// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

(function TestAsyncPromiseResolveValueAndChain() {
  let mutex = new Atomics.Mutex();
  let thenExecuted = false;
  let waitResolve;
  let waitPromise = new Promise((resolve) => {
    waitResolve = resolve;
  });

  // If the return value of the callback is a promise, then lockPromise will
  // be resolved when the promise is resolved.
  let lockPromise = Atomics.Mutex.lockAsync(mutex, async function() {
    return waitPromise;
  });

  assertFalse(Atomics.Mutex.tryLock(mutex, function() {}).success);
  waitResolve(42);

  lockPromise.then((result) => {
    assertTrue(Atomics.Mutex.tryLock(mutex, function() {}).success);
    assertEquals(0, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
    // lockPromise is resolved with the same value as the callback promise.
    thenExecuted = true;
    assertEquals(42, result.value);
    assertTrue(result.success);
  });

  // The lock and then callbacks will be executed when the microtask queue is
  // flushed, before proceeding to the next task.
  setTimeout(() => {
    assertTrue(thenExecuted);
  }, 0);
})();
