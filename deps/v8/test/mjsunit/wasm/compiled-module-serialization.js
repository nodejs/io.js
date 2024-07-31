// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Force TurboFan code for serialization.
// Flags: --expose-wasm --allow-natives-syntax --expose-gc --no-liftoff
// Flags: --no-wasm-lazy-compilation

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function SerializeAndDeserializeModule() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addImportedMemory("", "memory", 1);
  var kSig_v_i = makeSig([kWasmI32], []);
  var signature = builder.addType(kSig_v_i);
  builder.addImport("", "some_value", kSig_i_v);
  builder.addImport("", "writer", signature);

  builder.addFunction("main", kSig_i_i)
    .addBody([
      kExprLocalGet, 0,
      kExprI32LoadMem, 0, 0,
      kExprI32Const, 1,
      kExprCallIndirect, signature, kTableZero,
      kExprLocalGet,0,
      kExprI32LoadMem,0, 0,
      kExprCallFunction, 0,
      kExprI32Add
    ]).exportFunc();

  // writer(mem[i]);
  // return mem[i] + some_value();
  builder.addFunction("_wrap_writer", signature)
    .addBody([
      kExprLocalGet, 0,
      kExprCallFunction, 1]);
  builder.appendToTable([2, 3]);

  var wire_bytes = builder.toBuffer();
  var module = new WebAssembly.Module(wire_bytes);
  var mem_1 = new WebAssembly.Memory({initial: 1});
  var view_1 = new Int32Array(mem_1.buffer);
  view_1[0] = 42;
  var outval_1;
  var i1 = new WebAssembly.Instance(module, {"":
                                             {some_value: () => 1,
                                              writer: (x) => outval_1 = x ,
                                              memory: mem_1}
                                            });

  assertEquals(43, i1.exports.main(0));

  assertEquals(42, outval_1);
  var buff = %SerializeWasmModule(module);
  module = null;
  gc();
  module = %DeserializeWasmModule(buff, wire_bytes);

  var mem_2 = new WebAssembly.Memory({initial: 2});
  var view_2 = new Int32Array(mem_2.buffer);

  view_2[0] = 50;

  var outval_2;
  var i2 = new WebAssembly.Instance(module, {"":
                                             {some_value: () => 1,
                                              writer: (x) => outval_2 = x ,
                                              memory: mem_2}
                                            });

  assertEquals(51, i2.exports.main(0));

  assertEquals(50, outval_2);
  // The instances don't share memory through deserialization.
  assertEquals(43, i1.exports.main(0));
})();

(function DeserializeInvalidObject() {
  print(arguments.callee.name);
  const invalid_buffer = new ArrayBuffer(10);
  const invalid_buffer_view = new Uint8Array(10);

  module = %DeserializeWasmModule(invalid_buffer, invalid_buffer_view);
  assertEquals(module, undefined);
})();

(function RelationBetweenModuleAndClone() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("main", kSig_i_v)
    .addBody([kExprI32Const, 42])
    .exportFunc();

  var wire_bytes = builder.toBuffer();
  var compiled_module = new WebAssembly.Module(wire_bytes);
  var serialized = %SerializeWasmModule(compiled_module);
  var clone = %DeserializeWasmModule(serialized, wire_bytes);

  assertNotNull(clone);
  assertFalse(clone == undefined);
  assertFalse(clone == compiled_module);
  assertEquals(clone.constructor, compiled_module.constructor);
})();

(function SerializeWrappersWithSameSignature() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("main", kSig_i_v)
    .addBody([kExprI32Const, 42])
    .exportFunc();
  builder.addFunction("main_same_signature", kSig_i_v)
    .addBody([kExprI32Const, 23])
    .exportFunc();

  var wire_bytes = builder.toBuffer();
  var compiled_module = new WebAssembly.Module(wire_bytes);
  var serialized = %SerializeWasmModule(compiled_module);
  var clone = %DeserializeWasmModule(serialized, wire_bytes);

  assertNotNull(clone);
  assertFalse(clone == undefined);
  assertFalse(clone == compiled_module);
  assertEquals(clone.constructor, compiled_module.constructor);
})();

(function SerializeAfterInstantiation() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addFunction("main", kSig_i_v)
    .addBody([kExprI32Const, 42])
    .exportFunc();

  var wire_bytes = builder.toBuffer()
  var compiled_module = new WebAssembly.Module(wire_bytes);
  var instance1 = new WebAssembly.Instance(compiled_module);
  var instance2 = new WebAssembly.Instance(compiled_module);
  var serialized = %SerializeWasmModule(compiled_module);
  var clone = %DeserializeWasmModule(serialized, wire_bytes);

  assertNotNull(clone);
  assertFalse(clone == undefined);
  assertFalse(clone == compiled_module);
  assertEquals(clone.constructor, compiled_module.constructor);
  var instance3 = new WebAssembly.Instance(clone);
  assertFalse(instance3 == undefined);
})();


