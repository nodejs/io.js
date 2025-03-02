// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestImportTable64AsTable64() {
  print(arguments.callee.name);
  const builder1 = new WasmModuleBuilder();
  builder1.addTable64(kWasmAnyFunc, 10, 10).exportAs('table64');
  const {table64} = builder1.instantiate().exports;

  let builder2 = new WasmModuleBuilder();
  builder2.addImportedTable(
      'imports', 'table', 10, 10, kWasmAnyFunc, /*shared*/ false,
      /*table64*/ true);
  builder2.instantiate({imports: {table: table64}});
})();

(function TestImportTable32AsTable32() {
  print(arguments.callee.name);
  const builder1 = new WasmModuleBuilder();
  builder1.addTable(kWasmAnyFunc, 10, 10).exportAs('table32');
  const {table32} = builder1.instantiate().exports;

  let builder2 = new WasmModuleBuilder();
  builder2.addImportedTable(
      'imports', 'table', 10, 10, kWasmAnyFunc, /*shared*/ false,
      /*table64*/ false);
  builder2.instantiate({imports: {table: table32}});
})();

(function TestImportTable64AsTable32() {
  print(arguments.callee.name);
  const builder1 = new WasmModuleBuilder();
  builder1.addTable64(kWasmAnyFunc, 10, 10).exportAs('table64');
  const {table64} = builder1.instantiate().exports;

  let builder2 = new WasmModuleBuilder();
  builder2.addImportedTable(
      'imports', 'table', 10, 10, kWasmAnyFunc, /*shared*/ false,
      /*table64*/ false);
  assertThrows(
      () => builder2.instantiate({imports: {table: table64}}),
      WebAssembly.LinkError,
      'WebAssembly.Instance(): cannot import i64 table as i32');
})();

(function TestImportTable32AsTable64() {
  print(arguments.callee.name);
  const builder1 = new WasmModuleBuilder();
  builder1.addTable(kWasmAnyFunc, 10, 10).exportAs('table32');
  const {table32} = builder1.instantiate().exports;

  let builder2 = new WasmModuleBuilder();
  builder2.addImportedTable(
      'imports', 'table', 10, 10, kWasmAnyFunc, /*shared*/ false,
      /*table64*/ true);
  assertThrows(
      () => builder2.instantiate({imports: {table: table32}}),
      WebAssembly.LinkError,
      'WebAssembly.Instance(): cannot import i32 table as i64');
})();
