// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig3 = builder.addType(kSig_i_v);
let $sig7 = builder.addType(makeSig([kWasmI32], [wasmRefType(kWasmExternRef)]));
let $sig8 = builder.addType(makeSig([kWasmExternRef, kWasmI32], [kWasmI32]));
let $fromCharCode = builder.addImport("wasm:js-string", "fromCharCode", $sig7);
let $charCodeAt = builder.addImport("wasm:js-string", "charCodeAt", $sig8);
let $oob = builder.addFunction("oob", $sig3).exportFunc();
let $inbounds = builder.addFunction("inbounds", $sig3).exportFunc();

$oob.addBody([
  ...wasmI32Const(0x8000_0061),
  kExprCallFunction, $fromCharCode,
  // arm64 erroneously mapped this to 0, resulting in an in-bounds access.
  ...wasmI32Const(0x8000_0000),
  kExprCallFunction, $charCodeAt,
]);

$inbounds.addBody([
  // x64 erroneously mapped this to -0x8000_0000, resulting in the char
  // code (after masking) being 0.
  ...wasmI32Const(0x8000_0061),
  kExprCallFunction, $fromCharCode,
  ...wasmI32Const(0),
  kExprCallFunction, $charCodeAt,
]);

let kBuiltins = { builtins: ['js-string'] };
const instance = builder.instantiate({}, kBuiltins);

assertThrows(() => instance.exports.oob(), WebAssembly.RuntimeError,
             'string offset out of bounds');
assertEquals(0x61, instance.exports.inbounds());