(function SerializeAfterInstantiationWithMemory() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addImportedMemory("", "memory", 1);
  builder.addFunction("main", kSig_i_v)
    .addBody([kExprI32Const, 42])
    .exportFunc();

  var wire_bytes = builder.toBuffer()
  var compiled_module = new WebAssembly.Module(wire_bytes);
  var mem_1 = new WebAssembly.Memory({initial: 1});
  var ffi =  {"":{memory:mem_1}};
  var instance1 = new WebAssembly.Instance(compiled_module, ffi);
  var serialized = %SerializeWasmModule(compiled_module);
  var clone = %DeserializeWasmModule(serialized, wire_bytes);

  assertNotNull(clone);
  assertFalse(clone == undefined);
  assertFalse(clone == compiled_module);
  assertEquals(clone.constructor, compiled_module.constructor);
  var instance2 = new WebAssembly.Instance(clone, ffi);
  assertFalse(instance2 == undefined);
})();

(function GlobalsArePrivateBetweenClones() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true, false);
  builder.addFunction("read", kSig_i_v)
    .addBody([
      kExprGlobalGet, 0])
    .exportFunc();

  builder.addFunction("write", kSig_v_i)
    .addBody([
      kExprLocalGet, 0,
      kExprGlobalSet, 0])
    .exportFunc();

  var wire_bytes = builder.toBuffer();
  var module = new WebAssembly.Module(wire_bytes);
  var i1 = new WebAssembly.Instance(module);
  // serialize and replace module
  var buff = %SerializeWasmModule(module);
  var module_clone = %DeserializeWasmModule(buff, wire_bytes);
  var i2 = new WebAssembly.Instance(module_clone);
  i1.exports.write(1);
  i2.exports.write(2);
  assertEquals(1, i1.exports.read());
  assertEquals(2, i2.exports.read());
})();

(function SharedTableTest() {
  print(arguments.callee.name);
  let kTableSize = 3;
  var sig_index1;

  function MakeTableExportingModule(constant) {
    // A module that defines a table and exports it.
    var builder = new WasmModuleBuilder();
    builder.addType(kSig_i_i);
    builder.addType(kSig_i_ii);
    sig_index1 = builder.addType(kSig_i_v);
    var f1 = builder.addFunction("f1", sig_index1)
        .addBody([kExprI32Const, constant]);

    builder.addFunction("main", kSig_i_ii)
      .addBody([
        kExprLocalGet, 0,   // --
        kExprCallIndirect, sig_index1, kTableZero])  // --
      .exportAs("main");

    builder.setTableBounds(kTableSize, kTableSize);
    builder.addActiveElementSegment(0, wasmI32Const(0), [f1.index]);
    builder.addExportOfKind("table", kExternalTable, 0);

    return new WebAssembly.Module(builder.toBuffer());
  }
  var m1 = MakeTableExportingModule(11);

  // Module {m2} imports the table and adds {f2}.
  var builder = new WasmModuleBuilder();
  builder.addType(kSig_i_ii);
  var sig_index2 = builder.addType(kSig_i_v);
  var f2 = builder.addFunction("f2", sig_index2)
    .addBody([kExprI32Const, 22]);

  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,   // --
      kExprCallIndirect, sig_index2, kTableZero])  // --
    .exportAs("main");

  builder.addImportedTable("z", "table", kTableSize, kTableSize);
  builder.addActiveElementSegment(0, wasmI32Const(1), [f2.index]);
  var m2_bytes = builder.toBuffer();
  var m2 = new WebAssembly.Module(m2_bytes);

  assertFalse(sig_index1 == sig_index2);

  var i1 = new WebAssembly.Instance(m1);
  var i2 = new WebAssembly.Instance(m2, {z: {table: i1.exports.table}});

  var serialized_m2 = %SerializeWasmModule(m2);
  var m2_clone = %DeserializeWasmModule(serialized_m2, m2_bytes);

  var m3 = MakeTableExportingModule(33);
  var i3 = new WebAssembly.Instance(m3);
  var i2_prime = new WebAssembly.Instance(m2_clone,
                                          {z: {table: i3.exports.table}});

  assertEquals(11, i1.exports.main(0));
  assertEquals(11, i2.exports.main(0));

  assertEquals(22, i1.exports.main(1));
  assertEquals(22, i2.exports.main(1));

  assertEquals(33, i3.exports.main(0));
  assertEquals(33, i2_prime.exports.main(0));

  assertThrows(() => i1.exports.main(2));
  assertThrows(() => i2.exports.main(2));
  assertThrows(() => i1.exports.main(3));
  assertThrows(() => i2.exports.main(3));
})();

