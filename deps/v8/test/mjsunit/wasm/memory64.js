// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// We use standard JavaScript doubles to represent bytes and offsets. They offer
// enough precision (53 bits) for every allowed memory size.

const GB = 1024 * 1024 * 1024;
// The current limit is 16GB. Adapt this test if this changes.
const max_num_pages = 16 * GB / kPageSize;

function BasicMemory64Tests(num_pages, use_atomic_ops) {
  const num_bytes = num_pages * kPageSize;
  print(`Testing ${num_bytes} bytes (${num_pages} pages) on ${
      use_atomic_ops ? '' : 'non-'}atomic memory`);

  let builder = new WasmModuleBuilder();
  builder.addMemory64(num_pages, num_pages);
  builder.exportMemoryAs('memory');

  // A memory operation with alignment (2) and offset (0).
  let op = (non_atomic, atomic) => use_atomic_ops ?
      [kAtomicPrefix, atomic, 2, 0] :
      [non_atomic, 2, 0];
  builder.addFunction('load', makeSig([kWasmF64], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,                           // local.get 0
        kExprI64UConvertF64,                        // i64.uconvert_sat.f64
        ...op(kExprI32LoadMem, kExprI32AtomicLoad)  // load
      ])
      .exportFunc();
  builder.addFunction('store', makeSig([kWasmF64, kWasmI32], []))
      .addBody([
        kExprLocalGet, 0,                             // local.get 0
        kExprI64UConvertF64,                          // i64.uconvert_sat.f64
        kExprLocalGet, 1,                             // local.get 1
        ...op(kExprI32StoreMem, kExprI32AtomicStore)  // store
      ])
      .exportFunc();

  let module = builder.instantiate();
  let memory = module.exports.memory;
  let load = module.exports.load;
  let store = module.exports.store;

  assertEquals(num_bytes, memory.buffer.byteLength);
  // Test that we can create a TypedArray from that large buffer.
  let array = new Int8Array(memory.buffer);
  assertEquals(num_bytes, array.length);

  const GB = Math.pow(2, 30);
  let unalignedAndOobTrap =
    use_atomic_ops ? kTrapUnalignedAccess : kTrapMemOutOfBounds;
  assertEquals(0, load(num_bytes - 4));
  assertTraps(kTrapMemOutOfBounds, () => load(num_bytes));
  assertTraps(unalignedAndOobTrap, () => load(num_bytes - 3));
  assertTraps(kTrapMemOutOfBounds, () => load(num_bytes - 4 + 4 * GB));
  assertTraps(kTrapMemOutOfBounds, () => store(num_bytes));
  assertTraps(unalignedAndOobTrap, () => store(num_bytes - 3));
  assertTraps(kTrapMemOutOfBounds, () => store(num_bytes - 4 + 4 * GB));
  if (use_atomic_ops) {
    assertTraps(kTrapUnalignedAccess, () => load(num_bytes - 7));
    assertTraps(kTrapUnalignedAccess, () => store(num_bytes - 7));
  }

  store(num_bytes - 4, 0x12345678);
  assertEquals(0x12345678, load(num_bytes - 4));

  let kStoreOffset = use_atomic_ops ? 40 : 27;
  store(kStoreOffset, 11);
  assertEquals(11, load(kStoreOffset));

  // Now check some interesting positions, plus 100 random positions.
  const positions = [
    // Nothing at the beginning.
    0, 1,
    // Check positions around the store offset.
    kStoreOffset - 1, kStoreOffset, kStoreOffset + 1,
    // Check the end.
    num_bytes - 5, num_bytes - 4, num_bytes - 3, num_bytes - 2, num_bytes - 1,
    // Check positions at the end, truncated to 32 bit (might be
    // redundant).
    (num_bytes - 5) >>> 0, (num_bytes - 4) >>> 0, (num_bytes - 3) >>> 0,
    (num_bytes - 2) >>> 0, (num_bytes - 1) >>> 0
  ];
  const random_positions =
      Array.from({length: 100}, () => Math.floor(Math.random() * num_bytes));
  for (let position of positions.concat(random_positions)) {
    let expected = 0;
    if (position == kStoreOffset) {
      expected = 11;
    } else if (num_bytes - position <= 4) {
      expected = [0x12, 0x34, 0x56, 0x78][num_bytes - position - 1];
    }
    let value = new Int8Array(memory.buffer, position, 1)[0];
    assertEquals(expected, value);
  }
}

