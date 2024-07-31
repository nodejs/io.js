// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.startRecGroup();
builder.addStruct([]);
builder.endRecGroup();
builder.startRecGroup();
builder.addStruct([]);
builder.endRecGroup();
builder.startRecGroup();
builder.addStruct([]);
builder.endRecGroup();
builder.startRecGroup();
builder.addArray(kWasmI32, true);
builder.endRecGroup();
builder.startRecGroup();
builder.addArray(kWasmI32, true);
builder.endRecGroup();
builder.startRecGroup();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.endRecGroup();
builder.addType(makeSig([], []));
builder.addMemory(16, 32);
builder.addPassiveDataSegment([]);
builder.addTable(kWasmFuncRef, 1, 1, undefined)
builder.addActiveElementSegment(0, wasmI32Const(0), [[kExprRefFunc, 0, ]], kWasmFuncRef);
builder.addTag(makeSig([], []));
// Generate function 1 (out of 1).
builder.addFunction(undefined, 5 /* sig */)
  .addLocals(wasmRefType(4), 1)
  .addBodyWithEnd([
  // signature: i_iii
  // body:
  kExprI32Const, 0xca, 0xfc, 0x9e, 0x85, 0x78,  // i32.const
  kExprI32Const, 0xcd, 0xdf, 0xb0, 0x90, 0x7a,  // i32.const
  kGCPrefix, kExprArrayNewData, 0x04, 0x00,  // array.new_data
  kExprLocalSet, 0x03,  // local.set
  kExprI32Const, 0xdc, 0xfc, 0xd9, 0xf2, 0x01,  // i32.const
  kExprEnd,  // end @28
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertThrows(
    () => instance.exports.main(1, 2, 3), WebAssembly.RuntimeError,
    `data segment out of bounds`);
