// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function workerCode1() {
  onmessage = function({data:e}) {
    const a = new Int32Array(e.sab);
    while(true) {
      // This worker tries to switch the value from 1 to 2; if it succeeds, it
      // also notifies.
      const ret = Atomics.compareExchange(a, 0, 1, 2);
      if (ret === 1) {
        Atomics.notify(a, 0);
      }
      // Check if we're asked to terminate:
      if (Atomics.load(a, 1) == 1) {
        return;
      }
    }
  }
}

function workerCode2() {
  const MAX_ROUNDS = 40;
  onmessage = function({data:e}) {
    const a = new Int32Array(e.sab);
    let round = 0;
    function nextRound() {
      while (true) {
        if (round == MAX_ROUNDS) {
          // Tell worker1 to terminate.
          Atomics.store(a, 1, 1);
          postMessage('done');
          return;
        }

        // This worker changes the value to 1, and waits for it to change to 2
        // via Atomics.waitAsync.
        Atomics.store(a, 0, 1);

        const res = Atomics.waitAsync(a, 0, 1);
        if (res.async) {
          res.value.then(() => { ++round; nextRound();},
                         ()=> {});
          return;
        }
        // Else: continue looping. (This happens when worker1 changed the value
        // back to 2 before waitAsync started.)
      }
    }

    nextRound();
  }
}

let sab = new SharedArrayBuffer(8);

let w1 = new Worker(workerCode1, {type: 'function'});
w1.postMessage({sab: sab});

let w2 = new Worker(workerCode2, {type: 'function'});
w2.postMessage({sab: sab});

// Wait for worker2.
w2.getMessage();
w1.terminate();
w2.terminate();
