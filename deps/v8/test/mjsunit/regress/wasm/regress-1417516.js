// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-wasm-lazy-compilation

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
const call_sig = builder.addType(kSig_v_v);
builder.addMemory(16, 32);
builder.addTable(kWasmFuncRef, 3, 4, undefined)
builder.addFunction(undefined, kSig_i_iii)
  .addBodyWithEnd([
kExprTry, 0x7f,  // try @11 i32
  kExprI32Const, 0x01,  // i32.const
  kExprCallIndirect, call_sig, 0x00,  // call_indirect sig #2: v_v
  kExprI32Const, 0x00,  // i32.const
  kExprI32Const, 0x00,  // i32.const
  kAtomicPrefix, kExprI32AtomicExchange, 0x02, 0x80, 0x80, 0xe8, 0x05,  // i32.atomic.rmw.xchg
kExprCatchAll,  // catch_all @37
  kExprI32Const, 0x01,  // i32.const
  kSimdPrefix, kExprI8x16Splat,  // i8x16.splat
  kExprTry, 0x7f,  // try @62 i32
    kExprI32Const, 0x01,  // i32.const
    kExprCallIndirect, call_sig, 0x00,  // call_indirect sig #2: v_v
    kExprI32Const, 0x00,  // i32.const
    kExprI32Const, 0x00,  // i32.const
    kAtomicPrefix, kExprI32AtomicOr, 0x02, 0x00,  // i32.atomic.rmw.or
  kExprCatchAll,  // catch_all @77
    kExprI32Const, 0x00,  // i32.const
    kExprEnd,  // end @80
  kExprUnreachable,
  kExprEnd,  // end @121
kExprEnd,  // end @128
]);
builder.toModule();
