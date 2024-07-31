// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.startRecGroup();
builder.addStruct([]);
builder.endRecGroup();
builder.startRecGroup();
builder.addArray(kWasmI32, true);
builder.endRecGroup();
builder.startRecGroup();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.endRecGroup();
builder.addType(makeSig([], []));
builder.addMemory(16, 32);
builder.addTable(kWasmFuncRef, 1, 1, undefined)
builder.addActiveElementSegment(
    0, wasmI32Const(0), [[kExprRefFunc, 0, ]], kWasmFuncRef);
builder.addTag(makeSig([], []));
// Generate function 1 (out of 1).
builder.addFunction(undefined, 2 /* sig */)
  .addLocals(wasmRefType(kWasmAnyRef), 1)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprRefNull, 0x6e,  // ref.null
kGCPrefix, kExprExternConvertAny,  // extern.convert_any
kGCPrefix, kExprAnyConvertExtern,  // any.convert_extern
kExprRefAsNonNull,  // ref.as_non_null
kExprLocalSet, 0x03,  // local.set
kExprI32Const, 0x84, 0xe8, 0xf0, 0xff, 0x02,  // i32.const
kExprEnd,  // end @19
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertTraps(kTrapNullDereference, () => instance.exports.main(1, 2, 3));
