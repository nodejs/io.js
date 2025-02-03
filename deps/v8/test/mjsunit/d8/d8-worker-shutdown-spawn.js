// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --stress-runs=1

let script = `onmessage =
   function({data:msg}) {
     if (msg.depth > 0) {
        print("spawn");
        let w = new Worker(msg.script, {type : "string"});
        w.postMessage({script: msg.script, depth: msg.depth - 1});
        let m = w.getMessage();
        w.terminate();
        postMessage(m);
     } else {
        postMessage(-99);
     }
}`;

function RunWorker(depth) {
  let w = new Worker(script, {type : "string"});

  let array = new Int32Array([55, -77]);
  w.postMessage({script: script, depth: depth});
  let msg = w.getMessage();
  print(msg);
  w.terminate();
}

function RunTest(depth, iterations) {
  let time = performance.now();
  for (let i = 0; i < iterations; i++) {
    let now = performance.now();
    print(`iteration ${i}, Δ = ${(now - time).toFixed(3)} ms`);
    RunWorker(depth);
    gc();
    time = now;
  }
}

// TODO(9524): increase the workload of this test. Runs out of threads
// on too many platforms.
RunTest(1, 1);
RunTest(2, 2);
RunTest(5, 3);
RunTest(9, 2);
