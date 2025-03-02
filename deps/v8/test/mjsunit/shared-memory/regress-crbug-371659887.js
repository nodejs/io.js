// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

// If Promise.prototype.then is set to a non-callable, builtin
// promises become non thenables, hence any promise resolve to
// them calls its reactions instead of queing a then task.
// In the current implementation an internal promise with
// a reaction to release the lock is resolved to the result of
// the asyncLock callback, causing the unlock microtask to
// be executed before the asyncLock callback finishes. This test
// should be updated if the behavior intentionally changes.
(function TestAsyncWaitPromisePrototypeTampering() {
  const mutex = new Atomics.Mutex;
  const cv = new Atomics.Condition;
  let finishedExecution = false;
  let startedExecution = false;
  let lockAsyncPromise = Atomics.Mutex.lockAsync(mutex, async () => {
    startedExecution = true;
    await Atomics.Condition.waitAsync(cv, mutex);
    finishedExecution = true;
  });

  let thenExecuted = false;
  lockAsyncPromise.then(() => {
    thenExecuted = true;
  });

  Promise.prototype.then = undefined;
  // Run Microtasks
  setTimeout(() => {
    assertTrue(startedExecution);
    assertFalse(finishedExecution);
    // The lockAsyncPromise is resolved even though the async callback
    // didn't finish executing.
    assertTrue(thenExecuted);
  }, 0);
})();
