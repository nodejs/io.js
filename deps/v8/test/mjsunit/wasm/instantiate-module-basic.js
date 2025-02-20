// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

let kReturnValue = 17;

let buffer = (() => {
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);
  builder.exportMemoryAs('memory');
  builder.addFunction('main', kSig_i_v)
      .addBody([kExprI32Const, kReturnValue])
      .exportFunc();

  return builder.toBuffer();
})();

function CheckInstance(instance) {
  assertFalse(instance === undefined);
  assertFalse(instance === null);
  assertFalse(instance === 0);
  assertEquals('object', typeof instance);

  // Check the exports object is frozen.
  assertFalse(Object.isExtensible(instance.exports));
  assertTrue(Object.isFrozen(instance.exports));

  // Check the memory is WebAssembly.Memory.
  var mem = instance.exports.memory;
  assertFalse(mem === undefined);
  assertFalse(mem === null);
  assertFalse(mem === 0);
  assertEquals('object', typeof mem);
  assertTrue(mem instanceof WebAssembly.Memory);
  var buf = mem.buffer;
  assertTrue(buf instanceof ArrayBuffer);
  assertEquals(65536, buf.byteLength);
  for (var i = 0; i < 4; i++) {
    instance.exports.memory = 0;  // should be ignored
    mem.buffer = 0;               // should be ignored
    assertSame(mem, instance.exports.memory);
    assertSame(buf, mem.buffer);
  }

  // Check the properties of the main function.
  let main = instance.exports.main;
  assertFalse(main === undefined);
  assertFalse(main === null);
  assertFalse(main === 0);
  assertEquals('function', typeof main);

  assertEquals(kReturnValue, main());
}

// Official API
(function BasicJSAPITest() {
  print('sync module compile...');
  let module = new WebAssembly.Module(buffer);
  print('sync module instantiate...');
  CheckInstance(new WebAssembly.Instance(module));

  print('async module compile...');
  let promise = WebAssembly.compile(buffer);
  assertPromiseResult(
      promise, module => CheckInstance(new WebAssembly.Instance(module)));

  print('async instantiate...');
  let instance_promise = WebAssembly.instantiate(buffer);
  assertPromiseResult(instance_promise, pair => CheckInstance(pair.instance));
})();

// Check that validate works correctly for a module.
assertTrue(WebAssembly.validate(buffer));
assertFalse(WebAssembly.validate(bytes(88, 88, 88, 88, 88, 88, 88, 88)));

// Negative tests.
(function InvalidModules() {
  print('InvalidModules...');
  let invalid_cases = [undefined, 1, '', 'a', {some: 1, obj: 'b'}];
  let len = invalid_cases.length;
  for (var i = 0; i < len; ++i) {
    try {
      let instance = new WebAssembly.Instance(invalid_cases[i]);
      assertUnreachable('should not be able to instantiate invalid modules.');
    } catch (e) {
      assertContains('Argument 0', e.toString());
    }
  }
})();

// Compile async an invalid blob.
(function InvalidBinaryAsyncCompilation() {
  print('InvalidBinaryAsyncCompilation...');
  let builder = new WasmModuleBuilder();
  builder.addFunction('f', kSig_i_i).addBody([kExprCallFunction, 0]);
  assertThrowsAsync(
      WebAssembly.compile(builder.toBuffer()), WebAssembly.CompileError);
})();

// Multiple instances tests.
(function ManyInstances() {
  print('ManyInstances...');
  let compiled_module = new WebAssembly.Module(buffer);
  let instance_1 = new WebAssembly.Instance(compiled_module);
  let instance_2 = new WebAssembly.Instance(compiled_module);
  assertTrue(instance_1 != instance_2);
})();

(function ManyInstancesAsync() {
  print('ManyInstancesAsync...');
  let promise = WebAssembly.compile(buffer);
  assertPromiseResult(promise, compiled_module => {
    let instance_1 = new WebAssembly.Instance(compiled_module);
    let instance_2 = new WebAssembly.Instance(compiled_module);
    assertTrue(instance_1 != instance_2);
  });
})();

