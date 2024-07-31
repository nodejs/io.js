// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addStruct([]);
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addType(makeSig([kWasmFuncRef, kWasmFuncRef, kWasmExternRef],
                        [wasmRefType(0)]));
builder.addType(
  makeSig([kWasmI64, kWasmF32, kWasmS128, kWasmI32],
          [wasmRefType(1), wasmRefNullType(2), kWasmI64, wasmRefNullType(2),
           kWasmI64]));
builder.addType(
  makeSig([],
          [wasmRefNullType(2), wasmRefNullType(2), kWasmF64, wasmRefNullType(2),
           kWasmI32, wasmRefNullType(2), kWasmI32, kWasmI32, wasmRefNullType(2),
           kWasmI32, kWasmI32, kWasmI64, kWasmI32, kWasmS128,
           wasmRefNullType(2)]));
builder.addType(makeSig([], []));
builder.addType(
  makeSig([wasmRefType(kWasmFuncRef)],
          [kWasmI32, kWasmI32, wasmRefType(1), wasmRefType(kWasmAnyRef),
           kWasmI32, wasmRefType(1), kWasmI64, wasmRefNullType(4), kWasmI32,
           wasmRefType(kWasmAnyRef), wasmRefNullType(4), kWasmI64, kWasmI64,
           wasmRefType(kWasmEqRef), kWasmI32]));
builder.addType(
  makeSig([wasmRefType(kWasmEqRef), kWasmAnyRef, kWasmI32, kWasmI32],
          [wasmRefType(1), kWasmI64, wasmRefNullType(4), kWasmI32,
           wasmRefType(kWasmAnyRef), wasmRefNullType(4), kWasmI64, kWasmI64,
           wasmRefType(kWasmEqRef), kWasmI32]));
builder.addType(
  makeSig([kWasmI32, kWasmI32, wasmRefType(1), wasmRefType(kWasmAnyRef),
           kWasmI32, wasmRefType(1), kWasmI64, wasmRefNullType(4), kWasmI32,
           wasmRefType(kWasmAnyRef), wasmRefNullType(4), kWasmI64, kWasmI64,
           wasmRefType(kWasmEqRef), kWasmI32],
          [kWasmI32]));
builder.addMemory(16, 32);
builder.addTable(kWasmFuncRef, 4, 5, undefined)
builder.addTable(kWasmFuncRef, 15, 25, undefined)
builder.addTable(kWasmFuncRef, 1, 1, undefined)
builder.addTable(kWasmFuncRef, 16, 17, undefined)
builder.addActiveElementSegment(
  0, wasmI32Const(0),
  [[kExprRefFunc, 0], [kExprRefFunc, 1], [kExprRefFunc, 2], [kExprRefFunc, 3]],
  kWasmFuncRef);
builder.addActiveElementSegment(
  1, wasmI32Const(0),
  [[kExprRefFunc, 0], [kExprRefFunc, 1], [kExprRefFunc, 2], [kExprRefFunc, 3],
   [kExprRefFunc, 0], [kExprRefFunc, 1], [kExprRefFunc, 2], [kExprRefFunc, 3],
   [kExprRefFunc, 0], [kExprRefFunc, 1], [kExprRefFunc, 2], [kExprRefFunc, 3],
   [kExprRefFunc, 0], [kExprRefFunc, 1], [kExprRefFunc, 2]],
  kWasmFuncRef);
builder.addActiveElementSegment(
  2, wasmI32Const(0), [[kExprRefFunc, 0]], kWasmFuncRef);
builder.addActiveElementSegment(
  3, wasmI32Const(0),
  [[kExprRefFunc, 0], [kExprRefFunc, 1], [kExprRefFunc, 2], [kExprRefFunc, 3],
   [kExprRefFunc, 0], [kExprRefFunc, 1], [kExprRefFunc, 2], [kExprRefFunc, 3],
   [kExprRefFunc, 0], [kExprRefFunc, 1], [kExprRefFunc, 2], [kExprRefFunc, 3],
   [kExprRefFunc, 0], [kExprRefFunc, 1], [kExprRefFunc, 2], [kExprRefFunc, 3]],
  kWasmFuncRef);
