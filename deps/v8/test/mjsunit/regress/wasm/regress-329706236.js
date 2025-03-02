// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory64(0, 10);
builder.addFunction('main', makeSig([], [kWasmI64])).addBody([
  kExprI64Const, 1,
  kExprI64Const, 36,
  kAtomicPrefix, kExprI64AtomicExchange, 3, 66,
]).exportFunc();

const instance = builder.instantiate();
assertTraps(kTrapUnalignedAccess, () => instance.exports.main(1, 2, 3));
