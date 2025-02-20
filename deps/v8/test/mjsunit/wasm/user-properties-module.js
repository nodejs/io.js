// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --verify-heap

d8.file.execute("test/mjsunit/wasm/user-properties-common.js");

(function ModuleTest() {
  for (f of [x => (x + 19 + globalCounter), minus18]) {
    print("ModuleTest");

    let builder = new WasmModuleBuilder();
    builder.addImport("m", "f", kSig_i_i);
    builder.addFunction("main", kSig_i_i)
      .addBody([
        kExprLocalGet, 0,
        kExprCallFunction, 0])
      .exportAs("main");
    builder.addMemory(1, 1);
    builder.exportMemoryAs("memory");

    let module = builder.toModule();
    testProperties(module);

    for (let i = 0; i < 3; i++) {
      print("  instance " + i);
      let instance = new WebAssembly.Instance(module, {m: {f: f}});
      testProperties(instance);

      print("  memory   " + i);
      let m = instance.exports.memory;
      assertInstanceof(m, WebAssembly.Memory);
      testProperties(m);

      print("  function " + i);
      let g = instance.exports.main;
      assertInstanceof(g, Function);
      printName("before", g);
      testProperties(g);
      printName(" after", g);
      assertInstanceof(g, Function);
      testProperties(g);
      for (let j = 10; j < 15; j++) {
        assertEquals(f(j), g(j));
      }
      verifyHeap();
      // The Wasm-internal fields of {g} are only inspected when {g} is
      // used as an import into another instance. Use {g} as the import
      // the next time through the loop.
      f = g;
    }
  }
})();
