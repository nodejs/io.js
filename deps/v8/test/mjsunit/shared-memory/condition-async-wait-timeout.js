// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

(function TestAsyncWaitTimeout() {
  let mutex = new Atomics.Mutex;
  let cv = new Atomics.Condition;
  let notified = false;

  Atomics.Mutex.lockAsync(mutex, async function() {
    await Atomics.Condition.waitAsync(cv, mutex, 1);
    notified = true;
  });
  assertEquals(false, notified);

  // The wait will timeout after 1 ms, but setTimeout in tests doesn't wait
  // for the timeout if the test is already done, so use an async loop to wait.
  let asyncLoop = () => {
    if (!notified) {
      setTimeout(asyncLoop, 0);
      return;
    }
    // The waiter timedout and was removed from this isolate's async waiters
    // list.
    assertEquals(0, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
  };
  asyncLoop();
})();