(function InstancesAreIsolatedFromEachother() {
  print('InstancesAreIsolatedFromEachother...');
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory('', 'memory', 1);
  var kSig_v_i = makeSig([kWasmI32], []);
  var signature = builder.addType(kSig_v_i);
  builder.addImport('m', 'some_value', kSig_i_v);
  builder.addImport('m', 'writer', signature);

  builder.addFunction('main', kSig_i_i)
      .addBody([
        kExprLocalGet, 0, kExprI32LoadMem, 0, 0, kExprI32Const, 1,
        kExprCallIndirect, signature, kTableZero, kExprLocalGet, 0,
        kExprI32LoadMem, 0, 0, kExprCallFunction, 0, kExprI32Add
      ])
      .exportFunc();

  // writer(mem[i]);
  // return mem[i] + some_value();
  builder.addFunction('_wrap_writer', signature).addBody([
    kExprLocalGet, 0, kExprCallFunction, 1
  ]);
  builder.appendToTable([2, 3]);

  var module = new WebAssembly.Module(builder.toBuffer());
  var mem_1 = new WebAssembly.Memory({initial: 1});
  var mem_2 = new WebAssembly.Memory({initial: 1});
  var view_1 = new Int32Array(mem_1.buffer);
  var view_2 = new Int32Array(mem_2.buffer);

  view_1[0] = 42;
  view_2[0] = 1000;

  var outval_1;
  var outval_2;
  var i1 = new WebAssembly.Instance(module, {
    m: {some_value: () => 1, writer: (x) => outval_1 = x},
    '': {memory: mem_1}
  });

  var i2 = new WebAssembly.Instance(module, {
    m: {some_value: () => 2, writer: (x) => outval_2 = x},
    '': {memory: mem_2}
  });

  assertEquals(43, i1.exports.main(0));
  assertEquals(1002, i2.exports.main(0));

  assertEquals(42, outval_1);
  assertEquals(1000, outval_2);
})();

(function GlobalsArePrivateToTheInstance() {
  print('GlobalsArePrivateToTheInstance...');
  var builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true, false);
  builder.addFunction('read', kSig_i_v)
      .addBody([kExprGlobalGet, 0])
      .exportFunc();

  builder.addFunction('write', kSig_v_i)
      .addBody([kExprLocalGet, 0, kExprGlobalSet, 0])
      .exportFunc();

  var module = new WebAssembly.Module(builder.toBuffer());
  var i1 = new WebAssembly.Instance(module);
  var i2 = new WebAssembly.Instance(module);
  i1.exports.write(1);
  i2.exports.write(2);
  assertEquals(1, i1.exports.read());
  assertEquals(2, i2.exports.read());
})();

(function InstanceMemoryIsIsolated() {
  print('InstanceMemoryIsIsolated...');
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory('', 'memory', 1);

  builder.addFunction('f', kSig_i_v)
      .addBody([kExprI32Const, 0, kExprI32LoadMem, 0, 0])
      .exportFunc();

  var mem_1 = new WebAssembly.Memory({initial: 1});
  var mem_2 = new WebAssembly.Memory({initial: 1});
  var view_1 = new Int32Array(mem_1.buffer);
  var view_2 = new Int32Array(mem_2.buffer);
  view_1[0] = 1;
  view_2[0] = 1000;

  var module = new WebAssembly.Module(builder.toBuffer());
  var i1 = new WebAssembly.Instance(module, {'': {memory: mem_1}});
  var i2 = new WebAssembly.Instance(module, {'': {memory: mem_2}});

  assertEquals(1, i1.exports.f());
  assertEquals(1000, i2.exports.f());
})();

(function MustBeMemory() {
  print('MustBeMemory...');
  var memory = new ArrayBuffer(65536);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory('', 'memory');

  let module = new WebAssembly.Module(builder.toBuffer());

  assertThrows(
      () => new WebAssembly.Instance(module, {'': {memory: memory}}),
      WebAssembly.LinkError);
})();

(function TestNoMemoryToExport() {
  let builder = new WasmModuleBuilder();
  builder.exportMemoryAs('memory', 0);
  assertThrows(() => builder.instantiate(), WebAssembly.CompileError);
})();

(function TestIterableExports() {
  print('TestIterableExports...');
  let builder = new WasmModuleBuilder;
  builder.addExport('a', builder.addFunction('', kSig_v_v).addBody([]));
  builder.addExport('b', builder.addFunction('', kSig_v_v).addBody([]));
  builder.addExport('c', builder.addFunction('', kSig_v_v).addBody([]));
  builder.addExport('d', builder.addFunction('', kSig_v_v).addBody([]));
  builder.addExport('e', builder.addGlobal(kWasmI32, false, false));

  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module);

  let exports_count = 0;
  for (var e in instance.exports) ++exports_count;

  assertEquals(5, exports_count);
})();
