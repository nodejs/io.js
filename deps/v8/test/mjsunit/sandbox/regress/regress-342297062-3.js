// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --sandbox-testing --no-maglev-inlining --no-turbo-inlining

const kJSFunctionType = Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE');
const kJSFunctionDispatchHandleOffset = Sandbox.getFieldOffset(kJSFunctionType, 'dispatch_handle');

let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

let params = [];
for (let i = 1; i <= 10000; i++) {
  params.push(`a${i}`);
}
let body = 'return 42;';
let f1 = new Function(...params, body);
%PrepareFunctionForOptimization(f1);
f1();
%OptimizeFunctionOnNextCall(f1);
f1();

let f2 = new Function('a', 'b', 'c', 'return 43;');
%PrepareFunctionForOptimization(f2);
f2();
%OptimizeFunctionOnNextCall(f2);
f2();

// Inlining needs to be disabled here so the JIT compiler emits calls to the
// (known) functions instead of inlining them.
function caller1() {
  f1();
}
function caller2() {
  f2();
}
%PrepareFunctionForOptimization(caller1);
%PrepareFunctionForOptimization(caller2);
caller1();
caller2();
%OptimizeFunctionOnNextCall(caller1);
%OptimizeFunctionOnNextCall(caller2);
caller1();
caller2();

// Swap the dispatch handles of the two functions.
let f1_addr = Sandbox.getAddressOf(f1);
let f2_addr = Sandbox.getAddressOf(f2);
let dispatch_handle1 = memory.getUint32(f1_addr + kJSFunctionDispatchHandleOffset, true);
let dispatch_handle2 = memory.getUint32(f2_addr + kJSFunctionDispatchHandleOffset, true);
memory.setUint32(f2_addr + kJSFunctionDispatchHandleOffset, dispatch_handle1, true);
memory.setUint32(f1_addr + kJSFunctionDispatchHandleOffset, dispatch_handle2, true);

caller1();
caller2();
// Should either crash safely or succeed.
