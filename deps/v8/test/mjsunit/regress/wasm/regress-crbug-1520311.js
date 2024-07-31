// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

builder.addMemory(1, 1);
builder.exportMemoryAs("memory");

builder.addFunction("store", kSig_v_i).exportFunc().addBody([
  kExprLocalGet, 0,
  kExprI32Const, 3,
  kExprI32Shl,
  kExprI64Const, 42,
  kAtomicPrefix, kExprI64AtomicStore, 3, 0,  // alignment, offset
]);

builder.addFunction("load", kSig_l_i).exportFunc().addBody([
  kExprLocalGet, 0,
  kExprI32Const, 3,
  kExprI32Shl,
  kAtomicPrefix, kExprI64AtomicLoad, 3, 0,  // alignment, offset
]);

let instance = builder.instantiate();
const kStoreIndex = 1;
instance.exports.store(kStoreIndex);

let i64 = new DataView(instance.exports.memory.buffer);

assertEquals(0n, i64.getBigInt64(0, true));
assertEquals(42n, i64.getBigInt64(kStoreIndex * 8, true));

const kLoadIndex = 10;
const kLoadValue = 1234n;
i64.setBigInt64(kLoadIndex * 8, kLoadValue, true);
let load = instance.exports.load;
assertEquals(0n, load(kLoadIndex * 8));
assertEquals(kLoadValue, load(kLoadIndex));
