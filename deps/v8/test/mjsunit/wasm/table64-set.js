// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function exportTable64Getter(builder, table, type) {
  const table64_get_sig = makeSig([kWasmI64], [type]);
  builder.addFunction('table64_get', table64_get_sig)
      .addBody(
        [kExprLocalGet, 0,
         kExprTableGet, table.index])
      .exportFunc();
}

function exportTable64Setter(builder, table, type) {
  const table64_set_sig = makeSig([kWasmI64, type], []);
  builder.addFunction('table64_set', table64_set_sig)
      .addBody(
        [kExprLocalGet, 0,
         kExprLocalGet, 1,
         kExprTableSet, table.index])
      .exportFunc();
}

(function TestTable64SetFuncRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmAnyFunc, 10).exportAs('table');
  builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]).exportFunc();

  exportTable64Getter(builder, table, kWasmAnyFunc);
  exportTable64Setter(builder, table, kWasmAnyFunc);

  let exports = builder.instantiate().exports;

  const offset = 5n;
  assertSame(null, exports.table64_get(offset));
  exports.table64_set(offset, exports.f);
  assertSame(exports.f, exports.table64_get(offset));
})();

(function TestTable64SetExternRef() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmExternRef, 10, 20).exportAs('table');

  exportTable64Getter(builder, table, kWasmExternRef);
  exportTable64Setter(builder, table, kWasmExternRef);

  let exports = builder.instantiate().exports;

  const offset = 1n;
  const dummy_ref = {foo: 1, bar: 3};
  assertEquals(null, exports.table64_get(offset));
  exports.table64_set(offset, dummy_ref);
  assertSame(dummy_ref, exports.table64_get(offset));
})();

(function TestTable64SetFuncRefWrongType() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table = builder.addTable64(kWasmAnyFunc, 10).exportAs('table');
  builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]).exportFunc();

  // Table64 expects kWasmI64 as the index to the table.
  const table64_set_sig = makeSig([kWasmI32, kWasmAnyFunc], []);
  builder.addFunction('table64_set', table64_set_sig)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprTableSet, table.index])
      .exportFunc();

  assertThrows(() => builder.toModule(), WebAssembly.CompileError);
})();

(function TestTable64SetFuncRefOOB() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table_size = 4;
  const table = builder.addTable64(kWasmAnyFunc, table_size).exportAs('table');
  builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 11]).exportFunc();

  exportTable64Getter(builder, table, kWasmAnyFunc);
  exportTable64Setter(builder, table, kWasmAnyFunc);

  let exports = builder.instantiate().exports;

  exports.table64_set(BigInt(table_size - 1), exports.f);
  assertSame(exports.f, exports.table64_get(BigInt(table_size - 1)));
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_set(BigInt(table_size), exports.f));
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_set(BigInt(table_size + 1), exports.f));
  assertTraps(kTrapTableOutOfBounds, () => exports.table64_set(-1n, exports.f));
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_set(1n << 32n, exports.f));
})();

(function TestTable64SetExternRefOOB() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const table_size = 10;
  const table =
      builder.addTable64(kWasmExternRef, table_size, 20).exportAs('table');

  exportTable64Getter(builder, table, kWasmExternRef);
  exportTable64Setter(builder, table, kWasmExternRef);

  let exports = builder.instantiate().exports;

  const dummy_ref = {foo: 1, bar: 3};

  exports.table64_set(BigInt(table_size - 1), dummy_ref);
  assertSame(dummy_ref, exports.table64_get(BigInt(table_size - 1)));
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_set(BigInt(table_size), dummy_ref));
  assertTraps(
      kTrapTableOutOfBounds,
      () => exports.table64_set(BigInt(table_size + 1), dummy_ref));
  assertTraps(kTrapTableOutOfBounds, () => exports.table64_set(-1n, dummy_ref));
  assertTraps(
      kTrapTableOutOfBounds, () => exports.table64_set(1n << 32n, dummy_ref));
})();
