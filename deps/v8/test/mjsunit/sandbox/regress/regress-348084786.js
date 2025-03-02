// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sandbox-testing

const kJSFunctionType = Sandbox.getInstanceTypeIdFor('JS_FUNCTION_TYPE');
const kSharedFunctionInfoType = Sandbox.getInstanceTypeIdFor('SHARED_FUNCTION_INFO_TYPE');
const kJSFunctionSFIOffset = Sandbox.getFieldOffset(kJSFunctionType, 'shared_function_info');
const kSharedFunctionInfoLengthOffset = Sandbox.getFieldOffset(kSharedFunctionInfoType, 'length');
const kSharedFunctionInfoFormalParameterCountOffset = Sandbox.getFieldOffset(kSharedFunctionInfoType, 'formal_parameter_count');
const kHeapObjectTag = 1;

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

const builder = new WasmModuleBuilder();
builder.exportMemoryAs("mem0", 0);
let $mem0 = builder.addMemory(1, 1);

let $sig_i_l = builder.addType(kSig_v_i);

builder.addFunction("func", builder.addType(kSig_l_l)).exportFunc().addBody([
  kExprLocalGet, 0,
]);

let instance = builder.instantiate();

instance.exports.func(0n);

let func_addr = Sandbox.getAddressOf(instance.exports.func);
let sfi_addr = memory.getUint32(func_addr + kJSFunctionSFIOffset, true) - kHeapObjectTag;
memory.setUint16(sfi_addr + kSharedFunctionInfoLengthOffset, 0xffff, true);
memory.setUint16(sfi_addr + kSharedFunctionInfoFormalParameterCountOffset, 0xffff, true);

instance.exports.func(0x4141n);
