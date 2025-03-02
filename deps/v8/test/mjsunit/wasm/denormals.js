// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

builder.addFunction("flushed", kSig_i_v).exportFunc().addBody([
  ...wasmF64Const(Number.MIN_VALUE),
  ...wasmF64Const(2),
  kExprF64Mul,
  ...wasmF64Const(0),
  kExprF64Eq,
  kExprIf, kWasmI32,
    kExprI32Const, 1,
  kExprElse,
    kExprI32Const, 0,
  kExprEnd,
]);

let inst1 = builder.instantiate();
assertEquals(0, inst1.exports.flushed());
d8.test.setFlushDenormals(true);
let inst2 = builder.instantiate();
let result1 = %IsLiftoffFunction(inst1.exports.flushed) ? 1 : 0;
assertEquals(result1, inst1.exports.flushed());
assertEquals(1, inst2.exports.flushed());
d8.test.setFlushDenormals(false);
assertEquals(0, inst1.exports.flushed());
let result2 = %IsLiftoffFunction(inst2.exports.flushed) ? 0 : 1;
assertEquals(result2, inst2.exports.flushed());
