// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addStruct([]);
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
// Generate function 1 (out of 1).
builder.addFunction(undefined, 1 /* sig */)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprI32Const, 0,
kGCPrefix, kExprRefI31,  // ref.i31
kGCPrefix, kExprRefCastNull, 0x6e,  // ref.cast null
kGCPrefix, kExprRefCastNull, 0x6b,  // ref.cast null
kGCPrefix, kExprRefTestNull, 0x00,  // ref.test null
kGCPrefix, kExprRefI31,  // ref.i31
kGCPrefix, kExprRefTest, 0x00,  // ref.test
kGCPrefix, kExprRefI31,  // ref.i31
kGCPrefix, kExprRefTestNull, kNullRefCode,  // ref.test null
kExprEnd,  // end @28
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertTraps(kTrapIllegalCast, () => instance.exports.main(1, 2, 3));
