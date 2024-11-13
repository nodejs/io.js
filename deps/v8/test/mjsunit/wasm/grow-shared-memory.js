// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function assertIsWasmSharedMemory(memory) {
 assertTrue(memory instanceof Object,
     "Memory is not an object");
 assertTrue(memory instanceof WebAssembly.Memory,
     "Object is not WebAssembly.Memory" );
 assertTrue(memory.buffer instanceof SharedArrayBuffer,
     "Memory.buffer is not a SharedArrayBuffer");
 assertTrue(Object.isFrozen(memory.buffer),
     "Memory.buffer not frozen");
}

function assertTrue(value, msg) {
  if (!value) {
    postMessage("Error: " + msg);
    throw new Error("Exit");  // To stop testing.
  }
}

let workerHelpers = assertTrue.toString() + assertIsWasmSharedMemory.toString();

(function TestGrowSharedMemoryWithoutPostMessage() {
  print(arguments.callee.name);
  let memory = new WebAssembly.Memory({initial: 1, maximum: 5, shared: true});
  assertEquals(memory.buffer.byteLength, kPageSize);
  assertEquals(1, memory.grow(1));
  assertEquals(memory.buffer.byteLength, 2 * kPageSize);
})();

(function TestPostMessageWithGrow() {
  print(arguments.callee.name);
  function workerCode(workerHelpers) {
    eval(workerHelpers);
    onmessage = function({data:obj}) {
      assertIsWasmSharedMemory(obj.memory);
      assertTrue(1 === obj.memory.grow(1));
      assertTrue(obj.memory.buffer.byteLength === obj.expected_size);
      assertIsWasmSharedMemory(obj.memory);
      postMessage("OK");
    }
  }
  let worker = new Worker(workerCode,
                          {type: 'function', arguments: [workerHelpers]});

  let memory = new WebAssembly.Memory({initial: 1, maximum: 5, shared: true});
  let obj = {memory: memory, expected_size: 2 * kPageSize};
  assertEquals(obj.memory.buffer.byteLength, kPageSize);
  worker.postMessage(obj);
  assertEquals("OK", worker.getMessage());
  assertEquals(obj.memory.buffer.byteLength, 2 * kPageSize);
  worker.terminate();
})();


// PostMessage from two different workers, and assert that the grow
// operations are performed on the same memory object.
(function TestWorkersWithGrowEarlyWorkerTerminate() {
  print(arguments.callee.name);
  function workerCode(workerHelpers) {
    eval(workerHelpers);
    onmessage = function({data:obj}) {
       assertIsWasmSharedMemory(obj.memory);
       obj.memory.grow(1);
       assertIsWasmSharedMemory(obj.memory);
       assertTrue(obj.memory.buffer.byteLength === obj.expected_size);
       postMessage("OK");
    };
  }

  let workers = [new Worker(workerCode,
                            {type: 'function', arguments: [workerHelpers]}),
                 new Worker(workerCode,
                            {type: 'function', arguments: [workerHelpers]})];
  let memory = new WebAssembly.Memory({initial: 1, maximum: 5, shared: true});
  let expected_pages = 1;
  for (let worker of workers) {
    assertEquals(memory.buffer.byteLength, expected_pages++ * kPageSize);
    let obj = {memory: memory, expected_size: expected_pages * kPageSize};
    worker.postMessage(obj);
    assertEquals("OK", worker.getMessage());
    assertEquals(memory.buffer.byteLength, expected_pages * kPageSize);
    worker.terminate();
  }
  assertEquals(memory.buffer.byteLength, expected_pages * kPageSize);
})();

