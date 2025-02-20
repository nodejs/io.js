// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note that this number is way too large to trigger a deopt on its own, however
// with this test mode enabled, the deoptimizer triggers a GC just before
// materializing the heap objects.
// Flags: --deopt-every-n-times=1000000000
// Disable maglev as otherwise this reproducer doesn't cause a lazy deopt.
// Flags: --allow-natives-syntax --turbofan --no-maglev

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const iterationCount = 100;
var globalForDeopt = 0; // Triggers a lazy deopt when modified.
let callback = () => globalForDeopt = 1; // Modify the global.

let builder = new WasmModuleBuilder();
builder.addMemory(1, 1);
let callbackIndex = builder.addImport('env', 'callback', kSig_v_i);

builder.addFunction("triggerDeopt", makeSig([kWasmI32], [kWasmI64]))
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
    // Return an i64. Due to the value being odd, when the GC accidentally would
    // visit the raw value during a lazy deopt, this would cause a crash as the
    // GC would treat this as a heap object and dereference it to access its
    // map.
    ...wasmI64Const(0x1234567887654321n)
  ]).exportFunc();

let {triggerDeopt} = builder.instantiate({env: {callback}}).exports;

// The function that will lazily deopted when the global is modified inside the
// callback called within the wasm function from this function with an inlined
// JS-to-Wasm wrapper.
function test(arg0) {
  var result = 0;
  for (let i = 0; i < iterationCount + 5; i++) {
    if (i == 2) %OptimizeOsr();
    result = %ObserveNode(triggerDeopt(arg0 + globalForDeopt));
  }
  return result;
}

%PrepareFunctionForOptimization(test);
assertEquals(0x1234567887654321n, test(0));
%OptimizeFunctionOnNextCall(test);
assertEquals(0x1234567887654321n, test(0));
