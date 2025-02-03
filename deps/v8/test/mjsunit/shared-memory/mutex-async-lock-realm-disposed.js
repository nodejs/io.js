// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax --expose-gc
// Flags: --no-lazy-compile-dispatcher --no-concurrent-recompilation

if (this.Worker) {
  (function TestRealmWithAsyncWaiterDisposed() {
    let workerLockScript = `onmessage = function({data:msg}) {
        let {mutex, sharedObj} = msg;
        Atomics.Mutex.lock(mutex, function() {
          postMessage('Lock acquired');
          while (!Atomics.load(sharedObj, 'done')) {
          }
        });
        postMessage('Lock released');
      };
      postMessage('started');`;

    let realmLockAsyncScript = `
      Atomics.Mutex.lockAsync(Realm.shared.mutex, async function() {
        Realm.shared.realmLocked = true;
      });`;

    let workerLock1 = new Worker(workerLockScript, {type: 'string'});
    assertEquals('started', workerLock1.getMessage());
    let workerLock2 = new Worker(workerLockScript, {type: 'string'});
    assertEquals('started', workerLock2.getMessage());
    let realmLockAsyc = Realm.create();

    let mutex = new Atomics.Mutex;
    let sharedObj = new (new SharedStructType(['done']))();
    sharedObj.done = false;
    let lock_msg = {mutex, sharedObj};
    workerLock1.postMessage(lock_msg);
    assertEquals('Lock acquired', workerLock1.getMessage());

    Realm.shared = {mutex, realmLocked: false};
    Realm.eval(realmLockAsyc, realmLockAsyncScript);

    workerLock2.postMessage(lock_msg);
    while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex) !== 2) {
    }

    Realm.dispose(realmLockAsyc);
    let collected = false;
    // Cleanup the realm's native context.
    gc({type: 'major', execution: 'async'})
        .then(() => {collected = true}, (e) => {
          throw e;
        });

    let executeAsserts = () => {
      // The realm was disposed but the waiter node that it created is still in
      // the isolate's unlocked waiters list.
      assertEquals(
          1, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
      // There are still 2 waiters in the mutex queue.
      assertEquals(
          2, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex));
      sharedObj.done = true;
      assertEquals('Lock released', workerLock1.getMessage());

      // If the async waiter was still alive, it would acquire the lock and
      // set Realm.shared.realmLocked to true when we flush the microtask queue
      // here.
      setTimeout(() => {
        // The lock was never acquired by the realm because it was destroyed.
        assertFalse(Realm.shared.realmLocked)
        // The async waiter notification was processed, removed itself from this
        // isolate's async waiters list and notified the next worker.
        assertEquals(
            0, %AtomicsSychronizationNumAsyncWaitersInIsolateForTesting())
        assertEquals('Lock acquired', workerLock2.getMessage());
        assertEquals('Lock released', workerLock2.getMessage());
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
  })();
}
