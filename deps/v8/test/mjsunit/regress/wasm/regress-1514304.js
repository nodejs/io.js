// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-inlining --wasm-tiering-budget=10

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addType(makeSig([], []));
builder.addMemory(16, 32);
builder.addTag(makeSig([], []));

builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprCallFunction, 0x01,  // call function #2: v_v
kExprCallFunction, 0x01,  // call function #2: v_v
kExprUnreachable,
kExprEnd,  // end @427
]);

builder.addFunction(undefined, 1 /* sig */)
  .addBodyWithEnd([
// signature: v_v
// body:
kExprI32Const, 0x00,  // i32.const
kExprF64Const, 0xad, 0x83, 0xa5, 0x76, 0x18, 0xc5, 0x55, 0xf6,  // f64.const
kExprF64Const, 0x86, 0x09, 0xf0, 0x8f, 0x94, 0xc0, 0x49, 0x94,  // f64.const
kExprF64Div,  // f64.div
kNumericPrefix, kExprI32UConvertSatF64,  // i32.trunc_sat_f64_u
kAtomicPrefix, kExprI32AtomicAdd8U, 0x00, 0x38,  // i32.atomic.rmw8.add_u
kExprIf, 0x40,  // if @272
  kExprI32Const, 0xbf, 0xd7, 0xaf, 0xf5, 0x05,  // i32.const
  kExprI32Const, 0xfa, 0x8c, 0x8f, 0xcf, 0x7c,  // i32.const
  kExprI32Const, 0xe2, 0x92, 0xec, 0xf7, 0x7c,  // i32.const
  kExprRefFunc, 0x00,  // ref.func
  kExprCallRef, 0x00,  // call_ref: i_iii
  kExprDrop,  // drop
  kExprEnd,  // end @297
kExprEnd,  // end @298
]);
builder.addExport('main', 0);

const instance = builder.instantiate();
assertThrows(() => instance.exports.main(1, 2, 3), RangeError);
