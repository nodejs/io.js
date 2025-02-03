// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $struct2 = builder.addStruct([], kNoSuperType, false);
let $mem0 = builder.addMemory(0, 32);
let $mem1 = builder.addMemory(0, 32);

builder.addFunction("main", kSig_i_i).exportFunc().addBody([
  kExprBlock, kWasmI32,
    ...wasmF64Const(1.1),
    kNumericPrefix, kExprI32UConvertSatF64,
    kExprMemorySize, $mem1,
    kExprI32Ror,
    kExprI32Clz,
    kExprBlock, kWasmI32,
      ...wasmF64Const(1.1),
      kNumericPrefix, kExprI32UConvertSatF64,
      kExprMemorySize, $mem1,
      kExprI32Ror,
      kExprI32Clz,
    kExprEnd,
    kGCPrefix, kExprStructNewDefault, $struct2,
    kExprDrop,
    kExprReturn,
  kExprEnd,
  kGCPrefix, kExprStructNewDefault, $struct2,
  kExprDrop,
]);

const instance = builder.instantiate();
assertEquals(31, instance.exports.main(1));
