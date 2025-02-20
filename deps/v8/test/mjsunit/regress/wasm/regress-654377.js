// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function() {
  var builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);
  builder.addFunction("foo", kSig_i_v)
    .addBody([
              kExprI32Const, 00,
              kExprMemorySize,
              kExprBrIf, 00,
              kExprMemorySize,
              kExprBr, 0xe7, 0xd2, 0xf2, 0xff, 0x1d
              ])
              .exportFunc();
  assertThrows(function() { builder.instantiate(); });
})();
