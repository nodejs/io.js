// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --stress-wasm-code-gc

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

let $sig0 = builder.addType(kSig_v_v);
let $func0 = builder.addImport("m", "func", $sig0);
let $table0 = builder.addImportedTable("m", "table", 0, 3, kWasmFuncRef);
builder.addActiveElementSegment($table0, wasmI32Const(0), [$func0]);
builder.addFunction("main", $sig0).exportFunc().addBody([
  kExprI32Const, 0,  // table slot
  kExprCallIndirect, $sig0, $table0,
]);

var table = new WebAssembly.Table({initial: 2, element: 'anyfunc', maximum: 3});

function f1() {}
let f2 = (() => {}).bind(null);

(function() {
  // Instance 1, immediately eligible for GC.
  // Use a function that's not a plain JSFunction to force wrapper compilation.
  builder.instantiate({m: {table, func: f2}});
})();

let instance = builder.instantiate({m: {table, func: f1}});
instance.exports.main();
gc();
// Make sure the wrapper is still there after GC.
instance.exports.main();
