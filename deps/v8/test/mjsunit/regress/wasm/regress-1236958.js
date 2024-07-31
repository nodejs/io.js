// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

var builder = new WasmModuleBuilder();

let array = builder.addArray(kWasmI64, true);

builder.addFunction('test', kSig_v_v)
    .addBody([kExprLoop, kWasmVoid,
                kExprI64Const, 15,
                kExprI32Const, 12,
                kGCPrefix, kExprArrayNew, array,
                kExprDrop,
              kExprEnd])
    .exportFunc();

builder.instantiate();
