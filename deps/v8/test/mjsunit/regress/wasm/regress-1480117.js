// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32);
// Generate function 1 (out of 3).
builder.addFunction(undefined, makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32] ))
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprI16x8SConvertI8x16Low, 0x01,  // i16x8.extend_low_i8x16_s
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprI16x8SConvertI8x16Low, 0x01,  // i16x8.extend_low_i8x16_s
kSimdPrefix, kExprI16x8SConvertI8x16Low, 0x01,  // i16x8.extend_low_i8x16_s
kSimdPrefix, kExprI32x4DotI16x8S, 0x01,  // i32x4.dot_i16x8_s
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprI16x8SConvertI8x16Low, 0x01,  // i16x8.extend_low_i8x16_s
kSimdPrefix, kExprS128Or,  // v128.or
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, ...kExprI32x4RelaxedTruncF64x2UZero,  // i32x4.relaxed_trunc_f64x2_u_zero
kSimdPrefix, kExprI32x4LtU,  // i32x4.lt_u
kSimdPrefix, ...kExprI32x4RelaxedTruncF64x2UZero,  // i32x4.relaxed_trunc_f64x2_u_zero
kSimdPrefix, ...kExprI32x4RelaxedTruncF64x2UZero,  // i32x4.relaxed_trunc_f64x2_u_zero
kSimdPrefix, kExprI8x16ExtractLaneS, 0x06,  // i8x16.extract_lane_s
kExprEnd,  // end @100
]);
// Generate function 3 (out of 3).
builder.addExport('main', 0);
const instance = builder.instantiate();
assertEquals(0, instance.exports.main(1, 2, 3));
