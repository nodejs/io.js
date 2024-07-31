// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --turboshaft-wasm

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
builder.addPassiveDataSegment([11, 164, 106, 8, 188, 248, 2, 195, 186, 227, 6, 173, 145, 10, 239, 41, 165, 102, 205, 196, 250, 246, 89, 223, 75, 78, 190, 81, 51, 245, 207, 14, 88, 42, 70, 13, 119, 59, 95, 243, 83, 104, 132, 250, 201, 240, 63, 118, 228, 17, 98, 41, 15, 147, 82, 86, 231, 181, 103, 1]);
builder.addTable(kWasmFuncRef, 1, 1, undefined)
builder.addActiveElementSegment(0, wasmI32Const(0), [[kExprRefFunc, 0, ]], kWasmFuncRef);
builder.addTag(makeSig([], []));
// Generate function 1 (out of 1).
builder.addFunction(undefined, 2 /* sig */)
  .addLocals(wasmRefNullType(kWasmStructRef), 1)
  .addBodyWithEnd([
// signature: i_iii
// body:
kExprLoop, 0x7f,  // loop @3 i32
  kExprBlock, 0x40,  // block @5
    kExprBlock, 0x40,  // block @7
      kExprBlock, 0x40,  // block @9
        kExprI32Const, 0xc5, 0xd0, 0xe6, 0xcc, 0x01,  // i32.const
        kExprI32Const, 0xa6, 0xc8, 0xa8, 0x52,  // i32.const
        kExprI32Const, 0x14,  // i32.const
        kExprI32RemS,  // i32.rem_s
        kGCPrefix, kExprArrayNew, 0x01,  // array.new
        kExprI32Const, 0xca, 0xe0, 0xeb, 0xa7, 0x7c,  // i32.const
        kExprI32Const, 0xf9, 0xc1, 0xa7, 0xd9, 0x7b,  // i32.const
        kExprI32Const, 0xd7, 0xd1, 0xe2, 0x00,  // i32.const
        kGCPrefix, kExprArrayInitData, 0x01, 0x00,  // array.init_data
        kExprBlock, 0x40,  // block @49
          kExprBlock, 0x40,  // block @51
            kExprI32Const, 0xec, 0xcf, 0xfe, 0xe7, 0x01,  // i32.const
            kExprBrTable, 0x01, 0x00, 0x00,  // br_table entries=1
            kExprEnd,  // end @63
          kExprEnd,  // end @64
        kExprEnd,  // end @65
      kExprEnd,  // end @66
    kExprEnd,  // end @67
  kExprI32Const, 0xa7, 0xc1, 0xd7, 0xa0, 0x7f,  // i32.const
  kExprBrIf, 0x00,  // br_if depth=0
  kExprI32Const, 0xb6, 0xd2, 0x8c, 0xc5, 0x03,  // i32.const
  kExprEnd,  // end @82
kExprI32Const, 0xf3, 0xc7, 0x9b, 0xd6, 0x07,  // i32.const
kExprBrIf, 0x00,  // br_if depth=0
kExprNop,  // nop
kExprEnd,  // end @92
]);
builder.addExport('main', 0);
const instance = builder.instantiate();
try {
  print(instance.exports.main(1, 2, 3));
} catch (e) {
  print('caught exception', e);
}
