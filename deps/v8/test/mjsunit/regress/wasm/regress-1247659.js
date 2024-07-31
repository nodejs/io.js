// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(16, 17);
builder.addGlobal(kWasmI32, true, false, wasmI32Const(10));
// Generate function 1 (out of 3).
builder.addFunction('load', kSig_i_v)
    .addBody([
      kExprI32Const, 0,         // i32.const
      kExprI32LoadMem8U, 0, 5,  // i32.load8_u
    ])
    .exportFunc();
// Generate function 2 (out of 3).
builder.addFunction(undefined, makeSig([kWasmI64, kWasmI32], []))
    .addLocals(kWasmI64, 3)
    .addLocals(kWasmI32, 5)
    .addBody([
      // locals: [1, 0, 0, 0, 0, 0, 0, 0, 0, 0]; stack: []
      kExprGlobalGet, 0,  // global.get
      // locals: [1, 0, 0, 0, 0, 0, 0, 0, 0, 0]; stack: [10]
      kExprLocalSet, 5,  // local.set
      // locals: [1, 0, 0, 0, 0, 10, 0, 0, 0, 0]; stack: []
      kExprI32Const, 0,  // i32.const
      // locals: [1, 0, 0, 0, 0, 10, 0, 0, 0, 0]; stack: [0]
      kExprI32Eqz,  // i32.eqz
      // locals: [1, 0, 0, 0, 0, 10, 0, 0, 0, 0]; stack: [1]
      kExprLocalSet, 6,  // local.set
      // locals: [1, 0, 0, 0, 0, 10, 1, 0, 0, 0]; stack: []
      kExprGlobalGet, 0,  // global.get
      // locals: [1, 0, 0, 0, 0, 10, 1, 0, 0, 0]; stack: [10]
      kExprLocalSet, 7,  // local.set
      // locals: [1, 0, 0, 0, 0, 10, 1, 10, 0, 0]; stack: []
      kExprI32Const, 0,  // i32.const
      // locals: [1, 0, 0, 0, 0, 10, 1, 10, 0, 0]; stack: [0]
      kExprI32Const, 1,  // i32.const
      // locals: [1, 0, 0, 0, 0, 10, 1, 10, 0, 0]; stack: [0, 1]
      kExprI32Sub,  // i32.sub
      // locals: [1, 0, 0, 0, 0, 10, 1, 10, 0, 0]; stack: [-1]
      kExprLocalSet, 8,  // local.set
      // locals: [1, 0, 0, 0, 0, 10, 1, 10, -1, 0]; stack: []
      kExprI32Const, 1,  // i32.const
      // locals: [1, 0, 0, 0, 0, 10, 1, 10, -1, 0]; stack: [1]
      kExprI32Const, 15,  // i32.const
      // locals: [1, 0, 0, 0, 0, 10, 1, 10, -1, 0]; stack: [1, 15]
      kExprI32And,  // i32.and
      // locals: [1, 0, 0, 0, 0, 10, 1, 10, -1, 0]; stack: [1]
      kExprLocalSet, 9,  // local.set
      // locals: [1, 0, 0, 0, 0, 10, 1, 10, -1, 1]; stack: []
      kExprLocalGet, 0,  // local.get
      // locals: [1, 0, 0, 0, 0, 10, 1, 10, -1, 1]; stack: [1]
      kExprLocalSet, 2,  // local.set
      // locals: [1, 0, 1, 0, 0, 10, 1, 10, -1, 1]; stack: []
      kExprLocalGet, 0,  // local.get
      // locals: [1, 0, 1, 0, 0, 10, 1, 10, -1, 1]; stack: [1]
      kExprLocalSet, 3,  // local.set
      // locals: [1, 0, 1, 1, 0, 10, 1, 10, -1, 1]; stack: []
      kExprLocalGet, 2,  // local.get
      // locals: [1, 0, 1, 1, 0, 10, 1, 10, -1, 1]; stack: [1]
      kExprLocalGet, 3,  // local.get
      // locals: [1, 0, 1, 1, 0, 10, 1, 10, -1, 1]; stack: [1, 1]
      kExprI64Sub,  // i64.sub
      // locals: [1, 0, 1, 1, 0, 10, 1, 10, -1, 1]; stack: [0]
      kExprLocalSet, 4,  // local.set
      // locals: [1, 0, 1, 1, 0, 10, 1, 10, -1, 1]; stack: []
      kExprI32Const, 1,  // i32.const
      // locals: [1, 0, 1, 1, 0, 10, 1, 10, -1, 1]; stack: [1]
      kExprLocalGet, 4,  // local.get
      // locals: [1, 0, 1, 1, 0, 10, 1, 10, -1, 1]; stack: [1, 0]
      kExprI64StoreMem16, 1, 0x03,  // i64.store16
    ]);
// Generate function 3 (out of 3).
builder.addFunction('invoker', kSig_v_v)
    .addBody([
      ...wasmI64Const(1),    // i64.const
      ...wasmI32Const(0),    // i32.const
      kExprCallFunction, 1,  // call function #1
    ])
    .exportFunc();
const instance = builder.instantiate();

var exports = instance.exports;
exports.invoker();
assertEquals(0, exports.load());
