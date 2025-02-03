// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
let $sig0 = builder.addType(makeSig([], [kWasmF64]));
let main0 = builder.addFunction(undefined, $sig0).exportAs('main');
main0.addBody([
    ...[kExprF32Const, 0xfb, 0xff, 0xff, 0xff],
    kExprF64ConvertF32,
    kExprF64NearestInt,
    kExprI64ReinterpretF64,
    kExprF32SConvertI64,
    kExprF64ConvertF32,
]);
let kBuiltins = { builtins: ['js-string', 'text-decoder', 'text-encoder'] };
const wasmInstance = builder.instantiate({}, kBuiltins);
let f = wasmInstance.exports.main;
assertEquals(f(), -2684354560);
