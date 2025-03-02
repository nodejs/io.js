// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// cidx 0, 1, 2: i8arr, i16arr, jstag sig
let builder = new WasmModuleBuilder();

let $s0 = builder.addStruct([makeField(kWasmI32, true)]);  // cidx 3
let $s1 = builder.addStruct(
    [makeField(kWasmExternRef, true), makeField(kWasmI32, true)]);  // cidx 4

// recgroup: 0~3 dummy, 4 target, 5 ref cidx 4
builder.startRecGroup();
for (let i = 0; i < 4; i++) {
  builder.addArray(kWasmI32, true);  // ridx 0~3
}
builder.addStruct(
    [makeField(kWasmI32, true), makeField(wasmRefType($s0), true)]);  // ridx 4
let $s_s1 =
    builder.addStruct([makeField(wasmRefType($s1), true)]);  // ref cidx 4
builder.endRecGroup();

// recgroup: 0~3 dummy, 4 target, 5 relref 4
builder.startRecGroup();
for (let i = 0; i < 4; i++) {
  builder.addArray(kWasmI32, true);  // ridx 0~3
}
let $s2 = builder.addStruct(
    [makeField(kWasmI32, true), makeField(wasmRefType($s0), true)]);  // ridx 4
let $s_s2 =
    builder.addStruct([makeField(wasmRefType($s2), true)]);  // ref ridx 4
builder.endRecGroup();

let $sig_i_r = builder.addType(makeSig([kWasmExternRef], [kWasmI32]));
let $sig_i_i = builder.addType(makeSig([kWasmI32], [kWasmI32]));
let $sig_v_ii = builder.addType(makeSig([kWasmI32, kWasmI32], []));

builder.addFunction('boom', $sig_v_ii).addBody([
  kExprRefNull, kExternRefCode,
  kExprLocalGet, 0,
  ...wasmI32Const(7),
  kExprI32Sub,
  kGCPrefix, kExprStructNew, $s1,
  kGCPrefix, kExprStructNew, $s_s1,
  kGCPrefix, kExprStructGet, $s_s2, 0,
  kGCPrefix, kExprStructGet, $s2, 1,
  kExprLocalGet, 1,
  kGCPrefix, kExprStructSet, $s0, 0,
]).exportFunc();

assertThrows(
    () => builder.instantiate(), WebAssembly.CompileError,
    /struct.get.*expected type/);