// PostMessage of Multiple memories and grow
(function TestGrowSharedWithMultipleMemories() {
  print(arguments.callee.name);
  function workerCode(workerHelpers) {
    eval(workerHelpers);
    onmessage = function({data:obj}) {
      let expected_size = 0;
      let kPageSize = 0x10000;
      for (let memory of obj.memories) {
        assertIsWasmSharedMemory(memory);
        assertTrue(expected_size === memory.grow(2));
        expected_size+=2;
        assertIsWasmSharedMemory(memory);
        assertTrue(memory.buffer.byteLength === expected_size * kPageSize);
      }
      postMessage("OK");
    };
  }

  let worker = new Worker(workerCode,
                          {type: 'function', arguments: [workerHelpers]});
  let memories = [new WebAssembly.Memory({initial: 0, maximum: 2, shared: true}),
                  new WebAssembly.Memory({initial: 2, maximum: 10, shared: true}),
                  new WebAssembly.Memory({initial: 4, maximum: 12, shared: true})];
  let obj = {memories: memories};
  worker.postMessage(obj);
  assertEquals("OK", worker.getMessage());
  assertEquals(2 * kPageSize, memories[0].buffer.byteLength);
  assertEquals(4 * kPageSize, memories[1].buffer.byteLength);
  assertEquals(6 * kPageSize, memories[2].buffer.byteLength);
  worker.terminate();
})();

// SharedMemory Object shared between different instances
(function TestPostMessageJSAndWasmInterop() {
  print(arguments.callee.name);
  function workerCode(workerHelpers) {
    eval(workerHelpers);
    onmessage = function({data:obj}) {
      let kPageSize = 0x10000;
      assertIsWasmSharedMemory(obj.memory);
      let instance = new WebAssembly.Instance(
          obj.module, {m: {memory: obj.memory}});
      assertTrue(5 === obj.memory.grow(10));
      assertIsWasmSharedMemory(obj.memory);
      assertTrue(obj.memory.buffer.byteLength === 15 * kPageSize);
      assertTrue(15 === instance.exports.grow(5));
      assertIsWasmSharedMemory(obj.memory);
      assertTrue(obj.memory.buffer.byteLength === 20 * kPageSize);
      postMessage("OK");
    }
  }

  let worker = new Worker(workerCode,
                          {type: 'function', arguments: [workerHelpers]});
  let memory = new WebAssembly.Memory({initial: 5, maximum: 50, shared: true});
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 5, 100, "shared");
  builder.addFunction("grow", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
    .exportFunc();
  var module = new WebAssembly.Module(builder.toBuffer());
  let obj = {memory: memory, module: module};
  assertEquals(obj.memory.buffer.byteLength, 5 * kPageSize);
  worker.postMessage(obj);
  assertEquals("OK", worker.getMessage());
  worker.terminate();
  assertEquals(obj.memory.buffer.byteLength, 20 * kPageSize);
})();

(function TestConsecutiveJSAndWasmSharedGrow() {
  print(arguments.callee.name);
  function workerCode(workerHelpers) {
    eval(workerHelpers);
    onmessage = function({data:obj}) {
      let kPageSize = 0x10000;
      assertIsWasmSharedMemory(obj.memory);
      let instance = new WebAssembly.Instance(
          obj.module, {m: {memory: obj.memory}});
      assertTrue(5 === obj.memory.grow(10));
      assertIsWasmSharedMemory(obj.memory);
      assertTrue(obj.memory.buffer.byteLength === 15 * kPageSize);
      assertTrue(15 === instance.exports.grow(5));
      assertIsWasmSharedMemory(obj.memory);
      assertTrue(obj.memory.buffer.byteLength === 20 * kPageSize);
      postMessage("OK");
    }
  }

  let worker = new Worker(workerCode,
                          {type: 'function', arguments: [workerHelpers]});
  let memory = new WebAssembly.Memory({initial: 5, maximum: 50, shared: true});
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 5, 100, "shared");
  builder.addFunction("grow", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
    .exportFunc();
  var module = new WebAssembly.Module(builder.toBuffer());
  let obj = {memory: memory, module: module};
  assertEquals(obj.memory.buffer.byteLength, 5 * kPageSize);
  worker.postMessage(obj);
  assertEquals("OK", worker.getMessage());
  assertEquals(obj.memory.buffer.byteLength, 20 * kPageSize);
})();

