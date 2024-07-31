// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestPassiveDataSegment() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);
  builder.addPassiveDataSegment([0, 1, 2]);
  builder.addPassiveDataSegment([3, 4]);

  // Should not throw.
  builder.instantiate();
})();

(function TestPassiveElementSegment() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction('f', kSig_v_v).addBody([]);
  builder.setTableBounds(1, 1);
  builder.addPassiveElementSegment([0, 0, 0]);
  builder.addPassiveElementSegment([0, 0]);

  // Should not throw.
  builder.instantiate();
})();

function getMemoryInit(mem, segment_data) {
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory('', 'mem', 0);
  builder.addPassiveDataSegment(segment_data);
  builder.addFunction('init', kSig_v_iii)
      .addBody([
        kExprLocalGet, 0,  // Dest.
        kExprLocalGet, 1,  // Source.
        kExprLocalGet, 2,  // Size in bytes.
        kNumericPrefix, kExprMemoryInit,
        0,  // Data segment index.
        0,  // Memory index.
      ])
      .exportAs('init');
  return builder.instantiate({'': {mem}}).exports.init;
}

(function TestMemoryInitOutOfBoundsGrow() {
  print(arguments.callee.name);
  const mem = new WebAssembly.Memory({initial: 1});
  // Create a data segment that has a length of kPageSize.
  const memoryInit = getMemoryInit(mem, new Array(kPageSize));

  mem.grow(1);

  // Works properly after grow.
  memoryInit(kPageSize, 0, 1000);

  // Traps at new boundary.
  assertTraps(
      kTrapMemOutOfBounds, () => memoryInit(kPageSize + 1, 0, kPageSize));
})();

(function TestMemoryInitOnActiveSegment() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addMemory(1);
  builder.addPassiveDataSegment([1, 2, 3]);
  builder.addActiveDataSegment(0, [kExprI32Const, 0], [4, 5, 6]);
  builder.addFunction('init', kSig_v_i)
      .addBody([
        kExprI32Const, 0,  // Dest.
        kExprI32Const, 0,  // Source.
        kExprLocalGet, 0,  // Size in bytes.
        kNumericPrefix, kExprMemoryInit,
        1,  // Data segment index.
        0,  // Memory index.
      ])
      .exportAs('init');

  // Instantiation succeeds, because using memory.init with an active segment
  // is a trap, not a validation error.
  const instance = builder.instantiate();

  // Initialization succeeds, because source address and size are 0
  // which is valid on a dropped segment.
  instance.exports.init(0);

  // Initialization fails, because the segment is implicitly dropped.
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.init(1));
})();

(function TestDataDropOnActiveSegment() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addMemory(1);
  builder.addPassiveDataSegment([1, 2, 3]);
  builder.addActiveDataSegment(0, [kExprI32Const, 0], [4, 5, 6]);
  builder.addFunction('drop', kSig_v_v)
      .addBody([
        kNumericPrefix, kExprDataDrop,
        1,  // Data segment index.
      ])
      .exportAs('drop');

  const instance = builder.instantiate();
  // Drop on passive segment is equivalent to double drop which is allowed.
  instance.exports.drop();
})();

function getMemoryCopy(mem) {
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory("", "mem", 0);
  builder.addFunction("copy", kSig_v_iii).addBody([
    kExprLocalGet, 0,  // Dest.
    kExprLocalGet, 1,  // Source.
    kExprLocalGet, 2,  // Size in bytes.
    kNumericPrefix, kExprMemoryCopy, 0, 0,
  ]).exportAs("copy");
  return builder.instantiate({'': {mem}}).exports.copy;
}

(function TestMemoryCopyOutOfBoundsGrow() {
  print(arguments.callee.name);
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryCopy = getMemoryCopy(mem);

  mem.grow(1);

  // Works properly after grow.
  memoryCopy(0, kPageSize, 1000);

  // Traps at new boundary.
  assertTraps(
      kTrapMemOutOfBounds, () => memoryCopy(0, kPageSize + 1, kPageSize));
})();

