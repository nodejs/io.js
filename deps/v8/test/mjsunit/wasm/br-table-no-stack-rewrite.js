// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// Test that we don't rewrite the stack types when iterating over the targets
// for a br_table instrunction.
(function BrTable() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let funcT = builder.addType(makeSig([], []));
  let table = builder.addTable(
    wasmRefNullType(funcT), 1, 1, [kExprRefNull, funcT]);
  let returnsFuncT = builder.addType(makeSig([], [wasmRefNullType(funcT)]));
  let returnsFuncRef = builder.addType(makeSig([], [kWasmFuncRef]));
  builder.addFunction("br_table", makeSig([kWasmI32], [kWasmFuncRef]))
    .addBody([
      kExprBlock, returnsFuncRef,
        kExprBlock, returnsFuncT,
          kExprI32Const, 0,
          kExprTableGet, table.index,
          kExprLocalGet, 0,
          kExprBrTable, 2, 1, 1, 0,
        kExprEnd,
      kExprEnd,
    ])
    .exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(null, wasm.br_table(0));
})();
