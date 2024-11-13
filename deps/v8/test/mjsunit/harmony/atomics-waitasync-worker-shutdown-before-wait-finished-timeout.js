// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-gc

(function test() {
  const sab = new SharedArrayBuffer(16);
  const i32a = new Int32Array(sab);

  (function createWorker() {
    function workerCode() {
      onmessage = function({data:msg}) {
        if (msg.sab) {
          const i32a = new Int32Array(msg.sab);
          const result = Atomics.waitAsync(i32a, 0, 0, 100000);
          postMessage('worker waiting');
        }
      }
    }
    const w = new Worker(workerCode, {type: 'function'});
    w.postMessage({sab: sab});
    const m = w.getMessage();
    assertEquals('worker waiting', m);
    w.terminate();
  })();

  gc();

  Atomics.notify(i32a, 0, 1);
})();
