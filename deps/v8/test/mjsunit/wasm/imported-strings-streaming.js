// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-imported-strings --wasm-test-streaming

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let bad_builder = new WasmModuleBuilder();
let good_builder = new WasmModuleBuilder();
// This intentionally uses an incorrect return type to test LinkErrors.
bad_builder.addImport('wasm:js-string', 'charCodeAt',
                      makeSig([kWasmExternRef, kWasmI32], [kWasmI64]));
let charCodeAt = good_builder.addImport('wasm:js-string', 'charCodeAt',
                       makeSig([kWasmExternRef, kWasmI32], [kWasmI32]));
good_builder.addFunction('test', kSig_i_r).exportFunc().addBody([
  kExprLocalGet, 0, kExprI32Const, 0, kExprCallFunction, charCodeAt
]);

let bad = bad_builder.toBuffer();
let good = good_builder.toBuffer();
let kBuiltins = { builtins: ['js-string'] };

let message = new RegExp(
    'Imported builtin function "wasm:js-string" "charCodeAt" '+
    'has incorrect signature');

// WebAssembly.compileStreaming
assertPromiseResult(
    WebAssembly.compileStreaming(Promise.resolve(good), kBuiltins),
    // Demonstrate compilation success by showing that we can instantiate
    // the module without providing imports, and charCodeAt works.
    (module) => WebAssembly.instantiate(module).then(
        instance => assertEquals(97, instance.exports.test('abc'))),
    // Compilation failure doesn't happen.
    assertUnreachable);
assertThrowsAsync(
    WebAssembly.compileStreaming(Promise.resolve(bad), kBuiltins),
    WebAssembly.LinkError, message);

// WebAssembly.instantiateStreaming
assertPromiseResult(
    WebAssembly.instantiateStreaming(Promise.resolve(good), {}, kBuiltins),
    ({module, instance}) => assertEquals(97, instance.exports.test('abc')),
    assertUnreachable);
assertThrowsAsync(
    WebAssembly.instantiateStreaming(Promise.resolve(bad), {}, kBuiltins),
    WebAssembly.LinkError, message);
