// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing

// Test for different types of stack, local and literal values.
d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptTypesLiteralsInLocals() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_v_v);

  builder.addFunction("nop1", funcRefT).addBody([]).exportFunc();
  builder.addFunction("nop2", funcRefT).addBody([]).exportFunc();

  let mainSig = makeSig([wasmRefType(funcRefT)], [kWasmF64]);

  builder.addFunction("literals", mainSig)
    .addLocals(kWasmI32, 1)
    .addLocals(kWasmI64, 1)
    .addLocals(kWasmF32, 1)
    .addLocals(kWasmF64, 1)
    .addLocals(kWasmI31Ref, 2)
    .addLocals(kWasmS128, 1)
    .addBody([
      // Initialize the locals with non-default literal values.
      ...wasmI32Const(32),
      kExprLocalSet, 1,
      ...wasmI64Const(64),
      kExprLocalSet, 2,
      ...wasmF32Const(3.2),
      kExprLocalSet, 3,
      ...wasmF64Const(6.4),
      kExprLocalSet, 4,
      ...wasmI32Const(31),
      kGCPrefix, kExprRefI31,
      kExprLocalSet, 5,
      kExprRefNull, kI31RefCode,
      kExprLocalSet, 6,
      ...wasmS128Const(127, 129),
      kExprLocalSet, 7,

      // Perform the call_ref.
      kExprLocalGet, 0,
      kExprCallRef, funcRefT,

      // Use the locals in the now potentially deoptimized execution.
      kExprLocalGet, 1,
      kExprF64SConvertI32,
      kExprLocalGet, 2,
      kExprF64SConvertI64,
      kExprLocalGet, 3,
      kExprF64ConvertF32,
      kExprLocalGet, 4,
      kExprLocalGet, 5,
      kGCPrefix, kExprI31GetS,
      kExprF64SConvertI32,
      kExprLocalGet, 6,
      kExprRefIsNull,
      kExprF64SConvertI32,
      kExprLocalGet, 7,
      kSimdPrefix, kExprF64x2ExtractLane, 0,
      kExprLocalGet, 7,
      kSimdPrefix, kExprF64x2ExtractLane, 1,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  let expected = 32 + 64 + 3.2 + 6.4 + 31 + 1 + 127 + 129;
  let delta = 1e-4;
  assertEqualsDelta(expected, wasm.literals(wasm.nop1), delta);
  %WasmTierUpFunction(wasm.literals);
  assertEqualsDelta(expected, wasm.literals(wasm.nop1), delta);
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.literals));
  }
  // Deopt happened, the result should still be the same.
  assertEqualsDelta(expected, wasm.literals(wasm.nop2), delta);
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.literals));
  }
})();

(function TestDeoptTypesLiteralsOnValueStack() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_v_v);

  builder.addFunction("nop1", funcRefT).addBody([]).exportFunc();
  builder.addFunction("nop2", funcRefT).addBody([]).exportFunc();

  let mainSig = makeSig([wasmRefType(funcRefT)], [kWasmF64]);

  builder.addFunction("literals", mainSig)
    .addLocals(kWasmI32, 1)
    .addLocals(kWasmI64, 1)
    .addLocals(kWasmF32, 1)
    .addLocals(kWasmF64, 1)
    .addLocals(kWasmI31Ref, 2)
    .addLocals(kWasmS128, 1)
    .addBody([
      ...wasmI32Const(32),
      ...wasmI64Const(64),
      ...wasmF32Const(3.2),
      ...wasmF64Const(6.4),
      ...wasmI32Const(31),
      kGCPrefix, kExprRefI31,
      kExprRefNull, kI31RefCode,
      ...wasmS128Const(127, 129),

      // Perform the call_ref.
      kExprLocalGet, 0,
      kExprCallRef, funcRefT,

      // Use the values.
      kExprLocalSet, 7,
      kExprLocalSet, 6,
      kExprLocalSet, 5,
      kExprLocalSet, 4,
      kExprLocalSet, 3,
      kExprLocalSet, 2,
      kExprF64SConvertI32,
      kExprLocalGet, 2,
      kExprF64SConvertI64,
      kExprLocalGet, 3,
      kExprF64ConvertF32,
      kExprLocalGet, 4,
      kExprLocalGet, 5,
      kGCPrefix, kExprI31GetS,
      kExprF64SConvertI32,
      kExprLocalGet, 6,
      kExprRefIsNull,
      kExprF64SConvertI32,
      kExprLocalGet, 7,
      kSimdPrefix, kExprF64x2ExtractLane, 0,
      kExprLocalGet, 7,
      kSimdPrefix, kExprF64x2ExtractLane, 1,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  let expected = 32 + 64 + 3.2 + 6.4 + 31 + 1 + 127 + 129;
  let delta = 1e-4;
  assertEqualsDelta(expected, wasm.literals(wasm.nop1), delta);
  %WasmTierUpFunction(wasm.literals);
  assertEqualsDelta(expected, wasm.literals(wasm.nop1), delta);
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.literals));
  }
  // Deopt happened, the result should still be the same.
  assertEqualsDelta(expected, wasm.literals(wasm.nop2), delta);
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.literals));
  }
})();

(function TestDeoptTypesNonLiterals() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_v_v);

  builder.addFunction("nop1", funcRefT).addBody([]).exportFunc();
  builder.addFunction("nop2", funcRefT).addBody([]).exportFunc();

  let mainSig = makeSig([kWasmI32, wasmRefType(funcRefT)], [kWasmF64]);

  builder.addFunction("locals", mainSig)
    .addLocals(kWasmI32, 1)
    .addLocals(kWasmI64, 1)
    .addLocals(kWasmF32, 1)
    .addLocals(kWasmF64, 1)
    .addLocals(kWasmI31Ref, 1)
    .addLocals(kWasmS128, 1)
    .addBody([
      // Initialize the locals with non-default non-literal values.
      kExprLocalGet, 0,
      kExprLocalTee, 2,
      kExprI64SConvertI32,
      kExprLocalTee, 3,
      kExprF32SConvertI64,
      kExprLocalTee, 4,
      kExprF64ConvertF32,
      kExprLocalSet, 5,
      kExprLocalGet, 0,
      kGCPrefix, kExprRefI31,
      kExprLocalSet, 6,
      kExprLocalGet, 0,
      kExprF64SConvertI32,
      kSimdPrefix, kExprF64x2Splat,
      kExprLocalSet, 7,

      // Perform the call_ref.
      kExprLocalGet, 1,
      kExprCallRef, funcRefT,

      // Use the locals in the now potentially deoptimized stack.
      kExprLocalGet, 2,
      kExprF64SConvertI32,
      kExprLocalGet, 3,
      kExprF64SConvertI64,
      kExprLocalGet, 4,
      kExprF64ConvertF32,
      kExprLocalGet, 5,
      kExprLocalGet, 6,
      kGCPrefix, kExprI31GetS,
      kExprF64SConvertI32,
      kExprLocalGet, 7,
      kSimdPrefix, kExprF64x2ExtractLane, 0,
      kExprLocalGet, 7,
      kSimdPrefix, kExprF64x2ExtractLane, 1,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
      kExprF64Add,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  let delta = 1e-4;
  assertEqualsDelta(42 * 7, wasm.locals(42, wasm.nop1), delta);
  %WasmTierUpFunction(wasm.locals);
  assertEqualsDelta(42 * 7, wasm.locals(42, wasm.nop1), delta);
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.locals));
  }
  // Deopt happened, the result should still be the same.
  assertEqualsDelta(42 * 7, wasm.locals(42, wasm.nop2), delta);
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.locals));
  }
})();
