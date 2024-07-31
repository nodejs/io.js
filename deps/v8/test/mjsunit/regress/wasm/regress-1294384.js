// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([], [kWasmI32]));
builder.addType(makeSig([kWasmF64, kWasmF32, kWasmF32, kWasmF32,
                         kWasmF32, kWasmF64, kWasmF64],
                        [kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32,
                         kWasmF32, kWasmF32, kWasmF32, kWasmF32, kWasmF32]));

// Generate function 1 (out of 2).
// signature: i_iii
builder.addFunction(undefined, 0 /* sig */)
  .addBody([
    // body:
    kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // f64.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // f64.const
    kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // f64.const
    kExprCallFunction, 0x01,
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprI32SConvertF32]);  // i32.trunc_f32_s

// Generate function 2 (out of 2).
builder.addFunction(undefined, 1 /* sig */)
  .addBody([
    // body:
    kExprF32Const, 0x04, 0x04, 0x05, 0x04,  // f32.const
    kExprLoop, 0x40,  // loop @24
      kExprEnd,  // end @26
    kExprF32Ceil,  // f32.ceil
    kExprF32Const, 0x08, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprI32Const, 0x00,  // i32.const
    kExprBrIf, 0x00,  // br_if depth=0
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprF32Ceil,  // f32.ceil
    kExprF32Ceil,  // f32.ceil
    kExprF32Const, 0xed, 0xed, 0xed, 0xed,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x65, 0x73, 0x61, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprI64Const, 0x00,  // i64.const
    kExprF32SConvertI64]);  // f32.convert_i64_s

builder.addExport('main', 0);
const instance = builder.instantiate();
assertEquals(1, instance.exports.main());
