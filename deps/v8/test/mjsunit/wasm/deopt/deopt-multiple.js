// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Simple test containing more than one deopt point.
(function TestMultipleDeoptPoints() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
    .exportFunc();

  let mainSig = makeSig(
    [kWasmI32, kWasmI32, wasmRefType(funcRefT), kWasmI32,
     wasmRefType(funcRefT)], [kWasmI32]);

  builder.addFunction("main", mainSig)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallRef, funcRefT,
      kExprLocalGet, 3,
      kExprLocalGet, 4,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals((1 + 2) * 3, wasm.main(1, 2, wasm.add, 3, wasm.mul));
  %WasmTierUpFunction(wasm.main);
  assertEquals((1 + 2) * 3, wasm.main(1, 2, wasm.add, 3, wasm.mul));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  // New target on 2nd call_ref.
  assertEquals((1 + 2) + 3, wasm.main(1, 2, wasm.add, 3, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  %WasmTierUpFunction(wasm.main);
  // New target on 1st call_ref.
  assertEquals((1 * 2) * 3, wasm.main(1, 2, wasm.mul, 3, wasm.mul));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  %WasmTierUpFunction(wasm.main);
  // New combination but no new targets.
  assertEquals((1 * 2) + 3, wasm.main(1, 2, wasm.mul, 3, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
})();
