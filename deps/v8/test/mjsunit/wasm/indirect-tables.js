// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function AddFunctions(builder) {
  let sig_index = builder.addType(kSig_i_ii);
  let mul = builder.addFunction("mul", sig_index)
    .addBody([
      kExprLocalGet, 0,  // --
      kExprLocalGet, 1,  // --
      kExprI32Mul        // --
    ]);
  let add = builder.addFunction("add", sig_index)
    .addBody([
      kExprLocalGet, 0,  // --
      kExprLocalGet, 1,  // --
      kExprI32Add        // --
    ]);
  let sub = builder.addFunction("sub", sig_index)
    .addBody([
      kExprLocalGet, 0,  // --
      kExprLocalGet, 1,  // --
      kExprI32Sub        // --
    ]);
  return {mul: mul, add: add, sub: sub};
}

function js_div(a, b) { return (a / b) | 0; }

(function ExportedTableTest() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  let d = builder.addImport("q", "js_div", kSig_i_ii);
  let f = AddFunctions(builder);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprI32Const, 33,  // --
      kExprLocalGet, 0,   // --
      kExprLocalGet, 1,   // --
      kExprCallIndirect, 0, kTableZero])  // --
    .exportAs("main");

  f.add.exportAs("blarg");

  builder.setTableBounds(10, 10);
  let g = builder.addImportedGlobal("q", "base", kWasmI32);
  builder.addActiveElementSegment(0, [kExprGlobalGet, g],
                                  [f.mul.index, f.add.index, f.sub.index, d]);
  builder.addExportOfKind("table", kExternalTable, 0);

  let module = new WebAssembly.Module(builder.toBuffer());

  for (let i = 0; i < 5; i++) {
    print(" base = " + i);
    let instance = new WebAssembly.Instance(module, {q: {base: i, js_div: js_div}});
    main = instance.exports.main;
    let table = instance.exports.table;
    assertTrue(table instanceof WebAssembly.Table);
    assertEquals(10, table.length);
    for (let j = 0; j < i; j++) {
      assertSame(null, table.get(j));
    }
    let mul = table.get(i+0);
    let add = table.get(i+1);
    let sub = table.get(i+2);

    print("  mul=" + mul);
    print("  add=" + add);
    print("  sub=" + sub);
    assertEquals("function", typeof mul);
    assertEquals("function", typeof add);
    assertEquals("function", typeof sub);
    assertEquals(2, mul.length);
    assertEquals(2, add.length);
    assertEquals(2, sub.length);
    assertEquals(String(f.add.index), add.name);

    let exp_div = table.get(i+3);
    assertEquals("function", typeof exp_div);
    print("  js_div=" + exp_div);
    // Should have a new, wrapped version of the import.
    assertFalse(js_div == exp_div);


    for (let j = i + 4; j < 10; j++) {
      assertSame(null, table.get(j));
    }

    assertEquals(-33, mul(-11, 3));
    assertEquals(4444444, add(3333333, 1111111));
    assertEquals(-9999, sub(1, 10000));
    assertEquals(-44, exp_div(-88.1, 2));
  }
})();


