// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let nogc = () => {};

function newModule() {
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);
  builder.exportMemoryAs("memory");
  builder.addFunction("main", kSig_i_v)
    .addBody([kExprI32Const, 0, kExprI32LoadMem, 0, 0])
    .exportFunc();

  return new WebAssembly.Module(builder.toBuffer());
}

function newInstance(module, val) {
  var instance = new WebAssembly.Instance(module);
  var view = new Int32Array(instance.exports.memory.buffer);
  view[0] = val;
  return instance;
}

function TestSingleLiveInstance(gc) {
  let module = newModule();

  print("TestSingleLiveInstance...");
  for (var i = 0; i < 5; i++) {
    (() => {  // don't leak references between iterations.
      print(" [" + i + "]...");
      gc();
      var instance = newInstance(module, i + 99);
      assertEquals(i + 99, instance.exports.main());
    })();
  }
}

TestSingleLiveInstance(nogc);
TestSingleLiveInstance(gc);

function TestMultiInstance(gc) {
  let module = newModule();

  print("TestMultiInstance...");
  // Note: compute the root instances in another function to be
  // sure that {roots} really is the only set of roots to the instances.
  let roots = (() => { return [
    newInstance(module, 33),
    newInstance(module, 4444),
    newInstance(module, 555555)
  ];})();

  (() => { // don't leak references!
    print(" [0]...");
    gc();
    assertEquals(33, roots[0].exports.main());
    roots[0] = null;
  })();

  (() => { // don't leak references!
    print(" [1]...");
    gc();
    assertEquals(4444, roots[1].exports.main());
    roots[1] = null;
  })();

  (() => { // don't leak references!
    print(" [2]...");
    gc();
    assertEquals(555555, roots[2].exports.main());
    roots[2] = null;
  })();
}

TestMultiInstance(nogc);
TestMultiInstance(gc);

function TestReclaimingCompiledModule() {
  let module = newModule();

  print("TestReclaimingCompiledModule...");
  let roots = (() => { return [
    newInstance(module, 7777),
    newInstance(module, 8888),
  ];})();

  (() => { // don't leak references!
    print(" [0]...");
    assertEquals(7777, roots[0].exports.main());
    assertEquals(8888, roots[1].exports.main());
    roots[1] = null;
  })();

  (() => { // don't leak references!
    print(" [1]...");
    gc();
    roots[1] = newInstance(module, 9999);
    assertEquals(7777, roots[0].exports.main());
    assertEquals(9999, roots[1].exports.main());
    roots[0] = null;
    roots[1] = null;
  })();

  (() => { // don't leak references!
    print(" [2]...");
    gc();
    roots[0] = newInstance(module, 11111);
    roots[1] = newInstance(module, 22222);
    assertEquals(11111, roots[0].exports.main());
    assertEquals(22222, roots[1].exports.main());
    roots[0] = null;
    roots[1] = null;
  })();
}

TestReclaimingCompiledModule(nogc);
TestReclaimingCompiledModule(gc);
