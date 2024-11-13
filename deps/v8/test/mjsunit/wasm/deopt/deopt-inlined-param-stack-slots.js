// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm --liftoff
// Flags: --experimental-wasm-inlining --wasm-inlining-ignore-call-counts
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// This test case tests th scenario where the optimized function "main" doesn't
// have any parameter stack slots while the inner-most inlined function
// "innerInlinee" has multiple ones.
(function TestDeoptManyParameterStackSlotsInInlinedFunctionButNotInOuter() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
    .exportFunc();

  let sigInner = makeSig([wasmRefType(funcRefT), kWasmI32, kWasmI32, kWasmI32,
    kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
    kWasmI32, kWasmI32], [kWasmI32]);

  let innerInlinee = builder.addFunction("innerInlinee", sigInner)
  .addBody([
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 0,
    kExprCallRef, funcRefT,
  ]);

  let sigCallRef =
    makeSig([wasmRefType(funcRefT)], [kWasmI32]);

  let outerInlinee = builder.addFunction("outerInlinee", sigCallRef)
  .addBody([
    kExprLocalGet, 0, kExprI32Const, 30, kExprI32Const, 12, kExprI32Const, 0,
    kExprI32Const, 0, kExprI32Const, 0, kExprI32Const, 0, kExprI32Const, 0,
    kExprI32Const, 0, kExprI32Const, 0, kExprI32Const, 0, kExprI32Const, 0,
    kExprI32Const, 0,
    kExprCallFunction, innerInlinee.index,
  ]);

  builder.addFunction("main", sigCallRef)
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, outerInlinee.index,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(42, wasm.main(wasm.add));
  %WasmTierUpFunction(wasm.main);
  // tierup.
  assertEquals(42, wasm.main(wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  // deopt.
  assertEquals(360, wasm.main(wasm.mul));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
})();
