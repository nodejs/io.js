// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --no-jit-fuzzing --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --turboshaft-wasm
// Flags: --wasm-inlining-ignore-call-counts --wasm-inlining-factor=15

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestMultipleModulesUninlineableTargets() {
  const builder = new WasmModuleBuilder();
  let calleeSig = builder.addType(makeSig([], [kWasmI32]));
  let mainSig = builder.addType(makeSig([wasmRefType(calleeSig)], [kWasmI32]));
  let callee0 = builder.addFunction("callee_0", calleeSig)
    .exportFunc()
    .addBody([kExprI32Const, 42]);

  let inlinee = builder.addFunction("inlinee", mainSig).addBody([
    // Trigger the deopt.
    kExprLocalGet, 0,
    kExprCallRef, calleeSig,
  ]);

  builder.addFunction("main", mainSig).exportFunc()
    .addBody([
      // Call the inlinee.
      kExprLocalGet, 0,
      kExprCallFunction, inlinee.index,
      // Just performing a call which triggers an update of the feedback vector.
      kExprRefFunc, callee0.index,
      kExprCallRef, calleeSig,
      kExprI32Add,
    ]);

  const instance = builder.instantiate({});

  assertEquals(84, instance.exports.main(instance.exports.callee_0));
  %WasmTierUpFunction(instance.exports.main);
  assertEquals(84, instance.exports.main(instance.exports.callee_0));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance.exports.main));
  }

  const instance2 = builder.instantiate({});
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance2.exports.main));
  }
  assertEquals(84, instance2.exports.main(instance.exports.callee_0));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(instance2.exports.main));
  }
  // Run it one more time, so that the call count to inlinee is > 0 for
  // instance2 as otherwise the feedback doesn't get updated.
  assertEquals(84, instance2.exports.main(instance.exports.callee_0));
  %WasmTierUpFunction(instance2.exports.main);
  assertEquals(84, instance2.exports.main(instance.exports.callee_0));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance2.exports.main));
  }
})();

(function TestMultipleModulesUninlineableTargetsRecursiveFrames() {
  const builder = new WasmModuleBuilder();
  let calleeSig = builder.addType(makeSig([], [kWasmI32]));
  let mainSig =
    builder.addType(makeSig([kWasmI32, wasmRefType(calleeSig)], [kWasmI32]));
  let callee0 = builder.addFunction("callee_0", calleeSig)
    .exportFunc()
    .addBody([kExprI32Const, 42]);

  let mainIndex = callee0.index + 1;
  let main = builder.addFunction("main", mainSig).exportFunc()
    .addBody([
      // Call itself recursively.
      kExprLocalGet, 0,
      kExprIf, kWasmVoid,
        kExprLocalGet, 0,
        kExprI32Const, 1,
        kExprI32Sub,
        kExprLocalGet, 1,
        kExprCallFunction, mainIndex,
        kExprDrop,
      kExprEnd,
      // Perform the deopting call_ref. (Only the inner-most frame will trigger
      // it and deopt the outer frames for all inlined frames.)
      kExprLocalGet, 1,
      kExprCallRef, calleeSig,
    ]);
  assertEquals(mainIndex, main.index);

  const instance = builder.instantiate({});

  assertEquals(42, instance.exports.main(7, instance.exports.callee_0));
  %WasmTierUpFunction(instance.exports.main);
  assertEquals(42, instance.exports.main(7, instance.exports.callee_0));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance.exports.main));
  }

  const instance2 = builder.instantiate({});
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance2.exports.main));
  }
  assertEquals(42, instance2.exports.main(7, instance.exports.callee_0));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(instance2.exports.main));
  }
  %WasmTierUpFunction(instance2.exports.main);
  assertEquals(42, instance2.exports.main(7, instance.exports.callee_0));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance2.exports.main));
  }
})();
