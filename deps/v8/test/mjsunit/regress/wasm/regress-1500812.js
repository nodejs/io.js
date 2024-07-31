// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32);

builder
    .addFunction(undefined, makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]))
    .addBodyWithEnd([
      kExprI32Const, 0xac, 0x92, 0xe1, 0x94, 0x05,  // i32.const
      kSimdPrefix, kExprS128Load64Zero, 0x00, 0xe7, 0xe3,
      0x02,                                    // v128.load64_zero
      kSimdPrefix, kExprF64x2PromoteLowF32x4,  // f64x2.promote_low_f32x4
      kSimdPrefix, kExprI16x8AllTrue, 0x01,    // i16x8.all_true
      kExprEnd,                                // end @21
    ]);
builder.addExport('main', 0);
const instance = builder.instantiate();
try {
  instance.exports.main(1, 2, 3);
} catch (e) {
  assertException(
      e, WebAssembly.RuntimeError, new RegExp(kTrapMsgs[kTrapMemOutOfBounds]));
  assertTrue(e.stack.includes('[0]:0x31'));
}
