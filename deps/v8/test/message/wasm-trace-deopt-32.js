// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --trace-deopt --trace-deopt-verbose

d8.file.execute("test/mjsunit/mjsunit.js");
d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Test case for deopt tracing. Note that there is a 32 bit and a 64 bit variant
// of this test case. Please keep them in sync.

(function TestDeoptTracing() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(kSig_i_ii);

  builder.addFunction("add", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Add])
    .exportFunc();
  builder.addFunction("mul", funcRefT)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32Mul])
    .exportFunc();

  let mainSig = makeSig(
    [kWasmI32, kWasmI32, wasmRefType(funcRefT)], [kWasmI32]);

  builder.addFunction("main", mainSig)
    .addBody([
      ...wasmI32Const(32),
      ...wasmI64Const(64),
      ...wasmF32Const(3.2),
      ...wasmF64Const(6.4),
      ...wasmI64Const(0x5128), kSimdPrefix, kExprI64x2Splat,
      kExprRefNull, kAnyRefCode,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprCallRef, funcRefT,
      kExprReturn,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(2 + 3, wasm.main(2, 3, wasm.add));
  %WasmTierUpFunction(wasm.main);
  assertEquals(2 + 3, wasm.main(2, 3, wasm.add));
  assertTrue(%IsTurboFanFunction(wasm.main));
  // New target on 2nd call_ref.
  assertEquals(2 * 3, wasm.main(2, 3, wasm.mul));
  assertFalse(%IsTurboFanFunction(wasm.main));
})();
