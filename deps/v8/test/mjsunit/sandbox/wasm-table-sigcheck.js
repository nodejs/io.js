// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sandbox-testing

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.exportMemoryAs("mem0", 0);
let $mem0 = builder.addMemory(1, 1);

let $box = builder.addStruct([makeField(kWasmFuncRef, true)]);
let $struct = builder.addStruct([makeField(kWasmI32, true)]);

let $sig_i_l = builder.addType(kSig_i_l);
let $sig_v_struct = builder.addType(makeSig([wasmRefType($struct)], []));

let $f0 = builder.addFunction("func0", $sig_v_struct)
  .exportFunc()
  .addBody([
    kExprLocalGet, 0,
    kExprI32Const, 42,
    kGCPrefix, kExprStructSet, $struct, 0,
  ]);

let $f1 = builder.addFunction("func1", $sig_i_l).exportFunc().addBody([
  kExprI32Const, 0,
]);

let $t0 =
    builder.addTable(wasmRefType($sig_i_l), 1, 1, [kExprRefFunc, $f1.index]);
builder.addExportOfKind("table0", kExternalTable, $t0.index);

builder.addFunction("boom", kSig_i_l)
  .exportFunc()
  .addBody([
    kExprLocalGet, 0,  // func parameter
    kExprI32Const, 0,  // func index
    kExprCallIndirect, $sig_i_l, kTableZero,
  ])

let instance = builder.instantiate();

let boom = instance.exports.boom;
let func0 = instance.exports.func0;
let table0 = instance.exports.table0;

// Prepare corruption utilities.
const kHeapObjectTag = 1;
const kWasmTableObjectTypeOffset = 28;

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

// Without corruption, putting func0 into the table fails gracefully.
assertThrows(
    () => { table0.set(0, func0); }, TypeError,
    /assigned exported function has to be a subtype of the expected type/);

// Corrupt the table's type to accept putting $func0 into it.
let t0 = getPtr(table0);
const kRef = 9;
const kSmiTagSize = 1;
const kHeapTypeShift = 5;
let expected_old_type = (($sig_i_l << kHeapTypeShift) | kRef) << kSmiTagSize;
let new_type = (($sig_v_struct << kHeapTypeShift) | kRef) << kSmiTagSize;
assertEquals(expected_old_type, getField(t0, kWasmTableObjectTypeOffset));
setField(t0, kWasmTableObjectTypeOffset, new_type);

// This should run into a signature check that kills the process.
table0.set(0, func0);

// If the process was still alive, this would cause the sandbox violation.
instance.exports.boom(BigInt(Sandbox.targetPage));

assertUnreachable("Process should have been killed.");
