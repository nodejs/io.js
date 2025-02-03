// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

'use strict';

if (this.Worker) {
  (function TestTimeout() {
    let workerScript = `onmessage = function({data:msg}) {
        let {mutex, box, cv, cv_mutex} = msg;
        Atomics.Mutex.lock(mutex, function() {
          Atomics.Mutex.lock(cv_mutex, function() {
            // Atomics.Condition.wait releases cv_mutex but not mutex.
            postMessage("lock acquired");
            Atomics.Condition.wait(cv, cv_mutex);
          });
        });
        postMessage("done");
      };
      postMessage("started");`;
    let worker = new Worker(workerScript, {type: 'string'});

    assertEquals('started', worker.getMessage());

    let mutex = new Atomics.Mutex;
    let cv = new Atomics.Condition;
    let cv_mutex = new Atomics.Mutex;
    let msg = {mutex, cv, cv_mutex};
    worker.postMessage(msg);
    assertEquals('lock acquired', worker.getMessage());
    assertFalse(Atomics.Mutex.tryLock(mutex, function() {}).success);

    let thenResolved = false;
    let lockPromise = Atomics.Mutex.lockAsync(mutex, () => {
      return 42;
    });
    // There is one waiter for mutex in this isolate's async waiter list.
    assertEquals(1, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());

    lockPromise.then((result) => {
      thenResolved = true;
      assertEquals(true, result.success);
      assertEquals(42, result.value);
    });

    let cv_promise = Atomics.Mutex.lockAsync(cv_mutex, () => {
      Atomics.Condition.notify(cv, 1);
    });

    cv_promise.then((result) => {
      assertEquals(true, result.success);
      assertEquals('done', worker.getMessage());
    });

    let asyncLoop = () => {
      if (!thenResolved) {
        setTimeout(asyncLoop, 0);
      }
    };
    asyncLoop();
  })();
}