(function TestConsecutiveWasmSharedGrow() {
  print(arguments.callee.name);
  function workerCode(workerHelpers) {
    eval(workerHelpers);
    onmessage = function({data:obj}) {
      let kPageSize = 0x10000;
      assertIsWasmSharedMemory(obj.memory);
      let instance = new WebAssembly.Instance(
          obj.module, {m: {memory: obj.memory}});
      assertTrue(5 === obj.memory.grow(10));
      assertIsWasmSharedMemory(obj.memory);
      assertTrue(obj.memory.buffer.byteLength === 15 * kPageSize);
      assertTrue(17 === instance.exports.grow_twice(2));
      assertIsWasmSharedMemory(obj.memory);
      assertTrue(obj.memory.buffer.byteLength === 19 * kPageSize);
      postMessage("OK");
    }
  }

  let worker = new Worker(workerCode,
                          {type: 'function', arguments: [workerHelpers]});
  let memory = new WebAssembly.Memory({initial: 5, maximum: 50, shared: true});
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 5, 100, "shared");
  builder.addFunction("grow_twice", kSig_i_i)
    .addBody([kExprLocalGet, 0,
        kExprMemoryGrow, kMemoryZero,
        kExprDrop,
        kExprLocalGet, 0,
        kExprMemoryGrow, kMemoryZero])
    .exportFunc();
  var module = new WebAssembly.Module(builder.toBuffer());
  let obj = {memory: memory, module: module};
  assertEquals(obj.memory.buffer.byteLength, 5 * kPageSize);
  worker.postMessage(obj);
  assertEquals("OK", worker.getMessage());
  assertEquals(obj.memory.buffer.byteLength, 19 * kPageSize);
  let instance = new WebAssembly.Instance(module, {m: {memory: memory}});
  assertEquals(21, instance.exports.grow_twice(2));
  assertEquals(obj.memory.buffer.byteLength, 23 * kPageSize);
})();

(function TestConsecutiveSharedGrowAndMemorySize() {
  print(arguments.callee.name);
  function workerCode(workerHelpers) {
    eval(workerHelpers);
    onmessage = function({data:obj}) {
      let kPageSize = 0x10000;
      assertIsWasmSharedMemory(obj.memory);
      let instance = new WebAssembly.Instance(
          obj.module, {m: {memory: obj.memory}});
      assertTrue(5 === obj.memory.grow(10));
      assertTrue(15 === instance.exports.memory_size());
      assertIsWasmSharedMemory(obj.memory);
      assertTrue(obj.memory.buffer.byteLength === 15 * kPageSize);
      assertTrue(19 === instance.exports.grow_and_size(2));
      assertIsWasmSharedMemory(obj.memory);
      assertTrue(obj.memory.buffer.byteLength === 19 * kPageSize);
      postMessage("OK");
    }
  }

  let worker = new Worker(workerCode,
                          {type: 'function', arguments: [workerHelpers]});
  let memory = new WebAssembly.Memory({initial: 5, maximum: 50, shared: true});
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 5, 100, "shared");
  builder.addFunction("grow_and_size", kSig_i_i)
    .addBody([kExprLocalGet, 0,
        kExprMemoryGrow, kMemoryZero,
        kExprDrop,
        kExprLocalGet, 0,
        kExprMemoryGrow, kMemoryZero,
        kExprDrop,
        kExprMemorySize, kMemoryZero])
    .exportFunc();
  builder.addFunction("memory_size", kSig_i_v)
    .addBody([kExprMemorySize, kMemoryZero])
    .exportFunc();
  var module = new WebAssembly.Module(builder.toBuffer());
  let obj = {memory: memory, module: module};
  assertEquals(obj.memory.buffer.byteLength, 5 * kPageSize);
  worker.postMessage(obj);
  assertEquals("OK", worker.getMessage());
  assertEquals(memory.buffer.byteLength, 19 * kPageSize);
  let instance = new WebAssembly.Instance(module, {m: {memory: memory}});
  assertEquals(23, instance.exports.grow_and_size(2));
  assertEquals(obj.memory.buffer.byteLength, 23 * kPageSize);
  assertEquals(23, memory.grow(2));
  assertEquals(25, instance.exports.memory_size());
})();

