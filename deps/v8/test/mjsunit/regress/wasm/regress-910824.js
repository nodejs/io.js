// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addGlobal(kWasmI32, false, false);
builder.addGlobal(kWasmF32, false, false);
builder.addType(makeSig([kWasmI32, kWasmF32, kWasmF32, kWasmF64], [kWasmI32]));
builder.addFunction(undefined, 0 /* sig */)
  .addLocals(kWasmI32, 504)
  .addBody([
kExprGlobalGet, 0x00,
kExprLocalSet, 0x04,
kExprLocalGet, 0x04,
kExprI32Const, 0x01,
kExprI32Sub,
kExprGlobalGet, 0x00,
kExprI32Const, 0x00,
kExprI32Eqz,
kExprGlobalGet, 0x00,
kExprI32Const, 0x01,
kExprI32Const, 0x01,
kExprI32Sub,
kExprGlobalGet, 0x00,
kExprI32Const, 0x00,
kExprI32Eqz,
kExprGlobalGet, 0x00,
kExprI32Const, 0x00,
kExprI32Const, 0x01,
kExprI32Sub,
kExprGlobalGet, 0x01,
kExprUnreachable,
]);
builder.instantiate();
