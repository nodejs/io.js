// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct --allow-natives-syntax

"use strict";

if (this.Worker) {

(function TestWait() {
  let workerScript =
      `onmessage = function({data:msg}) {
         let mutex = msg.mutex;
         let cv = msg.cv;
         let res = Atomics.Mutex.lock(mutex, function() {
           return Atomics.Condition.wait(cv, mutex);
         });
         postMessage(res);
       };`;

  let mutex = new Atomics.Mutex;
  let cv = new Atomics.Condition;
  let msg = {mutex, cv};

  let worker1 = new Worker(workerScript, { type: 'string' });
  let worker2 = new Worker(workerScript, { type: 'string' });
  worker1.postMessage(msg);
  worker2.postMessage(msg);

  // Spin until both workers are waiting.
  while (%AtomicsSynchronizationPrimitiveNumWaitersForTesting(cv) != 2) {}

  assertEquals(2, Atomics.Condition.notify(cv, 2));

  assertEquals(true, worker1.getMessage());
  assertEquals(true, worker2.getMessage());

  worker1.terminate();
  worker2.terminate();
})();

}
