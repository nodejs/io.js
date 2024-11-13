// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff
// Flags: --turboshaft-wasm-instruction-selection-staged --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestDeoptSignalingNanFloat32Literal() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(makeSig([kWasmF32], [kWasmI32]));

  builder.addFunction("justOne", funcRefT)
    .addBody([kExprI32Const, 1])
    .exportFunc();
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
  assertEquals(1, wasm.main(wasm.justOne));
  %WasmTierUpFunction(wasm.main);
  assertEquals(1, wasm.main(wasm.justOne));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(0x7fa7a1b9, wasm.main(wasm.reinterpretF32));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  %WasmTierUpFunction(wasm.main);
  assertEquals(0x7fa7a1b9, wasm.main(wasm.reinterpretF32));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
})();

(function TestDeoptSignalingNanFloat32Value() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(makeSig([kWasmF32], [kWasmI32]));
  let mem = builder.addMemory(1, 1);
  builder.addActiveDataSegment(
    mem.index, [kExprI32Const, 0], [0xb9, 0xa1, 0xa7, 0x7f]);

  builder.addFunction("justOne", funcRefT)
    .addBody([kExprI32Const, 1])
    .exportFunc();
  builder.addFunction("reinterpretF32", funcRefT)
    .addBody([kExprLocalGet, 0, kExprI32ReinterpretF32])
    .exportFunc();

  let mainSig =
    makeSig([wasmRefType(funcRefT)], [kWasmI32]);
  builder.addFunction("main", mainSig)
    .addBody([
      kExprI32Const, 0,
      kExprF32LoadMem, 0, 0,
      kExprLocalGet, 0,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(1, wasm.main(wasm.justOne));
  %WasmTierUpFunction(wasm.main);
  assertEquals(1, wasm.main(wasm.justOne));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(0x7fa7a1b9, wasm.main(wasm.reinterpretF32));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  %WasmTierUpFunction(wasm.main);
  assertEquals(0x7fa7a1b9, wasm.main(wasm.reinterpretF32));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
})();

(function TestDeoptSignalingNanFloat64Literal() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(makeSig([kWasmF64], [kWasmI64]));

  builder.addFunction("justOne", funcRefT)
    .addBody([kExprI64Const, 1])
    .exportFunc();
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
  assertEquals(1n, wasm.main(wasm.justOne));
  %WasmTierUpFunction(wasm.main);
  assertEquals(1n, wasm.main(wasm.justOne));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(0x7ff4000000000000n, wasm.main(wasm.reinterpretF64));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  %WasmTierUpFunction(wasm.main);
  assertEquals(0x7ff4000000000000n, wasm.main(wasm.reinterpretF64));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
})();

(function TestDeoptSignalingNanFloat64Value() {
  var builder = new WasmModuleBuilder();
  let funcRefT = builder.addType(makeSig([kWasmF64], [kWasmI64]));
  let mem = builder.addMemory(1, 1);
  builder.addActiveDataSegment(
    mem.index, [kExprI32Const, 0],
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf4, 0x7f]);

  builder.addFunction("justOne", funcRefT)
    .addBody([kExprI64Const, 1])
    .exportFunc();
  builder.addFunction("reinterpretF32", funcRefT)
    .addBody([kExprLocalGet, 0, kExprI64ReinterpretF64])
    .exportFunc();

  let mainSig =
    makeSig([wasmRefType(funcRefT)], [kWasmI64]);
  builder.addFunction("main", mainSig)
    .addBody([
      kExprI32Const, 0,
      kExprF64LoadMem, 0, 0,
      kExprLocalGet, 0,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  let wasm = builder.instantiate().exports;
  assertEquals(1n, wasm.main(wasm.justOne));
  %WasmTierUpFunction(wasm.main);
  assertEquals(1n, wasm.main(wasm.justOne));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
  assertEquals(0x7ff4000000000000n, wasm.main(wasm.reinterpretF32));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.main));
  }
  %WasmTierUpFunction(wasm.main);
  assertEquals(0x7ff4000000000000n, wasm.main(wasm.reinterpretF32));
  if (%IsWasmTieringPredictable()) {
    assertTrue(%IsTurboFanFunction(wasm.main));
  }
})();
