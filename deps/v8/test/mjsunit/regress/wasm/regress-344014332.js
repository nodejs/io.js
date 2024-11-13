// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let array = builder.addArray(kWasmI64, true);

builder.addFunction("main", makeSig([kWasmI32], [kWasmAnyRef])).exportFunc()
    .addLocals(wasmRefNullType(array), 1)
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewDefault, array,
      kExprLocalTee, 1,
      ...wasmI32Const(0xffffffe),
      kExprI64Const, 42,
      kGCPrefix, kExprArraySet, array,
      kExprLocalGet, 1,
    ]);

const instance = builder.instantiate();
assertThrows(() => instance.exports.main(100), WebAssembly.RuntimeError,
            "array element access out of bounds");
