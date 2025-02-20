// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --verify-heap

d8.file.execute("test/mjsunit/wasm/user-properties-common.js");

(function ImportReexportChain() {
  print("ImportReexportChain");

  var f = id;

  for (let i = 0; i < 5; i++) {
    let builder = new WasmModuleBuilder();
    builder.addImport("imp", "func", kSig_i_i);
    builder.addExport("exp", 0);
    let module = builder.toModule();
    let instance = new WebAssembly.Instance(module, {imp: {func: f}});
    let g = instance.exports.exp;
    assertInstanceof(g, Function);
    printName("before", g);
    testProperties(g);
    printName(" after", g);

    // The Wasm-internal fields of {g} are only inspected when {g} is
    // used as an import into another instance. Use {g} as the import
    // the next time through the loop.
    f = g;
  }
})();