(function StackOverflowAfterSerialization() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  var fun = builder.addFunction('main', kSig_v_v);
  fun.addBody([kExprCallFunction, fun.index]);
  fun.exportFunc();

  var wire_bytes = builder.toBuffer();
  var module = new WebAssembly.Module(wire_bytes);
  var buffer = %SerializeWasmModule(module);
  module = %DeserializeWasmModule(buffer, wire_bytes);
  var instance = new WebAssembly.Instance(module);

  assertThrows(instance.exports.main, RangeError);
})();

(function TrapAfterDeserialization() {
  print(arguments.callee.name);
  function GenerateSerializedModule() {
    const builder = new WasmModuleBuilder();
    builder.addMemory(1, 1);
    builder.addFunction('main', kSig_i_i)
        .addBody([kExprLocalGet, 0, kExprI32LoadMem, 0, 0])
        .exportFunc();
    const wire_bytes = builder.toBuffer();
    const module = new WebAssembly.Module(wire_bytes);
    const buffer = %SerializeWasmModule(module);
    return [wire_bytes, buffer];
  }
  const [wire_bytes, buffer] = GenerateSerializedModule();
  module = %DeserializeWasmModule(buffer, wire_bytes);
  const instance = new WebAssembly.Instance(module);

  assertEquals(0, instance.exports.main(0));
  assertEquals(0, instance.exports.main(kPageSize - 4));
  assertTraps(
      kTrapMemOutOfBounds, _ => instance.exports.main(kPageSize - 3));
})();

(function DirectCallAfterSerialization() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  var fun1 = builder.addFunction('fun1', kSig_i_v)
      .addBody([kExprI32Const, 23]);
  var fun2 = builder.addFunction('fun2', kSig_i_v)
      .addBody([kExprI32Const, 42]);
  builder.addFunction('main', kSig_i_v)
      .addBody([kExprCallFunction, fun1.index,
                kExprCallFunction, fun2.index,
                kExprI32Add])
      .exportFunc();

  var wire_bytes = builder.toBuffer();
  var module = new WebAssembly.Module(wire_bytes);
  var buffer = %SerializeWasmModule(module);
  module = %DeserializeWasmModule(buffer, wire_bytes);
  var instance = new WebAssembly.Instance(module);

  assertEquals(65, instance.exports.main());
})();

(function ImportCallAfterSerialization() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  var fun_import = builder.addImport("", "my_import", kSig_i_v);
  var fun = builder.addFunction('fun', kSig_i_v)
      .addBody([kExprI32Const, 23]);
  builder.addFunction('main', kSig_i_v)
      .addBody([kExprCallFunction, fun.index,
                kExprCallFunction, fun_import,
                kExprI32Add])
      .exportFunc();

  var wire_bytes = builder.toBuffer();
  var module = new WebAssembly.Module(wire_bytes);
  var buffer = %SerializeWasmModule(module);
  module = %DeserializeWasmModule(buffer, wire_bytes);
  var instance = new WebAssembly.Instance(module, {"": {my_import: () => 42 }});

  assertEquals(65, instance.exports.main());
})();

(function BranchTableAfterSerialization() {
  print(arguments.callee.name);
  const builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_i_i)
      .addBody([kExprBlock, kWasmVoid,
                  kExprBlock, kWasmVoid,
                    kExprBlock, kWasmVoid,
                      kExprBlock, kWasmVoid,
                        kExprBlock, kWasmVoid,
                          kExprBlock, kWasmVoid,
                            kExprBlock, kWasmVoid,
                              kExprLocalGet, 0,
                              kExprBrTable, 6, 0, 1, 2, 3, 4, 5, 6,
                            kExprEnd,
                            kExprI32Const, 3,
                            kExprReturn,
                          kExprEnd,
                          kExprI32Const, 7,
                          kExprReturn,
                        kExprEnd,
                        kExprI32Const, 9,
                        kExprReturn,
                      kExprEnd,
                      kExprI32Const, 11,
                      kExprReturn,
                    kExprEnd,
                    kExprI32Const, 23,
                    kExprReturn,
                  kExprEnd,
                  kExprI32Const, 35,
                  kExprReturn,
                kExprEnd,
                kExprI32Const, 42,
                kExprReturn])
      .exportFunc();

  var wire_bytes = builder.toBuffer();
  var module = new WebAssembly.Module(wire_bytes);
  var buffer = %SerializeWasmModule(module);
  module = %DeserializeWasmModule(buffer, wire_bytes);
  var instance = new WebAssembly.Instance(module);

  assertEquals(3, instance.exports.main(0));
  assertEquals(7, instance.exports.main(1));
  assertEquals(9, instance.exports.main(2));
  assertEquals(11, instance.exports.main(3));
  assertEquals(23, instance.exports.main(4));
  assertEquals(35, instance.exports.main(5));
  assertEquals(42, instance.exports.main(6));
  assertEquals(42, instance.exports.main(9));
})();
