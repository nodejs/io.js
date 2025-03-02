// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// Add a {loadN} and {storeN} function for memory N.
function addLoadAndStoreFunctions(builder, mem_index) {
  builder.addFunction(`load${mem_index}`, kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0x40, mem_index, 0])
      .exportFunc();
  builder.addFunction(`store${mem_index}`, kSig_v_ii)
      .addBody([
        kExprLocalGet, 1, kExprLocalGet, 0, kExprI32StoreMem, 0x40, mem_index, 0
      ])
      .exportFunc();
}

// Add a {growN} and {sizeN} function for memory N.
function addGrowAndSizeFunctions(builder, mem_index) {
  builder.addFunction(`grow${mem_index}`, kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprMemoryGrow, mem_index])
      .exportFunc();
  builder.addFunction(`size${mem_index}`, kSig_i_v)
      .addBody([kExprMemorySize, mem_index])
      .exportFunc();
}

// Add a {initN_M} function for memory N and data segment M.
function addMemoryInitFunction(builder, data_segment, mem_index) {
  builder.addFunction(`init${mem_index}_${data_segment}`, kSig_v_iii)
      .addBody([
        kExprLocalGet, 0,  // dst
        kExprLocalGet, 1,  // offset
        kExprLocalGet, 2,  // size
        kNumericPrefix, kExprMemoryInit, data_segment, mem_index
      ])
      .exportFunc();
}

// Add a {fillN} function for memory N.
function addMemoryFillFunction(builder, mem_index) {
  builder.addFunction(`fill${mem_index}`, kSig_v_iii)
      .addBody([
        kExprLocalGet, 0,  // dst
        kExprLocalGet, 1,  // value
        kExprLocalGet, 2,  // size
        kNumericPrefix, kExprMemoryFill, mem_index
      ])
      .exportFunc();
}

// Add a {copyN_M} function for copying from memory M to memory N.
function addMemoryCopyFunction(builder, mem_dst_index, mem_src_index) {
  builder.addFunction(`copy${mem_dst_index}_${mem_src_index}`, kSig_v_iii)
      .addBody([
        kExprLocalGet, 0,  // dst
        kExprLocalGet, 1,  // src
        kExprLocalGet, 2,  // size
        kNumericPrefix, kExprMemoryCopy, mem_dst_index, mem_src_index
      ])
      .exportFunc();
}

// Helper to test that two memories can be accessed independently.
function testTwoMemories(instance, mem0_size, mem1_size) {
  const {load0, load1, store0, store1} = instance.exports;

  assertEquals(0, load0(0));
  assertEquals(0, load1(0));

  store0(47, 0);
  assertEquals(47, load0(0));
  assertEquals(0, load1(0));

  store1(11, 0);
  assertEquals(47, load0(0));
  assertEquals(11, load1(0));

  store1(22, 1);
  assertEquals(47, load0(0));
  assertEquals(22, load1(1));
  // The 22 is in the second byte when loading from 0.
  assertEquals(22 * 256 + 11, load1(0));

  const mem0_bytes = mem0_size * kPageSize;
  load0(mem0_bytes - 4);  // should not trap.
  for (const offset of [-3, -1, mem0_bytes - 3, mem0_bytes - 1, mem0_bytes]) {
    assertTraps(kTrapMemOutOfBounds, () => load0(offset));
    assertTraps(kTrapMemOutOfBounds, () => store0(0, offset));
  }

  const mem1_bytes = mem1_size * kPageSize;
  load1(mem1_bytes - 4);  // should not trap.
  for (const offset of [-3, -1, mem1_bytes - 3, mem1_bytes - 1, mem1_bytes]) {
    assertTraps(kTrapMemOutOfBounds, () => load1(offset));
    assertTraps(kTrapMemOutOfBounds, () => store1(0, offset));
  }
}

function assertMemoryEquals(expected, memory) {
  assertInstanceof(memory, WebAssembly.Memory);
  assertInstanceof(expected, Uint8Array);
  const buf = new Uint8Array(memory.buffer);
  // For better output, check the first 50 bytes separately first.
  assertEquals(expected.slice(0, 50), buf.slice(0, 50));
  // Now also check the full memory content.
  assertEquals(expected, buf);
}

