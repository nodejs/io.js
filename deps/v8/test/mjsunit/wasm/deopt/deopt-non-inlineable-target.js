// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm --liftoff
// Flags: --experimental-wasm-inlining
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing
// Flags: --wasm-inlining-call-indirect

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestDeoptWithNonInlineableTargetCallRef() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  let importMul = builder.addImport('m', 'mul', funcRefT);
  builder.addExport('mul', importMul);
  let mul = (a, b) => a * b;

  builder.addFunction('add', funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();

  let mainSig =
    makeSig([kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);
  builder.addFunction('main', mainSig)
    .addBody([
      kExprI32Const, 12,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  let wasm = builder.instantiate({m: {mul}}).exports;
  assertEquals(42, wasm.main(30, wasm.add));
  %WasmTierUpFunction(wasm.main);
  // Tier-up.
  assertEquals(42, wasm.main(30, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  // Non-deopt call succeeded, now causing deopt with imported function.
  assertEquals(360, wasm.main(30, wasm.mul));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  // Deopt happened, executions are now in Liftoff.
  assertEquals(42, wasm.main(30, wasm.add));
  // Re-opt.
  %WasmTierUpFunction(wasm.main);
  // There should be feedback for both targets (one of them being
  // non-inlineable), they should not trigger new deopts.
  assertEquals(360, wasm.main(30, wasm.mul));
  assertEquals(42, wasm.main(30, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
})();

(function TestDeoptWithNonInlineableTargetCallIndirect() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  let importMul = builder.addImport('m', 'mul', funcRefT);
  builder.addExport('mul', importMul);
  let mul = (a, b) => a * b;

  let add = builder.addFunction('add', funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();

  let table = builder.addTable(kWasmFuncRef, 2);
  builder.addActiveElementSegment(table.index, wasmI32Const(0), [
    [kExprRefFunc, add.index],
    [kExprRefFunc, importMul],
  ], kWasmFuncRef);

  let mainSig =
    makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]);
  builder.addFunction('main', mainSig)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallIndirect, funcRefT, table.index,
  ]).exportFunc();

  let wasm = builder.instantiate({m: {mul}}).exports;

  let addTableIndex = 0;
  let mulTableIndex = 1;
  assertEquals(42, wasm.main(12, 30, addTableIndex));
  %WasmTierUpFunction(wasm.main);
  // Tier-up.
  assertEquals(42, wasm.main(12, 30, addTableIndex));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  // Non-deopt call succeeded, now causing deopt with imported function.
  assertEquals(360, wasm.main(12, 30, mulTableIndex));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  // Deopt happened, executions are now in Liftoff.
  assertEquals(42, wasm.main(12, 30, addTableIndex));
  // Re-opt.
  %WasmTierUpFunction(wasm.main);
  // There should be feedback for both targets (one of them being
  // non-inlineable), they should not trigger new deopts.
  assertEquals(360, wasm.main(12, 30, mulTableIndex));
  assertEquals(42, wasm.main(12, 30, addTableIndex));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
})();