function getMemoryFill(mem) {
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory("", "mem", 0);
  builder.addFunction("fill", kSig_v_iii).addBody([
    kExprLocalGet, 0,  // Dest.
    kExprLocalGet, 1,  // Byte value.
    kExprLocalGet, 2,  // Size.
    kNumericPrefix, kExprMemoryFill, 0,
  ]).exportAs("fill");
  return builder.instantiate({'': {mem}}).exports.fill;
}

(function TestMemoryFillOutOfBoundsGrow() {
  print(arguments.callee.name);
  const mem = new WebAssembly.Memory({initial: 1});
  const memoryFill = getMemoryFill(mem);
  const v = 123;

  mem.grow(1);

  // Works properly after grow.
  memoryFill(kPageSize, v, 1000);

  // Traps at new boundary.
  assertTraps(
      kTrapMemOutOfBounds, () => memoryFill(kPageSize + 1, v, kPageSize));
})();

(function TestElemDropActive() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.setTableBounds(5, 5);
  builder.addActiveElementSegment(0, wasmI32Const(0), [0, 0, 0]);
  builder.addFunction('drop', kSig_v_v)
      .addBody([
        kNumericPrefix, kExprElemDrop,
        0,  // Element segment index.
      ])
      .exportAs('drop');

  const instance = builder.instantiate();
  // Segment already got dropped after initialization and is therefore
  // not active anymore.
  instance.exports.drop();
})();

(function TestLazyDataSegmentBoundsCheck() {
  print(arguments.callee.name);
  const memory = new WebAssembly.Memory({initial: 1});
  const view = new Uint8Array(memory.buffer);
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory('m', 'memory', 1);
  builder.addActiveDataSegment(0, wasmI32Const(kPageSize - 1), [42, 42]);
  builder.addActiveDataSegment(0, [kExprI32Const, 0], [111, 111]);

  assertEquals(0, view[kPageSize - 1]);

  // Instantiation fails, memory remains unmodified.
  assertThrows(
      () => builder.instantiate({m: {memory}}), WebAssembly.RuntimeError);

  assertEquals(0, view[kPageSize - 1]);
  // The second segment is not initialized.
  assertEquals(0, view[0]);
})();

(function TestLazyDataAndElementSegments() {
  print(arguments.callee.name);
  const table = new WebAssembly.Table({initial: 1, element: 'anyfunc'});
  const memory = new WebAssembly.Memory({initial: 1});
  const view = new Uint8Array(memory.buffer);
  const builder = new WasmModuleBuilder();

  builder.addImportedMemory('m', 'memory', 1);
  builder.addImportedTable('m', 'table', 1);
  const f = builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 42]);

  const tableIndex = 0;
  builder.addActiveElementSegment(
      tableIndex,
      wasmI32Const(0),
      [f.index, f.index]);
  builder.addActiveDataSegment(0, [kExprI32Const, 0], [42]);

  // Instantiation fails, but still modifies the table. The memory is not
  // modified, since data segments are initialized after element segments.
  assertThrows(
      () => builder.instantiate({m: {memory, table}}),
      WebAssembly.RuntimeError);

  assertEquals(0, view[0]);
})();

(function TestPassiveDataSegmentNoMemory() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addPassiveDataSegment([0, 1, 2]);

  // Should not throw.
  builder.instantiate();
})();

(function TestPassiveElementSegmentNoMemory() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction('f', kSig_v_v).addBody([]);
  builder.addPassiveElementSegment([0, 0, 0]);

  // Should not throw.
  builder.instantiate();
})();

(function TestIllegalNumericOpcode() {
  // Regression test for https://crbug.com/1382816.
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_v_v).addBody([kNumericPrefix, 0x90, 0x0f]);
  assertEquals(false, WebAssembly.validate(builder.toBuffer()));
  assertThrows(
      () => builder.toModule(), WebAssembly.CompileError,
      /invalid numeric opcode: 0xfc790/);
})();
