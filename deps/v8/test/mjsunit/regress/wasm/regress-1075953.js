// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(1, 1, true);
const sig = builder.addType(makeSig([], [kWasmI32]));

builder.addFunction(undefined, sig)
  .addLocals(kWasmI32, 1002).addLocals(kWasmI64, 3)
  .addBodyWithEnd([
// signature: i_v
// body:
  kExprLocalGet, 0xec, 0x07,  // local.get
  kExprLocalGet, 0xea, 0x07,  // local.set
  kExprLocalGet, 0x17,  // local.set
  kExprLocalGet, 0xb5, 0x01,  // local.set
  kExprI32Const, 0x00,  // i32.const
  kExprIf, kWasmI32,  // if @39 i32
    kExprI32Const, 0x91, 0xe8, 0x7e,  // i32.const
  kExprElse,  // else @45
    kExprI32Const, 0x00,  // i32.const
    kExprEnd,  // end @48
  kExprIf, kWasmVoid,  // if @49
    kExprI32Const, 0x00,  // i32.const
    kExprI32Const, 0x00,  // i32.const
    kAtomicPrefix, kExprI32AtomicSub, 0x02, 0x04,  // i32.atomic.sub
    kExprDrop,
  kExprEnd,
  kExprUnreachable,
kExprEnd
]);

const instance = builder.instantiate();
