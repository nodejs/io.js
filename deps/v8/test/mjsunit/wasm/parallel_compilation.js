// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-num-compilation-tasks=10

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function assertModule(module, memsize) {
  // Check the module exists.
  assertFalse(module === undefined);
  assertFalse(module === null);
  assertFalse(module === 0);
  assertEquals("object", typeof module);

  // Check the memory is an ArrayBuffer.
  var mem = module.exports.memory;
  assertFalse(mem === undefined);
  assertFalse(mem === null);
  assertFalse(mem === 0);
  assertEquals("object", typeof mem);
  assertTrue(mem instanceof WebAssembly.Memory);
  var buf = mem.buffer;
  assertTrue(buf instanceof ArrayBuffer);
  assertEquals(memsize, buf.byteLength);
  for (var i = 0; i < 4; i++) {
    module.exports.memory = 0;  // should be ignored
    mem.buffer = 0; // should be ignored
    assertSame(mem, module.exports.memory);
    assertSame(buf, mem.buffer);
  }
}

function assertFunction(module, func) {
  assertEquals("object", typeof module.exports);

  var exp = module.exports[func];
  assertFalse(exp === undefined);
  assertFalse(exp === null);
  assertFalse(exp === 0);
  assertEquals("function", typeof exp);
  return exp;
}

(function CompileFunctionsTest() {

  var builder = new WasmModuleBuilder();

  builder.addMemory(1, 1);
  builder.exportMemoryAs("memory");
  for (i = 0; i < 1000; i++) {
    builder.addFunction("sub" + i, kSig_i_i)
      .addBody([                // --
        kExprLocalGet, 0,       // --
        kExprI32Const, i % 61,  // --
        kExprI32Sub])           // --
      .exportFunc()
  }

  var module = builder.instantiate();
  assertModule(module, kPageSize);

  // Check the properties of the functions.
  for (i = 0; i < 1000; i++) {
    var sub = assertFunction(module, "sub" + i);
    assertEquals(33 - (i % 61), sub(33));
  }
})();

(function CallFunctionsTest() {

  var builder = new WasmModuleBuilder();

  var f = []

  f[0] = builder.addFunction("add0", kSig_i_ii)
  .addBody([
            kExprLocalGet, 0,             // --
            kExprLocalGet, 1,             // --
            kExprI32Add,                  // --
          ])
          .exportFunc()

  builder.addMemory(1, 1);
  builder.exportMemoryAs("memory");
  for (i = 1; i < 256; i++) {
    f[i] = builder.addFunction("add" + i, kSig_i_ii)
      .addBody([                                            // --
        kExprLocalGet, 0,                                   // --
        kExprLocalGet, 1,                                   // --
        kExprCallFunction, f[i >>> 1].index])      // --
      .exportFunc()
  }
  var module = builder.instantiate();
  assertModule(module, kPageSize);

  // Check the properties of the functions.
  for (i = 0; i < 256; i++) {
    var add = assertFunction(module, "add" + i);
    assertEquals(88, add(33, 55));
    assertEquals(88888, add(33333, 55555));
    assertEquals(8888888, add(3333333, 5555555));
  }
})();
