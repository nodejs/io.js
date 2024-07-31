// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

builder.addFunction("main", kSig_i_i).exportFunc().addBody([
  kExprLocalGet, 0
]);

let instance = builder.instantiate();

let wasm = instance.exports.main;
let array = new Int32Array(2);

function f(p, a) {
  var x;
  if (p >= a.length) {
    x = wasm(p);  // A phi input with Wasm type.
  } else {
    x = a[p];  // A phi input without Wasm type.
  }
  x++;  // Force creation of a phi node.
  return x;
}

%PrepareFunctionForOptimization(f);
for (let i = 0; i < 10; i++) f(i, array);
%OptimizeFunctionOnNextCall(f);
assertEquals(43, f(42, array));
