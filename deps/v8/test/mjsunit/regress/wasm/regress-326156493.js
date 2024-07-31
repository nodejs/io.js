// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --no-liftoff --future

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let generateUselessCode = (n) => new Array(n).fill([kExprMemoryGrow, 0]).flat();

const builder = new WasmModuleBuilder();
builder.startRecGroup();
builder.addArray(kWasmI8, true, kNoSuperType, true);
builder.endRecGroup();
builder.startRecGroup();
builder.addArray(kWasmI16, true, kNoSuperType, true);
builder.endRecGroup();
builder.addStruct([], kNoSuperType, false);
let mainSig = builder.addType(
    makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
let decodeSig = builder.addType(makeSig(
    [wasmRefNullType(0), kWasmI32, kWasmI32], [wasmRefType(kWasmExternRef)]));
let decode = builder.addImport(
    'wasm:text-decoder', 'decodeStringFromUTF8Array', decodeSig);
builder.addMemory(16, 32);
builder.addPassiveDataSegment([]);
builder.addTable(kWasmFuncRef, 1, 1, undefined)
builder.addFunction('main', mainSig)
  .addLocals(kWasmF64, 2).addLocals(kWasmF32, 1)
  .addLocals(kWasmF64, 2).addLocals(kWasmI32, 1)
  .addBody([
kExprI32Const, 0x06,
kExprLocalSet, 0x08,
kExprLoop, 0x40,
  kExprI32Const, 0x07,
  kExprLocalSet, 0x02,
  kExprLoop, 0x40,
    kExprLocalGet, 0x02,
    kExprI32Const, 0x01,
    kExprI32Sub,
    kExprLocalTee, 0x02,
    kExprIf, 0x40,
      kExprI32Const, 0x00,
      kExprI32Const, 0x01,
      kGCPrefix, kExprArrayNew, 0x01,
      kGCPrefix, kExprRefTestNull, 0x02,
      kExprI32Const, 0x00,
      kSimdPrefix, kExprI8x16Splat,
      kExprI32Const, 0xf1, 0x9f, 0x7e,
      kExprLocalTee, 0x01,
      kExprF64SConvertI32,
      kExprI32Const, 1,
      kExprI32Const, 0x91, 0x03,
      kExprI32Const, 0x00,
      kGCPrefix, kExprArrayNew, 0x00,
      kExprDrop,
      kExprI32Const, 0x84, 0xfc, 0x00,
      kExprLocalGet, 0x08,
      kExprI32Eqz,
      kExprBrIf, 0x02,
      kExprBr, 0x01,
      kExprEnd,
    kExprEnd,
  kExprLocalGet, 0x08,
  kExprI32Const, 0x01,
  kExprI32Sub,
  kExprLocalTee, 0x08,
  kExprIf, 0x40,
    kExprBr, 0x01,
    kExprEnd,
  kExprEnd,

kExprLoop, 0x7f,
  kExprRefNull, 0x00,
  kGCPrefix, kExprRefCastNull, 0x00,
  kExprI32Const, 0x01,
  // We need to make sure that the loop is large to prevent loop unrolling.
  ...generateUselessCode(20),
  kExprI32Const, 0xbc, 0xe9, 0xe9, 0x01,
  kExprCallFunction, decode,  // call function #16: r_nii
  kExprDrop,
  kExprI32Const, 0x01,
  kExprI32Const, 0x3c,
  kExprI32Const, 0x01,
  kExprI32RemU,
  kGCPrefix, kExprArrayNew, 0x01,
  kExprI32Const, 0x01,
  kExprBrIf, 0x00,
  kExprI32Const, 0xe2, 0xec, 0x8c, 0xb2, 0x02,
  kExprReturn,
  kExprEnd,
kExprDrop,
kExprI32Const, 1,
]).exportFunc();
const instance = builder.instantiate({}, {builtins: ['text-decoder']});
assertTraps(kTrapNullDereference, () => instance.exports.main(1, 2, 3));
