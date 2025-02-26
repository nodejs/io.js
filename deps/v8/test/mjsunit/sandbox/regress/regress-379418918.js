// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --sandbox-fuzzing --allow-natives-syntax

function corrupt(func) {
  const kHeapObjectTagMask = 0x3;
  const kJSFunctionType = Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE');
  const kSharedFunctionInfoOffset =
      Sandbox.getFieldOffset(kJSFunctionType, 'shared_function_info');
  const kSharedFunctionInfoType =
      Sandbox.getInstanceTypeIdFor('SHARED_FUNCTION_INFO_TYPE');
  const kFormalParameterCountOffset =
      Sandbox.getFieldOffset(kSharedFunctionInfoType, 'formal_parameter_count');

  const memview = new DataView(new Sandbox.MemoryView(0, 0x100000000));
  let func_addr = Sandbox.getAddressOf(func);
  let sfi_addr = memview.getUint32(func_addr + kSharedFunctionInfoOffset, true) & ~kHeapObjectTagMask;
  memview.setUint16(sfi_addr + kFormalParameterCountOffset, 32, true);
}

function f0(acc, value) {
  value.b = value;
}
corrupt(f0);

function f3() {
  const v5 = Array(Array);
  v5.reduce(f0, 0);
}
%PrepareFunctionForOptimization(f3);
%PrepareFunctionForOptimization(f0);
f3();
%OptimizeFunctionOnNextCall(f3);
f3();
