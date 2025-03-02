// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.startRecGroup();
let i8array = builder.addArray(kWasmI8, true, kNoSuperType, true);
builder.endRecGroup();

let kSig_e_aii = builder.addType(
    makeSig([wasmRefNullType(i8array), kWasmI32, kWasmI32], [wasmRefType(kWasmExternRef)]));

let kStringFromUtf8Array = builder.addImport(
    'wasm:text-decoder', 'decodeStringFromUTF8Array', kSig_e_aii);

let $data0 = builder.addPassiveDataSegment([97, 98, 99]);

builder.addFunction("main", makeSig([], [kWasmExternRef])).exportFunc()
  .addBody([
    kExprI32Const, 0,  // offset.
    kExprI32Const, 3,  // length.
    kGCPrefix, kExprArrayNewData, i8array, $data0,
    kExprI32Const, 0x5,  // start (OOB)
    kExprI32Const, 0x7,  // length
    kExprCallFunction, kStringFromUtf8Array,
    kExprDrop,  // Despite the string being unused, we have to trap.
    kExprRefNull, kExternRefCode,
]);

let kBuiltins = { builtins: ['text-decoder'] };
const instance = builder.instantiate({}, kBuiltins);
assertThrows(() => instance.exports.main(), WebAssembly.RuntimeError,
             "array element access out of bounds");