(function ImportedTableTest1() {
  let kTableSize = 10;
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();

  let d = builder.addImport("q", "js_div", kSig_i_ii);
  let f = AddFunctions(builder);
  builder.setTableBounds(kTableSize, kTableSize);
  let g = builder.addImportedGlobal("q", "base", kWasmI32);
  builder.addActiveElementSegment(0, [kExprGlobalGet, g],
                                  [f.mul.index, f.add.index, f.sub.index, d]);
  builder.addExportOfKind("table", kExternalTable, 0);

  let m1 = new WebAssembly.Module(builder.toBuffer());

  var builder = new WasmModuleBuilder();

  builder.addImportedTable("r", "table", kTableSize, kTableSize);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprI32Const, 33,  // --
      kExprLocalGet, 0,   // --
      kExprLocalGet, 1,   // --
      kExprCallIndirect, 0, kTableZero])  // --
    .exportAs("main");

  let m2 = new WebAssembly.Module(builder.toBuffer());

  // Run 5 trials at different table bases.
  for (let i = 0; i < 5; i++) {
    print(" base = " + i);
    let i1 = new WebAssembly.Instance(m1, {q: {base: i, js_div: js_div}});
    let table = i1.exports.table;
    assertEquals(10, table.length);
    let i2 = new WebAssembly.Instance(m2, {r: {table: table}});
    let main = i2.exports.main;

    for (var j = 0; j < i; j++) {
      assertTraps(kTrapFuncSigMismatch, () => main(0, j));
      assertSame(null, table.get(j));
    }

    // mul
    assertEquals("function", typeof table.get(i+0));
    assertEquals(0, main(0, i+0));
    assertEquals(66, main(2, i+0));

    // add
    assertEquals("function", typeof table.get(i+1));
    assertEquals(33, main(0, i+1));
    assertEquals(38, main(5, i+1));

    // sub
    assertEquals("function", typeof table.get(i+2));
    assertEquals(32, main(1, i+2));
    assertEquals(28, main(5, i+2));

    // div
    assertEquals("function", typeof table.get(i+3));
    assertEquals(8, main(4, i+3));
    assertEquals(3, main(11, i+3));

    for (var j = i + 4; j < (kTableSize + 5); j++) {
      assertThrows(x => main(0, j));
      if (j < kTableSize) assertSame(null, table.get(j));
    }
  }
})();

(function ImportedTableTest2() {
  let kTableSize = 10;
  print(arguments.callee.name);

  var builder = new WasmModuleBuilder();

  let d = builder.addImport("q", "js_div", kSig_i_ii);
  builder.addImportedTable("q", "table", kTableSize, kTableSize);
  let g = builder.addImportedGlobal("q", "base", kWasmI32);
  let f = AddFunctions(builder);
  builder.addActiveElementSegment(0, [kExprGlobalGet, g],
                                  [f.mul.index, f.add.index, f.sub.index, d]);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprI32Const, 55,  // --
      kExprLocalGet, 0,   // --
      kExprLocalGet, 1,   // --
      kExprCallIndirect, 0, kTableZero])  // --
    .exportAs("main");

  let m2 = new WebAssembly.Module(builder.toBuffer());

  // Run 5 trials at different table bases.
  for (let i = 0; i < 5; i++) {
    print(" base = " + i);
    let table = new WebAssembly.Table({element: "anyfunc",
                                       initial: kTableSize,
                                       maximum: kTableSize});
    assertEquals(10, table.length);
    let i2 = new WebAssembly.Instance(m2, {q: {base: i, table: table,
                                               js_div: js_div}});
    let main = i2.exports.main;

    for (var j = 0; j < i; j++) {
      assertTraps(kTrapFuncSigMismatch, () => main(0, j));
      assertSame(null, table.get(j));
    }

    // mul
    assertEquals("function", typeof table.get(i+0));
    assertEquals(0, main(0, i+0));
    assertEquals(110, main(2, i+0));

    // add
    assertEquals("function", typeof table.get(i+1));
    assertEquals(55, main(0, i+1));
    assertEquals(60, main(5, i+1));

    // sub
    assertEquals("function", typeof table.get(i+2));
    assertEquals(54, main(1, i+2));
    assertEquals(50, main(5, i+2));

    // div
    assertEquals("function", typeof table.get(i+3));
    assertEquals(13, main(4, i+3));
    assertEquals(5, main(11, i+3));

    for (var j = i + 4; j < (kTableSize + 5); j++) {
      assertThrows(x => main(0, j));
      if (j < kTableSize) assertSame(null, table.get(j));
    }
  }
})();


