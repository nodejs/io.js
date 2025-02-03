// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function exportTable64CallIndirect(builder, table, param_l, param_r, is_tail_call) {
  let sig = builder.addType(kSig_i_iii);
  builder.addFunction('table64_callindirect', kSig_i_i)
      .addBody([
        ...wasmI32Const(param_l),
        ...wasmI32Const(param_r),
        kExprLocalGet, 0,
        ...wasmI64Const(0),
        is_tail_call ? kExprReturnCallIndirect : kExprCallIndirect,
        sig, table.index
      ])
      .exportFunc();
}

let js_function = function(a, b, c) {
  return c ? a : b;
};

function test(is_tail_call) {
  const builder = new WasmModuleBuilder();

  let callee = builder.addImport('m', 'f', kSig_i_iii);
  let table = builder.addTable64(kWasmFuncRef, 10, 10);
  builder.addActiveElementSegment(table.index, wasmI64Const(0), [callee]);

  let left = -2;
  let right = 3;
  exportTable64CallIndirect(builder, table, left, right, is_tail_call);
  let instance = builder.instantiate({m: {f: js_function}});

  assertEquals(left, instance.exports.table64_callindirect(1));
  assertEquals(right, instance.exports.table64_callindirect(0));
}

(function TestTable64CallIndirect() {
  print(arguments.callee.name);
  test(false);
})();

(function TestTable64ReturnCallIndirect() {
  print(arguments.callee.name);
  test(true);
})();

function testOOB(is_tail_call, is_table_growable) {
  const builder = new WasmModuleBuilder();

  let callee = builder.addImport('m', 'f', kSig_i_iii);
  let table_size = 10
  let max_table_size = is_table_growable ? table_size + 1 : table_size;
  let table = builder.addTable64(kWasmFuncRef, table_size, max_table_size);
  builder.addActiveElementSegment(table.index, wasmI64Const(1), [callee]);

  let left = -2;
  let right = 3;
  let sig = builder.addType(kSig_i_iii);
  const kSig_i_il = makeSig([kWasmI32, kWasmI64], [kWasmI32]);
  builder.addFunction('table64_callindirect', kSig_i_il)
      .addBody([
        ...wasmI32Const(left),   // param 0 of js_function
        ...wasmI32Const(right),  // param 1 of js_function
        kExprLocalGet, 0,        // param 2 of js_function
        kExprLocalGet, 1,        // index into the table
        is_tail_call ? kExprReturnCallIndirect : kExprCallIndirect, sig,
        table.index
      ])
      .exportFunc();

  let instance = builder.instantiate({m: {f: js_function}});

  // Null entry.
  let table_index = 2n;
  assertTraps(
      kTrapFuncSigMismatch,
      () => instance.exports.table64_callindirect(1, table_index));
  table_index = BigInt(table_size - 1);
  assertTraps(
      kTrapFuncSigMismatch,
      () => instance.exports.table64_callindirect(1, table_index));
  // Table index is OOB.
  table_index = BigInt(table_size);
  assertTraps(
      kTrapTableOutOfBounds,
      () => instance.exports.table64_callindirect(1, table_index));
  table_index = -1n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => instance.exports.table64_callindirect(1, table_index));
  table_index = 1n << 32n;
  assertTraps(
      kTrapTableOutOfBounds,
      () => instance.exports.table64_callindirect(1, table_index));
}

(function TestTable64CallIndirectOOBNonGrowable() {
  print(arguments.callee.name);
  testOOB(false, false);
})();

(function TestTable64CallIndirectOOBGrowable() {
  print(arguments.callee.name);
  testOOB(false, true);
})();

(function TestTable64ReturnCallIndirectOOBNonGrowable() {
  print(arguments.callee.name);
  testOOB(true, false);
})();

(function TestTable64ReturnCallIndirectOOBGrowable() {
  print(arguments.callee.name);
  testOOB(true, true);
})();
