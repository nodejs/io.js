// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

builder.addMemory(1, 10);
let tag0 = builder.addTag(kSig_v_l);
let tag1 = builder.addTag(kSig_v_i);

builder.addFunction("main", kSig_i_v).exportFunc().addBody([
  kExprTry, kWasmVoid,
    kExprI32Const, 0,
    kAtomicPrefix, kExprI64AtomicLoad8U, 0 /*align*/, 0 /*offset*/,
    kExprThrow, tag0,
  kExprCatch, tag0,
    kExprI32Const, 42,
    kExprReturn,
  kExprEnd,
  kExprI32Const, 123,
]);

builder.addFunction("main32", kSig_i_v).exportFunc().addBody([
  kExprTry, kWasmVoid,
    kExprI32Const, 0,
    kAtomicPrefix, kExprI32AtomicLoad8U, 0 /*align*/, 0 /*offset*/,
    kExprThrow, tag1,
  kExprCatch, tag1,
    kExprI32Const, 42,
    kExprReturn,
  kExprEnd,
  kExprI32Const, 123,
]);

assertEquals(42, builder.instantiate().exports.main());
assertEquals(42, builder.instantiate().exports.main32());
