// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

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
  // Test two memories: One 32-bit and one 64-bit, in both orders.
  for (let mem64_idx of [0, 1]) {
    const builder = new WasmModuleBuilder();
    const mem32_idx = 1 - mem64_idx;

    const mem32_pages = 1;
    const mem32_size = mem32_pages * kPageSize;
    const mem64_pages = 3;
    const mem64_size = mem64_pages * kPageSize;
    if (mem32_idx == 0) {
      builder.addMemory(mem32_pages, mem32_pages);
      builder.addMemory64(mem64_pages, mem64_pages);
    } else {
      builder.addMemory64(mem64_pages, mem64_pages);
      builder.addMemory(mem32_pages, mem32_pages);
    }
    builder.exportMemoryAs('mem32', mem32_idx);
    builder.exportMemoryAs('mem64', mem64_idx);

    builder.addFunction('load32', kSig_i_i)
        .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0x40, mem32_idx, 0])
        .exportFunc();
    builder.addFunction('load64', kSig_i_l)
        .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0x40, mem64_idx, 0])
        .exportFunc();

    const instance = builder.instantiate();
    const mem32_offset = 48;
    const mem64_offset = 16;
    const value = 13;
    const view32 = new DataView(instance.exports.mem32.buffer);
    const view64 = new DataView(instance.exports.mem64.buffer);
    view32.setInt32(mem32_offset, value, true);
    view64.setInt32(mem64_offset, value, true);

    const {load32, load64} = instance.exports;

    assertEquals(value, load32(mem32_offset));
    assertEquals(0, load32(mem64_offset));
    assertEquals(0, load64(BigInt(mem32_offset)));
    assertEquals(value, load64(BigInt(mem64_offset)));

    assertEquals(0, load32(mem32_size - 4));
    assertEquals(0, load64(BigInt(mem64_size - 4)));
    assertTraps(kTrapMemOutOfBounds, () => load32(mem32_size - 3));
    assertTraps(kTrapMemOutOfBounds, () => load64(BigInt(mem64_size - 3)));
  }
})();

(function testMultiMemoryInit() {
  print(arguments.callee.name);
  // Test two memories: One 32-bit and one 64-bit, in both orders.
  for (let mem64_idx of [0, 1]) {
    const builder = new WasmModuleBuilder();
    const mem32_idx = 1 - mem64_idx;

    const mem32_pages = 1;
    const mem32_size = mem32_pages * kPageSize;
    const mem64_pages = 3;
    const mem64_size = mem64_pages * kPageSize;
    if (mem32_idx == 0) {
      builder.addMemory(mem32_pages, mem32_pages);
      builder.addMemory64(mem64_pages, mem64_pages);
    } else {
      builder.addMemory64(mem64_pages, mem64_pages);
      builder.addMemory(mem32_pages, mem32_pages);
    }
    builder.exportMemoryAs('mem32', mem32_idx);
    builder.exportMemoryAs('mem64', mem64_idx);

    const data = [1, 2, 3, 4, 5];
    const seg_id = builder.addPassiveDataSegment(data);

    builder.addFunction('init32', kSig_v_iii)
        .addBody([
          kExprLocalGet, 0,  // dst
          kExprLocalGet, 1,  // offset
          kExprLocalGet, 2,  // size
          kNumericPrefix, kExprMemoryInit, seg_id, mem32_idx
        ])
        .exportFunc();
    builder.addFunction('init64', kSig_v_lii)
        .addBody([
          kExprLocalGet, 0,  // dst
          kExprLocalGet, 1,  // offset
          kExprLocalGet, 2,  // size
          kNumericPrefix, kExprMemoryInit, seg_id, mem64_idx
        ])
        .exportFunc();

    const instance = builder.instantiate();
    const {init32, init64} = instance.exports;
    const expected_mem32 = new Uint8Array(mem32_size);
    const expected_mem64 = new Uint8Array(mem64_size);

    assertMemoryEquals(expected_mem32, instance.exports.mem32);
    assertMemoryEquals(expected_mem64, instance.exports.mem64);

    init32(7, 1, 3);  // dst, (data) offset, size
    expected_mem32.set(data.slice(1, 4), 7);
    assertMemoryEquals(expected_mem32, instance.exports.mem32);
    assertMemoryEquals(expected_mem64, instance.exports.mem64);

    init64(11n, 3, 2);  // dst, (data) offset, size
    expected_mem64.set(data.slice(3, 5), 11);
    assertMemoryEquals(expected_mem32, instance.exports.mem32);
    assertMemoryEquals(expected_mem64, instance.exports.mem64);

    // Test bounds checks.
    init32(mem32_size - 1, 0, 1);  // dst, (data) offset, size
    assertTraps(kTrapMemOutOfBounds, () => init32(mem32_size - 1, 0, 2));
    init64(BigInt(mem64_size - 1), 0, 1);  // dst, (data) offset, size
    assertTraps(
        kTrapMemOutOfBounds, () => init64(BigInt(mem64_size - 1), 0, 2));
  }
})();