(function CumulativeTest() {
  print(arguments.callee.name);

  let kTableSize = 10;
  let table = new WebAssembly.Table(
    {element: "anyfunc", initial: kTableSize, maximum: kTableSize});

  var builder = new WasmModuleBuilder();

  builder.addImportedTable("x", "table", kTableSize, kTableSize);
  let g = builder.addImportedGlobal("x", "base", kWasmI32);
  let sig_index = builder.addType(kSig_i_v);
  let f = builder.addFunction("f", sig_index)
    .addBody([
      kExprGlobalGet, g
    ]);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprCallIndirect, sig_index, kTableZero])  // --
    .exportAs("main");
  builder.addActiveElementSegment(0, [kExprGlobalGet, g], [f.index]);

  let module = new WebAssembly.Module(builder.toBuffer());

  for (var i = 0; i < kTableSize; i++) {
    print(" base = " + i);
    let instance = new WebAssembly.Instance(module, {x: {base: i, table: table}});

    for (var j = 0; j < kTableSize; j++) {
      let func = table.get(j);
      if (j > i) {
        assertSame(null, func);
        assertTraps(kTrapFuncSigMismatch, () => instance.exports.main(j));
      } else {
        assertEquals("function", typeof func);
        assertEquals(j, func());
        assertEquals(j, instance.exports.main(j));
      }
    }
  }
})();

(function TwoWayTest() {
  print(arguments.callee.name);
  let kTableSize = 3;

  // Module {m1} defines the table and exports it.
  var builder = new WasmModuleBuilder();
  builder.addType(kSig_i_i);
  builder.addType(kSig_i_ii);
  var sig_index1 = builder.addType(kSig_i_v);
  var f1 = builder.addFunction("f1", sig_index1)
    .addBody([kExprI32Const, 11]);

  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,   // --
      kExprCallIndirect, sig_index1, kTableZero])  // --
    .exportAs("main");

  builder.setTableBounds(kTableSize, kTableSize);
  builder.addActiveElementSegment(0, wasmI32Const(0), [f1.index]);
  builder.addExportOfKind("table", kExternalTable, 0);

  var m1 = new WebAssembly.Module(builder.toBuffer());

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

  var m2 = new WebAssembly.Module(builder.toBuffer());

  assertFalse(sig_index1 == sig_index2);

  var i1 = new WebAssembly.Instance(m1);
  var i2 = new WebAssembly.Instance(m2, {z: {table: i1.exports.table}});

  assertEquals(11, i1.exports.main(0));
  assertEquals(11, i2.exports.main(0));

  assertEquals(22, i1.exports.main(1));
  assertEquals(22, i2.exports.main(1));

  assertTraps(kTrapFuncSigMismatch, () => i1.exports.main(2));
  assertTraps(kTrapFuncSigMismatch, () => i2.exports.main(2));
  assertTraps(kTrapTableOutOfBounds, () => i1.exports.main(3));
  assertTraps(kTrapTableOutOfBounds, () => i2.exports.main(3));
})();

(function MismatchedTableSize() {
  print(arguments.callee.name);
  let kTableSize = 5;

  for (var expsize = 1; expsize < 4; expsize++) {
    for (var impsize = 1; impsize < 4; impsize++) {
      print(" expsize = " + expsize + ", impsize = " + impsize);
      var builder = new WasmModuleBuilder();
      builder.setTableBounds(expsize, expsize);
      builder.addExportOfKind("expfoo", kExternalTable, 0);

      let m1 = new WebAssembly.Module(builder.toBuffer());

      var builder = new WasmModuleBuilder();
      builder.addImportedTable("y", "impfoo", impsize, impsize);

      let m2 = new WebAssembly.Module(builder.toBuffer());

      var i1 = new WebAssembly.Instance(m1);

      // TODO(titzer): v8 currently requires import table size to match
      // export table size.
      var ffi = {y: {impfoo: i1.exports.expfoo}};
      if (expsize == impsize) {
        var i2 = new WebAssembly.Instance(m2, ffi);
      } else {
        assertThrows(() => new WebAssembly.Instance(m2, ffi));
      }
    }
  }
})();

