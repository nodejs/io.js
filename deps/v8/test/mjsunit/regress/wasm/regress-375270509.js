// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-liftoff --expose-fast-api --wasm-fast-api

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const fast_api = new d8.test.FastCAPI();
const bound = Function.prototype.call.bind(fast_api.sum_int64_as_number);
const builder = new WasmModuleBuilder();
const sig = makeSig([kWasmExternRef, kWasmF32, kWasmI32], [kWasmF64]);
const import_index = builder.addImport('mod', 'foo', sig);
builder.addFunction('main', sig)
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kExprCallFunction,
      import_index
    ])
    .exportFunc();
const instance = builder.instantiate({'mod': {'foo': bound}});
assertThrows(
    () => instance.exports.main(fast_api, 2 ** 63), Error,
    'First number is out of int64_t range.');
