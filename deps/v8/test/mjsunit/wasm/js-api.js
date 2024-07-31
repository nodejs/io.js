// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function assertEq(val, expected) {
  assertSame(expected, val);
}
function assertArrayBuffer(val, expected) {
  assertTrue(val instanceof ArrayBuffer);
  assertEq(expected.length, val.byteLength);
  var input = new Int8Array(val);
  for (var i = 0; i < expected.length; i++) {
    assertEq(expected[i], input[i]);
  }
}

function isConstructor(value) {
  var p = new Proxy(value, { construct: () => ({}) });
  try {
    return new p, true;
  } catch(e) {
    return false;
  }
}

let emptyModuleBinary = (() => {
  var builder = new WasmModuleBuilder();
  return new Int8Array(builder.toBuffer());
})();

let exportingModuleBinary = (() => {
  var builder = new WasmModuleBuilder();
  builder.addFunction('f', kSig_i_v).addBody([kExprI32Const, 42]).exportAs('f');
  return new Int8Array(builder.toBuffer());
})();

let importingModuleBinary = (() => {
  var builder = new WasmModuleBuilder();
  builder.addImport('', 'f', kSig_i_v);
  return new Int8Array(builder.toBuffer());
})();

let memoryImportingModuleBinary = (() => {
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory('', 'my_memory');
  return new Int8Array(builder.toBuffer());
})();

let moduleBinaryImporting2Memories = (() => {
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory('', 'memory1');
  builder.addImportedMemory('', 'memory2');
  return new Int8Array(builder.toBuffer());
})();

let moduleBinaryWithMemSectionAndMemImport = (() => {
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory('', 'memory1');
  builder.addMemory(1, 1);
  return new Int8Array(builder.toBuffer());
})();

// 'WebAssembly' data property on global object
let wasmDesc = Object.getOwnPropertyDescriptor(this, 'WebAssembly');
assertEq(typeof wasmDesc.value, 'object');
assertTrue(wasmDesc.writable);
assertFalse(wasmDesc.enumerable);
assertTrue(wasmDesc.configurable);

// 'WebAssembly' object
assertEq(WebAssembly, wasmDesc.value);
assertEq(String(WebAssembly), '[object WebAssembly]');

// 'WebAssembly.CompileError'
let compileErrorDesc =
    Object.getOwnPropertyDescriptor(WebAssembly, 'CompileError');
assertEq(typeof compileErrorDesc.value, 'function');
assertTrue(compileErrorDesc.writable);
assertFalse(compileErrorDesc.enumerable);
assertTrue(compileErrorDesc.configurable);
let CompileError = WebAssembly.CompileError;
assertEq(CompileError, compileErrorDesc.value);
assertEq(CompileError.length, 1);
assertEq(CompileError.name, 'CompileError');
assertTrue(isConstructor(CompileError));
let compileError = new CompileError;
assertTrue(compileError instanceof CompileError);
assertTrue(compileError instanceof Error);
assertFalse(compileError instanceof TypeError);
assertEq(compileError.message, '');
assertEq(new CompileError('hi').message, 'hi');

// 'WebAssembly.RuntimeError'
let runtimeErrorDesc =
    Object.getOwnPropertyDescriptor(WebAssembly, 'RuntimeError');
assertEq(typeof runtimeErrorDesc.value, 'function');
assertTrue(runtimeErrorDesc.writable);
assertFalse(runtimeErrorDesc.enumerable);
assertTrue(runtimeErrorDesc.configurable);
let RuntimeError = WebAssembly.RuntimeError;
assertEq(RuntimeError, runtimeErrorDesc.value);
assertEq(RuntimeError.length, 1);
assertEq(RuntimeError.name, 'RuntimeError');
assertTrue(isConstructor(RuntimeError));
let runtimeError = new RuntimeError;
assertTrue(runtimeError instanceof RuntimeError);
assertTrue(runtimeError instanceof Error);
assertFalse(runtimeError instanceof TypeError);
assertEq(runtimeError.message, '');
assertEq(new RuntimeError('hi').message, 'hi');

// 'WebAssembly.LinkError'
let linkErrorDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'LinkError');
assertEq(typeof linkErrorDesc.value, 'function');
assertTrue(linkErrorDesc.writable);
assertFalse(linkErrorDesc.enumerable);
assertTrue(linkErrorDesc.configurable);
let LinkError = WebAssembly.LinkError;
assertEq(LinkError, linkErrorDesc.value);
assertEq(LinkError.length, 1);
assertEq(LinkError.name, 'LinkError');
assertTrue(isConstructor(LinkError));
let linkError = new LinkError;
assertTrue(linkError instanceof LinkError);
assertTrue(linkError instanceof Error);
assertFalse(linkError instanceof TypeError);
assertEq(linkError.message, '');
assertEq(new LinkError('hi').message, 'hi');

// 'WebAssembly.Module' data property
let moduleDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'Module');
assertEq(typeof moduleDesc.value, 'function');
assertTrue(moduleDesc.writable);
assertFalse(moduleDesc.enumerable);
assertTrue(moduleDesc.configurable);

// 'WebAssembly.Module' constructor function
let Module = WebAssembly.Module;
assertEq(Module, moduleDesc.value);
assertEq(Module.length, 1);
assertEq(Module.name, 'Module');
assertTrue(isConstructor(Module));
assertThrows(
    () => Module(), TypeError, /must be invoked with 'new'/);
