// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --experimental-wasm-inlining

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder.addMemory(1, 2);

let callee = builder.addFunction("callee", kSig_i_i).addBody([
  kExprLocalGet, 0,
  kExprI32Eqz,
  kExprIf, kWasmVoid,
    kExprI32Const, 0,
    kExprReturn,
  kExprEnd,
  kExprLoop, kWasmVoid,
    kExprLocalGet, 0,
    kExprI32Const, 1,
    kExprI32Sub,
    kExprLocalSet, 0,
  kExprEnd,
  kExprLocalGet, 0,
]);

builder.addFunction("main", kSig_i_i).exportFunc().addBody([
  kExprBlock, kWasmVoid,
    kExprLoop, kWasmVoid,
      kExprLocalGet, 0,
      kExprBrIf, 1,
      kExprLocalGet, 0,
      kExprCallFunction, callee.index,
      kExprLocalSet, 0,
    kExprEnd,
  kExprEnd,
  kExprLocalGet, 0,
  kExprCallFunction, callee.index,
]);

let instance = builder.instantiate();
let main = instance.exports.main;
// Must accumulate a sufficient call count.
for (let i = 0; i < 40; i++) main();
%WasmTierUpFunction(main);
main();
