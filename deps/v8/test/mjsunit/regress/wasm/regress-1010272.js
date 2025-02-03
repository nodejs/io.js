// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-grow-shared-memory

const kNumWorkers = 100;
const kNumMessages = 50;

function AllocMemory(initial, maximum = initial) {
  return new WebAssembly.Memory({initial : initial, maximum : maximum, shared : true});
}

(function RunTest() {
  let worker = [];
  for (let w = 0; w < kNumWorkers; w++) {
    worker[w] = new Worker(
        `onmessage = function({data:msg}) {
          msg.memory.grow(1);
        }`, {type : 'string'});
  }

  for (let i = 0; i < kNumMessages; i++) {
    let memory = AllocMemory(1, 128);
    for (let w = 0; w < kNumWorkers; w++) {
      worker[w].postMessage({memory : memory});
    }
  }
})();