assertThrows(
    () => new Module(), TypeError, /Argument 0 must be a buffer source/);
assertThrows(
    () => new Module(undefined), TypeError,
    'WebAssembly.Module(): Argument 0 must be a buffer source');
assertThrows(
    () => new Module(1), TypeError,
    'WebAssembly.Module(): Argument 0 must be a buffer source');
assertThrows(
    () => new Module({}), TypeError,
    'WebAssembly.Module(): Argument 0 must be a buffer source');
assertThrows(
    () => new Module(new Uint8Array()), CompileError,
    /BufferSource argument is empty/);
assertThrows(
    () => new Module(new ArrayBuffer()), CompileError,
    /BufferSource argument is empty/);
assertTrue(new Module(emptyModuleBinary) instanceof Module);
assertTrue(new Module(emptyModuleBinary.buffer) instanceof Module);

// 'WebAssembly.Module.prototype' data property
let moduleProtoDesc = Object.getOwnPropertyDescriptor(Module, 'prototype');
assertEq(typeof moduleProtoDesc.value, 'object');
assertFalse(moduleProtoDesc.writable);
assertFalse(moduleProtoDesc.enumerable);
assertFalse(moduleProtoDesc.configurable);

// 'WebAssembly.Module.prototype' object
let moduleProto = Module.prototype;
assertEq(moduleProto, moduleProtoDesc.value);
assertEq(String(moduleProto), '[object WebAssembly.Module]');
assertEq(Object.getPrototypeOf(moduleProto), Object.prototype);

// 'WebAssembly.Module' instance objects
let emptyModule = new Module(emptyModuleBinary);
let importingModule = new Module(importingModuleBinary);
let exportingModule = new Module(exportingModuleBinary);
assertEq(typeof emptyModule, 'object');
assertEq(String(emptyModule), '[object WebAssembly.Module]');
assertEq(Object.getPrototypeOf(emptyModule), moduleProto);

// 'WebAssembly.Module.imports' data property
let moduleImportsDesc = Object.getOwnPropertyDescriptor(Module, 'imports');
assertEq(typeof moduleImportsDesc.value, 'function');
assertTrue(moduleImportsDesc.writable);
assertTrue(moduleImportsDesc.enumerable);
assertTrue(moduleImportsDesc.configurable);

// 'WebAssembly.Module.imports' method
let moduleImports = moduleImportsDesc.value;
assertEq(moduleImports.length, 1);
assertFalse(isConstructor(moduleImports));
assertThrows(
    () => moduleImports(), TypeError, /Argument 0 must be a WebAssembly.Module/);
assertThrows(
    () => moduleImports(undefined), TypeError,
    /Argument 0 must be a WebAssembly.Module/);
assertThrows(
    () => moduleImports({}), TypeError,
    /Argument 0 must be a WebAssembly.Module/);
var arr = moduleImports(new Module(emptyModuleBinary));
assertTrue(arr instanceof Array);
assertEq(arr.length, 0);
let importingModuleBinary2 = (() => {
  var text =
      '(module (func (import "a" "b")) (memory (import "c" "d") 1) (table (import "e" "f") 1 anyfunc) (global (import "g" "⚡") i32))'
  let builder = new WasmModuleBuilder();
  builder.addImport('a', 'b', kSig_i_i);
  builder.addImportedMemory('c', 'd');
  builder.addImportedTable('e', 'f');
  builder.addImportedGlobal('g', 'x', kWasmI32);
  return new Int8Array(builder.toBuffer());
})();
var arr = moduleImports(new Module(importingModuleBinary2));
assertTrue(arr instanceof Array);
assertEq(arr.length, 4);
assertEq(arr[0].kind, 'function');
assertEq(arr[0].module, 'a');
assertEq(arr[0].name, 'b');
assertEq(arr[1].kind, 'memory');
assertEq(arr[1].module, 'c');
assertEq(arr[1].name, 'd');
assertEq(arr[2].kind, 'table');
assertEq(arr[2].module, 'e');
assertEq(arr[2].name, 'f');
assertEq(arr[3].kind, 'global');
assertEq(arr[3].module, 'g');
assertEq(arr[3].name, 'x');

// 'WebAssembly.Module.exports' data property
let moduleExportsDesc = Object.getOwnPropertyDescriptor(Module, 'exports');
assertEq(typeof moduleExportsDesc.value, 'function');
assertTrue(moduleExportsDesc.writable);
assertTrue(moduleExportsDesc.enumerable);
assertTrue(moduleExportsDesc.configurable);

// 'WebAssembly.Module.exports' method
let moduleExports = moduleExportsDesc.value;
assertEq(moduleExports.length, 1);
assertFalse(isConstructor(moduleExports));
assertThrows(
    () => moduleExports(), TypeError, /Argument 0 must be a WebAssembly.Module/);
assertThrows(
    () => moduleExports(undefined), TypeError,
    /Argument 0 must be a WebAssembly.Module/);
assertThrows(
    () => moduleExports({}), TypeError,
    /Argument 0 must be a WebAssembly.Module/);
