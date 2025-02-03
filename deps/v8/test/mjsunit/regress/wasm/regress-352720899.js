// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-wasm-loop-peeling --no-wasm-loop-unrolling
d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig115 = builder.addType(makeSig([kWasmI32], [kWasmI32]));
let $sig201 = builder.addType(makeSig([kWasmAnyRef], [kWasmAnyRef]));

builder.addFunction(undefined, $sig115).exportAs("main").addBody([
      ...wasmI32Const(42),
    kGCPrefix, kExprRefI31,
    kExprBlock, $sig201,
      // This br_on_cast_fail will always branch as as the cast always fails.
      // (Although not statically, so the decoder doesn't "know" it.)
      kGCPrefix, kExprBrOnCastFail, 0b11, 0, kAnyRefCode, kStructRefCode,
      // This cast has the static source type "ref struct" while the actual
      // value is a "ref i31" whose intersection is the bottom type, so this
      // cast is unreachable.
      kGCPrefix, kExprRefCastNull, kNullRefCode,
      kExprI32Const, 10,
      kExprReturn,
    kExprEnd,
    kExprDrop,
    kExprI32Const, 42,
]);

assertEquals(42, builder.instantiate({}).exports.main());
