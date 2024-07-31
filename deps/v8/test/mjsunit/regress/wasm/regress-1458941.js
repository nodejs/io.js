// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

// Regression test for https://crbug.com/1458941.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function foo() {
    let builder = new WasmModuleBuilder();
    let array = builder.addArray(kWasmI32);
    builder.addFunction(`brOnCastFail`,
                        makeSig([], [kWasmI32]))
        .addBody([
            kExprBlock, kWasmRef, array,
            kGCPrefix, kExprArrayNewFixed, array, 0,
            ...wasmBrOnCastFail(0, wasmRefType(array), wasmRefType(array)),
            kGCPrefix, kExprArrayLen,
            kExprDrop,
            kExprI32Const, 0,
            kExprReturn,
            kExprEnd,
            kExprDrop,
            kExprI32Const, 1,
        ]).exportFunc();

    let instance = builder.instantiate();
    let wasm = instance.exports;

    assertEquals(0, wasm.brOnCastFail());
})();
