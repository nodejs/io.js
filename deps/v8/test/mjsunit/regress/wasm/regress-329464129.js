// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --no-liftoff --turboshaft-wasm

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addType(makeSig([kWasmExternRef, kWasmI32, kWasmI32],
                        [wasmRefType(kWasmExternRef)]));
builder.addImport('wasm:js-string', 'substring', 1 /* sig */);
builder.addMemory(16, 32);
builder.addTag(makeSig([kWasmI32], []));
// Generate function 18 (out of 18).
builder.addFunction(undefined, 0 /* sig */)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprTry, 0x7f,  // try @44 i32
  kExprRefNull, 0x6f,  // ref.null
  kExprI32Const, 0xb0, 0x88, 0xc9, 0x0f,  // i32.const
  kExprI32Const, 0x95, 0x92, 0xc9, 0x09,  // i32.const
  kExprCallFunction, 0x00,  // call function #8: r_nii
  kExprDrop,
  kExprI32Const, 0,
kExprCatch, 0x00,  // catch @344
  kExprI32Const, 0xcb, 0x06,  // i32.const
  kExprF64Const, 0x5e, 0xea, 0xf9, 0xa1, 0xf5, 0x72, 0xad, 0x68,  // f64.const
  kExprF64StoreMem, 0x00, 0x74,  // f64.store
  kExprEnd,  // end @368
kExprEnd,  // end @453
]);
builder.addExport('main', 1);
let kBuiltins = { builtins: ['js-string'] };
const instance = builder.instantiate({}, kBuiltins);
assertTraps(kTrapIllegalCast, () => instance.exports.main(1, 2, 3));
