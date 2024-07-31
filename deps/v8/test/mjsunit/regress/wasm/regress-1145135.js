// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(1, 1, true);
builder.addGlobal(kWasmI32, true, false);
builder.addFunction(undefined, kSig_v_i)
  .addLocals(kWasmI32, 5)
  .addBody([
// signature: v_i
// body:
kExprGlobalGet, 0x00,  // global.get
kExprI32Const, 0x10,  // i32.const
kExprI32Sub,  // i32.sub
kExprLocalTee, 0x02,  // local.tee
kExprGlobalSet, 0x00,  // global.set
kExprBlock, kWasmVoid,  // block @12
  kExprLocalGet, 0x00,  // local.get
  kExprI32LoadMem, 0x02, 0x00,  // i32.load
  kExprI32Eqz,  // i32.eqz
  kExprIf, kWasmVoid,  // if @20
    kExprLocalGet, 0x02,  // local.get
    kExprI32Const, 0x00,  // i32.const
    kExprI32StoreMem, 0x02, 0x0c,  // i32.store
    kExprLocalGet, 0x00,  // local.get
    kExprI32Const, 0x20,  // i32.const
    kExprI32Add,  // i32.add
    kExprLocalSet, 0x05,  // local.set
    kExprLocalGet, 0x00,  // local.get
    kExprI32Const, 0x00,  // i32.const
    kExprI32Const, 0x01,  // i32.const
    kAtomicPrefix, kExprI32AtomicCompareExchange, 0x02, 0x20,  // i32.atomic.cmpxchng32
]);
assertThrows(() => builder.toModule(), WebAssembly.CompileError);