var arr = moduleExports(emptyModule);
assertTrue(arr instanceof Array);
assertEq(arr.length, 0);
let exportingModuleBinary2 = (() => {
  var text =
      '(module (func (export "a")) (memory (export "b") 1) (table (export "c") 1 anyfunc) (global (export "⚡") i32 (i32.const 0)))';
  let builder = new WasmModuleBuilder();
  builder.addFunction('foo', kSig_v_v).addBody([]).exportAs('a');
  builder.addMemory(1, 1);
  builder.exportMemoryAs('b');
  builder.setTableBounds(1, 1);
  builder.addExportOfKind('c', kExternalTable, 0);
  var o = builder.addGlobal(kWasmI32, false, false).exportAs('x');
  return new Int8Array(builder.toBuffer());
})();
var arr = moduleExports(new Module(exportingModuleBinary2));
assertTrue(arr instanceof Array);
assertEq(arr.length, 4);
assertEq(arr[0].kind, 'function');
assertEq(arr[0].name, 'a');
assertEq(arr[1].kind, 'memory');
assertEq(arr[1].name, 'b');
assertEq(arr[2].kind, 'table');
assertEq(arr[2].name, 'c');
assertEq(arr[3].kind, 'global');
assertEq(arr[3].name, 'x');

// 'WebAssembly.Module.customSections' data property
let moduleCustomSectionsDesc =
    Object.getOwnPropertyDescriptor(Module, 'customSections');
assertEq(typeof moduleCustomSectionsDesc.value, 'function');
assertTrue(moduleCustomSectionsDesc.writable);
assertTrue(moduleCustomSectionsDesc.enumerable);
assertTrue(moduleCustomSectionsDesc.configurable);

// 'WebAssembly.Module.customSections' method
let moduleCustomSections = moduleCustomSectionsDesc.value;
assertEq(moduleCustomSections.length, 2);
assertFalse(isConstructor(moduleCustomSections));
assertThrows(
    () => moduleCustomSections(), TypeError, /Argument 0 must be a WebAssembly.Module/);
assertThrows(
    () => moduleCustomSections(undefined), TypeError,
    /Argument 0 must be a WebAssembly.Module/);
assertThrows(
    () => moduleCustomSections({}), TypeError,
    /Argument 0 must be a WebAssembly.Module/);
var arr = moduleCustomSections(emptyModule, 'x');
assertEq(arr instanceof Array, true);
assertEq(arr.length, 0);

assertThrows(
    () => moduleCustomSections(1), TypeError,
    'WebAssembly.Module.customSections(): Argument 0 must be a WebAssembly.Module');

let customSectionModuleBinary2 = (() => {
  let builder = new WasmModuleBuilder();
  builder.addCustomSection('x', [2]);
  builder.addCustomSection('foo', [66, 77]);
  builder.addCustomSection('foo', [91, 92, 93]);
  builder.addCustomSection('fox', [99, 99, 99]);
  return new Int8Array(builder.toBuffer());
})();
var arr = moduleCustomSections(new Module(customSectionModuleBinary2), 'x');
assertEq(arr instanceof Array, true);
assertEq(arr.length, 1);
assertArrayBuffer(arr[0], [2]);
var arr = moduleCustomSections(new Module(customSectionModuleBinary2), 'foo');
assertEq(arr instanceof Array, true);
assertEq(arr.length, 2);
assertArrayBuffer(arr[0], [66, 77]);
assertArrayBuffer(arr[1], [91, 92, 93]);
var arr = moduleCustomSections(new Module(customSectionModuleBinary2), 'bar');
assertEq(arr instanceof Array, true);
assertEq(arr.length, 0);
var o = {toString() { return "foo" }}
var arr = moduleCustomSections(new Module(customSectionModuleBinary2), o);
assertEq(arr instanceof Array, true);
assertEq(arr.length, 2);
assertArrayBuffer(arr[0], [66, 77]);
assertArrayBuffer(arr[1], [91, 92, 93]);
var o = {toString() { throw "boo!" }}
assertThrows(
  () => moduleCustomSections(new Module(customSectionModuleBinary2), o));

// 'WebAssembly.Instance' data property
let instanceDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'Instance');
assertEq(typeof instanceDesc.value, 'function');
assertTrue(instanceDesc.writable);
assertFalse(instanceDesc.enumerable);
assertTrue(instanceDesc.configurable);

// 'WebAssembly.Instance' constructor function
let Instance = WebAssembly.Instance;
assertEq(Instance, instanceDesc.value);
assertEq(Instance.length, 1);
assertEq(Instance.name, 'Instance');
assertTrue(isConstructor(Instance));
assertThrows(
    () => Instance(), TypeError, /must be invoked with 'new'/);
assertThrows(
    () => new Instance(1), TypeError,
    'WebAssembly.Instance(): Argument 0 must be a WebAssembly.Module');
assertThrows(
    () => new Instance({}), TypeError,
    'WebAssembly.Instance(): Argument 0 must be a WebAssembly.Module');
assertThrows(
    () => new Instance(emptyModule, null), TypeError,
    'WebAssembly.Instance(): Argument 1 must be an object');
assertThrows(() => new Instance(importingModule, null), TypeError);
assertThrows(
    () => new Instance(importingModule, undefined), TypeError);
assertThrows(
    () => new Instance(importingModule, {'': {g: () => {}}}), LinkError);
assertThrows(
    () => new Instance(importingModule, {t: {f: () => {}}}), TypeError);

assertTrue(new Instance(emptyModule) instanceof Instance);
assertTrue(new Instance(emptyModule, {}) instanceof Instance);

