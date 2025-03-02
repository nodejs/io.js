// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --wasm-deopt --liftoff --wasm-inlining
// Flags: --no-jit-fuzzing --wasm-inlining-call-indirect

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
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

let callRefSig =
  makeSig([kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);
builder.addFunction("callRef", callRefSig)
  // These are too many locals on 32 bit platforms as we'd lower each i64 into
  // 2 i32s and then we'd have 80_000+ inputs to a FrameState while any
  // operation can "only" have 65535 inputs.
  // The compiler needs to detect this and not emit a FrameState and deopt but
  // instead use a call for the (non-inlined) slow path.
  .addLocals(kWasmI64, 40_000)
  .addBody([
    kExprI32Const, 12,
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprCallRef, funcRefT,
]).exportFunc();

// Same but for callIndirect
builder.addFunction("callIndirect", makeSig([kWasmI32, kWasmI32], [kWasmI32]))
.addLocals(kWasmI64, 40_000)
.addBody([
  kExprI32Const, 12,
  kExprLocalGet, 0,
  kExprLocalGet, 1,
  kExprCallIndirect, funcRefT, kTableZero,
]).exportFunc();

let wasm = builder.instantiate().exports;

// Test call_ref.
assertEquals(42, wasm.callRef(30, wasm.add));
%WasmTierUpFunction(wasm.callRef);
assertEquals(42, wasm.callRef(30, wasm.add));
if (%IsWasmTieringPredictable()) {
  assertDidntDeopt(wasm.callRef);
}
// On 32 bits this doesn't trigger a deopt as we don't emit a deopt due to too
// many inputs to the FrameState.
assertEquals(360, wasm.callRef(30, wasm.mul));
if (%IsWasmTieringPredictable()) {
  assertEquals(!%Is64Bit(), %IsTurboFanFunction(wasm.callRef));
}

// Test call_indirect.
let addIndex = 0;
assertEquals(42, wasm.callIndirect(30, addIndex));
%WasmTierUpFunction(wasm.callIndirect);
assertEquals(42, wasm.callIndirect(30, addIndex));
if (%IsWasmTieringPredictable()) {
  assertDidntDeopt(wasm.callIndirect);
}
// On 32 bits this doesn't trigger a deopt as we don't emit a deopt due to too
// many inputs to the FrameState.
let mulIndex = 1;
assertEquals(360, wasm.callIndirect(30, mulIndex));
if (%IsWasmTieringPredictable()) {
  assertEquals(!%Is64Bit(), %IsTurboFanFunction(wasm.callIndirect));
}

function assertDidntDeopt(f) {
  assertTrue(%IsTurboFanFunction(f));
}
