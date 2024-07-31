// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addMemory(1, 32);
builder.addTable(wasmRefType(0), 2, 22, [kExprRefFunc, 0])
// Generate function 1 (out of 1).
builder.addFunction('main', 0 /* sig */)
  .addBody([
    kExprRefFunc, 0x00,  // ref.func
    kExprI32Const, 0x00,  // i32.const 0
    kExprMemoryGrow, 0x00,  // memory.grow
    kNumericPrefix, kExprTableGrow, 0x00,  // table.grow
    kExprUnreachable,  // unreachable
]).exportFunc();
const instance = builder.instantiate();
assertTraps(kTrapUnreachable, instance.exports.main);