// Only spot checking here because currently the underlying buffer doesn't move.
// In the case that the underlying buffer does move, more comprehensive memory
// integrity checking and bounds checks testing are needed.
(function TestSpotCheckMemoryWithSharedGrow() {
  print(arguments.callee.name);
  function workerCode(workerHelpers) {
    eval(workerHelpers);
    onmessage = function({data:obj}) {
      let kPageSize = 0x10000;
      assertIsWasmSharedMemory(obj.memory);
      let instance = new WebAssembly.Instance(
          obj.module, {m: {memory: obj.memory}});
      assertTrue(5 === obj.memory.grow(10));
      assertIsWasmSharedMemory(obj.memory);
      assertTrue(obj.memory.buffer.byteLength === 15 * kPageSize);
      // Store again, and verify that the previous stores are still reflected.
      instance.exports.atomic_store(15 * kPageSize - 4, 0xACED);
      assertTrue(0xACED === instance.exports.atomic_load(0));
      assertTrue(0xACED === instance.exports.atomic_load(5 * kPageSize - 4));
      assertTrue(0xACED === instance.exports.atomic_load(15 * kPageSize - 4));
      assertTrue(15 === instance.exports.grow(2));
      assertIsWasmSharedMemory(obj.memory);
      assertTrue(obj.memory.buffer.byteLength === 17 * kPageSize);
      // Validate previous writes.
      instance.exports.atomic_store(17 * kPageSize - 4, 0xACED);
      assertTrue(0xACED === instance.exports.atomic_load(0));
      assertTrue(0xACED === instance.exports.atomic_load(5 * kPageSize - 4));
      assertTrue(0xACED === instance.exports.atomic_load(15 * kPageSize - 4));
      assertTrue(0xACED === instance.exports.atomic_load(17 * kPageSize - 4));
      postMessage("OK");
    }
  }

  let worker = new Worker(workerCode,
                          {type: 'function', arguments: [workerHelpers]});
  let memory = new WebAssembly.Memory({initial: 5, maximum: 50, shared: true});
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 5, 100, "shared");
  builder.addFunction("grow", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
    .exportFunc();
  builder.addFunction("atomic_load", kSig_i_i)
    .addBody([kExprLocalGet, 0, kAtomicPrefix, kExprI32AtomicLoad, 2, 0])
    .exportFunc();
  builder.addFunction("atomic_store", kSig_v_ii)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
      kAtomicPrefix, kExprI32AtomicStore, 2, 0])
    .exportFunc();
  var module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory: memory}});
  // Store at first and last accessible 32 bit offset.
  instance.exports.atomic_store(0, 0xACED);
  instance.exports.atomic_store(5 * kPageSize - 4, 0xACED);
  // Verify that these were stored.
  assertEquals(0xACED, instance.exports.atomic_load(0));
  assertEquals(0xACED, instance.exports.atomic_load(5 * kPageSize - 4));
  // Verify bounds.
  // If an underlying platform uses traps for a bounds check,
  // kTrapUnalignedAccess will be thrown before kTrapMemOutOfBounds.
  // Otherwise, kTrapMemOutOfBounds will be first.
  assertTrapsOneOf([kTrapMemOutOfBounds, kTrapUnalignedAccess],
      () => instance.exports.atomic_load(5 * kPageSize - 3));
  let obj = {memory: memory, module: module};
  assertEquals(obj.memory.buffer.byteLength, 5 * kPageSize);
  // PostMessage
  worker.postMessage(obj);
  assertEquals("OK", worker.getMessage());
  assertEquals(memory.buffer.byteLength, 17 * kPageSize);
  assertEquals(17, instance.exports.grow(2));
  assertEquals(obj.memory.buffer.byteLength, 19 * kPageSize);
  // Validate previous writes, and check bounds.
  assertTrue(0xACED === instance.exports.atomic_load(0));
  assertTrue(0xACED === instance.exports.atomic_load(5 * kPageSize - 4));
  assertTrue(0xACED === instance.exports.atomic_load(15 * kPageSize - 4));
  assertTrue(0xACED === instance.exports.atomic_load(17 * kPageSize - 4));
  assertTrapsOneOf([kTrapMemOutOfBounds, kTrapUnalignedAccess],
      () => instance.exports.atomic_load(19 * kPageSize - 3));
  assertEquals(19, memory.grow(6));
  assertEquals(obj.memory.buffer.byteLength, 25 * kPageSize);
  assertTrapsOneOf([kTrapMemOutOfBounds, kTrapUnalignedAccess],
      () => instance.exports.atomic_load(25 * kPageSize - 3));
})();

