// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();
let tag = builder.addTag(kSig_v_v);
builder.addFunction("main", kSig_v_v)
    .addBody([
      kExprTry, kWasmVoid,
        kExprTryTable, kWasmVoid, 0,
        kExprEnd,
        kExprThrow, tag,
      kExprCatchAll,
      kExprEnd
    ]).exportFunc();
let instance = builder.instantiate();
assertDoesNotThrow(instance.exports.main);
