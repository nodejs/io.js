// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-type-reflection

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestExternRefTableSetWithMultipleTypes() {
  print(arguments.callee.name);
  let table = new WebAssembly.Table({element: "externref", initial: 10});

  // Table should be initialized with undefined.
  assertEquals(undefined, table.get(1));
  let obj = {'hello' : 'world'};
  table.set(2, obj);
  assertSame(obj, table.get(2));
  table.set(3, 1234);
  assertEquals(1234, table.get(3));
  table.set(4, 123.5);
  assertEquals(123.5, table.get(4));
  table.set(5, undefined);
  assertEquals(undefined, table.get(5));
  // Overwrite entry 4, because null would otherwise be the default value.
  table.set(4, null);
  assertEquals(null, table.get(4));
  table.set(7, print);
  assertEquals(print, table.get(7));

  assertThrows(() => table.set(12), RangeError);
})();

(function TestImportExternRefTable() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  const table_index = builder.addImportedTable(
      "imp", "table", 3, 10, kWasmExternRef);
  builder.addFunction('get', kSig_r_v)
  .addBody([kExprI32Const, 0, kExprTableGet, table_index]);

  let table_ref = new WebAssembly.Table(
      { element: "externref", initial: 3, maximum: 10 });
  builder.instantiate({imp:{table: table_ref}});

  let table_func = new WebAssembly.Table(
      { element: "anyfunc", initial: 3, maximum: 10 });
  assertThrows(() => builder.instantiate({ imp: { table: table_func } }),
    WebAssembly.LinkError, /imported table does not match the expected type/);
})();

(function TestExternRefDropDeclarativeElementSegment() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  builder.addDeclarativeElementSegment([[kExprRefNull, kFuncRefCode]],
                                        kWasmFuncRef);
  builder.addFunction('drop', kSig_v_v)
      .addBody([kNumericPrefix, kExprElemDrop, 0])
      .exportFunc();
  const instance = builder.instantiate();

  // Counts as double-drop because declarative segments are dropped on
  // initialization and is therefore not expected to throw.
  instance.exports.drop();
})();

(function TestExternRefTableInitFromDeclarativeElementSegment() {
  print(arguments.callee.name);

  const builder = new WasmModuleBuilder();
  const table = builder.addTable(kWasmAnyFunc, 10);
  builder.addDeclarativeElementSegment([[kExprRefNull, kFuncRefCode]],
                                       kWasmFuncRef);
  builder.addFunction('init', kSig_v_v)
      .addBody([
        kExprI32Const, 0, kExprI32Const, 0, kExprI32Const, 1, kNumericPrefix,
        kExprTableInit, table.index, 0
      ])
      .exportFunc();
  const instance = builder.instantiate();

  assertTraps(kTrapElementSegmentOutOfBounds, () => instance.exports.init());
})();

(function TestTableInitializer() {
  print(arguments.callee.name);

  let test = function(is_nullable) {
    const builder = new WasmModuleBuilder();
    const sig = builder.addType(kSig_i_i);
    const func = builder.addFunction("func", kSig_i_i)
      .addBody([kExprLocalGet, 0]);
    builder.addTable(is_nullable ? wasmRefNullType(sig) : wasmRefType(sig),
                     10, 10, [kExprRefFunc, func.index]);
    builder.addFunction("main", kSig_i_ii)
      .addBody([kExprLocalGet, 1, kExprLocalGet, 0, kExprTableGet, 0,
                kExprCallRef, sig])
      .exportFunc();

    const instance = builder.instantiate();

    assertEquals(1, instance.exports.main(0, 1));
    assertEquals(33, instance.exports.main(5, 33));
  }

  test(true);
  test(false);
})();

(function TestExternRefTableConstructorWithDefaultValue() {
  print(arguments.callee.name);
  const testObject = {};
  const argument = { "element": "externref", "initial": 3 };
  const table = new WebAssembly.Table(argument, testObject);
  assertEquals(table.length, 3);
  assertEquals(table.get(0), testObject);
  assertEquals(table.get(1), testObject);
  assertEquals(table.get(2), testObject);
})();