function allowOOM(fn) {
  try {
    fn();
  } catch (e) {
    const is_oom =
        (e instanceof RangeError) && e.message.includes('Out of memory');
    if (!is_oom) throw e;
  }
}

(function TestSmallMemory() {
  print(arguments.callee.name);
  BasicMemory64Tests(4);
})();

(function Test3GBMemory() {
  print(arguments.callee.name);
  let num_pages = 3 * GB / kPageSize;
  // This test can fail if 3GB of memory cannot be allocated.
  allowOOM(() => BasicMemory64Tests(num_pages));
})();

(function Test5GBMemory() {
  print(arguments.callee.name);
  let num_pages = 5 * GB / kPageSize;
  // This test can fail if 5GB of memory cannot be allocated.
  allowOOM(() => BasicMemory64Tests(num_pages));
})();

(function TestMaxMem64Size() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory64(max_num_pages);

  assertTrue(WebAssembly.validate(builder.toBuffer()));
  builder.toModule();

  // This test can fail if 16GB of memory cannot be allocated.
  allowOOM(() => BasicMemory64Tests(max_num_pages));
})();

(function TestTooBigDeclaredInitial() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory64(max_num_pages + 1);

  assertFalse(WebAssembly.validate(builder.toBuffer()));
  assertThrows(
      () => builder.toModule(), WebAssembly.CompileError,
      'WebAssembly.Module(): initial memory size (262145 pages) is larger ' +
          'than implementation limit (262144 pages) @+12');
})();

(function TestTooBigDeclaredMaximum() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory64(1, max_num_pages + 1);

  assertFalse(WebAssembly.validate(builder.toBuffer()));
  assertThrows(
      () => builder.toModule(), WebAssembly.CompileError,
      'WebAssembly.Module(): maximum memory size (262145 pages) is larger ' +
          'than implementation limit (262144 pages) @+13');
})();

(function TestGrow64() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory64(1, 10, false);

  builder.addFunction('grow', makeSig([kWasmI64], [kWasmI64]))
      .addBody([
        kExprLocalGet, 0,    // local.get 0
        kExprMemoryGrow, 0,  // memory.grow 0
      ])
      .exportFunc();

  let instance = builder.instantiate();

  assertEquals(1n, instance.exports.grow(2n));
  assertEquals(3n, instance.exports.grow(1n));
  assertEquals(-1n, instance.exports.grow(-1n));
  assertEquals(-1n, instance.exports.grow(1n << 31n));
  assertEquals(-1n, instance.exports.grow(1n << 32n));
  assertEquals(-1n, instance.exports.grow(1n << 33n));
  assertEquals(-1n, instance.exports.grow(1n << 63n));
  assertEquals(-1n, instance.exports.grow(7n));  // Above the maximum of 10.
  assertEquals(4n, instance.exports.grow(6n));   // Just at the maximum of 10.
})();

