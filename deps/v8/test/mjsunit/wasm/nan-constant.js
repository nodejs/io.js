// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestInstructionSelectionSignalingNanFloat32Literal() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(makeSig([kWasmF32], [kWasmI32]));

  builder.addFunction("reinterpretF32", funcRefT)
    .addBody([kExprLocalGet, 0, kExprI32ReinterpretF32])
    .exportFunc();

  let mainSig =
    makeSig([wasmRefType(funcRefT)], [kWasmI32]);
  builder.addFunction("main", mainSig)
    .addBody([
      ...wasmF32ConstSignalingNaN(),
      kExprLocalGet, 0,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0x7fa7a1b9, wasm.main(wasm.reinterpretF32));
  %WasmTierUpFunction(wasm.main);
  assertEquals(0x7fa7a1b9, wasm.main(wasm.reinterpretF32));
})();

(function TestInstructionSelectionSignalingNanFloat64Literal() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(makeSig([kWasmF64], [kWasmI64]));

  builder.addFunction("reinterpretF64", funcRefT)
    .addBody([kExprLocalGet, 0, kExprI64ReinterpretF64])
    .exportFunc();

  let mainSig =
    makeSig([wasmRefType(funcRefT)], [kWasmI64]);
  builder.addFunction("main", mainSig)
    .addBody([
      ...wasmF64ConstSignalingNaN(),
      kExprLocalGet, 0,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(0x7ff4000000000000n, wasm.main(wasm.reinterpretF64));
  %WasmTierUpFunction(wasm.main);
  assertEquals(0x7ff4000000000000n, wasm.main(wasm.reinterpretF64));
})();
