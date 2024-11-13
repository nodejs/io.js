// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-multi-memory

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
const mem0_idx = builder.addImportedMemory('imp', 'mem0', 0, 20, 'shared');
const mem1_idx = builder.addImportedMemory('imp', 'mem1', 0, 20, 'shared');
for (let mem_idx of [mem0_idx, mem1_idx]) {
  builder.addFunction(`notify${mem_idx}`, kSig_i_ii)
      .addBody([
        kExprLocalGet, 0,                                   // -
        kExprLocalGet, 1,                                   // -
        kAtomicPrefix, kExprAtomicNotify, 0x42, mem_idx, 0  // -
      ])
      .exportFunc();
  builder.addFunction(`wait${mem_idx}`, kSig_i_iii)
      .addBody([
        kExprLocalGet, 0,  // -
        kExprLocalGet, 1,  // -
        kExprLocalGet, 2,  // -
        // Convert to i64 and multiply by 1e6 (ms -> ns).
        kExprI64UConvertI32,                                 // -
        ...wasmI64Const(1e6),                                // -
        kExprI64Mul,                                         // -
        kAtomicPrefix, kExprI32AtomicWait, 0x42, mem_idx, 0  // -
      ])
      .exportFunc();
}
const module = builder.toModule();

const mem0 = new WebAssembly.Memory({initial: 1, maximum: 1, shared: true});
const mem1 = new WebAssembly.Memory({initial: 2, maximum: 2, shared: true});

const mem0_value = 0;
const mem1_value = 1;
new Uint32Array(mem1.buffer).fill(mem1_value);

const imports = {
  imp: {mem0: mem0, mem1: mem1}
};
const instance = new WebAssembly.Instance(module, imports);
const {notify0, notify1, wait0, wait1} = instance.exports;

const k10Ms = 10;
const k10s = 10000;

(function TestWaitNotEqual() {
  print(arguments.callee.name);
  assertEquals(kAtomicWaitNotEqual, wait0(0, mem0_value + 1, k10Ms));
  assertEquals(kAtomicWaitNotEqual, wait1(0, mem1_value + 1, k10Ms));
})();

(function TestWaitTimeout() {
  print(arguments.callee.name);
  assertEquals(kAtomicWaitTimedOut, wait0(0, mem0_value, k10Ms));
  assertEquals(kAtomicWaitTimedOut, wait1(0, mem1_value, k10Ms));
})();

(function TestWakeUpWorker() {
  print(arguments.callee.name);
  function workerCode() {
    instance = undefined;
    onmessage = function({data:msg}) {
      if (!instance) {
        instance = new WebAssembly.Instance(msg.module, msg.imports);
        postMessage('instantiated');
        return;
      }
      if (msg.action === 'wait0' || msg.action === 'wait1') {
        let result = instance.exports[msg.action](...msg.arguments);
        print(`[worker] ${msg.action} ->: ${result}`);
        postMessage(result);
        return;
      }
      postMessage(`Invalid action: ${msg.action}`);
    }
  }
  let worker = new Worker(workerCode, {type: 'function'});

  worker.postMessage({module: module, imports: imports});
  assertEquals('instantiated', worker.getMessage());

  const offset = 48;
  for (let [mem_idx, mem_value] of [
           [mem0_idx, mem0_value], [mem1_idx, mem1_value]]) {
    print(`- memory ${mem_idx}`);

    // Test "not equals".
    worker.postMessage(
        {action: `wait${mem_idx}`, arguments: [offset, mem_value + 1, k10Ms]});
    assertEquals(kAtomicWaitNotEqual, worker.getMessage());

    // Test "timed out".
    worker.postMessage(
        {action: `wait${mem_idx}`, arguments: [offset, mem_value, k10Ms]});
    assertEquals(kAtomicWaitTimedOut, worker.getMessage());

    // Test "ok".
    worker.postMessage(
        {action: `wait${mem_idx}`, arguments: [offset, mem_value, k10s]});
    const started = performance.now();
    let notify = mem_idx == 0 ? notify0 : notify1;
    let notified;
    while ((notified = notify(offset, 1)) === 0) {
      const now = performance.now();
      if (now - started > k10s) {
        throw new Error('Could not notify worker within 10s');
      }
    }
    assertEquals(1, notified);
    assertEquals(kAtomicWaitOk, worker.getMessage());
  }

  worker.terminate();
})();
