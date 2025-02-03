// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $mem0 = builder.addMemory64(0, 32);

builder.addFunction('grow', kSig_v_v).exportFunc().addBody([
  kExprI64Const, 1,
  kExprMemoryGrow, $mem0,
  kExprDrop,
]);

builder.addFunction('main', makeSig([kWasmI64], [kWasmI32])).exportFunc()
  .addBody([
    kExprLocalGet, 0,
    kExprI64Const, 1,
    kAtomicPrefix, kExprI64AtomicAnd16U, 1, 1,
    kExprI32ConvertI64,
  ]);

const instance = builder.instantiate({});
instance.exports.grow();
assertEquals(0, instance.exports.main(1n));
assertTraps(kTrapUnalignedAccess, () => instance.exports.main(2n));
assertTraps(kTrapMemOutOfBounds, () => instance.exports.main(65535n));
