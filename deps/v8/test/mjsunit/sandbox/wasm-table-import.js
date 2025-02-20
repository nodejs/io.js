// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sandbox-testing

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// module 0
let builder = new WasmModuleBuilder();
let $struct = builder.addStruct([makeField(kWasmI64, true)]);
let $sig0 = builder.addType(makeSig([wasmRefType($struct), kWasmI64], []));
let $sig1_0 = builder.addType(makeSig([kWasmI64, kWasmI64], []));
let $writer = builder.addFunction("writer", $sig0)
  .exportFunc()
  .addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kGCPrefix, kExprStructSet, $struct, 0,
  ]);
let $placeholder = builder.addFunction("placeholder", $sig1_0).addBody([]);
let $t0 = builder
    .addTable(wasmRefType($sig0), 1, 1, [kExprRefFunc, $writer.index])
    .exportAs('table0');
let $t1 = builder
    .addTable(wasmRefType($sig1_0), 1, 1, [kExprRefFunc, $placeholder.index])
    .exportAs('table1');

let instance0 = builder.instantiate();
let { writer, table0, table1 } = instance0.exports;

// module 1
builder = new WasmModuleBuilder();
// $sig_1_1 and $sig1_0 are canonicalized to the same index.
let $sig1_1 = builder.addType(makeSig([kWasmI64, kWasmI64], []));
let $boom = builder.addFunction("boom", $sig1_1)
  .exportFunc()
  .addBody([
    kExprLocalGet, 1,
    kExprLocalGet, 0,
    kExprI32Const, 0,
    kExprCallIndirect, $sig1_1, 0,
  ]);
let $t_imp =
    builder.addImportedTable('import', 'table', 1, 1, wasmRefType($sig1_1));

// Prepare corruption utilities.
const kHeapObjectTag = 1;
const kWasmTableType = Sandbox.getInstanceTypeIdFor('WASM_TABLE_OBJECT_TYPE');
const kWasmTableEntriesOffset = Sandbox.getFieldOffset(kWasmTableType, 'entries');
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

// Set table1.entries[0] = table0.entries[0].
let t0_entries = getField(getPtr(table0), kWasmTableEntriesOffset);
let t0_entry = getField(t0_entries, 8);
let t1_entries = getField(getPtr(table1), kWasmTableEntriesOffset);
setField(t1_entries, 8, t0_entry);

// All checks pass - table type already equivalent, but entry replaced.
let instance1 = builder.instantiate({'import': {'table': table1}});

// This still calls the original function, because overwriting an entry
// doesn't affect the WasmDispatchTable.
instance1.exports.boom(0x414141414141n, 42n);
