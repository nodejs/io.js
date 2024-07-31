// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32);
// Generate function 1 (out of 4).
builder.addFunction(undefined, makeSig([], [kWasmI32]))
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, ...kExprI32x4DotI8x16I7x16AddS,  // i32x4.dot_i8x16_i7x16_add_s
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, ...kExprI32x4DotI8x16I7x16AddS,  // i32x4.dot_i8x16_i7x16_add_s
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, ...kExprI32x4DotI8x16I7x16AddS,  // i32x4.dot_i8x16_i7x16_add_s
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, ...kExprI32x4DotI8x16I7x16AddS,  // i32x4.dot_i8x16_i7x16_add_s
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprS128Const, 0x5d, 0xb9, 0x7b, 0x5b, 0x16, 0x58, 0x6d, 0x5d, 0x68, 0x7a, 0x93, 0x4b, 0x39, 0xcc, 0x8e, 0x32,  // v128.const
kSimdPrefix, ...kExprI32x4DotI8x16I7x16AddS,  // i32x4.dot_i8x16_i7x16_add_s
kSimdPrefix, kExprI16x8ExtractLaneS, 0x00,  // i16x8.extract_lane_s
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprS128Load32Lane, 0x00, 0xe8, 0x4b, 0x00,  // v128.load32_lane
kSimdPrefix, kExprI16x8ExtractLaneS, 0x00,  // i16x8.extract_lane_s
kExprEnd,  // end @126
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertTraps(kTrapMemOutOfBounds, () => instance.exports.main());