// 'WebAssembly.Instance.prototype' data property
let instanceProtoDesc = Object.getOwnPropertyDescriptor(Instance, 'prototype');
assertEq(typeof instanceProtoDesc.value, 'object');
assertFalse(instanceProtoDesc.writable);
assertFalse(instanceProtoDesc.enumerable);
assertFalse(instanceProtoDesc.configurable);

// 'WebAssembly.Instance.prototype' object
let instanceProto = Instance.prototype;
assertEq(instanceProto, instanceProtoDesc.value);
assertEq(String(instanceProto), '[object WebAssembly.Instance]');
assertEq(Object.getPrototypeOf(instanceProto), Object.prototype);

// 'WebAssembly.Instance' instance objects
let exportingInstance = new Instance(exportingModule);
assertEq(typeof exportingInstance, 'object');
assertEq(String(exportingInstance), '[object WebAssembly.Instance]');
assertEq(Object.getPrototypeOf(exportingInstance), instanceProto);

// 'WebAssembly.Instance' 'exports' getter property
let instanceExportsDesc =
    Object.getOwnPropertyDescriptor(instanceProto, 'exports');
assertEq(typeof instanceExportsDesc.get, 'function');
assertEq(instanceExportsDesc.get.name, 'get exports');
assertEq(instanceExportsDesc.get.length, 0);
assertFalse(isConstructor(instanceExportsDesc.get));
assertFalse('prototype' in instanceExportsDesc.get);
assertEq(instanceExportsDesc.set, undefined);
assertTrue(instanceExportsDesc.enumerable);
assertTrue(instanceExportsDesc.configurable);

exportsObj = exportingInstance.exports;
assertEq(typeof exportsObj, 'object');
assertFalse(Object.isExtensible(exportsObj));
assertEq(Object.getPrototypeOf(exportsObj), null);
assertEq(Object.keys(exportsObj).join(), 'f');

// Exported WebAssembly functions
let f = exportingInstance.exports.f;
assertTrue(f instanceof Function);
assertEq(f.length, 0);
assertTrue('name' in f);
assertEq(Function.prototype.call.call(f), 42);
assertThrows(() => new f(), TypeError, /is not a constructor/);

// 'WebAssembly.Memory' data property
let memoryDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'Memory');
assertEq(typeof memoryDesc.value, 'function');
assertTrue(memoryDesc.writable);
assertFalse(memoryDesc.enumerable);
assertTrue(memoryDesc.configurable);

// 'WebAssembly.Memory' constructor function
let Memory = WebAssembly.Memory;
assertEq(Memory, memoryDesc.value);
assertEq(Memory.length, 1);
assertEq(Memory.name, 'Memory');
assertTrue(isConstructor(Memory));
assertThrows(
    () => Memory(), TypeError, /must be invoked with 'new'/);
assertThrows(
    () => new Memory(1), TypeError,
    'WebAssembly.Memory(): Argument 0 must be a memory descriptor');
assertThrows(
    () => new Memory({initial: {valueOf() { throw new Error('here') }}}), Error,
    'here');
assertThrows(
    () => new Memory({initial: -1}), TypeError, /must be non-negative/);
assertThrows(
    () => new Memory({initial: Math.pow(2, 32)}), TypeError,
    /must be in the unsigned long range/);
assertThrows(
    () => new Memory({initial: 1, maximum: Math.pow(2, 32) / Math.pow(2, 14)}),
    RangeError, /is above the upper bound/);
assertThrows(
    () => new Memory({initial: 2, maximum: 1}), RangeError,
    /is below the lower bound/);
assertThrows(
    () => new Memory({maximum: -1}), TypeError, /'initial' is required/);
assertTrue(new Memory({initial: 1}) instanceof Memory);
assertEq(new Memory({initial: 1.5}).buffer.byteLength, kPageSize);

// 'WebAssembly.Memory.prototype' data property
let memoryProtoDesc = Object.getOwnPropertyDescriptor(Memory, 'prototype');
assertEq(typeof memoryProtoDesc.value, 'object');
assertFalse(memoryProtoDesc.writable);
assertFalse(memoryProtoDesc.enumerable);
assertFalse(memoryProtoDesc.configurable);

// 'WebAssembly.Memory.prototype' object
let memoryProto = Memory.prototype;
assertEq(memoryProto, memoryProtoDesc.value);
assertEq(String(memoryProto), '[object WebAssembly.Memory]');
assertEq(Object.getPrototypeOf(memoryProto), Object.prototype);

// 'WebAssembly.Memory' instance objects
let mem1 = new Memory({initial: 1});
assertEq(typeof mem1, 'object');
assertEq(String(mem1), '[object WebAssembly.Memory]');
assertEq(Object.getPrototypeOf(mem1), memoryProto);

// 'WebAssembly.Memory.prototype.buffer' accessor property
let bufferDesc = Object.getOwnPropertyDescriptor(memoryProto, 'buffer');
assertEq(typeof bufferDesc.get, 'function');
assertEq(bufferDesc.get.name, 'get buffer');
assertEq(bufferDesc.get.length, 0);
assertFalse(isConstructor(bufferDesc.get));
assertFalse('prototype' in bufferDesc.get);
assertEq(bufferDesc.set, undefined);
assertTrue(bufferDesc.enumerable);
assertTrue(bufferDesc.configurable);

// 'WebAssembly.Memory.prototype.buffer' getter
let bufferGetter = bufferDesc.get;
assertThrows(
    () => bufferGetter.call(), TypeError, /Receiver is not a WebAssembly.Memory/);
