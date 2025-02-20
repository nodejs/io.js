// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-inlining

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");


(function InliningIntoTryBlockWithPhi() {
  let builder = new WasmModuleBuilder();
  let tag = builder.addTag(makeSig([], []));
  let sig = builder.addType(makeSig([], [kWasmI32]));

  let callee = builder.addFunction('callee', makeSig([], [kWasmI32, kWasmI32]))
      .addBody([
        kExprTry, kWasmVoid,
        kExprCatch, tag,
          kExprI32Const, 2,
          kExprI32Const, 2,
          kExprReturn,
        kExprEnd,
        kExprI32Const, 3,
        kExprI32Const, 3,
      ]).exportFunc();

  builder.addFunction('main',
      makeSig([kWasmI32], [kWasmI32]))
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprRefNull, sig, kExprDrop,
      kExprTry, sig,
        kExprCallFunction, callee.index,
        kExprI32Add,
      kExprCatch, tag,
        kExprI32Const, 2,
      kExprEnd,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  assertEquals(6, wasm.main());
})();
