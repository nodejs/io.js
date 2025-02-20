// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --no-jit-fuzzing --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let calleeSig = builder.addType(makeSig([], [kWasmI32]));
let mainSig = builder.addType(makeSig([wasmRefType(calleeSig)], [kWasmI32]));
let global = builder.addGlobal(kWasmI32, true).exportAs("global");

builder.addFunction("a", calleeSig)
  .exportFunc()
  .addBody([
    kExprGlobalGet, global.index,
    kExprI32Const, 1,
    kExprI32Add,
  ]);

builder.addFunction("b", calleeSig)
  .exportFunc()
  .addBody([
    kExprGlobalGet, global.index,
    kExprI32Const, 2,
    kExprI32Add,
  ]);

builder.addFunction("main", mainSig).exportFunc()
  .addBody([
    kExprGlobalGet, global.index,
    kExprI32Const, 10,
    kExprI32Mul,
    kExprLocalGet, 0,
    kExprCallRef, calleeSig,
    kExprI32Add,
  ]);

const instanceA = builder.instantiate({});
const instanceB = builder.instantiate({});
instanceA.exports.global.value = 10;
instanceB.exports.global.value = 20;

// Expected output:
// main: global * 10 + callee
// callee:
//   - a:    global + 1
//   - b:    global + 2
// Therefore we expect a three digit number:
//   - first  digit: 1 for instanceA.main,  2 for instanceB.main
//   - second digit: 1 for instanceA.(a|b), 2 for instanceB.(a|b)
//   - third  digit: 1 for a,               2 for b

assertEquals(111, instanceA.exports.main(instanceA.exports.a));
%WasmTierUpFunction(instanceA.exports.main);
assertEquals(111, instanceA.exports.main(instanceA.exports.a));
if (%IsWasmTieringPredictable()) {
  assertTrue(%IsTurboFanFunction(instanceA.exports.main));
}

// This triggers a deopt as the callee is not function a and the optimized code
// is shared with instanceA.
assertEquals(222, instanceB.exports.main(instanceB.exports.b));
if (%IsWasmTieringPredictable()) {
  assertFalse(%IsTurboFanFunction(instanceB.exports.main));
}

// This re-optimizes the function with the feedback from instanceB.
// To prevent deopt loops, it must also keep the feedback from instanceA.
%WasmTierUpFunction(instanceB.exports.main);
assertEquals(222, instanceB.exports.main(instanceB.exports.b));
if (%IsWasmTieringPredictable()) {
  assertTrue(%IsTurboFanFunction(instanceA.exports.main));
}

// Calling a or b does not trigger a deopt.
assertEquals(111, instanceA.exports.main(instanceA.exports.a));
assertEquals(112, instanceA.exports.main(instanceA.exports.b));
assertEquals(221, instanceB.exports.main(instanceB.exports.a));
assertEquals(222, instanceB.exports.main(instanceB.exports.b));
if (%IsWasmTieringPredictable()) {
  assertTrue(%IsTurboFanFunction(instanceA.exports.main));
  assertTrue(%IsTurboFanFunction(instanceB.exports.main));
}

// Note that so far no cross-intance calls have been performed.
// Doing so will trigger a deoptimization as cross-instance calls are not
// inlineable.
assertEquals(122, instanceA.exports.main(instanceB.exports.b));
if (%IsWasmTieringPredictable()) {
  assertFalse(%IsTurboFanFunction(instanceA.exports.main));
  assertFalse(%IsTurboFanFunction(instanceB.exports.main));
}

// Tiering up again will not emit a deopt any more as the feedback contains a
// non-inlineable target.
%WasmTierUpFunction(instanceA.exports.main);
assertEquals(121, instanceA.exports.main(instanceB.exports.a));
assertEquals(122, instanceA.exports.main(instanceB.exports.b));
assertEquals(211, instanceB.exports.main(instanceA.exports.a));
assertEquals(212, instanceB.exports.main(instanceA.exports.b));
if (%IsWasmTieringPredictable()) {
  assertTrue(%IsTurboFanFunction(instanceA.exports.main));
  assertTrue(%IsTurboFanFunction(instanceB.exports.main));
}