(function testBasicMultiMemory() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const mem0_idx = builder.addMemory(1, 1);
  const mem1_idx = builder.addMemory(1, 1);
  addLoadAndStoreFunctions(builder, mem0_idx);
  addLoadAndStoreFunctions(builder, mem1_idx);

  const instance = builder.instantiate();
  testTwoMemories(instance, 1, 1);
})();

(function testMemoryIndexDecodedAsU32() {
  print(arguments.callee.name);
  for (let leb_length = 1; leb_length <= 6; ++leb_length) {
    // Create the array [0x80, 0x80, ..., 0x0] of length `leb_length`. This
    // encodes `0` using `leb_length` bytes.
    const leb = new Array(leb_length).fill(0x80).with(leb_length - 1, 0);
    const builder = new WasmModuleBuilder();
    builder.addMemory(1, 1);
    builder.addFunction('load', kSig_i_i)
        .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0x40, ...leb, 0])
        .exportFunc();
    builder.addFunction('store', kSig_v_ii)
        .addBody([
          kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0x40, ...leb, 0
        ])
        .exportFunc();
    builder.exportMemoryAs('mem');

    if (leb_length == 6) {
      assertThrows(
          () => builder.instantiate(), WebAssembly.CompileError,
          /length overflow while decoding memory index/);
      continue;
    }
    const instance = builder.instantiate();
    assertEquals(0, instance.exports.load(7));
    instance.exports.store(7, 11);
    assertEquals(11, instance.exports.load(7));
    assertEquals(0, instance.exports.load(11));

    const expected_memory = new Uint8Array(kPageSize);
    expected_memory[7] = 11;
    assertMemoryEquals(expected_memory, instance.exports.mem);
  }
})();

(function testImportedAndDeclaredMemories() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory('imp', 'mem0');
  builder.addMemory(1, 1);
  addLoadAndStoreFunctions(builder, 0);
  addLoadAndStoreFunctions(builder, 1);

  const mem0 = new WebAssembly.Memory({initial: 3, maximum: 4});
  const instance = builder.instantiate({imp: {mem0: mem0}});
  testTwoMemories(instance, 3, 1);
})();

(function testTwoImportedMemories() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addImportedMemory('imp', 'mem0');
  builder.addImportedMemory('imp', 'mem1');
  addLoadAndStoreFunctions(builder, 0);
  addLoadAndStoreFunctions(builder, 1);

  const mem0 = new WebAssembly.Memory({initial: 2, maximum: 3});
  const mem1 = new WebAssembly.Memory({initial: 3, maximum: 4});
  const instance = builder.instantiate({imp: {mem0: mem0, mem1: mem1}});
  testTwoMemories(instance, 2, 3);
})();

(function testTwoExportedMemories() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const mem0_idx = builder.addMemory(1, 1);
  const mem1_idx = builder.addMemory(1, 1);
  addLoadAndStoreFunctions(builder, mem0_idx);
  addLoadAndStoreFunctions(builder, mem1_idx);
  builder.exportMemoryAs('mem0', mem0_idx);
  builder.exportMemoryAs('mem1', mem1_idx);

  const instance = builder.instantiate();
  const mem0 = new Uint8Array(instance.exports.mem0.buffer);
  const mem1 = new Uint8Array(instance.exports.mem1.buffer);
  assertEquals(0, mem0[11]);
  assertEquals(0, mem1[11]);

  instance.exports.store0(47, 11);
  assertEquals(47, mem0[11]);
  assertEquals(0, mem1[11]);

  instance.exports.store1(49, 11);
  assertEquals(47, mem0[11]);
  assertEquals(49, mem1[11]);
})();

(function testMultiMemoryDataSegments() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  const mem0_idx = builder.addMemory(1, 1);
  const mem1_idx = builder.addMemory(1, 1);
  addLoadAndStoreFunctions(builder, mem0_idx);
  addLoadAndStoreFunctions(builder, mem1_idx);
  const mem0_offset = 11;
  const mem1_offset = 23;
  builder.addActiveDataSegment(1, [kExprI32Const, mem1_offset], [7, 7]);
  builder.addActiveDataSegment(0, [kExprI32Const, mem0_offset], [9, 9]);
  builder.exportMemoryAs('mem0', 0);
  builder.exportMemoryAs('mem1', 1);

  const instance = builder.instantiate();
  const expected_memory0 = new Uint8Array(kPageSize);
  expected_memory0.set([9, 9], mem0_offset);
  const expected_memory1 = new Uint8Array(kPageSize);
  expected_memory1.set([7, 7], mem1_offset);

  assertMemoryEquals(expected_memory0, instance.exports.mem0);
  assertMemoryEquals(expected_memory1, instance.exports.mem1);
})();