assertThrows(
    () => bufferGetter.call({}), TypeError, /Receiver is not a WebAssembly.Memory/);
assertTrue(bufferGetter.call(mem1) instanceof ArrayBuffer);
assertEq(bufferGetter.call(mem1).byteLength, kPageSize);

// 'WebAssembly.Memory.prototype.grow' data property
let memGrowDesc = Object.getOwnPropertyDescriptor(memoryProto, 'grow');
assertEq(typeof memGrowDesc.value, 'function');
assertTrue(memGrowDesc.enumerable);
assertTrue(memGrowDesc.configurable);

// 'WebAssembly.Memory.prototype.grow' method
let memGrow = memGrowDesc.value;
assertEq(memGrow.length, 1);
assertFalse(isConstructor(memGrow));
assertThrows(
    () => memGrow.call(), TypeError, /Receiver is not a WebAssembly.Memory/);
assertThrows(
    () => memGrow.call({}), TypeError, /Receiver is not a WebAssembly.Memory/);
assertThrows(
    () => memGrow.call(mem1, -1), TypeError, /must be non-negative/);
assertThrows(
    () => memGrow.call(mem1, Math.pow(2, 32)), TypeError,
    /must be in the unsigned long range/);
var mem = new Memory({initial: 1, maximum: 2});
var buf = mem.buffer;
assertEq(buf.byteLength, kPageSize);
assertEq(mem.grow(0), 1);
assertTrue(buf !== mem.buffer);
assertEq(buf.byteLength, 0);
buf = mem.buffer;
assertEq(buf.byteLength, kPageSize);
assertEq(mem.grow(1, 23), 1);
assertTrue(buf !== mem.buffer);
assertEq(buf.byteLength, 0);
buf = mem.buffer;
assertEq(buf.byteLength, 2 * kPageSize);
assertEq(mem.grow(0), 2);
assertTrue(buf !== mem.buffer);
assertEq(buf.byteLength, 0);
buf = mem.buffer;
assertEq(buf.byteLength, 2 * kPageSize);
assertThrows(() => mem.grow(1), Error, /Maximum memory size exceeded/);
assertThrows(() => mem.grow(Infinity), Error, /must be convertible to a valid number/);
assertThrows(() => mem.grow(-Infinity), Error, /must be convertible to a valid number/);
assertEq(buf, mem.buffer);
let throwOnValueOf = {
  valueOf: function() {
    throw Error('throwOnValueOf')
  }
};
assertThrows(() => mem.grow(throwOnValueOf), Error, /throwOnValueOf/);
assertEq(buf, mem.buffer);
let zero_wrapper = {
  valueOf: function() {
    ++this.call_counter;
    return 0;
  },
  call_counter: 0
};
assertEq(mem.grow(zero_wrapper), 2);
assertEq(zero_wrapper.call_counter, 1);
assertTrue(buf !== mem.buffer);
assertEq(buf.byteLength, 0);
buf = mem.buffer;
assertEq(buf.byteLength, 2 * kPageSize);

let empty_mem = new Memory({initial: 0, maximum: 5});
let empty_buf = empty_mem.buffer;
assertEq(empty_buf.byteLength, 0);
assertEq(empty_mem.grow(0), 0);
assertEq(empty_mem.buffer.byteLength, 0);
assertTrue(empty_buf !== empty_mem.buffer);

// 'WebAssembly.Table' data property
let tableDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'Table');
assertEq(typeof tableDesc.value, 'function');
assertTrue(tableDesc.writable);
assertFalse(tableDesc.enumerable);
assertTrue(tableDesc.configurable);

// 'WebAssembly.Table' constructor function
let Table = WebAssembly.Table;
assertEq(Table, tableDesc.value);
assertEq(Table.length, 1);
assertEq(Table.name, 'Table');
assertTrue(isConstructor(Table));
assertThrows(
    () => Table(), TypeError, /must be invoked with 'new'/);
assertThrows(
    () => new Table(1), TypeError, 'WebAssembly.Table(): Argument 0 must be a table descriptor');
assertThrows(
    () => new Table({initial: 1, element: 1}), TypeError, /must be a WebAssembly reference type/);
assertThrows(
    () => new Table({initial: 1, element: 'any'}), TypeError,
    /must be a WebAssembly reference type/);
assertThrows(
    () => new Table({initial: 1, element: {valueOf() { return 'anyfunc' }}}),
    TypeError, /must be a WebAssembly reference type/);
assertThrows(
    () => new Table(
        {initial: {valueOf() { throw new Error('here') }}, element: 'anyfunc'}),
    Error, 'here');
assertThrows(
    () => new Table({initial: -1, element: 'anyfunc'}), TypeError,
    /must be non-negative/);
assertThrows(
    () => new Table({initial: Math.pow(2, 32), element: 'anyfunc'}), TypeError,
    /must be in the unsigned long range/);
assertThrows(
    () => new Table({initial: 2, maximum: 1, element: 'anyfunc'}), RangeError,
    /is below the lower bound/);
assertThrows(
    () => new Table({initial: 2, maximum: Math.pow(2, 32), element: 'anyfunc'}),
    TypeError, /must be in the unsigned long range/);
assertTrue(new Table({initial: 1, element: 'anyfunc'}) instanceof Table);
assertTrue(new Table({initial: 1.5, element: 'anyfunc'}) instanceof Table);
assertTrue(
    new Table({initial: 1, maximum: 1.5, element: 'anyfunc'}) instanceof Table);
