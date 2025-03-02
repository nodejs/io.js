// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

(function() {
  const builder = new WasmModuleBuilder();
  // Generate function 1 (out of 2).
  sig1 = makeSig([kWasmI32], []);
  builder.addFunction("main", sig1).addBodyWithEnd([
    // signature: v_i
    // body:
    kExprBlock,
  ]);
  assertThrows(function() { builder.instantiate(); }, WebAssembly.CompileError);
})();
