// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
let builder = new WasmModuleBuilder();
// To trigger the DCHECK, the fields' total size must be >= 256*kTaggedSize.
let $struct = builder.addStruct(new Array(128).fill(makeField(kWasmF64, true)));

builder.addFunction("makeStruct", makeSig([], [wasmRefType($struct)]))
  .exportFunc()
  .addBody([kGCPrefix, kExprStructNewDefault, $struct]);
let instance = builder.instantiate();
let wasm_object = instance.exports.makeStruct();

function main(obj) {
  try {
    for (let element of obj) {}
  } catch(e) {
    assertMatches(/obj is not iterable/, e.message);
  }
}

%PrepareFunctionForOptimization(main);
main(wasm_object);
main(wasm_object);
%OptimizeFunctionOnNextCall(main);
main(wasm_object);