(function TestGrow64_ToMemory() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory64(1, 10);
  builder.exportMemoryAs('memory');

  // Grow memory and store the result in memory for inspection from JS.
  builder.addFunction('grow', makeSig([kWasmI64], []))
      .addBody([
        kExprI64Const, 0,       // i64.const (offset for result)
        kExprLocalGet, 0,       // local.get 0
        kExprMemoryGrow, 0,     // memory.grow 0
        kExprI64StoreMem, 3, 0  // store result to memory
      ])
      .exportFunc();

  let instance = builder.instantiate();
  function grow(arg) {
    instance.exports.grow(arg);
    let i64_arr = new BigInt64Array(instance.exports.memory.buffer, 0, 1);
    return i64_arr[0];
  }

  assertEquals(1n, grow(2n));
  assertEquals(3n, grow(1n));
  assertEquals(-1n, grow(-1n));
  assertEquals(-1n, grow(1n << 31n));
  assertEquals(-1n, grow(1n << 32n));
  assertEquals(-1n, grow(1n << 33n));
  assertEquals(-1n, grow(1n << 63n));
  assertEquals(-1n, grow(7n));  // Above the maximum of 10.
  assertEquals(4n, grow(6n));   // Just at the maximum of 10.
})();

(function TestGrow64_Above4GB() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let max_pages = 5 * GB / kPageSize;
  builder.addMemory64(1, max_pages);
  builder.exportMemoryAs('memory');

  builder.addFunction('grow', makeSig([kWasmI64], [kWasmI64]))
      .addBody([
        kExprLocalGet, 0,    // local.get 0
        kExprMemoryGrow, 0,  // memory.grow 0
      ])
      .exportFunc();

  let instance = builder.instantiate();

  // Grow from 1 to 3 pages.
  assertEquals(1n, instance.exports.grow(2n));
  // Grow from 3 to {max_pages - 1} pages.
  // This step can fail. We have to allow this, even though it weakens this test
  // (we do not know if we failed because of OOM or because of a wrong
  // engine-internal limit of 4GB).
  let grow_big_result = instance.exports.grow(BigInt(max_pages) - 4n);
  if (grow_big_result == -1) return;
  assertEquals(3n, grow_big_result);
  // Cannot grow by 2 pages.
  assertEquals(-1n, instance.exports.grow(2n));
  // Cannot grow by 2^32 pages.
  assertEquals(-1n, instance.exports.grow(1n << 32n));
  // Grow by one more page to the maximum.
  grow_big_result = instance.exports.grow(1n);
  if (grow_big_result == -1) return;
  assertEquals(BigInt(max_pages) - 1n, grow_big_result);
  // Cannot grow further.
  assertEquals(-1n, instance.exports.grow(1n));
})();

