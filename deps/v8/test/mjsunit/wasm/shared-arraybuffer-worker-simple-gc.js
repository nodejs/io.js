// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

const kNumIterations = 10;

function NewWorker() {
  function workerCode() {
    onmessage = ({data:msg}) => {
      if (msg.memory) postMessage("ack");
      if (msg.quit) postMessage("bye");
      gc();
    }
  }
  return new Worker(workerCode, {type: 'function'});
}

function PingWorker(worker, memory) {
  worker.postMessage({memory: memory});
  assertEquals("ack", worker.getMessage());
  worker.postMessage({quit: true});
  assertEquals("bye", worker.getMessage());
}

function AllocMemory() {
  return new SharedArrayBuffer(1024);
}

function RunSingleWorkerSingleMemoryTest() {
  print(arguments.callee.name);
  let worker = NewWorker();
  let first = AllocMemory();
  for (let i = 0; i < kNumIterations; i++) {
    print(`iteration ${i}`);
    PingWorker(worker, first);
    gc();
  }
  worker.terminate();
}

function RunSingleWorkerTwoMemoryTest() {
  print(arguments.callee.name);
  let worker = NewWorker();
  let first = AllocMemory(), second = AllocMemory();
  for (let i = 0; i < kNumIterations; i++) {
    print(`iteration ${i}`);
    PingWorker(worker, first);
    PingWorker(worker, second);
    gc();
  }
  worker.terminate();
}

function RunSingleWorkerMultipleMemoryTest() {
  print(arguments.callee.name);
  let worker = NewWorker();
  let first = AllocMemory();
  for (let i = 0; i < kNumIterations; i++) {
    print(`iteration ${i}`);
    PingWorker(worker, first);
    PingWorker(worker, AllocMemory());
    gc();
  }
  worker.terminate();
}

function RunMultipleWorkerMultipleMemoryTest() {
  print(arguments.callee.name);
  let first = AllocMemory();
  for (let i = 0; i < kNumIterations; i++) {
    print(`iteration ${i}`);
    let worker = NewWorker();
    PingWorker(worker, first);
    PingWorker(worker, AllocMemory());
    worker.terminate();
    gc();
  }
}

RunSingleWorkerSingleMemoryTest();
RunSingleWorkerTwoMemoryTest();
RunSingleWorkerMultipleMemoryTest();
RunMultipleWorkerMultipleMemoryTest();
