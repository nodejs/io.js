// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-inlining

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(kSig_i_i);
builder.addFunction("main", kSig_i_i)
  .addBody([kExprI32Const, 0x00, kExprRefNull, 0x01, kExprCallRef, 0x01])
  .exportFunc();

let instance = builder.instantiate();

assertTraps(WebAssembly.RuntimeError, () => instance.exports.main());
