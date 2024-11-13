// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --no-liftoff
d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var builder = new WasmModuleBuilder();

builder.addMemory(1, 1);

builder.addFunction("main", kSig_i_i).addBody([
    kExprLocalGet, 0,
    kExprIf, kWasmI32,
      kExprLocalGet, 0,
    kExprElse,
      kExprI32Const, 42, // index
      kExprI32Const, 0,  // value
      kExprI32StoreMem, 0, 0,
      kExprI32Const, 11,
      kExprLocalGet, 0,
      kExprI32DivS,
    kExprEnd
  ]).exportFunc();

var instance = builder.instantiate({});