assertTrue(
    new Table({initial: 1, maximum: Math.pow(2, 32) - 1, element: 'anyfunc'})
        instanceof Table);

// 'WebAssembly.Table.prototype' data property
let tableProtoDesc = Object.getOwnPropertyDescriptor(Table, 'prototype');
assertEq(typeof tableProtoDesc.value, 'object');
assertFalse(tableProtoDesc.writable);
assertFalse(tableProtoDesc.enumerable);
assertFalse(tableProtoDesc.configurable);

// 'WebAssembly.Table.prototype' object
let tableProto = Table.prototype;
assertEq(tableProto, tableProtoDesc.value);
assertEq(String(tableProto), '[object WebAssembly.Table]');
assertEq(Object.getPrototypeOf(tableProto), Object.prototype);

// 'WebAssembly.Table' instance objects
let tbl1 = new Table({initial: 2, element: 'anyfunc'});
assertEq(typeof tbl1, 'object');
assertEq(String(tbl1), '[object WebAssembly.Table]');
assertEq(Object.getPrototypeOf(tbl1), tableProto);

// 'WebAssembly.Table.prototype.length' accessor data property
let lengthDesc = Object.getOwnPropertyDescriptor(tableProto, 'length');
assertEq(typeof lengthDesc.get, 'function');
assertEq(lengthDesc.get.name, 'get length');
assertEq(lengthDesc.get.length, 0);
assertFalse(isConstructor(lengthDesc.get));
assertFalse('prototype' in lengthDesc.get);
assertEq(lengthDesc.set, undefined);
assertTrue(lengthDesc.enumerable);
assertTrue(lengthDesc.configurable);

// 'WebAssembly.Table.prototype.length' getter
let lengthGetter = lengthDesc.get;
assertEq(lengthGetter.length, 0);
assertThrows(
    () => lengthGetter.call(), TypeError, /Receiver is not a WebAssembly.Table/);
assertThrows(
    () => lengthGetter.call({}), TypeError, /Receiver is not a WebAssembly.Table/);
assertEq(typeof lengthGetter.call(tbl1), 'number');
assertEq(lengthGetter.call(tbl1), 2);

// 'WebAssembly.Table.prototype.get' data property
let getDesc = Object.getOwnPropertyDescriptor(tableProto, 'get');
assertEq(typeof getDesc.value, 'function');
assertTrue(getDesc.enumerable);
assertTrue(getDesc.configurable);

// 'WebAssembly.Table.prototype.get' method
let get = getDesc.value;
assertEq(get.length, 1);
assertFalse(isConstructor(get));
assertThrows(
    () => get.call(), TypeError, /Receiver is not a WebAssembly.Table/);
assertThrows(
    () => get.call({}), TypeError, /Receiver is not a WebAssembly.Table/);
assertThrows(
    () => get.call(tbl1), TypeError, /must be convertible to a valid number/);
assertEq(get.call(tbl1, 0), null);
assertEq(get.call(tbl1, 0, Infinity), null);
assertEq(get.call(tbl1, 1), null);
assertEq(get.call(tbl1, 1.5), null);
assertThrows(
    () => get.call(tbl1, 2), RangeError,
    /invalid index 2 into funcref table of size 2/);
assertThrows(
    () => get.call(tbl1, 2.5), RangeError,
    /invalid index 2 into funcref table of size 2/);
assertThrows(() => get.call(tbl1, -1), TypeError, /must be non-negative/);
assertThrows(
    () => get.call(tbl1, Math.pow(2, 33)), TypeError,
  /must be in the unsigned long range/);
assertThrows(
    () => get.call(tbl1, {valueOf() { throw new Error('hi') }}), Error, 'hi');

// 'WebAssembly.Table.prototype.set' data property
let setDesc = Object.getOwnPropertyDescriptor(tableProto, 'set');
assertEq(typeof setDesc.value, 'function');
assertTrue(setDesc.enumerable);
assertTrue(setDesc.configurable);

// 'WebAssembly.Table.prototype.set' method
let set = setDesc.value;
assertEq(set.length, 1);
assertFalse(isConstructor(set));
assertThrows(
    () => set.call(), TypeError, /Receiver is not a WebAssembly.Table/);
assertThrows(
    () => set.call({}), TypeError, /Receiver is not a WebAssembly.Table/);
assertThrows(
    () => set.call(tbl1, undefined), TypeError,
    /must be convertible to a valid number/);
assertThrows(
    () => set.call(tbl1, 2, null), RangeError,
    /invalid index 2 into funcref table of size 2/);
assertThrows(
    () => set.call(tbl1, -1, null), TypeError, /must be non-negative/);
assertThrows(
    () => set.call(tbl1, Math.pow(2, 33), null), TypeError,
    /must be in the unsigned long range/);
assertThrows(
    () => set.call(tbl1, Infinity, null), TypeError,
  /must be convertible to a valid number/);
assertThrows(
    () => set.call(tbl1, -Infinity, null), TypeError,
  /must be convertible to a valid number/);
assertThrows(
    () => set.call(tbl1, 0, undefined), TypeError,
    /Argument 1 is invalid for table: /);
assertThrows(
    () => set.call(tbl1, undefined, undefined), TypeError,
    /must be convertible to a valid number/);