function getDummy(val) {
  let builder = new WasmModuleBuilder();
  builder.addFunction('dummy', kSig_i_v)
      .addBody([kExprI32Const, val])
      .exportAs('dummy');
  return builder.instantiate().exports.dummy;
}

(function TestFuncRefTableConstructorWithDefaultValue() {
  print(arguments.callee.name);

  const expected = 6;
  let dummy = getDummy(expected);

  const argument = { "element": "anyfunc", "initial": 3 };
  const table = new WebAssembly.Table(argument, dummy);
  assertEquals(table.length, 3);
  assertEquals(table.get(0)(), expected);
  assertEquals(table.get(1)(), expected);
  assertEquals(table.get(2)(), expected);
})();

(function TestExternFuncTableSetWithoutValue() {
  print(arguments.callee.name);

  const expected = 6;
  const dummy = getDummy(expected);
  const argument = { "element": "anyfunc", "initial": 3 };
  const table = new WebAssembly.Table(argument, dummy);
  assertEquals(table.get(1)(), expected);
  table.set(1);
  assertEquals(table.get(1), null);
})();

(function TestExternRefTableSetWithoutValue() {
  print(arguments.callee.name);

  const testObject = {};
  const argument = { "element": "externref", "initial": 3 };
  const table = new WebAssembly.Table(argument, testObject);
  assertEquals(table.get(1), testObject);
  table.set(1);
  assertEquals(table.get(1), undefined);
})();

(function TestFunctionExternRefTableRoundtrip() {
  // Test that
  // - initialization, setting, and growing an externref table, and
  // - (imported) externref globals
  // preserve function references.
  print(arguments.callee.name);

  const js_function = function (i) { return i + 1; };
  const wasm_js_function = new WebAssembly.Function(
    {parameters:['i32', 'i32'], results: ['i32']},
    function(a, b) { return a * b; })

  let extern_type = wasmRefType(kWasmExternRef);

  let builder = new WasmModuleBuilder();
  let imported_global = builder.addImportedGlobal('m', 'n', extern_type, false);
  let global =
    builder.addGlobal(kWasmExternRef, true, false).exportAs('global');
  let table = builder.addTable(extern_type, 2, 10,
                               [kExprGlobalGet, imported_global])
  builder.addFunction(
      'setup', makeSig([extern_type, extern_type], []))
    .addBody([
      kExprLocalGet, 0, kExprGlobalSet, global.index,
      kExprI32Const, 1, kExprLocalGet, 0, kExprTableSet, table.index,
      kExprLocalGet, 1, kExprI32Const, 1, kNumericPrefix,
      kExprTableGrow, table.index, kExprDrop])
    .exportFunc();
  builder.addFunction('get', makeSig([kWasmI32], [kWasmExternRef]))
    .addBody([kExprLocalGet, 0, kExprTableGet, table.index])
    .exportFunc();
  let instance = builder.instantiate({m : {n : js_function}});

  instance.exports.setup(wasm_js_function, instance.exports.setup);

  assertEquals(instance.exports.global.value, wasm_js_function);
  assertEquals(instance.exports.get(0), js_function);
  assertEquals(instance.exports.get(1), wasm_js_function);
  assertEquals(instance.exports.get(2), instance.exports.setup);
})();

(function TestFunctionExternRefTableRoundtrip2() {
  // Test that initialization, setting, and growing an externref table in the JS
  // API preserves function references.
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();
  builder.addFunction('dummy', kSig_i_v)
      .addBody([kExprI32Const, 0])
      .exportAs('dummy');
  let instance = builder.instantiate();
  const js_function = function (i) { return i + 1; };
  const wasm_js_function = new WebAssembly.Function(
    {parameters:['i32', 'i32'], results: ['i32']},
    function(a, b) { return a * b; })

  const argument = { "element": "externref", "initial": 3 };
  const table = new WebAssembly.Table(argument, js_function);
  table.set(1, wasm_js_function);
  table.set(2, instance.exports.dummy);
  table.grow(1, wasm_js_function);
  assertEquals(table.get(0), js_function);
  assertEquals(table.get(1), wasm_js_function);
  assertEquals(table.get(2), instance.exports.dummy);
  assertEquals(table.get(3), wasm_js_function);
})();
