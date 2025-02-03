// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-type-reflection

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

let $sig_v_l = builder.addType(kSig_v_l);
builder.addFunction("dummy0", $sig_v_l).exportFunc().addBody([]);
builder.addFunction("dummy1", $sig_v_l).exportFunc().addBody([]);
builder.addFunction("main", makeSig([wasmRefType($sig_v_l), kWasmI64], []))
  .exportFunc()
  .addBody([
    kExprLocalGet, 1,
    kExprLocalGet, 0,
    kExprCallRef, $sig_v_l,
  ]);
let instance = builder.instantiate();

// Trigger lazy compilation for one of the functions.
instance.exports.dummy0(0n);

let badguy0 = new WebAssembly.Function(
    {parameters: ['i64'], results: []}, instance.exports.dummy0);
let badguy1 = new WebAssembly.Function(
    {parameters: ['i64'], results: []}, instance.exports.dummy1);

instance.exports.main(badguy0, 0n);
instance.exports.main(badguy1, 0n);
