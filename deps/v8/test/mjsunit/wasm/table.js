// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

// Basic tests.

const outOfUint32RangeValue = 1e12;
const kV8MaxWasmTableSize = 10000000;

function assertTableIsValid(table, length) {
  assertSame(WebAssembly.Table.prototype, table.__proto__);
  assertSame(WebAssembly.Table, table.constructor);
  assertTrue(table instanceof Object);
  assertTrue(table instanceof WebAssembly.Table);
  assertEquals(length, table.length);
}

(function TestConstructor() {
  assertTrue(WebAssembly.Table instanceof Function);
  assertSame(WebAssembly.Table, WebAssembly.Table.prototype.constructor);
  assertTrue(WebAssembly.Table.prototype.grow instanceof Function);
  assertTrue(WebAssembly.Table.prototype.get instanceof Function);
  assertTrue(WebAssembly.Table.prototype.set instanceof Function);
  let desc = Object.getOwnPropertyDescriptor(WebAssembly.Table.prototype, 'length');
  assertTrue(desc.get instanceof Function);
  assertSame(undefined, desc.set);

  assertThrows(() => new WebAssembly.Table(), TypeError);
  assertThrows(() => new WebAssembly.Table(1), TypeError);
  assertThrows(() => new WebAssembly.Table(""), TypeError);

  assertThrows(() => new WebAssembly.Table({}), TypeError);
  assertThrows(() => new WebAssembly.Table({initial: 10}), TypeError);

  assertThrows(() => new WebAssembly.Table({element: 0, initial: 10}), TypeError);
  assertThrows(() => new WebAssembly.Table({element: "any", initial: 10}), TypeError);

  assertThrows(() => new WebAssembly.Table(
    {element: "anyfunc", initial: -1}), TypeError);
  assertThrows(() => new WebAssembly.Table(
    {element: "anyfunc", initial: outOfUint32RangeValue}), TypeError);

  assertThrows(() => new WebAssembly.Table(
    {element: "anyfunc", initial: 10, maximum: -1}), TypeError);
  assertThrows(() => new WebAssembly.Table(
    {element: "anyfunc", initial: 10, maximum: outOfUint32RangeValue}), TypeError);
  assertThrows(() => new WebAssembly.Table(
    {element: "anyfunc", initial: 10, maximum: 9}), RangeError);

  let table;
  table = new WebAssembly.Table({element: "anyfunc", initial: 1});
  assertTableIsValid(table, 1);
  assertEquals(null, table.get(0));
  assertEquals(undefined, table[0]);

  table = new WebAssembly.Table({element: "anyfunc", initial: "2"});
  assertTableIsValid(table, 2);
  assertEquals(null, table.get(0));
  assertEquals(null, table.get(1));
  assertEquals(undefined, table[0]);
  assertEquals(undefined, table[1]);

  table = new WebAssembly.Table({element: "anyfunc", initial: {valueOf() { return "1" }}});
  assertTableIsValid(table, 1);
  assertEquals(null, table.get(0));
  assertEquals(undefined, table[0]);

  table = new WebAssembly.Table({element: "anyfunc", initial: 0, maximum: 10});
  assertTableIsValid(table, 0);

  table = new WebAssembly.Table({element: "anyfunc", initial: 0,  maximum: "10"});
  assertTableIsValid(table, 0);

  table = new WebAssembly.Table({element: "anyfunc", initial: 0, maximum: {valueOf() { return "10" }}});
  assertTableIsValid(table, 0);

  table = new WebAssembly.Table({element: "anyfunc", initial: 0, maximum: undefined});
  assertTableIsValid(table, 0);

  table = new WebAssembly.Table({element: "anyfunc", initial: 0, maximum: 1000000});
  assertTableIsValid(table, 0);

  table = new WebAssembly.Table({element: "anyfunc", initial: 0, maximum: kV8MaxWasmTableSize});
  assertTableIsValid(table, 0);

  table = new WebAssembly.Table({element: "anyfunc", initial: 0, maximum: kV8MaxWasmTableSize + 1});
  assertTableIsValid(table, 0);

  assertThrows(
    () => new WebAssembly.Table(
      {element: "anyfunc", initial: kV8MaxWasmTableSize + 1}),
    RangeError, /above the upper bound/);
})();

(function TestMaximumIsReadOnce() {
  var a = true;
  var desc = {element: "anyfunc", initial: 10};
  Object.defineProperty(desc, 'maximum', {get: function() {
    if (a) {
      a = false;
      return 16;
    }
    else {
      // Change the return value on the second call so it throws.
      return -1;
    }
  }});
  let table = new WebAssembly.Table(desc);
  assertTableIsValid(table, 10);
})();

(function TestMaximumDoesHasProperty() {
  var hasPropertyWasCalled = false;
  var desc = {element: "anyfunc", initial: 10};
  var proxy = new Proxy({maximum: 16}, {
    has: function(target, name) { hasPropertyWasCalled = true; }
  });
  Object.setPrototypeOf(desc, proxy);
  let table = new WebAssembly.Table(desc);
  assertTableIsValid(table, 10);
})();

(function TestLength() {
  for (let i = 0; i < 10; ++i) {
    let table = new WebAssembly.Table({element: "anyfunc", initial: i});
    assertEquals(i, table.length);
  }

  assertThrows(() => WebAssembly.Table.prototype.length.call([]), TypeError);
})();

(function TestGet() {
  let table = new WebAssembly.Table({element: "anyfunc", initial: 10});

  for (let i = 0; i < table.length; ++i) {
    assertEquals(null, table.get(i));
    assertEquals(null, table.get(String(i)));
  }
  for (let key of [0.4, "", []]) {
    assertEquals(null, table.get(key));
  }
  for (let key of [-1, NaN, {}, () => {}]) {
    assertThrows(() => table.get(key), TypeError);
  }
  for (let key of [table.length, table.length * 10]) {
    assertThrows(() => table.get(key), RangeError);
  }
  assertThrows(() => table.get(Symbol()), TypeError);
  assertThrows(() => WebAssembly.Table.prototype.get.call([], 0), TypeError);
})();

