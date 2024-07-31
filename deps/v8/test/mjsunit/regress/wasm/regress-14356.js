// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

(function repro1() {
  builder.addFunction("main", kSig_i_v).exportFunc()
      .addBodyWithEnd([
          ...wasmI32Const(0xFF),
          kSimdPrefix, kExprI8x16Splat,
          kSimdPrefix, ...kExprI32x4RelaxedTruncF32x4U,
          kSimdPrefix, kExprI32x4ExtractLane, 0,
          kExprEnd,
      ]);
  const instance = builder.instantiate();
  const result = instance.exports.main();
  assertTrue(result == 0 || (result >>> 0) == 2**32 - 1);
})();

(function repro2() {
  const builder = new WasmModuleBuilder();
  builder.addFunction("main", kSig_i_v).exportFunc()
      .addBody([
          kExprI32Const, 0,
          kSimdPrefix, kExprI8x16Splat,
          kExprI32Const, 0,
          kSimdPrefix, kExprI8x16Splat,
          kSimdPrefix, kExprI8x16LeS,
          kSimdPrefix, ...kExprI32x4RelaxedTruncF32x4U,
          kSimdPrefix, kExprI32x4ExtractLane, 0,
      ]);
  const instance = builder.instantiate();
  const result = instance.exports.main();
  assertTrue(result == 0 || (result >>> 0) == 2 ** 32 - 1);
})();
