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
            postMessage("lock acquired");
            while(!box.timedOut) {
              Atomics.Condition.wait(cv, cv_mutex);
            }
          });
        });
        postMessage("done");
      };
      postMessage("started");`;
    let worker = new Worker(workerScript, {type: 'string'});

    assertEquals('started', worker.getMessage());

    let Box = new SharedStructType(['timedOut']);
    let box = new Box();
    box.timedOut = false;
    let mutex = new Atomics.Mutex;
    let cv = new Atomics.Condition;
    let cv_mutex = new Atomics.Mutex;
    let msg = {mutex, box, cv, cv_mutex};
    worker.postMessage(msg);
    assertEquals('lock acquired', worker.getMessage());

    let thenResolved = false;
    let lockPromise = Atomics.Mutex.lockAsync(mutex, () => {
      assertUnreachable();
    }, 1);

    lockPromise.then((result) => {
      thenResolved = true;
      assertEquals(false, result.success);
      assertEquals(undefined, result.value);
      assertEquals(
          0, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
    });

    let asyncLoop = () => {
      if (!thenResolved) {
        setTimeout(asyncLoop, 0);
      } else {
        // Ensure that the worker is waiting on the condition variable before
        // notifying it.
        while(!Atomics.Mutex.tryLock(cv_mutex, function() {
          box.timedOut = true;
          Atomics.Condition.notify(cv, 1);
        }).success) {
        }
        assertEquals('done', worker.getMessage());
        worker.terminate();
      }
    };
    asyncLoop();
  })();
}
