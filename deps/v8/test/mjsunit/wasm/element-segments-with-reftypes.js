// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestGlobalGetElement() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let table = builder.addTable(kWasmFuncRef, 10, 10).exportAs('table');
  let f0 = builder.addFunction('double', kSig_i_i).addBody([
    kExprLocalGet, 0, kExprLocalGet, 0, kExprI32Add
  ]);
  let f1 = builder.addFunction('inc', kSig_i_i).addBody([
    kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add
  ]);
  let global0 =
      builder.addGlobal(kWasmFuncRef, false, false, [kExprRefFunc, f0.index]);
  let global1 =
      builder.addGlobal(kWasmFuncRef, false, false, [kExprRefFunc, f1.index]);
  // At instantiation, table[0] = global0, table[1] = global1.
  builder.addActiveElementSegment(
      table.index, wasmI32Const(0),
      [[kExprGlobalGet, global0.index], [kExprGlobalGet, global1.index]],
      kWasmFuncRef);

  let passive = builder.addPassiveElementSegment(
      [[kExprGlobalGet, global0.index], [kExprGlobalGet, global1.index]],
      kWasmFuncRef);

  // table[2] = global0, table[3] = global1.
  builder.addFunction('init', kSig_v_v)
      .addBody([
        kExprI32Const, 2,  // table index
        kExprI32Const, 0,  // element index
        kExprI32Const, 2,  // length
        kNumericPrefix, kExprTableInit, passive, table.index])
      .exportFunc();

  let instance = builder.instantiate({});

  instance.exports.init();
  assertEquals(instance.exports.table.get(0)(10), 20);
  assertEquals(instance.exports.table.get(1)(10), 11);
  assertEquals(instance.exports.table.get(2)(10), 20);
  assertEquals(instance.exports.table.get(3)(10), 11);
})();

(function TestTypedFunctionElementSegment() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_i_i);

  let f0 = builder.addFunction('double', sig).addBody([
    kExprLocalGet, 0, kExprLocalGet, 0, kExprI32Add
  ]);
  let f1 = builder.addFunction('inc', sig).addBody([
    kExprLocalGet, 0, kExprI32Const, 1, kExprI32Add
  ]);

  let table =
      builder.addTable(wasmRefType(sig), 10, 10, [kExprRefFunc, f0.index])
          .exportAs('table');

  builder.addActiveElementSegment(
      table.index, wasmI32Const(0),
      [[kExprRefFunc, f0.index], [kExprRefFunc, f1.index]],
      wasmRefType(sig));

  let passive = builder.addPassiveElementSegment(
      [[kExprRefFunc, f0.index], [kExprRefFunc, f1.index]],
      wasmRefType(sig));

  builder.addFunction('init', kSig_v_v)
      .addBody([
        kExprI32Const, 2,  // table index
        kExprI32Const, 0,  // element index
        kExprI32Const, 2,  // length
        kNumericPrefix, kExprTableInit, passive, table.index
      ])
      .exportFunc();

  let instance = builder.instantiate({});

  instance.exports.init();
  assertEquals(instance.exports.table.get(0)(10), 20);
  assertEquals(instance.exports.table.get(1)(10), 11);
  assertEquals(instance.exports.table.get(2)(10), 20);
  assertEquals(instance.exports.table.get(3)(10), 11);
})();

// Test that mutable globals cannot be used in element segments, even with
// wasm-gc.
(function TestMutableGlobalInElementSegment() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let global = builder.addImportedGlobal("m", "g", kWasmFuncRef, true);
  let table = builder.addTable(kWasmFuncRef, 10, 10);
  builder.addActiveElementSegment(
      table.index, wasmI32Const(0),
      [[kExprGlobalGet, global]], kWasmFuncRef);
  builder.addExportOfKind("table", kExternalTable, table.index);

  assertThrows(
    () => builder.instantiate({m : {g :
        new WebAssembly.Global({value: "anyfunc", mutable: true}, null)}}),
    WebAssembly.CompileError,
    /mutable globals cannot be used in constant expressions/);
})();
