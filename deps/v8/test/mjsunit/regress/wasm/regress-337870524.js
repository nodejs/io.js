// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $func0 = builder.addFunction("main", kSig_v_v).exportFunc()
  .addLocals(kWasmS128, 3)
  .addBody([
    kExprLocalGet, 0,
    kExprLocalTee, 1,
    kExprLocalGet, 1,

    kExprI32Const, 0,
    kExprIf, kWasmVoid,
    kExprElse,
    kExprEnd,
    kExprReturn,
  ]);

let instance = builder.instantiate();
instance.exports.main();
