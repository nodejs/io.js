// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addStruct([]);
builder.addStruct([makeField(wasmRefType(kWasmFuncRef), false)], 0);
builder.addStruct([], 0);
builder.addArray(kWasmI32, true);
builder.addType(makeSig([], [kWasmI32]));
builder.addFunction(undefined, 4 /* sig */)
  .addLocals(kWasmI32, 2)
  .addLocals(wasmRefType(kWasmFuncRef), 1)
  .addBodyWithEnd([
kExprRefNull, 0x04,  // ref.null
kExprRefAsNonNull,  // ref.as_non_null
kGCPrefix, kExprRefCastNull, 0x04,  // ref.cast null
kGCPrefix, kExprRefCast, 0x04,  // ref.cast
kGCPrefix, kExprStructNew, 0x01,  // struct.new
kGCPrefix, kExprStructGet, 0x01, 0x00,  // struct.get
kGCPrefix, kExprStructNew, 0x01,  // struct.new
kGCPrefix, kExprStructGet, 0x01, 0x00,  // struct.get
kGCPrefix, kExprStructNew, 0x01,  // struct.new
kGCPrefix, kExprStructGet, 0x01, 0x00,  // struct.get
kExprLocalSet, 0x02,  // local.set
kGCPrefix, kExprStructNewDefault, 0x00,  // struct.new_default
kGCPrefix, kExprRefCast, 0x00,  // ref.cast
kExprDrop,
kExprI32Const, 0,
kExprEnd,
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertTraps(kTrapNullDereference, () => instance.exports.main());
