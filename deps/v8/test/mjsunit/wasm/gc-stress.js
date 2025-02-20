// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --gc-interval=500 --stress-compaction

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function run(f) {
  var builder = new WasmModuleBuilder();
  builder.addImport("m", "f", kSig_i_i);
  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, 0])
    .exportAs("main");

  print("module");
  var module = new WebAssembly.Module(builder.toBuffer());

  for (var i = 0; i < 10; i++) {
    print("  instance " + i);
    var instance = new WebAssembly.Instance(module, {m: {f: f}});
    var g = instance.exports.main;
    for (var j = 0; j < 10; j++) {
      assertEquals(f(j), g(j));
    }
  }
}

(function test() {
  for (var i = 0; i < 10; i++) {
    run(x => (x + 19 + i));
    run(x => (x - 18));
  }
})();
