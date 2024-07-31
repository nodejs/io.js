// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --trace-turbo-inlining --allow-natives-syntax
// Flags: --turbofan --no-always-turbofan --no-always-sparkplug
// Concurrent inlining leads to additional traces.
// Flags: --no-stress-concurrent-inlining

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function testOptimized(run, fctToOptimize) {
  fctToOptimize = fctToOptimize ?? run;
  %PrepareFunctionForOptimization(fctToOptimize);
  for (let i = 0; i < 10; ++i) {
    run();
  }
  %OptimizeFunctionOnNextCall(fctToOptimize);
  run();
}

function createWasmModule(moduleName) {
  let builder = new WasmModuleBuilder();
  builder.setName(moduleName);
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

    builder.addFunction(undefined, makeSig([], []))
    .addBody(new Array(100).fill(kExprNop))
    .exportAs('largeFunction');

  let instance = builder.instantiate({});
  return instance.exports;
}

let moduleA = createWasmModule("moduleA");
let moduleB = createWasmModule(/*no name*/);

(function TestInlining() {
  print(arguments.callee.name);

  let jsFct = () => {
    moduleA.largeFunction();
    let array = moduleA.createArray(42);
    try {
      moduleA.arrayLen(array)
    } catch (e) {
    }
    %PrepareFunctionForOptimization(inner);
    return inner();

    function inner() {
      return moduleB.arrayLen(array) + moduleA.arrayLen(array);
    }
  };

  testOptimized(jsFct);
})();
