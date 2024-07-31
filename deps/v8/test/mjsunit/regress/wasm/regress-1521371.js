// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging --turboshaft-wasm --no-liftoff

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addArray(kWasmI32, true);
builder.addFunction("main", makeSig([kWasmI32, kWasmI32, kWasmI32], [kWasmI32]))
  .addBody([
    kExprRefNull, 0x00,  // ref.null
    kExprBlock, 0x7f,  // block @7 i32
      kExprBlock, 0x7f,  // block @9 i32
        kExprRefNull, 0x00,  // ref.null
        kExprBlock, 0x7f,  // block @13 i32
          kExprBlock, 0x7f,  // block @15 i32
            kExprBlock, 0x7f,  // block @17 i32
              kExprBlock, 0x7f,  // block @19 i32
                kExprI32Const, 0x85, 0xd8, 0xe1, 0x87, 0x78,  // i32.const
                kExprRefNull, 0x6e,  // ref.null
                kExprBrOnNull, 0x00,  // br_on_null
                kExprDrop,  // drop
                kExprDrop,  // drop
                kExprBlock, 0x7f,  // block @45 i32
                  kExprI32Const, 0xbd, 0x97, 0xc2, 0x92, 0x05,  // i32.const
                  kExprI32Const, 0xa5, 0xb3, 0xa0, 0xb2, 0x07,  // i32.const
                  kExprBrTable, 0x01, 0x00, 0x00,  // br_table entries=1
                  kExprEnd,  // end @63
                kExprEnd,  // end @66
              kExprRefNull, 0x6e,  // ref.null
              kExprBrOnNull, 0x03,  // br_on_null
              kExprDrop,  // drop
              kExprEnd,  // end @111
            kExprEnd,  // end @112
          kExprRefNull, 0x6e,  // ref.null
          kExprBrOnNull, 0x00,  // br_on_null
          kExprDrop,  // drop
          kExprEnd,  // end @156
        kExprI32Const, 0xc9, 0x9d, 0xa2, 0xd0, 0x7b,  // i32.const
        kGCPrefix, kExprArraySet, 0x00,  // array.set
        kExprI32Const, 0xad, 0xf7, 0x93, 0x95, 0x7d,  // i32.const
        kExprEnd,  // end @200
      kExprEnd,  // end @201
    kExprI32Const, 0xcd, 0xe8, 0x9f, 0x9c, 0x05,  // i32.const
    kGCPrefix, kExprArraySet, 0x00,  // array.set
    kExprI32Const, 0xa1, 0xa6, 0x81, 0xc4, 0x7e,  // i32.const
]).exportFunc();
const instance = builder.instantiate();
assertTraps(kTrapNullDereference, () => instance.exports.main(1, 2, 3));
