// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-maglev

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const iterationCount = 100;
let arrayIndex = 0;

function createWasmModuleForLazyDeopt(returnType, createValue, callback) {
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);
  let index = builder.addArray(kWasmI32, true);
  assertEquals(arrayIndex, index);
  let callbackIndex = builder.addImport('env', 'callback', kSig_v_i);

  builder.addFunction("arrayGet",
      makeSig([kWasmExternRef, kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, arrayIndex,
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayGet, arrayIndex,
    ]).exportFunc();

  builder.addFunction("triggerDeopt", makeSig([kWasmI32], [returnType]))
    .addLocals(kWasmI32, 1)
    .addBody([
      ...wasmI32Const(1032),
      ...wasmI32Const(1032),
      kExprI32LoadMem, 0, 0,
      kExprI32Const, 1,
      kExprI32Add,
      kExprLocalTee, 1,
      kExprI32StoreMem, 0, 0,
      kExprBlock, kWasmVoid,
        kExprLocalGet, 1,
        ...wasmI32Const(iterationCount),
        kExprI32Ne,
        kExprBrIf, 0,
        kExprLocalGet, 0,
        kExprCallFunction, callbackIndex,
      kExprEnd,
      ...createValue,
    ]).exportFunc();

  return builder.instantiate({env: {callback}});
}

{ // Test externref.
  // This needs to be var to trigger the lazy deopt on the callback.
  var globalForExternref = 0;
  let {triggerDeopt, arrayGet} = createWasmModuleForLazyDeopt(kWasmExternRef, [
      kExprI32Const, 42, // initial value
      kExprI32Const, 40, // array size
      kGCPrefix, kExprArrayNew, arrayIndex,
      kGCPrefix, kExprExternConvertAny,
    ], () => globalForExternref = 1).exports;

  function test(arg0) {
    var result = 0;
    for (let i = 0; i < iterationCount + 5; i++) {
      if (i == 2) %OptimizeOsr();
      result = %ObserveNode(triggerDeopt(arg0 + globalForExternref));
    }
    return result;
  }

  %PrepareFunctionForOptimization(test);
  assertEquals(42, arrayGet(test(0), 10));
  %OptimizeFunctionOnNextCall(test);
  assertEquals(42, arrayGet(test(0), 10));
}

{ // Test i32.
  var globalForI32 = 0;
  let {triggerDeopt} = createWasmModuleForLazyDeopt(kWasmI32,
    [kExprI32Const, 43], () => globalForI32 = 1).exports;

  function test(arg0) {
    var result = 0;
    for (let i = 0; i < iterationCount + 5; i++) {
      if (i == 2) %OptimizeOsr();
      result = %ObserveNode(triggerDeopt(arg0 + globalForI32));
    }
    return result;
  }

  %PrepareFunctionForOptimization(test);
  assertEquals(43, test(0));
  %OptimizeFunctionOnNextCall(test);
  assertEquals(43, test(0));
}

{ // Test f32.
  var globalForF32 = 0;
  let {triggerDeopt} = createWasmModuleForLazyDeopt(kWasmF32,
    [...wasmF32Const(1.23)], () => globalForF32 = 1).exports;

  function test(arg0) {
    var result = 0;
    for (let i = 0; i < iterationCount + 5; i++) {
      if (i == 2) %OptimizeOsr();
      result = %ObserveNode(triggerDeopt(arg0 + globalForF32));
    }
    return result;
  }

  %PrepareFunctionForOptimization(test);
  assertEqualsDelta(1.23, test(0), .1);
  %OptimizeFunctionOnNextCall(test);
  assertEqualsDelta(1.23, test(0), .1);
}

{ // Test i64.
  var globalForI64 = 0;
  let {triggerDeopt} = createWasmModuleForLazyDeopt(kWasmI64,
    [...wasmI64Const(0x1234567887654321n)], () => globalForI64 = 1).exports;

  function test(arg0) {
    var result = 0;
    for (let i = 0; i < iterationCount + 5; i++) {
      if (i == 2) %OptimizeOsr();
      result = %ObserveNode(triggerDeopt(arg0 + globalForI64));
    }
    return result;
  }

  %PrepareFunctionForOptimization(test);
  assertEquals(0x1234567887654321n, test(0));
  %OptimizeFunctionOnNextCall(test);
  assertEquals(0x1234567887654321n, test(0));
}

{ // Test f64.
  var globalForF64 = 0;
  let {triggerDeopt} = createWasmModuleForLazyDeopt(kWasmF64,
    [...wasmF64Const(1.23)], () => globalForF64 = 1).exports;

  function test(arg0) {
    var result = 0;
    for (let i = 0; i < iterationCount + 5; i++) {
      if (i == 2) %OptimizeOsr();
      result = %ObserveNode(triggerDeopt(arg0 + globalForF64));
    }
    return result;
  }

  %PrepareFunctionForOptimization(test);
  assertEqualsDelta(1.23, test(0), .1);
  %OptimizeFunctionOnNextCall(test);
  assertEqualsDelta(1.23, test(0), .1);
}