(function TestBulkMemoryOperations() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  const kMemSizeInPages = 10;
  const kMemSize = kMemSizeInPages * kPageSize;
  builder.addMemory64(kMemSizeInPages, kMemSizeInPages);
  const kSegmentSize = 1024;
  // Build a data segment with values [0, kSegmentSize-1].
  const segment = Array.from({length: kSegmentSize}, (_, idx) => idx)
  builder.addPassiveDataSegment(segment);
  builder.exportMemoryAs('memory');

  builder.addFunction('fill', makeSig([kWasmI64, kWasmI32, kWasmI64], []))
      .addBody([
        kExprLocalGet, 0,                   // local.get 0 (dst)
        kExprLocalGet, 1,                   // local.get 1 (value)
        kExprLocalGet, 2,                   // local.get 2 (size)
        kNumericPrefix, kExprMemoryFill, 0  // memory.fill mem=0
      ])
      .exportFunc();

  builder.addFunction('copy', makeSig([kWasmI64, kWasmI64, kWasmI64], []))
      .addBody([
        kExprLocalGet, 0,                      // local.get 0 (dst)
        kExprLocalGet, 1,                      // local.get 1 (src)
        kExprLocalGet, 2,                      // local.get 2 (size)
        kNumericPrefix, kExprMemoryCopy, 0, 0  // memory.copy srcmem=0 dstmem=0
      ])
      .exportFunc();

  builder.addFunction('init', makeSig([kWasmI64, kWasmI32, kWasmI32], []))
      .addBody([
        kExprLocalGet, 0,                      // local.get 0 (dst)
        kExprLocalGet, 1,                      // local.get 1 (offset)
        kExprLocalGet, 2,                      // local.get 2 (size)
        kNumericPrefix, kExprMemoryInit, 0, 0  // memory.init seg=0 mem=0
      ])
      .exportFunc();

  let instance = builder.instantiate();
  let fill = instance.exports.fill;
  let copy = instance.exports.copy;
  let init = instance.exports.init;
  // {memory(offset,size)} extracts the memory at [offset, offset+size)] into an
  // Array.
  let memory = (offset, size) => Array.from(new Uint8Array(
      instance.exports.memory.buffer.slice(offset, offset + size)));

  // Empty init (size=0).
  init(0n, 0, 0);
  assertEquals([0, 0], memory(0, 2));
  // Init memory[5..7] with [10..12].
  init(5n, 10, 3);
  assertEquals([0, 0, 10, 11, 12, 0, 0], memory(3, 7));
  // Init the end of memory ([kMemSize-2, kMemSize-1]) with [20, 21].
  init(BigInt(kMemSize-2), 20, 2);
  assertEquals([0, 0, 20, 21], memory(kMemSize - 4, 4));
  // Writing slightly OOB.
  assertTraps(kTrapMemOutOfBounds, () => init(BigInt(kMemSize-2), 20, 3));
  // Writing OOB, but the low 32-bit are in-bound.
  assertTraps(kTrapMemOutOfBounds, () => init(1n << 32n, 0, 0));
  // OOB even though size == 0.
  assertTraps(kTrapMemOutOfBounds, () => init(-1n, 0, 0));
  // More OOB.
  assertTraps(kTrapMemOutOfBounds, () => init(-1n, 0, 1));
  assertTraps(kTrapMemOutOfBounds, () => init(1n << 62n, 0, 1));
  assertTraps(kTrapMemOutOfBounds, () => init(1n << 63n, 0, 1));

  // Empty copy (size=0).
  copy(0n, 0n, 0n);
  // Copy memory[5..7] (containing [10..12]) to [3..5].
  copy(3n, 5n, 3n);
  assertEquals([0, 0, 0, 10, 11, 12, 11, 12, 0], memory(0, 9));
  // Copy to the end of memory ([kMemSize-2, kMemSize-1]).
  copy(BigInt(kMemSize-2), 3n, 2n);
  assertEquals([0, 0, 10, 11], memory(kMemSize - 4, 4));
  // Writing slightly OOB.
  assertTraps(kTrapMemOutOfBounds, () => copy(BigInt(kMemSize-2), 0n, 3n));
  // Writing OOB, but the low 32-bit are in-bound.
  assertTraps(kTrapMemOutOfBounds, () => copy(1n << 32n, 0n, 1n));
  assertTraps(kTrapMemOutOfBounds, () => copy(0n, 0n, 1n << 32n));
  // OOB even though size == 0.
  assertTraps(kTrapMemOutOfBounds, () => copy(-1n, 0n, 0n));
  // More OOB.
  assertTraps(kTrapMemOutOfBounds, () => copy(-1n, 0n, 1n));
  assertTraps(kTrapMemOutOfBounds, () => copy(1n << 62n, 0n, 1n));
  assertTraps(kTrapMemOutOfBounds, () => copy(1n << 63n, 0n, 1n));

  // Empty fill (size=0).
  fill(0n, 0, 0n);
  // Fill memory[15..17] with 3s.
  fill(15n, 3, 3n);
  assertEquals([0, 3, 3, 3, 0], memory(14, 5));
  // Fill the end of memory ([kMemSize-2, kMemSize-1]) with 7s.
  fill(BigInt(kMemSize-2), 7, 2n);
  assertEquals([0, 0, 7, 7], memory(kMemSize - 4, 4));
  // Writing slightly OOB.
  assertTraps(kTrapMemOutOfBounds, () => fill(BigInt(kMemSize-2), 0, 3n));
  // Writing OOB, but the low 32-bit are in-bound.
  assertTraps(kTrapMemOutOfBounds, () => fill(1n << 32n, 0, 1n));
  assertTraps(kTrapMemOutOfBounds, () => fill(0n, 0, 1n << 32n));
  // OOB even though size == 0.
  assertTraps(kTrapMemOutOfBounds, () => fill(-1n, 0, 0n));
  // More OOB.
  assertTraps(kTrapMemOutOfBounds, () => fill(-1n, 0, 1n));
  assertTraps(kTrapMemOutOfBounds, () => fill(1n << 62n, 0, 1n));
  assertTraps(kTrapMemOutOfBounds, () => fill(1n << 63n, 0, 1n));
})();

