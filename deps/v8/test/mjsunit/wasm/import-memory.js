// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// V8 internal memory size limit.
var kV8MaxPages = 65536;

(function TestOne() {
  print("TestOne");
  let memory = new WebAssembly.Memory({initial: 1});
  assertEquals(kPageSize, memory.buffer.byteLength);
  let i32 = new Int32Array(memory.buffer);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("mod", "mine");
  builder.addFunction("main", kSig_i_v)
    .addBody([
      kExprI32Const, 0,
      kExprI32LoadMem, 0, 0])
    .exportAs("main");

  let main = builder.instantiate({mod: {mine: memory}}).exports.main;
  assertEquals(0, main());

  i32[0] = 993377;

  assertEquals(993377, main());
})();

(function TestIdentity() {
  print("TestIdentity");
  let memory = new WebAssembly.Memory({initial: 1});
  let i32 = new Int32Array(memory.buffer);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("dad", "garg");
  builder.exportMemoryAs("daggle");

  let instance = builder.instantiate({dad: {garg: memory}});
  assertSame(memory, instance.exports.daggle);
})();

(function TestImportExport() {
  print("TestImportExport");
  var i1;
  {
    let builder = new WasmModuleBuilder();
    builder.addMemory(1, 1);
    builder.exportMemoryAs("exported_mem");
    builder.addFunction("foo", kSig_i_i)
      .addBody([
        kExprLocalGet, 0,
        kExprI32LoadMem, 0, 0])
      .exportAs("foo");
    i1 = builder.instantiate();
  }

  var i2;
  {
    let builder = new WasmModuleBuilder();
    builder.addImportedMemory("fil", "imported_mem");
    builder.addFunction("bar", kSig_i_i)
      .addBody([
        kExprLocalGet, 0,
        kExprI32LoadMem, 0, 0])
      .exportAs("bar");
    i2 = builder.instantiate({fil: {imported_mem: i1.exports.exported_mem}});
  }

  let i32 = new Int32Array(i1.exports.exported_mem.buffer);

  for (var i = 0; i < 1e11; i = i * 3 + 5) {
    for (var j = 0; j < 10; j++) {
      var val = i + 99077 + j;
      i32[j] = val;
      assertEquals(val | 0, i1.exports.foo(j * 4));
      assertEquals(val | 0, i2.exports.bar(j * 4));
    }
  }
})();

(function ValidateBoundsCheck() {
  print("ValidateBoundsCheck");
  let memory = new WebAssembly.Memory({initial: 1, maximum: 5});
  assertEquals(kPageSize, memory.buffer.byteLength);
  let i32 = new Int32Array(memory.buffer);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("gaz", "mine");
  builder.addFunction("load", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0])
      .exportFunc();
  builder.addFunction("store", kSig_i_ii)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0, 0,
                kExprLocalGet, 1])
      .exportFunc();
  var offset;
  let instance = builder.instantiate({gaz: {mine: memory}});
  function load() { return instance.exports.load(offset); }
  function store(value) { return instance.exports.store(offset, value); }

  for (offset = 0; offset < kPageSize - 3; offset+=4) {
    store(offset);
  }
  for (offset = 0; offset < kPageSize - 3; offset+=4) {
    assertEquals(offset, load());
  }
  for (offset = kPageSize - 3; offset < kPageSize + 4; offset++) {
    assertTraps(kTrapMemOutOfBounds, load);
  }
})();

(function TestMemoryGrowMaxDesc() {
  print("MaximumDescriptor");
  let memory = new WebAssembly.Memory({initial: 1, maximum: 5});
  assertEquals(kPageSize, memory.buffer.byteLength);
  let i32 = new Int32Array(memory.buffer);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("mine", "dog", 0, 20);
  builder.addFunction("load", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0])
      .exportFunc();
  builder.addFunction("store", kSig_i_ii)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0, 0,
                kExprLocalGet, 1])
      .exportFunc();
  var offset;
  let instance = builder.instantiate({mine: {dog: memory}});
  function load() { return instance.exports.load(offset); }
  function store(value) { return instance.exports.store(offset, value); }

  for (var i = 1; i < 5; i++) {
    for (offset = (i - 1) * kPageSize; offset < i * kPageSize - 3; offset+=4) {
      store(offset * 2);
    }
    assertEquals(i, memory.grow(1));
    assertEquals((i + 1) * kPageSize, memory.buffer.byteLength);
  }
  for (offset = 4 * kPageSize; offset < 5 * kPageSize - 3; offset+=4) {
    store(offset * 2);
  }
  for (offset = 0; offset < 5 * kPageSize - 3; offset+=4) {
    assertEquals(offset * 2, load());
  }
  for (offset = 5 * kPageSize; offset < 5 * kPageSize + 4; offset++) {
    assertThrows(load);
  }
  assertThrows(() => memory.grow(1));
})();

