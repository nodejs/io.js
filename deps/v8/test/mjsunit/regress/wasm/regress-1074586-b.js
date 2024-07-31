// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 32, true);
const sig = builder.addType(makeSig(
    [kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32],
    []));
builder.addFunction(undefined, sig).addBodyWithEnd([
  // signature: v_iiiiifidi
  // body:
  kExprI32Const, 0x00,                             // i32.const
  kExprI64Const, 0x00,                             // i64.const
  kAtomicPrefix, kExprI64AtomicStore, 0x03, 0x00,  // i64.atomic.store64
  kExprEnd,                                        // end @9
]);
builder.addExport('main', 0);
assertDoesNotThrow(() => builder.instantiate());
