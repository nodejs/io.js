// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

var builder = new WasmModuleBuilder();

let array_index = builder.addArray(kWasmS128, true);

builder.addFunction("main", kSig_i_i)
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprArrayNewDefault, array_index,
    kGCPrefix, kExprArrayLen,
  ])
  .exportFunc();

var instance = builder.instantiate();

assertThrows(
    () => instance.exports.main(1 << 26), WebAssembly.RuntimeError,
    'requested new array is too large');
