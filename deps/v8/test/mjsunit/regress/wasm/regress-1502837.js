// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let tag = builder.addTag(makeSig([], []));
builder.addFunction(undefined, kSig_v_v /* sig */)
  .addBodyWithEnd([
    kExprBlock, kWasmVoid,
      kExprBlock, kWasmVoid,
        kExprTryTable, kWasmVoid, 2, kCatchAllNoRef, 2, kCatchNoRef, tag, 1,
        kExprThrow, tag,
        kExprEnd,
      kExprBr, 0x02,
      kExprEnd,
    kExprBr, 0x01,
    kExprEnd,
  kExprBr, 0x00,
kExprEnd,
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
instance.exports.main(1, 2, 3);
