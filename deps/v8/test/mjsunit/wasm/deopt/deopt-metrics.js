// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptMetrics() {
  // This can be non-zero in certain variants (e.g. `code_serializer`).
  const initialDeoptCount = %WasmDeoptsExecutedCount();

  let builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
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
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  const initialDeoptCountMain = %WasmDeoptsExecutedForFunction(wasm.main);
  assertEquals(initialDeoptCount, %WasmDeoptsExecutedCount());
  assertEquals(42, wasm.main(12, 30, wasm.add));
  %WasmTierUpFunction(wasm.main);
  assertEquals(initialDeoptCount, %WasmDeoptsExecutedCount());
  assertEquals(360, wasm.main(12, 30, wasm.mul));
  assertEquals(initialDeoptCount + 1, %WasmDeoptsExecutedCount());
  %WasmTierUpFunction(wasm.main);
  assertEquals(42, wasm.main(12, 30, wasm.add));
  assertEquals(360, wasm.main(12, 30, wasm.mul));
  assertEquals(initialDeoptCount + 1, %WasmDeoptsExecutedCount());
  assertEquals(0, %WasmDeoptsExecutedForFunction(wasm.add));
  assertEquals(0, %WasmDeoptsExecutedForFunction(wasm.mul));
  assertEquals(initialDeoptCountMain + 1,
               %WasmDeoptsExecutedForFunction(wasm.main));

  // A new instance shares the same counters for deopts.
  let wasm2 = builder.instantiate().exports;
  assertEquals(0, %WasmDeoptsExecutedForFunction(wasm2.add));
  assertEquals(0, %WasmDeoptsExecutedForFunction(wasm2.mul));
  assertEquals(initialDeoptCountMain + 1,
               %WasmDeoptsExecutedForFunction(wasm2.main));
})();
