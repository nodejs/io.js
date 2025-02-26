// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --sandbox-testing --expose-gc --allow-natives-syntax

const kJSFunctionType = Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE');
const kSharedFunctionInfoOffset =
    Sandbox.getFieldOffset(kJSFunctionType, 'shared_function_info');
const kSharedFunctionInfoType =
    Sandbox.getInstanceTypeIdFor('SHARED_FUNCTION_INFO_TYPE');
const kFormalParameterCountOffset =
    Sandbox.getFieldOffset(kSharedFunctionInfoType, 'formal_parameter_count');

function foo(a, b) {
  return foo.arguments;
}

%PrepareFunctionForOptimization(foo);
assertArrayEquals([1], foo(1));
assertArrayEquals([1, 2, 3], foo(1, 2, 3));
%OptimizeFunctionOnNextCall(foo);
assertArrayEquals([1], foo(1));
assertArrayEquals([1, 2, 3], foo(1, 2, 3));

// Corrupt foo->shared_function_info->formal_parameter_count.
let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));
let foo_sfi = Sandbox.getObjectAt(memory.getUint32(
    Sandbox.getAddressOf(foo) + kSharedFunctionInfoOffset, true));
let formal_parameter_count = memory.getUint16(
    Sandbox.getAddressOf(foo_sfi) + kFormalParameterCountOffset, true);
assertEquals(formal_parameter_count, 3);
memory.setUint16(
    Sandbox.getAddressOf(foo_sfi) + kFormalParameterCountOffset,
    formal_parameter_count + 1, true);

assertArrayEquals([1, 2, 3], foo(1, 2, 3));
