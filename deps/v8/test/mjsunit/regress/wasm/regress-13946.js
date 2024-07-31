// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

let array = builder.addArray(kWasmI32, true);
let sig = builder.addType(kSig_i_i);
let struct = builder.addStruct([makeField(kWasmI32, true)]);

builder.addFunction("test", sig)
  .addBody([
    kExprI32Const, 0x0a,
    kGCPrefix, kExprArrayNewDefault, 0x00,
    kExprI32Const, 0x00,
    kGCPrefix, kExprStructNew, 0x02,
    kGCPrefix, kExprStructGet, 0x02, 0x00,
    kExprLocalTee, 0x00,
    kExprI32Const, 0x01,
    kExprI32Const, 0x00,
    kGCPrefix, kExprArrayFill, 0x00,
    kExprLocalGet, 0x00])
  .exportFunc();

assertEquals(0, builder.instantiate().exports.test(42));
