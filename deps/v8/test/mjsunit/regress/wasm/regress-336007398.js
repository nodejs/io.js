// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let kSig_i_w = makeSig([kWasmStringRef], [kWasmI32]);

const builder = new WasmModuleBuilder();
builder.addMemory(1, 1);
builder.addFunction("main", kSig_i_w).exportFunc()
  .addBody([
    kExprLocalGet, 0,
    ...GCInstr(kExprStringAsWtf8),
    kExprI32Const, 0,  // address
    kExprI32Const, 0,  // position in the string
    kExprI32Const, 3,  // bytes
    ...GCInstr(kExprStringViewWtf8EncodeUtf8), 0,  // memory=0
    kExprI32Add,  // use "bytes written" and "next pos" for any i32 operation.
  ]);

let instance = builder.instantiate();
assertEquals(6, instance.exports.main("foo"));
