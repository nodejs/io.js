// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-deopt --allow-natives-syntax --turboshaft-wasm
// Flags: --experimental-wasm-inlining --liftoff --expose-gc
// Flags: --turboshaft-wasm-instruction-selection-staged
// Flags: --wasm-inlining-ignore-call-counts --wasm-inlining-factor=30
// Flags: --wasm-inlining-budget=100000 --no-jit-fuzzing

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Test deopt with many results with different types.
(function TestManyResults() {

  let returnCount = 25;
  let builder = new WasmModuleBuilder();
  let struct = builder.addStruct([makeField(kWasmI32, false)]);
  let array = builder.addArray(kWasmI32, true);

  let gcImport = builder.addImport("i", "gc", makeSig([], []));

  let createStruct = builder.addFunction("struct",
      makeSig([kWasmI32], [wasmRefType(struct)]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprStructNew, struct,
    ]).exportFunc();
  let createArray = builder.addFunction("array",
      makeSig([kWasmI32], [wasmRefType(array)]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewFixed, array, 1,
    ]).exportFunc();

  let types = [
    {type: kWasmI32, toI32: [], fromI32: []},
    {type: wasmRefType(struct), toI32: [kGCPrefix, kExprStructGet, struct, 0],
     fromI32: [kExprCallFunction, createStruct.index]},
    {type: kWasmF64, toI32: [kExprI32SConvertF64],
     fromI32: [kExprF64SConvertI32]},
    {type: wasmRefType(array),
     toI32: [kExprI32Const, 0, kGCPrefix, kExprArrayGet, array],
     fromI32: [kExprCallFunction, createArray.index]},
    {type: kWasmF32, toI32: [kExprI32SConvertF32],
     fromI32: [kExprF32SConvertI32]},
    {type: kWasmI31Ref, toI32: [kGCPrefix, kExprI31GetS],
     fromI32: [kGCPrefix, kExprRefI31]},
    {type: kWasmI64, toI32: [kExprI32ConvertI64],
     fromI32: [kExprI64SConvertI32]},
  ];

  let calleeReturns = new Array(returnCount).fill()
    .map((_, i) => types[i % types.length].type);
  let funcRefT = builder.addType(makeSig([], calleeReturns));

  builder.addFunction("one", funcRefT)
    .addBody(generateCalleeBody(1)).exportFunc();
  builder.addFunction("two", funcRefT)
    .addBody(generateCalleeBody(2)).exportFunc();
  builder.addFunction("threeGC", funcRefT)
    .addBody([
      kExprCallFunction, gcImport,
      ...generateCalleeBody(3)
    ]).exportFunc();
  builder.addFunction("four", funcRefT)
    .addBody(generateCalleeBody(4)).exportFunc();

  let main = builder.addFunction("passThrough",
      makeSig([wasmRefType(funcRefT)], calleeReturns))
    .addBody([
      kExprLocalGet, 0,
      kExprCallRef, funcRefT,
  ]).exportFunc();

  builder.addFunction("passThroughExport",
      makeSig([wasmRefType(funcRefT)], [kWasmI32]))
    .addLocals(kWasmI32, 1)
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, main.index,
      ...combine(),
    ]).exportFunc();

  let expectedOne = returnCount * (returnCount + 1) / 2;
  let expectedTwo = expectedOne + returnCount;
  let expectedThree = expectedTwo + returnCount;
  let expectedFour = expectedThree + returnCount;

  let wasm = builder.instantiate({i: {gc}}).exports;
  assertEquals(expectedOne, wasm.passThroughExport(wasm.one));
  %WasmTierUpFunction(wasm.passThrough);
  assertEquals(expectedTwo, wasm.passThroughExport(wasm.two));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.passThrough));
  }
  %WasmTierUpFunction(wasm.passThrough);
  assertEquals(expectedThree, wasm.passThroughExport(wasm.threeGC));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.passThrough));
  }
  // This time tier up the outer function.
  %WasmTierUpFunction(wasm.passThroughExport);
  assertEquals(expectedFour, wasm.passThroughExport(wasm.four));
  if (%IsWasmTieringPredictable()) {
    assertFalse(%IsTurboFanFunction(wasm.passThrough));
  }

  function generateCalleeBody(value) {
    let result = [];
    for (let i = 0; i < returnCount; ++i) {
      result.push(...wasmI32Const(value++), ...types[i % types.length].fromI32);
    }
    return result;
  }

  function combine() {
    let result = [];
    for (let i = 0; i < returnCount; ++i) {
      result.push(
        ...types[(returnCount - i - 1) % types.length].toI32,
        kExprLocalGet, 1,
        kExprI32Add,
        kExprLocalSet, 1
      );
    }
    result.push(kExprLocalGet, 1);
    return result;
  }
})();
