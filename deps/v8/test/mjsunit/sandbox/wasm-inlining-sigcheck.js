// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --sandbox-testing --turboshaft-wasm

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.exportMemoryAs("mem0", 0);
let $mem0 = builder.addMemory(1, 1);

let $box = builder.addStruct([makeField(kWasmFuncRef, true)]);
let $struct = builder.addStruct([makeField(kWasmI32, true)]);

let $sig_i_l = builder.addType(kSig_i_l);

let $f0 = builder.addFunction("func0", makeSig([wasmRefType($struct)], []))
  .exportFunc()
  .addBody([
    kExprLocalGet, 0,
    kExprI32Const, 42,
    kGCPrefix, kExprStructSet, $struct, 0,
  ]);

let $f1 = builder.addFunction("func1", $sig_i_l).exportFunc().addBody([
  kExprI32Const, 0,
]);

builder.addFunction("get_func0", kSig_r_v).exportFunc().addBody([
  kExprRefFunc, $f0.index,
  kGCPrefix, kExprStructNew, $box,
  kGCPrefix, kExprExternConvertAny,
]);
builder.addFunction("get_func1", kSig_r_v).exportFunc().addBody([
  kExprRefFunc, $f1.index,
  kGCPrefix, kExprStructNew, $box,
  kGCPrefix, kExprExternConvertAny,
]);
builder.addFunction("boom", makeSig([kWasmFuncRef, kWasmI64], [kWasmI32]))
  .exportFunc()
  .addBody([
    kExprLocalGet, 1,
    kExprLocalGet, 0,
    kGCPrefix, kExprRefCast, $sig_i_l,
    kExprCallRef, $sig_i_l,
  ])

let instance = builder.instantiate();

let func0 = instance.exports.func0;
let func1 = instance.exports.func1;
let boom = instance.exports.boom;

// Collect type feedback.
for (let i = 0; i < 10; i++) {
  instance.exports.boom(func1, 0n);
}

// Prepare corruption utilities.
const kHeapObjectTag = 1;
const kMapOffset = 0;
const kStructField0Offset = 8;  // 0:map, 4:hash
const kWasmInternalFunctionOffset = 4;

let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

function getPtr(obj) {
  return Sandbox.getAddressOf(obj) + kHeapObjectTag;
}
function getField(obj, offset) {
  return memory.getUint32(obj + offset - kHeapObjectTag, true);
}
function setField(obj, offset, value) {
  memory.setUint32(obj + offset - kHeapObjectTag, value, true);
}

// Corrupt $f1: if it has the wrong WasmInternalFunction when we process
// feedback, we'll inline the wrong target.
let f0_box = getPtr(instance.exports.get_func0());
let f0 = getField(f0_box, kStructField0Offset);
let f0_int = getField(f0, kWasmInternalFunctionOffset);

let f1_box = getPtr(instance.exports.get_func1());
let f1 = getField(f1_box, kStructField0Offset);

setField(f1, kWasmInternalFunctionOffset, f0_int);

// Also corrupt $f0 to make it past the type check.
let f1_map = getField(f1, kMapOffset);
setField(f0, kMapOffset, f1_map);

// Trigger optimization. This would inline the wrong target; the signature
// check should kill the process instead.
%WasmTierUpFunction(instance.exports.boom);

// If the process was still alive, this would cause the sandbox violation.
instance.exports.boom(func0, BigInt(Sandbox.targetPage));

assertUnreachable("Process should have been killed.");
