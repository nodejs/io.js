// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

(function test() {
  const sab = new SharedArrayBuffer(16);
  const i32a = new Int32Array(sab);
  const location = 0;

  (function createWorker() {
    function workerCode(location) {
      onmessage = function({data:msg}) {
        if (msg.sab) {
          const i32a = new Int32Array(msg.sab);
          // Start 2 async waits in the same location.
          const result1 = Atomics.waitAsync(i32a, location, 0);
          const result2 = Atomics.waitAsync(i32a, location, 0);
          postMessage('worker waiting');
        }
      }
    }
    const w = new Worker(workerCode, {type: 'function', arguments: [location]});
    w.postMessage({sab: sab});
    const m = w.getMessage();
    assertEquals('worker waiting', m);
    w.terminateAndWait();
  })();

  const notify_return_value = Atomics.notify(i32a, location, 2);
  // No waiters got notified, since they got cleaned up before it.
  assertEquals(0, notify_return_value);
})();
