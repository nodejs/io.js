// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax
// Flags: --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptAfterDebuggingRequest() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  let enterDebuggingIndex = builder.addImport("m", 'enterDebugging', kSig_v_v);
  let enterDebugging = () => %WasmEnterDebugging();

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
    .exportFunc();

  let mainSig =
    makeSig([kWasmI32, kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);
  builder.addFunction("main", mainSig)
    .addBody([
      // if (local[0]) %WasmEnterDebugging();
      kExprLocalGet, 0,
      kExprIf, kWasmVoid,
        kExprCallFunction, enterDebuggingIndex,
      kExprEnd,
      // call_ref(local[0], local[1], local[2])
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  let wasm = builder.instantiate({m: {enterDebugging}}).exports;
  assertEquals(10, wasm.main(0, 10, wasm.add));
  %WasmTierUpFunction(wasm.main);
  // Not yet in debugging mode.
  assertFalse(%IsWasmDebugFunction(wasm.main));
  // Trigger debugging mode while the optimized "main" function is on the stack,
  // followed by a deoptimization.
  assertEquals(56, wasm.main(7, 8, wasm.mul));
  // False because we deopted and false because we entered debugging.
  assertFalse(%IsTurboFanFunction(wasm.main));
  // We need to call it a second time, so that the function gets replaced with
  // the "for debugging" version. (Same behavior without deopts enabled.)
  assertEquals(0, wasm.main(0, 8, wasm.mul));
  assertTrue(%IsWasmDebugFunction(wasm.main));
  %WasmLeaveDebugging();
})();
