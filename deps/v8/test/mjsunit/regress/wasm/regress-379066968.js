// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-enable-sse4-2

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let sig = builder.addType(kSig_i_v);
builder.addFunction('i16x8GtU', sig).addBody([
  kSimdPrefix, kExprS128Const, ...(new Array(16).fill(0)),
  kSimdPrefix, kExprS128Const, ...(new Array(16).fill(0)),
  kSimdPrefix, kExprI16x8GtU,
  kSimdPrefix, kExprI8x16AllTrue,
]).exportFunc();

let instance = builder.instantiate();
instance.exports.i16x8GtU();
