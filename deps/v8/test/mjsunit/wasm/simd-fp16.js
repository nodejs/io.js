// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --only-liftoff --experimental-wasm-fp16

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestFP16() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  const addRes = builder.addGlobal(kWasmF32, true).exportAs('add');
  const subRes = builder.addGlobal(kWasmF32, true).exportAs('sub');
  const splatFloat = (f) => [wasmF32Const(f), kSimdPrefix, kExprF16x8Splat];
  const simdBin = (op, lhs, rhs) => [lhs, rhs, kSimdPrefix, op];
  const localTee = (idx, val) => [val, kExprLocalTee, idx];
  builder.addFunction("main", kSig_v_v)
    .addLocals(kWasmS128, 1)
    .addBody([
      simdBin(kExprF16x8Add, splatFloat(2), splatFloat(3)),
      kSimdPrefix, kExprF16x8ExtractLane, 1,
      kExprGlobalSet, addRes.index,

      simdBin(kExprF16x8Sub, localTee(0, splatFloat(5)), splatFloat(3)),
      kSimdPrefix, kExprF16x8ExtractLane, 1,
      kExprGlobalSet, subRes.index,
    ].flat(10)).exportFunc();
  const instance = builder.instantiate({});
  instance.exports.main();
  assertEquals(instance.exports.add.value, 5);
  assertEquals(instance.exports.sub.value, 2);
})();