(function TestMemoryBufferTypeAfterGrow() {
  const memory = new WebAssembly.Memory({
    "initial": 1, "maximum": 2, "shared": true });
  assertInstanceof(memory.buffer, SharedArrayBuffer);
  assertEquals(memory.grow(1), 1);
  assertInstanceof(memory.buffer, SharedArrayBuffer);
})();

(function TestSharedMemoryGrowByZero() {
  const memory = new WebAssembly.Memory({
    "initial": 1, "maximum": 2, "shared": true });
  assertEquals(memory.grow(0), 1);
})();

// Tests that a function receives the update of a shared memory's size if a
// loop's stack guard gets invoked. This is not strictly required by spec, but
// we implement it as an optimization.
(function TestStackGuardUpdatesMemorySize() {
  print(arguments.callee.name);

  let initial_size = 1;
  let final_size = 2;

  let memory = new WebAssembly.Memory({initial: 1, maximum: 5, shared: true});

  let sync_index = 64;
  let sync_value = 42;

  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("mod", "mem", 1, 5, true);
  // int x;
  // while (true) {
  //   memory[sync_index] = sync_value;
  //   x = memory_size();
  //   if (x != 1) break;
  // }
  // return x;
  builder.addFunction("main", kSig_i_v)
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLoop, kWasmVoid,
        ...wasmI32Const(sync_index),
        ...wasmI32Const(sync_value),
        kAtomicPrefix, kExprI32AtomicStore, 2, 0,
        kExprMemorySize, 0, kExprLocalTee, 0,
        kExprI32Const, initial_size,
        kExprI32Eq,
        kExprBrIf, 0,
      kExprEnd,
      kExprLocalGet, 0])
    .exportFunc();

  builder.addFunction("setter", kSig_v_ii)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1,
                kAtomicPrefix, kExprI32AtomicStore, 2, 0])
      .exportFunc();

  builder.addFunction("getter", kSig_i_i)
      .addBody([kExprLocalGet, 0, kAtomicPrefix, kExprI32AtomicLoad, 2, 0])
      .exportFunc();

  let module = new WebAssembly.Module(builder.toBuffer());

  function workerCode() {
    onmessage = function({data:obj}) {
      let instance = new WebAssembly.Instance(
          obj.module, {mod: {mem: obj.memory}});
      let res = instance.exports.main();
      postMessage(res);
    }
  }

  let worker = new Worker(workerCode,
                          {type: 'function', arguments: []});
  worker.postMessage({module: module, memory: memory});

  let instance = new WebAssembly.Instance(module, {mod: {mem: memory}});

  // Make sure the worker thread has entered the loop.
  while (instance.exports.getter(sync_index) != sync_value) {}

  memory.grow(final_size - initial_size);

  assertEquals(final_size, worker.getMessage());
})();
