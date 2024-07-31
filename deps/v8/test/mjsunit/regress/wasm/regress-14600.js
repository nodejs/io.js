// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.startRecGroup();
let array = builder.addArray(kWasmF64, true);
let array_subtype = builder.addArray(kWasmF64, true, array, true);
let func = builder.addType(makeSig([], [kWasmF64]))
let array_array = builder.addArray(wasmRefNullType(array_subtype), false);
let array_array_subtype = builder.addArray(wasmRefNullType(array_subtype), false, array_array, true);
builder.endRecGroup();
builder.addFunction("main", func)
  .addLocals(kWasmI32, 1)
  .addBody([
    kExprLoop, 0x7d,  // loop @3 f32
      kExprLocalGet, 0x00,  // local.get
      kExprIf, 0x40,  // if @7
        kExprUnreachable,  // unreachable
      kExprEnd,  // end @10
      kExprI32Const, 0x00,  // i32.const
      kGCPrefix, kExprArrayNewDefault, 0x03,  // array.new_default
      kGCPrefix, kExprRefTest, 0x04,  // ref.test
      kExprLocalTee, 0x00,  // local.tee
      kExprBrIf, 0x00,  // br_if depth=0
      kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
    kExprEnd,  // end @28
    kExprDrop,  // drop
    kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // f64.const
]).exportFunc();
const instance = builder.instantiate();
assertEquals(0, instance.exports.main());
