// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

const kNumMessages = 1000;

function AllocMemory(pages = 1, max = pages) {
  return new WebAssembly.Memory({initial : pages, maximum : max, shared : true});
}

(function RunTest() {
  function workerCode() {
    onmessage = function({data:msg}) {
      if (msg.memory) postMessage({memory : msg.memory});
      gc();
    }
  }

  let worker = new Worker(workerCode, {type: 'function'});

  let time = performance.now();

  for (let i = 0; i < kNumMessages; i++) {
    let now = performance.now();
    print(`iteration ${i}, Δ = ${(now - time).toFixed(3)} ms`);
    time = now;

    let memory = AllocMemory();
    worker.postMessage({memory : memory});
    let msg = worker.getMessage();
    if (msg.memory) {
      assertInstanceof(msg.memory, WebAssembly.Memory);
    }
    gc();
  }
})();
