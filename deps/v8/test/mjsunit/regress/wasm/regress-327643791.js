// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

builder.startRecGroup();
let $array0 = builder.addArray(kWasmI8, true, kNoSuperType, true);
builder.endRecGroup();
let $sig0 = builder.addType(kSig_i_v);
let $sig1 = builder.addType(makeSig([kWasmI32], [wasmRefType(kWasmExternRef)]));
let $sig2 = builder.addType(makeSig([kWasmExternRef, kWasmI32], [kWasmI32]));
let $sig3 = builder.addType(makeSig(
    [wasmRefNullType($array0), kWasmI32, kWasmI32],
    [wasmRefType(kWasmExternRef)]));

let $fromCharCode = builder.addImport("wasm:js-string", "fromCharCode", $sig1);
let $charCodeAt = builder.addImport("wasm:js-string", "charCodeAt", $sig2);
let $decodeStringFromUTF8Array =
    builder.addImport('wasm:text-decoder', 'decodeStringFromUTF8Array', $sig3);

let $main = builder.addFunction("main", $sig0).exportFunc();

// One valid and one invalid UTF-8 byte.
let $data0 = builder.addPassiveDataSegment([97, 141]);

// func $main: [] -> [kWasmI32]
$main.addBody([
  // Create an array from the data segment.
  kExprI32Const, 0,
  kExprI32Const, 2,
  kGCPrefix, kExprArrayNewData, $array0, $data0,
  // Create a string from the array.
  kExprI32Const, 0,
  kExprI32Const, 2,
  kExprCallFunction, $decodeStringFromUTF8Array,
  // The invalid UTF-8 byte should have been replaced with the replacement
  // character 0xFFFD. Read it and return it.
  kExprI32Const, 1,
  kExprCallFunction, $charCodeAt,
  ]);

let kBuiltins = { builtins: ['js-string', 'text-decoder', 'text-encoder'] };
const instance = builder.instantiate({}, kBuiltins);
assertEquals(0xFFFD, instance.exports.main());
