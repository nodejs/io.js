// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function() {
"use asm";
var builder = new WasmModuleBuilder();
builder.addMemory(0, 5);
builder.addFunction("regression_699485", kSig_i_v)
  .addBody([
      kExprI32Const, 0x04,
      kExprNop,
      kExprMemoryGrow, 0x00,
      ]).exportFunc();
let module = builder.instantiate();
assertEquals(0, module.exports.regression_699485());
})();