(function testMultiMemoryDataSegmentsOutOfBounds() {
  print(arguments.callee.name);
  // Check that we use the right memory size for the bounds check.
  for (let [mem0_size, mem1_size] of [[1, 2], [2, 1]]) {
    for (let [mem0_offset, mem1_offset] of [[0, 0], [1, 2], [0, 2], [1, 0]]) {
      var builder = new WasmModuleBuilder();
      const mem0_idx = builder.addMemory(mem0_size, mem0_size);
      const mem1_idx = builder.addMemory(mem1_size, mem1_size);
      builder.addActiveDataSegment(
          mem0_idx, wasmI32Const(mem0_offset * kPageSize), [0]);
      builder.addActiveDataSegment(
          mem1_idx, wasmI32Const(mem1_offset * kPageSize), [0]);

      if (mem0_offset < mem0_size && mem1_offset < mem1_size) {
        builder.instantiate();  // should not throw.
        continue;
      }
      const oob = mem0_offset >= mem0_size ? 0 : 1;
      const expected_offset = [mem0_offset, mem1_offset][oob] * kPageSize;
      const expected_mem_size = [mem0_size, mem1_size][oob] * kPageSize;
      const expected_msg =
          `WebAssembly.Instance(): data segment ${oob} is out of bounds ` +
          `(offset ${expected_offset}, length 1, ` +
          `memory size ${expected_mem_size})`;
      assertThrows(
          () => builder.instantiate(), WebAssembly.RuntimeError, expected_msg);
    }
  }
})();

(function testMultiMemoryInit() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  const mem0_idx = builder.addMemory(1, 1);
  const mem1_idx = builder.addMemory(1, 1);
  const mem0_offset = 11;
  const mem1_offset = 23;
  const seg0_init = [5, 4, 3];
  const seg0_id = builder.addPassiveDataSegment(seg0_init);
  const seg1_init = [11, 10, 9, 8, 7];
  const seg1_id = builder.addPassiveDataSegment(seg1_init);
  builder.exportMemoryAs('mem0', mem0_idx);
  builder.exportMemoryAs('mem1', mem1_idx);
  // Initialize memory 1 from data segment 0 and vice versa.
  addMemoryInitFunction(builder, mem0_idx, seg1_id);
  addMemoryInitFunction(builder, mem1_idx, seg0_id);

  const instance = builder.instantiate();
  const expected_memory0 = new Uint8Array(kPageSize);
  const expected_memory1 = new Uint8Array(kPageSize);

  assertMemoryEquals(expected_memory0, instance.exports.mem0);
  assertMemoryEquals(expected_memory1, instance.exports.mem1);

  instance.exports.init0_1(3, 1, 3);
  expected_memory0.set(seg1_init.slice(1, 4), 3);
  assertMemoryEquals(expected_memory0, instance.exports.mem0);
  assertMemoryEquals(expected_memory1, instance.exports.mem1);

  instance.exports.init1_0(17, 0, 2);
  expected_memory1.set(seg0_init.slice(0, 2), 17);
  assertMemoryEquals(expected_memory0, instance.exports.mem0);
  assertMemoryEquals(expected_memory1, instance.exports.mem1);

  assertTraps(kTrapMemOutOfBounds, () => instance.exports.init0_1(0, 5, 1));
  assertTraps(kTrapMemOutOfBounds, () => instance.exports.init1_0(0, 3, 1));
})();

