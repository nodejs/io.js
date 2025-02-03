// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32);
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addFunction(undefined, 0 /* sig */).addBody([
  kExprI64Const,    0x7a,                          // i64.const
  kExprI64Const,    0x42,                          // i64.const
  kExprI64Const,    0xb4, 0xbd, 0xeb, 0xb5, 0x72,  // i64.const
  kExprI32Const,    0x37,                          // i32.const
  kExprI32Const,    0x67,                          // i32.const
  kExprI32Const,    0x45,                          // i32.const
  kExprLoop,        0,                             // loop
  kExprLocalGet,    0,                             // local.get
  kExprBrIf,        1,                             // br_if depth=1
  kExprLocalGet,    1,                             // local.get
  kExprLocalGet,    0,                             // local.get
  kExprMemorySize,  0,                             // memory.size
  kExprLocalTee,    0,                             // local.tee
  kExprLocalGet,    0,                             // local.get
  kExprBrIf,        0,                             // br_if depth=0
  kExprUnreachable,                                // unreachable
  kExprEnd,                                        // end
  kExprUnreachable,                                // unreachable
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertEquals(16, instance.exports.main(0, 0, 0));
