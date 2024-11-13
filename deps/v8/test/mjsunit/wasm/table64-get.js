// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-memory64

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function exportTable64Getter(builder, table, type) {
  const table64_get_sig = makeSig([kWasmI64], [type]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody(
        [kExprLocalGet, 0,
         kExprTableGet, table.index])
      .exportFunc();
}

(function TestTable64GetFuncRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmAnyFunc, 10).exportAs('table');
  const f1 = builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]);
  const offset = 3;
  builder.addActiveElementSegment(0, wasmI64Const(offset), [f1.index]);

  exportTable64Getter(builder, table, kWasmAnyFunc);

  let exports = builder.instantiate().exports;

  assertEquals(11, exports.table64_get(BigInt(offset))());
})();

(function TestTable64GetAnyRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmAnyRef, 10, 15).exportAs('table');
  const struct = builder.addStruct([makeField(kWasmI32, true)])
  const offset = 4;
  builder.addActiveElementSegment(
      0, wasmI64Const(offset),
      [[kExprI32Const, 23, kGCPrefix, kExprStructNew, struct]], kWasmAnyRef);

  builder.addFunction('getField', makeSig([kWasmAnyRef], [kWasmI32]))
      .addBody([
        kExprLocalGet, 0,
        kGCPrefix, kExprRefCast, struct,
        kGCPrefix, kExprStructGet, struct, 0
      ])
      .exportFunc();

  exportTable64Getter(builder, table, kWasmAnyRef);

  let exports = builder.instantiate().exports;

  assertEquals(23, exports.getField(exports.table64_get(BigInt(offset))));
})();

(function TestTable64GetExternRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmExternRef, 10).exportAs('table');

  exportTable64Getter(builder, table, kWasmExternRef);

  let exports = builder.instantiate().exports;

  assertEquals(null, exports.table64_get(8n));
})();

(function TestTable64GetWrongType() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmAnyFunc, 10).exportAs('table');
  const f1 = builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]);
  const offset = 3;
  builder.addActiveElementSegment(0, wasmI64Const(offset), [f1.index]);

  // Table64 expects kWasmI64 as the index to the table.
  const table32_get_sig = makeSig([kWasmI32], [kWasmAnyFunc]);
  builder.addFunction('table32_get', table32_get_sig)
      .addBody([kExprLocalGet, 0, kExprTableGet, table.index])
      .exportFunc();

  assertThrows(() => builder.toModule(), WebAssembly.CompileError);
})();

(function TestTable64GetFuncRefOOB() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table_size = 5;
  const table = builder.addTable64(kWasmAnyFunc, table_size).exportAs('table');
  const f1 = builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]);
  const offset = 3;
  builder.addActiveElementSegment(0, wasmI64Const(offset), [f1.index]);

  exportTable64Getter(builder, table, kWasmAnyFunc);

  let exports = builder.instantiate().exports;

  assertEquals(11, exports.table64_get(BigInt(offset))());
  assertEquals(null, exports.table64_get(BigInt(table_size - 1)));
  assertTraps(kTrapTableOutOfBounds, () => exports.table64_get(BigInt(-1)));
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_get(BigInt(table_size)));
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_get(BigInt(table_size + 13)));
  assertTraps(kTrapTableOutOfBounds, () => exports.table64_get(1n << 32n));
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_get(BigInt(offset) << 32n));
})();

(function TestTable64GetExternRefOOB() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table_size = 12;
  const table = builder.addTable64(kWasmExternRef, table_size, table_size + 4)
                    .exportAs('table');

  exportTable64Getter(builder, table, kWasmExternRef);

  let exports = builder.instantiate().exports;

  assertEquals(null, exports.table64_get(BigInt(table_size - 1)));
  assertTraps(kTrapTableOutOfBounds, () => exports.table64_get(-1n));
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_get(BigInt(table_size)));
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_get(BigInt(table_size + 3)));
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_get(BigInt(table_size + 23)));
  assertTraps(kTrapTableOutOfBounds, () => exports.table64_get(1n << 32n));
})();
