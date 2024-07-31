// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

let struct = builder.addStruct([makeField(kWasmI32, true)]);

builder.addFunction('test', kSig_i_v)
    .exportFunc()
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprI32Const, 0,
      kExprIf, kWasmRef, struct,
      kGCPrefix, kExprStructNewDefault, struct,
      kExprElse,
      kGCPrefix, kExprStructNewDefault, struct,
      kExprEnd,
      kExprLocalTee, 0,
      kGCPrefix, kExprRefTestNull, kI31RefCode,
    ]);

builder.addFunction('cast', kSig_r_v)
    .exportFunc()
    .addLocals(kWasmAnyRef, 1)
    .addBody([
      kExprI32Const, 0,
      kExprIf, kWasmRef, struct,
      kGCPrefix, kExprStructNewDefault, struct,
      kExprElse,
      kGCPrefix, kExprStructNewDefault, struct,
      kExprEnd,
      kExprLocalTee, 0,
      kGCPrefix, kExprRefCastNull, kStructRefCode,
      kGCPrefix, kExprExternConvertAny,
    ]);

let instance = builder.instantiate();
let test = instance.exports.test;
let cast = instance.exports.cast;
assertEquals(0, test());
%WasmTierUpFunction(test);
assertEquals(0, test());

assertNotNull(cast());
%WasmTierUpFunction(cast);
assertNotNull(cast());
