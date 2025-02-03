// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
let sab = new SharedArrayBuffer(10 * 4);
let memory = new Int32Array(sab);
let workers = [];
let runningWorkers = 0;

function startWorker(script) {
  let worker = new Worker(script, {type: 'string'});
  worker.done = false;
  worker.idx = workers.length;
  workers.push(worker);
  worker.postMessage(memory);
  ++runningWorkers;
};

let shared = `
  function wait(memory, index, waitCondition, wakeCondition) {
    while (memory[index] == waitCondition) {
      var result = Atomics.wait(memory, index, waitCondition);
      switch (result) {
        case 'not-equal':
        case 'ok':
          break;
        default:
          postMessage('Error: bad result from wait: ' + result);
          break;
      }
      var value = memory[index];
      if (value != wakeCondition) {
        postMessage(
            'Error: wait returned not-equal but the memory has a bad value: ' +
            value);
      }
    }
    var value = memory[index];
    if (value != wakeCondition) {
      postMessage(
          'Error: done waiting but the memory has a bad value: ' + value);
    }
  }

  function wake(memory, index) {
    var result = Atomics.notify(memory, index, 1);
    if (result != 0 && result != 1) {
      postMessage('Error: bad result from wake: ' + result);
    }
  }
`;

let worker1 = startWorker(shared + `
  onmessage = function({data:msg}) {
    let memory = msg;
    const didStartIdx = 0;
    const shouldGoIdx = 1;
    const didEndIdx = 2;

    postMessage("started");
    postMessage("memory: " + memory);
    wait(memory, didStartIdx, 0, 1);
    memory[shouldGoIdx] = 1;
    wake(memory, shouldGoIdx);
    wait(memory, didEndIdx, 0, 1);
    postMessage("memory: " + memory);
    postMessage("done");
  };
`);

let worker2 = startWorker(shared + `
  onmessage = function({data:msg}) {
    let memory = msg;
    const didStartIdx = 0;
    const shouldGoIdx = 1;
    const didEndIdx = 2;

    postMessage("started");
    postMessage("memory: " + memory);
    Atomics.store(memory, didStartIdx, 1);
    wake(memory, didStartIdx);
    wait(memory, shouldGoIdx, 0, 1);
    Atomics.store(memory, didEndIdx, 1);
    wake(memory, didEndIdx, 1);
    postMessage("memory: " + memory);
    postMessage("done");
  };
`);

let running = true;
while (running) {
  for (let worker of workers) {
    if (worker.done) continue;

    let msg = worker.getMessage();
    if (msg) {
      switch (msg) {
        case "done":
          if (worker.done === false) {
            print("worker #" + worker.idx + " done.");
            worker.done = true;
            if (--runningWorkers === 0) {
              running = false;
            }
          }
          break;

        default:
          print("msg from worker #" + worker.idx + ": " + msg);
          break;
      }
    }
  }
}
