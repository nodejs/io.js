// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

let array = builder.addArray(kWasmI32, true);

builder.addFunction('main', kSig_v_v)
    .exportFunc()
    .addBody([
      // dest array
      kExprRefNull, kNullRefCode,
      // dest index
      kExprI32Const, 0,
      // src array
      kExprRefNull, kNullRefCode,
      // src index
      kExprI32Const, 0,
      // copy length
      kExprI32Const, 0,
      kGCPrefix, kExprArrayCopy, array, array,
    ]);

assertThrows(
    () => builder.instantiate().exports.main(), WebAssembly.RuntimeError,
    'dereferencing a null pointer');
