// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptMemoryStart() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  builder.addMemory(1, 1);

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
      // Store some value in the memory.
      kExprI32Const, 24,            // index
      ...wasmI32Const(0x12345678),  // value
      kExprI32StoreMem, 0, 0,
      // Perform the call_ref with potential deopt.
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallRef, funcRefT,
      // Load back the value and check that it's the same.
      // Note that the cached memory start should have been invalidated by the
      // call_ref, so this should reload the memory start.
      kExprI32Const, 24,
      kExprI32LoadMem, 0, 0,
      ...wasmI32Const(0x12345678),
      kExprI32Ne,
      kExprIf, kWasmVoid,
        kExprUnreachable,
      kExprEnd,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(42, wasm.main(12, 30, wasm.add));
  %WasmTierUpFunction(wasm.main);
  assertEquals(42, wasm.main(12, 30, wasm.add));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(360, wasm.main(12, 30, wasm.mul));
})();