(function TableGrowBoundsCheck() {
  print(arguments.callee.name);
  var kMaxSize = 30, kInitSize = 5;
  let table = new WebAssembly.Table({element: "anyfunc",
    initial: kInitSize, maximum: kMaxSize});
  var builder = new WasmModuleBuilder();
  builder.addImportedTable("x", "table", kInitSize, kMaxSize);
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {x: {base: 1, table: table}});

  for(var i = kInitSize; i < kMaxSize; i+=5) {
    assertEquals(i, table.length);
    for (var j = 0; j < i; j++) table.set(j, null);
    for (var j = 0; j < i; j++) assertEquals(null, table.get(j));
    assertThrows(() => table.set(i, null), RangeError);
    assertThrows(() => table.get(i), RangeError);
    assertEquals(i, table.grow(5));
  }
  assertEquals(30, table.length);
  assertThrows(() => table.grow(1), RangeError);
  assertThrows(() => table.set(kMaxSize, null), RangeError);
  assertThrows(() => table.get(kMaxSize), RangeError);
})();

(function CumulativeGrowTest() {
  print(arguments.callee.name);
  let table = new WebAssembly.Table({
    element: "anyfunc", initial: 10, maximum: 30});
  var builder = new WasmModuleBuilder();
  builder.addImportedTable("x", "table", 10, 30);

  let g = builder.addImportedGlobal("x", "base", kWasmI32);
  let sig_index = builder.addType(kSig_i_v);
  builder.addFunction("g", sig_index)
    .addBody([
      kExprGlobalGet, g
    ]);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprLocalGet, 0,
      kExprCallIndirect, sig_index, kTableZero])  // --
    .exportAs("main");
  builder.addActiveElementSegment(0, [kExprGlobalGet, g], [g]);
  let module = new WebAssembly.Module(builder.toBuffer());

  var instances = [];
  for (var i = 0; i < 10; i++) {
    print(" base = " + i);
    instances.push(new WebAssembly.Instance(
        module, {x: {base: i, table: table}}));
  }

  for (var j = 0; j < 10; j++) {
    let func = table.get(j);
    assertEquals("function", typeof func);
    assertEquals(j, func());
    assertEquals(j, instances[j].exports.main(j));
  }

  assertEquals(10, table.grow(10));

  // Verify that grow does not alter function behaviors
  for (var j = 0; j < 10; j++) {
    let func = table.get(j);
    assertEquals("function", typeof func);
    assertEquals(j, func());
    assertEquals(j, instances[j].exports.main(j));
  }

  let new_builder = new WasmModuleBuilder();
  new_builder.addExport("wasm", new_builder.addFunction("", kSig_v_v).addBody([]));
  new_builder.addImportedTable("x", "table", 20, 30);
  let new_module = new WebAssembly.Module(new_builder.toBuffer());
  let instance = new WebAssembly.Instance(new_module, {x: {table: table}});
  let new_func = instance.exports.wasm;

  for (var j = 10; j < 20; j++) {
    table.set(j, new_func);
    let func = table.get(j);
    assertEquals("function", typeof func);
    assertSame(new_func, table.get(j));
  }
  assertThrows(() => table.grow(11), RangeError);
})();


(function TestImportTooLarge() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addImportedTable("t", "t", 1, 2);

  // initial size is too large
  assertThrows(() => builder.instantiate({t: {t: new WebAssembly.Table(
    {element: "anyfunc", initial: 3, maximum: 3})}}), WebAssembly.LinkError);

  // maximum size is too large
  assertThrows(() => builder.instantiate({t: {t: new WebAssembly.Table(
    {element: "anyfunc", initial: 1, maximum: 4})}}), WebAssembly.LinkError);

  // no maximum
  assertThrows(() => builder.instantiate({t: {t: new WebAssembly.Table(
    {element: "anyfunc", initial: 1})}}), WebAssembly.LinkError);
})();

