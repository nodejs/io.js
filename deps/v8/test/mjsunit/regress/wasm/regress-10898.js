// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(1, 1, true);
builder.addActiveDataSegment(0, [kExprI32Const, 2], [0x12, 0x00, 0x1c]);
builder.addActiveDataSegment(0, [kExprI32Const, 17],
    [0x00, 0x00, 0x96, 0x00, 0x00, 0x00, 0xc2, 0x00, 0xb33, 0x03, 0xf6, 0x0e]);
builder.addActiveDataSegment(0, [kExprI32Const, 41],
    [0x00, 0xdb, 0xa6, 0xa6, 0x00, 0xe9, 0x1c, 0x06, 0xac]);
builder.addActiveDataSegment(0, [kExprI32Const, 57],
                             [0x00, 0x00, 0x00, 0x00, 0xda, 0xc0, 0xbe]);
builder.addType(makeSig([kWasmI32], [kWasmI32]));
builder.addType(makeSig([kWasmF32], [kWasmF32]));
builder.addType(makeSig([kWasmF64], [kWasmF64]));
// Generate function 1 (out of 3).
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
// signature: i_i
// body:
kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
kExprF32Le,  // f32.le
kExprLocalTee, 0x00,  // local.tee
kExprI32Const, 0xff, 0x00,  // i32.const
kAtomicPrefix, kExprAtomicNotify, 0x02, 0x03,  // atomic.notify
kExprI32LoadMem16S, 0x00, 0x02,  // i32.load16_s
kExprIf, kWasmVoid,  // if @28
  kExprLocalGet, 0x00,  // local.get
  kExprReturn,  // return
kExprElse,  // else @33
  kExprUnreachable,  // unreachable
  kExprEnd,  // end @35
kExprUnreachable,  // unreachable
kExprEnd,  // end @37
]);
builder.addExport('func_194', 0);
let instance = builder.instantiate();

assertEquals(1, instance.exports.func_194(0));
