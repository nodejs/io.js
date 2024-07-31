// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

builder.startRecGroup();
// 0
builder.addType(makeSig([wasmRefNullType(0)], [kWasmArrayRef]));
// 1
builder.addStruct([
  makeField(kWasmI32, false),
  makeField(wasmRefNullType(1), false),
  makeField(wasmRefType(0), true)
]);
// 2
builder.addType(makeSig([], [wasmRefNullType(2)]), kNoSuperType, false);
// 3
builder.addStruct([]);
// 4
builder.addStruct([
  makeField(kWasmExternRef, false),
  makeField(kWasmArrayRef, false),
  makeField(kWasmI16, false),
  makeField(wasmRefNullType(2), false),
  makeField(wasmRefType(5), false),
  makeField(wasmRefType(kWasmFuncRef), true)
]);
// 5
builder.addType(makeSig([kWasmF32], [kWasmAnyRef]));
// 6
builder.addType(makeSig([], []));
// 7
builder.addArray(kWasmAnyRef, true);
// 8
builder.addStruct([
  makeField(kWasmF32, true),
  makeField(kWasmF64, true),
  makeField(wasmRefNullType(8), false),
  makeField(kWasmFuncRef, true),
  makeField(kWasmI64, false)
]);
// 9
builder.addStruct([
  makeField(kWasmI8, false),
  makeField(wasmRefNullType(2), false)
]);
// 10
builder.addArray(kWasmF64, false);
// 11
builder.addType(makeSig([], [wasmRefNullType(11)]), 2, false);
// 12
builder.addType(makeSig([], [wasmRefNullType(12)]), 11);
// 13
builder.addStruct([
  makeField(wasmRefType(kWasmFuncRef), true),
  makeField(kWasmEqRef, false),
  makeField(kWasmI64, true),
  makeField(wasmRefType(8), true)
]);
// 14
builder.addStruct([
  makeField(kWasmExternRef, false),
  makeField(kWasmNullRef, false),
  makeField(kWasmI16, false),
  makeField(wasmRefType(11), false),
  makeField(wasmRefType(5), false),
  makeField(wasmRefType(kWasmFuncRef), true)
], 4);
// 15
builder.addStruct([]);
// 16
builder.addStruct([
  makeField(kWasmF32, false),
  makeField(wasmRefType(kWasmFuncRef), false),
  makeField(kWasmF32, false),
  makeField(kWasmF64, true),
  makeField(kWasmStructRef, true)
]);
// 17
builder.addType(makeSig([kWasmI32], [wasmRefNullType(15)]));
// 18
builder.addStruct([
  makeField(kWasmI32, true),
  makeField(kWasmI64, true),
  makeField(kWasmI16, true),
  makeField(wasmRefNullType(2), false),
  makeField(wasmRefNullType(2), false)
]);
// 19
builder.addStruct([makeField(wasmRefType(0), true)]);
// 20
builder.addArray(kWasmI32, true);
// 21
builder.addArray(kWasmI64, false);
builder.endRecGroup();

builder.addDeclarativeElementSegment([1, 2]);

builder.addFunction("test", kSig_v_v)
  .addLocals(wasmRefNullType(20), 1)
  .addBody([
    kExprLoop, 0x7d,  // loop @4 f32
      kExprRefFunc, 0x01,  // ref.func
      kExprRefNull, kNullRefCode,  // ref.null
      kExprRefAsNonNull,  // ref.as_non_null
      kExprI64Const, 0x81, 0x80, 0x7e,  // i64.const
      kExprRefNull, kNullRefCode,  // ref.null
      kExprRefAsNonNull,  // ref.as_non_null
      kGCPrefix, kExprStructNew, 0x0d,  // struct.new
      kGCPrefix, kExprStructGet, 0x0d, 0x03,  // struct.get
      kGCPrefix, kExprStructGet, 0x08, 0x00,  // struct.get
      kExprDrop,  // drop
      kExprI32Const, 0x00,  // i32.const
      kExprBrIf, 0x00,  // br_if depth=0
      kExprF32Const, 0x00, 0x00, 0x00, 0x00,  // f32.const
      kExprEnd,  // end @39
    kExprI32SConvertF32,  // i32.trunc_f32_s
    kExprRefNull, kNullRefCode,  // ref.null
    kExprRefFunc, 0x02,  // ref.func
    kGCPrefix, kExprStructNew, 0x01,  // struct.new
    kExprDrop,  // drop
    kExprI32Const, 0x00,  // i32.const
    kExprI32Const, 0x07,  // i32.const
    kGCPrefix, kExprArrayNew, 0x14,  // array.new
    kExprLocalTee, 0x00,  // local.tee
    kExprRefAsNonNull,  // ref.as_non_null
    kGCPrefix, kExprArrayLen,  // array.len
    kExprDrop,  // drop
    kExprUnreachable,])  // unreachable
  .exportFunc();

builder.addFunction(null, 17).addBody([kExprUnreachable]);
builder.addFunction(null, 0).addBody([kExprUnreachable]);

assertTraps(kTrapNullDereference, () => builder.instantiate().exports.test());
