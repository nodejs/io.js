// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();
let supertype = builder.addStruct([]);
let sub1 = builder.addStruct([makeField(kWasmI32, true)], supertype);
let sub2 = builder.addStruct([makeField(kWasmF64, true)], supertype);

let crash = builder.addFunction("crash", kSig_v_i).exportFunc()
 .addLocals(wasmRefNullType(sub1), 1)
 .addBody([
   kGCPrefix, kExprStructNewDefault, sub1,
   kExprLocalSet, 1,
   kExprLocalGet, 0,
   kExprI32Eqz,
   kExprIf, kWasmVoid,
     kExprLocalGet, 1,
     kGCPrefix, kExprStructGet, sub1, 0,
     kExprDrop,
   kExprElse,
     kExprLocalGet, 1,
     kGCPrefix, kExprRefCast, sub2,
     kGCPrefix, kExprStructGet, sub2, 0,
     kExprDrop,
   kExprEnd]);

let instance = builder.instantiate();
instance.exports.crash(0);
assertThrows(() => instance.exports.crash(1),
             WebAssembly.RuntimeError, 'illegal cast');
