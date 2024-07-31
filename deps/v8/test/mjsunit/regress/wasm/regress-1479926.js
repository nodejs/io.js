// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addStruct([makeField(kWasmI32, false), makeField(kWasmI32, false), makeField(wasmRefNullType(kWasmNullFuncRef), false)]);
builder.addArray(kWasmI32, true);
builder.addArray(kWasmI16, true);
builder.startRecGroup();
builder.addArray(kWasmI32, true);
builder.addArray(kWasmS128, true);
builder.endRecGroup();
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.addType(makeSig([], []));
builder.addType(makeSig([wasmRefType(4), kWasmS128, kWasmF64, kWasmEqRef, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32], [wasmRefNullType(3), kWasmI32, kWasmI32, kWasmI32]));
builder.addMemory(16, 32);
builder.addPassiveDataSegment([142, 208, 34, 139, 193, 226, 150, 137, 134, 207, 191, 41, 193, 120, 242, 233, 81, 225, 232, 110, 187, 70, 107, 51, 185, 74, 134, 121, 248, 65]);
builder.addTable(kWasmFuncRef, 1, 1, undefined)
builder.addActiveElementSegment(0, wasmI32Const(0), [[kExprRefFunc, 0, ]], kWasmFuncRef);
builder.addTag(makeSig([], []));
// Generate function 1 (out of 1).
builder.addFunction(undefined, 5 /* sig */)
  .addLocals(kWasmI32, 1).addLocals(wasmRefType(0), 18)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprTry, 0x7d,  // try @6 f32
  kExprI64Const, 0x70,  // i64.const
  kExprF32SConvertI64,  // f32.convert_i64_s
