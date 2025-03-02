// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-enable-sse4-2

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let sig = builder.addType(kSig_i_v);

let instructions = [
  'I32x4MinU',
  'I16x8MinU',
  'I16x8UConvertI32x4',
  'I8x16MinS',
  'I32x4MinS',
  'I8x16MaxS',
  'I32x4MaxS',
  'I16x8MaxU',
  'I32x4MaxU',
  'I32x4Mul'
];

// Add a simple function for each instruction.
for (let instr of instructions) {
  builder.addFunction(`${instr}`, sig).addBody([
    kSimdPrefix, kExprS128Const, ...(new Array(16).fill(0)),
    kSimdPrefix, kExprS128Const, ...(new Array(16).fill(0)),
    ...SimdInstr(eval('kExpr' + `${instr}`)),
    kSimdPrefix, kExprI8x16AllTrue,
  ]).exportFunc();
}

let instance = builder.instantiate();

// Call each function.
for (let instr of instructions) {
  instance.exports[instr]();
}