(function testMultiMemoryFill() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  const mem0_idx = builder.addMemory(1, 1);
  const mem1_idx = builder.addMemory(2, 2);
  builder.exportMemoryAs('mem0', mem0_idx);
  builder.exportMemoryAs('mem1', mem1_idx);
  addMemoryFillFunction(builder, mem0_idx);
  addMemoryFillFunction(builder, mem1_idx);

  const instance = builder.instantiate();
  const expected_memory0 = new Uint8Array(kPageSize);
  const expected_memory1 = new Uint8Array(2 * kPageSize);

  assertMemoryEquals(expected_memory0, instance.exports.mem0);
  assertMemoryEquals(expected_memory1, instance.exports.mem1);

  // Fill [3..4] in mem0 with value 7.
  instance.exports.fill0(3, 7, 2);  // dst, value, size
  expected_memory0.fill(7, 3, 5);   // value, start, end
  assertMemoryEquals(expected_memory0, instance.exports.mem0);
  assertMemoryEquals(expected_memory1, instance.exports.mem1);

  // Fill [4..11] in mem0 with value 17.
  instance.exports.fill1(4, 17, 8);  // dst, value, size
  expected_memory1.fill(17, 4, 12);  // value, start, end
  assertMemoryEquals(expected_memory0, instance.exports.mem0);
  assertMemoryEquals(expected_memory1, instance.exports.mem1);

  // mem0 has size 1.
  instance.exports.fill0(kPageSize - 1, 1, 1);
  assertTraps(
      kTrapMemOutOfBounds, () => instance.exports.fill0(kPageSize - 1, 1, 2));
  assertTraps(
      kTrapMemOutOfBounds, () => instance.exports.fill0(kPageSize, 1, 1));

  // mem1 has size 2.
  instance.exports.fill1(2 * kPageSize - 1, 1, 1);
  assertTraps(
      kTrapMemOutOfBounds,
      () => instance.exports.fill1(2 * kPageSize - 1, 1, 2));
  assertTraps(
      kTrapMemOutOfBounds, () => instance.exports.fill1(2 * kPageSize, 1, 1));
})();

(function testMultiMemoryCopy() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  const mem0_id = 0;
  builder.addMemory(1, 1);
  const mem1_id = 1;
  builder.addMemory(2, 2);
  builder.exportMemoryAs('mem0', mem0_id);
  builder.exportMemoryAs('mem1', mem1_id);
  addMemoryCopyFunction(builder, mem0_id, mem0_id);
  addMemoryCopyFunction(builder, mem0_id, mem1_id);
  addMemoryCopyFunction(builder, mem1_id, mem0_id);
  addMemoryCopyFunction(builder, mem1_id, mem1_id);

  const instance = builder.instantiate();
  const expected_memory0 = new Uint8Array(kPageSize);
  const expected_memory1 = new Uint8Array(2 * kPageSize);

  // Set some initial bytes that we can then copy around.
  new Uint8Array(instance.exports.mem0.buffer).set([1, 2, 3, 4, 5]);
  expected_memory0.set([1, 2, 3, 4, 5], 0);
  assertMemoryEquals(expected_memory0, instance.exports.mem0);

  // Copy [3..5] to [7..9] in mem0.
  instance.exports.copy0_0(7, 3, 3);  // dst, src, size
  expected_memory0.set([1, 2, 3, 4, 5, 0, 0, 4, 5, 0]);
  assertMemoryEquals(expected_memory0, instance.exports.mem0);

  // Copy [3..7] from mem0 to [2..6] in mem1.
  instance.exports.copy1_0(2, 3, 5);  // dst, src, size
  expected_memory1.set([0, 0, 4, 5, 0, 0, 4]);
  assertMemoryEquals(expected_memory0, instance.exports.mem0);
  assertMemoryEquals(expected_memory1, instance.exports.mem1);

  // Copy [3..4] from mem1 to [1..3] in mem0.
  instance.exports.copy0_1(1, 3, 2);  // dst, src, size
  expected_memory0.set([5, 0], 1);
  assertMemoryEquals(expected_memory0, instance.exports.mem0);
  assertMemoryEquals(expected_memory1, instance.exports.mem1);

  // Copy [2..4] to [0..2] in mem1.
  instance.exports.copy1_1(0, 2, 3);  // dst, src, size
  expected_memory1.set([4, 5, 0]);
  assertMemoryEquals(expected_memory0, instance.exports.mem0);
  assertMemoryEquals(expected_memory1, instance.exports.mem1);

  const mem0_size = kPageSize;
  const mem1_size = 2 * kPageSize;
  for ([method, dst_size, src_size] of [
           [instance.exports.copy0_0, mem0_size, mem0_size],
           [instance.exports.copy0_1, mem0_size, mem1_size],
           [instance.exports.copy1_0, mem1_size, mem0_size],
           [instance.exports.copy1_1, mem1_size, mem1_size]]) {
    print(`- ${method}`);
    // Test at the boundary.
    method(dst_size - 1, src_size - 1, 1);
    // Then test one byte further.
    assertTraps(kTrapMemOutOfBounds, () => method(dst_size, src_size - 1, 1));
    assertTraps(kTrapMemOutOfBounds, () => method(dst_size - 1, src_size, 1));
    assertTraps(
        kTrapMemOutOfBounds, () => method(dst_size - 1, src_size - 2, 2));
    assertTraps(
        kTrapMemOutOfBounds, () => method(dst_size - 2, src_size - 1, 2));
  }
})();

