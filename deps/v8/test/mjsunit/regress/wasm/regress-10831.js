// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// This test is shrunk from a test case provided at https://crbug.com/v8/10831.
// This exercises an aligned-load bug in ia32. Some SIMD operations were using
// instructions that required aligned operands (like movaps and movapd), but we
// don't have the right memory alignment yet, see https://crbug.com/v8/9198,
// resulting in a SIGSEGV when running the generated code.
const builder = new WasmModuleBuilder();
builder.addType(makeSig([], [kWasmI32]));
// Generate function 1 (out of 1).
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
// signature: i_v
// body:
kExprI32Const, 0xfc, 0xb6, 0xed, 0x02,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0xfc, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprI64x2Sub, 0x01,  // i64x2.sub
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0x81, 0x96, 0xf0, 0xe3, 0x07,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprF64x2Max, 0x01,  // f64x2.max
kSimdPrefix, kExprI64x2Sub, 0x01,  // i64x2.sub
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x0b,  // i32.const
kExprI32LtU,  // i32.lt_u
kSimdPrefix, kExprI8x16ReplaceLane, 0x00,  // i8x16.replace_lane
kExprI32Const, 0xfc, 0xf8, 0x01,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kSimdPrefix, kExprF64x2Max, 0x01,  // f64x2.max
kSimdPrefix, kExprI16x8MaxS, 0x01,  // i16x8.max_s
kSimdPrefix, kExprI8x16AllTrue,  // i8x16.all_true
kExprEnd,  // end @70
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
print(instance.exports.main());
