// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing

// Test for different types of stack, local and literal values.
d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Similar to deopt-value-types.js but focused on int64 which requires int64
// lowering.
(function TestDeoptTypesInt64Values() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_v_v);

  builder.addFunction("nop1", funcRefT).addBody([]).exportFunc();
  builder.addFunction("nop2", funcRefT).addBody([]).exportFunc();

  let mainSig = makeSig([kWasmI64, wasmRefType(funcRefT)], [kWasmI64]);

  let localConstant = (0x1n << 32n) + 0x2n;
  let valueStackConstant = (0x12345n << 32n) + 0x54321n;
  let addedValue = 123n;
  let subtractedValue = 321n;
  let argumentValue = 0x1234567887654321n;

  builder.addFunction("main", mainSig)
    .addLocals(kWasmI64, 3)
    .addBody([
      // Add a constant value as a local.
      ...wasmI64Const(localConstant),
      kExprLocalSet, 2,
      // Add a non-constant value as a local.
      kExprLocalGet, 0,
      ...wasmI64Const(addedValue),
      kExprI64Add,
      kExprLocalSet, 3,
      // Push a constant value to the value stack.
      ...wasmI64Const(valueStackConstant),
      // Push a non-constant value to the value stack.
      kExprLocalGet, 0,
      ...wasmI64Const(subtractedValue),
      kExprI64Sub,

      // Perform the call_ref.
      kExprLocalGet, 1,
      kExprCallRef, funcRefT,

      // Use the locals in the now potentially deoptimized execution.
      kExprLocalGet, 0, // the argument
      kExprLocalGet, 2, // the constant local
      kExprLocalGet, 3, // the non-constant local
      kExprLocalGet, 4,  // an uninitialized local (0)
      kExprI64Add,
      kExprI64Add,
      kExprI64Add,
      kExprI64Add,
      kExprI64Add,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  let expected = localConstant + valueStackConstant + addedValue
                 - subtractedValue + argumentValue * 3n;
  assertEquals(expected, wasm.main(argumentValue, wasm.nop1));
  %WasmTierUpFunction(wasm.main);
  assertEquals(expected, wasm.main(argumentValue, wasm.nop1));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  // Deopt happened, the result should still be the same.
  assertEquals(expected, wasm.main(argumentValue, wasm.nop2));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
})();

(function TestInlinedI64Params() {
  let builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(makeSig([kWasmI64, kWasmI64], [kWasmI64]));

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI64Add]).exportFunc();
  builder.addFunction("sub", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI64Sub]).exportFunc();

  let callerSig =
    makeSig([kWasmI64, kWasmI64, wasmRefType(funcRefT)], [kWasmI64]);

  let inlinee = builder.addFunction("inlinee", callerSig)
    .addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallRef, funcRefT,
  ]).exportFunc();

  builder.addFunction("main", callerSig)
  .addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, inlinee.index,
]).exportFunc();

  let wasm = builder.instantiate({}).exports;

  assertEquals(43n, wasm.main(1n, 42n, wasm.add));
  %WasmTierUpFunction(wasm.main);
  assertEquals(43n, wasm.main(1n, 42n, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(-41n, wasm.main(1n, 42n, wasm.sub));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  %WasmTierUpFunction(wasm.main);
  assertEquals(43n, wasm.main(1n, 42n, wasm.add));
  assertEquals(-41n, wasm.main(1n, 42n, wasm.sub));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
})();
