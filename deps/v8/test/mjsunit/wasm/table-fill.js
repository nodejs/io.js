// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function dummy_func(val) {
  let builder = new WasmModuleBuilder();
  builder.addFunction('dummy', kSig_i_v)
      .addBody([kExprI32Const, val])
      .exportAs('dummy');
  return builder.instantiate().exports.dummy;
}

let kSig_v_iri = makeSig([kWasmI32, kWasmExternRef, kWasmI32], []);
let kSig_v_iai = makeSig([kWasmI32, kWasmAnyFunc, kWasmI32], []);
let kSig_r_i = makeSig([kWasmI32], [kWasmExternRef]);

const builder = new WasmModuleBuilder();
const size = 10;
const maximum = size;
const import_ref =
    builder.addImportedTable('imp', 'table_ref', size, maximum, kWasmExternRef);
const import_func =
    builder.addImportedTable('imp', 'table_func', size, maximum, kWasmAnyFunc);
const internal_ref = builder.addTable(kWasmExternRef, size, maximum).index;
const internal_func = builder.addTable(kWasmAnyFunc, size, maximum).index;

// Add fill and get functions for the externref tables.
for (index of [import_ref, internal_ref]) {
  builder.addFunction(`fill${index}`, kSig_v_iri)
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kNumericPrefix,
        kExprTableFill, index
      ])
      .exportFunc();

  builder.addFunction(`get${index}`, kSig_r_i)
      .addBody([kExprLocalGet, 0, kExprTableGet, index])
      .exportFunc();
}

// Add fill and call functions for the anyfunc tables.
const sig_index = builder.addType(kSig_i_v);
for (index of [import_func, internal_func]) {
  builder.addFunction(`fill${index}`, kSig_v_iai)
      .addBody([
        kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2, kNumericPrefix,
        kExprTableFill, index
      ])
      .exportFunc();

  builder.addFunction(`call${index}`, kSig_i_i)
      .addBody([kExprLocalGet, 0, kExprCallIndirect, sig_index, index])
      .exportFunc();
}

const table_ref =
    new WebAssembly.Table({element: 'externref', initial: size, maximum: maximum});
const table_func = new WebAssembly.Table(
    {element: 'anyfunc', initial: size, maximum: maximum});

const instance =
    builder.instantiate({imp: {table_ref: table_ref, table_func: table_func}});

function checkExternRefTable(getter, start, count, value) {
  for (i = 0; i < count; ++i) {
    assertEquals(value, getter(start + i));
  }
}

(function testExternRefTableIsUninitialized() {
  print(arguments.callee.name);

  checkExternRefTable(instance.exports[`get${import_ref}`], 0, size, undefined);
  checkExternRefTable(instance.exports[`get${internal_ref}`], 0, size, null);
})();

(function testExternRefTableFill() {
  print(arguments.callee.name);
  // Fill table and check the content.
  let start = 1;
  let value = {foo: 23};
  let count = 3;
  instance.exports[`fill${import_ref}`](start, value, count);
  checkExternRefTable(instance.exports[`get${import_ref}`], start, count, value);
  value = 'foo';
  instance.exports[`fill${internal_ref}`](start, value, count);
  checkExternRefTable(instance.exports[`get${internal_ref}`], start, count, value);
})();

(function testExternRefTableFillOOB() {
  print(arguments.callee.name);
  // Fill table out-of-bounds, check if the table wasn't altered.
  let start = 7;
  let value = {foo: 27};
  // {maximum + 4} elements definitely don't fit into the table.
  let count = maximum + 4;
  assertTraps(
      kTrapTableOutOfBounds,
      () => instance.exports[`fill${import_ref}`](start, value, count));
  checkExternRefTable(
      instance.exports[`get${import_ref}`], start, size - start, undefined);

  value = 45;
  assertTraps(
      kTrapTableOutOfBounds,
      () => instance.exports[`fill${internal_ref}`](start, value, count));
  checkExternRefTable(
      instance.exports[`get${internal_ref}`], start, size - start, null);
})();

(function testExternRefTableFillOOBCountZero() {
  print(arguments.callee.name);
  // Fill 0 elements at an oob position. This should trap.
  let start = size + 32;
  let value = 'bar';
  assertTraps(
      kTrapTableOutOfBounds,
      () => instance.exports[`fill${import_ref}`](start, value, 0));
  assertTraps(
      kTrapTableOutOfBounds,
      () => instance.exports[`fill${internal_ref}`](start, value, 0));
})();

function checkAnyFuncTable(call, start, count, value) {
  for (i = 0; i < count; ++i) {
    if (value) {
      assertEquals(value, call(start + i));
    } else {
      assertTraps(kTrapFuncSigMismatch, () => call(start + i));
    }
  }
}

(function testExternRefTableIsUninitialized() {
  print(arguments.callee.name);
  // Check that the table is uninitialized.
  checkAnyFuncTable(instance.exports[`call${import_func}`], 0, size);
  checkAnyFuncTable(instance.exports[`call${internal_func}`], 0, size);
})();

(function testAnyFuncTableFill() {
  print(arguments.callee.name);
  // Fill and check the result.
  let start = 1;
  let value = 44;
  let count = 3;
  instance.exports[`fill${import_func}`](start, dummy_func(value), count);
  checkAnyFuncTable(
      instance.exports[`call${import_func}`], start, count, value);
  value = 21;
  instance.exports[`fill${internal_func}`](start, dummy_func(value), count);
  checkAnyFuncTable(
      instance.exports[`call${internal_func}`], start, count, value);
})();

(function testAnyFuncTableFillOOB() {
  print(arguments.callee.name);
  // Fill table out-of-bounds, check if the table wasn't altered.
  let start = 7;
  let value = 38;
  // {maximum + 4} elements definitely don't fit into the table.
  let count = maximum + 4;
  assertTraps(
      kTrapTableOutOfBounds,
      () => instance.exports[`fill${import_func}`](
          start, dummy_func(value), count));
  checkAnyFuncTable(
      instance.exports[`call${import_func}`], start, size - start, null);

  value = 46;
  assertTraps(
      kTrapTableOutOfBounds,
      () => instance.exports[`fill${internal_func}`](
          start, dummy_func(value), count));
  checkAnyFuncTable(
      instance.exports[`call${internal_func}`], start, size - start, null);
})();

(function testAnyFuncTableFillOOBCountZero() {
  print(arguments.callee.name);
  // Fill 0 elements at an oob position. This should trap.
  let start = size + 32;
  let value = dummy_func(33);
  assertTraps(
      kTrapTableOutOfBounds,
      () => instance.exports[`fill${import_func}`](start, null, 0));
  assertTraps(
      kTrapTableOutOfBounds,
      () => instance.exports[`fill${internal_func}`](start, null, 0));

  // Check that table.fill at position `size` is still valid.
  instance.exports[`fill${import_func}`](size, null, 0);
  instance.exports[`fill${internal_func}`](size, null, 0);
})();
