// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([kWasmF64, kWasmF64, kWasmI32], [kWasmI64]));
builder.addMemory(1, 1, true);
builder.addFunction('main', 0 /* sig */)
  .addBody([
    kExprI32Const, 0x00,  // i32.const
    kExprI64Const, 0x00,  // i64.const
    kAtomicPrefix, kExprI64AtomicOr, 0x03, 0x00,  // i64.atomic.rmw.or
]).exportFunc();
const instance = builder.instantiate();
print(instance.exports.main(1, 2, 3));