(function testGrowMultipleMemories() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  const mem0_idx = builder.addMemory(1, 4);
  const mem1_idx = builder.addMemory(1, 5);
  addGrowAndSizeFunctions(builder, mem0_idx);
  addGrowAndSizeFunctions(builder, mem1_idx);
  builder.exportMemoryAs('mem0', mem0_idx);
  builder.exportMemoryAs('mem1', mem1_idx);
  const instance = builder.instantiate();

  assertEquals(1, instance.exports.grow0(2));
  assertEquals(3, instance.exports.grow0(1));
  assertEquals(4, instance.exports.size0());
  assertEquals(-1, instance.exports.grow0(1));
  assertEquals(4, instance.exports.size0());

  assertEquals(1, instance.exports.grow1(2));
  assertEquals(3, instance.exports.grow1(2));
  assertEquals(5, instance.exports.size1());
  assertEquals(-1, instance.exports.grow1(1));
  assertEquals(5, instance.exports.size1());
})();

(function testGrowDecodesMemoryIndexAsU32() {
  print(arguments.callee.name);
  for (let leb_length = 1; leb_length <= 6; ++leb_length) {
    // Create the array [0x80, 0x80, ..., 0x0] of length `leb_length`. This
    // encodes `0` using `leb_length` bytes.
    const leb = new Array(leb_length).fill(0x80).with(leb_length - 1, 0);
    var builder = new WasmModuleBuilder();
    builder.addMemory(1, 4);
    builder.addFunction('grow', kSig_i_i)
        .addBody([kExprLocalGet, 0, kExprMemoryGrow, ...leb])
        .exportFunc();
    builder.exportMemoryAs('mem', 0);

    if (leb_length == 6) {
      assertThrows(
          () => builder.instantiate(), WebAssembly.CompileError,
          /length overflow while decoding memory index/);
      continue;
    }
    const instance = builder.instantiate();

    assertEquals(1, instance.exports.grow(2));
    assertEquals(-1, instance.exports.grow(2));
    assertEquals(3, instance.exports.grow(1));
  }
})();

