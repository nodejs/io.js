// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const kNumberOfWorker = 4;

const workerOnMessage = function({data:msg}) {
  if (msg.module) {
    let module = msg.module;
    let mem = msg.mem;
    this.instance = new WebAssembly.Instance(module, {m: {memory: mem}});
    postMessage({instantiated: true});
  } else {
    const kNumberOfRuns = 20;
    let result = new Array(kNumberOfRuns);
    for (let i = 0; i < kNumberOfRuns; ++i) {
      result[i] = instance.exports.grow();
    }
    postMessage({result: result});
  }
};

function spawnWorkers() {
  let workers = [];
  for (let i = 0; i < kNumberOfWorker; i++) {
    let worker = new Worker(
        'onmessage = ' + workerOnMessage.toString(), {type: 'string'});
    workers.push(worker);
  }
  return workers;
}

function instantiateModuleInWorkers(workers, module, shared_memory) {
  for (let worker of workers) {
    worker.postMessage({module: module, mem: shared_memory});
    let msg = worker.getMessage();
    if (!msg.instantiated) throw 'Worker failed to instantiate';
  }
}

function triggerWorkers(workers) {
  for (i = 0; i < workers.length; i++) {
    let worker = workers[i];
    worker.postMessage({});
  }
}

(function TestConcurrentGrowMemoryResult() {
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory('m', 'memory', 1, 500, 'shared');
  builder.addFunction('grow', kSig_i_v)
      .addBody([kExprI32Const, 1, kExprMemoryGrow, kMemoryZero])
      .exportFunc();

  const module = builder.toModule();
  const shared_memory =
      new WebAssembly.Memory({initial: 1, maximum: 500, shared: true});

  // Spawn off the workers and run the sequences.
  let workers = spawnWorkers();
  instantiateModuleInWorkers(workers, module, shared_memory);
  triggerWorkers(workers);
  let all_results = [];
  for (let worker of workers) {
    let msg = worker.getMessage();
    all_results = all_results.concat(msg.result);
  }

  all_results.sort((a, b) => a - b);
  for (let i = 1; i < all_results.length; ++i) {
    assertEquals(all_results[i - 1] + 1, all_results[i]);
  }

  // Terminate all workers.
  for (let worker of workers) {
    worker.terminate();
  }
})();
