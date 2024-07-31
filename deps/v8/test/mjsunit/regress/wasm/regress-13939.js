// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

builder.addMemory(10, 10);

let struct = builder.addStruct([makeField(kWasmF32, true)]);

builder.addFunction("tester", kSig_f_v)
  .addLocals(wasmRefType(struct), 1)
  .addLocals(kWasmF32, 1)
  .addBody([
    kExprLoop, 0x40,
      kExprRefNull, struct,
      kExprRefAsNonNull,
      kExprLocalTee, 0x00,
      kGCPrefix, kExprStructGet, struct, 0x00,
      kExprLocalSet, 0x01,
      kExprLocalGet, 0x01,
      kExprI32Const, 0x00,
      kExprF32LoadMem, 0x00, 0x04,
      kExprI32Const, 0xff, 0x7e,
      kExprSelect,
      kExprI32SConvertF32,
      kExprBrIf, 0x00,
    kExprEnd,
    kExprF32Const, 0x00, 0x00, 0x00, 0x00])
  .exportFunc();

assertTraps(kTrapNullDereference, () => builder.instantiate().exports.tester());
