// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --dump-counters --no-wasm-generic-wrapper

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function compileAdd(val) {
  var builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, true)]);
  let sig = makeSig([kWasmI32], [wasmRefType(struct)])
  builder.addFunction(`fct`, sig)
  .addBody([
    kExprLocalGet, 0,
    kExprI32Const, val,
    kExprI32Add,
    kGCPrefix, kExprStructNew, struct,
  ])
  .exportFunc();
  return builder.instantiate();
}

assertEquals(0, %WasmCompiledExportWrappersCount());
let a = compileAdd(1);
a.exports.fct(1);
assertEquals(1, %WasmCompiledExportWrappersCount());
let b = compileAdd(2);
b.exports.fct(1);
assertEquals(1, %WasmCompiledExportWrappersCount());
let c = compileAdd(2);
c.exports.fct(1);
assertEquals(1, %WasmCompiledExportWrappersCount());
