// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helpers to test interoperability of Wasm objects in JavaScript.

// The following flags are required:
// Flags: --turbofan --no-always-turbofan --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function CreateWasmObjects() {
  let builder = new WasmModuleBuilder();
  let struct_type = builder.addStruct([makeField(kWasmI32, true)]);
  let array_type = builder.addArray(kWasmI32, true);
  builder.addFunction('MakeStruct', makeSig([], [kWasmExternRef]))
      .exportFunc()
      .addBody([
        kExprI32Const, 42,                       // --
        kGCPrefix, kExprStructNew, struct_type,  // --
        kGCPrefix, kExprExternConvertAny        // --
      ]);
  builder.addFunction('MakeArray', makeSig([], [kWasmExternRef]))
      .exportFunc()
      .addBody([
        kExprI32Const, 2,                             // length
        kGCPrefix, kExprArrayNewDefault, array_type,  // --
        kGCPrefix, kExprExternConvertAny             // --
      ]);

  let instance = builder.instantiate();
  return {
    struct: instance.exports.MakeStruct(),
    array: instance.exports.MakeArray(),
  };
}

function testThrowsRepeated(fn, ErrorType, ignoreDeopts = false) {
  const maxRuns = 3;
  for (let run = 0; run < maxRuns; ++run) {
    %PrepareFunctionForOptimization(fn);
    for (let i = 0; i < 5; i++) assertThrows(fn, ErrorType);
    %OptimizeFunctionOnNextCall(fn);
    assertThrows(fn, ErrorType);
    if (isOptimized(fn) || ignoreDeopts) return;
  }
  assertOptimized(fn);
}

function repeated(fn, ignoreDeopts = false) {
  const maxRuns = 3;
  for (let run = 0; run < maxRuns; ++run) {
    %PrepareFunctionForOptimization(fn);
    for (let i = 0; i < 5; i++) fn();
    %OptimizeFunctionOnNextCall(fn);
    fn();
    if (isOptimized(fn) || ignoreDeopts) return;
  }
  assertOptimized(fn);
}

// Prevent optimization, so that the test functions can not be inlined which
// can cause issues in combination with `assertOptimized` and deopts in test
// code.
%NeverOptimizeFunction(testThrowsRepeated);
%NeverOptimizeFunction(repeated);
