// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-lazy-feedback-allocation

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function explore(v) {
    let context = {exploredValue: v };
    let array = [];
    array.push(context.exploredValue);
}

%PrepareFunctionForOptimization(explore);
explore(1);

const builder = new WasmModuleBuilder();
builder
    .addFunction("passThrough", makeSig([kWasmExternRef], [kWasmExternRef]))
    .addBody([kExprLocalGet,0]).exportFunc();
const wasm = builder.instantiate().exports;
function fct() {
    explore(wasm.passThrough());
}
%PrepareFunctionForOptimization(fct);
fct();
%OptimizeFunctionOnNextCall(fct);
fct();
