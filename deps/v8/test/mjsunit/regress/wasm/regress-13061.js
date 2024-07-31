// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

builder.addFunction('repro', kSig_v_v)
  .exportFunc()
  .addLocals(wasmRefNullType(kWasmStructRef), 1)
  .addBody([
    kExprI32Const, 0,
    kExprIf, kWasmVoid,
      kExprLoop, kWasmVoid,
        kExprCallFunction, 0,
        kExprLocalGet, 0,
        kExprRefAsNonNull,
        kExprLocalSet, 0,
        kExprI32Const, 0,
        kExprBrIf, 0,
      kExprEnd,
    kExprEnd,
  ]);

let instance = builder.instantiate();
instance.exports.repro();
