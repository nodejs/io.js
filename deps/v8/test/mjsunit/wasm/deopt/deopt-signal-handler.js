// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// The signal handler itself is completely unrelated to deopts. Still, when
// performing a deopt, the g_thread_in_wasm_code needs to be unset when calling
// into the deoptimizer.
(function TestDeoptSignalHandler() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("zero", funcRefT)
    .addBody([kExprI32Const, 0])
    .exportFunc();


  let mainSig =
    makeSig([kWasmI32, kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);
  builder.addFunction("main", mainSig)
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallRef, funcRefT,
      kExprLocalTee, 3,
      kExprI32Eqz,
      kExprIf, kWasmVoid,
        kExprRefNull, kArrayRefCode,
        kGCPrefix, kExprArrayLen, // traps,
        kExprUnreachable,
      kExprEnd,
      kExprLocalGet, 3,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(42, wasm.main(12, 30, wasm.add));
  %WasmTierUpFunction(wasm.main);
  assertEquals(42, wasm.main(12, 30, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertThrows(() => wasm.main(12, 30, wasm.zero));
})();