kExprCatchAll,  // catch_all @11
  kExprTry, 0x7d,  // try @12 f32
    kExprTry, 0x7d,  // try @14 f32
      kNumericPrefix, kExprTableSize, 0x00,  // table.size
      kExprF32SConvertI32,  // f32.convert_i32_s
      kExprF32NearestInt,  // f32.nearest
    kExprCatch, 0x00,  // catch @21
      kExprF32Const, 0xfe, 0x6b, 0x19, 0x77,  // f32.const
      kExprEnd,  // end @28
    kExprI32Const, 0x00,  // i32.const
    kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
    kExprI32Const, 0xd6, 0xc0, 0xe2, 0xc5, 0x00,  // i32.const
    kExprI32Const, 0x14,  // i32.const
    kExprI32RemS,  // i32.rem_s
    kGCPrefix, kExprArrayNew, 0x04,  // array.new
    kExprI32Const, 0x00,  // i32.const
    kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
    kExprF64Const, 0x5f, 0xf9, 0xcb, 0x10, 0x8e, 0xaa, 0xe8, 0x3f,  // f64.const
    kExprRefNull, 0x6d,  // ref.null
    kExprI32Const, 0x82, 0xc7, 0x85, 0xf9, 0x79,  // i32.const
    kExprI32Const, 0xed, 0xf6, 0xc5, 0x80, 0x7d,  // i32.const
    kExprI32Const, 0xf7, 0xd8, 0xa1, 0xd6, 0x7d,  // i32.const
    kExprI32Const, 0xf7, 0xf0, 0x94, 0xe0, 0x04,  // i32.const
    kExprI32Const, 0xc9, 0xb0, 0xe1, 0x99, 0x03,  // i32.const
    kExprI32Const, 0xb9, 0x9e, 0xa2, 0x9c, 0x7c,  // i32.const
    kExprI32Const, 0xaa, 0x8b, 0xb8, 0x8c, 0x7b,  // i32.const
    kExprI32Const, 0xcd, 0xe2, 0xe7, 0xb7, 0x01,  // i32.const
    kExprI32Const, 0xea, 0xa9, 0xaa, 0xea, 0x01,  // i32.const
    kExprI32Const, 0xc1, 0x8b, 0xa6, 0xcb, 0x78,  // i32.const
    kExprI32Const, 0xae, 0xbb, 0xab, 0xff, 0x07,  // i32.const
    kExprBlock, 0x07,  // block @126 (ref null 3) i32 i32 i32
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
      kExprRefNull, 0x03,  // ref.null
      kExprI32Const, 0xd5, 0x99, 0x8a, 0xd7, 0x06,  // i32.const
      kExprI32Const, 0xfa, 0xce, 0x8e, 0xdd, 0x79,  // i32.const
      kExprI32Const, 0xd3, 0x81, 0xa6, 0xf1, 0x7c,  // i32.const
      kExprEnd,  // end @163
    kGCPrefix, kExprArrayInitData, 0x03, 0x00,  // array.init_data
    kExprBlock, 0x40,  // block @168
      kExprI32Const, 0xbe, 0xc8, 0xc4, 0xcf, 0x06,  // i32.const
      kExprDrop,  // drop
      kExprEnd,  // end @177
  kExprCatchAll,  // catch_all @178
    kExprF32Const, 0xf0, 0xb6, 0xa3, 0x04,  // f32.const
    kExprEnd,  // end @184
  kExprEnd,  // end @185
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprF32Ceil,  // f32.ceil
kExprI32SConvertF32,  // i32.trunc_f32_s
kExprI32Const, 0x95, 0x80, 0xb2, 0xc3, 0x7b,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x04,  // local.set
kExprI32Const, 0x84, 0xe8, 0x86, 0xfc, 0x78,  // i32.const
kExprI32Const, 0xab, 0xa8, 0xf9, 0x8a, 0x7c,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x05,  // local.set
kExprI32Const, 0xd6, 0xce, 0xfb, 0xa6, 0x7c,  // i32.const
kExprI32Const, 0xc5, 0xc8, 0xa7, 0x92, 0x07,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x06,  // local.set
kExprI32Const, 0xec, 0xae, 0xd6, 0xe0, 0x07,  // i32.const
kExprI32Const, 0xd7, 0xee, 0xb2, 0x90, 0x78,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x07,  // local.set
kExprI32Const, 0xf7, 0xcf, 0xdb, 0xd9, 0x7c,  // i32.const
kExprI32Const, 0x92, 0xe8, 0x82, 0x26,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x08,  // local.set
kExprI32Const, 0xcb, 0xac, 0xc3, 0xcc, 0x01,  // i32.const
kExprI32Const, 0xdf, 0xd1, 0xdf, 0xce, 0x02,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x09,  // local.set
kExprI32Const, 0xe4, 0xd7, 0x8b, 0xe6, 0x04,  // i32.const
kExprI32Const, 0xdd, 0x95, 0xda, 0xe9, 0x04,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x0a,  // local.set
kExprI32Const, 0x8d, 0x98, 0xc9, 0xe0, 0x05,  // i32.const
kExprI32Const, 0xfc, 0x8b, 0xd9, 0x97, 0x07,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x0b,  // local.set
kExprI32Const, 0xdb, 0xd4, 0xc5, 0xe2, 0x04,  // i32.const
kExprI32Const, 0xd8, 0xa9, 0xb7, 0xe0, 0x06,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x0c,  // local.set
kExprI32Const, 0xb9, 0x80, 0xc4, 0xa1, 0x03,  // i32.const
kExprI32Const, 0xf8, 0x84, 0xc4, 0xbe, 0x7c,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x0d,  // local.set
kExprI32Const, 0xfe, 0xd7, 0xe3, 0x8e, 0x02,  // i32.const
kExprI32Const, 0xff, 0xcf, 0xcb, 0xde, 0x7c,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x0e,  // local.set
kExprI32Const, 0x8f, 0xbd, 0xd4, 0xa0, 0x79,  // i32.const
kExprI32Const, 0xdc, 0xc0, 0x9d, 0xfd, 0x06,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x0f,  // local.set
kExprI32Const, 0x91, 0xd6, 0x94, 0xfb, 0x04,  // i32.const
kExprI32Const, 0xc6, 0xf9, 0xbf, 0x92, 0x7a,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x10,  // local.set
kExprI32Const, 0x86, 0xb2, 0xd1, 0x95, 0x78,  // i32.const
kExprI32Const, 0xfc, 0xb6, 0xa1, 0xe1, 0x7d,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x11,  // local.set
kExprI32Const, 0x9e, 0xf5, 0x9b, 0xe4, 0x01,  // i32.const
kExprI32Const, 0x82, 0xcd, 0xd0, 0x92, 0x7e,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x12,  // local.set
kExprI32Const, 0xeb, 0x82, 0xc1, 0xaa, 0x06,  // i32.const
kExprI32Const, 0xe6, 0xa8, 0x90, 0x67,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x13,  // local.set
kExprI32Const, 0xbd, 0xea, 0xac, 0xd7, 0x7b,  // i32.const
kExprI32Const, 0xdb, 0xe7, 0x96, 0x87, 0x78,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x14,  // local.set
kExprI32Const, 0xca, 0xea, 0x9f, 0xb2, 0x7f,  // i32.const
kExprI32Const, 0xaa, 0xff, 0x9e, 0xfa, 0x7d,  // i32.const
kExprRefNull, kNullFuncRefCode,  // ref.null
kGCPrefix, kExprStructNew, 0x00,  // struct.new
kExprLocalSet, 0x15,  // local.set
kExprI32Const, 0xb1, 0xaf, 0xeb, 0x83, 0x03,  // i32.const
kExprEnd,  // end @550
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
print(instance.exports.main(1, 2, 3));