(function TestMemoryGrowZeroInitialMemory() {
  print("ZeroInitialMemory");
  let memory = new WebAssembly.Memory({initial: 0});
  assertEquals(0, memory.buffer.byteLength);
  let i32 = new Int32Array(memory.buffer);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("mine", "fro");
  builder.addFunction("load", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0])
      .exportFunc();
  builder.addFunction("store", kSig_i_ii)
      .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprI32StoreMem, 0, 0,
                kExprLocalGet, 1])
      .exportFunc();
  var offset;
  let instance = builder.instantiate({mine: {fro: memory}});
  function load() { return instance.exports.load(offset); }
  function store(value) { return instance.exports.store(offset, value); }

  for (var i = 1; i < 5; i++) {
    assertEquals(i - 1, memory.grow(1));
    assertEquals(i * kPageSize, memory.buffer.byteLength);
    for (offset = (i - 1) * kPageSize; offset < i * kPageSize - 3; offset++) {
      store(offset * 2);
    }
  }
  for (offset = 5 * kPageSize; offset < 5 * kPageSize + 4; offset++) {
    assertThrows(load);
  }
  assertThrows(() => memory.grow(kV8MaxPages - 3));
})();

(function ImportedMemoryBufferLength() {
  print("ImportedMemoryBufferLength");
  let memory = new WebAssembly.Memory({initial: 2, maximum: 10});
  assertEquals(2*kPageSize, memory.buffer.byteLength);
  let builder = new WasmModuleBuilder();
  builder.addFunction("grow", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
      .exportFunc();
  builder.addImportedMemory("cat", "mine");
  let instance = builder.instantiate({cat: {mine: memory}});
  function grow(pages) { return instance.exports.grow(pages); }
  assertEquals(2, grow(3));
  assertEquals(5*kPageSize, memory.buffer.byteLength);
  assertEquals(5, grow(5));
  assertEquals(10*kPageSize, memory.buffer.byteLength);
  assertThrows(() => memory.grow(1));
})();

(function TestMemoryGrowExportedMaximum() {
  print("TestMemoryGrowExportedMaximum");
  let initial_size = 1, maximum_size = 10;
  var exp_instance;
  {
    let builder = new WasmModuleBuilder();
    builder.addMemory(initial_size, maximum_size);
    builder.exportMemoryAs("exported_mem");
    exp_instance = builder.instantiate();
  }
  var instance;
  {
    var builder = new WasmModuleBuilder();
    builder.addImportedMemory("fur", "imported_mem");
    builder.addFunction("mem_size", kSig_i_v)
      .addBody([kExprMemorySize, kMemoryZero])
      .exportFunc();
    builder.addFunction("grow", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
      .exportFunc();
    instance = builder.instantiate({fur: {
      imported_mem: exp_instance.exports.exported_mem}});
  }
  for (var i = initial_size; i < maximum_size; i++) {
    assertEquals(i, instance.exports.grow(1));
    assertEquals((i+1), instance.exports.mem_size());
  }
  assertEquals(-1, instance.exports.grow(1));
})();

(function TestMemoryGrowWebAssemblyInstances() {
  print("TestMemoryGrowWebAssemblyInstances");
  let memory = new WebAssembly.Memory({initial: 1, maximum: 15});
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("lit", "imported_mem");
  builder.addFunction("mem_size", kSig_i_v)
    .addBody([kExprMemorySize, kMemoryZero])
    .exportAs("mem_size");
  builder.addFunction("grow", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
    .exportFunc();
  var module = new WebAssembly.Module(builder.toBuffer());
  var instances = [];
  for (var i = 0; i < 6; i++) {
    instances.push(new WebAssembly.Instance(module, {lit: {imported_mem: memory}}));
  }
  function verify_mem_size(expected_pages) {
    assertEquals(expected_pages*kPageSize,
        memory.buffer.byteLength);
    for (var i = 0; i < 6; i++) {
      assertEquals(expected_pages, instances[i].exports.mem_size());
    }
  }

  // Verify initial memory size
  verify_mem_size(1);

  // Verify memory size with interleaving calls to Memory.grow,
  // MemoryGrow opcode.
  var current_mem_size = 1;
  for (var i = 0; i < 5; i++) {
    function grow(pages) { return instances[i].exports.grow(pages); }
    assertEquals(current_mem_size, memory.grow(1));
    verify_mem_size(++current_mem_size);
    assertEquals(current_mem_size, instances[i].exports.grow(1));
    verify_mem_size(++current_mem_size);
  }

  assertThrows(() => memory.grow(5));
})();

(function TestImportedMemoryGrowMultipleInstances() {
  print("TestImportMemoryMultipleInstances");
  let memory = new WebAssembly.Memory({initial: 5, maximum: 100});
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("nob", "imported_mem");
  builder.addFunction("mem_size", kSig_i_v)
    .addBody([kExprMemorySize, kMemoryZero])
    .exportFunc();
  builder.addFunction("grow", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
    .exportFunc();
  var instances = [];
  for (var i = 0; i < 5; i++) {
    instances.push(builder.instantiate({nob: {imported_mem: memory}}));
  }
  function grow_instance_0(pages) { return instances[0].exports.grow(pages); }
  function grow_instance_1(pages) { return instances[1].exports.grow(pages); }
  function grow_instance_2(pages) { return instances[2].exports.grow(pages); }
  function grow_instance_3(pages) { return instances[3].exports.grow(pages); }
  function grow_instance_4(pages) { return instances[4].exports.grow(pages); }

  function verify_mem_size(expected_pages) {
    assertEquals(expected_pages*kPageSize, memory.buffer.byteLength);
    for (var i = 0; i < 5; i++) {
      assertEquals(expected_pages, instances[i].exports.mem_size());
    }
  }

  // Verify initial memory size
  verify_mem_size(5);

  // Grow instance memory and buffer memory out of order and verify memory is
  // updated correctly.
  assertEquals(5, grow_instance_0(7));
  verify_mem_size(12);

  assertEquals(12, memory.grow(4));
  verify_mem_size(16);

  assertEquals(16, grow_instance_4(1));
  verify_mem_size(17);

  assertEquals(17, grow_instance_1(6));
  verify_mem_size(23);

  assertEquals(23, grow_instance_3(2));
  verify_mem_size(25);

  assertEquals(25, memory.grow(10));
  verify_mem_size(35);

  assertEquals(35, grow_instance_2(15));
  verify_mem_size(50);
  assertThrows(() => memory.grow(51));
})();

(function TestExportImportedMemoryGrowMultipleInstances() {
  print("TestExportImportedMemoryGrowMultipleInstances");
  var instance;
  {
    let builder = new WasmModuleBuilder();
    builder.addMemory(1, 11);
    builder.exportMemoryAs("exported_mem");
    builder.addFunction("mem_size", kSig_i_v)
      .addBody([kExprMemorySize, kMemoryZero])
      .exportFunc();
    instance = builder.instantiate();
  }
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("doo", "imported_mem");
  builder.addFunction("mem_size", kSig_i_v)
    .addBody([kExprMemorySize, kMemoryZero])
    .exportFunc();
  builder.addFunction("grow", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
    .exportFunc();
  var instances = [];
  for (var i = 0; i < 10; i++) {
    instances.push(builder.instantiate({
      doo: {imported_mem: instance.exports.exported_mem}}));
  }
  function verify_mem_size(expected_pages) {
    for (var i = 0; i < 10; i++) {
      assertEquals(expected_pages, instances[i].exports.mem_size());
    }
  }
  var current_mem_size = 1;
  for (var i = 0; i < 10; i++) {
    function grow(pages) { return instances[i].exports.grow(pages); }
    assertEquals(current_mem_size, instances[i].exports.grow(1));
    verify_mem_size(++current_mem_size);
  }
  for (var i = 0; i < 10; i++) {
    assertEquals(-1, instances[i].exports.grow(1));
    verify_mem_size(current_mem_size);
  }
})();

(function TestExportImportedMemoryGrowPastV8Maximum() {
  // The spec maximum is higher than the internal V8 maximum. This test only
  // checks that grow_memory does not grow past the internally defined maximum
  // to reflect the current implementation even when the memory is exported.
  print("TestExportImportedMemoryGrowPastV8Maximum");
  var instance_1, instance_2;
  {
    let builder = new WasmModuleBuilder();
    builder.addMemory(1, kSpecMaxPages);
    builder.exportMemoryAs("exported_mem");
    builder.addFunction("grow", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
      .exportFunc();
    instance_1 = builder.instantiate();
  }
  {
    let builder = new WasmModuleBuilder();
    builder.addImportedMemory("doo", "imported_mem");
    builder.addFunction("grow", kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
      .exportFunc();
    instance_2 = builder.instantiate({
      doo: {imported_mem: instance_1.exports.exported_mem}});
  }
  assertEquals(1, instance_1.exports.grow(20));
  assertEquals(21, instance_2.exports.grow(20));
  assertEquals(-1, instance_1.exports.grow(kV8MaxPages - 40));
  assertEquals(-1, instance_2.exports.grow(kV8MaxPages - 40));
})();

(function TestExportGrow() {
  print("TestExportGrow");
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 5);
  builder.exportMemoryAs("exported_mem");
  builder.addFunction("mem_size", kSig_i_v)
    .addBody([kExprMemorySize, kMemoryZero])
    .exportFunc();
  builder.addFunction("grow", kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprMemoryGrow, kMemoryZero])
    .exportFunc();
  instance = builder.instantiate();
  assertEquals(kPageSize, instance.exports.exported_mem.buffer.byteLength);
  assertEquals(1, instance.exports.grow(2));
  assertEquals(3, instance.exports.mem_size());
  assertEquals(3*kPageSize, instance.exports.exported_mem.buffer.byteLength);
})();

(function TestImportTooLarge() {
  print("TestImportTooLarge");
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "m", 1, 2);

  // initial size is too large
  assertThrows(() => builder.instantiate({m: {m: new WebAssembly.Memory({initial: 3, maximum: 3})}}));

  // maximum size is too large
  assertThrows(() => builder.instantiate({m: {m: new WebAssembly.Memory({initial: 1, maximum: 4})}}));

  // no maximum
  assertThrows(() => builder.instantiate({m: {m: new WebAssembly.Memory({initial: 1})}}));
})();

(function TestMemoryGrowDetachBuffer() {
  print("TestMemoryGrowDetachBuffer");
  let memory = new WebAssembly.Memory({initial: 1, maximum: 5});
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "imported_mem");
  let instance = builder.instantiate({m: {imported_mem: memory}});
  let buffer = memory.buffer;
  assertEquals(kPageSize, buffer.byteLength);
  assertEquals(1, memory.grow(2));
  assertTrue(buffer !== memory.buffer);
  assertEquals(0, buffer.byteLength);
  assertEquals(3*kPageSize, memory.buffer.byteLength);
})();

(function TestInitialMemorySharedModule() {
  print("TestInitialMemorySharedModule");
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("m", "imported_mem");
  builder.addFunction('f', kSig_i_v)
      .addBody([
        kExprI32Const, 0x1d,                       // --
        kExprI32Const, 0x20,                       // --
        kExprI32StoreMem, 0, 0,  // --
        kExprI32Const, 0x1d,                       // --
        kExprI32LoadMem, 0, 0,  // --
      ])
      .exportFunc();

  // First instance load/store success
  var module = new WebAssembly.Module(builder.toBuffer());
  let memory1= new WebAssembly.Memory({initial: 1, maximum: 20});
  let instance1  = new WebAssembly.Instance(module, {m: {imported_mem: memory1}});
  assertEquals(0x20, instance1.exports.f());

  // Second instance should trap as it has no initial memory
  let memory2= new WebAssembly.Memory({initial: 0, maximum: 2});
  let instance2  = new WebAssembly.Instance(module, {m: {imported_mem: memory2}});
  assertTraps(kTrapMemOutOfBounds, () => instance2.exports.f());
})();
