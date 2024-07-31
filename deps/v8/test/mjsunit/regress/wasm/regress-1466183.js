// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

builder.addStruct([makeField(kWasmI32, true)]);
builder.addStruct([makeField(wasmRefNullType(0), true)]);

builder.addTable(wasmRefType(1), 1, 2, [kGCPrefix, kExprStructNewDefault, 1]);

builder.addFunction(undefined, kSig_i_v)
  .addBody([
    kExprI32Const, 0,  // i32.const
    kExprTableGet, 0x0,  // table.get
    kGCPrefix, kExprStructGet, 0x01, 0x00,  // struct.get
    kExprRefNull, 0,
    kExprRefEq]);

builder.addExport('main', 0);
const instance = builder.instantiate();
assertEquals(1, instance.exports.main());
