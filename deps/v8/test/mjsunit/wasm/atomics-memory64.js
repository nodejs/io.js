// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64 --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function allowOOM(fn) {
  try {
    fn();
  } catch (e) {
    const is_oom =
        (e instanceof RangeError) && e.message.includes('Out of memory');
    if (!is_oom) throw e;
  }
}

function CreateBigSharedWasmMemory64(num_pages) {
  let builder = new WasmModuleBuilder();
  builder.addMemory64(num_pages, num_pages, true);
  builder.exportMemoryAs('memory');
  return builder.instantiate().exports.memory;
}

function WasmAtomicNotify(memory, offset, index, num) {
  let builder = new WasmModuleBuilder();
  let pages = memory.buffer.byteLength / kPageSize;
  builder.addImportedMemory("m", "memory", pages, pages, "shared", "memory64");
  builder.addFunction("main", makeSig([kWasmF64, kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64SConvertF64,
      kExprLocalGet, 1,
      kAtomicPrefix,
      kExprAtomicNotify, /* alignment */ 2, ...wasmSignedLeb64(offset, 10)])
    .exportAs("main");

  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  return instance.exports.main(index, num);
}

function WasmI32AtomicWait(memory, offset, index, val, timeout) {
  let builder = new WasmModuleBuilder();
  let pages = memory.buffer.byteLength / kPageSize;
  builder.addImportedMemory("m", "memory", pages, pages, "shared", "memory64");
  builder.addFunction("main",
    makeSig([kWasmF64, kWasmI32, kWasmF64], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprI64SConvertF64,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprI64SConvertF64,
      kAtomicPrefix,
      kExprI32AtomicWait, /* alignment */ 2, ...wasmSignedLeb64(offset, 10)])
      .exportAs("main");

  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  return instance.exports.main(index, val, timeout);
}

function WasmI64AtomicWait(memory, offset, index, val_low,
                                   val_high, timeout) {
  let builder = new WasmModuleBuilder();
  let pages = memory.buffer.byteLength / kPageSize;
  builder.addImportedMemory("m", "memory", pages, pages, "shared", "memory64");
  // Wrapper for I64AtomicWait that takes two I32 values and combines to into
  // I64 for the instruction parameter.
  builder.addFunction("main",
    makeSig([kWasmF64, kWasmI32, kWasmI32, kWasmF64], [kWasmI32]))
    .addLocals(kWasmI64, 1)
    .addBody([
      kExprLocalGet, 1,
      kExprI64UConvertI32,
      kExprI64Const, 32,
      kExprI64Shl,
      kExprLocalGet, 2,
      kExprI64UConvertI32,
      kExprI64Ior,
      kExprLocalSet, 4, // Store the created I64 value in local
      kExprLocalGet, 0,
      kExprI64SConvertF64,
      kExprLocalGet, 4,
      kExprLocalGet, 3,
      kExprI64SConvertF64,
      kAtomicPrefix,
      kExprI64AtomicWait, /* alignment */ 3, ...wasmSignedLeb64(offset, 10)])
      .exportAs("main");

  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  return instance.exports.main(index, val_high, val_low, timeout);
}

function TestAtomicI32Wait(pages, offset) {
  if (!%IsAtomicsWaitAllowed()) return;
  print(arguments.callee.name);

  let memory = CreateBigSharedWasmMemory64(pages);

  assertEquals(kAtomicWaitNotEqual, WasmI32AtomicWait(memory, offset, 0, 42, -1));
  assertEquals(kAtomicWaitTimedOut, WasmI32AtomicWait(memory, offset, 0, 0, 0));
  assertEquals(kAtomicWaitNotEqual, WasmI32AtomicWait(memory, 0, offset, 42, -1));
  assertEquals(kAtomicWaitTimedOut, WasmI32AtomicWait(memory, 0, offset, 0, 0));

  let i32a = new Int32Array(memory.buffer);
  i32a[offset / 4] = 1;

  assertEquals(kAtomicWaitNotEqual, WasmI32AtomicWait(memory, offset, 0, 0, -1));
  assertEquals(kAtomicWaitTimedOut, WasmI32AtomicWait(memory, offset, 0, 1, 0));
  assertEquals(kAtomicWaitNotEqual, WasmI32AtomicWait(memory, 0, offset, 0, -1));
  assertEquals(kAtomicWaitTimedOut, WasmI32AtomicWait(memory, 0, offset, 1, 0));
}

function TestAtomicI64Wait(pages, offset) {
  if (!%IsAtomicsWaitAllowed()) return;
  print(arguments.callee.name);

  let memory = CreateBigSharedWasmMemory64(pages);
  assertEquals(kAtomicWaitNotEqual, WasmI64AtomicWait(memory, offset, 0, 42, 0, -1));
  assertEquals(kAtomicWaitTimedOut, WasmI64AtomicWait(memory, offset, 0, 0, 0, 0));
  assertEquals(kAtomicWaitNotEqual, WasmI64AtomicWait(memory, 0, offset, 42, 0, -1));
  assertEquals(kAtomicWaitTimedOut, WasmI64AtomicWait(memory, 0, offset, 0, 0, 0));

  let i32a = new Int32Array(memory.buffer);
  i32a[offset / 4] = 1;
  i32a[(offset / 4) + 1] = 2;

  assertEquals(kAtomicWaitNotEqual, WasmI64AtomicWait(memory, offset, 0, 2, 1, -1));
  assertEquals(kAtomicWaitTimedOut, WasmI64AtomicWait(memory, offset, 0, 1, 2, 0));
  assertEquals(kAtomicWaitNotEqual, WasmI64AtomicWait(memory, 0, offset, 2, 1, -1));
  assertEquals(kAtomicWaitTimedOut, WasmI64AtomicWait(memory, 0, offset, 1, 2, 0));
}

//// WORKER ONLY TESTS

function TestWasmAtomicsInWorkers(OFFSET, INDEX, PAGES) {
  // This test creates 4 workers that wait on consecutive (8 byte separated to
  // satisfy alignments for all kinds of wait) memory locations to test various
  // wait/wake combinations. For each combination, each thread waits 3 times
  // expecting all 4 threads to be woken with wake(4) in first iteration, all 4
  // to be woken with wake(5) in second iteration and, 3 and 1 to be woken in
  // third iteration.
  print(arguments.callee.name);

  const INDEX_JS = OFFSET + INDEX;
  let memory = CreateBigSharedWasmMemory64(PAGES);
  let i32a = new Int32Array(memory.buffer);
  const numWorkers = 4;

  let workerScript = `onmessage = function({data:msg}) {
    d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");
    ${WasmI32AtomicWait.toString()}
    ${WasmI64AtomicWait.toString()}
    let id = msg.id;
    let memory = msg.memory;
    let INDEX = msg.INDEX;
    let OFFSET = msg.OFFSET;
    let INDEX_JS = OFFSET + INDEX;
    let i32a = new Int32Array(memory.buffer);
    // Indices are divided by 4 for Atomics.wait to convert them to index
    // for Int32Array.
    // For wasm-wake numWorkers threads.
    let result = Atomics.wait(i32a, INDEX_JS / 4, 0);
    postMessage(result);
    // For wasm-wake numWorkers + 1 threads.
    result = Atomics.wait(i32a, (INDEX_JS + 8) / 4, 0);
    postMessage(result);
    // For wasm-wake numWorkers - 1 threads.
    result = Atomics.wait(i32a, (INDEX_JS + 16) / 4, 0);
    postMessage(result);
    // For js-wake numWorkers threads.
    result = WasmI32AtomicWait(memory, OFFSET, INDEX + 24, 0, -1);
    postMessage(result);
    // For js-wake numWorkers + 1 threads.
    result = WasmI32AtomicWait(memory, OFFSET, INDEX + 32, 0, -1);
    postMessage(result);
    // For js-wake numWorkers - 1 threads.
    result = WasmI32AtomicWait(memory, OFFSET, INDEX + 40, 0, -1);
    postMessage(result);
    // For wasm-wake numWorkers threads.
    result = WasmI32AtomicWait(memory, OFFSET, INDEX + 48, 0, -1);
    postMessage(result);
    // For wasm-wake numWorkers + 1 threads.
    result = WasmI32AtomicWait(memory, OFFSET, INDEX + 56, 0, -1);
    postMessage(result);
    // For wasm-wake numWorkers - 1 threads.
    result = WasmI32AtomicWait(memory, OFFSET, INDEX + 64, 0, -1);
    postMessage(result);
    // For js-wake numWorkers threads.
    result = WasmI64AtomicWait(memory, OFFSET, INDEX + 72, 0, 0, -1);
    postMessage(result);
    // For js-wake numWorkers + 1 threads.
    result = WasmI64AtomicWait(memory, OFFSET, INDEX + 80, 0, 0, -1);
    postMessage(result);
    // For js-wake numWorkers - 1 threads.
    result = WasmI64AtomicWait(memory, OFFSET, INDEX + 88, 0, 0, -1);
    postMessage(result);
    // For wasm-wake numWorkers threads.
    result = WasmI64AtomicWait(memory, OFFSET, INDEX + 96, 0, 0, -1);
    postMessage(result);
    // For wasm-wake numWorkers + 1 threads.
    result = WasmI64AtomicWait(memory, OFFSET, INDEX + 104, 0, 0, -1);
    postMessage(result);
    // For wasm-wake numWorkers - 1 threads.
    result = WasmI64AtomicWait(memory, OFFSET, INDEX + 112, 0, 0, -1);
    postMessage(result);
  };`;

  let waitForAllWorkers = function(index) {
    while (%AtomicsNumWaitersForTesting(i32a, index / 4) != numWorkers) {}
  }

  let jsWakeCheck = function(index, num, workers, msg) {
    waitForAllWorkers(index);
    let indexJs = index / 4;
    if (num >= numWorkers) {
      // If numWorkers or more is passed to wake, numWorkers workers should be
      // woken.
      assertEquals(numWorkers, Atomics.notify(i32a, indexJs, num));
    } else {
      // If num < numWorkers is passed to wake, num workers should be woken.
      // Then the remaining workers are woken for the next part.
      assertEquals(num, Atomics.notify(i32a, indexJs, num));
      assertEquals(numWorkers-num, Atomics.notify(i32a, indexJs, numWorkers));
    }
    for (let id = 0; id < numWorkers; id++) {
      assertEquals(msg, workers[id].getMessage());
    }
  };

  let wasmWakeCheck = function(offset, index, num, workers, msg) {
    waitForAllWorkers(offset + index);
    if (num >= numWorkers) {
      assertEquals(numWorkers, WasmAtomicNotify(memory, offset, index, num));
    } else {
      assertEquals(num, WasmAtomicNotify(memory, offset, index, num));
      assertEquals(numWorkers-num,
                   WasmAtomicNotify(memory, offset, index, numWorkers));
    }
    for (let id = 0; id < numWorkers; id++) {
      assertEquals(msg, workers[id].getMessage());
    }
  };

  let workers = [];
  for (let id = 0; id < numWorkers; id++) {
    workers[id] = new Worker(workerScript, {type: 'string'});
    workers[id].postMessage({id, memory, INDEX, OFFSET});
  }

  wasmWakeCheck(OFFSET, INDEX + 0, numWorkers, workers, "ok");
  wasmWakeCheck(OFFSET, INDEX + 8, numWorkers + 1, workers, "ok");
  wasmWakeCheck(OFFSET, INDEX + 16, numWorkers - 1, workers, "ok");

  jsWakeCheck(INDEX_JS + 24, numWorkers, workers, kAtomicWaitOk);
  jsWakeCheck(INDEX_JS + 32, numWorkers + 1, workers, kAtomicWaitOk);
  jsWakeCheck(INDEX_JS + 40, numWorkers - 1, workers, kAtomicWaitOk);

  wasmWakeCheck(OFFSET, INDEX + 48, numWorkers, workers, kAtomicWaitOk);
  wasmWakeCheck(OFFSET, INDEX + 56, numWorkers + 1, workers, kAtomicWaitOk);
  wasmWakeCheck(OFFSET, INDEX + 64, numWorkers - 1, workers, kAtomicWaitOk);

  jsWakeCheck(INDEX_JS + 72, numWorkers, workers, kAtomicWaitOk);
  jsWakeCheck(INDEX_JS + 80, numWorkers + 1, workers, kAtomicWaitOk);
  jsWakeCheck(INDEX_JS + 88, numWorkers - 1, workers, kAtomicWaitOk);

  wasmWakeCheck(OFFSET, INDEX + 96,  numWorkers, workers, kAtomicWaitOk);
  wasmWakeCheck(OFFSET, INDEX + 104, numWorkers + 1, workers, kAtomicWaitOk);
  wasmWakeCheck(OFFSET, INDEX + 112, numWorkers - 1, workers, kAtomicWaitOk);

  for (let id = 0; id < numWorkers; id++) {
    workers[id].terminate();
  }
}

const OFFSET = 4000;
const MEM_PAGES = 100;

const GB = 1024 * 1024 * 1024;
const BIG_OFFSET = 4294970000;
const BIG_MEM_PAGES = 5 * GB / kPageSize;

(function(){
  TestAtomicI32Wait(MEM_PAGES, OFFSET);
  TestAtomicI64Wait(MEM_PAGES, OFFSET);

  if (this.Worker) {
    TestWasmAtomicsInWorkers(OFFSET, 0, MEM_PAGES);
    TestWasmAtomicsInWorkers(0, OFFSET, MEM_PAGES);
  }
})();

// Tests require Big memory >4GB, they may fail because of OOM.
(function(){
  allowOOM(() => TestAtomicI32Wait(BIG_MEM_PAGES, BIG_OFFSET));
  allowOOM(() => TestAtomicI64Wait(BIG_MEM_PAGES, BIG_OFFSET));

  if (this.Worker) {
    allowOOM(() => TestWasmAtomicsInWorkers(BIG_OFFSET, 0, BIG_MEM_PAGES));
    allowOOM(() => TestWasmAtomicsInWorkers(0, BIG_OFFSET, BIG_MEM_PAGES));
  }
})();
