// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addMemory(16, 32);
// Generate function 1 (out of 1).
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprI32Const, 0x15,  // i32.const
kSimdPrefix, kExprS128Load8x8U, 0x00, 0xff, 0xff, 0xff, 0x00,  // v128.load8x8_u
kExprUnreachable,  // unreachable
kExprEnd,  // end @11
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertThrows(() => instance.exports.main(1, 2, 3), WebAssembly.RuntimeError, /memory access out of bounds/);