(function TableImportLargerThanCompiled() {
  print(arguments.callee.name);
  var kMaxSize = 30, kInitSize = 5;
  var builder = new WasmModuleBuilder();
  builder.addImportedTable("x", "table", 1, 35);
  let table = new WebAssembly.Table({element: "anyfunc",
    initial: kInitSize, maximum: kMaxSize});
  let module = new WebAssembly.Module(builder.toBuffer());
  let instance = new WebAssembly.Instance(module, {x: {base: 1, table: table}});
  for (var i = 0; i < kInitSize; ++i) table.set(i, null);
  for (var i = 0; i < kInitSize; ++i) assertEquals(null, table.get(i));
  assertThrows(() => table.set(kInitSize, null), RangeError);
})();

(function ModulesShareTableAndGrow() {
  print(arguments.callee.name);
  let module1 = (() => {
    let builder = new WasmModuleBuilder();
    builder.addImportedTable("x", "table", 1, 35);
    return new WebAssembly.Module(builder.toBuffer());
  })();
  let module2 = (() => {
    let builder = new WasmModuleBuilder();
    builder.addImportedTable("x", "table", 2, 40);
    return new WebAssembly.Module(builder.toBuffer());
  })();

  var kMaxSize = 30, kInitSize = 5;
  let table = new WebAssembly.Table({element: "anyfunc",
    initial: kInitSize, maximum: kMaxSize});
  let instance1 = new WebAssembly.Instance(
      module1, {x: {base: 1, table: table}});
  let instance2 = new WebAssembly.Instance(
      module2, {x: {base: 1, table: table}});

  for (var i = 0; i < kInitSize; ++i) table.set(i, null);
  for (var i = 0; i < kInitSize; ++i) assertEquals(null, table.get(i));
  assertThrows(() => table.set(kInitSize, null), RangeError);
  assertEquals(kInitSize, table.grow(5));
  for (var i = 0; i < 2*kInitSize; ++i) table.set(i, null);
  for (var i = 0; i < 2*kInitSize; ++i) assertEquals(null, table.get(i));
  assertThrows(() => table.set(2*kInitSize, null), RangeError);
  // Try to grow past imported maximum
  assertThrows(() => table.grow(21), RangeError);
})();

(function MultipleElementSegments() {
  let kTableSize = 10;
  print(arguments.callee.name);

  let mul = (a, b) => a * b;
  let add = (a, b) => a + b;
  let sub = (a, b) => a - b;

  // Test 1 to 3 segments in the elements section.
  // segment 1 sets [1, 2] to mul,
  // segment 2 sets [2, 3, 4] to add,
  // segment 3 sets [3, 4, 5, 6] to sub.
  for (let num_segments = 1; num_segments < 4; ++num_segments) {
    var builder = new WasmModuleBuilder();

    builder.setTableBounds(kTableSize, kTableSize);
    builder.addExportOfKind("table", kExternalTable, 0);
    let f = AddFunctions(builder);
    let indexes = [f.mul.index, f.add.index, f.sub.index];
    for (let i = 0; i < num_segments; ++i) {
      let offset = i + 1;
      let len = i + 2;
      let index = indexes[i];
      builder.addActiveElementSegment(0, wasmI32Const(offset),
                                      new Array(len).fill(index));
    }

    let instance = builder.instantiate();

    let table = instance.exports.table;
    assertEquals(kTableSize, table.length);

    for (let i = 0; i < num_segments; ++i) {
      let exp = i < 1 || i > 2 ? null : mul;
      if (num_segments > 1 && i >= 2 && i <= 4) exp = add;
      if (num_segments > 2 && i >= 3 && i <= 6) exp = sub;
      if (!exp) {
        assertSame(null, table.get(i));
      } else {
        assertEquals("function", typeof table.get(i));
    assertEquals(exp(6, 3), table.get(i)(6, 3));
      }
    }
  }
})();

