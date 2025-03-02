// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Should result in an exception when trying to use JSPI.
// Check that we cannot init after installing a SuspendError

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

WebAssembly.SuspendError = 'foo';

d8.test.enableJSPI();
d8.test.installConditionalFeatures();
let v0 = new WasmModuleBuilder();
let v1 = v0.addType(kSig_r_r);
let v2 = v0.addImport("mod", "func", kSig_r_v);
v0.addFunction("main", v1).addBody([kExprCallFunction, v2]).exportFunc();
let v3 = v0.instantiate({ mod: { func: () => {} } });

assertThrows(()=>WebAssembly.promising(v3.exports.main), TypeError);
