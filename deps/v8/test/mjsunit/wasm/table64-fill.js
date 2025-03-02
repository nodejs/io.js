// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function exportTable64Getter(builder, table, type) {
  const table64_get_sig = makeSig([kWasmI64], [type]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody([
        kExprLocalGet, 0,
        kExprTableGet, table.index])
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

function checkExternRefTable(getter, start, count, value) {
  for (let i = 0; i < count; ++i) {
    assertEquals(value, getter(start + BigInt(i)));
  }
}

(function TestTable64Fill() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmExternRef, 5, 20).exportAs('table');

  exportTable64Getter(builder, table, kWasmExternRef);
  exportTable64FillExternRef(builder, table)

  let exports = builder.instantiate().exports;

  let start = 1n;
  let value = {foo: 12, bar: 34};
  let count = 3n;

  exports.table64_fill(start, value, count);
  checkExternRefTable(exports.table64_get, start, count, value);
})();

(function TestTable64FillOOB() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmExternRef, 5, 20).exportAs('table');

  exportTable64Getter(builder, table, kWasmExternRef);
  exportTable64FillExternRef(builder, table)

  let exports = builder.instantiate().exports;

  let value = {foo: 12, bar: 34};
  // Just in bounds.
  let start = 1n;
  let count = 4n;
  exports.table64_fill(start, value, count);
  checkExternRefTable(exports.table64_get, start, count, value);
  // Start OOB.
  start = 5n;
  count = 1n;
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_fill(start, value, count));
  start = 1n << 32n;
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_fill(start, value, count));
  // Count OOB.
  start = 0n;
  count = 6n;
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_fill(start, value, count));
  count = 1n << 32n;
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_fill(start, value, count));
})();
