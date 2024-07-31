// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addGlobal(kWasmI32, true, false);
sig0 = makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]);
builder.addFunction(undefined, sig0)
  .addBody([
kExprF32Const, 0x01, 0x00, 0x00, 0x00,
kExprF32Const, 0x00, 0x00, 0x00, 0x00,
kExprF32Eq,  // --> i32:0
kExprF32Const, 0xc9, 0xc9, 0x69, 0xc9,
kExprF32Const, 0xc9, 0xc9, 0xc9, 0x00,
kExprF32Eq,  // --> i32:0 i32:0
kExprIf, kWasmF32,
  kExprF32Const, 0x00, 0x00, 0x00, 0x00,
kExprElse,   // @32
  kExprF32Const, 0x00, 0x00, 0x00, 0x00,
  kExprEnd,   // --> i32:0 f32:0
kExprF32Const, 0xc9, 0x00, 0x00, 0x00,
kExprF32Const, 0xc9, 0xc9, 0xc9, 0x00,
kExprF32Const, 0xc9, 0xc9, 0xa0, 0x00, // --> i32:0 f32:0 f32 f32 f32
kExprF32Eq,  // --> i32:0 f32:0 f32 i32:0
kExprIf, kWasmF32,
  kExprF32Const, 0x00, 0x00, 0x00, 0x00,
kExprElse,
  kExprF32Const, 0x00, 0x00, 0x00, 0x00,
  kExprEnd,  // --> i32:0 f32:0 f32 f32:0
kExprF32Eq,  // --> i32:0 f32:0 i32:0
kExprIf, kWasmF32,
  kExprF32Const, 0x00, 0x00, 0x00, 0x00,
kExprElse,
  kExprF32Const, 0x00, 0x00, 0x00, 0x00,
  kExprEnd,   // --> i32:0 f32:0 f32:0
kExprF32Const, 0xc9, 0xc9, 0xff, 0xff,  // --> i32:0 f32:0 f32:0 f32
kExprF32Eq,  // --> i32:0 f32:0 i32:0
kExprDrop,
kExprDrop, // --> i32:0
kExprI32Const, 1, // --> i32:0 i32:1
kExprI32GeU,  // --> i32:0
          ]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertEquals(0, instance.exports.main(1, 2, 3));
