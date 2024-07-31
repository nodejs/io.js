// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

// Tests that we do not define two safepoints at the same offset.

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
builder.addActiveElementSegment(0, wasmI32Const(0), [[kExprRefFunc, 0, ]], kWasmFuncRef);
builder.addTag(makeSig([], []));
// Generate function 1 (out of 1).
builder.addFunction(undefined, 2 /* sig */)
  .addLocals(wasmRefType(kWasmFuncRef), 1)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprRefFunc, 0x00,  // ref.func
kGCPrefix, kExprRefCastNull, 0x02,  // ref.cast null
kExprRefAsNonNull,  // ref.as_non_null
kExprLocalSet, 0x03,  // local.set
kExprI32Const, 0xfa, 0xab, 0xc5, 0xeb, 0x7a,  // i32.const
kExprEnd,  // end @18
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
try {
  print(instance.exports.main(1, 2, 3));
} catch (e) {
  print('caught exception', e);
}
