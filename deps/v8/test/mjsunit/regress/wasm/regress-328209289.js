// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addMemory(1, 1);
builder.addFunction("main", kSig_i_v).exportFunc().addBody([
    kExprF32Const, 0x11, 0x00, 0xc0, 0xff,
    kExprF32Const, 0x22, 0x00, 0xc0, 0xff,
    kExprI32Const, 0,       // Address.
    kExprI32LoadMem, 2, 0,  // Alignment, offset.
    kExprSelect,
    kExprI32ReinterpretF32,
]);

builder.addFunction("reinterpret", kSig_i_v).exportFunc().addBody([
    ...wasmI32Const(0xffc00011),
    kExprF32ReinterpretI32,
    ...wasmI32Const(0xffc00022),
    kExprF32ReinterpretI32,
    kExprI32Const, 0,       // Address.
    kExprI32LoadMem, 2, 0,  // Alignment, offset.
    kExprSelect,
    kExprI32ReinterpretF32,
]);

builder.addFunction("gvn", makeSig([], [kWasmI32, kWasmI32])).exportFunc().addBody([
    kExprF32Const, 0x11, 0x00, 0xc0, 0xff,
    kExprI32ReinterpretF32,
    kExprF32Const, 0x11, 0x00, 0xe0, 0xff,  // Note difference in third byte.
    kExprI32ReinterpretF32,
]);

const instance = builder.instantiate();
assertEquals(0xffc00022, instance.exports.main() >>> 0);
assertEquals(0xffc00022, instance.exports.reinterpret() >>> 0);

let fs = instance.exports.gvn();
assertEquals(0xffc00011, fs[0] >>> 0);
assertEquals(0xffe00011, fs[1] >>> 0);