(function TestBulkMemoryConstOperations() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  const kMemSizeInPages = 10;
  builder.addMemory64(kMemSizeInPages, kMemSizeInPages);
  const kSegmentSize = 1024;
  // Build a data segment with values [0, kSegmentSize-1].
  const segment = Array.from({length: kSegmentSize}, (_, idx) => idx)
  builder.addPassiveDataSegment(segment);
  builder.exportMemoryAs('memory');

  builder.addFunction('fill', makeSig([kWasmI32, kWasmI64], []))
      .addBody([
        kExprI64Const, 15,                  // i64.const 15
        kExprLocalGet, 0,                   // local.get 0 (value)
        kExprLocalGet, 1,                   // local.get 1 (size)
        kNumericPrefix, kExprMemoryFill, 0  // memory.fill mem=0
      ])
      .exportFunc();

  builder.addFunction('init', makeSig([kWasmI32, kWasmI32], []))
      .addBody([
        kExprI64Const, 5,                      // i64.const 5
        kExprLocalGet, 0,                      // local.get 0 (offset)
        kExprLocalGet, 1,                      // local.get 1 (size)
        kNumericPrefix, kExprMemoryInit, 0, 0  // memory.init seg=0 mem=0
      ])
      .exportFunc();

  let instance = builder.instantiate();
  let fill = instance.exports.fill;
  let init = instance.exports.init;
  // {memory(offset,size)} extracts the memory at [offset, offset+size)] into an
  // Array.
  let memory = (offset, size) => Array.from(new Uint8Array(
      instance.exports.memory.buffer.slice(offset, offset + size)));

  // Init memory[5..7] with [10..12].
  init(10, 3);
  assertEquals([0, 0, 10, 11, 12, 0, 0], memory(3, 7));

  // Fill memory[15..17] with 3s.
  fill(3, 3n);
  assertEquals([0, 3, 3, 3, 0], memory(14, 5));
})();

(function TestMemory64SharedBasic() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory64(1, 10, true);
  builder.exportMemoryAs('memory');
  builder.addFunction('load', makeSig([kWasmI64], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,       // local.get 0
        kExprI32LoadMem, 0, 0,  // i32.load_mem align=1 offset=0
      ])
      .exportFunc();
  let instance = builder.instantiate();

  assertTrue(instance.exports.memory instanceof WebAssembly.Memory);
  assertTrue(instance.exports.memory.buffer instanceof SharedArrayBuffer);
  assertEquals(0, instance.exports.load(0n));
})();

