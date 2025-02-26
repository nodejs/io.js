// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Use global variables for all values where the test wants to maintain strict
// control over value lifetime. Using local variables would not give sufficient
// guarantees of the value lifetime.
var module;
var instance1;
var instance2;
var instance3;
var instance4;

(function CompiledModuleInstancesInitialize1to3() {
  var builder = new WasmModuleBuilder();

  builder.addMemory(1, 1);
  builder.addImport("", "getValue", kSig_i_v);
  builder.addFunction("f", kSig_i_v)
    .addBody([
      kExprCallFunction, 0
    ]).exportFunc();

  module = new WebAssembly.Module(builder.toBuffer());

  print("Initial instances=0");
  assertEquals(0, %WasmGetNumberOfInstances(module));
  instance1 = new WebAssembly.Instance(module, {"": {getValue: () => 1}});

  print("Initial instances=1");
  assertEquals(1, %WasmGetNumberOfInstances(module));
  instance2 = new WebAssembly.Instance(module, {"": {getValue: () => 2}});

  print("Initial instances=2");
  assertEquals(2, %WasmGetNumberOfInstances(module));
  instance3 = new WebAssembly.Instance(module, {"": {getValue: () => 3}});

  print("Initial instances=3");
  assertEquals(3, %WasmGetNumberOfInstances(module));
})();

(function CompiledModuleInstancesClear1() {
  assertEquals(1, instance1.exports.f());
  instance1 = null;
})();

// Note that two GC's are required because weak slots clearing is deferred.
gc();
gc();
print("After gc instances=2");
assertEquals(2, %WasmGetNumberOfInstances(module));

(function CompiledModuleInstancesClear3() {
  assertEquals(3, instance3.exports.f());
  instance3 = null;
})();

// Note that two GC's are required because weak slots clearing is deferred.
gc();
gc();
print("After gc instances=1");
assertEquals(1, %WasmGetNumberOfInstances(module));

(function CompiledModuleInstancesClear2() {
  assertEquals(2, instance2.exports.f());
  instance2 = null;
})();

// Note that two GC's are required because weak slots clearing is deferred.
gc();
gc();
print("After gc instances=0");
assertEquals(0, %WasmGetNumberOfInstances(module));

(function CompiledModuleInstancesInitialize4AndClearModule() {
  instance4 = new WebAssembly.Instance(module, {"": {getValue: () => 4}});
  assertEquals(4, instance4.exports.f());
  module = null;
  instance4 = null;
})();

// Note that two GC's are required because weak slots clearing is deferred.
gc();
gc();
