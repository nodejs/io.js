// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
// Generate function 1 (out of 1).
builder.addFunction(undefined, kSig_i_v)
  .addBodyWithEnd([
// signature: i_iii
// body:
kSimdPrefix, kExprS128Const, 0x7d, 0x80, 0xa7, 0xa3, 0xac, 0xe0, 0xaa, 0x61, 0x84, 0xbd, 0x2d, 0x53, 0x09, 0xd8, 0x93, 0xcc,  // v128.const
kSimdPrefix, ...kExprI32x4RelaxedTruncF64x2UZero,  // i32x4.relaxed_trunc_f64x2_u_zero
kSimdPrefix, kExprI32x4ExtractLane, 0,
kExprEnd,  // end @24
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertEquals(-1, instance.exports.main(1, 2, 3));
