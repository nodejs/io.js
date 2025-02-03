// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax --expose-gc
// Flags: --no-lazy-compile-dispatcher --no-concurrent-recompilation

if (this.Worker) {
  (function TestRealmWithAsyncWaiterDisposed() {
    let workerScript = `onmessage = function({data:msg}) {
        let {mutex, cv, sharedObj} = msg;
        Atomics.Mutex.lock(mutex, function() {
          postMessage('Lock acquired');
          Atomics.Condition.wait(cv, mutex);
          sharedObj.counter++;
        });
        postMessage('Stopped waiting');
      };
      postMessage('started');`;

    let realmScript = `
      Atomics.Mutex.lockAsync(Realm.shared.mutex, async function() {
        await Atomics.Condition.waitAsync(Realm.shared.cv, Realm.shared.mutex);
        Realm.shared.sharedObj.counter++;
      });`;

    let worker1 = new Worker(workerScript, {type: 'string'});
    assertEquals('started', worker1.getMessage());
    let worker2 = new Worker(workerScript, {type: 'string'});
    assertEquals('started', worker2.getMessage());
    let realm = Realm.create();

    let mutex = new Atomics.Mutex;
    let cv = new Atomics.Condition;
    let SharedType = new SharedStructType(['counter']);
    let sharedObj = new SharedType();
    sharedObj.counter = 0;
    let lock_msg = {mutex, cv, sharedObj};
    Realm.shared = {mutex, cv, sharedObj};

    worker1.postMessage(lock_msg);
    assertEquals('Lock acquired', worker1.getMessage())
    // Wait until the cv waiter is queued.
    while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv) !== 1) {
    }

    Realm.eval(realm, realmScript);

    // Flush microtask queue, the async lock is taken and waitAsync is called.
    setTimeout(() => {
      assertEquals(
          2, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv));
      worker2.postMessage(lock_msg);
      assertEquals('Lock acquired', worker2.getMessage());
      // Wait until the cv waiter is queued.
      while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv) !== 3) {
      }

      Realm.dispose(realm);
      // Cleanup the realm's native context.
      let collected = false;
      gc({type: 'major', execution: 'async'})
          .then(() => {collected = true}, (e) => {
            throw e;
          });

      let executeAsserts = () => {
        // The realm was disposed but the waiter nodes created for both: the
        // asyncLock and the asyncWait are still in the isolate's async waiter
        // list.
        assertEquals(
            2, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
        // There are still 3 waiters in the cv queue.
        assertEquals(
            3, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv));
        assertEquals(0, sharedObj.counter);
        Atomics.Condition.notify(cv, 3);
        setTimeout(() => {
          assertEquals('Stopped waiting', worker1.getMessage());
          assertEquals('Stopped waiting', worker2.getMessage());
          assertEquals(
              0, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv));
          // The notification for the async waiter was processed but execution
          // didn't continue.
          assertEquals(2, sharedObj.counter);
        }, 0);
      };

      let asyncLoop = () => {
        if (!collected) {
          setTimeout(asyncLoop, 0);
          return;
        }
        executeAsserts();
      };
      asyncLoop();
    }, 0);
  })();
}
