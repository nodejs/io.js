// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --experimental-wasm-inlining
// Flags: --turboshaft-wasm --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addTag(makeSig([], []));
// Generate function 1 (out of 1).
builder.addFunction(undefined, 0 /* sig */)
  .addBody([
// signature: i_iii
// body:
kExprTry, 0x40,  // try @9
  kExprEnd,  // end @14
kExprTry, 0x7f,  // try @78 i32
  kExprI32Const, 0xcd, 0xe9, 0xa1, 0xcd, 0x7e,  // i32.const
  kExprI32Const, 0xa6, 0xbe, 0xcb, 0xbb, 0x05,  // i32.const
  kExprI32Const, 0x9d, 0xed, 0xa4, 0xbf, 0x07,  // i32.const
  kExprCallFunction, 0x00,  // call function #0: i_iii
  kExprEnd,  // end @101
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
try {
  print(instance.exports.main(1, 2, 3));
} catch (e) {  print('caught exception', e);}
