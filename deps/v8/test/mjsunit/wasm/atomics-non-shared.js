// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// TODO(gdeepti): If non-shared atomics are moving forward, ensure that
// the tests here are more comprehensive -i.e. reuse atomics.js/atomics64.js
// and cctests to run on both shared/non-shared memory.

(function TestCompileGenericAtomicOp() {
  print(arguments.callee.name);
  let memory = new WebAssembly.Memory({initial: 0, maximum: 10});
  let builder = new WasmModuleBuilder();
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kAtomicPrefix,
      kExprI32AtomicAdd, 2, 0]);
  builder.addImportedMemory("m", "imported_mem");
  let module = new WebAssembly.Module(builder.toBuffer());
})();

(function TestCompileWasmAtomicNotify() {
  print(arguments.callee.name);
  let memory = new WebAssembly.Memory({initial: 0, maximum: 10});
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kAtomicPrefix,
      kExprAtomicNotify, 2, 0])
    .exportAs("main");
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
})();

(function TestCompileWasmI32AtomicWait() {
  print(arguments.callee.name);
  let memory = new WebAssembly.Memory({initial: 0, maximum: 10});
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20);
  builder.addFunction("main",
    makeSig([kWasmI32, kWasmI32, kWasmF64], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprI64SConvertF64,
      kAtomicPrefix,
      kExprI32AtomicWait, 2, 0])
      .exportAs("main");
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {m: {memory}});
})();

(function TestWasmAtomicNotifyResult() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kAtomicPrefix,
      kExprAtomicNotify, 2, 0])
    .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1});
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  assertEquals(0, instance.exports.main(0, 100));
})();

(function TestWasmI32AtomicWaitTraps() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20);
  builder.addFunction("main",
    makeSig([kWasmI32, kWasmI32, kWasmF64], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprLocalGet, 2,
      kExprI64SConvertF64,
      kAtomicPrefix,
      kExprI32AtomicWait, 2, 0])
      .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1});
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  assertThrows(() => instance.exports.main(0, 5, 0), WebAssembly.RuntimeError);
})();

(function TestWasmI64AtomicWaitTraps() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "memory", 0, 20);
  builder.addFunction("main",
    makeSig([kWasmI32, kWasmI32, kWasmF64], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI64UConvertI32,
      kExprLocalGet, 2,
      kExprI64SConvertF64,
      kAtomicPrefix,
      kExprI64AtomicWait, 3, 0])
      .exportAs("main");

  // Instantiate module, get function exports
  let module = new WebAssembly.Module(builder.toBuffer());
  let memory = new WebAssembly.Memory({initial: 1, maximum: 1});
  let instance = new WebAssembly.Instance(module, {m: {memory}});
  assertThrows(() => instance.exports.main(0, 5, 0), WebAssembly.RuntimeError);
})();
