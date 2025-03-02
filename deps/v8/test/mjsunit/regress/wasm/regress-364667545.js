// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-staging

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();
let imported = builder.addImport('m', 'i', kSig_v_v);
builder.addFunction('main', kSig_v_v)
  .addBody([
      kExprCallFunction, imported,
  ])
  .exportFunc();

let instance = builder.instantiate(
    {m: {i: new WebAssembly.Suspending(() => Promise.resolve())}});
let cached = WebAssembly.promising(instance.exports.main);
let run = () => {
  return cached();
};
%PrepareFunctionForOptimization(run);
for (let i = 0; i < 10; ++i) {
  run();
}
%OptimizeFunctionOnNextCall(run);
run();
