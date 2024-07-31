// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addType(makeSig([], []));
builder.addMemory(1, 1, true);
builder.addGlobal(kWasmI32, true, false, wasmI32Const(10));
// Generate function 1 (out of 5).
builder.addFunction(undefined, 0 /* sig */)
  .addLocals(kWasmI64, 1).addLocals(kWasmF32, 1).addLocals(kWasmF64, 1)
  .addBodyWithEnd([
// signature: i_v
// body:
kExprLoop, 0x7f,  // loop i32
  kExprI32Const, 0x03,  // i32.const
  kExprF32LoadMem, 0x00, 0x00,  // f32.load
  kExprLocalSet, 0x01,  // local.set
  kExprBlock, 0x7f,  // block i32
    kExprLoop, 0x40,  // loop
      kExprGlobalGet, 0x00,  // global.get
      kExprI32Eqz,  // i32.eqz
      kExprIf, 0x40,  // if
        kExprReturn,
        kExprEnd,  // end
      kExprGlobalGet, 0x00,  // global.get
      kExprI32Const, 0x01,  // i32.const
      kExprI32Sub,  // i32.sub
      kExprGlobalSet, 0x00,  // global.set
      kExprI32Const, 0x9d, 0x7f,  // i32.const
      kExprIf, 0x7f,  // if i32
        kExprI32Const, 0x00,  // i32.const
        kExprLocalGet, 0x00,  // local.get
        kExprLocalGet, 0x00,  // local.get
        kAtomicPrefix, kExprI64AtomicCompareExchange8U, 0x00, 0x16,  // i64.atomic.rmw8.cmpxchg_u
        kExprLocalSet, 0x00,  // local.set
        kExprBr, 0x01,  // br depth=1
      kExprElse,  // else
        kExprF32Const, 0x00, 0x00, 0x00, 0xdf,  // f32.const
        kExprLocalSet, 0x01,  // local.set
        kExprI32Const, 0x91, 0xd4, 0x7e,  // i32.const
        kExprEnd,  // end
      kExprBrIf, 0x02,  // br_if depth=2
      kExprEnd,  // end
    kExprUnreachable,
    kExprEnd,  // end
  kExprEnd,  // end
kExprUnreachable,
kExprEnd,  // end
]);
builder.addExport('func_0', 0);
const instance = builder.instantiate();
instance.exports.func_0(1, 2, 3);
