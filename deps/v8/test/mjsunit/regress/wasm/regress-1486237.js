// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addStruct([]);
builder.addArray(kWasmF32, true);
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.startRecGroup();
builder.addType(makeSig([
  kWasmI32,
  wasmRefNullType(kWasmStructRef),
  wasmRefNullType(1),
  wasmRefType(kWasmExternRef),
  wasmRefNullType(kWasmArrayRef),
  kWasmI32,
  wasmRefNullType(kWasmStructRef),
], []));
builder.endRecGroup();
builder.addType(makeSig([], []));
builder.addMemory(16, 32);
builder.addPassiveDataSegment([72, 149, 203, 199, 152, 103, 153, 179]);
builder.addTable(kWasmFuncRef, 2, 2, undefined)
builder.addActiveElementSegment(0, wasmI32Const(0),
    [[kExprRefFunc, 0, ], [kExprRefFunc, 1, ]], kWasmFuncRef);
builder.addTag(makeSig([], []));
// Generate function 1 (out of 2).
builder.addFunction(undefined, 2 /* sig */)
  .addLocals(kWasmF64, 1).addLocals(wasmRefNullType(kWasmStructRef), 2)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprF32Const, 0xdd, 0x44, 0x44, 0x06,  // f32.const
kExprTry, 0x7f,  // try @10 i32
  kExprI32Const, 0xab, 0xfa, 0x8e, 0xaa, 0x78,  // i32.const
  kExprDelegate, 0x00,  // delegate
  kExprI32Const, 0x18,  // i32.const
  kExprLocalGet, 0x04,  // local.get
  kGCPrefix, kExprRefCastNull, 0x00,  // ref.cast null
  kExprLocalGet, 0x04,  // local.get
  kGCPrefix, kExprRefCastNull, 0x01,  // ref.cast null
  kExprRefNull, 0x6f,  // ref.null
  kExprRefAsNonNull,  // ref.as_non_null
  kExprRefNull, 0x6e,  // ref.null
  kGCPrefix, kExprRefCastNull, 0x6a,  // ref.cast null
  kExprI32Const, 0xc9, 0x87, 0xfc, 0xe7, 0x06,  // i32.const
  kExprRefNull, 0x6b,  // ref.null
  kExprCallFunction, 0x01,  // call function #1:
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprI32Const, 1,
  kExprEnd,  // end @147
]);
// Generate function 2 (out of 2).
builder.addFunction(undefined, 3 /* sig */)
  .addBody([]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertThrows(() => instance.exports.main(1, 2, 3));
