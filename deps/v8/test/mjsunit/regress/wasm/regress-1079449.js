// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32, true);
const sig = builder.addType(makeSig(
    [
      kWasmI64, kWasmI32, kWasmI64, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
      kWasmI32, kWasmI32, kWasmI64, kWasmI64, kWasmI64
    ],
    [kWasmI64]));
// Generate function 2 (out of 3).
builder.addFunction(undefined, sig)
    .addLocals(kWasmF32, 10)
    .addLocals(kWasmI32, 4)
    .addLocals(kWasmF64, 1)
    .addLocals(kWasmI32, 15)
    .addBodyWithEnd([
      // signature: v_liliiiiiilll
      // body:
      kExprI32Const, 0x00,  // i32.const
      kExprI64Const, 0x00,  // i64.const
      kExprI64Const, 0x00,  // i64.const
      kAtomicPrefix, kExprI64AtomicCompareExchange, 0x03,
      0x8,      // i64.atomic.cmpxchng64
      kExprEnd,  // end @124
    ]);

builder.addExport('main', 0);
const instance = builder.instantiate();
assertEquals(
    0n, instance.exports.main(1n, 2, 3n, 4, 5, 6, 7, 8, 9, 10n, 11n, 12n, 13n));
