// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestNonInlinedTarget() {
  let builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(makeSig([kWasmI32, kWasmI32], [kWasmI32]));

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add]).exportFunc();
  builder.addFunction("sub", funcRefT)
  .addBody([
    // Some useless instructions to make this non-inlined due to size.
    ...wasmS128Const(100, 200), kExprDrop,
    ...wasmS128Const(100, 200), kExprDrop,
    ...wasmS128Const(100, 200), kExprDrop,
    ...wasmS128Const(100, 200), kExprDrop,
    kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Sub]).exportFunc();
  builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul]).exportFunc();

  let mainParams = [kWasmI32, kWasmI32, wasmRefType(funcRefT)];
  builder.addFunction("main", makeSig(mainParams, [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  // Add call feedback for add().
  assertEquals(30, wasm.main(10, 20, wasm.add));
  %WasmTierUpFunction(wasm.main);
  assertEquals(30, wasm.main(10, 20, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  // Call with new target causing a deopt.
  assertEquals(-10, wasm.main(10, 20, wasm.sub));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  // Re-opt. Due to the size of sub(), the target will not be inlined.
  // This causes sub() to take the slow call_ref implementation and no deopt
  // point is created.
  %WasmTierUpFunction(wasm.main);
  // Calling with a new call target therefore doesn't trigger a deopt.
  assertEquals(200, wasm.main(10, 20, wasm.mul));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
})();
