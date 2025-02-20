// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-wrapper-tiering-budget=1

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let sig = builder.addType(makeSig(
    [kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
let f0 = builder.addImport('m', 'f', sig);
builder.addExport('boom', f0);

const instance = builder.instantiate({m: {f: () => {}}});
function boom() {
  instance.exports.boom(0, 0, 0, 0, 0x5deadbea, 0xdeadbeaf);
}
%PrepareFunctionForOptimization(boom);
for (let i = 0; i < 10; ++i) boom();
%OptimizeFunctionOnNextCall(boom);
boom();
%SimulateNewspaceFull();
boom();
