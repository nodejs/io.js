// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-wrapper-tiering-budget=1

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let sig = builder.addType(kSig_v_v);
let imp = builder.addImport('m', 'f', sig);
let table0 = builder.addTable(kWasmArrayRef)
let table1 = builder.addTable(kWasmFuncRef, 1, 1, [kExprRefFunc, 0,])
builder.addFunction('main', kSig_v_i).exportFunc().addBody([
  kExprLoop, kWasmVoid,
  kExprI32Const, 0,
  kExprCallIndirect, sig, table1.index,
  kExprLocalGet, 0,
  kExprI32Const, 1,
  kExprI32Sub,
  kExprLocalTee, 0,
  kExprBrIf, 0,
  kExprEnd,
]);
let instance = builder.instantiate({m: {f: x => x}});
instance.exports.main(1000);
