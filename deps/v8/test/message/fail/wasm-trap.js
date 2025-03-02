// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var builder = new WasmModuleBuilder();
// Add a dummy function to make the main function index 1.
builder.addFunction('dummy', kSig_i_v)
    .addBody([kExprI32Const, 0]);
builder.addFunction('main', kSig_i_v)
    .addBody([kExprI32Const, 2, kExprI32Const, 0, kExprI32DivU])
    .exportFunc();
var module = builder.instantiate();
module.exports.main();
