// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig4 = builder.addType(kSig_i_iii);
let $mem0 = builder.addMemory(16, 32);
// func $main: [kWasmI32, kWasmI32, kWasmI32] -> [kWasmI32]
builder.addFunction('main', $sig4).exportFunc().addBody([
    // Load from memory. The memory is zero-initialized, so the loaded value is
    // 0.
    kExprI32Const, 1,
    kExprI64LoadMem32S, 1, 0,

    // Store to memory using a SIMD store instruction. This store overlaps with
    // the load above.
    ...wasmI32Const(3),
    ...wasmI32Const(0xFF),
    kSimdPrefix, kExprI8x16Splat,
    kSimdPrefix, kExprS128Store64Lane, 0, 0, 0,

    // Consume the loaded value. Due to a bug the load was "delayed" to this
    // point behind the store causing the load to partially load the stored
    // value.
    kSimdPrefix, kExprI64x2Splat,
    ...SimdInstr(kExprI32x4BitMask),
  ]);

const instance = builder.instantiate({});
assertEquals(0, instance.exports.main(1, 2, 3));
