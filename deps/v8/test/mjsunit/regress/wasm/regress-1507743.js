// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([], []));
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addArray(kWasmF32, true);
builder.addMemory(16, 32);
builder.addPassiveDataSegment([148, 214, 121, 119]);
builder.addFunction(undefined, 1 /* sig */)
  .addBodyWithEnd([
kExprLoop, 0x7d,  // loop @29 f32
  kExprLoop, 0x7f,  // loop @31 i32
    kExprI32Const, 0,
    kExprLocalGet, 0, kExprI32Const, 1, kExprI32Sub, kExprLocalSet, 0,
    kExprLocalGet, 0,
    kExprBrIf, 0x00,
    kExprBlock, 0x40,
      kExprTryTable, 0x40, 0x00,  // try_table entries=0
        kExprEnd,
      kExprBr, 0x00,
      kExprEnd,
    kExprRefNull, 0x00,
    kExprCallRef, 0x00,
    kExprEnd,
  kExprI32Const, 0,
  kExprI32Const, 0,
  kAtomicPrefix, kExprI32AtomicSub8U, 0x00, 0xd0, 0xa0, 0x01,
  kGCPrefix, kExprArrayNewData, 0x02, 0x00,
  kExprI32Const, 53,
  kGCPrefix, kExprArrayGet, 0x02,
  kExprEnd,
kExprDrop,
kExprI32Const, 0x87, 0xfd, 0xab, 0xe1, 0x7a,
kExprEnd,
]);

builder.addExport('main', 0);
const instance = builder.instantiate();
try {
  print(instance.exports.main(1, 2, 3));
} catch (e) {
  print('caught exception', e);
}
