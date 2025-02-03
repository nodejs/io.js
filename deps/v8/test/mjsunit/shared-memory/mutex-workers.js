// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

"use strict";

if (this.Worker) {

(function TestMutexWorkers() {
  let workerScript =
      `onmessage = function({data:msg}) {
         let mutex = msg.mutex;
         let box = msg.box;
         for (let i = 0; i < 10; i++) {
           Atomics.Mutex.lock(mutex, function() {
             box.counter++;
           });
         }
         postMessage("done");
       };
       postMessage("started");`;

  let worker1 = new Worker(workerScript, { type: 'string' });
  let worker2 = new Worker(workerScript, { type: 'string' });
  assertEquals("started", worker1.getMessage());
  assertEquals("started", worker2.getMessage());

  let Box = new SharedStructType(['counter']);
  let box = new Box();
  box.counter = 0;
  let mutex = new Atomics.Mutex();
  let msg = { mutex, box };
  worker1.postMessage(msg);
  worker2.postMessage(msg);
  assertEquals("done", worker1.getMessage());
  assertEquals("done", worker2.getMessage());
  assertEquals(20, box.counter);

  worker1.terminate();
  worker2.terminate();
})();

(function TestMutexWaiters() {
  let workerScript = `onmessage = function({data:msg}) {
         let mutex = msg.mutex;
         let box = msg.box;
         Atomics.Mutex.lock(mutex, function() {
          while(!Atomics.load(box, 'isDone')) {}
         });
         postMessage("done");
       };
       postMessage("started");`;

  let worker1 = new Worker(workerScript, {type: 'string'});
  let worker2 = new Worker(workerScript, {type: 'string'});
  let worker3 = new Worker(workerScript, {type: 'string'});
  assertEquals('started', worker1.getMessage());
  assertEquals('started', worker2.getMessage());
  assertEquals('started', worker3.getMessage());

  let Box = new SharedStructType(['isDone']);
  let box = new Box();
  box.isDone = false;
  let mutex = new Atomics.Mutex();
  let msg = {mutex, box};
  worker1.postMessage(msg);
  worker2.postMessage(msg);
  worker3.postMessage(msg);
  while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex) !== 2) {}
  box.isDone = true;
  assertEquals('done', worker1.getMessage());
  assertEquals('done', worker2.getMessage());
  assertEquals('done', worker3.getMessage());
  assertEquals(0, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex));

  worker1.terminate();
  worker2.terminate();
  worker3.terminate();
})();

(function TestTimeout() {
  let worker1Script = `onmessage = function({data:msg}) {
        let {mutex, box, cv, cv_mutex} = msg;
        Atomics.Mutex.lock(mutex, function() {
          Atomics.Mutex.lock(cv_mutex, function() {
            // Post inside cv_mutex critical section to ensure worker2
            // requests it after worker1.
            postMessage("lock acquired");
            while(!box.timedOut) {
              Atomics.Condition.wait(cv, cv_mutex);
            }
          });
        });
        postMessage("done");
      };
      postMessage("started");`;
  let worker2Script = `onmessage = function({data:msg}) {
         let {mutex, box, cv, cv_mutex} = msg;
         let result =
            Atomics.Mutex.lockWithTimeout(mutex, ()=>{} , 1);
         box.timedOut = !result.success;
         // Use cv_mutex to ensure the notify happens after the wait.
         Atomics.Mutex.lock(cv_mutex, function() {
          Atomics.Condition.notify(cv);
         });
         postMessage("done");
        };
        postMessage("started");`;
  let worker1 = new Worker(worker1Script, {type: 'string'});
  let worker2 = new Worker(worker2Script, {type: 'string'});

  assertEquals('started', worker2.getMessage());
  assertEquals('started', worker1.getMessage());

  let Box = new SharedStructType(['timedOut']);
  let box = new Box();
  box.timedOut = false;
  let mutex = new Atomics.Mutex;
  let cv = new Atomics.Condition;
  let cv_mutex = new Atomics.Mutex;
  let msg = {mutex, box, cv, cv_mutex};
  worker1.postMessage(msg);
  assertEquals('lock acquired', worker1.getMessage());
  worker2.postMessage(msg);
  assertEquals('done', worker2.getMessage());
  assertEquals('done', worker1.getMessage());
  worker1.terminate();
  worker2.terminate();
})();

(function TestUnlockWhileTimingOut() {
  // Racy version of the timeout test, where the worker holding the lock might
  // unlock it while the other worker is timing out and cleaning its waiter from
  // the queue.
  let workerLockScript = `onmessage = function({data:msg}) {
      let {mutex, box} = msg;
      Atomics.Mutex.lock(mutex, function() {
        while (!Atomics.load(box, 'isDone')) {}
      });
      postMessage("done");
    }
    postMessage("started");`;

  let timedOutWorkerScript = `onmessage = function({data:msg}) {
      let {mutex} = msg;
      let result = Atomics.Mutex.lockWithTimeout(mutex, function() {}, 0);
      postMessage("done");
    }
    postMessage("started");`;

  let workerLock = new Worker(workerLockScript, {type: 'string'});
  let timedOutWorker = new Worker(timedOutWorkerScript, {type: 'string'});
  assertEquals('started', workerLock.getMessage());
  assertEquals('started', timedOutWorker.getMessage());

  let Box = new SharedStructType(['isDone']);
  let box = new Box();
  box.isDone = false;
  let mutex = new Atomics.Mutex;

  workerLock.postMessage({mutex, box});
  timedOutWorker.postMessage({mutex});
  box.isDone = true;
  assertEquals('done', workerLock.getMessage());
  assertEquals('done', timedOutWorker.getMessage());
  assertEquals(0, %AtomicsSynchronizationPrimitiveNumWaitersForTesting(mutex));
  assertTrue(Atomics.Mutex.tryLock(mutex, function() {}).success);

  workerLock.terminate();
  timedOutWorker.terminate();
})();

}
