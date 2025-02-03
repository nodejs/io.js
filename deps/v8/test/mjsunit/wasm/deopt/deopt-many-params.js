// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged
// Flags: --wasm-inlining-ignore-call-counts --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestManyParams() {
  let paramCount = 12;
  let builder = new WasmModuleBuilder();
  let calleeParams = new Array(paramCount).fill(kWasmI32);
  let funcRefT = builder.addType(makeSig(calleeParams, [kWasmI32]));

  builder.addFunction("add", funcRefT)
    .addBody(generateCalleeBody(kExprI32Add)).exportFunc();
  builder.addFunction("sub", funcRefT)
    .addBody(generateCalleeBody(kExprI32Sub)).exportFunc();

  let mainParams = [...calleeParams, wasmRefType(funcRefT)];
  builder.addFunction("main", makeSig(mainParams, [kWasmI32]))
    .addBody([
      ...pushArgs(paramCount + 1),
      kExprCallRef, funcRefT,
  ]).exportFunc();

  // [0, 1, ..., paramCount - 1]
  let values = [...Array(paramCount).keys()];
  let expectedSum = values.reduce((a, b) => a + b);
  let expectedDiff = values.reduce((a, b) => a - b);
  assertEquals(expectedSum, -expectedDiff);

  let wasm = builder.instantiate().exports;
  assertEquals(expectedSum, wasm.main(...values, wasm.add));
  %WasmTierUpFunction(wasm.main);
  assertEquals(expectedSum, wasm.main(...values, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(expectedDiff, wasm.main(...values, wasm.sub));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }


  function generateCalleeBody(binop) {
    let result = [kExprLocalGet, 0];
    for (let i = 1; i < paramCount; ++i) {
      result.push(kExprLocalGet, i, binop);
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
