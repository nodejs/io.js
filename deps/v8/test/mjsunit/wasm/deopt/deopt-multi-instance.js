// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --no-jit-fuzzing --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged
// Flags: --wasm-inlining-call-indirect

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function TestCallRef() {
  const builder = new WasmModuleBuilder();
  let calleeSig = builder.addType(makeSig([], [kWasmI32]));
  let mainSig = builder.addType(makeSig([wasmRefType(calleeSig)], [kWasmI32]));
  builder.addFunction("callee_0", calleeSig)
    .exportFunc()
    .addBody([kExprI32Const, 42]);

  builder.addFunction("main", mainSig).exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprCallRef, calleeSig,
    ]);

  const instance = builder.instantiate({});

  instance.exports.main(instance.exports.callee_0);
  %WasmTierUpFunction(instance.exports.main);
  instance.exports.main(instance.exports.callee_0);
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance.exports.main));
  }

  const instance2 = builder.instantiate({});
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance2.exports.main));
  }
  instance2.exports.main(instance.exports.callee_0);
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(instance2.exports.main));
  }
  %WasmTierUpFunction(instance2.exports.main);
  instance2.exports.main(instance.exports.callee_0);
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance2.exports.main));
  }
})();

(function TestCallIndirect() {
  const builder = new WasmModuleBuilder();
  let calleeSig = builder.addType(makeSig([], [kWasmI32]));
  let mainSig = builder.addType(makeSig([kWasmI32], [kWasmI32]));
  let callee1 = builder.addFunction("callee1", calleeSig)
    .exportFunc()
    .addBody([kExprI32Const, 42]);
  let callee2 = builder.addFunction("callee2", calleeSig)
    .exportFunc()
    .addBody([kExprI32Const, 10]);

  let table = builder.addTable(kWasmFuncRef, 2);
  builder.addActiveElementSegment(table.index, wasmI32Const(0), [
      [kExprRefFunc, callee1.index],
      [kExprRefFunc, callee2.index],
    ], kWasmFuncRef);


  builder.addFunction("main", mainSig).exportFunc()
    .addBody([
      kExprLocalGet, 0,
      kExprCallIndirect, calleeSig, table.index,
    ]);

  const instance = builder.instantiate({});

  instance.exports.main(0);
  %WasmTierUpFunction(instance.exports.main);
  instance.exports.main(0);
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance.exports.main));
  }

  const instance2 = builder.instantiate({});
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance2.exports.main));
  }
  instance2.exports.main(1);
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(instance2.exports.main));
  }
  %WasmTierUpFunction(instance2.exports.main);
  instance2.exports.main(1);
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(instance2.exports.main));
  }
})();
