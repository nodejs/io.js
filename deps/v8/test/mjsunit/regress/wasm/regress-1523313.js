// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --turboshaft-wasm

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
// Generate function 1 (out of 1).
builder.addFunction(undefined, makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]))
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprLoop, 0x7f,  // loop @1 i32
  kExprF64Const, 0x10, 0x1f, 0x2b, 0xb9, 0x57, 0x7b, 0x78, 0x6a,  // f64.const
  kExprI32SConvertF64,  // i32.trunc_f64_s
  kExprEnd,  // end @13
kExprEnd,  // end @14
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertTraps(kTrapFloatUnrepresentable, () => instance.exports.main(1, 2, 3));
