// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

(function TestAsyncLockFailsOnStackOveflow() {
  let count = 0;
  let error;
  let mutex = new Atomics.Mutex;
  (function f() {
    try { f() } catch (e) {}
    if (count === 0) {
      count++;
      try {
        // This call will fail because internally it calls the promise_then
        // builtin which will cause a stack overflow.
        Atomics.Mutex.lockAsync(mutex, () => {});
      } catch (e) {
        // Don't call functions in here to avoid stack overflow.
        error = e;
      }
    }
  })();
  // The lockAsync call failed and the waiter nodes were cleaned up.
  assertEquals("Maximum call stack size exceeded", error.message);
  assertEquals("RangeError", error.name);
  assertEquals(0, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex));
  assertEquals(0, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
})();
