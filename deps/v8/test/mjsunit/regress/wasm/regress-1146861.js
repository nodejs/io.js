// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addGlobal(kWasmI32, true, false);
builder.addType(makeSig([], [kWasmF64]));
// Generate function 1 (out of 1).
builder.addFunction(undefined, 0 /* sig */)
  .addLocals(kWasmI32, 8).addLocals(kWasmI64, 3)
  .addBodyWithEnd([
// signature: d_v
// body:
kExprGlobalGet, 0x00,  // global.get
kExprLocalSet, 0x00,  // local.set
kExprI32Const, 0x00,  // i32.const
kExprI32Eqz,  // i32.eqz
kExprLocalSet, 0x01,  // local.set
kExprGlobalGet, 0x00,  // global.get
kExprLocalSet, 0x02,  // local.set
kExprI32Const, 0x01,  // i32.const
kExprI32Const, 0x01,  // i32.const
kExprI32Sub,  // i32.sub
kExprLocalSet, 0x03,  // local.set
kExprGlobalGet, 0x00,  // global.get
kExprLocalSet, 0x04,  // local.set
kExprI32Const, 0x00,  // i32.const
kExprI32Eqz,  // i32.eqz
kExprLocalSet, 0x05,  // local.set
kExprGlobalGet, 0x00,  // global.get
kExprLocalSet, 0x06,  // local.set
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x01,  // i32.const
kExprI32Sub,  // i32.sub
kExprLocalSet, 0x07,  // local.set
kExprBlock, kWasmVoid,  // block @45
  kExprI32Const, 0x00,  // i32.const
  kExprIf, kWasmVoid,  // if @49
    kExprLocalGet, 0x0a,  // local.get
    kExprLocalSet, 0x08,  // local.set
  kExprElse,  // else @55
    kExprNop,  // nop
    kExprEnd,  // end @57
  kExprLocalGet, 0x08,  // local.get
  kExprLocalSet, 0x09,  // local.set
  kExprLocalGet, 0x09,  // local.get
  kExprI64Const, 0xff, 0x01,  // i64.const
  kExprI64Add,  // i64.add
  kExprDrop,  // drop
  kExprEnd,  // end @69
kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,  // f64.const
kExprEnd,  // end @79
]);
builder.instantiate();
