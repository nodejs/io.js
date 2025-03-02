// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sandbox-testing

const kJSFunctionType = Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE');
const kJSFunctionDispatchHandleOffset = Sandbox.getFieldOffset(kJSFunctionType, 'dispatch_handle');

let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

let params = [];
for (let i = 1; i <= 10000; i++) {
  params.push(`a${i}`);
}
let body = 'return 42;';
let f1 = new Function(...params, body);
f1();

let f2 = new Function('a', 'b', 'c', 'return 43;');
f2();

// Transplant the dispatch handle from one function to another.
let f1_addr = Sandbox.getAddressOf(f1);
let f2_addr = Sandbox.getAddressOf(f2);
let dispatch_handle1 = memory.getUint32(f1_addr + kJSFunctionDispatchHandleOffset, true);
let dispatch_handle2 = memory.getUint32(f2_addr + kJSFunctionDispatchHandleOffset, true);
memory.setUint32(f2_addr + kJSFunctionDispatchHandleOffset, dispatch_handle1, true);
memory.setUint32(f1_addr + kJSFunctionDispatchHandleOffset, dispatch_handle2, true);

f1();
f2();
// Should either crash safely or succeed.