assertThrows(
    () => set.call(tbl1, 0, {}), TypeError,
    /Argument 1 is invalid for table:.*null.*or a Wasm function object/);
assertThrows(
    () => set.call(tbl1, 0, function() {}), TypeError,
    /Argument 1 is invalid for table:.*null.*or a Wasm function object/);
assertThrows(
    () => set.call(tbl1, 0, Math.sin), TypeError,
    /Argument 1 is invalid for table:.*null.*or a Wasm function object/);
assertThrows(
    () => set.call(tbl1, {valueOf() { throw Error('hai') }}, null), Error,
    'hai');
assertEq(set.call(tbl1, 0, null), undefined);
assertEq(set.call(tbl1, 1, null), undefined);
assertThrows(
    () => set.call(tbl1, undefined, null), TypeError,
    /must be convertible to a valid number/);

// 'WebAssembly.Table.prototype.grow' data property
let tblGrowDesc = Object.getOwnPropertyDescriptor(tableProto, 'grow');
assertEq(typeof tblGrowDesc.value, 'function');
assertTrue(tblGrowDesc.enumerable);
assertTrue(tblGrowDesc.configurable);

// 'WebAssembly.Table.prototype.grow' method
let tblGrow = tblGrowDesc.value;
assertEq(tblGrow.length, 1);
assertFalse(isConstructor(tblGrow));
assertThrows(
    () => tblGrow.call(), TypeError, /Receiver is not a WebAssembly.Table/);
assertThrows(
    () => tblGrow.call({}), TypeError, /Receiver is not a WebAssembly.Table/);
assertThrows(
    () => tblGrow.call(tbl1, -1), TypeError, /must be non-negative/);
assertThrows(
    () => tblGrow.call(tbl1, Math.pow(2, 32)), TypeError,
    /must be in the unsigned long range/);
var tbl = new Table({element: 'anyfunc', initial: 1, maximum: 2});
assertEq(tbl.length, 1);
assertThrows(
    () => tbl.grow(Infinity), TypeError, /must be convertible to a valid number/);
assertThrows(
    () => tbl.grow(-Infinity), TypeError, /must be convertible to a valid number/);
assertEq(tbl.grow(0), 1);
assertEq(tbl.length, 1);
assertEq(tbl.grow(1, null, 4), 1);
assertEq(tbl.length, 2);
assertEq(tbl.length, 2);
assertThrows(() => tbl.grow(1), Error, /failed to grow table by \d+/);
assertThrows(
    () => tbl.grow(Infinity), TypeError, /must be convertible to a valid number/);
assertThrows(
    () => tbl.grow(-Infinity), TypeError, /must be convertible to a valid number/);

// 'WebAssembly.validate' function
assertThrows(() => WebAssembly.validate(), TypeError);
assertThrows(() => WebAssembly.validate('hi'), TypeError);
assertTrue(WebAssembly.validate(emptyModuleBinary));
assertTrue(WebAssembly.validate(moduleBinaryImporting2Memories));
assertTrue(WebAssembly.validate(moduleBinaryWithMemSectionAndMemImport));

// 'WebAssembly.compile' data property
let compileDesc = Object.getOwnPropertyDescriptor(WebAssembly, 'compile');
assertEq(typeof compileDesc.value, 'function');
assertTrue(compileDesc.writable);
assertTrue(compileDesc.enumerable);
assertTrue(compileDesc.configurable);

// 'WebAssembly.compile' function
let compile = WebAssembly.compile;
assertEq(compile, compileDesc.value);
assertEq(compile.length, 1);
assertEq(compile.name, 'compile');
assertFalse(isConstructor(compile));
function assertCompileError(args, err, msg) {
  assertThrowsAsync(compile(...args), err /* TODO , msg */);
}
assertCompileError([], TypeError, /requires more than 0 arguments/);
assertCompileError(
    [undefined], TypeError,
    /Argument 0 must be a buffer source/);
assertCompileError(
    [1], TypeError,
    /Argument 0 must be a buffer source/);
assertCompileError(
    [{}], TypeError,
    /Argument 0 must be a buffer source/);
assertCompileError(
    [new Uint8Array()], CompileError, /BufferSource argument is empty/);
assertCompileError(
    [new ArrayBuffer()], CompileError, /BufferSource argument is empty/);
assertCompileError(
    [new Uint8Array('hi!')], CompileError, /failed to match magic number/);
assertCompileError(
    [new ArrayBuffer('hi!')], CompileError, /failed to match magic number/);

function assertCompileSuccess(bytes) {
  var module = null;
  assertPromiseResult(compile(bytes), m => assertTrue(m instanceof Module));
}
assertCompileSuccess(emptyModuleBinary);
assertCompileSuccess(emptyModuleBinary.buffer);

// 'WebAssembly.instantiate' data property
let instantiateDesc =
    Object.getOwnPropertyDescriptor(WebAssembly, 'instantiate');
assertEq(typeof instantiateDesc.value, 'function');
assertTrue(instantiateDesc.writable);
assertTrue(instantiateDesc.enumerable);
assertTrue(instantiateDesc.configurable);

// 'WebAssembly.instantiate' function
let instantiate = WebAssembly.instantiate;
assertEq(instantiate, instantiateDesc.value);
assertEq(instantiate.length, 1);
assertEq(instantiate.name, 'instantiate');
assertFalse(isConstructor(instantiate));
function assertInstantiateError(args, err, msg) {
  assertThrowsAsync(instantiate(...args), err /* TODO , msg */);
}
var scratch_memory = new WebAssembly.Memory({ initial: 0 });
assertInstantiateError([], TypeError, /requires more than 0 arguments/);
assertInstantiateError(
    [undefined], TypeError, /first argument must be a BufferSource/);
