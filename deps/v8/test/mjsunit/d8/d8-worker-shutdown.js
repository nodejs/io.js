// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --stress-runs=1

const kBatchSize = 10;
const kNumBatches = 10;

function RunWorkerBatch(count) {
  let script = `onmessage = function({data:msg}) {
    if (msg.array) {
      msg.array[0] = 99;
      postMessage({array : msg.array});
    }
}`;

  // Launch workers.
  let workers = new Array(count);
  for (let i = 0; i < count; i++) {
    workers[i] = new Worker(script, {type : 'string'});
  }

  // Send messages.
  for (let i = 0; i < workers.length; i++) {
    let array = new Int32Array([55, -77]);
    workers[i].postMessage({array : array});
    // terminate half of the workers early.
    if ((i & 1) == 1) {
      workers[i].terminate();
      // A second terminate should have no effect.
      workers[i].terminate();
    }
  }

  // Wait for replies.
  for (let i = 0; i < workers.length; i++) {
    let msg = workers[i].getMessage();
    if (msg !== undefined && msg.array) {
      assertInstanceof(msg.array, Int32Array);
      assertEquals(99, msg.array[0]);
      assertEquals(-77, msg.array[1]);
    }
    // terminate all workers.
    workers[i].terminate();
  }
}

(function RunTest() {
  print(`running ${kNumBatches} batches...`);
  let time = performance.now();
  for (let i = 0; i < kNumBatches; i++) {
    let before = performance.now();
    RunWorkerBatch(kBatchSize);
    let time = performance.now() - before;
    print(`batch ${i+1}, Δ = ${(time).toFixed(3)} ms`);
  }
})();
