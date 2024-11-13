// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let builder = new WasmModuleBuilder();

let struct0 = builder.addStruct([makeField(kWasmI32, true)]);
let return_struct0 = builder.addType(makeSig([], [wasmRefType(struct0)]));

builder.addFunction("makeStruct", return_struct0)
  .exportFunc()
  .addBody([
    kExprI32Const, 42,
    kGCPrefix, kExprStructNew, struct0,
  ]);

let callee = builder.addFunction("callee", makeSig([wasmRefType(struct0)], [kWasmI32]))
  .addBody([
    kExprLocalGet, 0,
    kGCPrefix, kExprStructGet, struct0, 0,
  ]);

builder.addFunction("main", makeSig([wasmRefNullType(struct0)], [kWasmI32]))
  .exportFunc()
  .addBody([
    kExprBlock, return_struct0,
      kExprLocalGet, 0,
      kExprBrOnNonNull, 0,
      kExprUnreachable,
    kExprEnd,
    kExprCallFunction, callee.index,
  ]);

let instance = builder.instantiate();
let obj = instance.exports.makeStruct();

instance.exports.main(obj);
%WasmTierUpFunction(instance.exports.main);
assertEquals(42, instance.exports.main(obj));
