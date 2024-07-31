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
kExprI32Const, 0x41,  // i32.const
kExprI32Const, 0x41,  // i32.const
kExprF64Const, 0x5f, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,  // f64.const
kExprI64Const, 0xe9, 0x38,  // i64.const
kExprF64Const, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,  // f64.const
kExprF64Const, 0x44, 0x67, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,  // f64.const
kExprF64Const, 0x44, 0x44, 0x44, 0x61, 0x41, 0x41, 0x41, 0x41,  // f64.const
kExprI32Const, 0x41,  // i32.const
kExprRefFunc, 0x00,  // ref.func
kExprLocalGet, 0x00,  // local.get
kExprI64LoadMem16U, 0x01, 0x43,  // i64.load16_u
kExprF32Const, 0x43, 0x43, 0x43, 0x43,  // f32.const
kExprI32Const, 0x1d,  // i32.const
kExprI32Const, 0x41,  // i32.const
kExprI32And,  // i32.and
kExprI32Const, 0x6d,  // i32.const
kExprI32Popcnt,  // i32.popcnt
kExprBrTable, 0x00, 0x00,  // br_table entries=0
kExprUnreachable,  // unreachable
kExprI64LoadMem16U, 0x00, 0x67,  // i64.load16_u
kExprF32UConvertI64,  // f32.convert_i64_u
kExprI64Const, 0x00,  // i64.const
kExprF32Const, 0x43, 0x43, 0xc9, 0x43,  // f32.const
kExprF32Const, 0x43, 0x43, 0x43, 0x43,  // f32.const
kExprF32Const, 0x93, 0x43, 0x43, 0x43,  // f32.const
kExprI32ReinterpretF32,  // i32.reinterpret_f32
kExprF32Const, 0x43, 0x43, 0x43, 0x43,  // f32.const
kExprF32Const, 0x43, 0x43, 0x43, 0x43,  // f32.const
kExprF32Const, 0x43, 0x43, 0x43, 0x43,  // f32.const
kExprF32NearestInt,  // f32.nearest
kExprF32Mul,  // f32.mul
kExprF32Const, 0x43, 0x43, 0x43, 0x43,  // f32.const
kExprF32Add,  // f32.add
kExprI64UConvertF32,  // i64.trunc_f32_u
kExprF64UConvertI64,  // f64.convert_i64_u
kExprI32UConvertF64,  // i32.trunc_f64_u
kExprF32Const, 0x43, 0x43, 0x43, 0x43,  // f32.const
kExprF32Const, 0x43, 0x43, 0x43, 0x43,  // f32.const
kExprF32Const, 0x43, 0x43, 0x43, 0x00,  // f32.const
kExprRefNull, 0x00,  // ref.null
kExprUnreachable,  // unreachable
kExprUnreachable,  // unreachable
kExprUnreachable,  // unreachable
kExprUnreachable,  // unreachable
kExprEnd,  // end @139
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
try {
  print(instance.exports.main(1, 2, 3));
} catch (e) {  print('caught exception', e);}
