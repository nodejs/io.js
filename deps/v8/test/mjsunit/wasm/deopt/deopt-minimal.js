// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Minimal test case for a wasm deopt.
// The following flags can help:
// --trace-deopt-verbose (information about the deopt input and output frames)
// --print-wasm-code (prints some information about the generated deopt data)
// --nodebug-code (keeps the generated code small)
// --code-comments (helpful comments in generated code if enabled in gn args)
(function TestDeoptMinimalCallRef() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
    .exportFunc();

  let mainSig =
    makeSig([kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);
  builder.addFunction("main", mainSig)
    .addBody([
      kExprI32Const, 12,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(42, wasm.main(30, wasm.add));
  %WasmTierUpFunction(wasm.main);
  // tierup.
  assertEquals(42, wasm.main(30, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  // Non-deopt call succeeded, now causing deopt.
  assertEquals(360, wasm.main(30, wasm.mul));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  // Deopt happened, executions are now in liftoff.
  assertEquals(42, wasm.main(30, wasm.add));
  // Re-opt.
  %WasmTierUpFunction(wasm.main);
  // There is feedback for both targets, they do not trigger new deopts.
  assertEquals(360, wasm.main(30, wasm.mul));
  assertEquals(42, wasm.main(30, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
})();
