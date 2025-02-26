// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let module = (() => {
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, undefined);
  builder.addFunction("grow_memory", kSig_i_i)
              .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
    .exportFunc();
  builder.exportMemoryAs("memory");
  return builder.toModule();
})();

(function TestDetachingViaAPI() {
  print("TestDetachingViaAPI...");
  let memory = new WebAssembly.Memory({initial: 1, maximum: 100});
  let growMem = (pages) => memory.grow(pages);

  let b1 = memory.buffer;
  assertEquals(kPageSize, b1.byteLength);

  growMem(0);
  let b2 = memory.buffer;
  assertFalse(b1 === b2);
  assertEquals(0, b1.byteLength);
  assertEquals(kPageSize, b2.byteLength);

  growMem(1);
  let b3 = memory.buffer;
  assertFalse(b1 === b3);
  assertFalse(b2 === b3);
  assertEquals(0, b1.byteLength);
  assertEquals(0, b2.byteLength);
  assertEquals(2 * kPageSize, b3.byteLength);
})();

(function TestDetachingViaBytecode() {
  print("TestDetachingViaBytecode...");
  let instance = new WebAssembly.Instance(module);
  let growMem = instance.exports.grow_memory;
  let memory = instance.exports.memory;

  let b1 = memory.buffer;
  assertEquals(kPageSize, b1.byteLength);

  growMem(0);
  let b2 = memory.buffer;
  assertFalse(b1 === b2);
  assertEquals(0, b1.byteLength);
  assertEquals(kPageSize, b2.byteLength);

  growMem(1);
  let b3 = memory.buffer;
  assertFalse(b1 === b3);
  assertFalse(b2 === b3);
  assertEquals(0, b1.byteLength);
  assertEquals(0, b2.byteLength);
  assertEquals(2 * kPageSize, b3.byteLength);
})();
