// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged
// Flags: --wasm-tiering-budget=1000 --wasm-dynamic-tiering
// Flags: --no-predictable

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptTieringBudget() {
  // This can be non-zero in certain variants (e.g. `code_serializer`).
  let initialDeoptCount = %WasmDeoptsExecutedCount();

  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
    .exportFunc();
  let mainSig =
    makeSig([kWasmI32, kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);
  builder.addFunction("main", mainSig)
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(initialDeoptCount, %WasmDeoptsExecutedCount());
  print("Running until tier up");
  while (!%IsTurboFanFunction(wasm.main)) {
    assertEquals(42, wasm.main(12, 30, wasm.add));
  }
  assertEquals(initialDeoptCount, %WasmDeoptsExecutedCount());
  print("Tierup reached");
  // Cause deopt.
  assertEquals(360, wasm.main(12, 30, wasm.mul));
  assertEquals(initialDeoptCount + 1, %WasmDeoptsExecutedCount());
  print("Deopt reached");
  // Run again until tierup.
  while (!%IsTurboFanFunction(wasm.main)) {
    assertEquals(42, wasm.main(12, 30, wasm.add));
    assertEquals(360, wasm.main(12, 30, wasm.mul));
  }
  print("Tiered up again");
  // If we are very unlucky, there can still be compilations with old feedback
  // queued in the background that re-trigger new deopts with the wasm.mul
  // target. Therefore we can't assert that the deopt count is still 1.
  assertTrue(initialDeoptCount + %WasmDeoptsExecutedCount() < 20);
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(42, wasm.main(12, 30, wasm.add));
  assertEquals(360, wasm.main(12, 30, wasm.mul));
})();
