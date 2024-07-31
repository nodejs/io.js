// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let array = builder.addArray(kWasmI32, true);

builder.addFunction("main", kSig_v_v).exportFunc().addBody([
  kExprRefNull, array,
  ...wasmI32Const(0xbffffffa),
  kExprI32Const, 1,
  kExprI32ShrS,
  kExprI32Const, 42,
  kGCPrefix, kExprArraySet, array,
]);

const instance = builder.instantiate();
assertThrows(() => instance.exports.main(), WebAssembly.RuntimeError);
