// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $struct0 = builder.addStruct([makeField(kWasmI32, true)]);
let $array0 = builder.addArray(wasmRefType($struct0), true);

builder.addFunction("main", kSig_i_v).exportFunc()
  .addLocals(wasmRefType($struct0), 1)
  .addBody([
    // Allocate a struct with default field value 0.
    kGCPrefix, kExprStructNewDefault, $struct0,
    kExprLocalTee, 0,
    // Store it in an array (as default value) and reload it from there.
    // Make the array large enough that we initialize it via builtin call.
    ...wasmI32Const(50),  // array length
    kGCPrefix, kExprArrayNew, $array0,  // array
    kExprI32Const, 0,  // array index
    kGCPrefix, kExprArrayGet, $array0,  // struct
    // Overwrite the struct field's value.
    kExprI32Const, 42,
    kGCPrefix, kExprStructSet, $struct0, 0,
    // Reload the struct field's value.
    kExprLocalGet, 0,
    kGCPrefix, kExprStructGet, $struct0, 0,
  ]);

let instance = builder.instantiate();
assertEquals(42, instance.exports.main());
