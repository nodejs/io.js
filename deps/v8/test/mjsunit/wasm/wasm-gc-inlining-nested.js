// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan
// Flags: --no-always-turbofan --no-always-sparkplug --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function testOptimized(run, fctToOptimize) {
  fctToOptimize = fctToOptimize ?? run;
  %PrepareFunctionForOptimization(fctToOptimize);
  for (let i = 0; i < 10; ++i) {
    run();
  }
  %OptimizeFunctionOnNextCall(fctToOptimize);
  run();
  assertOptimized(fctToOptimize);
}

let wasm = (function createWasmModule() {
  let builder = new WasmModuleBuilder();
  let array = builder.addArray(kWasmI32, true);

  builder.addFunction('createArray', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprArrayNewDefault, array,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('arrayLen', makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCastNull, array,
      kGCPrefix, kExprArrayLen,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  return instance.exports;
})();

(function TestNestedInlining() {
  print(arguments.callee.name);

  let array42 = wasm.createArray(42);

  let testNested = (expected, array) => {
    let fct = () => assertSame(expected, wasm.arrayLen(array));
    %PrepareFunctionForOptimization(fct);
    return fct();
  };
  // Compile and optimize wasm function inlined into JS (not trapping).
  testOptimized(() => testNested(42, array42), testNested);
  // Cause trap and JS error creation.
  assertTraps(kTrapNullDereference, () => testNested(42, null));
  assertOptimized(testNested);
})();

(function TestNestedInliningExtraArguments() {
  print(arguments.callee.name);
  let array42 = wasm.createArray(42);
  // Compile and optimize wasm function inlined into JS (not trapping).
  let testExtraArgs = (expected, array) => {
    let fct = () => assertSame(expected, wasm.arrayLen(array));
    %PrepareFunctionForOptimization(fct);
    // fct doesn't take any arguments, so these are all "superfluous".
    return fct(array, 1, undefined, {"test": "object"});
  };
  testOptimized(() => testExtraArgs(42, array42), testExtraArgs);
  // Cause trap and JS error creation.
  assertTraps(kTrapNullDereference, () => testExtraArgs(42, null));
  assertOptimized(testExtraArgs);
})();

(function TestNestedInliningWithReceiverFromOuterScope() {
  print(arguments.callee.name);
  let array42 = wasm.createArray(42);
  // Compile and optimize wasm function inlined into JS (not trapping).
  let fct = (expected, array) => assertSame(expected, wasm.arrayLen(array));
  %PrepareFunctionForOptimization(fct);
  let obj = { fct };
  let testReceiverOuter = (expected, array) => {
    return obj.fct(expected, array);
  };
  testOptimized(() => testReceiverOuter(42, array42), testReceiverOuter);
  // Cause trap and JS error creation.
  assertTraps(kTrapNullDereference, () => testReceiverOuter(42, null));
  assertOptimized(testReceiverOuter);
})();

(function TestNestedInliningWithReceiverAsInlineObject() {
  print(arguments.callee.name);
  let array42 = wasm.createArray(42);
  // Compile and optimize wasm function inlined into JS (not trapping).
  let testReceiverInner = (expected, array) => {
    let fct = () => assertSame(expected, wasm.arrayLen(array));
    %PrepareFunctionForOptimization(fct);
    let obj = { fct };
    return obj.fct();
  };
  testOptimized(() => testReceiverInner(42, array42), testReceiverInner);
  // Cause trap and JS error creation.
  assertTraps(kTrapNullDereference, () => testReceiverInner(42, null));
  assertOptimized(testReceiverInner);
})();