(function TestSet() {
  let builder = new WasmModuleBuilder;
  builder.addExport("host", builder.addImport("test", "f", kSig_v_v));
  builder.addExport("wasm", builder.addFunction("", kSig_v_v).addBody([]));
  let {wasm, host} = builder.instantiate({test: {f() {}}}).exports;

  let table = new WebAssembly.Table({element: "anyfunc", initial: 10});

  for (let f of [wasm, host]) {
    for (let i = 0; i < table.length; ++i) table.set(i, null);
    for (let i = 0; i < table.length; ++i) {
      assertSame(null, table.get(i));
      assertSame(undefined, table.set(i, f));
      assertSame(f, table.get(i));
      assertSame(undefined, table[i]);
    }

    for (let i = 0; i < table.length; ++i) table.set(i, null);
    for (let i = 0; i < table.length; ++i) {
      assertSame(null, table.get(i));
      assertSame(undefined, table.set(String(i), f));
      assertSame(f, table.get(i));
      assertSame(undefined, table[i]);
    }

    for (let key of [0.4, "", []]) {
      assertSame(undefined, table.set(0, null));
      assertSame(undefined, table.set(key, f));
      assertSame(f, table.get(0));
      assertSame(undefined, table[key]);
    }
    for (let key of [NaN, {}, () => {}]) {
      assertSame(undefined, table[key]);
      assertThrows(() => table.set(key, f), TypeError);
    }

    assertThrows(() => table.set(-1, f), TypeError);
    for (let key of [table.length, table.length * 10]) {
      assertThrows(() => table.set(key, f), RangeError);
    }

    for (let val of [undefined, 0, "", {}, [], () => {}]) {
      assertThrows(() => table.set(0, val), TypeError);
    }

    assertThrows(() => table.set(Symbol(), f), TypeError);
    assertThrows(() => WebAssembly.Table.prototype.set.call([], 0, f),
                 TypeError);
  }
})();


(function TestIndexing() {
  let builder = new WasmModuleBuilder;
  builder.addExport("host", builder.addImport("test", "f", kSig_v_v));
  builder.addExport("wasm", builder.addFunction("", kSig_v_v).addBody([]));
  let {wasm, host} = builder.instantiate({test: {f() {}}}).exports;

  let table = new WebAssembly.Table({element: "anyfunc", initial: 10});

  for (let f of [wasm, host, () => {}, 5, {}, ""]) {
    for (let i = 0; i < table.length; ++i) table[i] = f;
    for (let i = 0; i < table.length; ++i) {
      assertSame(null, table.get(i));
      assertSame(f, table[i]);
    }

    for (let key of [NaN, {}, () => {}]) {
      assertSame(f, table[key] = f);
      assertSame(f, table[key]);
      assertThrows(() => table.get(key), TypeError);
    }
    for (let key of [0.4, "", []]) {
      assertSame(f, table[key] = f);
      assertSame(f, table[key]);
      assertSame(null, table.get(key));
    }
  }
})();

(function TestGrow() {
  let builder = new WasmModuleBuilder;
  builder.addExport("host", builder.addImport("test", "f", kSig_v_v));
  builder.addExport("wasm", builder.addFunction("", kSig_v_v).addBody([]));
  let {wasm, host} = builder.instantiate({test: {f() {}}}).exports;

  function init(table) {
    for (let i = 0; i < 5; ++i) table.set(i, wasm);
    for (let i = 15; i < 20; ++i) table.set(i, host);
  }
  function check(table) {
    for (let i = 0; i < 5; ++i) assertSame(wasm, table.get(i));
    for (let i = 6; i < 15; ++i) assertSame(null, table.get(i));
    for (let i = 15; i < 20; ++i) assertSame(host, table.get(i));
    for (let i = 21; i < table.length; ++i) assertSame(null, table.get(i));
  }

  let table = new WebAssembly.Table({element: "anyfunc", initial: 20});
  init(table);
  check(table);
  table.grow(0);
  check(table);
  table.grow(10);
  check(table);
  assertThrows(() => table.grow(-10), TypeError);

  table = new WebAssembly.Table({element: "anyfunc", initial: 20, maximum: 25});
  init(table);
  check(table);
  table.grow(0);
  check(table);
  table.grow(5);
  check(table);
  table.grow(0);
  check(table);
  assertThrows(() => table.grow(1), RangeError);
  assertThrows(() => table.grow(-10), TypeError);

  assertThrows(() => WebAssembly.Table.prototype.grow.call([], 0), TypeError);

  table = new WebAssembly.Table(
    {element: "anyfunc", initial: 0, maximum: kV8MaxWasmTableSize});
  table.grow(kV8MaxWasmTableSize);
  assertThrows(() => table.grow(1), RangeError);

  table = new WebAssembly.Table({element: "anyfunc", initial: 0});
  table.grow({valueOf: () => {table.grow(2); return 1;}});
  assertEquals(3, table.length);
})();

(function TestGrowWithInit() {
  function getDummy(val) {
    let builder = new WasmModuleBuilder();
    builder.addFunction('dummy', kSig_i_v)
        .addBody([kExprI32Const, val])
        .exportAs('dummy');
    return builder.instantiate().exports.dummy;
  }
  let table = new WebAssembly.Table({element: "anyfunc", initial: 1});
  table.grow(5, getDummy(24));
  for (let i = 1; i <= 5; ++i) {
    assertEquals(24, table.get(i)());
  }
})();