assertInstantiateError([1], TypeError, /first argument must be a BufferSource/);
assertInstantiateError(
    [{}], TypeError, /first argument must be a BufferSource/);
assertInstantiateError(
    [new Uint8Array()], CompileError, /failed to match magic number/);
assertInstantiateError(
    [new ArrayBuffer()], CompileError, /failed to match magic number/);
assertInstantiateError(
    [new Uint8Array('hi!')], CompileError, /failed to match magic number/);
assertInstantiateError(
    [new ArrayBuffer('hi!')], CompileError, /failed to match magic number/);
assertInstantiateError(
    [importingModule], TypeError, /second argument must be an object/);
assertInstantiateError(
    [importingModule, null], TypeError, /second argument must be an object/);
assertInstantiateError(
    [importingModuleBinary, null], TypeError,
    /second argument must be an object/);
assertInstantiateError(
    [emptyModule, null], TypeError, /first argument must be a BufferSource/);
assertInstantiateError(
    [importingModuleBinary, null], TypeError, /TODO: error messages?/);
assertInstantiateError(
    [importingModuleBinary, undefined], TypeError, /TODO: error messages?/);
assertInstantiateError(
    [importingModuleBinary, {}], TypeError, /TODO: error messages?/);
assertInstantiateError(
    [importingModuleBinary, {'': {g: () => {}}}], LinkError,
    /TODO: error messages?/);
assertInstantiateError(
    [importingModuleBinary, {t: {f: () => {}}}], TypeError,
    /TODO: error messages?/);
assertInstantiateError(
    [memoryImportingModuleBinary, null], TypeError, /TODO: error messages?/);
assertInstantiateError(
    [memoryImportingModuleBinary, undefined], TypeError,
    /TODO: error messages?/);
assertInstantiateError(
    [memoryImportingModuleBinary, {}], TypeError, /TODO: error messages?/);
assertInstantiateError(
    [memoryImportingModuleBinary, {'mod': {'my_memory': scratch_memory}}],
    TypeError, /TODO: error messages?/);
assertInstantiateError(
    [memoryImportingModuleBinary, {'': {'memory': scratch_memory}}], LinkError,
    /TODO: error messages?/);

function assertInstantiateSuccess(module_or_bytes, imports) {
  var result = null;
  assertPromiseResult(instantiate(module_or_bytes, imports), result => {
    if (module_or_bytes instanceof Module) {
      assertTrue(result instanceof Instance);
    } else {
      assertTrue(result.module instanceof Module);
      assertTrue(result.instance instanceof Instance);
    }
  });
}
assertInstantiateSuccess(emptyModule);
assertInstantiateSuccess(emptyModuleBinary);
assertInstantiateSuccess(emptyModuleBinary.buffer);
assertInstantiateSuccess(importingModule, {'': {f: () => {}}});
assertInstantiateSuccess(importingModuleBinary, {'': {f: () => {}}});
assertInstantiateSuccess(importingModuleBinary.buffer, {'': {f: () => {}}});
assertInstantiateSuccess(
    memoryImportingModuleBinary, {'': {'my_memory': scratch_memory}});

(function TestSubclassing() {
  class M extends WebAssembly.Module { }
  assertThrows(() => new M());

  class I extends WebAssembly.Instance { }
  assertThrows(() => new I());

  class T extends WebAssembly.Table { }
  assertThrows(() => new T());

  class Y extends WebAssembly.Memory { }
  assertThrows(() => new Y());
})();

(function TestCallWithoutNew() {
  var bytes = Uint8Array.of(0x0, 0x61, 0x73, 0x6d, 0x1, 0x00, 0x00, 0x00);
  assertThrows(() => WebAssembly.Module(bytes), TypeError);
  assertThrows(() => WebAssembly.Instance(new WebAssembly.Module(bytes)),
               TypeError);
  assertThrows(() => WebAssembly.Table({size: 10, element: 'anyfunc'}),
               TypeError);
  assertThrows(() => WebAssembly.Memory({size: 10}), TypeError);
})();

(function TestTinyModule() {
  var bytes = Uint8Array.of(0x0, 0x61, 0x73, 0x6d, 0x1, 0x00, 0x00, 0x00);
  var module = new WebAssembly.Module(bytes);
  assertTrue(module instanceof Module);
  var instance = new WebAssembly.Instance(module);
  assertTrue(instance instanceof Instance);
})();

(function TestAccessorFunctions() {
  function testAccessorFunction(obj, prop, accessor) {
    var desc = Object.getOwnPropertyDescriptor(obj, prop);
    assertSame('function', typeof desc[accessor]);
    assertFalse(desc[accessor].hasOwnProperty('prototype'));
    assertFalse(isConstructor(desc[accessor]));
  }
  testAccessorFunction(WebAssembly.Global.prototype, "value", "get");
  testAccessorFunction(WebAssembly.Global.prototype, "value", "set");
  testAccessorFunction(WebAssembly.Instance.prototype, "exports", "get");
  testAccessorFunction(WebAssembly.Memory.prototype, "buffer", "get");
  testAccessorFunction(WebAssembly.Table.prototype, "length", "get");
})();
