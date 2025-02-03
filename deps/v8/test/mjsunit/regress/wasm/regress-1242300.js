// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32);
builder.addFunction(undefined, kSig_i_iii)
  .addBody([
    kExprI32Const, 0x7f,  // i32.const
    kExprI32Const, 0x1e,  // i32.const
    kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
    kExprI32Const, 0,  // i32.const
    kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
    kExprI32Const, 0,  // i32.const
    kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
    kSimdPrefix, kExprS128Select,  // s128.select
    kSimdPrefix, kExprS128Load32Lane, 0x00, 0x89, 0xfe, 0x03, 0x00,  // s128.load32_lane
    kExprUnreachable,
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(1, 2, 3));
