// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Test progressing through the different "states" of feedback:
// - monomorphic
// - polymorphic (2-4 targets)
// - megamorphic
(function TestDeoptCallRef() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
    .exportFunc();
  builder.addFunction("sub", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Sub])
    .exportFunc();
  builder.addFunction("first", funcRefT)
    .addBody([kExprLocalGet, 0])
    .exportFunc();
  builder.addFunction("second", funcRefT)
    .addBody([kExprLocalGet, 1])
    .exportFunc();
  builder.addFunction("equals", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Eq,])
    .exportFunc();

  let mainSig =
    makeSig([kWasmI32, kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);
  builder.addFunction("main", mainSig)
    .addLocals(kWasmI32, 1)
    .addBody([
      // Set the local to some value.
      kExprI32Const, 1,
      kExprLocalSet, 3,

      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallRef, funcRefT,

      // Use the local after the deopt point.
      kExprLocalGet, 3,
      kExprI32Mul,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(42, wasm.main(12, 30, wasm.add));
  %WasmTierUpFunction(wasm.main);
  print("tierup");
  assertEquals(42, wasm.main(12, 30, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  print("non-deopt call succeeded, now causing deopt");
  assertEquals(360, wasm.main(12, 30, wasm.mul));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  print("deopt happened");
  assertEquals(42, wasm.main(12, 30, wasm.add));
  print("collect more feedback in liftoff");
  assertEquals(-18, wasm.main(12, 30, wasm.sub));
  print("re-opt");
  %WasmTierUpFunction(wasm.main);
  assertEquals(360, wasm.main(12, 30, wasm.mul));
  assertEquals(42, wasm.main(12, 30, wasm.add));
  assertEquals(-18, wasm.main(12, 30, wasm.sub));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(12, wasm.main(12, 30, wasm.first));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  print("deopt happened");
  print("re-opt with maximum polymorphism");
  %WasmTierUpFunction(wasm.main);
  assertEquals(360, wasm.main(12, 30, wasm.mul));
  assertEquals(42, wasm.main(12, 30, wasm.add));
  assertEquals(-18, wasm.main(12, 30, wasm.sub));
  assertEquals(12, wasm.main(12, 30, wasm.first));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(30, wasm.main(12, 30, wasm.second));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  print("deopt happened");
  print("reopt again");
  %WasmTierUpFunction(wasm.main);
  // All previous call targets still succeed.
  assertEquals(360, wasm.main(12, 30, wasm.mul));
  assertEquals(42, wasm.main(12, 30, wasm.add));
  assertEquals(-18, wasm.main(12, 30, wasm.sub));
  assertEquals(12, wasm.main(12, 30, wasm.first));
  assertEquals(30, wasm.main(12, 30, wasm.second));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  // Previously unseen call targets do not cause a deopt any more.
  assertEquals(1, wasm.main(42, 42, wasm.equals));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
})();
