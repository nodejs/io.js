// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();
let import_index = builder.addImport('m', 'i', kSig_i_v);
builder.addFunction('main', kSig_v_v).addBody([
    kExprCallFunction, import_index,
    kExprDrop,
    kExprCallFunction, import_index,
    kExprDrop,
]).exportFunc();
let instance = builder.instantiate({m: {
  i: new WebAssembly.Suspending(() => 0)
}});
let async_main = WebAssembly.promising(instance.exports.main);
async_main();
