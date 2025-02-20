// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function func(obj) {
  return obj;
}

// builder 0: export []->[ref 1]
let builder_0 = new WasmModuleBuilder();
let $s0_0 = builder_0.addStruct([makeField(kWasmI32, true)]);
let $s1_0 = builder_0.addStruct([makeField(kWasmI32, true),
                                 makeField(wasmRefType($s0_0), true)]);
let $sig_s1_ar_0 = builder_0.addType(
    makeSig([kWasmAnyRef], [wasmRefType($s1_0)]));
let $i_0 = builder_0.addImport('import', 'func', $sig_s1_ar_0);
let $t_0 = builder_0.addGlobal(
    wasmRefType($sig_s1_ar_0), 0, 0, [kExprRefFunc, $i_0])
  .exportAs('global11');

let instance_0 = builder_0.instantiate({ import: { func } });

let { global11 } = instance_0.exports;

// builder 1: export [ref 1]->[ref 2]
let builder_1 = new WasmModuleBuilder();
let $s0_1 = builder_1.addStruct([makeField(kWasmI32, true)]);
let $s1_1 = builder_1.addStruct([makeField(kWasmExternRef, true),
                                 makeField(kWasmI32, true)]);
// equiv. $s1_0
let $s2_1 = builder_1.addStruct(
    [makeField(kWasmI32, true), makeField(wasmRefType($s0_1), true)]);
// equiv. $sig_s1_ar_0
let $sig_s2_ar_1 = builder_1.addType(
    makeSig([kWasmAnyRef], [wasmRefType($s2_1)]));
let $sig_v_v_1 = builder_1.addType(kSig_v_v);
let $sig_i_r_1 = builder_1.addType(kSig_i_r);
let $sig_i_i_1 = builder_1.addType(kSig_i_i);
let $sig_v_ii_1 = builder_1.addType(kSig_v_ii);

let $g_1 = builder_1.addImportedGlobal(
    "import", "global11", wasmRefType($sig_s2_ar_1));

let instance_1 = builder_1.instantiate({ import: { global11 } });
