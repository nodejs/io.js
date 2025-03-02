// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --wasm-deopt --liftoff --no-jit-fuzzing

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let calleeSig = builder.addType(
  makeSig([wasmRefType(kWasmI31Ref)], [kWasmI32]));

builder.addFunction("getS", calleeSig).addBody([
  kExprLocalGet, 0, kGCPrefix, kExprI31GetS,
]).exportFunc();

builder.addFunction("getU", calleeSig).addBody([
  kExprLocalGet, 0, kGCPrefix, kExprI31GetU,
]).exportFunc();

builder.addFunction("main", makeSig([wasmRefType(calleeSig)], [kWasmI32]))
.addBody([
  ...wasmI32Const(1689912565),
  kGCPrefix, kExprRefI31,
  kExprLocalGet, 0,
  kExprCallRef, calleeSig,
]).exportFunc();

const instance = builder.instantiate({});
assertEquals(-457571083, instance.exports.main(instance.exports.getS));
%WasmTierUpFunction(instance.exports.main);
// Test value produced by DeoptimizerLiteral constant.
assertEquals(1689912565, instance.exports.main(instance.exports.getU));
if (%IsWasmTieringPredictable()) {
  assertFalse(%IsTurboFanFunction(instance.exports.main));
}
// Test value produced by Liftoff.
assertEquals(1689912565, instance.exports.main(instance.exports.getU));
%WasmTierUpFunction(instance.exports.main);
// Test value produced by Turboshaft.
assertEquals(1689912565, instance.exports.main(instance.exports.getU));
if (%IsWasmTieringPredictable()) {
  assertTrue(%IsTurboFanFunction(instance.exports.main));
}
