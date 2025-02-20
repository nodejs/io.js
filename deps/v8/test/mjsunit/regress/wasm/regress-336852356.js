// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --wasm-staging

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function js() {
  const result = [''];
  gc();
  return result;
}
let builder = new WasmModuleBuilder();
let js_index = builder.addImport('m', 'js', kSig_f_v);
builder.addFunction('main', kSig_f_v)
    .addBody([
        kExprCallFunction, js_index
    ]).exportFunc();
const instance = builder.instantiate({m: {js}});
let promising = WebAssembly.promising(instance.exports.main);
promising();
