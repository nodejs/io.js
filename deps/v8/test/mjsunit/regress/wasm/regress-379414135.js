// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --no-enable-avx --no-liftoff

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();
let foo = builder.addImport("m" ,"i", kSig_v_v);
builder.addFunction("main", kSig_l_v)
.addBody([
    kExprI64Const, 1,
    ...SimdInstr(kExprI64x2Splat),
    kExprCallFunction, foo,
    ...SimdInstr(kExprI64x2Neg),
    ...SimdInstr(kExprI64x2ExtractLane), 0x1,
]).exportFunc();
let instance = builder.instantiate({'m': {'i': () => {}}});
assertEquals(-1n, instance.exports.main());
