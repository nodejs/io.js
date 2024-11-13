// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addFunction('f32', makeSig([], [kWasmI32])).exportFunc().addBody([
  ...wasmF32ConstSignalingNaN(),
  kExprI32ReinterpretF32,
]);

builder.addFunction('f64', makeSig([], [kWasmI64])).exportFunc().addBody([
  ...wasmF64ConstSignalingNaN(),
  kExprI64ReinterpretF64,
]);

const instance = builder.instantiate();
assertEquals(2141692345, instance.exports.f32());
assertEquals(9219994337134247936n, instance.exports.f64());
