// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

// Under certain conditions, the following subgraph was not optimized correctly:
// cond
//  |  \
//  |   Branch
//  |   /    \
//  | IfTrue  IfFalse
//  |   |
// TrapUnless

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var builder = new WasmModuleBuilder();

builder.addGlobal(kWasmI32, true, false, wasmI32Const(1));

builder.addFunction("main", kSig_i_i)
  .addBody([
    kExprLocalGet, 0,
    kExprIf, kWasmI32,
      kExprUnreachable,
    kExprElse,
      kExprLoop, kWasmVoid,
        kExprBlock, kWasmVoid,
          kExprLocalGet, 0,
          kExprIf, kWasmVoid,
            kExprLocalGet, 0,
            kExprBrIf, 1,
            kExprI32Const, 7,
            kExprLocalSet, 0,
          kExprEnd,
          kExprGlobalGet, 0,
          kExprIf, kWasmVoid,
            kExprI32Const, 0, kExprReturn,
          kExprEnd,
          kExprBr, 1,
        kExprEnd,
      kExprEnd,
      kExprI32Const, 0,
    kExprEnd,
    kExprLocalGet, 0,
    kExprI32DivU])
  .exportFunc();

let instance = builder.instantiate();
assertEquals(0, instance.exports.main(0));
