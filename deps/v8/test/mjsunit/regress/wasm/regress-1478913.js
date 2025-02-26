// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addTag(makeSig([], []));
// Generate function 1 (out of 1).
builder.addFunction(undefined, 0 /* sig */)
  .addBody([
// signature: i_iii
// body:
    kExprTry, 0x7f,
      kExprI32Const, 0x01,
    kExprCatch, 0x00,  // catch @44
      kExprLoop, 0x7f,  // loop @46 i32
        kExprI32Const, 0x8b, 0xc9, 0x8d, 0xf3, 0x05,  // i32.const
      kExprEnd,  // end @54
    kExprEnd,
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
print(instance.exports.main(1, 2, 3));
