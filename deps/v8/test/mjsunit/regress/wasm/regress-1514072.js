// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turboshaft-wasm

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let array = builder.addArray(kWasmI32, true);
builder.addFunction('main', makeSig([kWasmI32, kWasmI32], [kWasmI32]))
  .addBody([
    kExprLoop, 0x7f,
      kExprLoop, 0x7f,
        kExprLoop, 0x7f,
          kExprI32Const, 0x11,  // i32.const
          kExprRefNull, 0x6f,  // ref.null
          kGCPrefix, kExprAnyConvertExtern,  // any.convert_extern
          kGCPrefix, kExprRefCastNull, array,  // ref.cast null
          kExprI32Const, 0x12,  // i32.const
          kExprI32Const, 0x13,  // i32.const
          kGCPrefix, kExprArraySet, array,  // array.set
          kExprI32Const, 0x14,  // i32.const
          kExprLocalSet, 0x00,  // local.set
          kExprRefNull, array,  // ref.null
          kExprBrOnNull, 0x00,  // br_on_null
          kExprRefNull, kAnyRefCode,  // ref.null
          kExprBrOnNull, 0x01,  // br_on_null
          kExprRefNull, kAnyRefCode,  // ref.null
          kExprBrOnNull, 0x02,  // br_on_null
          kExprBr, 0x00,  // br
        kExprEnd,
      kExprEnd,
    kExprEnd,
  ]).exportFunc();
const instance = builder.instantiate();
assertTraps(kTrapNullDereference, () => instance.exports.main(1, 2));
