// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --experimental-wasm-inlining

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder.addMemory(16, 32);  // Causes the InstanceCache to be used.

let v_v_index = builder.addType(kSig_v_v);
let nop = builder.addFunction('nop', v_v_index).addBody([]);
builder.addDeclarativeElementSegment([nop.index]);

let trampoline = builder.addFunction('trampoline', v_v_index).exportFunc()
  .addBody([
    kExprRefFunc, nop.index,
    kExprCallRef, v_v_index,
  ]);

let main = builder.addFunction('main', kSig_i_i).exportFunc().addBody([
  kExprTry, kWasmVoid,
    kExprCallFunction, nop.index,
    // The FunctionBodyDecoder assumes that any call can throw, so it marks
    // the {catch_all} block as reachable.
    // The graph builder interface inlines the call and knows
    // that it won't throw, so it considers the {catch_all} block (and
    // everything after it) unreachable.
    kExprI32Const, 42,
    kExprReturn,
  kExprCatchAll,
  kExprEnd,
  // The point of this test: what happens for this instruction when the
  // compiler knows it's emitting unreachable code.
  kExprCallFunction, trampoline.index,
  kExprI32Const, 1,
]);
let instance = builder.instantiate();
for (let i = 0; i < 100; i++) {
  instance.exports.trampoline();
  instance.exports.main(1);
}
%WasmTierUpFunction(instance.exports.trampoline);
%WasmTierUpFunction(instance.exports.main);
instance.exports.main(1);