(function InitImportedTableSignatureMismatch() {
  // instance0 exports a function table and a main function which indirectly
  // calls a function from the table.
  let builder0 = new WasmModuleBuilder();
  builder0.setName('module_0');
  let sig_index = builder0.addType(kSig_i_v);
  builder0.addFunction('main', kSig_i_i)
      .addBody([
        kExprLocalGet, 0,  // -
        kExprCallIndirect, sig_index, kTableZero
      ])
      .exportAs('main');
  builder0.setTableBounds(3, 3);
  builder0.addExportOfKind('table', kExternalTable);
  let module0 = new WebAssembly.Module(builder0.toBuffer());
  let instance0 = new WebAssembly.Instance(module0);

  // instance1 imports the table and adds a function to it.
  let builder1 = new WasmModuleBuilder();
  builder1.setName('module_1');
  builder1.addFunction('f', kSig_i_i).addBody([kExprLocalGet, 0]);
  builder1.addImportedTable('z', 'table');
  builder1.addActiveElementSegment(0, wasmI32Const(0), [0]);
  let module1 = new WebAssembly.Module(builder1.toBuffer());
  let instance1 =
      new WebAssembly.Instance(module1, {z: {table: instance0.exports.table}});

  // Calling the main method on instance0 should fail, because the signature of
  // the added function does not match.
  assertThrows(
      () => instance0.exports.main(0), WebAssembly.RuntimeError,
      /signature mismatch/);
})();

(function IndirectCallIntoOtherInstance() {
  print(arguments.callee.name);

  var mem_1 = new WebAssembly.Memory({initial: 1});
  var mem_2 = new WebAssembly.Memory({initial: 1});
  var view_1 = new Int32Array(mem_1.buffer);
  var view_2 = new Int32Array(mem_2.buffer);
  view_1[0] = 1;
  view_2[0] = 1000;

  let builder = new WasmModuleBuilder();
  let sig = builder.addType(kSig_i_v);
  builder.addFunction('main', kSig_i_i)
    .addBody([kExprLocalGet, 0, kExprCallIndirect, sig, kTableZero])
    .exportAs('main');
  builder.addImportedMemory('', 'memory', 1);

  builder.setTableBounds(1, 1);
  builder.addExportOfKind('table', kExternalTable);

  let module1 = new WebAssembly.Module(builder.toBuffer());
  let instance1 = new WebAssembly.Instance(module1, {'':{memory:mem_1}});

  builder = new WasmModuleBuilder();
  builder.addFunction('main', kSig_i_v)
         .addBody([kExprI32Const, 0, kExprI32LoadMem, 0, 0]);
  builder.addImportedTable('', 'table');
  builder.addActiveElementSegment(0, wasmI32Const(0), [0]);
  builder.addImportedMemory('', 'memory', 1);


  let module2 = new WebAssembly.Module(builder.toBuffer());
  let instance2 = new WebAssembly.Instance(module2, {
    '': {
      table: instance1.exports.table,
      memory: mem_2
    }
  });

  assertEquals(instance1.exports.main(0), 1000);
})();


(function ImportedFreestandingTable() {
  print(arguments.callee.name);

  function forceGc() {
    gc();
    gc();
    gc();
  }

  function setup() {
    let builder = new WasmModuleBuilder();
    let sig = builder.addType(kSig_i_v);
    builder.addFunction('main', kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprCallIndirect, sig, kTableZero])
      .exportAs('main');

    builder.addImportedTable('', 'table');

    let module1 = new WebAssembly.Module(builder.toBuffer());
    let table = new WebAssembly.Table({initial:2, element:'anyfunc'});
    let instance1 = new WebAssembly.Instance(module1, {'':{table: table}});

    builder = new WasmModuleBuilder();
    builder.addExport('theImport', builder.addImport('', 'callout', kSig_i_v));
    builder.addImportedMemory('', 'memory', 1);
    builder.addFunction('main', kSig_i_v)
      .addBody([
        kExprCallFunction, 0,
        kExprI32Const, 0, kExprI32LoadMem, 0, 0,
        kExprI32Add
      ]).exportAs('main');

    let mem = new WebAssembly.Memory({initial:1});
    let view = new Int32Array(mem.buffer);
    view[0] = 4;

    let module2 = new WebAssembly.Module(builder.toBuffer());
    let instance2 = new WebAssembly.Instance(module2, {
      '': {
        callout: () => {
          forceGc();
          return 3;
        },
        'memory': mem
      }
    });
    table.set(0, instance2.exports.main);
    table.set(1, instance2.exports.theImport);
    return instance1;
  }

  function test(variant, expectation) {
    var instance = setup();
    forceGc();
    assertEquals(expectation, instance.exports.main(variant));
  }

  // 0 indirectly calls the wasm function that calls the import,
  // 1 does the same but for the exported import.
  test(0, 7);
  test(1, 3);
})();


