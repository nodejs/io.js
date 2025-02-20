// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function() {
  var builder = new WasmModuleBuilder();
  builder.addFunction("foo", kSig_i_ii)
    .addBody([
        kExprLoop, 00,
        kExprBrTable, 0xfb, 0xff, 0xff, 0xff,
              ])
              .exportFunc();
  assertThrows(function() { builder.instantiate(); });
})();
