// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-wrapper-tiering-budget=1 --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// later filled in
function func(obj) {
  return obj;
}

// builder 0: export []->[ref 1]
let builder_0 = new WasmModuleBuilder();
let $s0_0 = builder_0.addStruct([makeField(kWasmI32, true)]);
let $s1_0 = builder_0.addStruct([makeField(kWasmI32, true), makeField(wasmRefType($s0_0), true)]);
let $sig_s1_ar_0 = builder_0.addType(makeSig([kWasmAnyRef], [wasmRefType($s1_0)]));
let $i_0 = builder_0.addImport('import', 'func', $sig_s1_ar_0);
let $t_0 = builder_0.addTable(wasmRefType($sig_s1_ar_0), 1, 1, [kExprRefFunc, $i_0]).exportAs('table');

let instance_0 = builder_0.instantiate({ import: { func } });
let { table } = instance_0.exports;

let builder_1 = new WasmModuleBuilder();
let $s0_1 = builder_1.addStruct([makeField(kWasmI32, true)]);
let $s1_1 = builder_1.addStruct([makeField(kWasmExternRef, true), makeField(kWasmI32, true)]);      // src type
let $s2_1 = builder_1.addStruct([makeField(kWasmI32, true), makeField(wasmRefType($s0_1), true)]);  // tgt type, equiv. $s1_0
let $sig_s2_ar_1 = builder_1.addType(makeSig([kWasmAnyRef], [wasmRefType($s2_1)]));                 // equiv. $sig_s1_ar_0
let $sig_v_v_1 = builder_1.addType(kSig_v_v);
let $sig_i_r_1 = builder_1.addType(kSig_i_r);
let $sig_i_i_1 = builder_1.addType(kSig_i_i);
let $sig_v_ii_1 = builder_1.addType(kSig_v_ii);
let $t_1 = builder_1.addImportedTable('import', 'table', 1, 1, wasmRefType($sig_s2_ar_1));

builder_1.addFunction('tierup', $sig_v_v_1).addLocals(wasmRefType($s2_1), 1).addBody([
  // local.set 0 w/ ref $s2_1
  ...wasmI32Const(0),
  kGCPrefix, kExprStructNewDefault, $s0_1,
  kGCPrefix, kExprStructNew, $s2_1,
  kExprLocalSet, 0,

  // call table[0][0]
  kExprLocalGet, 0,
  ...wasmI32Const(0),
  kExprCallIndirect, $sig_s2_ar_1, $t_1,
  kExprDrop,
  // call table[0][0]
  kExprLocalGet, 0,
  ...wasmI32Const(0),
  kExprCallIndirect, $sig_s2_ar_1, $t_1,
  kExprDrop,
]).exportFunc();

builder_1.addFunction('crash', $sig_v_ii_1).addBody([
  kExprRefNull, kNullExternRefCode,
  kExprLocalGet, 0,
  ...wasmI32Const(7),
  kExprI32Sub,
  kGCPrefix, kExprStructNew, $s1_1,
  ...wasmI32Const(0),
  kExprCallIndirect, $sig_s2_ar_1, $t_1,
  kGCPrefix, kExprStructGet, $s2_1, 1,
  kExprLocalGet, 1,
  kGCPrefix, kExprStructSet, $s0_1, 0,
]).exportFunc();

let instance_1 = builder_1.instantiate({ import: { table } });
let { tierup, crash } = instance_1.exports;

tierup();

assertThrows(() => crash(0x42424242, 0x13371337), TypeError);
