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

/**
 * Test load elimination using "user-space" arrays.
 * Assumption: A user-space array is implemented as a struct containing a wasm
 * array used as a backing store.
 * Assumption 2: For simplicity, there isn't any bounds check happening on the
 * size stored in the wasm struct.
 */
(function TestUserSpaceArrayLoadElimination() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let backingStore = builder.addArray(kWasmI8, true);
  let arrayStruct = builder.addStruct([
    makeField(kWasmI32 /*length*/, true),
    makeField(wasmRefType(backingStore), true)
  ]);

  builder.addFunction('createArray',
      makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprI32Const, 3, // length
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kGCPrefix, kExprArrayNewFixed, backingStore, 3,
      kGCPrefix, kExprStructNew, arrayStruct,
      kGCPrefix, kExprExternConvertAny,
    ])
    .exportFunc();

  builder.addFunction('getLength',
      makeSig([kWasmExternRef], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, arrayStruct,
      kGCPrefix, kExprStructGet, arrayStruct, 0,
    ])
    .exportFunc();

  builder.addFunction('get', makeSig([kWasmExternRef, kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kGCPrefix, kExprAnyConvertExtern,
      kGCPrefix, kExprRefCast, arrayStruct,
      kGCPrefix, kExprStructGet, arrayStruct, 1,
      kExprLocalGet, 1,
      kGCPrefix, kExprArrayGetU, backingStore,
    ])
    .exportFunc();

  let instance = builder.instantiate({});
  let wasm = instance.exports;

  let myUserArray = wasm.createArray(42, 43, 44);

  let sumArray = (arrayStruct) => {
    let get = wasm.get;
    let length = wasm.getLength(arrayStruct);
    let result = 0;
    for (let i = 0; i < length; ++i) {
      result += get(arrayStruct, i);
    }
    return result;
  };

  testOptimized(() => assertEquals(42+43+44, sumArray(myUserArray)), sumArray);
})();
