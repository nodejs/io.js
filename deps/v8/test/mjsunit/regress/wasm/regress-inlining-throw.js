// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tier up quickly to save time:
// Flags: --wasm-tiering-budget=100

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var builder = new WasmModuleBuilder();
let supertype = builder.addStruct([makeField(kWasmI32, true)]);
let subtype = builder.addStruct(
    [makeField(kWasmI32, true), makeField(kWasmI32, true)], supertype);
let unused_type = builder.addStruct(
    [makeField(kWasmI32, true), makeField(kWasmF64, true)], supertype);

let sig = makeSig([wasmRefNullType(supertype)], [kWasmI32]);

let callee1 = builder.addFunction('callee1', sig).addBody([
    kExprBlock, kWasmRef, subtype,
        kExprLocalGet, 0,
        kGCPrefix, kExprBrOnCast, 0b01, 0, supertype, subtype,
        kGCPrefix, kExprRefCast, unused_type,
        kGCPrefix, kExprStructGet, unused_type, 0,
        kExprReturn,
    kExprEnd,
    kGCPrefix, kExprStructGet, subtype, 1
]);

let callee2 = builder.addFunction('callee2', sig).addBody([
    kExprBlock, kWasmRef, subtype,
        kExprLocalGet, 0,
        kGCPrefix, kExprBrOnCast, 0b01, 0, supertype, subtype,
        kExprUnreachable,
        kExprReturn,
    kExprEnd,
    kGCPrefix, kExprStructGet, subtype, 1
]);

let callee3 = builder.addFunction('callee3', sig).addBody([
    kExprBlock, kWasmRef, supertype,
        kExprLocalGet, 0,
        kExprBrOnNonNull, 0,
        kExprUnreachable,
        kExprReturn,
    kExprEnd,
    kGCPrefix, kExprRefCast, subtype,
    kGCPrefix, kExprStructGet, subtype, 1
]);

function MakeCaller(name, callee) {
  builder.addFunction(name, kSig_i_v)
      .addBody([
        kExprI32Const, 10, kExprI32Const, 42,
        kGCPrefix, kExprStructNew, subtype,
        kExprCallFunction, callee.index
      ])
      .exportFunc();
}
MakeCaller("main1", callee1);
MakeCaller("main2", callee2);
MakeCaller("main3", callee3);

var module = builder.instantiate();

for (let i = 0; i < 100; i++) {
  assertEquals(42, module.exports.main1());
  assertEquals(42, module.exports.main2());
  assertEquals(42, module.exports.main3());
}
