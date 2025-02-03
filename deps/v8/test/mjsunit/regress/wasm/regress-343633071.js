// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let builder = new WasmModuleBuilder();

let callee_params = [];
// To trigger the DCHECK, more than 256 bytes need to be passed on the stack.
// Assume that no more than 10 parameters will be passed in registers.
let kNumCalleeParams = 10 + 256 / 8 + 1;
for (let i = 0; i < kNumCalleeParams; i++) {
  callee_params.push(kWasmI64);
}
let callee = builder.addFunction("callee", makeSig(callee_params, []))
  .addBody([]);
let body = [];
for (let i = 0; i < kNumCalleeParams; i++) {
  body.push(kExprI64Const, i);
}
body.push(kExprReturnCall, callee.index);
builder.addFunction("main", kSig_v_v).exportFunc().addBody(body);

builder.instantiate().exports.main();