(function ImportedWasmFunctionPutIntoTable() {
  print(arguments.callee.name);

  let wasm_mul = (() => {
    let builder = new WasmModuleBuilder();
    builder.addFunction("mul", kSig_i_ii)
      .addBody(
        [kExprLocalGet, 0,
         kExprLocalGet, 1,
         kExprI32Mul])
      .exportFunc();
    return builder.instantiate().exports.mul;
  })();

  let builder = new WasmModuleBuilder();

  let j = builder.addImport("q", "js_div", kSig_i_ii);
  let w = builder.addImport("q", "wasm_mul", kSig_i_ii);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprI32Const, 33,  // --
      kExprLocalGet, 0,   // --
      kExprLocalGet, 1,   // --
      kExprCallIndirect, 0, kTableZero])  // --
    .exportAs("main");

  builder.setTableBounds(10, 10);
  let g = builder.addImportedGlobal("q", "base", kWasmI32);
  builder.addActiveElementSegment(0, [kExprGlobalGet, g], [j, w]);

  let module = new WebAssembly.Module(builder.toBuffer());
  for (var i = 0; i < 5; i++) {
    let instance = new WebAssembly.Instance(module, {q: {base: i, js_div: js_div, wasm_mul: wasm_mul}});
    let j = i + 1;

    assertThrows(() => {instance.exports.main(j, i-1)});
    assertEquals((33/j)|0, instance.exports.main(j, i+0));
    assertEquals((33*j)|0, instance.exports.main(j, i+1));
    assertThrows(() => {instance.exports.main(j, i+2)});
  }

})();

(function ImportedWasmFunctionPutIntoImportedTable() {
  print(arguments.callee.name);

  let kTableSize = 10;

  let wasm_mul = (() => {
    let builder = new WasmModuleBuilder();
    builder.addFunction("mul", kSig_i_ii)
      .addBody(
        [kExprLocalGet, 0,
         kExprLocalGet, 1,
         kExprI32Mul])
      .exportFunc();
    return builder.instantiate().exports.mul;
  })();

  let table = new WebAssembly.Table({element: "anyfunc",
                                     initial: kTableSize,
                                     maximum: kTableSize});

  let builder = new WasmModuleBuilder();

  let j = builder.addImport("q", "js_div", kSig_i_ii);
  let w = builder.addImport("q", "wasm_mul", kSig_i_ii);
  builder.addImportedTable("q", "table", kTableSize, kTableSize);
  builder.addFunction("main", kSig_i_ii)
    .addBody([
      kExprI32Const, 44,  // --
      kExprLocalGet, 0,   // --
      kExprLocalGet, 1,   // --
      kExprCallIndirect, 0, kTableZero])  // --
    .exportAs("main");

  let g = builder.addImportedGlobal("q", "base", kWasmI32);
  builder.addActiveElementSegment(0, [kExprGlobalGet, g], [j, w]);

  let module = new WebAssembly.Module(builder.toBuffer());
  for (var i = 0; i < 5; i++) {
    let instance = new WebAssembly.Instance(
      module, {q: {base: i, js_div: js_div, wasm_mul: wasm_mul, table: table}});
    let j = i + 1;

    assertEquals((44/j)|0, instance.exports.main(j, i+0));
    assertEquals((44*j)|0, instance.exports.main(j, i+1));
    assertThrows(() => {instance.exports.main(j, i+2)});
  }
})();

