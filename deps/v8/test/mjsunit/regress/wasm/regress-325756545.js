// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.startRecGroup();
builder.addArray(kWasmI8, true, kNoSuperType, true);
builder.endRecGroup();
builder.startRecGroup();
builder.addArray(kWasmI16, true, kNoSuperType, true);
builder.endRecGroup();
builder.addStruct([], kNoSuperType, false);
builder.addStruct([], 2, false);
builder.addMemory(16, 32);
builder.addPassiveDataSegment([1, 2, 3]);
builder.addTable(kWasmFuncRef, 2, 2, undefined)
builder.addTag(makeSig([], []));
builder.addFunction('main', makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]))
  .addLocals(kWasmI64, 1)
  .addLocals(kWasmEqRef, 1)
  .addLocals(wasmRefNullType(5), 1)
  .addLocals(kWasmI32, 16)
  .addBody([
kExprLoop, 0x40,  // loop @92
  kExprI32Const, 0x01,  // i32.const
  kExprLocalSet, 0x07,  // local.set
  kExprLoop, 0x40,  // loop @98
    kExprI32Const, 0x02,  // i32.const
    kExprLocalSet, 0x08,  // local.set
    kExprLoop, 0x40,  // loop @104
      kExprI32Const, 0,
      kExprLocalSet, 0x01,  // local.set
      kExprLocalGet, 0x08,  // local.get
      kExprI32Const, 0x01,  // i32.const
      kExprI32Sub,  // i32.sub
      kExprLocalTee, 0x08,  // local.tee
      kExprIf, 0x40,  // if @183
        kExprRefNull, 0x01,  // ref.null
        kExprI32Const, 0xb0, 0xe6, 0xb5, 0xda, 0x01,  // i32.const
        kExprI32Const, 0x93, 0x97, 0xcd, 0x0a,  // i32.const
        kExprI32Const, 0xde, 0xe9, 0xab, 0x05,  // i32.const
        kGCPrefix, kExprArrayInitData, 0x01, 0x00,  // array.init_data
        kExprRefNull, 0x70,  // ref.null
        kExprBrOnNull, 0x03,  // br_on_null
        kExprDrop,  // drop
        kExprBr, 0x01,  // br depth=1
        kExprEnd,  // end @219
      kExprEnd,  // end @220
    kExprLocalGet, 0x07,  // local.get
    kExprI32Const, 0x01,  // i32.const
    kExprI32Sub,  // i32.sub
    kExprIf, 0x40,  // if @228
      kExprBr, 0x01,  // br depth=1
      kExprEnd,  // end @232
    kExprEnd,  // end @233
    kExprI32Const, 0,
  kExprDrop,
  kExprEnd,  // end @246
kExprLoop, 0x7f,  // loop @445 i32
  kExprLoop, 0x7f,  // loop @447 i32
    // TODO: Why is the i32AtomicOr needed?
    kExprI32Const, 0xc7, 0xe3, 0xbe, 0x05,  // i32.const
    kExprI32Const, 0x04,  // i32.const
    kAtomicPrefix, kExprI32AtomicOr, 0x02, 0xfb, 0xa5, 0x01,  // i32.atomic.rmw.or
    kExprI32Const, 0xc7, 0xa3, 0xc4, 0x07,  // i32.const
    kExprI32Xor,  // i32.xor
    kSimdPrefix, kExprS128Const, 0x46, 0x49, 0x42, 0x3c, 0xa9, 0xa0, 0x8c, 0x89, 0x37, 0x99, 0x5c, 0xde, 0xbb, 0xc9, 0x8a, 0x40,  // v128.const
    kSimdPrefix, kExprF32x4Ceil,  // f32x4.ceil
    kExprI32Const, 0x00,  // i32.const
    kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
    kSimdPrefix, kExprI8x16LeS,  // i8x16.le_s
    kSimdPrefix, kExprF32x4NearestInt,  // f32x4.nearest
    kSimdPrefix, kExprF32x4NearestInt,  // f32x4.nearest
    kSimdPrefix, kExprI8x16BitMask,  // i8x16.bitmask
    kAtomicPrefix, kExprI32AtomicXor16U, 0x01, 0x83, 0xb1, 0x01,  // i32.atomic.rmw16.xor_u
  kExprEnd,  // end @524
  kExprI32Const, 0x0b,  // i32.const
  kExprI32Const, 0xd1, 0xcb, 0xd7, 0xe7, 0x00,  // i32.const
  kAtomicPrefix, kExprI32AtomicSub, 0x02, 0x85, 0xd9, 0x02,  // i32.atomic.rmw.sub
  kExprI64Const, 1,
  kAtomicPrefix, kExprI64AtomicSub16U, 0x01, 0xe8, 0x8e, 0x03,  // i64.atomic.rmw16.sub_u
  kExprI32Const, 0xde, 0xd5, 0xb0, 0x02,  // i32.const
  kExprI64Const, 0x9f, 0x93, 0xe5, 0xf3, 0xea, 0x92, 0xd5, 0xa6, 0x1d,  // i64.const
  kAtomicPrefix, kExprI64AtomicExchange, 0x03, 0xed, 0xd7, 0x02,  // i64.atomic.rmw.xchg
  kExprI64ShrU,  // i64.shr_u
  kAtomicPrefix, kExprI64AtomicStore, 0x03, 0xaf, 0x2e,  // i64.atomic.store
  kExprI32Const, 0x0f,  // i32.const
  kExprBrIf, 0x00,  // br_if depth=0
  kExprI32Const, 0x00,  // i32.const
  kExprReturn,
kExprEnd,  // end @1016
]).exportFunc();
const instance = builder.instantiate();
assertTraps(kTrapNullDereference, () => instance.exports.main(1, 2, 3));
