// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

var kV8MaxWasmMemoryPages = 65536;  // 4 GiB
var kSpecMaxWasmMemoryPages = 65536;  // 4 GiB

(function testMemorySizeZero() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addMemory(0, 0);
  builder.addFunction("memory_size", kSig_i_v)
         .addBody([kExprMemorySize, kMemoryZero])
         .exportFunc();
  var module = builder.instantiate();
  assertEquals(0, module.exports.memory_size());
})();

(function testMemorySizeNonZero() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  var size = 11;
  builder.addMemory(size, size);
  builder.addFunction("memory_size", kSig_i_v)
         .addBody([kExprMemorySize, kMemoryZero])
         .exportFunc();
  var module = builder.instantiate();
  assertEquals(size, module.exports.memory_size());
})();

(function testMemorySizeSpecMaxOk() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addMemory(1, kSpecMaxWasmMemoryPages);
  builder.addFunction("memory_size", kSig_i_v)
         .addBody([kExprMemorySize, kMemoryZero])
         .exportFunc();
  var module = builder.instantiate();
  assertEquals(1, module.exports.memory_size());
})();

(function testMemorySizeV8MaxPlus1Throws() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addMemory(kV8MaxWasmMemoryPages + 1,
                    kV8MaxWasmMemoryPages + 1);
  builder.addFunction("memory_size", kSig_i_v)
         .addBody([kExprMemorySize, kMemoryZero])
         .exportFunc();
  assertThrows(() => builder.instantiate());
})();

(function testMemorySpecMaxOk() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addMemory(1, kSpecMaxWasmMemoryPages);
  builder.addFunction("memory_size", kSig_i_v)
         .addBody([kExprMemorySize, kMemoryZero])
    .exportFunc();
  var module = builder.instantiate();
  assertEquals(1, module.exports.memory_size());
})();

(function testMemoryInitialMaxPlus1Throws() {
  print(arguments.callee.name);
  assertThrows(() => new WebAssembly.Memory(
      {initial: kV8WasmMaxMemoryPages + 1}));
})();
