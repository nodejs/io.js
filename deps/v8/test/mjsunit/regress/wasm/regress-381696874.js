// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const BRUTE_FIELDS = 30;
const TOTAL_FIELDS = 40;
const colls = [
  {
    // pre-592f191
    none: [0x29a, 0x28afbef6],  // field[0]: ref null none
    any: [0x57, 0x2c199681],    // field[0]: ref null any
    idx: 0,
  },
  {
    // post-592f191
    none: [0x1e8, 0x2486eaaa],  // field[2]: ref null none
    any: [0x2e4, 0xc4d7a23],    // field[2]: ref null any
    idx: 2,
  },
];

function convert_to_fields([base, idx]) {
  let fields = [];
  for (let i = 0; i < TOTAL_FIELDS - BRUTE_FIELDS; i++) {
    fields.push(makeField(
        wasmRefNullType((base & (1 << i)) ? kWasmAnyRef : kWasmNullRef), true));
  }
  for (let i = 0; i < BRUTE_FIELDS; i++) {
    fields.push(makeField(
        wasmRefNullType((idx & (1 << i)) ? kWasmAnyRef : kWasmNullRef), true));
  }

  return fields;
}

let instance, addrof, caged_read, caged_write;
for (let coll of colls) {
  console.log(`[*] trying ${JSON.stringify(coll)}`);
  let builder = new WasmModuleBuilder();

  let $s0 = builder.addStruct([makeField(kWasmI32, true)]);
  let $s1 = builder.addStruct(
      [makeField(kWasmExternRef, true), makeField(kWasmI32, true)]);
  let $s2 = builder.addStruct(
      [makeField(kWasmI32, true), makeField(wasmRefType($s0), true)]);

  builder.startRecGroup();
  let $s_dst =
      builder.addStruct(convert_to_fields(coll.none), kNoSuperType, false);
  builder.endRecGroup();

  builder.startRecGroup();
  let $s_src =
      builder.addStruct(convert_to_fields(coll.any), kNoSuperType, false);
  builder.endRecGroup();

  let $sig_i_r = builder.addType(makeSig([kWasmExternRef], [kWasmI32]));
  let $sig_i_i = builder.addType(makeSig([kWasmI32], [kWasmI32]));
  let $sig_v_ii = builder.addType(makeSig([kWasmI32, kWasmI32], []));

  builder
    .addFunction('addrof', $sig_i_r)
    .addLocals(wasmRefType($s_src), 1)
    .addBody([
      kGCPrefix, kExprStructNewDefault, $s_src,
      kExprLocalTee, 1,

      kExprLocalGet, 0,
      ...wasmI32Const(0),
      kGCPrefix, kExprStructNew, $s1,
      kGCPrefix, kExprStructSet, $s_src, ...wasmSignedLeb(coll.idx),

      kExprLocalGet, 1,
      kGCPrefix, kExprStructGet, $s_dst, ...wasmSignedLeb(coll.idx),
      kGCPrefix, kExprStructGet, $s2, 0,
    ])
    .exportFunc();

  assertThrows(
      () => builder.toModule(), WebAssembly.CompileError,
      /struct.get\[0\] expected type.*, found local.get of type/);
}
