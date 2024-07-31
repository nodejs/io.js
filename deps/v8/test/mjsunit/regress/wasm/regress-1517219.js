// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([], [kWasmI32]));
builder.addTable(kWasmFuncRef, 1, 1, undefined)
builder.addActiveElementSegment(0, wasmI32Const(0), [[kExprRefFunc, 0, ]], kWasmFuncRef);
builder.addFunction(undefined, 0 /* sig */)
  .addBody([
    kExprRefNull, 0x70,  // ref.null
    kExprI32Const, 0x8f, 0xdb, 0x9f, 0x90, 0x79,  // i32.const
    kNumericPrefix, kExprTableGrow, 0x00,  // table.grow
    kGCPrefix, kExprRefI31,  // ref.i31
    kGCPrefix, kExprI31GetU,  // i31.get_u
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertEquals(instance.exports.main(), 2147483647);
