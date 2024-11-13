// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged
// Flags: --wasm-inlining-ignore-call-counts --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestManyParamsManyLocals() {
  // x86-64 has 16 XMM registers, so we need a few values to run out of
  // registers and end up with stack slots.
  let paramCount = 21;
  let builder = new WasmModuleBuilder();
  let calleeParams = new Array(paramCount).fill(kWasmS128);
  let funcRefT = builder.addType(makeSig(calleeParams, [kWasmS128]));

  builder.addFunction("add", funcRefT)
    .addBody(generateCalleeBody(kExprI32x4Add)).exportFunc();
  builder.addFunction("sub", funcRefT)
    .addBody(generateCalleeBody(kExprI32x4Sub)).exportFunc();
  builder.addFunction("max", funcRefT)
    .addBody(generateCalleeBody(kExprI32x4MaxU)).exportFunc();

  let deoptingFct = builder.addFunction(
      "deopting", makeSig([...calleeParams, wasmRefType(funcRefT)], [kWasmI32]))
    .addBody([
      // Push the arguments for the second call from the caller.
      ...pushArgs(paramCount + 1),
      // Create new local arguments for the first call.
      ...createArgs(paramCount),
      kExprLocalGet, paramCount,
      kExprCallRef, funcRefT,
      kExprLocalSet, 2,
      kExprCallRef, funcRefT,
      // Add both call_ref results and sum up the lanes.
      kExprLocalGet, 2,
      ...SimdInstr(kExprI32x4Add),
      kExprLocalTee, 2,
      kSimdPrefix, kExprI32x4ExtractLane, 0,
      kExprLocalGet, 2,
      kSimdPrefix, kExprI32x4ExtractLane, 1,
      kExprLocalGet, 2,
      kSimdPrefix, kExprI32x4ExtractLane, 2,
      kExprLocalGet, 2,
      kSimdPrefix, kExprI32x4ExtractLane, 3,
      kExprI32Add,
      kExprI32Add,
      kExprI32Add,
    ]).exportFunc();

  let mainParams = [kWasmI32, wasmRefType(funcRefT)];
  builder.addFunction("main", makeSig(mainParams, [kWasmI32]))
    .addLocals(kWasmS128, 1)
    .addBody([
      ...createArgs(paramCount),
      kExprLocalGet, 1,
      kExprCallFunction, deoptingFct.index,
  ]).exportFunc();

  // [0, 1, ..., paramCount - 1]
  let values = [...Array(paramCount).keys()];
  let expectedSum = values.reduce((a, b) => a + b) * 4 * 2;
  let expectedDiff = values.reduce((a, b) => a - b) * 4 * 2;
  let expectedMax = (paramCount - 1) * 4 * 2;
  assertEquals(expectedSum, -expectedDiff);

  let wasm = builder.instantiate().exports;
  assertEquals(expectedSum, wasm.main(0, wasm.add));
  %WasmTierUpFunction(wasm.deopting);
  assertEquals(expectedSum, wasm.main(0, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.deopting));
  }
  assertEquals(expectedDiff, wasm.main(0, wasm.sub));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.deopting));
  }

  // Repeat the test but this time with an additional layer of inlining.
  %WasmTierUpFunction(wasm.main);
  assertEquals(expectedSum, wasm.main(0, wasm.add));
  assertEquals(expectedDiff, wasm.main(0, wasm.sub));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(expectedMax, wasm.main(0, wasm.max));

  function generateCalleeBody(binop) {
    let result = [kExprLocalGet, 0];
    for (let i = 1; i < paramCount; ++i) {
      result.push(kExprLocalGet, i, ...SimdInstr(binop));
    }
    return result;
  }

  function createArgs(paramCount) {
    let result = [];
    for (let i = 0; i < paramCount; ++i) {
      result.push(kExprI32Const, i, kSimdPrefix, kExprI32x4Splat);
    }
    return result;
  }

  function pushArgs(paramCount) {
    let result = [];
    for (let i = 0; i < paramCount; ++i) {
      result.push(kExprLocalGet, i);
    }
    return result;
  }
})();
