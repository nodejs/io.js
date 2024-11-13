// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

if (this.Worker) {
  (function TestAsyncLockedWorkerTerminated() {
    let workerAsyncScript = `onmessage = function({data:msg}) {
      let {mutex, sharedObj} = msg;
      let asyncLoop = (resolve) => {
        if (!Atomics.load(sharedObj, 'done')) {
          setTimeout(asyncLoop, 0);
        }
        else {
          resolve();
        }
      };
      Atomics.Mutex.lockAsync(mutex, async function() {
        postMessage('Lock acquired');
        await new Promise((resolve) => {asyncLoop(resolve);});
      });
    };`;

    let workerLockScript = `onmessage = function({data:msg}) {
      let {mutex} = msg;
      Atomics.Mutex.lock(mutex, function() {
        postMessage('Lock acquired');
      });
      postMessage('Lock released');
    };`;

    let workerAsync = new Worker(workerAsyncScript, {type: 'string'});
    let workerLock = new Worker(workerLockScript, {type: 'string'});

    let mutex = new Atomics.Mutex;
    let SharedType = new SharedStructType(['done']);
    let sharedObj = new SharedType();
    sharedObj.done = false;
    workerAsync.postMessage({mutex, sharedObj});
    assertEquals('Lock acquired', workerAsync.getMessage());
    assertEquals(
        0, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex));
    workerLock.postMessage({mutex});
    while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex) !== 1) {
    }
    workerAsync.terminate();
    assertEquals('Lock acquired', workerLock.getMessage());
    assertEquals('Lock released', workerLock.getMessage());
    workerLock.terminate();
  })();
}
