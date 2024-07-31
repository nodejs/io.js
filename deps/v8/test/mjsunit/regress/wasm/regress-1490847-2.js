// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addStruct([makeField(wasmRefNullType(kWasmI31Ref), false)]);
builder.addStruct([makeField(kWasmF32, false), makeField(kWasmF64, true), makeField(kWasmF64, true)]);
builder.addStruct([makeField(wasmRefNullType(kWasmNullFuncRef), false)]);
builder.addStruct([]);
builder.startRecGroup();
builder.addArray(kWasmI32, true);
builder.addArray(kWasmI32, true);
builder.addArray(kWasmI32, true);
builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]));
builder.endRecGroup();
builder.addMemory(16, 32);
// Generate function 1 (out of 2).
builder.addFunction(undefined, 7 /* sig */)
  .addLocals(wasmRefType(5), 1).addLocals(wasmRefType(kWasmAnyRef), 1).addLocals(wasmRefNullType(3), 1)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprI64Const, 0x81, 0xee, 0xb8, 0x06,  // i64.const
kExprI64Eqz,  // i64.eqz
kExprBlock, 0x7f,  // block @16 i32
  kExprRefNull, 0x00,  // ref.null
  kGCPrefix, kExprExternConvertAny,  // extern.convert_any
  kGCPrefix, kExprAnyConvertExtern,  // any.convert_extern
  kGCPrefix, kExprRefCastNull, 0x06,  // ref.cast null
  kExprRefAsNonNull,  // ref.as_non_null
  kExprF32Const, 0x6d, 0x6c, 0x72, 0x9c,  // f32.const
  kExprF32Const, 0x3b, 0x17, 0x86, 0x92,  // f32.const
  kExprF32Lt,  // f32.lt
  kExprI32Const, 0xef, 0xc2, 0x95, 0xcb, 0x05,  // i32.const
  kExprI32Const, 0xe4, 0xfd, 0x8a, 0xb7, 0x7c,  // i32.const
  kGCPrefix, kExprArrayFill, 0x06,  // array.fill
  kExprBlock, 0x7f,  // block @54 i32
    kExprBlock, 0x7f,  // block @56 i32
      kExprBlock, 0x7f,  // block @58 i32
        kExprBlock, 0x7f,  // block @60 i32
          kExprBlock, 0x7f,  // block @62 i32
            kExprBlock, 0x7f,  // block @64 i32
              kExprBlock, 0x7f,  // block @66 i32
                kExprI32Const, 0x87, 0xe5, 0x99, 0xef, 0x04,  // i32.const
                kExprI32Const, 0xe1, 0xf4, 0x88, 0xc3, 0x79,  // i32.const
                kExprBrTable, 0x01, 0x00, 0x00,  // br_table entries=1
                kExprEnd,  // end @84
              kExprEnd,  // end @85
            kExprEnd,  // end @86
          kExprEnd,  // end @87
        kExprEnd,  // end @88
      kExprEnd,  // end @89
    kExprEnd,  // end @90
  kExprEnd,  // end @91
kExprI32Const, 0xdc, 0xdc, 0xef, 0x8f, 0x07,  // i32.const
kAtomicPrefix, kExprI32AtomicXor8U, 0x00, 0xba, 0x4d,  // i32.atomic.rmw8.xor_u
kExprI64Const, 0xc3, 0xe4, 0x93, 0x06,  // i64.const
kExprI64Eqz,  // i64.eqz
kAtomicPrefix, kExprI32AtomicXor8U, 0x00, 0xba, 0x1d,  // i32.atomic.rmw8.xor_u
kGCPrefix, kExprArrayNewFixed, 0x05, 0x02,  // array.new_fixed
kExprLocalSet, 0x03,  // local.set
kExprI32Const, 0xd4, 0xb1, 0xab, 0x98, 0x7a,  // i32.const
kExprEnd,  // end @298
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
assertTraps(kTrapNullDereference, () => instance.exports.main(1, 2, 3));
