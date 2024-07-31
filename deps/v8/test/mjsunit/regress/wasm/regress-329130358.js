// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --expose-gc --wasm-wrapper-tiering-budget=1 --experimental-wasm-type-reflection

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
const type = builder.addType(kSig_i_i);
const global = builder.addImportedGlobal('m', 'val', kWasmAnyFunc);

builder.addFunction('main', type)
    .addBody([
      kExprLocalGet, 0, kExprGlobalGet, global, kGCPrefix, kExprRefCast, type,
      kExprCallRef, type
    ])
    .exportFunc();

function foo() {
  gc();
}
const func =
    new WebAssembly.Function({parameters: ['i32'], results: ['i32']}, foo);

let instance = builder.instantiate({m: {val: func}});
instance.exports.main(3);
instance.exports.main(3);
