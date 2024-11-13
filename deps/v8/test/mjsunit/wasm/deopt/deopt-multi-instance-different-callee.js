// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --no-jit-fuzzing --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --turboshaft-wasm

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let calleeSig = builder.addType(makeSig([], [kWasmI32]));
let mainSig = builder.addType(makeSig([wasmRefType(calleeSig)], [kWasmI32]));

builder.addFunction("a", calleeSig)
  .exportFunc()
  .addBody([kExprI32Const, 1]);

builder.addFunction("b", calleeSig)
  .exportFunc()
  .addBody([kExprI32Const, 2]);

builder.addFunction("main", mainSig).exportFunc()
  .addBody([
    kExprLocalGet, 0,
    kExprCallRef, calleeSig,
  ]);

const instanceA = builder.instantiate({});
const instanceB = builder.instantiate({});

instanceA.exports.main(instanceA.exports.a);
%WasmTierUpFunction(instanceA.exports.main);
instanceA.exports.main(instanceA.exports.a);
if (%IsWasmTieringPredictable()) {
  assertTrue(%IsTurboFanFunction(instanceA.exports.main));
}

// This triggers a deopt as the callee is not function a and the optimized code
// is shared with instanceA.
instanceB.exports.main(instanceB.exports.b);
if (%IsWasmTieringPredictable()) {
  assertFalse(%IsTurboFanFunction(instanceB.exports.main));
}
// This re-optimizes the function with the feedback from instanceB.
// To prevent deopt loops, it must also keep the feedback from instanceA.
%WasmTierUpFunction(instanceB.exports.main);
instanceB.exports.main(instanceB.exports.b);
if (%IsWasmTieringPredictable()) {
  assertTrue(%IsTurboFanFunction(instanceA.exports.main));
}
// Calling a or b does not trigger a deopt.
instanceA.exports.main(instanceA.exports.a);
instanceA.exports.main(instanceA.exports.b);
instanceB.exports.main(instanceB.exports.a);
instanceB.exports.main(instanceB.exports.b);
if (%IsWasmTieringPredictable()) {
  assertTrue(%IsTurboFanFunction(instanceA.exports.main));
  assertTrue(%IsTurboFanFunction(instanceB.exports.main));
}
