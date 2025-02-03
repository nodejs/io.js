// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let array = builder.addArray(kWasmI64, true);

builder.addFunction("createArray", makeSig([], [kWasmAnyRef])).exportFunc()
  .addBody([
    kExprI32Const, 10, // size
    kGCPrefix, kExprArrayNewDefault, array,
  ]);

builder.addFunction("main", makeSig([wasmRefNullType(array)], [kWasmI64]))
    .addLocals(wasmRefNullType(array), 1)
    .addBody([
      kExprLocalGet, 0,
      ...wasmI32Const(0xffffffe),
      kGCPrefix, kExprArrayGet, array,
    ]).exportFunc();

let wasm = builder.instantiate().exports;
assertThrows(() => wasm.main(wasm.createArray()), WebAssembly.RuntimeError,
            "array element access out of bounds");
