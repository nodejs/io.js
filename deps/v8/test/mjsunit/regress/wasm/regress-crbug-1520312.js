// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

builder.addMemory(1, 2);

builder.addFunction("main", kSig_v_v).exportFunc().addBody([
  kExprMemorySize, 0,
  kExprI32Const, 1,
  kExprI32Sub,
  kExprI64Const, 42,
  kAtomicPrefix, kExprI64AtomicStore8U, 0x00, 0x10,  // alignment, offset
]);

const instance = builder.instantiate();
instance.exports.main();
