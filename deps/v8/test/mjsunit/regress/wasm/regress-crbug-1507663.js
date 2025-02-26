// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-inlining

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
builder.addMemory(0, 2, false);

let grow_func = builder.addFunction('grow', kSig_i_i).addBody([
  kExprLocalGet, 0,
  kExprMemoryGrow, kMemoryZero
]);

builder.addFunction('main', kSig_i_i).exportFunc().addBody([
  kExprLocalGet, 0,
  kExprCallFunction, grow_func.index,
  kExprDrop,
  kExprMemorySize, kMemoryZero
]);

let instance = builder.instantiate();
assertEquals(1, instance.exports.main(1));