(function ExportedFunctionsImportedOrder() {
  print(arguments.callee.name);

  let i1 = (() => {
    let builder = new WasmModuleBuilder();
    builder.addFunction("f1", kSig_i_v)
      .addBody(
        [kExprI32Const, 1])
      .exportFunc();
    builder.addFunction("f2", kSig_i_v)
      .addBody(
        [kExprI32Const, 2])
      .exportFunc();
    return builder.instantiate();
  })();

  let i2 = (() => {
    let builder = new WasmModuleBuilder();
    builder.addTable(kWasmAnyFunc, 4);
    builder.addImport("q", "f2", kSig_i_v);
    builder.addImport("q", "f1", kSig_i_v);
    builder.addFunction("main", kSig_i_i)
      .addBody([
        kExprLocalGet, 0,
        kExprCallIndirect, 0, kTableZero
      ])
      .exportFunc();
    builder.addActiveElementSegment(0, wasmI32Const(0), [0, 1, 1, 0]);

    return builder.instantiate({q: {f2: i1.exports.f2, f1: i1.exports.f1}});
  })();

  assertEquals(2, i2.exports.main(0));
  assertEquals(1, i2.exports.main(1));
  assertEquals(1, i2.exports.main(2));
  assertEquals(2, i2.exports.main(3));
})();

(function IndirectCallsToImportedFunctions() {
  print(arguments.callee.name);

  let module = (() => {
    let builder = new WasmModuleBuilder();
    builder.addMemory(1, 1);
    builder.addFunction("f", kSig_i_v)
      .addBody([
        kExprI32Const, 0,
        kExprI32LoadMem, 0, 4,
      ])
      .exportFunc();
    builder.exportMemoryAs("memory");
    return new WebAssembly.Module(builder.toBuffer());
  })();

  function setMemI32(instance, offset, val) {
    var array = new Int32Array(instance.exports.memory.buffer);
    array[offset/4] = val;
  }

  function makeFun(val) {
    let instance = new WebAssembly.Instance(module);
    setMemI32(instance, 0, 2000000);
    setMemI32(instance, 4, val);
    setMemI32(instance, 8, 3000000);
    return instance.exports.f;
  }

  let f300 = makeFun(300);
  let f100 = makeFun(100);
  let f200 = makeFun(200);

  let main = (() => {
    let builder = new WasmModuleBuilder();
    builder.addMemory(1, 1);
    builder.addTable(kWasmAnyFunc, 4);
    builder.addImport("q", "f1", kSig_i_v);
    builder.addImport("q", "f2", kSig_i_v);
    builder.addImport("q", "f3", kSig_i_v);
    builder.addFunction("f", kSig_i_v)
      .addBody([
        kExprI32Const, 8,
        kExprI32LoadMem, 0, 0,
      ]);
    builder.addFunction("main", kSig_i_i)
      .addBody([
        kExprLocalGet, 0,
        kExprCallIndirect, 0, kTableZero
      ])
      .exportFunc();
    builder.exportMemoryAs("memory");
    builder.addActiveElementSegment(0, wasmI32Const(0), [0, 1, 2, 3]);
    var instance = builder.instantiate({q: {f1: f100, f2: f200, f3: f300}});
    setMemI32(instance, 0, 5000000);
    setMemI32(instance, 4, 6000000);
    setMemI32(instance, 8, 400);
    return instance.exports.main;
  })();

  assertEquals(100, main(0));
  assertEquals(200, main(1));
  assertEquals(300, main(2));
  assertEquals(400, main(3));
})();

(function TestNonImportedGlobalInElementSegment() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  let global = builder.addGlobal(kWasmFuncRef, true, false,
                                 [kExprRefNull, kFuncRefCode]);
  let table = builder.addTable(kWasmFuncRef, 10, 10);
  builder.addActiveElementSegment(
      table.index, wasmI32Const(0),
      [[kExprGlobalGet, global.index]], kWasmFuncRef);
  builder.addExportOfKind("table", kExternalTable, table.index);

  assertThrows(
    () => builder.instantiate(), WebAssembly.CompileError,
    /mutable globals cannot be used in constant expressions/);
})();