(function TestMemory64SharedBetweenWorkers() {
  print(arguments.callee.name);
  let shared_mem64 = new WebAssembly.Memory(
      {initial: 1, maximum: 10, shared: true, index: 'i64'});

  let builder = new WasmModuleBuilder();
  builder.addImportedMemory('imp', 'mem', 1, 10, true, true);

  builder.addFunction('grow', makeSig([kWasmI64], [kWasmI64]))
      .addBody([
        kExprLocalGet, 0,    // local.get 0
        kExprMemoryGrow, 0,  // memory.grow 0
      ])
      .exportFunc();
  builder.addFunction('load', makeSig([kWasmI64], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,       // local.get 0
        kExprI32LoadMem, 0, 0,  // i32.load_mem align=1 offset=0
      ])
      .exportFunc();
  builder.addFunction('store', makeSig([kWasmI64, kWasmI32], []))
      .addBody([
        kExprLocalGet, 0,        // local.get 0
        kExprLocalGet, 1,        // local.get 1
        kExprI32StoreMem, 0, 0,  // i32.store_mem align=1 offset=0
      ])
      .exportFunc();

  let module = builder.toModule();
  let instance = new WebAssembly.Instance(module, {imp: {mem: shared_mem64}});

  assertEquals(1n, instance.exports.grow(2n));
  assertEquals(3n, instance.exports.grow(1n));
  const kOffset1 = 47n;
  const kOffset2 = 128n;
  const kValue = 21;
  assertEquals(0, instance.exports.load(kOffset1));
  instance.exports.store(kOffset1, kValue);
  assertEquals(kValue, instance.exports.load(kOffset1));
  let worker = new Worker(function() {
    onmessage = function([mem, module]) {
      function workerAssert(condition, message) {
        if (!condition) postMessage(`Check failed: ${message}`);
      }

      function workerAssertEquals(expected, actual, message) {
        if (expected != actual) {
          postMessage(`Check failed (${message}): ${expected} != ${actual}`);
        }
      }

      const kOffset1 = 47n;
      const kOffset2 = 128n;
      const kValue = 21;
      workerAssert(mem instanceof WebAssembly.Memory, 'Wasm memory');
      workerAssert(mem.buffer instanceof SharedArrayBuffer);
      workerAssertEquals(4, mem.grow(1), 'grow');
      let instance = new WebAssembly.Instance(module, {imp: {mem: mem}});
      let exports = instance.exports;
      workerAssertEquals(kValue, exports.load(kOffset1), 'load 1');
      workerAssertEquals(0, exports.load(kOffset2), 'load 2');
      exports.store(kOffset2, kValue);
      workerAssertEquals(kValue, exports.load(kOffset2), 'load 3');
      postMessage('OK');
    }
  }, {type: 'function'});
  worker.postMessage([shared_mem64, module]);
  assertEquals('OK', worker.getMessage());
  assertEquals(kValue, instance.exports.load(kOffset2));
  assertEquals(5n, instance.exports.grow(1n));
})();

(function TestAtomics_SmallMemory() {
  print(arguments.callee.name);
  BasicMemory64Tests(4, true);
})();

(function TestAtomics_5GB() {
  print(arguments.callee.name);
  let num_pages = 5 * GB / kPageSize;
  // This test can fail if 5GB of memory cannot be allocated.
  allowOOM(() => BasicMemory64Tests(num_pages, true));
})();

(function Test64BitOffsetOn32BitMemory() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);

  builder.addFunction('load', makeSig([kWasmI32], [kWasmI32]))
      .addBody([
        // local.get 0
        kExprLocalGet, 0,
        // i32.load align=0 offset=2^32+2
        kExprI32LoadMem, 0, ...wasmSignedLeb64(Math.pow(2, 32) + 2),
      ])
      .exportFunc();

  // An offset outside the 32-bit range should not validate.
  assertFalse(WebAssembly.validate(builder.toBuffer()));
})();

(function Test64BitOffsetOn64BitMemory() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addMemory64(1, 1);

  builder.addFunction('load', makeSig([kWasmI64], [kWasmI32]))
      .addBody([
        // local.get 0
        kExprLocalGet, 0,
        // i32.load align=0 offset=2^32+2
        kExprI32LoadMem, 0, ...wasmSignedLeb64(Math.pow(2, 32) + 2),
      ])
      .exportFunc();

  // Instantiation works, this should throw at runtime.
  let instance = builder.instantiate();
  let load = instance.exports.load;

  assertTraps(kTrapMemOutOfBounds, () => load(0n));
})();

