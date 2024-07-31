// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestRefEq() {
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(wasmRefType(kWasmEqRef), true);

  builder.addFunction("equal", makeSig([], [kWasmI32]))
    .addBody([
      kExprI32Const, 0,
      kGCPrefix, kExprRefI31,
      kExprI32Const, 0,
      kGCPrefix, kExprRefI31,
      kGCPrefix, kExprArrayNewFixed, array, 1,
      kExprI32Const, 0,
      kGCPrefix, kExprArrayGet, array,
      kExprRefEq
    ])
    .exportFunc()

  let instance = builder.instantiate();

  assertEquals(1, instance.exports.equal());
})();
