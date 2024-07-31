// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-wasm-lazy-compilation

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
/* sig 0 */ builder.addStruct([makeField(kWasmI16, true)]);
/* sig 1 */ builder.addType(makeSig([kWasmI32, kWasmI32, kWasmI32], []));
/* sig 2 */ builder.addType(makeSig(
    [], [kWasmI64, kWasmFuncRef, kWasmExternRef, wasmRefType(kWasmAnyRef)]));
builder.addFunction(undefined, 1 /* sig */)
  .addLocals(kWasmI64, 1)
  .addBodyWithEnd([
kExprBlock, 2,  // block
  kExprI64Const, 0xd7, 0x01,  // i64.const
  kExprI64Const, 0x00,  // i64.const
  kExprRefNull, 0x70,  // ref.null
  kExprRefNull, 0x6f,  // ref.null
  kExprI32Const, 0x00,  // i32.const
  kGCPrefix, kExprStructNew, 0x00,  // struct.new
  kExprRefNull, 0x6e,  // ref.null
  kExprBrOnNull, 0x00,  // br_on_null
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprDrop,  // drop
  kExprI64Xor,  // i64.xor
  kExprRefNull, 0x70,  // ref.null
  kExprRefNull, 0x6f,  // ref.null
  kExprI32Const, 0x00,  // i32.const
  kGCPrefix, kExprStructNew, 0x00,  // struct.new
  kExprEnd,  // end
kExprUnreachable,   // unreachable
kExprEnd,  // end
]);
builder.addExport('main', 0);
builder.toModule();