builder.addTag(makeSig([], []));
// Generate function 1 (out of 4).
builder.addFunction(undefined, 1 /* sig */)
  .addLocals(kWasmI64, 1).addLocals(wasmRefNullType(4), 1)
  .addLocals(kWasmI32, 2).addLocals(kWasmI64, 1)
  .addLocals(wasmRefNullType(4), 1).addLocals(kWasmI32, 1)
  .addLocals(kWasmI64, 3).addLocals(kWasmI32, 1).addLocals(kWasmI64, 1)
  .addLocals(kWasmI32, 1).addLocals(kWasmI64, 1)
  .addLocals(wasmRefNullType(4), 1).addLocals(kWasmI64, 1)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprRefFunc, 0x01,  // ref.func
kExprBlock, 0x06,  // block @32 i32 i32 (ref 1) (ref any) i32 (ref 1) i64 (ref null 4) i32 (ref any) (ref null 4) i64 i64 (ref eq) i32
  kExprDrop,  // drop
  kExprI32Const, 0xf1, 0x00,  // i32.const
  kExprI64Const, 0x00,  // i64.const
  kExprI64Const, 0xe1, 0x00,  // i64.const
  kExprI64Const, 0x00,  // i64.const
  kExprI64Const, 0xef, 0x00,  // i64.const
  kExprI32Const, 0x00,  // i32.const
  kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
  kExprI32Const, 0xf0, 0x02,  // i32.const
  kSimdPrefix, kExprI64x2ShrU, 0x01,  // i64x2.shr_u
  kSimdPrefix, kExprI32x4BitMask, 0x01,  // i32x4.bitmask
  kExprI32Const, 0x00,  // i32.const
  kExprRefFunc, 0x00,  // ref.func
  kGCPrefix, kExprStructNew, 0x00,  // struct.new
  kExprI32Const, 0x00,  // i32.const
  kExprRefFunc, 0x00,  // ref.func
  kExprI64Const, 0x00,  // i64.const
  kExprRefNull, 0x04,  // ref.null
  kExprI32Const, 0x00,  // i32.const
  kGCPrefix, kExprStructNew, 0x00,  // struct.new
  kExprRefNull, 0x04,  // ref.null
  kExprI64Const, 0x00,  // i64.const
  kExprI64Const, 0x00,  // i64.const
  kGCPrefix, kExprStructNew, 0x00,  // struct.new
  kExprI32Const, 0x00,  // i32.const
  kExprRefNull, 0x6e,  // ref.null
  kExprBrOnNull, 0x00,  // br_on_null
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprI64ShrU,  // i64.shr_u
  kExprI64Ror,  // i64.ror
  kExprI64ShrS,  // i64.shr_s
  kExprI64Const, 0x01,  // i64.const
  kSimdPrefix, kExprS128Const, 0xff, 0x01, 0x0d, 0x00, 0x70, 0x70, 0x71, 0x3a, 0x00, 0x00, 0x00, 0x73, 0x01, 0x6f, 0x70, 0x71,  // s128.const
  kSimdPrefix, kExprI64x2ExtractLane, 0x01,  // i64x2.extract_lane
  kExprI64ShrS,  // i64.shr_s
  kExprI64Ror,  // i64.ror
  kAtomicPrefix, kExprI64AtomicStore16U, 0x01, 0xef, 0xc2, 0xbd, 0x8b, 0x06,  // i64.atomic.store16_u
  kSimdPrefix, kExprS128Const, 0x71, 0x6f, 0x61, 0x61, 0x6f, 0x70, 0x00, 0x01, 0x70, 0x00, 0x71, 0x70, 0x3a, 0x70, 0x00, 0x00,  // s128.const
  kSimdPrefix, kExprI32x4BitMask, 0x01,  // i32x4.bitmask
  kExprRefNull, 0x03,  // ref.null
  kExprRefNull, 0x70,  // ref.null
  kExprRefNull, 0x6f,  // ref.null
  kExprI32Const, 0x01,  // i32.const
  kExprCallIndirect, 0x02, 0x00,  // call_indirect sig #2: r_nnn
  kExprDrop,  // drop
  kExprI32Const, 0x00,  // i32.const
  kExprI32Const, 0x00,  // i32.const
  kExprI32Const, 0x00,  // i32.const
  kExprCallIndirect, 0x01, 0x00,  // call_indirect sig #1: i_iii
  kExprNop,  // nop
  kExprI64Const, 0xe1, 0x00,  // i64.const
  kExprI32Const, 0x00,  // i32.const
  kAtomicPrefix, kExprI64AtomicLoad, 0x03, 0xe0, 0x8c, 0xbc, 0x03,  // i64.atomic.load64
  kExprI64ShrU,  // i64.shr_u
  kAtomicPrefix, kExprI64AtomicStore8U, 0x00, 0x80, 0x82, 0x7c,  // i64.atomic.store8_u
  kExprBlock, 0x40,  // block @219
    kExprEnd,  // end @221
  kExprBlock, 0x7f,  // block @222 i32
    kExprI32Const, 0x00,  // i32.const
    kExprEnd,  // end @226
  kExprI32Const, 0xe3, 0x00,  // i32.const
  kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
  kExprI32Const, 0xe3, 0x00,  // i32.const
  kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
  kSimdPrefix, kExprI32x4BitMask, 0x01,  // i32x4.bitmask
  kSimdPrefix, kExprI64x2ShrS, 0x01,  // i64x2.shr_s
  kSimdPrefix, kExprI32x4BitMask, 0x01,  // i32x4.bitmask
  kExprRefFunc, 0x00,  // ref.func
  kGCPrefix, kExprStructNew, 0x00,  // struct.new
  kExprI32Const, 0x00,  // i32.const
  kGCPrefix, kExprStructNew, 0x00,  // struct.new
  kExprRefNull, 0x6e,  // ref.null
  kExprI32Const, 0x00,  // i32.const
  kExprI32Const, 0x00,  // i32.const
  kExprBlock, 0x07,  // block @268 (ref 1) i64 (ref null 4) i32 (ref any) (ref null 4) i64 i64 (ref eq) i32
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprDrop,  // drop
    kExprRefFunc, 0x00,  // ref.func
    kExprI64Const, 0x00,  // i64.const
    kExprRefNull, 0x04,  // ref.null
    kExprI32Const, 0x00,  // i32.const
    kGCPrefix, kExprStructNew, 0x00,  // struct.new
    kExprRefNull, 0x04,  // ref.null
    kExprI64Const, 0x00,  // i64.const
    kExprI64Const, 0x00,  // i64.const
    kGCPrefix, kExprStructNew, 0x00,  // struct.new
    kExprI32Const, 0x00,  // i32.const
    kExprEnd,  // end @302
  kExprEnd,  // end @303
