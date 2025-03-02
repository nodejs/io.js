// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --wasm-staging

// Should execute without doing anything (i.e., no crashes)

assertEquals = () => {};
assertTrue  = () => {};
d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

d8.test.enableJSPI();
let v0 = new WasmModuleBuilder();
let v1 = v0.addType(kSig_r_r);
let v2 = v0.addImport("mod", "func", kSig_r_v);
v0.addFunction("main", v1).addBody([kExprCallFunction, v2]).exportFunc();
let v3 = v0.instantiate({ mod: { func: () => {} } });
let v4 = WebAssembly.promising(v3.exports.main);
v4();