(function testTypingForCopyBetween32And64Bit() {
  print(arguments.callee.name);
  for (let [src, dst, src_type, dst_type, size_type, expect_valid] of [
    // Copy from 32 to 64 bit with correct types.
    [32, 64, kWasmI32, kWasmI64, kWasmI32, true],
    // Copy from 64 to 32 bit with correct types.
    [64, 32, kWasmI64, kWasmI32, kWasmI32, true],
    // Copy from 32 to 64 bit with always one type wrong.
    [32, 64, kWasmI64, kWasmI64, kWasmI32, false],
    [32, 64, kWasmI32, kWasmI32, kWasmI32, false],
    [32, 64, kWasmI32, kWasmI64, kWasmI64, false],
    // Copy from 64 to 32 bit with always one type wrong.
    [64, 32, kWasmI32, kWasmI32, kWasmI32, false],
    [64, 32, kWasmI64, kWasmI64, kWasmI32, false],
    [64, 32, kWasmI64, kWasmI32, kWasmI64, false],
  ]) {
    let type_str = type => type == kWasmI32 ? 'i32' : 'i64';
    print(`- copy from ${src} to ${dst} using types src=${
        type_str(src_type)}, dst=${type_str(dst_type)}, size=${
        type_str(size_type)}`);
    let builder = new WasmModuleBuilder();
    const kMemSizeInPages = 10;
    const kMemSize = kMemSizeInPages * kPageSize;
    let mem64_index = builder.addMemory64(kMemSizeInPages, kMemSizeInPages);
    let mem32_index = builder.addMemory(kMemSizeInPages, kMemSizeInPages);
    builder.exportMemoryAs('mem64', mem64_index);
    builder.exportMemoryAs('mem32', mem32_index);

    let src_index = src == 32 ? mem32_index : mem64_index;
    let dst_index = dst == 32 ? mem32_index : mem64_index;

    builder.addFunction('copy', makeSig([dst_type, src_type, size_type], []))
        .addBody([
          kExprLocalGet, 0,                                      // dst
          kExprLocalGet, 1,                                      // src
          kExprLocalGet, 2,                                      // size
          kNumericPrefix, kExprMemoryCopy, dst_index, src_index  // memcpy
        ])
        .exportFunc();

    if (expect_valid) {
      builder.toModule();
    } else {
      assertThrows(
          () => builder.toModule(), WebAssembly.CompileError,
          /expected type i(32|64), found local.get of type i(32|64)/);
    }
  }
})();

(function testCopyBetween32And64Bit() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  const kMemSizeInPages = 10;
  const kMemSize = kMemSizeInPages * kPageSize;
  let mem64_index = builder.addMemory64(kMemSizeInPages, kMemSizeInPages);
  let mem32_index = builder.addMemory(kMemSizeInPages, kMemSizeInPages);
  builder.exportMemoryAs('mem64', mem64_index);
  builder.exportMemoryAs('mem32', mem32_index);

  builder
      .addFunction('copy_32_to_64', makeSig([kWasmI64, kWasmI32, kWasmI32], []))
      .addBody([
        kExprLocalGet, 0,                                          // dst
        kExprLocalGet, 1,                                          // src
        kExprLocalGet, 2,                                          // size
        kNumericPrefix, kExprMemoryCopy, mem64_index, mem32_index  // memcpy
      ])
      .exportFunc();
  builder
      .addFunction('copy_64_to_32', makeSig([kWasmI32, kWasmI64, kWasmI32], []))
      .addBody([
        kExprLocalGet, 0,                                          // dst
        kExprLocalGet, 1,                                          // src
        kExprLocalGet, 2,                                          // size
        kNumericPrefix, kExprMemoryCopy, mem32_index, mem64_index  // memcpy
      ])
      .exportFunc();

  let instance = builder.instantiate();
  let {mem32, mem64, copy_32_to_64, copy_64_to_32} = instance.exports;

  // These helpers extract the memory at [offset, offset+size)] into an Array.
  let memory32 = (offset, size) =>
      Array.from(new Uint8Array(mem32.buffer.slice(offset, offset + size)));
  let memory64 = (offset, size) =>
      Array.from(new Uint8Array(mem64.buffer.slice(offset, offset + size)));

  // Init mem32[3] to 11.
  new Uint8Array(mem32.buffer)[3] = 11;
  // Copy mem32[2..4] to mem64[1..3].
  copy_32_to_64(1n, 2, 3);
  assertEquals([0, 0, 0, 11, 0], memory32(0, 5));
  assertEquals([0, 0, 11, 0, 0], memory64(0, 5));
  // Copy mem64[2..3] to mem32[1..2].
  copy_64_to_32(1, 2n, 2);
  assertEquals([0, 11, 0, 11, 0], memory32(0, 5));
  assertEquals([0, 0, 11, 0, 0], memory64(0, 5));

  // Just before OOB.
  copy_32_to_64(BigInt(kMemSize), 0, 0);
  copy_64_to_32(kMemSize, 0n, 0);
  copy_32_to_64(BigInt(kMemSize - 3), 0, 3);
  copy_64_to_32(kMemSize - 3, 0n, 3);
  assertEquals([0, 11, 0], memory64(kMemSize - 3, 3));
  // OOB.
  assertTraps(
      kTrapMemOutOfBounds, () => copy_32_to_64(BigInt(kMemSize + 1), 0, 0));
  assertTraps(
      kTrapMemOutOfBounds, () => copy_64_to_32(kMemSize + 1, 0n, 0));
  assertTraps(
      kTrapMemOutOfBounds, () => copy_32_to_64(BigInt(kMemSize - 2), 0, 3));
  assertTraps(
      kTrapMemOutOfBounds, () => copy_64_to_32(kMemSize - 2, 0n, 3));
})();
