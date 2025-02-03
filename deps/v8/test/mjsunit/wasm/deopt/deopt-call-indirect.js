// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing
// Flags: --wasm-inlining-call-indirect

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptCallIndirect() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  let add = builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  let mul = builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
    .exportFunc();

  let table = builder.addTable(kWasmFuncRef, 2);
  builder.addActiveElementSegment(table.index, wasmI32Const(0), [
    [kExprRefFunc, add.index],
    [kExprRefFunc, mul.index],
  ], kWasmFuncRef);

  let mainSig =
    makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]);
  builder.addFunction("main", mainSig)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallIndirect, funcRefT, table.index,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  add = 0;
  mul = 1;
  assertEquals(42, wasm.main(12, 30, add));
  %WasmTierUpFunction(wasm.main);
  // Tier up.
  assertEquals(42, wasm.main(12, 30, add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  // Deopt.
  assertEquals(-360, wasm.main(12, -30, mul));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(42, wasm.main(12, 30, add));
  // Re-optimize.
  %WasmTierUpFunction(wasm.main);
  assertEquals(360, wasm.main(12, 30, mul));
  assertEquals(42, wasm.main(12, 30, add));
 })();
