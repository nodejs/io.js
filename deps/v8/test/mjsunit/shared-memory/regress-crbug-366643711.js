// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-struct --allow-natives-syntax

(function TestNegativeNotify() {
  const mutex = new Atomics.Mutex;
  const cv = new Atomics.Condition;
  let done = false;
  let promise = Atomics.Mutex.lockAsync(mutex, async () => {
    await Atomics.Condition.waitAsync(cv, mutex);
  })
  promise.then(() => {
    done = true;
  })

  const cleanupLoop = () => {
    assertEquals(0, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv));
    if (done) {
      return;
    }
    setTimeout(cleanupLoop, 0);
  };

  const notifyLoop = () => {
    if (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv) === 1) {
      Atomics.Condition.notify(cv, -14);
      assertEquals(
          1, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv));
      assertFalse(done);
      Atomics.Condition.notify(cv);
      cleanupLoop();
      return;
    }
    setTimeout(notifyLoop, 0);
  };
  notifyLoop();
})();
