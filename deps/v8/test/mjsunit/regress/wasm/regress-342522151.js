// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');
d8.test.enableJSPI();
let builder = new WasmModuleBuilder();
let js_index = builder.addImport("m", "js", kSig_v_v)
builder.addExport("main", js_index);
let instance = builder.instantiate({ m: { js: ()=>{} }});
WebAssembly.promising(instance.exports.main)();