(function TestImportMemory64() {
  print(arguments.callee.name);
  const builder1 = new WasmModuleBuilder();
  builder1.addMemory64(1, 1);
  builder1.exportMemoryAs('mem64');
  const instance1 = builder1.instantiate();
  const {mem64} = instance1.exports;

  let builder2 = new WasmModuleBuilder();
  builder2.addImportedMemory(
      'imp', 'mem', 1, 1, /* shared */ false, /* memory64 */ true);
  builder2.instantiate({imp: {mem: mem64}});
})();

(function TestImportMemory64AsMemory32() {
  print(arguments.callee.name);
  const builder1 = new WasmModuleBuilder();
  builder1.addMemory64(1, 1);
  builder1.exportMemoryAs('mem64');
  const instance1 = builder1.instantiate();
  const {mem64} = instance1.exports;

  let builder2 = new WasmModuleBuilder();
  builder2.addImportedMemory('imp', 'mem');
  assertThrows(
      () => builder2.instantiate({imp: {mem: mem64}}), WebAssembly.LinkError,
      'WebAssembly.Instance(): cannot import memory64 as memory32');
})();

(function TestImportMemory32AsMemory64() {
  print(arguments.callee.name);
  const builder1 = new WasmModuleBuilder();
  builder1.addMemory(1, 1);
  builder1.exportMemoryAs('mem32');
  const instance1 = builder1.instantiate();
  const {mem32} = instance1.exports;

  let builder2 = new WasmModuleBuilder();
  builder2.addImportedMemory(
      'imp', 'mem', 1, 1, /* shared */ false, /* memory64 */ true);
  assertThrows(
      () => builder2.instantiate({imp: {mem: mem32}}), WebAssembly.LinkError,
      'WebAssembly.Instance(): cannot import memory32 as memory64');
})();

function InstantiatingWorkerCode() {
  function workerAssert(condition, message) {
    if (!condition) postMessage(`Check failed: ${message}`);
  }

  onmessage = function([mem, module]) {
    workerAssert(mem instanceof WebAssembly.Memory, 'Wasm memory');
    workerAssert(mem.buffer instanceof SharedArrayBuffer, 'SAB');
    try {
      new WebAssembly.Instance(module, {imp: {mem: mem}});
      postMessage('Instantiation succeeded');
    } catch (e) {
      postMessage(`Exception: ${e}`);
    }
  };
}

(function TestImportMemory64AsMemory32InWorker() {
  print(arguments.callee.name);
  const builder1 = new WasmModuleBuilder();
  builder1.addMemory64(1, 1, /* shared */ true);
  builder1.exportMemoryAs('mem64');
  const instance1 = builder1.instantiate();
  const {mem64} = instance1.exports;

  let builder2 = new WasmModuleBuilder();
  builder2.addImportedMemory('imp', 'mem');
  let module2 = builder2.toModule();

  let worker = new Worker(InstantiatingWorkerCode, {type: 'function'});
  worker.postMessage([mem64, module2]);
  assertEquals(
      'Exception: LinkError: WebAssembly.Instance(): ' +
          'cannot import memory64 as memory32',
      worker.getMessage());
})();

(function TestImportMemory32AsMemory64InWorker() {
  print(arguments.callee.name);
  const builder1 = new WasmModuleBuilder();
  builder1.addMemory(1, 1, /* shared */ true);
  builder1.exportMemoryAs('mem32');
  const instance1 = builder1.instantiate();
  const {mem32} = instance1.exports;

  let builder2 = new WasmModuleBuilder();
  builder2.addImportedMemory(
      'imp', 'mem', 1, 1, /* shared */ false, /* memory64 */ true);
  let module2 = builder2.toModule();

  let worker = new Worker(InstantiatingWorkerCode, {type: 'function'});
  worker.postMessage([mem32, module2]);
  assertEquals(
      'Exception: LinkError: WebAssembly.Instance(): ' +
          'cannot import memory32 as memory64',
      worker.getMessage());
})();
