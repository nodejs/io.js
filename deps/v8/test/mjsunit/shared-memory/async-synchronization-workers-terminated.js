
// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax
if (this.Worker) {
  (function TestWorkerTerminated() {
    let workerLockScript = `onmessage = function({data:msg}) {
      let {mutex, sharedObj} = msg;
      Atomics.Mutex.lock(mutex, function() {
        postMessage('Lock acquired');
        while(!Atomics.load(sharedObj, 'done')) {}
      });
      postMessage('Lock released');
    };
    postMessage('Worker started');`;
    let workerWaitScript = `onmessage = function({data:msg}) {
      let {cv_mutex, cv, shared_Obj} = msg;
      Atomics.Mutex.lock(cv_mutex, function() {
        postMessage('Waiting started');
        Atomics.Condition.wait(cv, cv_mutex);
      });
      postMessage('Waiting done');
    };
    postMessage('Worker started');`;
    let workerAsyncScript = `onmessage = function({data:msg}) {
      if (msg.type === 'lock') {
        let {mutex, sharedObj} = msg;
        for (let i = 0; i < 10; i++) {
          Atomics.Mutex.lockAsync(mutex, async function() {})
        }
        postMessage('Lock waiters queued');
      }
      else if (msg.type === 'wait'){
        let {cv_mutex, cv} = msg;
        for (let i = 0; i < 10; i++) {
          Atomics.Mutex.lockAsync(cv_mutex, async function() {
            await Atomics.Condition.waitAsync(cv, cv_mutex);
          })
        }
        // The waiters will be processed when the microtask queue is flushed
        // after this task. This will queue both tasks and microtasks, so other
        // incoming tasks might be handled before the the waitAsyncs are
        // executed.
        postMessage('Handled wait messages');
      }
      else {
        postMessage(%AtomicsSychronizationNumAsyncWaitersInIsolateForTesting());
      }
    };
    postMessage('Worker started');`;

    let workerLock1 = new Worker(workerLockScript, {type: 'string'});
    assertEquals('Worker started', workerLock1.getMessage());
    let workerLock2 = new Worker(workerLockScript, {type: 'string'});
    assertEquals('Worker started', workerLock2.getMessage());
    let workerWait1 = new Worker(workerWaitScript, {type: 'string'});
    assertEquals('Worker started', workerWait1.getMessage());
    let workerWait2 = new Worker(workerWaitScript, {type: 'string'});
    assertEquals('Worker started', workerWait2.getMessage());
    let workerAsync = new Worker(workerAsyncScript, {type: 'string'});
    assertEquals('Worker started', workerAsync.getMessage());
    let SharedType = new SharedStructType(['done']);
    let sharedObj = new SharedType();
    sharedObj.done = false;
    let mutex = new Atomics.Mutex;
    let cv_mutex = new Atomics.Mutex;
    let cv = new Atomics.Condition;
    let lock_msg = {mutex, sharedObj, type: 'lock'};
    let wait_msg = {cv_mutex, cv, type: 'wait'};
    let count_msg = {type: 'count'};
    workerLock1.postMessage(lock_msg);
    workerWait1.postMessage(wait_msg);
    assertEquals('Lock acquired', workerLock1.getMessage());
    assertEquals('Waiting started', workerWait1.getMessage());
    workerAsync.postMessage(lock_msg);
    assertEquals('Lock waiters queued', workerAsync.getMessage());
    workerAsync.postMessage(wait_msg);
    assertEquals('Handled wait messages', workerAsync.getMessage());
    // Wait until the waitAsync-related tasks are processed in the worker before
    // posting a new message.
    while(%AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv) !== 11) {
    }
    // Verify there are 30 waiters in the async waiter list of the workerAsync's
    // isolate.
    workerAsync.postMessage(count_msg);
    assertEquals(30, workerAsync.getMessage());
    workerLock2.postMessage(lock_msg);
    workerWait2.postMessage(wait_msg);
    assertEquals('Waiting started', workerWait2.getMessage());
    while(%AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv) !== 12) {
    }
    workerAsync.terminate();
    while(!Atomics.Mutex.tryLock(cv_mutex, function() {
      sharedObj.done = true;
      Atomics.Condition.notify(cv, 20);
    }).success) {
    }
    assertEquals('Lock released', workerLock1.getMessage());
    assertEquals('Lock acquired', workerLock2.getMessage());
    assertEquals('Lock released', workerLock2.getMessage());
    assertEquals('Waiting done', workerWait1.getMessage());
    assertEquals('Waiting done', workerWait2.getMessage());
    workerLock1.terminate();
    workerLock2.terminate();
    workerWait1.terminate();
    workerWait2.terminate();
  })();
}
