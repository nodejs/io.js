// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(1, 1, true);
builder.addType(
    makeSig([kWasmF64, kWasmI32, kWasmI32, kWasmF64, kWasmF32], [kWasmI64]));
builder.addFunction(undefined, 0 /* sig */)
  .addLocals(kWasmI64, 1)
  .addBodyWithEnd([
// signature: l_diidf
// body:
kExprLoop, 0x7e,  // loop @3 i64
  kExprI64Const, 0x01,  // i64.const
  kExprEnd,  // end @7
kExprBlock, 0x7f,  // block @8 i32
  kExprLocalGet, 0x05,  // local.get
  kExprLocalSet, 0x05,  // local.set
  kExprI32Const, 0x00,  // i32.const
  kExprEnd,  // end @16
kExprLocalGet, 0x05,  // local.get
kExprLocalGet, 0x05,  // local.get
kAtomicPrefix, kExprI64AtomicCompareExchange, 0x03, 0x04,
kExprI64GtS,  // i64.gt_s
kExprDrop,  // drop
kExprI64Const, 0x01,  // i64.const
kExprEnd,  // end @29
]);
const instance = builder.instantiate();
