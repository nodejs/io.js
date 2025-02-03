// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(1, 1);
let main = builder.addFunction("main", makeSig([], [kWasmI64]))
  .addBody([
    kExprI32Const, 0,
    kAtomicPrefix, kExprI64AtomicLoad32U, 2, 0,
    kExprI64Const, 10,
    kExprI64Add,
  ]).exportFunc();

const instance = builder.instantiate({});
assertEquals(10n, instance.exports.main());