(function testMultipleMemoriesInOneFunction() {
  print(arguments.callee.name);
  // Some "interesting" access patterns.
  const mem_indexes_list =
      [[0, 1, 0, 1], [1, 0, 1, 0], [0, 0, 1, 1], [1, 1, 0, 0]];
  const builder = new WasmModuleBuilder();
  const mem0_idx = builder.addMemory(1, 1);
  const mem1_idx = builder.addMemory(1, 1);
  for (let [idx, mem_indexes] of mem_indexes_list.entries()) {
    let sig = makeSig(new Array(4).fill(kWasmI32), [kWasmI32]);
    builder.addFunction(`load${idx}`, sig)
        .addBody([
          kExprLocalGet, 0, kExprI32LoadMem, 0x40, mem_indexes[0], 0,  // load 1
          kExprLocalGet, 1, kExprI32LoadMem, 0x40, mem_indexes[1], 0,  // load 2
          kExprI32Add,                                                 // add
          kExprLocalGet, 2, kExprI32LoadMem, 0x40, mem_indexes[2], 0,  // load 3
          kExprI32Add,                                                 // add
          kExprLocalGet, 3, kExprI32LoadMem, 0x40, mem_indexes[3], 0,  // load 4
          kExprI32Add,                                                 // add
        ])
        .exportFunc();
  }
  builder.exportMemoryAs('mem0', mem0_idx);
  builder.exportMemoryAs('mem1', mem1_idx);

  const instance = builder.instantiate();

  // Create random memory contents.
  let buf0 = new DataView(instance.exports.mem0.buffer);
  let buf1 = new DataView(instance.exports.mem1.buffer);
  for (let i = 0; i < kPageSize; ++i) {
    buf0.setUint8(i, Math.floor(Math.random() * 0xff));
    buf1.setUint8(i, Math.floor(Math.random() * 0xff));
  }

  for (let run = 0; run < 10; ++run) {
    // Return a random index in [0, kPageSize - 3) such that we can read four
    // bytes from there.
    let get_random_offset = () => Math.floor(Math.random() * (kPageSize - 3));
    let inputs = new Array(4).fill(0).map(get_random_offset);
    for (let [func_idx, mem_indexes] of mem_indexes_list.entries()) {
      let expected = 0;
      for (let i = 0; i < 4; ++i) {
        let buf = mem_indexes[i] == 0 ? buf0 : buf1;
        expected += buf.getInt32(inputs[i], true);
      }
      expected >>= 0;  // Truncate to 32 bit.
      assertEquals(expected, instance.exports[`load${func_idx}`](...inputs));
    }
  }
})();

(function testAtomicsOnMultiMemory() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const mem0_idx = builder.addMemory(1, 1, true);
  const mem1_idx = builder.addMemory(2, 2, true);
  builder.exportMemoryAs('mem0', mem0_idx);
  builder.exportMemoryAs('mem1', mem1_idx);

  for (let mem_idx of [mem0_idx, mem1_idx]) {
    builder.addFunction(`load${mem_idx}`, kSig_i_i)
        .addBody([
          kExprLocalGet, 0,                                    // -
          kAtomicPrefix, kExprI32AtomicLoad, 0x42, mem_idx, 0  // -
        ])
        .exportFunc();
    builder.addFunction(`store${mem_idx}`, kSig_v_ii)
        .addBody([
          kExprLocalGet, 0,                                     // -
          kExprLocalGet, 1,                                     // -
          kAtomicPrefix, kExprI32AtomicStore, 0x42, mem_idx, 0  // -
        ])
        .exportFunc();
    builder.addFunction(`cmpxchg${mem_idx}`, kSig_i_iii)
        .addBody([
          kExprLocalGet, 0,                                               // -
          kExprLocalGet, 1,                                               // -
          kExprLocalGet, 2,                                               // -
          kAtomicPrefix, kExprI32AtomicCompareExchange, 0x42, mem_idx, 0  // -
        ])
        .exportFunc();
  }

  const instance = builder.instantiate();

  const {mem0, load0, store0, cmpxchg0, mem1, load1, store1, cmpxchg1} =
      instance.exports;

  const data0 = new DataView(mem0.buffer);
  const data1 = new DataView(mem1.buffer);

  const offset0 = 16;
  const value0 = 13;
  store0(offset0, value0);
  assertEquals(value0, load0(offset0));
  assertEquals(0, load1(offset0));

  const offset1 = 24;
  const value1 = 11;
  store1(offset1, value1);
  assertEquals(value1, load1(offset1));
  assertEquals(0, load0(offset1));

  assertEquals(value0, cmpxchg0(offset0, -1, -1));
  assertEquals(value0, cmpxchg0(offset0, value0, value1));
  assertEquals(value1, load0(offset0));

  assertEquals(value1, cmpxchg1(offset1, -1, -1));
  assertEquals(value1, cmpxchg1(offset1, value1, value0));
  assertEquals(value0, load1(offset1));

  assertEquals(0, load0(offset1));
  assertEquals(0, load1(offset0));

  // Test traps.
  assertEquals(0, load0(kPageSize - 4));
  assertEquals(0, load1(2 * kPageSize - 4));
  assertTraps(kTrapMemOutOfBounds, () => load0(kPageSize));
  assertTraps(kTrapMemOutOfBounds, () => load1(2 * kPageSize));
})();
