// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const builder = new WasmModuleBuilder();
let struct_type = builder.addStruct([makeField(kWasmStructRef, false)]);
let struct_type2 =
    builder.addStruct([makeField(wasmRefType(kWasmStructRef), false)]);
let field_index = 0;

builder.addFunction('cast_i31', kSig_v_v)
  .exportFunc()
  .addBody([
    kExprRefNull, struct_type,
    kGCPrefix, kExprStructGet, struct_type, field_index,
    kGCPrefix, kExprRefCastNull, kI31RefCode,
    kExprDrop,
  ]);

builder.addFunction('cast_i31_nn', kSig_v_v)
  .exportFunc()
  .addBody([
    kExprRefNull, struct_type2,
    kGCPrefix, kExprStructGet, struct_type2, field_index,
    kGCPrefix, kExprRefCast, kI31RefCode,
    kExprDrop,
  ]);

builder.addFunction('cast_eq', kSig_v_v)
  .exportFunc()
  .addBody([
    kExprRefNull, struct_type,
    kGCPrefix, kExprStructGet, struct_type, field_index,
    kGCPrefix, kExprRefCastNull, kEqRefCode,
    kExprDrop,
  ]);

builder.addFunction('test_i31', kSig_v_v)
  .exportFunc()
  .addBody([
    kExprRefNull, struct_type,
    kGCPrefix, kExprStructGet, struct_type, field_index,
    kGCPrefix, kExprRefTestNull, kI31RefCode,
    kExprDrop,
  ]);

builder.addFunction('test_eq', kSig_v_v)
  .exportFunc()
  .addBody([
    kExprRefNull, struct_type,
    kGCPrefix, kExprStructGet, struct_type, field_index,
    kGCPrefix, kExprRefTestNull, kEqRefCode,
    kExprDrop,
  ]);

const instance = builder.instantiate();

assertThrows(() => instance.exports.cast_i31(), WebAssembly.RuntimeError,
             'dereferencing a null pointer');
assertThrows(() => instance.exports.cast_i31_nn(), WebAssembly.RuntimeError,
             'dereferencing a null pointer');
assertThrows(() => instance.exports.cast_eq(), WebAssembly.RuntimeError,
             'dereferencing a null pointer');
assertThrows(() => instance.exports.test_i31(), WebAssembly.RuntimeError,
             'dereferencing a null pointer');
assertThrows(() => instance.exports.test_eq(), WebAssembly.RuntimeError,
             'dereferencing a null pointer');