kExprBlock, 0x08,  // block @304 i32
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprNop,  // nop
  kExprEnd,  // end @321
kExprEnd,  // end @322
]);
// Generate function 2 (out of 4).
builder.addFunction(undefined, 2 /* sig */)
  .addBodyWithEnd([
// signature: r_nnn
// body:
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprEnd,  // end @7
]);
// Generate function 3 (out of 4).
builder.addFunction(undefined, 3 /* sig */)
  .addBodyWithEnd([
// signature: rnlnl_lfsi
// body:
kExprRefFunc, 0x00,  // ref.func
kExprRefNull, 0x02,  // ref.null
kExprI64Const, 0x00,  // i64.const
kExprRefNull, 0x02,  // ref.null
kExprI64Const, 0x00,  // i64.const
kExprEnd,  // end @11
]);
// Generate function 4 (out of 4).
builder.addFunction(undefined, 4 /* sig */)
  .addBodyWithEnd([
// signature: nndniniiniilisn_v
// body:
kExprRefNull, 0x02,  // ref.null
kExprRefNull, 0x02,  // ref.null
kExprF64Const, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // f64.const
kExprRefNull, 0x02,  // ref.null
kExprI32Const, 0x00,  // i32.const
kExprRefNull, 0x02,  // ref.null
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x00,  // i32.const
kExprRefNull, 0x02,  // ref.null
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x00,  // i32.const
kExprI64Const, 0x00,  // i64.const
kExprI32Const, 0x00,  // i32.const
kExprI32Const, 0x00,  // i32.const
kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
kExprRefNull, 0x02,  // ref.null
kExprEnd,  // end @40
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertEquals(0, instance.exports.main(1, 2, 3));
