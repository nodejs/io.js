// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --liftoff-only

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32);
builder.addType(makeSig([], [kWasmI32]));
builder.addTable(kWasmFuncRef, 1, 1, undefined)
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
kExprI32Const, 0x00,
kExprI32Const, 0x00,
kExprTableGet, 0x00,
kExprI32Const, 0xff, 0x01,
kNumericPrefix, kExprTableGrow, 0x00,
kExprF32Const, 0x00, 0x00, 0x00, 0x00,
kExprF32StoreMem, 0x00, 0x01,
kExprEnd
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertThrows(() => instance.exports.main(), WebAssembly.RuntimeError);
