// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function exportTable64Getter(builder, table, type) {
  const table64_get_sig = makeSig([kWasmI64], [type]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody([
        kExprLocalGet, 0,
        kExprTableGet, table.index])
      .exportFunc();
}

function exportTable64Copy(builder, table_dst, table_src) {
  const kSig_v_lll = makeSig([kWasmI64, kWasmI64, kWasmI64], []);
  builder.addFunction('table64_copy', kSig_v_lll)
      .addBody([
        kExprLocalGet, 0,
        kExprLocalGet, 1,
        kExprLocalGet, 2,
        kNumericPrefix, kExprTableCopy, table_dst.index, table_src.index
      ])
      .exportFunc();
}

function exportTable64FillExternRef(builder, table) {
    let kSig_v_lrl = makeSig([kWasmI64, kWasmExternRef, kWasmI64], []);
    builder.addFunction('table64_fill', kSig_v_lrl)
        .addBody([
          kExprLocalGet, 0,
          kExprLocalGet, 1,
          kExprLocalGet, 2,
          kNumericPrefix, kExprTableFill, table.index
        ])
        .exportFunc();
}

function exportTable64Size(builder, table) {
  builder.addFunction('table64_size', kSig_l_v)
  .addBody([kNumericPrefix, kExprTableSize, table.index])
  .exportFunc();
}

function checkExternRefTable(getter, size, start, count, value) {
  for (let i = 0; i < size; ++i) {
    if (i < start || i >= start + count) {
      assertEquals(null, getter(BigInt(i)))
    } else {
      assertEquals(value, getter(BigInt(i)));
    }
  }
}

(function TestTable64Copy() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table_src =
      builder.addTable64(kWasmExternRef, 15, 20).exportAs('table_src');
  const table_dst =
      builder.addTable64(kWasmExternRef, 10).exportAs('table_dst');

  exportTable64Getter(builder, table_dst, kWasmExternRef);
  exportTable64Size(builder, table_dst);
  exportTable64Copy(builder, table_dst, table_src);
  exportTable64FillExternRef(builder, table_src)

  let exports = builder.instantiate().exports;

  let dummy_externref = {foo: 12, bar: 34};

  // Just in bounds.
  let start_dst = 3n;
  let start_src = 6n;
  let count = 7n;
  exports.table64_fill(start_src, dummy_externref, count);
  exports.table64_copy(start_dst, start_src, count);
  let size = exports.table64_size(builder, table_dst);
  checkExternRefTable(
      exports.table64_get, size, start_dst, count, dummy_externref);

  // start_dst is OOB.
  start_dst = 4n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_copy(start_dst, start_src, count));
  start_dst = 1n << 32n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_copy(start_dst, start_src, count));

  // start_src is OOB.
  start_dst = 1n;
  start_src = 9n;
  count = 7n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_copy(start_dst, start_src, count));
  start_src = 1n << 32n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_copy(start_dst, start_src, count));
  // count is OOB.
  start_dst = 3n;
  start_src = 6n;
  count = 1n << 32n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_copy(start_dst, start_src, count));
})();

(function TestTypingForCopyBetween32And64Bit() {
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
    const kTableSize = 10;
    let table64_index = builder.addTable64(kWasmExternRef, kTableSize)
                            .exportAs('table64')
                            .index;
    let table32_index =
        builder.addTable(kWasmExternRef, kTableSize).exportAs('table32').index;

    let src_index = src == 32 ? table32_index : table64_index;
    let dst_index = dst == 32 ? table32_index : table64_index;

    builder.addFunction('copy', makeSig([dst_type, src_type, size_type], []))
        .addBody([
          kExprLocalGet, 0,                                     // dst
          kExprLocalGet, 1,                                     // src
          kExprLocalGet, 2,                                     // size
          kNumericPrefix, kExprTableCopy, dst_index, src_index  // table.copy
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

(function TestCopyBetweenTable32AndTable64() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const kTableSize = 10;
  let table64_index =
      builder.addTable64(kWasmExternRef, kTableSize).exportAs('table64').index;
  let table32_index =
      builder.addTable(kWasmExternRef, kTableSize).exportAs('table32').index;

  builder
      .addFunction('copy_32_to_64', makeSig([kWasmI64, kWasmI32, kWasmI32], []))
      .addBody([
        kExprLocalGet, 0,  // dst
        kExprLocalGet, 1,  // src
        kExprLocalGet, 2,  // size
        kNumericPrefix, kExprTableCopy, table64_index, table32_index
      ])
      .exportFunc();
  builder
      .addFunction('copy_64_to_32', makeSig([kWasmI32, kWasmI64, kWasmI32], []))
      .addBody([
        kExprLocalGet, 0,  // dst
        kExprLocalGet, 1,  // src
        kExprLocalGet, 2,  // size
        kNumericPrefix, kExprTableCopy, table32_index, table64_index
      ])
      .exportFunc();

  let instance = builder.instantiate();
  let {table32, table64, copy_32_to_64, copy_64_to_32} = instance.exports;

  let object = {foo: 12, bar: 34};

  // These helpers extract the table elements at [offset, offset+size)] into an
  // Array.
  let table32_elements = (offset, size) =>
      new Array(size).fill(0).map((e, i) => table32.get(i));
  let table64_elements = (offset, size) =>
      new Array(size).fill(0).map((e, i) => table64.get(i));

  // Init table32[2] to object.
  table32.set(2, object);
  // Copy table32[1..3] to table64[0..2].
  copy_32_to_64(0n, 1, 3);
  assertEquals([null, null, object, null], table32_elements(0, 4));
  assertEquals([null, object, null, null], table64_elements(0, 4));
  // Copy table64[1..2] to table32[0..1].
  copy_64_to_32(0, 1n, 2);
  assertEquals([object, null, object, null], table32_elements(0, 4));
  assertEquals([null, object, null, null], table64_elements(0, 4));

  // Just before OOB.
  copy_32_to_64(BigInt(kTableSize), 0, 0);
  copy_64_to_32(kTableSize, 0n, 0);
  copy_32_to_64(BigInt(kTableSize - 3), 0, 3);
  copy_64_to_32(kTableSize - 3, 0n, 3);
  assertEquals([null, object, null], table64_elements(kTableSize - 3, 3));
  // OOB.
  assertTraps(
      kTrapTableOutOfBounds, () => copy_32_to_64(BigInt(kTableSize + 1), 0, 0));
  assertTraps(
      kTrapTableOutOfBounds, () => copy_64_to_32(kTableSize + 1, 0n, 0));
  assertTraps(
      kTrapTableOutOfBounds, () => copy_32_to_64(BigInt(kTableSize - 2), 0, 3));
  assertTraps(
      kTrapTableOutOfBounds, () => copy_64_to_32(kTableSize - 2, 0n, 3));
})();
