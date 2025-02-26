// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const BRUTE_FIELDS = 30;
const TOTAL_FIELDS = 40;
function convert_to_fields([base, idx], nullify = false) {
  let fields = [];
  for (let i = 0; i < TOTAL_FIELDS - BRUTE_FIELDS; i++) {
    let is_mut = !!(base & (1 << i));
    fields.push(makeField(
        wasmRefNullType(!is_mut && nullify ? kWasmNullRef : kWasmAnyRef),
        is_mut));
  }
  for (let i = 0; i < BRUTE_FIELDS; i++) {
    let is_mut = !!(idx & (1 << i));
    fields.push(makeField(
        wasmRefNullType(!is_mut && nullify ? kWasmNullRef : kWasmAnyRef),
        is_mut));
  }

  return fields;
}

let instance, addrof, caged_read, caged_write;
let coll = {
  // post-592f191
  mut: [0x1f7, 0x2e971cab],    // field[0]: mut ref null any
  const : [0x29c, 0xf09b901],  // field[0]: !mut ref null any
  idx: 0,
};

let builder = new WasmModuleBuilder();

let $s0 = builder.addStruct([makeField(kWasmI32, true)]);
let $s1 = builder.addStruct(
    [makeField(kWasmExternRef, true), makeField(kWasmI32, true)]);
let $s2 = builder.addStruct(
    [makeField(kWasmI32, true), makeField(wasmRefType($s0), true)]);

builder.startRecGroup();
let $s_dst =
    builder.addStruct(convert_to_fields(coll.mut), kNoSuperType, false);
builder.endRecGroup();

builder.startRecGroup();
let $s_src =
    builder.addStruct(convert_to_fields(coll.const), kNoSuperType, false);
builder.endRecGroup();

builder.startRecGroup();
let $s_src_none =
    builder.addStruct(convert_to_fields(coll.const, true), $s_src, false);
builder.endRecGroup();

let $sig_i_r = builder.addType(makeSig([kWasmExternRef], [kWasmI32]));
let $sig_i_i = builder.addType(makeSig([kWasmI32], [kWasmI32]));
let $sig_v_ii = builder.addType(makeSig([kWasmI32, kWasmI32], []));

builder
  .addFunction('foo', $sig_i_r)
  .addLocals(wasmRefType($s_src_none), 1)
  .addBody([
    kGCPrefix, kExprStructNewDefault, $s_src_none,
    kExprLocalTee, 1,

    kExprLocalGet, 0,
    ...wasmI32Const(0),
    kGCPrefix, kExprStructNew, $s1,
    kGCPrefix, kExprStructSet, $s_dst, ...wasmSignedLeb(coll.idx),

    kExprLocalGet, 1,
    kGCPrefix, kExprStructGet, $s_src_none, ...wasmSignedLeb(coll.idx),
    kGCPrefix, kExprStructGet, $s2, 0,
  ])
  .exportFunc();

assertThrows(
    () => builder.toModule(), WebAssembly.CompileError,
    /struct.set\[0\] expected type.*, found local.tee of type/);
