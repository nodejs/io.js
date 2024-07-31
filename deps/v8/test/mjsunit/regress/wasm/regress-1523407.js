// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --turboshaft-wasm --no-turboshaft-wasm-load-elimination

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.startRecGroup();
builder.addStruct([]);
builder.endRecGroup();
builder.startRecGroup();
builder.addArray(kWasmI32, true);
builder.endRecGroup();
builder.startRecGroup();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.endRecGroup();
builder.addType(makeSig([], []));
builder.addMemory(16, 32);
builder.addTable(kWasmFuncRef, 1, 1, undefined)
builder.addActiveElementSegment(0, wasmI32Const(0), [[kExprRefFunc, 0, ]], kWasmFuncRef);
builder.addTag(makeSig([], []));
// Generate function 1 (out of 1).
builder.addFunction(undefined, 2 /* sig */)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprTry, 0x7f,  // try @1 i32
  kExprI32Const, 0x60,  // i32.const
  kExprI32LoadMem, 0x02, 0xbb, 0xf7, 0x02,  // i32.load
  kExprIf, 0x7f,  // if @10 i32
    kExprI32Const, 0xb5, 0xa7, 0x96, 0xee, 0x78,  // i32.const
    kExprI32Const, 0x94, 0xce, 0xfa, 0x90, 0x7d,  // i32.const
    kAtomicPrefix, kExprI32AtomicXor8U, 0x00, 0x8a, 0x01,  // i32.atomic.rmw8.xor_u
    kExprMemoryGrow, 0x00,  // memory.grow
  kExprElse,  // else @31
    kExprI32Const, 0xb9, 0xac, 0x85, 0x2b,  // i32.const
    kExprEnd,  // end @37
  kExprIf, 0x7f,  // if @38 i32
    kExprI32Const, 0x92, 0xc9, 0xb7, 0xda, 0x7e,  // i32.const
  kExprElse,  // else @46
    kExprI32Const, 0xd3, 0xbc, 0xdb, 0x87, 0x79,  // i32.const
    kExprEnd,  // end @53
  kExprIf, 0x7f,  // if @54 i32
    kExprI32Const, 0xce, 0x9e, 0xd0, 0xcd, 0x04,  // i32.const
  kExprElse,  // else @62
    kExprI32Const, 0xc0, 0xdd, 0xb4, 0x2f,  // i32.const
    kExprEnd,  // end @68
  kExprIf, 0x7f,  // if @69 i32
    kExprI32Const, 0xef, 0x8f, 0xb7, 0xc6, 0x7b,  // i32.const
  kExprElse,  // else @77
    kExprI32Const, 0xf9, 0xa8, 0xe8, 0xc5, 0x06,  // i32.const
    kExprEnd,  // end @84
  kExprIf, 0x7f,  // if @85 i32
    kExprI32Const, 0x91, 0xd2, 0xa2, 0xa0, 0x7e,  // i32.const
  kExprElse,  // else @93
    kExprI32Const, 0xd0, 0x84, 0xd8, 0x9b, 0x79,  // i32.const
    kExprEnd,  // end @100
  kExprI32Const, 0x9d, 0x95, 0xd5, 0x9f, 0x02,  // i32.const
  kExprI32Const, 0x14,  // i32.const
  kExprI32RemS,  // i32.rem_s
  kGCPrefix, kExprArrayNew, 0x01,  // array.new
  kGCPrefix, kExprArrayLen,  // array.len
kExprCatch, 0x00,  // catch @115
  kExprI32Const, 0xb7, 0xaf, 0x8c, 0xc5, 0x79,  // i32.const
  kExprEnd,  // end @123
kExprEnd,  // end @124
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
try {
  print(instance.exports.main(1, 2, 3));
} catch (e) {
  print('caught exception', e);
}
