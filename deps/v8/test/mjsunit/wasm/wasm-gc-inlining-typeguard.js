// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Create a dummy wasm function that only passes a value. This will however
// trigger the wasm inlining and run the TurboFan wasm optimization passes on
// the JS function.
let builder = new WasmModuleBuilder();
builder.addFunction('passThrough', makeSig([kWasmExternRef], [kWasmExternRef]))
  .addBody([kExprLocalGet, 0])
  .exportFunc();
let instance = builder.instantiate({});
let wasm = instance.exports;

// The graph for foo contains a TypeGuard not simply guarding a type but an
// integer range.
(function TestUntypedTypeGuard() {
  function foo(a, o) {
    return wasm.passThrough(a.find(x => x === o.x));
  }
  %PrepareFunctionForOptimization(foo);
  assertEquals(3, foo([1, 2, 3], {x:3}));
  %OptimizeFunctionOnNextCall(foo);
  assertEquals(3, foo([1, 2, 3], {x:3}));
})();
