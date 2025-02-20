// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

(function TestAsyncWaitPromisePrototypeTampering() {
  const mutex = new Atomics.Mutex;
  const cv = new Atomics.Condition;
  let finishedExecution = false;
  let lockAsyncPromise = Atomics.Mutex.lockAsync(mutex, async () => {
    Atomics.Condition.waitAsync(cv, mutex);
    finishedExecution = true;
  });

  let thenExecuted = false;
  lockAsyncPromise.then(() => {
    thenExecuted = true;
  });

  // Run Microtasks
  setTimeout(() => {
    assertTrue(finishedExecution);
    assertTrue(thenExecuted);
    assertEquals(1, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv));
  }, 0);
})();
