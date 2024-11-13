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
      let {mutex, sharedObj} = msg;
      Atomics.Mutex.lock(mutex, function() {
        postMessage('Lock acquired');
        while(!Atomics.load(sharedObj, 'done')) {}
      });
      postMessage('Lock released');
    };`;

    let workerAsync = new Worker(workerAsyncScript, {type: 'string'});
    let workerLock1 = new Worker(workerLockScript, {type: 'string'});
    let workerLock2 = new Worker(workerLockScript, {type: 'string'});

    let mutex = new Atomics.Mutex;
    let SharedType = new SharedStructType(['done']);
    let sharedObj = new SharedType();
    sharedObj.done = false;
    let msg = {mutex, sharedObj};

    workerLock1.postMessage(msg);
    assertEquals(workerLock1.getMessage(), 'Lock acquired');
    assertEquals(
        0, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex));
    workerAsync.postMessage(msg);
    while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex) !== 1) {
    }
    workerLock2.postMessage(msg);
    while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex) !== 2) {
    }
    workerAsync.terminate();
    sharedObj.done = true;
    assertEquals(workerLock1.getMessage(), 'Lock released');
    assertEquals(workerLock2.getMessage(), 'Lock acquired');
    assertEquals(workerLock2.getMessage(), 'Lock released');
    workerLock1.terminate();
    workerLock2.terminate();
  })();
}
