// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32);
builder.addGlobal(kWasmI32, false, false);
const sig0 = makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]);
builder.addFunction(undefined, sig0).addBody([
  kExprI32Const, 1,  // i32.const 1
  kExprI32Const, 0,  // i32.const 0
  kExprI32Const, 3,  // i32.const 3
  kExprI32GeU,       // i32.ge_u
  kExprI32Rol,       // i32.rol
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertEquals(1, instance.exports.main());
