// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

let callee = builder.addFunction("callee", kSig_v_v).addBody([]);

builder.addFunction("main", makeSig([kWasmI32], [kWasmF64])).exportFunc()
  .addBody([
    kExprI32Const, 0,
    kSimdPrefix, kExprI8x16Splat,
    kExprLocalGet, 0,
    kExprIf, kWasmF64,
      ...wasmF64Const(42),
    kExprElse,
      ...wasmF64Const(5),
    kExprEnd,
    kExprCallFunction, callee.index,
    kSimdPrefix, kExprF64x2ReplaceLane, 0x01,
    kSimdPrefix, kExprF64x2ExtractLane, 0x01,
]);
const instance = builder.instantiate();
assertEquals(42, instance.exports.main(1));
